"""Pure constant-profile parser tests; no game/native instruction execution."""
import math
import struct
import unittest

import p99_nadine_profile_audit as a


def fixture(scalars=3):
    data = bytearray(256)
    ops = [(22, 0x40), (24, 0x80)] + ([(25, 0xC0)] if scalars else [])
    for index, (opcode, offset) in enumerate(ops):
        struct.pack_into("<4H", data, index * 8, 0x300 | opcode, 8, 3, offset)
    struct.pack_into("<8H", data, 0x40, 0, 48, 32, 80, 96, 96, 96, 96)
    for index, values in enumerate(((1, 2, 3), (2, 3, 4), (4, 5, 6), (5, 6, 7))):
        struct.pack_into("<3e", data, 0x50 + index * 6, *values)
    # Padding is deliberately nonfinite. It is a discarded destination, not a
    # logical reference row (nor evidence of unsafe native input admission).
    for index in range(4, 8):
        struct.pack_into("<3e", data, 0x50 + index * 6, math.nan, math.inf, -math.inf)
    struct.pack_into("<8H", data, 0x80, 16, 64, 96, 96, 96, 96, 96, 96)
    for index, omitted in enumerate((3, 0)):
        bits = omitted | (16384 << 2) | (16384 << 17) | (32767 << 32)
        data[0x90 + index * 6:0x96 + index * 6] = bits.to_bytes(6, "little")
    if scalars:
        struct.pack_into("<8H", data, 0xC0, 0, 4, 8, 16, 16, 16, 16, 16)
        struct.pack_into("<8f", data, 0xD0, .125, .25, .5, *([math.nan] * 5))
    return data


