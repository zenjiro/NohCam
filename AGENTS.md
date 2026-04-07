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
- **GUI (planned)**: WinUI 3

## Build Commands

```powershell
# C++ backend (requires Developer PowerShell for VS)
cmake --build build --config Release

# WinUI 3 frontend (when implemented)
dotnet build src/NohCam.WinUI/NohCam.WinUI.csproj -c Release

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
| ONNX Runtime | 1.24.4 | `third_party/onnxruntime/` |
| Cubism SDK | 5-r.5 | `CubismSdkForNative-5-r.5/` |
| ONNX Models | - | `assets/onnx/` |
| Live2D Models | - | `assets/models/` |

## Current Development Phase

Phase 1 complete (build environment, DX11 init, camera input). Phase 2 in progress (tracking integration).

## Important Notes

- ONNX Runtime must be manually placed in `third_party/onnxruntime/` as prebuilt package
- DirectML requires Windows SDK linkage
- Cubism SDK must be extracted to project root
- All paths use backslash on Windows (handled by CMake PATH variables)
