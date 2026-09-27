#!/usr/bin/env python3
"""Bounded pure numeric kernels and staged Nadine g0 helper85..88 replay.

NOT a complete native/helper replay: this staged API keeps op50 Q85, op44
Q87/Q88 and op41's Euler24 Y output as explicit supplied intermediates.
p99_wrist_g0_replay closes these analytic stages from required source operands;
neither API chooses a profile/reference/live baseline. The companion
p99_wrist_g6_replay closes the reached g6 helper slice, not the whole program.
The historical name Euler24-middle means output slot1(Y),
NOT the middle rotation in mode0's Y-X-Z composition (which is X).

Closed here: op52 constants, op47 arity1/2 clamped piecewise cubics (including
SIMD read-before-write and repeated destinations), op49's pinned translation
case, op51's pinned mode3 aim/up construction for Q86, and op40 propagation.
Scalar double arithmetic uses exact sqrt/division, not native rcp/rsqrt or
instruction-order float32 replay. Branch-near-threshold equality is unproven.
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
import p99_profile_matrix_audit as matrix

ROOT = Path(__file__).resolve().parents[1]
NATIVE_AIM_EPSILON = struct.unpack('<f',bytes.fromhex('32000034'))[0]
GROUP0 = 0x53E0


def require(condition,message):
    if not condition:
        raise ValueError(message)


def finite(values,length,name):
    values = tuple(float(x) for x in values)
    require(len(values)==length and all(math.isfinite(x) for x in values),name)
    return values


def vadd(a,b):
    return tuple(x+y for x,y in zip(a,b))


def vsub(a,b):
    return tuple(x-y for x,y in zip(a,b))


def dot(a,b):
    return sum(x*y for x,y in zip(a,b))


def scale(a,value):
    return tuple(x*value for x in a)


def cross(a,b):
    return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])


def multiply(a,b):
    x,y,z,w=a
    X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)


def inverse3(a):
    c = (cross(a[1],a[2]),cross(a[2],a[0]),cross(a[0],a[1]))
    determinant=dot(a[0],c[0])
    require(math.isfinite(determinant) and abs(determinant)>1e-12,'singular parent affine basis')
    return tuple(tuple(c[j][i]/determinant for j in range(3)) for i in range(3))


def matvec(a,v):
    return tuple(dot(row,v) for row in a)


def workspace(value):
    a,t,s=value
    a=tuple(finite(row,3,'workspace basis') for row in a)
    require(len(a)==3,'workspace basis dimensions')
    return a,finite(t,3,'workspace translation'),finite(s,3,'workspace scale')


def affine_basis(frame):
    a,_,s=workspace(frame)
    return tuple(tuple(a[i][j]*s[j] for j in range(3)) for i in range(3))


def quaternion_from_columns(columns):
    """14218F3B0's trace/largest-diagonal quaternion extraction, scalar model."""
    a=tuple(tuple(columns[j][i] for j in range(3)) for i in range(3))
    trace=sum(a[i][i] for i in range(3))
    if trace>0:
        r=math.sqrt(trace+1)
        c=.5/r
        return ((a[2][1]-a[1][2])*c,(a[0][2]-a[2][0])*c,
                (a[1][0]-a[0][1])*c,.5*r)
    # Native compares strictly; ties pass to Y or finally Z.
    i=0 if a[0][0]>a[1][1] and a[0][0]>a[2][2] else (1 if a[1][1]>a[2][2] else 2)
    j,k=(i+1)%3,(i+2)%3
    r=math.sqrt(max(0.,1+a[i][i]-a[j][j]-a[k][k]))
    require(r>0,'invalid quaternion basis')
    c=.5/r
    out=[0.]*4
    out[i]=.5*r
    out[j]=(a[i][j]+a[j][i])*c
    out[k]=(a[i][k]+a[k][i])*c
    out[3]=(a[k][j]-a[j][k])*c
    return tuple(out)


