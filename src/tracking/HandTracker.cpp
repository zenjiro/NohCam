#include "tracking/HandTracker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>

#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>

namespace nohcam {

namespace {

constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr float kHandScoreThreshold = 0.5f;
constexpr float kPalmScoreThreshold = 0.05f;
constexpr float kPalmNmsIouThreshold = 0.35f;
constexpr int kHandModelInputSize = 224;

HandTracker::InputLayout DetectInputLayout(const std::vector<int64_t>& shape) {
    if (shape.size() != 4) {
        return HandTracker::InputLayout::Unknown;
    }
    if (shape[1] == 3) {
        return HandTracker::InputLayout::Nchw;
    }
    if (shape[3] == 3) {
        return HandTracker::InputLayout::Nhwc;
    }
    return HandTracker::InputLayout::Unknown;
}

bool GetInputShape(const std::vector<int64_t>& shape, HandTracker::InputLayout layout, int& channels, int& height, int& width) {
    if (shape.size() != 4) {
        return false;
    }
    if (layout == HandTracker::InputLayout::Nchw) {
        channels = shape[1] > 0 ? static_cast<int>(shape[1]) : 0;
        height = shape[2] > 0 ? static_cast<int>(shape[2]) : 0;
        width = shape[3] > 0 ? static_cast<int>(shape[3]) : 0;
        return true;
    }
    if (layout == HandTracker::InputLayout::Nhwc) {
        height = shape[1] > 0 ? static_cast<int>(shape[1]) : 0;
        width = shape[2] > 0 ? static_cast<int>(shape[2]) : 0;
        channels = shape[3] > 0 ? static_cast<int>(shape[3]) : 0;
        return true;
    }
    return false;
}

float ToDegrees(float radians) {
    return radians * kRadiansToDegrees;
}

float SafeAngleDegrees(const glm::vec3& a, const glm::vec3& b) {
    const float len_a = glm::length(a);
    const float len_b = glm::length(b);
    if (len_a < 1e-6f || len_b < 1e-6f) {
        return 0.0f;
    }
    float cosine = glm::dot(a / len_a, b / len_b);
    cosine = std::clamp(cosine, -1.0f, 1.0f);
    return ToDegrees(std::acos(cosine));
}

HandResult MakeHandResultFromFlat63(const std::vector<float>& values) {
    HandResult hand;
    if (values.size() < 63) {
        return hand;
    }
    hand.detected = true;
    for (std::size_t i = 0; i < 21; ++i) {
        hand.landmarks[i] = glm::vec3(values[i * 3 + 0], values[i * 3 + 1], values[i * 3 + 2]);
    }
    return hand;
}

glm::vec2 MeanPoint(const std::array<glm::vec3, 21>& pts) {
    glm::vec2 acc(0.0f, 0.0f);
    for (const auto& p : pts) {
        acc += glm::vec2(p.x, p.y);
    }
    return acc / 21.0f;
}

}  // namespace

HandTracker::HandTracker() = default;

bool HandTracker::Initialize(std::string* error_message) {
    std::string local_error;
    const bool palm_loaded = LoadPalmModel(&local_error);
    const bool hand_loaded = LoadHandModel(&local_error);
    initialized_ = palm_loaded && hand_loaded;

    init_error_ = std::move(local_error);
    if (error_message) {
        *error_message = init_error_;
    }
    return initialized_;
}

bool HandTracker::IsInitialized() const {
    return initialized_;
}

const std::string& HandTracker::GetInitializeError() const {
    return init_error_;
}

const HandTrackingFrameResult& HandTracker::GetLastResult() const {
    return last_result_;
}

HandTrackingFrameResult HandTracker::Track(const CameraCapture::CaptureFrame& capture_frame) {
    last_result_ = HandTrackingFrameResult{};
    if (!initialized_ || !capture_frame.valid || !palm_session_.has_value() || !hand_session_.has_value()) {
        return last_result_;
    }

    auto palms = RunPalmInference(capture_frame);
    palms = ApplyPalmNms(palms, 2);
    if (palms.size() < 2 && !palms.empty()) {
        const auto masked = MaskPalmRegion(capture_frame, palms.front());
        auto extra = RunPalmInference(masked);
        for (const auto& det : extra) {
            bool overlaps = false;
            for (const auto& existing : palms) {
                if (PalmIoU(det, existing) > 0.2f) {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps) {
                palms.push_back(det);
            }
        }
        palms = ApplyPalmNms(palms, 2);
    }
    if (palms.size() < 2) {
        const auto flipped = FlipHorizontal(capture_frame);
        auto extra = RunPalmInference(flipped);
        for (auto det : extra) {
            det.cx = 1.0f - det.cx;
            bool overlaps = false;
            for (const auto& existing : palms) {
                if (PalmIoU(det, existing) > 0.2f) {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps) {
                palms.push_back(det);
            }
        }
        palms = ApplyPalmNms(palms, 2);
    }
    if (palms.empty()) {
        has_prev_left_ = false;
        has_prev_right_ = false;
        return last_result_;
    }

    std::vector<ParsedHandCandidate> candidates;
    candidates.reserve(palms.size());
    for (const auto& palm : palms) {
        const auto roi = BuildRoi(palm, capture_frame.width, capture_frame.height);
        auto candidate = RunLandmarkOnRoi(capture_frame, roi);
        if (candidate.has_value() && candidate->hand.detected) {
            candidates.push_back(*candidate);
        }
    }

    if (candidates.empty()) {
        has_prev_left_ = false;
        has_prev_right_ = false;
        return last_result_;
    }

    auto assigned = AssignHandsStable(candidates);
    last_result_.left_hand = assigned.first;
    last_result_.right_hand = assigned.second;
    if (last_result_.left_hand.detected) {
        FillJointAngles(last_result_.left_hand);
    }
    if (last_result_.right_hand.detected) {
        FillJointAngles(last_result_.right_hand);
    }
    return last_result_;
}

bool HandTracker::LoadPalmModel(std::string* error_message) {
    const std::filesystem::path model_path = std::filesystem::path(OnnxSession::GetDefaultModelDirectory()) / L"palm_detection.onnx";
    if (!std::filesystem::exists(model_path)) {
        if (error_message) {
            *error_message += "palm_detection.onnx is missing from the model directory. ";
        }
        return false;
    }

    palm_session_.emplace();
    if (!palm_session_->LoadModel(model_path, error_message)) {
        palm_session_.reset();
        return false;
    }

    const auto& metadata = palm_session_->GetMetadata();
    if (metadata.inputs.empty()) {
        if (error_message) {
            *error_message += "palm_detection.onnx has no input metadata. ";
        }
        palm_session_.reset();
        return false;
    }
    if (!IsPalmOutputShapeSupported(metadata.outputs)) {
        if (error_message) {
            *error_message += "palm_detection.onnx output shape is unsupported. ";
        }
        palm_session_.reset();
        return false;
    }

    palm_input_layout_ = DetectInputLayout(metadata.inputs.front().shape);
    if (palm_input_layout_ == InputLayout::Unknown) {
        palm_input_layout_ = InputLayout::Nchw;
    }
    GetInputShape(metadata.inputs.front().shape, palm_input_layout_, palm_input_channels_, palm_input_height_, palm_input_width_);
    spdlog::info("Loaded palm detection model: {}", std::filesystem::path(metadata.model_path).string());
    return true;
}

bool HandTracker::LoadHandModel(std::string* error_message) {
    const std::filesystem::path model_path = std::filesystem::path(OnnxSession::GetDefaultModelDirectory()) / L"hand_landmarks.onnx";
    if (!std::filesystem::exists(model_path)) {
        if (error_message) {
            *error_message += "hand_landmarks.onnx is missing from the model directory. ";
        }
        return false;
    }

    hand_session_.emplace();
    if (!hand_session_->LoadModel(model_path, error_message)) {
        hand_session_.reset();
        return false;
    }
    const auto& metadata = hand_session_->GetMetadata();
    if (metadata.inputs.empty()) {
        if (error_message) {
            *error_message += "hand_landmarks.onnx has no input metadata. ";
        }
        hand_session_.reset();
        return false;
    }
    if (!IsHandOutputShapeSupported(metadata.outputs)) {
        if (error_message) {
            *error_message += "hand_landmarks.onnx output shape is unsupported. ";
        }
        hand_session_.reset();
        return false;
    }

    hand_input_layout_ = DetectInputLayout(metadata.inputs.front().shape);
    if (hand_input_layout_ == InputLayout::Unknown) {
        hand_input_layout_ = InputLayout::Nchw;
    }
    GetInputShape(metadata.inputs.front().shape, hand_input_layout_, hand_input_channels_, hand_input_height_, hand_input_width_);
    spdlog::info("Loaded hand landmark model: {}", std::filesystem::path(metadata.model_path).string());
    return true;
}

std::vector<float> HandTracker::PreprocessFrame(
    const CameraCapture::CaptureFrame& frame,
    int target_width,
    int target_height,
    int target_channels,
    InputLayout layout) {
    if (!frame.valid || frame.width == 0 || frame.height == 0 || frame.pixels.empty()) {
        return {};
    }
    if (target_width <= 0) target_width = static_cast<int>(frame.width);
    if (target_height <= 0) target_height = static_cast<int>(frame.height);
    if (target_channels <= 0) target_channels = 3;
    if (layout == InputLayout::Unknown) layout = InputLayout::Nchw;

    const std::uint32_t src_w = frame.width;
    const std::uint32_t src_h = frame.height;
    const std::uint32_t src_stride = frame.stride;
    const std::uint32_t out_px = static_cast<std::uint32_t>(target_width) * static_cast<std::uint32_t>(target_height);
    std::vector<float> out(static_cast<std::size_t>(out_px) * target_channels, 0.0f);

    for (int y = 0; y < target_height; ++y) {
        const std::uint32_t sy = std::min<std::uint32_t>(src_h - 1, static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * src_h) / target_height));
        for (int x = 0; x < target_width; ++x) {
            const std::uint32_t sx = std::min<std::uint32_t>(src_w - 1, static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * src_w) / target_width));
            const std::size_t so = static_cast<std::size_t>(sy) * src_stride + static_cast<std::size_t>(sx) * 4;
            const float b = static_cast<float>(frame.pixels[so + 0]);
            const float g = static_cast<float>(frame.pixels[so + 1]);
            const float r = static_cast<float>(frame.pixels[so + 2]);

            if (layout == InputLayout::Nhwc) {
                const std::size_t i = (static_cast<std::size_t>(y) * target_width + static_cast<std::size_t>(x)) * target_channels;
                if (target_channels >= 1) out[i + 0] = r;
                if (target_channels >= 2) out[i + 1] = g;
                if (target_channels >= 3) out[i + 2] = b;
            } else {
                const std::size_t pi = static_cast<std::size_t>(y) * target_width + static_cast<std::size_t>(x);
                if (target_channels >= 1) out[0 * out_px + pi] = b;
                if (target_channels >= 2) out[1 * out_px + pi] = g;
                if (target_channels >= 3) out[2 * out_px + pi] = r;
            }
        }
    }
    return out;
}

