#ifndef ENCODER_DEVICE_HPP
#define ENCODER_DEVICE_HPP

#include <string>
#include <cstdint>
#include <cerrno>
#include <sys/mman.h>
#include <sys/time.h>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <iostream>

class EncoderDevice {
private:
    std::string devicePath;
    int fd;
    struct Buffer {
        void* start;
        size_t length;
    };
    std::vector<Buffer> capture_buffers;
    std::vector<size_t> output_buffer_lengths;

    // These are now private internal steps
    bool openDevice();
    bool configureFormats(uint32_t width, uint32_t height);
    bool configureFrameRate(uint32_t fps);
    bool setupH264Controls();
    bool requestBuffers(uint32_t count);
    bool mapCaptureBuffers(uint32_t count);
    bool startStreaming();

public:
    EncoderDevice(const std::string& path);
    ~EncoderDevice();

    // Single public method for complete setup
    bool initialize(uint32_t width, uint32_t height, uint32_t fps, uint32_t count);

    bool queueOutputBuffer(int index, int dmabuf_fd, uint32_t bytesused,
                           const struct timeval& timestamp);
    int dequeueOutputBuffer();
    int dequeueCaptureBuffer(uint32_t& bytes_used, struct timeval& timestamp);
    bool queueCaptureBuffer(int index);

    void* getCaptureBufferPointer(int index) const;
    int getFd() const;
};

#endif // ENCODER_DEVICE_HPP