"""Pinned, pure-byte checks for normal arms ComputeWind admission."""
import unittest
from unittest import mock

import p99_arms_wind_audit as a


class NativeContractUnitTests(unittest.TestCase):
    def test_missing_native_code_rejected(self):
        with self.assertRaisesRegex(ValueError, "truncated native contract"):
            a.validate_native_contract(lambda address, size: None)

    def test_wrong_complete_native_code_rejected(self):
        with self.assertRaisesRegex(ValueError, "changed native contract"):
            a.validate_native_contract(lambda address, size: bytes(size))


class PinnedWindTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not a.DEFAULT_EXE.exists() or not a.DEFAULT_BODY.exists():
            raise unittest.SkipTest("pinned local body/retail PE unavailable")
        cls.image = a.Image(str(a.DEFAULT_EXE))
        with mock.patch("native_anim_pose_audit.MappedImage", side_effect=AssertionError("retail execution forbidden")):
            cls.result = a.audit()

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "image"):
            cls.image.pe.close()

    def patched_reader(self, changed_address, replacement):
        def read(address, size):
            original = self.image.read(address, size)
            if original is None:
                return None
            output = bytearray(original)
            for index, value in enumerate(replacement):
                at = changed_address + index - address
                if 0 <= at < len(output):
                    output[at] = value
            return bytes(output)
        return read

    def test_reviewed_complete_functions_and_rtti_match(self):
        self.assertEqual(len(a.validate_native_contract(self.image.read)), 10)

    def test_argument14_cannot_change_to_enabled(self):
        with self.assertRaisesRegex(ValueError, "changed native contract"):
            a.validate_native_contract(self.patched_reader(0x14192EC7D, b"\x01"))

    def test_every_reviewed_function_is_required_not_only_callsite(self):
        for address, size, name, digest in a.FUNCTIONS:
            original = self.image.read(address + size - 1, 1)[0]
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "changed native contract"):
                a.validate_native_contract(self.patched_reader(address + size - 1, bytes([original ^ 1])))

    def test_rtti_and_vtable_cannot_point_elsewhere(self):
        for address, value, label in a.EXACT_FIELDS[1:]:
            with self.subTest(label=label), self.assertRaisesRegex(ValueError, "contract field"):
                a.validate_native_contract(self.patched_reader(address, bytes([value[0] ^ 1])))

    def test_wrong_body_cannot_receive_normal_arms_admission(self):
        pak = a.PagedPak(a.DEFAULT_BODY)
        pak.data = pak.data[:-1] + bytes([pak.data[-1] ^ 1])
        with self.assertRaisesRegex(ValueError, "changed Nadine rural body"):
            a.validate_body_scope(pak)

    def test_scope_is_wind_command_absence_not_global_position_absence(self):
        result = self.result
        self.assertEqual(result["classification"], "STATIC_NORMAL_FGINSTANCE_COMPUTE_WIND_ABSENT")
        self.assertEqual(result["ownership"]["material"], "18D658")
        for field in ("wind_descriptor_scan_enabled", "normal_initializer_constructs_wind_deformer",
                      "normal_command_path_constructs_compute_wind", "global_or_live_position_writer_absence_proven",
                      "retail_or_game_execution", "runtime_writes", "shader_execution"):
            with self.subTest(field=field):
                self.assertIs(result[field], False)
        self.assertIn("argument14=true", result["contrasting_path"])


if __name__ == "__main__":
    unittest.main()
