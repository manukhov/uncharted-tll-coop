import dataclasses
import math
from pathlib import Path
import struct
import unittest

import p99_wrist_g6_euler_audit as audit


IDENTITY=(0.,0.,0.,1.)


def axis_q(axis,degrees):
    half=math.radians(degrees)*.5
    result=[0.,0.,0.,math.cos(half)]
    result[axis]=math.sin(half)
    return tuple(result)


def product(a,b):
    """Independent cross/dot quaternion fixture, mathematical degrees elsewhere."""
    u,s=a[:3],a[3]
    v,t=b[:3],b[3]
    cross=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
    return tuple(s*v[i]+t*u[i]+cross[i] for i in range(3))+(s*t-sum(u[i]*v[i] for i in range(3)),)


def fixture(angles,mode):
    x,y,z=(axis_q(i,v) for i,v in enumerate(angles))
    if mode==3:
        return product(product(x,z),y)
    if mode==4:
        return product(product(z,y),x)
    raise ValueError("test mode")


class Mode3MathTests(unittest.TestCase):
    def assertQuaternionClose(self,a,b,tol=1e-7):
        self.assertLess(max(abs(x-y) for x,y in zip(a,b)),tol)

    def test_independent_axes_and_zero(self):
        self.assertEqual(audit.quaternion_mode3((0,0,0)),IDENTITY)
        for axis in range(3):
            for angle in (-150,-60,30,90,180):
                angles=[0.,0.,0.]
                angles[axis]=angle
                self.assertQuaternionClose(audit.quaternion_mode3(angles),axis_q(axis,angle))

    def test_noncommuting_xyz_order_and_mode_distinction(self):
        angles=(35,-50,75)
        actual=audit.quaternion_mode3(angles)
        self.assertQuaternionClose(actual,fixture(angles,3))
        self.assertGreater(max(abs(x-y) for x,y in zip(actual,fixture(angles,4))),.3)
        x,y,z=(axis_q(i,v) for i,v in enumerate(angles))
        reversed_order=product(product(y,z),x)
        self.assertGreater(max(abs(x-y) for x,y in zip(actual,reversed_order)),.3)

    def test_known_two_axis_control_exposes_signs(self):
        # Qx90*Qz90 has negative Y; mode4 Qz90*Qx90 has positive Y.
        q=audit.quaternion_mode3((90,0,90))
        for actual,want in zip(q,(.5,-.5,.5,.5)):
            self.assertAlmostEqual(actual,want,places=7)

    def test_authored_pre_post_noncommuting_order(self):
        angles=(25,-40,65)
        pre,post=axis_q(1,55),axis_q(2,-20)
        for mode in (3,4):
            actual=audit.framed_quaternion(angles,pre,post,mode)
            expected=product(product(pre,fixture(angles,mode)),post)
            self.assertQuaternionClose(actual,expected)
            reversed_frame=product(product(post,fixture(angles,mode)),pre)
            self.assertGreater(max(abs(x-y) for x,y in zip(actual,reversed_frame)),.2)

    def test_no_authored_normalization(self):
        pre=tuple(v*1.0002 for v in axis_q(2,30))
        q=audit.framed_quaternion((20,40,60),pre,IDENTITY,3)
        self.assertAlmostEqual(math.sqrt(sum(v*v for v in q)),1.0002,places=12)

    def test_invalid_modes_and_inputs(self):
        for mode in (0,1,2,5,True):
            with self.assertRaises(ValueError):
                audit.framed_quaternion((0,0,0),IDENTITY,IDENTITY,mode)
        for angles in ((0,0),(0,math.nan,0),(0,0,math.inf)):
            with self.assertRaises(ValueError):
                audit.quaternion_mode3(angles)


