import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import p99_hand_surface_delta_audit as surface
import p99_npc_grip_candidate as candidate


class NpcGripCandidateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = candidate.audit()

    def test_separate_disk_only_schema_and_exact_joint_contract(self):
        report = self.report
        self.assertEqual(report["schema"], "tll-p99-npc-grip-candidate-v1")
        self.assertEqual(report["classification"],
                         "STATIC_DISK_ONLY_NATIVE_NPC_GRIP_CANDIDATE")
        self.assertFalse(report["retail_executable_mapped"])
        self.assertFalse(report["native_routine_called"])
        self.assertFalse(report["game_launched_or_attached"])
        self.assertNotEqual(report["classification"], surface.SOURCE_CLASSIFICATION)
        rows = report["candidate_rows"]
        self.assertEqual([row["target_global"] for row in rows], list(surface.JOINTS))
        self.assertTrue(all(set(row) == {"target_global", "target_name", "local_row",
                                        "prop_relative_row"} for row in rows))
        self.assertEqual(rows[1]["target_name"], "r_hand_prop_attachment")
        identity = [1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
                    0.0, 0.0, 0.0, 0.0]
        self.assertLess(max(abs(a - b) for a, b in
                            zip(rows[1]["prop_relative_row"], identity)), 1e-12)

    def test_authored_clip_and_exact_row149_are_pinned(self):
        report = self.report
        self.assertEqual(report["inputs"]["animation_sha256"], candidate.ANIMATION_SHA256)
        self.assertEqual(report["inputs"]["clip_raw_sha256"], candidate.CLIP_RAW_SHA256)
        self.assertEqual(report["source_rows"]["f32_sha256"],
                         candidate.SOURCE_ROWS_F32_SHA256)
        self.assertEqual(report["source_rows"]["groups"], [
            {"group": 0, "selected_locals": [28, 30],
             "selected_globals": [28, 30]},
            {"group": 1, "selected_locals": list(range(1, 38, 2)),
             "selected_globals": list(range(68, 105, 2))},
        ])
        self.assertEqual([row["source_name"] for row in report["authored_selection"][:2]],
                         ["r_palm", "r_hand_prop_attachment"])
        self.assertEqual(report["candidate_rows_contract"]["local_rows_f32_sha256"],
                         candidate.CANDIDATE_LOCAL_ROWS_F32_SHA256)
        self.assertEqual(report["candidate_rows_contract"]["prop_relative_rows_f32_sha256"],
                         candidate.CANDIDATE_PROP_ROWS_F32_SHA256)
        self.assertEqual(report["retarget"]["row_index"], 149)
        self.assertEqual(report["retarget"]["row_offset"], "1C40")
        self.assertEqual(report["retarget"]["map_offset"], "2AA0")
        self.assertEqual(report["retarget"]["target_profile"], "00000000")
        self.assertEqual(report["retarget"]["selected_mode_census"],
                         {"mode1": 20, "mode3_weapon_prop": 1})

    def test_candidate_is_numerically_distinct_from_current_r124_b8(self):
        comparison = self.report["comparison_to_current_r124_b8"]
        rows = {row["target_global"]: row for row in comparison["rows"]}
        self.assertAlmostEqual(rows[28]["local_rotation_delta_degrees"],
                               0.011864069, places=6)
        self.assertAlmostEqual(rows[30]["local_rotation_delta_degrees"],
                               15.474411327, places=6)
        self.assertEqual(comparison["max_local_rotation"]["target_global"], 139)
        self.assertGreater(comparison["max_local_rotation"]["local_rotation_delta_degrees"], 68.1)
        self.assertEqual(comparison["max_prop_relative_origin"]["target_global"], 141)
        self.assertTrue(math.isclose(
            comparison["max_prop_relative_origin"]["prop_relative_origin_delta_asset_units"],
            0.0450475508, rel_tol=0, abs_tol=1e-8))


if __name__ == "__main__":
    unittest.main()
