from __future__ import annotations

from dataclasses import dataclass

from pythonosc import udp_client
from pythonosc.osc_message_builder import OscMessageBuilder


VRM_SCALE = 11.0


def to_vmc_position(pos: tuple[float, float, float], mirror: bool = True) -> tuple[float, float, float]:
    sign = -1.0 if mirror else 1.0
    return (pos[0] * sign, pos[1], -pos[2] * sign)


def to_vmc_quaternion_vrm0(q: tuple[float, float, float, float]) -> tuple[float, float, float, float]:
    return (-q[0], -q[1], q[2], q[3])


def to_vmc_bone_vrm1(pos: tuple[float, float, float], rot: tuple[float, float, float, float]) -> tuple[tuple[float, float, float], tuple[float, float, float, float]]:
    pos1 = (-pos[0], pos[1], -pos[2])
    rot1 = (-rot[0], rot[1], -rot[2], rot[3])
    vmc_pos = (pos1[0], pos1[1], -pos1[2])
    vmc_rot = (-rot1[0], -rot1[1], rot1[2], rot1[3])
    return vmc_pos, vmc_rot


def vrm1_to_vrm0_bone_name(name: str) -> str:
    name = name.replace("ThumbProximal", "ThumbIntermediate")
    name = name.replace("ThumbMetacarpal", "ThumbProximal")
    return name


@dataclass
class BonePacket:
    name: str
    pos: tuple[float, float, float]
    rot: tuple[float, float, float, float]


class VmcSender:
    def __init__(self, host: str, port: int) -> None:
        self.client = udp_client.SimpleUDPClient(host, port)
        self._sent_ok = False

    @staticmethod
    def build_ok_message() -> object:
        builder = OscMessageBuilder(address="/VMC/Ext/OK")
        builder.add_arg(1, "i")
        return builder.build()

    def send_frame(
        self,
        root_pos: tuple[float, float, float],
        root_rot: tuple[float, float, float, float],
        bones: list[BonePacket],
        send_ok_every_frame: bool = False,
        blend_shapes: dict[str, float] | None = None,
    ) -> None:
        self.client.send_message("/VMC/Ext/Root/Pos", ["root", *root_pos, *root_rot])
        for bone in bones:
            self.client.send_message("/VMC/Ext/Bone/Pos", [bone.name, *bone.pos, *bone.rot])
        if blend_shapes:
            for k, v in blend_shapes.items():
                self.client.send_message("/VMC/Ext/Blend/Val", [k, float(v)])
            self.client.send_message("/VMC/Ext/Blend/Apply", [])

        if send_ok_every_frame or not self._sent_ok:
            self.client.send(self.build_ok_message())
            self._sent_ok = True
