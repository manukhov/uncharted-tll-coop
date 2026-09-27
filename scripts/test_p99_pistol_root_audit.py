"""Read-only pinned root contracts and post-pin negative dataflow checks."""
import struct
import unittest
from unittest import mock

import p99_pistol_root_audit as a


class PistolRootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not a.mesh.DEFAULT_ARCHIVE.is_file():
            raise unittest.SkipTest("pinned actor77.psarc is not installed")
        _, cls.data = a.mesh.load_assets()
        cls.report = a.audit(cls.data)

    def changed(self, offset, value, fmt="<H"):
        data = bytearray(self.data)
        struct.pack_into(fmt, data, offset, value)
        return bytes(data)

    def pak(self, data):
        return a.mesh.PagedPak(a.mesh.BytesSource(data))

    def test_seven_typed_clips_and_identity_root(self):
        self.assertEqual(len(self.report["clips"]), 7)
        self.assertEqual({x["name"] for x in self.report["clips"]},
                         {x.name for x in a.CLIPS})
        self.assertTrue(all(x["root_sqt_identity"] for x in self.report["clips"]))

    def test_root_program_does_not_touch_local_or_final_matrix(self):
        result = self.report["joint_program"]
        self.assertEqual(result["root_jointparams_writes"], 0)
        self.assertEqual(result["later_root_matrix_writes"], 0)
        self.assertEqual(result["root_propagation_command"], "BE6")
        self.assertEqual(result["root_parent_encoding"], "0009")

    def test_offline_audit_cannot_claim_live_identity_or_execute_retail(self):
        with mock.patch("native_anim_pose_audit.MappedImage",
                        side_effect=AssertionError("retail execution prohibited")):
            result = a.audit(self.data)
        self.assertFalse(result["live_root_identity_proven"])
        self.assertFalse(result["retail_code_executed"])
        self.assertIn("same-generation", result["limits"])

    def test_unknown_or_truncated_asset_is_rejected(self):
        for data in (self.data[:-1], self.changed(0x880, 2., "<f")):
            with self.assertRaisesRegex(ValueError, "asset size/SHA256"):
                a.audit(data)

    def test_dynamic_root_scatter_is_rejected_beyond_digest_gate(self):
        clip = a.CLIPS[0]
        # Reload op15 map: base2D20 +230 ->2F50, firstT destination50.
        self.assertEqual(struct.unpack_from("<H", self.data, 0x2F50)[0], 0x50)
        with self.assertRaisesRegex(ValueError, "scatter overwrites root"):
            a._audit_clip(self.pak(self.changed(0x2F50, 0x20)), clip)

    def test_dynamic_padding_root_write_is_not_ignored(self):
        clip = a.CLIPS[0]
        with self.assertRaisesRegex(ValueError, "scatter overwrites root"):
            a._audit_clip(self.pak(self.changed(0x2F56, 0)), clip)

    def test_constant_nonidentity_root_is_rejected(self):
        clip = a.CLIPS[-1]
        # Idle map at4170 has40 destinations, followed by40 half3 vectors.
        offset = 0x4150+0x20+40*2
        self.assertEqual(self.data[offset:offset+6], b"\0<\0<\0<")
        with self.assertRaisesRegex(ValueError, "root is not identity"):
            a._audit_clip(self.pak(self.changed(offset, 2., "<e")), clip)

    def test_unknown_clip_opcode_is_not_treated_as_read_only(self):
        with self.assertRaisesRegex(ValueError, "opcode coverage"):
            a._audit_clip(self.pak(self.changed(0x4150, 0x03FF)), a.CLIPS[-1])

    def test_program_local_root_overwrite_is_rejected(self):
        # op52 first destination lives atprogram+70 and normally selectsbank2.
        self.assertEqual(struct.unpack_from("<H", self.data, 0xC20)[0], 2)
        with self.assertRaisesRegex(ValueError, "overwrites root JointParams"):
            a._audit_program(self.changed(0xC20, 0))

    def test_program_root_parent_or_padding_changes_are_rejected(self):
        for offset in (0x1052, 0x105E):
            self.assertEqual(struct.unpack_from("<H", self.data, offset)[0], 9)
            with self.subTest(offset=offset):
                with self.assertRaisesRegex(ValueError, "parent or padding"):
                    a._audit_program(self.changed(offset, 0x49))

    def test_later_root_matrix_overwrite_is_rejected(self):
        # op49 first destination atprogram+4B0+2 normally JP350.
        offset = 0xBB0+0x4B0+2
        self.assertEqual(struct.unpack_from("<H", self.data, offset)[0], 0x350)
        with self.assertRaisesRegex(ValueError, "overwrites root matrix"):
            a._audit_program(self.changed(offset, 0x41))


if __name__ == "__main__":
    unittest.main()
