import live2d.v3 as live2d
import sys
import os
import pygame
from pygame.locals import *
from OpenGL.GL import *

def main():
    model_path = r"D:\git\NohCam\assets\live2d-models\ulvm2_0001\ulvm2_0001.model3.json"
    if not os.path.exists(model_path):
        print(f"Model not found: {model_path}")
        return

    pygame.init()
    pygame.display.set_mode((1, 1), DOUBLEBUF | OPENGL)

    live2d.init()
    live2d.glInit()
    
    model = live2d.LAppModel()
    model.LoadModelJson(model_path)
    
    param_ids = model.GetParamIds()
    print(f"Total params: {len(param_ids)}")
    
    for i, p_id in enumerate(param_ids):
        if "MouthOpenY" in p_id or "AngleX" in p_id:
            val = model.GetParameterValue(i)
            print(f"\nParam: {p_id} (Index: {i})")
            
            # Try GetParameter object
            try:
                param_obj = model.GetParameter(i)
                print(f"  obj.min: {param_obj.min}")
                print(f"  obj.max: {param_obj.max}")
                print(f"  obj.default_value: {param_obj.default_value}")
                print(f"  obj.value: {param_obj.value}")
            except Exception as e:
                print(f"  Error getting param_obj: {e}")
            
            # Check model methods directly
            methods = [
                "GetParameterMinimumValue", 
                "GetParameterMaximumValue", 
                "GetParameterDefaultValue",
                "GetParameterValue"
            ]
            for method in methods:
                try:
                    res = getattr(model, method)(i)
                    print(f"  {method}({i}): {res}")
                except AttributeError:
                    print(f"  {method} NOT FOUND")
                except Exception as e:
                    print(f"  {method} error: {e}")

    live2d.glRelease()
    live2d.dispose()
    pygame.quit()

if __name__ == "__main__":
    main()
