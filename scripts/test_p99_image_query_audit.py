"""Pure query-only image admission/cleanup tests; no file mapping or retail code."""
import hashlib
import json
from pathlib import Path
import struct
from types import SimpleNamespace
import unittest

import p99_image_query_audit as a


def header():
    value = bytearray(4096)
    value[:2] = b"MZ"
    struct.pack_into("<I", value, 0x3C, 0x80)
    value[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", value, 0x84, 0x8664, 1)
    struct.pack_into("<H", value, 0x94, 0xF0)
    struct.pack_into("<H", value, 0x98, 0x20B)
    struct.pack_into("<Q", value, 0xB0, a.layout.PREFERRED_BASE)
    struct.pack_into("<II", value, 0xD0, a.layout.IMAGE_BYTES, 0x400)
    value[0x188:0x190] = b".data\0\0\0"
    struct.pack_into("<II", value, 0x190, 0x3DD6000, 0x2FDD000)
    struct.pack_into("<I", value, 0x1AC, 0xC0000040)
    value[0x400:] = bytes([0xB7]) * (4096 - 0x400)  # Disk section data is NOT header padding.
    return bytes(value)


class Fake:
    image = 0x50000000
    def __init__(self, failure=None):
        self.data, self.failure = header(), failure
        self.position, self.timed_calls = 0, 0
        self.view_reads = 0
        self.header_base, self.timed_addresses = None, []
        self.events, self.open_handles = [], set()

    def open_file(self, path):
        self.events.append("open_file")
        self.open_handles.add(10)
        return 10

    def file_size(self, handle):
        assert handle in self.open_handles
        return len(self.data)

    def read_file(self, handle, size):
        assert handle in self.open_handles
        data = self.data[self.position:self.position + size]
        self.position += len(data)
        return data

    def create_mapping(self, handle):
        assert handle in self.open_handles and self.position == len(self.data)
        self.events.append("create_mapping")
        if self.failure == "create_mapping":
            raise OSError("mapping failure")
        self.open_handles.add(20)
        return 20

    def map_view(self, handle):
        assert 10 in self.open_handles and handle in self.open_handles
        self.events.append("map_view")
        if self.failure == "map_view":
            raise OSError("view failure")
        return self.image

    def query(self, address, timed=False):
        assert self.open_handles == {10, 20}, "file must remain locked throughout view lifetime"
        base = address & ~4095
        end = self.image + (4096 if address == self.image else 0x6DB3000)
        value = {"base": base, "allocation_base": self.image, "allocation_protect": 2,
                 "partition_id": 0, "region_size": end - base, "state": 0x1000,
                 "protect": 2, "type": a.MEM_IMAGE}
        if address == self.image + 0x1000 and self.failure == "code_execute":
            value["protect"] = 0x20
        if timed:
            self.timed_calls += 1
            self.timed_addresses.append(address)
            if self.failure == "query":
                raise OSError("query failure")
            if self.failure == "executable":
                value["protect"] = 0x20
            if self.failure == "foreign_allocation":
                value["allocation_base"] += 4096
            if self.failure == "topology" and self.timed_calls > 2:
                value["allocation_protect"] = 8
        return value, 1000 if timed else 0

    def read_view(self, address, size):
        self.view_reads += 1
        value = bytearray(self.data[:0x400] + bytes(4096 - 0x400))
        struct.pack_into("<Q", value, 0xB0, self.image if self.header_base is None else self.header_base)
        if self.failure == "header":
            value[0x20] = 0x78
        if self.failure == "image_base":
            struct.pack_into("<Q", value, 0xB0, 0x7FF600000000)
        if self.failure == "outside_base":
            value[0xB8] ^= 1
        if self.failure == "header_after" and self.view_reads > 1:
            value[0xFFF] = 1
        return bytes(value[:size])

    def unmap(self, image):
        assert image == self.image and self.open_handles == {10, 20}
        self.events.append("unmap")
        if self.failure == "unmap":
            raise OSError("unmap failure")

    def close(self, handle):
        self.events.append(f"close{handle}")
        self.open_handles.discard(handle)
        if self.failure == f"close{handle}":
            raise OSError("close failure")

    def run(self, execute=True, samples=2, digest=None):
        return a._audit(Path("fake.exe"), execute, samples, self,
                        digest or hashlib.sha256(self.data).hexdigest())


class ImageQueryTests(unittest.TestCase):
    def test_static_preflight_never_maps(self):
        fake = Fake()
        result = fake.run(False)
        self.assertEqual(fake.events, ["open_file", "close10"])
        self.assertEqual(result["classification"], "STATIC_PREFLIGHT")
        self.assertFalse(result["mapping_created"])
        self.assertEqual(result["queries"], {})

    def test_success_schema_and_header_size(self):
        fake = Fake()
        result = fake.run()
        self.assertEqual(fake.events[-3:], ["unmap", "close20", "close10"])
        self.assertEqual(fake.open_handles, set())
        self.assertEqual(fake.timed_calls, 6)
        self.assertTrue(result["cleanup_complete"])
        self.assertEqual(result["mapping"]["protection_flags"], 0x11000002)
        self.assertEqual(result["mapping"]["view_access"], 4)
        self.assertEqual(result["mapping"]["text_probe_rva"], 0x1000)
        self.assertEqual(result["mapping"]["text_region"]["protect"], 2)
        self.assertEqual(result["file"]["header_bytes"], 0x400)
        admission = result["mapping"]["header_admission"]
        self.assertEqual(admission["raw_image_base"], a.layout.PREFERRED_BASE)
        self.assertEqual(admission["mapped_header_image_base"], fake.image)
        self.assertEqual(admission["actual_view_base"], fake.image)
        self.assertTrue(admission["image_base_matches_view"])
        self.assertEqual(admission["image_base_width"], 8)
        self.assertGreater(admission["difference_count"], 0)
        self.assertEqual(result["retail_calls"] + result["payload_writes"] + result["virtual_protect_calls"], 0)
        self.assertFalse(result["live_budget_admitted"])
        self.assertEqual(set(result["queries"]), set(a.CURSORS))
        for query in result["queries"].values():
            self.assertEqual(query["warm_time"]["count"], 2)
            self.assertEqual(query["descriptor"]["type"], a.MEM_IMAGE)

    def test_flags_use_read_lock_and_nonexecuting_image_view(self):
        calls = []
        windows = a._Windows.__new__(a._Windows)
        windows.kernel = SimpleNamespace(
            CreateFileW=lambda *args: calls.append(args) or 10,
            CreateFileMappingW=lambda *args: calls.append(args) or 20,
            MapViewOfFile=lambda *args: calls.append(args) or Fake.image)
        windows.open_file(Path("fake.exe"))
        windows.create_mapping(10)
        windows.map_view(20)
        self.assertEqual(calls[0][1:], (0x80000000, 1, None, 3, 0x80, None))
        self.assertEqual(calls[1], (10, None, 0x11000002, 0, 0, None))
        self.assertEqual(calls[2], (20, 4, 0, 0, 0))

    def test_arbitrary_header_base_never_controls_query_addresses(self):
        for header_base in (0, (1 << 64) - 1, 0x7FF600000000, a.layout.PREFERRED_BASE, Fake.image):
            fake = Fake()
            fake.header_base = header_base
            result = fake.run()
            details = result["mapping"]["header_admission"]
            self.assertEqual(details["mapped_header_image_base"], header_base)
            self.assertEqual(details["actual_view_base"], fake.image)
            self.assertEqual(details["image_base_matches_view"], header_base == fake.image)
            self.assertEqual(fake.timed_addresses, [fake.image + rva for rva in a.CURSORS.values()] * 3)
            self.assertEqual(fake.events[-3:], ["unmap", "close20", "close10"])

    def test_preferred_mapping_keeps_exact_header_admission(self):
        fake = Fake()
        fake.image = a.layout.PREFERRED_BASE
        result = fake.run()
        admission = result["mapping"]["header_admission"]
        self.assertEqual(admission["difference_count"], 0)
        self.assertEqual(admission["raw_declared_header_sha256"], admission["mapped_declared_header_sha256"])

    def test_header_width_raw_pin_and_outside_field_are_rejected(self):
        raw = header()
        mapped = bytearray(raw[:0x400] + bytes(4096 - 0x400))
        struct.pack_into("<Q", mapped, 0xB0, Fake.image)
        a.admit_mapped_header(raw, bytes(mapped), Fake.image)
        with self.assertRaises(ValueError):
            a.admit_mapped_header(raw, bytes(mapped[:-1]), Fake.image)
        bad_raw = bytearray(raw)
        struct.pack_into("<Q", bad_raw, 0xB0, Fake.image)
        with self.assertRaisesRegex(ValueError, "pinned preferred base"):
            a.admit_mapped_header(bytes(bad_raw), bytes(mapped), Fake.image)
        mapped[0xB8] ^= 1  # The ninth byte is NOT part of the allowed field.
        with self.assertRaisesRegex(ValueError, "outside the single eight-byte"):
            a.admit_mapped_header(raw, bytes(mapped), Fake.image)

    def test_header_diagnostic_caps_payload_and_handles_malformed_offsets(self):
        for raw, mapped in ((bytes(1024), bytes([1]) * 1024),
                (bytes(1024), bytes([1, 0]) * 512), (header()[:1024], b"MZ")):
            with self.assertRaises(ValueError) as raised:
                a._require_same_headers(raw, mapped)
            details = json.loads(str(raised.exception).split(": ", 1)[1])
            self.assertLessEqual(len(details["ranges"]), 16)
            self.assertLessEqual(details["shown_payload_bytes"], 64)
            self.assertIn("unavailable", details["mapped_pe"])

    def test_hash_and_headers_refuse_before_mapping(self):
        fake = Fake()
        with self.assertRaises(ValueError):
            fake.run(digest="0" * 64)
        self.assertEqual(fake.events, ["open_file", "close10"])
        fake = Fake()
        bad = bytearray(fake.data)
        struct.pack_into("<I", bad, 0xD4, 0x1000)
        fake.data = bytes(bad)
        with self.assertRaises(ValueError):
            fake.run()
        self.assertEqual(fake.events, ["open_file", "close10"])

    def test_partial_failures_close_every_open_resource(self):
        for failure, suffix in (("create_mapping", ["close10"]),
                ("map_view", ["close20", "close10"]), ("header", ["unmap", "close20", "close10"]),
                ("query", ["unmap", "close20", "close10"]),
                ("outside_base", ["unmap", "close20", "close10"]),
                ("header_after", ["unmap", "close20", "close10"]),
                ("code_execute", ["unmap", "close20", "close10"]),
                ("executable", ["unmap", "close20", "close10"]),
                ("foreign_allocation", ["unmap", "close20", "close10"]),
                ("topology", ["unmap", "close20", "close10"])):
            fake = Fake(failure)
            with self.subTest(failure=failure), self.assertRaises((ValueError, OSError)):
                fake.run()
            self.assertEqual(fake.events[-len(suffix):], suffix)
            self.assertEqual(fake.open_handles, set())

    def test_cleanup_failure_does_not_skip_later_cleanup(self):
        for failure in ("unmap", "close20", "close10"):
            fake = Fake(failure)
            with self.subTest(failure=failure), self.assertRaisesRegex(RuntimeError, "cleanup failed"):
                fake.run()
            self.assertEqual(fake.events[-3:], ["unmap", "close20", "close10"])

    def test_region_rejects_unsafe_or_foreign_memory(self):
        fake = Fake()
        fake.open_handles = {10, 20}
        address = fake.image + a.CURSORS["fixed_table"]
        good, _ = fake.query(address)
        for key, value in (("type", 0x20000), ("protect", 0x102), ("protect", 0x20),
                ("protect", 1), ("state", 0x2000), ("allocation_base", fake.image + 4096),
                ("region_size", 0), ("region_size", 1 << 64), ("base", address + 1)):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                a.validate_region(good | {key: value}, address, fake.image)

    def test_bad_sample_bound_never_opens_file(self):
        for samples in (0, 513, True):
            fake = Fake()
            with self.assertRaises(ValueError):
                fake.run(samples=samples)
            self.assertEqual(fake.events, [])


if __name__ == "__main__":
    unittest.main()
