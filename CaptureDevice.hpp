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

class CaptureDevice {
private:
    std::string devicePath;
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t pixelFormat;
    struct Buffer {
        void* start;
        size_t length;
        int export_fd;
    };
    std::vector<Buffer> buffers;

public:
    CaptureDevice(const std::string& path, uint32_t w, uint32_t h, uint32_t format)
        : devicePath(path), fd(-1), width(w), height(h), pixelFormat(format) {}

    ~CaptureDevice() {
        if (fd != -1) {
            for (auto& buf : buffers) {
            munmap(buf.start, buf.length);
        }
            close(fd);
            std::cout << "Device " << devicePath << " closed." << std::endl;
        }
    }

    bool openDevice() {
        fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK, 0);
        if (fd == -1) {
            std::cerr << "Failed to open device: " << devicePath << std::endl;
            return false;
        }
        std::cout << "Device " << devicePath << " opened successfully." << std::endl;
        return true;
    }

    bool configureFormat() {
        struct v4l2_format fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = pixelFormat;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;

        if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
            std::cerr << "Failed to set format on " << devicePath << std::endl;
            return false;
        }

        std::cout << "Format set to " << width << "x" << height << " on " << devicePath << std::endl;
        return true;
    }

    int getFd() const {
        return fd;
    }
    bool requestBuffers(uint32_t count) {
        struct v4l2_requestbuffers req = {};
        req.count = count;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
            std::cerr << "Failed to request buffers on " << devicePath << std::endl;
            return false;
        }

        std::cout << req.count << " buffers requested on " << devicePath << std::endl;
        return true;
    }
    bool mapAndQueueBuffers(uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
                std::cerr << "Failed to query buffer " << i << std::endl;
                return false;
            }

            Buffer buffer;
            buffer.length = buf.length;
            buffer.start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
            buffer.export_fd = -1; 

            if (buffer.start == MAP_FAILED) {
                std::cerr << "Failed to mmap buffer " << i << std::endl;
                return false;
            }

            buffers.push_back(buffer);

            if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
                std::cerr << "Failed to queue buffer " << i << std::endl;
                return false;
            }
        }
        std::cout << count << " buffers mapped and queued." << std::endl;
        return true;
    }

    bool startStreaming() {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
            std::cerr << "Failed to start streaming on " << devicePath << std::endl;
            return false;
        }
        std::cout << "Streaming started on " << devicePath << std::endl;
        return true;
    }
    int dequeueBuffer(uint32_t& bytes_used) {
        struct pollfd fds;
        fds.fd = fd;
        fds.events = POLLIN;

        if (poll(&fds, 1, 2000) <= 0) {
            return -1; 
        }

        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            return -1;
        }

        bytes_used = buf.bytesused;
        return buf.index;
    }

    bool queueBuffer(int index) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = index;

        return (ioctl(fd, VIDIOC_QBUF, &buf) != -1);
    }
    bool exportBuffers() {
        for (size_t i = 0; i < buffers.size(); ++i) {
            struct v4l2_exportbuffer expbuf = {};
            expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            expbuf.index = i;

            if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) == -1) {
                std::cerr << "Failed to export buffer " << i << " on " << devicePath << std::endl;
                return false;
            }
            buffers[i].export_fd = expbuf.fd;
        }
        std::cout << "Exported " << buffers.size() << " DMA-BUF file descriptors from " << devicePath << std::endl;
        return true;
    }

    int getExportFd(size_t index) const {
        if (index < buffers.size()) {
            return buffers[index].export_fd;
        }
        return -1;
    }
};

#endif // CAPTURE_DEVICE_HPP