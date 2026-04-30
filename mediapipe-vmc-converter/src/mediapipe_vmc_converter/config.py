from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path


TARGET_PRESETS = {
    "vseeface": {"port": 39539, "vrm_version": "vrm0", "root_turned_around": False},
    "vnyan": {"port": 39539, "vrm_version": "vrm0", "root_turned_around": False},
    "warudo": {"port": 19190, "vrm_version": "vrm1", "root_turned_around": True},
}


@dataclass
class AppConfig:
    target: str = "vseeface"
    host: str = "127.0.0.1"
    port: int = 39539
    mirror: bool = True
    vrm_version: str = "vrm0"
    root_turned_around: bool = False
    camera_index: int = 0
    model_path: str = "models/pose_landmarker_full.task"
    visibility_threshold: float = 0.5
    fps: float = 30.0
    send_ok_every_frame: bool = False
    enable_face: bool = False
    enable_hands: bool = False

    @staticmethod
    def load(path: str | Path, override_target: str | None = None) -> "AppConfig":
        config_path = Path(path)
        payload = json.loads(config_path.read_text(encoding="utf-8"))
        cfg = AppConfig(**payload)
        if override_target:
            cfg.target = override_target
        preset = TARGET_PRESETS.get(cfg.target.lower())
        if preset:
            cfg.port = preset["port"]
            cfg.vrm_version = preset["vrm_version"]
            cfg.root_turned_around = preset["root_turned_around"]
        return cfg
