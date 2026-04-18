# NohCam Development Guide

## Project Overview

NohCam is a Windows desktop application that creates virtual camera output with Live2D avatars driven by face/hand tracking via ONNX Runtime and DirectML.

## Tech Stack

- **Language**: C++20
- **Build System**: CMake 3.21+
- **Platform**: Windows only
- **Rendering**: DirectX 11
- **AI Inference**: ONNX Runtime with DirectML (GPU) / CPU fallback
- **Avatar**: Live2D Cubism SDK for Native 5-r.5
- **Camera**: Media Foundation (Windows)
- **Package Manager**: vcpkg
- **GUI**: WinUI 3 (.NET 8)

## Build Commands

```powershell
# C++ backend (requires Developer PowerShell for VS)
cmake --build build --config Release

# WinUI 3 frontend
dotnet build src/NohCam.WinUI/NohCam.WinUI.csproj -c Release

# After C++ build, copy DLLs to WinUI output
cp build/Release/nohcam_*.dll src/NohCam.WinUI/bin/Release/net8.0-windows10.0.19041.0/

# Tests
.\build\Release\NohCamOnnxSmokeTest.exe
.\build\Release\NohCamFaceTrackerTest.exe
```

## Key CMake Options

- `NOHCAM_ENABLE_CUBISM_FRAMEWORK`: Enable Live2D Cubism SDK (default: ON)
- `NOHCAM_ENABLE_ONNXRUNTIME`: Enable ONNX Runtime tracking (default: ON)
- `NOHCAM_ENABLE_DIRECTML`: Enable DirectML GPU acceleration (default: ON)

## Code Conventions

- **Standard**: C++20, `/permissive-` for MSVC
- **Headers**: `#pragma once`
- **Naming**: PascalCase for types/functions, camelCase for variables, UPPER_SNAKE for macros
- **Compilation**: `/W4 /EHsc /utf-8` for MSVC
- **Unicode**: `UNICODE` and `_UNICODE` defined throughout
- **Windows defines**: `WIN32_LEAN_AND_MEAN`, `NOMINMAX`
- **Dependencies**: spdlog, glm, nlohmann_json via vcpkg

## Module Structure

| Directory | Purpose |
|-----------|---------|
| `src/app/` | Application lifecycle, config |
| `src/capture/` | Camera input via Media Foundation |
| `src/pipeline/` | Processing pipeline, preview tap |
| `src/tracking/` | ONNX Runtime, face/hand tracking |
| `src/render/` | DirectX 11 rendering |
| `src/ui/` | Win32/WinUI windows and controls |
| `src/avatar/` | Live2D Cubism management |
| `src/virtualcam/` | DirectShow virtual camera filter |
| `src/tools/` | Test utilities (OnnxSmokeTest, etc.) |

## Third-Party Dependencies

| Dependency | Version | Location |
|------------|---------|----------|
| ONNX Runtime | 1.23.4 | `third_party/onnxruntime/` |
| Cubism SDK | 5-r.5 | `CubismSdkForNative-5-r.5/` |
| ONNX Models | - | `assets/onnx/` |
| Live2D Models | - | `assets/models/` |

## Current Development Phase

Phase 1-2 complete (build environment, DX11 init, camera input, ONNX face tracking). WinUI 3 face tracking integration working with dark theme UI.

## UI Theme

Dark theme with the following color palette:
- Background: `#1F2937` (gray-900)
- Surface: `#111827` (gray-900 variant)
- Text Primary: `White`
- Text Secondary: `#9CA3AF` (gray-400)
- Border/Divider: `#374151` (gray-700)
- Placeholder: `#6B7280` (gray-500)

## WinUI 3 Integration Notes

### ONNX Runtime DLL Conflict

Windows System32 contains `onnxruntime.dll` v1.17.1 which conflicts with our v1.23.4 build.

**Solution:**
1. Rename our ONNX Runtime DLL: `onnxruntime.dll` ↁE`nohcam_onnxruntime.dll`
2. Use delay-load with custom hook in `FaceTrackerBridge.cpp` to load from app directory

### Camera Frame Format

Most webcams support NV12/MJPG/YUY2, not BGRA8.

**Solution:**
1. Set `MemoryPreference = MediaCaptureMemoryPreference.Cpu` to get SoftwareBitmap
2. Request NV12 format: `_mediaCapture.CreateFrameReaderAsync(frameSource, "NV12")`
3. Convert to BGRA8: `SoftwareBitmap.Convert(bitmap, BitmapPixelFormat.Bgra8, BitmapAlphaMode.Ignore)`
4. Copy pixels: `bitmap.CopyToBuffer(pixelData.AsBuffer())`

### P/Invoke Pixel Data

`IMemoryBufferByteAccess` COM interface cast fails with WinRT types.

**Solution:**
- Use `CopyToBuffer()` + `byte[]` array instead of raw pointer access
- P/Invoke signature: `bool FaceTracker_Track(byte[] pixels, ...)`

## Important Notes

- ONNX Runtime DLL must be renamed to `nohcam_onnxruntime.dll` to avoid System32 conflict
- Copy `nohcam_onnxruntime.dll` and `nohcam_bridge.dll` to WinUI output directory
- Cubism SDK must be extracted to project root
- All paths use backslash on Windows (handled by CMake PATH variables)

## Python Dependencies

Always use `uv` instead of `pip` for Python packages:

```powershell
uv add pygame
```

## Troubleshooting

### XamlCompiler Fails with Exit Code 1

If the XAML compiler fails with exit code 1 during incremental builds, do a clean build:

```powershell
cd src/NohCam.WinUI
rm -rf obj bin
dotnet build -c Release
```

This clears the corrupted incremental build state and rebuilds from scratch.
