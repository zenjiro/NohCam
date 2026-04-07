#include "ui/MainWindow.h"

#include "render/D3D11Renderer.h"

#include <spdlog/spdlog.h>

namespace nohcam {

namespace {

constexpr wchar_t kWindowClassName[] = L"NohCamMainWindow";

}  // namespace

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {}

bool MainWindow::Create(const wchar_t* title, std::uint32_t width, std::uint32_t height, int show_command) {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &MainWindow::WindowProc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&window_class)) {
        spdlog::error("RegisterClassExW failed. GetLastError={}", static_cast<unsigned long>(GetLastError()));
        return false;
    }

    class_registered_ = true;

    RECT window_rect = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);

    handle_ = CreateWindowExW(
        0,
        kWindowClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        nullptr,
        nullptr,
        instance_,
        this);

    if (!handle_) {
        spdlog::error("CreateWindowExW failed. GetLastError={}", static_cast<unsigned long>(GetLastError()));
        Destroy();
        return false;
    }

    ShowWindow(handle_, show_command);
    UpdateWindow(handle_);

    RECT client_rect = {};
    GetClientRect(handle_, &client_rect);
    client_width_ = static_cast<std::uint32_t>(client_rect.right - client_rect.left);
    client_height_ = static_cast<std::uint32_t>(client_rect.bottom - client_rect.top);

    return true;
}

void MainWindow::Destroy() {
    if (handle_) {
        DestroyWindow(handle_);
        handle_ = nullptr;
    }

    if (class_registered_) {
        UnregisterClassW(kWindowClassName, instance_);
        class_registered_ = false;
    }
}

bool MainWindow::ProcessMessages() {
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            return false;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return true;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<MainWindow*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(window_handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }

    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window_handle, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(window_handle, message, wparam, lparam);
    }

    return self->HandleMessage(message, wparam, lparam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            client_width_ = static_cast<std::uint32_t>(LOWORD(lparam));
            client_height_ = static_cast<std::uint32_t>(HIWORD(lparam));
            if (renderer_) {
                renderer_->Resize(client_width_, client_height_);
            }
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(handle_, message, wparam, lparam);
    }
}

}  // namespace nohcam
