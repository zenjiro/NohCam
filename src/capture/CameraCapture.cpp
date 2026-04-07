#include "capture/CameraCapture.h"

#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <string_view>

#include <spdlog/spdlog.h>

namespace nohcam {

namespace {

using Microsoft::WRL::ComPtr;
constexpr DWORD kVideoStreamIndex = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

std::string Narrow(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring HResultMessage(const wchar_t* prefix, HRESULT hr) {
    wchar_t buffer[128] = {};
    swprintf_s(buffer, L"%ls (HRESULT=0x%08X)", prefix, static_cast<unsigned int>(hr));
    return buffer;
}

std::wstring SubtypeName(const GUID& subtype) {
    if (subtype == MFVideoFormat_RGB32) {
        return L"RGB32";
    }
    if (subtype == MFVideoFormat_NV12) {
        return L"NV12";
    }

    LPOLESTR guid_string = nullptr;
    if (SUCCEEDED(StringFromCLSID(subtype, &guid_string)) && guid_string != nullptr) {
        std::wstring result(guid_string);
        CoTaskMemFree(guid_string);
        return result;
    }

    return L"Unknown";
}

std::uint8_t ClampToByte(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void ConvertNv12ToBgra(
    const std::uint8_t* source,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t source_stride,
    std::vector<std::uint8_t>* destination) {
    destination->resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);

    const std::uint8_t* y_plane = source;
    const std::uint8_t* uv_plane = source + static_cast<std::size_t>(source_stride) * height;

    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* y_row = y_plane + static_cast<std::size_t>(y) * source_stride;
        const std::uint8_t* uv_row = uv_plane + static_cast<std::size_t>(y / 2) * source_stride;
        std::uint8_t* output_row = destination->data() + static_cast<std::size_t>(y) * width * 4;

        for (std::uint32_t x = 0; x < width; ++x) {
            const int y_value = static_cast<int>(y_row[x]);
            const int u_value = static_cast<int>(uv_row[(x / 2) * 2]) - 128;
            const int v_value = static_cast<int>(uv_row[(x / 2) * 2 + 1]) - 128;

            const int c = std::max(0, y_value - 16);
            const int d = u_value;
            const int e = v_value;

            const int r = (298 * c + 409 * e + 128) >> 8;
            const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            const int b = (298 * c + 516 * d + 128) >> 8;

            output_row[x * 4 + 0] = ClampToByte(b);
            output_row[x * 4 + 1] = ClampToByte(g);
            output_row[x * 4 + 2] = ClampToByte(r);
            output_row[x * 4 + 3] = 255;
        }
    }
}

bool GetAllocatedString(IMFActivate* activate, REFGUID key, std::wstring* value) {
    wchar_t* buffer = nullptr;
    UINT32 length = 0;
    const HRESULT hr = activate->GetAllocatedString(key, &buffer, &length);
    if (FAILED(hr)) {
        return false;
    }

    value->assign(buffer, length);
    CoTaskMemFree(buffer);
    return true;
}

}  // namespace

CameraCapture::CameraCapture() = default;

CameraCapture::~CameraCapture() {
    Shutdown();
}

bool CameraCapture::Initialize() {
    if (media_foundation_started_) {
        return true;
    }

    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"MFStartup failed", hr));
        spdlog::error("{}", Narrow(state_.last_error));
        return false;
    }

    media_foundation_started_ = true;
    {
        std::scoped_lock lock(state_mutex_);
        state_.media_foundation_ready = true;
    }

    if (!EnumerateDevices()) {
        return false;
    }

    return true;
}

void CameraCapture::Shutdown() {
    stop_requested_ = true;
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    {
        std::scoped_lock lock(state_mutex_);
        state_.running = false;
    }

    {
        std::scoped_lock lock(capture_frame_mutex_);
        latest_capture_frame_ = {};
    }

    devices_.clear();

    if (media_foundation_started_) {
        MFShutdown();
        media_foundation_started_ = false;
    }
}

bool CameraCapture::StartDefaultDevice() {
    if (!media_foundation_started_ && !Initialize()) {
        return false;
    }

    if (devices_.empty()) {
        SetLastError(L"No camera devices were found.");
        return false;
    }

    stop_requested_ = false;
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    const DeviceInfo device = devices_.front();
    capture_thread_ = std::thread(&CameraCapture::CaptureLoop, this, device.id, device.name);
    return true;
}

