#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

#include "capture/CameraCapture.h"

namespace nohcam {

class PreviewTap {
public:
    struct Config {
        std::uint32_t max_width = 640;
        std::uint32_t max_height = 360;
        std::uint32_t target_fps = 10;
    };

    struct StateSnapshot {
        bool enabled = true;
        std::uint32_t max_width = 640;
        std::uint32_t max_height = 360;
        std::uint32_t target_fps = 10;
        std::uint64_t source_frame_count = 0;
        std::uint64_t preview_frame_count = 0;
    };

    explicit PreviewTap(Config config = {});

    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    StateSnapshot GetStateSnapshot() const;
    bool SubmitFrame(const CameraCapture::CaptureFrame& capture_frame);
    std::optional<CameraCapture::CaptureFrame> GetLatestPreviewFrame() const;

private:
    static CameraCapture::CaptureFrame DownsampleFrame(
        const CameraCapture::CaptureFrame& source_frame,
        std::uint32_t max_width,
        std::uint32_t max_height);

    Config config_;
    mutable std::mutex mutex_;
    bool enabled_ = true;
    std::uint64_t last_submitted_frame_count_ = 0;
    std::chrono::steady_clock::time_point last_preview_time_ = {};
    StateSnapshot state_;
    CameraCapture::CaptureFrame latest_preview_frame_;
};

}  // namespace nohcam
