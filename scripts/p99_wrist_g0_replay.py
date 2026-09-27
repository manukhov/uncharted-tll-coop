#!/usr/bin/env python3
"""Conditional analytic slice for Nadine g0 helper joints85..88.

Requires a caller-supplied shoulder workspace and LOCAL JP24/26/51. Neither
the sparse grip clips nor a reference profile supplies this actual pose here.
The scalar math is not an instruction-exact native evaluator. No game, native
worker, source observer, attachment or runtime writer is invoked.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

import p99_wrist_helper_inputs_audit as dependency
import p99_wrist_helper_replay as staged
import p99_wrist_scalar_audit as scalar


ROOT = staged.ROOT
ANGLE_CELLS = {87: (0x2E8, 0x354, 0x3B8), 88: (0x2EC, 0x358, 0x3BC)}
EXPECTED_ANGLE_ANCESTORS = {
    87: {(0, 52), (4, 129), (11, 47), (21, 128), (22, 128), (25, 47)},
    88: {(0, 52), (8, 41), (11, 47)},
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def prove_scalar_slice(data, base, ops):
    """Trace the actual op44 consumers, not merely final AUX values.

    Use the existing snapshot-aware graph only through the last consumer31;
    later op49 variants need not be guessed or silently accepted. Custom
    128/129 leaves are allowed ONLY at their pinned cells/ordinals, and their
    separate scalar contract pins the LOCAL Q26 dependency and full records.
    """
    graph = dependency.Graph()
    traces = {}
    for op in ops[:32]:
        if op.ordinal in (27, 31):
            joint = 88 if op.ordinal == 27 else 87
            require(op.opcode == 44, 'angle consumer opcode changed')
            lane = 7 if joint == 88 else 2
            words = struct.unpack_from('<8H', data, base+op.payload[2]+16*lane)
            cells = tuple(dependency.cells(x, 4)[0] for x in words[5:8])
            require(words[4] == 48*joint+16 and
                    cells == tuple((2, off) for off in ANGLE_CELLS[joint]),
                    'pinned angle consumer changed')
            leaves, opaque, ancestors = graph.trace(cells)
            expected_leaves = {(0, x) for x in range(0x9A0, 0x9B0, 4)} if joint == 88 else set()
            expected_opaque = set() if joint == 88 else {
                (4, 129, (2, 0x400)), (21, 128, (2, 0x404)), (22, 128, (2, 0x408))}
            require(leaves == expected_leaves, 'angle source leaves changed')
            require(opaque == expected_opaque, 'angle opaque boundary changed')
            require(ancestors == EXPECTED_ANGLE_ANCESTORS[joint], 'angle producer ancestry changed')
            traces[joint] = {
                'consumer_ordinal': op.ordinal,
                'entry_cells': [dependency.format_cell(x) for x in sorted(leaves)],
                'custom_scalar_cells': [dependency.format_cell(x[2]) for x in sorted(opaque)],
                'producer_ordinals_and_opcodes': sorted(ancestors),
            }
        graph.run(dependency.decode_batches(data, base, op))
    require(set(traces) == {87, 88}, 'angle slice incomplete')
    return traces


def prove_operand_survival(data, base, ops):
    """All writes, including vector overlaps; exact arm parent and phase cuts."""
    expected = {}
    for joint in (24, 26, 51):
        expected.update({(0, 48*joint+4*i): () for i in range(12)})
    for joint, ordinal in ((22, 36), (24, 41), (26, 44)):
        expected.update({(1, 64*(joint+1)+4*i): ((ordinal, 40),) for i in range(16)})
    expected.update({(1, 4*i): () for i in range(16)})  # reserved identity W0
    actual = {cell: [] for cell in expected}
    for op in ops:
        writes = {cell for dest, width in dependency.program.writes(data, base, op)
                  for cell in dependency.cells(dest, width)}
        for cell in writes & expected.keys():
            actual[cell].append((op.ordinal, op.opcode))
    for cell, wanted in expected.items():
        require(tuple(actual[cell]) == wanted,
                'input survival changed at '+dependency.format_cell(cell))
    for joint, ordinal, parent in ((24, 41, 0x5C9), (26, 44, 0x649)):
        records = [entry for entry in dependency.program.propagation_records(data, base, ops[ordinal])
                   if entry[0] == joint]
        require(records == [(joint, parent)], 'arm op40 parent changed')
    return {'untouched_local_parameters': [24, 26, 51],
            'workspace_single_writers': {'22': [36, 40], '24': [41, 40], '26': [44, 40]},
            'parents': {'24': 'matrix22, flag8', '26': 'matrix24, flag8'},
            'reserved_workspace0_written_by_program': False}


def load_contract(data):
    """Load pinned assets and all native-derived subcontracts; no source pose."""
    import p99_wrist_euler_audit as euler
    import p99_wrist_rotation_audit as rotation
    skeleton = dependency.source.load_skeleton(data, dependency.source.NADINE_SKELETON)
    base, ops = dependency.group_program(data, skeleton, 0)
    return {
        'staged': staged.load_contract(data),
        'scalars': scalar.load_descriptors(data),
        'euler': euler.load_contract(data),
        'rotation': rotation.load_contract(data),
        'angle_ancestry': prove_scalar_slice(data, base, ops),
        'operand_survival': prove_operand_survival(data, base, ops),
        'curves11': staged.scalar_curves(data, base, ops[11]),
        'curves25': staged.scalar_curves(data, base, ops[25]),
    }


def scalar_angles(contract, *, weights, euler51):
    """Only angle destinations, preserving native curve ordering and old value."""
    angles51 = staged.finite(euler51, 3, 'explicit Euler51 xyz')
    require(set(weights) == {0x400, 0x3E4, 0x3EC, 0x404, 0x408}, 'scalar weight set')
    values = staged.finite((weights[x] for x in sorted(weights)), 5, 'scalar weights')
    bank = dict(contract['staged']['constants'])
    bank.update({(2, offset): value for offset, value in zip(sorted(weights), values)})
    bank.update({(2, offset): value for offset, value in zip((0x2B0, 0x31C, 0x380), angles51)})
    selected = {(2, offset) for offsets in ANGLE_CELLS.values() for offset in offsets}
    staged.apply_curves(contract['curves11'], bank, selected)
    staged.apply_curves(contract['curves25'], bank, selected)
    return {joint: tuple(bank[2, offset] for offset in offsets)
            for joint, offsets in ANGLE_CELLS.items()}


def replay_helpers(contract, *, shoulder22, jp24, jp26, jp51):
    """Analytic 85..88 slice from required source operands, not a live pose.

    Derive elbow24/wrist26 from the supplied shoulder and LOCAL rows using the
    pinned native parent/scale flags. Do not accept independently inconsistent
    Q24, Q26, elbow/wrist workspaces or supplied procedural intermediates.
    """
    import p99_wrist_euler_audit as euler
    import p99_wrist_rotation_audit as rotation
    shoulder = staged.workspace(shoulder22)
    rows = {joint: staged.finite(row, 12, 'explicit JP'+str(joint))
            for joint, row in ((24, jp24), (26, jp26), (51, jp51))}
    elbow = staged.matrix.propagate(shoulder, rows[24], True)
    wrist = staged.matrix.propagate(elbow, rows[26], True)
    weights = scalar.evaluate_weights(rows[26][4:8], contract['scalars'])
    euler24 = euler.decompose_local_q(contract['euler'], 24, rows[24][4:8])
    euler51 = euler.decompose_local_q(contract['euler'], 51, rows[51][4:8])
    angles = scalar_angles(contract, weights=weights, euler51=euler51)
    q85 = rotation.evaluate_q85(shoulder, rows[24][4:8], contract['rotation'])
    q87 = euler.compose_helper_q(contract['euler'], 87, angles[87])
    q88 = euler.compose_helper_q(contract['euler'], 88, angles[88])
    result = staged.replay_g0_staged(contract['staged'], elbow24=elbow, wrist26=wrist,
        jp51=rows[51], q85=q85, q87=q87, q88=q88, euler24_middle=euler24[1])
    result.update({
        'classification': 'STATIC_CONDITIONAL_G0_HELPER_ANALYTIC_SLICE',
        'helper85_88_analytic_slice_closed': True,
        'supplied_unreplayed_intermediates': [],
        'required_source_operands': ['shoulder22 workspace', 'LOCAL JP24', 'LOCAL JP26', 'LOCAL JP51'],
        'actual_same_generation_source_proven': False,
        'native_numeric_equivalence_proven': False,
        'full_skin_contact_admitted': False,
        'upstream_matrices': {24: elbow, 26: wrist},
        'weights': weights,
        'euler24': euler24, 'euler51': euler51, 'angles87_88': angles,
    })
    return result


def audit(path):
    """Report static closure only; deliberately has no pose default or replay."""
    contract = load_contract(Path(path).read_bytes())
    return {
        'classification': 'STATIC_G0_HELPER_ANALYTIC_CONTRACT',
        'helper85_88_analytic_slice_closed': True,
        'actual_pose_replayed': False, 'retail_or_game_execution': False,
        'source_or_runtime_admission': False, 'full_skin_contact_admitted': False,
        'complete_group0_native_replay': False, 'complete_group6_native_replay': False,
        'angle_ancestry': contract['angle_ancestry'],
        'operand_survival': contract['operand_survival'],
        'required_explicit_source_inputs': ['shoulder22 workspace', 'LOCAL JP24', 'LOCAL JP26', 'LOCAL JP51'],
        'limits': [
            'No actual same-generation source values/validity recovered',
            'No identity/bind/profile fallback for unknown valid source rows',
            'Analytic double math; no native float32/rsqrt/rcp/trigonometric error bound',
            'Group6 helper analytic slice is in p99_wrist_g6_replay; actual source/absolute skin positions remain unadmitted',
            'No selected corrected pose, attachment change, hook or visual fix'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skeleton', type=Path, default=ROOT/'reports/actor_packages/nadine-dlc-base.pak')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(args.skeleton)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
