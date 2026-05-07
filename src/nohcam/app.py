import sys
import os
import warnings
from typing import Optional
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
from .landmark_drawer import draw_landmarks
from .parameter_display import ParameterDisplayRenderer


WIDTH, HEIGHT = 1280, 720
ARM_TRACKING_GAIN = 2.2

# Overlay dimensions (top-left 1/4 of screen)
OVERLAY_WIDTH = WIDTH // 2  # 640
OVERLAY_HEIGHT = HEIGHT // 2  # 360


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


class OverlayRenderer:
    """Manages overlay texture rendering for debug landmarks."""
    
    def __init__(self, x=0, y=0):
        self.texture_id = None
        self.texture_data = None
        self.x = x
        self.y = y
    
    def create_texture(self):
        """Create an OpenGL texture for the overlay."""
        self.texture_id = glGenTextures(1)
        glBindTexture(GL_TEXTURE_2D, self.texture_id)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, OVERLAY_WIDTH, OVERLAY_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, None)
        glBindTexture(GL_TEXTURE_2D, 0)
    
    def update_texture(self, cv_image):
        """Update the texture with new image data."""
        if self.texture_id is None:
            self.create_texture()
        
        # Handle both BGR and RGBA images
        if cv_image.shape[2] == 3:
            # Convert BGR to RGBA
            rgba_image = cv2.cvtColor(cv_image, cv2.COLOR_BGR2RGBA)
        else:
            # Already RGBA
            rgba_image = cv_image
        
        glBindTexture(GL_TEXTURE_2D, self.texture_id)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, OVERLAY_WIDTH, OVERLAY_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba_image)
        glBindTexture(GL_TEXTURE_2D, 0)
        glFlush()  # Ensure the texture update is processed
    
    def render(self):
        """Render the overlay texture as a 2D quad at the specified position."""
        if self.texture_id is None:
            return
        
        # Save OpenGL state
        glPushAttrib(GL_ALL_ATTRIB_BITS)
        
        glDisable(GL_DEPTH_TEST)
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glEnable(GL_TEXTURE_2D)
        
        glBindTexture(GL_TEXTURE_2D, self.texture_id)
        
        # Ortho projection
        glMatrixMode(GL_PROJECTION)
        glPushMatrix()
        glLoadIdentity()
        glOrtho(0, WIDTH, HEIGHT, 0, -1, 1)
        glMatrixMode(GL_MODELVIEW)
        glPushMatrix()
        glLoadIdentity()
        
        # Calculate vertices
        x1, y1 = self.x, self.y
        x2, y2 = self.x + OVERLAY_WIDTH, self.y + OVERLAY_HEIGHT
        
        glBegin(GL_QUADS)
        glColor4f(1.0, 1.0, 1.0, 1.0)
        glTexCoord2f(0, 0); glVertex2f(x1, y1)
        glTexCoord2f(1, 0); glVertex2f(x2, y1)
        glTexCoord2f(1, 1); glVertex2f(x2, y2)
        glTexCoord2f(0, 1); glVertex2f(x1, y2)
        glEnd()
        
        # Restore OpenGL state
        glMatrixMode(GL_PROJECTION)
        glPopMatrix()
        glMatrixMode(GL_MODELVIEW)
        glPopMatrix()
        
        glDisable(GL_TEXTURE_2D)
        glDisable(GL_BLEND)
        glBindTexture(GL_TEXTURE_2D, 0)
        
        glPopAttrib()
    
    def cleanup(self):
        """Delete the texture."""
        if self.texture_id is not None:
            glDeleteTextures([self.texture_id])
            self.texture_id = None


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


