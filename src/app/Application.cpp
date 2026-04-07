#include "app/Application.h"

#include <chrono>
#include <exception>

#include "capture/CameraCapture.h"
#include "pipeline/PreviewTap.h"
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
        camera_capture_ = std::make_unique<CameraCapture>();
        preview_tap_ = std::make_unique<PreviewTap>();

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

        if (!camera_capture_->Initialize()) {
            spdlog::warn("Camera capture initialization failed; the app will continue without live input.");
        } else if (!camera_capture_->StartDefaultDevice()) {
            spdlog::warn("Camera capture did not start; see the UI status panel for details.");
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
        const CameraCapture::StateSnapshot camera_state =
            camera_capture_ ? camera_capture_->GetStateSnapshot() : CameraCapture::StateSnapshot{};
        const bool preview_enabled = imgui_layer_ ? imgui_layer_->WantsPreview() : true;

        if (preview_tap_) {
            preview_tap_->SetEnabled(preview_enabled);
        }

        if (camera_capture_) {
            const auto capture_frame = camera_capture_->GetLatestCaptureFrame();
            if (capture_frame.has_value() && preview_tap_ && capture_frame->frame_count != last_capture_frame_count_) {
                preview_tap_->SubmitFrame(*capture_frame);
                last_capture_frame_count_ = capture_frame->frame_count;
            }
        }

        const PreviewTap::StateSnapshot preview_state =
            preview_tap_ ? preview_tap_->GetStateSnapshot() : PreviewTap::StateSnapshot{};

        if (preview_tap_) {
            const auto preview_frame = preview_tap_->GetLatestPreviewFrame();
            if (preview_frame.has_value() && preview_frame->frame_count != last_preview_frame_count_) {
                renderer_->UpdatePreviewTexture(
                    preview_frame->pixels.data(),
                    preview_frame->width,
                    preview_frame->height,
                    preview_frame->stride);
                last_preview_frame_count_ = preview_frame->frame_count;
            }
        }

        renderer_->BeginFrame();
        imgui_layer_->BeginFrame();
        imgui_layer_->RenderMainUi(
            camera_state,
            preview_state,
            renderer_->GetPreviewShaderResourceView(),
            renderer_->GetPreviewWidth(),
            renderer_->GetPreviewHeight());
        imgui_layer_->Render(renderer_->GetDeviceContext());
        renderer_->EndFrame();
    }

    return EXIT_SUCCESS;
}

void Application::Shutdown() {
    if (imgui_layer_) {
        imgui_layer_->Shutdown();
    }

    if (camera_capture_) {
        camera_capture_->Shutdown();
    }

    if (renderer_) {
        renderer_->Shutdown();
    }

    if (main_window_) {
        main_window_->Destroy();
    }

    imgui_layer_.reset();
    camera_capture_.reset();
    preview_tap_.reset();
    renderer_.reset();
    main_window_.reset();
    initialized_ = false;
}

}  // namespace nohcam
