#!/usr/bin/env python3
"""Derive Nadine's authored left support-hand constraints from shipped assets.

This is a disk-only scalar audit.  It resolves the two one-sample Nadine NPC
weapon up/down clips, decodes their constant programs, applies the already
audited Elena->Nadine retarget row149 to ``l_palm`` and preserves the authored
mode3 ``l_hand_prop_attachment`` row.  It then derives the exact terminal IK
constraint used by the native WeaponIK formula::

    C = Relative(worldProp, worldWrist)
      = inverse(worldProp) * worldWrist
      = inverse(localPalm * localProp)

The cancellation is a rigid-transform identity: it does not import the donor
wrist basis or proximal arm ancestry.  This report proves the asset constants
and the algebra only.  It does not establish live clip selection, callback
weight, a mutation boundary, runtime admission, or a visual fix.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source
import p99_grip_composition_audit as grip
import p99_locator_basis_audit as locator
import p99_native_source_audit as authored
import p99_npc_grip_candidate as npc


ROOT = Path(__file__).resolve().parents[1]
BASE = source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
DEFAULT_ACTOR_ARCHIVE = BASE / "actor77.psarc"
DEFAULT_DC_ARCHIVE = BASE / "dc1.psarc"

SCHEMA = "tll-p99-npc-support-constraint-v1"
CLASSIFICATION = "STATIC_DISK_ONLY_NADINE_SUPPORT_CONSTRAINT_CANDIDATE"
DECODER_MODEL = npc.DECODER_MODEL

ANIMATION_ENTRY = npc.ANIMATION_ENTRY
ANIMATION_SIZE = npc.ANIMATION_SIZE
ANIMATION_SHA256 = npc.ANIMATION_SHA256
OVERLAY_ENTRY = "anim-overlays-nadine.bin"
OVERLAY_SIZE = 88_185
OVERLAY_SHA256 = "f2e4ad2493aebb1719abeafb589f06f3ea3394a09dcfd3ba491c0317833efc8b"

SUPPORT_UP_ID = 0x036F20E5
SUPPORT_DOWN_ID = 0x5E67B1FA

ANIMATIONS = {
    "weapon_up": source.AnimationSpec(
        SUPPORT_UP_ID, npc.ELENA_SKELETON_ID,
        (0, 0x440E8), (0, 0x3BD88), (6, 0x4C200), (5, 0xE92B0),
        0x5FFD00, 0x6A0, 0, 7,
    ),
    "weapon_down": source.AnimationSpec(
        SUPPORT_DOWN_ID, npc.ELENA_SKELETON_ID,
        (0, 0x3EE78), (0, 0x3BDD8), (6, 0x4BA70), (5, 0xE8570),
        0x5FEFC0, 0x6A0, 0, 7,
    ),
}

RAW_SHA256 = {
    "weapon_up": "f5fba241400a56c1dd8768de73dae40b3190bdb8878ff6458224ed81360fe717",
    "weapon_down": "42b20919ead031d51125aee6aa174bb9ac8cfb1f1b1a5a819ffb1172b4eea0d6",
}

GROUP_DESCRIPTORS = (
    (0x120, 0x1B3C0000, 0x690, 0),
    (0x270, 0x00260000, 0x690, 0),
    (0x640, 0x00520000, 0x690, 0),
    (0x650, 0x00530000, 0x690, 0),
    (0x660, 0x00580000, 0x690, 0),
    (0x670, 0x00340000, 0x690, 0),
    (0x680, 0x00010000, 0x690, 0),
)

SHARED_OPCODE_MAPS = {
    0: (
        source.OpcodeMapSpec(
            24, 0,
            "00d1ed27230b7b852d76abd7ab4fb6c64e9f15adebc319b357f73d941947b114",
        ),
        source.OpcodeMapSpec(
            16, 4,
            "be141eeb3c2125c480fc609e0e2803df06abe53b8b1448e1328911c57e27dde7",
        ),
    ),
    1: (
        source.OpcodeMapSpec(
            80, 4,
            "77be0020145b13d23b0e34d2fed443b415c4e1ac81ce7216deca56e951034b0d",
        ),
        source.OpcodeMapSpec(
            40, 2,
            "040b143d66b35ad2d5f9f2f6ec494a78255147d75c324ff2f40bc238f90b4c66",
        ),
    ),
}

PROGRAM_PINS = {
    "weapon_up": (
        (0x160, 0x1742A5C7CCFA8ED4,
         "a77b85a1a95d8b8180417af95a102981a7589b3a15c9c988d7b0308fba96d77b"),
        (0x3D0, 0x5E02C01AC0D8ED0F,
         "a1b98c153caa4f1ee4f1caae33a5fd475f6c16a1f352fb977cead1b605d5fa35"),
    ),
    "weapon_down": (
        (0x160, 0x8A0EF3042A57AF2A,
         "7c4ef8c4f10f082e666892214da92f1cc47dff5448a60e1f9ce10ffd8cf7c4be"),
        (0x3D0, 0xFD5B0EA9396DC78F,
         "0912d04b81cab07ddc34941d42743ac7a2325263114a10d99f9f260e1c201e60"),
    ),
}

SOURCE_ROWS_SHA256 = {
    "weapon_up": "e79559564e31290b937317d91037b78c1d5ca0ab5e7160876171ab4eac50e6be",
    "weapon_down": "729adf6c1254540f07071dbfa266625a67a6b242d884d2cb9ca48fb8e126b0a4",
}
TARGET_PAIR_SHA256 = {
    "weapon_up": "31744c4a5f13fc7f33257f7020c9c3cdd1814eab5efd2c5f2c147103b152b1bc",
    "weapon_down": "bb960352195c153c36a061bda7543ef4a933c1dac7439150c073dcc91ea8f3f2",
}
SOURCE_CONSTRAINT_SHA256 = {
    "weapon_up": "041fbbc408eb614586eaae012906762c32afe56e25a15aea69060cce1627e0dd",
    "weapon_down": "60183ee8a7301de1100b8452e73d443e1bb5f039a99b27a27f7f709952a2411f",
}
TARGET_CONSTRAINT_SHA256 = {
    "weapon_up": "04954d7c9ea458690c4f18d5641456978c0189ead3eab6b85d90f6b3f3922667",
    "weapon_down": "19e6944679d6e6f89d91020a083393806655a9d6291cf38b90a5223a64942a21",
}

EXPECTED_MODES = {
    19: 2, 20: 2, 21: 2, 22: 2,
    23: 1, 24: 1, 25: 1, 26: 1, 27: 1, 28: 1,
    29: 3, 30: 3,
}
SEMANTICS = {
    "left_wrist": (25, 0x57F325C7, "l_wrist"),
    "left_palm": (27, 0x81DC7B7E, "l_palm"),
    "left_prop": (29, 0xA7479EC2, "l_hand_prop_attachment"),
}

EXPECTED_TARGET_ROWS = {
    "weapon_up": {
        "left_palm": (
            1.0, 1.0, 1.0, 0.0,
            0.025075018405914307, -0.056494295597076416,
            0.15826177597045898, 0.9854607582092285,
            -1.3278329824562007e-11, -5.8906256608648455e-12,
            0.050810057669878006, 0.0,
        ),
        "left_prop": (
            1.0, 1.0, 1.0, 0.0,
            -0.21507975459098816, 0.20677173137664795,
            0.8519173860549927, 0.43037527799606323,
            -0.026763916015625, -0.051300048828125,
            0.011260986328125, 0.0,
        ),
    },
    "weapon_down": {
        "left_palm": (
            1.0, 1.0, 1.0, 0.0,
            0.025075018405914307, -0.056494295597076416,
            0.15826177597045898, 0.9854607582092285,
            -1.3278329824562007e-11, -5.8906256608648455e-12,
            0.050810057669878006, 0.0,
        ),
        "left_prop": (
            1.0, 1.0, 1.0, 0.0,
            -0.40881767868995667, 0.04777634143829346,
            0.7349188923835754, 0.5389617085456848,
            -0.0171966552734375, -0.0489501953125,
            0.0048370361328125, 0.0,
        ),
    },
}

TYPE5_DESCRIPTOR_OFFSET = 0x14860
TYPE5_DESCRIPTOR_SHA256 = "75e5bc0800146c306f1f18151aed5fb647a74dba99ca8efce4eeff0fed0a3ac0"
TYPE5_CODE_OFFSET = 0x14E50
TYPE5_POOL_OFFSET = 0x14E90
TYPE5_SCRIPT_ID = 0xCDFC7F1B
TYPE5_CODE_SHA256 = "7cd394de8b9e7d3ad40f9fe3881dbff6b7628ab2a68ef799c0119864d14f429c"
TYPE5_POOL_SHA256 = "271e7637821453dd2f95df8c3d00f5afeefd438a09fe0d9a9886abc4cb8838e6"


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _f32_rows_sha256(rows) -> str:
    return _sha256(b"".join(struct.pack("<12f", *row) for row in rows))


def _constraint_sha256(transform) -> str:
    # Inspection contract: translation P, then quaternion Q, all rounded to f32.
    return _sha256(struct.pack("<7f", *transform[8:11], *transform[4:8]))


def _unit_rigid(transform, label: str):
    if len(transform) != 12 or not all(math.isfinite(value) for value in transform):
        raise ValueError(f"{label}: invalid transform")
    if max(abs(transform[index] - 1.0) for index in range(3)) > 1e-5:
        raise ValueError(f"{label}: non-unit scale is outside this rigid proof")
    if abs(transform[3]) > 1e-12 or abs(transform[11]) > 1e-12:
        raise ValueError(f"{label}: nonzero padding")
    if abs(sum(value * value for value in transform[4:8]) - 1.0) > 1e-5:
        raise ValueError(f"{label}: non-unit quaternion")
    return tuple(transform)


def compose(parent, local):
    """Compose two unit-scale row transforms as ``parent * local``."""

    parent = _unit_rigid(parent, "parent")
    local = _unit_rigid(local, "local")
    moved = grip.rotate(parent[4:8], local[8:11])
    return grip.row(
        rotation=grip.qnorm(grip.qmul(parent[4:8], local[4:8])),
        translation=tuple(parent[8 + axis] + moved[axis] for axis in range(3)),
    )


def inverse(transform):
    """Invert a unit-scale rigid row transform."""

    transform = _unit_rigid(transform, "inverse operand")
    rotation = grip.qconj(grip.qnorm(transform[4:8]))
    translation = grip.rotate(rotation, tuple(-value for value in transform[8:11]))
    return grip.row(rotation=rotation, translation=translation)


def relative(first_world, second_world):
    """Match native ``Relative(first, second) = inverse(first) * second``."""

    return compose(inverse(first_world), second_world)


def support_constraint(palm_local, prop_local):
    """Return ``inverse(localPalm * localProp)`` in wrist space."""

    return inverse(compose(palm_local, prop_local))


def world_support_constraint(wrist_world, palm_local, prop_local):
    """Evaluate the unreduced world formula used by WeaponIK."""

    prop_world = compose(compose(wrist_world, palm_local), prop_local)
    return relative(prop_world, wrist_world)


def transform_error(left, right) -> float:
    return max(
        grip.qerror(left[4:8], right[4:8]),
        *(abs(left[index] - right[index]) for index in (8, 9, 10)),
    )


def _angular_degrees(left, right) -> float:
    return grip.angular_degrees(left[4:8], right[4:8])


def _origin_distance(left, right) -> float:
    return math.sqrt(sum(
        (left[8 + axis] - right[8 + axis]) ** 2 for axis in range(3)
    ))


def _program_specs(label: str):
    specs = []
    for group, descriptor in enumerate(GROUP_DESCRIPTORS[:2]):
        program_bytes, fnv, _program_sha = PROGRAM_PINS[label][group]
        opcode22, opcode24 = SHARED_OPCODE_MAPS[group]
        specs.append(source.GroupProgramSpec(
            ANIMATIONS[label].clip_id, group, descriptor, program_bytes, fnv,
            opcode22, opcode24,
        ))
    return tuple(specs)


def _validate_descriptors(data: bytes, animation: source.AnimationResolution):
    clip = animation.raw_offset
    table_relative = source.native.u32(data, clip + 0x14)
    descriptors = tuple(
        struct.unpack_from("<4I", data, clip + table_relative + 0x10 * group)
        for group in range(animation.spec.group_count)
    )
    if descriptors != GROUP_DESCRIPTORS:
        raise ValueError("support clip group descriptor table changed")
    return descriptors


def _decode_clip(label: str, data: bytes, animation_pak: source.PagedPak,
                 source_skeleton: source.SkeletonAsset, parser):
    spec = ANIMATIONS[label]
    resolution = source.resolve_animation(animation_pak, spec)
    raw = data[resolution.raw_offset:resolution.raw_offset + spec.object_size]
    if _sha256(raw) != RAW_SHA256[label]:
        raise ValueError(f"{label}: raw ANIM hash changed")
    descriptors = _validate_descriptors(data, resolution)

    decoded = {}
    programs = []
    for group, program_spec in enumerate(_program_specs(label)):
        rows = npc._decode_group(data, resolution, source_skeleton, program_spec, parser)
        if set(decoded).intersection(rows):
            raise ValueError(f"{label}: duplicate decoded source global")
        decoded.update(rows)
        start = resolution.raw_offset + program_spec.descriptor_words[0]
        program = data[start:start + program_spec.program_bytes]
        program_sha = _sha256(program)
        if program_sha != PROGRAM_PINS[label][group][2]:
            raise ValueError(f"{label}: group{group} program SHA-256 changed")
        programs.append({
            "group": group,
            "descriptor_words": [f"{value:08X}" for value in descriptors[group]],
            "program_bytes": len(program),
            "program_fnv1a64": f"{program_spec.program_fnv1a64:016X}",
            "program_sha256": program_sha,
            "opcode22_count": program_spec.opcode22.count,
            "opcode22_sentinel_count": program_spec.opcode22.sentinel_count,
            "opcode22_map_sha256": program_spec.opcode22.sha256,
            "opcode24_count": program_spec.opcode24.count,
            "opcode24_sentinel_count": program_spec.opcode24.sentinel_count,
            "opcode24_map_sha256": program_spec.opcode24.sha256,
        })
    if set(decoded) != set(range(19, 31)) | set(range(67, 105)):
        raise ValueError(f"{label}: decoded source selection changed")
    if _f32_rows_sha256(decoded[joint] for joint in range(19, 31)) != SOURCE_ROWS_SHA256[label]:
        raise ValueError(f"{label}: source rows 19..30 changed")
    return resolution, decoded, programs


def _target_pair(label: str, decoded: dict[int, tuple[float, ...]],
                 source_skeleton, target_skeleton, source_inverse, target_inverse,
                 source_parents, target_parents, records, target_inputs, parser,
                 source_data: bytes, target_data: bytes):
    source_pak = source.PagedPak(source_data)
    target_pak = source.PagedPak(target_data)
    authored_rows = []
    target_rows = {}
    for source_joint in range(19, 31):
        semantic = source_skeleton.joint_ids[source_joint]
        record = source.one_record(records, semantic, "row149 support")
        if (record.source_global != source_joint or
                record.target_global != source_joint or
                record.target_id != semantic or
                record.mode != EXPECTED_MODES[source_joint]):
            raise ValueError(f"{label}: row149 arm record {source_joint} changed")
        source_name = npc._joint_name(
            source_data, source_pak, source_skeleton, source_joint, parser)
        target_name = npc._joint_name(
            target_data, target_pak, target_skeleton, record.target_global, parser)
        authored_rows.append({
            "semantic": f"{semantic:08X}",
            "source_global": source_joint,
            "source_name": source_name,
            "target_global": record.target_global,
            "target_name": target_name,
            "retarget_mode": record.mode,
            "retarget_map_index": record.map_index,
            "source_local_row": list(decoded[source_joint]),
        })
        if source_joint not in (27, 29):
            continue
        if record.mode == 3:
            local = tuple(decoded[source_joint])
        else:
            source_parent = source_parents[source_joint]
            target_parent = target_parents[record.target_global]
            joint_locator = locator.build_locator_quaternions(
                source_inverse[source_joint], target_inverse[record.target_global])
            parent_locator = locator.build_locator_quaternions(
                source_inverse[source_parent], target_inverse[target_parent])
            reference = npc._target_reference(
                target_inputs, target_skeleton, record.target_global)
            local = npc._retarget_local(
                decoded[source_joint], reference, record.mode,
                joint_locator, parent_locator)
        key = "left_palm" if source_joint == 27 else "left_prop"
        target_rows[key] = tuple(local)

    expected = EXPECTED_TARGET_ROWS[label]
    if target_rows != expected:
        raise ValueError(f"{label}: exact retargeted palm/prop rows changed")
    if _f32_rows_sha256((target_rows["left_palm"], target_rows["left_prop"])) != TARGET_PAIR_SHA256[label]:
        raise ValueError(f"{label}: target palm/prop pair hash changed")
    return authored_rows, target_rows


def _constraint_record(label: str, decoded, target_rows):
    source_constraint = support_constraint(decoded[27], decoded[29])
    target_constraint = support_constraint(
        target_rows["left_palm"], target_rows["left_prop"])
    if _constraint_sha256(source_constraint) != SOURCE_CONSTRAINT_SHA256[label]:
        raise ValueError(f"{label}: source constraint changed")
    if _constraint_sha256(target_constraint) != TARGET_CONSTRAINT_SHA256[label]:
        raise ValueError(f"{label}: target constraint changed")
    return {
        "source_constraint": list(source_constraint),
        "target_constraint": list(target_constraint),
        "source_constraint_p_q_f32_sha256": SOURCE_CONSTRAINT_SHA256[label],
        "target_constraint_p_q_f32_sha256": TARGET_CONSTRAINT_SHA256[label],
        "source_to_target_rotation_delta_degrees": _angular_degrees(
            source_constraint, target_constraint),
        "source_to_target_origin_delta_asset_units": _origin_distance(
            source_constraint, target_constraint),
        "source_to_target_origin_delta_vector": [
            target_constraint[8 + axis] - source_constraint[8 + axis]
            for axis in range(3)
        ],
    }


def _audit_type5_callback(overlay: bytes, parser):
    descriptor = overlay[TYPE5_DESCRIPTOR_OFFSET:TYPE5_DESCRIPTOR_OFFSET + 0x1C]
    if len(descriptor) != 0x1C or _sha256(descriptor) != TYPE5_DESCRIPTOR_SHA256:
        raise ValueError("type5 callback 14860 descriptor changed")
    code_offset, pool_offset, script_id, instructions, result = (
        parser.decode_linear_script_callback(overlay, TYPE5_DESCRIPTOR_OFFSET))
    descriptor_type, size_field, descriptor_script_id = struct.unpack_from(
        "<III", descriptor, 0x10)
    if (code_offset, pool_offset, script_id, descriptor_script_id) != (
            TYPE5_CODE_OFFSET, TYPE5_POOL_OFFSET, TYPE5_SCRIPT_ID, TYPE5_SCRIPT_ID):
        raise ValueError("type5 callback 14860 pointers/id changed")
    if descriptor_type != parser.string_id(b"function") or size_field != 0x4C:
        raise ValueError("type5 callback 14860 descriptor type/size changed")
    if len(instructions) != 15:
        raise ValueError("type5 callback 14860 instruction count changed")
    code = overlay[code_offset:code_offset + 4 * len(instructions)]
    pool = overlay[pool_offset:pool_offset + 8]
    if _sha256(code) != TYPE5_CODE_SHA256 or _sha256(pool) != TYPE5_POOL_SHA256:
        raise ValueError("type5 callback 14860 code/pool changed")
    opcode_sequence = [instruction[1] for instruction in instructions]
    unsupported = sorted(set(opcode_sequence) - {
        0x00, 0x01, 0x0C, 0x0D, 0x0E, 0x0F, 0x3B, 0x43,
    })
    contains_float_one = any(
        struct.unpack_from("<I", pool, offset)[0] == 0x3F800000
        for offset in range(0, len(pool), 4)
    )
    if contains_float_one or result != "r6" or unsupported != [0x15, 0x1C]:
        raise ValueError("type5 callback 14860 bounded interpretation changed")
    return {
        "descriptor_offset": f"{TYPE5_DESCRIPTOR_OFFSET:X}",
        "descriptor_sha256": TYPE5_DESCRIPTOR_SHA256,
        "descriptor_type": f"{descriptor_type:08X}",
        "descriptor_size_field": size_field,
        "script_id": f"{script_id:08X}",
        "code_offset": f"{code_offset:X}",
        "code_bytes": len(code),
        "code_sha256": TYPE5_CODE_SHA256,
        "pool_offset": f"{pool_offset:X}",
        "bounded_pool_bytes": len(pool),
        "bounded_pool_hex": pool.hex(),
        "bounded_pool_sha256": TYPE5_POOL_SHA256,
        "opcode_sequence": [f"{opcode:02X}" for opcode in opcode_sequence],
        "unsupported_static_opcodes": [f"{opcode:02X}" for opcode in unsupported],
        "linear_decoder_result": result,
        "pool_contains_f32_one_literal": contains_float_one,
        "endpoint_weight_one_proven": False,
        "conclusion": (
            "The primary 0x14860 pool has no f32 1.0 literal and opcode 0x1C "
            "depends on live callback inputs; this bounded disk pass cannot "
            "prove that the callback reaches weight 1."
        ),
    }


def audit(actor_archive=DEFAULT_ACTOR_ARCHIVE, dc_archive=DEFAULT_DC_ARCHIVE):
    actor_archive, dc_archive = Path(actor_archive), Path(dc_archive)
    parser = source.native.load_audit_module()
    if parser.PACKED_QUATERNION_MODEL != DECODER_MODEL:
        raise ValueError("packed-quaternion scalar model changed")
    actor = parser.Psarc(actor_archive, None)
    dc = parser.Psarc(dc_archive, None)
    animation_data = actor.extract(ANIMATION_ENTRY)
    source_data = actor.extract(npc.ELENA_SKELETON.entry_name)
    target_data = actor.extract(source.NADINE_SKELETON.entry_name)
    retarget_data = dc.extract(source.RETARGET_ENTRY_NAME)
    overlay = dc.extract(OVERLAY_ENTRY)
    source.require_blob(ANIMATION_ENTRY, animation_data, ANIMATION_SIZE, ANIMATION_SHA256)
    source.require_blob(OVERLAY_ENTRY, overlay, OVERLAY_SIZE, OVERLAY_SHA256)

    animation_pak = source.PagedPak(animation_data)
    source_skeleton, source_inverse, source_parents = locator.load_basis(
        source_data, npc.ELENA_SKELETON)
    target_skeleton, target_inverse, target_parents = locator.load_basis(
        target_data, source.NADINE_SKELETON)
    table, count = source.parse_retarget_table(retarget_data)
    mapping = source.resolve_retarget(
        retarget_data, table, count, npc.ELENA_TO_NADINE)
    records = source.resolve_runtime_records(
        mapping, source_skeleton, target_skeleton)
    target_inputs = authored.skeleton_inputs(target_data, target_skeleton)

    clips = {}
    constraints = {}
    targets_by_label = {}
    for label in ("weapon_up", "weapon_down"):
        resolution, decoded, programs = _decode_clip(
            label, animation_data, animation_pak, source_skeleton, parser)
        authored_rows, target_rows = _target_pair(
            label, decoded, source_skeleton, target_skeleton,
            source_inverse, target_inverse, source_parents, target_parents,
            records, target_inputs, parser, source_data, target_data)
        constraint = _constraint_record(label, decoded, target_rows)
        targets_by_label[label] = target_rows
        constraints[label] = constraint
        spec = ANIMATIONS[label]
        clips[label] = {
            "clip_id": f"{spec.clip_id:08X}",
            "source_skeleton_id": f"{spec.skeleton_id:08X}",
            "registration": f"page{spec.registration[0]}+{spec.registration[1]:X}",
            "slot": f"page{spec.slot[0]}+{spec.slot[1]:X}",
            "descriptor": f"page{spec.descriptor[0]}+{spec.descriptor[1]:X}",
            "raw_location": f"page{spec.raw_location[0]}+{spec.raw_location[1]:X}",
            "raw_absolute": f"{resolution.raw_offset:X}",
            "raw_bytes": spec.object_size,
            "raw_sha256": RAW_SHA256[label],
            "sample_count": 1,
            "flags": spec.flags,
            "group_count": spec.group_count,
            "group_descriptors": [
                [f"{value:08X}" for value in descriptor]
                for descriptor in GROUP_DESCRIPTORS
            ],
            "decoded_group0_globals": list(range(19, 31)),
            "decoded_group1_globals": list(range(67, 105)),
            "source_rows_19_30_f32_sha256": SOURCE_ROWS_SHA256[label],
            "programs": programs,
            "authored_rows_19_30": authored_rows,
            "retargeted_terminal_rows": {
                key: list(row) for key, row in target_rows.items()
            },
            "retargeted_terminal_pair_f32_sha256": TARGET_PAIR_SHA256[label],
            "constraint": constraint,
        }

    for key, (joint, semantic, name) in SEMANTICS.items():
        if (source_skeleton.joint_ids[joint] != semantic or
                target_skeleton.joint_ids[joint] != semantic):
            raise ValueError(f"{key}: semantic ID changed")
        source_name = npc._joint_name(
            source_data, source.PagedPak(source_data), source_skeleton, joint, parser)
        target_name = npc._joint_name(
            target_data, source.PagedPak(target_data), target_skeleton, joint, parser)
        if source_name != name or target_name != name:
            raise ValueError(f"{key}: authored name changed")

    up_target = constraints["weapon_up"]["target_constraint"]
    down_target = constraints["weapon_down"]["target_constraint"]
    palm_error = transform_error(
        targets_by_label["weapon_up"]["left_palm"],
        targets_by_label["weapon_down"]["left_palm"])
    if palm_error != 0.0:
        raise ValueError("up/down target palm reference unexpectedly differs")

    return {
        "schema": SCHEMA,
        "classification": CLASSIFICATION,
        "candidate_fix_status": "HYPOTHESIS_NOT_RUNTIME_ADMITTED",
        "retail_executable_mapped": False,
        "native_routine_called": False,
        "game_launched_or_attached": False,
        "runtime_writer_or_admission_proven": False,
        "live_clip_selection_proven": False,
        "visual_fix_proven": False,
        "quaternion_decoder_model": DECODER_MODEL,
        "inputs": {
            "animation_entry": ANIMATION_ENTRY,
            "animation_bytes": len(animation_data),
            "animation_sha256": source.sha256(animation_data),
            "source_skeleton_entry": npc.ELENA_SKELETON.entry_name,
            "source_skeleton_sha256": source.sha256(source_data),
            "target_skeleton_entry": source.NADINE_SKELETON.entry_name,
            "target_skeleton_sha256": source.sha256(target_data),
            "retarget_entry": source.RETARGET_ENTRY_NAME,
            "retarget_sha256": source.sha256(retarget_data),
            "overlay_entry": OVERLAY_ENTRY,
            "overlay_bytes": len(overlay),
            "overlay_sha256": source.sha256(overlay),
        },
        "retarget": {
            "row_index": npc.ELENA_TO_NADINE.row_index,
            "row_offset": f"{npc.ELENA_TO_NADINE.row_offset:X}",
            "map_offset": f"{npc.ELENA_TO_NADINE.map_offset:X}",
            "target_profile": f"{npc.ELENA_TO_NADINE.profile:08X}",
            "scale_bits": f"{npc.ELENA_TO_NADINE.scale_bits:08X}",
            "flags": npc.ELENA_TO_NADINE.flags,
            "row_sha256": npc.ELENA_TO_NADINE.row_sha256,
            "map_sha256": npc.ELENA_TO_NADINE.map_sha256,
            "arm_records": {
                "source_globals": list(range(19, 31)),
                "target_globals": list(range(19, 31)),
                "modes": [EXPECTED_MODES[joint] for joint in range(19, 31)],
            },
        },
        "terminal_semantics": {
            key: {"global": joint, "string_id": f"{semantic:08X}", "name": name}
            for key, (joint, semantic, name) in SEMANTICS.items()
        },
        "clips": clips,
        "up_down_difference": {
            "target_palm_transform_error": palm_error,
            "target_prop_rotation_delta_degrees": _angular_degrees(
                targets_by_label["weapon_up"]["left_prop"],
                targets_by_label["weapon_down"]["left_prop"]),
            "target_prop_origin_delta_asset_units": _origin_distance(
                targets_by_label["weapon_up"]["left_prop"],
                targets_by_label["weapon_down"]["left_prop"]),
            "target_constraint_rotation_delta_degrees": _angular_degrees(
                up_target, down_target),
            "target_constraint_origin_delta_asset_units": _origin_distance(
                up_target, down_target),
        },
        "constraint_algebra": {
            "native_shape": "Relative(worldProp, worldWrist)",
            "relative_definition": "inverse(first) * second",
            "world_prop": "worldWrist * localPalm27 * localProp29",
            "reduced": "inverse(localPalm27 * localProp29)",
            "cancelled": "arbitrary worldWrist/proximal ancestry",
            "noncommuting_ancestor_regression_test": (
                "scripts/test_p99_npc_support_constraint.py"
            ),
        },
        "type5_weight_callback_14860": _audit_type5_callback(overlay, parser),
        "candidate": {
            "choice": (
                "Use the Nadine-target-space up/down terminal constraint, "
                "selected by a separately proven state boundary, rather than "
                "copying a Chloe world wrist or raw Chloe arm chain."
            ),
            "proven_operands": (
                "Nadine local palm27 reference plus retargeted/native mode3 "
                "prop29 constants in clips 036F20E5 and 5E67B1FA"
            ),
            "missing_evidence": [
                "Which live Player/Nadine state selects up versus down.",
                "Whether callback 0x14860 reaches terminal weight 1.",
                "A safe current-player writer for the terminal left target.",
                "Runtime confirmation that this constraint fixes the visible support arm.",
            ],
        },
        "limits": [
            "This is a scalar disk reconstruction, not instruction-exact native float execution.",
            "The cancellation proves ancestry independence; it does not prove the bad visible pose is caused at this seam.",
            "The two clips are native Nadine NPC references, not proof that the current-player graph selects them.",
            "No process, executable mapping, native call, runtime mutation, or game input occurs.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--actor-archive", type=Path, default=DEFAULT_ACTOR_ARCHIVE)
    parser.add_argument("--dc-archive", type=Path, default=DEFAULT_DC_ARCHIVE)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(args.actor_archive, args.dc_archive)
    payload = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8")
    print(payload, end="")


if __name__ == "__main__":
    main()
