#!/usr/bin/env python3
"""Prove which upstream wrist/helper inputs the pinned grip clips do NOT supply.

Read assets only. A target reference is a conditional missing-valid-bit fallback,
not a substitute for an unknown preceding animation lane or locator profile.
No runtime/native evaluator, final arm pose or helper baseline is constructed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import p99_direct_grip_audit as direct
import p99_wrist_helper_inputs_audit as helper
import p99_wrist_scalar_audit as scalars
import p99_weapon_mesh_audit as weapon

source = direct.source
authored = direct.matrix.authored
ROOT = direct.ROOT
UPSTREAM = (22, 24, 26, 49, 51)
MODEL = "p99-sparse-grip-upstream-admission-v1"


def complete_sparse_sources(data, animation, skeleton):
    """Inspect every declared clip group, including terminator-only groups."""
    raw = animation.raw_offset
    end = raw + animation.spec.object_size
    table = raw + source.native.u32(data, raw + 0x14)
    active = tuple(group for clip,group in source.GROUP_PROGRAM_SPECS
                   if clip == animation.spec.clip_id)
    if not active:
        raise ValueError("no pinned sparse program contract")
    empty = []
    for group in range(animation.spec.group_count):
        descriptor = table + 16*group
        if descriptor < raw or descriptor+16 > end:
            raise ValueError("clip descriptor outside object")
        program = raw + source.native.u32(data, descriptor)
        if program < raw or program+2 > end:
            raise ValueError("clip program outside object")
        if group not in active:
            if source.native.u16(data, program) != 0:
                raise ValueError("previously empty clip group now writes unknown rows")
            empty.append(group)
    rows = authored.sparse_sources(data, animation, skeleton, active)
    if len(rows) != 21:
        raise ValueError("sparse grip semantic count changed")
    return rows, {"clip": f"{animation.spec.clip_id:08X}",
        "active_groups": sorted(active), "terminator_only_groups": empty,
        "source_globals": sorted(v[0] for v in rows.values()),
        "all_declared_groups_inspected": True}


def finalize_missing_rows(current, defaults, *, valid_before_finalize):
    """Small semantic model of 141AB78F0, not the complete command evaluator.

    The validity argument is REQUIRED. Readable bytes or an existing reference
    alone cannot decide which rows native finalization is permitted to replace.
    """
    valid = set(valid_before_finalize)
    if not valid <= set(defaults) or not valid <= set(current):
        raise ValueError("valid row missing or outside the default group")
    result = {}
    for joint, fallback in defaults.items():
        value = current[joint] if joint in valid else fallback
        authored.validate_reference(struct.pack("<12f", *value), "finalize semantic model")
        result[joint] = value
    return result


def upstream_nonuniqueness_control(nadine_data):
    """Two explicit synthetic values for an operand the grip never supplies."""
    descriptors = scalars.load_descriptors(nadine_data)
    controls = (("identity", (0.,0.,0.,1.)), ("local-X-60deg", (.5,0.,0.,3.**.5/2)))
    cases = [{"control": name, "local_Q26": q,
              "weights": {f"AUX+{k:X}": v for k,v in scalars.evaluate_weights(q,descriptors).items()}}
             for name,q in controls]
    difference = abs(cases[0]["weights"]["AUX+3EC"]-cases[1]["weights"]["AUX+3EC"])
    if difference <= .5:
        raise ValueError("pinned nonuniqueness control changed")
    return {"classification": "SYNTHETIC_CONTROL_NOT_LIVE_OR_ANATOMICALLY_ADMITTED",
        "varying_input": "LOCAL Q26, outside both sparse grip source masks",
        "grip_clip_source_bytes_changed": False, "cases": cases,
        "AUX3EC_absolute_difference": difference, "scalar_model": scalars.MODEL,
        "real_pose_selected": False, "native_numeric_equivalence_proven": False}


def inspect_assets(animation_data, player_data, chloe_data, nadine_data, retarget_data):
    source.require_blob(source.ANIMATION_ENTRY_NAME, animation_data,
                        source.ANIMATION_ENTRY_SIZE, source.ANIMATION_ENTRY_SHA256)
    player = source.load_skeleton(player_data, source.PLAYER_SKELETON)
    chloe = source.load_skeleton(chloe_data, source.CHLOE_SKELETON)
    nadine = source.load_skeleton(nadine_data, source.NADINE_SKELETON)
    pak = source.PagedPak(animation_data)
    p_sources, p_scope = complete_sparse_sources(animation_data,
        source.resolve_animation(pak, source.PLAYER_ANIMATION), player)
    c_sources, c_scope = complete_sparse_sources(animation_data,
        source.resolve_animation(pak, source.CHLOE_ANIMATION), chloe)
    if set(p_sources) != set(c_sources):
        raise ValueError("Player/Chloe sparse semantic sets differ")
    r124 = source.resolve_runtime_records(direct.fixture.row124_mapping(retarget_data), player, nadine)
    table, count = source.parse_retarget_table(retarget_data)
    r157 = source.resolve_runtime_records(source.resolve_retarget(
        retarget_data, table, count, source.CHLOE_TO_NADINE), chloe, nadine)
    inputs = authored.skeleton_inputs(nadine_data, nadine)
    names = helper.skin.read_joint_names(helper.skin.PagedPak(weapon.BytesSource(nadine_data)),
                                       nadine.spec.joint_table_offset, nadine.spec.joint_count)
    base, ops = helper.group_program(nadine_data, nadine, 0)
    rows = []
    for joint in UPSTREAM:
        maps = []
        for label, records, selected in (("R124", r124, p_sources), ("R157", r157, c_sources)):
            matches = [record for record in records if record.target_global == joint]
            if len(matches) > 1:
                raise ValueError("ambiguous target retarget record")
            maps.append({"map": label, "record_present": bool(matches),
                "source_global": matches[0].source_global if matches else None,
                "mode": matches[0].mode if matches else None,
                "source_authored_by_grip_clip": bool(matches and matches[0].source_id in selected)})
        parameter = authored.parameter_index(nadine, joint)
        raw_ref = inputs["references"][48*parameter:48*(parameter+1)]
        cells = set(helper.cells(48*joint, 48))
        writers = [op.ordinal for op in ops if any(cells & set(helper.cells(dest,width))
                    for dest,width in helper.program.writes(nadine_data,base,op))]
        if writers or any(m["source_authored_by_grip_clip"] for m in maps):
            raise ValueError("upstream source/JP producer boundary changed")
        rows.append({"joint": joint, "name": names[joint][1], "parent": inputs["parents"][joint],
            "parameter": parameter, "reference_file_offset": hex(inputs["reference_offset"]+48*parameter),
            "raw_reference_row": list(struct.unpack("<12f", raw_ref)),
            "raw_reference_sha256": hashlib.sha256(raw_ref).hexdigest(),
            "retarget_maps": maps, "group0_JP_writers": writers,
            "reference_admission": "ONLY_IF_FINAL_TARGET_VALID_BIT_CLEAR",
            "actual_pose_value_recovered": False})
    # The grip images do not author any upstream row, even though R124/R157
    # contain arm records that will be active for OTHER source clips.
    for records, selected in ((r124,p_sources),(r157,c_sources)):
        targets = {r.target_global for r in records if r.source_id in selected}
        if targets != {28,30,*range(105,142,2)}:
            raise ValueError("grip target validity coverage changed")
    return {"classification": "STATIC_UPSTREAM_OPERANDS_NOT_IN_SPARSE_GRIP", "model": MODEL,
        "retail_or_game_execution": False, "runtime_writes": False,
        "animation_sha256": source.ANIMATION_ENTRY_SHA256,
        "skeleton_sha256": nadine.spec.entry_sha256,
        "clips": [p_scope, c_scope], "upstream": rows,
        "target_grip_valid_globals": [28,30,*range(105,142,2)],
        "same_grip_does_not_determine_upstream_pose": True,
        "full_helper_input_admitted": False,
        "upstream_nonuniqueness_control": upstream_nonuniqueness_control(nadine_data),
        "native_evidence": {
            "retarget": "141AE2D05..D39 non-null source-valid bitmap gate before141AE2EBE; target bit set141AE2E55 only after gate",
            "blend": "142188A30 copies one-sided valid inputs; does not replace upstream valid rows with references",
            "finalize": "141AB7993/799F invert existing validity;141AB79A7 intersects count mask;141AB79D7 fills only missing rows",
            "copy": "142188FF0 ->142192640 exact48-byte target-reference copy"},
        "limits": ["Sparse retarget statement concerns the ordinary masked path with a non-null source ValidBits buffer",
                   "Reference fallback is conditional on the FINAL incoming validity, not clip-local absence",
                   "Other base/arm/IK layers may already own valid upstream rows",
                   "JP49/51 lack R124/R157 entries but no universal live constant is proved",
                   "E51542CA is locator-profile data, not the missing actual animation input",
                   "No bind-parent substitution, numerical helper pose, live frame or game-test admission"]}


def audit(nadine_path, archive=weapon.DEFAULT_ARCHIVE):
    parser = source.native.load_audit_module()
    actor = parser.Psarc(Path(archive), None)
    dc = parser.Psarc(Path(archive).with_name("dc1.psarc"), None)
    return inspect_assets(actor.extract(source.ANIMATION_ENTRY_NAME),
        actor.extract(source.PLAYER_SKELETON.entry_name), actor.extract(source.CHLOE_SKELETON.entry_name),
        Path(nadine_path).read_bytes(), dc.extract(source.RETARGET_ENTRY_NAME))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skeleton", type=Path, default=ROOT/"reports/actor_packages/nadine-dlc-base.pak")
    parser.add_argument("--archive", type=Path, default=weapon.DEFAULT_ARCHIVE)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(args.skeleton, args.archive)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
