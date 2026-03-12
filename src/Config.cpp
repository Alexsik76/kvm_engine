#include "Config.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

namespace Config {
    ConfigData data;
    
    uint32_t Loader::parseFormat(const std::string& fmt) {
        if (fmt == "UYVY") return V4L2_PIX_FMT_UYVY;
        if (fmt == "YUYV") return V4L2_PIX_FMT_YUYV;
        if (fmt == "RGB24") return V4L2_PIX_FMT_RGB24;
        if (fmt == "BGR24") return V4L2_PIX_FMT_BGR24;
        if (fmt == "MJPEG") return V4L2_PIX_FMT_MJPEG;
        std::cerr << "Unknown format: " << fmt << ", defaulting to UYVY" << std::endl;
        return V4L2_PIX_FMT_UYVY;
    }
    
    uint32_t Loader::parseProfile(const std::string& profile) {
        if (profile == "baseline") return V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
        if (profile == "main") return V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
        if (profile == "high") return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
        return V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
    }
    
    uint32_t Loader::parseLevel(const std::string& level) {
        if (level == "3.1") return V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
        if (level == "4.0") return V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
        if (level == "4.1") return V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
        if (level == "4.2") return V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
        if (level == "5.0") return V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
        return V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
    }
    
    ConfigData Loader::load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open config file: " + path);
        }
        
        nlohmann::json j;
        file >> j;
        
        ConfigData cfg;
        
        if (j.contains("video")) {
            const auto& v = j["video"];
            cfg.video.device = v.value("device", "/dev/video0");
            cfg.video.width = v.value("width", 1280);
            cfg.video.height = v.value("height", 720);
            cfg.video.formatStr = v.value("format", "UYVY");
            cfg.video.fps = v.value("fps", 60);
        }
        
        if (j.contains("encoder")) {
            const auto& e = j["encoder"];
            cfg.encoder.device = e.value("device", "/dev/video11");
            cfg.encoder.bitrate = e.value("bitrate", 2000000);
            cfg.encoder.gop = e.value("gop", 30);
            cfg.encoder.profile = e.value("profile", "baseline");
            cfg.encoder.level = e.value("level", "4.0");
        }
        
        if (j.contains("buffers")) {
            const auto& b = j["buffers"];
            cfg.buffers.count = b.value("count", 3);
        }
        
        if (j.contains("server")) {
            const auto& s = j["server"];
            cfg.server.port = s.value("port", 8080);
            cfg.server.keyboardDevice = s.value("keyboard_device", "/dev/hidg0");
            cfg.server.mouseDevice = s.value("mouse_device", "/dev/hidg1");
        }
        
        data = cfg;
        std::cout << "Configuration loaded from " << path << std::endl;
        return cfg;
    }
}
