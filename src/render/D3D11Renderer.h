#pragma once

#include <cstdint>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace nohcam {

class D3D11Renderer {
public:
    D3D11Renderer() = default;
    ~D3D11Renderer() = default;

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    bool Initialize(HWND window_handle, std::uint32_t width, std::uint32_t height);
    void Shutdown();
    void Resize(std::uint32_t width, std::uint32_t height);
    void BeginFrame();
    void EndFrame();

    ID3D11Device* GetDevice() const { return device_.Get(); }
    ID3D11DeviceContext* GetDeviceContext() const { return device_context_.Get(); }

private:
    bool CreateSwapChain(HWND window_handle, std::uint32_t width, std::uint32_t height);
    bool CreateRenderTarget();
    void ReleaseRenderTarget();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> device_context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view_;

    D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_11_0;
    float clear_color_[4] = {0.08f, 0.09f, 0.11f, 1.0f};
};

}  // namespace nohcam
