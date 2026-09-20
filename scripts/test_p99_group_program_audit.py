#!/usr/bin/env python3
"""Asset-independent regression tests for the pinned program decoder."""

import struct
import unittest

import p99_group_program_audit as audit


# Independent authored descriptor order; right/left rows are interleaved and
# the final two local37 descriptors are SIMD padding, not logical joints.
GROUP1_RECORDS = (
    (0, 0xF9B), (1, 0xFDB), (8, 0xF9B), (9, 0xFDB),
    (16, 0xF9B), (17, 0xFDB), (24, 0xF9B), (25, 0xFDB),
    (2, 0x49), (3, 0x89), (10, 0x249), (11, 0x289),
    (18, 0x449), (19, 0x489), (26, 0x649), (27, 0x689),
    (32, 0xF9B), (33, 0xFDB), (4, 0xC9), (5, 0x109),
    (12, 0x2C9), (13, 0x309), (20, 0x4C9), (21, 0x509),
    (28, 0x6C9), (29, 0x709), (34, 0x849), (35, 0x889),
    (6, 0x149), (7, 0x189), (14, 0x349), (15, 0x389),
    (22, 0x549), (23, 0x589), (30, 0x749), (31, 0x789),
    (36, 0x8C9), (37, 0x909), (37, 0x909), (37, 0x909),
)
GROUP0_RECORDS = ((26, 0x649), (28, 0x6C9), (30, 0x749), (31, 0x649))


