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
    std::array<glm::vec3, 21> landmarks{};
};

struct TrackingResult {
    FaceResult face;
    HandResult left_hand;
    HandResult right_hand;
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
};

}  // namespace nohcam
