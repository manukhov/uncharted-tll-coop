#!/usr/bin/env python3
"""Conditional analytic g6 base553 (local50), without an actual pose source.

Pinned op49@60E5A writes T553 from shared g0 matrix26; op50@60E62 writes
Q553 from shared g0 matrix24 and LOCAL Q26. Authored primary/auxiliary Qs,
weights, zero point, and the full producer inventory are checked separately.
No generic op50-reference-matrix scale rule is inferred. Native code is never
executed; binary64 sqrt/division replaces native float32 rsqrt/rcp arithmetic.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source
import p99_group_program_audit as program
import p99_profile_matrix_audit as matrix
import p99_wrist_helper_inputs_audit as inputs
import p99_wrist_helper_replay as staged
import p99_wrist_rotation_audit as rotation

ROOT = Path(__file__).resolve().parents[1]
GROUP6_BASE = 0x60E50
TRANSLATION_SHA256 = '883c4ac41c42d7486a0e0ef4b941e15bf3bb7ea27ebbcac8f298b607570d1f31'
ROTATION_SHA256 = '072324b4c7c587fd0ca0eabe2b8276ed3d86090cf59b128706722a8d6b6d2c29'
FUNCTIONS = {
    0x14218D570:(0x28A,'182979595507fbea3695086b7526142b4dfd0e5967d73d89879e96c410858165'),
    0x14218D800:(0x267,'ae1a8bbd475a118728c6f7830da85cd7d1c02c34fd0afffa5c2f572899b6defc'),
    0x142194880:(0xB2,'c5aaac92e6ca5288d6b775c1bbe536960c24b602da05115b71c6c8d7967f5edd'),
    0x142189FB0:(0x292,'a9a2d2414f377bc7e475fd61e663db9e79c83792e88e48be36c457587d22a030'),
}
require = rotation.require


@dataclass(frozen=True)
class Base553Contract:
    asset_sha256: str
    translation_sha256: str
    rotation_sha256: str
    scale_xyzw: tuple[float,...]
    initial_translation_xyzw: tuple[float,...]
    local_point: tuple[float,...]
    weight: float
    parent_encoded: int


_PINNED = Base553Contract(source.NADINE_SKELETON.entry_sha256,TRANSLATION_SHA256,
                         ROTATION_SHA256,(1.,)*4,(0.,0.,0.,1.),(0.,)*3,1.,9)


def shared_origins():
    """Exact meaningful bank reads, including conservative native load widths."""
    result = {}
    # group0 ordinal49/op53 exports LOCAL Q26 and workspace24/26, not a grip
    # source/profile pose. Matrix24 reads only48B; op49 matrix26 loads64B.
    for shared,bank,origin,size in ((0x250,0,0x4F0,16),
                                    (0xED0,1,0x640,48),
                                    (0xF50,1,0x6C0,64)):
        for component in range(0,size,4):
            result[3,shared+component] = (0,49,53,(bank,origin+component))
    return result


def prove_writer_inventory(data,base,ops):
    expected = {(2,x):((0,52),) for x in (0x2C,0x3C,0x4C,0x5C)}
    for component in range(4):
        expected[0,0x960+4*component] = ((0,52),)
        expected[0,0x970+4*component] = ((2,50),)
        expected[0,0x980+4*component] = ((0,52),(1,49))
    for component in range(16):
        expected[1,4*component] = ()
        expected[1,0xCC0+4*component] = ((12,40),)
    expected.update({cell:() for cell in shared_origins()})
    actual = {cell:[] for cell in expected}
    for op in ops:
        touched = {cell for encoded,width in program.writes(data,base,op)
                   for cell in inputs.cells(encoded,width)}
        for cell in expected.keys() & touched:
            actual[cell].append((op.ordinal,op.opcode))
    for cell,writers in expected.items():
        require(tuple(actual[cell]) == writers,
                f'base553 writer inventory changed at {inputs.format_cell(cell)}')
    return expected


def prove_shared_sources(data,skeleton):
    expected = shared_origins()
    actual = inputs.export_origins(data,skeleton)
    require(all(actual.get(cell) == origin for cell,origin in expected.items()),
            'base553 shared g0 source mapping')
    base,ops = inputs.group_program(data,skeleton,0)
    latest = {}
    for op in ops:
        for encoded,width in program.writes(data,base,op):
            for cell in inputs.cells(encoded,width):
                if cell in expected:
                    latest[cell] = (op.ordinal,op.opcode)
    require(all(latest.get(cell) == (49,53) for cell in expected),
            'base553 shared g0 last writer')
    inputs.prove_shared_survival(data,skeleton,set(expected))


def load_contract(data):
    skeleton = source.load_skeleton(data,source.NADINE_SKELETON)
    require(skeleton.location(553) == (6,50),'base553 group/local identity')
    base,ops = inputs.group_program(data,skeleton,6)
    require(base == GROUP6_BASE and len(ops) == 16,'base553 group6 program')
    for ordinal,opcode,payload,absolute in (
        (0,52,(24,49,3,0x90),0x60E50),
        (1,49,(4,3,0x640),0x60E5A),
        (2,50,(4,3,0x690),0x60E62),
        (12,40,(1,3,0x5780),0x60EBC)):
        op = ops[ordinal]
        require((op.opcode,op.payload,skeleton.spec.raw_offset+op.relative) ==
                (opcode,payload,absolute),'base553 operator identity')
    trans = base+ops[1].payload[2]
    quat = base+ops[2].payload[2]
    require(hashlib.sha256(data[trans:trans+80]).hexdigest() == TRANSLATION_SHA256,
            'base553 op49 descriptor bytes')
    require(hashlib.sha256(data[quat:quat+208]).hexdigest() == ROTATION_SHA256,
            'base553 op50 descriptor bytes')
    # Source flag8 => replace, not add old T553. The source is shared matrix26.
    require(struct.unpack_from('<2H',data,trans+12) == (0xF5B,0x980) and
            struct.unpack_from('<4H',data,trans+40) == (0x3E,0x4E,0x5E,0x2E),
            'base553 translation source/zero-point/weight')
    require(struct.unpack_from('<3I',data,trans+48) == (8,32,0) and
            struct.unpack_from('<8H',data,trans+64) ==
                (0x380,0x530,0x860,0x980,0x980,0x980,0x980,0x980),
            'base553 homogeneous-normalization phase')
    # Q source flag8 supplies zero oldQ; reference matrixW0 is identity.
    require(struct.unpack_from('<4H',data,quat+24) == (0xED3,1,0x25B,0x970),
            'base553 rotation source/reference/localQ/replacement')
    identity_soa = (0.,)*12+(1.,)*4
    require(struct.unpack_from('<16f',data,quat+32) == identity_soa and
            struct.unpack_from('<16f',data,quat+144) == identity_soa,
            'base553 authored primary and auxiliary quaternions')
    require(struct.unpack_from('<4I',data,quat+96) == (0x1A,0x22,0x26,0x2E) and
            struct.unpack_from('<4I',data,quat+112) == (1,0,0,0) and
            struct.unpack_from('<8H',data,quat+128) == (0x370,0x520,0x850,0x970,0,0,0,0),
            'base553 rotation scalar/normalization stream')
    # Exact op52 records retain S.w and initial T.w; no workspace padding is
    # manufactured. Full all-op cell audit below excludes later curve writers.
    init_records = ((0x60EF0,0x22,0x13),(0x60F24,0x960,0x13),
                    (0x60F28,0x32,0x23),(0x60F2C,0x42,0x23),
                    (0x60F30,0x52,0x23),(0x61008,0x980,0x33))
    require(all(struct.unpack_from('<2H',data,where) == (dest,origin)
                for where,dest,origin in init_records),'base553 op52 initializer records')
    bank = staged.authored_constants(data,base,ops[0])
    require(tuple(bank[0,0x960+4*i] for i in range(4)) == _PINNED.scale_xyzw and
            tuple(bank[0,0x980+4*i] for i in range(4)) == _PINNED.initial_translation_xyzw and
            tuple(bank[2,i] for i in (0x2C,0x3C,0x4C,0x5C)) == (1.,0.,0.,0.),
            'base553 authored S/T/weight constants')
    require(program.propagation_records(data,base,ops[12]) ==
            ((18,9),(27,9),(44,9),(50,9)),'base553 op40 native parent')
    prove_writer_inventory(data,base,ops)
    prove_shared_sources(data,skeleton)
    return _PINNED


def replay_base553(contract,*,elbow24,wrist26,local_q26):
    """Complete pinned base553 slice with explicit same-generation g0 inputs.

    Matrix26's basis/S and matrix24's translation/separateS are validated but
    algebraically unused by these particular zero-point/identity-reference
    lanes. Q26 magnitude is removed by op50 AUX normalization, sign retained.
    The caller remains responsible for source validity and cross-input frame
    consistency. No independent bind/reference/profile fallback is permitted.
    """
    require(contract == _PINNED,'validated base553 contract required')
    elbow = staged.workspace(elbow24)
    wrist = staged.workspace(wrist26)
    local = rotation.finite(local_q26,4,'explicit g0 LOCAL Q26')
    basis_q,_ = rotation.shoulder_rotation(elbow)
    q553 = rotation.normalized(rotation.multiply(basis_q,local),'base553 auxiliary quaternion')
    # op49: A26*(S26*zero)+t26, homogeneousW=1, weight1, divide byW.
    row = (*contract.scale_xyzw,*q553,*wrist[1],1.)
    frame = matrix.propagate(matrix.IDENTITY,row,True)
    return {'classification':'STATIC_CONDITIONAL_G6_BASE553_ANALYTIC_REPLAY',
            'source_or_runtime_admission':False,'actual_pose_replayed':False,
            'global_joint':553,'group':6,'local_joint':50,
            'jointparams':row,'matrix':frame,
            'complete_group6_native_replay':False,
            'native_workspace_padding_serialized':False}


def verify_native_image(image):
    rotation.verify_native_image(image)  # Includes complete op50 bodies.
    for address,(size,digest) in FUNCTIONS.items():
        require(hashlib.sha256(image.read(address,size)).hexdigest() == digest,
                f'base553 native worker hash {address:X}')
    require(struct.unpack('<Q',image.read(0x143026EA8,8))[0] == 0x142194880,
            'base553 op49 dispatch slot')
    require(struct.unpack('<4f',image.read(0x142BED260,16)) == (1.,)*4,
            'base553 op49 homogeneous coordinate constant')


def audit(path):
    contract = load_contract(Path(path).read_bytes())
    return {'classification':'STATIC_CONDITIONAL_G6_BASE553_CONTRACT',
            'actual_pose_replayed':False,'source_or_runtime_admission':False,
            'retail_or_game_execution':False,
            'asset_sha256':contract.asset_sha256,
            'global_joint':553,'group':6,'local_joint':50,
            'formula':{'S553_xyzw':[1.,1.,1.,1.],
                       'Q553':'normalize(Qorthonormalized(A24) * LOCAL_Q26)',
                       'T553_xyzw':'(matrix26.translation.xyz, 1)',
                       'W553':'op40(parent=W0 identity, flag8, LOCAL row50)'},
            'explicit_inputs':['same-generation final g0 matrix24',
                               'same-generation final g0 matrix26',
                               'same-generation g0 LOCAL Q26'],
            'source_map':['g0 op53 ordinal49: JP4F0 -> SHARED250 LOCAL Q26',
                          'g0 op53 ordinal49: W640 -> SHAREDED0 matrix24 first48B',
                          'g0 op53 ordinal49: W6C0 -> SHAREDF50 matrix26 all64B'],
            'writes':['S553 only52@0','Q553 only50@2','T55352@0 then49@1',
                      'AUX2C/3C/4C/5C only52@0','W553 only40@12',
                      'W0 and selected shared leaves unwritten in g6',
                      'exact g0 exports are last writers; shared inputs survive g1..5'],
            'native_evidence':{
                'op49_primary':'14218D570: A*(S*point)+t, append1, multiplyweight; sourceflag8 replaces',
                'op49_aux':'14218D800: all8 inputs loaded before stores; each float4 divided by its ownW (rcp+one Newton)',
                'op49_wrapper':'142194880 calls18D570@1948EE, then18D800@194900; third phase count0 here',
                'op50':'14218F660 Gram-Schmidt relative basis and primary multiply;142190910 normalizes before post multiply',
                'op40':'142189FB0; parent0x9 reservedW0, no bind-parent inference'},
            'limits':['Conditional analytic binary64, not native float32 instruction/error equivalence',
                      'No actual source pose, validity, owner/generation or contact admission',
                      'Only base553; no complete group6 replay claimed here',
                      'JP S.w/T.w are proved values; native workspace padding is not serialized',
                      'Same-source consistency of supplied workspaces/Q remains caller-owned']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nadine-skeleton',type=Path,default=ROOT/'reports/actor_packages/nadine-dlc-base.pak')
    args = parser.parse_args()
    print(json.dumps(audit(args.nadine_skeleton),indent=2,allow_nan=False))


if __name__ == '__main__':
    main()
