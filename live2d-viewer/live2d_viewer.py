# -*- coding: utf-8 -*-
import os
import sys

os.environ['SDL_VIDEODRIVER'] = 'windows'

import pygame
from pygame.locals import DOUBLEBUF, OPENGL

import live2d.v3 as live2d
from live2d.v3 import StandardParams

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_BASE_PATH = os.path.join(os.path.dirname(SCRIPT_DIR), "assets", "live2d-models")
WINDOW_WIDTH = 800
WINDOW_HEIGHT = 600

def get_model_paths():
    models = []
    model_base = MODEL_BASE_PATH
    
    if not os.path.exists(model_base):
        return models
    
    for entry in os.listdir(model_base):
        model_dir = os.path.join(model_base, entry)
        if not os.path.isdir(model_dir):
            continue
        
        runtime_dir = os.path.join(model_dir, "runtime")
        if not os.path.exists(runtime_dir):
            continue
        
        for fname in os.listdir(runtime_dir):
            if fname.endswith(".model3.json"):
                models.append({
                    "name": entry,
                    "json_path": os.path.join(runtime_dir, fname)
                })
                break
    
    return models

def main():
    pygame.init()
    pygame.display.set_caption("Live2D Viewer")
    
    display = (WINDOW_WIDTH, WINDOW_HEIGHT)
    screen = pygame.display.set_mode(display, DOUBLEBUF | OPENGL)
    
    live2d.init()
    live2d.glInit()
    
    model_paths = get_model_paths()
    print(f"Models: {[m['name'] for m in model_paths]}")
    
    model = live2d.LAppModel()
    
    # Load first available model
    if model_paths:
        model_path = model_paths[0]["json_path"]
        print(f"Loading: {model_path}")
        model.LoadModelJson(model_path)
        model.Resize(WINDOW_WIDTH, WINDOW_HEIGHT)
        print("Model loaded")
    else:
        print("No models found!")
        live2d.dispose()
        pygame.quit()
        sys.exit(1)
    
    state = {
        "current_index": 0,
        "angle_x": 0.0,
        "angle_y": 0.0,
        "angle_z": 0.0,
        "pos_x": 0.0,
        "pos_y": 0.0,
        "pos_z": 0.0
    }
    
    clock = pygame.time.Clock()
    running = True
    
    print("Starting loop")
    
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                print("QUIT")
            
            if event.type == pygame.KEYDOWN:
                keys = pygame.key.get_pressed()
                shift = keys[pygame.K_LSHIFT] or keys[pygame.K_RSHIFT]
                ctrl = keys[pygame.K_LCTRL] or keys[pygame.K_RCTRL]
                
                key_name = pygame.key.name(event.key)
                print(f"Key: {key_name}, shift={shift}, ctrl={ctrl}")
                
                if ctrl and event.key == pygame.K_TAB:
                    print("Ctrl+Tab pressed")
                    continue
                
                if shift:
                    if event.key == pygame.K_LEFT:
                        state["pos_x"] -= 0.1
                        state["angle_z"] -= 5.0
                        print(f"Shift+Left: pos_x={state['pos_x']}")
                    elif event.key == pygame.K_RIGHT:
                        state["pos_x"] += 0.1
                        state["angle_z"] += 5.0
                        print(f"Shift+Right: pos_x={state['pos_x']}")
                    elif event.key == pygame.K_UP:
                        state["pos_z"] += 0.1
                        state["angle_x"] -= 3.0
                    elif event.key == pygame.K_DOWN:
                        state["pos_z"] -= 0.1
                        state["angle_x"] += 3.0
                else:
                    if event.key == pygame.K_LEFT:
                        state["angle_y"] -= 10.0
                        print(f"Left: angle_y={state['angle_y']}")
                    elif event.key == pygame.K_RIGHT:
                        state["angle_y"] += 10.0
                        print(f"Right: angle_y={state['angle_y']}")
                    elif event.key == pygame.K_UP:
                        state["angle_x"] -= 10.0
                        print(f"Up: angle_x={state['angle_x']}")
                    elif event.key == pygame.K_DOWN:
                        state["angle_x"] += 10.0
                        print(f"Down: angle_x={state['angle_x']}")
        
        # Render
        live2d.clearBuffer()
        
        model.SetParameterValue("ParamAngleX", state["angle_x"], 1.0)
        model.SetParameterValue("ParamAngleY", state["angle_y"], 1.0)
        model.SetParameterValue("ParamAngleZ", state["angle_z"], 1.0)
        
        model.Update()
        model.Draw()
        
        pygame.display.flip()
        
        clock.tick(60)
    
    live2d.dispose()
    pygame.quit()
    print("Done")

if __name__ == "__main__":
    main()