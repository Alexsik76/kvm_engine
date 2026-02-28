#include "CaptureDevice.hpp"
#include "EncoderDevice.hpp"
#include "TcpServer.hpp"
#include <linux/videodev2.h>
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

    if (!capture.openDevice())        return 1;
    if (!capture.configureFormat())   return 1;
    if (!capture.requestBuffers(bufCount))        return 1;
    if (!capture.mapAndQueueBuffers(bufCount))     return 1;
    if (!capture.startStreaming())    return 1;

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

    std::cout << "\nStreaming H.264 over TCP... Press Ctrl+C to stop." << std::endl;

    // ── Main loop ───────────────────────────────────────────────────────────
    // We poll both file descriptors simultaneously so the loop sleeps until
    // there is real work to do, instead of spinning with usleep().
    //
    //  fds[0] = capture fd  — POLLIN  means a raw frame is ready to dequeue
    //  fds[1] = encoder fd  — POLLIN  means an encoded frame is ready
    //                       — POLLOUT means an output slot is free (not used
    //                         explicitly here, but useful for back-pressure)
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
        // ret == 0 → timeout, just continue (keeps the loop alive without spinning)

        // 1. Dequeue a raw frame from the capture device and hand it to the encoder
        if (fds[0].revents & POLLIN) {
            uint32_t bytes_used = 0;
            int cap_idx = capture.dequeueBuffer(bytes_used);
            if (cap_idx != -1) {
                int dmabuf_fd = capture.getExportFd(cap_idx);
                encoder.queueOutputBuffer(cap_idx, dmabuf_fd, bytes_used);
            }
        }

        // 2. Recycle the encoder's output (raw-input) buffer back to capture
        {
            int enc_out_idx = encoder.dequeueOutputBuffer();
            if (enc_out_idx != -1) {
                capture.queueBuffer(enc_out_idx);
            }
        }

        // 3. Dequeue an encoded H.264 frame and send it over TCP
        if (fds[1].revents & POLLIN) {
            uint32_t h264_bytes = 0;
            int enc_cap_idx = encoder.dequeueCaptureBuffer(h264_bytes);
            if (enc_cap_idx != -1) {
                void* frame_data = encoder.getCaptureBufferPointer(enc_cap_idx);
                if (frame_data && h264_bytes > 0) {
                    if (!server.sendData(frame_data, h264_bytes)) {
                        std::cout << "\nClient disconnected. Waiting for reconnect..." << std::endl;
                        // Re-queue the capture buffer before blocking on accept
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