#pragma once

#include <d3d11.h>

struct ImDrawData;

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
    void RenderDemoUi();
    void Render(ID3D11DeviceContext* device_context);

private:
    bool initialized_ = false;
    bool show_demo_window_ = true;
};

}  // namespace nohcam
