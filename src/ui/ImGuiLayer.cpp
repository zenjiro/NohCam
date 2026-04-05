#include "ui/ImGuiLayer.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <spdlog/spdlog.h>

namespace nohcam {

bool ImGuiLayer::Initialize(HWND window_handle, ID3D11Device* device, ID3D11DeviceContext* device_context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(window_handle)) {
        spdlog::error("ImGui_ImplWin32_Init failed.");
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplDX11_Init(device, device_context)) {
        spdlog::error("ImGui_ImplDX11_Init failed.");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    initialized_ = true;
    return true;
}

void ImGuiLayer::Shutdown() {
    if (!initialized_) {
        return;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void ImGuiLayer::BeginFrame() {
    if (!initialized_) {
        return;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::RenderDemoUi() {
    if (!initialized_) {
        return;
    }

    ImGui::Begin("Phase 1 Bootstrap");
    ImGui::TextUnformatted("NohCam Win32 + DirectX 11 initialization is running.");
    ImGui::Separator();
    ImGui::Checkbox("Show Dear ImGui demo", &show_demo_window_);
    ImGui::TextUnformatted("Next steps: camera capture, tracking, and avatar rendering.");
    ImGui::End();

    if (show_demo_window_) {
        ImGui::ShowDemoWindow(&show_demo_window_);
    }
}

void ImGuiLayer::Render(ID3D11DeviceContext*) {
    if (!initialized_) {
        return;
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace nohcam
