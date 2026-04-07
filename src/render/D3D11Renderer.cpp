#include "render/D3D11Renderer.h"

#include <array>
#include <cstring>

#include <dxgi.h>
#include <spdlog/spdlog.h>

namespace nohcam {

namespace {

constexpr std::array<D3D_FEATURE_LEVEL, 3> kFeatureLevels = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_0,
};

}  // namespace

bool D3D11Renderer::Initialize(HWND window_handle, std::uint32_t width, std::uint32_t height) {
    UINT device_flags = 0;
#if defined(_DEBUG)
    device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        device_flags,
        kFeatureLevels.data(),
        static_cast<UINT>(kFeatureLevels.size()),
        D3D11_SDK_VERSION,
        &device_,
        &feature_level_,
        &device_context_);

    if (hr == E_INVALIDARG) {
        constexpr std::array<D3D_FEATURE_LEVEL, 2> kFallbackLevels = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0,
        };

        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            device_flags,
            kFallbackLevels.data(),
            static_cast<UINT>(kFallbackLevels.size()),
            D3D11_SDK_VERSION,
            &device_,
            &feature_level_,
            &device_context_);
    }

    if (FAILED(hr)) {
        spdlog::error("D3D11CreateDevice failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
        return false;
    }

    if (!CreateSwapChain(window_handle, width, height)) {
        Shutdown();
        return false;
    }

    if (!CreateRenderTarget()) {
        Shutdown();
        return false;
    }

    spdlog::info("D3D11 initialized with feature level {}", static_cast<int>(feature_level_));
    return true;
}

void D3D11Renderer::Shutdown() {
    ReleaseRenderTarget();
    ReleasePreviewTexture();
    swap_chain_.Reset();
    device_context_.Reset();
    device_.Reset();
}

void D3D11Renderer::Resize(std::uint32_t width, std::uint32_t height) {
    if (!swap_chain_ || width == 0 || height == 0) {
        return;
    }

    ReleaseRenderTarget();

    const HRESULT hr = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        spdlog::error("ResizeBuffers failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
        return;
    }

    if (!CreateRenderTarget()) {
        spdlog::error("Failed to recreate render target after resize.");
    }
}

void D3D11Renderer::BeginFrame() {
    if (!device_context_ || !render_target_view_) {
        return;
    }

    device_context_->OMSetRenderTargets(1, render_target_view_.GetAddressOf(), nullptr);
    device_context_->ClearRenderTargetView(render_target_view_.Get(), clear_color_);
}

void D3D11Renderer::EndFrame() {
    if (!swap_chain_) {
        return;
    }

    const HRESULT hr = swap_chain_->Present(1, 0);
    if (FAILED(hr)) {
        spdlog::error("Present failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
    }
}

bool D3D11Renderer::UpdatePreviewTexture(const void* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t stride) {
    if (!device_ || !device_context_ || pixels == nullptr || width == 0 || height == 0 || stride == 0) {
        return false;
    }

    if (!preview_texture_ || width != preview_width_ || height != preview_height_) {
        ReleasePreviewTexture();
        if (!CreatePreviewTexture(width, height)) {
            return false;
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped_resource = {};
    const HRESULT hr = device_context_->Map(preview_texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
    if (FAILED(hr)) {
        spdlog::error("Map for preview texture failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
        return false;
    }

    const auto* source = static_cast<const std::uint8_t*>(pixels);
    auto* destination = static_cast<std::uint8_t*>(mapped_resource.pData);
    for (std::uint32_t row = 0; row < height; ++row) {
        std::memcpy(
            destination + static_cast<std::size_t>(row) * mapped_resource.RowPitch,
            source + static_cast<std::size_t>(row) * stride,
            stride);
    }

    device_context_->Unmap(preview_texture_.Get(), 0);
    return true;
}

bool D3D11Renderer::CreateSwapChain(HWND window_handle, std::uint32_t width, std::uint32_t height) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;

    HRESULT hr = device_.As(&dxgi_device);
    if (FAILED(hr)) {
        spdlog::error("QueryInterface for IDXGIDevice failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
        return false;
    }

    hr = dxgi_device->GetAdapter(&adapter);
    if (FAILED(hr)) {
        spdlog::error("GetAdapter failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
        return false;
    }

    hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf()));
    if (FAILED(hr)) {
        spdlog::error("Failed to get IDXGIFactory2. HRESULT={:#x}", static_cast<unsigned int>(hr));
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
    swap_chain_desc.Width = width;
    swap_chain_desc.Height = height;
    swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    hr = factory->CreateSwapChainForHwnd(
        device_.Get(),
        window_handle,
        &swap_chain_desc,
        nullptr,
        nullptr,
        &swap_chain_);

    if (FAILED(hr)) {
        spdlog::error("CreateSwapChainForHwnd failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
        return false;
    }

    factory->MakeWindowAssociation(window_handle, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool D3D11Renderer::CreateRenderTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
    const HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr)) {
        spdlog::error("GetBuffer failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
        return false;
    }

    const HRESULT rtv_hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_view_);
    if (FAILED(rtv_hr)) {
        spdlog::error("CreateRenderTargetView failed. HRESULT={:#x}", static_cast<unsigned int>(rtv_hr));
        return false;
    }

    return true;
}

bool D3D11Renderer::CreatePreviewTexture(std::uint32_t width, std::uint32_t height) {
    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DYNAMIC;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device_->CreateTexture2D(&texture_desc, nullptr, &preview_texture_);
    if (FAILED(hr)) {
        spdlog::error("CreateTexture2D for preview failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
        return false;
    }

    hr = device_->CreateShaderResourceView(preview_texture_.Get(), nullptr, &preview_shader_resource_view_);
    if (FAILED(hr)) {
        spdlog::error("CreateShaderResourceView for preview failed. HRESULT={:#x}", static_cast<unsigned int>(hr));
        ReleasePreviewTexture();
        return false;
    }

    preview_width_ = width;
    preview_height_ = height;
    return true;
}

void D3D11Renderer::ReleaseRenderTarget() {
    render_target_view_.Reset();
}

void D3D11Renderer::ReleasePreviewTexture() {
    preview_shader_resource_view_.Reset();
    preview_texture_.Reset();
    preview_width_ = 0;
    preview_height_ = 0;
}

}  // namespace nohcam
