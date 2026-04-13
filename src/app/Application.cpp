#include "app/Application.h"

#include <chrono>
#include <exception>

#include "capture/CameraCapture.h"
#include "pipeline/PreviewTap.h"
#include "render/D3D11Renderer.h"
#include "tracking/TrackingTracker.h"
#include "tracking/TrackingResult.h"
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

        if (!camera_capture_->Start(0)) {
            spdlog::warn("Camera capture did not start; see the UI status panel for details.");
        }

        tracking_tracker_ = std::make_unique<TrackingTracker>();
        std::string tracker_error;
        if (!tracking_tracker_->Initialize(&tracker_error)) {
            spdlog::error("Tracking tracker initialization failed: {}", tracker_error);
            Shutdown();
            return false;
        } else {
            spdlog::info("Tracking tracker initialized successfully.");
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
        const bool preview_enabled = true;

        if (preview_tap_) {
            preview_tap_->SetEnabled(preview_enabled);
        }

        if (camera_capture_) {
            const auto capture_frame = camera_capture_->GetLatestFrame();
            if (capture_frame.valid && preview_tap_ && capture_frame.frame_count != last_capture_frame_count_) {
                preview_tap_->SubmitFrame(capture_frame);
                last_capture_frame_count_ = capture_frame.frame_count;
            }

            if (capture_frame.valid && tracking_tracker_) {
                last_tracking_result_ = tracking_tracker_->Track(capture_frame);
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
        renderer_->EndFrame();
    }

    return EXIT_SUCCESS;
}

void Application::Shutdown() {
    if (camera_capture_) {
        camera_capture_->Stop();
    }

    if (renderer_) {
        renderer_->Shutdown();
    }

    if (main_window_) {
        main_window_->Destroy();
    }

    camera_capture_.reset();
    preview_tap_.reset();
    tracking_tracker_.reset();
    renderer_.reset();
    main_window_.reset();
    initialized_ = false;
}

}  // namespace nohcam
