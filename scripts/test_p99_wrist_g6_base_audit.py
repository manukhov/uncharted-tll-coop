import dataclasses
import math
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

import p99_wrist_g6_base_audit as audit


I = ((1.,0.,0.),(0.,1.,0.),(0.,0.,1.))
FRAME = (I,(0.,0.,0.),(1.,1.,1.))
Q = (0.,0.,0.,1.)


def matmul(a,b):
    return tuple(tuple(sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


class Base553Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = audit.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        if not cls.path.exists():
            raise unittest.SkipTest('pinned Nadine asset unavailable')
        cls.data = cls.path.read_bytes()
        cls.skeleton = audit.source.load_skeleton(cls.data,audit.source.NADINE_SKELETON)
        cls.contract = audit.load_contract(cls.data)

    def replay(self,elbow=FRAME,wrist=FRAME,local=Q):
        return audit.replay_base553(self.contract,elbow24=elbow,wrist26=wrist,local_q26=local)

    def assertClose(self,a,b,tol=1e-11):
        self.assertEqual(len(a),len(b))
        self.assertLessEqual(max(abs(x-y) for x,y in zip(a,b)),tol)

    def assertMatrixClose(self,a,b):
        for x,y in zip(a,b):
            self.assertClose(x,y)

    def test_identity_control_preserves_proven_homogeneous_components(self):
        result = self.replay()
        self.assertEqual(result['jointparams'],(1.,1.,1.,1.,*Q,0.,0.,0.,1.))
        self.assertEqual(result['matrix'],FRAME)
        self.assertFalse(result['native_workspace_padding_serialized'])

    def test_noncommuting_elbow_and_local_q26_order(self):
        #Elbow Z90 * local X60; oracle is independent matrix multiplication.
        z90 = ((0.,-1.,0.),(1.,0.,0.),(0.,0.,1.))
        x60 = ((1.,0.,0.),(0.,.5,-math.sqrt(.75)),(0.,math.sqrt(.75),.5))
        local = (.5,0.,0.,math.sqrt(.75))
        result = self.replay((z90,(4.,5.,6.),(2.,3.,4.)),FRAME,local)
        self.assertMatrixClose(result['matrix'][0],matmul(z90,x60))
        wrong = matmul(x60,z90)
        self.assertGreater(max(abs(result['matrix'][0][i][j]-wrong[i][j])
                               for i in range(3) for j in range(3)),.5)

    def test_translation_comes_only_from_wrist_not_elbow_or_orientation(self):
        elbow = (I,(300.,-10.,99.),(2.,3.,4.))
        wrist = (((7.,2.,1.),(0.,0.,-2.),(0.,0.,0.)),(1.,2.,3.),(0.,-9.,4.))
        out = self.replay(elbow,wrist)
        self.assertEqual(out['jointparams'][8:],(1.,2.,3.,1.))
        self.assertEqual(out['matrix'][1],(1.,2.,3.))
        self.assertEqual(out['matrix'][2],(1.,1.,1.))

    def test_elbow_separate_scale_and_third_column_do_not_leak_into_q(self):
        altered = (((1.,0.,99.),(0.,1.,-3.),(0.,0.,0.)),(9.,8.,7.),(-3.,0.,6.))
        self.assertEqual(self.replay(altered)['jointparams'][4:8],Q)

    def test_positive_column_scale_and_shear_removed(self):
        basis = ((0.,-1.,0.),(1.,0.,0.),(0.,0.,1.))
        distorted = tuple((3*r[0],4*r[1]+9*r[0],r[2]) for r in basis)
        out = self.replay((distorted,FRAME[1],FRAME[2]))
        self.assertMatrixClose(out['matrix'][0],basis)

    def test_auxiliary_q_normalization_and_sign_preservation(self):
        local = (.5,0.,0.,math.sqrt(.75))
        positive = self.replay(local=tuple(5*x for x in local))
        negative = self.replay(local=tuple(-7*x for x in local))
        self.assertClose(positive['jointparams'][4:8],local)
        self.assertClose(negative['jointparams'][4:8],tuple(-x for x in local))
        self.assertMatrixClose(positive['matrix'][0],negative['matrix'][0])

    def test_global_rigid_transform_covariance_with_coherent_inputs(self):
        #Synthetic coherent inputs: elbowI and localX90, arbitrary wristT.
        local = (math.sqrt(.5),0.,0.,math.sqrt(.5))
        wrist = (((1.,0.,0.),(0.,0.,-1.),(0.,1.,0.)),(2.,3.,4.),(1.,1.,1.))
        baseline = self.replay(FRAME,wrist,local)['matrix']
        turn = ((0.,-1.,0.),(1.,0.,0.),(0.,0.,1.))
        offset = (10.,20.,30.)
        moved_t = tuple(sum(turn[i][j]*wrist[1][j] for j in range(3))+offset[i] for i in range(3))
        moved = self.replay((turn,offset,(1.,1.,1.)),
                            (matmul(turn,wrist[0]),moved_t,(1.,1.,1.)),local)['matrix']
        self.assertMatrixClose(moved[0],matmul(turn,baseline[0]))
        self.assertEqual(moved[1],moved_t)

    def test_no_default_inputs_or_default_source_admission(self):
        with self.assertRaises(TypeError):
            audit.replay_base553(self.contract)
        result = self.replay()
        self.assertFalse(result['actual_pose_replayed'])
        self.assertFalse(result['source_or_runtime_admission'])
        self.assertFalse(result['complete_group6_native_replay'])

    def test_degenerate_and_nonfinite_inputs_fail_closed(self):
        for local in ((0.,)*4,(math.nan,0.,0.,1.)):
            with self.assertRaises(ValueError):
                self.replay(local=local)
        with self.assertRaises(ValueError):
            self.replay((((0.,1.,0.),(0.,0.,1.),(0.,0.,0.)),FRAME[1],FRAME[2]))

    def test_frozen_contract_and_altered_weight_rejected(self):
        with self.assertRaises(dataclasses.FrozenInstanceError):
            self.contract.weight = 2.
        with self.assertRaises(ValueError):
            audit.replay_base553(dataclasses.replace(self.contract,weight=2.),
                                 elbow24=FRAME,wrist26=FRAME,local_q26=Q)

    def test_full_asset_hash_guard(self):
        data = bytearray(self.data)
        data[-1] ^= 1
        with self.assertRaisesRegex(ValueError,'SHA-256'):
            audit.load_contract(data)

    def test_both_descriptor_guards_independent_of_full_asset_hash(self):
        for offset in (0x61490+12,0x614E0+24,0x614E0+144+60):
            data = bytearray(self.data)
            data[offset] ^= 8
            with patch.object(audit.source,'load_skeleton',return_value=self.skeleton):
                with self.assertRaisesRegex(ValueError,'descriptor bytes'):
                    audit.load_contract(data)

    def test_authored_initializer_mutation_rejected(self):
        #Changing the shared constant's W changes both S.w and weight1.
        data = bytearray(self.data)
        struct.pack_into('<f',data,0x6138C,2.)
        with patch.object(audit.source,'load_skeleton',return_value=self.skeleton):
            with self.assertRaisesRegex(ValueError,'authored S/T/weight'):
                audit.load_contract(data)

    def test_extra_scalar_vector_and_workspace_writers_rejected(self):
        original = audit.program.writes
        for extra in ((0x2E,4),(0x3E,4),(0x960,16),(0x970,16),(0x980,16),(0xCC1,64),(1,64)):
            def changed(data,base,op):
                result = original(data,base,op)
                return result+[extra] if base==audit.GROUP6_BASE and op.ordinal==4 else result
            with patch.object(audit.program,'writes',side_effect=changed):
                with self.assertRaisesRegex(ValueError,'writer inventory'):
                    audit.load_contract(self.data)

    def test_shared_q_and_matrix_mapping_guards(self):
        original = audit.inputs.export_origins
        for cell in ((3,0x250),(3,0xED0),(3,0xF50)):
            changed = original(self.data,self.skeleton)
            changed[cell] = (0,49,53,(0,0))
            with patch.object(audit.inputs,'export_origins',return_value=changed):
                with self.assertRaisesRegex(ValueError,'source mapping'):
                    audit.load_contract(self.data)

    def test_later_g0_write_invalidates_export(self):
        original = audit.program.writes
        def changed(data,base,op):
            result = original(data,base,op)
            return result+[(0x253,16)] if base==0x593C0 and op.ordinal==50 else result
        with patch.object(audit.program,'writes',side_effect=changed):
            with self.assertRaisesRegex(ValueError,'last writer'):
                audit.load_contract(self.data)

    def test_intervening_shared_overwrite_rejected(self):
        original = audit.program.writes
        def changed(data,base,op):
            result = original(data,base,op)
            return result+[(0xED3,16)] if base==0x5CD70 and op.ordinal==0 else result
        with patch.object(audit.program,'writes',side_effect=changed):
            with self.assertRaisesRegex(ValueError,'group1 overwrites'):
                audit.load_contract(self.data)

    def test_native_parent_not_guessed_from_bind_table(self):
        data = bytearray(self.data)
        struct.pack_into('<H',data,0x665D0+14,0xCC9)
        with patch.object(audit.source,'load_skeleton',return_value=self.skeleton):
            with self.assertRaisesRegex(ValueError,'native parent'):
                audit.load_contract(data)

    def test_native_worker_hashes_and_dispatch_read_only(self):
        from pe_disassemble_va import Image
        path = Path(r'C:\UNCHARTED Legacy of Thieves Collection (2016-2017-2022)\Uncharted Legacy of Thieves Collection\tll.exe')
        if not path.exists():
            self.skipTest('installed retail executable unavailable')
        image = Image(str(path))
        try:
            audit.verify_native_image(image)
        finally:
            image.pe.close()

    def test_contract_cli_report_does_not_invent_a_baseline(self):
        report = audit.audit(self.path)
        self.assertFalse(report['actual_pose_replayed'])
        self.assertFalse(report['source_or_runtime_admission'])
        self.assertFalse(report['retail_or_game_execution'])
        self.assertEqual(report['formula']['S553_xyzw'],[1.,1.,1.,1.])


if __name__ == '__main__':
    unittest.main()
