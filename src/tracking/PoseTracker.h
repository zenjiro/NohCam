#pragma once

#include <optional>
#include <string>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "capture/CameraCapture.h"
#include "tracking/OnnxSession.h"
#include "tracking/TrackingResult.h"

namespace nohcam {

class PoseTracker {
public:
    PoseTracker();
    ~PoseTracker();

    bool Initialize(std::string* error_message = nullptr);
    PoseResult Track(const CameraCapture::CaptureFrame& capture_frame);

private:
    struct PoseRoi {
        glm::vec2 center;
        glm::vec2 size;
        float rotation = 0.0f;
    };

    bool LoadDetectionModel(std::string* error);
    bool LoadLandmarkModel(std::string* error);

    PoseRoi DetectPose(const CameraCapture::CaptureFrame& frame);
    PoseResult ExtractLandmarks(const CameraCapture::CaptureFrame& frame, const PoseRoi& roi);

    std::unique_ptr<OnnxSession> detection_session_;
    std::unique_ptr<OnnxSession> landmark_session_;

    bool initialized_ = false;
    std::string init_error_;

    PoseRoi last_roi_;
    bool has_last_roi_ = false;
};

}  // namespace nohcam
