"""Independent affine/weight guards and pinned conditional surface integration."""
import copy
import math
from pathlib import Path
import struct
import unittest
from unittest import mock

import p99_hand_surface_delta_audit as a
import p99_direct_grip_audit as direct
import pak_skin_influence_audit as skin


def report():
    return {"classification": a.SOURCE_CLASSIFICATION, "quaternion_decoder_model": a.DECODER_MODEL,
            "retail_or_game_execution": False, "runtime_record_output": False,
            "live_grip_or_skin_contact_proven": False,
            "prop_relative_joint_frames": [{"target_global": j, "target_name": str(j),
                "direct_row": list(a.grip.row()), "proposed_row": list(a.grip.row())} for j in a.JOINTS]}


class SurfaceMathTests(unittest.TestCase):
    def test_affine_rows_include_translation_not_transposed(self):
        self.assertEqual(a.affine_point((2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37), (1, 2, 3)),
                         (30., 107., 211.))

    def test_inverse_bind_precedes_pose_rotation_and_translation(self):
        inverse = (1, 0, 0, -1, 0, 1, 0, -2, 0, 0, 1, -3)
        q = (0, 0, math.sqrt(.5), math.sqrt(.5))
        frames = {7: a.grip.row(rotation=q, translation=(5, 7, 11))}
        result = a.skin_point((2, 2, 3), [skin.PackedInfluence(7, 0x3FFFFF)], frames, {7: inverse})
        for value, expected in zip(result, (5, 8, 11)):
            self.assertAlmostEqual(value, expected)

    def test_weighted_translation_no_sum_renormalization(self):
        rows = [skin.PackedInfluence(1, 0x200000), skin.PackedInfluence(2, 0x200000)]
        frames = {1: a.grip.row(translation=(2, 0, 0)), 2: a.grip.row(translation=(0, 4, 0))}
        value = a.skin_point((0, 0, 0), rows, frames, {1: a.IDENTITY, 2: a.IDENTITY})
        self.assertEqual(value, (1.000000238418579, 2.000000476837158, 0.))
        self.assertNotEqual(value, (1., 2., 0.))

    def test_zero_weight_outside_palette_ignored_but_positive_refused(self):
        frames = {1: a.grip.row()}
        good = skin.PackedInfluence(1, 0x3FFFFF)
        self.assertEqual(a.skin_point((1, 2, 3), [good, skin.PackedInfluence(999, 0)],
                                     frames, {1: a.IDENTITY}), (1, 2, 3))
        with self.assertRaisesRegex(ValueError, "escapes"):
            a.skin_point((1, 2, 3), [good, skin.PackedInfluence(999, 0x10000)], frames, {1: a.IDENTITY})

    def test_invalid_scale_quaternion_nonfinite_and_empty_support(self):
        for row in (a.grip.row(scale=(1, 2, 1)), a.grip.row(rotation=(0, 0, 0, 2)),
                    a.grip.row(translation=(math.nan, 0, 0))):
            with self.assertRaises(ValueError):
                a.checked_frame(row)
        with self.assertRaisesRegex(ValueError, "no positive"):
            a.skin_point((0, 0, 0), [skin.PackedInfluence(1, 0)], {}, {})

    def test_report_order_independent_but_set_duplicates_and_prop_checked(self):
        valid = report()
        valid["prop_relative_joint_frames"].reverse()
        self.assertEqual(set(a.report_frames(valid)[0]), set(a.JOINTS))
        valid["prop_relative_joint_frames"][0] = valid["prop_relative_joint_frames"][1]
        with self.assertRaisesRegex(ValueError, "21 joints"):
            a.report_frames(valid)
        valid = report()
        valid["prop_relative_joint_frames"][1]["proposed_row"][8] = .1
        with self.assertRaisesRegex(ValueError, "identity"):
            a.report_frames(valid)

    def test_stale_or_overclaiming_report_refused(self):
        for key, value in (("quaternion_decoder_model", "old"), ("classification", "CONFIRMED"),
                           ("live_grip_or_skin_contact_proven", True), ("retail_or_game_execution", True)):
            changed = report()
            changed[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                a.report_frames(changed)

    def test_only_closed_vertices_compared_and_same_pose_is_zero(self):
        frames = {1: a.grip.row()}
        full = skin.PackedInfluence(1, 0x3FFFFF)
        values = a.compare_surface([(1, 2, 3)]*3,
            [[full], [full, skin.PackedInfluence(2, 0x10000)], [skin.PackedInfluence(2, 0x3FFFFF)]],
            frames, frames, {1: a.IDENTITY})
        self.assertEqual((values["closed_vertices"], values["touched_vertices"], values["excluded_touched_vertices"]), (1, 2, 1))
        self.assertEqual(values["displacement"]["max_asset_units"], 0.)


class PinnedSurfaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = direct.source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
        cls.body = a.ROOT / "reports/actor_packages/nadine-rural-body.pak"
        cls.skeleton = a.ROOT / "reports/actor_packages/nadine-dlc-base.pak"
        if not all(p.exists() for p in (cls.body, cls.skeleton, base/"actor77.psarc", base/"dc1.psarc")):
            raise unittest.SkipTest("pinned local assets unavailable")
        with mock.patch("native_anim_pose_audit.MappedImage", side_effect=AssertionError("retail mapping forbidden")):
            cls.pose = direct.audit(cls.skeleton, base/"actor77.psarc", base/"dc1.psarc")
            cls.result = a.audit(cls.pose, cls.body, cls.skeleton)

    def test_exact_typed_mesh_and_closed_subset(self):
        r = self.result
        self.assertEqual(r["ownership"]["normal_palette"], "1E15C0")
        self.assertEqual((r["all_vertices"], r["touched_vertices"], r["closed_vertices"]), (6448, 4822, 3040))
        self.assertEqual(r["closed_vertex_indices_sha256"], "232789ed8d78b7ee6d959f6cc1d46c94aee6b2f48903eb0588fb3dd52afd7aa3")
        self.assertAlmostEqual(r["displacement"]["max_asset_units"], .02335654264458289, places=12)
        self.assertFalse(r["skin_contact_or_improvement_proven"])
        self.assertFalse(r["runtime_writes"])

    def test_same_pose_asset_control_is_exact_zero(self):
        same = copy.deepcopy(self.pose)
        for row in same["prop_relative_joint_frames"]:
            row["proposed_row"] = row["direct_row"][:]
        self.assertEqual(a.audit(same, self.body, self.skeleton)["displacement"]["max_asset_units"], 0.)

    def test_valid_wrong_mesh_pointer_and_palette_rejected_after_digest_gate(self):
        for offset, source in ((0x18FEE8, 0x18FED8), (0x1975E8, 0x190148)):
            pak = skin.PagedPak(self.body)
            raw = bytearray(pak.data)
            raw[offset:offset+8] = raw[source:source+8]
            pak.data = bytes(raw)
            with self.subTest(offset=hex(offset)), self.assertRaises(ValueError):
                a.body_ownership(pak)


if __name__ == "__main__":
    unittest.main()
