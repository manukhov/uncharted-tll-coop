"""Independent pure comparators; optional frozen assets are read, never executed."""
from dataclasses import replace
import json
import math
from pathlib import Path
import struct
from types import SimpleNamespace
import unittest
from unittest import mock

try:
    import p99_direct_grip_audit as a
except ModuleNotFoundError as error:
    # The matrix dependency is developed independently. Do not disguise a
    # missing unrelated dependency or a broken import as an optional skip.
    if error.name != "p99_profile_matrix_audit":
        raise
    a = None


SQRT_HALF = math.sqrt(.5)
IDENTITY_Q = (0., 0., 0., 1.)
X90 = (SQRT_HALF, 0., 0., SQRT_HALF)
Y90 = (0., SQRT_HALF, 0., SQRT_HALF)
Z90 = (0., 0., SQRT_HALF, SQRT_HALF)


def row(q=IDENTITY_Q, scale=(1., 1., 1.), translation=(0., 0., 0.)):
    return (*scale, 0., *q, *translation, 0.)


def locator(q=IDENTITY_Q):
    # Distinguish the consumer's normalized view from every earlier view.
    return SimpleNamespace(normalized=q, decoded=(3., 4., 5., 6.),
                           unquantized=(7., 8., 9., 10.))


def record(mode=1, joint=IDENTITY_Q, parent=IDENTITY_Q):
    data = bytearray(46)
    struct.pack_into("<4e", data, 6, *joint)
    struct.pack_into("<4e", data, 0x14, *parent)
    data[44] = mode
    return bytes(data)


def semantic_fixture():
    transfers, records = [], []
    for index in range(21):
        semantic = 0x10100000 + index
        src, dst = 100 + 3*index, 200 + 2*index
        transfers.append({"semantic": f"{semantic:08X}",
                          "player_global": src, "nadine_global": dst})
        records.append(a.source.RuntimeRecord(index, semantic, semantic,
                                              3 if index == 1 else 1, src, dst))
    records.extend(a.source.RuntimeRecord(21+i, 0x20200000+i, 0x20200000+i,
                                          0, 400+i, 500+i) for i in range(50))
    return transfers, records