CameraCapture::StateSnapshot CameraCapture::GetStateSnapshot() const {
    std::scoped_lock lock(state_mutex_);
    return state_;
}

std::optional<CameraCapture::CaptureFrame> CameraCapture::GetLatestCaptureFrame() const {
    std::scoped_lock lock(capture_frame_mutex_);
    if (!latest_capture_frame_.valid) {
        return std::nullopt;
    }

    return latest_capture_frame_;
}

bool CameraCapture::EnumerateDevices() {
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"MFCreateAttributes failed during camera enumeration", hr));
        return false;
    }

    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"Failed to set video capture source type", hr));
        return false;
    }

    IMFActivate** raw_devices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attributes.Get(), &raw_devices, &count);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"MFEnumDeviceSources failed", hr));
        return false;
    }

    devices_.clear();
    for (UINT32 index = 0; index < count; ++index) {
        ComPtr<IMFActivate> activate;
        activate.Attach(raw_devices[index]);

        DeviceInfo device;
        if (!GetAllocatedString(activate.Get(), MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &device.id)) {
            continue;
        }
        if (!GetAllocatedString(activate.Get(), MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &device.name)) {
            device.name = L"Unknown camera";
        }

        devices_.push_back(std::move(device));
    }

    CoTaskMemFree(raw_devices);

    {
        std::scoped_lock lock(state_mutex_);
        state_.device_count = devices_.size();
        if (devices_.empty()) {
            state_.last_error = L"No camera devices were found.";
        } else {
            state_.last_error.clear();
        }
    }

    return true;
}

