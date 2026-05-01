import sys
import os
import warnings
# warnings.filterwarnings("ignore")
print("Imports successful", flush=True)

import cv2
import pygame
from pygame.locals import *

from OpenGL.GL import *

import live2d.v3 as live2d
from nohcam_tracker.tracker import Tracker
from live2d.v3.params import StandardParams


WIDTH, HEIGHT = 1280, 720
ARM_TRACKING_GAIN = 2.2


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_DEFAULT_MODEL = os.path.normpath(os.path.join(
    _SCRIPT_DIR, "..", "..", "..", "assets", "live2d-models",
    "hiyori_free_jp", "runtime", "hiyori_free_t08.model3.json"
))


def main(model_path: str = None):
    if model_path is None:
        model_path = _DEFAULT_MODEL
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

    tracker = Tracker(camera_id=1)
    print("Opening camera...", flush=True)
    tracker.start()
    if not (tracker.cap and tracker.cap.isOpened()):
        print("ERROR: Camera failed to open!", flush=True)

    auto_track = True

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