@unittest.skipIf(a is None, "pure matrix module not yet available")
class DirectGripTests(unittest.TestCase):
    def assert_components(self, actual, expected, tolerance=1e-12):
        self.assertEqual(len(actual), len(expected))
        self.assertLessEqual(max(abs(x-y) for x, y in zip(actual, expected)), tolerance)

    def test_column_major_layout_keeps_rows_and_translation(self):
        columns = (2., 7., 13., 0., 3., 11., 17., 0.,
                   5., 19., 23., 0., 29., 31., 37., 1.)
        self.assertEqual(a.column_major_to_mat34(columns),
                         (2., 3., 5., 29., 7., 11., 19., 31., 13., 17., 23., 37.))
        self.assertNotEqual(a.column_major_to_mat34(columns), columns[:12])

    def test_nonaffine_nonfinite_and_wrong_extent_matrices_fail(self):
        identity = (1., 0., 0., 0., 0., 1., 0., 0.,
                    0., 0., 1., 0., 0., 0., 0., 1.)
        for index, value in ((3, 1.), (7, -.5), (11, 1e-20), (15, 0.),
                             (0, math.nan), (12, math.inf)):
            with self.subTest(index=index, value=value):
                changed = list(identity)
                changed[index] = value
                with self.assertRaises(ValueError):
                    a.column_major_to_mat34(changed)
        for changed in (identity[:-1], identity + (0.,), ()):
            with self.assertRaises(ValueError):
                a.column_major_to_mat34(changed)

    def test_mode1_uses_raw_receiver_ST_and_not_player_or_profile_rows(self):
        player = row(Z90, (2., 3., 4.), (11., 13., 17.))
        raw_reference = row(X90, (5., 7., 9.), (-19., 23., -29.))
        actual = a.direct_base(player, raw_reference, 1, locator(), locator())
        self.assertEqual(actual[:3], raw_reference[:3])
        self.assertEqual(actual[8:11], raw_reference[8:11])
        self.assertEqual(actual[4:8], Z90)  # Target reference Q is not consumed.
        self.assertNotEqual(actual[:3], player[:3])
        self.assertNotEqual(actual[8:11], player[8:11])

    def test_mode1_locator_order_and_conjugation_are_independently_known(self):
        # conjugate(X90) * Z90 * Y90 = (-sqrt(.5), sqrt(.5), 0, 0).
        # All rotations are noncommuting; swapping parent/joint or dropping
        # conjugation does not satisfy this hand-derived result.
        actual = a.direct_base(row(Z90), row(), 1, locator(Y90), locator(X90))
        self.assert_components(actual[4:8], (-SQRT_HALF, SQRT_HALF, 0., 0.))

    def test_mode3_preserves_entire_source_and_never_consumes_reference_or_locators(self):
        class Unreadable:
            def __getattribute__(self, name):
                raise AssertionError("mode3 must not consume a locator")
            def __getitem__(self, key):
                raise AssertionError("mode3 must not consume receiver reference")
        player = list(row(Z90, (2., 3., 4.), (5., 6., 7.)))
        player[3], player[11] = 12., -13.
        self.assertEqual(a.direct_base(player, Unreadable(), 3,
                                       Unreadable(), Unreadable()), tuple(player))
        # This is the pure copy equation, not a claim that native mode3 skips
        # decoding/validating both packed locators before the mode switch.

    def test_unsupported_modes_are_rejected(self):
        for mode in (-1, 0, 2, 4, 255):
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                a.direct_base(row(), row(), mode, locator(), locator())

    def test_rotation_delta_survives_common_additive_right_multiplication(self):
        reference = row(translation=(3., 5., 7.))
        actual, comparison = a.compare_row(row(), row(Z90), reference, 1,
            locator(X90), locator(), record(), row(), row(Z90, translation=(3., 5., 7.)))
        self.assert_components(actual[4:8], (.5, -.5, .5, .5))  # X90 * Z90.
        self.assertAlmostEqual(comparison["base_rotation_delta_degrees"], 90.)
        self.assertAlmostEqual(comparison["composite_rotation_delta_degrees"], 90.)
        self.assertEqual(comparison["base_ST_component_delta"], 0.)
        self.assertEqual(comparison["composite_ST_component_delta"], 0.)

    def test_additive_retarget_uses_conjugated_joint_and_identity_ST(self):
        reference = row(scale=(2., 3., 4.), translation=(5., 7., 11.))
        # conjugate(X90) * Z90 * X90 = Y90; arbitrary additive S/T must
        # disappear in mode1. An absolute-retarget call would not do this.
        additive = row(Z90, (7., 8., 9.), (17., 19., 23.))
        actual, _ = a.compare_row(row(), additive, reference, 1,
            locator(), locator(), record(joint=X90), row(), row())
        self.assert_components(actual[4:8], Y90)
        self.assertEqual(actual[:3], reference[:3])
        self.assertEqual(actual[8:11], reference[8:11])

    def test_mode3_prop_is_noop_between_direct_and_composed_routes(self):
        player = row(X90, translation=(1., 2., 3.))
        additive = row(Z90, translation=(0., 1., 0.))
        expected = row((.5, -.5, .5, .5), translation=(1., 2., 4.))
        actual, comparison = a.compare_row(player, additive,
            row(Y90, (8., 9., 10.), (20., 30., 40.)), 3,
            locator(Y90), locator(Z90), record(3, X90, Y90), player, expected)
        self.assert_components(actual, expected)
        self.assertLess(max(comparison.values()), 1e-12)

    def test_bad_frozen_numeric_record_is_not_silently_accepted(self):
        for changed in (record()[:-1], record(2), bytes(46)):
            with self.subTest(length=len(changed)), self.assertRaises(ValueError):
                a.compare_row(row(), row(), row(), 1, locator(), locator(),
                              changed, row(), row())

    def test_direct_and_composed_modes_must_match(self):
        for direct_mode, composed_mode in ((1, 3), (3, 1)):
            with self.subTest(direct=direct_mode, composed=composed_mode):
                with self.assertRaisesRegex(ValueError, "semantic modes disagree"):
                    a.compare_row(row(), row(), row(), direct_mode, locator(), locator(),
                                  record(composed_mode), row(), row())

    def test_all21_semantics_are_joined_by_id_not_record_order(self):
        transfers, records = semantic_fixture()
        report = a.grip.validate_row124_records(list(reversed(records)), transfers)
        self.assertEqual(report["resolved_records"], 71)
        self.assertEqual(report["selected_semantics"], 21)
        self.assertEqual((report["selected_mode1"], report["selected_mode3"]), (20, 1))
        self.assertEqual(report["prop"]["source_global"], 103)
        self.assertEqual(report["prop"]["target_global"], 202)
        self.assertEqual(report["prop"]["mode"], 3)

    def test_every_selected_semantic_and_exact_global_mode_are_required(self):
        transfers, records = semantic_fixture()
        for index in range(21):
            for change in ({"source_id": 0x40400000}, {"target_id": 0x40400000},
                           {"source_global": records[index].source_global+1},
                           {"target_global": records[index].target_global+1},
                           {"mode": 1 if index == 1 else 3}):
                with self.subTest(index=index, change=change):
                    changed = records.copy()
                    changed[index] = replace(changed[index], **change)
                    with self.assertRaises(ValueError):
                        a.grip.validate_row124_records(changed, transfers)
        changed = records.copy()
        changed[-1] = records[0]
        with self.assertRaises(ValueError):
            a.grip.validate_row124_records(changed, transfers)
        for selected, mapped in ((transfers[:-1], records), (transfers, records[:-1])):
            with self.assertRaises(ValueError):
                a.grip.validate_row124_records(mapped, selected)


def have_frozen_inputs():
    if a is None:
        return False
    base = a.source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
    paths = [a.ROOT / "reports/actor_packages/nadine-dlc-base.pak",
             base / "actor77.psarc", base / "dc1.psarc", a.grip.DEFAULT_METADATA,
             a.grip.DEFAULT_HEADER]
    paths.extend(Path(str(a.grip.DEFAULT_PREFIX) + suffix)
                 for suffix in (".full-input.bin", ".outputs.bin", ".full-output.bin"))
    return all(path.is_file() for path in paths)


