import os
import sys
import platform
import cv2
import mediapipe as mp
import numpy as np
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
from dataclasses import dataclass
from typing import Optional, List, Dict


def get_camera_list() -> List[Dict]:
    """Get a list of available camera indices and their names."""
    cameras = []
    
    if platform.system() == "Windows":
        try:
            from pygrabber.dshow_graph import FilterGraph
            graph = FilterGraph()
            devices = graph.get_input_devices()
            for i, name in enumerate(devices):
                # Verify if it can be opened by OpenCV
                cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)
                if cap.isOpened():
                    cameras.append({"id": i, "name": name})
                    cap.release()
        except ImportError:
            # Fallback if pygrabber is not available
            for i in range(5):
                cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)
                if cap.isOpened():
                    cameras.append({"id": i, "name": f"Camera {i}"})
                    cap.release()
    else:
        # Non-Windows platforms
        for i in range(5):
            cap = cv2.VideoCapture(i)
            if cap.isOpened():
                cameras.append({"id": i, "name": f"Camera {i}"})
                cap.release()
                
    return cameras


def find_default_camera_id() -> int:
    """Find the first camera that doesn't look like a virtual camera."""
    cameras = get_camera_list()
    if not cameras:
        return 0
    
    # Try to find a non-virtual camera
    for cam in cameras:
        name = cam["name"].lower()
        if "virtual" not in name:
            return cam["id"]
            
    # Fallback to the first available camera
    return cameras[0]["id"]


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
    face_mesh: list[FaceLandmarkData] = None
    blendshapes: Dict[str, float] = None


CameraFps = 30

ProcessWidth = 640
ProcessHeight = 480


class Tracker:
    def __init__(self, camera_id: int = 0):
        self.camera_id = camera_id
        self.cap: Optional[cv2.VideoCapture] = None

        _DIR = os.path.dirname(os.path.abspath(__file__))
        model_path = os.path.join(_DIR, "..", "..", "models", "holistic_landmarker.task")
        face_model_path = os.path.join(_DIR, "..", "..", "models", "face_landmarker.task")

        # Holistic Landmarker
        base_options = python.BaseOptions(model_asset_path=model_path)
        options = vision.HolisticLandmarkerOptions(
            base_options=base_options,
            running_mode=vision.RunningMode.VIDEO,
            min_face_detection_confidence=0.3,
            min_face_suppression_threshold=0.3,
            min_face_landmarks_confidence=0.3,
            min_pose_detection_confidence=0.3,
            min_pose_suppression_threshold=0.3,
            min_pose_landmarks_confidence=0.3,
        )
        self.detector = vision.HolisticLandmarker.create_from_options(options)

        # Face Landmarker for Blendshapes
        face_base_options = python.BaseOptions(face_model_path)
        face_options = vision.FaceLandmarkerOptions(
            base_options=face_base_options,
            running_mode=vision.RunningMode.VIDEO,
            output_face_blendshapes=True,
            min_face_detection_confidence=0.1,  # Lowered for better sensitivity
            min_face_presence_confidence=0.1,
            min_tracking_confidence=0.1,
        )
        self.face_detector = vision.FaceLandmarker.create_from_options(face_options)

        self.frame_count = 0
        self.start_time = 0

    def start(self):
        self.cap = cv2.VideoCapture(self.camera_id, cv2.CAP_DSHOW)
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

        # Run Holistic
        result = self.detector.detect_for_video(mp_image, timestamp_ms)
        
        # Run Face Landmarker
        face_result = self.face_detector.detect_for_video(mp_image, timestamp_ms)

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

        # Face Landmarker details
        face_mesh = []
        blendshapes = {}
        if face_result.face_landmarks:
            # face_result.face_landmarks is a list of lists (for each face)
            # We take the first face detected
            for lm in face_result.face_landmarks[0]:
                face_mesh.append(FaceLandmarkData(x=lm.x, y=lm.y, z=lm.z))
        
        if face_result.face_blendshapes:
            for bs in face_result.face_blendshapes[0]:
                blendshapes[bs.category_name] = bs.score

        return TrackingResult(
            frame=self.frame_count,
            timestamp_ms=timestamp_ms,
            face=face_landmarks,
            hands=hands_data,
            pose=pose_landmarks,
            face_mesh=face_mesh if face_mesh else None,
            blendshapes=blendshapes if blendshapes else None
        )


def create_tracker(camera_id: int = 0) -> Tracker:
    return Tracker(camera_id)
