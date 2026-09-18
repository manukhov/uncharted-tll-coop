#!/usr/bin/env python3
"""Pure regressions for the static-only P99 authored-input audit (no game files)."""

from dataclasses import replace
import math
import struct
import unittest
from unittest.mock import patch

import p99_native_source_audit as audit


def skeleton(groups):
    layouts = tuple(audit.source.GroupLayout(first, index, workspace, parameters)
                    for index, (first, workspace, parameters) in enumerate(groups))
    return audit.source.SkeletonAsset(audit.source.NADINE_SKELETON, layouts, (), ())


def reference(**changes):
    values = [1., 1., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0.]
    for key, value in changes.items():
        values[int(key[1:])] = value
    return struct.pack("<12f", *values)


class ParameterTests(unittest.TestCase):
    def setUp(self):
        self.nadine = skeleton(((0, 104, 66), (104, 38, 38), (142, 119, 96)))
        self.chloe = skeleton(((0, 205, 89), (205, 62, 1), (267, 38, 38)))
        self.player = skeleton(((0, 168, 71), (168, 63, 63), (231, 76, 76),
                                (307, 83, 83), (390, 107, 88), (497, 52, 52),
                                (549, 90, 2), (639, 38, 38)))

    def test_compressed_not_global(self):
        for skel, global_joint, parameter in ((self.nadine, 105, 67),
                                              (self.chloe, 268, 91),
                                              (self.player, 640, 436),
                                              (self.nadine, 141, 103),
                                              (self.nadine, 142, 104)):
            with self.subTest(global_joint=global_joint):
                self.assertEqual(audit.parameter_index(skel, global_joint), parameter)

    def test_workspace_only_or_outside_rejected(self):
        for value in (-1, 66, 103, 238, 260, 261, 2 ** 64):
            with self.subTest(value=value), self.assertRaises(ValueError):
                audit.parameter_index(self.nadine, value)

    def test_exact_nine_byte_tail_ignores_unowned_padding(self):
        rec = audit.source.RuntimeRecord(3, 0xD38C382D, 0xD38C382D, 1, 268, 105)
        tail = audit.record_tail(rec, self.chloe, self.nadine)
        self.assertEqual(len(tail), 9)
        self.assertEqual(struct.unpack("<HHHHB", tail), (268, 91, 105, 67, 1))
        self.assertEqual(len(tail * 42), 378)
        self.assertNotEqual(len(tail * 42), 21 * 188)
        for pad in (0, 0xA5, 0xFF):
            runtime = bytes(36) + tail + bytes([pad])
            self.assertEqual(runtime[0x2C], 1)
            self.assertEqual(runtime[0x24:0x2D], tail)

    def test_mode_three_preserved(self):
        rec = audit.source.RuntimeRecord(1, 7, 7, 3, 41, 30)
        self.assertEqual(audit.record_tail(rec, self.chloe, self.nadine)[-1], 3)

    def test_other_modes_refused(self):
        rec = audit.source.RuntimeRecord(1, 7, 7, 3, 41, 30)
        for mode in (0, 2, 4, 0x103, -1):
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                audit.record_tail(replace(rec, mode=mode), self.chloe, self.nadine)

    def test_builder_source_translation_keeps_global_domain(self):
        inputs = {"reference_offset": 0x100, "reference_count": 473}
        full_raw = bytes(0x100 + 702 * 48)
        located = audit.builder_source_translation(full_raw, inputs, 640)
        self.assertEqual(located["disk_offset"], 0x100 + 640 * 48 + 0x20)
        self.assertNotEqual(located["disk_offset"], 0x100 + 436 * 48 + 0x20)
        self.assertTrue(located["outside_compressed_reference_rows"])
        self.assertFalse(audit.builder_source_translation(full_raw, inputs, 30)
                         ["outside_compressed_reference_rows"])
        with self.assertRaises(ValueError):
            audit.builder_source_translation(bytes(0x100 + 473 * 48), inputs, 640)
        with self.assertRaises(ValueError):
            audit.builder_source_translation(full_raw, inputs, -1)


class BytesTests(unittest.TestCase):
    def test_exact_range_and_empty_end(self):
        self.assertEqual(audit.extent(b"abc", 1, 2, "x"), b"bc")
        self.assertEqual(audit.extent(b"abc", 3, 0, "x"), b"")

    def test_bad_ranges(self):
        for offset, size in ((-1, 1), (0, -1), (4, 0), (2, 2), (2 ** 64, 1)):
            with self.subTest(offset=offset, size=size), self.assertRaises(ValueError):
                audit.extent(b"abc", offset, size, "x")

    def test_reference_accepts_zero_scale_and_unowned_w(self):
        # Actual proto reference63 has zero scale. The game row admission does
        # not require nonzero scale or finite unused S/T W lanes.
        audit.validate_reference(reference(v0=0., v3=math.nan, v11=math.inf), "fixture")

    def test_reference_numerical_and_extent_failures(self):
        for row in (b"", bytes(47), bytes(49), reference(v7=0.), reference(v7=2.),
                    reference(v4=math.nan), reference(v0=math.inf), reference(v8=math.nan)):
            with self.subTest(row=row.hex()), self.assertRaises(ValueError):
                audit.validate_reference(row, "fixture")

    def test_complete_input_truncation_fails_before_profile(self):
        with self.assertRaises(ValueError), patch.object(audit, "profile_provenance") as profile:
            audit.generate(b"bad", {}, b"bad", b"bad")
        profile.assert_not_called()

    def test_profile_bad_hash_is_not_accepted(self):
        with self.assertRaises(ValueError):
            audit.profile_provenance(bytes(audit.PROFILE_ENTRY_SIZE))

    def test_profile_identity_does_not_equal_nadine(self):
        self.assertEqual(audit.PROFILE_ID, audit.source.CHLOE_TO_NADINE.profile)
        self.assertNotEqual(audit.PROFILE_DESCRIPTOR_DATABASE, audit.source.NADINE_DATABASE_ID)
        self.assertNotEqual(audit.PROFILE_SOURCE_SKELETON, audit.source.NADINE_SKELETON_ID)
        self.assertEqual(audit.source.PLAYER_TO_CHLOE.profile, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
