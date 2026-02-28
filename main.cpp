#include "CaptureDevice.hpp"
#include "EncoderDevice.hpp"
#include "TcpServer.hpp"
#include <linux/videodev2.h>
#include <sys/time.h>
#include <iostream>
#include <poll.h>
#include <chrono>

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
    const std::string videoNode   = "/dev/video0";
    const uint32_t    width       = 1280;
    const uint32_t    height      = 720;
    const uint32_t    format      = V4L2_PIX_FMT_UYVY;
    const std::string encoderNode = "/dev/video11";
    // fps: set to match your HDMI source (ffplay reported 25 fps previously).
    // If source is 30fps-native, change to 30. The encoder uses this value
    // to build the H.264 SPS VUI timing — wrong fps causes display flicker.
    const uint32_t    fps         = 25;
    // 3 buffers: one frame margin over the 2-buffer minimum.
    // Prevents encoder starvation if TCP sendData() blocks for >1 frame period.
    const uint32_t    bufCount    = 3;

    // ── Capture device ──────────────────────────────────────────────────────
    CaptureDevice capture(videoNode, width, height, format);

    if (!capture.openDevice())                 return 1;
    if (!capture.configureFormat())            return 1;
    capture.configureFrameRate(fps);           // set SPS timing before encoder reads it
    if (!capture.requestBuffers(bufCount))     return 1;
    if (!capture.mapAndQueueBuffers(bufCount)) return 1;
    if (!capture.startStreaming())             return 1;

    std::cout << "Capture initialised." << std::endl;

    // ── Encoder device ──────────────────────────────────────────────────────
    EncoderDevice encoder(encoderNode);

    if (!encoder.openDevice())                    return 1;
    if (!encoder.configureFormats(width, height)) return 1;
    encoder.configureFrameRate(fps);              // must be before setupH264Controls()
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
    uint64_t total_bytes  = 0;
    auto     t_start      = std::chrono::steady_clock::now();

    std::cout << "Streaming raw H.264 over TCP... Press Ctrl+C to stop." << std::endl;

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
        //    copies it to the CAPTURE buffer (useful for future timestamping).
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
                    // Send raw H.264 Annex-B bytes directly — no container overhead
                    if (!server.sendData(frame_data, h264_bytes)) {
                        std::cout << "\nClient disconnected. Waiting for reconnect..." << std::endl;
                        encoder.queueCaptureBuffer(enc_cap_idx);
                        // Reset bitrate counters for the new session
                        total_bytes = 0;
                        t_start     = std::chrono::steady_clock::now();
                        if (!server.waitForNextClient()) {
                            std::cerr << "Failed to accept next client. Stopping." << std::endl;
                            break;
                        }
                        continue;
                    }
                    // Bitrate measurement — runs on every successfully sent frame
                    total_bytes += h264_bytes;
                    auto now = std::chrono::steady_clock::now();
                    auto sec = std::chrono::duration_cast<std::chrono::seconds>(
                                   now - t_start).count();
                    if (sec > 0 && sec % 5 == 0) {
                        std::cout << "Bitrate: "
                                  << (total_bytes * 8 / sec / 1000)
                                  << " kbps\n";
                    }
                }
                encoder.queueCaptureBuffer(enc_cap_idx);
            }
        }
    }

    return 0;
}