std::vector<HandTracker::PalmDetection> HandTracker::ParsePalmDetections(const std::vector<OnnxSession::TensorData>& outputs) {
    const OnnxSession::TensorData* det_tensor = nullptr;
    const OnnxSession::TensorData* batch_tensor = nullptr;
    for (const auto& out : outputs) {
        if (out.values.size() >= 8 && out.values.size() % 8 == 0) {
            det_tensor = &out;
        } else if (!out.values.empty() && out.values.size() <= 64) {
            batch_tensor = &out;
        }
    }
    if (!det_tensor) {
        return {};
    }

    const std::size_t count = det_tensor->values.size() / 8;
    std::vector<PalmDetection> result;
    result.reserve(count);
    float max_score_seen = -1.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float score = det_tensor->values[i * 8 + 0];
        max_score_seen = std::max(max_score_seen, score);
        if (score < kPalmScoreThreshold) {
            continue;
        }
        if (batch_tensor && i < batch_tensor->values.size() && batch_tensor->values[i] < 0.0f) {
            continue;
        }

        float cx = det_tensor->values[i * 8 + 1];
        float cy = det_tensor->values[i * 8 + 2];
        float w = det_tensor->values[i * 8 + 3];
        float wx = det_tensor->values[i * 8 + 4];
        float wy = det_tensor->values[i * 8 + 5];
        float mx = det_tensor->values[i * 8 + 6];
        float my = det_tensor->values[i * 8 + 7];

        // If normalized values are not used, convert from input pixels to normalized.
        if (std::abs(cx) > 2.0f || std::abs(cy) > 2.0f || std::abs(w) > 2.0f) {
            cx /= 192.0f;
            cy /= 192.0f;
            w /= 192.0f;
            wx /= 192.0f;
            wy /= 192.0f;
            mx /= 192.0f;
            my /= 192.0f;
        }

        PalmDetection palm;
        palm.score = score;
        palm.cx = cx;
        palm.cy = cy;
        palm.size = std::max(0.04f, w);
        palm.angle_radians = std::atan2(my - wy, mx - wx);
        result.push_back(palm);
    }
    static int palm_log_counter = 0;
    if (palm_log_counter++ < 20) {
        spdlog::info("Palm parse: rows={} max_score={} kept={}", count, max_score_seen, result.size());
    }
    return result;
}

