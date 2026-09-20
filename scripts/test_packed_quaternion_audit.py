"""Independent op24 golden vectors and archived native D350/B8 comparisons.

No encoder roundtrip and no retail execution. Native142185CF0 constructs
pre-permutation lanes [C,B,A,D]; CRT140128020 installs the selector table from
142BF0700/0740. The 15/16-bit scales are pinned constants383504F3/37B504F3,
with denominator32768, not32767. D is the positive reconstructed component.
Scalar-vs-native tolerances cover float/SIMD rsqrt rounding, not axis mistakes.
"""
import math
from pathlib import Path
import struct
import unittest

import generate_p99_chloe_source_rows as source
import p99_grip_composition_audit as frozen
import psarc_asset_audit as decoder


ROOT = Path(__file__).resolve().parents[1]
ANIMATION = ROOT / "reports/actor_packages/anim-player-movement.pak"
NATIVE_TOLERANCE = 1e-6


class PackedQuaternionGoldenTests(unittest.TestCase):
    def assert_components(self, actual, expected, tolerance=2e-7):
        self.assertEqual(len(actual), 4)
        self.assertTrue(all(math.isfinite(value) for value in actual))
        self.assertLessEqual(max(abs(a-b) for a, b in zip(actual, expected)), tolerance)

    def test_asymmetric_native_selector_vectors_all_four(self):
        # Literal packed inputs: A15=20480, B15=8192, C16=36864.
        # Therefore A=sqrt(.5)/4, B=-sqrt(.5)/2, C=sqrt(.5)/8;
        # D=sqrt(107/128). All four magnitudes differ, unlike identity tests.
        unit = math.sqrt(.5)
        A, B, C, D = unit/4, -unit/2, unit/8, math.sqrt(107/128)
        cases = (("004001400090", (D, B, A, C)),
                 ("014001400090", (C, D, A, B)),
                 ("024001400090", (C, B, D, A)),
                 ("034001400090", (C, B, A, D)))
        for raw, expected in cases:
            with self.subTest(packed=raw):
                self.assert_components(decoder.decode_packed_quaternion_48(bytes.fromhex(raw)), expected)
        # Each natural-insertion competitor is materially different. This
        # assertion documents why these vectors detect the historical bug.
        for selector, (_, expected) in enumerate(cases):
            wrong = [A, B, C]
            wrong.insert(selector, D)
            self.assertGreater(max(abs(a-b) for a, b in zip(wrong, expected)), .08)

    def test_zero_centers_are_exact_not_denominator32767_bias(self):
        # Native integer centers are A=B=16384, C=32768. The old /32767
        # formula introduced roughly2.158e-5 in every supposedly zero lane.
        for raw, expected in (("000001800080", (1., 0., 0., 0.)),
                              ("010001800080", (0., 1., 0., 0.)),
                              ("020001800080", (0., 0., 1., 0.)),
                              ("030001800080", (0., 0., 0., 1.))):
            with self.subTest(packed=raw):
                actual = decoder.decode_packed_quaternion_48(bytes.fromhex(raw))
                self.assert_components(actual, expected)
                for value, wanted in zip(actual, expected):
                    if wanted == 0.:
                        self.assertLess(abs(value), 1e-8)

    def test_fifteen_and_sixteen_bit_upper_quantizers_are_distinct(self):
        unit = math.sqrt(.5)
        a = unit*(1-2/32768)
        c = unit*(1-1/32768)
        # Only A15 is max in the first input; only C16 is max in the second.
        self.assert_components(decoder.decode_packed_quaternion_48(bytes.fromhex("ffff01800080")),
                               (0., 0., a, math.sqrt(1-a*a)))
        self.assert_components(decoder.decode_packed_quaternion_48(bytes.fromhex("03000180ffff")),
                               (c, 0., 0., math.sqrt(1-c*c)))
        self.assertGreater(c-a, 2e-5)

    def test_literal_archived_native_rows_discriminate_three_selectors(self):
        # Values copied from the already frozen, hash-pinned native source
        # header, NOT produced by this scalar decoder or a paired encoder.
        cases = (
            ("0f323b70fb7e", (-.005632162094116211, -.08713674545288086,
                               .13823628425598145, .9865426421165466)),
            ("c0eb7ee58163", (.8110110759735107, .5606704354286194,
                               -.055933237075805664, -.15742015838623047)),
            ("8af2b958846a", (-.11868554353713989, -.2170003056526184,
                               .7000376582145691, .6699042916297913)),
            ("030001806038", (-.3956759572029114, 0., 0., .9183902144432068)),
        )
        for raw, expected in cases:
            with self.subTest(packed=raw):
                self.assert_components(decoder.decode_packed_quaternion_48(bytes.fromhex(raw)),
                                       expected, NATIVE_TOLERANCE)

    def test_negative_components_are_preserved_not_absolute_valued(self):
        actual = decoder.decode_packed_quaternion_48(bytes.fromhex("004001400090"))
        self.assertLess(actual[1], -.35)
        self.assertGreater(actual[2], .17)
        self.assertGreater(actual[3], .08)

    def test_exact_six_byte_extent_required(self):
        for length in (0, 1, 2, 4, 5, 7, 8, 12):
            with self.subTest(length=length), self.assertRaises(ValueError):
                decoder.decode_packed_quaternion_48(bytes(length))

    def test_radicand_uses_flt_min_without_normalizing_completed_quaternion(self):
        # Intentionally outside the authored unit envelope: the native clamp
        # applies only to the missing component, not to the full quaternion.
        actual = decoder.decode_packed_quaternion_48(bytes(6))
        self.assertEqual(actual[0], 2**-63)
        self.assertGreater(sum(value*value for value in actual), 1.49)
        self.assertEqual(actual[1], actual[2])
        self.assertEqual(actual[2], actual[3])


