from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy.spatial.transform import Rotation


LM = {
    "NOSE": 0,
    "LEFT_SHOULDER": 11,
    "RIGHT_SHOULDER": 12,
    "LEFT_ELBOW": 13,
    "RIGHT_ELBOW": 14,
    "LEFT_WRIST": 15,
    "RIGHT_WRIST": 16,
    "LEFT_HIP": 23,
    "RIGHT_HIP": 24,
    "LEFT_KNEE": 25,
    "RIGHT_KNEE": 26,
    "LEFT_ANKLE": 27,
    "RIGHT_ANKLE": 28,
}

T_POSE_DIRECTIONS = {
    "Spine": np.array([0.0, 1.0, 0.0]),
    "Chest": np.array([0.0, 1.0, 0.0]),
    "Neck": np.array([0.0, 1.0, 0.0]),
    "Head": np.array([0.0, 1.0, 0.0]),
    "LeftUpperArm": np.array([-1.0, 0.0, 0.0]),
    "LeftLowerArm": np.array([-1.0, 0.0, 0.0]),
    "RightUpperArm": np.array([1.0, 0.0, 0.0]),
    "RightLowerArm": np.array([1.0, 0.0, 0.0]),
    "LeftUpperLeg": np.array([0.0, -1.0, 0.0]),
    "LeftLowerLeg": np.array([0.0, -1.0, 0.0]),
    "RightUpperLeg": np.array([0.0, -1.0, 0.0]),
    "RightLowerLeg": np.array([0.0, -1.0, 0.0]),
}


@dataclass
class BonePose:
    name: str
    pos: np.ndarray
    rot_xyzw: tuple[float, float, float, float]


def _normalize(v: np.ndarray) -> np.ndarray:
    n = np.linalg.norm(v)
    if n < 1e-8:
        return np.array([0.0, 1.0, 0.0], dtype=np.float64)
    return v / n


def quaternion_from_two_vectors(a: np.ndarray, b: np.ndarray) -> Rotation:
    av = _normalize(a)
    bv = _normalize(b)
    cross = np.cross(av, bv)
    dot = float(np.dot(av, bv))
    if dot < -0.999999:
        axis = _normalize(np.cross(np.array([1.0, 0.0, 0.0]), av))
        if np.linalg.norm(axis) < 1e-4:
            axis = _normalize(np.cross(np.array([0.0, 1.0, 0.0]), av))
        return Rotation.from_rotvec(axis * np.pi)
    q = np.array([cross[0], cross[1], cross[2], 1.0 + dot], dtype=np.float64)
    q /= np.linalg.norm(q)
    return Rotation.from_quat(q)


def calc_bone_rotation(parent_pos: np.ndarray, child_pos: np.ndarray, t_pose_dir: np.ndarray, parent_chain_rot: Rotation) -> Rotation:
    world_dir = _normalize(child_pos - parent_pos)
    local_dir = parent_chain_rot.inv().apply(world_dir)
    return quaternion_from_two_vectors(t_pose_dir, local_dir)


