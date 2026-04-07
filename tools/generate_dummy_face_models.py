from pathlib import Path
import onnx
from onnx import TensorProto, helper

def create_model(name, input_shape, output_shape, output_name="output"):
    input_tensor = helper.make_tensor_value_info("input", TensorProto.FLOAT, input_shape)
    output_tensor = helper.make_tensor_value_info(output_name, TensorProto.FLOAT, output_shape)
    
    # Just a simple identity-like or constant output for dummy
    # For landmarks (1, 468*3), we can just output a constant
    if output_name == "landmarks":
        val = [0.5] * (468 * 3)
    else:
        val = [0.1] * output_shape[1]

    const_node = helper.make_node(
        "Constant",
        inputs=[],
        outputs=[output_name],
        value=helper.make_tensor(
            name="const_tensor",
            data_type=TensorProto.FLOAT,
            dims=output_shape,
            vals=val,
        ),
    )

    graph = helper.make_graph(
        nodes=[const_node],
        name=name,
        inputs=[input_tensor],
        outputs=[output_tensor],
    )

    model = helper.make_model(
        graph,
        producer_name="NohCam-Dummy",
        opset_imports=[helper.make_opsetid("", 13)],
    )
    onnx.checker.check_model(model)
    return model

def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    onnx_dir = repo_root / "assets" / "onnx"
    onnx_dir.mkdir(parents=True, exist_ok=True)

    # face_landmarks.onnx: Input (1, 3, 192, 192), Output (1, 1404) for 468 points * 3
    face_model = create_model("dummy_face_landmarks", [1, 3, 192, 192], [1, 1404], "landmarks")
    onnx.save(face_model, onnx_dir / "face_landmarks.onnx")
    print(onnx_dir / "face_landmarks.onnx")

    # face_blendshapes.onnx: Input (1, 3, 192, 192), Output (1, 52)
    blend_model = create_model("dummy_face_blendshapes", [1, 3, 192, 192], [1, 52], "blendshapes")
    onnx.save(blend_model, onnx_dir / "face_blendshapes.onnx")
    print(onnx_dir / "face_blendshapes.onnx")

if __name__ == "__main__":
    main()
