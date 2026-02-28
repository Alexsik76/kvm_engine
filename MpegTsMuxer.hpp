#ifndef MPEG_TS_MUXER_HPP
#define MPEG_TS_MUXER_HPP

#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <sys/time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Minimal MPEG-TS muxer for raw H.264 → MPEG-TS over TCP.
//
// Why this is needed:
//   Raw H.264 over TCP has no container, so VLC has no reference clock and
//   cannot anchor PTS values → "Timestamp conversion failed / no reference
//   clock" errors and cascading late-frame warnings.
//   MPEG-TS carries a PCR (Program Clock Reference) in every few packets,
//   which VLC uses to synchronise playback timing exactly.
//
// Structure of the output stream:
//   PAT (PID 0x0000) ──────── program/PMT map, sent every 15 frames
//   PMT (PID 0x0100) ──────── stream type map  (H.264 = 0x1B)
//   Video PES (PID 0x0101) ── H.264 payload, with PCR and PTS per frame
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint16_t TS_PID_PAT   = 0x0000;
static constexpr uint16_t TS_PID_PMT   = 0x0100;
static constexpr uint16_t TS_PID_VIDEO = 0x0101;
static constexpr uint16_t TS_PROGRAM   = 1;
static constexpr uint8_t  TS_STREAM_TYPE_H264 = 0x1B;
static constexpr size_t   TS_PACKET_SIZE = 188;

class MpegTsMuxer {
private:
    uint8_t  cc_pat   = 0;   // continuity counter for PAT PID
    uint8_t  cc_pmt   = 0;   // continuity counter for PMT PID
    uint8_t  cc_video = 0;   // continuity counter for video PID
    uint32_t frame_count = 0;

