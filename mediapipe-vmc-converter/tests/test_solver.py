import numpy as np
from scipy.spatial.transform import Rotation

from mediapipe_vmc_converter.solver import calc_bone_rotation


def test_calc_bone_rotation_identity() -> None:
    parent = np.array([0.0, 0.0, 0.0])
    child = np.array([0.0, 1.0, 0.0])
    t_dir = np.array([0.0, 1.0, 0.0])
    rot = calc_bone_rotation(parent, child, t_dir, Rotation.identity())
    q = rot.as_quat()
    assert np.allclose(q, np.array([0.0, 0.0, 0.0, 1.0]), atol=1e-6)


def test_calc_bone_rotation_with_parent_chain() -> None:
    parent_chain = Rotation.from_euler("z", 90, degrees=True)
    parent = np.array([0.0, 0.0, 0.0])
    child = np.array([1.0, 0.0, 0.0])
    t_dir = np.array([0.0, 1.0, 0.0])
    rot = calc_bone_rotation(parent, child, t_dir, parent_chain)
    # parent inverse makes +X into -Y in local; expected rotation near 180deg around X/Z axis family.
    out = rot.apply(t_dir)
    local_dir = parent_chain.inv().apply((child - parent) / np.linalg.norm(child - parent))
    assert np.allclose(out, local_dir, atol=1e-6)
