"""Disk-only arm-pair audit; no live pose is reconstructed from bind rows.

Compare pinned, retargeted NPC UP/DOWN rows 19..30 under common ancestor 18.
Bind/subset mixtures below are explicitly synthetic controls, NOT the current
game pose. Reachability is geometric only: it does not establish torso skin
clearance, selected animation, attachment safety or permission for a writer.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import p99_npc_support_constraint as support


PARENTS = {19: 18, 20: 18, 21: 19, 22: 20, 23: 21, 24: 22,
           25: 23, 26: 24, 27: 25, 28: 26, 29: 27, 30: 28}
LEFT_PROXIMAL = (19, 21, 23, 25)
RIGHT_PROXIMAL = (20, 22, 24, 26)
DISTAL = (27, 28, 29, 30)


def arm_world(rows, ancestor=None):
    if set(rows) != set(PARENTS):
        raise ValueError("expected exactly globals 19..30")
    result = {18: support.grip.row() if ancestor is None else ancestor}
    for joint, parent in PARENTS.items():
        result[joint] = support.compose(result[parent], rows[joint])
    return result


def _radius(shoulder, elbow, wrist):
    axis = tuple(wrist[i] - shoulder[i] for i in range(3))
    upper = tuple(elbow[i] - shoulder[i] for i in range(3))
    denominator = sum(x * x for x in axis)
    if denominator <= 1e-20:
        raise ValueError("coincident shoulder/wrist has no elbow-circle axis")
    projection = sum(a * b for a, b in zip(axis, upper)) / denominator
    return math.sqrt(sum((upper[i] - projection * axis[i]) ** 2
                         for i in range(3)))


def geometry(rows, ancestor=None):
    world = arm_world(rows, ancestor)
    constraint = support.support_constraint(rows[27], rows[29])
    target = support.compose(world[30], constraint)
    shoulder, elbow, wrist = (world[j][8:11] for j in (21, 23, 25))
    upper = math.dist(shoulder, elbow)
    lower = math.dist(elbow, wrist)
    distance = math.dist(shoulder, target[8:11])
    if min(upper, lower, distance) <= 1e-10:
        raise ValueError("degenerate arm or target")
    attainable = abs(upper - lower) <= distance <= upper + lower
    # Standard two-link geometry, not an instruction-exact native solver.
    along = (upper * upper - lower * lower + distance * distance) / (2 * distance)
    radius = math.sqrt(max(0.0, upper * upper - along * along)) if attainable else None
    return {
        "upper": upper, "lower": lower,
        "requested_reach": distance,
        "target_excess": distance - upper - lower,
        "geometrically_attainable": attainable,
        "attainable_target_elbow_radius": radius,
        "pre_reach": math.dist(shoulder, wrist),
        "pre_elbow_radius": _radius(shoulder, elbow, wrist),
        "pre_wrist_to_target": math.dist(wrist, target[8:11]),
        "pre_wrist_to_target_degrees": support._angular_degrees(world[25], target),
        "left_prop_to_right_prop": support._origin_distance(world[29], world[30]),
        "left_prop_to_right_prop_degrees": support._angular_degrees(world[29], world[30]),
        "shoulder_in_common18": list(shoulder),
        "right_prop_in_common18": list(world[30][8:11]),
        "target_in_common18": list(target[8:11]),
        "shoulder_in_right_prop": list(support.relative(world[30], world[21])[8:11]),
    }


def substitute(base, donor, joints, rotation_only=False):
    result = dict(base)
    for joint in joints:
        result[joint] = (support.grip.row(base[joint][:3], donor[joint][4:8],
                                         base[joint][8:11])
                         if rotation_only else donor[joint])
    return result


def load_bind(actor_archive):
    source = support.source
    parser = source.native.load_audit_module()
    actor = parser.Psarc(Path(actor_archive), None)
    data = actor.extract(source.NADINE_SKELETON.entry_name)
    skeleton, _inverse, parents = support.locator.load_basis(data, source.NADINE_SKELETON)
    if any(parents[joint] != parent for joint, parent in PARENTS.items()):
        raise ValueError("Nadine arm hierarchy changed")
    inputs = support.authored.skeleton_inputs(data, skeleton)
    return {j: support.npc._target_reference(inputs, skeleton, j) for j in PARENTS}


def audit(actor_archive=support.DEFAULT_ACTOR_ARCHIVE,
          dc_archive=support.DEFAULT_DC_ARCHIVE):
    authored = support.audit(actor_archive, dc_archive)
    rows = {name: {r["target_global"]: tuple(r["target_local_row"])
                   for r in clip["authored_rows_19_30"]}
            for name, clip in authored["clips"].items()}
    bind = load_bind(actor_archive)
    controls = {}
    for name, donor in rows.items():
        subsets = {
            "distal_only": DISTAL,
            "left_proximal_and_distal": LEFT_PROXIMAL + DISTAL,
            "right_proximal_and_distal": RIGHT_PROXIMAL + DISTAL,
            "both_proximal_keep_bind_left_clavicle": tuple(j for j in PARENTS if j != 19),
            "both_proximal_and_distal": tuple(PARENTS),
        }
        controls[name] = {key: geometry(substitute(bind, donor, joints))
                          for key, joints in subsets.items()}
        controls[name]["all_rotations_keep_bind_translations"] = geometry(
            substitute(bind, donor, PARENTS, rotation_only=True))
        controls[name]["proximal_rotations_keep_bind_translations_with_full_distal"] = geometry(
            substitute(substitute(bind, donor, LEFT_PROXIMAL + RIGHT_PROXIMAL,
                                  rotation_only=True), donor, DISTAL))
    up, down = rows["weapon_up"], rows["weapon_down"]
    between_endpoints = {
        "up_with_down_left_proximal": geometry(substitute(up, down, LEFT_PROXIMAL)),
        "up_with_down_right_proximal": geometry(substitute(up, down, RIGHT_PROXIMAL)),
        "up_with_down_left_distal": geometry(substitute(up, down, (27, 29))),
        "up_with_down_right_proximal_and_left_distal": geometry(
            substitute(up, down, RIGHT_PROXIMAL + (27, 29))),
    }
    deltas = [{"global": j,
               "rotation_degrees": support._angular_degrees(up[j], down[j]),
               "translation_distance": support._origin_distance(up[j], down[j])}
              for j in PARENTS]
    return {
        "schema": "tll-p99-coherent-arm-pose-v1",
        "classification": "STATIC_DISK_ONLY_CONDITIONAL_ARM_GEOMETRY",
        "common_ancestor": 18,
        "retargeted_arm_hashes": {n: c["retargeted_rows_19_30_f32_sha256"]
                                  for n, c in authored["clips"].items()},
        "raw_nadine_reference_segment_translations": {
            j: list(bind[j][8:11]) for j in (23, 24, 25, 26)},
        "raw_nadine_reference_segment_lengths": {
            j: math.sqrt(sum(x*x for x in bind[j][8:11])) for j in (23, 24, 25, 26)},
        "full_authored": {name: geometry(local) for name, local in rows.items()},
        "synthetic_bind_controls_NOT_current_pose": controls,
        "synthetic_endpoint_mixtures_NOT_current_pose": between_endpoints,
        "up_down_local_deltas": deltas,
        "live_proximal_locals_available": False,
        "skin_clearance_proven": False,
        "runtime_writer_admitted": False,
        "game_launched_or_attached": False,
        "visual_fix_proven": False,
        "limitations": [
            "Current frozen 21-row grip models contain no proximal globals19..26.",
            "PID17512 has world S/E/W and four terminal getters, but no torso18 or proximal locals.",
            "No minimal subset for the current live pose is inferred from bind mixtures.",
            "Changing only21/23/25 leaves S, RP, C and requested distance unchanged.",
            "Both endpoint poses are authored geometry, not proven active clip selection.",
            "Small prop-frame separation does not establish hand/finger or torso mesh contact.",
            "Reference/local comparison assumes unit JointSet root scale; live rootS was not captured.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--actor-archive", type=Path, default=support.DEFAULT_ACTOR_ARCHIVE)
    parser.add_argument("--dc-archive", type=Path, default=support.DEFAULT_DC_ARCHIVE)
    args = parser.parse_args()
    print(json.dumps(audit(args.actor_archive, args.dc_archive), indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
