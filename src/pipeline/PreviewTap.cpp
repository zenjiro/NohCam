#include "pipeline/PreviewTap.h"

#include <algorithm>

namespace nohcam {

namespace {

std::uint32_t ComputeScaledDimension(std::uint32_t value, float scale) {
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(value * scale));
}

}  // namespace

PreviewTap::PreviewTap(Config config) : config_(config) {
    state_.max_width = config_.max_width;
    state_.max_height = config_.max_height;
    state_.target_fps = config_.target_fps;
}

void PreviewTap::SetEnabled(bool enabled) {
    std::scoped_lock lock(mutex_);
    enabled_ = enabled;
    state_.enabled = enabled;
}

bool PreviewTap::IsEnabled() const {
    std::scoped_lock lock(mutex_);
    return enabled_;
}

PreviewTap::StateSnapshot PreviewTap::GetStateSnapshot() const {
    std::scoped_lock lock(mutex_);
    return state_;
}

bool PreviewTap::SubmitFrame(const CameraCapture::CaptureFrame& capture_frame) {
    if (!capture_frame.valid || capture_frame.width == 0 || capture_frame.height == 0 || capture_frame.pixels.empty()) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    state_.source_frame_count = capture_frame.frame_count;

    if (!enabled_ || capture_frame.frame_count == last_submitted_frame_count_) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto min_interval = std::chrono::milliseconds(1000 / std::max<std::uint32_t>(1, config_.target_fps));
    if (last_preview_time_.time_since_epoch().count() != 0 && (now - last_preview_time_) < min_interval) {
        return false;
    }

    latest_preview_frame_ = DownsampleFrame(capture_frame, config_.max_width, config_.max_height);
    if (!latest_preview_frame_.valid) {
        return false;
    }

    last_preview_time_ = now;
    last_submitted_frame_count_ = capture_frame.frame_count;
    state_.preview_frame_count = latest_preview_frame_.frame_count;
    return true;
}

std::optional<CameraCapture::CaptureFrame> PreviewTap::GetLatestPreviewFrame() const {
    std::scoped_lock lock(mutex_);
    if (!latest_preview_frame_.valid) {
        return std::nullopt;
    }

    return latest_preview_frame_;
}

CameraCapture::CaptureFrame PreviewTap::DownsampleFrame(
    const CameraCapture::CaptureFrame& source_frame,
    std::uint32_t max_width,
    std::uint32_t max_height) {
    CameraCapture::CaptureFrame preview_frame;
    if (source_frame.width == 0 || source_frame.height == 0 || max_width == 0 || max_height == 0) {
        return preview_frame;
    }

    const float width_scale = static_cast<float>(max_width) / static_cast<float>(source_frame.width);
    const float height_scale = static_cast<float>(max_height) / static_cast<float>(source_frame.height);
    const float scale = std::min({1.0f, width_scale, height_scale});

    preview_frame.valid = true;
    preview_frame.width = ComputeScaledDimension(source_frame.width, scale);
    preview_frame.height = ComputeScaledDimension(source_frame.height, scale);
    preview_frame.stride = preview_frame.width * 4;
    preview_frame.frame_count = source_frame.frame_count;
    preview_frame.timestamp_hns = source_frame.timestamp_hns;
    preview_frame.pixels.resize(static_cast<std::size_t>(preview_frame.stride) * preview_frame.height);

    for (std::uint32_t y = 0; y < preview_frame.height; ++y) {
        const std::uint32_t source_y = std::min<std::uint32_t>(
            source_frame.height - 1,
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * source_frame.height) / preview_frame.height));
        for (std::uint32_t x = 0; x < preview_frame.width; ++x) {
            const std::uint32_t source_x = std::min<std::uint32_t>(
                source_frame.width - 1,
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * source_frame.width) / preview_frame.width));

            const std::size_t source_offset =
                static_cast<std::size_t>(source_y) * source_frame.stride + static_cast<std::size_t>(source_x) * 4;
            const std::size_t destination_offset =
                static_cast<std::size_t>(y) * preview_frame.stride + static_cast<std::size_t>(x) * 4;

            preview_frame.pixels[destination_offset + 0] = source_frame.pixels[source_offset + 0];
            preview_frame.pixels[destination_offset + 1] = source_frame.pixels[source_offset + 1];
            preview_frame.pixels[destination_offset + 2] = source_frame.pixels[source_offset + 2];
            preview_frame.pixels[destination_offset + 3] = source_frame.pixels[source_offset + 3];
        }
    }

    return preview_frame;
}

}  // namespace nohcam
