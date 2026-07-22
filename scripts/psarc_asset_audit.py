#!/usr/bin/env python3
"""Read-only PSARC manifest, StringId, and printable-string inspector.

The Lost Legacy's gameplay DC and animation archives use Oodle-compressed PSARC
blocks.  This tool keeps selected entries in memory; it never extracts assets to
the workspace.  It is intentionally small and analysis-oriented rather than a
general-purpose archive unpacker.
"""

from __future__ import annotations

import argparse
import ctypes
import re
import struct
import sys
from pathlib import Path


def string_id(text: bytes) -> int:
    """Return the game's non-reflected, zero-seeded CRC32 StringId."""

    value = 0
    for byte in text:
        value ^= byte << 24
        for _ in range(8):
            if value & 0x80000000:
                value = ((value << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                value = (value << 1) & 0xFFFFFFFF
    return value


def parse_id(value: str) -> tuple[int, str]:
    try:
        if value.lower().startswith("0x"):
            return int(value, 16) & 0xFFFFFFFF, value
        if value.isdecimal():
            return int(value, 10) & 0xFFFFFFFF, value
    except ValueError:
        pass
    return string_id(value.encode("utf-8")), value


def find_oodle(archive: Path, explicit: Path | None) -> Path:
    if explicit is not None:
        if not explicit.is_file():
            raise FileNotFoundError(f"Oodle DLL not found: {explicit}")
        return explicit

    for parent in archive.parents:
        candidate = parent / "oo2core_9_win64.dll"
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "oo2core_9_win64.dll was not found above the archive; pass --oodle"
    )


class Psarc:
    def __init__(self, path: Path, oodle_path: Path | None) -> None:
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 32:
            raise ValueError("truncated PSARC header")

        (
            magic,
            self.version,
            self.compression,
            toc_size,
            entry_size,
            entry_count,
            self.block_size,
            self.flags,
        ) = struct.unpack_from(">4sI4sIIIII", self.data, 0)
        if magic != b"PSAR":
            raise ValueError(f"not a PSARC archive: {magic!r}")
        if entry_size < 30:
            raise ValueError(f"unsupported TOC entry size: {entry_size}")

        self.entries: list[tuple[int, int, int]] = []
        for index in range(entry_count):
            offset = 32 + index * entry_size
            block_index = struct.unpack_from(">I", self.data, offset + 16)[0]
            size = int.from_bytes(self.data[offset + 20 : offset + 25], "big")
            data_offset = int.from_bytes(
                self.data[offset + 25 : offset + 30], "big"
            )
            self.entries.append((block_index, size, data_offset))

        # A full block is represented by zero, so 64 KiB blocks use two bytes.
        width = max(1, (self.block_size.bit_length() + 6) // 8)
        block_table = 32 + entry_count * entry_size
        self.block_lengths = [
            int.from_bytes(self.data[pos : pos + width], "big")
            for pos in range(block_table, toc_size, width)
        ]

        if self.compression != b"oodl":
            raise ValueError(
                f"unsupported PSARC compression {self.compression!r}; expected b'oodl'"
            )
        dll_path = find_oodle(path, oodle_path)
        library = ctypes.WinDLL(str(dll_path))
        self.decompress = library.OodleLZ_Decompress
        self.decompress.restype = ctypes.c_longlong

        manifest = self.extract_index(0).decode("utf-8", "replace")
        self.names = manifest.splitlines()
        if len(self.names) != len(self.entries) - 1:
            raise ValueError(
                "manifest/TOC mismatch: "
                f"{len(self.names)} names for {len(self.entries) - 1} entries"
            )
        self.name_to_index = {name: index + 1 for index, name in enumerate(self.names)}

    def extract_index(self, index: int) -> bytes:
        block_index, total_size, data_offset = self.entries[index]
        output = bytearray()
        remaining = total_size
        position = data_offset

        while remaining:
            wanted = min(self.block_size, remaining)
            compressed_size = self.block_lengths[block_index] or wanted
            source = self.data[position : position + compressed_size]
            if len(source) != compressed_size:
                raise ValueError(f"truncated data block {block_index}")

            if compressed_size == wanted:
                chunk = source
            else:
                source_buffer = ctypes.create_string_buffer(source)
                target_buffer = ctypes.create_string_buffer(wanted)
                produced = self.decompress(
                    source_buffer,
                    compressed_size,
                    target_buffer,
                    wanted,
                    1,
                    0,
                    0,
                    None,
                    0,
                    None,
                    None,
                    None,
                    0,
                    3,
                )
                if produced != wanted:
                    raise RuntimeError(
                        "Oodle decompression failed for block "
                        f"{block_index}: expected {wanted}, got {produced}"
                    )
                chunk = target_buffer.raw[:produced]

            output.extend(chunk)
            position += compressed_size
            remaining -= wanted
            block_index += 1

        return bytes(output)

    def extract(self, name: str) -> bytes:
        try:
            index = self.name_to_index[name]
        except KeyError as exc:
            raise KeyError(f"entry not found: {name}") from exc
        return self.extract_index(index)

    def entry_size(self, name: str) -> int:
        return self.entries[self.name_to_index[name]][1]


def find_all(data: bytes, needle: bytes) -> list[int]:
    offsets: list[int] = []
    start = 0
    while True:
        offset = data.find(needle, start)
        if offset < 0:
            return offsets
        offsets.append(offset)
        start = offset + 1


def format_context(
    data: bytes, offset: int, radius: int, known: dict[int, list[str]]
) -> str:
    aligned = offset & ~3
    start = max(0, aligned - radius * 4)
    end = min(len(data) - 3, aligned + (radius + 1) * 4)
    parts: list[str] = []
    for position in range(start, end, 4):
        value = struct.unpack_from("<I", data, position)[0]
        labels = known.get(value, [])
        label = f" ({'|'.join(labels)})" if labels else ""
        marker = "*" if position == aligned else ""
        delta = position - aligned
        signed_offset = f"+0x{delta:X}" if delta >= 0 else f"-0x{-delta:X}"
        parts.append(f"{marker}{signed_offset}=0x{value:08X}{label}")
    return "  ".join(parts)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="PSARC archive path")
    parser.add_argument("--oodle", type=Path, help="explicit Oodle DLL path")
    parser.add_argument("--list", metavar="REGEX", help="list matching manifest entries")
    parser.add_argument("--entry", help="entry to inspect in memory")
    parser.add_argument(
        "--id",
        action="append",
        default=[],
        metavar="STRING_OR_U32",
        help="scan for a little-endian StringId/u32; repeatable",
    )
    parser.add_argument(
        "--context-dwords",
        type=int,
        default=0,
        help="show this many dwords around each --id hit",
    )
    parser.add_argument(
        "--strings",
        metavar="REGEX",
        help="print unique printable ASCII strings matching REGEX",
    )
    parser.add_argument("--min-string", type=int, default=3)
    parser.add_argument("--limit", type=int, default=200)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    archive = Psarc(args.archive.resolve(), args.oodle.resolve() if args.oodle else None)
    print(
        f"archive={archive.path} version=0x{archive.version:X} "
        f"entries={len(archive.names)} block=0x{archive.block_size:X}"
    )

    if args.list is not None:
        pattern = re.compile(args.list, re.IGNORECASE)
        shown = 0
        for name in archive.names:
            if pattern.search(name):
                print(f"{archive.entry_size(name):>12}  {name}")
                shown += 1
                if shown >= args.limit:
                    break
        print(f"listed={shown} limit={args.limit}")

    if args.entry is None:
        if args.id or args.strings:
            raise ValueError("--entry is required with --id or --strings")
        return 0

    data = archive.extract(args.entry)
    print(f"entry={args.entry} bytes={len(data)}")

    parsed_ids = [parse_id(value) for value in args.id]
    known: dict[int, list[str]] = {}
    for value, label in parsed_ids:
        known.setdefault(value, []).append(label)

    for value, label in parsed_ids:
        offsets = find_all(data, struct.pack("<I", value))
        rendered = ", ".join(f"0x{offset:X}" for offset in offsets[: args.limit])
        print(f"id={label!r} value=0x{value:08X} hits={len(offsets)} offsets=[{rendered}]")
        if args.context_dwords:
            for offset in offsets[: args.limit]:
                print(
                    f"  at 0x{offset:X}: "
                    + format_context(data, offset, args.context_dwords, known)
                )

    if args.strings is not None:
        pattern = re.compile(args.strings)
        printable = re.compile(rb"[ -~]{%d,}" % max(1, args.min_string))
        matches: dict[str, int] = {}
        for match in printable.finditer(data):
            text = match.group().decode("ascii")
            if pattern.search(text):
                matches.setdefault(text, match.start())
        for index, (text, offset) in enumerate(sorted(matches.items())):
            if index >= args.limit:
                break
            print(f"0x{offset:X}  0x{string_id(text.encode('ascii')):08X}  {text}")
        print(f"matching_strings={len(matches)} limit={args.limit}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, KeyError, RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
