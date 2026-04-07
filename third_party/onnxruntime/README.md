# ONNX Runtime Layout

This directory stores the official prebuilt ONNX Runtime packages used by NohCam.

Current local setup:

- `Microsoft.ML.OnnxRuntime` `1.24.4`
- `Microsoft.ML.OnnxRuntime.DirectML` `1.24.4`

Expected layout:

- headers in `build/native/include/`
- Windows import libraries and runtime DLLs in `runtimes/win-x64/native/`

Notes:

- Extract the official NuGet packages directly into this directory.
- The DirectML package should be overlaid on top of the base ONNX Runtime package so the DirectML-enabled Windows binaries are present.
- This directory is ignored by git except for this file.
- CMake reads this directory through `NOHCAM_ONNXRUNTIME_ROOT`.
