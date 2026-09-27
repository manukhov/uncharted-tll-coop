import copy
import math
import unittest
from unittest import mock

import p99_wrist_surface_audit as a


class WristInverseMathTests(unittest.TestCase):
    def test_noncommuting_inverse_order_and_translation(self):
        # Independent exact matrices: Rz(90)*Rx(90), not Rx(90)*Rz(90).
        h = math.sqrt(.5)
        palm = a.grip.row(rotation=(.5, .5, .5, .5), translation=(1.7, 2.2, 4.3))
        local = a.grip.row(rotation=(h, 0., 0., h), translation=(.2, -.7, 1.3))
        wrist = a.recover_parent(palm, local)
        self.assertLess(a.grip.qerror(wrist[4:8], (0., 0., h, h)), 1e-15)
        self.assertLess(max(abs(x-y) for x, y in zip(wrist[8:11], (1., 2., 3.))), 1e-15)
        wrong = a.grip.qmul(a.grip.qconj(local[4:8]), palm[4:8])
        self.assertGreater(a.grip.qerror(wrong, wrist[4:8]), .5)

    def test_local_translation_is_rotated_not_subtracted_in_world_axes(self):
        h = math.sqrt(.5)
        palm = a.grip.row(rotation=(0., 0., h, h), translation=(2., 4., 6.))
        wrist = a.recover_parent(palm, a.grip.row(translation=(1., 0., 0.)))
        self.assertAlmostEqual(wrist[8], 2.)
        self.assertAlmostEqual(wrist[9], 3.)
        self.assertAlmostEqual(wrist[10], 6.)

    def test_identity_and_quaternion_sign_are_physical_invariants(self):
        frame = a.grip.row(rotation=(.5, .5, .5, .5), translation=(1., 2., 3.))
        self.assertEqual(a.recover_parent(frame, a.grip.row()), frame)
        local = a.grip.row(rotation=(-.5, .5, -.5, .5), translation=(.2, .4, .6))
        flipped = a.grip.row(rotation=tuple(-x for x in local[4:8]), translation=local[8:11])
        self.assertLess(a.grip.row_error(a.recover_parent(frame, local),
                                        a.recover_parent(frame, flipped)), 1e-15)

    def test_geometry_normalizes_raw_local_q_and_ignores_native_padding(self):
        raw = list(a.grip.row(rotation=(0., 0., 0., 1.000001)))
        raw[3], raw[11] = 7., -2.
        self.assertEqual(a.rigid_local(raw), a.grip.row())
        self.assertEqual(a.recover_parent(a.grip.row(), raw), a.grip.row())

    def test_scaled_nonfinite_degenerate_and_malformed_inputs_refused(self):
        for local in (a.grip.row(scale=(1., 2., 1.)),
                      a.grip.row(rotation=(0., 0., 0., 0.)),
                      a.grip.row(rotation=(0., 0., 0., 2.)),
                      a.grip.row(translation=(float('nan'), 0., 0.)), (1.,)*11):
            with self.subTest(local=local), self.assertRaises(ValueError):
                a.recover_parent(a.grip.row(), local)
        with self.assertRaises(ValueError):
            a.recover_parent(a.grip.row(scale=(2., 1., 1.)), a.grip.row())

    def test_forward_worker_affine_reconstructs_palm(self):
        wrist = a.grip.row(rotation=(.5, .5, -.5, .5), translation=(.12, -.23, .34))
        local = a.grip.row(rotation=(.6, 0., 0., .8), translation=(.013, -.024, .035))
        palm = a.grip.blend(wrist, local)
        recovered = a.recover_parent(palm, local)
        matrix = a.response.matrix
        workspace = (matrix.rotation(recovered[4:8]), recovered[8:11], recovered[:3])
        forward = matrix.export_affine(matrix.propagate(workspace, local, True))
        expected = matrix.export_affine((matrix.rotation(palm[4:8]), palm[8:11], palm[:3]))
        self.assertLess(max(abs(x-y) for x, y in zip(forward, expected)), 2e-15)

    def test_unknown_unchanged_helper_cancels_without_weight_renormalization(self):
        weights = [a.surface.skin.PackedInfluence(113, 0x100000),
                   a.surface.skin.PackedInfluence(536, 0x100000)]
        before = {113: a.grip.row()}
        after = {113: a.grip.row(translation=(.1, .2, .3))}
        inverse = {113: a.surface.IDENTITY, 536: a.surface.IDENTITY}
        delta = a.partial_skin_delta((1., 2., 3.), weights, before, after, inverse, {113})
        for i, x in enumerate((.1, .2, .3)):
            self.assertAlmostEqual(delta[i], weights[0].weight*x)
        self.assertLess(delta[0], .026)  # Not the .05 from renormalizing sumW=.5.
        for helper in (a.grip.row(translation=(2., 3., 4.)),
                       a.grip.row(rotation=(.5, .5, .5, .5), translation=(-4., 7., 2.))):
            full_before = {**before, 536: helper}
            full_after = {**after, 536: helper}
            absolute = a.contact.sub(a.surface.skin_point((1.,2.,3.), weights, full_after, inverse),
                                      a.surface.skin_point((1.,2.,3.), weights, full_before, inverse))
            self.assertLess(max(abs(x-y) for x,y in zip(delta, absolute)), 1e-15)

    def test_missing_changed_frame_refused_but_unknown_fixed_frame_not_needed(self):
        weights = [a.surface.skin.PackedInfluence(536, 0x3FFFFF)]
        self.assertEqual(a.partial_skin_delta((0.,0.,0.), weights, {113:a.grip.row()},
                         {113:a.grip.row()}, {113:a.surface.IDENTITY}, {113}), (0.,0.,0.))
        with self.assertRaises(ValueError):
            a.partial_skin_delta((0.,0.,0.), weights, {}, {}, {}, {113})


class PinnedWristSurfaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.body = a.surface.ROOT/'reports/actor_packages/nadine-rural-body.pak'
        cls.skeleton = a.surface.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        cls.archive = a.contact.weapon.DEFAULT_ARCHIVE
        if not all(p.is_file() for p in (cls.body, cls.skeleton, cls.archive,
                                        cls.archive.with_name('dc1.psarc'))):
            raise unittest.SkipTest('pinned local assets unavailable')
        with mock.patch('native_anim_pose_audit.MappedImage', side_effect=AssertionError('retail forbidden')):
            cls.pose = a.direct.audit(cls.skeleton, cls.archive, cls.archive.with_name('dc1.psarc'),
                                      include_local_rows=True)
            cls.context, cls.closure, cls.proof = a.materialize_context(cls.pose, cls.body, cls.skeleton)
            cls.p99, cls.pistol = a.contact.weapon.load_assets()
            cls.result = a.audit(cls.pose, cls.body, cls.skeleton, cls.p99, cls.pistol,
                                  alignment_assumption=a.contact.ALIGNMENT)

    def test_default_report_unchanged_and_optional_local_evidence_pinned(self):
        default = a.direct.audit(self.skeleton, self.archive, self.archive.with_name('dc1.psarc'))
        self.assertNotIn('local_joint_rows', default)
        self.assertEqual(a.digest(default), a.SOURCE_SHA256)
        extended = dict(self.pose)
        local = extended.pop('local_joint_rows')
        self.assertEqual(extended, default)
        self.assertEqual(a.digest(local), a.LOCAL_SHA256)

    def test_stale_missing_and_inconsistent_palm_local_evidence_refused(self):
        for mutation in ('missing', 'Q', 'T', 'scale', 'parent', 'source', 'base'):
            report = copy.deepcopy(self.pose)
            if mutation == 'missing':
                report.pop('local_joint_rows')
            elif mutation in ('Q', 'T', 'scale'):
                lane = {'Q': 4, 'T': 8, 'scale': 0}[mutation]
                report['local_joint_rows']['rows'][0]['direct_row'][lane] += .001
            elif mutation == 'parent':
                report['local_joint_rows']['rows'][0]['parent_global'] = 24
            elif mutation == 'source':
                report['local_joint_rows']['skeleton_sha256'] = 'stale'
            else:
                report['quaternion_decoder_model'] = 'old-decoder'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                a.extended_frames(report, self.skeleton.read_bytes())

    def test_changed_skeleton_refused(self):
        data = bytearray(self.skeleton.read_bytes())
        data[-1] ^= 1
        with self.assertRaises(ValueError):
            a.extended_frames(self.pose, bytes(data))

    def test_exact_old_frames_and_native_parent_coverage(self):
        base, extended, proof = a.extended_frames(self.pose, self.skeleton.read_bytes())
        original = a.surface.report_frames(base)
        for old, new in zip(original, extended):
            self.assertEqual(set(new)-set(old), {26})
            self.assertTrue(all(new[j] == old[j] for j in old))
        self.assertEqual([(e['global'], e['parent']) for e in proof['native_parent_events']],
                         [(26, 24), (28, 26), (30, 28)])
        self.assertEqual(proof['native_required_non_op40_writes'], 0)
        self.assertFalse(proof['native_or_live_wrist_reproduced'])

    def test_surface_cohort_exactly_partitioned_and_helpers_not_fabricated(self):
        c = self.closure
        self.assertEqual((c['original_closed_vertices'], c['added_wrist_closed_vertices'],
                          c['closed_vertices'], c['excluded_helper_vertices'],
                          c['original_touched_vertices']), (3040, 1288, 4328, 494, 4822))
        self.assertEqual(c['closed_triangles'], 8026)
        self.assertEqual(c['closed_vertex_ids_sha256'],
                         '9ddbc5e401ee368b597df66091ece57c139672433f364bf4883cef74c63068dc')
        self.assertEqual(c['added_vertex_ids_sha256'],
                         '6439e79b5e44aa0184af682b3e907a3b0956c5ae5080bbb34721997fe9948640')
        self.assertEqual(c['excluded_positive_joints_overlapping'], {88:351,535:91,536:287,537:310,554:267})
        self.assertEqual(c['ring113_changed_joint_support_on_excluded_vertices'], 137)
        self.assertTrue(c['original_samples_exactly_preserved'])
        self.assertFalse(c['full_hand_surface_closed'])
        self.assertFalse(c['procedural_helper_frames_fabricated'])

    def test_zero_rotation_retains_entire_extended_skin(self):
        samples, _, _, frames, parents, inverse, positions, weights = self.context
        changed, affected = a.response.rotate_local(frames, parents, 113, 0, 0.)
        moved, distances = a.response.reskin(samples, positions, weights, inverse, changed, affected)
        self.assertEqual(max(distances), 0.)
        self.assertTrue(all(r['direct'] == r['proposed'] for r in moved))

    def test_extended_counterfactual_has_new_crossings_and_is_not_admitted(self):
        probe = self.result['counterfactual_113_localX_minus5']
        self.assertEqual(probe['crossing_pairs'], 261)
        self.assertEqual(probe['changed_supported_vertices'], 1278)
        self.assertEqual((probe['removed_baseline_pairs'], probe['new_pairs']), (108,54))
        self.assertEqual(probe['affected_selected_joints'], [113,115,117,119])
        self.assertTrue(probe['invariants']['palm_prop_exactly_unchanged'])
        self.assertFalse(probe['runtime_candidate_admitted'])
        self.assertFalse(self.result['retail_or_game_execution'])
        self.assertFalse(self.result['runtime_writes'])
        self.assertIsNone(self.result['preferred_pose'])

    def test_alignment_must_remain_explicit(self):
        with self.assertRaises(ValueError):
            a.audit(self.pose, self.body, self.skeleton, self.p99, self.pistol,
                    alignment_assumption=None)

    def test_complete_ring_delta_does_not_invent_missing_positions(self):
        delta = self.result['conditional_full_ring_displacement']
        self.assertEqual(delta['changed_joint_supported_vertices'], 1415)
        self.assertEqual(delta['unknown_absolute_position_vertices'], 137)
        self.assertLess(delta['closed_absolute_subtraction_max_error'], 2e-17)
        self.assertAlmostEqual(delta['unknown_position_delta_magnitudes']['max_asset_units'],
                               .001787464019603302, places=14)
        self.assertFalse(delta['missing_absolute_positions_recovered'])
        self.assertFalse(delta['full_contact_proven'])
        self.assertFalse(delta['live_helper_constancy_proven'])

    def test_unresolved_helper_dependency_refuses_full_delta(self):
        proof = a.helpers.audit(self.skeleton)
        proof['targets']['536']['group1_shared_finger_export_reached'] = True
        with self.assertRaises(ValueError):
            a.full_ring_delta(self.context, proof)
        proof['targets']['536']['group1_shared_finger_export_reached'] = False
        proof['targets']['536']['opaque_group6_operations'] = [(1, 51)]
        with self.assertRaises(ValueError):
            a.full_ring_delta(self.context, proof)


if __name__ == '__main__':
    unittest.main()