    // ── CRC-32/MPEG-2 (used for PAT / PMT sections) ─────────────────────────
    static uint32_t crc32_mpeg(const uint8_t* data, size_t len) {
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= (uint32_t)data[i] << 24;
            for (int b = 0; b < 8; ++b)
                crc = (crc & 0x80000000) ? (crc << 1) ^ 0x04C11DB7 : (crc << 1);
        }
        return crc;
    }

    // ── TS header helpers ────────────────────────────────────────────────────

    // Plain header (no adaptation field)
    static void writeHeader(uint8_t* pkt, uint16_t pid, bool pusi, uint8_t& cc) {
        pkt[0] = 0x47;
        pkt[1] = (pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F);
        pkt[2] =  pid & 0xFF;
        pkt[3] = 0x10 | (cc++ & 0x0F);  // payload_only
    }

    // Header + adaptation field carrying PCR (occupies 12 bytes total)
    static void writeHeaderWithPCR(uint8_t* pkt, uint16_t pid, bool pusi,
                                   uint8_t& cc, uint64_t pts_90k) {
        pkt[0] = 0x47;
        pkt[1] = (pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F);
        pkt[2] =  pid & 0xFF;
        pkt[3] = 0x30 | (cc++ & 0x0F);  // adaptation_field + payload

        // Adaptation field: length=7 ( flags(1) + PCR(6) )
        pkt[4] = 7;     // adaptation_field_length
        pkt[5] = 0x10;  // PCR_flag=1, rest 0

        // PCR = pts_90k * 300 in 27 MHz domain
        const uint64_t pcr_base = pts_90k;          // in 90 kHz (33 bits)
        const uint64_t pcr_ext  = 0;                // extension (9 bits)
        pkt[6]  = (pcr_base >> 25) & 0xFF;
        pkt[7]  = (pcr_base >> 17) & 0xFF;
        pkt[8]  = (pcr_base >>  9) & 0xFF;
        pkt[9]  = (pcr_base >>  1) & 0xFF;
        pkt[10] = ((pcr_base & 0x01) << 7) | 0x7E | ((pcr_ext >> 8) & 0x01);
        pkt[11] = pcr_ext & 0xFF;
        // bytes 12..187 = payload
    }

    // ── PAT (Program Association Table) ─────────────────────────────────────
    void buildPAT(uint8_t* out) {
        memset(out, 0xFF, TS_PACKET_SIZE);
        writeHeader(out, TS_PID_PAT, /*pusi=*/true, cc_pat);
        out[4] = 0x00; // pointer_field

        uint8_t* p = out + 5;
        p[0] = 0x00;  // table_id = PAT
        // section_syntax_indicator=1, '0'=0, reserved=11, section_length
        // section_length = 13  (2 ts_id + 1 ver + 1 sec_num + 1 last_sec + 4 prog_entry + 4 crc)
        p[1] = 0xB0;
        p[2] = 13;
        p[3] = 0x00; p[4] = 0x01; // transport_stream_id = 1
        p[5] = 0xC1;               // version=0, current_next=1
        p[6] = 0x00;               // section_number
        p[7] = 0x00;               // last_section_number
        // Program entry
        p[8]  = (TS_PROGRAM >> 8) & 0xFF;
        p[9]  =  TS_PROGRAM & 0xFF;
        p[10] = 0xE0 | ((TS_PID_PMT >> 8) & 0x1F);
        p[11] =  TS_PID_PMT & 0xFF;
        // CRC32 covers from table_id through last program entry byte
        uint32_t crc = crc32_mpeg(p, 12);
        p[12] = (crc >> 24) & 0xFF;
        p[13] = (crc >> 16) & 0xFF;
        p[14] = (crc >>  8) & 0xFF;
        p[15] =  crc & 0xFF;
    }

    // ── PMT (Program Map Table) ──────────────────────────────────────────────
    void buildPMT(uint8_t* out) {
        memset(out, 0xFF, TS_PACKET_SIZE);
        writeHeader(out, TS_PID_PMT, /*pusi=*/true, cc_pmt);
        out[4] = 0x00; // pointer_field

        uint8_t* p = out + 5;
        p[0] = 0x02;  // table_id = PMT
        // section_length = 18
        p[1] = 0xB0;
        p[2] = 18;
        p[3] = (TS_PROGRAM >> 8) & 0xFF;
        p[4] =  TS_PROGRAM & 0xFF;
        p[5] = 0xC1;  // version=0, current_next=1
        p[6] = 0x00;  // section_number
        p[7] = 0x00;  // last_section_number
        // PCR_PID = video PID
        p[8]  = 0xE0 | ((TS_PID_VIDEO >> 8) & 0x1F);
        p[9]  =  TS_PID_VIDEO & 0xFF;
        // program_info_length = 0
        p[10] = 0xF0;
        p[11] = 0x00;
        // ES stream: H.264 on video PID
        p[12] = TS_STREAM_TYPE_H264;
        p[13] = 0xE0 | ((TS_PID_VIDEO >> 8) & 0x1F);
        p[14] =  TS_PID_VIDEO & 0xFF;
        // ES_info_length = 0
        p[15] = 0xF0;
        p[16] = 0x00;
        // CRC32
        uint32_t crc = crc32_mpeg(p, 17);
        p[17] = (crc >> 24) & 0xFF;
        p[18] = (crc >> 16) & 0xFF;
        p[19] = (crc >>  8) & 0xFF;
        p[20] =  crc & 0xFF;
    }

    // ── PTS encoding (5 bytes, PTS-only, prefix = 0x21) ─────────────────────
    static void writePTS(uint8_t* buf, uint64_t pts) {
        // PTS-only marker: '0010' in upper nibble, marker bits at positions 0
        buf[0] = 0x21 | (((pts >> 30) & 0x07) << 1);  // [32..30]
        buf[1] =         (pts >> 22) & 0xFF;           // [29..22]
        buf[2] = 0x01 | (((pts >> 15) & 0x7F) << 1);  // [21..15]
        buf[3] =         (pts >>  7) & 0xFF;           // [14..7]
        buf[4] = 0x01 | ((pts & 0x7F) << 1);          // [6..0]
    }

