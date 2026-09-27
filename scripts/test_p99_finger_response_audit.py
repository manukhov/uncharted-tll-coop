"""Independent local-axis/pivot controls; no game or retail execution."""
import math
import unittest
from unittest import mock

import p99_finger_response_audit as a
import p99_direct_grip_audit as direct


def model():
    # Parent/prop fixed, one noncommuting rotated chain, one unaffected finger.
    qz = (0., 0., math.sqrt(.5), math.sqrt(.5))
    frames = {28: a.grip.row(), 30: a.grip.row(),
              105: a.grip.row(rotation=qz, translation=(1., 2., 3.)),
              107: a.grip.row(rotation=qz, translation=(2., 2., 3.)),
              109: a.grip.row(rotation=qz, translation=(3., 2., 3.)),
              113: a.grip.row(translation=(4., 5., 6.))}
    parents = {28: -1, 30: 28, 105: 28, 107: 105, 109: 107, 113: 28}
    return frames, parents


class LocalResponseMathTests(unittest.TestCase):
    def test_local_x_axis_rotated_by_joint_basis_not_global_x(self):
        frames, parents = model()
        changed, affected = a.rotate_local(frames, parents, 105, 0, 10.)
        sine, cosine = math.sin(math.radians(10)), math.cos(math.radians(10))
        for actual, expected in zip(changed[107][8:11], (1+cosine, 2, 3-sine)):
            self.assertAlmostEqual(actual, expected, places=12)
        self.assertEqual(affected, {105, 107, 109})
        for j in (28, 30, 113):
            self.assertEqual(changed[j], frames[j])

    def test_only_target_local_q_changes_and_all_local_st_survive(self):
        frames, parents = model()
        changed, affected = a.rotate_local(frames, parents, 107, 2, -5.)
        checks = a.invariants(frames, changed, parents, 107, affected)
        self.assertLess(checks['local_ST_max_error'], 1e-12)
        self.assertLess(checks['other_local_Q_max_error'], 1e-12)
        old = a.local_row(frames, parents, 107)
        new = a.local_row(changed, parents, 107)
        self.assertAlmostEqual(a.grip.angular_degrees(old[4:8], new[4:8]), 5., places=10)
        self.assertEqual(changed[107][8:11], frames[107][8:11])

    def test_zero_probe_is_exact_identity(self):
        frames, parents = model()
        changed, _ = a.rotate_local(frames, parents, 105, 1, 0.)
        self.assertEqual(changed, frames)

    def test_opposite_rotation_round_trip(self):
        frames, parents = model()
        changed, _ = a.rotate_local(frames, parents, 105, 0, 5.)
        restored, _ = a.rotate_local(changed, parents, 105, 0, -5.)
        self.assertLess(max(a.grip.row_error(frames[j], restored[j]) for j in frames), 1e-12)

    def test_protected_left_unknown_axis_large_and_nonfinite_refused(self):
        frames, parents = model()
        for joint, axis, angle in ((28, 0, 5), (30, 0, 5), (104, 0, 5),
                                    (105, 3, 5), (105, 0, 11), (105, 0, math.nan)):
            with self.subTest(joint=joint, axis=axis, angle=angle), self.assertRaises(ValueError):
                a.rotate_local(frames, parents, joint, axis, angle)

    def test_cycle_missing_parent_and_protected_descendant_refused(self):
        frames, parents = model()
        for changed in ({**parents, 105: 107}, {**parents, 105: 999}, {**parents, 30: 105}):
            with self.assertRaises(ValueError):
                a.rotate_local(frames, changed, 105, 0, 5.)

    def test_nonunit_scale_and_nonunit_q_refused(self):
        frames, parents = model()
        for value in (a.grip.row(scale=(1, 2, 1)), a.grip.row(rotation=(0, 0, 0, 2))):
            with self.assertRaises(ValueError):
                a.rotate_local({**frames, 105: value}, parents, 105, 0, 5.)

    def test_invariant_check_rejects_unrelated_or_local_translation_edits(self):
        frames, parents = model()
        changed, affected = a.rotate_local(frames, parents, 105, 0, 5.)
        bad = {**changed, 113: a.grip.row(translation=(4, 5, 7))}
        with self.assertRaisesRegex(ValueError, 'unaffected'):
            a.invariants(frames, bad, parents, 105, affected)
        bad = dict(changed)
        row = list(bad[107]); row[8] += .01; bad[107] = tuple(row)
        with self.assertRaisesRegex(ValueError, 'local'):
            a.invariants(frames, bad, parents, 105, affected)

    def test_weighted_surface_recomputed_not_whole_mesh_rigid_motion(self):
        frames, parents = model()
        changed, affected = a.rotate_local(frames, parents, 105, 0, 5.)
        weights = [[a.surface.skin.PackedInfluence(105, 0x200000),
                    a.surface.skin.PackedInfluence(113, 0x200000)],
                   [a.surface.skin.PackedInfluence(113, 0x3FFFFF)]]
        positions = [(0, 1, 0), (1, 2, 3)]
        inverse = {j: a.surface.IDENTITY for j in frames}
        samples = [{'vertex': i, 'direct': a.surface.skin_point(p, weights[i], frames, inverse),
                    'positive_joints': [w.joint for w in weights[i]]} for i, p in enumerate(positions)]
        moved, displacement = a.reskin(samples, positions, weights, inverse, changed, affected)
        self.assertEqual(moved[0]['proposed'], a.surface.skin_point(positions[0], weights[0], changed, inverse))
        self.assertEqual(moved[1]['proposed'], samples[1]['direct'])
        self.assertEqual(displacement[1], 0.)
        self.assertGreater(displacement[0], 0.)


class PinnedResponseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.body = a.surface.ROOT/'reports/actor_packages/nadine-rural-body.pak'
        cls.skeleton = a.surface.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        archive = a.contact.weapon.DEFAULT_ARCHIVE
        if not all(p.is_file() for p in (cls.body, cls.skeleton, archive, archive.with_name('dc1.psarc'))):
            raise unittest.SkipTest('pinned local assets unavailable')
        with mock.patch('native_anim_pose_audit.MappedImage', side_effect=AssertionError('retail forbidden')):
            cls.pose = direct.audit(cls.skeleton, archive, archive.with_name('dc1.psarc'))
            cls.p99, cls.pistol = a.contact.weapon.load_assets()
            cls.context = a.materialize_context(cls.pose, cls.body, cls.skeleton)
            cls.result = a.audit(cls.pose, cls.body, cls.skeleton, cls.p99, cls.pistol,
                                alignment_assumption=a.contact.ALIGNMENT, joints=(113,), angles=(-5.,))

    def test_authored_parent_map_is_odd_right_chains_only(self):
        parents, _ = a.selected_parents(self.skeleton.read_bytes())
        self.assertEqual(a.descendants(parents, 113), {113, 115, 117, 119})
        self.assertEqual(parents[30], 28)
        self.assertEqual(parents[113], 28)
        self.assertNotIn(112, parents)

    def test_zero_local_probe_reproduces_closed_surface_exactly(self):
        samples, _, _, frames, parents, inverse, positions, weights = self.context
        changed, affected = a.rotate_local(frames, parents, 113, 0, 0.)
        moved, distances = a.reskin(samples, positions, weights, inverse, changed, affected)
        self.assertTrue(all(row['proposed'] == row['direct'] for row in moved))
        self.assertEqual(max(distances), 0.)

    def test_single_joint_counterfactual_is_not_a_runtime_fix(self):
        result = self.result
        self.assertEqual(result['baseline_crossing_pairs'], 223)
        self.assertEqual(result['probe_count'], 3)
        x = result['probes'][0]
        self.assertEqual((x['joint'], x['axis'], x['degrees']), (113, 'x', -5.))
        self.assertEqual(x['crossing_pairs'], 159)
        self.assertEqual(x['pairs_by_finger_support_group'].get('ring', 0), 0)
        self.assertEqual(x['new_pairs'], 9)
        self.assertTrue(x['all_group1_shared_exports_unchanged'])
        self.assertEqual(x['changed_shared_quaternion_exports'], [])
        self.assertFalse(result['runtime_candidate_admitted'])
        self.assertFalse(result['runtime_record_output'])
        self.assertIsNone(result['preferred_pose'])

    def test_unaffected_finger_samples_are_exactly_fixed(self):
        samples, _, _, frames, parents, inverse, positions, weights = self.context
        changed, affected = a.rotate_local(frames, parents, 113, 0, -5.)
        moved, _ = a.reskin(samples, positions, weights, inverse, changed, affected)
        untouched = [r for r in moved if not set(r['positive_joints']) & affected]
        self.assertGreater(len(untouched), 2000)
        self.assertTrue(all(r['direct'] == r['proposed'] for r in untouched))

    def test_probe_set_scope_and_alignment_gate(self):
        for options in ({'alignment_assumption': None},
                        {'alignment_assumption': a.contact.ALIGNMENT, 'joints': (137,)},
                        {'alignment_assumption': a.contact.ALIGNMENT, 'joints': (113, 113)},
                        {'alignment_assumption': a.contact.ALIGNMENT, 'angles': (0.,)}):
            with self.assertRaises(ValueError):
                a.audit(self.pose, self.body, self.skeleton, self.p99, self.pistol, **options)


if __name__ == '__main__':
    unittest.main()
