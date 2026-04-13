#pragma once

#include <string>

#include "capture/CameraCapture.h"
#include "tracking/FaceTracker.h"
#include "tracking/HandTracker.h"
#include "tracking/TrackingResult.h"

namespace nohcam {

class TrackingTracker {
public:
    TrackingTracker() = default;
    ~TrackingTracker() = default;

    TrackingTracker(const TrackingTracker&) = delete;
    TrackingTracker& operator=(const TrackingTracker&) = delete;

    bool Initialize(std::string* error_message = nullptr);
    bool IsInitialized() const;
    const std::string& GetInitializeError() const;
    const TrackingResult& GetLastResult() const;

    TrackingResult Track(const CameraCapture::CaptureFrame& capture_frame);

private:
    FaceTracker face_tracker_;
    HandTracker hand_tracker_;

    bool initialized_ = false;
    std::string init_error_;
    TrackingResult last_result_;
};

}  // namespace nohcam
