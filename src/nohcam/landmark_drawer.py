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


FACEMESH_LIPS = [(61, 146), (146, 91), (91, 181), (181, 84), (84, 17), (17, 314), (314, 405), (405, 321), (321, 375), (375, 291), (61, 185), (185, 40), (40, 39), (39, 37), (37, 0), (0, 267), (267, 269), (269, 270), (270, 409), (409, 291), (78, 95), (95, 88), (88, 178), (178, 87), (87, 14), (14, 317), (317, 402), (402, 318), (318, 324), (324, 308), (78, 191), (191, 80), (80, 81), (81, 82), (82, 13), (13, 312), (312, 311), (311, 310), (310, 415), (415, 308)]
FACEMESH_LEFT_EYE = [(263, 249), (249, 390), (390, 373), (373, 374), (374, 380), (380, 381), (381, 382), (382, 362), (263, 466), (466, 388), (388, 387), (387, 386), (386, 385), (385, 384), (384, 398), (398, 362)]
FACEMESH_RIGHT_EYE = [(33, 7), (7, 163), (163, 144), (144, 145), (145, 153), (153, 154), (154, 155), (155, 133), (33, 246), (246, 161), (161, 160), (160, 159), (159, 158), (158, 157), (157, 173), (173, 133)]
FACEMESH_LEFT_IRIS = [(474, 475), (475, 476), (476, 477), (477, 474)]
FACEMESH_RIGHT_IRIS = [(469, 470), (470, 471), (471, 472), (472, 469)]


def draw_landmarks(frame, face_landmarks, left_hand_landmarks, right_hand_landmarks, pose_landmarks, detail_face=False):
    """Draw face, hand, and pose landmarks on a frame."""
    h, w = frame.shape[:2]
    
    # Background fill: Transparent if RGBA, black if BGR
    if frame.shape[2] == 4:
        frame[:] = (0, 0, 0, 0)
    else:
        frame[:] = (0, 0, 0)
    
    # For RGBA images, preserve transparency
    is_rgba = frame.shape[2] == 4
    
    if face_landmarks:
        face_pts = [(int(lm.x * w), int(lm.y * h)) for lm in face_landmarks]

        if detail_face and len(face_pts) > 0:
            # DEBUG: Check coordinate ranges
            xs = [pt[0] for pt in face_pts]
            ys = [pt[1] for pt in face_pts]
            print(f"DEBUG: Face points x range: {min(xs)}-{max(xs)}, y range: {min(ys)}-{max(ys)} (frame: {w}x{h})", flush=True)

        if detail_face:
            # Draw detailed mesh connections for Face Landmarker
            # Red lines (RGB: 255, 0, 0), Cyan points (RGB: 0, 255, 255)
            color_line = (255, 0, 0, 255) if is_rgba else (255, 0, 0) # Red
            color_point = (0, 255, 255, 255) if is_rgba else (0, 255, 255) # Cyan

            # 1. Draw points (Circles) FIRST
            for pt in face_pts:
                cv2.circle(frame, pt, 2, color_point, -1)

            # 2. Draw connections (Lines) SECOND (on top)
            connections_count = 0
            for connections in [FACEMESH_LIPS, FACEMESH_LEFT_EYE, FACEMESH_RIGHT_EYE, FACEMESH_LEFT_IRIS, FACEMESH_RIGHT_IRIS]:
                for i, j in connections:
                    if i < len(face_pts) and j < len(face_pts):
                        cv2.line(frame, face_pts[i], face_pts[j], color_line, 2)
                        connections_count += 1

            print(f"DEBUG: Draw landmarks (detail) drew {connections_count} lines and {len(face_pts)} points.", flush=True)
        else:
            # Draw simplified outline for Holistic
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
