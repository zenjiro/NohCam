from __future__ import annotations

import time

import numpy as np
from .config import AppConfig
from .filtering import LandmarkFilterBank, OneEuroParams
from .landmark import PoseLandmarkStream, extract_pose_frame
from .solver import BonePose, solve_pose_to_bones
from .vmc import BonePacket, VmcSender, to_vmc_bone_vrm1, to_vmc_position, to_vmc_quaternion_vrm0


SUPPORTED_BONES = {
    "LeftUpperArm",
    "LeftLowerArm",
    "RightUpperArm",
    "RightLowerArm",
}


def _convert_bones(bones: list[BonePose], cfg: AppConfig) -> list[BonePacket]:
    out: list[BonePacket] = []
    for bone in bones:
        if bone.name not in SUPPORTED_BONES:
            continue
        # VMC bone position expects local offset; for tracker-style retargeting
        # we keep bone local position at zero and drive mainly by rotation.
        pos = (0.0, 0.0, 0.0)
        rot = to_vmc_quaternion_vrm0(bone.rot_xyzw)
        if cfg.vrm_version.lower() == "vrm1":
            pos, rot = to_vmc_bone_vrm1(pos, rot)
        out.append(BonePacket(name=bone.name, pos=pos, rot=rot))
    return out


def run(cfg: AppConfig) -> None:
    print(f"[startup] target={cfg.target} host={cfg.host} port={cfg.port} camera={cfg.camera_index}", flush=True)
    print(f"[startup] model={cfg.model_path}", flush=True)
    stream = PoseLandmarkStream(cfg.model_path, cfg.camera_index)
    sender = VmcSender(cfg.host, cfg.port)
    pose_filter = LandmarkFilterBank(33, OneEuroParams(frequency=cfg.fps, mincutoff=1.0, beta=1.0, dcutoff=2.0))
    prev_points: np.ndarray | None = None
    prev_ts: float | None = None
    frames = 0
    sent = 0
    no_pose = 0
    last_log = time.time()
    try:
        while True:
            frames += 1
            _, result, ts = stream.read()
            if result is None:
                time.sleep(0.001)
                continue
            frame = extract_pose_frame(result, ts, cfg.visibility_threshold, prev_points)
            if frame is None:
                no_pose += 1
                now = time.time()
                if now - last_log >= 2.0:
                    print(f"[status] frames={frames} sent={sent} no_pose={no_pose}", flush=True)
                    last_log = now
                continue
            if prev_ts is None:
                dt = 1.0 / cfg.fps
            else:
                dt = max(1e-3, frame.timestamp_s - prev_ts)
            filtered = pose_filter.filter_points(frame.world_points, dt)
            prev_points = filtered
            prev_ts = frame.timestamp_s

            hips, root_rot, bones = solve_pose_to_bones(filtered)
            # Keep the avatar anchored in place, but preserve root rotation.
            root_pos = (0.0, 0.0, 0.0)
            root_q = (0.0, 0.0, 0.0, 1.0)
            if cfg.vrm_version.lower() == "vrm1":
                root_pos, root_q = to_vmc_bone_vrm1(root_pos, root_q)

            sender.send_frame(
                root_pos=root_pos,
                root_rot=root_q,
                bones=_convert_bones(bones, cfg),
                send_ok_every_frame=cfg.send_ok_every_frame,
                blend_shapes=None,
            )
            sent += 1
            now = time.time()
            if now - last_log >= 2.0:
                print(f"[status] frames={frames} sent={sent} no_pose={no_pose}", flush=True)
                last_log = now
    finally:
        stream.close()
