#include "tracking/TrackingTracker.h"
#include <spdlog/spdlog.h>
#include <utility>

namespace nohcam {

bool TrackingTracker::Initialize(std::string* error_message) {
    spdlog::info("TrackingTracker: Initializing...");
    std::string face_error;
    std::string hand_error;
    std::string pose_error;
    const bool face_ok = face_tracker_.Initialize(&face_error);
    const bool hand_ok = hand_tracker_.Initialize(&hand_error);
    const bool pose_ok = pose_tracker_.Initialize(&pose_error);

    initialized_ = face_ok && hand_ok && pose_ok;
    init_error_.clear();
    if (!face_ok) {
        spdlog::error("TrackingTracker: FaceTracker init failed: {}", face_error);
        init_error_ += "FaceTracker: " + face_error;
    }
    if (!hand_ok) {
        spdlog::error("TrackingTracker: HandTracker init failed: {}", hand_error);
        if (!init_error_.empty()) init_error_ += " ";
        init_error_ += "HandTracker: " + hand_error;
    }
    if (!pose_ok) {
        spdlog::error("TrackingTracker: PoseTracker init failed: {}", pose_error);
        if (!init_error_.empty()) init_error_ += " ";
        init_error_ += "PoseTracker: " + pose_error;
    }

    if (error_message) {
        *error_message = init_error_;
    }
    spdlog::info("TrackingTracker: Initialize result: {}", initialized_);
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

    try {
        // 1. Run Pose Tracker
        last_result_.pose = pose_tracker_.Track(capture_frame);

        // 2. Run Face Tracker
        last_result_.face = face_tracker_.Track(capture_frame);

        // 3. Run Hand Tracker
        const HandTrackingFrameResult hands = hand_tracker_.Track(capture_frame, &last_result_.pose);
        last_result_.left_hand = hands.left_hand;
        last_result_.right_hand = hands.right_hand;
    } catch (const std::exception& e) {
        spdlog::error("TrackingTracker: Exception in Track(): {}", e.what());
    } catch (...) {
        spdlog::error("TrackingTracker: Unknown exception in Track()");
    }

    last_result_.timestamp = std::chrono::steady_clock::now();
    return last_result_;
}


}  // namespace nohcam
