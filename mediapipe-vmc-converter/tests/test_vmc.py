from mediapipe_vmc_converter.vmc import (
    VmcSender,
    to_vmc_bone_vrm1,
    to_vmc_position,
    to_vmc_quaternion_vrm0,
)


def test_to_vmc_position_mirror() -> None:
    assert to_vmc_position((1.0, 2.0, 3.0), mirror=True) == (-1.0, 2.0, 3.0)
    assert to_vmc_position((1.0, 2.0, 3.0), mirror=False) == (1.0, 2.0, -3.0)


def test_to_vmc_quaternion_vrm0() -> None:
    assert to_vmc_quaternion_vrm0((0.1, 0.2, 0.3, 0.4)) == (-0.1, -0.2, 0.3, 0.4)


def test_to_vmc_bone_vrm1() -> None:
    pos, rot = to_vmc_bone_vrm1((1.0, 2.0, 3.0), (0.1, 0.2, 0.3, 0.4))
    assert pos == (-1.0, 2.0, 3.0)
    assert rot == (0.1, -0.2, -0.3, 0.4)


def test_ok_message_contains_int_tag() -> None:
    msg = VmcSender.build_ok_message()
    assert b",i" in msg.dgram
