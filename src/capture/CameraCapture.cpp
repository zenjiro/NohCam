#include "capture/CameraCapture.h"
#include <spdlog/spdlog.h>

namespace nohcam {

CameraCapture::CameraCapture() = default;

CameraCapture::~CameraCapture() {
    Stop();
}

bool CameraCapture::Start(int device_index) {
    stop_requested_ = false;
    if (!video_capture_.open(device_index)) {
        spdlog::error("CameraCapture: Failed to open camera device {}", device_index);
        return false;
    }
    is_video_file_ = false;
    capture_thread_ = std::thread(&CameraCapture::CaptureLoop, this);
    return true;
}

bool CameraCapture::Start(const std::string& file_path) {
    stop_requested_ = false;
    if (!video_capture_.open(file_path)) {
        spdlog::error("CameraCapture: Failed to open video file {}", file_path);
        return false;
    }
    is_video_file_ = true;
    capture_thread_ = std::thread(&CameraCapture::CaptureLoop, this);
    return true;
}

void CameraCapture::Stop() {
    stop_requested_ = true;
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    video_capture_.release();
}

void CameraCapture::CaptureLoop() {
    cv::Mat frame;
    while (!stop_requested_) {
        if (!video_capture_.read(frame)) {
            if (is_video_file_) {
                // Loop the video
                video_capture_.set(cv::CAP_PROP_POS_FRAMES, 0);
                continue;
            } else {
                break;
            }
        }

        CaptureFrame capture_frame;
        capture_frame.width = frame.cols;
        capture_frame.height = frame.rows;
        capture_frame.stride = frame.cols * 4;
        capture_frame.pixels.resize(capture_frame.stride * capture_frame.height);

        cv::Mat bgra_frame(frame.rows, frame.cols, CV_8UC4, capture_frame.pixels.data());
        cv::cvtColor(frame, bgra_frame, cv::COLOR_BGR2BGRA);

        capture_frame.valid = true;

        {
            std::lock_guard<std::mutex> lock(capture_frame_mutex_);
            latest_capture_frame_ = std::move(capture_frame);
        }
    }
}

CameraCapture::CaptureFrame CameraCapture::GetLatestFrame() {
    std::lock_guard<std::mutex> lock(capture_frame_mutex_);
    return latest_capture_frame_;
}

}  // namespace nohcam
