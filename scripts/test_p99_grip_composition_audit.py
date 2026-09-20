"""Pure algebra/property checks; optional pinned native artifacts are read-only."""
import json
import math
import random
import struct
import unittest
from types import SimpleNamespace

import p99_grip_composition_audit as a


def record(A=(0., 0., 0., 1.), E=(0., 0., 0., 1.), mode=1):
    result = bytearray(46)
    struct.pack_into("<4e", result, 6, *A)
    struct.pack_into("<4e", result, 0x14, *E)
    result[44] = mode
    return bytes(result)


class AlgebraTests(unittest.TestCase):
    def test_noncommuting_hamilton_convention(self):
        x = (math.sqrt(.5), 0., 0., math.sqrt(.5))
        z = (0., 0., math.sqrt(.5), math.sqrt(.5))
        self.assertGreater(a.qerror(a.qmul(x, z), a.qmul(z, x)), .5)
        rotated = a.rotate(z, (1., 0., 0.))
        self.assertAlmostEqual(rotated[0], 0.)
        self.assertAlmostEqual(rotated[1], 1.)

    def test_sign_equivalence_does_not_hide_norm_error(self):
        self.assertEqual(a.qerror((0, 0, 0, 1), (0, 0, 0, -1)), 0.)
        self.assertEqual(a.qerror((0, 0, 0, 1), (0, 0, 0, 2)), 1.)

    def test_mode1_absolute_reference_ST_but_not_reference_Q(self):
        source = a.row((2., 3., 4.), a.qnorm((1., 2., 3., 4.)), (5., 6., 7.))
        reference = a.row((.5, .6, .7), a.qnorm((4., 3., 2., 1.)), (8., 9., 10.))
        result = a.retarget(record(), source, reference)
        self.assertEqual(result[:3], reference[:3])
        self.assertEqual(result[8:11], reference[8:11])
        self.assertEqual(result[4:8], source[4:8])

    def test_mode1_additive_neutral_ST_and_conjugation(self):
        A = a.qnorm((.25, .5, 0., .75))
        packed = record(A=A)
        normalized = a.qnorm(struct.unpack_from("<4e", packed, 6))
        source = a.row((2., 3., 4.), a.qnorm((1., 0., 2., 3.)), (7., 8., 9.))
        result = a.retarget(packed, source, a.row(), additive=True)
        self.assertEqual(result[:3], (1., 1., 1.))
        self.assertEqual(result[8:11], (0., 0., 0.))
        self.assertLess(a.qerror(result[4:8], a.qmul(a.qmul(a.qconj(normalized), source[4:8]), normalized)), 1e-15)

    def test_mode3_copies_absolute_and_additive_without_reference(self):
        source = a.row((2., 3., 4.), a.qnorm((1., 2., 3., 4.)), (5., 6., 7.))
        for additive in (False, True):
            self.assertEqual(a.retarget(record(mode=3), source, a.row(), additive), source)

    def test_invalid_packed_locators_modes_and_extent_refused(self):
        for invalid in (b"", record()[:-1], record(mode=2), record(A=(0., 0., 0., 0.)),
                        record(E=(float("inf"), 0., 0., 1.)), record(mode=3, E=(0., 0., 0., 0.))):
            with self.assertRaises(ValueError):
                a.retarget(invalid, a.row(), a.row())

    def test_equivariance_512_nontrivial_cases_per_mode(self):
        rng = random.Random(0x993157)
        def quat():
            return a.qnorm(tuple(rng.uniform(-1., 1.) for _ in range(4)))
        for mode in (1, 3):
            for _ in range(512):
                packed = record(quat(), quat(), mode)
                source = a.row(tuple(rng.uniform(.5, 2.) for _ in range(3)), quat(),
                               tuple(rng.uniform(-2., 2.) for _ in range(3)))
                additive = a.row(tuple(rng.uniform(.5, 2.) for _ in range(3)), quat(),
                                 tuple(rng.uniform(-2., 2.) for _ in range(3)))
                reference = a.row((.8, .9, 1.1), quat(), (1., 2., 3.))
                before = a.retarget(packed, a.blend(source, additive), reference)
                after = a.blend(a.retarget(packed, source, reference),
                                a.retarget(packed, additive, reference, additive=True))
                self.assertLess(a.row_error(before, after), a.ALGEBRA_TOLERANCE)

    def test_additive_false_is_a_real_discriminator(self):
        packed = record(a.qnorm((.1, .2, .3, .9)), a.qnorm((.3, -.4, .2, .7)))
        source = a.row(rotation=a.qnorm((.5, .3, .2, .1)))
        additive = a.row(rotation=a.qnorm((.2, -.3, .6, .7)))
        reference = a.row(translation=(1., 2., 3.))
        expected = a.retarget(packed, a.blend(source, additive), reference)
        wrong = a.blend(a.retarget(packed, source, reference), a.retarget(packed, additive, reference))
        self.assertGreater(a.row_error(expected, wrong), .1)

    def test_prop_mode3_pipeline_exact_structural_noop(self):
        base = a.row(rotation=a.qnorm((.2, -.5, .7, .4)), translation=(.1, -.2, .3))
        additive = a.row(rotation=a.qnorm((.3, .2, -.1, .8)), translation=(.2, .1, -.1))
        r20, r124, r157 = record(mode=3), record(mode=3), record(mode=3)
        proposed = a.retarget(r157, a.blend(a.retarget(r20, base, a.row()), additive), a.row())
        current = a.blend(a.retarget(r124, base, a.row()), a.retarget(r157, additive, a.row(), True))
        self.assertEqual(proposed, current)

    def test_semantic_parent_closure_and_prop_parent(self):
        transfers = [{"nadine_global": 100+i, "nadine_parent_global": 99 if i == 0 else 100}
                     for i in range(21)]
        self.assertEqual(a.parent_indices(transfers, "nadine"), [-1]+[0]*20)
        transfers[1]["nadine_parent_global"] = 102
        with self.assertRaises(ValueError):
            a.parent_indices(transfers, "nadine")

    def test_geometry_removes_common_palm_transform_and_checks_scale(self):
        parents = [-1]+[0]*20
        rows = [a.row(translation=(1., 2., 3.)) for _ in range(21)]
        before = a.distal_frames(rows, parents)
        rows[0] = a.row(rotation=a.qnorm((.5, .4, .3, .2)), translation=(40., 50., 60.))
        self.assertEqual(a.distal_frames(rows, parents), before)
        rows[2] = a.row(scale=(2., 1., 1.))
        with self.assertRaises(ValueError):
            a.distal_frames(rows, parents)

    def test_source_parser_pins_values(self):
        header = a.DEFAULT_HEADER.read_text(encoding="utf-8")
        self.assertEqual(len(a.parse_sources(header)), 42)
        with self.assertRaises(ValueError):
            a.parse_sources(header.replace("0xBBB88E00u", "0xBBB88E01u"))

    def test_artifact_pins_reject_wrong_extent_and_hash(self):
        for key, (size, _) in a.PINS.items():
            for bad in (b"", bytes(size), bytes(size+1)):
                with self.assertRaises(ValueError):
                    a.pinned(bad, key)

    def test_row124_selected_semantics_modes_and_duplicates(self):
        transfers = [{"semantic": f"{100+i:08X}", "player_global": 200+i, "nadine_global": 300+i}
                     for i in range(21)]
        records = [SimpleNamespace(source_id=100+i, target_id=100+i, source_global=200+i,
                                  target_global=300+i, mode=3 if i == 1 else 1, map_index=i)
                   for i in range(71)]
        result = a.validate_row124_records(records, transfers)
        self.assertEqual(result["selected_semantics"], 21)
        self.assertEqual(result["prop"]["mode"], 3)
        records[1].mode = 1
        with self.assertRaises(ValueError):
            a.validate_row124_records(records, transfers)
        records[1].mode = 3
        records[70].source_id = records[1].source_id
        with self.assertRaises(ValueError):
            a.validate_row124_records(records, transfers)


class FrozenStageTests(unittest.TestCase):
    @unittest.skipUnless(a.DEFAULT_METADATA.exists() and a.DEFAULT_PREFIX.with_suffix(".full-output.bin").exists(),
                         "local pinned AGZ artifacts not present")
    def test_independent_equations_against_all_frozen_native_stages(self):
        result = a.audit()
        self.assertFalse(result["native_or_game_execution"])
        self.assertLess(max(result["native_stage_component_max_errors"].values()), 1e-5)
        self.assertGreater(result["reversed_blend_order_component_error"], .2)
        self.assertTrue(result["prop_final_matches_chloe_composite_all48_bytes"])
        self.assertEqual(len(result["finger_geometry_chloe_vs_nadine"]), 19)
        # The schema must preserve the explicitly unresolved direct-base rival.
        self.assertTrue(any("R124" in item for item in result["limitations"]))
        json.dumps(result, allow_nan=False)


if __name__ == "__main__":
    unittest.main()
