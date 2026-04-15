import os
import cv2
import mediapipe as mp
import numpy as np
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
from dataclasses import dataclass
from typing import Optional


@dataclass
class FaceLandmarkData:
    x: float
    y: float
    z: float


@dataclass
class HandData:
    handedness: str
    landmarks: list[FaceLandmarkData]


@dataclass
class PoseLandmarkData:
    x: float
    y: float
    z: float


@dataclass
class TrackingResult:
    frame: int
    timestamp_ms: int
    face: list[FaceLandmarkData]
    hands: list[HandData]
    pose: list[PoseLandmarkData]


CameraWidth = 1920
CameraHeight = 1080
CameraFps = 30

ProcessWidth = 640
ProcessHeight = 480


class Tracker:
    def __init__(self, camera_id: int = 0):
        self.camera_id = camera_id
        self.cap: Optional[cv2.VideoCapture] = None

        _DIR = os.path.dirname(os.path.abspath(__file__))
        base_options = python.BaseOptions(
            model_asset_path=os.path.join(_DIR, "..", "..", "models", "holistic_landmarker.task")
        )
        options = vision.HolisticLandmarkerOptions(
            base_options=base_options,
            running_mode=vision.RunningMode.VIDEO,
        )
        self.detector = vision.HolisticLandmarker.create_from_options(options)

        self.frame_count = 0
        self.start_time = 0

    def start(self):
        self.cap = cv2.VideoCapture(self.camera_id)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, CameraWidth)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CameraHeight)
        self.cap.set(cv2.CAP_PROP_FPS, CameraFps)
        self.start_time = cv2.getTickCount()

    def stop(self):
        if self.cap:
            self.cap.release()
            self.cap = None

    def process_frame(self) -> Optional[TrackingResult]:
        if not self.cap:
            return None

        ret, frame = self.cap.read()
        if not ret:
            return None

        self.frame_count += 1

        timestamp_ms = int((cv2.getTickCount() - self.start_time) / cv2.getTickFrequency() * 1000)

        resized = cv2.resize(frame, (ProcessWidth, ProcessHeight))
        rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

        result = self.detector.detect_for_video(mp_image, timestamp_ms)

        face_landmarks = []
        if result.face_landmarks:
            for lm in result.face_landmarks:
                face_landmarks.append(FaceLandmarkData(x=lm.x, y=lm.y, z=lm.z))

        hands_data = []
        if result.left_hand_landmarks:
            hand_landmark_list = []
            for lm in result.left_hand_landmarks:
                hand_landmark_list.append(FaceLandmarkData(x=lm.x, y=lm.y, z=lm.z))
            hands_data.append(HandData(handedness="Left", landmarks=hand_landmark_list))

        if result.right_hand_landmarks:
            hand_landmark_list = []
            for lm in result.right_hand_landmarks:
                hand_landmark_list.append(FaceLandmarkData(x=lm.x, y=lm.y, z=lm.z))
            hands_data.append(HandData(handedness="Right", landmarks=hand_landmark_list))

        pose_landmarks = []
        if result.pose_landmarks:
            for lm in result.pose_landmarks:
                pose_landmarks.append(PoseLandmarkData(x=lm.x, y=lm.y, z=lm.z))

        return TrackingResult(
            frame=self.frame_count,
            timestamp_ms=timestamp_ms,
            face=face_landmarks,
            hands=hands_data,
            pose=pose_landmarks
        )


def create_tracker(camera_id: int = 0) -> Tracker:
    return Tracker(camera_id)
