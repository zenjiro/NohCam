#pragma once

#include <d3d11.h>

#include "capture/CameraCapture.h"

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
        ID3D11ShaderResourceView* preview_shader_resource_view,
        std::uint32_t preview_width,
        std::uint32_t preview_height);
    void Render(ID3D11DeviceContext* device_context);

private:
    bool initialized_ = false;
    bool show_demo_window_ = true;
};

}  // namespace nohcam
