#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <cstdint>
#include <linux/videodev2.h>
#include <memory>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Config {
    struct VideoConfig {
        std::string device;
        uint32_t width = 1280;
        uint32_t height = 720;
        std::string formatStr = "UYVY";
        uint32_t fps = 60;
        
        uint32_t getV4L2Format() const;
    };
    
    struct EncoderConfig {
        std::string device;
        uint32_t bitrate = 2000000;
        uint32_t gop = 30;
        std::string profile = "baseline";
        std::string level = "4.0";
    };
    
    struct BufferConfig {
        uint32_t count = 3;
    };
    
    struct ServerConfig {
        uint16_t port = 8080;
        std::string keyboardDevice = "/dev/hidg0";
        std::string mouseDevice = "/dev/hidg1";
    };
    
    struct ConfigData {
        VideoConfig video;
        EncoderConfig encoder;
        BufferConfig buffers;
        ServerConfig server;
    };
    
    class Loader {
    public:
        static ConfigData load(const std::string& path = "/home/alex/kvm_engine/config/config.json");
        
    public:
        static uint32_t parseFormat(const std::string& fmt);
        static uint32_t parseProfile(const std::string& profile);
        static uint32_t parseLevel(const std::string& level);
    };
    
    extern ConfigData data;
    
    inline uint32_t VideoConfig::getV4L2Format() const {
        return Loader::parseFormat(formatStr);
    }
}

#endif
