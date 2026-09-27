import dataclasses
import math
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

import p99_wrist_rotation_audit as rotation


I = ((1.,0.,0.),(0.,1.,0.),(0.,0.,1.))
UNIT = (0.,0.,0.,1.)
FRAME = (I,(0.,0.,0.),(1.,1.,1.))


def matrix(q):
    """Independent vector-rotation oracle, not the implementation's product."""
    x,y,z,w = q
    return ((1-2*(y*y+z*z),2*(x*y-w*z),2*(x*z+w*y)),
            (2*(x*y+w*z),1-2*(x*x+z*z),2*(y*z-w*x)),
            (2*(x*z-w*y),2*(y*z+w*x),1-2*(x*x+y*y)))


def matmul(a,b):
    return tuple(tuple(sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def columns(a):
    return tuple(tuple(a[i][j] for i in range(3)) for j in range(3))


class VectorAssertions:
    def assertClose(self,a,b,tolerance=1e-11):
        self.assertEqual(len(a),len(b))
        self.assertLessEqual(max(abs(x-y) for x,y in zip(a,b)),tolerance)

    def assertMatrixClose(self,a,b):
        for actual,expected in zip(a,b):
            self.assertClose(actual,expected)


class AnalyticTests(VectorAssertions,unittest.TestCase):
    def test_identity(self):
        self.assertEqual(rotation.shoulder_rotation(FRAME),(UNIT,'nonnegative_trace_w'))

    def test_three_180_degree_branches(self):
        for index,axis in enumerate('xyz'):
            q = tuple(float(i==index) for i in range(4))
            actual,branch = rotation.shoulder_rotation((matrix(q),FRAME[1],FRAME[2]))
            self.assertClose(actual,q)
            self.assertEqual(branch,'negative_trace_'+axis)

    def test_diagonal_ties_prioritize_x_then_y(self):
        #180-degree rotations with equal largest entries. Positive dominant
        #component exposes extraction hemisphere, not merely matrix equality.
        for q,branch in (((-math.sqrt(.5),math.sqrt(.5),0.,0.),'negative_trace_x'),
                         ((0.,-math.sqrt(.5),math.sqrt(.5),0.),'negative_trace_y')):
            actual,got = rotation.quaternion_from_columns(columns(matrix(q)))
            self.assertEqual(got,branch)
            self.assertClose(actual,tuple(-v for v in q))

    def test_exact_zero_trace_uses_w_branch(self):
        #120 degrees about (1,1,1): a cyclic coordinate permutation.
        a = ((0.,0.,1.),(1.,0.,0.),(0.,1.,0.))
        actual,branch = rotation.shoulder_rotation((a,FRAME[1],FRAME[2]))
        self.assertEqual(branch,'nonnegative_trace_w')
        self.assertClose(actual,(.5,.5,.5,.5))

    def test_translation_third_column_and_separate_scale_are_unused(self):
        a = ((0.,-1.,17.),(1.,0.,-8.),(0.,0.,0.))
        actual,_ = rotation.shoulder_rotation((a,(100.,-200.,300.),(-3.,0.,9.)))
        self.assertClose(actual,(0.,0.,math.sqrt(.5),math.sqrt(.5)))

    def test_positive_column_scale_and_shear_are_removed(self):
        q = (.2,-.3,.1,math.sqrt(.86))
        a = matrix(q)
        distorted = tuple((3*row[0],4*row[1]+7*row[0],row[2]) for row in a)
        actual,_ = rotation.shoulder_rotation((distorted,FRAME[1],FRAME[2]))
        self.assertMatrixClose(matrix(actual),a)
        #The raw basis is not a rotation; direct matrix-to-quaternion differs.
        wrong,_ = rotation.quaternion_from_columns(columns(distorted))
        self.assertGreater(max(abs(actual[i]-wrong[i]) for i in range(4)),.1)

    def test_negative_column_scale_is_not_discarded(self):
        a = ((-2.,0.,0.),(0.,3.,0.),(0.,0.,1.))
        actual,branch = rotation.shoulder_rotation((a,FRAME[1],FRAME[2]))
        self.assertClose(actual,(0.,1.,0.,0.))
        self.assertEqual(branch,'negative_trace_y')

    def test_degenerate_first_or_parallel_columns_rejected(self):
        for a in (((0.,1.,0.),(0.,0.,0.),(0.,0.,1.)),
                  ((1.,2.,0.),(2.,4.,0.),(3.,6.,1.))):
            with self.assertRaisesRegex(ValueError,'degeneracy'):
                rotation.shoulder_rotation((a,FRAME[1],FRAME[2]))

    def test_nonfinite_and_malformed_workspace_rejected(self):
        with self.assertRaises(ValueError):
            rotation.shoulder_rotation((I,(0.,math.nan,0.),FRAME[2]))
        with self.assertRaises(ValueError):
            rotation.shoulder_rotation((I[:2],FRAME[1],FRAME[2]))


class PinnedTests(VectorAssertions,unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = rotation.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        if not cls.path.exists():
            raise unittest.SkipTest('pinned local Nadine asset unavailable')
        cls.data = cls.path.read_bytes()
        cls.contract = rotation.load_contract(cls.data)
        cls.skeleton = rotation.source.load_skeleton(cls.data,rotation.source.NADINE_SKELETON)

    def test_noncommuting_q24_is_right_of_shoulder(self):
        #Shoulder Z90 and local X60: independent rotation-matrix multiplication.
        shoulder = ((0.,-1.,0.),(1.,0.,0.),(0.,0.,1.))
        local = (.5,0.,0.,math.sqrt(.75))
        out = rotation.evaluate_q85((shoulder,FRAME[1],FRAME[2]),local,self.contract)
        self.assertMatrixClose(matrix(out),matmul(shoulder,matrix(local)))
        wrong = matmul(matrix(local),shoulder)
        self.assertGreater(max(abs(matrix(out)[i][j]-wrong[i][j]) for i in range(3) for j in range(3)),.5)

    def test_auxiliary_normalizes_scaled_local_q(self):
        q = (.5,0.,0.,math.sqrt(.75))
        for gain in (2.,7.,.25):
            self.assertClose(rotation.evaluate_q85(FRAME,tuple(gain*x for x in q),self.contract),q)

    def test_auxiliary_preserves_quaternion_sign_not_canonical_w(self):
        q = (.5,0.,0.,math.sqrt(.75))
        self.assertClose(rotation.evaluate_q85(FRAME,tuple(-3*x for x in q),self.contract),tuple(-x for x in q))

    def test_missing_source_q_not_filled_from_reference(self):
        with self.assertRaises(TypeError):
            rotation.evaluate_q85(FRAME,contract=self.contract)
        with self.assertRaisesRegex(ValueError,'degeneracy'):
            rotation.evaluate_q85(FRAME,(0.,0.,0.,0.),self.contract)

    def test_contract_is_immutable_and_wrong_contract_rejected(self):
        with self.assertRaises(dataclasses.FrozenInstanceError):
            self.contract.scalar_weight = 2.
        with self.assertRaisesRegex(ValueError,'contract'):
            rotation.evaluate_q85(FRAME,UNIT,dataclasses.replace(self.contract,scalar_weight=2.))

    def test_full_asset_mutation_fails_closed(self):
        altered = bytearray(self.data)
        altered[-1] ^= 1
        with self.assertRaisesRegex(ValueError,'SHA-256'):
            rotation.load_contract(altered)

    def test_exact_descriptor_guard_independent_of_full_asset_hash(self):
        altered = bytearray(self.data)
        #Remove Q source flag8: old Q85 would become a real operand.
        struct.pack_into('<H',altered,0x5BE80+8+4,0x490)
        with patch.object(rotation.source,'load_skeleton',return_value=self.skeleton):
            with self.assertRaisesRegex(ValueError,'descriptor'):
                rotation.load_contract(altered)

    def test_initializer_weight_guard_independent_of_full_asset_hash(self):
        altered = bytearray(self.data)
        struct.pack_into('<f',altered,0x5983C,2.)
        with patch.object(rotation.source,'load_skeleton',return_value=self.skeleton):
            with self.assertRaisesRegex(ValueError,'authored weight'):
                rotation.load_contract(altered)

    def test_later_scalar_writer_invalidates_initial_weight(self):
        original = rotation.program.writes
        def extra(data,base,op):
            return original(data,base,op)+([(0xBE,4)] if op.ordinal==10 else [])
        with patch.object(rotation.program,'writes',side_effect=extra):
            with self.assertRaisesRegex(ValueError,'bank2\\+BC'):
                rotation.load_contract(self.data)

    def test_identity_reference_and_entry_q_writers_rejected(self):
        original = rotation.program.writes
        for encoded in (1,0x490):
            def extra(data,base,op):
                return original(data,base,op)+([(encoded,16)] if op.ordinal==10 else [])
            with patch.object(rotation.program,'writes',side_effect=extra):
                with self.assertRaisesRegex(ValueError,'writer inventory'):
                    rotation.load_contract(self.data)

    def test_report_does_not_invent_pose_or_runtime_admission(self):
        report = rotation.audit(self.path)
        self.assertFalse(report['source_or_runtime_admission'])
        self.assertFalse(report['actual_pose_replayed'])
        self.assertFalse(report['retail_or_game_execution'])
        self.assertIn('normalize',report['formula'])

    def test_native_functions_dispatch_and_constants_read_only(self):
        from pe_disassemble_va import Image
        path = Path(r'C:\UNCHARTED Legacy of Thieves Collection (2016-2017-2022)\Uncharted Legacy of Thieves Collection\tll.exe')
        if not path.exists():
            self.skipTest('installed retail executable unavailable')
        image = Image(str(path))
        try:
            rotation.verify_native_image(image)
        finally:
            image.pe.close()


if __name__ == '__main__':
    unittest.main()
