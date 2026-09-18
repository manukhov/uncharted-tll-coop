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


class FullChainTests(unittest.TestCase):
    def test_row157_never_implicitly_admitted(self):
        mapping = SimpleNamespace(spec=SimpleNamespace(flags=0, row_index=157, profile=audit.authored.PROFILE_ID))
        with self.assertRaises(ValueError):
            audit.admit_numeric_mapping(mapping, 1, False)
        audit.admit_numeric_mapping(mapping, 1, True)
        self.assertEqual(mapping.spec.profile, audit.authored.PROFILE_ID)
        for field, value in (("profile", 0), ("flags", 1), ("row_index", 20)):
            original = getattr(mapping.spec, field)
            setattr(mapping.spec, field, value)
            with self.assertRaises(ValueError):
                audit.admit_numeric_mapping(mapping, 1, True)
            setattr(mapping.spec, field, original)

    def test_row20_admission_still_requires_profile_zero(self):
        mapping = SimpleNamespace(spec=SimpleNamespace(flags=0, row_index=20, profile=0))
        audit.admit_numeric_mapping(mapping, 0, False)
        mapping.spec.profile = audit.authored.PROFILE_ID
        with self.assertRaises(ValueError):
            audit.admit_numeric_mapping(mapping, 0, True)

    @staticmethod
    def transfers():
        return [{"nadine_group": int(i >= 6), "chloe_global": i + 63,
                 "nadine_global": i + 126} for i in range(21)]

    def output(self):
        row = struct.pack("<12f", 1., 1., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0.)
        return row * 21 + audit.expected_chain_masks(self.transfers())

    def test_exact_output_layout_and_masks(self):
        value = self.output()
        self.assertEqual(len(value), 1360)
        masks = struct.unpack_from("<44Q", value, 1008)
        self.assertEqual(masks[0], 1 << 63)
        self.assertEqual(masks[1], 31)
        self.assertEqual(masks[11 + 1], 3 << 62)
        audit.validate_chain_output(value, self.transfers())
        for index in range(352):
            changed = bytearray(value)
            changed[1008 + index] ^= 1
            with self.subTest(mask_byte=index), self.assertRaises(ValueError):
                audit.validate_chain_output(changed, self.transfers())

    def test_final_row_finite_and_rotation_checks(self):
        for index in range(21):
            for lane in range(12):
                if lane in (3, 11):
                    continue
                output = bytearray(self.output())
                struct.pack_into("<f", output, index * 48 + lane * 4, float("nan"))
                with self.assertRaises(ValueError):
                    audit.validate_chain_output(output, self.transfers())
            output = bytearray(self.output())
            struct.pack_into("<4f", output, index * 48 + 16, 0., 0., 0., 0.)
            with self.assertRaises(ValueError):
                audit.validate_chain_output(output, self.transfers())

    def test_mask_scope_duplicates_and_count_refused(self):
        for key, value in (("nadine_group", 2), ("chloe_global", 704), ("nadine_global", -1), ("nadine_global", 127)):
            transfers = self.transfers()
            transfers[0][key] = value
            with self.assertRaises(ValueError):
                audit.expected_chain_masks(transfers)
        with self.assertRaises(ValueError):
            audit.expected_chain_masks(self.transfers()[:-1])

    def test_full_input_preserves_every_record_and_reference_byte(self):
        refs = bytes((i % 256 for i in range(2016)))
        first = [bytes([i]) * 46 for i in range(21)]
        second = [bytes([i + 100]) * 46 for i in range(21)]
        with patch.object(audit, "selected_records", side_effect=[first, second]):
            value = audit.full_chain_input({"references": refs}, b"first", b"second")
        self.assertEqual(len(value), 3948)
        for index in range(21):
            self.assertEqual(value[index * 188:(index + 1) * 188],
                             first[index] + second[index] + refs[index * 96:(index + 1) * 96])
        with patch.object(audit, "selected_records", side_effect=[first, second]), self.assertRaises(ValueError):
            audit.full_chain_input({"references": refs[:-1]}, b"first", b"second")

    def test_group_masks_derive_from_authored_workspace_not_compressed_index(self):
        skeleton = SimpleNamespace(groups=(audit.source.GroupLayout(0, 0, 104, 66),
                                           audit.source.GroupLayout(104, 1, 38, 38)))
        result = audit.grouped_transfers([{"nadine_global": 28}, {"nadine_global": 105}], skeleton)
        self.assertEqual([item["nadine_group"] for item in result], [0, 1])
        for joint in (70, 142, -1):
            with self.assertRaises(ValueError):
                audit.grouped_transfers([{"nadine_global": joint}], skeleton)
        for groups in ((), (skeleton.groups[0], skeleton.groups[0]),
                       (*skeleton.groups, audit.source.GroupLayout(142, 2, 5, 5))):
            with self.assertRaises(ValueError):
                audit.grouped_transfers([{"nadine_global": 142 if len(groups) == 3 else 28}],
                                        SimpleNamespace(groups=groups))

    def test_conditional_baseline_refuses_size_and_digest_drift(self):
        values = [bytes(71 * 46), bytes(3948), bytes(1360)]
        with self.assertRaises(ValueError):
            audit.validate_conditional_baseline(*values)
        digests = [audit.source.sha256(value) for value in values]
        with patch.multiple(audit, EXPECTED_CONDITIONAL_ROW157_SHA256=digests[0],
                           EXPECTED_FULL_INPUT_SHA256=digests[1], EXPECTED_FULL_OUTPUT_SHA256=digests[2]):
            audit.validate_conditional_baseline(*values)
            for index in range(3):
                changed = values.copy()
                changed[index] = changed[index][:-1]
                with self.assertRaises(ValueError):
                    audit.validate_conditional_baseline(*changed)
                changed[index] = b"x" + values[index][1:]
                with self.assertRaises(ValueError):
                    audit.validate_conditional_baseline(*changed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
