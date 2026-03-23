#ifndef CAPTURE_DEVICE_HPP
#define CAPTURE_DEVICE_HPP

#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <iostream>
#include <cstdint>
#include <sys/mman.h>
#include <vector>
#include <poll.h>
#include <sys/time.h>

class CaptureDevice {
private:
    std::string devicePath;
    int      fd;
    uint32_t width;
    uint32_t height;
    uint32_t pixelFormat;

    struct Buffer {
        void* start;
        size_t length;
        int    export_fd;
    };
    std::vector<Buffer> buffers;

    // These are now private internal steps
    bool openDevice();
    bool configureFormat();
    bool requestBuffers(uint32_t count);
    bool mapAndQueueBuffers(uint32_t count);
    bool exportBuffers();
    bool startStreaming();

public:
    CaptureDevice(const std::string& path, uint32_t w, uint32_t h, uint32_t format);
    ~CaptureDevice();

    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }

    // Single public method for complete setup
    bool initialize(uint32_t count);

    int dequeueBuffer(uint32_t& bytes_used, struct timeval& timestamp);
    bool queueBuffer(int index);

    int getExportFd(size_t index) const;
    int getFd() const;
};

#endif // CAPTURE_DEVICE_HPP