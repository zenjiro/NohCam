#include "tracking/FaceTracker.h"
#include "tracking/HandTracker.h"
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
    std::unique_ptr<nohcam::FaceTracker> g_face_tracker;
    std::unique_ptr<nohcam::HandTracker> g_hand_tracker;
    std::mutex g_face_tracker_mutex;
    std::string g_init_error;
    bool g_logger_initialized = false;

    void EnsureLoggerInitialized() {
        if (!g_logger_initialized) {
            try {
                auto file_sink = spdlog::basic_logger_mt("nohcam_bridge", "nohcam_bridge.log", true);
                spdlog::set_default_logger(file_sink);
                spdlog::set_level(spdlog::level::info);
                spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e] [%l] %v");
                g_logger_initialized = true;
            } catch (...) {
            }
        }
    }
}

EXPORT bool FaceTracker_Initialize() {
    EnsureLoggerInitialized();
    
    std::lock_guard<std::mutex> lock(g_face_tracker_mutex);
    if (!g_face_tracker) {
        g_face_tracker = std::make_unique<nohcam::FaceTracker>();
    }
    if (!g_hand_tracker) {
        g_hand_tracker = std::make_unique<nohcam::HandTracker>();
    }
    g_init_error.clear();
    bool success = g_face_tracker->Initialize(&g_init_error);
    std::string hand_error;
    const bool hand_success = g_hand_tracker->Initialize(&hand_error);
    if (!hand_success && !hand_error.empty()) {
        spdlog::warn("HandTracker initialization failed (hands will be unavailable): {}", hand_error);
    }
    spdlog::info("FaceTracker_Initialize result: {}", success);
    return success;
}

EXPORT void FaceTracker_Shutdown() {
    std::lock_guard<std::mutex> lock(g_face_tracker_mutex);
    g_face_tracker.reset();
    g_hand_tracker.reset();
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
    float* blendshapes,
    int blendshape_capacity) {

    std::lock_guard<std::mutex> lock(g_face_tracker_mutex);
    if (!g_face_tracker || !g_face_tracker->IsInitialized()) {
        spdlog::error("FaceTracker_Track: not initialized");
        return false;
    }

    if (!pixels || width == 0 || height == 0) {
        spdlog::error("FaceTracker_Track: invalid frame params w={} h={} stride={}", 
            width, height, stride);
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
    const int output_count = static_cast<int>(result.blendshapes.size());
    const int safe_capacity = std::max(0, blendshape_capacity);
    const int copied_count = std::min(output_count, safe_capacity);
    *blendshape_count = copied_count;

    if (blendshapes && copied_count > 0) {
        std::copy_n(result.blendshapes.begin(), copied_count, blendshapes);
    }

    // Log every 60 frames
    static int log_counter = 0;
    if (++log_counter % 60 == 0) {
        spdlog::info("FaceTracker_Track: detected={} yaw={:.2f} pitch={:.2f} roll={:.2f} x={:.3f} y={:.3f} blendshapes={}", 
            *detected, *yaw, *pitch, *roll, *x, *y, *blendshape_count);
    }

    return true;
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

    std::lock_guard<std::mutex> lock(g_face_tracker_mutex);
    if (!g_hand_tracker || !g_hand_tracker->IsInitialized()) {
        return false;
    }
    if (!pixels || width == 0 || height == 0) {
        return false;
    }
    if (!left_detected || !left_wrist_pitch || !left_wrist_yaw || !left_wrist_roll ||
        !right_detected || !right_wrist_pitch || !right_wrist_yaw || !right_wrist_roll) {
        return false;
    }

    *left_detected = false;
    *left_wrist_pitch = 0.0f;
    *left_wrist_yaw = 0.0f;
    *left_wrist_roll = 0.0f;
    *right_detected = false;
    *right_wrist_pitch = 0.0f;
    *right_wrist_yaw = 0.0f;
    *right_wrist_roll = 0.0f;

    try {
        nohcam::CameraCapture::CaptureFrame frame;
        frame.valid = true;
        frame.width = width;
        frame.height = height;
        frame.stride = stride;
        frame.pixels.assign(pixels, pixels + (static_cast<std::size_t>(stride) * height));

        const auto result = g_hand_tracker->Track(frame);
        *left_detected = result.left_hand.detected;
        *left_wrist_pitch = result.left_hand.wrist_pitch;
        *left_wrist_yaw = result.left_hand.wrist_yaw;
        *left_wrist_roll = result.left_hand.wrist_roll;
        *right_detected = result.right_hand.detected;
        *right_wrist_pitch = result.right_hand.wrist_pitch;
        *right_wrist_yaw = result.right_hand.wrist_yaw;
        *right_wrist_roll = result.right_hand.wrist_roll;
        return true;
    } catch (const std::exception& ex) {
        spdlog::error("FaceTracker_TrackHands exception: {}", ex.what());
    } catch (...) {
        spdlog::error("FaceTracker_TrackHands unknown exception");
    }
    return false;
}
