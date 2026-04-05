#include "app/Application.h"

#include <chrono>
#include <exception>

#include "render/D3D11Renderer.h"
#include "ui/ImGuiLayer.h"
#include "ui/MainWindow.h"

#include <spdlog/spdlog.h>

namespace nohcam {

Application::Application(HINSTANCE instance) : instance_(instance) {}

Application::~Application() {
    Shutdown();
}

bool Application::Initialize(int show_command) {
    if (initialized_) {
        return true;
    }

    try {
        main_window_ = std::make_unique<MainWindow>(instance_);
        renderer_ = std::make_unique<D3D11Renderer>();
        imgui_layer_ = std::make_unique<ImGuiLayer>();

        if (!main_window_->Create(L"NohCam", 1280, 720, show_command)) {
            spdlog::error("Failed to create main window.");
            Shutdown();
            return false;
        }

        if (!renderer_->Initialize(main_window_->GetHandle(), main_window_->GetClientWidth(), main_window_->GetClientHeight())) {
            spdlog::error("Failed to initialize Direct3D 11 renderer.");
            Shutdown();
            return false;
        }

        main_window_->SetRenderer(renderer_.get());

        if (!imgui_layer_->Initialize(main_window_->GetHandle(), renderer_->GetDevice(), renderer_->GetDeviceContext())) {
            spdlog::error("Failed to initialize Dear ImGui.");
            Shutdown();
            return false;
        }

        initialized_ = true;
        spdlog::info("Application initialized successfully.");
        return true;
    } catch (const std::exception& exception) {
        spdlog::error("Initialization failed with exception: {}", exception.what());
        Shutdown();
        return false;
    }
}

int Application::Run() {
    if (!initialized_) {
        return EXIT_FAILURE;
    }

    while (main_window_ && main_window_->ProcessMessages()) {
        renderer_->BeginFrame();
        imgui_layer_->BeginFrame();
        imgui_layer_->RenderDemoUi();
        imgui_layer_->Render(renderer_->GetDeviceContext());
        renderer_->EndFrame();
    }

    return EXIT_SUCCESS;
}

void Application::Shutdown() {
    if (imgui_layer_) {
        imgui_layer_->Shutdown();
    }

    if (renderer_) {
        renderer_->Shutdown();
    }

    if (main_window_) {
        main_window_->Destroy();
    }

    imgui_layer_.reset();
    renderer_.reset();
    main_window_.reset();
    initialized_ = false;
}

}  // namespace nohcam
