#include "tracking/FaceTracker.h"
#include <memory>
#include <mutex>
#include <cstring>
#include <iostream>
#include <fstream>
#include <Windows.h>

static bool SetDllDirectoryToModulePath() {
    HMODULE hModule = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&SetDllDirectoryToModulePath,
            &hModule)) {
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(hModule, path, MAX_PATH) > 0) {
            wchar_t* slash = wcsrchr(path, L'\\');
            if (slash) {
                *slash = L'\0';
                SetDllDirectoryW(path);
                return true;
            }
        }
    }
    return false;
}

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

namespace {
    std::unique_ptr<nohcam::FaceTracker> g_face_tracker;
    std::mutex g_face_tracker_mutex;
    std::string g_init_error;
}

EXPORT bool FaceTracker_Initialize() {
    SetDllDirectoryToModulePath();
    
    std::ofstream log("D:\\kumano\\github\\NohCam\\face_debug.log", std::ios::trunc);
    log << "FaceTracker_Initialize called" << std::endl;
    log.close();
    
    std::lock_guard<std::mutex> lock(g_face_tracker_mutex);
    if (!g_face_tracker) {
        g_face_tracker = std::make_unique<nohcam::FaceTracker>();
    }
    g_init_error.clear();
    bool success = g_face_tracker->Initialize(&g_init_error);
    
    std::ofstream log2("D:\\kumano\\github\\NohCam\\face_debug.log", std::ios::app);
    log2 << "Initialize result: " << (success ? "OK" : "FAILED") << std::endl;
    log2 << "Error: " << g_init_error << std::endl;
    log2.close();
    
    return success;
}

EXPORT void FaceTracker_Shutdown() {
    std::lock_guard<std::mutex> lock(g_face_tracker_mutex);
    g_face_tracker.reset();
    g_init_error.clear();
}

EXPORT void FaceTracker_GetInitError(char* error_buffer, int buffer_size) {
    if (error_buffer && buffer_size > 0) {
        strncpy_s(error_buffer, buffer_size, g_init_error.c_str(), buffer_size - 1);
        error_buffer[buffer_size - 1] = '\0';
    }
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

    // Debug: check if there's any pixel data
    static int frame_count = 0;
    if (stride > 0 && width > 0 && height > 0 && ++frame_count % 60 == 0) {
        // Calculate average brightness of center region
        int center_x = width / 4;
        int center_y = height / 4;
        int sample_w = width / 2;
        int sample_h = height / 2;
        float sum = 0;
        int count = 0;
        for (int y = center_y; y < center_y + sample_h && y < (int)height; y += 10) {
            for (int x = center_x; x < center_x + sample_w && x < (int)width; x += 10) {
                std::size_t offset = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
                if (offset + 2 < static_cast<std::size_t>(stride) * height) {
                    sum += pixels[offset] + pixels[offset + 1] + pixels[offset + 2];
                    count++;
                }
            }
        }
        float avg = count > 0 ? sum / (count * 3) : 0;
        
        std::ofstream log("D:\\kumano\\github\\NohCam\\face_debug.log", std::ios::app);
        log << "Frame " << width << "x" << height << " avg brightness: " << avg << " detected=" << *detected << std::endl;
        log.close();
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
