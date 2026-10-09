from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import p99_default_hands_baseline as default


class DefaultHandsBaselineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = default.audit()

    def test_multisample_headers_do_not_imply_variable_group_zero(self):
        for key, samples in (("chloe_default_hands", 337), ("player_default_hands", 301)):
            clip = self.report["clips"][key]
            self.assertEqual(clip["samples"][0]["sample"], 1)
            self.assertEqual(clip["header_sample_count"], samples)
            self.assertEqual(clip["group0_opcodes"], [22, 24, 0])
            self.assertTrue(clip["group0_is_constant"])

    def test_corrected_opcode24_order_is_retained(self):
        rows = self.report["clips"]["chloe_default_hands"]["source_rows"]
        self.assertEqual(rows[39][4:8], (-.005632162094116211, -.08713674545288086,
                                       .13823628425598145, .9865425229072571))
        self.assertEqual(rows[41][4:8], (.806647777557373, .574308454990387,
                                       .04781949520111084, -.13115829229354858))

    def test_chloe_default_is_close_but_not_identical_to_all_four_pairs(self):
        samples = self.report["clips"]["chloe_default_hands"]["samples"]
        self.assertEqual(len(samples), 4)
        for sample in samples:
            self.assertGreater(sample["rotation_gap_degrees"], .05)
            self.assertLess(sample["rotation_gap_degrees"], .15)
            self.assertLess(sample["position_gap_asset_units"], .0003)

    def test_conditional_target_rows_expose_exact_palm_and_prop(self):
        rows = self.report["clips"]["chloe_default_hands"]["conditional_target_rows"]
        self.assertEqual(set(rows), {28, 30})
        self.assertEqual(rows[30][8:11], (-.0018434524536132812, .0294952392578125,
                                        -.01934814453125))
        self.assertEqual(rows[28][8:11], (2.7088802312391635e-7, -8.56068709254032e-8,
                                        -.050809573382139206))

    def test_player_conditional_path_has_distinct_prop_translation(self):
        clips = self.report["clips"]
        chloe = clips["chloe_default_hands"]["conditional_target_rows"][30]
        player = clips["player_default_hands"]["conditional_target_rows"][30]
        self.assertEqual(chloe[4:8], player[4:8])
        self.assertNotEqual(chloe[8:11], player[8:11])
        self.assertEqual(clips["player_default_hands"]["conditional_retarget_path"], "R20_then_R157")

    def test_small_layer_fit_needs_no_arbitrary_point95_scale(self):
        fits = self.report["default_to_conditional_R124_D350_B8_numeric_fit_NOT_native_weight"]
        self.assertEqual(len(fits), 4)
        for fit in fits:
            self.assertLess(fit["numerically_fitted_weight_not_native_observation"], .011)
            self.assertGreater(fit["numerically_fitted_weight_not_native_observation"], .007)
            self.assertLess(fit["rotation_residual_degrees"], .001)
            self.assertLess(fit["translation_residual_no_scale"], .00004)

    def test_fit_improves_pure_default_in_rotation_and_unscaled_translation(self):
        fits = self.report["default_to_conditional_R124_D350_B8_numeric_fit_NOT_native_weight"]
        default_samples = self.report["clips"]["chloe_default_hands"]["samples"]
        for fit, pure in zip(fits, default_samples):
            self.assertEqual(fit["sample"], pure["sample"])
            self.assertLess(fit["rotation_residual_degrees"], pure["rotation_gap_degrees"] / 100)
            self.assertLess(fit["translation_residual_no_scale"], pure["position_gap_asset_units"] / 5)
            self.assertLess(abs(fit["whole_chain_scale_control"]["fitted_scale"] - 1), .0001)

    def test_two_different_partial_paths_both_fit_so_source_is_not_identified(self):
        first = self.report["default_to_conditional_R124_D350_B8_numeric_fit_NOT_native_weight"]
        second = self.report["default_to_conditional_Chloe_R157_R20_B8_numeric_fit_NOT_native_weight"]
        self.assertEqual(len(second), 4)
        for direct, chloe in zip(first, second):
            self.assertEqual(direct["sample"], chloe["sample"])
            self.assertLess(chloe["rotation_residual_degrees"], .001)
            self.assertLess(chloe["translation_residual_no_scale"], .00004)
            self.assertNotEqual(direct["numerically_fitted_weight_not_native_observation"],
                                chloe["numerically_fitted_weight_not_native_observation"])
        self.assertFalse(self.report["live_clip_selection_proven"])

    def test_numeric_fit_does_not_admit_writer_or_recover_native_weight(self):
        self.assertEqual(self.report["capture_pid"], 17512)
        self.assertEqual(self.report["capture_context"], default.live.capture_context())
        self.assertFalse(self.report["capture_context"]["steady_animation_proven"])
        for key in ("native_profile_choice_observed", "live_clip_selection_proven",
                    "live_layer_weight_proven", "live_local_rows_observed",
                    "runtime_writer_admitted", "visual_fix_proven"):
            self.assertFalse(self.report[key])


if __name__ == "__main__":
    unittest.main()
