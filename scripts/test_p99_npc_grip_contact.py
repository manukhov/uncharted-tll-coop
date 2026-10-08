import copy
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import p99_npc_grip_contact as a
import p99_npc_grip_candidate as npc


class CandidateFrameTests(unittest.TestCase):
    def setUp(self):
        self.parents = {j: 28 for j in a.surface.JOINTS}
        self.parents[28] = 26
        self.names = {j: f'joint{j}' for j in a.surface.JOINTS}
        self.rows = [{'target_global': j, 'target_name': self.names[j],
                      'local_row': list(a.grip.row()),
                      'prop_relative_row': list(a.grip.row())} for j in a.surface.JOINTS]
        self.baseline = {'local_joint_rows': {'rows': [
            {'target_global': j, 'target_name': self.names[j], 'direct_row': list(a.grip.row())}
            for j in a.surface.JOINTS]}}

    def test_exact_semantic_coverage_and_wrist_reconstruction(self):
        frames = a.checked_candidate(self.rows, self.parents, self.names)
        self.assertEqual(set(frames), set(a.surface.JOINTS) | {26})
        self.assertTrue(all(frame == a.grip.row() for frame in frames.values()))
        for variant in a.VARIANTS:
            self.assertEqual(a.select_variant(self.rows, self.baseline, self.parents, variant), self.rows)

    def test_missing_duplicate_name_and_inconsistent_frames_refused(self):
        for mutation in ('missing', 'duplicate', 'name', 'frame', 'local', 'nan', 'scale'):
            rows = copy.deepcopy(self.rows)
            if mutation == 'missing':
                rows.pop()
            elif mutation == 'duplicate':
                rows[-1] = copy.deepcopy(rows[0])
            elif mutation == 'name':
                rows[-1]['target_name'] = 'l_palm'
            elif mutation == 'frame':
                rows[-1]['prop_relative_row'][8] = .001
            elif mutation == 'local':
                rows[-1]['local_row'][8] = .001
            elif mutation == 'nan':
                rows[-1]['local_row'][8] = float('nan')
            else:
                rows[-1]['local_row'][0] = 2.
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                a.checked_candidate(rows, self.parents, self.names)

    def test_unknown_variant_and_unstated_alignment_refused(self):
        with self.assertRaises(ValueError):
            a.select_variant(self.rows, self.baseline, self.parents, 'best')
        with mock.patch.object(a.wrist, 'materialize_context', side_effect=AssertionError('too late')):
            with self.assertRaises(ValueError):
                a.compare([], {}, None, None, None, None, alignment_assumption=None)


class PinnedNpcGripContactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.body = a.surface.ROOT/'reports/actor_packages/nadine-rural-body.pak'
        cls.skeleton = npc.DEFAULT_NADINE_ASSET
        if not all(p.is_file() for p in (cls.body, cls.skeleton, npc.DEFAULT_ACTOR_ARCHIVE,
                                        npc.DEFAULT_DC_ARCHIVE)):
            raise unittest.SkipTest('pinned local assets unavailable')
        with mock.patch('native_anim_pose_audit.MappedImage', side_effect=AssertionError('retail forbidden')):
            cls.baseline = a.wrist.direct.audit(cls.skeleton, npc.DEFAULT_ACTOR_ARCHIVE,
                                               npc.DEFAULT_DC_ARCHIVE, include_local_rows=True)
            cls.candidate = npc.audit()['candidate_rows']
            cls.context, _, _ = a.wrist.materialize_context(cls.baseline, cls.body, cls.skeleton)
            assets = a.contact.weapon.load_assets()
            cls.results = {variant: a.compare(cls.candidate, cls.baseline, cls.body, cls.skeleton,
                                              *assets, alignment_assumption=a.contact.ALIGNMENT,
                                              variant=variant) for variant in a.VARIANTS}

    def test_fingers_only_preserves_attachment_and_selects_no_palm_prop_local(self):
        parents = self.context[4]
        selected = a.select_variant(self.candidate, self.baseline, parents, 'fingers-only')
        originals = self.baseline['local_joint_rows']['rows']
        for row, old, candidate in zip(selected, originals, self.candidate):
            expected = old['direct_row'] if row['target_global'] in (28, 30) else candidate['local_row']
            self.assertEqual(row['local_row'], expected)
        self.assertTrue(self.results['fingers-only']['palm_prop_wrist_frames_preserved'])
        self.assertFalse(self.results['full-hand']['palm_prop_wrist_frames_preserved'])

    def test_conditional_crossings_distinguish_three_candidates(self):
        expected = {'full-hand': (175, 313, 173), 'fingers-only': (813, 265, 763),
                    'palm-prop-only': (104, 313, 102)}
        for variant, (pairs, removed, added) in expected.items():
            result = self.results[variant]
            with self.subTest(variant=variant):
                self.assertEqual(result['conditional_surface_crossings']['direct']['crossing_pairs'], 315)
                self.assertEqual(result['conditional_surface_crossings']['proposed']['crossing_pairs'], pairs)
                self.assertEqual((result['removed_baseline_pairs'], result['new_pairs']), (removed, added))
                self.assertEqual(result['surface_closure']['closed_vertices'], 4328)
                self.assertEqual(result['surface_closure']['excluded_helper_vertices'], 494)
                self.assertFalse(result['runtime_candidate_admitted'])
                self.assertFalse(result['visual_improvement_proven'])
                self.assertFalse(result['left_support_arm_fixed'])

    def test_lower_crossings_do_not_get_automatically_ranked_as_better(self):
        result = self.results['palm-prop-only']
        distances = result['comparison']['all_supported_vertices']
        self.assertGreater(distances['proposed']['mean_asset_units'], distances['direct']['mean_asset_units'])
        self.assertIsNone(result['comparison']['preferred_pose'])
        full = self.results['full-hand']['conditional_surface_crossings']
        self.assertGreater(full['proposed']['pairs_by_finger_support_group']['ring'],
                           full['direct']['pairs_by_finger_support_group']['ring'])


if __name__ == '__main__':
    unittest.main()