class FrozenPackedQuaternionTests(unittest.TestCase):
    @unittest.skipUnless(ANIMATION.is_file() and frozen.DEFAULT_HEADER.is_file()
                         and frozen.DEFAULT_METADATA.is_file(), "pinned local source assets absent")
    def test_all42_packed_sources_against_independent_archived_native_rows(self):
        data = ANIMATION.read_bytes()
        source.require_blob(source.ANIMATION_ENTRY_NAME, data,
                            source.ANIMATION_ENTRY_SIZE, source.ANIMATION_ENTRY_SHA256)
        pak = source.PagedPak(data)
        transfers = frozen.load_metadata(frozen.DEFAULT_METADATA)
        native_rows = frozen.parse_sources(frozen.DEFAULT_HEADER.read_text(encoding="utf-8"))
        errors, selectors = [], set()
        for parity, (animation_spec, name, skeleton_spec) in enumerate((
                (source.PLAYER_ANIMATION, "player", source.PLAYER_SKELETON),
                (source.CHLOE_ANIMATION, "chloe", source.CHLOE_SKELETON))):
            animation = source.resolve_animation(pak, animation_spec)
            for index, transfer in enumerate(transfers):
                joint = transfer[name + "_global"]
                locations = [(group, joint-first) for group, (first, count)
                             in enumerate(skeleton_spec.groups) if first <= joint < first+count]
                self.assertEqual(len(locations), 1)
                group, local = locations[0]
                spec = source.GROUP_PROGRAM_SPECS[(animation_spec.clip_id, group)]
                # This validates the complete program/map hashes and contains
                # no value-decoder call, so it cannot mask a scalar decode bug.
                maps, _ = source.parse_group_program(data, animation, spec)
                program = animation.raw_offset + spec.descriptor_words[0]
                header, count, selector, relative = struct.unpack_from("<4H", data, program+8)
                self.assertEqual((header, selector), (0x318, 3))
                map_offset = program + relative
                destinations = struct.unpack_from(f"<{count}H", data, map_offset)
                self.assertEqual(destinations, maps[24])
                destination = local*48+16
                self.assertEqual(destinations.count(destination), 1)
                position = destinations.index(destination)
                offset = map_offset+2*count+6*position
                packed = data[offset:offset+6]
                actual = decoder.decode_packed_quaternion_48(packed)
                expected = native_rows[2*index+parity][4:8]
                selectors.add(packed[0] & 3)
                error = max(abs(a-b) for a, b in zip(actual, expected))
                errors.append(error)
                with self.subTest(source=name, semantic=transfer["semantic"], packed=packed.hex()):
                    self.assertLessEqual(error, NATIVE_TOLERANCE)
        self.assertEqual(len(errors), 42)
        # Selector1 is independently covered by the asymmetric golden vector;
        # do not pretend it occurs in these particular native clip snapshots.
        self.assertEqual(selectors, {0, 2, 3})
        self.assertLessEqual(max(errors), NATIVE_TOLERANCE)


if __name__ == "__main__":
    unittest.main()
