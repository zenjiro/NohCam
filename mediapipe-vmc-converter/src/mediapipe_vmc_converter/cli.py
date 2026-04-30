from __future__ import annotations

import argparse
from pathlib import Path

from .app import run
from .config import AppConfig
from .landmark import list_cameras


def _resolve_model_path(config_path: str, model_value: str) -> Path:
    raw = Path(model_value)
    if raw.exists():
        return raw

    cfg_dir = Path(config_path).resolve().parent
    candidates = [
        cfg_dir / raw,
        cfg_dir / "models" / "pose_landmarker_full.task",
        cfg_dir.parent / "nohcam-tracker" / "models" / "pose_landmarker_full.task",
        cfg_dir.parent / "models" / "pose_landmarker_full.task",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate

    holistic = cfg_dir.parent / "nohcam-tracker" / "models" / "holistic_landmarker.task"
    hint = ""
    if holistic.exists():
        hint = f"\nDetected holistic model at: {holistic}\nThis app uses PoseLandmarker, so please provide a pose_landmarker_*.task model via --model."
    checked = "\n".join(f"- {p}" for p in candidates)
    raise FileNotFoundError(f"Pose model not found.\nChecked:\n{checked}{hint}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MediaPipe to VMC converter")
    parser.add_argument("--config", default="config.json", help="Path to config.json")
    parser.add_argument("--target", default=None, choices=["vseeface", "warudo", "vnyan"], help="VMC target preset")
    parser.add_argument("--camera", type=int, default=None, help="OpenCV camera index")
    parser.add_argument("--model", default=None, help="PoseLandmarker .task model path")
    parser.add_argument("--enable-face", action="store_true", help="Enable face expression stage (placeholder)")
    parser.add_argument("--enable-hands", action="store_true", help="Enable hand stage (placeholder)")
    parser.add_argument("--list-cameras", action="store_true", help="List available camera indices and exit")
    parser.add_argument("--camera-scan-max", type=int, default=10, help="Max camera index (exclusive) for listing")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    print("[cli] starting mediapipe-vmc-converter", flush=True)
    if args.list_cameras:
        print(f"[camera] scanning 0..{max(0, args.camera_scan_max - 1)}", flush=True)
        cameras = list_cameras(max_index=max(1, args.camera_scan_max))
        for cam in cameras:
            print(
                f"[camera] index={cam['index']} opened={cam['opened']} "
                f"size={cam['width']}x{cam['height']} backend={cam['backend_name']}({cam['backend_id']})",
                flush=True,
            )
        return

    cfg = AppConfig.load(args.config, override_target=args.target)
    if args.camera is not None:
        cfg.camera_index = args.camera
    if args.model is not None:
        cfg.model_path = args.model
    if args.enable_face:
        cfg.enable_face = True
    if args.enable_hands:
        cfg.enable_hands = True

    model_path = _resolve_model_path(args.config, cfg.model_path)
    cfg.model_path = str(model_path)
    print(f"[cli] resolved model={cfg.model_path}", flush=True)
    cams = list_cameras(max_index=max(5, cfg.camera_index + 3))
    selected = next((cam for cam in cams if cam["index"] == cfg.camera_index), None)
    for cam in cams:
        if cam["opened"]:
            print(
                f"[camera] available index={cam['index']} size={cam['width']}x{cam['height']} "
                f"backend={cam['backend_name']}({cam['backend_id']})",
                flush=True,
            )
    if selected is not None:
        print(
            f"[camera] selected index={selected['index']} opened={selected['opened']} "
            f"size={selected['width']}x{selected['height']} backend={selected['backend_name']}({selected['backend_id']})",
            flush=True,
        )
    else:
        print(f"[camera] selected index={cfg.camera_index} not in scan range", flush=True)

    run(cfg)


if __name__ == "__main__":
    main()
