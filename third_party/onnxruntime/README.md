# ONNX Runtime Layout

Place the official prebuilt ONNX Runtime distribution in this directory.

Supported layouts:

- Release zip extracted directly under `third_party/onnxruntime`
- NuGet package contents extracted under `third_party/onnxruntime`

The CMake build looks for:

- headers in `include/` or `build/native/include/`
- import libraries in `lib/`, `lib/x64/`, `build/native/lib/`, or `build/native/lib/x64/`

Keep binaries out of git. This directory is ignored except for this file.