std::vector<HandTracker::PalmDetection> HandTracker::ApplyPalmNms(const std::vector<PalmDetection>& detections, std::size_t max_count) {
    std::vector<PalmDetection> sorted = detections;
    std::sort(sorted.begin(), sorted.end(), [](const PalmDetection& a, const PalmDetection& b) {
        return a.score > b.score;
    });

    std::vector<PalmDetection> selected;
    for (const auto& det : sorted) {
        bool overlaps = false;
        for (const auto& kept : selected) {
            if (PalmIoU(det, kept) > kPalmNmsIouThreshold) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) {
            selected.push_back(det);
            if (selected.size() >= max_count) {
                break;
            }
        }
    }
    return selected;
}

float HandTracker::PalmIoU(const PalmDetection& a, const PalmDetection& b) {
    const float a_half = a.size * 0.5f;
    const float b_half = b.size * 0.5f;
    const float ax1 = a.cx - a_half;
    const float ay1 = a.cy - a_half;
    const float ax2 = a.cx + a_half;
    const float ay2 = a.cy + a_half;
    const float bx1 = b.cx - b_half;
    const float by1 = b.cy - b_half;
    const float bx2 = b.cx + b_half;
    const float by2 = b.cy + b_half;

    const float ix1 = std::max(ax1, bx1);
    const float iy1 = std::max(ay1, by1);
    const float ix2 = std::min(ax2, bx2);
    const float iy2 = std::min(ay2, by2);
    const float iw = std::max(0.0f, ix2 - ix1);
    const float ih = std::max(0.0f, iy2 - iy1);
    const float inter = iw * ih;
    const float area_a = std::max(0.0f, ax2 - ax1) * std::max(0.0f, ay2 - ay1);
    const float area_b = std::max(0.0f, bx2 - bx1) * std::max(0.0f, by2 - by1);
    const float uni = area_a + area_b - inter;
    return uni > 1e-6f ? inter / uni : 0.0f;
}

