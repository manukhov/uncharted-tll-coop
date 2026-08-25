#!/usr/bin/env python3
"""Reconstruct Chloe's steady Mazur holster Locator from retail assets.

The audit is offline.  It evaluates Chloe's exact 5F913F74 animation with the
retail joint-program handlers, recovers ``upper_base`` joint 25 from the
``chloe-base`` inverse-bind table, composes joints 26..29 and the exact
``targSpineProp`` marker through retail ``Transform_Compose``, and prints the
result relative to the Player root.  It never launches or attaches to the game.
"""

from __future__ import annotations

import argparse
import ctypes
import math
from pathlib import Path
import struct

import native_anim_pose_audit as native


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SKELETON = ROOT / "reports" / "actor_packages" / "chloe-base.pak"
ANIMATION_OFFSET = 0x17A460
SKELETON_OBJECT_OFFSET = 0x5A180
INVERSE_BIND_OFFSET = 0x8C350
UPPER_BASE_JOINT = 25
SPINE_CHAIN = (25, 26, 27, 28, 29)
SPINE_PROP_ATTACHMENT_JOINT = 47
SAMPLES = (0.0, 84.0, 168.0, 252.0, 335.0)
MARKER_BITS = (
    0x3CCCCCCD,
    0x3DEF9DB2,
    0xBDCED917,
    0x3F800000,
    0x3F45AE02,
    0xBF10FC5B,
    0xBC4A2765,
    0x3E93592B,
)
EXPECTED_ENDPOINT_BITS = (
    0x3D627B2F,
    0x3FA9294C,
    0xBDC35992,
    0x3F800000,
    0xBF058597,
    0x3F1D1AF9,
    0xBE88E0D5,
    0xBF076D88,
)


def dot(left: tuple[float, ...], right: tuple[float, ...]) -> float:
    return sum(a * b for a, b in zip(left, right))


def inverse_bind_world(
    skeleton: bytes, joint: int
) -> tuple[tuple[float, float, float], tuple[float, float, float, float]]:
    values = struct.unpack_from(
        "<12f", skeleton, INVERSE_BIND_OFFSET + joint * 48
    )
    inverse_rotation = (
        values[0:3],
        values[4:7],
        values[8:11],
    )
    for row in inverse_rotation:
        if not math.isclose(dot(row, row), 1.0, abs_tol=2.0e-4):
            raise ValueError("inverse-bind rotation row is not unit length")
    world_rotation = tuple(
        tuple(inverse_rotation[column][row] for column in range(3))
        for row in range(3)
    )
    inverse_translation = (values[3], values[7], values[11])
    world_translation = tuple(
        -dot(world_rotation[row], inverse_translation) for row in range(3)
    )
    return world_translation, matrix_to_quaternion(world_rotation)


def matrix_to_quaternion(
    rotation: tuple[tuple[float, float, float], ...]
) -> tuple[float, float, float, float]:
    trace = rotation[0][0] + rotation[1][1] + rotation[2][2]
    if trace > 0.0:
        scale = math.sqrt(trace + 1.0) * 2.0
        quaternion = (
            (rotation[2][1] - rotation[1][2]) / scale,
            (rotation[0][2] - rotation[2][0]) / scale,
            (rotation[1][0] - rotation[0][1]) / scale,
            0.25 * scale,
        )
    elif rotation[0][0] > rotation[1][1] and rotation[0][0] > rotation[2][2]:
        scale = math.sqrt(
            1.0 + rotation[0][0] - rotation[1][1] - rotation[2][2]
        ) * 2.0
        quaternion = (
            0.25 * scale,
            (rotation[0][1] + rotation[1][0]) / scale,
            (rotation[0][2] + rotation[2][0]) / scale,
            (rotation[2][1] - rotation[1][2]) / scale,
        )
    elif rotation[1][1] > rotation[2][2]:
        scale = math.sqrt(
            1.0 + rotation[1][1] - rotation[0][0] - rotation[2][2]
        ) * 2.0
        quaternion = (
            (rotation[0][1] + rotation[1][0]) / scale,
            0.25 * scale,
            (rotation[1][2] + rotation[2][1]) / scale,
            (rotation[0][2] - rotation[2][0]) / scale,
        )
    else:
        scale = math.sqrt(
            1.0 + rotation[2][2] - rotation[0][0] - rotation[1][1]
        ) * 2.0
        quaternion = (
            (rotation[0][2] + rotation[2][0]) / scale,
            (rotation[1][2] + rotation[2][1]) / scale,
            0.25 * scale,
            (rotation[1][0] - rotation[0][1]) / scale,
        )
    norm = math.sqrt(dot(quaternion, quaternion))
    if not 0.999 <= norm <= 1.001:
        raise ValueError(f"invalid reconstructed quaternion norm {norm}")
    return tuple(value / norm for value in quaternion)


def transform_bytes(
    translation: tuple[float, ...], quaternion: tuple[float, ...]
) -> bytes:
    return struct.pack(
        "<8f", translation[0], translation[1], translation[2], 1.0, *quaternion
    )


