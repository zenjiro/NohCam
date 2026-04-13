#pragma once

#include <optional>
#include <string>
#include <vector>
#include <glm/vec2.hpp>

#include "capture/CameraCapture.h"
#include "tracking/OnnxSession.h"
#include "tracking/TrackingResult.h"

namespace nohcam {

struct HandTrackingFrameResult {
    HandResult left_hand;
    HandResult right_hand;
};

class HandTracker {
public:
    enum class InputLayout {
        Unknown,
        Nchw,
        Nhwc,
    };

    HandTracker();
    ~HandTracker() = default;

    HandTracker(const HandTracker&) = delete;
    HandTracker& operator=(const HandTracker&) = delete;

    bool Initialize(std::string* error_message = nullptr);
    bool IsInitialized() const;
    const std::string& GetInitializeError() const;
    const HandTrackingFrameResult& GetLastResult() const;

    HandTrackingFrameResult Track(const CameraCapture::CaptureFrame& capture_frame);

private:
    struct PalmDetection {
        float score = 0.0f;
        float cx = 0.0f;
        float cy = 0.0f;
        float size = 0.0f;
        float angle_radians = 0.0f;
    };

    struct RoiTransform {
        float left = 0.0f;
        float top = 0.0f;
        float size = 1.0f;
    };

    struct ParsedHandCandidate {
        HandResult hand;
        float handedness_left_score = -1.0f;
        float hand_score = 0.0f;
        glm::vec2 wrist = glm::vec2(0.0f, 0.0f);
    };

    bool LoadHandModel(std::string* error_message);
    bool LoadPalmModel(std::string* error_message);
    static std::vector<float> PreprocessFrame(
        const CameraCapture::CaptureFrame& frame,
        int target_width,
        int target_height,
        int target_channels,
        InputLayout layout);
    static std::vector<PalmDetection> ParsePalmDetections(const std::vector<OnnxSession::TensorData>& outputs);
    static std::vector<PalmDetection> ApplyPalmNms(const std::vector<PalmDetection>& detections, std::size_t max_count);
    static float PalmIoU(const PalmDetection& a, const PalmDetection& b);
    static RoiTransform BuildRoi(const PalmDetection& palm, std::uint32_t width, std::uint32_t height);
    static CameraCapture::CaptureFrame CropToRoi(const CameraCapture::CaptureFrame& frame, const RoiTransform& roi, int target_size);
    std::vector<PalmDetection> RunPalmInference(const CameraCapture::CaptureFrame& capture_frame);
    static CameraCapture::CaptureFrame MaskPalmRegion(const CameraCapture::CaptureFrame& frame, const PalmDetection& palm);
    static CameraCapture::CaptureFrame FlipHorizontal(const CameraCapture::CaptureFrame& frame);
    std::optional<ParsedHandCandidate> RunLandmarkOnRoi(const CameraCapture::CaptureFrame& capture_frame, const RoiTransform& roi);
    static ParsedHandCandidate MapLandmarksToFrame(const ParsedHandCandidate& local_result, const RoiTransform& roi);
    std::pair<HandResult, HandResult> AssignHandsStable(const std::vector<ParsedHandCandidate>& candidates);
    static void FillJointAngles(HandResult& hand);
    static bool IsPalmOutputShapeSupported(const std::vector<OnnxSession::TensorMetadata>& outputs);
    static bool IsHandOutputShapeSupported(const std::vector<OnnxSession::TensorMetadata>& outputs);

    std::optional<OnnxSession> palm_session_;
    std::optional<OnnxSession> hand_session_;
    bool initialized_ = false;
    std::string init_error_;

    int palm_input_width_ = 0;
    int palm_input_height_ = 0;
    int palm_input_channels_ = 0;
    InputLayout palm_input_layout_ = InputLayout::Unknown;

    int hand_input_width_ = 0;
    int hand_input_height_ = 0;
    int hand_input_channels_ = 0;
    InputLayout hand_input_layout_ = InputLayout::Unknown;

    bool has_prev_left_ = false;
    bool has_prev_right_ = false;
    glm::vec2 prev_left_wrist_ = glm::vec2(0.0f, 0.0f);
    glm::vec2 prev_right_wrist_ = glm::vec2(0.0f, 0.0f);

    HandTrackingFrameResult last_result_;
};

}  // namespace nohcam
