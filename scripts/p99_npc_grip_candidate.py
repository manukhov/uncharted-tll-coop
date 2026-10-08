#!/usr/bin/env python3
"""Build the shipped NPC ``weapon-grip`` distal-hand candidate from disk.

This is a deliberately separate candidate schema.  It does not pretend to be
the current-player report accepted by ``p99_hand_surface_delta_audit``.  The
script extracts and fully pins the authored clip, both skeletons and retarget
table, decodes the corrected opcode-24 scalar model, applies row149 at full
weight against Nadine's bind basis, and emits the exact 21 Nadine local and
prop-relative rows used by the low-level hand geometry tools.

No executable is mapped, no native routine is called, and no process or game is
opened.  The result is a scalar candidate, not proof of live clip selection or
visual improvement.  In particular, the native type5 overlay wrapper may apply
less than full weight; this report models the terminal hand layer at weight 1.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source
import p99_direct_grip_audit as current_grip
import p99_grip_composition_audit as grip
import p99_hand_surface_delta_audit as surface
import p99_locator_basis_audit as locator
import p99_native_source_audit as authored


ROOT = Path(__file__).resolve().parents[1]
BASE = source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
DEFAULT_ACTOR_ARCHIVE = BASE / "actor77.psarc"
DEFAULT_DC_ARCHIVE = BASE / "dc1.psarc"
DEFAULT_NADINE_ASSET = ROOT / "reports/actor_packages/nadine-dlc-base.pak"
DEFAULT_OUTPUT = ROOT / "reports/p99-npc-grip-candidate-20261008.json"

SCHEMA = "tll-p99-npc-grip-candidate-v1"
CLASSIFICATION = "STATIC_DISK_ONLY_NATIVE_NPC_GRIP_CANDIDATE"
DECODER_MODEL = "opcode24-cba-swap-f32-32768-sqrt-v2"

ANIMATION_ENTRY = "anim-elena-combat-dlc.pak"
ANIMATION_SIZE = 8_268_944
ANIMATION_SHA256 = "eb556fd2bb53ea5907abdbd43db2be2b17cad98715088f3d422b76ffc3b4b59e"
CLIP_RAW_SHA256 = "81f0429e8e1a3b3420a12ed50f58fed92fed1b1b89499aa409daaf5e443334f1"
SOURCE_ROWS_F32_SHA256 = "a5aa5978d578f8c7a5eccb5bcb555b9cac98a3e8b7ece033b2e1b562abd473c8"
CANDIDATE_LOCAL_ROWS_F32_SHA256 = "e538153dfe8053ab47c757d7b1fb8c5be2bf116e1b4945ba0306bccfe2a69fe6"
CANDIDATE_PROP_ROWS_F32_SHA256 = "c3ecf80cc504bc5442f9981b82f3e67e599dc109fde7cc5282be27ab6c6be360"

ELENA_BASE_ID = 0xD0B04A60
ELENA_DATABASE_ID = 0x46622D19
ELENA_SKELETON_ID = 0x8592680F
NPC_GRIP_CLIP_ID = 0x4CAA7420

ELENA_SKELETON = source.SkeletonSpec(
    "elena-base.pak",
    409_616,
    "d77161503375df9a06ffc81646a88692dc437947274cf172ac7481be8d9b26f2",
    ELENA_BASE_ID,
    ELENA_DATABASE_ID,
    ELENA_SKELETON_ID,
    0x1E24,
    0x3BC0,
    0x3AC90,
    0x3AE28,
    548,
    8,
    0x140,
    ((0, 67), (67, 38), (105, 105), (210, 83),
     (293, 107), (400, 52), (452, 25), (477, 71)),
    "ebf193c3ce8049861f546780bbed86bdfc5e6a37fb916f66e88006ffa5418737",
    "a11edb220ba6e133e07408a25246c839850c245790f0e19dc4e6a4162ba57ce6",
    "1e8b84c5fee7f5e786cc898d302de56a913b333cf7c0a2580a2d563b2b83644b",
    "dbee52090025cdc0ece53e35410b06ad5c35204f62b54c010486fa3f6f8c080b",
    "4d7e21547af30f78ea132390a285db1ab0498533698bc7cf860e98d7086cc22f",
)

NPC_GRIP_ANIMATION = source.AnimationSpec(
    NPC_GRIP_CLIP_ID,
    ELENA_SKELETON_ID,
    (0, 0x43C98),
    (0, 0x3BD78),
    (6, 0x4C2B0),
    (6, 0xBE40),
    0x621480,
    0x400,
    0,
    7,
)

GROUP_PROGRAMS = (
    source.GroupProgramSpec(
        NPC_GRIP_CLIP_ID, 0, (0x120, 0x1B3C0000, 0x3F0, 0), 0x90,
        0xB3DF1E3AE5FCCBCE,
        source.OpcodeMapSpec(8, 4, "f67203d9f71320c166f2a91f3db34cbe291955b590b03c2ee711856e1f917bb5"),
        source.OpcodeMapSpec(8, 6, "4a75ea02e7e0bb50a9ea1538969fbe134a8bf64a0d2171025da69f911a71a9d7"),
    ),
    source.GroupProgramSpec(
        NPC_GRIP_CLIP_ID, 1, (0x190, 0x00260000, 0x3F0, 0), 0x220,
        0x8F9E726C83EADE17,
        source.OpcodeMapSpec(40, 2, "79f68091fa1a53723201467643836493b50c1826bda1c51b08fe881ed3ec41b2"),
        source.OpcodeMapSpec(24, 5, "f3ab77e04cf0e9b447652377c06b57bca8424da8992718d819942622c793b374"),
    ),
)

ELENA_TO_NADINE = source.RetargetSpec(
    ELENA_BASE_ID,
    source.NADINE_BASE_ID,
    149,
    0x1C40,
    0x2AA0,
    97,
    88,
    0,
    0x3F828F5C,
    0,
    "db8f4990ede179de773b237bf7e95d3289ef2709d15a9569e2f04dffe3729976",
    "7c7c422f40f8752dd07acfacd3f0946556d79a0d3c311bc99bccf7343ab5cb93",
)


def _f32_rows_sha256(rows) -> str:
    return hashlib.sha256(b"".join(struct.pack("<12f", *row) for row in rows)).hexdigest()


def _joint_name(data: bytes, pak: source.PagedPak, skeleton: source.SkeletonAsset,
                joint: int, parser) -> str:
    _page, _local, at = pak.resolve_pointer(skeleton.spec.joint_table_offset + 16 * joint + 8)
    page, _ = pak.page_location(at)
    end = data.find(b"\0", at, pak.pages[page].file_offset + pak.pages[page].size)
    if end < 0:
        raise ValueError("unterminated authored joint name")
    name = data[at:end].decode("ascii")
    if parser.string_id(name.encode("ascii")) != skeleton.joint_ids[joint]:
        raise ValueError("authored joint name/StringId mismatch")
    return name


def _decode_group(data: bytes, animation: source.AnimationResolution,
                  skeleton: source.SkeletonAsset,
                  spec: source.GroupProgramSpec, parser) -> dict[int, tuple[float, ...]]:
    destinations, _program = source.parse_group_program(data, animation, spec)
    layout = skeleton.groups[spec.source_group]
    processing = skeleton.processing[layout.processing_index]
    if processing.joint_count != layout.parameter_count:
        raise ValueError("source group processing/parameter count changed")
    selected = source.selected_rows_from_destinations(destinations, processing.joint_count, spec)
    clip = animation.raw_offset
    program = clip + spec.descriptor_words[0]
    rows = [[0.0] * 12 for _ in range(processing.joint_count)]
    for ordinal, opcode in enumerate((22, 24)):
        header, count, selector, payload_relative = struct.unpack_from(
            "<4H", data, program + ordinal * 8)
        if header != 0x300 + opcode or selector != 3:
            raise ValueError("constant program command changed after structural parse")
        map_at = program + payload_relative
        values_at = map_at + 2 * count
        for index, destination in enumerate(struct.unpack_from(f"<{count}H", data, map_at)):
            if destination == 48 * processing.joint_count:
                continue
            row, byte_field = divmod(destination, 48)
            raw = data[values_at + 6 * index:values_at + 6 * (index + 1)]
            values = (struct.unpack("<3e", raw) if opcode == 22
                      else parser.decode_packed_quaternion_48(raw))
            rows[row][byte_field // 4:byte_field // 4 + len(values)] = values
    result = {}
    for local in selected:
        row = tuple(rows[local])
        if row[3] != 0 or row[11] != 0 or not all(math.isfinite(v) for v in row):
            raise ValueError("decoded source row has invalid padding or component")
        if abs(sum(v * v for v in row[4:8]) - 1) > 1e-5:
            raise ValueError("decoded source quaternion is outside unit envelope")
        result[layout.first + local] = row
    return result


def _target_reference(inputs: dict, skeleton: source.SkeletonAsset,
                      joint: int) -> tuple[float, ...]:
    parameter = authored.parameter_index(skeleton, joint)
    return struct.unpack_from("<12f", inputs["references"], 48 * parameter)


def _retarget_local(source_row, reference, mode: int,
                    joint_locator: locator.LocatorQuaternion,
                    parent_locator: locator.LocatorQuaternion):
    # This matches the already validated scalar composition boundary.  Mode3
    # is the weapon-prop special case; mode1 preserves target reference S/T.
    if mode == 3:
        return tuple(source_row)
    if mode != 1:
        raise ValueError(f"unsupported selected retarget mode {mode}")
    rotation = grip.qmul(
        grip.qmul(grip.qconj(parent_locator.normalized), source_row[4:8]),
        joint_locator.normalized)
    return grip.row(reference[:3], rotation, reference[8:11])


def _candidate_rows(animation_data: bytes, source_data: bytes, target_data: bytes,
                    retarget_data: bytes, parser):
    source.require_blob(ANIMATION_ENTRY, animation_data, ANIMATION_SIZE, ANIMATION_SHA256)
    animation_pak = source.PagedPak(animation_data)
    animation = source.resolve_animation(animation_pak, NPC_GRIP_ANIMATION)
    raw = animation_data[animation.raw_offset:animation.raw_offset + NPC_GRIP_ANIMATION.object_size]
    if hashlib.sha256(raw).hexdigest() != CLIP_RAW_SHA256:
        raise ValueError("NPC grip raw ANIM hash changed")
    if parser.PACKED_QUATERNION_MODEL != DECODER_MODEL:
        raise ValueError("packed-quaternion scalar model changed")

    source_skeleton, source_inverse, source_parents = locator.load_basis(
        source_data, ELENA_SKELETON)
    target_skeleton, target_inverse, target_parents = locator.load_basis(
        target_data, source.NADINE_SKELETON)
    decoded = {}
    programs = []
    for spec in GROUP_PROGRAMS:
        group_rows = _decode_group(animation_data, animation, source_skeleton, spec, parser)
        if set(decoded).intersection(group_rows):
            raise ValueError("decoded NPC source global is duplicated")
        decoded.update(group_rows)
        programs.append({
            "group": spec.source_group,
            "descriptor_words": [f"{value:08X}" for value in spec.descriptor_words],
            "program_bytes": spec.program_bytes,
            "program_fnv1a64": f"{spec.program_fnv1a64:016X}",
            "opcode22_map_sha256": spec.opcode22.sha256,
            "opcode24_map_sha256": spec.opcode24.sha256,
        })
    ordered_source = [decoded[joint] for joint in (28, 30, *range(68, 105, 2))]
    if _f32_rows_sha256(ordered_source) != SOURCE_ROWS_F32_SHA256:
        raise ValueError("decoded NPC grip source rows changed")

    table, count = source.parse_retarget_table(retarget_data)
    mapping = source.resolve_retarget(retarget_data, table, count, ELENA_TO_NADINE)
    records = source.resolve_runtime_records(mapping, source_skeleton, target_skeleton)
    target_inputs = authored.skeleton_inputs(target_data, target_skeleton)
    source_by_semantic = {source_skeleton.joint_ids[joint]: (joint, row)
                          for joint, row in decoded.items()}
    if len(source_by_semantic) != len(decoded):
        raise ValueError("selected NPC source semantics are not unique")
    selected = []
    for semantic, (source_joint, source_row) in source_by_semantic.items():
        record = source.one_record(records, semantic, "row149")
        if record.source_global != source_joint or record.target_id != semantic:
            raise ValueError("row149 selected semantic join changed")
        source_parent = source_parents[source_joint]
        target_parent = target_parents[record.target_global]
        if source_parent < 0 or target_parent < 0:
            raise ValueError("selected distal record unexpectedly reaches a root")
        joint_q = locator.build_locator_quaternions(
            source_inverse[source_joint], target_inverse[record.target_global])
        parent_q = locator.build_locator_quaternions(
            source_inverse[source_parent], target_inverse[target_parent])
        reference = _target_reference(target_inputs, target_skeleton, record.target_global)
        local = _retarget_local(source_row, reference, record.mode, joint_q, parent_q)
        selected.append({
            "semantic": semantic,
            "source_global": source_joint,
            "target_global": record.target_global,
            "mode": record.mode,
            "map_index": record.map_index,
            "source_row": source_row,
            "local_row": local,
        })
    selected.sort(key=lambda row: row["target_global"] if row["target_global"] < 100
                  else tuple(surface.JOINTS).index(row["target_global"]) + 100)
    # Do not silently accept a different selection merely because it also has
    # 21 rows.  This exact order is the low-level surface/contact contract.
    selected_by_target = {row["target_global"]: row for row in selected}
    if len(selected_by_target) != len(surface.JOINTS) or set(selected_by_target) != set(surface.JOINTS):
        raise ValueError("NPC grip selection is not the exact surface.JOINTS set")
    selected = [selected_by_target[joint] for joint in surface.JOINTS]
    if Counter(row["mode"] for row in selected) != Counter({1: 20, 3: 1}):
        raise ValueError("row149 selected mode census changed")
    if selected[1]["target_global"] != 30 or selected[1]["mode"] != 3:
        raise ValueError("weapon-prop mode3 record changed")

    local_rows = [row["local_row"] for row in selected]
    target_to_index = {row["target_global"]: index for index, row in enumerate(selected)}
    parents = []
    for index, row in enumerate(selected):
        parent = target_to_index.get(target_parents[row["target_global"]], -1)
        if index == 0:
            if parent != -1:
                raise ValueError("palm should be the selected subtree root")
        elif not 0 <= parent < index:
            raise ValueError("selected distal subtree is not topologically closed")
        parents.append(parent)
    prop_rows = grip.prop_relative(grip.distal_frames(local_rows, parents))
    candidate = []
    target_pak = source.PagedPak(target_data)
    for index, row in enumerate(selected):
        candidate.append({
            "target_global": row["target_global"],
            "target_name": _joint_name(target_data, target_pak, target_skeleton,
                                       row["target_global"], parser),
            "local_row": list(row["local_row"]),
            "prop_relative_row": list(prop_rows[index]),
        })
    return candidate, selected, programs, records


def _compare_current(candidate, nadine_asset: Path, actor_archive: Path,
                     dc_archive: Path, prefix: Path, metadata: Path):
    report = current_grip.audit(nadine_asset, actor_archive, dc_archive,
                                prefix=prefix, metadata=metadata,
                                include_local_rows=True)
    if (report.get("quaternion_decoder_model") != DECODER_MODEL or
            report.get("classification") !=
            "APPROXIMATE_PROFILE_PRESENT_R124_VS_CONDITIONAL_CHLOE_CHAIN"):
        raise ValueError("current R124 comparison report model changed")
    local = {row["target_global"]: row["direct_row"]
             for row in report["local_joint_rows"]["rows"]}
    prop = {row["target_global"]: row["direct_row"]
            for row in report["prop_relative_joint_frames"]}
    if set(local) != set(surface.JOINTS) or set(prop) != set(surface.JOINTS):
        raise ValueError("current R124 comparison joint set changed")
    rows = []
    for candidate_row in candidate:
        joint = candidate_row["target_global"]
        candidate_local = candidate_row["local_row"]
        candidate_prop = candidate_row["prop_relative_row"]
        rows.append({
            "target_global": joint,
            "target_name": candidate_row["target_name"],
            "local_rotation_delta_degrees": grip.angular_degrees(
                local[joint][4:8], candidate_local[4:8]),
            "local_ST_component_delta": max(abs(local[joint][i] - candidate_local[i])
                                             for i in (0, 1, 2, 8, 9, 10)),
            "prop_relative_rotation_delta_degrees": grip.angular_degrees(
                prop[joint][4:8], candidate_prop[4:8]),
            "prop_relative_origin_delta_asset_units": math.sqrt(sum(
                (prop[joint][8 + axis] - candidate_prop[8 + axis]) ** 2
                for axis in range(3))),
        })
    max_local = max(rows, key=lambda row: row["local_rotation_delta_degrees"])
    max_prop_rotation = max(rows, key=lambda row: row["prop_relative_rotation_delta_degrees"])
    max_prop_origin = max(rows, key=lambda row: row["prop_relative_origin_delta_asset_units"])
    return {
        "baseline": "profile-present Player->Nadine row124 plus B8 additive, direct branch",
        "baseline_report_classification": report["classification"],
        "baseline_local_rows_model": report["local_joint_rows"]["model"],
        "baseline_row124_map": report["row124_map"],
        "rows": rows,
        "max_local_rotation": max_local,
        "max_prop_relative_rotation": max_prop_rotation,
        "max_prop_relative_origin": max_prop_origin,
    }


def audit(actor_archive=DEFAULT_ACTOR_ARCHIVE, dc_archive=DEFAULT_DC_ARCHIVE,
          nadine_asset=DEFAULT_NADINE_ASSET,
          prefix=grip.DEFAULT_PREFIX, metadata=grip.DEFAULT_METADATA):
    actor_archive, dc_archive = Path(actor_archive), Path(dc_archive)
    nadine_asset, prefix, metadata = Path(nadine_asset), Path(prefix), Path(metadata)
    parser = source.native.load_audit_module()  # psarc_asset_audit.py; no MappedImage.
    actor = parser.Psarc(actor_archive, None)
    dc = parser.Psarc(dc_archive, None)
    animation_data = actor.extract(ANIMATION_ENTRY)
    source_data = actor.extract(ELENA_SKELETON.entry_name)
    target_data = actor.extract(source.NADINE_SKELETON.entry_name)
    retarget_data = dc.extract(source.RETARGET_ENTRY_NAME)
    if target_data != nadine_asset.read_bytes():
        raise ValueError("comparison Nadine asset differs from actor77 entry")

    candidate, selected, programs, records = _candidate_rows(
        animation_data, source_data, target_data, retarget_data, parser)
    comparison = _compare_current(candidate, nadine_asset, actor_archive,
                                  dc_archive, prefix, metadata)
    source_skeleton = source.load_skeleton(source_data, ELENA_SKELETON)
    source_pak = source.PagedPak(source_data)
    candidate_by_target = {row["target_global"]: row for row in candidate}
    authored_selection = []
    for row in selected:
        group, local = source_skeleton.location(row["source_global"])
        authored_selection.append({
            "semantic": f"{row['semantic']:08X}",
            "source_group": group,
            "source_local": local,
            "source_global": row["source_global"],
            "source_name": _joint_name(source_data, source_pak, source_skeleton,
                                       row["source_global"], parser),
            "source_local_row": list(row["source_row"]),
            "target_global": row["target_global"],
            "target_name": candidate_by_target[row["target_global"]]["target_name"],
            "retarget_mode": row["mode"],
            "retarget_map_index": row["map_index"],
        })
    local_hash = _f32_rows_sha256([row["local_row"] for row in candidate])
    prop_hash = _f32_rows_sha256([row["prop_relative_row"] for row in candidate])
    if local_hash != CANDIDATE_LOCAL_ROWS_F32_SHA256:
        raise ValueError("modeled NPC candidate local rows changed")
    if prop_hash != CANDIDATE_PROP_ROWS_F32_SHA256:
        raise ValueError("modeled NPC candidate prop-relative rows changed")
    return {
        "schema": SCHEMA,
        "classification": CLASSIFICATION,
        "retail_executable_mapped": False,
        "native_routine_called": False,
        "game_launched_or_attached": False,
        "runtime_record_output": False,
        "live_selection_or_visual_improvement_proven": False,
        "quaternion_decoder_model": DECODER_MODEL,
        "inputs": {
            "animation_entry": ANIMATION_ENTRY,
            "animation_bytes": len(animation_data),
            "animation_sha256": source.sha256(animation_data),
            "clip_id": f"{NPC_GRIP_CLIP_ID:08X}",
            "clip_registration": "page0+43C98",
            "clip_descriptor": "page6+4C2B0",
            "clip_raw": "page6+BE40/file+621480",
            "clip_raw_bytes": NPC_GRIP_ANIMATION.object_size,
            "clip_raw_sha256": CLIP_RAW_SHA256,
            "source_skeleton_entry": ELENA_SKELETON.entry_name,
            "source_skeleton_sha256": source.sha256(source_data),
            "source_database_skeleton": f"{ELENA_DATABASE_ID:08X}/{ELENA_SKELETON_ID:08X}",
            "target_skeleton_entry": source.NADINE_SKELETON.entry_name,
            "target_skeleton_sha256": source.sha256(target_data),
            "target_database_skeleton": f"{source.NADINE_DATABASE_ID:08X}/{source.NADINE_SKELETON_ID:08X}",
            "retarget_entry": source.RETARGET_ENTRY_NAME,
            "retarget_entry_bytes": len(retarget_data),
            "retarget_entry_sha256": source.sha256(retarget_data),
        },
        "clip_programs": programs,
        "source_rows": {
            "count": len(selected),
            "selected_globals": [row["source_global"] for row in selected],
            "groups": [
                {"group": group,
                 "selected_locals": [row["source_local"] for row in authored_selection
                                     if row["source_group"] == group],
                 "selected_globals": [row["source_global"] for row in authored_selection
                                      if row["source_group"] == group]}
                for group in (0, 1)
            ],
            "f32_sha256": SOURCE_ROWS_F32_SHA256,
        },
        "authored_selection": authored_selection,
        "retarget": {
            "row_index": ELENA_TO_NADINE.row_index,
            "row_offset": f"{ELENA_TO_NADINE.row_offset:X}",
            "source_target_base": f"{ELENA_BASE_ID:08X}->{source.NADINE_BASE_ID:08X}",
            "source_target_database_skeleton":
                f"{ELENA_DATABASE_ID:08X}/{ELENA_SKELETON_ID:08X}->"
                f"{source.NADINE_DATABASE_ID:08X}/{source.NADINE_SKELETON_ID:08X}",
            "map_offset": f"{ELENA_TO_NADINE.map_offset:X}",
            "authored_count": ELENA_TO_NADINE.authored_count,
            "runtime_resolved_count": len(records),
            "target_profile": f"{ELENA_TO_NADINE.profile:08X}",
            "scale_bits": f"{ELENA_TO_NADINE.scale_bits:08X}",
            "scale": struct.unpack("<f", struct.pack("<I", ELENA_TO_NADINE.scale_bits))[0],
            "flags": ELENA_TO_NADINE.flags,
            "row_sha256": ELENA_TO_NADINE.row_sha256,
            "map_sha256": ELENA_TO_NADINE.map_sha256,
            "selected_mode_census": {"mode1": 20, "mode3_weapon_prop": 1},
        },
        "candidate_rows_contract": {
            "joint_set": "p99_hand_surface_delta_audit.JOINTS",
            "joint_count": len(candidate),
            "local_rows_f32_sha256": local_hash,
            "prop_relative_rows_f32_sha256": prop_hash,
            "row_format": "[Sx,Sy,Sz,pad,Qx,Qy,Qz,Qw,Tx,Ty,Tz,pad]",
            "prop_relative_reference_joint": 30,
        },
        "candidate_rows": candidate,
        "comparison_to_current_r124_b8": comparison,
        "capture_formula": {
            "source": "one-sample 4CAA7420 constant opcode22/opcode24 rows",
            "target_basis": "row149 profile00000000 => Nadine bind inverse-global basis",
            "mode1": "target bind-reference S/T + conjugate(parent locator)*source Q*joint locator",
            "mode3": "source local row copied for r_hand_prop_attachment",
            "row_scale": "authored 1.02 is retained as row metadata; it is not a selected mode1 local S/T operand at this composition boundary",
            "composition": "normal masked terminal hand layer at full weight",
        },
        "limits": [
            "Full hand-layer weight is conditional; the native type5 wrapper/selector may choose a lower weight.",
            "4CAA7420 is the authored generic NPC weapon-grip hand clip, not a P99-silencer-specific clip.",
            "The clip owns palm, weapon-prop and 19 right fingers; it does not own wrist, elbow, shoulder or the left support arm.",
            "Scalar locator math is validated but not instruction-exact float32/half/rsqrt execution.",
            "Candidate separation from current R124+B8 proves a distinct pose, not better rendered contact.",
            "No live selection, mutation boundary, skin contact or visual result is established by this report.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--actor-archive", type=Path, default=DEFAULT_ACTOR_ARCHIVE)
    parser.add_argument("--dc-archive", type=Path, default=DEFAULT_DC_ARCHIVE)
    parser.add_argument("--nadine-asset", type=Path, default=DEFAULT_NADINE_ASSET)
    parser.add_argument("--prefix", type=Path, default=grip.DEFAULT_PREFIX)
    parser.add_argument("--metadata", type=Path, default=grip.DEFAULT_METADATA)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(args.actor_archive, args.dc_archive, args.nadine_asset,
                   args.prefix, args.metadata)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n",
                               encoding="utf-8")
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
