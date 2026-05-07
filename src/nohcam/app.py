import sys
import os
import warnings
# warnings.filterwarnings("ignore")
print("Imports successful", flush=True)

import cv2
import numpy as np
import pygame
from pygame.locals import *
import questionary

from OpenGL.GL import *

import live2d.v3 as live2d
from .tracker import Tracker
from live2d.v3.params import StandardParams


WIDTH, HEIGHT = 1280, 720
ARM_TRACKING_GAIN = 2.2

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


def draw_landmarks_overlay(frame: np.ndarray, tracking_result) -> np.ndarray:
    """Draw landmarks on frame from TrackingResult"""
    h, w = frame.shape[:2]

    if tracking_result.face:
        face_pts = [(int(lm.x * w), int(lm.y * h)) for lm in tracking_result.face]
        for i in range(len(face_pts) - 1):
            cv2.line(frame, face_pts[i], face_pts[i + 1], (0, 255, 0), 1)
        cv2.polylines(frame, [np.array([face_pts[i] for i in FACE_OUTLINE], np.int32)], True, (0, 255, 0), 2)

    for hand_data in tracking_result.hands:
        hand_pts = [(int(lm.x * w), int(lm.y * h)) for lm in hand_data.landmarks]
        for i, j in HAND_CONNECTIONS:
            if i < len(hand_pts) and j < len(hand_pts):
                cv2.line(frame, hand_pts[i], hand_pts[j], (0, 255, 0), 2)
        for pt in hand_pts:
            cv2.circle(frame, pt, 3, (0, 0, 255), -1)

    if tracking_result.pose:
        pose_pts = [(int(lm.x * w), int(lm.y * h)) for lm in tracking_result.pose]
        for i, j in POSE_CONNECTIONS:
            if i < len(pose_pts) and j < len(pose_pts):
                cv2.line(frame, pose_pts[i], pose_pts[j], (0, 255, 0), 2)
        for pt in pose_pts:
            cv2.circle(frame, pt, 3, (0, 0, 255), -1)
        nose_idx = [0, 1, 2, 3, 4, 5, 6]
        for idx in nose_idx:
            if idx < len(pose_pts):
                cv2.circle(frame, pose_pts[idx], 5, (255, 0, 0), -1)

    return frame


def draw_texture(texture_id, x, y, width, height):
    """Draw a texture at specified position using OpenGL"""
    glEnable(GL_TEXTURE_2D)
    glBindTexture(GL_TEXTURE_2D, texture_id)
    glColor3f(1.0, 1.0, 1.0)

    glBegin(GL_QUADS)
    glTexCoord2f(0, 1)
    glVertex2f(x, y)
    glTexCoord2f(1, 1)
    glVertex2f(x + width, y)
    glTexCoord2f(1, 0)
    glVertex2f(x + width, y + height)
    glTexCoord2f(0, 0)
    glVertex2f(x, y + height)
    glEnd()

    glDisable(GL_TEXTURE_2D)


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def find_all_models(start_dir: str = ".") -> list[str]:
    """Recursively find all .model3.json files."""
    models = []
    for root, _, files in os.walk(start_dir):
        for file in files:
            if file.endswith(".model3.json"):
                models.append(os.path.normpath(os.path.join(root, file)))
    return models


def select_model_interactively() -> str | None:
    """Find models and let the user select one via a terminal menu."""
    # Search in assets/live2d-models relative to the script
    base_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    model_dir = os.path.join(base_dir, "assets", "live2d-models")
    
    # Search in current directory
    cwd = os.getcwd()
    
    search_dirs = [cwd]
    if os.path.isdir(model_dir):
        search_dirs.append(model_dir)
    
    models_abs = set()
    for d in search_dirs:
        for m in find_all_models(d):
            models_abs.add(os.path.abspath(m))
    
    if not models_abs:
        return None

    # Sort for consistent display
    sorted_models = sorted(list(models_abs), key=lambda x: os.path.basename(x).lower())

    # Format choices for questionary
    choices = [questionary.Choice(title=os.path.basename(m), value=m) for m in sorted_models]
    choices.append(questionary.Choice(title="[Cancel]", value=None))

    selected = questionary.select(
        "Select a Live2D model:",
        choices=choices
    ).ask()

    return selected


