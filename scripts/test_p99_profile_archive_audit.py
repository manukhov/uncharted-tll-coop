#!/usr/bin/env python3
"""Synthetic PSARC/PAK regressions; no game, Oodle, or mapped retail image."""
import hashlib
import mmap
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
from unittest.mock import patch

import p99_profile_archive_audit as audit
import psarc_asset_audit as reader


def pak_fixture():
    data = bytearray(0x400)
    struct.pack_into("<4I", data, 0x10, 1, 0x28, 8, 0x34)
    struct.pack_into("<3I", data, 0x28, 0x200, 0x200, 0)
    struct.pack_into("<3i", data, 0x34, 0, 0xA0, 1)
    struct.pack_into("<3i", data, 0x34 + 7 * 12, 0, 0xA8, 2)
    for index, offset in enumerate((0x18, 0x30, 0xA8)):
        struct.pack_into("<hhi", data, 0xA0 + index * 8, 0, 0, offset)
    struct.pack_into("<IIQ", data, 0x210, audit.authored.PROFILE_ID, 0, 0x30)
    struct.pack_into("<Q", data, 0x230, 0x80)
    struct.pack_into("<Q", data, 0x2A8, 0x100)
    struct.pack_into("<I", data, 0x2C0, 0x46622D19)
    struct.pack_into("<III", data, 0x300, 0x41430102, 0x8592680F, 0x40)
    return data


def psarc_fixture(payload=b"A" * 70):
    files = (b"test.pak", payload)
    block_size = 32
    chunks, rows, block_lengths = [], [], []
    for data in files:
        first = len(chunks)
        for start in range(0, len(data), block_size):
            raw = data[start:start + block_size]
            compressed = zlib.compress(raw)
            chunk = compressed if len(compressed) < len(raw) else raw
            chunks.append(chunk)
            block_lengths.append(len(chunk) if len(chunk) != block_size else 0)
        rows.append((first, len(data)))
    toc_size = 32 + 2 * 30 + len(chunks)
    header = struct.pack(">4sI4sIIIII", b"PSAR", 0, b"zlib", toc_size, 30, 2, block_size, 0)
    toc, position = bytearray(), toc_size
    for index, (first, size) in enumerate(rows):
        toc.extend(bytes(16) + struct.pack(">I", first) + size.to_bytes(5, "big") + position.to_bytes(5, "big"))
        end = rows[index + 1][0] if index + 1 < len(rows) else len(chunks)
        position += sum(len(chunk) for chunk in chunks[first:end])
    return header + toc + bytes(block_lengths) + b"".join(chunks)


