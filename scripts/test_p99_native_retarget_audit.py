#!/usr/bin/env python3
"""Owned synthetic data only. Does not map or execute the retail image."""
import struct
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import p99_native_retarget_audit as audit

IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.)


class NumericTests(unittest.TestCase):
    def valid(self, mode=1):
        result = bytearray(46)
        struct.pack_into("<H", result, 0xC, 0x3C00)
        struct.pack_into("<H", result, 0x1A, 0x3C00)
        result[0x2C] = mode
        return result

    def test_all_builder_modes_admitted_numerically(self):
        for mode in range(4):
            audit.validate_numeric_record(self.valid(mode))

    def test_nonfinite_any_half_rejected(self):
        for offset in range(0, 36, 2):
            for bits in (0x7C00, 0xFC00, 0x7E00):
                record = self.valid()
                struct.pack_into("<H", record, offset, bits)
                with self.subTest(offset=offset, bits=bits), self.assertRaises(ValueError):
                    audit.validate_numeric_record(record)

    def test_both_signed_zero_quaternions_rejected(self):
        for offset in (6, 20):
            record = self.valid()
            struct.pack_into("<4H", record, offset, *([0x8000] * 4))
            with self.assertRaises(ValueError):
                audit.validate_numeric_record(record)

    def test_stride_and_unknown_mode_rejected(self):
        for record in (self.valid()[:-1], self.valid() + b"x", self.valid(4)):
            with self.assertRaises(ValueError):
                audit.validate_numeric_record(record)

    def test_metadata_and_padding_are_not_half_fields(self):
        record = self.valid()
        record[36:44] = b"\xFF" * 8
        record[45] = 0xFF
        audit.validate_numeric_record(record)


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.data = bytearray(0x500)
        for index in range(2):
            struct.pack_into("<12f", self.data, 0x100 + 48 * index, *IDENTITY)
            struct.pack_into("<3f", self.data, 0x300 + 48 * index + 32, 1., 0., 0.)
        spec = SimpleNamespace(entry_name="fixture")
        self.skel = audit.source.SkeletonAsset(spec,
            (audit.source.GroupLayout(0, 0, 2, 2),), (), ())
        self.mapping = SimpleNamespace(spec=SimpleNamespace(flags=0, row_index=20, profile=0, authored_count=1))
        self.inputs = [{"parents": [-1, 0], "global_matrix_offset": 0x100,
                        "reference_offset": 0x300, "reference_count": 2} for _ in range(2)]

    def run_fixture(self, joint=1, target=None):
        record = audit.source.RuntimeRecord(0, 0, 0, 1, joint, joint)
        with patch.object(audit.source, "resolve_runtime_records", return_value=[record]), \
                patch.object(audit.authored, "skeleton_inputs", side_effect=self.inputs):
            return audit.preflight_mapping(self.mapping, self.skel, self.skel,
                                           self.data, self.data if target is None else target)

    def test_real_nonroot_matrices_checked(self):
        result = self.run_fixture()
        self.assertEqual(result["matrix_checks"], 4)
        self.assertEqual(result["max_conservative_ratio"], 1.)

    def test_root_prefix_is_read_but_not_required_invertible(self):
        result = self.run_fixture(0)
        self.assertEqual(result["matrix_checks"], 2)

    def test_root_prefix_still_requires_extent(self):
        self.inputs[0]["global_matrix_offset"] = 32
        with self.assertRaises(ValueError):
            self.run_fixture(0)

    def test_negative_parent_mismatch_refused(self):
        self.inputs[1]["parents"] = [0, 0]
        with self.assertRaises(ValueError):
            self.run_fixture(0)

    def test_singular_or_nonfinite_matrix_refused(self):
        for value in (0., float("nan"), float("inf")):
            struct.pack_into("<f", self.data, 0x100, value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.run_fixture()

    def test_unbounded_translation_refused(self):
        for value in (10001., float("nan"), float("inf")):
            struct.pack_into("<f", self.data, 0x300 + 48 + 32, value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.run_fixture()

    def test_zero_length_and_half_ratio_bound(self):
        target = bytes(self.data)
        struct.pack_into("<3f", self.data, 0x300 + 48 + 32, 0., 0., 0.)
        self.assertEqual(self.run_fixture(target=target)["max_conservative_ratio"], 0.)
        struct.pack_into("<3f", self.data, 0x300 + 48 + 32, 1e-6, 0., 0.)
        with self.assertRaises(ValueError):
            self.run_fixture(target=target)

    def test_ik_branch_refused(self):
        self.mapping.spec.flags = 1
        with self.assertRaises(ValueError):
            self.run_fixture()


if __name__ == "__main__":
    unittest.main(verbosity=2)
