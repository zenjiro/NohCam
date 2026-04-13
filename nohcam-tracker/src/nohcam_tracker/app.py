import sys
import os
import pygame
from pygame.locals import *

from OpenGL.GL import *

import live2d.v3 as live2d


WIDTH, HEIGHT = 1280, 720
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.join(SCRIPT_DIR, "..", "..")
MODEL_PATH = os.path.join(PROJECT_ROOT, "assets", "live2d-models", "hiyori_free_jp", "runtime", "hiyori_free_t08.model3.json")
MODEL_PATH = os.path.normpath(MODEL_PATH)
print(f"Loading model from: {MODEL_PATH}")


def main():
    pygame.init()
    pygame.display.set_mode((WIDTH, HEIGHT), DOUBLEBUF | OPENGL)
    pygame.display.set_caption("Live2D Viewer")

    live2d.init()
    live2d.glInit()

    model = live2d.LAppModel()
    model.LoadModelJson(MODEL_PATH)
    model.Resize(WIDTH, HEIGHT)

    clock = pygame.time.Clock()
    running = True

    while running:
        for event in pygame.event.get():
            if event.type == QUIT:
                running = False

        glClear(GL_COLOR_BUFFER_BIT)
        model.Update()
        model.Draw()

        pygame.display.flip()
        clock.tick(30)

    live2d.glRelease()
    live2d.dispose()
    pygame.quit()


if __name__ == "__main__":
    main()