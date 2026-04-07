#include "ui/ImGuiLayer.h"

#include <Windows.h>

#include <algorithm>
#include <string_view>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <spdlog/spdlog.h>

namespace nohcam {

namespace {

std::string Narrow(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

}  // namespace

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

void ImGuiLayer::RenderMainUi(
    const CameraCapture::StateSnapshot& camera_state,
    const PreviewTap::StateSnapshot& preview_state,
    const FaceResult& face_result,
    bool face_tracker_ready,
    const std::string& face_tracker_error,
    ID3D11ShaderResourceView* preview_shader_resource_view,
    std::uint32_t preview_width,
    std::uint32_t preview_height) {
    if (!initialized_) {
        return;
    }

    if (show_preview_window_) {
        if (ImGui::Begin("Preview", &show_preview_window_)) {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            if (preview_shader_resource_view != nullptr && preview_width > 0 && preview_height > 0) {
                float draw_width = available.x;
                float draw_height = draw_width * (static_cast<float>(preview_height) / static_cast<float>(preview_width));

                if (draw_height > available.y && available.y > 0.0f) {
                    draw_height = available.y;
                    draw_width = draw_height * (static_cast<float>(preview_width) / static_cast<float>(preview_height));
                }

                const float offset_x = std::max(0.0f, (available.x - draw_width) * 0.5f);
                const float offset_y = std::max(0.0f, (available.y - draw_height) * 0.5f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset_y);
                ImGui::Image(preview_shader_resource_view, ImVec2(draw_width, draw_height));
            } else if (camera_state.frame_received) {
                ImGui::TextUnformatted("A frame arrived, but no preview texture is available.");
            } else if (camera_state.running) {
                ImGui::TextUnformatted("Waiting for the first camera frame...");
            } else {
                ImGui::TextUnformatted("No preview available.");
            }
        }
        ImGui::End();
    }

    ImGui::Begin("Camera Status");
    ImGui::TextUnformatted("Phase 1 camera input uses a full-rate capture path plus a throttled preview tap.");
    ImGui::Separator();
    ImGui::Text("Media Foundation: %s", camera_state.media_foundation_ready ? "ready" : "not ready");
    ImGui::Text("Camera devices: %zu", camera_state.device_count);
    ImGui::Text("Capture status: %s", camera_state.running ? "running" : "idle");

    const std::string active_device_name = Narrow(camera_state.active_device_name);
    ImGui::Text("Active device: %s", active_device_name.empty() ? "(none)" : active_device_name.c_str());

    const std::string negotiated_subtype = Narrow(camera_state.negotiated_subtype);
    ImGui::Text("Negotiated format: %s", negotiated_subtype.empty() ? "Unknown" : negotiated_subtype.c_str());
    ImGui::Text("NV12 fallback: %s", camera_state.using_nv12_fallback ? "yes" : "no");

    if (camera_state.width > 0 && camera_state.height > 0) {
        ImGui::Text("Capture resolution: %ux%u", camera_state.width, camera_state.height);
    } else {
        ImGui::TextUnformatted("Capture resolution: waiting for negotiation");
    }

    if (preview_width > 0 && preview_height > 0) {
        ImGui::Text("Preview texture: %ux%u", preview_width, preview_height);
    } else {
        ImGui::TextUnformatted("Preview texture: not created yet");
    }
    ImGui::Text("Preview tap: %s", preview_state.enabled ? "enabled" : "disabled");
    ImGui::Text("Preview budget: %ux%u @ %u fps", preview_state.max_width, preview_state.max_height, preview_state.target_fps);
    ImGui::Text("Preview frames: %llu", static_cast<unsigned long long>(preview_state.preview_frame_count));
    ImGui::Text("Source frames seen by preview tap: %llu", static_cast<unsigned long long>(preview_state.source_frame_count));

    ImGui::Separator();
    ImGui::Text("Face tracker: %s", face_tracker_ready ? "ready" : "not ready");
    if (!face_tracker_ready && !face_tracker_error.empty()) {
        ImGui::TextWrapped("Face tracker warning: %s", face_tracker_error.c_str());
    }

    ImGui::Text("Face detected: %s", face_result.detected ? "yes" : "no");
    if (face_result.detected) {
        ImGui::Text("Head yaw: %.2f", face_result.yaw);
        ImGui::Text("Head pitch: %.2f", face_result.pitch);
        ImGui::Text("Head roll: %.2f", face_result.roll);
        ImGui::Text("Face center (normalized): %.2f, %.2f", face_result.x, face_result.y);
        ImGui::Text("Blendshape count: %zu", face_result.blendshapes.size());
    }

    ImGui::Text("Frames received: %llu", static_cast<unsigned long long>(camera_state.frame_count));
    if (camera_state.frame_received) {
        ImGui::Text("Last sample time: %.2f ms", static_cast<double>(camera_state.last_sample_time_hns) / 10000.0);
    } else {
        ImGui::TextUnformatted("Last sample time: no frame yet");
    }

    if (!camera_state.last_error.empty()) {
        const std::string last_error = Narrow(camera_state.last_error);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "Camera warning: %s", last_error.c_str());
    }

    ImGui::Spacing();
    ImGui::Checkbox("Show preview window", &show_preview_window_);
    ImGui::Checkbox("Show Dear ImGui demo", &show_demo_window_);
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
