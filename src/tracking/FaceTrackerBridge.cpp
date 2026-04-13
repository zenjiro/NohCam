#include "tracking/TrackingTracker.h"
#include <memory>
#include <mutex>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <Windows.h>

#define DELAYIMP_INSECURE_WRITABLE_HOOKS
#include <delayimp.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace {
    HMODULE g_loaded_onnx_module = nullptr;
}

static FARPROC WINAPI MyDelayLoadHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliNotePreLoadLibrary) {
        const char* dll_name = pdli->szDll;
        
        if (strcmp(dll_name, "onnxruntime.dll") == 0 || 
            strcmp(dll_name, "onnxruntime_providers_shared.dll") == 0) {
            
            wchar_t module_path[MAX_PATH];
            if (GetModuleFileNameW(nullptr, module_path, MAX_PATH) == 0) {
                return nullptr;
            }
            
            std::wstring dll_dir = module_path;
            size_t lastslash = dll_dir.find_last_of(L"\\/");
            if (lastslash != std::wstring::npos) {
                dll_dir = dll_dir.substr(0, lastslash + 1);
            }
            
            std::wstring full_path = dll_dir + L"nohcam_onnxruntime.dll";
            HMODULE h = LoadLibraryExW(full_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            
            if (h && strcmp(dll_name, "onnxruntime.dll") == 0) {
                g_loaded_onnx_module = h;
            }
            
            return reinterpret_cast<FARPROC>(h);
        }
    }
    return nullptr;
}

extern "C" PfnDliHook __pfnDliNotifyHook2 = MyDelayLoadHook;

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

namespace {
    std::unique_ptr<nohcam::TrackingTracker> g_tracker;
    std::mutex g_tracker_mutex;
    std::string g_init_error;
    bool g_logger_initialized = false;
    nohcam::TrackingResult g_last_result;

    void EnsureLoggerInitialized() {
        if (!g_logger_initialized) {
            try {
                auto file_sink = spdlog::basic_logger_mt("nohcam_bridge", "nohcam_bridge.log", false); // append
                spdlog::set_default_logger(file_sink);
                spdlog::flush_on(spdlog::level::info);
                spdlog::set_level(spdlog::level::info);
                spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e] [%l] %v");
                g_logger_initialized = true;
                
                char cwd[MAX_PATH];
                GetCurrentDirectoryA(MAX_PATH, cwd);
                spdlog::info("--- Logger initialized. CWD: {} ---", cwd);
            } catch (...) {
            }
        }
    }
}

EXPORT bool FaceTracker_Initialize() {
    EnsureLoggerInitialized();
    
    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    if (!g_tracker) {
        g_tracker = std::make_unique<nohcam::TrackingTracker>();
    }
    g_init_error.clear();
    bool success = g_tracker->Initialize(&g_init_error);
    if (!success) {
        spdlog::error("TrackingTracker initialization failed: {}", g_init_error);
    }
    spdlog::info("FaceTracker_Initialize (TrackingTracker) result: {}", success);
    return success;
}

EXPORT void FaceTracker_Shutdown() {
    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    g_tracker.reset();
    g_init_error.clear();
}

EXPORT void FaceTracker_GetInitError(char* error_buffer, int buffer_size) {
    if (error_buffer && buffer_size > 0) {
        strncpy_s(error_buffer, buffer_size, g_init_error.c_str(), buffer_size - 1);
        error_buffer[buffer_size - 1] = '\0';
    }
}

EXPORT bool FaceTracker_TrackAll(
    const std::uint8_t* pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t stride) {

    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    if (!g_tracker || !g_tracker->IsInitialized()) {
        return false;
    }

    nohcam::CameraCapture::CaptureFrame frame;
    frame.valid = true;
    frame.width = width;
    frame.height = height;
    frame.stride = stride;
    frame.pixels.assign(pixels, pixels + (static_cast<std::size_t>(stride) * height));

    g_last_result = g_tracker->Track(frame);
    return true;
}

EXPORT bool FaceTracker_GetFaceResult(
    bool* detected,
    float* yaw,
    float* pitch,
    float* roll,
    float* x,
    float* y,
    int* blendshape_count,
    float* blendshapes,
    int blendshape_capacity) {

    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    const auto& result = g_last_result.face;
    *detected = result.detected;
    *yaw = result.yaw;
    *pitch = result.pitch;
    *roll = result.roll;
    *x = result.x;
    *y = result.y;
    
    const int output_count = static_cast<int>(result.blendshapes.size());
    const int safe_capacity = std::max(0, blendshape_capacity);
    const int copied_count = std::min(output_count, safe_capacity);
    *blendshape_count = copied_count;

    if (blendshapes && copied_count > 0) {
        std::copy_n(result.blendshapes.begin(), copied_count, blendshapes);
    }
    return true;
}

