import copy
import math
import struct
import unittest
from unittest.mock import patch

import p99_wrist_g0_replay as replay


IDENTITY = replay.staged.matrix.IDENTITY
UNIT = (0., 0., 0., 1.)


def row(q=UNIT, t=(0., 0., 0.), s=(1., 1., 1.)):
    return (*s, 1., *q, *t, 1.)


class PinnedG0IntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = replay.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        if not path.exists():
            raise unittest.SkipTest('pinned Nadine skeleton asset not installed')
        cls.data = path.read_bytes()
        skeleton = replay.dependency.source.load_skeleton(cls.data, replay.dependency.source.NADINE_SKELETON)
        cls.base, cls.ops = replay.dependency.group_program(cls.data, skeleton, 0)
        cls.contract = replay.load_contract(cls.data)

    def inputs(self):
        # Explicit synthetic control only: not an authored/default/live pose.
        return dict(shoulder22=IDENTITY,
                    jp24=row(t=(0., 0., -.28)),
                    jp26=row(t=(0., 0., -.25)),
                    jp51=row(t=(.01, .02, .03)))

    def assertVectorClose(self, a, b, tol=1e-9):
        self.assertEqual(len(a), len(b))
        self.assertLessEqual(max(abs(x-y) for x, y in zip(a, b)), tol)

    def test_pinned_ancestry_splits_two_angle_sources(self):
        proof = self.contract['angle_ancestry']
        self.assertEqual(proof[87]['entry_cells'], [])
        self.assertEqual(proof[87]['custom_scalar_cells'], ['AUX+400', 'AUX+404', 'AUX+408'])
        self.assertEqual(proof[88]['entry_cells'], ['JP+9A0', 'JP+9A4', 'JP+9A8', 'JP+9AC'])
        self.assertEqual(proof[88]['custom_scalar_cells'], [])

    def test_full_writer_scan_covers_local_rows_and_workspace(self):
        proof = self.contract['operand_survival']
        self.assertEqual(proof['untouched_local_parameters'], [24, 26, 51])
        self.assertEqual(proof['workspace_single_writers']['26'], [44, 40])
        self.assertFalse(proof['reserved_workspace0_written_by_program'])

    def test_new_vector_overlap_on_source_parameter_rejected(self):
        data = bytearray(self.data)
        op = self.ops[31]
        struct.pack_into('<H', data, self.base+op.payload[2]+8, 48*24+16)
        with self.assertRaisesRegex(ValueError, 'JP\\+49'):
            replay.prove_operand_survival(data, self.base, self.ops)

    def test_new_vector_overlap_on_shoulder_workspace_rejected(self):
        data = bytearray(self.data)
        op = self.ops[31]
        struct.pack_into('<H', data, self.base+op.payload[2]+8, 0x5D1)
        with self.assertRaisesRegex(ValueError, 'W\\+5D'):
            replay.prove_operand_survival(data, self.base, self.ops)

    def test_new_write_to_reserved_identity_rejected(self):
        data = bytearray(self.data)
        op = self.ops[31]
        struct.pack_into('<H', data, self.base+op.payload[2]+8, 1)
        with self.assertRaisesRegex(ValueError, 'W\\+'):
            replay.prove_operand_survival(data, self.base, self.ops)

    def test_curve_reads_different_joint_rejected_by_ancestry(self):
        data = bytearray(self.data)
        op = self.ops[11]
        desc = self.base+op.payload[2]
        index = next(i for i in range(op.payload[0])
                     if struct.unpack_from('<H', data, desc+4*i+2)[0] == 0x2EE)
        struct.pack_into('<H', data, desc+4*index, 0x490)
        with self.assertRaisesRegex(ValueError, 'source leaves'):
            replay.prove_scalar_slice(data, self.base, self.ops)

    def test_no_optional_baseline_or_supplied_procedural_q(self):
        with self.assertRaises(TypeError):
            replay.replay_helpers(self.contract)
        with self.assertRaises(TypeError):
            replay.replay_helpers(self.contract, **self.inputs(), q85=UNIT)
        out = replay.replay_helpers(self.contract, **self.inputs())
        self.assertTrue(out['helper85_88_analytic_slice_closed'])
        self.assertEqual(out['supplied_unreplayed_intermediates'], [])
        for key in ('source_or_runtime_admission', 'actual_same_generation_source_proven',
                    'native_numeric_equivalence_proven', 'full_skin_contact_admitted',
                    'complete_group0_native_replay', 'complete_group6_native_replay'):
            self.assertFalse(out[key])

    def test_replay_is_pure_and_derives_both_arm_workspaces(self):
        args = self.inputs()
        before = copy.deepcopy(args)
        out = replay.replay_helpers(self.contract, **args)
        self.assertEqual(args, before)
        elbow = replay.staged.matrix.propagate(args['shoulder22'], args['jp24'], True)
        wrist = replay.staged.matrix.propagate(elbow, args['jp26'], True)
        self.assertEqual(out['upstream_matrices'], {24: elbow, 26: wrist})
        self.assertEqual(set(out['matrices']), {85, 86, 87, 88})

    def test_q51_changes_q88_but_not_85_86_87(self):
        args = self.inputs()
        first = replay.replay_helpers(self.contract, **args)
        args['jp51'] = row(q=(math.sin(.13), 0, 0, math.cos(.13)), t=(.01, .02, .03))
        second = replay.replay_helpers(self.contract, **args)
        for joint in (85, 86, 87):
            self.assertEqual(first['matrices'][joint], second['matrices'][joint])
        self.assertNotEqual(first['jointparams'][88][4:8], second['jointparams'][88][4:8])
        self.assertNotEqual(first['matrices'][88], second['matrices'][88])

    def test_q26_changes_angles87_but_not_local_q88(self):
        args = self.inputs()
        first = replay.replay_helpers(self.contract, **args)
        args['jp26'] = row(q=(.5, 0, 0, math.sqrt(.75)), t=(0, 0, -.25))
        second = replay.replay_helpers(self.contract, **args)
        self.assertEqual(first['jointparams'][85], second['jointparams'][85])
        self.assertEqual(first['jointparams'][88][4:8], second['jointparams'][88][4:8])
        self.assertNotEqual(first['jointparams'][87][4:8], second['jointparams'][87][4:8])

    def test_shoulder_translation_transports_helpers_without_changing_local_q(self):
        args = self.inputs()
        first = replay.replay_helpers(self.contract, **args)
        shift = (2., -3., 4.)
        args['shoulder22'] = (IDENTITY[0], shift, IDENTITY[2])
        second = replay.replay_helpers(self.contract, **args)
        for joint in (85, 86, 87, 88):
            self.assertVectorClose(first['jointparams'][joint][4:8], second['jointparams'][joint][4:8])
            self.assertVectorClose(replay.staged.vadd(first['matrices'][joint][1], shift),
                                   second['matrices'][joint][1])

    def test_rigid_shoulder_rotation_transports_every_helper_frame(self):
        args = self.inputs()
        first = replay.replay_helpers(self.contract, **args)
        global_rotation = replay.staged.matrix.rotation((0., 0., math.sin(.4), math.cos(.4)))
        args['shoulder22'] = (global_rotation, (0., 0., 0.), (1., 1., 1.))
        second = replay.replay_helpers(self.contract, **args)
        for joint in (85, 86, 87, 88):
            before, after = first['matrices'][joint], second['matrices'][joint]
            self.assertVectorClose(replay.staged.matvec(global_rotation, before[1]), after[1])
            for col in range(3):
                self.assertVectorClose(
                    replay.staged.matvec(global_rotation, tuple(before[0][i][col] for i in range(3))),
                    tuple(after[0][i][col] for i in range(3)))
            self.assertEqual(before[2], after[2])

    def test_q87_x_is_authored_constant_and_y_uses_two_sequential_curves(self):
        weights = {0x400: 12., 0x3E4: .4, 0x3EC: .3, 0x404: .2, 0x408: .6}
        out = replay.scalar_angles(self.contract, weights=weights, euler51=(10, 20, 30))
        constants = self.contract['staged']['constants']
        curves = [c for c in self.contract['curves25'] if c.destination == 0x356]
        self.assertEqual(len(curves), 2)
        expected = constants[2, 0x354] + sum(c.contribution(weights[c.source & ~3]) for c in curves)
        self.assertAlmostEqual(out[87][1], expected)
        self.assertEqual(out[87][0], constants[2, 0x2E8])

    def test_nonfinite_required_inputs_fail_closed(self):
        args = self.inputs()
        args['jp24'] = row(t=(math.nan, 0, 0))
        with self.assertRaises(ValueError):
            replay.replay_helpers(self.contract, **args)

    def test_unknown_scalar_weight_set_rejected(self):
        with self.assertRaisesRegex(ValueError, 'weight set'):
            replay.scalar_angles(self.contract, weights={0x400: 0.}, euler51=(0, 0, 0))

    def test_native_execution_forbidden_and_contract_audit_has_no_pose(self):
        with patch('native_anim_pose_audit.MappedImage', side_effect=AssertionError('native execution')):
            replay.replay_helpers(self.contract, **self.inputs())
            report = replay.audit(replay.ROOT/'reports/actor_packages/nadine-dlc-base.pak')
        self.assertFalse(report['actual_pose_replayed'])
        self.assertFalse(report['full_skin_contact_admitted'])


if __name__ == '__main__':
    unittest.main()
