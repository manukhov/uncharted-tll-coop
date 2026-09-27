#!/usr/bin/env python3
"""Pure wrist-relative extension of the conditional, pinned P99 hand model.

Recover wrist26 from palm28 and its actual modeled LOCAL row, not its bind
parent pose. Only unit-scale geometry is admitted. Procedural helper frames
remain unknown; neither a runtime pose nor a complete grip is produced.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

import p99_direct_grip_audit as direct
import p99_finger_response_audit as response
import p99_wrist_helper_inputs_audit as helpers

contact = response.contact
surface = contact.surface
grip = surface.grip
MODEL = "conditional-palm-local-wrist-surface-v1"
SOURCE_SHA256 = "ac850b5e7d8fe2706a46964dba5d86cb6cf674b797abe0ec1616974e8b55158f"
LOCAL_SHA256 = "a11af17ccce46887b36b0474e058869d7d5e8e980b9b413bce7c1cdc2ed82e58"


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


def ids_digest(ids):
    return hashlib.sha256(struct.pack(f"<{len(ids)}I", *ids)).hexdigest()


def rigid_local(raw):
    """Same explicit geometry normalization as the historical distal model."""
    raw = surface.locator.finite_tuple(raw, 12, "modeled local JointParams")
    if raw[:3] != (1., 1., 1.):
        raise ValueError("wrist reconstruction requires unit local scale")
    if abs(sum(v*v for v in raw[4:8])-1.) > grip.COMPONENT_TOLERANCE:
        raise ValueError("local Q outside audited near-unit envelope")
    # Raw native padding is not an S/Q/T operand (see grip.read_row). The
    # geometry frame has fresh zero padding, as in grip.distal_frames.
    return grip.row(rotation=grip.qnorm(raw[4:8]), translation=raw[8:11])


def recover_parent(child_frame, local_child):
    """W=P*inverse(L), i.e. Qw=Qp*conj(Ql), Tw=Tp-Qw*Tl.

    The order matters for noncommuting rotations. Local scale and parent
    accumulated scale are assumed unit, consistently with the existing model.
    This is not an inverse for a scaled/sheared native workspace.
    """
    child_frame = surface.checked_frame(child_frame)
    local_child = rigid_local(local_child)
    q = grip.qmul(child_frame[4:8], grip.qconj(local_child[4:8]))
    offset = grip.rotate(q, local_child[8:11])
    result = grip.row(rotation=q, translation=contact.sub(child_frame[8:11], offset))
    surface.checked_frame(result)
    rebuilt = grip.blend(result, local_child)
    if grip.row_error(rebuilt, child_frame) > 2e-12:
        raise ValueError("wrist/palm forward reconstruction failed")
    return result


def extended_frames(report, skeleton_data):
    """Pinned provenance, names, exact native parent/write audit, then algebra."""
    base = dict(report)
    local = base.pop("local_joint_rows", None)
    if digest(base) != SOURCE_SHA256 or digest(local) != LOCAL_SHA256:
        raise ValueError("changed or absent pinned pose/local-row evidence")
    original = surface.report_frames(base)
    skeleton, _, parents = surface.locator.load_basis(
        skeleton_data, surface.locator.source.NADINE_SKELETON)
    proof = response.matrix.prove_slice(skeleton_data, skeleton)
    events = {event["global"]: event for event in proof["events"]}
    if parents[28] != 26 or parents[30] != 28 or events[28]["parent"] != 26:
        raise ValueError("native palm/wrist parent relation changed")
    if local["skeleton_sha256"] != hashlib.sha256(skeleton_data).hexdigest():
        raise ValueError("local pose belongs to a different skeleton")
    rows = local["rows"]
    globals_ = [r["target_global"] for r in rows]
    if len(rows) != 21 or set(globals_) != set(surface.JOINTS) or globals_[:2] != [28, 30]:
        raise ValueError("local row selection/order changed")
    if any(r["parent_global"] != parents[r["target_global"]] for r in rows):
        raise ValueError("local row parent disagrees with native skeleton")
    indices = {j: i for i, j in enumerate(globals_)}
    selected_parents = [-1] + [indices[parents[j]] for j in globals_[1:]]
    result = []
    for key, old in zip(("direct_row", "proposed_row"), original):
        locals_ = [rigid_local(r[key]) for r in rows]
        rebuilt = grip.prop_relative(grip.distal_frames(locals_, selected_parents))
        if max(grip.row_error(rebuilt[i], old[j]) for i, j in enumerate(globals_)) > 2e-12:
            raise ValueError("local rows do not reproduce the historical distal frames")
        new = {**old, 26: recover_parent(old[28], rows[0][key])}
        if any(new[j] != old[j] for j in old):
            raise ValueError("wrist extension altered the historical frames")
        result.append(new)
    return base, tuple(result), {
        "source_report_sha256": SOURCE_SHA256, "local_rows_sha256": LOCAL_SHA256,
        "parent_equation": "WristProp = PalmProp * inverse(LocalPalm28)",
        "unit_scale_geometry_assumed": True, "all_21_original_frames_exactly_preserved": True,
        "native_parent_events": [events[j] for j in (26, 28, 30)],
        "native_required_non_op40_writes": proof["non_op40_required_matrix_writes"],
        "geometry_Q_normalized": True, "native_or_live_wrist_reproduced": False}


def materialize_context(report, body_path, skeleton_path):
    data = Path(skeleton_path).read_bytes()
    base, (frames, composed), proof = extended_frames(report, data)
    context = response.materialize_context(base, body_path, skeleton_path)
    old_samples, admission, triangles, _, parents, inverse, positions, weights = context
    _, all_inverse, _ = surface.locator.load_basis(data, surface.locator.source.NADINE_SKELETON)
    parents = {**parents, 26: -1, 28: 26}
    inverse = {**inverse, 26: all_inverse[26]}
    original = {r["vertex"]: r for r in old_samples}
    samples, remaining, outside_counts, added = [], [], Counter(), []
    all_selected = set(frames)
    for i, (p, influences) in enumerate(zip(positions, weights)):
        support = {r.joint for r in influences if r.weight_quantized}
        # Keep the original 4822 hand-touched cohort. Do not quietly pull in
        # unrelated forearm vertices merely because wrist26 is now available.
        if not support & set(surface.JOINTS):
            continue
        if not support <= all_selected:
            remaining.append(i)
            outside_counts.update(support - all_selected)
            continue
        groups = [name for name, joints in contact.FINGERS.items()
                  if support & set(joints) and support <= set(joints) | {26, 28, 30}]
        sample = {"vertex": i, "finger_support_group": groups[0] if len(groups) == 1 else "mixed_or_palm",
                  "positive_joints": sorted(support),
                  "direct": surface.skin_point(p, influences, frames, inverse),
                  "proposed": surface.skin_point(p, influences, composed, inverse)}
        if i in original and sample != original[i]:
            raise ValueError("original surface sample changed during wrist extension")
        if i not in original:
            added.append(i)
        samples.append(sample)
    if len(samples) + len(remaining) != admission["touched_vertices"]:
        raise ValueError("original touched cohort was not preserved")
    selected_ids = {r["vertex"] for r in samples}
    affected_ring = response.descendants(parents, 113)
    closure = {"cohort": "positive influence intersects the original 21 hand joints",
        "original_touched_vertices": admission["touched_vertices"],
        "original_closed_vertices": len(old_samples), "added_wrist_closed_vertices": len(added),
        "closed_vertices": len(samples), "closed_vertex_ids_sha256": ids_digest(sorted(selected_ids)),
        "added_vertex_ids_sha256": ids_digest(added), "excluded_helper_vertices": len(remaining),
        "excluded_positive_joints_overlapping": dict(sorted(outside_counts.items())),
        "closed_triangles": sum(all(v in selected_ids for v in t) for t in triangles),
        "original_samples_exactly_preserved": True, "procedural_helper_frames_fabricated": False,
        "ring113_changed_joint_support_on_excluded_vertices": sum(
            bool({r.joint for r in weights[i] if r.weight_quantized} & affected_ring) for i in remaining),
        "full_hand_surface_closed": not remaining}
    return (samples, admission, triangles, frames, parents, inverse, positions, weights), closure, proof


def partial_skin_delta(point, weights, before, after, inverse, affected):
    """LBS difference with all nonaffected matrices held fixed; not a position.

    Unknown unchanged helper contributions cancel term-by-term. Never drop them
    from an absolute skin position or renormalize the remaining weights.
    """
    if any(j not in before or j not in after or j not in inverse for j in affected):
        raise ValueError("missing changed-joint transform")
    selected = tuple(w for w in weights if w.weight_quantized and w.joint in affected)
    if not selected:
        return (0., 0., 0.)
    return contact.sub(surface.skin_point(point, selected, after, inverse),
                       surface.skin_point(point, selected, before, inverse))


def full_ring_delta(context, helper_proof):
    if (helper_proof["numeric_helper_replay"] is not False or
            set(helper_proof["targets"]) != {"535", "536", "537", "554"} or
            any(t["group1_shared_finger_export_reached"] or t["opaque_group6_operations"]
                for t in helper_proof["targets"].values())):
        raise ValueError("helper buffer dependency proof is incomplete")
    samples, _, _, frames, parents, inverse, positions, weights = context
    changed, affected = response.rotate_local(frames, parents, 113, 0, -5.)
    supported = {r["vertex"]: r for r in samples}
    moving, excluded, errors = [], [], []
    for i, (p, w) in enumerate(zip(positions, weights)):
        if not any(v.weight_quantized and v.joint in affected for v in w):
            continue
        delta = partial_skin_delta(p, w, frames, changed, inverse, affected)
        magnitude = contact.dot(delta, delta)**.5
        moving.append(magnitude)
        if i not in supported:
            excluded.append(magnitude)
        else:
            absolute = contact.sub(surface.skin_point(p, w, changed, inverse), supported[i]["direct"])
            errors.append(max(abs(x-y) for x,y in zip(delta, absolute)))
    return {"condition": "same group0 inputs and nonaliasing banks; only localQ113 changed",
        "formula": "sum_changed_j w_j * ((Mafter_j - Mbefore_j) * inverseBind_j * bindVertex)",
        "helper_dependency_model": helper_proof["model"],
        "changed_joint_supported_vertices": len(moving),
        "unknown_absolute_position_vertices": len(excluded),
        "all_changed_joint_delta_magnitudes": surface.summarize(moving),
        "unknown_position_delta_magnitudes": surface.summarize(excluded),
        "closed_absolute_subtraction_max_error": max(errors),
        "missing_absolute_positions_recovered": False, "full_contact_proven": False,
        "weight_sum_renormalized": False, "live_helper_constancy_proven": False}


def audit(report, body_path, skeleton_path, p99, pistol, *, alignment_assumption):
    if alignment_assumption != contact.ALIGNMENT:
        raise ValueError("explicit unverified identity alignment assumption required")
    context, closure, proof = materialize_context(report, body_path, skeleton_path)
    mesh = contact.weapon.audit_assets(p99, pistol, include_geometry=True)
    vertices, triangles = mesh["vertices_asset_units"], mesh["triangles"]
    selected, region = contact.lower_grip_patch(vertices, triangles)
    index = contact.TriangleIndex(vertices, triangles, selected)
    crossings = contact.compare_crossings(context[0], context[2], index,
                                           alignment_assumption=alignment_assumption)
    probe = response.candidate_report(context, index, 113, 0, -5., crossings["direct"])
    helper_proof = helpers.audit(skeleton_path)
    return {"classification": "STATIC_CONDITIONAL_WRIST_SURFACE_EXTENSION", "model": MODEL,
        "retail_or_game_execution": False, "runtime_writes": False,
        "runtime_candidate_admitted": False, "preferred_pose": None,
        "alignment_assumption": alignment_assumption,
        "native_live_alignment_proven": False, "wrist_reconstruction": proof,
        "surface_closure": closure, "region": region, "conditional_crossings": crossings,
        "counterfactual_113_localX_minus5": probe,
        "conditional_full_ring_displacement": full_ring_delta(context, helper_proof),
        "limits": ["Same conditional D350/B8 source, not live final owner or left support arm",
                   "Unit-scale scalar geometry; not a general inversion of scaled native matrices",
                   "Procedural elbow/wrist helpers remain excluded; no bind-parent substitution",
                   "The unchanged 21-joint surface is extended, not renormalized or realigned",
                   "Patch crossings are incomplete unsigned evidence, not grip quality or penetration depth",
                   "No runtime pose records, native execution, writer or game-test authorization"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--body", type=Path, default=surface.ROOT/"reports/actor_packages/nadine-rural-body.pak")
    parser.add_argument("--skeleton", type=Path, default=surface.ROOT/"reports/actor_packages/nadine-dlc-base.pak")
    parser.add_argument("--archive", type=Path, default=contact.weapon.DEFAULT_ARCHIVE)
    parser.add_argument("--alignment-assumption", required=True, choices=[contact.ALIGNMENT])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    pose = direct.audit(args.skeleton, args.archive, args.archive.with_name("dc1.psarc"),
                        include_local_rows=True)
    p99, pistol = contact.weapon.load_assets(args.archive)
    result = audit(pose, args.body, args.skeleton, p99, pistol,
                   alignment_assumption=args.alignment_assumption)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("model", "surface_closure", "counterfactual_113_localX_minus5")},
                     indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
