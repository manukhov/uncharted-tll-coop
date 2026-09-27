import dataclasses
import math
from pathlib import Path
import unittest

import p99_wrist_euler_audit as audit


IDENTITY=(0.,0.,0.,1.)


def axis_quaternion(axis,degrees):
    half=math.radians(degrees)*.5
    q=[0.,0.,0.,math.cos(half)]
    q[axis]=math.sin(half)
    return tuple(q)


def product(a,b):
    """Independent vector form for test controls, not the production kernel."""
    u,s=a[:3],a[3]
    v,t=b[:3],b[3]
    cross=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
    return tuple(s*v[i]+t*u[i]+cross[i] for i in range(3))+(s*t-sum(u[i]*v[i] for i in range(3)),)


def authored_euler(angles,mode):
    """Independent rotations in mathematical degrees; not native constants."""
    x,y,z=(axis_quaternion(i,v) for i,v in enumerate(angles))
    if mode==0:
        return product(product(y,x),z)
    if mode==4:
        return product(product(z,y),x)
    raise ValueError("fixture mode")


class EulerMathTests(unittest.TestCase):
    def assertRotationEqual(self,a,b,tolerance=1e-6):
        na=math.sqrt(sum(x*x for x in a))
        nb=math.sqrt(sum(x*x for x in b))
        error=min(max(abs(a[i]/na-sign*b[i]/nb) for i in range(4)) for sign in (-1,1))
        self.assertLess(error,tolerance)

    def test_independent_single_axis_controls(self):
        for mode in (0,4):
            for axis in range(3):
                for angle in (-60,-25,0,25,60):
                    expected=[0.,0.,0.]
                    expected[axis]=angle
                    got=audit.euler_degrees(axis_quaternion(axis,angle),mode)
                    for actual,want in zip(got,expected):
                        self.assertAlmostEqual(actual,want,places=5)

    def test_noncommuting_orders_and_xyz_output_slots(self):
        angles=(20.,-35.,50.)
        for mode in (0,4):
            q=authored_euler(angles,mode)
            got=audit.euler_degrees(q,mode)
            for actual,want in zip(got,angles):
                self.assertAlmostEqual(actual,want,places=5)
            wrong=audit.euler_degrees(q,4 if mode==0 else 0)
            self.assertGreater(max(abs(w-a) for w,a in zip(wrong,angles)),5)

    def test_mode4_composition_against_independent_product(self):
        angles=(27.,-41.,68.)
        self.assertRotationEqual(audit.quaternion_mode4(angles),authored_euler(angles,4))
        self.assertGreater(max(abs(x-y) for x,y in zip(audit.quaternion_mode4(angles),authored_euler(angles,0))),.1)

    def test_sign_invariance_and_rotation_roundtrip(self):
        for mode in (0,4):
            for angles in ((10,20,30),(-40,32,-71),(62,-49,107)):
                q=authored_euler(angles,mode)
                got=audit.euler_degrees(q,mode)
                self.assertEqual(got,audit.euler_degrees(tuple(-x for x in q),mode))
                self.assertRotationEqual(q,authored_euler(got,mode))

    def test_singular_modes_have_distinct_zero_output_axis(self):
        for angles,mode,want in (((90,20,30),0,(90,-10,0)),
                                 ((-90,20,30),0,(-90,50,0)),
                                 ((20,90,30),4,(0,90,10)),
                                 ((20,-90,30),4,(0,-90,50))):
            got=audit.euler_degrees(authored_euler(angles,mode),mode)
            for actual,expected in zip(got,want):
                self.assertAlmostEqual(actual,expected,places=4)
            self.assertRotationEqual(authored_euler(got,mode),authored_euler(angles,mode))

    def test_control_crosses_native_singular_threshold(self):
        for mode in (0,4):
            control=[]
            for s in (audit.SINGULAR_LIMIT-1e-7,audit.SINGULAR_LIMIT+1e-7):
                middle=math.degrees(math.asin(s))
                angles=(middle,20,30) if mode==0 else (20,middle,30)
                control.append(audit.euler_degrees(authored_euler(angles,mode),mode))
            forced_axis=2 if mode==0 else 0
            self.assertGreater(abs(control[0][forced_axis]),15)
            self.assertEqual(control[1][forced_axis],0.)

    def test_asin_input_clamp_and_no_silent_quaternion_normalization(self):
        # Native uses squares directly, then clamps asin, rather than normalizing Q.
        t=math.sqrt(1.0004/2)
        result=audit.euler_degrees((0,t,0,t),4)
        self.assertAlmostEqual(result[1],90,places=4)
        angles=(20,30,40)
        q=authored_euler(angles,4)
        slightly_scaled=tuple(v*math.sqrt(1.0004) for v in q)
        self.assertGreater(abs(audit.euler_degrees(slightly_scaled,4)[1]-30),.005)

    def test_authored_frame_order_and_conjugation(self):
        a,b=axis_quaternion(0,40),axis_quaternion(2,-25)
        local=product(product(a,authored_euler((15,-35,60),4)),b)
        got=audit.framed_euler(local,a,b,4)
        for actual,want in zip(got,(15,-35,60)):
            self.assertAlmostEqual(actual,want,places=5)
        composed=audit.framed_quaternion((15,-35,60),a,b)
        self.assertRotationEqual(composed,local)
        wrong=product(product(b,authored_euler((15,-35,60),4)),a)
        self.assertGreater(max(abs(x-y) for x,y in zip(composed,wrong)),.1)

    def test_framed_composition_retains_authored_norm(self):
        a=tuple(v*1.0001 for v in axis_quaternion(0,20))
        got=audit.framed_quaternion((15,-35,60),a,IDENTITY)
        self.assertAlmostEqual(math.sqrt(sum(x*x for x in got)),1.0001,places=12)

    def test_invalid_unreviewed_inputs_refused(self):
        for mode in (1,2,3,5,6,True):
            with self.assertRaises(ValueError):
                audit.euler_degrees(IDENTITY,mode)
        for q in ((0,0,0,0),(0,0,0,2),(math.nan,0,0,1)):
            with self.assertRaises(ValueError):
                audit.euler_degrees(q,4)
        for angles in ((1,2),(0,math.nan,0),(0,math.inf,0)):
            with self.assertRaises(ValueError):
                audit.quaternion_mode4(angles)


class AssetContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path=audit.ROOT/"reports/actor_packages/nadine-dlc-base.pak"
        if not path.exists():
            raise unittest.SkipTest("installed Nadine skeleton unavailable")
        cls.data=audit.skin.PagedPak(path).data
        cls.contract=audit.load_contract(cls.data)

    def test_pinned_modes_sources_destinations(self):
        self.assertEqual(self.contract.record(24).words,(0,0,32,16,0x490,0xFFFF,0x316,0xFFFF))
        self.assertEqual(self.contract.record(51).words,(4,0,64,16,0x9A0,0x2B2,0x31E,0x382))
        self.assertEqual(self.contract.record(87).words,(4,0,0,0,0x1060,0x2EA,0x356,0x3BA))
        self.assertEqual(self.contract.record(88).words,(4,0,64,16,0x1090,0x2EE,0x35A,0x3BE))

    def test_reference_controls_are_not_actual_baselines(self):
        for joint in (24,51):
            record=self.contract.record(joint)
            got=audit.decompose_local_q(self.contract,joint,record.pre)
            self.assertLess(max(abs(v) for v in got),1e-7)
        with self.assertRaises(TypeError):
            audit.decompose_local_q(self.contract,24)
        with self.assertRaises(TypeError):
            audit.compose_helper_q(self.contract,87)

    def test_exported_q24_y_is_not_mathematical_middle_x(self):
        record=self.contract.record(24)
        q=product(record.pre,authored_euler((10,25,40),0))
        got=audit.decompose_local_q(self.contract,24,q)
        self.assertAlmostEqual(got[1],25,places=4)
        self.assertAlmostEqual(got[0],10,places=4)
        self.assertGreater(abs(got[0]-got[1]),14)

    def test_q51_angles_and_helper_composition(self):
        record=self.contract.record(51)
        angles=(20,-30,45)
        q=product(record.pre,authored_euler(angles,4))
        got=audit.decompose_local_q(self.contract,51,q)
        for actual,want in zip(got,angles):
            self.assertAlmostEqual(actual,want,places=4)
        q87=audit.compose_helper_q(self.contract,87,angles)
        q88=audit.compose_helper_q(self.contract,88,angles)
        expected=authored_euler(angles,4)
        self.assertLess(max(abs(x-y) for x,y in zip(q87,expected)),1e-7)
        self.assertLess(max(abs(x-y) for x,y in zip(q88,expected)),1e-7)

    def test_q88_tiny_authored_rotation_not_rounded_to_identity(self):
        q=audit.compose_helper_q(self.contract,88,(0,0,0))
        self.assertEqual(q,self.contract.record(88).pre)
        self.assertNotEqual(q,IDENTITY)

    def test_full_asset_mutation_rejected(self):
        for offset in (0,0x5A190,0x5A260,0x5B9C0,0x5BC90):
            changed=bytearray(self.data)
            changed[offset]^=1
            with self.assertRaises(ValueError):
                audit.load_contract(changed)

    def test_contract_record_constant_and_mode_mutations_rejected(self):
        for index,record in enumerate(self.contract.records):
            for field in ("raw","pre_raw","post_raw"):
                changed=bytearray(getattr(record,field))
                changed[0]^=1
                records=list(self.contract.records)
                records[index]=dataclasses.replace(record,**{field:bytes(changed)})
                contract=dataclasses.replace(self.contract,records=tuple(records))
                with self.assertRaisesRegex(ValueError,"mutated"):
                    audit.decompose_local_q(contract,24,IDENTITY)
        with self.assertRaises(ValueError):
            audit.compose_helper_q(dataclasses.replace(self.contract,skeleton_sha256="0"*64),87,(0,0,0))
        with self.assertRaises(ValueError):
            audit.decompose_local_q(self.contract,26,IDENTITY)
        with self.assertRaises(ValueError):
            audit.compose_helper_q(self.contract,85,(0,0,0))

    def test_source_worker_hashes_and_dispatch_read_only(self):
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
