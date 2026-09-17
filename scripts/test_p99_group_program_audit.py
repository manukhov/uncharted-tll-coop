#!/usr/bin/env python3
"""Asset-independent regression tests for the pinned program decoder."""

import struct
import unittest

import p99_group_program_audit as audit


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


if __name__ == "__main__":
    unittest.main()