HandTracker::RoiTransform HandTracker::BuildRoi(const PalmDetection& palm, std::uint32_t width, std::uint32_t height) {
    RoiTransform roi;
    const float px_w = static_cast<float>(width);
    const float px_h = static_cast<float>(height);
    const float side_px = std::max(64.0f, palm.size * std::min(px_w, px_h) * 2.4f);
    const float center_x = palm.cx * px_w;
    const float center_y = palm.cy * px_h;
    roi.size = side_px;
    roi.left = center_x - side_px * 0.5f;
    roi.top = center_y - side_px * 0.5f;
    return roi;
}

CameraCapture::CaptureFrame HandTracker::CropToRoi(const CameraCapture::CaptureFrame& frame, const RoiTransform& roi, int target_size) {
    CameraCapture::CaptureFrame cropped;
    cropped.valid = frame.valid;
    cropped.width = static_cast<std::uint32_t>(target_size);
    cropped.height = static_cast<std::uint32_t>(target_size);
    cropped.stride = static_cast<std::uint32_t>(target_size * 4);
    cropped.frame_count = frame.frame_count;
    cropped.timestamp_hns = frame.timestamp_hns;
    cropped.pixels.resize(static_cast<std::size_t>(cropped.stride) * cropped.height, 0);

    for (int y = 0; y < target_size; ++y) {
        for (int x = 0; x < target_size; ++x) {
            const float fx = roi.left + ((x + 0.5f) / target_size) * roi.size;
            const float fy = roi.top + ((y + 0.5f) / target_size) * roi.size;
            const int sx = std::clamp(static_cast<int>(std::round(fx)), 0, static_cast<int>(frame.width) - 1);
            const int sy = std::clamp(static_cast<int>(std::round(fy)), 0, static_cast<int>(frame.height) - 1);
            const std::size_t src = static_cast<std::size_t>(sy) * frame.stride + static_cast<std::size_t>(sx) * 4;
            const std::size_t dst = static_cast<std::size_t>(y) * cropped.stride + static_cast<std::size_t>(x) * 4;
            cropped.pixels[dst + 0] = frame.pixels[src + 0];
            cropped.pixels[dst + 1] = frame.pixels[src + 1];
            cropped.pixels[dst + 2] = frame.pixels[src + 2];
            cropped.pixels[dst + 3] = 255;
        }
    }
    return cropped;
}

