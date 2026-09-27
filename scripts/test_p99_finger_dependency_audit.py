"""Pinned propagation/export separation, not an engine-helper simulation."""
from pathlib import Path
import struct
import unittest
from unittest import mock

import p99_finger_dependency_audit as a


class FingerDependencies(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.skeleton_path = a.ROOT/"reports/actor_packages/nadine-dlc-base.pak"
        cls.body_path = a.ROOT/"reports/actor_packages/nadine-rural-body.pak"
        if not cls.skeleton_path.is_file() or not cls.body_path.is_file():
            raise unittest.SkipTest("pinned Nadine assets not installed")
        cls.data = cls.skeleton_path.read_bytes()
        cls.skeleton = a.source.load_skeleton(cls.data, a.source.NADINE_SKELETON)
        cls.report = a.audit(cls.skeleton_path, cls.body_path)

    def changed(self, offset, value):
        data = bytearray(self.data)
        struct.pack_into("<H", data, offset, value)
        return bytes(data)

    def test_left_fingers_are_not_helpers(self):
        self.assertEqual(self.report["left_chains"][-1], [136, 138, 140])
        self.assertEqual(self.report["right_chains"][-1], [137, 139, 141])
        self.assertNotIn("140", self.report["outside_joint_producers"])

    def test_changed_matrix_descendants_are_not_changed_local_quaternions(self):
        q = self.report["q_sensitivity"]["137"]
        self.assertEqual(q["affected_group1_matrices"], [137, 139, 141])
        self.assertEqual(q["changed_local_jointparams"], [137])
        self.assertEqual(q["changed_shared_quaternion_exports"], ["A60"])
        self.assertFalse(q["palm28_or_prop30_changed_by_group1"])
        self.assertEqual(a.descendants(133), (133, 135))
        with self.assertRaises(ValueError):
            a.descendants(140)

    def test_no_export_does_not_mean_no_matrix_change(self):
        q = self.report["q_sensitivity"]["105"]
        self.assertEqual(q["changed_shared_quaternion_exports"], [])
        self.assertEqual(q["affected_group1_matrices"], [105, 107, 109, 111])
        ring = self.report["q_sensitivity"]["113"]
        self.assertEqual(ring["affected_group1_matrices"], [113, 115, 117, 119])
        self.assertTrue(ring["all_group1_shared_exports_unchanged"])
        self.assertFalse(self.report["q_sensitivity"]["115"]["all_group1_shared_exports_unchanged"])

    def test_mixed_support_counts_are_not_unique_vertex_sum(self):
        self.assertEqual(self.report["closed_vertices"], 3040)
        self.assertEqual(self.report["excluded_mixed_vertices"], 1782)
        self.assertEqual(self.report["outside_joint_positive_vertex_counts_overlapping"],
                         {26: 1782, 88: 351, 535: 91, 536: 287, 537: 310, 554: 267})

    def test_procedural_helpers_remain_explicitly_unresolved(self):
        self.assertFalse(self.report["helper_replay_proven"])
        helpers = self.report["outside_joint_producers"]
        self.assertEqual(helpers["88"]["group"], 0)
        self.assertEqual(helpers["536"]["group"], 6)
        self.assertEqual(helpers["536"]["finger_dependency"],
                         "UNRESOLVED_PROCEDURAL_INPUT_DEPENDENCY")
        self.assertEqual([w["opcode"] for w in helpers["536"]["jointparams_writers"]],
                         [52, 47, 44])

    def test_right_root_cannot_silently_take_left_palm(self):
        # Second op40 record belongs to right pinky and normally usesFDB.
        offset = 0x5CD70+0x20+4+2
        self.assertEqual(struct.unpack_from("<H", self.data, offset)[0], 0xFDB)
        with self.assertRaises(ValueError):
            a.prove_group1(self.changed(offset, 0xF9B), self.skeleton)

    def test_export_cannot_overwrite_parent_input(self):
        with self.assertRaises(ValueError):
            a.prove_group1(self.changed(0x5CD70+0xC0, 0xFD3), self.skeleton)

    def test_export_cannot_be_changed_to_local_jointparams_write(self):
        with self.assertRaises(ValueError):
            a.prove_group1(self.changed(0x5CD70+0xC0, 0x40), self.skeleton)

    def test_changed_local_export_origin_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "export map"):
            a.prove_group1(self.changed(0x5CD70+0xC0+2, 0x70), self.skeleton)

    def test_audit_calls_no_retail_evaluator(self):
        with mock.patch("native_anim_pose_audit.MappedImage",
                        side_effect=AssertionError("retail forbidden")):
            report = a.audit(self.skeleton_path, self.body_path)
        self.assertFalse(report["retail_or_game_execution"])


if __name__ == "__main__":
    unittest.main()
