#ifndef ENCODER_DEVICE_HPP
#define ENCODER_DEVICE_HPP

#include <string>
#include <cstdint>
#include <cerrno>
#include <sys/mman.h>
#include <sys/time.h>   // struct timeval
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
        void*  start;
        size_t length;
    };
    std::vector<Buffer> capture_buffers;
    // Buffer sizes for the OUTPUT (raw input) queue, needed for correct DMABUF queuing
    std::vector<size_t> output_buffer_lengths;

public:
    EncoderDevice(const std::string& path);
    ~EncoderDevice();

    bool openDevice();
    bool configureFormats(uint32_t width, uint32_t height);

    // Set the encoder OUTPUT queue frame rate via VIDIOC_S_PARM.
    // Must be called AFTER configureFormats() and BEFORE setupH264Controls().
    // The encoder uses this fps to fill the H.264 SPS VUI timing fields;
    // without it the SPS defaults to 60fps regardless of actual capture rate,
    // causing ffplay/VLC to compute wrong display timestamps.
    bool configureFrameRate(uint32_t fps);

    bool setupH264Controls();
    bool requestBuffers(uint32_t count);
    bool mapCaptureBuffers(uint32_t count);
    bool startStreaming();

    // timestamp: the wall-clock time the raw frame was captured (from CaptureDevice).
    // The V4L2 M2M spec requires the driver to copy buf.timestamp from the OUTPUT
    // queue to the corresponding CAPTURE queue entry, allowing us to track frame
    // latency end-to-end.
    bool queueOutputBuffer(int index, int dmabuf_fd, uint32_t bytesused,
                           const struct timeval& timestamp);

    int dequeueOutputBuffer();

    // timestamp: the capture wall-clock time, copied by the driver from the
    //            matching OUTPUT buffer we queued in queueOutputBuffer().
    int dequeueCaptureBuffer(uint32_t& bytes_used, struct timeval& timestamp);
    bool queueCaptureBuffer(int index);

    void* getCaptureBufferPointer(int index) const;
    int getFd() const;
};

#endif // ENCODER_DEVICE_HPP