std::vector<HandTracker::PalmDetection> HandTracker::RunPalmInference(const CameraCapture::CaptureFrame& capture_frame) {
    const auto& palm_meta = palm_session_->GetMetadata();
    if (palm_meta.inputs.empty()) {
        return {};
    }

    const int palm_w = palm_input_width_ > 0 ? palm_input_width_ : 192;
    const int palm_h = palm_input_height_ > 0 ? palm_input_height_ : 192;
    const int palm_c = palm_input_channels_ > 0 ? palm_input_channels_ : 3;
    auto palm_input_vals = PreprocessFrame(capture_frame, palm_w, palm_h, palm_c, palm_input_layout_);
    if (palm_input_vals.empty()) {
        return {};
    }

    OnnxSession::TensorData palm_input;
    palm_input.name = palm_meta.inputs.front().name;
    if (palm_input_layout_ == InputLayout::Nhwc) {
        palm_input.shape = {1, palm_h, palm_w, palm_c};
    } else {
        palm_input.shape = {1, palm_c, palm_h, palm_w};
    }
    palm_input.values = std::move(palm_input_vals);
    auto palm_outputs = palm_session_->Run({palm_input});
    return ParsePalmDetections(palm_outputs);
}

CameraCapture::CaptureFrame HandTracker::MaskPalmRegion(const CameraCapture::CaptureFrame& frame, const PalmDetection& palm) {
    CameraCapture::CaptureFrame masked = frame;
    const float px_w = static_cast<float>(frame.width);
    const float px_h = static_cast<float>(frame.height);
    const float side = std::max(32.0f, palm.size * std::min(px_w, px_h) * 2.0f);
    const int left = std::clamp(static_cast<int>(palm.cx * px_w - side * 0.5f), 0, static_cast<int>(frame.width) - 1);
    const int top = std::clamp(static_cast<int>(palm.cy * px_h - side * 0.5f), 0, static_cast<int>(frame.height) - 1);
    const int right = std::clamp(static_cast<int>(palm.cx * px_w + side * 0.5f), 0, static_cast<int>(frame.width) - 1);
    const int bottom = std::clamp(static_cast<int>(palm.cy * px_h + side * 0.5f), 0, static_cast<int>(frame.height) - 1);

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * masked.stride + static_cast<std::size_t>(x) * 4;
            masked.pixels[idx + 0] = 0;
            masked.pixels[idx + 1] = 0;
            masked.pixels[idx + 2] = 0;
        }
    }
    return masked;
}

