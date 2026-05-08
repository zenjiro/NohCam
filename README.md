# NohCam

Live2D viewer driven by MediaPipe face, hand, and pose tracking from webcam input.

## Features

- Interactive Live2D model selection (`*.model3.json`) from the current directory tree
- Live2D parameter driving from MediaPipe landmarks (face, hands, pose)
- Facial blendshape mapping (blink, mouth open, smile)
- Landmark debug GUI (`--debug-landmarks`)
- Camera discovery and selection (`--list-cameras`, `--camera`)
- Always shares the Live2D viewer output through Spout2 as the `NohCam` sender for SpoutCam
- Processing resolution: 640x480 for tracking performance

## Requirements

- Python 3.11+
- [uv](https://github.com/astral-sh/uv)
- SpoutCam / Spout2 runtime installed on Windows for virtual camera output

## Setup

```powershell
uv sync
```

This installs dependencies. Ensure model assets are available in `models/` and your Live2D model files are accessible from your working directory.

## Usage

### 1. Launch Viewer with Interactive Model Selection (Default)
Run without options to search for Live2D models (`*.model3.json`) in the current directory and subdirectories. Use arrow keys to select a model.

```powershell
uv run nohcam
```

### 2. Launch with a Specific Model
Specify the path to a `.model3.json` file.

```powershell
uv run nohcam --model path/to/model.model3.json
```

### 3. Debug Landmarks
Launch a GUI to visualize MediaPipe landmarks (Face/Hands/Pose) overlaid on the webcam feed.

```powershell
uv run nohcam --debug-landmarks
```

### 4. Camera Selection
You can list and select specific camera devices by their index.

- **List available camera indices:**
  ```powershell
  uv run nohcam --list-cameras
  ```
- **Select a camera by ID (index):**
  ```powershell
  uv run nohcam --camera 1
  ```

**Default Behavior:**
If no camera is specified, the application attempts to automatically select the first non-virtual camera. If you have multiple cameras and the wrong one is selected, use `--list-cameras` to find the correct index and specify it with `--camera`.

## Notes

- There is currently no standalone JSONL tracker mode wired to the CLI entrypoint.
- Face mesh detail is provided through Face Landmarker and used for expression/blendshape extraction.
- Performance depends on hardware; tracking runs on resized frames (640x480).
