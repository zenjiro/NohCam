#pragma once

#include <optional>
#include <string>
#include <vector>

#include "capture/CameraCapture.h"
#include "tracking/OnnxSession.h"
#include "tracking/TrackingResult.h"

namespace nohcam {

class FaceTracker {
public:
    enum class InputLayout {
        Unknown,
        Nchw,
        Nhwc,
    };

    FaceTracker();
    ~FaceTracker() = default;

    FaceTracker(const FaceTracker&) = delete;
    FaceTracker& operator=(const FaceTracker&) = delete;

    bool Initialize(std::string* error_message = nullptr);
    bool IsInitialized() const;
    const std::string& GetInitializeError() const;
    const FaceResult& GetLastResult() const;

    FaceResult Track(const CameraCapture::CaptureFrame& capture_frame, const PoseResult* pose_hint = nullptr);

private:

    struct FaceRoi {
        float cx = 0.5f;
        float cy = 0.5f;
        float size = 1.0f;
    };

    bool LoadFaceModel(std::string* error_message);
    bool LoadBlendshapeModel(std::string* error_message);
    static std::vector<float> PreprocessFrame(
        const CameraCapture::CaptureFrame& frame,
        int target_width,
        int target_height,
        int target_channels,
        InputLayout layout);
    static FaceResult ParseFaceOutput(const std::vector<float>& output_values);

    FaceRoi BuildRoiFromPose(const PoseResult& pose);
    CameraCapture::CaptureFrame CropToRoi(const CameraCapture::CaptureFrame& frame, const FaceRoi& roi, int target_size);
    FaceResult MapLandmarksToFrame(const FaceResult& local_result, const FaceRoi& roi);

    std::optional<OnnxSession> face_session_;
    std::optional<OnnxSession> blendshape_session_;
    bool initialized_ = false;
    std::string init_error_;

    int face_input_width_ = 0;
    int face_input_height_ = 0;
    int face_input_channels_ = 0;
    InputLayout face_input_layout_ = InputLayout::Unknown;

    int blendshape_input_width_ = 0;
    int blendshape_input_height_ = 0;
    int blendshape_input_channels_ = 0;
    InputLayout blendshape_input_layout_ = InputLayout::Unknown;

    FaceResult last_result_;
};

}  // namespace nohcam
