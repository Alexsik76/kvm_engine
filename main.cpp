#include "CaptureDevice.hpp"
#include "EncoderDevice.hpp"
#include "TcpServer.hpp"
#include <linux/videodev2.h>
#include <iostream>

int main() {
    std::string videoNode = "/dev/video0";
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t format = V4L2_PIX_FMT_UYVY;

    CaptureDevice capture(videoNode, width, height, format);

    if (!capture.openDevice()) {
        return 1;
    }

    if (!capture.configureFormat()) {
        return 1;
    }

    if (!capture.requestBuffers(4)) {
        return 1;
    }
    if (!capture.mapAndQueueBuffers(4)) {
        return 1;
    }

    if (!capture.startStreaming()) {
        return 1;
    }

    std::cout << "Initialization successful. Ready for buffers!" << std::endl;
    std::string encoderNode = "/dev/video11";
    EncoderDevice encoder(encoderNode);

    if (!encoder.openDevice()) {
        return 1;
    }

    if (!encoder.configureFormats(width, height)) {
        return 1;
    }
    encoder.setupH264Controls();

    if (!encoder.requestBuffers(4)) {
        return 1;
    }
    if (!encoder.mapCaptureBuffers(4)) {
        return 1;
    }
    if (!capture.exportBuffers()) {
        return 1;
    }

    TcpServer server(8080);
    if (!server.start()) {
        std::cerr << "Failed to start TCP server." << std::endl;
        return 1;
    }

    server.waitForClient();

    if (!encoder.startStreaming()) {
        return 1;
    }

    std::cout << "\nStreaming H.264 over TCP... Press Ctrl+C to stop." << std::endl;
    
    while (true) {
        uint32_t bytes_used = 0;
        bool worked = false;
        int cap_idx = capture.dequeueBuffer(bytes_used);
        if (cap_idx != -1) {
            int dmabuf_fd = capture.getExportFd(cap_idx);
            encoder.queueOutputBuffer(cap_idx, dmabuf_fd, bytes_used);
            worked = true;
        }

        int enc_out_idx = encoder.dequeueOutputBuffer();
        if (enc_out_idx != -1) {
            capture.queueBuffer(enc_out_idx);
            worked = true;
        }

        uint32_t h264_bytes = 0;
        int enc_cap_idx = encoder.dequeueCaptureBuffer(h264_bytes);
        if (enc_cap_idx != -1) {
            void* frame_data = encoder.getCaptureBufferPointer(enc_cap_idx);
            if (frame_data && h264_bytes > 0) {
                if (!server.sendData(frame_data, h264_bytes)) {
                    std::cout << "\nClient disconnected. Stopping." << std::endl;
                    break; 
                }
            }
            encoder.queueCaptureBuffer(enc_cap_idx);
            worked = true;
        }
        if (!worked) {
            usleep(1000); 
    }
    }

    return 0;
}