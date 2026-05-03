# nohcam-tracker

MediaPipe Face/Hands/Pose tracker with webcam input.

## Features

- Webcam input: 1920x1080 @ 30fps
- Detection: MediaPipe Holistic (Hands 21x2 + Pose 33 landmarks)
- Process resolution: 640x480 (for performance)
- Output: JSONL to stdout

## Requirements

- Python 3.11+
- [uv](https://github.com/astral-sh/uv)

## Setup

```powershell
cd nohcam-tracker
uv sync
```

This will install dependencies and download the model automatically.

## Usage

### 1. Interactive Model Selection (Default)
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

- Face mesh is not included by default with HolisticTasks API
- Performance: ~8-15 FPS on Windows without GPU
