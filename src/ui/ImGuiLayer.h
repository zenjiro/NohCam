#pragma once

#include <d3d11.h>
#include <string>

#include "capture/CameraCapture.h"
#include "pipeline/PreviewTap.h"
#include "tracking/TrackingResult.h"

namespace nohcam {

class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer() = default;

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    bool Initialize(HWND window_handle, ID3D11Device* device, ID3D11DeviceContext* device_context);
    void Shutdown();
    void BeginFrame();
    void RenderMainUi(
        const CameraCapture::StateSnapshot& camera_state,
        const PreviewTap::StateSnapshot& preview_state,
        const FaceResult& face_result,
        bool face_tracker_ready,
        const std::string& face_tracker_error,
        ID3D11ShaderResourceView* preview_shader_resource_view,
        std::uint32_t preview_width,
        std::uint32_t preview_height);
    void Render(ID3D11DeviceContext* device_context);
    bool WantsPreview() const { return show_preview_window_; }

private:
    bool initialized_ = false;
    bool show_preview_window_ = true;
    bool show_demo_window_ = false;
};

}  // namespace nohcam
