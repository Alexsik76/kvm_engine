#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <cstdint>
#include <linux/videodev2.h>

namespace Config {
    const std::string videoNode   = "/dev/video0";
    const uint32_t    width       = 1280;
    const uint32_t    height      = 720;
    const uint32_t    format      = V4L2_PIX_FMT_UYVY;
    const std::string encoderNode = "/dev/video11";
    // fps: capture device confirmed at 60fps via v4l2-ctl --stream-mmap
    const uint32_t    fps         = 60;
    // 3 buffers: one frame margin over the 2-buffer minimum.
    // Prevents encoder starvation if stdout writes block for >1 frame period.
    const uint32_t    bufCount    = 3;
}

#endif // CONFIG_HPP
