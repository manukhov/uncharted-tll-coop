#!/usr/bin/env python3
"""Pinned, pure analytic Nadine g0 op50 Q85 kernel; no source pose default.

Only the authored lane at op594F6 is implemented, NOT arbitrary op50 modes.
Native evidence: wrapper142194940 -> primary14218F660 -> auxiliary142190910.
The primary orthonormalizes relative-basis columns before quaternion extraction;
the auxiliary NORMALIZES its input quaternion before right multiplying its
authored quaternion. Both authored quaternions and reference W0 are identity
for this lane. Thus Q85 = normalize(q(orthonormalize(A22)) * LOCAL_Q24).

All arithmetic here is binary64 analytic sqrt/division, not instruction-order
float32 / rsqrt / rcp emulation. Near-degenerate inputs fail closed. Nothing
supplies a live shoulder/elbow pose, establishes source generation, or admits
a runtime modification. No native executable is mapped for execution or called.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source
import p99_group_program_audit as program

ROOT = Path(__file__).resolve().parents[1]
GROUP0 = 0x53E0
DESCRIPTOR_SHA256 = '8d5a0db3f82ed9c8fb4b094f0fa0daa51605cb91ec79f8bb73a9a301a3fde78f'
MODEL_MIN_NORM2 = 1e-24  # Defensive model guard, NOT a native comparison.
EXE_SHA256 = 'ed1371a4b47961abc160e92aa0b9bdfd787638fa1a39ba2403b423ab45c210be'
# Full contiguous function bodies through RET/tailcall, including chained
# unwind fragments (the first .pdata fragment alone omits most of each worker).
FUNCTIONS = {
    0x142181430: (0x1FA,'be2dfb2a607f5db2232fc11c5197b220a1712851bd734a62bbfcd5669e1bb280'),
    0x14218F660: (0xA57,'9c921e24738d1dab26b6665fa8acc74e00f88e1a6470941d226afd4173a85665'),
    0x142190910: (0x283,'2b5db09de2ebf21aef14f4f0c299061a2c3f921653e2a57105b5bff6b1ecf0c0'),
    0x142194940: (0x99,'0c11b3f5014ade4423475193c92f74ba7ff9e7d5ccb201d74c7a7cdd97da0a35'),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def finite(values, count, name):
    result = tuple(float(x) for x in values)
    require(len(result) == count and all(math.isfinite(x) for x in result), name)
    return result


def normalized(values, name):
    norm2 = sum(x*x for x in values)
    require(math.isfinite(norm2) and norm2 > MODEL_MIN_NORM2, name + ' degeneracy')
    return tuple(x / math.sqrt(norm2) for x in values)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def multiply(a, b):
    """Hamilton XYZW product, matching14218FD12..FEA6 component equations."""
    x,y,z,w = a
    X,Y,Z,W = b
    return (w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z)


def quaternion_from_columns(columns):
    """142181430, including trace sign and X/Y/Z diagonal tie priority.

    This is NOT14218F3B0's strict-greater variant used by op51. Trace's sign
    bit chooses the native branch; zero-positive uses W. Negative trace picks
    X on diagonal ties, then Y, then Z (native uses strict-less masks).
    """
    require(len(columns) == 3, 'quaternion basis columns')
    columns = tuple(finite(c, 3, 'quaternion basis column') for c in columns)
    a = tuple(tuple(columns[j][i] for j in range(3)) for i in range(3))
    trace = a[0][0] + a[1][1] + a[2][2]
    require(math.isfinite(trace), 'quaternion trace')
    if math.copysign(1., trace) > 0:
        root = math.sqrt(1. + trace)
        c = .5 / root
        q = ((a[2][1]-a[1][2])*c, (a[0][2]-a[2][0])*c,
             (a[1][0]-a[0][1])*c, .5*root)
        branch = 'nonnegative_trace_w'
    else:
        i = 0 if a[0][0] >= a[1][1] and a[0][0] >= a[2][2] else (1 if a[1][1] >= a[2][2] else 2)
        j,k = (i+1)%3, (i+2)%3
        term = 1. + a[i][i] - a[j][j] - a[k][k]
        require(term > 0 and math.isfinite(term), 'quaternion diagonal branch')
        root = math.sqrt(term)
        c = .5 / root
        q = [0.]*4
        q[i] = .5*root
        q[j] = (a[i][j]+a[j][i])*c
        q[k] = (a[i][k]+a[k][i])*c
        q[3] = (a[k][j]-a[j][k])*c
        branch = 'negative_trace_' + 'xyz'[i]
    #14218FC20..FC85 normalizes extractor output, independently of aux phase.
    return normalized(q, 'extracted quaternion'), branch


def shoulder_rotation(shoulder22):
    """14218FA87..FBE4 on sourceA's first48 bytes with pinned identity B.

    Native sourceA separate S(+30) is not read. Translation and third basis
    column are loaded, but do not enter the two-column orthonormalization.
    Validating these unused fields is an input-shape guard, not dependence.
    """
    require(len(shoulder22) == 3, 'shoulder workspace shape')
    a,t,s = shoulder22
    a = tuple(finite(row, 3, 'shoulder basis row') for row in a)
    require(len(a) == 3, 'shoulder basis shape')
    finite(t, 3, 'shoulder translation')
    finite(s, 3, 'shoulder separate scale')
    first = tuple(a[i][0] for i in range(3))
    second = tuple(a[i][1] for i in range(3))
    x = normalized(first, 'shoulder first column')
    z = normalized(cross(x, second), 'shoulder first/second columns')
    y = cross(z, x)
    q, branch = quaternion_from_columns((x,y,z))
    return q, branch


@dataclass(frozen=True)
class RotationContract:
    asset_sha256: str
    descriptor_sha256: str
    operator_absolute: int
    source_matrix_encoded: int
    reference_matrix_encoded: int
    source_q_encoded: int
    destination_encoded: int
    scalar_weight: float


_PINNED = RotationContract(source.NADINE_SKELETON.entry_sha256, DESCRIPTOR_SHA256,
                           0x594F6, 0x5C1, 1, 0x498, 0x1000, 1.)


def prove_writer_inventory(data, base, ops):
    """Do not infer unchanged operands from an authored initial value alone."""
    expected = {(2,0xBC): ((0,52),)}
    for component in range(16):
        expected[1,4*component] = ()  # Reserved W0 identity survives g0.
        expected[1,0x5C0+4*component] = ((36,40),)
    for component in range(4):
        expected[0,0x490+4*component] = ()  # Explicit entry LOCAL Q24.
        expected[0,0x1000+4*component] = ((37,50),)
    actual = {cell: [] for cell in expected}
    for op in ops:
        touched = set()
        for encoded,width in program.writes(data,base,op):
            start = encoded & (-16 if width >= 16 else -4)
            touched.update((encoded&3,start+i) for i in range(0,width,4))
        for cell in expected.keys() & touched:
            actual[cell].append((op.ordinal,op.opcode))
    for cell, writers in expected.items():
        require(tuple(actual[cell]) == writers,
                f'op50 writer inventory changed at bank{cell[0]}+{cell[1]:X}')
    return expected


def load_contract(data):
    """Whole-asset pin plus independently checked exact operand/writer guards."""
    skeleton = source.load_skeleton(data,source.NADINE_SKELETON)
    base = skeleton.spec.raw_offset + GROUP0
    ops = program.operators(data,skeleton.spec.raw_offset,GROUP0)
    op = ops[37]
    require((op.opcode,op.payload,skeleton.spec.raw_offset+op.relative) ==
            (50,(4,3,0x2AC0),0x594F6), 'Q85 op50 identity')
    desc = base + op.payload[2]
    require(hashlib.sha256(data[desc:desc+208]).hexdigest() == DESCRIPTOR_SHA256,
            'Q85 descriptor bytes')
    require(struct.unpack_from('<4H',data,desc+8) == (0x5C1,1,0x498,0x1000),
            'Q85 source/reference/Q/replacement/destination')
    require(all(struct.unpack_from('<4H',data,desc+8*i) == (0x5C1,1,0x498,0x1000)
                for i in (1,2,3)), 'Q85 repeated SIMD lane')
    soa_identity = (0.,)*12 + (1.,)*4
    require(struct.unpack_from('<16f',data,desc+32) == soa_identity and
            struct.unpack_from('<16f',data,desc+144) == soa_identity,
            'Q85 authored primary/auxiliary identity quaternions')
    require(struct.unpack_from('<4I',data,desc+96) == (0x9E,0xBE,0xBE,0xBE) and
            struct.unpack_from('<4I',data,desc+112) == (1,0,0,0) and
            struct.unpack_from('<8H',data,desc+128) == (0xC70,0x1000,0x1000,0x1000,0,0,0,0),
            'Q85 scalar and auxiliary stream')
    # Op52 vector AUX+B0 is sourced from authored1/.081.../.081.../1;
    # its fourth component is the AUX+BC scalar read by all three Q85 lanes.
    require((ops[0].opcode,ops[0].payload) == (52,(34,6,3,0x1B0)), 'Q85 op52 initializer')
    initializer = base + ops[0].payload[3]
    require(initializer+0x9C == 0x5960C and
            struct.unpack_from('<2H',data,initializer+0x9C) == (0xB2,0x43),
            'Q85 AUX+BC op52 source')
    tail = initializer + 16*(34+6)
    require(tail+0x4C == 0x5983C and struct.unpack_from('<f',data,tail+0x4C)[0] == 1.,
            'Q85 AUX+BC authored weight')
    prove_writer_inventory(data,base,ops)
    return _PINNED


def evaluate_q85(shoulder22, local_q24, contract):
    """Conditional complete pinned lane; shoulder/Q24 MUST be explicit inputs.

    No bind-parent reconstruction or source validity is inferred. Flag8 resets
    old destination, so primary hemisphere accumulation cannot pick a sign.
    Auxiliary normalization preserves Q24's sign but removes its magnitude.
    """
    require(contract == _PINNED, 'validated Q85 contract required')
    local = finite(local_q24,4,'explicit LOCAL Q24')
    extracted, _ = shoulder_rotation(shoulder22)
    primary = multiply(extracted,local)  # Both authored corrections are identity.
    return normalized(primary,'Q85 auxiliary input')


def verify_native_image(image):
    """Read-only pe_disassemble_va.Image: no executable-memory mapping/calls."""
    require(hashlib.sha256(image.data).hexdigest() == EXE_SHA256,'retail executable SHA-256')
    for address,(size,digest) in FUNCTIONS.items():
        require(hashlib.sha256(image.read(address,size)).hexdigest() == digest,
                f'op50 native worker hash {address:X}')
    require(struct.unpack('<Q',image.read(0x143026EB0,8))[0] == 0x142194940,
            'op50 built-in dispatch slot')
    for address,expected in ((0x142BDD920,(.5,)*4),(0x142BDDA10,(3.,)*4)):
        require(struct.unpack('<4f',image.read(address,16)) == expected,'op50 Newton constant')
    require(struct.unpack('<4I',image.read(0x142BDD9D0,16)) == (0x80000000,)*4,
            'op50 hemisphere sign mask')


def audit(path):
    contract = load_contract(Path(path).read_bytes())
    return {
        'classification': 'STATIC_CONDITIONAL_OP50_Q85_ANALYTIC_KERNEL',
        'source_or_runtime_admission': False, 'actual_pose_replayed': False,
        'retail_or_game_execution': False,
        'asset_sha256': contract.asset_sha256,
        'operator_absolute': '0x594F6', 'descriptor_absolute': '0x5BE80',
        'descriptor_sha256': contract.descriptor_sha256,
        'formula': 'normalize(q(columns x=normalize(A22.c0), z=normalize(x cross A22.c1), y=z cross x) * LOCAL_Q24)',
        'explicit_inputs': ['same-source final W+5C0 shoulder22 basis', 'same-source entry JP+490 LOCAL Q24'],
        'pins': ['reference W0 identity', 'primary and auxiliary authored Q identity',
                 'AUX+BC weight1, only writer op52 ordinal0', 'flag8 replaces old Q85',
                 'W0 has no g0 writers', 'Q24 has no g0 writers',
                 'shoulder22 only writer op40 ordinal36', 'Q85 only writer op50 ordinal37'],
        'native_evidence': {
            'dispatch': '143026EB0 = 142194940; wrapper primary call1421949AB, auxiliary tailcall1421949D4',
            'first_operand_read': '14218F776..F858: affine rows+0/+10/+20, no separate S+30',
            'orthonormalization': '14218FA87..FBE4: normalize column0, normalize cross(column0,column1), third cross',
            'extractor': '142181430: nonnegative trace W; negative-trace maximum diagonal, X then Y tie priority',
            'primary_normalization': '14218FC20..FC85: normalize extracted matrix quaternion',
            'composition': '14218FD12..FEA6: Qmatrix * (Qinput * Qauthored)',
            'hemisphere': '14218FEDA..FFE8: oldQ*sign(dot(oldQ,newQ)) + weight*newQ; flag8 supplies zero oldQ',
            'auxiliary': '1421909FE..AB8: normalize primary quaternion, then right multiply authored quaternion'},
        'limits': ['Analytic binary64, not native float32 rsqrt/rcp instruction equivalence',
                   'First-column or cross-product near degeneracy rejected by model-only norm2>1e-24 guard',
                   'Only pinned identity-reference Q85 lane; arbitrary op50 second-matrix scale semantics not modeled',
                   'No sparse clip reconstruction of shoulder22 or Q24; no actual baseline, group6 replay or grip claim']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nadine-skeleton',type=Path,default=ROOT/'reports/actor_packages/nadine-dlc-base.pak')
    args = parser.parse_args()
    print(json.dumps(audit(args.nadine_skeleton),indent=2))


if __name__ == '__main__':
    main()