class FrozenDirectGripTests(unittest.TestCase):
    @unittest.skipUnless(have_frozen_inputs(), "pinned local assets/dependency not present")
    def test_pinned_comparison_is_finite_conditional_and_not_runtime_evidence(self):
        base = a.source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
        result = a.audit(a.ROOT / "reports/actor_packages/nadine-dlc-base.pak",
                         base / "actor77.psarc", base / "dc1.psarc")
        self.assertFalse(result["retail_or_game_execution"])
        self.assertFalse(result["runtime_record_output"])
        self.assertFalse(result["live_grip_or_skin_contact_proven"])
        self.assertEqual(result["classification"],
                         "APPROXIMATE_PROFILE_PRESENT_R124_VS_CONDITIONAL_CHLOE_CHAIN")
        self.assertIn("conditional", result["row157_basis"])
        self.assertIn("not live proof", result["row157_basis"])
        self.assertEqual(result["row124_map"]["selected_semantics"], 21)
        self.assertEqual(len(result["comparisons"]), 21)
        self.assertEqual(len(result["profile_slice_required_globals"]), 71)
        for comparison in result["comparisons"]:
            for key, value in comparison.items():
                if "delta" in key:
                    self.assertTrue(math.isfinite(value))
                    self.assertGreaterEqual(value, 0.)
        prop = result["comparisons"][1]
        self.assertEqual(prop["target_global"], 30)
        self.assertLess(prop["base_rotation_delta_degrees"], 1e-5)
        self.assertLess(prop["composite_rotation_delta_degrees"], 1e-5)
        self.assertLess(prop["composite_ST_component_delta"], 1e-5)
        json.dumps(result, allow_nan=False)

    @unittest.skipUnless(have_frozen_inputs(), "pinned local assets/dependency not present")
    def test_bind_control_changes_only_locators_and_remains_explicitly_invalid_R124(self):
        base = a.source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
        # Observe production operand wiring without replacing any numerical
        # function or synthesizing the outcome. Direct/control calls alternate.
        with mock.patch.object(a, "compare_row", wraps=a.compare_row) as observed:
            result = a.audit(a.ROOT / "reports/actor_packages/nadine-dlc-base.pak",
                             base / "actor77.psarc", base / "dc1.psarc")
        self.assertEqual(observed.call_count, 42)
        changed_locator_pairs = 0
        for index in range(21):
            direct = observed.call_args_list[2*index].args
            control = observed.call_args_list[2*index+1].args
            self.assertEqual(len(direct), 9)
            self.assertEqual(len(control), 9)
            for operand in (0, 1, 2, 3, 6, 7, 8):
                self.assertEqual(direct[operand], control[operand],
                                 f"row{index} changed non-locator operand{operand}")
            changed_locator_pairs += direct[4:6] != control[4:6]
        self.assertGreater(changed_locator_pairs, 0)
        control = result["bind_basis_control"]
        self.assertEqual(control["classification"],
                         "ANALYTIC_BIND_BASIS_CONTROL_NOT_AUTHORED_R124")
        self.assertIs(control["valid_profile_present_operand"], False)
        self.assertIs(control["live_profile_absence_proven"], False)
        self.assertIn("Only target locator inverse-global basis", control["changed_input"])
        self.assertIn("conditional", result["row157_basis"])
        self.assertEqual(len(control["comparisons"]), 21)
        for direct_row, control_row in zip(result["comparisons"], control["comparisons"]):
            self.assertEqual(direct_row["semantic"], control_row["semantic"])
            self.assertEqual(direct_row["target_global"], control_row["target_global"])
            self.assertEqual(control_row["base_ST_component_delta"], 0.)
            self.assertEqual(control_row["composite_ST_component_delta"], 0.)
        # Deliberately nonzero residual: independently quantized locators do
        # not cancel exactly. These are scalar-model regressions, not native
        # accuracy tolerances or evidence that the live profile is absent.
        self.assertAlmostEqual(control["max_base_rotation_delta_degrees"],
                               .01345765079997228, delta=1e-9)
        self.assertAlmostEqual(control["max_composite_rotation_delta_degrees"],
                               .013458585624756304, delta=1e-9)
        self.assertAlmostEqual(control["max_prop_relative_origin_delta_asset_units"],
                               6.757504591161754e-6, delta=1e-12)
        self.assertGreater(result["max_composite_rotation_delta_degrees"],
                           1000*control["max_composite_rotation_delta_degrees"])
        self.assertGreater(result["max_prop_relative_origin_delta_asset_units"],
                           1000*control["max_prop_relative_origin_delta_asset_units"])
        prop = control["comparisons"][1]
        self.assertEqual(prop["target_global"], 30)
        self.assertLess(prop["composite_rotation_delta_degrees"], 1e-5)
        self.assertEqual(prop["prop_relative_origin_delta_asset_units"], 0.)
        json.dumps(result, allow_nan=False)


if __name__ == "__main__":
    unittest.main()
