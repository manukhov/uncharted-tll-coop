#!/usr/bin/env python3
"""Read-only PSARC, StringId, DC00 overlay, and printable-string inspector.

The Lost Legacy's gameplay DC and animation archives use Oodle-compressed PSARC
blocks.  This tool keeps selected entries in memory; it never extracts assets to
the workspace.  It is intentionally small and analysis-oriented rather than a
general-purpose archive unpacker.
"""

from __future__ import annotations

import argparse
from collections import deque
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


def audit_pak_animation_masks(data: bytes, labels: list[str]) -> None:
    """Resolve animation registrations in a paged PAK and print joint masks.

    The animation PAK uses page-relative pointers.  Its fixup table records the
    source page, target page, and source-local pointer location for every such
    pointer.  Animation registrations live in page zero as
    ``{StringId, 0, slot_offset}``; the slot resolves to a resource descriptor,
    whose pointer at +0x28 resolves to the raw ANIM object.
    """

    if len(data) < 0x2C:
        raise ValueError("--animation-mask requires a paged animation PAK")

    page_count = struct.unpack_from("<I", data, 0x10)[0]
    page_table_offset = struct.unpack_from("<I", data, 0x14)[0]
    if not page_count or page_count > 0x10000:
        raise ValueError(f"invalid PAK page count: {page_count}")
    if page_table_offset + page_count * 0xC > len(data):
        raise ValueError("truncated PAK page table")

    pages: list[tuple[int, int]] = []
    for index in range(page_count):
        file_offset, size, _flags = struct.unpack_from(
            "<III", data, page_table_offset + index * 0xC
        )
        if file_offset + size > len(data):
            raise ValueError(f"PAK page {index} extends beyond the entry")
        pages.append((file_offset, size))

    metadata_end = pages[0][0]
    if metadata_end < page_table_offset + page_count * 0xC:
        raise ValueError("PAK page zero overlaps its metadata")

    fixups: dict[tuple[int, int], list[int]] = {}
    for offset in range(0, metadata_end - 7, 8):
        source_page, target_page, pointer_location = struct.unpack_from(
            "<HHI", data, offset
        )
        if source_page >= page_count or target_page >= page_count:
            continue
        if pointer_location + 8 > pages[source_page][1]:
            continue
        fixups.setdefault((source_page, pointer_location), []).append(target_page)

    def resolve_pointer(source_page: int, pointer_location: int) -> tuple[int, int]:
        targets = fixups.get((source_page, pointer_location), [])
        unique_targets = sorted(set(targets))
        if len(unique_targets) != 1:
            raise ValueError(
                "expected one PAK pointer fixup for "
                f"page {source_page} +0x{pointer_location:X}; "
                f"found {unique_targets}"
            )
        target_page = unique_targets[0]
        source_offset = pages[source_page][0] + pointer_location
        target_local = struct.unpack_from("<Q", data, source_offset)[0]
        if target_local >= pages[target_page][1]:
            raise ValueError(
                f"PAK pointer target page {target_page} +0x{target_local:X} "
                "is out of range"
            )
        return target_page, target_local

    def read_c_string(page: int, local: int) -> bytes | None:
        absolute = pages[page][0] + local
        end = data.find(b"\0", absolute, min(len(data), absolute + 0x400))
        if end < 0:
            return None
        value = data[absolute:end]
        if not value or any(byte < 0x20 or byte >= 0x7F for byte in value):
            return None
        return value

    page_zero_offset, page_zero_size = pages[0]
    print(
        f"pak_pages={page_count} page_table=0x{page_table_offset:X} "
        f"fixups={sum(len(values) for values in fixups.values())}"
    )
    for label in labels:
        animation_id, _original = parse_id(label)
        registration_signature = struct.pack("<II", animation_id, 0)
        registrations: list[tuple[int, int]] = []
        for hit in find_all(
            data[page_zero_offset : page_zero_offset + page_zero_size],
            registration_signature,
        ):
            if hit & 7 or hit + 16 > page_zero_size:
                continue
            slot_offset = struct.unpack_from("<Q", data, page_zero_offset + hit + 8)[0]
            if slot_offset > 0xFFFFFFFF or (0, slot_offset) not in fixups:
                continue
            registrations.append((hit, int(slot_offset)))

        descriptors: list[tuple[int, int, str]] = []
        for registration_offset, slot_offset in registrations:
            descriptor_page, descriptor_local = resolve_pointer(0, slot_offset)
            descriptors.append(
                (
                    descriptor_page,
                    descriptor_local,
                    f"page0+0x{registration_offset:X}/slot=0x{slot_offset:X}",
                )
            )

        # NPC-oriented PAKs keep resource names in pointer-linked descriptors
        # instead of duplicating their StringIds in page zero.  Resolve the
        # same descriptor by its authored name and type before following +0x28.
        if not descriptors:
            seen_descriptors: set[tuple[int, int]] = set()
            for source_page, source_local in fixups:
                try:
                    descriptor_page, descriptor_local = resolve_pointer(
                        source_page, source_local
                    )
                except ValueError:
                    continue
                if (
                    (descriptor_page, descriptor_local) in seen_descriptors
                    or (descriptor_page, descriptor_local + 0x28) not in fixups
                    or descriptor_local + 0x30 > pages[descriptor_page][1]
                ):
                    continue
                seen_descriptors.add((descriptor_page, descriptor_local))
                descriptor_offset = pages[descriptor_page][0] + descriptor_local
                name_local, type_local = struct.unpack_from(
                    "<QQ", data, descriptor_offset
                )
                if (
                    name_local >= pages[descriptor_page][1]
                    or type_local >= pages[descriptor_page][1]
                ):
                    continue
                name = read_c_string(descriptor_page, name_local)
                resource_type = read_c_string(descriptor_page, type_local)
                if (
                    name is None
                    or resource_type != b"ANIM"
                    or string_id(name) != animation_id
                ):
                    continue
                descriptors.append(
                    (
                        descriptor_page,
                        descriptor_local,
                        f"named-descriptor/{name.decode('ascii')}",
                    )
                )

        if not descriptors:
            print(
                f"animation={label!r} id=0x{animation_id:08X} "
                "registrations=0 named_descriptors=0"
            )
            continue

        for descriptor_page, descriptor_local, origin in descriptors:
            if descriptor_local + 0x30 > pages[descriptor_page][1]:
                raise ValueError(
                    f"truncated animation descriptor for 0x{animation_id:08X}"
                )
            raw_page, raw_local = resolve_pointer(
                descriptor_page, descriptor_local + 0x28
            )
            raw_offset = pages[raw_page][0] + raw_local
            if raw_offset + 0x20 > len(data) or data[raw_offset : raw_offset + 4] != b"\x02\x01CA":
                raise ValueError(
                    f"0x{animation_id:08X} resolved to a non-ANIM object "
                    f"at 0x{raw_offset:X}"
                )

            source_skeleton_id = struct.unpack_from("<I", data, raw_offset + 4)[0]
            group_count = struct.unpack_from("<H", data, raw_offset + 0xC)[0]
            group_table_offset = struct.unpack_from("<I", data, raw_offset + 0x14)[0]
            mask_offset = raw_offset + group_table_offset + group_count * 0x10
            if group_count == 0 or mask_offset + 16 > len(data):
                raise ValueError(
                    f"invalid ANIM group table for 0x{animation_id:08X}"
                )
            mask_words = struct.unpack_from("<QQ", data, mask_offset)
            joints = [
                joint
                for joint in range(128)
                if mask_words[joint // 64] & (1 << (63 - joint % 64))
            ]
            rendered_joints = ",".join(str(joint) for joint in joints)
            print(
                f"animation={label!r} id=0x{animation_id:08X} "
                f"origin={origin} "
                f"descriptor=page{descriptor_page}+0x{descriptor_local:X} "
                f"raw=0x{raw_offset:X} source_skeleton=0x{source_skeleton_id:08X} "
                f"groups={group_count} "
                f"mask={mask_words[0]:016X}:{mask_words[1]:016X} "
                f"joints=[{rendered_joints}]"
            )


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


def inspect_player_mesh_dc00(
    data: bytes, look_labels: list[str], source_labels: list[str]
) -> None:
    """Print selected player-mesh anim-overlay-set objects from a DC00 entry."""

    if len(data) < 0x20 or data[:4] != b"00CD":
        raise ValueError("--player-mesh-look requires a DC00 entry")

    object_count = struct.unpack_from("<I", data, 0x14)[0]
    index_end = 0x20 + object_count * 0x10
    if index_end > len(data):
        raise ValueError("truncated DC00 object index")

    wanted_looks = [parse_id(label) for label in look_labels]
    wanted_sources = [parse_id(label) for label in source_labels]
    wanted_look_ids = {value for value, _ in wanted_looks}
    player_mesh_id = string_id(b"player-mesh")
    player_id = string_id(b"player")
    overlay_set_id = string_id(b"anim-overlay-set")
    shown = 0
    found_look_ids: set[int] = set()

    print(
        f"dc00_objects={object_count} "
        f"player_mesh_id=0x{player_mesh_id:08X} "
        f"overlay_set_id=0x{overlay_set_id:08X}"
    )
    for index in range(object_count):
        index_offset = 0x20 + index * 0x10
        object_id, type_id, record_offset, reserved = struct.unpack_from(
            "<IIII", data, index_offset
        )
        if record_offset + 0x30 > len(data):
            raise ValueError(f"DC00 object {index} has an invalid record offset")

        table_offset = struct.unpack_from("<Q", data, record_offset)[0]
        table_count, object_kind, record_object_id, owner_id, look_id = (
            struct.unpack_from("<IIIII", data, record_offset + 8)
        )
        aux_offset = struct.unpack_from("<Q", data, record_offset + 0x20)[0]
        record_type = struct.unpack_from("<I", data, record_offset + 0x2C)[0]
        if (
            type_id != overlay_set_id
            or record_type != overlay_set_id
            or object_kind != player_mesh_id
            or owner_id != player_id
        ):
            continue
        if wanted_look_ids and look_id not in wanted_look_ids:
            continue

        labels = [label for value, label in wanted_looks if value == look_id]
        label_text = "|".join(labels) if labels else "unlabelled"
        print(
            f"object[{index}] id=0x{object_id:08X} look=0x{look_id:08X} "
            f"({label_text}) record=0x{record_offset:X} "
            f"table=0x{table_offset:X} count={table_count} aux=0x{aux_offset:X} "
            f"echo=0x{record_object_id:08X} reserved=0x{reserved:08X}"
        )
        shown += 1
        found_look_ids.add(look_id)

        table_end = table_offset + table_count * 0x28
        if table_offset > len(data) or table_end > len(data):
            raise ValueError(f"DC00 object {index} has an invalid overlay table")
        for source_id, source_label in wanted_sources:
            hits = 0
            for row in range(table_count):
                row_offset = table_offset + row * 0x28
                row_source, row_target = struct.unpack_from("<II", data, row_offset)
                if row_source == source_id:
                    print(
                        f"  map source={source_label!r}/0x{source_id:08X} "
                        f"row={row} offset=0x{row_offset:X} "
                        f"target=0x{row_target:08X}"
                    )
                    hits += 1
            if hits == 0:
                print(f"  map source={source_label!r}/0x{source_id:08X} hits=0")

    for look_id, look_label in wanted_looks:
        if look_id not in found_look_ids:
            print(f"look={look_label!r}/0x{look_id:08X} objects=0")
    print(f"dc00_player_mesh_objects_shown={shown}")


def audit_dc00_objects(
    data: bytes, record_labels: list[str], table_offsets: list[str], limit: int
) -> None:
    """Locate DC00 objects by record ids or by a contained 0x28-row offset."""

    if len(data) < 0x20 or data[:4] != b"00CD":
        raise ValueError("--dc00-record-id/--dc00-table-offset requires DC00")

    object_count = struct.unpack_from("<I", data, 0x14)[0]
    index_end = 0x20 + object_count * 0x10
    if index_end > len(data):
        raise ValueError("truncated DC00 object index")

    wanted_ids = [parse_id(label) for label in record_labels]
    wanted_offsets = [int(value, 0) for value in table_offsets]
    shown = 0
    print(
        f"dc00_object_count={object_count} index_end=0x{index_end:X} "
        f"record_ids={','.join(f'0x{value:08X}' for value, _ in wanted_ids)} "
        f"table_offsets={','.join(f'0x{value:X}' for value in wanted_offsets)}"
    )

    for index in range(object_count):
        index_offset = 0x20 + index * 0x10
        object_id, type_id, record_offset, reserved = struct.unpack_from(
            "<IIII", data, index_offset
        )
        if record_offset + 0x30 > len(data):
            continue
        record = data[record_offset : record_offset + 0x30]
        record_values = struct.unpack_from("<12I", record)
        record_matches = all(
            struct.pack("<I", value) in record for value, _ in wanted_ids
        )

        table_offset = struct.unpack_from("<Q", record, 0)[0]
        table_count = struct.unpack_from("<I", record, 8)[0]
        table_end = table_offset + table_count * 0x28
        offset_matches = any(
            table_offset <= wanted < table_end and (wanted - table_offset) % 0x28 < 8
            for wanted in wanted_offsets
        )
        if (wanted_ids and not record_matches) or (wanted_offsets and not offset_matches):
            continue
        if not wanted_ids and not wanted_offsets:
            continue

        values = ",".join(f"{value:08X}" for value in record_values)
        print(
            f"object[{index}] index=0x{index_offset:X} id=0x{object_id:08X} "
            f"type=0x{type_id:08X} record=0x{record_offset:X} "
            f"reserved=0x{reserved:08X} table=0x{table_offset:X} "
            f"count={table_count} record_dwords={values}"
        )
        for wanted in wanted_offsets:
            if table_offset <= wanted < table_end:
                row = (wanted - table_offset) // 0x28
                row_offset = table_offset + row * 0x28
                source_id, target_id = struct.unpack_from("<II", data, row_offset)
                print(
                    f"  contains=0x{wanted:X} row={row} row_offset=0x{row_offset:X} "
                    f"source=0x{source_id:08X} target=0x{target_id:08X}"
                )
        shown += 1
        if shown >= limit:
            break
    print(f"dc00_objects_shown={shown} limit={limit}")


def audit_dc00_overlay_rows(data: bytes, map_labels: list[str], limit: int) -> None:
    """Find source/target occurrences across every well-formed overlay-set table."""

    if len(data) < 0x20 or data[:4] != b"00CD":
        raise ValueError("--dc00-map-id requires DC00")
    wanted = {value for value, _ in map(parse_id, map_labels)}
    overlay_set_id = string_id(b"anim-overlay-set")
    object_count = struct.unpack_from("<I", data, 0x14)[0]
    shown = 0

    for index in range(object_count):
        index_offset = 0x20 + index * 0x10
        object_id, type_id, record_offset, _reserved = struct.unpack_from(
            "<IIII", data, index_offset
        )
        if type_id != overlay_set_id or record_offset + 0x30 > len(data):
            continue
        table_offset = struct.unpack_from("<Q", data, record_offset)[0]
        table_count, category_id, _echo_id, owner_id, selector_id = (
            struct.unpack_from("<IIIII", data, record_offset + 8)
        )
        record_type = struct.unpack_from("<I", data, record_offset + 0x2C)[0]
        table_end = table_offset + table_count * 0x28
        if (
            record_type != overlay_set_id
            or table_offset > len(data)
            or table_end > len(data)
        ):
            continue
        for row in range(table_count):
            row_offset = table_offset + row * 0x28
            source_id, target_id = struct.unpack_from("<II", data, row_offset)
            if source_id not in wanted and target_id not in wanted:
                continue
            print(
                f"object[{index}] id=0x{object_id:08X} record=0x{record_offset:X} "
                f"category=0x{category_id:08X} owner=0x{owner_id:08X} "
                f"selector=0x{selector_id:08X} table=0x{table_offset:X} "
                f"count={table_count} row={row} offset=0x{row_offset:X} "
                f"source=0x{source_id:08X} target=0x{target_id:08X}"
            )
            shown += 1
            if shown >= limit:
                print(f"dc00_overlay_rows_shown={shown} limit={limit}")
                return
    print(f"dc00_overlay_rows_shown={shown} limit={limit}")


def collect_player_mesh_rows(
    data: bytes, look_label: str
) -> list[tuple[int, int, int, bytes]]:
    """Return (source, target, file offset, remaining row bytes) for one look."""

    if len(data) < 0x20 or data[:4] != b"00CD":
        raise ValueError("player-mesh comparison requires a DC00 entry")
    look_id, _ = parse_id(look_label)
    player_mesh_id = string_id(b"player-mesh")
    player_id = string_id(b"player")
    overlay_set_id = string_id(b"anim-overlay-set")
    object_count = struct.unpack_from("<I", data, 0x14)[0]
    rows: list[tuple[int, int, int, bytes]] = []

    for index in range(object_count):
        index_offset = 0x20 + index * 0x10
        _object_id, type_id, record_offset, _reserved = struct.unpack_from(
            "<IIII", data, index_offset
        )
        if record_offset + 0x30 > len(data):
            raise ValueError(f"DC00 object {index} has an invalid record offset")
        table_offset = struct.unpack_from("<Q", data, record_offset)[0]
        table_count, object_kind, _record_object_id, owner_id, record_look_id = (
            struct.unpack_from("<IIIII", data, record_offset + 8)
        )
        record_type = struct.unpack_from("<I", data, record_offset + 0x2C)[0]
        if (
            type_id != overlay_set_id
            or record_type != overlay_set_id
            or object_kind != player_mesh_id
            or owner_id != player_id
            or record_look_id != look_id
        ):
            continue
        table_end = table_offset + table_count * 0x28
        if table_offset > len(data) or table_end > len(data):
            raise ValueError(f"DC00 object {index} has an invalid overlay table")
        for row in range(table_count):
            row_offset = table_offset + row * 0x28
            source_id, target_id = struct.unpack_from("<II", data, row_offset)
            rows.append(
                (source_id, target_id, row_offset, data[row_offset + 8 : row_offset + 0x28])
            )
    return rows


def collect_printable_string_ids(
    data: bytes, min_length: int
) -> dict[int, list[str]]:
    """Build a StringId-to-text index from NUL-separated printable asset names."""

    printable = re.compile(rb"[ -~]{%d,}" % max(1, min_length))
    names: dict[int, list[str]] = {}
    for match in printable.finditer(data):
        raw = match.group()
        text = raw.decode("ascii")
        names.setdefault(string_id(raw), []).append(text)
    return names


def audit_skeleton_joint_tables(
    data: bytes, specifications: list[str], min_string: int
) -> None:
    """Print stride-16 skeleton joint hashes from explicit OFFSET:COUNT tables."""

    known_names = collect_printable_string_ids(data, min_string)
    for specification in specifications:
        try:
            offset_text, count_text = specification.split(":", 1)
            table_offset = int(offset_text, 0)
            joint_count = int(count_text, 0)
        except (ValueError, TypeError) as exc:
            raise ValueError(
                "--skeleton-joint-table expects OFFSET:COUNT"
            ) from exc
        if table_offset < 0 or joint_count <= 0 or joint_count > 4096:
            raise ValueError(f"invalid skeleton joint table: {specification}")
        table_end = table_offset + joint_count * 0x10
        if table_end > len(data):
            raise ValueError(
                f"skeleton joint table exceeds entry: {specification}"
            )
        print(
            f"skeleton_joint_table=0x{table_offset:X} count={joint_count} "
            "stride=0x10"
        )
        for joint in range(joint_count):
            row_offset = table_offset + joint * 0x10
            joint_id = struct.unpack_from("<I", data, row_offset)[0]
            names = "|".join(known_names.get(joint_id, []))
            print(
                f"  joint={joint} offset=0x{row_offset:X} "
                f"id=0x{joint_id:08X} names={names!r}"
            )


def audit_skeleton_retargets(data: bytes, specifications: list[str]) -> None:
    """Find DC00 retarget records for explicit SOURCE_ART:TARGET_ART pairs."""

    if len(data) < 0x20 or data[:4] != b"00CD":
        raise ValueError("--skeleton-retarget requires a DC00 entry")
    object_count = struct.unpack_from("<I", data, 0x14)[0]
    wanted: list[tuple[int, int, str]] = []
    for specification in specifications:
        try:
            source_text, target_text = specification.split(":", 1)
        except ValueError as exc:
            raise ValueError(
                "--skeleton-retarget expects SOURCE_ART:TARGET_ART"
            ) from exc
        source_id = parse_id(source_text)[0]
        target_id = parse_id(target_text)[0]
        wanted.append((source_id, target_id, specification))

    for source_id, target_id, specification in wanted:
        matches = 0
        for index in range(object_count):
            index_offset = 0x20 + index * 0x10
            object_id, type_id, record_offset, _reserved = struct.unpack_from(
                "<IIII", data, index_offset
            )
            if record_offset + 0x10 > len(data):
                continue
            table_offset = struct.unpack_from("<Q", data, record_offset)[0]
            table_count = struct.unpack_from("<I", data, record_offset + 8)[0]
            table_end = table_offset + table_count * 0x30
            if (
                table_count > 4096
                or table_offset > len(data)
                or table_end > len(data)
            ):
                continue
            for table_row in range(table_count):
                retarget_offset = table_offset + table_row * 0x30
                values = struct.unpack_from("<12I", data, retarget_offset)
                if values[3] != source_id or values[4] != target_id:
                    continue
                map_offset = values[6]
                map_count = values[8]
                map_end = map_offset + map_count * 0x0C
                if (
                    map_count > 4096
                    or map_offset > len(data)
                    or map_end > len(data)
                ):
                    raise ValueError(
                        f"invalid retarget map in object {index} row "
                        f"{table_row}: offset=0x{map_offset:X} "
                        f"count={map_count}"
                    )
                print(
                    f"skeleton_retarget={specification!r} object[{index}] "
                    f"index=0x{index_offset:X} id=0x{object_id:08X} "
                    f"type=0x{type_id:08X} record=0x{record_offset:X} "
                    f"table=0x{table_offset:X} row={table_row} "
                    f"row_offset=0x{retarget_offset:X} "
                    f"map=0x{map_offset:X} count={map_count} "
                    f"profile=0x{values[9]:08X} "
                    f"scale_bits=0x{values[10]:08X} "
                    f"scale={struct.unpack('<f', struct.pack('<I', values[10]))[0]:.9g}"
                )
                for map_row in range(map_count):
                    row_offset = map_offset + map_row * 0x0C
                    source_joint, target_joint, mode = struct.unpack_from(
                        "<III", data, row_offset
                    )
                    print(
                        f"  row={map_row} offset=0x{row_offset:X} "
                        f"source=0x{source_joint:08X} "
                        f"target=0x{target_joint:08X} mode={mode}"
                    )
                matches += 1
        print(
            f"skeleton_retarget_matches={matches} "
            f"source=0x{source_id:08X} target=0x{target_id:08X}"
        )


def compare_player_mesh_rows(
    data: bytes,
    base_look: str,
    compare_looks: list[str],
    known_names: dict[int, list[str]],
    name_regex: str | None,
    limit: int,
) -> None:
    """Compare one player-mesh table with control looks by exact source id."""

    base_rows = collect_player_mesh_rows(data, base_look)
    controls = {
        label: collect_player_mesh_rows(data, label) for label in compare_looks
    }
    control_by_source: dict[str, dict[int, list[tuple[int, int, bytes]]]] = {}
    for label, rows in controls.items():
        by_source: dict[int, list[tuple[int, int, bytes]]] = {}
        for source_id, target_id, offset, tail in rows:
            by_source.setdefault(source_id, []).append((target_id, offset, tail))
        control_by_source[label] = by_source

    pattern = re.compile(name_regex) if name_regex is not None else None
    shown = 0
    matched = 0
    control_stats = {
        label: {
            "source_hits": 0,
            "source_misses": 0,
            "same_target": 0,
            "different_target": 0,
            "same_tail": 0,
            "different_tail": 0,
        }
        for label in compare_looks
    }
    for source_id, target_id, offset, tail in base_rows:
        source_name = "|".join(known_names.get(source_id, []))
        if pattern is not None and not pattern.search(source_name):
            continue
        matched += 1
        for label in compare_looks:
            hits = control_by_source[label].get(source_id, [])
            stats = control_stats[label]
            if not hits:
                stats["source_misses"] += 1
                continue
            stats["source_hits"] += 1
            if any(control_target == target_id for control_target, _, _ in hits):
                stats["same_target"] += 1
            else:
                stats["different_target"] += 1
            if any(control_tail == tail for _, _, control_tail in hits):
                stats["same_tail"] += 1
            else:
                stats["different_tail"] += 1
        if shown >= limit:
            continue
        target_name = "|".join(known_names.get(target_id, []))
        print(
            f"compare base={base_look!r} source=0x{source_id:08X} "
            f"source_name={source_name!r} target=0x{target_id:08X} "
            f"target_name={target_name!r} offset=0x{offset:X}"
        )
        for label in compare_looks:
            hits = control_by_source[label].get(source_id, [])
            if not hits:
                print(f"  control={label!r} hits=0")
                continue
            for control_target, control_offset, control_tail in hits:
                control_name = "|".join(known_names.get(control_target, []))
                print(
                    f"  control={label!r} target=0x{control_target:08X} "
                    f"target_name={control_name!r} offset=0x{control_offset:X} "
                    f"same_tail={int(control_tail == tail)}"
                )
        shown += 1
    print(
        f"compared_base_rows={len(base_rows)} matched={matched} "
        f"shown={shown} limit={limit}"
    )
    for label in compare_looks:
        stats = control_stats[label]
        print(
            f"control_summary={label!r} "
            f"source_hits={stats['source_hits']} "
            f"source_misses={stats['source_misses']} "
            f"same_target={stats['same_target']} "
            f"different_target={stats['different_target']} "
            f"same_tail={stats['same_tail']} "
            f"different_tail={stats['different_tail']}"
        )


def collect_gesture_target_types(data: bytes) -> dict[int, int]:
    """Return authored gesture object id -> target-provider type from DC00."""

    if len(data) < 0x20 or data[:4] != b"00CD":
        raise ValueError("gesture settings audit requires a DC00 entry")
    object_count = struct.unpack_from("<I", data, 0x14)[0]
    index_end = 0x20 + object_count * 0x10
    if index_end > len(data):
        raise ValueError("truncated gesture DC00 object index")

    gesture_type_id = string_id(b"gesture")
    targets: dict[int, int] = {}
    for index in range(object_count):
        index_offset = 0x20 + index * 0x10
        object_id, type_id, record_offset, _reserved = struct.unpack_from(
            "<IIII", data, index_offset
        )
        if type_id != gesture_type_id:
            continue
        if record_offset + 0x18 > len(data):
            raise ValueError(
                f"gesture DC00 object {index} has an invalid record offset"
            )
        targets[object_id] = struct.unpack_from("<I", data, record_offset + 0x14)[0]
    return targets


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def qword_references(data: bytes, target: int) -> list[int]:
    return find_all(data, struct.pack("<Q", target))


def read_c_string(data: bytes, offset: int, limit: int = 128) -> str | None:
    if not 0 < offset < len(data):
        return None
    end = data.find(b"\0", offset, min(len(data), offset + limit))
    if end < 0 or end == offset:
        return None
    raw = data[offset:end]
    if any(byte < 0x20 or byte > 0x7E for byte in raw):
        return None
    return raw.decode("ascii")


def state_record_for_graph_reference(
    data: bytes, reference_offset: int, graph_offset: int
) -> tuple[int, str, int, int] | None:
    record_offset = reference_offset - 0x18
    if record_offset < 0 or record_offset + 0x28 > len(data):
        return None
    if read_u64(data, record_offset + 0x18) != graph_offset:
        return None
    name_offset = read_u64(data, record_offset)
    name = read_c_string(data, name_offset)
    if name is None:
        return None
    state_id = read_u32(data, record_offset + 0x8)
    if string_id(name.encode("ascii")) != state_id:
        return None
    animation_id = read_u32(data, record_offset + 0x20)
    return record_offset, name, state_id, animation_id


def containing_typed_descriptor(data: bytes, reference_offset: int) -> int | None:
    """Find a nearby descriptor start marked by the duplicated type-id prefix."""

    candidates: list[int] = []
    start = max(4, reference_offset - 0x40)
    for descriptor_offset in range(start, reference_offset + 1, 4):
        if descriptor_offset + 4 > len(data):
            continue
        if read_u32(data, descriptor_offset) == 0:
            continue
        if data[descriptor_offset - 4 : descriptor_offset] != data[
            descriptor_offset : descriptor_offset + 4
        ]:
            continue
        field_offset = reference_offset - descriptor_offset
        if 0x8 <= field_offset <= 0x40 and field_offset % 8 == 0:
            candidates.append(descriptor_offset)
    return max(candidates) if candidates else None


def trace_unique_state_owner(
    data: bytes, graph_offset: int, max_steps: int = 8
) -> tuple[list[tuple[int, int]], tuple[int, str, int, int] | None]:
    """Walk unique reverse qword references until an animation-state record."""

    chain: list[tuple[int, int]] = []
    target = graph_offset
    for _ in range(max_steps):
        references = qword_references(data, target)
        if len(references) != 1:
            break
        reference = references[0]
        chain.append((target, reference))
        state = state_record_for_graph_reference(data, reference, target)
        if state is not None:
            return chain, state
        descriptor = containing_typed_descriptor(data, reference)
        target = descriptor if descriptor is not None else reference
    return chain, None


def trace_state_owners(
    data: bytes,
    graph_offset: int,
    max_depth: int = 12,
    max_nodes: int = 4096,
) -> tuple[list[tuple[int, str, int, int]], int, bool]:
    """Reverse-walk all qword parents and return validated state records."""

    queue = deque([(graph_offset, 0)])
    seen = {graph_offset}
    states: set[tuple[int, str, int, int]] = set()
    complete = True
    while queue:
        target, depth = queue.popleft()
        if depth >= max_depth:
            complete = False
            continue
        for reference in qword_references(data, target):
            state = state_record_for_graph_reference(data, reference, target)
            if state is not None:
                states.add(state)
                continue
            descriptor = containing_typed_descriptor(data, reference)
            parent = descriptor if descriptor is not None else reference
            if parent in seen:
                continue
            seen.add(parent)
            if len(seen) > max_nodes:
                complete = False
                queue.clear()
                break
            queue.append((parent, depth + 1))
    return sorted(states), len(seen), complete


def format_linear_expression(expression: tuple[str | None, int]) -> str:
    base, offset = expression
    if base is None:
        return f"0x{offset:X}"
    if offset == 0:
        return base
    sign = "+" if offset >= 0 else "-"
    return f"{base} {sign} 0x{abs(offset):X}"


def decode_linear_script_callback(
    data: bytes, descriptor_offset: int
) -> tuple[int, int, int, list[tuple[int, int, int, int, int]], str]:
    """Decode the straight-line subset used by authored gesture callbacks."""

    if descriptor_offset + 0x1C > len(data):
        raise ValueError(f"truncated script descriptor at 0x{descriptor_offset:X}")
    code_offset, pool_offset = struct.unpack_from("<QQ", data, descriptor_offset)
    descriptor_type, _size, script_id = struct.unpack_from(
        "<III", data, descriptor_offset + 0x10
    )
    if descriptor_type != string_id(b"function"):
        raise ValueError(
            f"bad script descriptor type at 0x{descriptor_offset:X}: "
            f"0x{descriptor_type:08X}"
        )
    if not 0 < code_offset < len(data):
        raise ValueError(f"bad script code offset 0x{code_offset:X}")

    registers: dict[int, tuple[str | None, int]] = {
        0x31 + index: (f"arg{index}", 0) for index in range(16)
    }
    instructions: list[tuple[int, int, int, int, int]] = []
    result = "unknown"
    for instruction_offset in range(code_offset, min(len(data), code_offset + 0x400), 4):
        opcode, destination, source_a, source_b = data[
            instruction_offset : instruction_offset + 4
        ]
        instructions.append(
            (instruction_offset, opcode, destination, source_a, source_b)
        )
        if opcode == 0x00:
            result = format_linear_expression(
                registers.get(source_a, (f"r{source_a}", 0))
            )
            break
        if opcode == 0x43:
            registers[destination] = registers.get(source_a, (f"r{source_a}", 0))
        elif opcode == 0x3B:
            base, offset = registers.get(source_a, (f"r{source_a}", 0))
            registers[destination] = (base, offset + source_b)
        elif opcode == 0x0C:
            registers[destination] = (None, source_a | (source_b << 8))
        elif opcode == 0x01:
            left = registers.get(source_a, (f"r{source_a}", 0))
            right = registers.get(source_b, (f"r{source_b}", 0))
            if left[0] is None:
                registers[destination] = (right[0], right[1] + left[1])
            elif right[0] is None:
                registers[destination] = (left[0], left[1] + right[1])
            else:
                registers[destination] = (
                    f"({format_linear_expression(left)}) + "
                    f"({format_linear_expression(right)})",
                    0,
                )
        elif opcode in (0x0D, 0x0E, 0x0F):
            width = {0x0D: "u32", 0x0E: "i32", 0x0F: "u64"}[opcode]
            address = format_linear_expression(
                registers.get(source_a, (f"r{source_a}", 0))
            )
            registers[destination] = (f"*({width} *)({address})", 0)
        else:
            # Keep decoding for a raw instruction listing, but stop symbolic
            # propagation through operations outside this deliberately small set.
            registers.pop(destination, None)
    return code_offset, pool_offset, script_id, instructions, result


def audit_script_member_accesses(data: bytes, base_offset: int) -> None:
    """Summarize direct VM callbacks that materialize one structure base."""

    script_lambda_id = string_id(b"script-lambda")
    function_id = string_id(b"function")
    store_opcodes = {0x10, 0x11, 0x12, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57}
    rows: list[tuple[int, int, int, int]] = []
    for signature_offset in find_all(data, struct.pack("<I", script_lambda_id)):
        descriptor_offset = signature_offset + 4
        if descriptor_offset + 0x1C > len(data):
            continue
        code_offset, pool_offset = struct.unpack_from("<QQ", data, descriptor_offset)
        descriptor_type, _size, script_id = struct.unpack_from(
            "<III", data, descriptor_offset + 0x10
        )
        if (
            descriptor_type != function_id
            or not 0 < code_offset < pool_offset <= len(data)
            or (pool_offset - code_offset) % 4
        ):
            continue
        instructions = [
            tuple(data[offset : offset + 4])
            for offset in range(code_offset, pool_offset, 4)
        ]
        materializes_base = any(
            opcode == 0x0C and (source_a | (source_b << 8)) == base_offset
            for opcode, _destination, source_a, source_b in instructions
        )
        if not materializes_base:
            continue
        store_count = sum(
            opcode in store_opcodes
            for opcode, _destination, _source_a, _source_b in instructions
        )
        rows.append((descriptor_offset, code_offset, script_id, store_count))
    rendered = ",".join(
        f"0x{descriptor:X}/0x{code:X}/0x{script_id:08X}/stores={stores}"
        for descriptor, code, script_id, stores in rows
    )
    print(
        f"vm_member_base=0x{base_offset:X} callbacks={len(rows)} "
        f"direct_store_opcodes={sum(row[3] for row in rows)} rows=[{rendered}]"
    )


def audit_dynamic_gesture_owner(
    data: bytes, descriptor_offset: int, target_callback: int
) -> None:
    blend_type_id = string_id(b"anim-node-blend")
    animation_type_id = string_id(b"anim-node-animation")
    parents: list[tuple[int, int]] = []
    for reference in qword_references(data, descriptor_offset):
        for child_offset, child_index in ((0x8, 0), (0x10, 1)):
            parent = reference - child_offset
            if parent < 0 or parent + 0x30 > len(data):
                continue
            if read_u32(data, parent) != blend_type_id:
                continue
            if read_u64(data, parent + child_offset) == descriptor_offset:
                parents.append((parent, child_index))

    print(
        f"dynamic_node=0x{descriptor_offset:X} "
        f"blend_parents={len(parents)}"
    )
    for parent, child_index in parents:
        child0 = read_u64(data, parent + 0x8)
        child1 = read_u64(data, parent + 0x10)
        callback = read_u64(data, parent + 0x20)
        weight_bits = read_u32(data, parent + 0x28)
        weight = struct.unpack("<f", struct.pack("<I", weight_bits))[0]
        fallback_id = 0
        if child0 + 8 <= len(data) and read_u32(data, child0) == animation_type_id:
            fallback_id = read_u32(data, child0 + 0x4)
        print(
            f"dynamic_parent=0x{parent:X} child_index={child_index} "
            f"child0=0x{child0:X} child1=0x{child1:X} "
            f"blend_callback=0x{callback:X} authored_weight={weight:g} "
            f"fallback_animation=0x{fallback_id:08X}"
        )
        chain, state = trace_unique_state_owner(data, parent)
        rendered_chain = " -> ".join(
            f"0x{target:X}@0x{reference:X}" for target, reference in chain
        )
        print(f"dynamic_owner_chain={rendered_chain or 'none'}")
        if state is not None:
            state_offset, name, state_id, animation_id = state
            print(
                f"dynamic_owner_state=0x{state_offset:X} name={name!r} "
                f"id=0x{state_id:08X} animation=0x{animation_id:08X} "
                f"fallback_matches={int(animation_id == fallback_id)}"
            )

    selector_callback = read_u64(data, descriptor_offset + 0x8)
    for role, callback in (("selector", selector_callback), ("target", target_callback)):
        if not callback:
            continue
        code, pool, script_id, instructions, result = decode_linear_script_callback(
            data, callback
        )
        rendered = " ".join(
            f"{opcode:02X}:{destination:02X}:{source_a:02X}:{source_b:02X}"
            for _offset, opcode, destination, source_a, source_b in instructions
        )
        print(
            f"dynamic_{role}_callback=0x{callback:X} code=0x{code:X} "
            f"pool=0x{pool:X} script=0x{script_id:08X} "
            f"result={result} instructions=[{rendered}]"
        )


def audit_anim_node_gestures(anim_data: bytes, gesture_data: bytes) -> None:
    """Cross-reference Player AnimNodeGesture descriptors with gesture settings."""

    node_type_id = string_id(b"anim-node-gesture")
    signature = struct.pack("<II", node_type_id, node_type_id)
    targets = collect_gesture_target_types(gesture_data)
    target_labels = {
        0: "none",
        0xA399FA92: "LookAt",
        0xE83A8507: "AimAt",
    }
    descriptors: list[tuple[int, int, int, int, int, int, int, int]] = []
    for signature_offset in find_all(anim_data, signature):
        descriptor_offset = signature_offset + 4
        if descriptor_offset + 0x28 > len(anim_data):
            raise ValueError(
                f"truncated AnimNodeGesture descriptor at 0x{descriptor_offset:X}"
            )
        (
            descriptor_type,
            gesture_id,
            selector_callback,
            remap_callback,
            target_callback,
            mode,
            flags,
        ) = struct.unpack_from("<IIQQQII", anim_data, descriptor_offset)
        if descriptor_type != node_type_id:
            raise ValueError(
                f"bad AnimNodeGesture type at 0x{descriptor_offset:X}"
            )
        target_type = targets.get(gesture_id, 0) if gesture_id else 0
        descriptors.append(
            (
                descriptor_offset,
                gesture_id,
                selector_callback,
                remap_callback,
                target_callback,
                mode,
                flags,
                target_type,
            )
        )

    fixed = [row for row in descriptors if row[1] != 0]
    dynamic = [row for row in descriptors if row[1] == 0]
    fixed_look_at = [row for row in fixed if row[7] == 0xA399FA92]
    fixed_aim_at = [row for row in fixed if row[7] == 0xE83A8507]
    fixed_none = [row for row in fixed if row[7] == 0]
    dynamic_target_callbacks = [row for row in dynamic if row[4] != 0]

    print(
        f"anim_node_gesture_id=0x{node_type_id:08X} "
        f"gesture_settings={len(targets)} descriptors={len(descriptors)} "
        f"fixed={len(fixed)} dynamic={len(dynamic)}"
    )
    for (
        offset,
        gesture_id,
        selector_callback,
        remap_callback,
        target_callback,
        mode,
        flags,
        target_type,
    ) in descriptors:
        target_label = target_labels.get(target_type, "other")
        print(
            f"node offset=0x{offset:X} gesture=0x{gesture_id:08X} "
            f"target=0x{target_type:08X}/{target_label} "
            f"selector=0x{selector_callback:X} remap=0x{remap_callback:X} "
            f"target_callback=0x{target_callback:X} mode=0x{mode:X} "
            f"flags=0x{flags:X}"
        )
    print(
        f"fixed_look_at={len(fixed_look_at)} "
        f"fixed_aim_at={len(fixed_aim_at)} fixed_none={len(fixed_none)} "
        f"dynamic_target_callbacks={len(dynamic_target_callbacks)}"
    )
    ads_name = "strafe-idle-ap"
    ads_rows: list[tuple[int, int, int, int]] = []
    ownership_complete = True
    for row in descriptors:
        owners, searched, complete = trace_state_owners(anim_data, row[0])
        ownership_complete &= complete
        rendered_owners = ",".join(
            f"0x{offset:X}:{name}:0x{state_id:08X}"
            for offset, name, state_id, _animation_id in owners
        )
        print(
            f"node_owners offset=0x{row[0]:X} gesture=0x{row[1]:08X} "
            f"target=0x{row[7]:08X} searched={searched} "
            f"complete={int(complete)} states=[{rendered_owners}]"
        )
        if any(owner[1] == ads_name for owner in owners):
            ads_rows.append((row[0], row[1], row[4], row[7]))
    rendered_ads = ",".join(
        f"0x{offset:X}/gesture=0x{gesture_id:08X}/"
        f"target_callback=0x{target_callback:X}/target=0x{target_type:08X}"
        for offset, gesture_id, target_callback, target_type in ads_rows
    )
    print(
        f"gesture_owner_audit_complete={int(ownership_complete)} "
        f"control_state={ads_name!r} nodes={len(ads_rows)} rows=[{rendered_ads}]"
    )
    audit_script_member_accesses(anim_data, 0x5B0)
    for row in dynamic_target_callbacks:
        audit_dynamic_gesture_owner(anim_data, row[0], row[4])


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
        "--resolve-string-id",
        action="append",
        default=[],
        metavar="U32",
        help=(
            "hash printable strings in --entry and print preimages for this "
            "StringId; repeatable"
        ),
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
    parser.add_argument(
        "--player-mesh-look",
        action="append",
        default=[],
        metavar="STRING_OR_U32",
        help="inspect this look in a player-mesh DC00 entry; repeatable",
    )
    parser.add_argument(
        "--map-source",
        action="append",
        default=[],
        metavar="STRING_OR_U32",
        help="print replacement rows for this source id; repeatable",
    )
    parser.add_argument(
        "--dc00-record-id",
        action="append",
        default=[],
        metavar="STRING_OR_U32",
        help=(
            "show DC00 objects whose first 0x30 record bytes contain every "
            "supplied id; repeatable"
        ),
    )
    parser.add_argument(
        "--dc00-table-offset",
        action="append",
        default=[],
        metavar="OFFSET",
        help=(
            "show the DC00 object whose 0x28-row table contains this file "
            "offset; repeatable"
        ),
    )
    parser.add_argument(
        "--dc00-map-id",
        action="append",
        default=[],
        metavar="STRING_OR_U32",
        help=(
            "find source or target occurrences in every valid DC00 "
            "anim-overlay-set table; repeatable"
        ),
    )
    parser.add_argument(
        "--compare-player-mesh-look",
        action="append",
        default=[],
        metavar="STRING_OR_U32",
        help="compare the first --player-mesh-look with this control look",
    )
    parser.add_argument(
        "--name-archive",
        type=Path,
        help="optional PSARC containing printable animation names",
    )
    parser.add_argument(
        "--name-entry",
        help="entry in --name-archive used to resolve row StringIds",
    )
    parser.add_argument(
        "--map-name-regex",
        help="restrict compared rows to resolved source names matching this regex",
    )
    parser.add_argument(
        "--gesture-node-audit",
        action="store_true",
        help=(
            "cross-reference AnimNodeGesture descriptors in --entry with "
            "authored target types from --gesture-settings-entry"
        ),
    )
    parser.add_argument(
        "--gesture-settings-entry",
        default="gestures.bin",
        help="DC00 gesture settings entry used by --gesture-node-audit",
    )
    parser.add_argument(
        "--animation-mask",
        action="append",
        default=[],
        metavar="STRING_OR_U32",
        help=(
            "resolve this animation in a paged PAK and print its authored "
            "joint ValidBits mask; repeatable"
        ),
    )
    parser.add_argument(
        "--skeleton-joint-table",
        action="append",
        default=[],
        metavar="OFFSET:COUNT",
        help=(
            "print an explicit stride-16 PAK skeleton joint-hash table; "
            "repeatable"
        ),
    )
    parser.add_argument(
        "--skeleton-retarget",
        action="append",
        default=[],
        metavar="SOURCE_ART:TARGET_ART",
        help=(
            "find and print an exact DC00 skeleton-retarget record and map; "
            "repeatable"
        ),
    )
    parser.add_argument("--min-string", type=int, default=3)
    parser.add_argument("--limit", type=int, default=200)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.map_source and not args.player_mesh_look:
        raise ValueError("--map-source requires at least one --player-mesh-look")
    if args.compare_player_mesh_look and len(args.player_mesh_look) != 1:
        raise ValueError(
            "--compare-player-mesh-look requires exactly one --player-mesh-look"
        )
    if (args.name_archive is None) != (args.name_entry is None):
        raise ValueError("--name-archive and --name-entry must be supplied together")
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
        if (
            args.id
            or args.resolve_string_id
            or args.strings
            or args.player_mesh_look
            or args.map_source
            or args.dc00_record_id
            or args.dc00_table_offset
            or args.dc00_map_id
            or args.gesture_node_audit
            or args.animation_mask
            or args.skeleton_joint_table
            or args.skeleton_retarget
        ):
            raise ValueError(
                "--entry is required with --id, --resolve-string-id, --strings, "
                "--player-mesh-look, --map-source, --gesture-node-audit "
                "--dc00-record-id, --dc00-table-offset, --dc00-map-id or "
                "--animation-mask, --skeleton-joint-table or "
                "--skeleton-retarget"
            )
        return 0

    data = archive.extract(args.entry)
    print(f"entry={args.entry} bytes={len(data)}")

    if args.animation_mask:
        audit_pak_animation_masks(data, args.animation_mask)

    if args.skeleton_joint_table:
        audit_skeleton_joint_tables(
            data, args.skeleton_joint_table, args.min_string
        )

    if args.skeleton_retarget:
        audit_skeleton_retargets(data, args.skeleton_retarget)

    if args.gesture_node_audit:
        gesture_data = archive.extract(args.gesture_settings_entry)
        print(
            f"gesture_settings_entry={args.gesture_settings_entry} "
            f"bytes={len(gesture_data)}"
        )
        audit_anim_node_gestures(data, gesture_data)

    if args.player_mesh_look or args.map_source:
        inspect_player_mesh_dc00(data, args.player_mesh_look, args.map_source)

    if args.dc00_record_id or args.dc00_table_offset:
        audit_dc00_objects(
            data, args.dc00_record_id, args.dc00_table_offset, args.limit
        )

    if args.dc00_map_id:
        audit_dc00_overlay_rows(data, args.dc00_map_id, args.limit)

    if args.compare_player_mesh_look:
        known_names: dict[int, list[str]] = {}
        if args.name_archive is not None:
            name_archive = Psarc(
                args.name_archive.resolve(),
                args.oodle.resolve() if args.oodle else None,
            )
            name_data = name_archive.extract(args.name_entry)
            known_names = collect_printable_string_ids(name_data, args.min_string)
            print(
                f"name_archive={name_archive.path} name_entry={args.name_entry} "
                f"name_bytes={len(name_data)} known_name_ids={len(known_names)}"
            )
        compare_player_mesh_rows(
            data,
            args.player_mesh_look[0],
            args.compare_player_mesh_look,
            known_names,
            args.map_name_regex,
            args.limit,
        )

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

    if args.resolve_string_id:
        targets = {parse_id(label)[0] for label in args.resolve_string_id}
        printable = re.compile(rb"[ -~]{%d,}" % max(1, args.min_string))
        matches: dict[tuple[int, str], int] = {}
        for match in printable.finditer(data):
            raw = match.group()
            value = string_id(raw)
            if value in targets:
                matches.setdefault(
                    (value, raw.decode("ascii")), match.start()
                )
        shown = 0
        for (value, text), offset in sorted(matches.items()):
            if shown >= args.limit:
                break
            print(f"string_id=0x{value:08X} offset=0x{offset:X} text={text!r}")
            shown += 1
        unresolved = targets.difference(value for value, _text in matches)
        rendered = ",".join(f"0x{value:08X}" for value in sorted(unresolved))
        print(
            f"resolved_string_ids={shown} unresolved=[{rendered}] "
            f"limit={args.limit}"
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
