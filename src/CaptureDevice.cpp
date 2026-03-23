#include "CaptureDevice.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <iostream>
#include <sys/mman.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <poll.h>

CaptureDevice::CaptureDevice(const std::string& path, uint32_t w, uint32_t h, uint32_t format)
    : devicePath(path), fd(-1), width(w), height(h), pixelFormat(format) {}

CaptureDevice::~CaptureDevice() {
    if (fd != -1) {
        for (auto& buf : buffers) {
            munmap(buf.start, buf.length);
        }
        close(fd);
        std::cout << "Device " << devicePath << " closed." << std::endl;
    }
}

bool CaptureDevice::openDevice() {
    fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd == -1) {
        std::cerr << "Failed to open device: " << devicePath << std::endl;
        return false;
    }
    std::cout << "Device " << devicePath << " opened successfully." << std::endl;
    return true;
}

bool CaptureDevice::configureFormat() {
    // Query active timings from the hardware
    struct v4l2_dv_timings timings;
    if (ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &timings) == 0) {
        width = timings.bt.width;
        height = timings.bt.height;
        std::cout << "Dynamically detected signal: " << width << "x" << height << std::endl;
    } else {
        std::cerr << "Warning: Could not query DV timings. Using default: " 
                  << width << "x" << height << std::endl;
    }

    struct v4l2_format fmt = {};
    fmt.type               = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width      = width;
    fmt.fmt.pix.height     = height;
    fmt.fmt.pix.pixelformat = pixelFormat;
    fmt.fmt.pix.field      = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        std::cerr << "Failed to set format on " << devicePath << std::endl;
        return false;
    }

    // Update dimensions in case the driver adjusted them
    width = fmt.fmt.pix.width;
    height = fmt.fmt.pix.height;

    std::cout << "Format set and confirmed to " << width << "x" << height
              << " on " << devicePath << std::endl;
    return true;
}

bool CaptureDevice::requestBuffers(uint32_t count) {
    struct v4l2_requestbuffers req = {};
    req.count  = count;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        std::cerr << "Failed to request buffers on " << devicePath << std::endl;
        return false;
    }

    std::cout << req.count << " buffers requested on " << devicePath << std::endl;
    return true;
}

bool CaptureDevice::mapAndQueueBuffers(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        struct v4l2_buffer buf = {};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
            std::cerr << "Failed to query buffer " << i << std::endl;
            return false;
        }

        Buffer buffer;
        buffer.length    = buf.length;
        buffer.start     = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, buf.m.offset);
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

bool CaptureDevice::startStreaming() {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        std::cerr << "Failed to start streaming on " << devicePath << std::endl;
        return false;
    }
    std::cout << "Streaming started on " << devicePath << std::endl;
    return true;
}

int CaptureDevice::dequeueBuffer(uint32_t& bytes_used, struct timeval& timestamp) {
    struct pollfd fds;
    fds.fd     = fd;
    fds.events = POLLIN;

    int ret = poll(&fds, 1, 100);
    if (ret < 0) {
        std::cerr << "poll() error on " << devicePath << std::endl;
        return -1;
    }
    if (ret == 0) {
        return -1;
    }

    struct v4l2_buffer buf = {};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
        if (errno == EAGAIN) {
            return -1;
        }
        
        std::string err_msg = "VIDIOC_DQBUF hardware failure: ";
        err_msg += strerror(errno);
        throw std::runtime_error(err_msg);
    }

    bytes_used = buf.bytesused;
    timestamp  = buf.timestamp;
    return buf.index;
}

// Повернуто необхідний метод queueBuffer
bool CaptureDevice::queueBuffer(int index) {
    struct v4l2_buffer buf = {};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = index;

    return (ioctl(fd, VIDIOC_QBUF, &buf) != -1);
}

bool CaptureDevice::exportBuffers() {
    for (size_t i = 0; i < buffers.size(); ++i) {
        struct v4l2_exportbuffer expbuf = {};
        expbuf.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        expbuf.index = i;

        if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) == -1) {
            std::cerr << "Failed to export buffer " << i
                      << " on " << devicePath << std::endl;
            return false;
        }
        buffers[i].export_fd = expbuf.fd;
    }
    std::cout << "Exported " << buffers.size()
              << " DMA-BUF file descriptors from " << devicePath << std::endl;
    return true;
}

int CaptureDevice::getExportFd(size_t index) const {
    if (index < buffers.size()) {
        return buffers[index].export_fd;
    }
    return -1;
}

int CaptureDevice::getFd() const { return fd; }

bool CaptureDevice::initialize(uint32_t count) {
    if (!openDevice())                 return false;
    if (!configureFormat())            return false;
    if (!requestBuffers(count))        return false;
    if (!mapAndQueueBuffers(count))    return false;
    if (!exportBuffers())              return false;
    if (!startStreaming())             return false;
    return true;
}