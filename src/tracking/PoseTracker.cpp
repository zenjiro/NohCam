#include "tracking/PoseTracker.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem>

namespace nohcam {

PoseTracker::PoseTracker() : initialized_(false) {}
PoseTracker::~PoseTracker() = default;

bool PoseTracker::Initialize(std::string* error_message) {
    std::string local_error;
    const bool det_ok = LoadDetectionModel(&local_error);
    const bool lm_ok = LoadLandmarkModel(&local_error);

    initialized_ = det_ok && lm_ok;
    init_error_ = local_error;

    if (error_message) {
        *error_message = init_error_;
    }
    return initialized_;
}

bool PoseTracker::LoadDetectionModel(std::string* error) {
    detection_session_ = std::make_unique<OnnxSession>();
    const std::filesystem::path model_path = std::filesystem::path(OnnxSession::GetDefaultModelDirectory()) / L"pose_detection.onnx";
    if (!detection_session_->LoadModel(model_path, error)) {
        return false;
    }
    return true;
}

bool PoseTracker::LoadLandmarkModel(std::string* error) {
    landmark_session_ = std::make_unique<OnnxSession>();
    const std::filesystem::path model_path = std::filesystem::path(OnnxSession::GetDefaultModelDirectory()) / L"pose_landmark_full.onnx";
    if (!landmark_session_->LoadModel(model_path, error)) {
        return false;
    }
    return true;
}

PoseResult PoseTracker::Track(const CameraCapture::CaptureFrame& capture_frame) {
    if (!initialized_) return {};

    PoseRoi roi = DetectPose(capture_frame);

    if (roi.size.x <= 0 || roi.size.y <= 0) {
        return {};
    }

    PoseResult result = ExtractLandmarks(capture_frame, roi);
    return result;
}

PoseTracker::PoseRoi PoseTracker::DetectPose(const CameraCapture::CaptureFrame& frame) {
    const int input_size = 224;
    std::vector<float> input_values(1 * 3 * input_size * input_size, 0.0f);

    float scale = std::min((float)input_size / frame.width, (float)input_size / frame.height);
    int dx = (input_size - (int)(frame.width * scale)) / 2;
    int dy = (input_size - (int)(frame.height * scale)) / 2;

    const std::size_t total_pixels = frame.pixels.size();
    
    for (int y = 0; y < input_size; ++y) {
        for (int x = 0; x < input_size; ++x) {
            int src_x = (int)((x - dx) / scale);
            int src_y = (int)((y - dy) / scale);

            if (src_x >= 0 && src_x < (int)frame.width && src_y >= 0 && src_y < (int)frame.height) {
                const std::size_t offset = static_cast<std::size_t>(src_y) * frame.stride + static_cast<std::size_t>(src_x) * 4;
                if (offset + 2 < total_pixels) {
                    const uint8_t* p = &frame.pixels[offset];
                    // OpenCV Zoo model expects BGR [0, 255]
                    input_values[0 * input_size * input_size + y * input_size + x] = static_cast<float>(p[0]); // B
                    input_values[1 * input_size * input_size + y * input_size + x] = static_cast<float>(p[1]); // G
                    input_values[2 * input_size * input_size + y * input_size + x] = static_cast<float>(p[2]); // R
                }
            }
        }
    }

    const auto& meta = detection_session_->GetMetadata();
    if (meta.inputs.empty()) return {};

    OnnxSession::TensorData input;
    input.name = meta.inputs.front().name;
    input.shape = {1, 3, input_size, input_size};
    input.values = std::move(input_values);

    auto outputs = detection_session_->Run({input});
    if (outputs.size() < 2) return {};

    const float detection_threshold = 0.25f;
    const float* scores = outputs[0].values.data();
    const float* bboxes = outputs[1].values.data();
    const int num_anchors = static_cast<int>(outputs[0].values.size());
    
    int best_idx = -1;
    float best_logit = -1e10f;

    for (int i = 0; i < num_anchors; ++i) {
        if (scores[i] > best_logit) {
            best_logit = scores[i];
            best_idx = i;
        }
    }

    float final_score = 1.0f / (1.0f + std::exp(-best_logit));
    
    static int log_cnt = 0;
    if (log_cnt++ % 60 == 0) {
        spdlog::info("Pose Detection: Best logit={:.2f}, Score={:.2f}, Idx={}", best_logit, final_score, best_idx);
    }

    if (final_score < detection_threshold) return {};

    float cx = bboxes[best_idx * 12 + 0];
    float cy = bboxes[best_idx * 12 + 1];
    float w = bboxes[best_idx * 12 + 2];
    float h = bboxes[best_idx * 12 + 3];

    cx = (cx * input_size - dx) / scale / frame.width;
    cy = (cy * input_size - dy) / scale / frame.height;
    w = (w * input_size) / scale / frame.width;
    h = (h * input_size) / scale / frame.height;

    float size = std::max(w, h) * 1.5f;
    return {glm::vec2(cx, cy), glm::vec2(size, size), 0.0f};
}

PoseResult PoseTracker::ExtractLandmarks(const CameraCapture::CaptureFrame& frame, const PoseRoi& roi) {
    const int input_size = 256;
    std::vector<float> input_values(1 * input_size * input_size * 3, 0.0f);

    const std::size_t total_pixels = frame.pixels.size();

    for (int y = 0; y < input_size; ++y) {
        for (int x = 0; x < input_size; ++x) {
            float src_xf = (x / (float)input_size - 0.5f) * roi.size.x + roi.center.x;
            float src_yf = (y / (float)input_size - 0.5f) * roi.size.y + roi.center.y;
            int src_x = (int)(src_xf * frame.width);
            int src_y = (int)(src_yf * frame.height);

            if (src_x >= 0 && src_x < (int)frame.width && src_y >= 0 && src_y < (int)frame.height) {
                const std::size_t offset = static_cast<std::size_t>(src_y) * frame.stride + static_cast<std::size_t>(src_x) * 4;
                if (offset + 2 < total_pixels) {
                    const uint8_t* p = &frame.pixels[offset];
                    // NHWC: [batch, height, width, channels]
                    int base = (y * input_size + x) * 3;
                    input_values[base + 0] = p[2] / 255.0f; // R
                    input_values[base + 1] = p[1] / 255.0f; // G
                    input_values[base + 2] = p[0] / 255.0f; // B
                }
            }
        }
    }

    const auto& meta = landmark_session_->GetMetadata();
    if (meta.inputs.empty()) return {};

    OnnxSession::TensorData input;
    input.name = meta.inputs.front().name;
    input.shape = {1, input_size, input_size, 3};
    input.values = std::move(input_values);

    auto outputs = landmark_session_->Run({input});
    if (outputs.size() < 2) return {};

    static int log_cnt = 0;

    const float landmark_threshold = 0.25f;
    const float score = outputs[1].values[0];
    
    if (log_cnt % 60 == 0) {
        spdlog::info("Pose Landmarks: Score={:.2f}, Threshold={:.2f}", score, landmark_threshold);
    }

    if (score < landmark_threshold) return {};

    PoseResult result;
    result.detected = true;
    result.score = score;

    const float* ld2d = outputs[0].values.data();
    for (int i = 0; i < 33; ++i) {
        float x = ld2d[i * 5 + 0] / input_size;
        float y = ld2d[i * 5 + 1] / input_size;
        float z = ld2d[i * 5 + 2] / input_size;

        result.landmarks[i] = glm::vec3(
            (x - 0.5f) * roi.size.x + roi.center.x,
            (y - 0.5f) * roi.size.y + roi.center.y,
            z * roi.size.x 
        );
        result.visibility[i] = ld2d[i * 5 + 3];
        result.presence[i] = ld2d[i * 5 + 4];
    }

    return result;
}

}  // namespace nohcam
