#include "tracking/FaceTracker.h"

#include <algorithm>
#include <filesystem>
#include <iterator>

#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>

namespace nohcam {

namespace {

FaceTracker::InputLayout DetectInputLayout(const std::vector<int64_t>& shape) {
    if (shape.size() != 4) {
        return FaceTracker::InputLayout::Unknown;
    }

    if (shape[1] == 3) {
        return FaceTracker::InputLayout::Nchw;
    }

    if (shape[3] == 3) {
        return FaceTracker::InputLayout::Nhwc;
    }

    return FaceTracker::InputLayout::Unknown;
}

bool GetInputShape(const std::vector<int64_t>& shape, FaceTracker::InputLayout layout, int& channels, int& height, int& width) {
    if (shape.size() != 4) {
        return false;
    }

    if (layout == FaceTracker::InputLayout::Nchw) {
        channels = shape[1] > 0 ? static_cast<int>(shape[1]) : 0;
        height = shape[2] > 0 ? static_cast<int>(shape[2]) : 0;
        width = shape[3] > 0 ? static_cast<int>(shape[3]) : 0;
        return true;
    }

    if (layout == FaceTracker::InputLayout::Nhwc) {
        height = shape[1] > 0 ? static_cast<int>(shape[1]) : 0;
        width = shape[2] > 0 ? static_cast<int>(shape[2]) : 0;
        channels = shape[3] > 0 ? static_cast<int>(shape[3]) : 0;
        return true;
    }

    return false;
}

constexpr float ToNormalizedFloat(std::uint8_t value) {
    return static_cast<float>(value);
}

}  // namespace

FaceTracker::FaceTracker() = default;

bool FaceTracker::Initialize(std::string* error_message) {
    std::string local_error;
    const bool face_loaded = LoadFaceModel(&local_error);
    const bool blendshape_loaded = LoadBlendshapeModel(&local_error);

    if (!face_loaded && !blendshape_loaded) {
        initialized_ = false;
        init_error_ = local_error.empty() ? "Face and blendshape models were not found." : local_error;
        if (error_message) {
            *error_message = init_error_;
        }
        return false;
    }

    initialized_ = face_loaded;
    if (!face_loaded) {
        local_error += " Face landmark model was not loaded, so face tracking is disabled.";
    }

    init_error_ = std::move(local_error);
    if (error_message) {
        *error_message = init_error_;
    }
    return initialized_;
}

bool FaceTracker::IsInitialized() const {
    return initialized_;
}

const std::string& FaceTracker::GetInitializeError() const {
    return init_error_;
}

const FaceResult& FaceTracker::GetLastResult() const {
    return last_result_;
}

FaceResult FaceTracker::Track(const CameraCapture::CaptureFrame& capture_frame) {
    last_result_ = FaceResult{};
    if (!initialized_ || !capture_frame.valid) {
        return last_result_;
    }

    const auto& face_metadata = face_session_->GetMetadata();
    if (face_metadata.inputs.empty()) {
        return last_result_;
    }

    const std::string input_name = face_metadata.inputs.front().name;
    const int target_width = face_input_width_ > 0 ? face_input_width_ : static_cast<int>(capture_frame.width);
    const int target_height = face_input_height_ > 0 ? face_input_height_ : static_cast<int>(capture_frame.height);
    const int target_channels = face_input_channels_ > 0 ? face_input_channels_ : 3;
    
    static int track_counter = 0;
    const bool is_debug = (++track_counter % 60 == 0);
    if (is_debug) {
        spdlog::info("FaceTracker::Track: input={} target={}x{}x{} layout={} frame={}x{}", 
            input_name, target_width, target_height, target_channels,
            (int)face_input_layout_, capture_frame.width, capture_frame.height);
    }

    auto input_values = PreprocessFrame(capture_frame, target_width, target_height, target_channels, face_input_layout_);
    if (input_values.empty()) {
        spdlog::warn("FaceTracker::Track: PreprocessFrame returned empty");
        return last_result_;
    }

    if (is_debug) {
        spdlog::info("FaceTracker::Track: preprocessed {} values, first 6: {},{},{},{},{},{}", 
            input_values.size(),
            input_values[0], input_values[1], input_values[2],
            input_values[3], input_values[4], input_values[5]);
    }

    OnnxSession::TensorData input_tensor;
    input_tensor.name = input_name;
    if (face_input_layout_ == InputLayout::Nhwc) {
        input_tensor.shape = {1, target_height, target_width, target_channels};
    } else {
        input_tensor.shape = {1, target_channels, target_height, target_width};
    }
    input_tensor.values = std::move(input_values);

    const auto face_outputs = face_session_->Run({input_tensor});
    if (face_outputs.size() < 2) {
        spdlog::warn("FaceTracker::Track: unexpected output count {}", face_outputs.size());
        return last_result_;
    }

    const auto& regressors_output = face_outputs[0];
    const auto& classificators_output = face_outputs[1];

    constexpr float kFacePresenceThreshold = 0.5f;
    const float face_score = classificators_output.values.empty() ? 1.0f : classificators_output.values[0];
    if (is_debug) {
        spdlog::info("FaceTracker::Track: face_score={} threshold={}", face_score, kFacePresenceThreshold);
    }
    
    if (face_score < kFacePresenceThreshold) {
        return last_result_;
    }

    if (is_debug) {
        spdlog::info("FaceTracker::Track: regressors values={}", regressors_output.values.size());
        if (!regressors_output.values.empty()) {
            spdlog::info("FaceTracker::Track: first 6 regressors: {},{},{},{},{},{}",
                regressors_output.values[0], regressors_output.values[1],
                regressors_output.values[2], regressors_output.values[3],
                regressors_output.values[4], regressors_output.values[5]);
        }
    }

    last_result_ = ParseFaceOutput(regressors_output.values);

    if (blendshape_session_.has_value()) {
        const auto& blend_metadata = blendshape_session_->GetMetadata();
        if (!blend_metadata.inputs.empty() && last_result_.detected) {
            OnnxSession::TensorData blend_input;
            blend_input.name = blend_metadata.inputs.front().name;
            
            // MediaPipe blendshapes V2 usually takes 146 specific landmarks (x, y)
            // Input shape: [1, 146, 2]
            blend_input.shape = {1, 146, 2};
            blend_input.values.resize(146 * 2);
            
            // Mapping MediaPipe's 146 blendshape-required landmark indices
            // For now, let's just use the first 146 landmarks as a placeholder
            // or if the model expects normalized [0, 1] coordinates.
            for (int i = 0; i < 146; ++i) {
                blend_input.values[i * 2 + 0] = last_result_.landmarks[i].x;
                blend_input.values[i * 2 + 1] = last_result_.landmarks[i].y;
            }

            const auto blend_outputs = blendshape_session_->Run({blend_input});
            if (!blend_outputs.empty()) {
                last_result_.blendshapes = blend_outputs.front().values;
            }
        }
    }

    return last_result_;
}

bool FaceTracker::LoadFaceModel(std::string* error_message) {
    const std::filesystem::path model_path = std::filesystem::path(OnnxSession::GetDefaultModelDirectory()) / L"face_landmarks.onnx";
    if (!std::filesystem::exists(model_path)) {
        if (error_message) {
            *error_message += "face_landmarks.onnx is missing from the model directory. ";
        }
        return false;
    }

    face_session_.emplace();
    if (!face_session_->LoadModel(model_path, error_message)) {
        face_session_.reset();
        return false;
    }

    const auto& metadata = face_session_->GetMetadata();
    if (metadata.inputs.empty()) {
        if (error_message) {
            *error_message += "face_landmarks.onnx has no input metadata. ";
        }
        face_session_.reset();
        return false;
    }

    const auto& input_shape = metadata.inputs.front().shape;
    face_input_layout_ = DetectInputLayout(input_shape);
    if (face_input_layout_ == InputLayout::Unknown) {
        face_input_layout_ = InputLayout::Nchw;
    }
    GetInputShape(input_shape, face_input_layout_, face_input_channels_, face_input_height_, face_input_width_);

    spdlog::info("Loaded face landmark model: {}", std::filesystem::path(metadata.model_path).string());
    return true;
}

bool FaceTracker::LoadBlendshapeModel(std::string* error_message) {
    const std::filesystem::path model_path = std::filesystem::path(OnnxSession::GetDefaultModelDirectory()) / L"face_blendshapes.onnx";
    if (!std::filesystem::exists(model_path)) {
        if (error_message) {
            *error_message += "face_blendshapes.onnx is missing from the model directory. ";
        }
        return false;
    }

    blendshape_session_.emplace();
    if (!blendshape_session_->LoadModel(model_path, error_message)) {
        blendshape_session_.reset();
        return false;
    }

    const auto& metadata = blendshape_session_->GetMetadata();
    if (metadata.inputs.empty()) {
        if (error_message) {
            *error_message += "face_blendshapes.onnx has no input metadata. ";
        }
        blendshape_session_.reset();
        return false;
    }

    const auto& input_shape = metadata.inputs.front().shape;
    blendshape_input_layout_ = DetectInputLayout(input_shape);
    if (blendshape_input_layout_ == InputLayout::Unknown) {
        blendshape_input_layout_ = InputLayout::Nchw;
    }
    GetInputShape(input_shape, blendshape_input_layout_, blendshape_input_channels_, blendshape_input_height_, blendshape_input_width_);

    spdlog::info("Loaded face blendshape model: {}", std::filesystem::path(metadata.model_path).string());
    return true;
}

std::vector<float> FaceTracker::PreprocessFrame(
    const CameraCapture::CaptureFrame& frame,
    int target_width,
    int target_height,
    int target_channels,
    InputLayout layout) {
    if (!frame.valid || frame.width == 0 || frame.height == 0 || frame.pixels.empty()) {
        return {};
    }

    if (target_width <= 0) {
        target_width = static_cast<int>(frame.width);
    }
    if (target_height <= 0) {
        target_height = static_cast<int>(frame.height);
    }
    if (target_channels <= 0) {
        target_channels = 3;
    }
    if (layout == InputLayout::Unknown) {
        layout = InputLayout::Nchw;
    }

    const std::uint32_t source_width = frame.width;
    const std::uint32_t source_height = frame.height;
    const std::uint32_t source_stride = frame.stride;
    const std::uint32_t output_pixel_count = static_cast<std::uint32_t>(target_width) * static_cast<std::uint32_t>(target_height);

    std::vector<float> result;
    result.reserve(static_cast<std::size_t>(output_pixel_count) * target_channels);
    result.assign(static_cast<std::size_t>(output_pixel_count) * target_channels, 0.0f);

    for (int y = 0; y < target_height; ++y) {
        const std::uint32_t source_y = std::min<std::uint32_t>(source_height - 1,
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * source_height) / static_cast<std::uint32_t>(target_height)));
        for (int x = 0; x < target_width; ++x) {
            const std::uint32_t source_x = std::min<std::uint32_t>(source_width - 1,
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * source_width) / static_cast<std::uint32_t>(target_width)));
            const std::size_t source_offset = static_cast<std::size_t>(source_y) * source_stride + static_cast<std::size_t>(source_x) * 4;
            const std::uint8_t blue = frame.pixels[source_offset + 0];
            const std::uint8_t green = frame.pixels[source_offset + 1];
            const std::uint8_t red = frame.pixels[source_offset + 2];

            if (layout == InputLayout::Nhwc) {
                const std::size_t base_index = (static_cast<std::size_t>(y) * target_width + static_cast<std::size_t>(x)) * target_channels;
                if (target_channels >= 1) {
                    result[base_index + 0] = ToNormalizedFloat(red);
                }
                if (target_channels >= 2) {
                    result[base_index + 1] = ToNormalizedFloat(green);
                }
                if (target_channels >= 3) {
                    result[base_index + 2] = ToNormalizedFloat(blue);
                }
            } else {
                const std::size_t pixel_index = static_cast<std::size_t>(y) * target_width + static_cast<std::size_t>(x);
                if (target_channels >= 1) {
                    result[static_cast<std::size_t>(0) * output_pixel_count + pixel_index] = ToNormalizedFloat(blue);
                }
                if (target_channels >= 2) {
                    result[static_cast<std::size_t>(1) * output_pixel_count + pixel_index] = ToNormalizedFloat(green);
                }
                if (target_channels >= 3) {
                    result[static_cast<std::size_t>(2) * output_pixel_count + pixel_index] = ToNormalizedFloat(red);
                }
            }
        }
    }

    return result;
}

