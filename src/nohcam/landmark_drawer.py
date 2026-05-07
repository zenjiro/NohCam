"""Utility module for drawing MediaPipe landmarks on images."""

import cv2
import numpy as np


HAND_CONNECTIONS = [
    (0, 1), (1, 2), (2, 3), (3, 4),
    (0, 5), (5, 6), (6, 7), (7, 8),
    (5, 9), (9, 10), (10, 11), (11, 12),
    (9, 13), (13, 14), (14, 15), (15, 16),
    (13, 17), (17, 18), (18, 19), (19, 20),
]

POSE_CONNECTIONS = [
    (0, 1), (1, 2), (2, 3), (3, 7),
    (0, 4), (4, 5), (5, 6), (6, 8),
    (9, 10),
    (11, 12),
    (11, 13), (13, 15), (15, 17), (15, 19), (15, 21), (17, 19),
    (12, 14), (14, 16), (16, 18), (16, 20), (16, 22), (18, 20),
    (11, 23), (12, 24), (23, 24),
    (23, 25), (24, 26),
    (25, 27), (26, 28),
    (27, 29), (28, 30),
    (29, 31), (30, 32),
    (27, 31), (28, 32),
]

FACE_OUTLINE = [10, 338, 297, 332, 284, 328, 291, 324, 318, 196, 389, 394, 364, 292, 439, 276, 53, 412, 476, 356, 11]


def draw_landmarks(frame, face_landmarks, left_hand_landmarks, right_hand_landmarks, pose_landmarks):
    """Draw face, hand, and pose landmarks on a frame."""
    h, w = frame.shape[:2]
    
    # For RGBA images, preserve transparency
    is_rgba = frame.shape[2] == 4
    
    if face_landmarks:
        face_pts = [(int(lm.x * w), int(lm.y * h)) for lm in face_landmarks]
        
        for i in range(len(face_pts) - 1):
            cv2.line(frame, face_pts[i], face_pts[i + 1], (0, 255, 0, 255) if is_rgba else (0, 255, 0), 1)
        
        cv2.polylines(frame, [np.array([face_pts[i] for i in FACE_OUTLINE], np.int32)], True, (0, 255, 0, 255) if is_rgba else (0, 255, 0), 2)
    
    if left_hand_landmarks:
        hand_pts = [(int(lm.x * w), int(lm.y * h)) for lm in left_hand_landmarks]
        _draw_hand(frame, hand_pts, is_rgba)
    
    if right_hand_landmarks:
        hand_pts = [(int(lm.x * w), int(lm.y * h)) for lm in right_hand_landmarks]
        _draw_hand(frame, hand_pts, is_rgba)
    
    if pose_landmarks:
        pose_pts = [(int(lm.x * w), int(lm.y * h)) for lm in pose_landmarks]
        _draw_pose(frame, pose_pts, is_rgba)
    
    return frame


def _draw_hand(frame, pts, is_rgba=False):
    """Draw hand landmarks and connections."""
    color_line = (0, 255, 0, 255) if is_rgba else (0, 255, 0)
    color_circle = (0, 0, 255, 255) if is_rgba else (0, 0, 255)
    
    for i, j in HAND_CONNECTIONS:
        if i < len(pts) and j < len(pts):
            cv2.line(frame, pts[i], pts[j], color_line, 2)
    
    for pt in pts:
        cv2.circle(frame, pt, 3, color_circle, -1)


def _draw_pose(frame, pts, is_rgba=False):
    """Draw pose landmarks and connections."""
    color_line = (0, 255, 0, 255) if is_rgba else (0, 255, 0)
    color_circle = (0, 0, 255, 255) if is_rgba else (0, 0, 255)
    color_nose = (255, 0, 0, 255) if is_rgba else (255, 0, 0)
    
    for i, j in POSE_CONNECTIONS:
        if i < len(pts) and j < len(pts):
            cv2.line(frame, pts[i], pts[j], color_line, 2)
    
    for pt in pts:
        cv2.circle(frame, pt, 3, color_circle, -1)
    
    nose_idx = [0, 1, 2, 3, 4, 5, 6]
    for idx in nose_idx:
        if idx < len(pts):
            cv2.circle(frame, pts[idx], 5, color_nose, -1)
