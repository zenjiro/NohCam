from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Callable

import cv2
import mediapipe as mp
import numpy as np
from mediapipe.tasks import python
from mediapipe.tasks.python import vision


@dataclass
class PoseFrame:
    timestamp_s: float
    world_points: np.ndarray
    visibility: np.ndarray


def extract_pose_frame(result: vision.PoseLandmarkerResult, timestamp_s: float, visibility_threshold: float, prev_points: np.ndarray | None) -> PoseFrame | None:
    if not result.pose_world_landmarks:
        return None
    lms = result.pose_world_landmarks[0]
    points = np.zeros((33, 3), dtype=np.float64)
    visibility = np.zeros((33,), dtype=np.float64)
    for i, lm in enumerate(lms):
        visibility[i] = float(lm.visibility)
        # MediaPipe world landmarks use +Y downward.
        # Convert to application space with +Y upward before solving.
        candidate = np.array([lm.x, -lm.y, lm.z], dtype=np.float64)
        if visibility[i] >= visibility_threshold or prev_points is None:
            points[i] = candidate
        else:
            points[i] = prev_points[i]
    return PoseFrame(timestamp_s=timestamp_s, world_points=points, visibility=visibility)


class PoseLandmarkStream:
    def __init__(self, model_path: str, camera_index: int) -> None:
        self.cap = cv2.VideoCapture(camera_index)
        self._result: vision.PoseLandmarkerResult | None = None

        options = vision.PoseLandmarkerOptions(
            base_options=python.BaseOptions(model_asset_path=model_path),
            running_mode=vision.RunningMode.LIVE_STREAM,
            result_callback=self._on_result,
        )
        self.landmarker = vision.PoseLandmarker.create_from_options(options)

    def _on_result(self, result: vision.PoseLandmarkerResult, output_image: object, timestamp_ms: int) -> None:
        self._result = result

    def read(self) -> tuple[np.ndarray | None, vision.PoseLandmarkerResult | None, float]:
        ok, frame = self.cap.read()
        now_s = time.time()
        if not ok:
            return None, None, now_s
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
        self.landmarker.detect_async(mp_image, int(now_s * 1000))
        return frame, self._result, now_s

    def close(self) -> None:
        self.cap.release()
        self.landmarker.close()


def list_cameras(max_index: int = 10) -> list[dict[str, object]]:
    cameras: list[dict[str, object]] = []
    for idx in range(max_index):
        cap = cv2.VideoCapture(idx, cv2.CAP_DSHOW)
        opened = cap.isOpened()
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)) if opened else 0
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT)) if opened else 0
        backend_id = int(cap.get(cv2.CAP_PROP_BACKEND)) if opened else -1
        backend_name = cap.getBackendName() if opened else "N/A"
        cameras.append(
            {
                "index": idx,
                "opened": opened,
                "width": width,
                "height": height,
                "backend_id": backend_id,
                "backend_name": backend_name,
            }
        )
        cap.release()
    return cameras


def stream_pose(model_path: str, camera_index: int, on_result: Callable[[PoseLandmarkerResult, float], None]) -> None:
    stream = PoseLandmarkStream(model_path, camera_index)
    try:
        while True:
            _, result, ts = stream.read()
            if result is not None:
                on_result(result, ts)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
    finally:
        stream.close()
