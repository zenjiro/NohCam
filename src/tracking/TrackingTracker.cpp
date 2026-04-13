#include "tracking/TrackingTracker.h"

#include <utility>

namespace nohcam {

bool TrackingTracker::Initialize(std::string* error_message) {
    std::string face_error;
    std::string hand_error;
    const bool face_ok = face_tracker_.Initialize(&face_error);
    const bool hand_ok = hand_tracker_.Initialize(&hand_error);

    initialized_ = face_ok && hand_ok;
    init_error_.clear();
    if (!face_error.empty()) {
        init_error_ += "FaceTracker: " + face_error;
    }
    if (!hand_error.empty()) {
        if (!init_error_.empty()) {
            init_error_ += " ";
        }
        init_error_ += "HandTracker: " + hand_error;
    }

    if (error_message) {
        *error_message = init_error_;
    }
    return initialized_;
}

bool TrackingTracker::IsInitialized() const {
    return initialized_;
}

const std::string& TrackingTracker::GetInitializeError() const {
    return init_error_;
}

const TrackingResult& TrackingTracker::GetLastResult() const {
    return last_result_;
}

TrackingResult TrackingTracker::Track(const CameraCapture::CaptureFrame& capture_frame) {
    last_result_ = TrackingResult{};
    if (!initialized_) {
        return last_result_;
    }

    last_result_.face = face_tracker_.Track(capture_frame);
    const HandTrackingFrameResult hands = hand_tracker_.Track(capture_frame);
    last_result_.left_hand = hands.left_hand;
    last_result_.right_hand = hands.right_hand;
    last_result_.timestamp = std::chrono::steady_clock::now();
    return last_result_;
}

}  // namespace nohcam
