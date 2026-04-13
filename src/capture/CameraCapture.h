#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

namespace nohcam {

class CameraCapture {
public:
    struct CaptureFrame {
        bool valid = false;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride = 0;
        uint64_t frame_count = 0;
        uint64_t timestamp_hns = 0;
        std::vector<uint8_t> pixels;
    };

    CameraCapture();
    ~CameraCapture();

    bool Start(int device_index);
    bool Start(const std::string& file_path);
    void Stop();

    CaptureFrame GetLatestFrame();

private:
    void CaptureLoop();

    std::mutex capture_frame_mutex_;
    CaptureFrame latest_capture_frame_;
    
    std::thread capture_thread_;
    std::atomic<bool> stop_requested_ = false;

    cv::VideoCapture video_capture_;
    bool is_video_file_ = false;
};

}  // namespace nohcam
