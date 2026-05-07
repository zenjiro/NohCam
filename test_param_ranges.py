#!/usr/bin/env python3
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "src"))

import live2d.v3 as live2d

live2d.init()
model = live2d.LAppModel()
model_path = r"D:\git\NohCam\assets\live2d-models\A1\A1.model3.json"
model.LoadModelJson(model_path)

param_ids = model.GetParamIds()
print(f"Total parameters: {len(param_ids)}\n")
print(f"{'Index':<5} {'Parameter Name':<30} {'Min':<10} {'Max':<10} {'Current':<10}")
print("=" * 70)

for i, param_id in enumerate(param_ids):
    param_obj = model.GetParameter(i)
    current = model.GetParameterValue(i)
    print(f"{i:<5} {param_id:<30} {param_obj.min:<10.2f} {param_obj.max:<10.2f} {current:<10.2f}")

live2d.dispose()