def _mid(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return (a + b) * 0.5


def _safe_cross(a: np.ndarray, b: np.ndarray, fallback: np.ndarray) -> np.ndarray:
    c = np.cross(a, b)
    if np.linalg.norm(c) < 1e-8:
        return fallback
    return _normalize(c)


def calc_root_rotation(hips: np.ndarray, shoulder_mid: np.ndarray, left_shoulder: np.ndarray, right_shoulder: np.ndarray) -> Rotation:
    right_axis = _normalize(right_shoulder - left_shoulder)
    up_axis = _normalize(shoulder_mid - hips)
    # MediaPipe body basis points backward relative to VSeeFace/VRM, so flip
    # only the derived forward axis here instead of adding a later Y180 hack.
    forward_axis = -_safe_cross(right_axis, up_axis, np.array([0.0, 0.0, 1.0], dtype=np.float64))
    up_axis = _safe_cross(forward_axis, right_axis, np.array([0.0, 1.0, 0.0], dtype=np.float64))
    basis = np.column_stack((right_axis, up_axis, forward_axis))
    return Rotation.from_matrix(basis)


def solve_pose_to_bones(points: np.ndarray) -> tuple[np.ndarray, Rotation, list[BonePose]]:
    left_hip = points[LM["LEFT_HIP"]]
    right_hip = points[LM["RIGHT_HIP"]]
    left_shoulder = points[LM["LEFT_SHOULDER"]]
    right_shoulder = points[LM["RIGHT_SHOULDER"]]
    nose = points[LM["NOSE"]]

    hips = _mid(left_hip, right_hip)
    shoulder_mid = _mid(left_shoulder, right_shoulder)

    root_rot = calc_root_rotation(hips, shoulder_mid, left_shoulder, right_shoulder)
    bones: list[BonePose] = []

    spine_rot = Rotation.identity()
    chest_rot = Rotation.identity()
    neck_rot = calc_bone_rotation(shoulder_mid, nose, T_POSE_DIRECTIONS["Neck"], root_rot)
    head_rot = calc_bone_rotation(shoulder_mid, nose, T_POSE_DIRECTIONS["Head"], root_rot * neck_rot)

    l_upper_rot = calc_bone_rotation(left_shoulder, points[LM["LEFT_ELBOW"]], T_POSE_DIRECTIONS["LeftUpperArm"], root_rot)
    l_lower_rot = calc_bone_rotation(points[LM["LEFT_ELBOW"]], points[LM["LEFT_WRIST"]], T_POSE_DIRECTIONS["LeftLowerArm"], root_rot * l_upper_rot)
    r_upper_rot = calc_bone_rotation(right_shoulder, points[LM["RIGHT_ELBOW"]], T_POSE_DIRECTIONS["RightUpperArm"], root_rot)
    r_lower_rot = calc_bone_rotation(points[LM["RIGHT_ELBOW"]], points[LM["RIGHT_WRIST"]], T_POSE_DIRECTIONS["RightLowerArm"], root_rot * r_upper_rot)

    l_upper_leg_rot = calc_bone_rotation(left_hip, points[LM["LEFT_KNEE"]], T_POSE_DIRECTIONS["LeftUpperLeg"], root_rot)
    l_lower_leg_rot = calc_bone_rotation(points[LM["LEFT_KNEE"]], points[LM["LEFT_ANKLE"]], T_POSE_DIRECTIONS["LeftLowerLeg"], root_rot * l_upper_leg_rot)
    r_upper_leg_rot = calc_bone_rotation(right_hip, points[LM["RIGHT_KNEE"]], T_POSE_DIRECTIONS["RightUpperLeg"], root_rot)
    r_lower_leg_rot = calc_bone_rotation(points[LM["RIGHT_KNEE"]], points[LM["RIGHT_ANKLE"]], T_POSE_DIRECTIONS["RightLowerLeg"], root_rot * r_upper_leg_rot)

    def add(name: str, pos: np.ndarray, rot: Rotation) -> None:
        q = rot.as_quat()
        bones.append(BonePose(name=name, pos=pos, rot_xyzw=(float(q[0]), float(q[1]), float(q[2]), float(q[3]))))

    add("Spine", hips, spine_rot)
    add("Chest", shoulder_mid, chest_rot)
    add("Neck", shoulder_mid, neck_rot)
    add("Head", nose, head_rot)
    add("LeftUpperArm", left_shoulder, l_upper_rot)
    add("LeftLowerArm", points[LM["LEFT_ELBOW"]], l_lower_rot)
    add("RightUpperArm", right_shoulder, r_upper_rot)
    add("RightLowerArm", points[LM["RIGHT_ELBOW"]], r_lower_rot)
    add("LeftUpperLeg", left_hip, l_upper_leg_rot)
    add("LeftLowerLeg", points[LM["LEFT_KNEE"]], l_lower_leg_rot)
    add("RightUpperLeg", right_hip, r_upper_leg_rot)
    add("RightLowerLeg", points[LM["RIGHT_KNEE"]], r_lower_leg_rot)

    return hips, root_rot, bones