class PakTests(unittest.TestCase):
    def test_resource_header_write_domain(self):
        data = pak_fixture()
        struct.pack_into("<H", data, 0x212, 1)
        struct.pack_into("<i", data, 0x214 + 8, 0x80)
        struct.pack_into("<QQ", data, 0x280, 0x180, 0x188)
        data[0x380:0x385] = b"test\0"
        data[0x388:0x38D] = b"ANIM\0"
        resources = audit.resource_headers(data, audit.authored.source.PagedPak(data))
        self.assertEqual(len(resources), 1)
        self.assertEqual(resources[0]["type"], "ANIM")
        self.assertEqual(resources[0]["prepare_write_ranges"], [[0x14, 8], [0x80, 8], [0x88, 8], [0x98, 4], [0x9C, 4]])

    def test_prelogin_certificate_requires_pinned_elena(self):
        with self.assertRaises(ValueError):
            audit.elena_prelogin_certificate(pak_fixture())

    def test_eighth_directory_is_used(self):
        data = pak_fixture()
        parsed = audit.authored.source.PagedPak(data)
        self.assertEqual(parsed.resolve_pointer(0x2A8), (0, 0x100, 0x300))
        found = audit.registration_candidate(data, 0x210)
        self.assertTrue(found["resolved"])
        self.assertEqual(found["database"], "46622D19")
        self.assertEqual(found["database_pointer_fixup_overlaps"], [])

    def test_empty_directories_do_not_require_zero_fields(self):
        data = pak_fixture()
        struct.pack_into("<iii", data, 0x34 + 12, -2, -3, -1)
        self.assertTrue(audit.registration_candidate(data, 0x210)["resolved"])

    def test_nonempty_external_login_chunk_is_not_content_page(self):
        data = pak_fixture()
        struct.pack_into("<i", data, 0x34 + 7 * 12, 1)
        with self.assertRaisesRegex(ValueError, "login chunk"):
            audit.authored.source.PagedPak(data)

    def test_small_parser_refusal_is_unresolved_not_absent(self):
        data = pak_fixture()
        struct.pack_into("<i", data, 0x34 + 7 * 12, 1)
        found = audit.registration_candidate(data, 0x210)
        self.assertFalse(found["resolved"])
        self.assertTrue(found["unresolved_candidate"])
        self.assertIn("login chunk", found["reason"])

    def test_large_bad_directory_layout_cannot_prove_absence(self):
        data = pak_fixture() + struct.pack("<I", audit.authored.PROFILE_ID) + bytes(16)
        struct.pack_into("<I", data, 0x1C, 0x400)
        archive = reader.Psarc(Path("unused"), None, data=psarc_fixture(data))
        found = audit.large_registration_candidate(archive, "test.pak", len(data), 0x400)
        self.assertTrue(found["unresolved_candidate"])
        self.assertIn("directory offset", found["reason"])

    def test_signed_fixup_fields_refused(self):
        for fmt, offset in (("h", 0xA0), ("h", 0xA2), ("i", 0xA4)):
            data = pak_fixture()
            struct.pack_into("<" + fmt, data, offset, -1)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                audit.authored.source.PagedPak(data)

    def test_table_extents_and_directory_count(self):
        for offset, value in ((0x18, 1), (0x38, 0x80), (0x3C, 1000)):
            data = pak_fixture()
            struct.pack_into("<I", data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                audit.authored.source.PagedPak(data)

    def test_registration_rejects_false_pattern_and_raw_extent(self):
        for offset, value in ((0x210, 123), (0x300, 0), (0x308, 0x1000)):
            data = pak_fixture()
            struct.pack_into("<I", data, offset, value)
            self.assertFalse(audit.registration_candidate(data, 0x210)["resolved"])

    def test_registration_padding_is_not_an_admission_condition(self):
        data = pak_fixture()
        struct.pack_into("<I", data, 0x214, 0x12345)
        found = audit.registration_candidate(data, 0x210)
        self.assertTrue(found["resolved"])
        self.assertEqual(found["padding_word"], 0x12345)

    def test_large_candidate_refusal_requires_missing_pointer_fixup(self):
        data = pak_fixture()
        archive = reader.Psarc(Path("unused"), None, data=psarc_fixture(data))
        existing = audit.large_registration_candidate(archive, "test.pak", len(data), 0x210)
        self.assertTrue(existing["unresolved_candidate"])
        struct.pack_into("<I", data, 0x214, 0x12345)
        struct.pack_into("<i", data, 0xA4, 0x50)
        archive = reader.Psarc(Path("unused"), None, data=psarc_fixture(data))
        missing = audit.large_registration_candidate(archive, "test.pak", len(data), 0x210)
        self.assertNotIn("unresolved_candidate", missing)
        self.assertEqual(missing["fixup_directories_checked"], 8)
        self.assertIn("no authored relocation", missing["reason"])

    def test_appended_texture_bytes_are_outside_content_pages(self):
        data = pak_fixture() + struct.pack("<I", audit.authored.PROFILE_ID) + bytes(16)
        archive = reader.Psarc(Path("unused"), None, data=psarc_fixture(data))
        found = audit.large_registration_candidate(archive, "test.pak", len(data), 0x400)
        self.assertNotIn("unresolved_candidate", found)
        self.assertEqual(found["reason"], "outside all authored content pages")
        self.assertEqual(found["content_page_ranges"], [[0x200, 0x200]])


class ArchiveTests(unittest.TestCase):
    def test_invalid_toc_and_zero_block_size_refused(self):
        for offset, value in ((24, 0), (20, 0), (12, 2 ** 32 - 1), (16, 29)):
            data = bytearray(psarc_fixture())
            struct.pack_into(">I", data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                reader.Psarc(Path("unused"), None, data=data)

    def test_borrowed_map_and_compressed_range(self):
        blob = psarc_fixture()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic.psarc"
            path.write_bytes(blob)
            with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapped, \
                    patch.object(Path, "read_bytes", side_effect=AssertionError("must not snapshot archive")):
                archive = reader.Psarc(path, None, data=mapped)
                self.assertEqual(archive.extract("test.pak"), b"A" * 70)
                self.assertEqual(archive.extract_range("test.pak", 30, 6), b"A" * 6)
                self.assertEqual(archive.extract_range("test.pak", 70, 0), b"")
                for start, size in ((-1, 1), (0, -1), (69, 2), (71, 0)):
                    with self.assertRaises(ValueError):
                        archive.extract_range("test.pak", start, size)
                self.assertEqual(mapped[:], blob)

    def test_plain_bytes_default_unchanged(self):
        blob = psarc_fixture(bytes(range(70)))
        with patch.object(Path, "read_bytes", return_value=blob):
            archive = reader.Psarc(Path("unused"), None)
        self.assertEqual(archive.extract("test.pak"), bytes(range(70)))
        self.assertEqual(archive.extract_range("test.pak", 63, 7), bytes(range(63, 70)))

    def test_truncated_block_refused(self):
        archive = reader.Psarc(Path("unused"), None, data=psarc_fixture()[:-1])
        with self.assertRaises(ValueError):
            archive.extract("test.pak")

    def test_streaming_search_covers_block_boundary(self):
        for split in range(1, 4):
            needle = struct.pack("<I", audit.authored.PROFILE_ID)
            payload = b"x" * (32 - split) + needle + b"y" * 50
            archive = reader.Psarc(Path("unused"), None, data=psarc_fixture(payload))
            result = audit.inspect_archive(archive, True)
            self.assertEqual(result["entries"][0]["sha256"], hashlib.sha256(payload).hexdigest())
            self.assertEqual(result["entries"][0]["byte_offsets"], [32 - split])
            self.assertFalse(result["hits"][0]["registration"]["resolved"])

    def test_profile_name_crosses_every_possible_chunk_boundary(self):
        self.assertEqual(reader.string_id(audit.PROFILE_NAME[:-1]), audit.authored.PROFILE_ID)
        for split in range(1, len(audit.PROFILE_NAME)):
            payload = b"x" * (32 - split) + audit.PROFILE_NAME + b"y" * 50
            archive = reader.Psarc(Path("unused"), None, data=psarc_fixture(payload))
            result = audit.inspect_archive(archive, True)
            self.assertEqual(result["entries"][0]["profile_name_offsets"], [32 - split])

    def test_index_does_not_scan_payload_or_claim_live_registry(self):
        archive = reader.Psarc(Path("unused"), None, data=psarc_fixture())
        with patch.object(archive, "extract", side_effect=AssertionError("no extract")):
            result = audit.inspect_archive(archive, False)
        self.assertEqual(result["pak_count"], 1)
        self.assertFalse(result["all_paks_scanned"])
        self.assertEqual(result["entries"], [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
