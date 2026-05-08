import live2d.v3 as live2d
import sys
import os

def main():
    model_path = r"D:\git\NohCam\assets\live2d-models\ulvm2_0001\ulvm2_0001.model3.json"
    if not os.path.exists(model_path):
        print(f"Model not found: {model_path}")
        return

    live2d.init()
    model = live2d.LAppModel()
    model.LoadModelJson(model_path)
    
    param_ids = model.GetParamIds()
    print(f"Total params: {len(param_ids)}")
    
    for i, p_id in enumerate(param_ids):
        if "MouthOpenY" in p_id:
            val = model.GetParameterValue(i)
            # Try to get min/max
            try:
                param_obj = model.GetParameter(i)
                print(f"Param: {p_id}")
                print(f"  Index: {i}")
                print(f"  Value: {val}")
                print(f"  Min (from obj): {param_obj.min}")
                print(f"  Max (from obj): {param_obj.max}")
                print(f"  Default (from obj): {param_obj.default_value}")
            except Exception as e:
                print(f"  Error getting param_obj: {e}")
            
            # Try other methods if they exist
            for method in ["GetParameterMinimumValue", "GetParameterMaximumValue", "GetParameterDefaultValue"]:
                try:
                    res = getattr(model, method)(i)
                    print(f"  {method}: {res}")
                except AttributeError:
                    pass
                except Exception as e:
                    print(f"  {method} error: {e}")

    live2d.dispose()

if __name__ == "__main__":
    main()
