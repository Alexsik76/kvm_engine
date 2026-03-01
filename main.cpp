#include "CaptureDevice.hpp"
#include "EncoderDevice.hpp"
#include <linux/videodev2.h>
#include <sys/time.h>
#include <iostream>
#include <poll.h>
#include <chrono>
#include <cstdio>
#include "Config.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// KVM Engine — streams raw H.264 over TCP with minimum latency.
//
// VLC client command (on Windows host):
//   vlc tcp://pi4.lan:8080 :demux=h264 :network-caching=0 :clock-jitter=0
//        :clock-synchro=0
//
// Why raw H.264 (no container):
//   MPEG-TS adds framing overhead and extra buffering. For KVM use, we
//   prioritise frame delivery speed over perfect clock synchronisation.
//   VLC in :clock-synchro=0 mode does not need a container PCR.
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    // ── Capture device ──────────────────────────────────────────────────────
    CaptureDevice capture(Config::videoNode, Config::width, Config::height, Config::format);

    if (!capture.openDevice())                 return 1;
    if (!capture.configureFormat())            return 1;
    if (!capture.requestBuffers(Config::bufCount))     return 1;
    if (!capture.mapAndQueueBuffers(Config::bufCount)) return 1;
    if (!capture.startStreaming())             return 1;

    std::cerr << "Capture initialised." << std::endl;

    // ── Encoder device ──────────────────────────────────────────────────────
    EncoderDevice encoder(Config::encoderNode);

    if (!encoder.openDevice())                    return 1;
    if (!encoder.configureFormats(Config::width, Config::height)) return 1;
    encoder.configureFrameRate(Config::fps);              // must be before setupH264Controls()
    encoder.setupH264Controls();
    if (!encoder.requestBuffers(Config::bufCount))        return 1;
    if (!encoder.mapCaptureBuffers(Config::bufCount))     return 1;
    if (!capture.exportBuffers())                 return 1;

    if (!encoder.startStreaming()) return 1;

    std::cerr << "Streaming raw H.264 to stdout... Press Ctrl+C to stop." << std::endl;

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
        int ret = poll(fds, 2, 200);
        if (ret < 0) {
            std::cerr << "poll() error in main loop." << std::endl;
            break;
        }

        // 1. Raw frame ready → queue to encoder
        //    We propagate the V4L2 buffer timestamp so the encoder driver
        //    copies it to the CAPTURE buffer.
        if (fds[0].revents & POLLIN) {
            uint32_t       bytes_used = 0;
            struct timeval cap_ts     = {};
            int cap_idx = capture.dequeueBuffer(bytes_used, cap_ts);
            if (cap_idx != -1) {
                int dmabuf_fd = capture.getExportFd(cap_idx);
                encoder.queueOutputBuffer(cap_idx, dmabuf_fd, bytes_used, cap_ts);
            }
        }

        // 2. Recycle the encoder OUTPUT buffer slot back to capture
        {
            int enc_out_idx = encoder.dequeueOutputBuffer();
            if (enc_out_idx != -1) {
                capture.queueBuffer(enc_out_idx);
            }
        }

        // 3. Encoded H.264 frame ready → send raw bytes to client
        if (fds[1].revents & POLLIN) {
            uint32_t       h264_bytes = 0;
            struct timeval enc_ts     = {};
            int enc_cap_idx = encoder.dequeueCaptureBuffer(h264_bytes, enc_ts);
            if (enc_cap_idx != -1) {
                void* frame_data = encoder.getCaptureBufferPointer(enc_cap_idx);
                if (frame_data && h264_bytes > 0) {
                    std::fwrite(frame_data, 1, h264_bytes, stdout);
                    std::fflush(stdout);
                }
                encoder.queueCaptureBuffer(enc_cap_idx);
            }
        }
    }

    return 0;
}