CameraCapture::CaptureFrame HandTracker::FlipHorizontal(const CameraCapture::CaptureFrame& frame) {
    CameraCapture::CaptureFrame flipped = frame;
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const std::uint32_t fx = frame.width - 1 - x;
            const std::size_t src = static_cast<std::size_t>(y) * frame.stride + static_cast<std::size_t>(x) * 4;
            const std::size_t dst = static_cast<std::size_t>(y) * flipped.stride + static_cast<std::size_t>(fx) * 4;
            flipped.pixels[dst + 0] = frame.pixels[src + 0];
            flipped.pixels[dst + 1] = frame.pixels[src + 1];
            flipped.pixels[dst + 2] = frame.pixels[src + 2];
            flipped.pixels[dst + 3] = frame.pixels[src + 3];
        }
    }
    return flipped;
}

std::optional<HandTracker::ParsedHandCandidate> HandTracker::RunLandmarkOnRoi(const CameraCapture::CaptureFrame& capture_frame, const RoiTransform& roi) {
    const auto roi_frame = CropToRoi(capture_frame, roi, kHandModelInputSize);
    const auto& meta = hand_session_->GetMetadata();
    if (meta.inputs.empty()) {
        return std::nullopt;
    }

    auto vals = PreprocessFrame(roi_frame, hand_input_width_ > 0 ? hand_input_width_ : kHandModelInputSize,
        hand_input_height_ > 0 ? hand_input_height_ : kHandModelInputSize,
        hand_input_channels_ > 0 ? hand_input_channels_ : 3, hand_input_layout_);
    if (vals.empty()) {
        return std::nullopt;
    }

    OnnxSession::TensorData in;
    in.name = meta.inputs.front().name;
    if (hand_input_layout_ == InputLayout::Nhwc) {
        in.shape = {1, hand_input_height_ > 0 ? hand_input_height_ : kHandModelInputSize,
            hand_input_width_ > 0 ? hand_input_width_ : kHandModelInputSize,
            hand_input_channels_ > 0 ? hand_input_channels_ : 3};
    } else {
        in.shape = {1, hand_input_channels_ > 0 ? hand_input_channels_ : 3,
            hand_input_height_ > 0 ? hand_input_height_ : kHandModelInputSize,
            hand_input_width_ > 0 ? hand_input_width_ : kHandModelInputSize};
    }
    in.values = std::move(vals);

    auto outs = hand_session_->Run({in});
    const OnnxSession::TensorData* xyz = nullptr;
    const OnnxSession::TensorData* score = nullptr;
    const OnnxSession::TensorData* lr = nullptr;
    for (const auto& out : outs) {
        if (out.values.size() == 63) xyz = &out;
        else if (out.values.size() == 1 && out.name.find("hand_score") != std::string::npos) score = &out;
        else if (out.values.size() == 1 && out.name.find("lefthand_0_or_righthand_1") != std::string::npos) lr = &out;
    }
    if (!xyz) {
        return std::nullopt;
    }

    ParsedHandCandidate cand;
    cand.hand = MakeHandResultFromFlat63(xyz->values);
    cand.hand_score = score ? score->values[0] : 1.0f;
    if (lr) {
        cand.handedness_left_score = 1.0f - std::clamp(lr->values[0], 0.0f, 1.0f);
    }
    cand.hand.detected = cand.hand.detected && cand.hand_score >= kHandScoreThreshold;
    if (!cand.hand.detected) {
        return std::nullopt;
    }

    // Convert either normalized or pixel-space model output into [0,1] crop coordinates.
    float max_abs = 0.0f;
    for (std::size_t i = 0; i < 21; ++i) {
        max_abs = std::max(max_abs, std::abs(cand.hand.landmarks[i].x));
        max_abs = std::max(max_abs, std::abs(cand.hand.landmarks[i].y));
    }
    const bool pixel_space = max_abs > 2.0f;
    for (std::size_t i = 0; i < 21; ++i) {
        if (pixel_space) {
            cand.hand.landmarks[i].x /= static_cast<float>(kHandModelInputSize);
            cand.hand.landmarks[i].y /= static_cast<float>(kHandModelInputSize);
            cand.hand.landmarks[i].z /= static_cast<float>(kHandModelInputSize);
        }
    }

    ParsedHandCandidate mapped = MapLandmarksToFrame(cand, roi);
    return mapped;
}