void CameraCapture::CaptureLoop(std::wstring device_id, std::wstring device_name) {
    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(com_hr);
    if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE) {
        SetLastError(HResultMessage(L"CoInitializeEx failed in capture thread", com_hr));
        return;
    }

    auto cleanup = [&]() {
        if (com_initialized) {
            CoUninitialize();
        }
    };

    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"MFCreateAttributes failed in capture thread", hr));
        cleanup();
        return;
    }

    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"Failed to set capture source type", hr));
        cleanup();
        return;
    }

    IMFActivate** raw_devices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attributes.Get(), &raw_devices, &count);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"MFEnumDeviceSources failed in capture thread", hr));
        cleanup();
        return;
    }

    ComPtr<IMFMediaSource> media_source;
    for (UINT32 index = 0; index < count; ++index) {
        ComPtr<IMFActivate> activate;
        activate.Attach(raw_devices[index]);

        std::wstring current_id;
        if (!GetAllocatedString(activate.Get(), MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &current_id)) {
            continue;
        }

        if (current_id != device_id) {
            continue;
        }

        hr = activate->ActivateObject(IID_PPV_ARGS(&media_source));
        if (FAILED(hr)) {
            CoTaskMemFree(raw_devices);
            SetLastError(HResultMessage(L"Failed to activate selected camera", hr));
            cleanup();
            return;
        }

        break;
    }

    CoTaskMemFree(raw_devices);

    if (!media_source) {
        SetLastError(L"Selected camera disappeared before capture could start.");
        cleanup();
        return;
    }

    ComPtr<IMFSourceReader> source_reader;
    hr = MFCreateSourceReaderFromMediaSource(media_source.Get(), nullptr, &source_reader);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"MFCreateSourceReaderFromMediaSource failed", hr));
        cleanup();
        return;
    }

    ComPtr<IMFMediaType> requested_type;
    hr = MFCreateMediaType(&requested_type);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"MFCreateMediaType failed", hr));
        cleanup();
        return;
    }

    requested_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    requested_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = source_reader->SetCurrentMediaType(kVideoStreamIndex, nullptr, requested_type.Get());
    bool using_nv12_fallback = false;
    if (FAILED(hr)) {
        requested_type.Reset();
        hr = MFCreateMediaType(&requested_type);
        if (FAILED(hr)) {
            SetLastError(HResultMessage(L"MFCreateMediaType failed for NV12 fallback", hr));
            cleanup();
            return;
        }

        requested_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        requested_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        hr = source_reader->SetCurrentMediaType(kVideoStreamIndex, nullptr, requested_type.Get());
        if (FAILED(hr)) {
            SetLastError(HResultMessage(L"Failed to request RGB32 or NV12 camera frames", hr));
            cleanup();
            return;
        }

        using_nv12_fallback = true;
    }

    ComPtr<IMFMediaType> current_type;
    hr = source_reader->GetCurrentMediaType(kVideoStreamIndex, &current_type);
    if (FAILED(hr)) {
        SetLastError(HResultMessage(L"GetCurrentMediaType failed", hr));
        cleanup();
        return;
    }

    UINT32 width = 0;
    UINT32 height = 0;
    GUID negotiated_subtype = GUID_NULL;
    hr = MFGetAttributeSize(current_type.Get(), MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) {
        width = 0;
        height = 0;
    }
    current_type->GetGUID(MF_MT_SUBTYPE, &negotiated_subtype);

    {
        std::scoped_lock lock(state_mutex_);
        state_.active_device_name = std::move(device_name);
        state_.running = true;
        state_.frame_received = false;
        state_.width = width;
        state_.height = height;
        state_.frame_count = 0;
        state_.last_sample_time_hns = 0;
        state_.negotiated_subtype = SubtypeName(negotiated_subtype);
        state_.using_nv12_fallback = using_nv12_fallback;
        state_.last_error.clear();
    }

    spdlog::info(
        "Camera capture started: {} ({}x{}, {})",
        Narrow(state_.active_device_name),
        width,
        height,
        Narrow(state_.negotiated_subtype));

    while (!stop_requested_) {
        DWORD stream_index = 0;
        DWORD stream_flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;

        hr = source_reader->ReadSample(
            kVideoStreamIndex,
            0,
            &stream_index,
            &stream_flags,
            &timestamp,
            &sample);

        if (FAILED(hr)) {
            SetLastError(HResultMessage(L"ReadSample failed", hr));
            break;
        }

        if ((stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            SetLastError(L"Camera stream reached end of stream.");
            break;
        }

        if ((stream_flags & MF_SOURCE_READERF_STREAMTICK) != 0 || !sample) {
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) {
            SetLastError(HResultMessage(L"ConvertToContiguousBuffer failed", hr));
            break;
        }

        std::uint8_t* source_bytes = nullptr;
        DWORD max_length = 0;
        DWORD current_length = 0;
        hr = buffer->Lock(&source_bytes, &max_length, &current_length);
        if (FAILED(hr)) {
            SetLastError(HResultMessage(L"Failed to lock camera sample buffer", hr));
            break;
        }

        CaptureFrame capture_frame;
        capture_frame.valid = true;
        capture_frame.width = width;
        capture_frame.height = height;
        capture_frame.stride = width * 4;
        capture_frame.timestamp_hns = timestamp;

        if (negotiated_subtype == MFVideoFormat_NV12) {
            ConvertNv12ToBgra(source_bytes, width, height, width, &capture_frame.pixels);
        } else {
            capture_frame.pixels.resize(static_cast<std::size_t>(capture_frame.stride) * capture_frame.height);
            const std::size_t source_stride = width * 4;
            const std::size_t required_bytes = source_stride * height;
            if (current_length < required_bytes) {
                buffer->Unlock();
                SetLastError(L"RGB32 camera frame was smaller than expected.");
                break;
            }

            for (std::uint32_t row = 0; row < height; ++row) {
                std::memcpy(
                    capture_frame.pixels.data() + static_cast<std::size_t>(row) * capture_frame.stride,
                    source_bytes + static_cast<std::size_t>(row) * source_stride,
                    capture_frame.stride);
            }
        }

        buffer->Unlock();

        std::scoped_lock lock(state_mutex_);
        state_.frame_received = true;
        ++state_.frame_count;
        state_.last_sample_time_hns = timestamp;

        capture_frame.frame_count = state_.frame_count;

        {
            std::scoped_lock preview_lock(capture_frame_mutex_);
            latest_capture_frame_ = std::move(capture_frame);
        }
    }

    {
        std::scoped_lock lock(state_mutex_);
        state_.running = false;
    }

    cleanup();
}

void CameraCapture::SetLastError(std::wstring message) {
    {
        std::scoped_lock lock(state_mutex_);
        state_.last_error = std::move(message);
        state_.running = false;
    }

    spdlog::warn("{}", Narrow(state_.last_error));
}

}  // namespace nohcam
