import copy
import math
import struct
import unittest
from unittest.mock import patch

import p99_wrist_g6_replay as replay


IDENTITY = replay.kernels.matrix.IDENTITY
UNIT = (0.,0.,0.,1.)


def row(q=UNIT, t=(0.,0.,0.), s=(1.,1.,1.)):
    return (*s,1.,*q,*t,1.)


class G6IntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = replay.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        if not path.exists():
            raise unittest.SkipTest('pinned Nadine asset unavailable')
        cls.data = path.read_bytes()
        cls.skeleton = replay.dependency.source.load_skeleton(cls.data,replay.dependency.source.NADINE_SKELETON)
        cls.base, cls.ops = replay.dependency.group_program(cls.data,cls.skeleton,6)
        cls.contract = replay.load_contract(cls.data)

    def inputs(self):
        # Explicit synthetic input, not reference admission or a corrected pose.
        return dict(shoulder22=IDENTITY,jp24=row(t=(0,0,-.28)),jp26=row(t=(0,0,-.25)),
                    jp49=row(t=(.01,.02,.03)),jp51=row(t=(.04,.01,-.02)))

    def assertVectorClose(self, a, b, tol=1e-9):
        self.assertEqual(len(a),len(b))
        self.assertLessEqual(max(abs(x-y) for x,y in zip(a,b)),tol)

    def test_exact_target_slices_leave_arity34_and_aim_unreached(self):
        proof = self.contract['proof']
        self.assertEqual(set(proof['selected_cells']),{0,1,2,3,4,7,9,11,12,14,15})
        self.assertEqual(proof['joint49_group0_writers'],[])
        for joint, target in proof['targets'].items():
            self.assertEqual(set(map(tuple,target['operators'])),replay.EXPECTED_OPS[joint])

    def test_actual_g0_exports_are_not_assumed_aliases(self):
        self.assertEqual(replay.expected_origin((3,0x480)),(0,49,53,(0,48*49)))
        self.assertEqual(replay.expected_origin((3,0x1290)),(0,49,53,(1,64*87)))
        self.assertEqual(replay.expected_origin((3,0x12D0)),(0,49,53,(1,64*89)))
        self.assertEqual(replay.expected_origin((3,0x1940)),(0,50,54,(2,0x400)))
        with self.assertRaisesRegex(ValueError,'unreviewed'):
            replay.expected_origin((3,0x720))  # Finger export is outside this slice.

    def test_opaque_aim_writer_into_target_translation_rejected(self):
        data = bytearray(self.data)
        struct.pack_into('<H',data,self.base+self.ops[13].payload[2]+2,48*32+32)
        with self.assertRaisesRegex(ValueError,'unsupported'):
            replay.prove_slice(data,self.skeleton)

    def test_unreviewed_arity4_writer_into_target_rejected(self):
        data = bytearray(self.data)
        struct.pack_into('<H',data,self.base+self.ops[5].payload[2]+2,48*32+32)
        with self.assertRaisesRegex(ValueError,'unsupported'):
            replay.prove_slice(data,self.skeleton)

    def test_changed_parent_not_silently_interpreted_as_bind_hierarchy(self):
        data = bytearray(self.data)
        descriptor = self.base+self.ops[15].payload[2]
        for index in range(4*self.ops[15].payload[0]):
            key = struct.unpack_from('<H',data,descriptor+4*index)[0]
            if key == 16*(33+1):
                struct.pack_into('<H',data,descriptor+4*index+2,0x129B)
                break
        with self.assertRaises(ValueError):
            replay.prove_slice(data,self.skeleton)

    def test_changed_g0_copy_origin_rejected(self):
        data = bytearray(self.data)
        base, ops = replay.dependency.group_program(data,self.skeleton,0)
        descriptor = base+ops[49].payload[2]
        found = False
        for index in range(ops[49].payload[0]):
            dest, source = struct.unpack_from('<HH',data,descriptor+4*index)
            if dest&~15 == 0x480 and dest&3 == 3:
                struct.pack_into('<H',data,descriptor+4*index+2,source+48)
                found = True
                break
        self.assertTrue(found)
        with self.assertRaisesRegex(ValueError,'copy origin'):
            replay.prove_slice(data,self.skeleton)

    def test_intervening_shared_writer_is_not_ignored(self):
        original = replay.dependency.program.writes
        def extra(data, base, op):
            writes = original(data,base,op)
            return writes+([(0x1293,16)] if base == 0x5CD70 and op.ordinal == 0 else [])
        with patch.object(replay.dependency.program,'writes',side_effect=extra):
            with self.assertRaisesRegex(ValueError,'group1 overwrites'):
                replay.prove_slice(self.data,self.skeleton)

    def test_g0_last_writer_must_be_the_expected_export(self):
        original = replay.dependency.program.writes
        def extra(data, base, op):
            writes = original(data,base,op)
            return writes+([(0x1293,16)] if base == 0x593C0 and op.ordinal == 50 else [])
        with patch.object(replay.dependency.program,'writes',side_effect=extra):
            with self.assertRaisesRegex(ValueError,'g0 shared last writer'):
                replay.prove_slice(self.data,self.skeleton)

    def test_every_source_required_no_procedural_q_or_missing_jp49_default(self):
        args = self.inputs()
        for key in args:
            missing = dict(args)
            missing.pop(key)
            with self.assertRaises(TypeError):
                replay.replay_helpers(self.contract,**missing)
        with self.assertRaises(TypeError):
            replay.replay_helpers(self.contract,**args,q553=UNIT)

    def test_input_purity_complete_frame_set_and_no_runtime_admission(self):
        args = self.inputs()
        before = copy.deepcopy(args)
        out = replay.replay_helpers(self.contract,**args)
        self.assertEqual(args,before)
        self.assertEqual(set(out['missing_skin_helper_frames']),{88,535,536,537,554})
        self.assertEqual(set(out['g6']['matrices']),{534,535,536,537,553,554})
        self.assertTrue(out['helper88_535_536_537_554_analytic_slice_closed'])
        for field in ('source_or_runtime_admission','actual_same_generation_source_proven',
                      'native_numeric_equivalence_proven','full_skin_contact_admitted',
                      'complete_group0_native_replay','complete_group6_native_replay'):
            self.assertFalse(out[field])

    def test_exact_parent_frames_and_authored_homogeneous_fields(self):
        out = replay.replay_helpers(self.contract,**self.inputs())
        parents = {534:out['g0']['matrices'][86],535:out['g6']['matrices'][534],
                   536:out['g0']['matrices'][88],537:out['g0']['matrices'][88],
                   554:out['g6']['matrices'][553]}
        for joint, parent in parents.items():
            local = out['g6']['jointparams'][joint]
            self.assertEqual(out['g6']['matrices'][joint],replay.kernels.matrix.propagate(parent,local,True))
            self.assertEqual((local[3],local[11]),(1.,1.))

    def test_q49_changes_only535_not_other_target_helpers(self):
        args = self.inputs()
        first = replay.replay_helpers(self.contract,**args)
        args['jp49'] = row(q=(math.sin(.2),0,0,math.cos(.2)),t=(.01,.02,.03))
        second = replay.replay_helpers(self.contract,**args)
        self.assertNotEqual(first['g6']['matrices'][535],second['g6']['matrices'][535])
        for joint in (88,536,537,554):
            self.assertEqual(first['missing_skin_helper_frames'][joint],second['missing_skin_helper_frames'][joint])

    def test_jp49_scale_translation_are_not_dropped(self):
        args = self.inputs()
        first = replay.replay_helpers(self.contract,**args)
        args['jp49'] = row(t=(.1,.2,.3),s=(1.2,.8,1.1))
        second = replay.replay_helpers(self.contract,**args)
        self.assertNotEqual(first['g6']['jointparams'][535][:3],second['g6']['jointparams'][535][:3])
        self.assertNotEqual(first['g6']['matrices'][535][1],second['g6']['matrices'][535][1])
        self.assertEqual(first['g6']['matrices'][534],second['g6']['matrices'][534])

    def test_jp51_changes_536537_parent_not_535554(self):
        args = self.inputs()
        first = replay.replay_helpers(self.contract,**args)
        args['jp51'] = row(q=(0,math.sin(.13),0,math.cos(.13)),t=(.04,.01,-.02))
        second = replay.replay_helpers(self.contract,**args)
        for joint in (535,554):
            self.assertEqual(first['g6']['matrices'][joint],second['g6']['matrices'][joint])
        for joint in (536,537):
            self.assertEqual(first['g6']['jointparams'][joint],second['g6']['jointparams'][joint])
            self.assertNotEqual(first['g6']['matrices'][joint],second['g6']['matrices'][joint])

    def test_shoulder_rigid_transform_covariance(self):
        args = self.inputs()
        first = replay.replay_helpers(self.contract,**args)
        rotation = replay.kernels.matrix.rotation((0,0,math.sin(.3),math.cos(.3)))
        translation = (2.,-3.,4.)
        args['shoulder22'] = (rotation,translation,(1.,1.,1.))
        second = replay.replay_helpers(self.contract,**args)
        for joint, before in first['missing_skin_helper_frames'].items():
            after = second['missing_skin_helper_frames'][joint]
            self.assertVectorClose(replay.kernels.vadd(replay.kernels.matvec(rotation,before[1]),translation),after[1])
            for column in range(3):
                self.assertVectorClose(replay.kernels.matvec(rotation,tuple(before[0][i][column] for i in range(3))),
                                       tuple(after[0][i][column] for i in range(3)))
            self.assertEqual(before[2],after[2])

    def test_unknown_jp49_nonfinite_or_bad_q_rejected(self):
        for q in ((0,0,0,0),(0,0,math.nan,1)):
            args = self.inputs()
            args['jp49'] = row(q=q)
            with self.assertRaises(ValueError):
                replay.replay_helpers(self.contract,**args)

    def test_asset_change_rejected_before_replay(self):
        data = bytearray(self.data)
        data[-1] ^= 1
        with self.assertRaises(ValueError):
            replay.load_contract(data)

    def test_no_native_execution_or_pose_baseline_in_report(self):
        with patch('native_anim_pose_audit.MappedImage',side_effect=AssertionError('native execution')):
            replay.replay_helpers(self.contract,**self.inputs())
            report = replay.audit(replay.ROOT/'reports/actor_packages/nadine-dlc-base.pak')
        self.assertFalse(report['actual_pose_replayed'])
        self.assertFalse(report['full_skin_contact_admitted'])
        self.assertEqual(report['unreached_operator_ordinals'],[5,6,8,10,13])


if __name__ == '__main__':
    unittest.main()
