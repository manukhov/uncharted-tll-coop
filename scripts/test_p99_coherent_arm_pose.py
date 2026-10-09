import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import p99_coherent_arm_pose as coherent


class CoherentArmPoseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = coherent.audit()
        cls.authored = coherent.support.audit()
        cls.rows = {n: {r["target_global"]: tuple(r["target_local_row"])
                        for r in c["authored_rows_19_30"]}
                    for n, c in cls.authored["clips"].items()}

    def test_no_live_or_visual_admission(self):
        for key in ("live_proximal_locals_available", "skin_clearance_proven",
                    "runtime_writer_admitted", "game_launched_or_attached", "visual_fix_proven"):
            self.assertFalse(self.report[key])

    def test_full_authored_targets_have_meaningful_bend_without_margin(self):
        for name, expected in (("weapon_up", .44617375615447297),
                               ("weapon_down", .43919374109548204)):
            row = self.report["full_authored"][name]
            self.assertTrue(row["geometrically_attainable"])
            self.assertAlmostEqual(row["requested_reach"], expected, places=10)
            self.assertLess(row["target_excess"], -.08)
            self.assertGreater(row["attainable_target_elbow_radius"], .14)
            self.assertLess(row["pre_wrist_to_target"], .0015)
            self.assertLess(row["left_prop_to_right_prop"], .0015)
            self.assertLess(row["left_prop_to_right_prop_degrees"], .02)

    def test_arm_lengths_match_asset_reference(self):
        for row in self.report["full_authored"].values():
            self.assertAlmostEqual(row["upper"], .28543218970298767, places=12)
            self.assertAlmostEqual(row["lower"], .24556228518486023, places=12)

    def test_up_down_share_left_shoulder_origin(self):
        full = self.report["full_authored"]
        self.assertEqual(full["weapon_up"]["shoulder_in_common18"],
                         full["weapon_down"]["shoulder_in_common18"])

    def test_left_proximal_without_clavicle_cannot_repair_endpoint_distance(self):
        up, down = self.rows["weapon_up"], self.rows["weapon_down"]
        original = coherent.geometry(up)
        mixed = coherent.geometry(coherent.substitute(up, down, (21, 23, 25)))
        for key in ("requested_reach", "target_excess"):
            self.assertAlmostEqual(original[key], mixed[key], places=14)
        self.assertEqual(original["shoulder_in_right_prop"], mixed["shoulder_in_right_prop"])
        self.assertGreater(abs(original["pre_wrist_to_target"] - mixed["pre_wrist_to_target"]), .1)

    def test_common_ancestor_does_not_change_reach_or_contact(self):
        ancestor = coherent.support.grip.row(
            rotation=coherent.support.grip.qnorm((.2, -.4, .3, .7)),
            translation=(4, -9, 13))
        for rows in self.rows.values():
            before = coherent.geometry(rows)
            after = coherent.geometry(rows, ancestor)
            for key in ("requested_reach", "target_excess", "pre_reach",
                        "pre_elbow_radius", "attainable_target_elbow_radius",
                        "pre_wrist_to_target", "left_prop_to_right_prop"):
                self.assertAlmostEqual(before[key], after[key], places=12)

    def test_synthetic_mixtures_are_not_current_pose(self):
        self.assertIn("synthetic_bind_controls_NOT_current_pose", self.report)
        self.assertIn("synthetic_endpoint_mixtures_NOT_current_pose", self.report)
        for name, rows in self.rows.items():
            full = self.report["full_authored"][name]
            synthetic = self.report["synthetic_bind_controls_NOT_current_pose"][name]
            self.assertEqual(full, synthetic["both_proximal_and_distal"])

    def test_raw_reference_lengths_are_not_unscaled_point30045(self):
        lengths = self.report["raw_nadine_reference_segment_lengths"]
        self.assertEqual(lengths[23], .28543218970298767)
        self.assertEqual(lengths[25], .24556228518486023)
        self.assertGreater(abs(lengths[23] * .95 - .28543), .014)

    def test_full_distal_with_proximal_q_preserves_asset_contact_conditionally(self):
        for controls in self.report["synthetic_bind_controls_NOT_current_pose"].values():
            row = controls["proximal_rotations_keep_bind_translations_with_full_distal"]
            self.assertTrue(row["geometrically_attainable"])
            self.assertGreater(row["attainable_target_elbow_radius"], .14)
            self.assertLess(row["pre_wrist_to_target"], .0016)

    def test_invalid_rows_and_degenerate_axis_refuse(self):
        with self.assertRaises(ValueError):
            coherent.arm_world({})
        with self.assertRaises(ValueError):
            coherent._radius((0, 0, 0), (1, 0, 0), (0, 0, 0))


if __name__ == "__main__":
    unittest.main()