def aim_mode3(parent,target_world,local_translation,up_frame,up_local,post_q):
    """Closed math of 1421902D0 for the pinned mode3 Q86 lane.

    target_world is the primary op51 weighted position float4, NOT a quaternion.
    The native destination+10 is the next JP vector (local T86). The up vector
    is a direction (no translation) transformed by another workspace matrix.
    Parent local scale participates in the inverse and up-frame scale applies.
    """
    parent=workspace(parent)
    up_frame=workspace(up_frame)
    target=finite(target_world,4,'aim target float4')
    local_t=finite(local_translation,3,'aim local translation')
    up_local=finite(up_local,3,'aim up direction')
    post=finite(post_q,4,'authored post quaternion')
    inv=inverse3(affine_basis(parent))
    target_local=matvec(inv,vsub(target[:3],scale(parent[1],target[3])))
    aim=vsub(target_local,scale(local_t,target[3]))
    aim2=dot(aim,aim)
    require(math.isfinite(aim2),'nonfinite aim norm')
    unit=scale(aim,1/math.sqrt(aim2)) if aim2 else (0.,0.,0.)
    up=matvec(inv,matvec(affine_basis(up_frame),up_local))
    projection=dot(up,unit)
    up2=dot(up,up)
    orthogonal2=up2-projection*projection
    require(all(math.isfinite(x) for x in (projection,up2,orthogonal2)), 'nonfinite up projection')
    # 1421906AF..6C4, JB to the full orthogonal-basis path.
    full_basis=(up2*NATIVE_AIM_EPSILON*aim2 < aim2*orthogonal2)
    if full_basis:
        y=scale(vsub(up,scale(unit,projection)),1/math.sqrt(orthogonal2))
        z=cross(unit,y)
        rotation=quaternion_from_columns((unit,y,z))
        branch='orthogonal_up_basis'
    else:
        # 1421906C6..722. Antiparallel -X produces a zero quaternion in native;
        # fail closed instead of inventing an arbitrary fallback axis.
        q=(0.,-unit[2],unit[1],unit[0]+1)
        q2=dot(q,q)
        require(q2>1e-24,'native shortest-arc antiparallel degeneracy')
        rotation=scale(q,1/math.sqrt(q2))
        branch='shortest_arc_fallback'
    return finite(multiply(rotation,post),4,'nonfinite aim quaternion'),branch


def authored_constants(data,base,op):
    require(op.opcode==52 and op.payload[2]==3 and op.payload[3]&3==0,'op52 contract')
    vectors,scalars,_,relative=op.payload
    desc=base+relative
    tail=desc+16*(vectors+scalars)
    out={}
    for index in range(4*vectors):
        dest,origin=struct.unpack_from('<HH',data,desc+4*index)
        for component,value in enumerate(struct.unpack_from('<4f',data,tail+(origin&~15))):
            out[dest&3,(dest&~15)+4*component]=value
    for index in range(4*scalars):
        dest,origin=struct.unpack_from('<HH',data,desc+16*vectors+4*index)
        out[dest&3,dest&~3]=struct.unpack_from('<f',data,tail+(origin&~3))[0]
    return out


@dataclass(frozen=True)
class ScalarCurve:
    source:int
    destination:int
    lower:float
    upper:float
    knot:float|None
    coefficients:tuple[tuple[float,...],...]

    def contribution(self,value):
        require(math.isfinite(value),'nonfinite curve input')
        value=min(max(value,self.lower),self.upper)
        segment=int(self.knot is not None and self.knot<value)
        origin=self.lower if not segment else self.knot
        x=value-origin
        a,b,c,d=self.coefficients[segment]
        return ((a*x+b)*x+c)*x+d