def propagation_fixture(records):
    if len(records) % 4:
        raise ValueError("fixture records must be four-wide")
    data = bytearray(2048)
    for index, (local, parent) in enumerate(records):
        struct.pack_into("<HH", data, 0x80 + 4 * index, 16 * (local + 1), parent)
    return data, (audit.Operator(0, 0, 40, (len(records) // 4, 3, 0x80)),)


class ProgramDecoderTests(unittest.TestCase):
    def test_invalid_operator_contracts(self):
        for opcode, payload in (
            (200, (0, 3, 0)),  # An unknown opcode is not read-only.
            (40, (0, 1, 0)),  # Owner arena is not the nested base table.
            (40, (0, 3, 1)),  # Descriptors must be program-relative.
            (40, (0, 3)),
            (52, (0, 0, 1, 0)),
        ):
            with self.subTest(opcode=opcode, payload=payload):
                with self.assertRaises(ValueError):
                    audit.writes(bytes(128), 0, audit.Operator(0, 0, opcode, payload))

    def test_bounded_program_termination(self):
        for data in (b"\x28", b"\x00\x01",
                     struct.pack("<HH", 0x0128, 0) * 256):
            with self.subTest(size=len(data)):
                with self.assertRaises((ValueError, struct.error)):
                    audit.operators(data, 0, 0)
        self.assertEqual(audit.operators(b"\x00\x00", 0, 0), ())

    def test_custom132_uses_offset46_not_coefficient_tail4c(self):
        fixture = bytearray(256)
        struct.pack_into("<I", fixture, 4, 1)
        struct.pack_into("<H", fixture, 0x46, 128)
        struct.pack_into("<H", fixture, 0x4C, 160)
        struct.pack_into("<I", fixture, 132, 0x10000540)
        struct.pack_into("<I", fixture, 164, 0x3E969D8E)
        self.assertEqual(
            audit.writes(fixture, 0, audit.Operator(0, 0, 132, (1, 3, 0))),
            [(0x10000540, 4)],
        )

    def test_all_custom_destinations_keep_32_bits(self):
        # 0x10000540 must not alias JP row28 at 0x540 after truncation.
        for opcode in (128, 129, 130, 131, 132):
            with self.subTest(opcode=opcode):
                fixture = bytearray(256)
                if opcode in (128, 129):
                    destination = 60
                else:
                    count_field, offset_field, destination_field = {
                        130: (8, 22, 0), 131: (4, 10, 4), 132: (4, 70, 4),
                    }[opcode]
                    struct.pack_into("<I", fixture, count_field, 1)
                    struct.pack_into("<H", fixture, offset_field, 128)
                    destination = 128 + destination_field
                struct.pack_into("<I", fixture, destination, 0x10000540)
                self.assertEqual(
                    audit.writes(fixture, 0, audit.Operator(0, 0, opcode, (1, 3, 0))),
                    [(0x10000540, 4)],
                )


class SelectedConsumptionTests(unittest.TestCase):
    def test_positive_full_rows_and_explicit_padding(self):
        for group, records in enumerate((GROUP0_RECORDS, GROUP1_RECORDS)):
            with self.subTest(group=group):
                data, ops = propagation_fixture(records)
                self.assertEqual(audit.propagation_records(data, 0, ops[0]), records)
                audit.audit_selected_consumption(data, 0, ops, group)
        self.assertEqual(len(set(local for local, _ in GROUP1_RECORDS)), 38)
        self.assertEqual(GROUP1_RECORDS[37:], ((37, 0x909),) * 3)

    def test_malformed_native_row_indices_fail(self):
        for k in (0, 1, 15, 17):
            with self.subTest(k=k):
                data, ops = propagation_fixture(GROUP0_RECORDS)
                struct.pack_into("<H", data, 0x80, k)
                with self.assertRaisesRegex(ValueError, "positive 16-byte index"):
                    audit.propagation_records(data, 0, ops[0])
        data, ops = propagation_fixture(GROUP0_RECORDS)
        with self.assertRaises(struct.error):
            audit.propagation_records(data[:0x8F], 0, ops[0])
        for opcode, payload in ((53, (1, 3, 0x80)), (40, (1, 1, 0x80)),
                                (40, (1, 3, 0x81)), (40, (1, 3))):
            with self.subTest(opcode=opcode, payload=payload):
                with self.assertRaises(ValueError):
                    audit.propagation_records(data, 0, audit.Operator(0, 0, opcode, payload))

    def test_every_selected_parent_encoding_is_checked(self):
        for group, records in enumerate((GROUP0_RECORDS, GROUP1_RECORDS)):
            selected = (28, 30) if group == 0 else tuple(range(1, 38, 2))
            for index, (local, parent) in enumerate(records):
                if local not in selected or group == 1 and index >= 38:
                    continue
                # Wrong base selector, scale flag, or parent row must all fail.
                for changed in (parent ^ 1, parent ^ 8, parent ^ 0x40):
                    with self.subTest(group=group, local=local, parent=changed):
                        mutated = list(records)
                        mutated[index] = (local, changed)
                        data, ops = propagation_fixture(mutated)
                        with self.assertRaises(ValueError):
                            audit.audit_selected_consumption(data, 0, ops, group)

    def test_parent_must_be_propagated_before_selected_child(self):
        for group, records, first, second in (
            (0, GROUP0_RECORDS, 0, 1),  # Forearm26 must precede palm28.
            (0, GROUP0_RECORDS, 1, 2),  # Palm28 must precede prop30.
            (1, GROUP1_RECORDS, 1, 9),  # Finger root1 must precede child3.
        ):
            with self.subTest(group=group, first=first, second=second):
                mutated = list(records)
                mutated[first], mutated[second] = mutated[second], mutated[first]
                data, ops = propagation_fixture(mutated)
                with self.assertRaisesRegex(ValueError, "before its parent"):
                    audit.audit_selected_consumption(data, 0, ops, group)

    def test_missing_duplicate_and_out_of_range_rows_fail(self):
        for records, expected in (
            (((26, 0x649), (28, 0x6C9), (29, 0x709), (31, 0x649)), "not every"),
            (((26, 0x649), (28, 0x6C9), (30, 0x749), (30, 0x749)), "duplicate"),
            (((26, 0x649), (28, 0x6C9), (30, 0x749), (104, 0x649)), "exceeds"),
        ):
            with self.subTest(expected=expected):
                data, ops = propagation_fixture(records)
                with self.assertRaisesRegex(ValueError, expected):
                    audit.audit_selected_consumption(data, 0, ops, 0)
        with self.assertRaisesRegex(ValueError, "not every"):
            audit.audit_selected_consumption(b"", 0, (), 1)
        with self.assertRaisesRegex(ValueError, "unexpected target group"):
            audit.audit_selected_consumption(b"", 0, (), 2)

    def test_removing_any_of_the_21_logical_consumers_fails(self):
        for group, records in enumerate((GROUP0_RECORDS, GROUP1_RECORDS)):
            selected = (28, 30) if group == 0 else tuple(range(1, 38, 2))
            for index, (local, parent) in enumerate(records):
                if local not in selected or group == 1 and index >= 38:
                    continue
                with self.subTest(group=group, local=local):
                    mutated = list(records)
                    # Replace the selected right-side row with a nonselected
                    # row; group1's later padding cannot supply ownership.
                    mutated[index] = (local - 1, parent)
                    data, ops = propagation_fixture(mutated)
                    with self.assertRaises(ValueError):
                        audit.audit_selected_consumption(data, 0, ops, group)

    def test_only_identical_trailing_local37_repeats_are_padding(self):
        for index, replacement in ((38, (35, 0x889)), (39, (37, 0x901)),
                                   (9, (1, 0xFDB)), (37, (38, 0x909))):
            with self.subTest(index=index, replacement=replacement):
                records = list(GROUP1_RECORDS)
                records[index] = replacement
                data, ops = propagation_fixture(records)
                with self.assertRaisesRegex(ValueError, "logical rows or local37 padding"):
                    audit.audit_selected_consumption(data, 0, ops, 1)
        data, ops = propagation_fixture(GROUP1_RECORDS)
        with self.assertRaisesRegex(ValueError, "logical rows or local37 padding"):
            audit.audit_selected_consumption(data, 0, ops + ops, 1)

    def test_later_writes_to_every_selected_matrix_fail(self):
        for group, records in enumerate((GROUP0_RECORDS, GROUP1_RECORDS)):
            selected = (28, 30) if group == 0 else tuple(range(1, 38, 2))
            for local in selected:
                for opcode, offset in ((53, 0), (53, 48), (54, 60)):
                    with self.subTest(group=group, local=local, opcode=opcode, offset=offset):
                        data, ops = propagation_fixture(records)
                        destination = (64 * (local + 1) + offset) | 1
                        struct.pack_into("<HH", data, 0x400, destination, 0)
                        overwrite = audit.Operator(1, 8, opcode, (1, 3, 0x400))
                        with self.assertRaisesRegex(ValueError, "later operator overwrites"):
                            audit.audit_selected_consumption(data, 0, ops + (overwrite,), group)

    def test_matrix_alias_check_preserves_base_and_full_32_bit_offset(self):
        # Same numeric offset in scratch is not a matrix write. Nor may a
        # 32-bit custom destination at +0x10000000 be truncated into one.
        for destination in (0x743, 0x10000741, 0x780):
            with self.subTest(destination=destination):
                data, ops = propagation_fixture(GROUP0_RECORDS)
                struct.pack_into("<I", data, 0x400 + 60, destination)
                writer = audit.Operator(1, 8, 128, (1, 3, 0x400))
                audit.audit_selected_consumption(data, 0, ops + (writer,), 0)


if __name__ == "__main__":
    unittest.main()