EXPORT bool FaceTracker_GetHandResult(
    bool* left_detected,
    float* left_wrist_x,
    float* left_wrist_y,
    float* left_wrist_z,
    bool* right_detected,
    float* right_wrist_x,
    float* right_wrist_y,
    float* right_wrist_z) {

    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    *left_detected = g_last_result.left_hand.detected;
    if (*left_detected) {
        *left_wrist_x = g_last_result.left_hand.landmarks[0].x;
        *left_wrist_y = g_last_result.left_hand.landmarks[0].y;
        *left_wrist_z = g_last_result.left_hand.landmarks[0].z;
    }
    *right_detected = g_last_result.right_hand.detected;
    if (*right_detected) {
        *right_wrist_x = g_last_result.right_hand.landmarks[0].x;
        *right_wrist_y = g_last_result.right_hand.landmarks[0].y;
        *right_wrist_z = g_last_result.right_hand.landmarks[0].z;
    }
    return true;
}

EXPORT bool FaceTracker_GetHandLandmarks(
    bool left,
    float* landmarks_21_xyz) {

    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    const auto& hand = left ? g_last_result.left_hand : g_last_result.right_hand;
    if (!hand.detected || !landmarks_21_xyz) {
        return false;
    }
    for (int i = 0; i < 21; ++i) {
        landmarks_21_xyz[i * 3 + 0] = hand.landmarks[i].x;
        landmarks_21_xyz[i * 3 + 1] = hand.landmarks[i].y;
        landmarks_21_xyz[i * 3 + 2] = hand.landmarks[i].z;
    }
    return true;
}

EXPORT bool FaceTracker_GetFaceLandmarks(
    float* landmarks_478_xyz) {

    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    const auto& face = g_last_result.face;
    if (!face.detected || !landmarks_478_xyz) {
        return false;
    }
    for (int i = 0; i < 478; ++i) {
        landmarks_478_xyz[i * 3 + 0] = face.landmarks[i].x;
        landmarks_478_xyz[i * 3 + 1] = face.landmarks[i].y;
        landmarks_478_xyz[i * 3 + 2] = face.landmarks[i].z;
    }
    return true;
}

EXPORT bool FaceTracker_GetPoseResult(
    bool* detected,
    float* score,
    float* landmarks_33_xyz, // array of 33 * 3 floats
    float* visibility_33,    // array of 33 floats
    float* presence_33) {    // array of 33 floats

    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    const auto& pose = g_last_result.pose;
    *detected = pose.detected;
    *score = pose.score;
    if (pose.detected) {
        if (landmarks_33_xyz) {
            for (int i = 0; i < 33; ++i) {
                landmarks_33_xyz[i * 3 + 0] = pose.landmarks[i].x;
                landmarks_33_xyz[i * 3 + 1] = pose.landmarks[i].y;
                landmarks_33_xyz[i * 3 + 2] = pose.landmarks[i].z;
            }
        }
        if (visibility_33) {
            std::copy_n(pose.visibility.begin(), 33, visibility_33);
        }
        if (presence_33) {
            std::copy_n(pose.presence.begin(), 33, presence_33);
        }
    }
    return true;
}

// Backward compatibility (optional, but keep for now if possible)
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
    float* blendshapes,
    int blendshape_capacity) {

    if (FaceTracker_TrackAll(pixels, width, height, stride)) {
        return FaceTracker_GetFaceResult(detected, yaw, pitch, roll, x, y, blendshape_count, blendshapes, blendshape_capacity);
    }
    return false;
}

EXPORT bool FaceTracker_TrackHands(
    const std::uint8_t* pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t stride,
    bool* left_detected,
    float* left_wrist_pitch,
    float* left_wrist_yaw,
    float* left_wrist_roll,
    bool* right_detected,
    float* right_wrist_pitch,
    float* right_wrist_yaw,
    float* right_wrist_roll) {

    if (FaceTracker_TrackAll(pixels, width, height, stride)) {
        std::lock_guard<std::mutex> lock(g_tracker_mutex);
        *left_detected = g_last_result.left_hand.detected;
        *left_wrist_pitch = g_last_result.left_hand.wrist_pitch;
        *left_wrist_yaw = g_last_result.left_hand.wrist_yaw;
        *left_wrist_roll = g_last_result.left_hand.wrist_roll;
        *right_detected = g_last_result.right_hand.detected;
        *right_wrist_pitch = g_last_result.right_hand.wrist_pitch;
        *right_wrist_yaw = g_last_result.right_hand.wrist_yaw;
        *right_wrist_roll = g_last_result.right_hand.wrist_roll;
        return true;
    }
    return false;
}
