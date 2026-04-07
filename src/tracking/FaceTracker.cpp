#include "tracking/FaceTracker.h"

#include <algorithm>
#include <filesystem>
#include <iterator>

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
    return static_cast<float>(value) / 255.0f;
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
    auto input_values = PreprocessFrame(capture_frame, target_width, target_height, target_channels, face_input_layout_);
    if (input_values.empty()) {
        return last_result_;
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
    if (face_outputs.empty()) {
        return last_result_;
    }

    last_result_ = ParseFaceOutput(face_outputs.front().values);

    if (blendshape_session_.has_value()) {
        const auto& blend_metadata = blendshape_session_->GetMetadata();
        if (!blend_metadata.inputs.empty()) {
            OnnxSession::TensorData blend_input;
            blend_input.name = blend_metadata.inputs.front().name;
            if (blendshape_input_layout_ == InputLayout::Nhwc) {
                blend_input.shape = {1, target_height, target_width, target_channels};
            } else {
                blend_input.shape = {1, target_channels, target_height, target_width};
            }
            blend_input.values = input_tensor.values;
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
                    result[static_cast<std::size_t>(0) * output_pixel_count + pixel_index] = ToNormalizedFloat(red);
                }
                if (target_channels >= 2) {
                    result[static_cast<std::size_t>(1) * output_pixel_count + pixel_index] = ToNormalizedFloat(green);
                }
                if (target_channels >= 3) {
                    result[static_cast<std::size_t>(2) * output_pixel_count + pixel_index] = ToNormalizedFloat(blue);
                }
            }
        }
    }

    return result;
}

FaceResult FaceTracker::ParseFaceOutput(const std::vector<float>& output_values) {
    FaceResult result;
    if (output_values.size() < 3) {
        return result;
    }

    const std::size_t available_points = output_values.size() / 3;
    const std::size_t point_count = std::min<std::size_t>(available_points, result.landmarks.size());
    float sum_x = 0.0f;
    float sum_y = 0.0f;

    for (std::size_t index = 0; index < point_count; ++index) {
        const float x = output_values[index * 3 + 0];
        const float y = output_values[index * 3 + 1];
        const float z = output_values[index * 3 + 2];
        result.landmarks[index] = glm::vec3{x, y, z};
        sum_x += x;
        sum_y += y;
    }

    result.detected = point_count > 0;
    if (result.detected) {
        const float inv_count = 1.0f / static_cast<float>(point_count);
        result.x = sum_x * inv_count;
        result.y = sum_y * inv_count;
        result.yaw = (result.x - 0.5f) * 90.0f;
        result.pitch = (0.5f - result.y) * 90.0f;
        result.roll = 0.0f;
    }

    return result;
}

}  // namespace nohcam
