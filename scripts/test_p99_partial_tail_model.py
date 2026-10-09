import math
import unittest

import p99_grip_composition_audit as grip
import p99_partial_tail_model as model


class PartialTailModelTests(unittest.TestCase):
    def setUp(self):
        self.base = {28: grip.row(translation=(1, 2, 3))}
        self.partial = {28: grip.row(rotation=(0, 0, 1, 0), translation=(3, 6, 9))}

    def test_empty_root_inherits_base_not_identity(self):
        for weight in (0, .01, .5, 1):
            self.assertEqual(model.masked_normal_mix(self.base, {}, weight), self.base)
        self.assertNotEqual(self.base[28], grip.row())

    def test_empty_both_sides_stays_invalid(self):
        self.assertEqual(model.masked_normal_mix({}, {}, .5), {})

    def test_jointly_valid_translation_keeps_product_residual(self):
        for layer in (0, .01, .4, 1):
            for newest in (0, .3, .99, 1):
                lanes = model.two_track_empty_tail(self.base, self.partial, layer, newest)
                expected_weight = model.jointly_valid_translation_residual(layer, newest)
                expected = model.masked_normal_mix(self.base, self.partial, expected_weight)
                for a, b in zip(lanes["layer_result"][28][8:11], expected[28][8:11]):
                    self.assertAlmostEqual(a, b, places=12)

    def test_full_newest_track_returns_base_for_jointly_valid_rows(self):
        for layer in (0, .2, 1):
            result = model.two_track_empty_tail(self.base, self.partial, layer, 1)
            self.assertEqual(result["layer_result"], self.base)

    def test_oldest_only_endpoint_is_layer_weighted_partial(self):
        result = model.two_track_empty_tail(self.base, self.partial, .4, 0)
        expected = model.masked_normal_mix(self.base, self.partial, .4)
        for a, b in zip(result["layer_result"][28], expected[28]):
            self.assertAlmostEqual(a, b, places=14)

    def test_nested_quaternions_do_not_collapse_to_translation_coefficient(self):
        layer, newest = .4, .6
        actual = model.two_track_empty_tail(self.base, self.partial, layer, newest)
        collapsed = model.masked_normal_mix(
            self.base, self.partial, model.jointly_valid_translation_residual(layer, newest))
        self.assertGreater(grip.angular_degrees(actual["layer_result"][28][4:8],
                                               collapsed[28][4:8]), 1)

    def test_one_sided_validity_is_not_scaled_to_zero(self):
        for weight in (0, .25, 1):
            self.assertEqual(model.masked_normal_mix({}, self.partial, weight), self.partial)
        # With no base row, even a fully weighted empty newer lane cannot replace
        # the older valid row. The product formula must NOT be used for this case.
        actual = model.two_track_empty_tail({}, self.partial, .3, 1)
        self.assertEqual(actual["layer_result"], self.partial)

    def test_translation_fit_cannot_identify_two_native_weights(self):
        first = model.two_track_empty_tail(self.base, self.partial, .01, 0)
        second = model.two_track_empty_tail(self.base, self.partial, 1, .99)
        for a, b in zip(first["layer_result"][28][8:11], second["layer_result"][28][8:11]):
            self.assertAlmostEqual(a, b, places=12)
        self.assertAlmostEqual(model.jointly_valid_translation_residual(.01, 0),
                               model.jointly_valid_translation_residual(1, .99))

    def test_invalid_weights_refused_even_for_empty_poses(self):
        for value in (-.01, 1.01, math.nan, math.inf):
            with self.assertRaises(ValueError):
                model.masked_normal_mix({}, {}, value)
            with self.assertRaises(ValueError):
                model.two_track_empty_tail({}, {}, value, .2)
            with self.assertRaises(ValueError):
                model.two_track_empty_tail({}, {}, .2, value)


if __name__ == "__main__":
    unittest.main()
