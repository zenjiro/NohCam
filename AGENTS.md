# NohCam Development Guide

## Project Overview

NohCam is a Python application that creates virtual camera output with Live2D avatars driven by face, hand, and pose tracking via MediaPipe.

## Tech Stack

- **Language**: Python 3.11+
- **Package Manager**: [uv](https://github.com/astral-sh/uv)
- **AI Tracking**: MediaPipe (Holistic Landmarker)
- **GUI**: wxPython (Tracker Debugger)
- **Rendering**: Pygame, PyOpenGL
- **Live2D**: [live2d-py](https://github.com/Arkueid/live2d-py)
- **Virtual Camera**: [pyvirtualcam](https://github.com/letmaik/pyvirtualcam)

## Commands

```powershell
# Install dependencies
uv sync

# Run tracker CLI (outputs JSON to stdout)
uv run nohcam

# Run tracker GUI (visual debugger)
uv run nohcam --debug-landmarks

# List available cameras
uv run nohcam --list-cameras

# Run Live2D viewer (interactive model selection)
uv run nohcam

# Run Live2D viewer with a specific model
uv run nohcam --model path/to/model.model3.json

# Specify camera for Live2D viewer
uv run nohcam --model path/to/model.model3.json --camera 1
```

## Module Structure

| Directory / File | Purpose |
|-----------|---------|
| `src/nohcam/` | Core package directory |
| `src/nohcam/__main__.py` | CLI entry point and orchestration |
| `src/nohcam/app.py` | Live2D rendering loop and tracking integration (Pygame/OpenGL) |
| `src/nohcam/gui.py` | Visual debugger GUI for MediaPipe landmarks (wxPython) |
| `src/nohcam/tracker.py` | MediaPipe Holistic Landmarker wrapper |
| `models/` | AI model files (e.g., `holistic_landmarker.task`) |

## Directory Structure

```
D:\git\NohCam\
├───docs\                # Design documents and plans
├───models\              # MediaPipe task models
├───src\
│   └───nohcam\          # Main Python package
│       ├───app.py       # Live2D application
│       ├───gui.py       # Tracking GUI
│       ├───tracker.py   # MediaPipe tracker
│       └───__main__.py  # Entry point
├───pyproject.toml       # Project metadata and dependencies
└───uv.lock              # Lock file for dependencies
```


## Development Notes

- **Tracking**: Uses MediaPipe Holistic Landmarker for simultaneous face, hand, and pose tracking.
- **Live2D Integration**: Uses `live2d-py` (Cubism 3+ support). Parameters are mapped from MediaPipe landmarks in `app.py`.
- **Performance**: OpenCV is used for camera capture (`cv2.VideoCapture`). The tracker runs in the same thread or can be orchestrated via CLI.
- **Dependencies**: Managed via `pyproject.toml` and `uv`. Always use `uv add <package>` to add new dependencies.
