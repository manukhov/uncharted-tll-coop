"""Compare a freshly reconstructed NPC grip with the current P99 hand geometry.

Uses the existing, pinned SAME NADINE skin/weapon models. No runtime output,
game execution or unverified report-schema substitution. A lower intersection
count is not sufficient to accept a grip; separation and missing helper skin
remain explicit. This is a candidate selection aid, not another loader test.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import p99_wrist_surface_audit as wrist

contact = wrist.contact
surface = contact.surface
grip = surface.grip
MODEL = 'conditional-npc149-vs-p99-grip-contact-v1'
VARIANTS = ('full-hand', 'fingers-only', 'palm-prop-only')


def checked_candidate(rows, parents, names):
    if len(rows) != len(surface.JOINTS):
        raise ValueError('candidate requires exactly 21 right distal rows')
    by_joint = {r['target_global']: r for r in rows}
    if set(by_joint) != set(surface.JOINTS):
        raise ValueError('candidate right-hand semantic coverage differs')
    order = surface.JOINTS
    indices = {j: i for i, j in enumerate(order)}
    parent_indices = [-1 if j == 28 else indices[parents[j]] for j in order]
    local = [wrist.rigid_local(by_joint[j]['local_row']) for j in order]
    rebuilt = grip.prop_relative(grip.distal_frames(local, parent_indices))
    frames = {}
    for index, joint in enumerate(order):
        row = by_joint[joint]
        if row['target_name'] != names[joint]:
            raise ValueError('candidate joint name differs from the pinned Nadine skeleton')
        frame = surface.checked_frame(row['prop_relative_row'])
        if grip.row_error(frame, rebuilt[index]) > 2e-12:
            raise ValueError('candidate local rows and prop-relative frames disagree')
        frames[joint] = frame
    frames[26] = wrist.recover_parent(frames[28], by_joint[28]['local_row'])
    return frames


def select_variant(candidate_rows, baseline, parents, variant):
    """Isolate finger articulation from the different weapon-in-palm transform.

    These are explicit counterfactuals, not three authored clips. Fingers-only
    retains the baseline palm/prop locals, so the held weapon is not moved.
    """
    if variant not in VARIANTS:
        raise ValueError('unknown NPC grip counterfactual')
    original = {r['target_global']: r for r in baseline['local_joint_rows']['rows']}
    names = {j: r['target_name'] for j, r in original.items()}
    checked_candidate(candidate_rows, parents, names)
    candidates = {r['target_global']: r for r in candidate_rows}
    selected = {j for j in surface.JOINTS
                if variant == 'full-hand' or
                (variant == 'fingers-only' and j > 100) or
                (variant == 'palm-prop-only' and j < 100)}
    locals_ = [candidates[j]['local_row'] if j in selected else original[j]['direct_row']
               for j in surface.JOINTS]
    indices = {j: i for i, j in enumerate(surface.JOINTS)}
    parent_indices = [-1 if j == 28 else indices[parents[j]] for j in surface.JOINTS]
    frames = grip.prop_relative(grip.distal_frames([wrist.rigid_local(r) for r in locals_], parent_indices))
    return [{'target_global': j, 'target_name': names[j], 'local_row': list(locals_[i]),
             'prop_relative_row': list(frames[i])} for i, j in enumerate(surface.JOINTS)]


def compare(candidate_rows, baseline, body_path, skeleton_path, p99, pistol, *,
            alignment_assumption, variant='full-hand'):
    if alignment_assumption != contact.ALIGNMENT:
        raise ValueError('explicit unverified weapon/prop alignment required')
    context, closure, proof = wrist.materialize_context(baseline, body_path, skeleton_path)
    samples, admission, triangles, frames, parents, inverse, positions, weights = context
    names = {r['target_global']: r['target_name'] for r in baseline['local_joint_rows']['rows']}
    selected = select_variant(candidate_rows, baseline, parents, variant)
    candidate = checked_candidate(selected, parents, names)
    moved, movement = wrist.response.reskin(samples, positions, weights, inverse, candidate, set(candidate))
    geometry = contact.weapon.audit_assets(p99, pistol, include_geometry=True)
    vertices, weapon_triangles = geometry['vertices_asset_units'], geometry['triangles']
    ids, patch = contact.lower_grip_patch(vertices, weapon_triangles)
    index = contact.TriangleIndex(vertices, weapon_triangles, ids)
    crossings = contact.compare_crossings(moved, triangles, index, alignment_assumption=alignment_assumption)
    distances = contact.compare_distances(moved, index, alignment_assumption=alignment_assumption)
    before = {(r['body_triangle'], r['weapon_triangle']) for r in crossings['direct']['witnesses']}
    after = {(r['body_triangle'], r['weapon_triangle']) for r in crossings['proposed']['witnesses']}
    return {'model': MODEL, 'baseline': 'conditional directR124 D350 + B8',
            'candidate': 'full-weight nonadditive NPC4CAA7420 through authored row149',
            'variant': variant,
            'alignment_assumption': alignment_assumption, 'surface_closure': closure,
            'wrist_reconstruction': proof, 'candidate_rows_sha256': wrist.digest(selected),
            'baseline_report_sha256': admission['source_report_canonical_sha256'],
            'body_sha256': surface.BODY_SHA256, 'weapon_sha256': contact.weapon.P99_SHA256,
            'lower_grip_patch': patch, 'conditional_surface_crossings': crossings,
            'comparison': distances, 'removed_baseline_pairs': len(before-after),
            'new_pairs': len(after-before), 'max_surface_displacement_asset_units': max(movement),
            'palm_prop_wrist_frames_preserved': all(grip.row_error(candidate[j], frames[j]) < 2e-12
                                                   for j in (26, 28, 30)),
            'retail_or_game_execution': False, 'runtime_record_output': False,
            'runtime_candidate_admitted': False, 'visual_improvement_proven': False,
            'left_support_arm_fixed': False,
            'limits': ['Both poses use the SAME Nadine skin; no foreign bone-index copy',
                '4328 wrist/distal-supported vertices only; 494 helper-supported touched vertices remain excluded',
                'Full-hand changes palm/prop and fingers; fingers-only/palm-prop-only isolate those changes and are not authored clips',
                'No variant repairs shoulder/elbow or the left support arm',
                'Only the manually selected lower grip patch; no whole-hand collision or physical grasp score',
                'Fewer strict crossings may mean separation, containment or omitted/coplanar contacts',
                'Full-weight sparse hand-layer model, not the live final blended pose or a runtime writer']}


def main():
    import p99_npc_grip_candidate as npc
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=contact.weapon.DEFAULT_ARCHIVE)
    parser.add_argument('--body', type=Path, default=surface.ROOT/'reports/actor_packages/nadine-rural-body.pak')
    parser.add_argument('--skeleton', type=Path, default=surface.ROOT/'reports/actor_packages/nadine-dlc-base.pak')
    parser.add_argument('--assume-identity-weapon-prop', action='store_true')
    parser.add_argument('--variant', choices=VARIANTS, default='full-hand')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--preview', type=Path)
    args = parser.parse_args()
    # Reconstruct from pinned files in this invocation. Never relabel a supplied
    # candidate JSON as the old directR124/Chloe report to bypass its checks.
    baseline = wrist.direct.audit(args.skeleton, args.archive, args.archive.with_name('dc1.psarc'), include_local_rows=True)
    candidate = npc.audit(args.archive, args.archive.with_name('dc1.psarc'), args.skeleton)
    result = compare(candidate['candidate_rows'], baseline, args.body, args.skeleton,
                     *contact.weapon.load_assets(args.archive),
                     alignment_assumption=contact.ALIGNMENT if args.assume_identity_weapon_prop else None,
                     variant=args.variant)
    result['candidate_provenance'] = {k: v for k, v in candidate.items() if k != 'candidate_rows'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    if args.preview:
        geometry = contact.weapon.audit_assets(*contact.weapon.load_assets(args.archive), include_geometry=True)
        contact.preview(result, geometry['vertices_asset_units'], geometry['triangles'], args.preview)
    summary = {k: result[k] for k in ('model', 'variant', 'surface_closure', 'removed_baseline_pairs', 'new_pairs',
               'palm_prop_wrist_frames_preserved',
               'max_surface_displacement_asset_units', 'runtime_candidate_admitted')}
    summary['crossings'] = {key: {k: v for k, v in result['conditional_surface_crossings'][key].items() if k != 'witnesses'}
                           for key in ('direct', 'proposed')}
    summary['distances'] = result['comparison']['all_supported_vertices']
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