def compose(image: native.MappedImage, parent: bytes, local: bytes) -> bytes:
    parent_buffer = ctypes.create_string_buffer(parent)
    local_buffer = ctypes.create_string_buffer(local)
    output = ctypes.create_string_buffer(32)
    function = image.function3(native.TRANSFORM_COMPOSE)
    result = function(
        ctypes.c_void_p(ctypes.addressof(parent_buffer)),
        ctypes.c_void_p(ctypes.addressof(output)),
        ctypes.c_void_p(ctypes.addressof(local_buffer)),
    )
    if result != ctypes.addressof(output):
        raise ValueError("retail Transform_Compose returned an unexpected pointer")
    values = list(struct.unpack("<8f", output.raw))
    values[3] = 1.0  # transform position W is non-spatial bookkeeping
    return struct.pack("<8f", *values)


def angular_distance_degrees(left: bytes, right: bytes) -> float:
    left_q = struct.unpack_from("<4f", left, 16)
    right_q = struct.unpack_from("<4f", right, 16)
    cosine = abs(dot(left_q, right_q)) / math.sqrt(
        dot(left_q, left_q) * dot(right_q, right_q)
    )
    return math.degrees(2.0 * math.acos(max(-1.0, min(1.0, cosine))))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=native.DEFAULT_GAME / "tll.exe")
    parser.add_argument(
        "--archive",
        type=Path,
        default=(
            native.DEFAULT_GAME
            / "Uncharted4_data"
            / "build"
            / "pc"
            / "thelostlegacy"
            / "actor77.psarc"
        ),
    )
    parser.add_argument("--skeleton-pak", type=Path, default=DEFAULT_SKELETON)
    args = parser.parse_args()

    audit = native.load_audit_module()
    archive = audit.Psarc(args.archive, None)
    animation = archive.extract(native.DEFAULT_ENTRY)
    skeleton = args.skeleton_pak.read_bytes()
    inverse_bind_relative = struct.unpack_from(
        "<I", skeleton, SKELETON_OBJECT_OFFSET + 0x28
    )[0]
    if SKELETON_OBJECT_OFFSET + inverse_bind_relative != INVERSE_BIND_OFFSET:
        raise ValueError("unexpected chloe-base inverse-bind table pointer")
    root_translation, root_quaternion = inverse_bind_world(
        skeleton, UPPER_BASE_JOINT
    )
    marker = struct.pack("<8I", *MARKER_BITS)

    image = native.MappedImage(args.exe)
    endpoints: list[tuple[float, bytes]] = []
    try:
        bind_spined_translation, bind_spined_quaternion = inverse_bind_world(
            skeleton, SPINE_CHAIN[-1]
        )
        bind_marker = compose(
            image,
            transform_bytes(bind_spined_translation, bind_spined_quaternion),
            marker,
        )
        bind_attachment_translation, bind_attachment_quaternion = inverse_bind_world(
            skeleton, SPINE_PROP_ATTACHMENT_JOINT
        )
        bind_attachment = transform_bytes(
            bind_attachment_translation, bind_attachment_quaternion
        )
        bind_position_delta = math.dist(
            struct.unpack_from("<3f", bind_marker),
            struct.unpack_from("<3f", bind_attachment),
        )
        bind_angle_delta = angular_distance_degrees(
            bind_marker, bind_attachment
        )
        if bind_position_delta > 0.02:
            raise ValueError("marker composition disagrees with Chloe bind skeleton")
        print(
            f"bindMarkerVsSpinePropAttachment position={bind_position_delta:.9g} "
            f"angleDeg={bind_angle_delta:.9g}"
        )
        for sample in SAMPLES:
            pose, opcodes, _destinations, _count, stride = native.decode_phase(
                image, animation, ANIMATION_OFFSET, sample, 60
            )
            if opcodes != [22, 24, 11, 27, 29, 30, 15, 16] or stride != 60:
                raise ValueError(f"unexpected 5F913F74 program {opcodes}/{stride}")
            current = transform_bytes(root_translation, root_quaternion)
            for joint in SPINE_CHAIN:
                values = native.joint_values(pose, joint)
                current = compose(
                    image,
                    current,
                    transform_bytes(values[8:11], values[4:8]),
                )
            current = compose(image, current, marker)
            endpoints.append((sample, current))
            values = struct.unpack("<8f", current)
            bits = struct.unpack("<8I", current)
            print(
                f"sample={sample:g} "
                f"T=({values[0]:.9g},{values[1]:.9g},{values[2]:.9g}) "
                f"Q=({values[4]:.9g},{values[5]:.9g},"
                f"{values[6]:.9g},{values[7]:.9g})"
            )
            print("  bits=" + ",".join(f"0x{value:08X}" for value in bits))
    finally:
        image.close()

    if endpoints[0][1] != endpoints[-1][1]:
        raise ValueError("5F913F74 loop endpoints are not byte-identical")
    if struct.unpack("<8I", endpoints[0][1]) != EXPECTED_ENDPOINT_BITS:
        raise ValueError("retail endpoint no longer matches the embedded constant")
    first_translation = struct.unpack_from("<3f", endpoints[0][1])
    for sample, endpoint in endpoints[1:]:
        translation = struct.unpack_from("<3f", endpoint)
        print(
            f"deltaFrom0 sample={sample:g} "
            f"position={math.dist(first_translation, translation):.9g} "
            f"angleDeg={angular_distance_degrees(endpoints[0][1], endpoint):.9g}"
        )
    print("loopClosure=exact embeddedConstant=exact")


if __name__ == "__main__":
    main()
