#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace nohcam {

class CameraCapture {
public:
    struct CaptureFrame {
        bool valid = false;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t stride = 0;
        std::uint64_t frame_count = 0;
        std::int64_t timestamp_hns = 0;
        std::vector<std::uint8_t> pixels;
    };

    struct DeviceInfo {
        std::wstring id;
        std::wstring name;
    };

    struct StateSnapshot {
        bool media_foundation_ready = false;
        bool running = false;
        bool frame_received = false;
        std::wstring active_device_name;
        std::wstring last_error;
        std::size_t device_count = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t frame_count = 0;
        std::int64_t last_sample_time_hns = 0;
        std::wstring negotiated_subtype = L"Unknown";
        bool using_nv12_fallback = false;
    };

    CameraCapture();
    ~CameraCapture();

    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    bool Initialize();
    void Shutdown();

    bool StartDefaultDevice();
    StateSnapshot GetStateSnapshot() const;
    std::optional<CaptureFrame> GetLatestCaptureFrame() const;

private:
    bool EnumerateDevices();
    void CaptureLoop(std::wstring device_id, std::wstring device_name);
    void SetLastError(std::wstring message);

    std::vector<DeviceInfo> devices_;
    mutable std::mutex state_mutex_;
    mutable std::mutex capture_frame_mutex_;
    StateSnapshot state_;
    CaptureFrame latest_capture_frame_;
    std::thread capture_thread_;
    std::atomic<bool> stop_requested_ = false;
    bool media_foundation_started_ = false;
};

}  // namespace nohcam
