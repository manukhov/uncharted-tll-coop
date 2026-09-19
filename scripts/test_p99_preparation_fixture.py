"""Owned data graph tests. No retail image/code is mapped or executed."""
import ctypes
import struct
import unittest

import p99_preparation_fixture as f


class DataImage:
    def __init__(self):
        self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._kernel32.VirtualAlloc.restype = ctypes.c_void_p
        self.size = 0x8000000
        self.address = self._kernel32.VirtualAlloc(None, self.size, 0x3000, 4)
        if not self.address:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.address:
            if not self._kernel32.VirtualFree(ctypes.c_void_p(self.address), 0, 0x8000):
                raise ctypes.WinError(ctypes.get_last_error())
            self.address = 0


class FixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = f.source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
        cls.loaded = f.audit.load_inputs(base / "actor77.psarc", base / "dc1.psarc")

    def test_buckets_match_static_fvn(self):
        self.assertEqual([f.bucket_for(s.spec.database_id) for s in self.loaded["skels"]], [227, 89, 358])

    def test_pinned_artifacts(self):
        first, second = f.validate_artifacts()
        self.assertEqual((len(first), len(second)), (4278, 3266))

    def test_owned_graph_restore_and_negative_mutations(self):
        image = DataImage()
        try:
            for count in (175, 4096):
                with f.PreparationFixture(image, self.loaded, table_count=count) as fixture:
                    self.assertEqual(ctypes.c_uint64.from_address(fixture.job + 8).value, fixture.fg)
                    for field in ("weapon_generation", "fg_raw_joint_buffer", "record20_numeric",
                                  "record157_last_numeric", "record20_padding", "lock"):
                        address, size = fixture.fields[field]
                        before = ctypes.string_at(address, size)
                        altered = bytes([before[0] ^ 1]) + before[1:]
                        with fixture.patch(address, altered):
                            self.assertEqual(ctypes.string_at(address, size), altered)
                            with self.assertRaises(ValueError):
                                fixture.verify()
                        fixture.verify()
                    with self.assertRaises(ValueError):
                        with fixture.patch(image.address + 0x1000, b"x"):
                            pass
                    for i, skel in enumerate(self.loaded["skels"]):
                        node = fixture.buffers[f"registry_node{i}"].read()
                        self.assertEqual(struct.unpack_from("<Q", node)[0], 0)
                        self.assertEqual(struct.unpack_from("<I", node, 0x10)[0], skel.spec.database_id)
                self.assertEqual(ctypes.c_uint64.from_address(image.address + f.RETAINED - f.BASE).value, 0)
                self.assertEqual(ctypes.c_uint64.from_address(image.address + f.LOCK - f.BASE).value, 0)
        finally:
            image.close()

    def test_table_count_refuses_before_any_image_access(self):
        for count in (0, 157, 174, 4097):
            with self.assertRaises(ValueError):
                f.PreparationFixture(None, {}, table_count=count)


if __name__ == "__main__":
    unittest.main()
