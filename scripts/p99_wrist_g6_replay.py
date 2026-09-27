#!/usr/bin/env python3
"""Pinned analytic helper535/536/537/554 slice, composed with the g0 slice.

Five source operands are mandatory: shoulder22 workspace and LOCAL JP24/26/49/51.
They are NOT acquired here. No bind/profile/default pose, native execution,
runtime admission or absolute hand/contact result follows from this model.
Workspace values stay semantic (basis, translation, separate scale); no native
padding bytes or otherwise unread source components are manufactured.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import p99_wrist_g0_replay as g0
import p99_wrist_helper_inputs_audit as dependency
import p99_wrist_helper_replay as kernels


ROOT = kernels.ROOT
TARGETS = (535, 536, 537, 554)
LOCALS = (31, 32, 33, 34, 51)
ANGLES = {31: (0xEC, 0x1BC, 0x28C), 32: (0xF0, 0x1C0, 0x290),
          33: (0xF4, 0x1C4, 0x294), 34: (0xF8, 0x1C8, 0x298),
          51: (0x12C, 0x1FC, 0x2CC)}
EXPECTED_OPS = {
    535: {(0,52),(4,47),(7,41),(9,47),(11,44),(14,44),(15,40)},
    536: {(0,52),(4,47),(11,44),(15,40)},
    537: {(0,52),(3,47),(4,47),(11,44),(15,40)},
    554: {(0,52),(1,49),(2,50),(4,47),(11,44),(12,40),(15,40)},
}
SCALAR_EXPORTS = {0x1924: 0x3E4, 0x192C: 0x3EC, 0x1940: 0x400,
                  0x1944: 0x404, 0x1948: 0x408}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def expected_origin(cell):
    """Exact final g0 copy origins, not assumed equality by matching joint name."""
    bank, off = cell
    require(bank == 3, 'expected a shared export')
    for first, end, src_bank, src_off in (
            (0x480,0x4B0,0,48*49), (0x250,0x260,0,48*26+16),
            (0xED0,0xF00,1,64*25), (0xF50,0xF90,1,64*27),
            (0x1290,0x12D0,1,64*87), (0x12D0,0x1310,1,64*89)):
        if first <= off < end and (off-first)%4 == 0:
            return (0,49,53,(src_bank,src_off+off-first))
    require(off in SCALAR_EXPORTS, 'unreviewed g0 export origin')
    return (0,49,53,(2,SCALAR_EXPORTS[off])) if off < 0x1940 else (0,50,54,(2,SCALAR_EXPORTS[off]))


def prove_slice(data, skeleton):
    """Snapshot-aware target graph, all intervening writes, exact native parents."""
    graph = dependency.group6_graph(data, skeleton)
    origins = dependency.export_origins(data, skeleton)
    required_shared = {cell for row in dependency.EXPECTED_INPUTS.values() for cell in row if cell[0] == 3}
    for cell in required_shared:
        require(origins.get(cell) == expected_origin(cell), 'g0 copy origin changed at '+dependency.format_cell(cell))
    survival = dependency.prove_shared_survival(data, skeleton, required_shared)
    target_proofs = {}
    pending = []
    for joint in TARGETS:
        outputs = dependency.cells((joint-503+1)*64|1,64)
        leaves, opaque, ops = graph.trace(outputs)
        require(not opaque, f'helper{joint} reaches unsupported numerical operator')
        require(leaves == dependency.EXPECTED_INPUTS[joint], f'helper{joint} input set changed')
        require(ops == EXPECTED_OPS[joint], f'helper{joint} numerical producer slice changed')
        target_proofs[joint] = {'operators': sorted(ops),
            'entry_cells': [dependency.format_cell(cell) for cell in sorted(leaves)]}
        pending.extend(graph.read(cell) for cell in outputs)
    selected = {}
    seen = set()
    while pending:
        node = pending.pop()
        if node in seen:
            continue
        seen.add(node)
        edge, cell, inputs = graph.nodes[node]
        if edge is not None:
            selected.setdefault(edge.ordinal,set()).add(cell)
        pending.extend(inputs)
    base, ops = dependency.group_program(data,skeleton,6)
    parents = {ordinal: tuple((local,parent) for local,parent in dependency.program.propagation_records(data,base,ops[ordinal])
                             if local in (31,32,33,34,50,51)) for ordinal in (8,12,15)}
    require(parents == {8: (), 12: ((50,9),), 15: ((31,0x129B),(32,0x809),
            (33,0x12DB),(34,0x12DB),*((51,0xCC9),)*4)}, 'g6 exact helper parent/order changed')
    # JP49 is a required source, not a constant inferred from absent grip rows.
    g0base, g0ops = dependency.group_program(data,skeleton,0)
    last_shared = {}
    for op in g0ops:
        for dest, width in dependency.program.writes(data,g0base,op):
            for cell in dependency.cells(dest,width):
                if cell in required_shared:
                    last_shared[cell] = (op.ordinal,op.opcode)
    require(all(last_shared.get(cell) == expected_origin(cell)[1:3] for cell in required_shared),
            'g0 shared last writer changed')
    source49 = set(dependency.cells(48*49,48))
    require(not any(source49 & set(dependency.cells(dest,width)) for op in g0ops
                    for dest,width in dependency.program.writes(data,g0base,op)), 'g0 JP49 writer changed')
    return {'targets': target_proofs, 'selected_cells': selected,
            'intervening_write_counts': survival, 'parents': parents,
            'joint49_group0_writers': []}


def load_contract(data):
    import p99_wrist_g6_base_audit as base_kernel
    import p99_wrist_g6_euler_audit as euler
    skeleton = dependency.source.load_skeleton(data,dependency.source.NADINE_SKELETON)
    base, ops = dependency.group_program(data,skeleton,6)
    proof = prove_slice(data,skeleton)
    constants = kernels.authored_constants(data,base,ops[0])
    # Native op40 consumes xyz scale/translation. Preserve authored fourth
    # components when exposing JP rows; no inferred homogeneous defaults.
    for local in LOCALS:
        for component in (*range(4),*range(8,12)):
            require((0,48*local+4*component) in constants, 'missing g6 S/T authored component')
    return {'g0': g0.load_contract(data), 'base553': base_kernel.load_contract(data),
            'euler': euler.load_contract(data), 'proof': proof, 'constants': constants,
            'curves': {ordinal: kernels.scalar_curves(data,base,ops[ordinal]) for ordinal in (3,4,9)}}


def _replay_from_g0(contract, g0_result, jp26, jp49):
    """Internal composition only; public API computes g0 from the same inputs."""
    import p99_wrist_g6_base_audit as base_kernel
    import p99_wrist_g6_euler_audit as euler
    row49 = kernels.finite(jp49,12,'explicit LOCAL JP49')
    bank = dict(contract['constants'])
    for component, value in enumerate(row49):
        bank[3,0x480+4*component] = value
    for shared, source in SCALAR_EXPORTS.items():
        bank[3,shared] = g0_result['weights'][source]
    for ordinal in (3,4):
        kernels.apply_curves(contract['curves'][ordinal],bank,contract['proof']['selected_cells'][ordinal])
    angles49 = euler.decompose49(contract['euler'],row49[4:8])
    bank.update({(2,offset):value for offset,value in zip((0x78,0x148,0x218),angles49)})
    kernels.apply_curves(contract['curves'][9],bank,contract['proof']['selected_cells'][9])
    rows = {}
    angle_values = {}
    for local in LOCALS:
        angles = tuple(bank[2,offset] for offset in ANGLES[local])
        angle_values[503+local] = angles
        q = euler.compose_local(contract['euler'],local,angles)
        rows[local] = tuple((*[bank[0,48*local+4*i] for i in range(4)], *q,
                             *[bank[0,48*local+4*i] for i in range(8,12)]))
    base553 = base_kernel.replay_base553(contract['base553'],
        elbow24=g0_result['upstream_matrices'][24], wrist26=g0_result['upstream_matrices'][26],
        local_q26=jp26[4:8])
    matrices = {553: base553['matrix']}
    rows[50] = base553['jointparams']
    for local, parent in contract['proof']['parents'][15]:
        if parent == 0x129B:
            parent_frame = g0_result['matrices'][86]
        elif parent == 0x12DB:
            parent_frame = g0_result['matrices'][88]
        elif parent == 0x809:
            parent_frame = matrices[534]
        else:
            require(parent == 0xCC9, 'unreviewed helper parent')
            parent_frame = matrices[553]
        matrices[503+local] = kernels.matrix.propagate(parent_frame,rows[local],bool(parent&8))
    return {'jointparams': {503+local:row for local,row in rows.items()},
            'matrices': matrices, 'angles': angle_values, 'euler49': angles49}


def replay_helpers(contract, *, shoulder22, jp24, jp26, jp49, jp51):
    """Conditional analytic closure of the five missing skin-helper frames.

    The input set is source data, not a bag of independently chosen procedural
    intermediates. This still does NOT prove that the caller has actual valid
    same-generation values or the right weapon/skin coordinate frame.
    """
    arm = g0.replay_helpers(contract['g0'],shoulder22=shoulder22,jp24=jp24,jp26=jp26,jp51=jp51)
    helpers = _replay_from_g0(contract,arm,jp26,jp49)
    return {'classification': 'STATIC_CONDITIONAL_G0_G6_HELPER_ANALYTIC_SLICE',
            'helper88_535_536_537_554_analytic_slice_closed': True,
            'actual_same_generation_source_proven': False, 'source_or_runtime_admission': False,
            'native_numeric_equivalence_proven': False, 'full_skin_contact_admitted': False,
            'complete_group0_native_replay': False, 'complete_group6_native_replay': False,
            'required_source_operands': ['shoulder22 workspace','LOCAL JP24','LOCAL JP26','LOCAL JP49','LOCAL JP51'],
            'g0': arm, 'g6': helpers,
            'missing_skin_helper_frames': {88: arm['matrices'][88], **{joint: helpers['matrices'][joint] for joint in TARGETS}}}


def audit(path):
    contract = load_contract(Path(path).read_bytes())
    proof = contract['proof']
    return {'classification': 'STATIC_G0_G6_HELPER_ANALYTIC_CONTRACT',
            'helper88_535_536_537_554_analytic_slice_closed': True,
            'actual_pose_replayed': False, 'retail_or_game_execution': False,
            'source_or_runtime_admission': False, 'full_skin_contact_admitted': False,
            'target_dependency_slices': proof['targets'],
            'intervening_group_write_counts': proof['intervening_write_counts'],
            'required_source_operands': ['shoulder22 workspace','LOCAL JP24','LOCAL JP26','LOCAL JP49','LOCAL JP51'],
            'unreached_operator_ordinals': [5,6,8,10,13],
            'limits': ['Source values/validity/generation not recovered; no default pose',
                       'No native instruction-level float32/rsqrt/rcp/trigonometric equivalence',
                       'Only reached helper slice, not whole g0 or g6',
                       'No absolute helper skin positions/contact admitted or preferred correction selected',
                       'No runtime writer/source observer, attachment change or visual verdict']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skeleton',type=Path,default=ROOT/'reports/actor_packages/nadine-dlc-base.pak')
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    result = audit(args.skeleton)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2,allow_nan=False))


if __name__ == '__main__':
    main()
