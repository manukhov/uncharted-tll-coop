"""Build-pinned SEC_IMAGE_NO_EXECUTE query-only control, never game execution.

Default: locked-file hash/header preflight only. --execute uses one hidden,
60-second child. No retail entry points, loader initialization, manual relocations,
VirtualProtect, payload writes, or preparation calls exist in this script.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import statistics
import struct
import subprocess
import sys
from time import perf_counter_ns

import native_anim_pose_audit as native
import p99_query_layout_probe as layout

SEC_IMAGE_NO_EXECUTE = 0x11000000  # SDK winnt.h: SEC_IMAGE | SEC_NOCACHE.
MAPPING_PROTECTION, VIEW_ACCESS = SEC_IMAGE_NO_EXECUTE | 0x02, 0x04
MEM_IMAGE = 0x1000000
CURSORS = {"fixed_table": 0x3864A00, "retained_slot": 0x3EEBE80}
MAX_FILE_BYTES, CHUNK_BYTES = 128 * 1024 * 1024, 1024 * 1024


def _distribution(values):
    ordered = sorted(values)
    return {"count": len(values), "min_ms": ordered[0], "median_ms": statistics.median(ordered),
            "p95_ms": ordered[math.ceil(len(ordered) * .95) - 1], "max_ms": ordered[-1]}


def validate_region(value, address, image, span=1):
    keys = ("base", "allocation_base", "allocation_protect", "partition_id",
            "region_size", "state", "protect", "type")
    if any(type(value.get(key)) is not int or not 0 <= value[key] <= layout.U64_MAX for key in keys):
        raise ValueError("invalid image-region descriptor")
    if (not 0 < image <= address < address + span <= image + layout.IMAGE_BYTES <= layout.U64_MAX or
            value["allocation_base"] != image or value["state"] != 0x1000 or
            value["type"] != MEM_IMAGE or value["protect"] not in (2, 4, 8) or
            not image <= value["base"] <= address < address + span <=
                value["base"] + value["region_size"] <= image + layout.IMAGE_BYTES):
        raise ValueError("mapped cursor is not owned MEM_IMAGE/readable/nonexecuting/unguarded")


def _safe_pe_fields(header):
    if len(header) < 0x40 or header[:2] != b"MZ":
        return {"unavailable": "DOS header bounds/signature"}
    pe = struct.unpack_from("<I", header, 0x3C)[0]
    if not 0x40 <= pe <= len(header) - 24 or header[pe:pe + 4] != b"PE\0\0":
        return {"unavailable": "PE header bounds/signature"}
    machine, sections = struct.unpack_from("<HH", header, pe + 4)
    optional_bytes = struct.unpack_from("<H", header, pe + 20)[0]
    optional = pe + 24
    if (optional_bytes < 0x40 or not 1 <= sections <= 96 or
            optional + optional_bytes + sections * 40 > len(header)):
        return {"unavailable": "optional header/section table bounds"}
    magic = struct.unpack_from("<H", header, optional)[0]
    if magic != 0x20B:
        return {"unavailable": "not PE32+", "optional_magic": hex(magic)}
    return {"machine": hex(machine), "sections": sections, "optional_magic": hex(magic),
            "ImageBase": hex(struct.unpack_from("<Q", header, optional + 0x18)[0])}


def _require_same_headers(raw, mapped):
    if raw == mapped:
        return
    # Diagnostic only: never normalize/admit differing headers. At most16 ranges
    # and32 changed positions => at most64 dumped bytes across raw+mapped sides.
    raw, mapped = raw[:0x400], mapped[:0x400]
    changed = [offset for offset in range(max(len(raw), len(mapped)))
               if raw[offset:offset + 1] != mapped[offset:offset + 1]]
    spans = []
    for offset in changed[:32]:
        if spans and offset == spans[-1][1]:
            spans[-1][1] += 1
        elif len(spans) < 16:
            spans.append([offset, offset + 1])
        else:
            break
    ranges = [{"offset": hex(start), "raw": raw[start:end].hex(), "mapped": mapped[start:end].hex()}
              for start, end in spans]
    details = {"raw_length": len(raw), "mapped_length": len(mapped),
        "difference_count": len(changed), "shown_payload_bytes": sum(
            len(item["raw"]) // 2 + len(item["mapped"]) // 2 for item in ranges),
        "omitted_difference_bytes": len(changed) - sum(end - start for start, end in spans),
        "raw_pe": _safe_pe_fields(raw), "mapped_pe": _safe_pe_fields(mapped), "ranges": ranges}
    raise ValueError("mapped declared headers differ from same locked file: " + json.dumps(details, sort_keys=True))


def _reject_mapped_header(raw, mapped, reason):
    try:
        _require_same_headers(raw, mapped)
    except ValueError as difference:
        raise ValueError(reason + "; " + str(difference)) from None
    raise ValueError(reason)


def admit_mapped_header(raw_page, mapped_page, image):
    """Treat only mapped ImageBase as opaque metadata; actual view is authoritative."""
    if (len(raw_page) != 4096 or len(mapped_page) != 4096 or type(image) is not int or
            not 0 < image < image + layout.IMAGE_BYTES <= layout.U64_MAX):
        raise ValueError("invalid complete header-page width or mapped-view address")
    raw, mapped = raw_page[:0x400], mapped_page[:0x400]
    fields = _safe_pe_fields(raw)
    if "unavailable" in fields or fields["ImageBase"] != hex(layout.PREFERRED_BASE):
        _reject_mapped_header(raw, mapped, "raw PE32+ ImageBase is not the pinned preferred base")
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    base_offset = pe + 24 + 0x18
    header_base = struct.unpack_from("<Q", mapped, base_offset)[0]
    local_copy = bytearray(mapped_page)
    local_copy[base_offset:base_offset + 8] = raw[base_offset:base_offset + 8]
    if local_copy[:0x400] != raw:
        _reject_mapped_header(raw, mapped, "mapped header changed outside the single eight-byte ImageBase field")
    layout.validate_layout(bytes(local_copy), layout.IMAGE_BYTES)
    return {"policy": "Only eight-byte OptionalHeader.ImageBase may differ: pinned raw preferred base; mapped field is uninterpreted metadata, never address authority; local-copy normalization only; no claim why it differs",
        "raw_image_base": layout.PREFERRED_BASE, "mapped_header_image_base": header_base,
        "actual_view_base": image, "image_base_matches_view": header_base == image,
        "image_base_offset": base_offset, "image_base_width": 8,
        "raw_declared_header_sha256": hashlib.sha256(raw).hexdigest(),
        "mapped_declared_header_sha256": hashlib.sha256(mapped).hexdigest(),
        "difference_count": sum(left != right for left, right in zip(raw, mapped))}


def _locked_file(ops, handle, expected_sha256):
    size = ops.file_size(handle)
    if not 4096 <= size <= MAX_FILE_BYTES:
        raise ValueError("file size outside bounded pinned-image admission")
    digest, first, consumed = hashlib.sha256(), b"", 0
    while consumed < size:
        chunk = ops.read_file(handle, min(CHUNK_BYTES, size - consumed))
        if not chunk or len(chunk) > min(CHUNK_BYTES, size - consumed):
            raise ValueError("short or oversized locked-file read")
        if len(first) < 4096:
            first += chunk[:4096 - len(first)]
        digest.update(chunk)
        consumed += len(chunk)
    if ops.read_file(handle, 1) or ops.file_size(handle) != size or digest.hexdigest() != expected_sha256:
        raise ValueError("locked executable length/hash changed or unsupported build")
    pe = struct.unpack_from("<I", first, 0x3C)[0]
    geometry = layout.validate_layout(first, layout.IMAGE_BYTES)
    header_bytes = struct.unpack_from("<I", first, pe + 24 + 0x3C)[0]
    if header_bytes != 0x400:
        raise ValueError("pinned SizeOfHeaders differs")
    return first, {"size": size, "sha256": digest.hexdigest(), "header_bytes": header_bytes,
        "header_sha256": hashlib.sha256(first[:header_bytes]).hexdigest(),
        "image_size": geometry["image_size"], "data_start": geometry["data_start"],
        "data_end": geometry["data_end"], "data_characteristics": geometry["data_characteristics"]}


class _Windows:
    def __init__(self):
        if ctypes.sizeof(ctypes.c_void_p) != 8:
            raise RuntimeError("64-bit Windows Python is required")
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        pointer, u32 = ctypes.c_void_p, ctypes.c_uint32
        declarations = {
            "CreateFileW": ([ctypes.c_wchar_p, u32, u32, pointer, u32, u32, pointer], pointer),
            "GetFileSizeEx": ([pointer, ctypes.POINTER(ctypes.c_int64)], ctypes.c_int),
            "ReadFile": ([pointer, pointer, u32, ctypes.POINTER(u32), pointer], ctypes.c_int),
            "CreateFileMappingW": ([pointer, pointer, u32, u32, u32, ctypes.c_wchar_p], pointer),
            "MapViewOfFile": ([pointer, u32, u32, u32, ctypes.c_size_t], pointer),
            "UnmapViewOfFile": ([pointer], ctypes.c_int),
            "CloseHandle": ([pointer], ctypes.c_int),
            "VirtualQuery": ([pointer, ctypes.POINTER(layout.MemoryBasicInformation64), ctypes.c_size_t], ctypes.c_size_t)}
        for name, (arguments, result) in declarations.items():
            function = getattr(self.kernel, name)
            function.argtypes, function.restype = arguments, result

    @staticmethod
    def _error():
        return ctypes.WinError(ctypes.get_last_error())

    def open_file(self, path):
        # FILE_SHARE_READ only: retain this handle through unmapping; deny write/delete.
        handle = self.kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x80, None)
        if not handle or handle == ctypes.c_void_p(-1).value:
            raise self._error()
        return int(handle)

    def file_size(self, handle):
        size = ctypes.c_int64()
        if not self.kernel.GetFileSizeEx(handle, ctypes.byref(size)):
            raise self._error()
        return size.value

    def read_file(self, handle, size):
        buffer, used = ctypes.create_string_buffer(size), ctypes.c_uint32()
        if not self.kernel.ReadFile(handle, buffer, size, ctypes.byref(used), None):
            raise self._error()
        if used.value > size:
            raise ValueError("ReadFile returned an oversized count")
        return buffer.raw[:used.value]

    def create_mapping(self, handle):
        value = self.kernel.CreateFileMappingW(handle, None, MAPPING_PROTECTION, 0, 0, None)
        if not value:
            raise self._error()
        return int(value)

    def map_view(self, mapping):
        value = self.kernel.MapViewOfFile(mapping, VIEW_ACCESS, 0, 0, 0)
        if not value:
            raise self._error()
        return int(value)

    def query(self, address, timed=False):
        info = layout.MemoryBasicInformation64()
        pointer, output, size = ctypes.c_void_p(address), ctypes.byref(info), ctypes.sizeof(info)
        begin = perf_counter_ns() if timed else 0
        result = self.kernel.VirtualQuery(pointer, output, size)
        elapsed = perf_counter_ns() - begin if timed else 0
        if result != 48:
            raise self._error()
        if elapsed < 0:
            raise ValueError("query clock moved backward")
        descriptor = {name: int(getattr(info, field)) for name, field in (
            ("base", "BaseAddress"), ("allocation_base", "AllocationBase"),
            ("allocation_protect", "AllocationProtect"), ("partition_id", "PartitionId"),
            ("region_size", "RegionSize"), ("state", "State"), ("protect", "Protect"), ("type", "Type"))}
        return descriptor, elapsed

    @staticmethod
    def read_view(address, size):
        return ctypes.string_at(address, size)

    def unmap(self, view):
        if not self.kernel.UnmapViewOfFile(view):
            raise self._error()

    def close(self, handle):
        if not self.kernel.CloseHandle(handle):
            raise self._error()


def _audit(path, execute, samples, ops, expected_sha256=native.EXPECTED_EXE_SHA256):
    if type(samples) is not int or not 1 <= samples <= 512:
        raise ValueError("samples must be between1 and512")
    file_handle = mapping = image = 0
    try:
        file_handle = ops.open_file(path)
        raw_header, metadata = _locked_file(ops, file_handle, expected_sha256)
        result = {"schema": "tll-image-query-audit-v1",
            "classification": "OWNED_IMAGE_QUERY_ONLY" if execute else "STATIC_PREFLIGHT",
            "file": metadata, "retail_calls": 0, "payload_writes": 0, "virtual_protect_calls": 0,
            "game_launched_or_attached": False, "live_budget_admitted": False,
            "mapping_created": bool(execute), "queries": {}}
        if execute:
            mapping = ops.create_mapping(file_handle)
            image = ops.map_view(mapping)
            header_region, _ = ops.query(image)
            validate_region(header_region, image, image, 4096)
            text_region, _ = ops.query(image + 0x1000)
            validate_region(text_region, image + 0x1000, image)
            mapped_header = ops.read_view(image, 4096)
            header_admission = admit_mapped_header(raw_header, mapped_header, image)
            values, descriptors = {name: [] for name in CURSORS}, {}
            for _ in range(samples + 1):
                for name, rva in CURSORS.items():
                    descriptor, elapsed = ops.query(image + rva, timed=True)
                    validate_region(descriptor, image + rva, image)
                    if type(elapsed) is not int or elapsed < 0:
                        raise ValueError("invalid query timing")
                    if name in descriptors and descriptors[name] != descriptor:
                        raise ValueError("image query descriptor changed across samples")
                    descriptors[name] = descriptor
                    values[name].append(elapsed / 1_000_000)
            if ops.read_view(image, 4096) != mapped_header:
                raise ValueError("full mapped header page changed during query-only control")
            result.update({"mapping": {"protection_flags": MAPPING_PROTECTION,
                "view_access": VIEW_ACCESS, "base": image, "size": layout.IMAGE_BYTES,
                "header_region": header_region, "text_probe_rva": 0x1000, "text_region": text_region,
                "header_admission": header_admission,
                "read_access_note": "FILE_MAP_READ requested; data protection may be RO/RW/WRITECOPY; no whole-image read-only claim",
                "mapped_first_page_sha256": hashlib.sha256(mapped_header).hexdigest()},
                "timing_scope": "perf_counter_ns around VirtualQuery; Python/FFI included, not preparation",
                "queries": {name: {"rva": CURSORS[name], "address": image + CURSORS[name],
                    "descriptor": descriptors[name], "first_ms": values[name][0],
                    "warm_time": _distribution(values[name][1:])} for name in CURSORS}})
    finally:
        errors = []
        for action, value in ((ops.unmap, image), (ops.close, mapping), (ops.close, file_handle)):
            if value:
                try:
                    action(value)
                except BaseException as error:
                    errors.append(error)
        if errors:
            raise RuntimeError(f"image-query cleanup failed ({len(errors)} operations); stop child") from errors[0]
    result["cleanup_complete"] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=native.DEFAULT_GAME / "tll.exe")
    parser.add_argument("--samples", type=int, default=128)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 1 <= args.samples <= 512 or (args.worker and not args.execute):
        parser.error("samples must be1..512; worker requires --execute")
    if args.execute and (args.output is None or args.output.exists()):
        parser.error("--execute requires a new, explicit --output path")
    if args.worker or not args.execute:
        result = _audit(args.exe.resolve(), args.execute, args.samples, _Windows())
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            with args.output.open("x", encoding="utf-8") as stream:
                json.dump(result, stream, indent=2)
                stream.write("\n")
        print(json.dumps(result, indent=2))
        return
    # Parent performs only file preflight; the child rehashes its own still-open
    # handle before mapping, so no parent->child pathname race admits a new file.
    _audit(args.exe.resolve(), False, args.samples, _Windows())
    command = [sys.executable, str(Path(__file__).resolve()), "--worker", "--execute",
        "--exe", str(args.exe.resolve()), "--samples", str(args.samples), "--output", str(args.output.resolve())]
    completed = subprocess.run(command, capture_output=True, text=True, timeout=60,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    if completed.returncode:
        raise RuntimeError(f"query-only child failed ({completed.returncode:#x}): {completed.stderr[-4000:]}")
    print(completed.stdout, end="")


if __name__ == "__main__":
    main()
