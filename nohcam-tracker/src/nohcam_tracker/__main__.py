import sys
import json
import time
import argparse
from collections import deque

from .tracker import create_tracker, TrackingResult


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
    parser.add_argument("--gui", action="store_true", help="Launch GUI")
    args = parser.parse_args()

    if args.gui:
        from .gui import main as gui_main
        gui_main()
        return

    tracker = create_tracker()
    tracker.start()

    fps_meter = FPSMeter()

    print("nohcam-tracker started", file=sys.stderr)
    sys.stderr.flush()

    try:
        while True:
            result = tracker.process_frame()
            if result:
                data = format_result(result)
                print(json.dumps(data), flush=True)

                fps = fps_meter.update()
                if result.frame % 30 == 0:
                    print(f"FPS: {fps:.2f}", file=sys.stderr)
    except KeyboardInterrupt:
        pass
    finally:
        tracker.stop()


if __name__ == "__main__":
    main()