def scalar_curves(data,base,op):
    require(op.opcode==47 and op.payload[1]==3 and op.payload[2]&3==0
            and op.payload[0]%4==0 and op.payload[3] in (1,2),'reviewed scalar curve contract')
    count,_,relative,arity=op.payload
    desc=base+relative
    out=[]
    for index in range(count):
        origin,dest=struct.unpack_from('<HH',data,desc+4*index)
        lower=struct.unpack_from('<f',data,desc+4*count+4*index)[0]
        upper=struct.unpack_from('<f',data,desc+8*count+4*index)[0]
        knot=struct.unpack_from('<f',data,desc+12*count+4*index)[0] if arity==2 else None
        coeff=desc+(arity+2)*4*count+(index//4)*64*arity
        values=tuple(tuple(struct.unpack_from('<f',data,coeff+degree*16*arity+16*segment+4*(index%4))[0]
                           for degree in range(4)) for segment in range(arity))
        require(all(math.isfinite(x) for x in (lower,upper,*[c for row in values for c in row]))
                and lower<=upper and (knot is None or lower<=knot<=upper),'curve coefficient domain')
        out.append(ScalarCurve(origin,dest,lower,upper,knot,values))
    return tuple(out)


def apply_curves(curves,bank,selected):
    """Selected-output slice only; unselected outputs are not evaluated."""
    require(len(curves)%4==0,'curve SIMD padding')
    for start in range(0,len(curves),4):
        pending=[]
        for curve in curves[start:start+4]:
            destination=(curve.destination&3,curve.destination&~3)
            if destination not in selected:
                continue
            origin=(curve.source&3,curve.source&~3)
            require(origin in bank and destination in bank,'missing explicit scalar operand')
            pending.append((destination,bank[destination]+curve.contribution(bank[origin])))
        for destination,value in pending:
            bank[destination]=value


def prove_staged_writer_inventory(data,base,ops):
    """Check every operator for writes to assumptions used by the staged math.

    Exact per-cell ordinal/opcode sets retain the initial writer and every
    allowed later writer. Vector spans and scalar destinations are both tested;
    looking only for exact destination-address equality would miss overlaps.
    """
    expected={(2,offset):((0,52),) for offset in
              (0xBC,0xC0,0x144,0x1C0,0x23C,0x74,0x84,0x94)}
    for joint in (85,86,87,88):
        for component in (*range(4),*range(8,12)):
            writers=[(0,52)]
            if joint==85 and component>=8:
                writers.append((42,49))
            elif joint==86 and component in (8,10):
                writers.append((10,47))
            elif joint==88 and component in (0,1,2,8,9,10):
                writers.append((1,47))
            expected[0,48*joint+4*component]=tuple(writers)
    # Explicit caller inputs: JP51 is an untouched parameter, while the supplied
    # AUX314 is op41's Y output slot, after ordinal8 and before consumer47@10.
    expected.update({(0,48*51+4*i):() for i in range(12)})
    expected[2,0x314]=((0,52),(8,41))
    actual={cell:[] for cell in expected}
    for op in ops:
        touched=set()
        for encoded,width in program.writes(data,base,op):
            offset=encoded & (-16 if width>=16 else -4)
            touched.update((encoded&3,offset+i) for i in range(0,width,4))
        for cell in expected.keys() & touched:
            actual[cell].append((op.ordinal,op.opcode))
    for cell,wanted in expected.items():
        name=f"{('JP','W','AUX','SHARED')[cell[0]]}+{cell[1]:X}"
        require(tuple(actual[cell])==wanted,f"staged writer inventory changed at {name}")
    return {f"{('JP','W','AUX','SHARED')[bank]}+{off:X}":list(writers)
            for (bank,off),writers in expected.items()}


def load_contract(data):
    """Pin all authored data before providing numerical coefficients/operands."""
    skeleton=source.load_skeleton(data,source.NADINE_SKELETON)
    base=skeleton.spec.raw_offset+GROUP0
    ops=program.operators(data,skeleton.spec.raw_offset,GROUP0)
    require((ops[37].opcode,ops[42].opcode,ops[47].opcode)==(50,49,51),'g0 helper producers')
    writer_inventory=prove_staged_writer_inventory(data,base,ops)
    constants=authored_constants(data,base,ops[0])
    # The T85 simplification also depends on the exact source/weight tuple,
    # replacement flag and normalize-by-W phase, not only writer identity.
    trans_desc=base+ops[42].payload[2]
    require(ops[42].payload==(4,3,0x32E0) and
            struct.unpack_from('<HH',data,trans_desc+4)==(0x649,0x1010) and
            struct.unpack_from('<4H',data,trans_desc+24)==(0x146,0x1C2,0x23E,0xBE) and
            struct.unpack_from('<III',data,trans_desc+48)==(8,32,0) and
            struct.unpack_from('<8H',data,trans_desc+64)==(0xC80,)+(0x1010,)*7,
            'g0 T85 exact translation lane')
    # Pin the logical third lane of op51; authored quaternion tail is SoA.
    desc=base+ops[47].payload[2]
    require(ops[47].payload==(4,3,0x3480),'g0 Q86 op51 identity')
    primary=struct.unpack_from('<8H',data,desc)
    weights=struct.unpack_from('<4H',data,desc+16)
    header=desc+32
    count,matrices,coords,modes=struct.unpack_from('<4I',data,header)
    pairs=struct.unpack_from('<8H',data,header+16)
    require(primary[4:6]==(0x6C9,0x1030) and weights[2]==0xC2 and
            (count,matrices,coords,modes)==(1,0x60,0x70,0xA0) and
            pairs[4:6]==(0x1030,0x1581) and
            program.u16(data,header+matrices+4)==0x641 and
            struct.unpack_from('<3H',data,header+coords+12)==(0x76,0x86,0x96) and
            program.u16(data,header+modes+4)==3,'g0 Q86 exact aim lane')
    post=tuple(struct.unpack_from('<f',data,header+32+16*i+8)[0] for i in range(4))
    require(post==(0.,-0.7071067690849304,0.,0.7071067690849304),'Q86 authored post quaternion')
    require(tuple(constants[2,x] for x in (0xBC,0xC0,0x144,0x1C0,0x23C,0x74,0x84,0x94))==
            (1.,1.,0.,0.,0.,0.,1.,0.),'g0 authored translation/up constants')
    expected_parents={85:0x9,86:0x1589,87:0x15C9,88:0x1609}
    parents={}
    for op in ops:
        if op.opcode==40:
            for local,parent in program.propagation_records(data,base,op):
                if local in expected_parents:
                    require(parent==expected_parents[local],'g0 helper op40 parent')
                    parents[local]=parent
    require(parents==expected_parents,'g0 helper parent coverage')
    return {'constants':constants,'post_q86':post,'writer_inventory':writer_inventory,
            'curves1':scalar_curves(data,base,ops[1]),
            'curves10':scalar_curves(data,base,ops[10]),
            'missing_native_numerical_stages':{
                'q85':'op50@594F6: native14218F660/142190910, matrix22 + localQ24',
                'euler24_middle':'op41@59404 lane3: native14218BC40/14218B780, localQ24 Y output slot',
                'q87':'op44@594C6 lane2: native14218C530/14218C200, mode4 + AUX2E8/354/3B8',
                'q88':'op44@594A6 lane7: native14218C530/14218C200, mode4 + AUX2EC/358/3BC',
                'group6':'Outside this staged API; see p99_wrist_g6_replay for the conditional helper slice'}}


def replay_g0_staged(contract,*,elbow24,wrist26,jp51,q85,q87,q88,euler24_middle):
    """Numerically replay the closed tail, conditional on ALL explicit operands.

    Q85/Q87/Q88 and Euler24 Y output are supplied procedural intermediates; this
    function does not establish their value, source validity, or generation.
    These inputs MUST NOT be filled from an arbitrary bind/profile by default.
    """
    elbow=workspace(elbow24)
    wrist=workspace(wrist26)
    row51=finite(jp51,12,'explicit JP51')
    q85=finite(q85,4,'explicit op50 Q85')
    q87=finite(q87,4,'explicit op44 Q87')
    q88=finite(q88,4,'explicit op44 Q88')
    require(all(dot(q,q)>1e-12 for q in (q85,q87,q88)),'zero supplied quaternion')
    require(math.isfinite(euler24_middle),'explicit op41 Euler24-middle')
    bank=dict(contract['constants'])
    bank.update({(0,48*51+4*i):value for i,value in enumerate(row51)})
    bank[2,0x314]=float(euler24_middle)
    selected={(0,48*j+4*i) for j in (85,86,87,88) for i in (*range(3),*range(8,11))}
    apply_curves(contract['curves1'],bank,selected)
    apply_curves(contract['curves10'],bank,selected)
    rows={j:[0.]*12 for j in (85,86,87,88)}
    for joint,row in rows.items():
        for i in (*range(4),*range(8,12)):
            require((0,48*joint+4*i) in bank,'missing authored S/T initialization')
            row[i]=bank[0,48*joint+4*i]
    # Pinned op49@5951E uses zero local point, weight1, parent matrix24; its
    # normalize-by-W auxiliary phase therefore preserves exactly position24.
    rows[85][4:8]=q85
    rows[85][8:11]=elbow[1]
    m85=matrix.propagate(matrix.IDENTITY,rows[85],True)
    q86,branch=aim_mode3(m85,(*wrist[1],1.),rows[86][8:11],elbow,(0.,1.,0.),contract['post_q86'])
    rows[86][4:8]=q86
    rows[87][4:8]=q87
    rows[88][4:8]=q88
    matrices={85:m85}
    for joint in (86,87,88):
        matrices[joint]=matrix.propagate(matrices[joint-1],rows[joint],True)
    return {'classification':'STATIC_CONDITIONAL_STAGED_REPLAY',
            'complete_group0_native_replay':False,'complete_group6_native_replay':False,
            'source_or_runtime_admission':False,'q86_branch':branch,
            'jointparams':{j:tuple(row) for j,row in rows.items()},'matrices':matrices,
            'supplied_unreplayed_intermediates':['q85','q87','q88','euler24_middle']}


def audit(path):
    """Report the closed contract without manufacturing a source-pose baseline."""
    data=Path(path).read_bytes()
    contract=load_contract(data)
    return {
        'classification':'STATIC_PARTIAL_NUMERIC_CONTRACT',
        'skeleton_sha256':hashlib.sha256(data).hexdigest(),
        'retail_or_game_execution':False,'actual_pose_replayed':False,
        'complete_group0_native_replay':False,'complete_group6_native_replay':False,
        'source_or_runtime_admission':False,
        'closed_kernels':{
            '52':'142194AB0/1421897C0: authored constant tail only',
            '47':'142192B10/142192D30: clamped piecewise cubic + old destination; SIMD snapshot',
            '49_pinned_T85':'5951E/14218D570/14218D800: zero local point, weight1, matrix24 translation',
            '51_pinned_Q86':'59546/14218D2D0/1421902D0: matrix26 target, parent85, elbow24 up',
            '40':'142189FB0: native scale-separated workspace; 85->86->87->88'},
        'q86_exact_authored_lane':{
            'primary_source':'W+6C0 matrix26','primary_weight':'AUX+C0 = authored1',
            'destination':'JP+1030 Q86 (temporary target position before auxiliary stage)',
            'local_translation_read':'JP+1040 T86',
            'reference_parent':'W+1580 matrix85',
            'up_matrix':'W+640 matrix24',
            'up_direction':'AUX74/84/94 = authored(0,1,0)',
            'mode':3,'post_quaternion_xyzw':contract['post_q86'],
            'post_quaternion_layout':'SoA transposed by1421903A0..3E7, lane2',
            'orthogonal_branch_epsilon':NATIVE_AIM_EPSILON},
        'staged_assumption_writer_inventory':contract['writer_inventory'],
        'required_explicit_source_inputs':{
            'elbow24':'same-source final g0 workspace matrix24; not determined by sparse palm/prop rows',
            'wrist26':'same-source final g0 workspace matrix26; frame equality required',
            'jp51':'g0 LOCAL parameter51: S/T read by op47@593CA fromJP990/9B0; Q read by op41@59404 lane5',
            'q85':'op50@594F6 result; operands W5C0 matrix22 r_shoulder and LOCAL Q24 atJP490, identityW0',
            'euler24_middle':'AUX314 from op41@59404 lane3, mode0, LOCAL Q24 atJP490; Y output, not middle rotation X',
            'q87':'op44@594C6 lane2 result, mode4; AUX2E8/354/3B8 plus authored quaternions',
            'q88':'op44@594A6 lane7 result, mode4; AUX2EC/358/3BC plus authored quaternions'},
        'remaining_native_numerical_stages':contract['missing_native_numerical_stages'],
        'limits':[
            'Staged replay requires supplied Q85/Q87/Q88 and Euler24-middle; it is not a complete helper solution',
            'p99_wrist_g0_replay provides the integrated analytic slice from explicit shoulder22 and LOCAL JP24/26/51',
            'No default authored/profile/current values are selected for missing source parameters',
            'Scalar double math is not native float32 rsqrt/rcp/libm instruction equivalence',
            'Singular parent and antiparallel fallback degeneracy fail closed; no invented replacement axis',
            'No helper baseline, mixed-skin contact, visual improvement, hook safety or runtime admission is proved']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skeleton',type=Path,default=ROOT/'reports/actor_packages/nadine-dlc-base.pak')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    result=audit(args.skeleton)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2,allow_nan=False))


if __name__=='__main__':
    main()
