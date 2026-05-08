import os
import sys
import time
import argparse
from collections import deque

from .tracker import create_tracker, TrackingResult


def resolve_model_path(model_input: str) -> str | None:
    # 1. Try as exact path
    if os.path.isfile(model_input):
        return os.path.abspath(model_input)
    
    # 2. Try adding extension
    model_with_ext = model_input if model_input.endswith(".model3.json") else model_input + ".model3.json"
    if os.path.isfile(model_with_ext):
        return os.path.abspath(model_with_ext)
        
    # 3. Search recursively in current directory
    search_filename = os.path.basename(model_with_ext).lower()
    for root, _, files in os.walk("."):
        for file in files:
            if file.lower() == search_filename:
                return os.path.abspath(os.path.join(root, file))
    
    return None


def format_result(result: TrackingResult) -> dict:
    data = {
        "frame": result.frame,
        "timestamp_ms": result.timestamp_ms,
    }

    if result.face:
        data["face"] = [{"x": lm.x, "y": lm.y, "z": lm.z} for lm in result.face]
    else:
        data["face"] = []

    if result.hands:
        data["hands"] = [
            {
                "handedness": h.handedness,
                "landmarks": [{"x": lm.x, "y": lm.y, "z": lm.z} for lm in h.landmarks]
            }
            for h in result.hands
        ]
    else:
        data["hands"] = []

    if result.pose:
        data["pose"] = [{"x": lm.x, "y": lm.y, "z": lm.z} for lm in result.pose]
    else:
        data["pose"] = []

    return data


class FPSMeter:
    def __init__(self, window_seconds: float = 5.0):
        self.timestamps = deque()
        self.window_seconds = window_seconds

    def update(self) -> float:
        now = time.perf_counter()
        self.timestamps.append(now)

        while self.timestamps and now - self.timestamps[0] > self.window_seconds:
            self.timestamps.popleft()

        if len(self.timestamps) > 1:
            duration = self.timestamps[-1] - self.timestamps[0]
            return len(self.timestamps) / duration if duration > 0 else 0.0
        return 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--debug-landmarks", action="store_true", help="Launch tracker GUI with landmark visualization")
    parser.add_argument("--model", help="Path or name of .model3.json to launch Live2D viewer directly")
    parser.add_argument("--list-models", action="store_true", help="List available models (*.model3.json) in current directory and exit")
    parser.add_argument("--list-cameras", action="store_true", help="List available cameras and exit")
    parser.add_argument("--camera", type=int, help="Camera index to use")
    parser.add_argument("--background", help="Path to background image")
    args = parser.parse_args()

    from .tracker import get_camera_list, find_default_camera_id

    if args.list_cameras:
        cameras = get_camera_list()
        print("Available cameras:")
        for cam in cameras:
            print(f"  {cam['id']}: {cam['name']}")
        return

    if args.list_models:
        from .app import find_all_models
        models = find_all_models(".")
        if not models:
            print("No models found in the current directory.")
        else:
            print("Available models:")
            for m in models:
                print(f"  {os.path.normpath(m)}")
        return

    # Determine camera ID
    if args.camera is not None:
        camera_id = args.camera
    else:
        camera_id = find_default_camera_id()

    # If --debug-landmarks is requested, launch the GUI
    if args.debug_landmarks:
        from .gui import main as gui_main
        gui_main(camera_id=camera_id)
        return

    # If a specific model is provided, launch Live2D viewer with it
    if args.model:
        model_path = resolve_model_path(args.model)
        if not model_path:
            print(f"Error: Could not find model matching '{args.model}'", file=sys.stderr)
            sys.exit(1)
        
        from .app import main as app_main
        app_main(model_path=model_path, camera_id=camera_id, background_path=args.background)
        return

    # If no arguments provided (other than the script name), try interactive selection
    if args.model is None:
        from .app import select_model_interactively, main as app_main
        selected_model = select_model_interactively()
        if selected_model:
            app_main(model_path=selected_model, camera_id=camera_id, background_path=args.background)
            return
        # If no models found or user canceled, the application exits.
    print("No model selected. Exiting.", file=sys.stderr)
    sys.exit(0)


if __name__ == "__main__":
    main()