def _find_default_model() -> str:
    # Look for any .model3.json in the assets/live2d-models directory
    base_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    model_dir = os.path.join(base_dir, "assets", "live2d-models")
    if not os.path.isdir(model_dir):
        raise FileNotFoundError(
            "Could not find a default Live2D model. Please check the assets/ directory or specify a model with --model."
        )
    for root, dirs, files in os.walk(model_dir):
        for file in files:
            if file.endswith(".model3.json"):
                return os.path.normpath(os.path.join(root, file))
    # If we get here, no model was found
    raise FileNotFoundError(
        "Could not find a default Live2D model. Please check the assets/ directory or specify a model with --model."
    )


def main(model_path: str = None, camera_id: int = 0):
    if model_path is None:
        model_path = _find_default_model()
    model_path = os.path.normpath(model_path)
    print(f"Loading: {model_path}", flush=True)
    if not os.path.isfile(model_path):
        raise FileNotFoundError(
            f"Model file not found: {model_path}\n"
            "Expected a .model3.json file path."
        )
    if not model_path.lower().endswith(".model3.json"):
        raise ValueError(
            f"Invalid model path: {model_path}\n"
            "Expected a .model3.json file."
        )

    print("Main started", flush=True)
    pygame.init()
    print("PyGame initialized", flush=True)
    pygame.display.set_mode((WIDTH, HEIGHT), DOUBLEBUF | OPENGL)
    print("Display mode set", flush=True)
    pygame.display.set_caption("Live2D Tracking - Arrow keys manual, T auto-track")

    live2d.init()
    print("Live2D initialized", flush=True)
    live2d.glInit()
    print("Live2D GL initialized", flush=True)

    model = live2d.LAppModel()
    print("Model instance created", flush=True)
    try:
        model.LoadModelJson(model_path)
    except Exception:
        print("ERROR: LoadModelJson failed.", file=sys.stderr, flush=True)
        raise
    print("Model JSON loaded", flush=True)
    model.Resize(WIDTH, HEIGHT)
    print("Model resized", flush=True)

    model.StopAllMotions()
    model.SetAutoBlinkEnable(False)
    model.SetAutoBreathEnable(False)
    model.ResetParameters()
    print("Model parameters reset", flush=True)

    param_ids = model.GetParamIds()
    print(f"Params ({len(param_ids)}): {param_ids}", flush=True)

    param_angle_x = None
    param_angle_y = None
    param_angle_z = None
    param_body_x = None
    param_body_y = None
    param_body_z = None
    param_arm_l = None
    param_arm_r = None

    for i, p in enumerate(param_ids):
        p_norm = p.upper().replace("_", "")
        if "BODYANGLEX" in p_norm and param_body_x is None:
            param_body_x = i
        elif "BODYANGLEY" in p_norm and param_body_y is None:
            param_body_y = i
        elif "BODYANGLEZ" in p_norm and param_body_z is None:
            param_body_z = i
        elif "ANGLEX" in p_norm and param_angle_x is None:
            param_angle_x = i
        elif "ANGLEY" in p_norm and param_angle_y is None:
            param_angle_y = i
        elif "ANGLEZ" in p_norm and param_angle_z is None:
            param_angle_z = i
        elif "ARML" in p_norm and param_arm_l is None:
            if "LB" not in p_norm:
                param_arm_l = i
        elif "ARMR" in p_norm and param_arm_r is None:
            if "RB" not in p_norm:
                param_arm_r = i

    print(f"Found: AngleX={param_angle_x}, AngleY={param_angle_y}, AngleZ={param_angle_z}, BodyX={param_body_x}, BodyY={param_body_y}, BodyZ={param_body_z}", flush=True)

    tracker = Tracker(camera_id=camera_id)
    print(f"Opening camera {camera_id}...", flush=True)
    tracker.start()
    if not (tracker.cap and tracker.cap.isOpened()):
        print("ERROR: Camera failed to open!", flush=True)

    auto_track = True
    overlay_texture_id = None
    overlay_width = WIDTH // 2
    overlay_height = HEIGHT // 2

    manual_angle_x = 0.0
    manual_angle_y = 0.0
    arm_l_value = 0.0
    arm_r_value = 0.0

    clock = pygame.time.Clock()
    running = True
    frame = 0

    while running:
        for event in pygame.event.get():
            if event.type == QUIT:
                running = False
            elif event.type == KEYDOWN:
                if event.key == K_t:
                    auto_track = not auto_track
                    sys.stdout.write(f"T pressed! auto_track={auto_track}\n")
                    sys.stdout.flush()
                elif event.key == K_ESCAPE:
                    running = False
                elif event.key == K_LEFT:
                    manual_angle_x -= 90
                elif event.key == K_RIGHT:
                    manual_angle_x += 90
                elif event.key == K_UP:
                    manual_angle_y -= 90
                elif event.key == K_DOWN:
                    manual_angle_y += 90

        keys = pygame.key.get_pressed()
        mouse_x, mouse_y = pygame.mouse.get_pos()

        # カメラから tracking 結果を取得
        tracking_result = None
        if auto_track:
            try:
                tracking_result = tracker.process_frame()
            except Exception as e:
                import traceback
                traceback.print_exc()
                print(f"Error: {e}", file=sys.stderr)

        frame += 1

        glClear(GL_COLOR_BUFFER_BIT)
        model.Update()

        # Update() 後にパラメータを設定（LoadParameters() のリセットを回避）
        if auto_track and tracking_result and len(tracking_result.face) > 263:
            lm = tracking_result.face
            nose     = lm[4]    # 鼻先
            forehead = lm[10]   # 額
            chin     = lm[152]  # 顎
            eye_l    = lm[33]   # 右目外端
            eye_r    = lm[263]  # 左目外端

            # 左右回転: 目に対する鼻の左右非対称
            left_dist  = nose.x - eye_l.x
            right_dist = eye_r.x - nose.x
            denom = max(left_dist + right_dist, 0.001)
            angle_x = (right_dist - left_dist) / denom * 120

            # 上下回転: 顔の縦幅内での鼻の位置比率（上向き↑正、下向き↓負）
            face_h = chin.y - forehead.y
            nose_ratio = (nose.y - forehead.y) / max(face_h, 0.001)
            angle_y = (0.45 - nose_ratio) * 300

            # ロール: 目の高さ差
            angle_z = -(eye_r.y - eye_l.y) * 800

            if param_angle_x is not None:
                model.SetIndexParamValue(param_angle_x, angle_x, 1.0)
            if param_angle_y is not None:
                model.SetIndexParamValue(param_angle_y, angle_y, 1.0)
            if param_angle_z is not None:
                model.SetIndexParamValue(param_angle_z, angle_z, 1.0)
            if param_body_x is not None:
                model.SetIndexParamValue(param_body_x, angle_x * 0.3, 1.0)
            if param_body_y is not None:
                model.SetIndexParamValue(param_body_y, angle_y * 0.3, 1.0)
            if param_body_z is not None:
                model.SetIndexParamValue(param_body_z, angle_z * 0.3, 1.0)

        # MediaPipe Pose から腕パラメータを更新
        if auto_track and tracking_result and len(tracking_result.pose) > 16:
            pose = tracking_result.pose
            ls = pose[11]  # left shoulder
            rs = pose[12]  # right shoulder
            le = pose[13]  # left elbow
            re = pose[14]  # right elbow
            lw = pose[15]  # left wrist
            rw = pose[16]  # right wrist

            left_lift = (ls.y - lw.y) * 260.0
            left_open = (lw.x - ls.x) * 80.0
            left_elbow_bend = (le.y - lw.y) * 40.0
            target_arm_l = (left_lift + left_open + left_elbow_bend) * ARM_TRACKING_GAIN

            right_lift = (rs.y - rw.y) * 260.0
            right_open = (rs.x - rw.x) * 80.0
            right_elbow_bend = (re.y - rw.y) * 40.0
            target_arm_r = (right_lift + right_open + right_elbow_bend) * ARM_TRACKING_GAIN

            target_arm_l = clamp(target_arm_l, -60.0, 60.0)
            target_arm_r = clamp(target_arm_r, -60.0, 60.0)

            # ジッタ低減
            arm_l_value = arm_l_value + (target_arm_l - arm_l_value) * 0.25
            arm_r_value = arm_r_value + (target_arm_r - arm_r_value) * 0.25

            # 顔のミラー表示に合わせて腕は左右を反転して適用する
            if param_arm_l is not None:
                model.SetIndexParamValue(param_arm_l, arm_r_value, 1.0)
            if param_arm_r is not None:
                model.SetIndexParamValue(param_arm_r, arm_l_value, 1.0)

        elif not auto_track:
            if param_angle_x is not None:
                model.SetIndexParamValue(param_angle_x, manual_angle_x, 1.0)
            if param_angle_y is not None:
                model.SetIndexParamValue(param_angle_y, manual_angle_y, 1.0)

        model.Draw()

        # Draw landmark overlay in top-left quarter (transparent background)
        if auto_track and tracking_result:
            # Create a 3-channel black image for drawing landmarks
            overlay_frame_3ch = np.zeros((overlay_height, overlay_width, 3), dtype=np.uint8)
            overlay_frame_3ch = draw_landmarks_overlay(overlay_frame_3ch, tracking_result)

            # Convert to 4-channel (BGRA) and set black background to transparent
            overlay_frame_bgra = cv2.cvtColor(overlay_frame_3ch, cv2.COLOR_BGR2BGRA)
            # Set alpha to 0 for black background (where BGR = [0,0,0])
            black_mask = np.all(overlay_frame_3ch == [0, 0, 0], axis=2)
            overlay_frame_bgra[black_mask, 3] = 0

            # Convert BGRA to RGBA for OpenGL
            overlay_frame_rgba = cv2.cvtColor(overlay_frame_bgra, cv2.COLOR_BGRA2RGBA)
            overlay_frame_rgba = cv2.flip(overlay_frame_rgba, 0)

            if overlay_texture_id is None:
                overlay_texture_id = glGenTextures(1)
                glBindTexture(GL_TEXTURE_2D, overlay_texture_id)
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, overlay_width, overlay_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, overlay_frame_rgba)
            else:
                glBindTexture(GL_TEXTURE_2D, overlay_texture_id)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, overlay_width, overlay_height, GL_RGBA, GL_UNSIGNED_BYTE, overlay_frame_rgba)

            # Enable blending for transparent overlay
            glEnable(GL_BLEND)
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
            draw_texture(overlay_texture_id, -1, 1, 1.0, -1.0)
            glDisable(GL_BLEND)

        ax = model.GetParameter(0).value if param_angle_x is not None else 0
        ay = model.GetParameter(1).value if param_angle_y is not None else 0
        pygame.display.set_caption(f"AngleX={ax:.1f} AngleY={ay:.1f}  (T=toggle auto, ESC=quit)")

        pygame.display.flip()
        clock.tick(30)

    tracker.stop()
    live2d.glRelease()
    live2d.dispose()
    pygame.quit()


if __name__ == "__main__":
    main()
