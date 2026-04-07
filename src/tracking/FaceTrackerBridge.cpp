#include "tracking/FaceTracker.h"
#include <memory>
#include <mutex>

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

namespace {
    std::unique_ptr<nohcam::FaceTracker> g_face_tracker;
    std::mutex g_face_tracker_mutex;
}

EXPORT bool FaceTracker_Initialize() {
    std::lock_guard<std::mutex> lock(g_face_tracker_mutex);
    if (!g_face_tracker) {
        g_face_tracker = std::make_unique<nohcam::FaceTracker>();
    }
    return g_face_tracker->Initialize();
}

EXPORT void FaceTracker_Shutdown() {
    std::lock_guard<std::mutex> lock(g_face_tracker_mutex);
    g_face_tracker.reset();
}

EXPORT bool FaceTracker_Track(
    const std::uint8_t* pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t stride,
    bool* detected,
    float* yaw,
    float* pitch,
    float* roll,
    float* x,
    float* y,
    int* blendshape_count,
    float* blendshapes) {

    std::lock_guard<std::mutex> lock(g_face_tracker_mutex);
    if (!g_face_tracker || !g_face_tracker->IsInitialized()) {
        return false;
    }

    nohcam::CameraCapture::CaptureFrame frame;
    frame.valid = true;
    frame.width = width;
    frame.height = height;
    frame.stride = stride;
    frame.pixels.assign(pixels, pixels + (static_cast<std::size_t>(stride) * height));

    auto result = g_face_tracker->Track(frame);

    *detected = result.detected;
    *yaw = result.yaw;
    *pitch = result.pitch;
    *roll = result.roll;
    *x = result.x;
    *y = result.y;
    *blendshape_count = static_cast<int>(result.blendshapes.size());

    if (blendshapes && !result.blendshapes.empty()) {
        std::copy(result.blendshapes.begin(), result.blendshapes.end(), blendshapes);
    }

    return true;
}