public:
    // ── Public API ───────────────────────────────────────────────────────────

    // Convert a V4L2 struct timeval (wall-clock capture time) to a 90 kHz PTS.
    static uint64_t timevalToPts(const struct timeval& tv) {
        return (uint64_t)tv.tv_sec * 90000ULL
             + (uint64_t)tv.tv_usec * 9ULL / 100ULL;
    }

    // Mux one H.264 NAL unit (or AU) into a sequence of 188-byte MPEG-TS packets.
    // PAT+PMT are prepended every 15 frames so VLC can re-sync after connection.
    std::vector<uint8_t> muxFrame(const uint8_t* h264, size_t h264_size,
                                   uint64_t pts_90k) {
        std::vector<uint8_t> out;
        out.reserve(((h264_size / 176) + 4) * TS_PACKET_SIZE);

        // PAT + PMT every 15 frames (≈ 0.5 s at 30 fps)
        if (frame_count % 15 == 0) {
            uint8_t pat[TS_PACKET_SIZE], pmt[TS_PACKET_SIZE];
            buildPAT(pat);
            buildPMT(pmt);
            out.insert(out.end(), pat, pat + TS_PACKET_SIZE);
            out.insert(out.end(), pmt, pmt + TS_PACKET_SIZE);
        }
        ++frame_count;

        // ── Build PES header ─────────────────────────────────────────────────
        // Format: start_code(3) + stream_id(1) + pkt_len(2) + flags(2) +
        //         header_data_length(1) + PTS(5)  = 14 bytes total
        uint8_t pes_hdr[14];
        pes_hdr[0] = 0x00; pes_hdr[1] = 0x00; pes_hdr[2] = 0x01;
        pes_hdr[3] = 0xE0;   // stream_id: video
        // PES packet length = 0 (unbounded — standard practice for video)
        pes_hdr[4] = 0x00; pes_hdr[5] = 0x00;
        pes_hdr[6] = 0x80;   // marker bits
        pes_hdr[7] = 0x80;   // PTS_DTS_flags: PTS present only
        pes_hdr[8] = 0x05;   // header_data_length = 5 (just PTS)
        writePTS(&pes_hdr[9], pts_90k);
        // ─────────────────────────────────────────────────────────────────────

        const uint8_t* pes_ptr  = pes_hdr;
        size_t         pes_rem  = sizeof(pes_hdr);
        const uint8_t* pay_ptr  = h264;
        size_t         pay_rem  = h264_size;
        bool           first_pkt = true;

        while (pes_rem > 0 || pay_rem > 0) {
            uint8_t pkt[TS_PACKET_SIZE];
            memset(pkt, 0xFF, TS_PACKET_SIZE);

            size_t payload_start;
            if (first_pkt) {
                // First TS packet of this PES: include PCR in adaptation field
                writeHeaderWithPCR(pkt, TS_PID_VIDEO, /*pusi=*/true,
                                   cc_video, pts_90k);
                payload_start = 12; // 4 header + 8 adaptation field
                first_pkt = false;
            } else {
                writeHeader(pkt, TS_PID_VIDEO, /*pusi=*/false, cc_video);
                payload_start = 4;
            }

            uint8_t* dst   = pkt + payload_start;
            size_t   space = TS_PACKET_SIZE - payload_start;

            // Fill with PES header first (only relevant for the first packet)
            if (pes_rem > 0) {
                size_t n = std::min(pes_rem, space);
                memcpy(dst, pes_ptr, n);
                dst += n; space -= n; pes_ptr += n; pes_rem -= n;
            }
            // Then fill with H.264 payload
            if (space > 0 && pay_rem > 0) {
                size_t n = std::min(pay_rem, space);
                memcpy(dst, pay_ptr, n);
                pay_ptr += n; pay_rem -= n;
                // Remaining bytes stay 0xFF (legal stuffing in MPEG-TS)
            }

            out.insert(out.end(), pkt, pkt + TS_PACKET_SIZE);
        }

        return out;
    }
};

#endif // MPEG_TS_MUXER_HPP
