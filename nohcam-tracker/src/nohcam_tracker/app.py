import sys
import os
import cv2
import pygame
from pygame.locals import *

from OpenGL.GL import *

import live2d.v3 as live2d
from tracker import Tracker, TrackingResult


WIDTH, HEIGHT = 1280, 720
CameraWidth = 1920
CameraHeight = 1080
CameraFps = 30
ProcessWidth = 640
ProcessHeight = 480

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "..", "..", "..", "assets", "live2d-models", "hiyori_free_jp", "runtime", "hiyori_free_t08.model3.json")
MODEL_PATH = os.path.normpath(MODEL_PATH)
print(f"Model path: {MODEL_PATH}")
print(f"Loading model from: {MODEL_PATH}")


def main():
    pygame.init()
    pygame.display.set_mode((WIDTH, HEIGHT), DOUBLEBUF | OPENGL)
    pygame.display.set_caption("Live2D Tracker")

    live2d.init()
    live2d.glInit()

    model = live2d.LAppModel()
    model.LoadModelJson(MODEL_PATH)
    model.Resize(WIDTH, HEIGHT)

    tracker = Tracker(camera_id=0)
    tracker.start()

    clock = pygame.time.Clock()
    running = True

    while running:
        for event in pygame.event.get():
            if event.type == QUIT:
                running = False
            elif event.type == KEYDOWN:
                if event.key == K_ESCAPE:
                    running = False

        result = tracker.process_frame()

        if result:
            apply_tracking_to_model(model, result)

        glClear(GL_COLOR_BUFFER_BIT)
        model.Update()
        model.Draw()

        pygame.display.flip()
        clock.tick(CameraFps)

    tracker.stop()
    live2d.glRelease()
    live2d.dispose()
    pygame.quit()


def apply_tracking_to_model(model: live2d.LAppModel, result: TrackingResult):
    if result.face and len(result.face) > 1:
        nose = result.face[1]
        model.SetParameterValue("ParamAngleX", nose.x * 30 - 15)
        model.SetParameterValue("ParamAngleY", nose.y * 30 - 15)
        model.SetParameterValue("ParamAngleZ", -nose.z * 30)

    for hand in result.hands:
        if hand.handedness == "Left" and hand.landmarks:
            wrist = hand.landmarks[0]
            model.SetParameterValue("ParamHandL_X", (wrist.x - 0.5) * 2)
            model.SetParameterValue("ParamHandL_Y", (0.5 - wrist.y) * 2)
        elif hand.handedness == "Right" and hand.landmarks:
            wrist = hand.landmarks[0]
            model.SetParameterValue("ParamHandR_X", (wrist.x - 0.5) * 2)
            model.SetParameterValue("ParamHandR_Y", (0.5 - wrist.y) * 2)


if __name__ == "__main__":
    main()