def main(model_path: Optional[str] = None, camera_id: int = 0):
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
    param_eye_l_open = None
    param_eye_r_open = None
    param_mouth_open_y = None
    param_mouth_form = None

    for i, p in enumerate(param_ids):
        p_norm = p.upper().replace("_", "")
        if ("BODYANGLEX" in p_norm or "BODYX" in p_norm) and param_body_x is None:
            param_body_x = i
        elif ("BODYANGLEY" in p_norm or "BODYY" in p_norm) and param_body_y is None:
            param_body_y = i
        elif ("BODYANGLEZ" in p_norm or "BODYZ" in p_norm) and param_body_z is None:
            param_body_z = i
        elif "ANGLEX" in p_norm and param_angle_x is None:
            param_angle_x = i
        elif "ANGLEY" in p_norm and param_angle_y is None:
            param_angle_y = i
        elif "ANGLEZ" in p_norm and param_angle_z is None:
            param_angle_z = i
        elif "EYELOPEN" in p_norm and param_eye_l_open is None:
            param_eye_l_open = i
        elif "EYEROPEN" in p_norm and param_eye_r_open is None:
            param_eye_r_open = i
        elif "MOUTHOPENY" in p_norm and param_mouth_open_y is None:
            param_mouth_open_y = i
        elif "MOUTHFORM" in p_norm and param_mouth_form is None:
            param_mouth_form = i
        elif "ARML" in p_norm and param_arm_l is None:
            if "LB" not in p_norm:
                param_arm_l = i
        elif "ARMR" in p_norm and param_arm_r is None:
            if "RB" not in p_norm:
                param_arm_r = i

    print(f"Found: AngleX={param_angle_x}, AngleY={param_angle_y}, AngleZ={param_angle_z}, BodyX={param_body_x}, BodyY={param_body_y}, BodyZ={param_body_z}", flush=True)
    print(f"Found Expression: EyeLOpen={param_eye_l_open}, EyeROpen={param_eye_r_open}, MouthOpenY={param_mouth_open_y}, MouthForm={param_mouth_form}", flush=True)

    tracker = Tracker(camera_id=camera_id)
    print(f"Opening camera {camera_id}...", flush=True)
    tracker.start()
    if not (tracker.cap and tracker.cap.isOpened()):
        print("ERROR: Camera failed to open!", flush=True)

    auto_track = True

    manual_angle_x = 0.0
    manual_angle_y = 0.0
    arm_l_value = 0.0
    arm_r_value = 0.0
    body_x_value = 0.0
    body_y_value = 0.0
    body_z_value = 0.0
    
    # Dual overlays
    holistic_overlay = OverlayRenderer(x=0, y=0) # Top-left
    face_overlay = OverlayRenderer(x=0, y=HEIGHT // 2) # Bottom-left
    print("Dual landmark overlays enabled", flush=True)

    param_display_renderer = ParameterDisplayRenderer(width=WIDTH, height=HEIGHT)
    param_display_renderer.set_parameters(model, model_path=model_path)
    print("Parameter display enabled", flush=True)

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
        target_body_x = 0.0
        target_body_y = 0.0
        target_body_z = 0.0

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
            angle_x = -(right_dist - left_dist) / denom * 120

            # 上下回転: 顔の縦幅内での鼻の位置比率（上向き↑正、下向き↓負）
            face_h = chin.y - forehead.y
            nose_ratio = (nose.y - forehead.y) / max(face_h, 0.001)
            angle_y = (0.45 - nose_ratio) * 300

            # ロール: 目の高さ差
            angle_z = (eye_r.y - eye_l.y) * 800

            target_body_x += angle_x * 0.3
            target_body_y += angle_y * 0.3
            target_body_z += angle_z * 0.3

            if param_angle_x is not None:
                model.SetIndexParamValue(param_angle_x, angle_x, 1.0)
            if param_angle_y is not None:
                model.SetIndexParamValue(param_angle_y, angle_y, 1.0)
            if param_angle_z is not None:
                model.SetIndexParamValue(param_angle_z, angle_z, 1.0)

        # MediaPipe Pose から腕・体パラメータを更新
        if auto_track and tracking_result and len(tracking_result.pose) > 16:
            pose = tracking_result.pose
            ls = pose[11]  # left shoulder
            rs = pose[12]  # right shoulder
            le = pose[13]  # left elbow
            re = pose[14]  # right elbow
            lw = pose[15]  # left wrist
            rw = pose[16]  # right wrist

            # 体の傾き (Pose)
            # Roll: 肩の高さの差
            target_body_z += (rs.y - ls.y) * 400.0
            # Yaw: 肩の前後差 (Z座標)
            target_body_x += -(ls.z - rs.z) * 150.0
            # Pitch: 肩の前後位置 (平均Z座標)
            target_body_y += (ls.z + rs.z) / 2.0 * 150.0

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

        if auto_track:
            # 体の傾きのジッタ低減
            body_x_value = body_x_value + (target_body_x - body_x_value) * 0.25
            body_y_value = body_y_value + (target_body_y - body_y_value) * 0.25
            body_z_value = body_z_value + (target_body_z - body_z_value) * 0.25

            if param_body_x is not None:
                model.SetIndexParamValue(param_body_x, body_x_value, 1.0)
            if param_body_y is not None:
                model.SetIndexParamValue(param_body_y, body_y_value, 1.0)
            if param_body_z is not None:
                model.SetIndexParamValue(param_body_z, body_z_value, 1.0)

        elif not auto_track:
            if param_angle_x is not None:
                model.SetIndexParamValue(param_angle_x, manual_angle_x, 1.0)
            if param_angle_y is not None:
                model.SetIndexParamValue(param_angle_y, manual_angle_y, 1.0)

        # Apply Blendshapes from Face Landmarker
        if auto_track and tracking_result and tracking_result.blendshapes:
            bs = tracking_result.blendshapes
            
            # Blink (MediaPipe: 0=open, 1=closed -> Live2D: 1=open, 0=closed)
            eye_l_open = 1.0 - bs.get("eyeBlinkLeft", 0.0)
            eye_r_open = 1.0 - bs.get("eyeBlinkRight", 0.0)
            
            # Mouth Open (MediaPipe: 0=closed, 1=open -> Live2D: 0=closed, 1=open)
            mouth_open = bs.get("jawOpen", 0.0)
            
            # Mouth Form (MediaPipe Smile -> Live2D Form: -1.0 to 1.0, 0 is neutral)
            smile_l = bs.get("mouthSmileLeft", 0.0)
            smile_r = bs.get("mouthSmileRight", 0.0)
            mouth_form = (smile_l + smile_r) / 2.0  # 0 to 1
            
            # Apply to model using Index for reliability
            if param_eye_l_open is not None:
                model.SetIndexParamValue(param_eye_l_open, eye_l_open, 1.0)
            if param_eye_r_open is not None:
                model.SetIndexParamValue(param_eye_r_open, eye_r_open, 1.0)
            if param_mouth_open_y is not None:
                model.SetIndexParamValue(param_mouth_open_y, mouth_open, 1.0)
            if param_mouth_form is not None:
                model.SetIndexParamValue(param_mouth_form, mouth_form, 1.0)

        model.Draw()

        # Render landmark overlays
        if tracking_result:
            # 1. Holistic Overlay (Top-left)
            if holistic_overlay:
                h_frame = np.zeros((OVERLAY_HEIGHT, OVERLAY_WIDTH, 4), dtype=np.uint8)
                draw_landmarks(
                    h_frame,
                    tracking_result.face,
                    tracking_result.hands[0].landmarks if len(tracking_result.hands) > 0 else None,
                    tracking_result.hands[1].landmarks if len(tracking_result.hands) > 1 else None,
                    tracking_result.pose,
                )
                holistic_overlay.update_texture(h_frame)
                holistic_overlay.render()
            
            # 2. Face Overlay (Bottom-left)
            mesh_to_draw = tracking_result.face_mesh
            if not mesh_to_draw and len(tracking_result.face) >= 468:
                mesh_to_draw = tracking_result.face
                
            if face_overlay and mesh_to_draw:
                f_frame = np.zeros((OVERLAY_HEIGHT, OVERLAY_WIDTH, 4), dtype=np.uint8)
                draw_landmarks(
                    f_frame,
                    mesh_to_draw,
                    None, None, None,
                    detail_face=True  # Show detailed connections
                )
                face_overlay.update_texture(f_frame)
                face_overlay.render()

        # Render parameter display
        if param_display_renderer:
            param_display_renderer.update_parameter_values(model)
            text_color = (255, 255, 255, 255)
            param_image = param_display_renderer.render_to_image(text_color)
            param_display_renderer.update_texture(param_image)
            param_display_renderer.render(WIDTH, HEIGHT)

        ax = model.GetParameter(0).value if param_angle_x is not None else 0
        ay = model.GetParameter(1).value if param_angle_y is not None else 0
        pygame.display.set_caption(f"AngleX={ax:.1f} AngleY={ay:.1f}  (T=toggle auto, ESC=quit)")

        pygame.display.flip()
        clock.tick(30)

    tracker.stop()
    if holistic_overlay:
        holistic_overlay.cleanup()
    if face_overlay:
        face_overlay.cleanup()
    if param_display_renderer:
        param_display_renderer.cleanup()
    live2d.glRelease()
    live2d.dispose()
    pygame.quit()


if __name__ == "__main__":
    main()
