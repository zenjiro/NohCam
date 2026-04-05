#pragma once

#include <cstdint>

#include <Windows.h>

namespace nohcam {

class D3D11Renderer;

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    ~MainWindow() = default;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool Create(const wchar_t* title, std::uint32_t width, std::uint32_t height, int show_command);
    void Destroy();
    bool ProcessMessages();

    void SetRenderer(D3D11Renderer* renderer) { renderer_ = renderer; }

    HWND GetHandle() const { return handle_; }
    std::uint32_t GetClientWidth() const { return client_width_; }
    std::uint32_t GetClientHeight() const { return client_height_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_;
    HWND handle_ = nullptr;
    D3D11Renderer* renderer_ = nullptr;
    std::uint32_t client_width_ = 0;
    std::uint32_t client_height_ = 0;
    bool class_registered_ = false;
};

}  // namespace nohcam