class ConstantProfileTests(unittest.TestCase):
    def test_full_rows_and_separate_scalar_bank(self):
        rows, scalars, ops = a.decode_program(fixture(), 0, 2, 3)
        self.assertEqual(rows[0][:3], [1, 2, 3])
        self.assertEqual(rows[1][:3], [2, 3, 4])
        self.assertEqual(rows[0][8:11], [4, 5, 6])
        self.assertEqual(rows[1][8:11], [5, 6, 7])
        self.assertGreater(rows[0][7], .99999)
        self.assertGreater(rows[1][4], .99999)
        self.assertEqual(scalars, [.125, .25, .5])
        self.assertEqual([item["padding_count"] for item in ops], [4, 6, 5])
        self.assertEqual([item["sentinel_byte_offset"] for item in ops], [96, 96, 16])

    def test_no_scalar_group_has_only_two_ops(self):
        rows, scalars, ops = a.decode_program(fixture(0), 0, 2, 0)
        self.assertEqual(len(rows), 2)
        self.assertEqual(scalars, [])
        self.assertEqual([item["opcode"] for item in ops], [22, 24])

    def test_header_selector_pointer_count_and_tail_fail(self):
        for offset, value in ((0, 0x216), (2, 7), (4, 1),
                              (6, 0x41), (6, 0), (8, 0x319), (24, 0x100)):
            with self.subTest(offset=offset, value=value):
                data = fixture()
                struct.pack_into("<H", data, offset, value)
                with self.assertRaises(ValueError):
                    a.decode_program(data, 0, 2, 3)

    def test_missing_duplicate_misaligned_or_cross_bank_destination_fails(self):
        for offset, destination in ((0x40, 48), (0x40, 16), (0x40, 97),
                                    (0x80, 64), (0x80, 0), (0xC0, 4),
                                    (0xC0, 12), (0xC0, 96), (0xC0, 1)):
            with self.subTest(offset=offset, destination=destination):
                data = fixture()
                struct.pack_into("<H", data, offset, destination)
                with self.assertRaisesRegex(ValueError, "destination"):
                    a.decode_program(data, 0, 2, 3)

    def test_each_logical_lane_is_required_not_sentinel_padding(self):
        for table, count, sentinel in ((0x40, 4, 96), (0x80, 2, 96), (0xC0, 3, 16)):
            for index in range(count):
                with self.subTest(table=table, index=index):
                    data = fixture()
                    struct.pack_into("<H", data, table + 2 * index, sentinel)
                    with self.assertRaises(ValueError):
                        a.decode_program(data, 0, 2, 3)

    def test_nonfinite_meaningful_lanes_fail_but_padding_is_discarded(self):
        for offset, fmt in ((0x50, "<e"), (0xD0, "<f")):
            for value in (math.nan, math.inf, -math.inf):
                data = fixture()
                struct.pack_into(fmt, data, offset, value)
                with self.assertRaisesRegex(ValueError, "nonfinite"):
                    a.decode_program(data, 0, 2, 3)

    def test_all_extents_and_counts_are_bounded(self):
        for end in (0, 1, 7, 24, 0x41, 0x51, 0x90, 0xD0, 239):
            with self.subTest(end=end):
                with self.assertRaises(ValueError):
                    a.decode_program(fixture()[:end], 0, 2, 3)
        for joints, scalars in ((0, 0), (-1, 0), (257, 0), (2, -1), (2, 257)):
            with self.assertRaises(ValueError):
                a.decode_program(fixture(), 0, joints, scalars)
        with self.assertRaises(ValueError):
            a.decode_program(fixture(), -1, 2, 3)

    def test_asset_pin_rejects_untrusted_profile_graph(self):
        with self.assertRaises(ValueError):
            a.resolve_profile(bytes(0x100))

    def test_omitted_components_and_invalid_meaningful_quaternion(self):
        for omitted in range(4):
            data = fixture()
            bits = omitted | (16384 << 2) | (16384 << 17) | (32767 << 32)
            data[0x90:0x96] = bits.to_bytes(6, "little")
            rows, _, _ = a.decode_program(data, 0, 2, 3)
            self.assertGreater(rows[0][4 + omitted], .99999)
        data[0x90:0x96] = bytes(6)
        with self.assertRaisesRegex(ValueError, "quaternion"):
            a.decode_program(data, 0, 2, 3)

    def test_meaningful_half_subnormals_are_counted_not_native_exact(self):
        data = fixture()
        struct.pack_into("<H", data, 0x50, 1)
        rows, _, ops = a.decode_program(data, 0, 2, 3)
        self.assertEqual(rows[0][0], 2**-24)
        self.assertEqual(ops[0]["meaningful_half_subnormals"], 1)

    @unittest.skipUnless(a.DEFAULT_ASSET.exists(), "local pinned Nadine PAK absent")
    def test_complete_shipped_profile_is_not_mislabeled_as_sampled_output(self):
        result = a.audit(a.DEFAULT_ASSET.read_bytes())
        self.assertFalse(result["game_or_native_execution"])
        self.assertFalse(result["runtime_builder_operands_ready"])
        self.assertEqual(result["quaternion_decoder_model"],
                         "opcode24-cba-swap-f32-32768-sqrt-v2")
        self.assertEqual(result["decoded_parameter_rows"], 426)
        self.assertEqual(result["decoded_scalars"], 27)
        self.assertEqual(sum(op["meaningful_half_subnormals"] for group in result["groups"]
                             for op in group["operators"]), 26)
        self.assertEqual([g["parameter_count"] for g in result["groups"]], list(a.COUNTS))
        self.assertEqual([g["first_parameter"] for g in result["groups"]], [0, 66, 104, 200, 283, 371, 423])
        self.assertEqual(len(bytes.fromhex(result["scalar_model_rows_f32_hex"])), 426 * 48)
        # Regression pin AFTER the independently native-backed opcode24
        # permutation/32768-quantizer correction. This hash alone is no oracle.
        self.assertEqual(result["scalar_model_rows_sha256"], "fb79dd267fb62650bce59b2d14100bc4557d40d63712fb7576e26e9486f124cb")
        self.assertEqual(result["scalar_bank_sha256"], "ee934915ba1f832e25da615234ccf35e086a76f4236860977a23750ca581be31")
        self.assertAlmostEqual(result["scalars"][26], .3)
        self.assertTrue(any("not evaluated" in note for note in result["limits"]))


if __name__ == "__main__":
    unittest.main()