HandTracker::ParsedHandCandidate HandTracker::MapLandmarksToFrame(const ParsedHandCandidate& local_result, const RoiTransform& roi) {
    ParsedHandCandidate mapped = local_result;
    for (std::size_t i = 0; i < 21; ++i) {
        mapped.hand.landmarks[i].x = roi.left + local_result.hand.landmarks[i].x * roi.size;
        mapped.hand.landmarks[i].y = roi.top + local_result.hand.landmarks[i].y * roi.size;
        mapped.hand.landmarks[i].z = local_result.hand.landmarks[i].z * roi.size;
    }
    mapped.wrist = glm::vec2(mapped.hand.landmarks[0].x, mapped.hand.landmarks[0].y);
    return mapped;
}

std::pair<HandResult, HandResult> HandTracker::AssignHandsStable(const std::vector<ParsedHandCandidate>& candidates) {
    HandResult left;
    HandResult right;
    if (candidates.empty()) {
        return {left, right};
    }

    auto choose_closest = [&](const glm::vec2& ref, const std::vector<int>& idxs) -> int {
        int best = -1;
        float best_dist = std::numeric_limits<float>::max();
        for (int idx : idxs) {
            const float d = glm::distance(ref, candidates[idx].wrist);
            if (d < best_dist) {
                best_dist = d;
                best = idx;
            }
        }
        return best;
    };

    std::vector<int> left_pref;
    std::vector<int> right_pref;
    std::vector<int> unknown;
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        if (candidates[i].handedness_left_score >= 0.6f) left_pref.push_back(i);
        else if (candidates[i].handedness_left_score >= 0.0f && candidates[i].handedness_left_score <= 0.4f) right_pref.push_back(i);
        else unknown.push_back(i);
    }

    int left_idx = -1;
    int right_idx = -1;

    if (has_prev_left_) {
        std::vector<int> pool = left_pref;
        pool.insert(pool.end(), unknown.begin(), unknown.end());
        if (!pool.empty()) left_idx = choose_closest(prev_left_wrist_, pool);
    }
    if (left_idx < 0 && !left_pref.empty()) left_idx = left_pref[0];

    if (has_prev_right_) {
        std::vector<int> pool = right_pref;
        pool.insert(pool.end(), unknown.begin(), unknown.end());
        if (!pool.empty()) right_idx = choose_closest(prev_right_wrist_, pool);
    }
    if (right_idx < 0 && !right_pref.empty()) right_idx = right_pref[0];

    if (left_idx == right_idx && left_idx >= 0) {
        right_idx = -1;
    }

    if (left_idx < 0 || right_idx < 0) {
        std::vector<int> remaining;
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
            if (i != left_idx && i != right_idx) remaining.push_back(i);
        }
        if (left_idx < 0 && !remaining.empty()) {
            left_idx = remaining[0];
            remaining.erase(remaining.begin());
        }
        if (right_idx < 0 && !remaining.empty()) {
            right_idx = remaining[0];
        }
    }

    if (left_idx >= 0) {
        left = candidates[left_idx].hand;
        has_prev_left_ = true;
        prev_left_wrist_ = candidates[left_idx].wrist;
    } else {
        has_prev_left_ = false;
    }
    if (right_idx >= 0) {
        right = candidates[right_idx].hand;
        has_prev_right_ = true;
        prev_right_wrist_ = candidates[right_idx].wrist;
    } else {
        has_prev_right_ = false;
    }

    // Final fallback: ensure deterministic ordering for unknown handedness.
    if (!left.detected && !right.detected && !candidates.empty()) {
        auto sorted = candidates;
        std::sort(sorted.begin(), sorted.end(), [](const ParsedHandCandidate& a, const ParsedHandCandidate& b) {
            return MeanPoint(a.hand.landmarks).x > MeanPoint(b.hand.landmarks).x;
        });
        left = sorted[0].hand;
        if (sorted.size() > 1) right = sorted[1].hand;
    }

    return {left, right};
}

