import math
import unittest

import p99_live_grip_baseline as audit


class LiveGripBaselineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = audit.audit()

    def test_fixture_and_no_native_authority(self):
        self.assertEqual(len(audit.observed_rows()), 4)
        for key in ("live_clip_selection_proven", "live_weight_proven",
                    "live_chain_scale_observed", "pose_writer_admitted"):
            self.assertFalse(self.report[key])
        self.assertEqual(self.report["skeleton_sha256"],
                         "2bf66e61fae156a41952d8d2659e06215dbe8f0c827a9f6a89cada0c9490b06b")

    def test_near_bind_but_not_exact_bind(self):
        angles = [s["bind_rotation_error_degrees"] for s in self.report["samples"]]
        self.assertAlmostEqual(angles[0], .1185904978, places=8)
        self.assertAlmostEqual(angles[-1], .09201942845, places=8)
        self.assertTrue(all(a > b > 0 for a, b in zip(angles, angles[1:])))
        for s in self.report["samples"]:
            self.assertGreater(s["bind_whole_chain_scale_control"]["residual_asset_units"], .001)

    def test_bind_direct_fade_distinguishes_npc_direction(self):
        previous = math.inf
        for sample in self.report["samples"]:
            direct = sample["fits"]["conditional_direct_R124_B8"]
            weight = direct["numerically_fitted_weight_not_native_observation"]
            self.assertTrue(0 < weight < .01)
            self.assertLess(weight, previous)
            previous = weight
            self.assertLess(direct["rotation_residual_degrees"], .003)
            self.assertGreater(sample["fits"]["native_NPC_4CAA7420"]["rotation_residual_degrees"], .09)
            self.assertLess(direct["hypothetical_prop_only_fixed_095_residual"], .00006)

    def test_synthetic_weight_and_scale_controls(self):
        self.assertAlmostEqual(audit.fit_weight(lambda x: (x-.37)**2), .37, places=10)
        self.assertEqual(audit.fit_weight(lambda x: x*x), 0)
        self.assertEqual(audit.fit_weight(lambda x: (1-x)**2), 1)
        result = audit.scalar_fit((1., 2., 3.), (.95, 1.9, 2.85))
        self.assertAlmostEqual(result["fitted_scale"], .95)
        self.assertLess(result["residual_asset_units"], 1e-15)

    def test_rotation_hemisphere_and_invalid_inputs(self):
        first = audit.grip.row(rotation=(0, 0, 0, 1))
        other = audit.grip.row(rotation=(0, 0, 0, -1))
        self.assertEqual(audit.normal_mix(first, other, .4), first)
        for weight in (-.1, 1.1, math.nan):
            with self.assertRaises(ValueError):
                audit.normal_mix(first, other, weight)
        with self.assertRaises(ValueError):
            audit.scalar_fit((0., 0., 0.), (1., 2., 3.))

    def test_product_does_not_identify_palm_and_prop(self):
        palm, prop = audit.local_pair(self.report["bind_local_rows"],
                                      self.report["bind_local_rows"], 0)
        change = audit.grip.row(rotation=(0, 0, math.sin(.1), math.cos(.1)),
                                translation=(.003, -.002, .001))
        inverse_q = audit.grip.qconj(change[4:8])
        inverse = audit.grip.row(rotation=inverse_q, translation=audit.grip.rotate(
            inverse_q, tuple(-x for x in change[8:11])))
        alternative_palm = audit.grip.blend(palm, change)
        alternative_prop = audit.grip.blend(inverse, prop)
        expected = audit.compose_pair(palm, prop)
        actual = audit.compose_pair(alternative_palm, alternative_prop)
        self.assertGreater(audit.grip.angular_degrees(palm[4:8], alternative_palm[4:8]), 10)
        self.assertLess(audit.grip.angular_degrees(expected[4:8], actual[4:8]), 1e-10)
        self.assertLess(math.dist(expected[8:11], actual[8:11]), 1e-15)


if __name__ == "__main__":
    unittest.main()