FaceResult FaceTracker::ParseFaceOutput(const std::vector<float>& output_values) {
    FaceResult result;
    // face_landmarks.onnx (v1/Legacy) returns 468 landmarks (x,y,z * 468 = 1404 values)
    if (output_values.size() < 468 * 3) {
        return result;
    }

    const std::size_t point_count = 468;
    float sum_x = 0.0f;
    float sum_y = 0.0f;

    for (std::size_t index = 0; index < point_count; ++index) {
        const float x = output_values[index * 3 + 0] / 192.0f;
        const float y = output_values[index * 3 + 1] / 192.0f;
        const float z = output_values[index * 3 + 2] / 192.0f;
        
        result.landmarks[index] = glm::vec3{x, y, z};
        sum_x += x;
        sum_y += y;
    }

    result.detected = true;
    const float inv_count = 1.0f / static_cast<float>(point_count);
    result.x = sum_x * inv_count;
    result.y = sum_y * inv_count;

    // Head pose from landmarks
    // 1: Nose tip
    // 33: Left eye outer
    // 263: Right eye outer
    // 152: Chin
    const auto& nose = result.landmarks[1];
    const auto& left_eye = result.landmarks[33];
    const auto& right_eye = result.landmarks[263];
    
    // Normalize relative to face size
    float eye_dist = glm::distance(left_eye, right_eye);
    if (eye_dist > 0.001f) {
        result.yaw = (nose.x - (left_eye.x + right_eye.x) * 0.5f) / eye_dist * 45.0f;
        result.pitch = (nose.y - (left_eye.y + right_eye.y) * 0.5f) / eye_dist * 45.0f;
        
        // Calculate roll from eye angle
        float dx = right_eye.x - left_eye.x;
        float dy = right_eye.y - left_eye.y;
        result.roll = std::atan2(dy, dx) * 180.0f / 3.14159265f;
        
        // Log every 60 frames
        static int log_counter = 0;
        if (++log_counter % 60 == 0) {
            spdlog::info("Head pose: yaw={:.2f} pitch={:.2f} roll={:.2f}", 
                result.yaw, result.pitch, result.roll);
        }
    }

    return result;
}

}  // namespace nohcam
