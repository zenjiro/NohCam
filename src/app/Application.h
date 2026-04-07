#pragma once

#include <cstdint>
#include <memory>

#include <Windows.h>

#include "tracking/FaceTracker.h"
#include "tracking/TrackingResult.h"

namespace nohcam {

class CameraCapture;
class D3D11Renderer;
class MainWindow;
class PreviewTap;

class Application {
public:
    explicit Application(HINSTANCE instance);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize(int show_command);
    int Run();
    void Shutdown();

private:
    HINSTANCE instance_;
    bool initialized_ = false;

    std::unique_ptr<MainWindow> main_window_;
    std::unique_ptr<D3D11Renderer> renderer_;
    std::unique_ptr<CameraCapture> camera_capture_;
    std::unique_ptr<PreviewTap> preview_tap_;
    std::unique_ptr<FaceTracker> face_tracker_;
    FaceResult last_face_result_;
    std::uint64_t last_capture_frame_count_ = 0;
    std::uint64_t last_preview_frame_count_ = 0;
};

}  // namespace nohcam
