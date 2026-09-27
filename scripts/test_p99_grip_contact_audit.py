"""Independent analytic geometry controls and pinned conditional integration."""
import math
from pathlib import Path
import random
import unittest
from unittest import mock

import p99_grip_contact_audit as a
import p99_direct_grip_audit as direct


class GeometryTests(unittest.TestCase):
    def assertPoint(self, actual, expected):
        for x, y in zip(actual, expected):
            self.assertAlmostEqual(x, y, places=11)

    def test_interior_face_projection_not_vertex_distance(self):
        t = ((0, 0, 0), (4, 0, 0), (0, 4, 0))
        self.assertPoint(a.triangle_point((1, 1, 5), *t), (1, 1, 0))
        self.assertEqual(a.TriangleIndex(t, [(0, 1, 2)]).nearest((1, 1, 5))[0], 25)

    def test_outside_hypotenuse_projects_to_edge(self):
        self.assertPoint(a.triangle_point((3, 3, 1), (0, 0, 0), (4, 0, 0), (0, 4, 0)),
                         (2, 2, 0))

    def test_vertex_region_and_degenerate_segment_and_point(self):
        self.assertPoint(a.triangle_point((-1, -2, 3), (0, 0, 0), (4, 0, 0), (0, 4, 0)), (0, 0, 0))
        self.assertPoint(a.triangle_point((1, 2, 3), (0, 0, 0), (4, 0, 0), (2, 0, 0)), (1, 0, 0))
        self.assertPoint(a.triangle_point((1, 2, 3), *((2, 3, 4),)*3), (2, 3, 4))

    def test_rotated_translated_asymmetric_face(self):
        # Cyclic axis permutation is a proper rigid rotation; no shared pose math.
        move = lambda p: (p[2]+7, p[0]-11, p[1]+3)
        tri = tuple(move(p) for p in ((0, 0, 0), (4, 0, 0), (0, 4, 0)))
        self.assertPoint(a.triangle_point(move((1, 2, 5)), *tri), move((1, 2, 0)))

    def test_bvh_matches_exhaustive_and_preserves_source_ordinals(self):
        rng = random.Random(81726)
        vertices = [tuple(rng.uniform(-4, 4) for _ in range(3)) for _ in range(90)]
        triangles = [tuple(range(i, i+3)) for i in range(0, 90, 3)]
        selected = list(range(1, 30, 2))
        index = a.TriangleIndex(vertices, triangles, selected)
        for _ in range(40):
            p = tuple(rng.uniform(-6, 6) for _ in range(3))
            oracle = []
            for i in selected:
                q = a.triangle_point(p, *(vertices[j] for j in triangles[i]))
                oracle.append((a.dot(a.sub(p, q), a.sub(p, q)), i, q))
            self.assertEqual(index.nearest(p), min(oracle))

    def test_equal_distance_tie_uses_smallest_original_id(self):
        index = a.TriangleIndex([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 1, 2)]*20,
                                list(reversed(range(20))))
        self.assertEqual(index.nearest((.1, .1, 2))[1], 0)

    def test_bad_geometry_selection_and_query_refused(self):
        v, t = [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 1, 2)]
        for args in (([], t), (v, []), (v, [(0, 1, 3)]), (v, t, []), (v, t, [0, 0]),
                     ([(math.nan, 0, 0)], [(0, 0, 0)])):
            with self.assertRaises(ValueError):
                a.TriangleIndex(*args)
        with self.assertRaises(ValueError):
            a.TriangleIndex(v, t).nearest((math.inf, 0, 0))

    def test_open_mesh_and_exact_seam_weld(self):
        v = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
        result = a.topology(v, [(0, 1, 2), (3, 4, 5)])
        self.assertEqual((result['welded_used_vertices'], result['connected_components'],
                          result['boundary_edges'], result['same_direction_pairs']), (4, 1, 4, 0))
        v[3] = (1+1e-10, 0, 0)
        self.assertEqual(a.topology(v, [(0, 1, 2), (3, 4, 5)])['boundary_edges'], 6)

    def test_topology_detects_nonmanifold_and_reversed_orientation(self):
        v = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)]
        self.assertEqual(a.topology(v, [(0, 1, 2)]*3)['nonmanifold_edges'], 3)
        self.assertEqual(a.topology(v, [(0, 1, 2), (1, 2, 3)])['same_direction_pairs'], 1)

    def test_roi_all_vertices_not_centroid_no_cap(self):
        v = [(0, -2, 0), (1, -2, 0), (0, 0, 1), (0, .01, 1)]
        selected, report = a.lower_grip_patch(v, [(0, 1, 2), (0, 1, 3)])
        self.assertEqual(selected, [0])
        self.assertEqual(report['excluded_plane_crossing_triangles'], 1)
        self.assertFalse(report['synthetic_cap_added'])

    def test_strict_crossing_has_analytic_witness_and_winding_independence(self):
        tri = ((0, 0, 0), (4, 0, 0), (0, 4, 0))
        for order in (tri, tuple(reversed(tri))):
            self.assertPoint(a.strict_segment_crossing((1, 1, -2), (1, 1, 3), order), (1, 1, 0))
        other = ((1, 1, -2), (1, 1, 3), (2, 1, 1))
        self.assertIsNotNone(a.strict_triangle_crossing(tri, other))
        self.assertIsNotNone(a.strict_triangle_crossing(other, tri))

    def test_boundary_tangent_coplanar_and_outside_not_counted_strict(self):
        tri = ((0, 0, 0), (4, 0, 0), (0, 4, 0))
        for p, q in (((2, 2, -1), (2, 2, 1)), ((1, 1, 0), (1, 1, 2)),
                     ((1, 1, 0), (2, 1, 0)), ((4, 4, -1), (4, 4, 1)),
                     ((1, 1, -1e-10), (1, 1, 1e-10))):
            self.assertIsNone(a.strict_segment_crossing(p, q, tri))

    def test_nearness_is_unsigned_and_requires_explicit_frame_assumption(self):
        index = a.TriangleIndex([(0, 0, 0), (4, 0, 0), (0, 4, 0)], [(0, 1, 2)])
        samples = [{'vertex': 0, 'finger_support_group': 'pinky',
                    'direct': (1, 1, 2), 'proposed': (1, 1, -2)}]
        with self.assertRaisesRegex(ValueError, 'alignment'):
            a.compare_distances(samples, index, alignment_assumption=None)
        result = a.compare_distances(samples, index, alignment_assumption=a.ALIGNMENT)
        self.assertEqual(result['all_supported_vertices']['direct'], result['all_supported_vertices']['proposed'])
        self.assertIsNone(result['preferred_pose'])
        self.assertFalse(result['signed_penetration_computed'])

    def test_crossings_exclude_partly_unmodeled_hand_triangles(self):
        index = a.TriangleIndex([(0, 0, 0), (4, 0, 0), (0, 4, 0)], [(0, 1, 2)])
        points = ((1, 1, -2), (1, 1, 3), (2, 1, 1))
        samples = [{'vertex': i, 'finger_support_group': 'pinky', 'direct': p,
                    'proposed': tuple(x+10 for x in p)} for i, p in enumerate(points)]
        result = a.compare_crossings(samples, [(0, 1, 2), (0, 1, 99)], index, alignment_assumption=a.ALIGNMENT)
        self.assertEqual(result['closed_hand_triangles'], 1)
        self.assertEqual(result['direct']['crossing_pairs'], 1)
        self.assertEqual(result['proposed']['crossing_pairs'], 0)


class PinnedContactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.body = a.surface.ROOT/'reports/actor_packages/nadine-rural-body.pak'
        cls.skeleton = a.surface.ROOT/'reports/actor_packages/nadine-dlc-base.pak'
        archive = a.weapon.DEFAULT_ARCHIVE
        if not all(p.is_file() for p in (cls.body, cls.skeleton, archive, archive.with_name('dc1.psarc'))):
            raise unittest.SkipTest('pinned game assets unavailable')
        with mock.patch('native_anim_pose_audit.MappedImage', side_effect=AssertionError('retail forbidden')):
            cls.p99, cls.pistol = a.weapon.load_assets()
            cls.pose = direct.audit(cls.skeleton, archive, archive.with_name('dc1.psarc'))
            cls.result = a.audit(cls.p99, cls.pistol, cls.pose, body_path=cls.body,
                                skeleton_path=cls.skeleton, alignment_assumption=a.ALIGNMENT)

    def test_pinned_roi_is_lower_patch_not_closed_engine_region(self):
        result = self.result
        self.assertEqual(result['main_mesh']['topology']['boundary_edges'], 56)
        roi = result['lower_grip_patch']
        self.assertEqual((roi['triangle_count'], roi['excluded_plane_crossing_triangles']), (827, 36))
        self.assertEqual(roi['source_triangle_ids_sha256'],
                         '323c996898d3fdb64f1bbdf65bace921673997cdfbc26cc0d2ab6ab8f3bdf7ed')
        self.assertEqual(roi['topology']['boundary_edges'], 49)
        self.assertFalse(roi['engine_semantic_region'])
        self.assertFalse(roi['full_grip_coverage'])

    def test_same_nadine_subset_no_live_or_quality_claim(self):
        result = self.result
        self.assertEqual(len(result['comparison']['samples']), 3040)
        self.assertEqual(result['closed_vertex_indices_sha256'],
                         '232789ed8d78b7ee6d959f6cc1d46c94aee6b2f48903eb0588fb3dd52afd7aa3')
        self.assertFalse(result['live_frame_proven'])
        self.assertFalse(result['retail_or_game_execution'])
        self.assertFalse(result['skin_contact_or_improvement_proven'])
        self.assertIsNone(result['comparison']['preferred_pose'])

    def test_both_conditional_poses_have_strict_surface_crossing_witnesses(self):
        crossings = self.result['conditional_surface_crossings']
        self.assertEqual(crossings['closed_hand_triangles'], 5526)
        self.assertEqual(crossings['all_body_triangles'], 12194)
        self.assertEqual(crossings['direct']['crossing_pairs'], 223)
        self.assertEqual(crossings['proposed']['crossing_pairs'], 191)
        self.assertFalse(crossings['live_surface_intersection_proven'])
        self.assertFalse(crossings['solid_penetration_depth_computed'])

    def test_unsigned_stats_do_not_uniformly_favor_proposed(self):
        groups = self.result['comparison']['per_finger_support_group']
        self.assertGreater(groups['pinky']['proposed']['mean_asset_units'], groups['pinky']['direct']['mean_asset_units'])
        self.assertLess(groups['ring']['proposed']['mean_asset_units'], groups['ring']['direct']['mean_asset_units'])

    def test_geometry_only_never_silently_assumes_alignment(self):
        result = a.audit(self.p99, self.pistol)
        self.assertNotIn('comparison', result)
        with self.assertRaisesRegex(ValueError, 'alignment'):
            a.audit(self.p99, self.pistol, self.pose)


if __name__ == '__main__':
    unittest.main()
