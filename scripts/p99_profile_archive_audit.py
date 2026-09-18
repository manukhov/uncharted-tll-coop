#!/usr/bin/env python3
"""Inventory installed TLL PSARC names and optionally scan every .pak entry.

Read-only disk audit, no tll math/game execution. Uses bounded borrowed mappings
instead of copying multi-GB texture archives just to read their name tables.
PAK scope is explicit; a missing byte pattern is NOT a live registry verdict.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct

import p99_native_source_audit as authored

PROFILE_NAME = b"retarget-pose-npc-normal\0"


def resource_headers(data: bytes, pak) -> list[dict]:
    """Disk operands of Package_PreparePageResourceHeaders1416356A0."""
    result = []
    for page_index, page in enumerate(pak.pages):
        page_data = authored.extent(data, page.file_offset, page.size, "content page")
        count = struct.unpack_from("<H", authored.extent(page_data, 0x12, 2, "resource count"))[0]
        authored.extent(page_data, 0x14, count * 16, "resource table")
        for index in range(count):
            row = 0x14 + index * 16
            descriptor = struct.unpack_from("<i", page_data, row + 8)[0]
            authored.extent(page_data, descriptor, 0x20, "resource header")
            strings = []
            for field in (0, 8):
                local = struct.unpack_from("<Q", page_data, descriptor + field)[0]
                authored.extent(page_data, local, 1, "resource string")
                end = page_data.find(b"\0", local, min(len(page_data), local + 4096))
                if end < 0:
                    raise ValueError("unterminated resource string")
                strings.append(page_data[local:end].decode("ascii", "strict"))
            result.append({"page": page_index, "descriptor": descriptor,
                           "name": strings[0], "type": strings[1],
                           "prepare_write_ranges": [[row, 8], [descriptor, 8],
                                [descriptor + 8, 8], [descriptor + 0x18, 4], [descriptor + 0x1C, 4]]})
    return result


def elena_prelogin_certificate(data: bytes) -> dict:
    """Pinned disk write-domain certificate, not execution of arbitrary callbacks."""
    authored.profile_provenance(data)
    pak = authored.source.PagedPak(data)
    page_index, descriptor = authored.PROFILE_DESCRIPTOR
    protected_start, protected_end = descriptor + 0x40, descriptor + 0x44
    resources = resource_headers(data, pak)
    overlaps = [[item["descriptor"], offset, size] for item in resources
                if item["page"] == page_index for offset, size in item["prepare_write_ranges"]
                if offset < protected_end and offset + size > protected_start]
    relocations = [[page, offset] for page, offset in pak.fixups
                   if page == page_index and offset < protected_end and offset + 8 > protected_start]
    if overlaps or relocations:
        raise ValueError("Elena profile database overlaps native preparation writes")
    directory_offset = struct.unpack_from("<i", data, 0x1C)[0]
    directories = [list(struct.unpack_from("<iii", data, directory_offset + index * 12)) for index in range(8)]
    if any(directory[0] != 0 for directory in directories):
        raise ValueError("FreeLoginPages consumes a nonzero login-page index")
    login_page, login_local = struct.unpack_from("<ii", data, 8)
    login_table = pak.absolute(login_page, login_local)
    count = struct.unpack_from("<i", data, login_table + 0x20)[0]
    if count < 0 or login_local + 0x28 + count * 8 > pak.pages[login_page].size:
        raise ValueError("login table exceeds content page")
    by_descriptor = {(item["page"], item["descriptor"]): item for item in resources}
    login_items = []
    for index in range(count):
        page, local = struct.unpack_from("<ii", data, login_table + 0x28 + index * 8)
        if (page, local) not in by_descriptor:
            raise ValueError("login item is not a declared resource")
        item = by_descriptor[(page, local)]
        login_items.append({"page": page, "descriptor": local, "name": item["name"], "type": item["type"]})
    return {"resource_count": len(resources), "resources": resources,
            "fixup_directories": directories, "relocation_count": sum(len(targets) for targets in pak.fixups.values()),
            "database_header_write_overlaps": overlaps, "database_relocation_overlaps": relocations,
            "login_items": login_items,
            "header_preparation_and_relocations_preserve_database_bytes": True,
            "arbitrary_callback_absence_proven": False,
            "note": "1416356A0 header writes and141635760 relocation writes only; login callbacks audited separately"}


def registration_candidate(data: bytes, hit: int) -> dict:
    """Resolve registration->slot->descriptor->ANIM; unsupported graphs stay open."""
    try:
        pak = authored.source.PagedPak(data)
        authored.extent(data, hit, 16, "registration")
        if struct.unpack_from("<I", data, hit)[0] != authored.PROFILE_ID:
            raise ValueError("not the requested profile ID")
        slot_page, slot_local, slot = pak.resolve_pointer(hit + 8)
        desc_page, desc_local, desc = pak.resolve_pointer(slot)
        authored.extent(data, desc, 0x48, "descriptor")
        if desc_local + 0x48 > pak.pages[desc_page].size:
            raise ValueError("descriptor crosses page")
        raw_page, raw_local, raw = pak.resolve_pointer(desc + 0x28)
        authored.extent(data, raw, 0x24, "ANIM header")
        if struct.unpack_from("<I", data, raw)[0] != 0x41430102:
            raise ValueError("not ANIM magic")
        size = struct.unpack_from("<I", data, raw + 8)[0]
        if size < 0x24 or raw_local + size > pak.pages[raw_page].size:
            raise ValueError("ANIM extent crosses page")
        payload = authored.extent(data, raw, size, "ANIM")
        overlaps = [local for page, local in pak.fixups
                    if page == desc_page and local < desc_local + 0x44
                    and local + 8 > desc_local + 0x40]
        return {"resolved": True, "slot": [slot_page, slot_local],
                "padding_word": struct.unpack_from("<I", data, hit + 4)[0],
                "descriptor": [desc_page, desc_local], "raw": [raw_page, raw_local],
                "raw_size": size, "raw_sha256": hashlib.sha256(payload).hexdigest(),
                "source_skeleton": f"{struct.unpack_from('<I', data, raw + 4)[0]:08X}",
                "database": f"{struct.unpack_from('<I', data, desc + 0x40)[0]:08X}",
                "database_pointer_fixup_overlaps": overlaps}
    except (ValueError, struct.error) as error:
        # PagedPak deliberately admits only a subset of native layouts. A
        # refused parser/graph is not evidence that the registration is absent.
        return {"resolved": False, "reason": str(error), "unresolved_candidate": True}


def large_registration_candidate(archive, name: str, total: int, hit: int) -> dict:
    """Check required pointer fixup by bounded reads, never reject mere padding."""
    try:
        read = lambda offset, size: archive.extract_range(name, offset, size)
        header = read(0, 0x2C)
        page_count, page_table, directories, directory_offset = struct.unpack_from("<4I", header, 0x10)
        if not 0 < page_count <= 0x10000 or directories != 8:
            raise ValueError("unadmitted page/fixup layout")
        page_data = read(page_table, page_count * 12)
        pages = [struct.unpack_from("<III", page_data, index * 12)[:2] for index in range(page_count)]
        if any(offset + size > total for offset, size in pages):
            raise ValueError("content page exceeds entry")
        if directory_offset != page_table + page_count * 12:
            raise ValueError("unadmitted fixup-directory offset")
        if directory_offset + 96 > min(offset for offset, _ in pages):
            raise ValueError("truncated fixup directories")
        fields = [(index, hit + 8 - offset) for index, (offset, size) in enumerate(pages)
                  if offset <= hit and hit + 16 <= offset + size]
        if not fields:
            return {"resolved": False, "reason": "outside all authored content pages",
                    "content_page_ranges": [list(page) for page in pages],
                    "pointer_field_offset": hit + 8}
        if len(fields) != 1:
            raise ValueError("overlapping content pages")
        source_page, source_local = fields[0]
        directory_data = read(directory_offset, 96)
        metadata_end = min(offset for offset, _ in pages)
        matches = []
        for index in range(8):
            login_page, table, count = struct.unpack_from("<iii", directory_data, index * 12)
            if count <= 0:
                continue
            if login_page != 0:
                raise ValueError("nonzero login chunk requires follow-up")
            if table < directory_offset + 96 or table + count * 8 > metadata_end:
                raise ValueError("fixup table exceeds admitted metadata")
            table_data = read(table, count * 8)
            for src, dst, local in struct.iter_unpack("<hhi", table_data):
                if src == source_page and local == source_local:
                    matches.append(dst)
        word = struct.unpack_from("<I", read(hit + 4, 4))[0]
        if not matches:
            return {"resolved": False, "reason": "no authored relocation for registration slot pointer",
                    "pointer_page": source_page, "pointer_local": source_local,
                    "fixup_directories_checked": 8, "padding_word": word}
        return {"resolved": False, "reason": "pointer relocation exists; full graph requires follow-up",
                "unresolved_candidate": True, "target_pages": matches, "padding_word": word}
    except (ValueError, struct.error) as error:
        return {"resolved": False, "reason": str(error), "unresolved_candidate": True}


def inspect_archive(archive, scan: bool) -> dict:
    if len(archive.names) != len(set(archive.names)):
        raise ValueError("duplicate PSARC names")
    manifest = [{"name": name, "bytes": archive.entry_size(name)} for name in sorted(archive.names)]
    encoded = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
    selected = [item for item in manifest if item["name"].lower().endswith(".pak")]
    hits, entries = [], []
    for item in selected if scan else []:
        hasher = hashlib.sha256()
        offset, tail, name_tail, offsets, name_offsets = 0, b"", b"", [], []
        needle = struct.pack("<I", authored.PROFILE_ID)
        for chunk in archive.iter_index_chunks(archive.name_to_index[item["name"]]):
            hasher.update(chunk)
            window = tail + chunk
            offsets.extend(offset - len(tail) + hit for hit in authored.source.find_all(window, needle))
            name_window = name_tail + chunk
            name_offsets.extend(offset - len(name_tail) + hit for hit in authored.source.find_all(name_window, PROFILE_NAME))
            offset += len(chunk)
            tail = window[-3:]
            name_tail = name_window[-(len(PROFILE_NAME) - 1):]
        if offset != item["bytes"]:
            raise ValueError(f"extracted size differs: {item['name']}")
        digest = hasher.hexdigest()
        entries.append({**item, "sha256": digest, "byte_offsets": offsets, "profile_name_offsets": name_offsets})
        data = archive.extract(item["name"]) if offsets and item["bytes"] <= 64 * 1024 * 1024 else None
        for hit in offsets:
            if data is not None:
                candidate = registration_candidate(data, hit)
            elif hit + 16 > item["bytes"]:
                candidate = {"resolved": False, "reason": "truncated registration"}
            else:
                candidate = large_registration_candidate(archive, item["name"], item["bytes"], hit)
            hits.append({"entry": item["name"], "entry_sha256": digest, "offset": hit,
                         "registration": candidate})
            if item["name"] == authored.PROFILE_ENTRY and digest == authored.PROFILE_ENTRY_SHA256:
                hits[-1]["prelogin_certificate"] = elena_prelogin_certificate(data)
    return {"name_count": len(manifest), "name_manifest_sha256": hashlib.sha256(encoded).hexdigest(),
            "all_entry_bytes": sum(item["bytes"] for item in manifest),
            "pak_count": len(selected), "pak_bytes": sum(item["bytes"] for item in selected),
            "all_paks_scanned": scan, "entries": entries, "hits": hits,
            "nonpak_count": len(manifest) - len(selected)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    native = authored.source.native
    parser.add_argument("--root", type=Path, default=native.DEFAULT_GAME / "Uncharted4_data" / "build" / "pc" / "thelostlegacy")
    parser.add_argument("--exe", type=Path, default=native.DEFAULT_GAME / "tll.exe")
    parser.add_argument("--scan-paks", action="store_true")
    parser.add_argument("--check", action="store_true", help="recompute and compare the existing report without writing it")
    parser.add_argument("--output", type=Path, default=authored.ROOT / "reports" / "p99-profile-archives.json")
    args = parser.parse_args()
    with args.exe.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != native.EXPECTED_EXE_SHA256:
        raise ValueError("unsupported executable")
    paths = sorted(args.root.rglob("*.psarc"))
    if not paths:
        raise ValueError("no PSARC archives found")
    reader = native.load_audit_module()
    results = []
    for path in paths:
        with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
            archive = reader.Psarc(path, None, data=mapped)
            result = inspect_archive(archive, args.scan_paks)
            result["archive"] = str(path.relative_to(args.root))
            result["archive_bytes"] = len(mapped)
            results.append(result)
            print(json.dumps({key: value for key, value in result.items() if key not in ("entries", "hits")}
                             | {"hit_count": len(result["hits"])}), flush=True)
    result = {"schema": 2, "executable_sha256": digest, "archive_root": str(args.root.resolve()),
              "profile_id": f"{authored.PROFILE_ID:08X}", "profile_name": PROFILE_NAME[:-1].decode(), "archives": results,
              "archive_count": len(results), "pak_count": sum(item["pak_count"] for item in results),
              "pak_bytes": sum(item["pak_bytes"] for item in results),
              "scope": "all *.psarc recursively under root; every case-insensitive .pak entry",
              "all_paks_scanned": args.scan_paks, "nonpak_payloads_scanned": False,
              "live_registry_proven": False, "game_launched_or_attached": False,
              "native_profile_lookup_result": "unresolved", "tll_function_calls": 0}
    encoded = json.dumps(result, indent=2) + "\n"
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != encoded:
            raise ValueError("archive report differs from the recomputed disk evidence")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")


if __name__ == "__main__":
    main()