class G6DescriptorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path=audit.ROOT/"reports/actor_packages/nadine-dlc-base.pak"
        if not path.exists():
            raise unittest.SkipTest("installed Nadine skeleton unavailable")
        cls.data=audit.skin.PagedPak(path).data
        cls.contract=audit.load_contract(cls.data)

    def test_exact_g6_sources_and_destinations(self):
        self.assertEqual(self.contract.program_base,0x60E50)
        self.assertEqual(self.contract.records(49)[0].words,(4,0,0x30,0x10,0x493,0x7A,0x14A,0x21A))
        wanted={31:(4,0x5E0,(0xEE,0x1BE,0x28E)),32:(4,0x610,(0xF2,0x1C2,0x292)),
                33:(3,0x640,(0xF6,0x1C6,0x296)),34:(3,0x670,(0xFA,0x1CA,0x29A)),
                51:(3,0x9A0,(0x12E,0x1FE,0x2CE))}
        for joint,(mode,dest,angles) in wanted.items():
            words=self.contract.records(joint)[0].words
            self.assertEqual((words[0],words[4],words[5:8]),(mode,dest,angles))

    def test_q49_supplied_frame_and_sign(self):
        record=self.contract.records(49)[0]
        angles=(25,-35,60)
        q=product(product(record.pre,fixture(angles,4)),record.post)
        got=audit.decompose49(self.contract,q)
        for actual,want in zip(got,angles):
            self.assertAlmostEqual(actual,want,places=4)
        self.assertEqual(got,audit.decompose49(self.contract,tuple(-x for x in q)))

    def test_q49_existing_singular_mode4_branch(self):
        record=self.contract.records(49)[0]
        q=product(record.pre,fixture((20,90,30),4))
        result=audit.decompose49(self.contract,q)
        self.assertEqual(result[0],0.)
        self.assertAlmostEqual(result[2],10,places=4)
        # Authored A is not exactly unit; native also does not normalize it.
        self.assertGreater(result[1],89.9)

    def test_all_local_compositions_keep_authored_quaternions(self):
        angles=(20,-30,45)
        for local in (31,32,33,34,51):
            record=self.contract.records(local)[0]
            expected=product(product(record.pre,fixture(angles,record.words[0])),record.post)
            actual=audit.compose_local(self.contract,local,angles)
            self.assertLess(max(abs(x-y) for x,y in zip(expected,actual)),1e-7)
        # Wrist corrective helpers cannot silently discard their nonidentity A.
        q33=audit.compose_local(self.contract,33,(0,0,0))
        self.assertEqual(q33,self.contract.records(33)[0].pre)
        self.assertGreater(max(abs(x-y) for x,y in zip(q33,IDENTITY)),.1)

    def test_tiny_authored_components_not_rounded(self):
        for local in (32,51):
            q=audit.compose_local(self.contract,local,(0,0,0))
            self.assertEqual(q,self.contract.records(local)[0].pre)
            self.assertNotEqual(q,IDENTITY)

    def test_three_local51_stores_are_identical_overwrites(self):
        records=self.contract.records(51)
        self.assertEqual(len(records),3)
        self.assertEqual(tuple(r.file_offset for r in records),(0x664C0,0x664D0,0x664E0))
        self.assertEqual(len({r.raw for r in records}),1)
        self.assertEqual(len({(r.pre_raw,r.post_raw) for r in records}),1)
        angles=(30,40,50)
        one=audit.framed_quaternion(angles,records[0].pre,records[0].post,3)
        self.assertEqual(audit.compose_local(self.contract,51,angles),one)
        accumulated=product(product(one,one),one)
        self.assertGreater(max(abs(x-y) for x,y in zip(one,accumulated)),.3)

    def test_full_asset_mutations_rejected(self):
        for offset in (0,0x65D70,0x65E00,0x664C0,0x664D0,0x664E0,0x66500):
            data=bytearray(self.data)
            data[offset]^=1
            with self.assertRaises(ValueError):
                audit.load_contract(data)

    def test_contract_repeats_and_constants_mutations_rejected(self):
        for index,entry in enumerate(self.contract.entries):
            for field in ("raw","pre_raw","post_raw"):
                raw=bytearray(getattr(entry.record,field))
                raw[0]^=1
                record=dataclasses.replace(entry.record,**{field:bytes(raw)})
                entries=list(self.contract.entries)
                entries[index]=dataclasses.replace(entry,record=record)
                mutated=dataclasses.replace(self.contract,entries=tuple(entries))
                with self.assertRaisesRegex(ValueError,"mutated"):
                    audit.compose_local(mutated,51,(0,0,0))
        for entries in (self.contract.entries[:-1],self.contract.entries[::-1]):
            with self.assertRaises(ValueError):
                audit.compose_local(dataclasses.replace(self.contract,entries=entries),51,(0,0,0))

    def test_new_vector_or_scalar_overlap_writer_rejected(self):
        skeleton=audit.source.load_skeleton(self.data,audit.source.NADINE_SKELETON)
        base,ops=audit.inputs.group_program(self.data,skeleton,6)
        audit.prove_quaternion_writers(self.data,base,ops)
        # Redirect unrelated op44 lane0 over Q31; overwrite inventory must fail.
        data=bytearray(self.data)
        struct.pack_into("<H",data,base+ops[14].payload[2]+8,0x5E0)
        with self.assertRaisesRegex(ValueError,"writer"):
            audit.prove_quaternion_writers(data,base,ops)
        # A single scalar component overwrite also matters, not only float4 stores.
        self.assertEqual(ops[3].opcode,47)
        data=bytearray(self.data)
        struct.pack_into("<H",data,base+ops[3].payload[2]+2,0x5E4)
        with self.assertRaisesRegex(ValueError,"writer"):
            audit.prove_quaternion_writers(data,base,ops)

    def test_no_implicit_input_or_global_local_joint_confusion(self):
        with self.assertRaises(TypeError):
            audit.decompose49(self.contract)
        with self.assertRaises(TypeError):
            audit.compose_local(self.contract,31)
        for joint in (49,535,536,537,554,True):
            with self.assertRaises(ValueError):
                audit.compose_local(self.contract,joint,(0,0,0))

    def test_native_workers_and_mode3_selector_read_only(self):
        from pe_disassemble_va import Image
        path=Path(r"C:\UNCHARTED Legacy of Thieves Collection (2016-2017-2022)\Uncharted Legacy of Thieves Collection\tll.exe")
        if not path.exists():
            self.skipTest("installed retail executable unavailable")
        image=Image(str(path))
        try:
            audit.verify_native_image(image)
        finally:
            image.pe.close()


if __name__=="__main__":
    unittest.main()
