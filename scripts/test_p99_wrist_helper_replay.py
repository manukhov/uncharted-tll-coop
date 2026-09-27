import copy
import math
import struct
import unittest
from unittest.mock import patch

import p99_group_program_audit as program
import p99_wrist_helper_replay as replay


IDENTITY=replay.matrix.IDENTITY
UNIT=(0.,0.,0.,1.)


class PureKernelTests(unittest.TestCase):
    def assertVectorClose(self,a,b,tolerance=1e-10):
        self.assertEqual(len(a),len(b))
        self.assertLessEqual(max(abs(x-y) for x,y in zip(a,b)),tolerance)

    def test_aim_positive_x_with_up_y_is_identity(self):
        q,branch=replay.aim_mode3(IDENTITY,(4,0,0,1),(0,0,0),IDENTITY,(0,1,0),UNIT)
        self.assertVectorClose(q,UNIT)
        self.assertEqual(branch,'orthogonal_up_basis')

    def test_aim_positive_y_rotates_x_and_uses_up_minus_x(self):
        q,_=replay.aim_mode3(IDENTITY,(0,3,0,1),(0,0,0),IDENTITY,(-1,0,0),UNIT)
        basis=replay.matrix.rotation(q)
        self.assertVectorClose(replay.matvec(basis,(1,0,0)),(0,1,0))
        self.assertVectorClose(replay.matvec(basis,(0,1,0)),(-1,0,0))

    def test_post_quaternion_is_right_multiplied(self):
        post=(math.sin(.31),0,0,math.cos(.31))
        q,_=replay.aim_mode3(IDENTITY,(0,3,0,1),(0,0,0),IDENTITY,(-1,0,0),post)
        look=(0,0,math.sqrt(.5),math.sqrt(.5))
        self.assertVectorClose(q,replay.multiply(look,post))
        self.assertGreater(max(abs(x-y) for x,y in zip(q,replay.multiply(post,look))),.1)

    def test_parent_inverse_and_local_translation_are_used(self):
        a=replay.matrix.rotation((0,0,math.sqrt(.5),math.sqrt(.5)))
        parent=(a,(4.,6.,8.),(2.,3.,4.))
        local_t=(.2,.3,.4)
        desired_local=replay.vadd(local_t,(7,0,0))
        target=replay.vadd(parent[1],replay.matvec(replay.affine_basis(parent),desired_local))
        q,_=replay.aim_mode3(parent,(*target,1),local_t,parent,(0,1,0),UNIT)
        self.assertVectorClose(q,UNIT)

    def test_up_uses_direction_not_up_frame_translation(self):
        arbitrary=(IDENTITY[0],(999.,-70.,13.),(1.,1.,1.))
        q,_=replay.aim_mode3(IDENTITY,(1,0,0,1),(0,0,0),arbitrary,(0,1,0),UNIT)
        self.assertVectorClose(q,UNIT)

    def test_parallel_up_uses_shortest_arc(self):
        q,branch=replay.aim_mode3(IDENTITY,(0,1,0,1),(0,0,0),IDENTITY,(0,1,0),UNIT)
        self.assertEqual(branch,'shortest_arc_fallback')
        self.assertVectorClose(replay.matvec(replay.matrix.rotation(q),(1,0,0)),(0,1,0))

    def test_antiparallel_degeneracy_is_not_invented_away(self):
        with self.assertRaisesRegex(ValueError,'antiparallel'):
            replay.aim_mode3(IDENTITY,(-1,0,0,1),(0,0,0),IDENTITY,(-1,0,0),UNIT)

    def test_zero_aim_uses_native_zero_direction_fallback(self):
        q,branch=replay.aim_mode3(IDENTITY,(0,0,0,1),(0,0,0),IDENTITY,(0,1,0),UNIT)
        self.assertVectorClose(q,UNIT)
        self.assertEqual(branch,'shortest_arc_fallback')

    def test_quaternion_largest_diagonal_branches(self):
        for q in ((1,0,0,0),(0,1,0,0),(0,0,1,0),(math.sqrt(.5),math.sqrt(.5),0,0)):
            a=replay.matrix.rotation(q)
            columns=tuple(tuple(a[i][j] for i in range(3)) for j in range(3))
            out=replay.quaternion_from_columns(columns)
            self.assertAlmostEqual(abs(replay.dot(q,out)),1.,places=12)

    def test_singular_and_nonfinite_inputs_rejected(self):
        with self.assertRaisesRegex(ValueError,'singular'):
            replay.aim_mode3((IDENTITY[0],(0,0,0),(1,0,1)),(1,0,0,1),(0,0,0),IDENTITY,(0,1,0),UNIT)
        with self.assertRaises(ValueError):
            replay.aim_mode3(IDENTITY,(math.nan,0,0,1),(0,0,0),IDENTITY,(0,1,0),UNIT)

    def test_piecewise_clamp_and_strict_knot(self):
        curve=replay.ScalarCurve(0,4,-2,4,1,((0,0,2,10),(0,0,3,20)))
        self.assertEqual(curve.contribution(-100),10)
        self.assertEqual(curve.contribution(1),16)
        self.assertAlmostEqual(curve.contribution(1.5),21.5)
        self.assertEqual(curve.contribution(100),29)

    def test_simd_duplicate_destinations_do_not_accumulate_each_lane(self):
        curves=tuple(replay.ScalarCurve(0,4,0,100,None,((0,0,float(i),0),)) for i in range(1,5))
        bank={(0,0):2.,(0,4):7.}
        replay.apply_curves(curves,bank,{(0,4)})
        self.assertEqual(bank[0,4],15.)  # last store7+4*2, not7+(1+2+3+4)*2

    def test_missing_explicit_scalar_operand_rejected(self):
        curve=replay.ScalarCurve(0,4,0,100,None,((0,0,1,0),))
        with self.assertRaisesRegex(ValueError,'missing explicit'):
            replay.apply_curves((curve,)*4,{(0,4):7.},{(0,4)})


class PinnedStagedReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path=replay.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        if not path.exists():
            raise unittest.SkipTest('pinned Nadine skeleton asset not installed')
        cls.data=path.read_bytes()
        cls.contract=replay.load_contract(cls.data)

    def inputs(self):
        return dict(elbow24=IDENTITY,wrist26=(IDENTITY[0],(1.,0.,0.),(1.,1.,1.)),
                    jp51=(1.,1.,1.,1.,0.,0.,0.,1.,.01,.02,.03,1.),
                    q85=UNIT,q87=UNIT,q88=UNIT,euler24_middle=0.)

    def test_pinned_post_quaternion_is_soa_not_aos(self):
        self.assertEqual(self.contract['post_q86'],(0.,-0.7071067690849304,0.,0.7071067690849304))

    def test_assumed_constants_and_st_have_complete_writer_inventory(self):
        inventory=self.contract['writer_inventory']
        self.assertEqual(inventory['AUX+C0'],[(0,52)])
        self.assertEqual(inventory['JP+1010'],[(0,52),(42,49)])
        self.assertEqual(inventory['JP+1040'],[(0,52),(10,47)])
        self.assertEqual(inventory['JP+1080'],[(0,52),(1,47)])
        self.assertEqual(inventory['JP+990'],[])
        self.assertEqual(inventory['AUX+314'],[(0,52),(8,41)])

    def test_intervening_scalar_write_to_assumed_weight_rejected(self):
        data=bytearray(self.data)
        base=0x593C0
        ops=program.operators(data,0x53FE0,replay.GROUP0)
        struct.pack_into('<H',data,base+ops[25].payload[2]+2,0xC2)
        with self.assertRaisesRegex(ValueError,'AUX\\+C0'):
            replay.prove_staged_writer_inventory(data,base,ops)

    def test_overlapping_vector_write_to_helper_st_rejected(self):
        data=bytearray(self.data)
        base=0x593C0
        ops=program.operators(data,0x53FE0,replay.GROUP0)
        # Change a later quaternion destination into S85 (all four words).
        struct.pack_into('<H',data,base+ops[31].payload[2]+8,0xFF0)
        with self.assertRaisesRegex(ValueError,'JP\\+FF'):
            replay.prove_staged_writer_inventory(data,base,ops)

    def test_pinned_scalar_coefficient_layout_identity_like_not_exact_identity(self):
        curve=next(x for x in self.contract['curves1'] if x.destination==0x1080)
        self.assertEqual(curve.source,0x990)
        self.assertAlmostEqual(curve.contribution(1),1.,places=6)
        self.assertNotEqual(curve.contribution(1),1.)

    def test_no_default_live_baseline(self):
        with self.assertRaises(TypeError):
            replay.replay_g0_staged(self.contract)
        result=replay.replay_g0_staged(self.contract,**self.inputs())
        self.assertFalse(result['complete_group0_native_replay'])
        self.assertFalse(result['complete_group6_native_replay'])
        self.assertFalse(result['source_or_runtime_admission'])

    def test_staged_inputs_preserved_and_chain_consumes_helpers(self):
        args=self.inputs()
        before=copy.deepcopy(args)
        out=replay.replay_g0_staged(self.contract,**args)
        self.assertEqual(args,before)
        self.assertEqual(set(out['matrices']),{85,86,87,88})
        for joint in (86,87,88):
            expected=replay.matrix.propagate(out['matrices'][joint-1],out['jointparams'][joint],True)
            self.assertEqual(out['matrices'][joint],expected)

    def test_missing_q87_changes_descendants_not_earlier_helpers(self):
        args=self.inputs()
        first=replay.replay_g0_staged(self.contract,**args)
        args['q87']=(math.sin(.2),0,0,math.cos(.2))
        second=replay.replay_g0_staged(self.contract,**args)
        self.assertEqual(first['matrices'][85],second['matrices'][85])
        self.assertEqual(first['matrices'][86],second['matrices'][86])
        self.assertNotEqual(first['matrices'][88],second['matrices'][88])

    def test_native_execution_forbidden(self):
        with patch('native_anim_pose_audit.MappedImage',side_effect=AssertionError('native execution')):
            replay.replay_g0_staged(replay.load_contract(self.data),**self.inputs())


if __name__=='__main__':
    unittest.main()
