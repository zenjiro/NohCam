# -*- coding: utf-8 -*-
import os
import sys
import time

os.environ['SDL_VIDEODRIVER'] = 'windows'

import pygame
from pygame.locals import DOUBLEBUF, OPENGL

import live2d.v3 as live2d
from live2d.v3 import StandardParams

MODEL_BASE_PATH = r"D:\git\NohCam\assets\live2d-models"
WINDOW_WIDTH = 800
WINDOW_HEIGHT = 600

def get_model_paths():
    models = []
    model_base = MODEL_BASE_PATH
    
    if not os.path.exists(model_base):
        print(f"Model base path not found: {model_base}")
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
    print("Initializing pygame...", flush=True)
    pygame.init()
    pygame.display.set_caption("Live2D Viewer")
    
    display = (WINDOW_WIDTH, WINDOW_HEIGHT)
    screen = pygame.display.set_mode(display, DOUBLEBUF | OPENGL)
    print(f"Display mode set", flush=True)
    
    print("Initializing live2d...", flush=True)
    live2d.init()
    live2d.glInit()
    print("Live2D initialized", flush=True)
    
    model_paths = get_model_paths()
    print(f"Found {len(model_paths)} models", flush=True)
    
    model = live2d.LAppModel()
    print("Created LAppModel", flush=True)
    
    # Load first model
    state = {
        "current_index": 0,
        "angle_x": 0.0,
        "angle_y": 0.0,
        "angle_z": 0.0,
        "pos_x": 0.0,
        "pos_y": 0.0,
        "pos_z": 0.0
    }
    
    def load_model(index):
        state["current_index"] = index
        model_path = model_paths[index]["json_path"]
        print(f"Loading: {model_path}", flush=True)
        model.LoadModelJson(model_path)
        model.Resize(WINDOW_WIDTH, WINDOW_HEIGHT)
        print(f"Loaded: {model_paths[index]['name']}", flush=True)
    
    load_model(0)
    
    print("Starting main loop...", flush=True)
    clock = pygame.time.Clock()
    running = True
    frame_count = 0
    
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            
            if event.type == pygame.KEYDOWN:
                keys = pygame.key.get_pressed()
                shift = keys[pygame.K_LSHIFT] or keys[pygame.K_RSHIFT]
                ctrl = keys[pygame.K_LCTRL] or keys[pygame.K_RCTRL]
                
                if ctrl and event.key == pygame.K_TAB:
                    if shift:
                        prev_idx = (state["current_index"] - 1) % len(model_paths)
                    else:
                        prev_idx = (state["current_index"] + 1) % len(model_paths)
                    load_model(prev_idx)
                    continue
                
                if shift:
                    if event.key == pygame.K_LEFT:
                        state["pos_x"] -= 0.1
                        state["angle_z"] -= 5.0
                    elif event.key == pygame.K_RIGHT:
                        state["pos_x"] += 0.1
                        state["angle_z"] += 5.0
                    elif event.key == pygame.K_UP:
                        state["pos_z"] += 0.1
                        state["angle_x"] -= 3.0
                    elif event.key == pygame.K_DOWN:
                        state["pos_z"] -= 0.1
                        state["angle_x"] += 3.0
                else:
                    if event.key == pygame.K_LEFT:
                        state["angle_y"] -= 10.0
                    elif event.key == pygame.K_RIGHT:
                        state["angle_y"] += 10.0
                    elif event.key == pygame.K_UP:
                        state["angle_x"] -= 10.0
                    elif event.key == pygame.K_DOWN:
                        state["angle_x"] += 10.0
                
                state["angle_x"] = max(-60, min(60, state["angle_x"]))
                state["angle_y"] = max(-60, min(60, state["angle_y"]))
                state["angle_z"] = max(-30, min(30, state["angle_z"]))
                state["pos_x"] = max(-3, min(3, state["pos_x"]))
                state["pos_y"] = max(-3, min(3, state["pos_y"]))
                state["pos_z"] = max(-2, min(2, state["pos_z"]))
        
        live2d.clearBuffer()
        
        model.SetParameterValue(StandardParams.ParamAngleX, state["angle_x"], 1.0)
        model.SetParameterValue(StandardParams.ParamAngleY, state["angle_y"], 1.0)
        model.SetParameterValue(StandardParams.ParamAngleZ, state["angle_z"], 1.0)
        model.SetParameterValue(StandardParams.ParamBaseX, state["pos_x"] * 10, 1.0)
        model.SetParameterValue(StandardParams.ParamBaseY, state["pos_y"] * 10, 1.0)
        
        model.Update()
        model.Draw()
        
        # Use pygame.display.update() instead of flip() - more stable
        pygame.display.update()
        
        frame_count += 1
        if frame_count % 60 == 0:
            print(f"Frame {frame_count}", flush=True)
        
        clock.tick(60)
    
    live2d.dispose()
    pygame.quit()
    print("Done", flush=True)

if __name__ == "__main__":
    main()