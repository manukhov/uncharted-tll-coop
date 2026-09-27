"""Pure format guards plus optional pinned SDK disassembly; never run shaders."""
import struct
import unittest

import p99_skin_shader_audit as a


IR = """
; g_infl SkinningVertexInfo
%2 = lshr i32 %1, 22
%3 = and i32 %1, 4194303
%4 = uitofp i32 %3 to float
%5 = fmul fast float %4, 0x3E90000040000000
%6 = add i32 %20, 4
%7 = mul nuw nsw i32 %2, 48
%8 = call float @dx.op.dot4.f32(i32 56)
%9 = call float @dx.op.dot4.f32(i32 56)
%10 = call float @dx.op.dot4.f32(i32 56)
%11 = fmul fast float %8, %5
%12 = fmul fast float %9, %5
%13 = fmul fast float %10, %5
%14 = fadd fast float %11, %21
%15 = fadd fast float %12, %22
%16 = fadd fast float %13, %23
"""


class FormatTests(unittest.TestCase):
    def test_exact_scale_and_full_weight(self):
        self.assertEqual(struct.unpack("<I", struct.pack("<f", 1 / 4194303))[0], 0x34800002)
        for joint in (0, 1, 2, 8, 579, 1023):
            self.assertEqual(a.decode_word((joint << 22) | 0x3FFFFF), (joint, 4194303, 1.))

    def test_low16_zero_still_positive(self):
        joint, numerator, weight = a.decode_word((137 << 22) | 0x10000)
        self.assertEqual((joint, numerator), (137, 65536))
        self.assertGreater(weight, 0.)
        self.assertEqual(((137 << 22) | 0x10000) & 0xFFFF, 0)

    def test_zero_and_unsigned_limits(self):
        self.assertEqual(a.decode_word(0), (0, 0, 0.))
        self.assertEqual(a.decode_word(0xFFFFFFFF), (1023, 4194303, 1.))
        for invalid in (-1, 1 << 32, True, 1., None):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                a.decode_word(invalid)

    def test_connected_ir_chain(self):
        proof = a.prove_packed_ir(IR)
        self.assertEqual(proof["weight_scale_f32_bits"], "34800002")
        self.assertEqual(proof["weighted_position_lanes"], 3)
        self.assertFalse(proof["weight_sum_renormalized"])

    def test_competing_unpack_models_rejected(self):
        changes = (("4194303", "65535"), (", 22", ", 16"),
                   ("uitofp", "sitofp"), ("0x3E90000040000000", "0x3E90000000000000"),
                   ("and i32 %1", "and i32 %99"), ("i32 %3 to float", "i32 %99 to float"),
                   ("float %4, 0x", "float %99, 0x"), ("i32 %2, 48", "i32 %2, 64"),
                   ("fadd fast float %11", "fadd fast float %99"))
        for old, new in changes:
            with self.subTest(old=old), self.assertRaises(ValueError):
                a.prove_packed_ir(IR.replace(old, new))

    def test_weight_sum_division_rejected(self):
        with self.assertRaises(ValueError):
            a.prove_packed_ir(IR + "\n%99 = fdiv float %14, %25\n")

    def test_unpinned_blob_refused_before_disassembly(self):
        with self.assertRaisesRegex(ValueError, "unreviewed"):
            a.admit_blob(next(iter(a.SHADERS)), b"DXBC" + bytes(64))
        with self.assertRaisesRegex(ValueError, "unreviewed"):
            a.admit_blob("unknown", b"")


@unittest.skipUnless(a.DEFAULT_ARCHIVE.exists() and a.DEFAULT_COMPILER.exists(),
                     "pinned archive or SDK disassembler unavailable")
class PinnedDisassemblyTests(unittest.TestCase):
    def test_five_packed_mode1_shader_families(self):
        result = a.audit()
        self.assertEqual(len(result["shaders"]), 5)
        self.assertFalse(result["retail_or_shader_execution"])
        for shader in result["shaders"]:
            self.assertEqual(shader["weight_mask"], 4194303)
            self.assertEqual(shader["weight_scale_f32_bits"], "34800002")
            self.assertEqual(shader["joint_shift"], 22)
            self.assertEqual(shader["palette_row_bytes"], 48)


if __name__ == "__main__":
    unittest.main()
