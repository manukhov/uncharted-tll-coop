#!/usr/bin/env python3
"""Pure STATIC finger dependency slice, not a procedural-helper evaluator.

Nadine group1 has two operators:40 propagates both hands;53 exports selected
LOCAL JointParams vectors to shared scratch. Even104..140 are LEFT fingers,
not helper joints. Matrix descendants and local-JP exports are different edges.
No retail code, runtime buffer, mutation or helper pose is evaluated here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source
import p99_group_program_audit as program
import p99_profile_matrix_audit as matrix
import p99_hand_surface_delta_audit as surface
import pak_skin_influence_audit as skin


RIGHT_CHAINS = ((105, 107, 109, 111), (113, 115, 117, 119),
                (121, 123, 125, 127), (129, 131, 133, 135), (137, 139, 141))
LEFT_CHAINS = tuple(tuple(joint-1 for joint in chain) for chain in RIGHT_CHAINS)
OUTSIDE_COUNTS = {26: 1782, 88: 351, 535: 91, 536: 287, 537: 310, 554: 267}
ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def descendants(joint):
    """Affected group1 matrices, including self; never altered descendant JP."""
    for chain in RIGHT_CHAINS:
        if joint in chain:
            return chain[chain.index(joint):]
    raise ValueError("not a selected right-finger joint")


def prove_group1(data, skeleton):
    # Existing exhaustive g0/g1 write and parent audit independently excludes
    # procedural writes into the required parameter slice/palm exports.
    matrix.prove_slice(data, skeleton)
    raw = skeleton.spec.raw_offset
    base = raw+0x8D90
    ops = program.operators(data, raw, 0x8D90)
    require(tuple((op.opcode, op.payload) for op in ops) ==
            ((40, (10, 3, 0x20)), (53, (60, 3, 0xC0))),
            "group1 complete opcode contract")
    records = program.propagation_records(data, base, ops[0])
    expected_parents = {}
    for root, chains in ((27, LEFT_CHAINS), (28, RIGHT_CHAINS)):
        for chain in chains:
            for index, joint in enumerate(chain):
                expected_parents[joint] = root if index == 0 else chain[index-1]
    seen = set()
    for ordinal, (local, encoded) in enumerate(records):
        joint = 104+local
        require(joint in expected_parents, "unexpected group1 joint")
        parent = expected_parents[joint]
        require(encoded == matrix.parent_encoding(1, joint, parent),
                "finger parent/scale contract")
        require(parent in (27, 28) or parent in seen, "parent read before production")
        if joint in seen:
            require(ordinal in (38, 39) and joint == 141, "unexpected duplicate output")
        seen.add(joint)
    require(seen == set(range(104, 142)), "incomplete both-hand graph")
    for op in ops:
        writes = program.writes(data, base, op)
        require(all(encoded & 3 != 0 for encoded, _ in writes),
                "group1 writes local JointParams")
        if op.opcode == 53:
            require(all(encoded & 3 == 3 for encoded, _ in writes),
                    "group1 export writes a non-scratch bank")
            require(not any(matrix._overlaps(e, w, 3, 0xF90, 0x1010)
                            for e, w in writes), "group1 overwrites palm inputs")
    pairs = tuple(struct.unpack_from("<HH", data, base+0xC0+4*i) for i in range(60))
    # The complete map, not just right-hand or quaternion entries. Each pair
    # copies16 bytes directly inside native142194B10, no skeletal recursion.
    origins = tuple(start+16*i for start in (0x60, 0x1E0, 0x360, 0x4E0)
                    for i in range(12)) + tuple(0x600+16*i for i in range(9)) + (
                        0x6F0, 0x700, 0x710)
    expected = tuple(((0x720+16*i) | 3, origin) for i, origin in enumerate(origins))
    require(pairs == expected, "group1 complete local-JP export map")
    return {joint: [destination & 0xFFF0 for destination, origin in pairs
                    if origin == (joint-104)*48+16]
            for chain in RIGHT_CHAINS for joint in chain}


def helper_producer_inventory(data, skeleton, names):
    """Report exact writers/parents, without interpreting their nonlinear math."""
    raw = skeleton.spec.raw_offset
    result = {}
    for joint in OUTSIDE_COUNTS:
        group_index = 0 if joint < 104 else 6
        group = skeleton.groups[group_index]
        relative = (0x53E0 if group_index == 0 else 0xCE70)
        descriptor = raw+program.u32(data, raw+0x2C)+32*group_index
        table = raw+program.u32(data, raw+0x34)
        require(data[descriptor+15] == 1 and
                program.u32(data, table+8*program.u16(data, descriptor+6)+4) == relative,
                "helper program owner changed")
        base = raw+relative
        ops = program.operators(data, raw, relative)
        local, writers, parents = joint-group.first, [], []
        for op in ops:
            if any(matrix._overlaps(e, w, 0, 48*local, 48*(local+1))
                   for e, w in program.writes(data, base, op)):
                writers.append({"ordinal": op.ordinal, "opcode": op.opcode,
                                "address": f"{raw+op.relative:X}"})
            if op.opcode == 40:
                parents.extend({"ordinal": op.ordinal, "parent_encoding": f"{parent:04X}"}
                               for row, parent in program.propagation_records(data, base, op)
                               if row == local)
        require(parents, "missing outside-joint matrix producer")
        result[str(joint)] = {
            "name": names[joint][1], "group": group_index, "program": f"{base:X}",
            "jointparams_writers": writers, "matrix_producers_including_padding": parents,
            "finger_dependency": ("UPSTREAM_FIXED_GROUP0_INPUT" if group_index == 0
                                  else "UNRESOLVED_PROCEDURAL_INPUT_DEPENDENCY"),
            "helper_pose_replayed": False}
    return result


def audit(skeleton_path, body_path):
    pak = skin.PagedPak(Path(skeleton_path))
    data = pak.data
    skeleton = source.load_skeleton(data, source.NADINE_SKELETON)
    names = skin.read_joint_names(pak, skeleton.spec.joint_table_offset, 580)
    require(all(names[j][1].startswith("r_") for chain in RIGHT_CHAINS for j in chain)
            and all(names[j][1].startswith("l_") for chain in LEFT_CHAINS for j in chain),
            "finger side semantics")
    exports = prove_group1(data, skeleton)
    body = skin.PagedPak(Path(body_path))
    require(hashlib.sha256(body.data).hexdigest() == surface.BODY_SHA256,
            "body asset SHA256")
    surface.body_ownership(body)
    mesh = next(m for m in skin.find_mesh_headers(body, 580)
                if m.absolute == surface.MESH_OFFSET)
    selected = set(surface.JOINTS)
    counts, mixed, closed = {}, 0, 0
    for influences in skin.iter_vertex_influences(body, mesh, 580):
        support = {i.joint for i in influences if i.weight_quantized > 0}
        if support and support <= selected:
            closed += 1
        elif support & selected:
            mixed += 1
            for joint in support-selected:
                counts[joint] = counts.get(joint, 0)+1
    require((closed, mixed, counts) == (3040, 1782, OUTSIDE_COUNTS),
            "closed/mixed support inventory changed")
    return {
        "model": "nadine-group1-finger-dependencies-v1", "classification": "STATIC",
        "skeleton_sha256": hashlib.sha256(data).hexdigest(),
        "retail_or_game_execution": False, "helper_replay_proven": False,
        "right_chains": [list(c) for c in RIGHT_CHAINS],
        "left_chains": [list(c) for c in LEFT_CHAINS],
        "group1_program": "5CD70", "upstream_fixed": [26, 27, 28, 30, 88],
        "q_sensitivity": {str(j): {
            "name": names[j][1], "affected_group1_matrices": list(descendants(j)),
            "changed_local_jointparams": [j],
            "changed_shared_quaternion_exports": [f"{off:X}" for off in exports[j]],
            "all_group1_shared_exports_unchanged": not bool(exports[j]),
            "palm28_or_prop30_changed_by_group1": False}
            for chain in RIGHT_CHAINS for j in chain},
        "closed_vertices": closed, "excluded_mixed_vertices": mixed,
        "outside_joint_positive_vertex_counts_overlapping": counts,
        "outside_joint_producers": helper_producer_inventory(data, skeleton, names),
        "constraints": [
            "Even104..140 are left fingers, not helper joints",
            "Group1 has no JP writes; it reads exported palms, propagates same-finger children, then exports selected LOCAL JP vectors",
            "Only the changed joint Q is exported; descendant WORLD matrix changes do not change descendant LOCAL Q exports",
            "For a non-exported Q (including113), all60 group1 shared exports remain byte-identical; this excludes that specific scratch route into later helpers under fixed other inputs, not arbitrary live feedback",
            "Before-op40 localQ correction preserves the native op53 handoff; a final-matrix-only patch is not equivalent",
            "Group0 and exported palms are held fixed for this local-sensitivity slice; this is not a global animation or IK feedback claim",
            "Procedural group6 helpers cannot be replaced by bind-parent rigid propagation; dependency and numeric replay remain unresolved",
            "Only3040 fully supported vertices are admitted to the existing surface model; all1782 mixed vertices also require wrist26",
            "No live work-buffer lifetime, generation, hook safety, grip quality or runtime admission is established"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skeleton", type=Path,
                        default=ROOT/"reports/actor_packages/nadine-dlc-base.pak")
    parser.add_argument("--body", type=Path,
                        default=ROOT/"reports/actor_packages/nadine-rural-body.pak")
    args = parser.parse_args()
    print(json.dumps(audit(args.skeleton, args.body), indent=2))


if __name__ == "__main__":
    main()
