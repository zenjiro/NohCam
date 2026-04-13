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

```powershell
uv run nohcam-tracker
```

Or:

```powershell
python -m nohcam_tracker
```

Output JSONL format:

```json
{"frame":1,"timestamp_ms":504,"face":[],"hands":[{"handedness":"Left","landmarks":[{"x":0.582,"y":0.803,"z":0.0},...]}],"pose":[{"x":0.489,"y":0.322,"z":-0.333}]}
```

Fields:
- `frame`: Frame number
- `timestamp_ms`: Timestamp in milliseconds
- `face`: Face landmarks (empty by default with HolisticTasks API)
- `hands`: Hand landmarks (21 points each, including handedness)
- `pose`: Pose landmarks (33 points)

## C++ Integration

From C++, spawn as subprocess with stdout redirected:

```cpp
PROCESS_INFORMATION pi;
STARTUPINFOA si = {sizeof(si)};
CreateProcessA("python.exe", "-m nohcam_tracker", ..., &pi, &si);
// Read JSONL from stdout pipe
```

## Notes

- Face mesh is not included by default with HolisticTasks API
- Performance: ~8-15 FPS on Windows without GPU