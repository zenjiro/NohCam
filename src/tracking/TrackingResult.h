#pragma once

#include <array>
#include <chrono>
#include <vector>

#include <glm/vec3.hpp>

namespace nohcam {

struct FaceResult {
    bool detected = false;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    std::array<glm::vec3, 478> landmarks{};
    std::vector<float> blendshapes;
};
struct HandResult {
    bool detected = false;
    bool is_left = false;
    float score = 0.0f;
    std::array<glm::vec3, 21> landmarks{};
    float wrist_pitch = 0.0f;
    float wrist_yaw = 0.0f;
    float wrist_roll = 0.0f;
    std::array<float, 5> mcp_flexion{};
    std::array<float, 5> pip_flexion{};
    std::array<float, 5> dip_flexion{};
};

struct PoseResult {
    bool detected = false;
    float score = 0.0f;
    // 33 standard MediaPipe pose landmarks
    std::array<glm::vec3, 33> landmarks{};
    // Visibility and presence for each landmark (33 each)
    std::array<float, 33> visibility{};
    std::array<float, 33> presence{};
};

struct TrackingResult {
    FaceResult face;
    HandResult left_hand;
    HandResult right_hand;
    PoseResult pose;
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
};

}  // namespace nohcam
