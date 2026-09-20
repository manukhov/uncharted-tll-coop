"""Decode the full authored E51542CA Nadine reference profile, without native code.

This is a scalar CONSTANT-STREAM audit, not AnimateObject or builder operands.
Opcode22 half vectors, opcode24 smallest-three quaternions and opcode25 float
scalars occupy separate JointParams/scalar banks. Padding destinations are not
logical joints. Native reciprocal-square-root/unused SIMD lanes are not emulated exactly.
The sampled global matrices, procedural joint programs and live residency remain
outside this decoder. Never substitute its rows for completed runtime records.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source
import p99_native_source_audit as authored

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSET = ROOT / "reports/actor_packages/nadine-dlc-base.pak"
PROFILE_ID = 0xE51542CA
DECODER_MODEL = source.native.load_audit_module().PACKED_QUATERNION_MODEL
RAW_OFFSET, RAW_SIZE = 0x2DA0, 11088
RAW_SHA = "322dc889cfbef6d041081956a8bebc55d65f2257290bd2836aadbe73677ae74c"
COUNTS = (66, 38, 96, 83, 88, 52, 3)
DESCRIPTORS = (
    (288, 457310208, 11072, 0), (2112, 2490368, 11072, 0),
    (3088, 6291456, 11072, 0), (5424, 5439488, 11072, 0),
    (7488, 5767168, 11072, 0), (9632, 3407872, 11072, 0),
    (10928, 196608, 11072, 0),
)


def extent(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError("constant stream extent outside clip")
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, extent(data, offset, struct.calcsize(fmt)))


def padded8(count):
    return (count + 7) & ~7


def decode_program(data: bytes, program: int, joints: int, scalars: int):
    """Decode exactly one complete constant profile group, not a sparse clip.

    Op25 state+10h is a separate scalar base; op22/24 use state+8. The scalar
    sentinel is the next four-float-aligned position after the logical scalars.
    Meaningful destinations must occur once. SIMD padding may contain arbitrary
    bits (and can overlap the following payload) but may only target a sentinel.
    """
    if not 0 < joints <= 256 or not 0 <= scalars <= 256:
        raise ValueError("unsupported logical group counts")
    expected_ops = [22, 24] + ([25] if scalars else [])
    expected_destinations = {
        22: {48 * index + field for index in range(joints) for field in (0, 32)},
        24: {48 * index + 16 for index in range(joints)},
        25: {4 * index for index in range(scalars)},
    }
    sentinel = {22: 48 * joints, 24: 48 * joints, 25: 4 * ((scalars + 3) & ~3)}
    rows = [[0.0] * 12 for _ in range(joints)]
    scalar_values = [0.0] * scalars
    metadata = []
    decode_q = source.native.load_audit_module().decode_packed_quaternion_48
    for ordinal, opcode in enumerate(expected_ops):
        cursor = program + ordinal * 8
        header, count, selector, payload = unpack("<4H", data, cursor)
        if header != (0x300 | opcode) or selector != 3 or payload & 3:
            raise ValueError("unexpected constant opcode/selector/descriptor")
        logical = len(expected_destinations[opcode])
        if count != padded8(logical):
            raise ValueError("constant block count is not exact SIMD padding")
        map_start = program + payload
        if map_start < program + len(expected_ops) * 8 + 2:
            raise ValueError("constant data overlaps program headers")
        destinations = unpack(f"<{count}H", data, map_start)
        counts = Counter(destinations)
        wanted = expected_destinations[opcode]
        if (set(counts) - {sentinel[opcode]} != wanted or
                any(counts[destination] != 1 for destination in wanted) or
                counts[sentinel[opcode]] != count - logical):
            raise ValueError("missing/duplicate/out-of-bank constant destination")
        stride = 4 if opcode == 25 else 6
        values_start = map_start + 2 * count
        extent(data, values_start, count * stride)
        half_subnormals = 0
        for index, destination in enumerate(destinations):
            if destination == sentinel[opcode]:
                continue
            packed = extent(data, values_start + stride * index, stride)
            if opcode == 25:
                value, = struct.unpack("<f", packed)
                if not math.isfinite(value):
                    raise ValueError("nonfinite meaningful scalar")
                scalar_values[destination // 4] = value
                continue
            row, byte_field = divmod(destination, 48)
            if opcode == 22:
                half_subnormals += sum(0 < (bits & 0x7FFF) < 0x0400
                                       for bits in struct.unpack("<3H", packed))
            values = struct.unpack("<3e", packed) if opcode == 22 else decode_q(packed)
            if not all(math.isfinite(value) for value in values):
                raise ValueError("nonfinite meaningful JointParams field")
            rows[row][byte_field // 4:byte_field // 4 + len(values)] = values
        metadata.append({"opcode": opcode, "physical_count": count,
                         "logical_count": logical, "padding_count": count - logical,
                         "sentinel_byte_offset": sentinel[opcode],
                         "meaningful_half_subnormals": half_subnormals})
    if unpack("<H", data, program + len(expected_ops) * 8)[0] != 0:
        raise ValueError("unexpected constant-program tail")
    for row in rows:
        if abs(sum(value * value for value in row[4:8]) - 1) > 1e-5:
            raise ValueError("decoded quaternion outside unit envelope")
    return rows, scalar_values, metadata


def resolve_profile(data):
    """Exact registration graph, not a four-byte-ID occurrence as provenance."""
    source.load_skeleton(data, source.NADINE_SKELETON)  # Complete asset/hash pin.
    hits = source.find_all(data, struct.pack("<I", PROFILE_ID))
    if hits != (0x93E98,):
        raise ValueError("profile registration occurrence changed")
    pak = source.PagedPak(data)
    slot = pak.resolve_pointer(hits[0] + 8)
    descriptor = pak.resolve_pointer(slot[2])
    raw = pak.resolve_pointer(descriptor[2] + 0x28)
    if (slot != (0, 0x90C18, 0x93278) or descriptor != (0, 0x9FB40, 0xA21A0)
            or raw != (0, 0x740, RAW_OFFSET)):
        raise ValueError("profile registration pointer graph changed")
    if unpack("<I", data, descriptor[2] + 0x40)[0] != source.NADINE_DATABASE_ID:
        raise ValueError("profile target database mismatch")
    clip = extent(data, RAW_OFFSET, RAW_SIZE)
    if source.sha256(clip) != RAW_SHA:
        raise ValueError("profile constant ANIM changed")
    if (unpack("<3I", clip, 0) != (0x41430102, source.NADINE_SKELETON_ID, RAW_SIZE)
            or unpack("<H", clip, 0xC)[0] != 7 or unpack("<H", clip, 0x20)[0] != 0
            or unpack("<H", clip, 0x22)[0] != 1 or unpack("<I", clip, 0x14)[0] != 0x30):
        raise ValueError("profile header changed")
    return clip


def audit(data: bytes):
    clip = resolve_profile(data)
    skeleton = source.load_skeleton(data, source.NADINE_SKELETON)
    inputs = authored.skeleton_inputs(data, skeleton)
    rows, groups, scalar_bank = [], [], []
    for group, count in enumerate(COUNTS):
        descriptor = unpack("<4I", clip, 0x30 + group * 16)
        layout = skeleton.groups[group]
        processing = skeleton.processing[layout.processing_index]
        scalar_count = 27 if group == 0 else 0
        if (descriptor != DESCRIPTORS[group] or layout.parameter_count != count
                or processing.joint_count != count or processing.scalar_count != scalar_count
                or processing.first_parameter != len(rows)):
            raise ValueError("profile/skeleton processing layout disagrees")
        decoded, scalars, ops = decode_program(clip, descriptor[0], count, scalar_count)
        first = len(rows)
        rows.extend(decoded)
        scalar_bank.extend(scalars)
        groups.append({"group": group, "first_global": layout.first,
                       "first_parameter": first, "parameter_count": count,
                       "workspace_count": layout.workspace_count, "scalar_count": scalar_count,
                       "operators": ops})
    if len(rows) != 426 or len(scalar_bank) != 27:
        raise ValueError("incomplete profile coverage")
    packed = b"".join(struct.pack("<12f", *row) for row in rows)
    scalars = struct.pack("<27f", *scalar_bank)
    differences = []
    for group in groups:
        for local in range(group["parameter_count"]):
            parameter = group["first_parameter"] + local
            bind = struct.unpack_from("<12f", inputs["references"], parameter * 48)
            pose = rows[parameter]
            qerror = min(max(abs(a - b) for a, b in zip(pose[4:8], bind[4:8])),
                         max(abs(a + b) for a, b in zip(pose[4:8], bind[4:8])))
            st_error = max(abs(pose[index] - bind[index]) for index in (0, 1, 2, 8, 9, 10))
            differences.append({"global": group["first_global"] + local,
                                "parameter": parameter, "rotation_component_delta": qerror,
                                "scale_translation_component_delta": st_error})
    return {"classification": "STATIC_SCALAR_CONSTANT_STREAM_DECODE",
            "quaternion_decoder_model": DECODER_MODEL,
            "game_or_native_execution": False, "runtime_builder_operands_ready": False,
            "profile": f"{PROFILE_ID:08X}", "database": f"{source.NADINE_DATABASE_ID:08X}",
            "asset_sha256": source.sha256(data), "raw_sha256": RAW_SHA,
            "decoded_parameter_rows": len(rows), "decoded_scalars": len(scalar_bank),
            "groups": groups, "scalar_model_rows_sha256": source.sha256(packed),
            "scalar_bank_sha256": source.sha256(scalars), "scalars": scalar_bank,
            "reference_differences": differences,
            "scalar_model_rows_f32_hex": packed.hex(),
            "limits": ["Quaternion decoder models native component order/float constants, but sqrt is not instruction-exact rsqrt/Newton",
                       "Python preserves half subnormals; native op22 manual conversion is MXCSR/DAZ-dependent",
                       "JointParams padding lanes are zero placeholders, not native SIMD bytes",
                       "All constant rows decoded; joint programs/global matrices not evaluated",
                       "No live residency, record provenance, skin-contact or visual grip claim"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset", type=Path, default=DEFAULT_ASSET)
    parser.add_argument("--output", type=Path, help="optional local complete scalar-model artifact")
    args = parser.parse_args()
    result = audit(args.asset.read_bytes())
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    summary = {key: value for key, value in result.items()
               if key not in ("scalar_model_rows_f32_hex", "reference_differences")}
    print(json.dumps(summary, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
