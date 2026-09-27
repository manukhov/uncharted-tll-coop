#!/usr/bin/env python3
"""Bounded, pure local-joint response study for the conditional Nadine P99 pose.

Probe right finger LOCAL quaternion axes, keeping local S/T and palm/prop fixed.
This is not an optimizer, native helper replay, runtime pose export, anatomical
joint-limit proof, or grip-quality score. A smaller crossing count is not a fix.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import p99_grip_contact_audit as contact
import p99_finger_dependency_audit as dependency
import p99_profile_matrix_audit as matrix

surface = contact.surface
grip = surface.grip
LOWER_FINGERS = (*contact.FINGERS["pinky"], *contact.FINGERS["ring"], *contact.FINGERS["middle"])
MODEL = "conditional-right-finger-local-axis-response-v1"


def selected_parents(data):
    skeleton, inverse, parents = surface.locator.load_basis(data, surface.locator.source.NADINE_SKELETON)
    matrix.prove_slice(data, skeleton)  # Includes native op40 parent/write checks.
    selected = set(surface.JOINTS)
    expected = {28: 26, 30: 28}
    for chain in contact.FINGERS.values():
        expected.update({j: 28 if i == 0 else chain[i-1] for i, j in enumerate(chain)})
    if {j: parents[j] for j in selected} != expected:
        raise ValueError("right finger authored parent closure changed")
    return {j: parents[j] if parents[j] in selected else -1 for j in selected}, {
        j: inverse[j] for j in selected}


def descendants(parents, joint):
    if joint not in parents:
        raise ValueError("missing joint")
    result = set()
    for candidate in parents:
        cursor, seen = candidate, set()
        while cursor != -1:
            if cursor in seen or cursor not in parents:
                raise ValueError("invalid or cyclic selected hierarchy")
            seen.add(cursor)
            if cursor == joint:
                result.add(candidate)
            cursor = parents[cursor]
    return result


def local_row(frames, parents, joint):
    parent = parents[joint]
    if parent == -1:
        return frames[joint]
    child, ancestor = frames[joint], frames[parent]
    q = grip.qconj(ancestor[4:8])
    return grip.row(rotation=grip.qmul(q, child[4:8]),
                    translation=grip.rotate(q, contact.sub(child[8:11], ancestor[8:11])))


def rotate_local(frames, parents, joint, axis, degrees):
    """Right-multiply Q_local by an axis rotation, transporting its descendants.

    In prop coordinates D=Qjoint*dQ*inverse(Qjoint); apply D around Tjoint.
    Unaffected rows are retained byte-for-byte as Python tuples; target origin
    is assigned its original tuple explicitly. No re-solving palm/prop.
    """
    if set(frames) != set(parents):
        raise ValueError("frame/hierarchy set mismatch")
    for frame in frames.values():
        surface.checked_frame(frame)
    if joint in (28, 30) or joint not in set().union(*map(set, contact.FINGERS.values())):
        raise ValueError("only right finger joints may be perturbed")
    if type(axis) is not int or axis not in (0, 1, 2):
        raise ValueError("axis must be local x/y/z index")
    if not math.isfinite(degrees) or abs(degrees) > 10.:
        raise ValueError("probe angle exceeds the arbitrary 10 degree study bound")
    affected = descendants(parents, joint)
    if affected & {28, 30}:
        raise ValueError("finger perturbation reaches protected palm/prop")
    if degrees == 0.:
        return dict(frames), affected
    half = math.radians(degrees)*.5
    dq = tuple(math.sin(half) if i == axis else 0. for i in range(3)) + (math.cos(half),)
    source = frames[joint]
    delta = grip.qmul(grip.qmul(source[4:8], dq), grip.qconj(source[4:8]))
    result = dict(frames)
    pivot = source[8:11]
    for child in affected:
        old = frames[child]
        t = pivot if child == joint else tuple(pivot[i]+x for i, x in enumerate(
            grip.rotate(delta, contact.sub(old[8:11], pivot))))
        result[child] = grip.row(rotation=grip.qmul(delta, old[4:8]), translation=t)
    return result, affected


def invariants(before, after, parents, joint, affected):
    if set(before) != set(after):
        raise ValueError("changed frame coverage")
    if any(before[j] != after[j] for j in set(before)-affected):
        raise ValueError("unaffected frame changed")
    if any(before[j] != after[j] for j in (28, 30)) or before[joint][8:11] != after[joint][8:11]:
        raise ValueError("protected anchor moved")
    st_error, other_q_error = 0., 0.
    for j in before:
        a, b = local_row(before, parents, j), local_row(after, parents, j)
        st_error = max(st_error, *(abs(a[i]-b[i]) for i in (0, 1, 2, 8, 9, 10)))
        if j != joint:
            other_q_error = max(other_q_error, grip.qerror(a[4:8], b[4:8]))
    if max(st_error, other_q_error) > 2e-12:
        raise ValueError("perturbation changed another local Q or local S/T")
    return {"palm_prop_exactly_unchanged": True, "target_origin_exactly_unchanged": True,
            "unaffected_frames_exactly_unchanged": True, "local_ST_max_error": st_error,
            "other_local_Q_max_error": other_q_error}


def materialize_context(report, body_path, skeleton_path):
    samples, admission, triangles = contact.modeled_points(report, body_path, skeleton_path)
    frames, _ = surface.report_frames(report)
    parents, inverse = selected_parents(Path(skeleton_path).read_bytes())
    body = surface.skin.PagedPak(Path(body_path))
    mesh = next(m for m in surface.skin.find_mesh_headers(body, 580) if m.absolute == surface.MESH_OFFSET)
    positions = surface.read_positions(body, mesh)
    weights = tuple(surface.skin.iter_vertex_influences(body, mesh, 580))
    return samples, admission, triangles, frames, parents, inverse, positions, weights


def reskin(samples, positions, weights, inverse, frames, affected):
    result, movement = [], []
    for sample in samples:
        row = dict(sample)
        vertex = row["vertex"]
        # Study starts from direct only, not a hybrid of direct/proposed joints.
        point = (surface.skin_point(positions[vertex], weights[vertex], frames, inverse)
                 if set(row["positive_joints"]) & affected else row["direct"])
        row["proposed"] = point  # Existing comparison API's second-pose field.
        delta = contact.sub(point, row["direct"])
        movement.append(math.sqrt(contact.dot(delta, delta)))
        result.append(row)
    return result, movement


def candidate_report(context, index, joint, axis, angle, baseline):
    samples, _, triangles, frames, parents, inverse, positions, weights = context
    changed, affected = rotate_local(frames, parents, joint, axis, angle)
    checks = invariants(frames, changed, parents, joint, affected)
    moved, movement = reskin(samples, positions, weights, inverse, changed, affected)
    crossings = contact.compare_crossings(moved, triangles, index, alignment_assumption=contact.ALIGNMENT)
    result = crossings["proposed"]
    old = {(r["body_triangle"], r["weapon_triangle"]) for r in baseline["witnesses"]}
    new = {(r["body_triangle"], r["weapon_triangle"]) for r in result["witnesses"]}
    return {"joint": joint, "axis": "xyz"[axis], "degrees": angle,
            "affected_selected_joints": sorted(affected), "invariants": checks,
            "changed_supported_vertices": sum(x > 1e-12 for x in movement),
            "max_surface_displacement_asset_units": max(movement),
            "crossing_pairs": result["crossing_pairs"], "hand_triangles": result["hand_triangles"],
            "pairs_by_finger_support_group": result["pairs_by_finger_support_group"],
            "removed_baseline_pairs": len(old-new), "new_pairs": len(new-old),
            "zero_crossing_proves_valid_grip": False, "runtime_candidate_admitted": False}


def audit(report, body_path, skeleton_path, p99, pistol, *,
          alignment_assumption, joints=LOWER_FINGERS, angles=(-5., 5.), progress=None):
    if alignment_assumption != contact.ALIGNMENT:
        raise ValueError("explicit unverified identity alignment assumption required")
    if not joints or len(set(joints)) != len(joints) or any(j not in LOWER_FINGERS for j in joints):
        raise ValueError("study admits distinct lower three finger joints only")
    if not angles or len(set(angles)) != len(angles) or any(not math.isfinite(a) or not 0 < abs(a) <= 10 for a in angles):
        raise ValueError("probe angles must be distinct nonzero and within 10 degrees")
    context = materialize_context(report, body_path, skeleton_path)
    routes = dependency.audit(skeleton_path, body_path)
    geometry = contact.weapon.audit_assets(p99, pistol, include_geometry=True)
    vertices, triangles = geometry["vertices_asset_units"], geometry["triangles"]
    ids, patch = contact.lower_grip_patch(vertices, triangles)
    index = contact.TriangleIndex(vertices, triangles, ids)
    baseline = contact.compare_crossings(context[0], context[2], index,
                                        alignment_assumption=contact.ALIGNMENT)["direct"]
    results = []
    for joint in joints:
        for axis in range(3):
            for angle in angles:
                result = candidate_report(context, index, joint, axis, angle, baseline)
                route = routes["q_sensitivity"][str(joint)]
                result["changed_shared_quaternion_exports"] = route["changed_shared_quaternion_exports"]
                result["all_group1_shared_exports_unchanged"] = route["all_group1_shared_exports_unchanged"]
                results.append(result)
                if progress:
                    progress(result)
    return {"model": MODEL, "status": "CONDITIONAL_LOCAL_RESPONSE_NOT_A_FIX",
            "alignment_assumption": alignment_assumption, "retail_or_game_execution": False,
            "runtime_writes": False, "runtime_record_output": False,
            "source_report_sha256": context[1]["source_report_canonical_sha256"],
            "body_sha256": surface.BODY_SHA256, "weapon_sha256": contact.weapon.P99_SHA256,
            "skeleton_sha256": hashlib.sha256(Path(skeleton_path).read_bytes()).hexdigest(),
            "lower_grip_patch_sha256": patch["source_triangle_ids_sha256"],
            "pose": "directR124 conditional D350/B8 subtree only",
            "baseline_crossing_pairs": baseline["crossing_pairs"],
            "baseline_pairs_by_finger_support_group": baseline["pairs_by_finger_support_group"],
            "probe_count": len(results), "probes": results,
            "dependency_model": routes["model"],
            "excluded_mixed_vertices": routes["excluded_mixed_vertices"],
            "helper_replay_proven": False,
            "preferred_pose": None, "runtime_candidate_admitted": False,
            "limits": ["Angles are arbitrary bounded probes, not anatomical ranges or prescribed corrections",
                       "Only right finger local Q changes; local S/T, palm, prop and other selected frames are preserved",
                       "Unmodeled helper-dependent skin and native downstream helper replay remain excluded",
                       "Unchanged group1 shared exports exclude that specific route only; other helper dependencies are unresolved",
                       "Strict crossings are a lower bound, not contact quality or penetration depth",
                       "A missing crossing can mean separation or complete containment, not a correct grip",
                       "Frame/source approximation and live admission limitations of the input report remain"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pose-report", type=Path, required=True)
    parser.add_argument("--body", type=Path, default=surface.ROOT/"reports/actor_packages/nadine-rural-body.pak")
    parser.add_argument("--skeleton", type=Path, default=surface.ROOT/"reports/actor_packages/nadine-dlc-base.pak")
    parser.add_argument("--assume-identity-weapon-prop", action="store_true")
    parser.add_argument("--joints", type=int, nargs="+", default=LOWER_FINGERS)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.loads(args.pose_report.read_text(encoding="utf-8"))
    def progress(row):
        print(f"probe joint={row['joint']} axis={row['axis']} deg={row['degrees']:+g} "
              f"pairs={row['crossing_pairs']} maxmove={row['max_surface_displacement_asset_units']:.8f}", flush=True)
    result = audit(report, args.body, args.skeleton, *contact.weapon.load_assets(),
                   alignment_assumption=contact.ALIGNMENT if args.assume_identity_weapon_prop else None,
                   joints=tuple(args.joints), progress=progress)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "probes"}, indent=2))


if __name__ == "__main__":
    main()
