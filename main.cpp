#include "CaptureDevice.hpp"
#include "EncoderDevice.hpp"
#include "MpegTsMuxer.hpp"
#include "TcpServer.hpp"
#include <linux/videodev2.h>
#include <sys/time.h>
#include <iostream>
#include <poll.h>

int main() {
    const std::string videoNode   = "/dev/video0";
    const uint32_t    width       = 1280;
    const uint32_t    height      = 720;
    const uint32_t    format      = V4L2_PIX_FMT_UYVY;
    const std::string encoderNode = "/dev/video11";
    const uint32_t    bufCount    = 4;

    // ── Capture device ──────────────────────────────────────────────────────
    CaptureDevice capture(videoNode, width, height, format);

    if (!capture.openDevice())                return 1;
    if (!capture.configureFormat())           return 1;
    if (!capture.requestBuffers(bufCount))    return 1;
    if (!capture.mapAndQueueBuffers(bufCount)) return 1;
    if (!capture.startStreaming())            return 1;

    std::cout << "Capture initialised. Ready for buffers!" << std::endl;

    // ── Encoder device ──────────────────────────────────────────────────────
    EncoderDevice encoder(encoderNode);

    if (!encoder.openDevice())                    return 1;
    if (!encoder.configureFormats(width, height)) return 1;
    encoder.setupH264Controls();
    if (!encoder.requestBuffers(bufCount))        return 1;
    if (!encoder.mapCaptureBuffers(bufCount))     return 1;
    if (!capture.exportBuffers())                 return 1;

    // ── TCP server ──────────────────────────────────────────────────────────
    TcpServer server(8080);
    if (!server.start()) {
        std::cerr << "Failed to start TCP server." << std::endl;
        return 1;
    }

    server.waitForClient();

    if (!encoder.startStreaming()) return 1;

    // ── MPEG-TS muxer ───────────────────────────────────────────────────────
    // Wraps each encoded H.264 frame in MPEG-TS packets with PAT, PMT, PES
    // headers and a PCR/PTS derived from the V4L2 capture timestamp.
    // This is what fixes VLC's "no reference clock" / "Timestamp conversion
    // failed" errors — without a container clock, VLC cannot schedule frames.
    MpegTsMuxer muxer;

    std::cout << "\nStreaming MPEG-TS (H.264) over TCP... Press Ctrl+C to stop." << std::endl;

    // ── Main loop ───────────────────────────────────────────────────────────
    // poll() on both fds simultaneously — zero CPU waste when idle.
    //   fds[0] = capture fd  — POLLIN: a raw frame is ready to dequeue
    //   fds[1] = encoder fd  — POLLIN: an encoded frame is ready
    struct pollfd fds[2];
    fds[0].fd     = capture.getFd();
    fds[0].events = POLLIN;
    fds[1].fd     = encoder.getFd();
    fds[1].events = POLLIN;

    while (true) {
        // Block up to 200 ms waiting for any event on either fd
        int ret = poll(fds, 2, 200);
        if (ret < 0) {
            std::cerr << "poll() error in main loop." << std::endl;
            break;
        }
        // ret == 0 → timeout, just continue to keep the loop alive

        // 1. Dequeue a raw frame from capture and hand it to the encoder,
        //    passing along the V4L2 buffer timestamp so the driver can
        //    propagate it to the encoded CAPTURE buffer.
        if (fds[0].revents & POLLIN) {
            uint32_t     bytes_used = 0;
            struct timeval cap_ts  = {};
            int cap_idx = capture.dequeueBuffer(bytes_used, cap_ts);
            if (cap_idx != -1) {
                int dmabuf_fd = capture.getExportFd(cap_idx);
                encoder.queueOutputBuffer(cap_idx, dmabuf_fd, bytes_used, cap_ts);
            }
        }

        // 2. Recycle the encoder's OUTPUT (raw-input) buffer back to capture
        {
            int enc_out_idx = encoder.dequeueOutputBuffer();
            if (enc_out_idx != -1) {
                capture.queueBuffer(enc_out_idx);
            }
        }

        // 3. Dequeue an encoded H.264 frame, mux into MPEG-TS, send over TCP
        if (fds[1].revents & POLLIN) {
            uint32_t       h264_bytes = 0;
            struct timeval enc_ts    = {};
            int enc_cap_idx = encoder.dequeueCaptureBuffer(h264_bytes, enc_ts);
            if (enc_cap_idx != -1) {
                void* frame_data = encoder.getCaptureBufferPointer(enc_cap_idx);
                if (frame_data && h264_bytes > 0) {
                    // Convert the propagated capture timestamp to 90 kHz PTS
                    uint64_t pts = MpegTsMuxer::timevalToPts(enc_ts);
                    // Wrap H.264 AU in MPEG-TS packets with PAT+PMT+PCR+PES
                    auto ts_packets = muxer.muxFrame(
                        static_cast<const uint8_t*>(frame_data), h264_bytes, pts);

                    if (!server.sendData(ts_packets.data(), ts_packets.size())) {
                        std::cout << "\nClient disconnected. Waiting for reconnect..." << std::endl;
                        encoder.queueCaptureBuffer(enc_cap_idx);
                        if (!server.waitForNextClient()) {
                            std::cerr << "Failed to accept next client. Stopping." << std::endl;
                            break;
                        }
                        continue;
                    }
                }
                encoder.queueCaptureBuffer(enc_cap_idx);
            }
        }
    }

    return 0;
}