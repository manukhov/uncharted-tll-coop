#!/usr/bin/env python3
"""Asset-independent mutations of the enemy matrix storage/dataflow proof."""

import struct
import unittest
from unittest.mock import patch

import enemy_group0_matrix_audit as audit


def fixture(records=((0, 9), (1, 0x49), (2, 0x89), (3, 0xC9)), extra=()):
    if len(records) % 4:
        raise ValueError("four-wide records required")
    data = bytearray(512)
    ops = [(40, (len(records) // 4, 3, 0x80)), *extra]
    cursor = 0
    for opcode, payload in ops:
        struct.pack_into("<H", data, cursor, opcode | (len(payload) << 8))
        struct.pack_into(f"<{len(payload)}H", data, cursor + 2, *payload)
        cursor += 2 + 2 * len(payload)
    struct.pack_into("<H", data, cursor, 0)
    for index, (local, parent) in enumerate(records):
        struct.pack_into("<HH", data, 0x80 + 4 * index, 16 * (local + 1), parent)
    return data


def check(data, count=4, parameters=3, required=(0, 3)):
    return audit.audit_program(bytes(data), count, parameters, required, None)


class MatrixAuditTests(unittest.TestCase):
    def test_complete_producers_and_reserved_root(self):
        result = check(fixture())
        self.assertEqual(result["unique_produced_local_rows"], 4)
        self.assertTrue(result["all_workspace_rows_produced"])
        self.assertEqual(result["verified_capacities_by_bank"], {0: 192, 1: 320})
        self.assertEqual(result["native_export_index_shift"], -1)
        self.assertEqual(result["other_banks_capacity_unproven"], [2, 3])

    def test_simd_repeats_do_not_invent_additional_rows(self):
        rows = ((0, 9),) * 4 + ((1, 0x49), (2, 0x89), (3, 0xC9), (3, 0xC9))
        result = check(fixture(rows))
        self.assertEqual(result["op40_records_including_repeats"], 8)
        self.assertEqual(result["unique_produced_local_rows"], 4)

    def test_missing_producer(self):
        with self.assertRaisesRegex(ValueError, "missing matrix producer"):
            check(fixture(((0, 9), (1, 0x49), (2, 0x89), (2, 0x89))))

    def test_parent_must_exist_before_child(self):
        with self.assertRaisesRegex(ValueError, "before its parent"):
            check(fixture(((0, 0x49), (1, 9), (2, 0x89), (3, 0xC9))))

    def test_parent_and_destination_range(self):
        for rows in (
            ((0, 0x149), (1, 9), (2, 0x89), (3, 0xC9)),
            ((0, 9), (1, 0x49), (2, 0x89), (4, 0xC9)),
        ):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                check(fixture(rows))

    def test_parent_bank_alignment_and_flags(self):
        for parent in (0, 1, 8, 10, 0x19, 0x29, 0x39):
            with self.subTest(parent=parent), self.assertRaisesRegex(ValueError, "parent is not"):
                check(fixture(((0, parent), (1, 0x49), (2, 0x89), (3, 0xC9))))

    def test_duplicate_changes_parent(self):
        rows = ((0, 9), (1, 0x49), (2, 0x89), (3, 0xC9),
                (3, 0x89), (3, 0x89), (3, 0x89), (3, 0x89))
        with self.assertRaisesRegex(ValueError, "changes parent"):
            check(fixture(rows))

    def test_non40_matrix_overwrite_including_reserved_zero(self):
        for opcode, width in ((53, 16), (54, 4), (128, 4)):
            for offset in (0, 64, 320 - width):
                data = fixture(extra=((opcode, (1, 3, 0x100)),))
                if opcode == 128:
                    struct.pack_into("<I", data, 0x100 + 60, offset | 1)
                else:
                    struct.pack_into("<H", data, 0x100, offset | 1)
                with self.subTest(opcode=opcode, offset=offset), self.assertRaisesRegex(ValueError, "non40"):
                    check(data)

    def test_expanded_jp_bound_not_parameter_prefix(self):
        data = fixture(extra=((53, (1, 3, 0x100)),))
        struct.pack_into("<H", data, 0x100, 176)  # End of expanded row3, beyond P=3.
        self.assertEqual(check(data)["decoded_writes_by_bank"][0], 1)
        struct.pack_into("<H", data, 0x100, 192)
        with self.assertRaisesRegex(ValueError, "bank0 write exceeds"):
            check(data)

    def test_other_bank_extents_are_not_claimed_as_capacities(self):
        data = fixture(extra=((128, (1, 3, 0x100)),))
        struct.pack_into("<I", data, 0x100 + 60, 0x10000542)
        result = check(data)
        self.assertEqual(result["observed_max_end_by_bank"][2], 0x10000544)
        self.assertNotIn(2, result["verified_capacities_by_bank"])

    def test_full32bit_matrix_destination_cannot_alias_small_row(self):
        data = fixture(extra=((128, (1, 3, 0x100)),))
        struct.pack_into("<I", data, 0x100 + 60, 0x10000041)
        with self.assertRaisesRegex(ValueError, "bank1 write exceeds"):
            check(data)

    def test_wrong_write_width_is_rejected(self):
        with patch.object(audit, "writes", return_value=[(65, 32)]):
            with self.assertRaisesRegex(ValueError, "unsupported write width"):
                check(fixture())

    def test_decoder_disagreement_is_rejected(self):
        with patch.object(audit, "writes", return_value=[(65, 16)] * 4):
            with self.assertRaisesRegex(ValueError, "write width/offset changed"):
                check(fixture())

    def test_unexpected_opcode_and_inventory_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "unexpected opcode"):
            check(fixture(extra=((200, (0, 3, 0x100)),)))
        with self.assertRaisesRegex(ValueError, "inventory changed"):
            audit.audit_program(bytes(fixture()), 4, 3, (0, 3))

    def test_truncated_descriptor_cannot_escape_program_extent(self):
        with self.assertRaisesRegex(ValueError, "declared extent"):
            check(fixture()[:0x8F])
        with self.assertRaisesRegex(ValueError, "outside supplied bytes"):
            audit.extent(b"1234", 3, 2, "program")

    def test_bad_propagation_encoding(self):
        for encoded in (0, 1, 15, 17):
            data = fixture()
            struct.pack_into("<H", data, 0x80, encoded)
            with self.subTest(encoded=encoded), self.assertRaises(ValueError):
                check(data)

    def test_invalid_ranges_and_required_colliders(self):
        for count, parameters, required in ((0, 0, ()), (4, 5, ()), (4097, 1, ()),
                                            (4, 3, (4,)), (4, 3, (-1,)), (4, 3, (0, 0))):
            with self.subTest(count=count, parameters=parameters, required=required):
                with self.assertRaisesRegex(ValueError, "range"):
                    check(fixture(), count, parameters, required)

    def test_candidate_cannot_bypass_hash_or_size(self):
        for data in (b"", b"x" * audit.CANDIDATES[0].size):
            with self.assertRaisesRegex(ValueError, "size/SHA-256"):
                audit.audit_candidate(data, audit.CANDIDATES[0])

    def test_pinned_inventory_has_all47_operators(self):
        self.assertEqual(sum(audit.EXPECTED_INVENTORY.values()), 47)
        self.assertEqual(audit.EXPECTED_INVENTORY[40], 11)
        self.assertEqual(len(audit.CANDIDATES), 2)


if __name__ == "__main__":
    unittest.main()
