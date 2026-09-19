"""Pure PE/descriptor/cleanup tests: fake memory only, no DLL or native execution."""
import ctypes
import struct
from types import SimpleNamespace
import unittest

import p99_query_layout_probe as p


def pe_headers():
    result = bytearray(4096)
    result[:2] = b"MZ"
    struct.pack_into("<I", result, 0x3C, 0x80)
    result[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", result, 0x84, 0x8664, 1)
    struct.pack_into("<H", result, 0x94, 0xF0)
    struct.pack_into("<H", result, 0x98, 0x20B)
    struct.pack_into("<Q", result, 0xB0, p.PREFERRED_BASE)
    struct.pack_into("<I", result, 0xD0, p.IMAGE_BYTES)
    result[0x188:0x190] = b".data\0\0\0"
    struct.pack_into("<II", result, 0x190, 0x3DD6000, 0x2FDD000)
    struct.pack_into("<I", result, 0x1AC, 0xC0000040)
    return bytes(result)


class Fake:
    def __init__(self, failure=None):
        self.image = SimpleNamespace(address=0x10000000, size=p.IMAGE_BYTES)
        def buffer(base, size, pages):
            return SimpleNamespace(base=base, address=base + 0x1040,
                                   size=size, pages=pages, readonly=True)
        self.fixture = SimpleNamespace(image=self.image, _closed=False, table_count=175,
            _image_slots=[], buffers={"weapon": buffer(0x50000000, 0x864, 0x1000),
                                     "skeleton0": buffer(0x60000000, 424352, 0x68000)},
            verify=lambda: None)
        self.protections, self.protect_calls = {}, []
        self.failure = failure
        self.timed_calls = self.summaries = 0

    def query(self, address):
        image = self.image.address
        if image <= address < image + self.image.size:
            allocation, initial = image, 4
            base = address & ~4095
            protect = self.protections.get(base, p.RX)
            if protect == p.RO:
                end = base + 4096
            else:
                end = min([image + 0x66EC000] + [page for page in self.protections if page > address])
        else:
            buffer = next(b for b in self.fixture.buffers.values()
                          if b.address <= address < b.address + b.size)
            allocation, initial, base = buffer.base, 1, buffer.base + 4096
            protect, end = p.RO, base + buffer.pages
            if self.protections and self.failure == "control_changed":
                initial = 4
        return {"base": base, "allocation_base": allocation, "allocation_protect": initial,
                "partition_id": 0, "region_size": end - base, "state": p.COMMIT,
                "protect": protect, "type": p.PRIVATE}

    def timed_query(self, address):
        self.timed_calls += 1
        if self.protections and self.failure == "split_sample":
            raise ValueError("injected split sample failure")
        return self.query(address), 1000

    def protect(self, address, protection):
        self.protect_calls.append((address, protection))
        second = self.image.address + p.SPLIT_RVAS[1]
        if address == second and protection == p.RO and self.failure == "second_protect":
            raise OSError("injected second protect failure")
        if address == second and protection == p.RX and self.failure == "restore":
            raise OSError("injected restore failure")
        old = self.protections.get(address, p.RX)
        if protection == p.RX:
            self.protections.pop(address, None)
        else:
            self.protections[address] = protection
        return old

    def read(self, address, size):
        return pe_headers() if address == self.image.address else bytes([0xA5]) * size

    def distribution(self, values):
        if self.protections:
            raise AssertionError("caller callback ran while image was split")
        self.summaries += 1
        return {"count": len(values), "min_ms": min(values), "max_ms": max(values)}

    def run(self, samples=2):
        return p._measure_owned(self.image, self.fixture, samples, self.distribution, self)


class QueryLayoutTests(unittest.TestCase):
    def test_sdk_abi(self):
        self.assertEqual(ctypes.sizeof(p.MemoryBasicInformation64), 48)
        self.assertEqual(p.MemoryBasicInformation64.PartitionId.offset, 20)
        self.assertEqual(p.MemoryBasicInformation64.RegionSize.offset, 24)
        self.assertEqual(p.MemoryBasicInformation64.Type.offset, 40)

    def test_exact_pe_layout(self):
        self.assertEqual(p.validate_layout(pe_headers(), p.IMAGE_BYTES)["split_rvas"], list(p.SPLIT_RVAS))
        for offset, fmt, value in ((0x84, "H", 0x14C), (0x86, "H", 97), (0x94, "H", 0),
                (0x98, "H", 0x10B), (0xB0, "Q", 0), (0xD0, "I", 0x8000000),
                (0x194, "I", 0), (0x1AC, "I", 0xE0000040)):
            bad = bytearray(pe_headers())
            struct.pack_into("<" + fmt, bad, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                p.validate_layout(bytes(bad), p.IMAGE_BYTES)
        bad = bytearray(pe_headers())
        struct.pack_into("<H", bad, 0x86, 2)
        bad[0x1B0:0x1B8] = b".text\0\0\0"
        struct.pack_into("<II", bad, 0x1B8, 4096, p.SPLIT_RVAS[0])
        struct.pack_into("<I", bad, 0x1D4, 0x60000020)
        with self.assertRaises(ValueError):
            p.validate_layout(bytes(bad), p.IMAGE_BYTES)

    def test_descriptor_admission(self):
        fake = Fake()
        address, end = fake.image.address, fake.image.address + fake.image.size
        good = fake.query(address)
        p.validate_descriptor(good, address, address, end, p.RX)
        for key, value in (("base", address + 1), ("allocation_base", address + 4096),
                ("region_size", 0), ("region_size", 1 << 64), ("state", 0x2000),
                ("type", 0x1000000), ("protect", 0x120), ("protect", 4), ("partition_id", -1)):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                p.validate_descriptor(good | {key: value}, address, address, end, p.RX)

    def test_three_paired_phases_restore(self):
        fake = Fake()
        report = fake.run()
        self.assertEqual(fake.timed_calls, 3 * 4 * 3)
        self.assertEqual(fake.summaries, 12)
        self.assertEqual(fake.protections, {})
        self.assertEqual(len(fake.protect_calls), 4)
        self.assertTrue(report["protections_restored"] and report["page_bytes_unchanged"])
        self.assertEqual(report["retail_or_preparation_calls"], 0)
        for name in ("fixed_table", "retained_slot", "weapon", "skeleton0"):
            self.assertEqual(report["before"][name]["descriptor"], report["restored"][name]["descriptor"])
            self.assertEqual(report["split"][name]["descriptor"]["protect"],
                             p.RO if name in ("weapon", "skeleton0") else p.RX)
        self.assertLess(report["split"]["fixed_table"]["descriptor"]["region_size"],
                        report["before"]["fixed_table"]["descriptor"]["region_size"])

    def test_failure_paths_restore_all_changed_pages(self):
        for failure in ("second_protect", "split_sample", "control_changed"):
            fake = Fake(failure)
            with self.subTest(failure=failure), self.assertRaises((ValueError, OSError)):
                fake.run()
            self.assertEqual(fake.protections, {})
            self.assertEqual(fake.summaries, 0)

    def test_restore_failure_does_not_skip_other_page(self):
        fake = Fake("restore")
        with self.assertRaisesRegex(RuntimeError, "cleanup failed"):
            fake.run()
        self.assertIn((fake.image.address + p.SPLIT_RVAS[0], p.RX), fake.protect_calls)
        self.assertNotIn(fake.image.address + p.SPLIT_RVAS[0], fake.protections)

    def test_bad_context_and_overlap_never_protect(self):
        for samples in (0, 513, True):
            fake = Fake()
            with self.assertRaises(ValueError):
                fake.run(samples)
            self.assertEqual(fake.protect_calls, [])
        fake = Fake()
        fake.fixture._image_slots = [(fake.image.address + p.SPLIT_RVAS[0], b"x", b"x")]
        with self.assertRaises(ValueError):
            fake.run()
        self.assertEqual(fake.protect_calls, [])

    def test_public_api_refuses_unpinned_fake_without_queries(self):
        fake = Fake()
        with self.assertRaises(ValueError):
            p.measure(fake.image, fake.fixture, 2, fake.distribution)
        self.assertEqual(fake.protect_calls, [])


if __name__ == "__main__":
    unittest.main()
