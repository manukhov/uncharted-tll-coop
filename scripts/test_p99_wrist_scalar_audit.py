import dataclasses
import math
from pathlib import Path
import unittest

import p99_wrist_scalar_audit as audit


IDENTITY = (0.,0.,0.,1.)


def rotation(axis, degrees):
    """Independent mathematical axis-angle fixture (not audit degree constants)."""
    half = math.radians(degrees)*.5
    value = [0.,0.,0.,math.cos(half)]
    value[axis] = math.sin(half)
    return tuple(value)


def hamilton(a,b):
    """Independent scalar fixture product using vector cross/dot definition."""
    v,w = a[:3],a[3]
    u,s = b[:3],b[3]
    cross = (v[1]*u[2]-v[2]*u[1],v[2]*u[0]-v[0]*u[2],v[0]*u[1]-v[1]*u[0])
    return tuple(w*u[i]+s*v[i]+cross[i] for i in range(3))+(w*s-sum(v[i]*u[i] for i in range(3)),)


class ScalarMathTests(unittest.TestCase):
    def test_signed_known_axis_angles(self):
        for axis in range(3):
            for angle in (-179,-90,-30,0,30,90,179):
                with self.subTest(axis=axis,angle=angle):
                    self.assertAlmostEqual(audit.signed_twist_degrees(rotation(axis,angle),IDENTITY,axis),angle,places=5)

    def test_quaternion_sign_and_hemisphere(self):
        q = rotation(2,270)
        self.assertAlmostEqual(audit.signed_twist_degrees(q,IDENTITY,2),-90,places=5)
        self.assertEqual(audit.signed_twist_degrees(q,IDENTITY,2),
                         audit.signed_twist_degrees(tuple(-x for x in q),IDENTITY,2))

    def test_swing_removed_with_nonidentity_reference_and_noncommuting_products(self):
        # Reference turns local Z onto world X; swing is world Y, local twist Z.
        reference = rotation(1,90)
        q = hamilton(hamilton(rotation(1,40),reference),rotation(2,-35))
        self.assertAlmostEqual(audit.signed_twist_degrees(q,reference,2),-35,places=5)
        wrong_order = hamilton(rotation(2,-35),hamilton(rotation(1,40),reference))
        self.assertGreater(abs(audit.signed_twist_degrees(wrong_order,reference,2)+35),10)

    def test_pure_swing_has_zero_twist(self):
        self.assertAlmostEqual(audit.signed_twist_degrees(rotation(0,70),IDENTITY,2),0,places=6)

    def test_antiparallel_native_fixed_fallback(self):
        # q=180 around X maps Z to -Z; native fallback is Y180, not shortest X180.
        # Residual is Z180. The second case distinguishes hard-coded Y from Z.
        self.assertAlmostEqual(audit.signed_twist_degrees((1.,0.,0.,0.),IDENTITY,2),180,places=4)
        self.assertEqual(audit.signed_twist_degrees((0.,1.,0.,0.),IDENTITY,2),0)

    def test_swing_threshold_is_not_generic_swing_twist(self):
        # Dot still >= native cutoff: swing deliberately stays identity.
        q = rotation(0,.05)
        self.assertAlmostEqual(audit.signed_twist_degrees(q,IDENTITY,2),.05,places=6)
        self.assertAlmostEqual(audit.signed_twist_degrees(rotation(0,.1),IDENTITY,2),0,places=6)

    def test_wrap_strict_endpoints_and_turns(self):
        self.assertEqual(audit.wrap_degrees(audit.PI),audit.PI*audit.RAD_TO_DEG)
        self.assertEqual(audit.wrap_degrees(-audit.PI),-audit.PI*audit.RAD_TO_DEG)
        self.assertEqual(audit.wrap_degrees(audit.TAU),0.)
        self.assertAlmostEqual(audit.wrap_degrees(audit.PI+.01),(-audit.PI+.01)*audit.RAD_TO_DEG)
        self.assertAlmostEqual(audit.wrap_degrees(-audit.PI-.01),(audit.PI-.01)*audit.RAD_TO_DEG)
        self.assertAlmostEqual(audit.wrap_degrees(2*audit.TAU+.2),.2*audit.RAD_TO_DEG)

    def test_clamp_endpoints_linear_and_hard_step(self):
        for angle,want in ((-1,1),(10,1),(30,.5),(50,0),(100,0)):
            self.assertEqual(audit.angular_falloff(angle,10,50),want)
        for end in (5,10):
            self.assertEqual(audit.angular_falloff(10,10,end),1)
            self.assertEqual(audit.angular_falloff(10.001,10,end),0)
            self.assertEqual(audit.angular_falloff(9.999,10,end),1)

    def test_weight_known_axis_angle(self):
        # X basis rotates away from X through Z angle30: midpoint of [0,60].
        self.assertAlmostEqual(audit.angular_weight(rotation(2,30),IDENTITY,0,0,60),.5,places=7)
        self.assertEqual(audit.angular_weight(rotation(2,90),IDENTITY,0,0,60),0)
        self.assertEqual(audit.angular_weight(IDENTITY,IDENTITY,0,0,60),1)

    def test_optional_rotation_direction_and_axis(self):
        # Optional +90 about Z rotates authored X to current Y, not -Y.
        kwargs = dict(reference_q=IDENTITY,reference_axis=2,optional_degrees=90)
        self.assertAlmostEqual(audit.angular_weight(rotation(2,90),IDENTITY,0,0,60,**kwargs),1,places=7)
        self.assertEqual(audit.angular_weight(rotation(2,-90),IDENTITY,0,0,60,**kwargs),0)
        # Reference Q=Y90 makes reference Z equal world X; now rotate Y -> Z.
        kwargs = dict(reference_q=rotation(1,90),reference_axis=2,optional_degrees=90)
        self.assertAlmostEqual(audit.angular_weight(rotation(0,90),IDENTITY,1,0,60,**kwargs),1,places=7)

    def test_invalid_inputs_refused_without_normalizing(self):
        for q in ((0,0,0,0),(0,0,0,2),(math.nan,0,0,1),(0,0,1)):
            with self.assertRaises(ValueError):
                audit.signed_twist_degrees(q,IDENTITY,2)
        with self.assertRaises(ValueError):
            audit.signed_twist_degrees(IDENTITY,IDENTITY,3)
        with self.assertRaises(ValueError):
            audit.angular_weight(IDENTITY,IDENTITY,0,0,90,optional_degrees=20)
        with self.assertRaises(ValueError):
            audit.angular_falloff(math.nan,0,90)
        with self.assertRaises(ValueError):
            audit.wrap_degrees(math.inf)


class PinnedDescriptorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = audit.ROOT/"reports/actor_packages/nadine-dlc-base.pak"
        if not path.exists():
            raise unittest.SkipTest("installed Nadine skeleton unavailable")
        cls.data = audit.skin.PagedPak(path).data
        cls.descriptors = audit.load_descriptors(cls.data)

    def test_exact_record_contract(self):
        self.assertEqual(set(self.descriptors),{0x400,0x3E4,0x3EC,0x404,0x408})
        self.assertEqual(self.descriptors[0x400].axis,2)
        for aux,end in ((0x3E4,100),(0x3EC,90),(0x404,70),(0x408,70)):
            self.assertEqual(self.descriptors[aux].limits,(0,end))
            self.assertEqual(self.descriptors[aux].reference_axis,2)
            self.assertEqual(self.descriptors[aux].reference_q,self.descriptors[0x400].target_q)

    def test_authored_reference_is_synthetic_only_not_actual_baseline(self):
        q = self.descriptors[0x400].target_q
        values = audit.evaluate_weights(q,self.descriptors)
        self.assertAlmostEqual(values[0x400],0,places=6)
        self.assertTrue(all(0 <= values[k] <= 1 for k in values if k != 0x400))
        changed = audit.evaluate_weights(hamilton(q,rotation(0,40)),self.descriptors)
        self.assertGreater(max(abs(changed[k]-values[k]) for k in (0x3E4,0x3EC,0x404,0x408)),.2)
        # Supplying a local twist changes AUX400 independently of finger grip.
        twist = audit.evaluate_weights(hamilton(q,rotation(2,45)),self.descriptors)
        self.assertAlmostEqual(twist[0x400],45,places=4)

    def test_same_model_differs_for_independent_local_q26(self):
        # Explicit synthetic fixture; there is deliberately no argument default.
        a = audit.evaluate_weights(IDENTITY,self.descriptors)
        b = audit.evaluate_weights(rotation(0,60),self.descriptors)
        self.assertNotEqual(a,b)
        with self.assertRaises(TypeError):
            audit.evaluate_weights(descriptors=self.descriptors)

    def test_whole_asset_and_descriptors_fail_closed(self):
        changed = bytearray(self.data)
        changed[0] ^= 1
        with self.assertRaisesRegex(ValueError,"SHA-256"):
            audit.load_descriptors(changed)
        for key in self.descriptors:
            changed_descriptors = dict(self.descriptors)
            record = changed_descriptors[key]
            raw = bytearray(record.raw)
            raw[52] ^= 1
            changed_descriptors[key] = dataclasses.replace(record,raw=bytes(raw))
            with self.assertRaisesRegex(ValueError,"descriptor"):
                audit.evaluate_weights(IDENTITY,changed_descriptors)
        with self.assertRaisesRegex(ValueError,"five exact"):
            audit.evaluate_weights(IDENTITY,{})
        wrong = dict(self.descriptors)
        wrong[0x400] = dataclasses.replace(wrong[0x400],file_offset=0x5A064)
        with self.assertRaisesRegex(ValueError,"descriptor"):
            audit.evaluate_weights(IDENTITY,wrong)

    def test_native_source_hash_and_table_read_only(self):
        # This reader never maps executable memory or runs a retail worker.
        from pe_disassemble_va import Image
        path = Path(r"C:\UNCHARTED Legacy of Thieves Collection (2016-2017-2022)\Uncharted Legacy of Thieves Collection\tll.exe")
        if not path.exists():
            self.skipTest("installed retail executable unavailable")
        image = Image(str(path))
        try:
            audit.verify_native_image(image)
        finally:
            image.pe.close()


if __name__ == "__main__":
    unittest.main()
