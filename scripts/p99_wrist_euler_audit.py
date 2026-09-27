#!/usr/bin/env python3
"""Pinned STATIC analytic op41 modes0/4 and op44 mode4 wrist kernels.

Local inputs remain mandatory: this does not acquire an actual arm pose, fill
unknown rows with bind data, execute retail code or claim native bit identity.
Python binary64 asin/atan2/sin/cos replace the SIMD polynomial/rcp/rsqrt paths.
Native authored float32 constants, branch threshold and multiplication order
are retained; no numeric error bound, especially at the singular boundary, is
claimed. Input/output quaternions are NOT silently normalized.

op41: Euler_mode(conj(A) * localQ * conj(B)), mode0 inverses Qy*Qx*Qz,
mode4 inverses Qz*Qy*Qx. Output is always xyz degrees, not rotation-order slots.
op44 mode4: A * (Qz * Qy * Qx) * B. A/B are the two authored constants.
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
import p99_wrist_helper_inputs_audit as inputs
import p99_wrist_scalar_audit as scalar
import pak_skin_influence_audit as skin

ROOT = Path(__file__).resolve().parents[1]
MODEL = "nadine-op41-0-4-op44-4-analytic-v1"
SINGULAR_LIMIT = 0.9999899864196777  # 142BED020
RAD_TO_DEG = 57.2957763671875  # 142BEDF50 (NOT custom128/129's scalar constant)
DEG_TO_RAD = 0.01745329238474369  # 142BEB5E0
IDENTITY_HEX = "0000000000000000000000000000803f"

# joint: opcode, ordinal, logical lane, operator, payload, record,
#        exact16B record, constant-A address/bytes, constant-B address/bytes.
SPECS = {
    24:(41,8,3,0x59404,(12,3,0xDA0),0x5A190,"00000000200010009004ffff1603ffff",
        0x5A240,"602dd8b9c4b187be2699483de986763f",0x5A230,IDENTITY_HEX),
    51:(41,8,5,0x59404,(12,3,0xDA0),0x5A1B0,"0400000040001000a009b2021e038203",
        0x5A260,"2c1ac9be60a7693db6de11bf5a36383f",0x5A230,IDENTITY_HEX),
    87:(44,31,2,0x594C6,(4,3,0x2890),0x5BC70,"04000000000000006010ea025603ba03",
        0x5BC90,IDENTITY_HEX,0x5BC90,IDENTITY_HEX),
    88:(44,27,7,0x594A6,(16,3,0x2590),0x5B9C0,"04000000400010009010ee025a03be03",
        0x5BA90,"000000a40000c024000090a40000803f",0x5BA60,IDENTITY_HEX),
}
FUNCTIONS = {
    0x14218B780:(0x4B5,"5348f81e66e07d3f08b82fac82f0c3e38a9fc343c28d526f3135246ad67d2314"),
    0x14218BC40:(0x5BA,"f16d0551833547bf26dca9f03a079f41515c5a56cbcb1a83d8f5d889a9b8ebcd"),
    0x14218C200:(0x321,"22835c5b9438a45b6344839c6a3a0d51812928696fa42850302d7314442acfa4"),
    0x14218C530:(0x58F,"92a6584c422876ea2c621aa9fa6402f94c752aa5cc4e2800f014538f65cce6a4"),
}
EVIDENCE = {
    "dispatch":"143026E68 -> op41 142194680; 143026E80 -> op44 1421946D0; authored constants start descriptor+16*count",
    "op41_frame":"14218BDC7..BE4D conjugate A then LEFT-multiply Q; 14218BE57..BF78 RIGHT-multiply conjugate B; call14218B780@BF8C",
    "op41_modes":"14218B7E2..B8C8 masks modes0..5; pinned Q24 mode0/Q51 mode4; no quaternion normalization",
    "op41_branch":"14218B9E3..BA21 singular iff abs(s)>=float32 .99999; clamp(s,-1,1) then negate before asin",
    "op41_singular":"14218BA35..BAE4 singular replaces first atan2 arguments, forces second atan2(0,0); mode0 Z=0/mode4 X=0",
    "op41_math":"asin-like14217FCF0@14218BAFD; atan2-like142180030@BB1B/BB48; xyz permutation and degrees@BB4D..BBE1",
    "op41_scatter":"14218BF91..C154 destination FFFF skips; four lanes read before any scatter; Q24 writes only Y->AUX314",
    "op44_inputs":"14218C5EF..C7B0 reads three encoded scalars per lane; first/second/third map xyz, degrees->half radians@14218C2D4..C30E",
    "op44_mode4":"14218C313/325/336 sincos1421803E0; masks14218C33B..C4B9 form Qz*Qy then1421817F0@C4BE multiplies Qx on RIGHT",
    "op44_frame":"14218C7D1..C8C6 LEFT A*EulerQ; 14218C8CB..C9D7 RIGHT *B; no normalization; four stores@C9D7..CA00",
    "alternatives_rejected":"Raw scatter/XYZ-only or reversed product order do not match mode masks and quaternion products; reference Q is not current local Q",
}


def require(condition,message):
    if not condition:
        raise ValueError(message)


def finite3(values):
    v = tuple(float(x) for x in values)
    require(len(v)==3 and all(math.isfinite(x) for x in v),"three finite xyz angles required")
    return v


def euler_degrees(local_q_xyzw,mode):
    """Analytic equivalents of pinned 14218B780 modes0/4, output xyz degrees.

    The native mode0 middle ROTATION is X, whereas its middle OUTPUT is Y.
    Q24's AUX314 consumes that Y output, not asin(X)'s result.
    """
    require(type(mode) is int and mode in (0,4),"only reviewed Euler modes0/4")
    x,y,z,w = scalar.quaternion(local_q_xyzw)
    dx = x*x+w*w-y*y-z*z
    dy = w*w+y*y-x*x-z*z
    dz = w*w+z*z-x*x-y*y
    s = 2*(y*z-w*x) if mode==0 else 2*(x*z-w*y)
    singular = abs(s) >= SINGULAR_LIMIT
    middle = math.asin(-min(1.,max(-1.,s)))
    if mode == 0:
        if singular:
            result = (middle, math.copysign(1.,s)*math.atan2(2*(w*z-x*y),dx), 0.)
        else:
            result = (middle, math.atan2(2*(w*y+x*z),dz), math.atan2(2*(w*z+x*y),dy))
    else:
        if singular:
            result = (0.,middle,math.copysign(1.,s)*math.atan2(2*(w*x-y*z),dy))
        else:
            result = (math.atan2(2*(w*x+y*z),dz),middle,math.atan2(2*(w*z+x*y),dx))
    return tuple(v*RAD_TO_DEG for v in result)


def quaternion_mode4(angles_xyz_degrees):
    """Analytic 14218C200 mode4: Qz(z)*Qy(y)*Qx(x), no normalization."""
    x,y,z = (a*DEG_TO_RAD*.5 for a in finite3(angles_xyz_degrees))
    sx,sy,sz = math.sin(x),math.sin(y),math.sin(z)
    cx,cy,cz = math.cos(x),math.cos(y),math.cos(z)
    return (cz*cy*sx-sz*sy*cx,cz*sy*cx+sz*cy*sx,
            sz*cy*cx-cz*sy*sx,cz*cy*cx+sz*sy*sx)


def framed_euler(local_q,pre_q,post_q,mode):
    """14218BC40: conj(A)*Q*conj(B). Conjugates, not norm-adjusted inverses."""
    q,a,b = (scalar.quaternion(v) for v in (local_q,pre_q,post_q))
    transformed = scalar.multiply(scalar.multiply(scalar.conjugate(a),q),scalar.conjugate(b))
    return euler_degrees(transformed,mode)


def framed_quaternion(angles,pre_q,post_q):
    """14218C530 mode4: A*EulerQ*B; authored tiny components are retained."""
    a,b = scalar.quaternion(pre_q),scalar.quaternion(post_q)
    return scalar.multiply(scalar.multiply(a,quaternion_mode4(angles)),b)


@dataclass(frozen=True)
class Record:
    joint: int
    opcode: int
    file_offset: int
    raw: bytes
    pre_offset: int
    pre_raw: bytes
    post_offset: int
    post_raw: bytes

    @property
    def words(self):
        return struct.unpack("<8H",self.raw)

    @property
    def pre(self):
        return struct.unpack("<4f",self.pre_raw)

    @property
    def post(self):
        return struct.unpack("<4f",self.post_raw)


@dataclass(frozen=True)
class Contract:
    skeleton_sha256: str
    records: tuple[Record,...]

    def record(self,joint):
        matches = [record for record in self.records if record.joint==joint]
        require(len(matches)==1,"unknown/duplicate joint record")
        return matches[0]


def _expected_record(joint):
    op,_,_,_,_,offset,raw,pa,praw,pb,pbraw = SPECS[joint]
    return Record(joint,op,offset,bytes.fromhex(raw),pa,bytes.fromhex(praw),pb,bytes.fromhex(pbraw))


def validate_contract(contract):
    require(isinstance(contract,Contract) and contract.skeleton_sha256==scalar.SKELETON_SHA256,
            "pinned Nadine contract required")
    require(contract.records == tuple(_expected_record(j) for j in SPECS),
            "stale or mutated Euler descriptor/constants")


def load_contract(data):
    """Full unpacked Nadine pak + parsed operator identity + exact descriptors."""
    skeleton = source.load_skeleton(data,source.NADINE_SKELETON)
    require(hashlib.sha256(data).hexdigest()==scalar.SKELETON_SHA256,"Nadine asset SHA-256")
    base,ops = inputs.group_program(data,skeleton,0)
    records=[]
    for joint,spec in SPECS.items():
        opcode,ordinal,lane,operator,payload,offset,_,preoff,_,postoff,_ = spec
        op=ops[ordinal]
        require(op.opcode==opcode and op.payload==payload
                and skeleton.spec.raw_offset+op.relative==operator,"Euler operator identity")
        descriptor=base+op.payload[2]
        require(descriptor+16*lane==offset,"Euler descriptor lane identity")
        raw=bytes(data[offset:offset+16])
        words=struct.unpack("<8H",raw)
        constants=descriptor+16*op.payload[0]
        require(constants+words[2]==preoff and constants+words[3]==postoff,
                "Euler authored constant base/offset")
        records.append(Record(joint,opcode,offset,raw,preoff,bytes(data[preoff:preoff+16]),
                              postoff,bytes(data[postoff:postoff+16])))
    contract=Contract(hashlib.sha256(data).hexdigest(),tuple(records))
    validate_contract(contract)
    return contract


def decompose_local_q(contract,joint,local_q_xyzw):
    """Q24(mode0) or Q51(mode4) -> all xyz degrees; Q24 only scatters [1]."""
    validate_contract(contract)
    require(joint in (24,51),"only local Q24/Q51 decomposition")
    record=contract.record(joint)
    return framed_euler(local_q_xyzw,record.pre,record.post,record.words[0])


def compose_helper_q(contract,joint,angles_xyz_degrees):
    """Supplied angle bank values -> Q87/Q88. Does not source those angles."""
    validate_contract(contract)
    require(joint in (87,88),"only helper Q87/Q88 composition")
    record=contract.record(joint)
    return framed_quaternion(angles_xyz_degrees,record.pre,record.post)


def verify_native_image(image):
    """Read-only pe_disassemble_va.Image; never executable-memory mapping."""
    require(hashlib.sha256(image.data).hexdigest()==scalar.EXE_SHA256,"retail SHA-256")
    for address,(size,digest) in FUNCTIONS.items():
        require(hashlib.sha256(image.read(address,size)).hexdigest()==digest,
                f"Euler worker hash {address:X}")
    for address,target in ((0x143026E68,0x142194680),(0x143026E80,0x1421946D0)):
        require(struct.unpack("<Q",image.read(address,8))[0]==target,"Euler built-in dispatch slot")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skeleton",type=Path,default=ROOT/"reports/actor_packages/nadine-dlc-base.pak")
    parser.add_argument("--joint",type=int,choices=(24,51),required=True)
    parser.add_argument("--local-q",type=float,nargs=4,required=True)
    args=parser.parse_args()
    contract=load_contract(skin.PagedPak(args.skeleton).data)
    print(json.dumps({"classification":"STATIC_ANALYTIC_APPROXIMATION","model":MODEL,
        "retail_or_game_execution":False,"actual_pose_baseline":False,
        "joint":args.joint,"supplied_local_q":args.local_q,
        "euler_xyz_degrees":decompose_local_q(contract,args.joint,args.local_q),
        "evidence":EVIDENCE,"limits":["Binary64 analytic math, not native SIMD polynomial/float32 equivalence",
            "Input generation/validity remains caller-owned; no actual baseline supplied",
            "Only pinned op41 modes0/4 and op44 mode4; no all-mode or final surface claim"]},indent=2,allow_nan=False))


if __name__=="__main__":
    main()
