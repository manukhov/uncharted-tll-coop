#!/usr/bin/env python3
"""Pinned, pure STATIC audit of pistol-base root inputs and joint-program writes.

This does not evaluate retail code or claim a live root identity. It establishes
that all seven shipped clips author an identity root, their dynamic scatter maps
do not replace it, and the sole skeleton program propagates that unchanged root.
Runtime callbacks, other assets, stale generations and alternate render palettes
remain outside this asset contract.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct

import p99_group_program_audit as program
import p99_weapon_mesh_audit as mesh
from psarc_asset_audit import decode_packed_quaternion_48, find_all, string_id


MODEL = "pistol-base-authored-root-preservation-v1"


@dataclass(frozen=True)
class Clip:
    name: str
    descriptor: int
    raw: int
    size: int
    opcodes: tuple[int, ...]
    dynamic_maps: tuple[tuple[int, tuple[int, ...]], ...] = ()


CLIPS = (
    Clip("p-reload", 0x4A70, 0x2CD0, 0x480, (22, 24, 11, 27, 29, 15, 16),
         ((15, (0x50, 0x80, 0x330, 0x330)),
          (16, (0x70, 0x330, 0x330, 0x330)))),
    Clip("revolver-reload", 0x4AC8, 0x3150, 0x320, (22, 24, 11, 27, 29, 16),
         ((16, (0xD0, 0x330, 0x330, 0x330)),)),
    Clip("p-fire", 0x49C0, 0x3470, 0x300, (22, 24, 11, 27, 28, 15, 16),
         ((15, (0x50, 0x1A0, 0x330, 0x330)),
          (16, (0x160, 0x190, 0x330, 0x330)))),
    Clip("toy-gun-idle", 0x4B20, 0x3A40, 0x240, (22, 24)),
    Clip("revolver-idle", 0x4B78, 0x3C80, 0x240, (22, 24)),
    Clip("p-noammo", 0x4A18, 0x3EC0, 0x240, (22, 24)),
    Clip("idle", 0x4968, 0x4100, 0x240, (22, 24)),
)
PROGRAM_OPCODES = (52, 50, 47, 54, 47, 41, 40, 49, 47, 40, 44, 40)


def require(value: bool, message: str) -> None:
    if not value:
        raise ValueError(message)


def _clip_span(clip: Clip, offset: int, size: int) -> None:
    require(size > 0 and clip.raw <= offset and offset + size <= clip.raw + clip.size,
            "clip payload exceeds owned ANIM object")


def _audit_clip(pak: mesh.PagedPak, clip: Clip) -> dict:
    d, raw = pak.data, clip.raw
    require(mesh._metadata(pak, clip.descriptor, 0) == clip.name and
            mesh._metadata(pak, clip.descriptor, 8) == "ANIM" and
            pak.resolve_absolute(clip.descriptor + 0x28) == raw,
            "clip typed descriptor ownership")
    require(d[raw:raw+4] == b"\x02\x01CA" and
            struct.unpack_from("<IIH", d, raw+4) ==
            (mesh.SKELETON_ID, clip.size, 1), "clip skeleton/dimensions")
    require(program.u32(d, raw+0x14) == 0x30 and
            struct.unpack_from("<QQ", d, raw+0x40) ==
            (0xFFFF800000000000, 0), "clip parameter mask")
    relative = program.u32(d, raw+0x30)
    require(relative == 0x50, "clip program offset")
    base = raw + relative
    ops = program.operators(d, raw, relative)
    require(tuple(op.opcode for op in ops) == clip.opcodes, "clip opcode coverage")
    roots = {}
    dynamic = []
    for op in ops:
        _clip_span(clip, raw + op.relative, 2 + 2*len(op.payload))
        p = op.payload
        if op.opcode in (22, 24):
            require(len(p) == 3 and p[1] == 3, "constant encoded base")
            count, _, offset = p
            table = base + offset
            # Padded vector/quaternion counts can refer to sentinel destinations
            # whose unused payload tail overlaps subsequent blocks. This audit
            # admits only the full map plus the three root payloads it reads;
            # it is deliberately not a decoder of every padded output.
            _clip_span(clip, table, count*2)
            destinations = struct.unpack_from(f"<{count}H", d, table)
            for index, dest in enumerate(destinations):
                if dest < 0x30:
                    require(dest not in roots, "duplicate root component write")
                    _clip_span(clip, table+2*count+6*index, 6)
                    value = d[table+2*count+6*index:table+2*count+6*index+6]
                    roots[dest] = (struct.unpack("<3e", value) if op.opcode == 22
                                   else decode_packed_quaternion_48(value))
        elif op.opcode in (15, 16):
            # Native142193970/39F0 pass this uint16 destination map into workers
            # 142184220/43A0. Both scatter four float4s per iteration, including
            # tail padding, relative to the decode context's JointParams output.
            require(len(p) == 4 and p[1] == 3 and p[2] & 3 == 0,
                    "dynamic scatter encoded bases")
            count = ((p[0] & 0x7FF) + 3) // 4 * 4
            _clip_span(clip, base+p[2], 2*count)
            destinations = struct.unpack_from(f"<{count}H", d, base+p[2])
            require(all(dest >= 0x30 for dest in destinations),
                    "dynamic scatter overwrites root")
            dynamic.append((op.opcode, destinations))
        else:
            # Reviewed scratch-only unpack stages. Their wrappers derive output
            # from context+0x230/+0x240; only15/16 subsequently scatter into JP.
            require(op.opcode in (11, 27, 28, 29) and p[1] == 3 and p[2] & 3 == 0,
                    "unsupported dynamic scratch stage")
    require(roots == {0: (1., 1., 1.), 0x10: (0., 0., 0., 1.),
                      0x20: (0., 0., 0.)}, "clip root is not identity SQT")
    require(tuple(dynamic) == clip.dynamic_maps, "dynamic scatter map changed")
    return {"name": clip.name, "id": f"{string_id(clip.name.encode()):08X}",
            "descriptor": f"{clip.descriptor:X}", "raw": f"{raw:X}",
            "opcodes": list(clip.opcodes), "root_sqt_identity": True,
            "dynamic_scatter_maps": [
                {"opcode": opcode, "destinations": [f"{dest:X}" for dest in dests]}
                for opcode, dests in dynamic]}


def _audit_program(data: bytes) -> dict:
    raw = 0x800
    require(program.u32(data, raw+0x2C) == 0x40 and
            program.u32(data, raw+0x34) == 0x70, "skeleton program tables")
    group = raw + 0x40
    require(data[group+0x0F] == 1 and program.u16(data, group+6) == 0 and
            struct.unpack_from("<HH", data, group+0x10) == (23, 17),
            "skeleton group/program dimensions")
    relative = program.u32(data, raw+0x74)
    require(relative == 0x3B0, "skeleton program offset")
    base = raw+relative
    ops = program.operators(data, raw, relative)
    require(tuple(op.opcode for op in ops) == PROGRAM_OPCODES,
            "skeleton opcode coverage")
    root_propagations = []
    for op in ops:
        for encoded, width in program.writes(data, base, op):
            bank = encoded & 3
            offset = encoded & (0xFFFFFFF0 if width >= 16 else 0xFFFFFFFC)
            if bank == 0:
                require(offset >= 48, "skeleton operator overwrites root JointParams")
            if bank == 1:
                require(offset >= 64, "skeleton operator overwrites identity parent")
                if offset < 128 and offset+width > 64:
                    require(op.opcode == 40, "skeleton operator overwrites root matrix")
        if op.opcode == 40:
            records = program.propagation_records(data, base, op)
            roots = [(local, parent) for local, parent in records if local == 0]
            if roots:
                require(op.ordinal == 6 and records == ((0, 9),)*4,
                        "root propagation parent or padding changed")
                root_propagations.append(op.relative)
    require(root_propagations == [0x3E6], "root propagation coverage")
    return {"raw_skeleton": "800", "program": "BB0", "opcode_count": len(ops),
            "opcodes": list(PROGRAM_OPCODES), "root_jointparams_writes": 0,
            "root_propagation_command": "BE6", "root_parent_encoding": "0009",
            "root_matrix_offset": "40", "root_padding_records": 4,
            "later_root_matrix_writes": 0,
            "condition": "Reserved matrix row0 must carry the native identity basis."}


def audit(data: bytes) -> dict:
    require(len(data) == 86976 and hashlib.sha256(data).hexdigest() == mesh.PISTOL_SHA256,
            "pistol-base asset size/SHA256")
    pak = mesh.PagedPak(mesh.BytesSource(data))
    skeleton = mesh._validate_skeleton(pak)
    require(tuple(find_all(data, b"\x02\x01CA")) == tuple(c.raw for c in CLIPS),
            "complete pinned animation inventory")
    clips = [_audit_clip(pak, clip) for clip in CLIPS]
    return {"model": MODEL, "status": "STATIC", "asset_sha256": mesh.PISTOL_SHA256,
            "live_root_identity_proven": False, "retail_code_executed": False,
            "skeleton": skeleton, "clips": clips,
            "joint_program": _audit_program(data),
            "limits": ("All seven authored inputs and the pinned skeleton program preserve root. "
                       "No live callback, selected-animation provenance, same-generation "
                       "publication, render override or world/rightProp equality is admitted.")}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=mesh.DEFAULT_ARCHIVE)
    args = parser.parse_args()
    _, pistol = mesh.load_assets(args.archive)
    print(json.dumps(audit(pistol), indent=2))


if __name__ == "__main__":
    main()
