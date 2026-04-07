from pathlib import Path

import onnx
from onnx import TensorProto, helper


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    output_path = repo_root / "assets" / "onnx" / "smoke_add.onnx"
    output_path.parent.mkdir(parents=True, exist_ok=True)

    input_tensor = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3])
    output_tensor = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 3])
    one_tensor = helper.make_tensor("one", TensorProto.FLOAT, [1, 3], [1.0, 1.0, 1.0])

    graph = helper.make_graph(
        nodes=[
            helper.make_node("Add", inputs=["input", "one"], outputs=["output"]),
        ],
        name="nohcam_smoke_add",
        inputs=[input_tensor],
        outputs=[output_tensor],
        initializer=[one_tensor],
    )

    model = helper.make_model(
        graph,
        producer_name="NohCam",
        opset_imports=[helper.make_opsetid("", 13)],
    )
    onnx.checker.check_model(model)
    onnx.save(model, output_path)
    print(output_path)


if __name__ == "__main__":
    main()
