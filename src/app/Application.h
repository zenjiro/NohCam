#pragma once

#include <memory>

#include <Windows.h>

namespace nohcam {

class CameraCapture;
class D3D11Renderer;
class ImGuiLayer;
class MainWindow;

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
    std::unique_ptr<ImGuiLayer> imgui_layer_;
    std::unique_ptr<CameraCapture> camera_capture_;
};

}  // namespace nohcam
