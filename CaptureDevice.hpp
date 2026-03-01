#ifndef CAPTURE_DEVICE_HPP
// NOTE: sys/time.h is included for struct timeval (V4L2 buffer timestamps)
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

class CaptureDevice {
private:
    std::string devicePath;
    int      fd;
    uint32_t width;
    uint32_t height;
    uint32_t pixelFormat;

    struct Buffer {
        void*  start;
        size_t length;
        int    export_fd;
    };
    std::vector<Buffer> buffers;

public:
    CaptureDevice(const std::string& path, uint32_t w, uint32_t h, uint32_t format);
    ~CaptureDevice();

    bool openDevice();
    bool configureFormat();
    bool requestBuffers(uint32_t count);
    bool mapAndQueueBuffers(uint32_t count);
    bool startStreaming();

    // Returns the buffer index, or -1 on timeout/error.
    // bytes_used: number of valid bytes in the buffer.
    // timestamp:  wall-clock time the frame was captured (from V4L2 driver).
    //             Can be used for latency measurement or future container muxing.
    int dequeueBuffer(uint32_t& bytes_used, struct timeval& timestamp);
    bool queueBuffer(int index);

    bool exportBuffers();

    int getExportFd(size_t index) const;
    int getFd() const;
};

#endif // CAPTURE_DEVICE_HPP