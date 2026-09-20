"""Pure locator algebra/ABI regressions; frozen native data are read, not run."""
import json
from pathlib import Path
import random
import struct
import tempfile
import unittest
from unittest import mock

import p99_locator_basis_audit as a


def rotation(q, translation=(0., 0., 0.)):
    x, y, z, w = q
    return (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), translation[0],
            2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), translation[1],
            2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), translation[2])


def compose(left, right):
    return tuple(sum(left[4 * row + k] * right[4 * k + column] for k in range(3))
                 + (left[4 * row + 3] if column == 3 else 0.)
                 for row in range(3) for column in range(4))


class LocatorTests(unittest.TestCase):
    def assert_values_close(self, left, right, tolerance=1e-12):
        self.assertEqual(len(left), len(right))
        self.assertLessEqual(max(abs(x - y) for x, y in zip(left, right)), tolerance)

    def test_affine_inverse_handles_scale_shear_and_translation(self):
        matrix = (2., .3, -.4, 11., .1, 3., .2, -7., .2, -.1, 4., 5.)
        inverse = a.affine_inverse(matrix)
        self.assert_values_close(compose(matrix, inverse), a.IDENTITY)
        self.assert_values_close(compose(inverse, matrix), a.IDENTITY)
        self.assert_values_close(a.affine_inverse(inverse), matrix)
        self.assertNotEqual(inverse[0], matrix[0])  # Not a transpose-only inverse.

    def test_bad_affine_inputs_are_refused(self):
        for values in ((), a.IDENTITY[:-1], (0.,) * 12,
                       (float("nan"),) + a.IDENTITY[1:],
                       a.IDENTITY[:-1] + (float("inf"),)):
            with self.subTest(values=values):
                with self.assertRaises(ValueError):
                    a.affine_inverse(values)

    def test_all_matrix_quaternion_branches(self):
        for q, expected in (((0., 0., 0., 1.), "positive-trace"),
                            ((1., 0., 0., 0.), "largest-x"),
                            ((0., 1., 0., 0.), "largest-y"),
                            ((0., 0., 1., 0.), "largest-z")):
            with self.subTest(branch=expected):
                actual, branch = a.quaternion_from_basis(rotation(q))
                self.assertEqual(branch, expected)
                self.assertLess(a.sign_error(actual, q), 1e-15)

    def test_negative_trace_ties_prefer_first_largest_axis(self):
        for q, expected in ((a.normalized((1., 1., 0., 0.)), "largest-x"),
                            (a.normalized((0., 1., 1., 0.)), "largest-y"),
                            (a.normalized((1., 1., 1., 0.)), "largest-x")):
            actual, branch = a.quaternion_from_basis(rotation(q))
            self.assertEqual(branch, expected)
            self.assertLess(a.sign_error(actual, q), 1e-15)

    def test_positive_trace_and_sign_convention(self):
        q = a.normalized((.1, .2, .3, .9))
        actual, branch = a.quaternion_from_basis(rotation(q, (3., 4., 5.)))
        self.assertEqual(branch, "positive-trace")
        self.assertLess(a.sign_error(actual, q), 1e-15)
        self.assertGreater(a.sign_error(actual, a.conjugate(q)), .2)

    def test_joint_locator_is_conjugate_source_times_target(self):
        src = a.normalized((.2, -.5, .7, .4))
        dst = a.normalized((-.3, .2, .4, .8))
        result = a.build_locator_quaternions(a.affine_inverse(rotation(src)),
                                            a.affine_inverse(rotation(dst)))
        expected = a.multiply(a.conjugate(src), dst)
        self.assertLess(a.sign_error(result.unquantized, expected), 1e-15)
        self.assertGreater(a.sign_error(result.unquantized,
                                       a.multiply(dst, a.conjugate(src))), .1)
        self.assertEqual(result.packed, a.pack_quaternion(expected))

    def test_translation_does_not_change_locator_quaternion(self):
        src, dst = a.normalized((1., 2., 3., 4.)), a.normalized((4., 3., 2., 1.))
        fixed = a.build_locator_quaternions(a.affine_inverse(rotation(src)),
                                           a.affine_inverse(rotation(dst)))
        moved = a.build_locator_quaternions(a.affine_inverse(rotation(src, (7., 8., 9.))),
                                           a.affine_inverse(rotation(dst, (-4., 1., 5.))))
        self.assertEqual(fixed, moved)

    def test_256_nontrivial_basis_pairs(self):
        randomizer = random.Random(0xCF634)
        for _ in range(256):
            src, dst = (a.normalized(tuple(randomizer.uniform(-1., 1.) for _ in range(4)))
                        for _ in range(2))
            result = a.build_locator_quaternions(a.affine_inverse(rotation(src)),
                                                a.affine_inverse(rotation(dst)))
            expected = a.multiply(a.conjugate(src), dst)
            self.assertLess(a.sign_error(result.unquantized, expected), 2e-15)
            self.assertLess(abs(sum(value * value for value in result.normalized) - 1), 1e-15)

    def test_half_rounding_is_nearest_even_after_float32(self):
        packed = a.pack_quaternion((1. + 2. ** -11, 1. + 3 * 2. ** -11, -0., 1.))
        self.assertEqual(struct.unpack("<4H", packed), (0x3C00, 0x3C02, 0x8000, 0x3C00))
        # This lies above the half midpoint in double but rounds to that exact
        # midpoint in float32, so vcvtps2ph immediate0 chooses even1.0.
        value = 1. + 2. ** -11 + 2. ** -30
        self.assertNotEqual(struct.pack("<e", value), a.pack_quaternion((value, 0., 0., 1.))[:2])
        self.assertEqual(a.pack_quaternion((value, 0., 0., 1.))[:2], b"\x00\x3c")

    def test_packing_does_not_pre_normalize_but_consumer_view_does(self):
        packed = a.pack_quaternion((0., 0., 0., 2.))
        self.assertEqual(struct.unpack("<4e", packed), (0., 0., 0., 2.))
        self.assertEqual(a.normalized(struct.unpack("<4e", packed)), (0., 0., 0., 1.))
        self.assertEqual(a.sign_error((0., 0., 0., 1.), (0., 0., 0., 2.)), 1.)

    def test_nonfinite_zero_underflow_and_overflow_halves_refused(self):
        for q in ((0., -0., 0., -0.), (1e-100, 0., 0., 0.),
                  (float("nan"), 0., 0., 1.), (float("inf"), 0., 0., 1.),
                  (1e100, 0., 0., 1.), (65536., 0., 0., 1.)):
            with self.subTest(q=q):
                with self.assertRaises(ValueError):
                    a.pack_quaternion(q)

    def test_matched_roots_do_not_touch_arrays(self):
        class Untouchable:
            def __len__(self):
                raise AssertionError("matched root must not inspect storage")
            def __getitem__(self, index):
                raise AssertionError("matched root must not read storage")
        result = a.parent_locator(Untouchable(), Untouchable(), -1, -1)
        self.assertEqual(result.decoded, (0., 0., 0., 1.))

    def test_parent_indices_never_wrap(self):
        for src, dst in ((-1, 0), (0, -1), (-2, -2), (1, 0), (0, 1),
                         (0.5, 0), (True, 0)):
            with self.subTest(src=src, dst=dst):
                with self.assertRaises(ValueError):
                    a.parent_locator((a.IDENTITY,), (a.IDENTITY,), src, dst)
        self.assertEqual(a.parent_locator((a.IDENTITY,), (a.IDENTITY,), 0, 0).decoded,
                         (0., 0., 0., 1.))

    def test_missing_or_changed_frozen_artifact_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            # A custom nonexistent prefix must fail, not silently fall back to
            # the default artifacts. No game assets are read on this path.
            with mock.patch.object(a.frozen, "load_metadata", return_value=[]):
                with self.assertRaises(FileNotFoundError):
                    a.audit(prefix=Path(directory) / "absent")
        with mock.patch.object(a.frozen, "load_metadata", return_value=[]):
            with mock.patch.object(Path, "read_bytes", return_value=bytes(a.frozen.PINS["input"][0])):
                with self.assertRaisesRegex(ValueError, "changed frozen input"):
                    a.audit()
        with self.assertRaises(ValueError):
            a.load_basis(b"not the pinned asset", a.source.PLAYER_SKELETON)


class FrozenLocatorTests(unittest.TestCase):
    @unittest.skipUnless(a.frozen.DEFAULT_METADATA.exists()
                         and Path(str(a.frozen.DEFAULT_PREFIX) + ".full-input.bin").exists()
                         and a.DEFAULT_ARCHIVE.exists(), "pinned local assets/artifacts not present")
    def test_all_84_frozen_locators(self):
        result = a.audit()
        self.assertFalse(result["retail_or_game_execution"])
        self.assertFalse(result["runtime_record_output"])
        self.assertFalse(result["row124_profile_evaluated"])
        self.assertEqual([entry["locator_count"] for entry in result["mappings"]], [42, 42])
        for entry in result["mappings"]:
            self.assertLessEqual(entry["max_quantized_component_error"], a.LOCATOR_TOLERANCE)
            self.assertGreater(entry["wrong_order_component_error"], .1)
        json.dumps(result, allow_nan=False)


if __name__ == "__main__":
    unittest.main()