void HandTracker::FillJointAngles(HandResult& hand) {
    if (!hand.detected) return;

    const glm::vec3 wrist = hand.landmarks[0];
    const glm::vec3 index_mcp = hand.landmarks[5];
    const glm::vec3 pinky_mcp = hand.landmarks[17];
    const glm::vec3 palm_center = (index_mcp + pinky_mcp) * 0.5f;

    glm::vec3 y_axis = palm_center - wrist;
    glm::vec3 x_axis = index_mcp - pinky_mcp;
    if (glm::length(y_axis) > 1e-6f) y_axis = glm::normalize(y_axis);
    if (glm::length(x_axis) > 1e-6f) x_axis = glm::normalize(x_axis);

    hand.wrist_pitch = ToDegrees(std::atan2(y_axis.z, std::sqrt(y_axis.x * y_axis.x + y_axis.y * y_axis.y)));
    hand.wrist_yaw = ToDegrees(std::atan2(y_axis.x, y_axis.y));
    hand.wrist_roll = ToDegrees(std::atan2(x_axis.y, x_axis.x));

    hand.mcp_flexion[0] = SafeAngleDegrees(hand.landmarks[1] - hand.landmarks[2], hand.landmarks[3] - hand.landmarks[2]);
    hand.pip_flexion[0] = SafeAngleDegrees(hand.landmarks[2] - hand.landmarks[3], hand.landmarks[4] - hand.landmarks[3]);
    hand.dip_flexion[0] = hand.pip_flexion[0];

    const std::array<std::array<int, 4>, 4> chains = {{{{0, 5, 6, 7}}, {{0, 9, 10, 11}}, {{0, 13, 14, 15}}, {{0, 17, 18, 19}}}};
    for (std::size_t i = 0; i < chains.size(); ++i) {
        const auto& c = chains[i];
        const std::size_t finger = i + 1;
        hand.mcp_flexion[finger] = SafeAngleDegrees(hand.landmarks[c[0]] - hand.landmarks[c[1]], hand.landmarks[c[2]] - hand.landmarks[c[1]]);
        hand.pip_flexion[finger] = SafeAngleDegrees(hand.landmarks[c[1]] - hand.landmarks[c[2]], hand.landmarks[c[3]] - hand.landmarks[c[2]]);
        hand.dip_flexion[finger] = SafeAngleDegrees(hand.landmarks[c[2]] - hand.landmarks[c[3]], hand.landmarks[c[3] + 1] - hand.landmarks[c[3]]);
    }
}

bool HandTracker::IsPalmOutputShapeSupported(const std::vector<OnnxSession::TensorMetadata>& outputs) {
    bool found = false;
    for (const auto& out : outputs) {
        if (out.shape.size() == 2 && (out.shape[1] <= 0 || out.shape[1] == 8 || out.shape[1] == 1)) {
            found = true;
        }
    }
    return found;
}

bool HandTracker::IsHandOutputShapeSupported(const std::vector<OnnxSession::TensorMetadata>& outputs) {
    bool found_xyz = false;
    for (const auto& out : outputs) {
        if (out.shape.size() == 2 && (out.shape[1] <= 0 || out.shape[1] == 63 || out.shape[1] == 1)) {
            if (out.shape[1] == 63 || out.shape[1] <= 0) {
                found_xyz = true;
            }
        }
    }
    return found_xyz;
}

}  // namespace nohcam
