#!/usr/bin/env python3
"""Evaluate one TLL ANIM group with the retail joint-program handlers.

This is an offline, build-pinned audit.  It maps tll.exe into the current
helper process, applies PE relocations, and calls only the known joint-program
opcode handlers over an in-memory PSARC entry.  It never launches or attaches
to the game.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import importlib.util
import math
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME = Path(
    r"C:\UNCHARTED Legacy of Thieves Collection (2016-2017-2022)"
    r"\Uncharted Legacy of Thieves Collection"
)
DEFAULT_ENTRY = "anim-chloe-relaxed-movement.pak"
EXPECTED_EXE_SHA256 = (
    "ed1371a4b47961abc160e92aa0b9bdfd787638fa1a39ba2403b423ab45c210be"
)
PREFERRED_BASE = 0x140000000

MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
PAGE_EXECUTE_READ = 0x20
IMAGE_REL_BASED_DIR64 = 10


def u16(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def load_audit_module():
    path = ROOT / "scripts" / "psarc_asset_audit.py"
    spec = importlib.util.spec_from_file_location("psarc_asset_audit", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load psarc_asset_audit.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class MappedImage:
    def __init__(self, path: Path) -> None:
        raw = path.read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        if digest != EXPECTED_EXE_SHA256:
            raise ValueError(
                f"unsupported tll.exe SHA-256 {digest}; "
                f"expected {EXPECTED_EXE_SHA256}"
            )
        pe = u32(raw, 0x3C)
        if raw[pe : pe + 4] != b"PE\0\0":
            raise ValueError("bad PE signature")
        coff = pe + 4
        section_count = u16(raw, coff + 2)
        optional_size = u16(raw, coff + 16)
        optional = coff + 20
        if u16(raw, optional) != 0x20B:
            raise ValueError("expected PE32+")
        image_base = u64(raw, optional + 0x18)
        image_size = u32(raw, optional + 0x38)
        header_size = u32(raw, optional + 0x3C)
        reloc_rva = u32(raw, optional + 0x70 + 5 * 8)
        reloc_size = u32(raw, optional + 0x70 + 5 * 8 + 4)
        if image_base != PREFERRED_BASE:
            raise ValueError(f"unexpected image base 0x{image_base:X}")

        self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._kernel32.VirtualAlloc.restype = ctypes.c_void_p
        address = self._kernel32.VirtualAlloc(
            None, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE
        )
        if not address:
            raise OSError(ctypes.get_last_error(), "VirtualAlloc")
        self.address = int(address)
        self.size = image_size
        ctypes.memmove(self.address, raw, min(header_size, len(raw)))

        sections = optional + optional_size
        for index in range(section_count):
            row = sections + index * 40
            virtual_size = u32(raw, row + 8)
            virtual_address = u32(raw, row + 12)
            raw_size = u32(raw, row + 16)
            raw_offset = u32(raw, row + 20)
            copied = min(raw_size, max(virtual_size, raw_size))
            if copied:
                ctypes.memmove(
                    self.address + virtual_address,
                    raw[raw_offset : raw_offset + copied],
                    copied,
                )

        delta = self.address - image_base
        if delta and reloc_rva and reloc_size:
            cursor = reloc_rva
            end = reloc_rva + reloc_size
            while cursor + 8 <= end:
                page_rva = ctypes.c_uint32.from_address(
                    self.address + cursor
                ).value
                block_size = ctypes.c_uint32.from_address(
                    self.address + cursor + 4
                ).value
                if block_size < 8 or cursor + block_size > end:
                    raise ValueError("invalid relocation block")
                for item in range((block_size - 8) // 2):
                    encoded = ctypes.c_uint16.from_address(
                        self.address + cursor + 8 + 2 * item
                    ).value
                    kind = encoded >> 12
                    offset = encoded & 0xFFF
                    if kind == IMAGE_REL_BASED_DIR64:
                        location = self.address + page_rva + offset
                        value = ctypes.c_uint64.from_address(location).value
                        ctypes.c_uint64.from_address(location).value = (
                            value + delta
                        ) & 0xFFFFFFFFFFFFFFFF
                    elif kind != 0:
                        raise ValueError(f"unsupported relocation type {kind}")
                cursor += block_size

        # The retail CRT initializer at 0x140128020 materializes four AVX
        # permutation rows used by joint-program opcode 28.  A mapped image
        # does not run the CRT constructor table, so copy only the two exact
        # build-pinned constant blocks instead of executing arbitrary game
        # initialization inside the audit helper.
        for source_va, destination_va in (
            (0x142BF0700, 0x146C71CE0),
            (0x142BF0740, 0x146C71D00),
        ):
            ctypes.memmove(
                self.address + (destination_va - PREFERRED_BASE),
                self.address + (source_va - PREFERRED_BASE),
                0x20,
            )

        old = ctypes.c_ulong()
        if not self._kernel32.VirtualProtect(
            ctypes.c_void_p(self.address),
            self.size,
            PAGE_EXECUTE_READ,
            ctypes.byref(old),
        ):
            raise OSError(ctypes.get_last_error(), "VirtualProtect")
        self._kernel32.FlushInstructionCache(
            self._kernel32.GetCurrentProcess(),
            ctypes.c_void_p(self.address),
            self.size,
        )

    def close(self) -> None:
        if self.address:
            if not self._kernel32.VirtualFree(
                ctypes.c_void_p(self.address), 0, MEM_RELEASE
            ):
                raise OSError(ctypes.get_last_error(), "VirtualFree")
            self.address = 0

    def function(self, va: int):
        prototype = ctypes.WINFUNCTYPE(
            ctypes.c_uint64, ctypes.c_void_p, ctypes.c_void_p
        )
        return prototype(self.address + (va - PREFERRED_BASE))

    def function3(self, va: int):
        prototype = ctypes.WINFUNCTYPE(
            ctypes.c_uint64, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p
        )
        return prototype(self.address + (va - PREFERRED_BASE))


HANDLERS = {
    11: 0x142193790,
    15: 0x142193970,
    16: 0x1421939F0,
    22: 0x142193CE0,
    24: 0x142193D60,
    27: 0x142193E30,
    28: 0x142193E90,
    29: 0x142193EF0,
    30: 0x142193F50,
}
TRANSFORM_COMPOSE = 0x1401998F0


def decode_phase(
    image: MappedImage,
    animation: bytes,
    animation_offset: int,
    sample: float,
    expected_frame_stride: int | None,
    group_index: int = 0,
) -> tuple[bytes, list[int], dict[int, tuple[int, ...]], int, int]:
    animation_buffer = ctypes.create_string_buffer(animation)
    base = ctypes.addressof(animation_buffer) + animation_offset
    group_count = ctypes.c_uint16.from_address(base + 0x0C).value
    if group_index < 0 or group_index >= group_count:
        raise ValueError(
            f"group index {group_index} exceeds animation group count {group_count}"
        )
    group_table = ctypes.c_uint32.from_address(base + 0x14).value
    group = base + group_table + group_index * 0x10
    program_rel = ctypes.c_uint32.from_address(group).value
    frame_data_rel = ctypes.c_uint32.from_address(group + 8).value
    frame_stride = ctypes.c_uint16.from_address(group + 12).value
    frame_count = ctypes.c_uint16.from_address(base + 0x22).value
    clamped = min(max(sample, 0.0), float(max(frame_count - 1, 0)))
    frame = min(int(math.floor(clamped)), max(frame_count - 2, 0))
    fraction = clamped - frame

    output = ctypes.create_string_buffer(0x30 * 512)
    state = ctypes.create_string_buffer(0x1000)
    state_address = ctypes.addressof(state)
    if frame_stride:
        ctypes.c_uint64.from_address(state_address).value = (
            base + frame_data_rel + frame_stride * frame
        )
    ctypes.c_uint64.from_address(state_address + 8).value = ctypes.addressof(output)
    ctypes.c_float.from_address(state_address + 0x20).value = fraction

    bases = (ctypes.c_uint64 * 4)()
    bases[0] = base + program_rel
    bases[3] = state_address
    bases_address = ctypes.addressof(bases)
    command = base + program_rel
    seen: list[int] = []
    destinations_by_opcode: dict[int, tuple[int, ...]] = {}
    for _ in range(32):
        header = ctypes.c_uint16.from_address(command).value
        opcode = header & 0xFF
        words = header >> 8
        if opcode == 0:
            break
        if opcode not in HANDLERS:
            raise ValueError(f"unhandled opcode {opcode} at +0x{command-base:X}")
        if opcode in (22, 24):
            count = ctypes.c_uint16.from_address(command + 2).value
            payload_offset = ctypes.c_uint16.from_address(command + 6).value
            payload = base + program_rel + payload_offset
            destinations_by_opcode[opcode] = tuple(
                ctypes.c_uint16.from_address(payload + 2 * index).value
                for index in range(count)
            )
        image.function(HANDLERS[opcode])(
            ctypes.c_void_p(command + 2), ctypes.c_void_p(bases_address)
        )
        seen.append(opcode)
        command += 2 * (1 + words)
    else:
        raise ValueError("unterminated group-zero program")
    if 11 in seen and frame_count < 2:
        raise ValueError(f"unexpected dynamic frame count {frame_count}")
    if (
        expected_frame_stride is not None
        and frame_stride != expected_frame_stride
    ):
        raise ValueError(
            f"unexpected frame stride {frame_stride}; "
            f"expected {expected_frame_stride}"
        )
    return output.raw, seen, destinations_by_opcode, frame_count, frame_stride


def joint_values(pose: bytes, joint: int) -> tuple[float, ...]:
    return struct.unpack_from("<12f", pose, joint * 0x30)


def validate_target_pose(pose: bytes, first_joint: int, last_joint: int) -> None:
    for joint in range(first_joint, last_joint + 1):
        values = joint_values(pose, joint)
        if not all(math.isfinite(value) for value in values[:3] + values[4:11]):
            raise ValueError(f"non-finite target transform at joint {joint}")
        quaternion_norm = math.sqrt(sum(value * value for value in values[4:8]))
        if not 0.999 <= quaternion_norm <= 1.001:
            raise ValueError(
                f"non-unit target quaternion at joint {joint}: {quaternion_norm}"
            )


def decode_constant_pose(
    audit, animation: bytes, animation_offset: int
) -> dict[int, tuple[float, ...]]:
    if u32(animation, animation_offset) != 0x41430102:
        raise ValueError("constant animation magic mismatch")
    group_table = u32(animation, animation_offset + 0x14)
    program = u32(animation, animation_offset + group_table)
    vectors: dict[int, tuple[float, float, float]] = {}
    quaternions: dict[int, tuple[float, float, float, float]] = {}
    cursor = program
    for _ in range(16):
        command = u16(animation, animation_offset + cursor)
        opcode = command & 0xFF
        words = command >> 8
        if opcode == 0:
            break
        if words != 3 or opcode not in (22, 24):
            raise ValueError(
                f"constant animation has unsupported command 0x{command:04X}"
            )
        count, _selector, payload_offset = struct.unpack_from(
            "<3H", animation, animation_offset + cursor + 2
        )
        map_offset = animation_offset + program + payload_offset
        map_end = map_offset + 2 * count
        destinations = struct.unpack_from(f"<{count}H", animation, map_offset)
        for index, destination in enumerate(destinations):
            payload = animation[map_end + 6 * index : map_end + 6 * (index + 1)]
            if opcode == 22:
                vectors[destination] = struct.unpack("<3e", payload)
            else:
                quaternions[destination] = audit.decode_packed_quaternion_48(payload)
        cursor += 2 * (1 + words)
    else:
        raise ValueError("unterminated constant animation program")

    joints = {
        destination // 0x30 for destination in vectors | quaternions
    }
    result: dict[int, tuple[float, ...]] = {}
    for joint in joints:
        scale = vectors.get(joint * 0x30, (1.0, 1.0, 1.0))
        rotation = quaternions.get(joint * 0x30 + 0x10, (0.0, 0.0, 0.0, 1.0))
        translation = vectors.get(joint * 0x30 + 0x20, (0.0, 0.0, 0.0))
        result[joint] = (*scale, 0.0, *rotation, *translation, 0.0)
    return result


def compose_chain(
    image: MappedImage,
    pose: bytes,
    constants: dict[int, tuple[float, ...]],
    chain: tuple[int, ...],
) -> tuple[tuple[float, float, float], tuple[float, float, float, float]]:
    current = ctypes.create_string_buffer(0x20)
    ctypes.c_float.from_address(ctypes.addressof(current) + 0x1C).value = 1.0
    compose = image.function3(TRANSFORM_COMPOSE)
    for joint in chain:
        values = constants.get(joint, joint_values(pose, joint))
        relative = ctypes.create_string_buffer(0x20)
        result = ctypes.create_string_buffer(0x20)
        struct.pack_into("<4f", relative, 0, *values[8:11], 0.0)
        struct.pack_into("<4f", relative, 0x10, *values[4:8])
        compose(
            ctypes.c_void_p(ctypes.addressof(current)),
            ctypes.c_void_p(ctypes.addressof(result)),
            ctypes.c_void_p(ctypes.addressof(relative)),
        )
        current = result
    position = struct.unpack_from("<3f", current, 0)
    rotation = struct.unpack_from("<4f", current, 0x10)
    return position, rotation


def parse_samples(value: str) -> tuple[float, ...]:
    samples = tuple(float(item) for item in value.split(",") if item.strip())
    if not samples:
        raise argparse.ArgumentTypeError("at least one sample is required")
    return samples


def parse_joint_chain(value: str) -> tuple[int, ...]:
    joints = tuple(int(item, 0) for item in value.split(",") if item.strip())
    if not joints:
        raise argparse.ArgumentTypeError("joint chain must not be empty")
    return joints


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=DEFAULT_GAME / "tll.exe")
    parser.add_argument(
        "--archive",
        type=Path,
        default=(
            DEFAULT_GAME
            / "Uncharted4_data"
            / "build"
            / "pc"
            / "thelostlegacy"
            / "actor77.psarc"
        ),
    )
    parser.add_argument("--entry", default=DEFAULT_ENTRY)
    parser.add_argument(
        "--animation-offset", type=lambda value: int(value, 0), required=True
    )
    parser.add_argument("--group-index", type=int, default=0)
    parser.add_argument("--first-joint", type=int, required=True)
    parser.add_argument("--last-joint", type=int, required=True)
    parser.add_argument(
        "--joints",
        type=parse_joint_chain,
        help="comma-separated sparse processing-row indices to validate and print",
    )
    parser.add_argument(
        "--emit-bits",
        action="store_true",
        help="also print the exact twelve uint32 words of each 48-byte row",
    )
    parser.add_argument("--samples", type=parse_samples, required=True)
    parser.add_argument("--expected-frame-stride", type=int)
    parser.add_argument(
        "--constant-animation-offset", type=lambda value: int(value, 0)
    )
    parser.add_argument("--left-chain", type=parse_joint_chain)
    parser.add_argument("--right-chain", type=parse_joint_chain)
    parser.add_argument(
        "--expected-opcodes",
        default="22,24,11,27,29,30,15,16",
        help="comma-separated exact opcode sequence",
    )
    args = parser.parse_args()
    if args.first_joint < 0 or args.last_joint < args.first_joint:
        parser.error("invalid joint range")
    selected_joints = (
        args.joints
        if args.joints is not None
        else tuple(range(args.first_joint, args.last_joint + 1))
    )
    if any(joint < 0 for joint in selected_joints):
        parser.error("joint indices must be non-negative")
    expected_opcodes = [int(item, 0) for item in args.expected_opcodes.split(",")]

    audit = load_audit_module()
    archive = audit.Psarc(args.archive, None)
    animation = archive.extract(args.entry)
    constants: dict[int, tuple[float, ...]] = {}
    if args.constant_animation_offset is not None:
        if args.left_chain is None or args.right_chain is None:
            parser.error("constant animation requires --left-chain and --right-chain")
        constants = decode_constant_pose(
            audit, animation, args.constant_animation_offset
        )
    image = MappedImage(args.exe)
    try:
        target_samples: dict[float, bytes] = {}
        for sample in args.samples:
            pose, seen, destinations, frame_count, frame_stride = decode_phase(
                image,
                animation,
                args.animation_offset,
                sample,
                args.expected_frame_stride,
                args.group_index,
            )
            if seen != expected_opcodes:
                raise ValueError(f"unexpected opcodes {seen}; expected {expected_opcodes}")
            for joint in selected_joints:
                validate_target_pose(pose, joint, joint)
            target_samples[sample] = pose
            if sample == args.samples[0]:
                print(
                    f"groupIndex={args.group_index} "
                    f"frameCount={frame_count} frameStride={frame_stride}"
                )
                for opcode in (22, 24):
                    if opcode not in destinations:
                        continue
                    mapped_joints = sorted(
                        {
                            destination // 0x30
                            for destination in destinations[opcode]
                            if destination != 0x10B0
                        }
                    )
                    print(f"opcode={opcode} mappedJoints={mapped_joints}")
            print(f"sample={sample:.3f}")
            for joint in selected_joints:
                values = joint_values(pose, joint)
                print(
                    f"  joint={joint} "
                    f"S=({values[0]:.9g},{values[1]:.9g},{values[2]:.9g}) "
                    f"Q=({values[4]:.9g},{values[5]:.9g},"
                    f"{values[6]:.9g},{values[7]:.9g}) "
                    f"T=({values[8]:.9g},{values[9]:.9g},{values[10]:.9g})"
                )
                if args.emit_bits:
                    row_words = struct.unpack_from("<12I", pose, joint * 0x30)
                    print(
                        "    bits="
                        + ",".join(f"0x{word:08X}" for word in row_words)
                    )
            if constants:
                endpoint_positions: dict[str, tuple[float, float, float]] = {}
                for side, chain in (
                    ("left", args.left_chain),
                    ("right", args.right_chain),
                ):
                    position, rotation = compose_chain(
                        image, pose, constants, chain
                    )
                    endpoint_positions[side] = position
                    print(
                        f"  {side}EndpointRel "
                        f"P=({position[0]:.9g},{position[1]:.9g},{position[2]:.9g}) "
                        f"Q=({rotation[0]:.9g},{rotation[1]:.9g},"
                        f"{rotation[2]:.9g},{rotation[3]:.9g})"
                    )
                print(
                    "  endpointGap="
                    f"{math.dist(endpoint_positions['left'], endpoint_positions['right']):.9g}"
                )

        if len(args.samples) >= 2:
            first = target_samples[args.samples[0]]
            last = target_samples[args.samples[-1]]
            exact = all(
                first[joint * 0x30 : (joint + 1) * 0x30]
                == last[joint * 0x30 : (joint + 1) * 0x30]
                for joint in selected_joints
            )
            print(
                "loopClosure="
                + ("exact" if exact else "different")
            )
    finally:
        image.close()


if __name__ == "__main__":
    main()
