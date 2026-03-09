#include "CaptureDevice.hpp"
#include "EncoderDevice.hpp"
#include "Config.hpp"
#include <iostream>
#include <poll.h>
#include <cstdio>
#include <unistd.h>
#include <csignal>
#include <atomic>

std::atomic<bool> keepRunning(true);

void signalHandler(int signum) {
    std::cerr << "\nInterrupt signal (" << signum << ") received." << std::endl;
    keepRunning = false;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    CaptureDevice capture(Config::videoNode, Config::width, Config::height, Config::format);
    if (!capture.initialize(Config::bufCount)) {
        std::cerr << "Fatal error: Failed to initialize capture device." << std::endl;
        return 1;
    }
    std::cerr << "Capture initialised." << std::endl;

    EncoderDevice encoder(Config::encoderNode);
    if (!encoder.initialize(Config::width, Config::height, Config::fps, Config::bufCount)) {
        std::cerr << "Fatal error: Failed to initialize encoder device." << std::endl;
        return 1;
    }
    std::cerr << "Streaming raw H.264 to stdout... Press Ctrl+C to stop." << std::endl;

    struct pollfd fds[2];
    fds[0].fd     = capture.getFd();
    fds[0].events = POLLIN;
    fds[1].fd     = encoder.getFd();
    fds[1].events = POLLIN;

    int timeout_counter = 0;
    const int MAX_TIMEOUTS = 10; 

    try {
        while (keepRunning) {
            int ret = poll(fds, 2, 200);
            
            if (ret < 0) {
                if (errno == EINTR) continue; 
                std::cerr << "poll() error in main loop." << std::endl;
                break;
            }

            if (ret == 0) {
                timeout_counter++;
                if (timeout_counter >= MAX_TIMEOUTS) {
                    std::cerr << "Signal lost (consecutive timeouts). Exiting to orchestrator." << std::endl;
                    break;
                }
                continue;
            }

            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                std::cerr << "Capture device disconnected or fatal hardware error. Exiting." << std::endl;
                break;
            }

            timeout_counter = 0;

            if (fds[0].revents & POLLIN) {
                uint32_t       bytes_used = 0;
                struct timeval cap_ts     = {};
                int cap_idx = capture.dequeueBuffer(bytes_used, cap_ts);
                if (cap_idx != -1) {
                    int dmabuf_fd = capture.getExportFd(cap_idx);
                    encoder.queueOutputBuffer(cap_idx, dmabuf_fd, bytes_used, cap_ts);
                }
            }

            {
                int enc_out_idx = encoder.dequeueOutputBuffer();
                if (enc_out_idx != -1) {
                    capture.queueBuffer(enc_out_idx);
                }
            }

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
    } catch (const std::exception& e) {
        std::cerr << "Runtime Exception: " << e.what() << std::endl;
        return 1;
    }

    std::cerr << "KVM Engine stopped gracefully." << std::endl;
    return 0;
}