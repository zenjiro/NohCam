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
    bool UpdatePreviewTexture(const void* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t stride);

    ID3D11Device* GetDevice() const { return device_.Get(); }
    ID3D11DeviceContext* GetDeviceContext() const { return device_context_.Get(); }
    ID3D11ShaderResourceView* GetPreviewShaderResourceView() const { return preview_shader_resource_view_.Get(); }
    std::uint32_t GetPreviewWidth() const { return preview_width_; }
    std::uint32_t GetPreviewHeight() const { return preview_height_; }

private:
    bool CreateSwapChain(HWND window_handle, std::uint32_t width, std::uint32_t height);
    bool CreateRenderTarget();
    bool CreatePreviewTexture(std::uint32_t width, std::uint32_t height);
    void ReleaseRenderTarget();
    void ReleasePreviewTexture();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> device_context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> preview_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> preview_shader_resource_view_;

    D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_11_0;
    std::uint32_t preview_width_ = 0;
    std::uint32_t preview_height_ = 0;
    float clear_color_[4] = {0.08f, 0.09f, 0.11f, 1.0f};
};

}  // namespace nohcam
