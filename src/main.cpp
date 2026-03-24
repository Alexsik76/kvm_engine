#include "CaptureDevice.hpp"
#include "EncoderDevice.hpp"
#include "Config.hpp"
#include <iostream>
#include <poll.h>
#include <cstdio>
#include <unistd.h>
#include <csignal>
#include <atomic>
#include <filesystem>

std::atomic<bool> keepRunning(true);

void signalHandler(int signum) {
    std::cerr << "\nInterrupt signal (" << signum << ") received." << std::endl;
    keepRunning = false;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Get executable directory for config path
    std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe").parent_path();
    std::string configPath = (exePath / "config" / "config.json").string();
    
    // Also check current directory as fallback
    if (!std::filesystem::exists(configPath)) {
        configPath = "./config/config.json";
    }

    try {
        Config::Loader::load(configPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    CaptureDevice capture(Config::data.video.device, Config::data.video.width, Config::data.video.height, Config::data.video.getV4L2Format());
    if (!capture.initialize(Config::data.buffers.count)) {
        std::cerr << "Fatal error: Failed to initialize capture device." << std::endl;
        return 1;
    }
    std::cerr << "Capture initialised." << std::endl;

    EncoderDevice encoder(Config::data.encoder.device);
    if (!encoder.initialize(capture.getWidth(), capture.getHeight(), Config::data.video.fps, Config::data.buffers.count)) {
        std::cerr << "Fatal error: Failed to initialize encoder device." << std::endl;
        return 1;
    }
    std::cerr << "Streaming raw H.264 to stdout... Press Ctrl+C to stop." << std::endl;

    struct pollfd fds[2];
    fds[0].fd     = capture.getFd();
    fds[0].events = POLLIN;
    fds[1].fd     = encoder.getFd();
    fds[1].events = POLLIN;

    try {
        while (keepRunning) {
            int ret = poll(fds, 2, 1000); // 1s timeout for lower CPU when idle
            
            if (ret < 0) {
                if (errno == EINTR) continue; 
                std::cerr << "poll() error in main loop." << std::endl;
                break;
            }

            if (ret == 0) {
                // Keep the process alive even if there's no signal.
                // This prevents MediaMTX from dropping the stream source.
                continue;
            }

            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                std::cerr << "Capture device disconnected or fatal hardware error. Exiting." << std::endl;
                break;
            }

            if (fds[0].revents & POLLIN) {
                uint32_t       bytes_used = 0;
                struct timeval cap_ts     = {};
                int cap_idx = capture.dequeueBuffer(bytes_used, cap_ts);
                if (cap_idx != -1) {
                    int dmabuf_fd = capture.getExportFd(cap_idx);
                    encoder.queueOutputBuffer(cap_idx, dmabuf_fd, bytes_used, cap_ts);
                }
            }

            // Internal feedback loop
            int enc_out_idx = encoder.dequeueOutputBuffer();
            if (enc_out_idx != -1) {
                capture.queueBuffer(enc_out_idx);
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
