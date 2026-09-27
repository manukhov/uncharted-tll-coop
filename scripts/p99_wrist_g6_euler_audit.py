#!/usr/bin/env python3
"""STATIC analytic Euler slice for Nadine g6 helpers, not an actual pose.

Pinned op41 mode4 reads explicitly supplied g0 LOCAL Q49 copied to SHARED493.
Pinned op44 mode3/4 compose local31/32/33/34/51 from supplied XYZ-degree triples.
Mode3=Qx*Qz*Qy; mode4=Qz*Qy*Qx. Both use authored A*Q*B without normalization.
Binary64 sin/cos/asin/atan2 replace native SIMD approximations; there is no
measured native error bound. This module neither supplies missing arm poses,
executes native code, nor admits absolute skin/contact or a runtime test.
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
import p99_wrist_euler_audit as euler
import p99_wrist_helper_inputs_audit as inputs
import p99_wrist_scalar_audit as scalar
import pak_skin_influence_audit as skin

ROOT=Path(__file__).resolve().parents[1]
MODEL="nadine-g6-euler-analytic-modes3-4-v1"
IDENTITY=euler.IDENTITY_HEX
WRIST="2f71d73dcecfbd3df4b6b8bbaa777d3f"
TINY="0000fe25000080240000b0a40000803f"
PROGRAM_BASE=0x60E50

# opcode, ordinal, lane, joint (49 is g0; all op44 joints are g6-local),
# operator address, payload, descriptor address/bytes, A address/bytes, B address/bytes.
SPECS=(
    (41,7,6,49,0x60E92,(12,3,0x4EC0),0x65D70,"040000003000100093047a004a011a02",
     0x65E00,"2c1ac9be60a7693db6de11bf5a36383f",0x65DE0,IDENTITY),
    (44,11,16,31,0x60EB4,(32,3,0x54A0),0x663F0,"0400000000000000e005ee00be018e02",
     0x664F0,IDENTITY,0x664F0,IDENTITY),
    (44,14,8,32,0x60ECC,(12,3,0x5860),0x66730,"04000000600010001006f200c2019202",
     0x667D0,"000000a40000c024000090a40000803f",0x66780,IDENTITY),
    (44,11,17,33,0x60EB4,(32,3,0x54A0),0x66400,"03000000100000004006f600c6019602",
     0x66500,WRIST,0x664F0,IDENTITY),
    (44,11,18,34,0x60EB4,(32,3,0x54A0),0x66410,"03000000100000007006fa00ca019a02",
     0x66500,WRIST,0x664F0,IDENTITY),
    (44,11,29,51,0x60EB4,(32,3,0x54A0),0x664C0,"03000000d0000000a0092e01fe01ce02",
     0x665C0,TINY,0x664F0,IDENTITY),
    (44,11,30,51,0x60EB4,(32,3,0x54A0),0x664D0,"03000000d0000000a0092e01fe01ce02",
     0x665C0,TINY,0x664F0,IDENTITY),
    (44,11,31,51,0x60EB4,(32,3,0x54A0),0x664E0,"03000000d0000000a0092e01fe01ce02",
     0x665C0,TINY,0x664F0,IDENTITY),
)
EVIDENCE={
    "mode3_select":"14218C25B..C2D0 mode3 mask via dword3 at142BEADD0; all other mode masks clear",
    "trig":"14218C2D4..C30E XYZ degrees*float32(deg2rad)*.5; sincos1421803E0@C313/C325/C336",
    "mode3_order":"14218C33B..C496 mode3 first SoA quaternion=(sx*cz,-sx*sz,cx*sz,cx*cz)=Qx*Qz; second@C49A..C4B9=(0,sy,0,cy)",
    "multiply":"14218C4BE ->1421817F0, Hamilton first*second, hence (Qx*Qz)*Qy; no normalization",
    "mode4_order":"Same previously pinned worker mode4=(Qz*Qy)*Qx, not interchangeable with mode3",
    "frame":"14218C530@C7D1..C8C6 left authoredA; C8CB..C9D7 right authoredB; four lanes read before stores C9D7..CA00",
    "decompose49":"op41@60E92 lane6 descriptor65D70: SHARED493 -> AUX78/148/218, mode4; conj(A)*Q49*conj(B), existing singular branches retained",
    "repeats":"op44@60EB4 lanes29/30/31 write same JP9A0 from same AUX12C/1FC/2CC and same A/B; lane31 wins, not accumulation",
    "ownership":"No source-pose/default acquisition; caller supplies same-generation Q49 and scalar-consumer XYZ angles",
    "alternatives_rejected":"Mode4 reused as mode3, reversed products, Euler-order output reordering, normalization and accumulated repeated stores contradict native masks/stores",
}


def require(condition,message):
    if not condition:
        raise ValueError(message)


def quaternion_mode3(angles_xyz):
    """Analytic native14218C200 mode3, XYZW, Qx(x)*Qz(z)*Qy(y)."""
    x,y,z=(a*euler.DEG_TO_RAD*.5 for a in euler.finite3(angles_xyz))
    sx,sy,sz=math.sin(x),math.sin(y),math.sin(z)
    cx,cy,cz=math.cos(x),math.cos(y),math.cos(z)
    return (sx*cz*cy-cx*sz*sy,cx*cz*sy-sx*sz*cy,
            sx*cz*sy+cx*sz*cy,cx*cz*cy+sx*sz*sy)


def framed_quaternion(angles_xyz,pre,post,mode):
    """Only reviewed composition modes3/4; A*Q*B without normalizing."""
    require(type(mode) is int and mode in (3,4),"only g6 Euler composition modes3/4")
    q=quaternion_mode3(angles_xyz) if mode==3 else euler.quaternion_mode4(angles_xyz)
    a,b=scalar.quaternion(pre),scalar.quaternion(post)
    return scalar.multiply(scalar.multiply(a,q),b)


@dataclass(frozen=True)
class Entry:
    ordinal:int
    lane:int
    operator:int
    payload:tuple[int,...]
    record:euler.Record


@dataclass(frozen=True)
class Contract:
    skeleton_sha256:str
    program_base:int
    entries:tuple[Entry,...]

    def records(self,joint):
        return tuple(entry.record for entry in self.entries if entry.record.joint==joint)


def _canonical(spec):
    opcode,ordinal,lane,joint,operator,payload,offset,raw,pa,praw,pb,braw=spec
    return Entry(ordinal,lane,operator,payload,euler.Record(joint,opcode,offset,
        bytes.fromhex(raw),pa,bytes.fromhex(praw),pb,bytes.fromhex(braw)))


def validate_contract(contract):
    require(isinstance(contract,Contract) and contract.skeleton_sha256==scalar.SKELETON_SHA256
            and contract.program_base==PROGRAM_BASE,"pinned g6 Euler contract required")
    require(contract.entries==tuple(_canonical(spec) for spec in SPECS),
            "mutated g6 Euler descriptor/constant/repeat")


def prove_quaternion_writers(data,base,ops):
    """All g6 operators, scalar-cell overlap check, including repeated stores."""
    expected={31:((11,44),),32:((14,44),),33:((11,44),),34:((11,44),),
              51:((11,44),)*3}
    for joint,wanted in expected.items():
        cells=inputs.cells(48*joint+16,16)
        actual={cell:[] for cell in cells}
        for op in ops:
            for encoded,width in inputs.program.writes(data,base,op):
                touched=set(inputs.cells(encoded,width))
                for cell in actual.keys() & touched:
                    actual[cell].append((op.ordinal,op.opcode))
        require(all(tuple(writes)==wanted for writes in actual.values()),
                f"g6 localQ{joint} overlapping/later writer contract")
    return expected


def load_contract(data):
    skeleton=source.load_skeleton(data,source.NADINE_SKELETON)
    require(hashlib.sha256(data).hexdigest()==scalar.SKELETON_SHA256,"Nadine full-asset SHA-256")
    base,ops=inputs.group_program(data,skeleton,6)
    require(base==PROGRAM_BASE and len(ops)==16,"g6 program identity")
    prove_quaternion_writers(data,base,ops)
    entries=[]
    for spec in SPECS:
        opcode,ordinal,lane,joint,operator,payload,offset,_,pa,_,pb,_=spec
        op=ops[ordinal]
        require((op.opcode,op.payload,skeleton.spec.raw_offset+op.relative)==
                (opcode,payload,operator),"g6 Euler operator identity")
        descriptor=base+op.payload[2]
        require(descriptor+16*lane==offset,"g6 Euler lane identity")
        raw=bytes(data[offset:offset+16])
        words=struct.unpack("<8H",raw)
        constants=descriptor+16*op.payload[0]
        require((constants+words[2],constants+words[3])==(pa,pb),"g6 Euler constant offsets")
        record=euler.Record(joint,opcode,offset,raw,pa,bytes(data[pa:pa+16]),pb,bytes(data[pb:pb+16]))
        entries.append(Entry(ordinal,lane,operator,payload,record))
    result=Contract(hashlib.sha256(data).hexdigest(),base,tuple(entries))
    validate_contract(result)
    return result


def decompose49(contract,q49):
    """Supplied LOCAL Q49, copied to shared493, -> XYZ-degree output tuple."""
    validate_contract(contract)
    record,=contract.records(49)
    return euler.framed_euler(q49,record.pre,record.post,record.words[0])


def compose_local(contract,local_joint,angles_xyz):
    """Supplied XYZ angles -> g6-local31/32/33/34/51 Q; never an input default."""
    validate_contract(contract)
    require(type(local_joint) is int and local_joint in (31,32,33,34,51),"reviewed g6 local joint required")
    records=contract.records(local_joint)
    # For51 all3 byte-identical semantic records are pinned; the final store wins.
    record=records[-1]
    return framed_quaternion(angles_xyz,record.pre,record.post,record.words[0])


def verify_native_image(image):
    """Full retail and workers/table byte guards; read-only Image, never MappedImage."""
    euler.verify_native_image(image)
    require(struct.unpack("<4I",image.read(0x142BEADD0,16))==(3,3,3,3),"native mode3 selector")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skeleton",type=Path,default=ROOT/"reports/actor_packages/nadine-dlc-base.pak")
    args=parser.parse_args()
    contract=load_contract(skin.PagedPak(args.skeleton).data)
    print(json.dumps({"classification":"STATIC_ANALYTIC_CONTRACT","model":MODEL,
        "actual_pose_replayed":False,"retail_or_game_execution":False,
        "required_operands":["explicit g0 LOCAL Q49", "explicit g6 consumer XYZ-degree angles for local31/32/33/34/51"],
        "modes":{str(j):contract.records(j)[0].words[0] for j in (31,32,33,34,51)},
        "repeated_local51_stores":len(contract.records(51)),"evidence":EVIDENCE,
        "limits":["No actual pose/default/validity admission", "Binary64 analytic math, not native instruction equivalence",
                  "Only reached Euler producers; scalar bank and matrix propagation remain separate"]},indent=2,allow_nan=False))


if __name__=="__main__":
    main()
