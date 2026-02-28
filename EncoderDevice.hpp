#ifndef ENCODER_DEVICE_HPP
#define ENCODER_DEVICE_HPP

#include <string>
#include <cstdint>
#include <cerrno>
#include <sys/mman.h>
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

public:
    EncoderDevice(const std::string& path) : devicePath(path), fd(-1) {}

    ~EncoderDevice() {
        if (fd != -1) {
            for (auto& buf : capture_buffers) {
            munmap(buf.start, buf.length);
        }
            close(fd);
            std::cout << "Encoder " << devicePath << " closed." << std::endl;
        }
    }

    bool openDevice() {
        fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK, 0);
        if (fd == -1) {
            std::cerr << "Failed to open encoder: " << devicePath << std::endl;
            return false;
        }
        std::cout << "Encoder " << devicePath << " opened successfully." << std::endl;

        struct v4l2_capability cap = {};
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
            std::cerr << "Failed to query encoder capabilities." << std::endl;
            return false;
        }

        std::cout << "Encoder Driver: " << cap.driver << std::endl;
        
        uint32_t caps = cap.capabilities;
        if (caps & V4L2_CAP_DEVICE_CAPS) {
            caps = cap.device_caps;
        }

        if (caps & V4L2_CAP_VIDEO_M2M_MPLANE) {
            std::cout << "Encoder requires Multi-Planar API." << std::endl;
        } else if (caps & V4L2_CAP_VIDEO_M2M) {
            std::cout << "Encoder requires Single-Planar API." << std::endl;
        } else {
            std::cout << "Warning: Device doesn't report standard M2M capabilities." << std::endl;
        }

        return true;
    }
    bool configureFormats(uint32_t width, uint32_t height) {
        // 1. Configure OUTPUT queue (Input for encoder: Raw UYVY)
        struct v4l2_format fmt_out = {};
        fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        fmt_out.fmt.pix_mp.width = width;
        fmt_out.fmt.pix_mp.height = height;
        fmt_out.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_UYVY;
        fmt_out.fmt.pix_mp.num_planes = 1;
        fmt_out.fmt.pix_mp.field = V4L2_FIELD_ANY;

        if (ioctl(fd, VIDIOC_S_FMT, &fmt_out) == -1) {
            std::cerr << "Failed to set OUTPUT format on encoder." << std::endl;
            return false;
        }

        // 2. Configure CAPTURE queue (Output from encoder: H.264)
        struct v4l2_format fmt_cap = {};
        fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt_cap.fmt.pix_mp.width = width;
        fmt_cap.fmt.pix_mp.height = height;
        fmt_cap.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
        fmt_cap.fmt.pix_mp.num_planes = 1;

        if (ioctl(fd, VIDIOC_S_FMT, &fmt_cap) == -1) {
            std::cerr << "Failed to set CAPTURE format on encoder." << std::endl;
            return false;
        }
        std::cout << "Encoder formats configured successfully (MPLANE)." << std::endl;
        return true;
    }
    bool setupH264Controls() {
        struct v4l2_ext_control ctrls[3] = {};
        
        ctrls[0].id = V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER;
        ctrls[0].value = 1;

        ctrls[1].id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
        ctrls[1].value = 30;

        ctrls[2].id = V4L2_CID_MPEG_VIDEO_BITRATE;
        ctrls[2].value = 2000000; 

        struct v4l2_ext_controls ext_ctrls = {};
        ext_ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
        ext_ctrls.count = 3;
        ext_ctrls.controls = ctrls;

        if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls) == -1) {
            std::cerr << "Warning: Failed to set some H.264 parameters." << std::endl;
        } else {
            std::cout << "H.264 controls (Bitrate 2Mbps, GOP 30, SPS/PPS) applied." << std::endl;
        }
        return true;
    }
    bool requestBuffers(uint32_t count) {
        struct v4l2_requestbuffers req_out = {};
        req_out.count = count;
        req_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        req_out.memory = V4L2_MEMORY_DMABUF; 

        if (ioctl(fd, VIDIOC_REQBUFS, &req_out) == -1) {
            std::cerr << "Failed to request OUTPUT buffers on encoder." << std::endl;
            return false;
        }

        struct v4l2_requestbuffers req_cap = {};
        req_cap.count = count;
        req_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req_cap.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_REQBUFS, &req_cap) == -1) {
            std::cerr << "Failed to request CAPTURE buffers on encoder." << std::endl;
            return false;
        }

        std::cout << count << " buffers requested for encoder (DMABUF in, MMAP out)." << std::endl;
        return true;
    }
    bool mapCaptureBuffers(uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            struct v4l2_buffer buf = {};
            struct v4l2_plane planes[1] = {};
            
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            buf.m.planes = planes;
            buf.length = 1; 

            if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
                std::cerr << "Failed to query CAPTURE buffer " << i << " on encoder." << std::endl;
                return false;
            }

            Buffer buffer;
            buffer.length = buf.m.planes[0].length;
            buffer.start = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.planes[0].m.mem_offset);

            if (buffer.start == MAP_FAILED) {
                std::cerr << "Failed to mmap CAPTURE buffer " << i << " on encoder." << std::endl;
                return false;
            }

            capture_buffers.push_back(buffer);
        }
        std::cout << count << " CAPTURE buffers mapped on encoder." << std::endl;
        return true;
    }
    bool startStreaming() {
        int type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        if (ioctl(fd, VIDIOC_STREAMON, &type_out) == -1) {
            std::cerr << "Failed to start OUTPUT stream on encoder." << std::endl;
            return false;
        }

        int type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (ioctl(fd, VIDIOC_STREAMON, &type_cap) == -1) {
            std::cerr << "Failed to start CAPTURE stream on encoder." << std::endl;
            return false;
        }

        for (uint32_t i = 0; i < capture_buffers.size(); ++i) {
            struct v4l2_buffer buf = {};
            struct v4l2_plane planes[1] = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            buf.length = 1;
            buf.m.planes = planes;

            if (ioctl(fd, VIDIOC_QBUF, &buf) == -1) {
                return false;
            }
        }
        std::cout << "Encoder streaming started." << std::endl;
        return true;
    }

    bool queueOutputBuffer(int index, int dmabuf_fd, uint32_t bytesused) {
        struct v4l2_buffer buf = {};
        struct v4l2_plane planes[1] = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index = index;
        buf.length = 1;
        buf.m.planes = planes;
        buf.m.planes[0].m.fd = dmabuf_fd;
        buf.m.planes[0].bytesused = bytesused;
        buf.m.planes[0].length = bytesused;

        return (ioctl(fd, VIDIOC_QBUF, &buf) != -1);
    }

    int dequeueOutputBuffer() {
        struct v4l2_buffer buf = {};
        struct v4l2_plane planes[1] = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.length = 1;
        buf.m.planes = planes;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            return -1; 
        }
        return buf.index;
    }

    int dequeueCaptureBuffer(uint32_t& bytes_used) {
        struct v4l2_buffer buf = {};
        struct v4l2_plane planes[1] = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = 1;
        buf.m.planes = planes;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            return -1;
        }
        bytes_used = buf.m.planes[0].bytesused;
        return buf.index;
    }

    bool queueCaptureBuffer(int index) {
        struct v4l2_buffer buf = {};
        struct v4l2_plane planes[1] = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = index;
        buf.length = 1;
        buf.m.planes = planes;

        return (ioctl(fd, VIDIOC_QBUF, &buf) != -1);
    }
    void* getCaptureBufferPointer(int index) const {
        if (index >= 0 && index < (int)capture_buffers.size()) {
            return capture_buffers[index].start;
        }
        return nullptr;
    }
};

#endif