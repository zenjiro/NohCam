import sys
import os
import warnings
warnings.filterwarnings("ignore")

import cv2
import pygame
from pygame.locals import *

from OpenGL.GL import *

import live2d.v3 as live2d
from nohcam_tracker.tracker import Tracker
from live2d.v3.params import StandardParams


WIDTH, HEIGHT = 1280, 720

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "..", "..", "..", "assets", "live2d-models", "hiyori_free_jp", "runtime", "hiyori_free_t08.model3.json")
MODEL_PATH = os.path.normpath(MODEL_PATH)
print(f"Loading: {MODEL_PATH}", flush=True)


def main():
    pygame.init()
    pygame.display.set_mode((WIDTH, HEIGHT), DOUBLEBUF | OPENGL)
    pygame.display.set_caption("Live2D Tracking - Arrow keys manual, T auto-track")

    live2d.init()
    live2d.glInit()

    model = live2d.LAppModel()
    model.LoadModelJson(MODEL_PATH)
    model.Resize(WIDTH, HEIGHT)

    model.StopAllMotions()
    model.SetAutoBlinkEnable(False)
    model.SetAutoBreathEnable(False)
    model.ResetParameters()

    param_ids = model.GetParamIds()
    print(f"Params ({len(param_ids)}): {param_ids}", flush=True)

    param_angle_x = None
    param_angle_y = None
    param_angle_z = None
    param_arm_l = None
    param_arm_r = None
    
    for i, p in enumerate(param_ids):
        p_upper = p.upper()
        if "ANGLEX" in p_upper and param_angle_x is None:
            param_angle_x = i
        elif "ANGLEY" in p_upper and param_angle_y is None:
            param_angle_y = i
        elif "ANGLEZ" in p_upper and param_angle_z is None:
            param_angle_z = i
        elif ("ARML" in p_upper or "ARM_L" in p_upper) and param_arm_l is None:
            if "LB" not in p_upper and "L_B" not in p_upper:
                param_arm_l = i
        elif ("ARMR" in p_upper or "ARM_R" in p_upper) and param_arm_r is None:
            if "RB" not in p_upper and "R_B" not in p_upper:
                param_arm_r = i

    print(f"Found: AngleX={param_angle_x}, AngleY={param_angle_y}, AngleZ={param_angle_z}, ArmL={param_arm_l}, ArmR={param_arm_r}", flush=True)

    tracker = Tracker(camera_id=0)
    tracker.start()

    auto_track = True

    manual_angle_x = 0.0
    manual_angle_y = 0.0

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

        if auto_track:
            try:
                result = tracker.process_frame()
                if result and result.face and len(result.face) > 1:
                    nose = result.face[1]
                    angle_x = (nose.x - 0.5) * 360
                    angle_y = (0.5 - nose.y) * 360

                    if param_angle_x is not None:
                        model.SetIndexParamValue(param_angle_x, angle_x, 1.0)
                    if param_angle_y is not None:
                        model.SetIndexParamValue(param_angle_y, angle_y, 1.0)
                    if param_angle_z is not None:
                        model.SetIndexParamValue(param_angle_z, -nose.z * 180, 1.0)

                if result and result.hands:
                    for hand in result.hands:
                        if hand.landmarks:
                            wrist = hand.landmarks[0]
                            if hand.handedness == "Left" and param_arm_l is not None:
                                model.SetIndexParamValue(param_arm_l, (wrist.x - 0.5) * 10, 1.0)
                            elif hand.handedness == "Right" and param_arm_r is not None:
                                model.SetIndexParamValue(param_arm_r, (wrist.x - 0.5) * 10, 1.0)
            except Exception as e:
                import traceback
                traceback.print_exc()
                print(f"Error: {e}", file=sys.stderr)
        else:
            if param_angle_x is not None:
                model.SetIndexParamValue(param_angle_x, manual_angle_x, 1.0)
            if param_angle_y is not None:
                model.SetIndexParamValue(param_angle_y, manual_angle_y, 1.0)

            arm_val = (mouse_x / WIDTH - 0.5) * 2
            if param_arm_r is not None:
                model.SetIndexParamValue(param_arm_r, arm_val, 1.0)

        frame += 1

        glClear(GL_COLOR_BUFFER_BIT)
        model.Update()
        model.Draw()

        pygame.display.flip()
        clock.tick(30)

    tracker.stop()
    live2d.glRelease()
    live2d.dispose()
    pygame.quit()


if __name__ == "__main__":
    main()
