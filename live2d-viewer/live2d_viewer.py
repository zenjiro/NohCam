# -*- coding: utf-8 -*-
# pyright: ignoreOperator, E501

import os
import sys

os.environ['SDL_VIDEODRIVER'] = 'windows'

import pygame
from pygame.locals import DOUBLEBUF, OPENGL
from OpenGL.GL import *

import os
import live2d.v3 as live2d

live2d.enableLog(True)
live2d.setLogLevel(live2d.Live2DLogLevels.LV_DEBUG)

MODEL_DIRS = sorted([
    d for d in os.listdir("../assets/live2d-models")
    if os.path.isdir(os.path.join("../assets/live2d-models", d))
])
current_model_index = 0

print("1. Pygame init")
pygame.init()

print("2. Set display")
screen = pygame.display.set_mode((800, 600), DOUBLEBUF + OPENGL)

print("3. OpenGL clear")
glClearColor(1.0, 1.0, 1.0, 1.0)
glClear(GL_COLOR_BUFFER_BIT + GL_DEPTH_BUFFER_BIT)

print("4. live2d.init")
live2d.init()

print("5. live2d.glInit")
live2d.glInit()

print("6. OpenGL setup")
glDisable(GL_DEPTH_TEST)
glEnable(GL_BLEND)
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)


def find_model_json(dir_path):
    for root, dirs, files in os.walk(dir_path):
        for f in files:
            if f.endswith(".model3.json"):
                return os.path.join(root, f)
    return None


def find_moc_file(dir_path):
    for root, dirs, files in os.walk(dir_path):
        for f in files:
            if f.endswith(".moc3"):
                return os.path.join(root, f)
    return None


def load_model(index):
    global model
    model_dir = os.path.join("../assets/live2d-models", MODEL_DIRS[index])
    model_json = find_model_json(model_dir)
    moc_file = find_moc_file(model_dir)
    if model_json and moc_file:
        print(f"Loading: {MODEL_DIRS[index]}")
        if model:
            try:
                del model
            except Exception:
                pass
            model = None
        try:
            model = live2d.LAppModel()
            has_consistency = model.HasMocConsistencyFromFile(moc_file)
            if not has_consistency:
                print(f"MOC3 file is inconsistent: {moc_file}")
                model = None
                return False
            model.LoadModelJson(model_json)
            model.Resize(800, 600)
            print(f"Canvas: {model.GetCanvasSize()}")
            pygame.display.set_caption(f"Live2D Viewer - {MODEL_DIRS[index]}")
            return True
        except Exception as e:
            print(f"Failed to load: {e}")
            model = None
            return False
    print(f"No model3.json or moc3 found in {model_dir}")
    return False


print("5. Create LAppModel")
model = None

print("6. Load model")
if not load_model(current_model_index):
    print("Failed to load model, using dummy")

if model:
    print("7. Resize")
    model.Resize(800, 600)

    print("8. GetCanvasSize")
    print(f"Canvas: {model.GetCanvasSize()}")

    print("9. Get canvas size")
    print(f"Canvas: {model.GetCanvasSize()}")
    print(f"CanvasPixel: {model.GetCanvasSizePixel()}")
    print(f"PixelsPerUnit: {model.GetPixelsPerUnit()}")

    print("10. Set offset")
    model.SetOffset(0, 0)

    print("11. Set scale")
    model.SetScale(1.0)
else:
    print("No model loaded - window will be blank")

print("12. Enter main loop")
clock = pygame.time.Clock()
running = True
while running:
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False
        if event.type == pygame.KEYDOWN:
            keys = pygame.key.get_mods()
            if keys & pygame.KMOD_CTRL and event.key == pygame.K_TAB:
                if keys & pygame.KMOD_SHIFT:
                    current_model_index = (current_model_index - 1) % len(MODEL_DIRS)
                else:
                    current_model_index = (current_model_index + 1) % len(MODEL_DIRS)
                load_model(current_model_index)
    live2d.clearBuffer(0.0, 0.0, 0.0, 0.0)
    if model:
        try:
            model.Update()
            model.Draw()
        except Exception as e:
            print(f"Render error: {e}")
            model = None
    pygame.display.flip()
    pygame.time.wait(10)

print("EXIT")
pygame.quit()
sys.exit(0)