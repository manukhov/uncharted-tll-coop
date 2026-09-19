"""Owned-image VirtualQuery layout control; NEVER invokes retail/preparation code.

Only the two reviewed non-executable .data separator pages may change RX -> RO.
No bytes change. All protections are restored before returning or raising.
Timings use perf_counter_ns and include Python/FFI overhead, not preparation.
"""
from __future__ import annotations

import ctypes
import hashlib
import struct
from time import perf_counter_ns

IMAGE_BYTES = 0x7047000
PREFERRED_BASE = 0x140000000
PAGE_BYTES = 0x1000
SPLIT_RVAS = (0x38A5000, 0x3EEC000)
TABLE_RVA = 0x3864A00
TABLE_MAX_END = TABLE_RVA + 256 * 64
assert TABLE_MAX_END == 0x3868A00
RETAINED_RVA = 0x3EEBE80
RX, RO, COMMIT, PRIVATE = 0x20, 0x02, 0x1000, 0x20000
U64_MAX = (1 << 64) - 1


class MemoryBasicInformation64(ctypes.Structure):
    # Windows SDK 10.0.26100.0/um/winnt.h:14870, native _WIN64 structure.
    # PartitionId is followed by implicit padding; RegionSize is at +0x18.
    _fields_ = [("BaseAddress", ctypes.c_uint64), ("AllocationBase", ctypes.c_uint64),
                ("AllocationProtect", ctypes.c_uint32), ("PartitionId", ctypes.c_uint16),
                ("RegionSize", ctypes.c_uint64), ("State", ctypes.c_uint32),
                ("Protect", ctypes.c_uint32), ("Type", ctypes.c_uint32)]


assert ctypes.sizeof(MemoryBasicInformation64) == 48
assert MemoryBasicInformation64.RegionSize.offset == 24


def validate_layout(headers: bytes, image_size: int) -> dict:
    """Pure build-specific PE layout admission, not a replacement for SHA pinning."""
    if image_size != IMAGE_BYTES or len(headers) != PAGE_BYTES or headers[:2] != b"MZ":
        raise ValueError("unreviewed image size or DOS header")
    pe = struct.unpack_from("<I", headers, 0x3C)[0]
    if not 0x40 <= pe <= PAGE_BYTES - 24 or headers[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid bounded PE header")
    machine, section_count = struct.unpack_from("<HH", headers, pe + 4)
    optional_bytes = struct.unpack_from("<H", headers, pe + 20)[0]
    optional = pe + 24
    rows = optional + optional_bytes
    if (machine != 0x8664 or not 1 <= section_count <= 96 or optional_bytes < 0x40 or
            rows + section_count * 40 > len(headers) or
            struct.unpack_from("<H", headers, optional)[0] != 0x20B or
            struct.unpack_from("<Q", headers, optional + 0x18)[0] != PREFERRED_BASE or
            struct.unpack_from("<I", headers, optional + 0x38)[0] != IMAGE_BYTES):
        raise ValueError("unreviewed PE32+ header or section table")
    sections = []
    for index in range(section_count):
        row = rows + index * 40
        name = headers[row:row + 8].rstrip(b"\0")
        size, start = struct.unpack_from("<II", headers, row + 8)
        flags = struct.unpack_from("<I", headers, row + 36)[0]
        if size == 0 or start + size > image_size:
            raise ValueError("section outside owned image")
        sections.append((name, start, start + size, flags))
    data = [item for item in sections if item[0] == b".data"]
    if data != [(b".data", 0x2FDD000, 0x6DB3000, 0xC0000040)]:
        raise ValueError("non-executable .data section differs from pinned image")
    for rva in SPLIT_RVAS:
        covering = [item for item in sections if item[1] < rva + PAGE_BYTES and rva < item[2]]
        if covering != data or not data[0][1] <= rva < rva + PAGE_BYTES <= data[0][2]:
            raise ValueError("split page overlaps another or executable section")
    if TABLE_MAX_END >= SPLIT_RVAS[0] or RETAINED_RVA + 16 >= SPLIT_RVAS[1]:
        raise ValueError("split page overlaps a query operand")
    return {"image_size": image_size, "data_start": data[0][1], "data_end": data[0][2],
            "data_characteristics": data[0][3], "split_rvas": list(SPLIT_RVAS)}


def validate_descriptor(value: dict, address: int, allocation: int, end: int,
                        expected_protection: int) -> None:
    required = ("base", "allocation_base", "allocation_protect", "partition_id",
                "region_size", "state", "protect", "type")
    if any(type(value.get(key)) is not int or not 0 <= value[key] <= U64_MAX for key in required):
        raise ValueError("invalid VirtualQuery descriptor fields")
    if (not 0 < allocation <= address < end <= U64_MAX or value["allocation_base"] != allocation or
            value["state"] != COMMIT or value["type"] != PRIVATE or
            value["protect"] != expected_protection or value["region_size"] == 0 or
            not allocation <= value["base"] <= address < value["base"] + value["region_size"] <= end):
        raise ValueError("query descriptor escaped owned committed allocation/protection")


def validate_split_slots(image_address: int, slots) -> None:
    for rva in SPLIT_RVAS:
        page = image_address + rva
        for start, before, after in slots:
            if len(before) != len(after) or not before:
                raise ValueError("invalid fixture slot snapshot")
            if page < start + len(after) and start < page + PAGE_BYTES:
                raise ValueError("split page overlaps a fixture data slot")


class _WindowsQueries:
    def __init__(self, image):
        # Private ctypes function wrappers: do not alter the caller's argtypes,
        # which its fixture cleanup still needs after this query-only probe.
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.query_function = self.kernel.VirtualQuery
        self.query_function.argtypes = [ctypes.c_void_p,
            ctypes.POINTER(MemoryBasicInformation64), ctypes.c_size_t]
        self.query_function.restype = ctypes.c_size_t
        self.protect_function = self.kernel.VirtualProtect
        self.protect_function.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                         ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        self.protect_function.restype = ctypes.c_int

    @staticmethod
    def _descriptor(info):
        return {name: int(getattr(info, field)) for name, field in (
            ("base", "BaseAddress"), ("allocation_base", "AllocationBase"),
            ("allocation_protect", "AllocationProtect"), ("partition_id", "PartitionId"),
            ("region_size", "RegionSize"), ("state", "State"),
            ("protect", "Protect"), ("type", "Type"))}

    def query(self, address):
        info = MemoryBasicInformation64()
        if self.query_function(ctypes.c_void_p(address), ctypes.byref(info), ctypes.sizeof(info)) != 48:
            raise ctypes.WinError(ctypes.get_last_error())
        return self._descriptor(info)

    def timed_query(self, address):
        info = MemoryBasicInformation64()
        pointer, info_pointer, info_size = ctypes.c_void_p(address), ctypes.byref(info), ctypes.sizeof(info)
        begin = perf_counter_ns()
        result = self.query_function(pointer, info_pointer, info_size)
        end = perf_counter_ns()
        if result != 48:
            raise ctypes.WinError(ctypes.get_last_error())
        if end < begin:
            raise ValueError("query clock moved backward")
        return self._descriptor(info), end - begin

    def protect(self, address, protection):
        previous = ctypes.c_uint32()
        if not self.protect_function(ctypes.c_void_p(address), PAGE_BYTES, protection, ctypes.byref(previous)):
            raise ctypes.WinError(ctypes.get_last_error())
        return previous.value

    @staticmethod
    def read(address, size):
        return ctypes.string_at(address, size)


def _phase(ops, cursors, samples):
    values = {name: [] for name in cursors}
    descriptors = {}
    # Each round visits both image cursors and both controls in the same order.
    for _ in range(samples + 1):
        for name, (address, allocation, end, protection) in cursors.items():
            descriptor, elapsed_ns = ops.timed_query(address)
            validate_descriptor(descriptor, address, allocation, end, protection)
            if type(elapsed_ns) is not int or elapsed_ns < 0:
                raise ValueError("invalid query timing")
            if name in descriptors and descriptors[name] != descriptor:
                raise ValueError("query descriptor changed inside one phase")
            descriptors[name] = descriptor
            values[name].append(elapsed_ns / 1_000_000)
    return {name: {"address": cursors[name][0], "descriptor": descriptors[name], "samples_ms": times}
            for name, times in values.items()}


def _summarize_phase(phase, distribution):
    # Never call the caller-supplied summarizer while protections are changed.
    return {name: {"address": item["address"], "descriptor": item["descriptor"],
                   "first_ms": item["samples_ms"][0], "warm_time": distribution(item["samples_ms"][1:])}
            for name, item in phase.items()}


def _measure_owned(image, fixture, samples, distribution, ops):
    """Internal injectable orchestration for pure cleanup/admission tests."""
    if (type(samples) is not int or not 1 <= samples <= 512 or fixture.image is not image or
            fixture._closed or not 0 < image.address < image.address + image.size <= U64_MAX or
            image.size != IMAGE_BYTES or fixture.table_count not in (158, 175, 256)):
        raise ValueError("unadmitted owned query-probe context")
    image_end = image.address + image.size
    validate_descriptor(ops.query(image.address), image.address, image.address, image_end, RX)
    headers = ops.read(image.address, PAGE_BYTES)
    layout = validate_layout(headers, image.size)
    validate_split_slots(image.address, fixture._image_slots)
    fixture.verify()
    cursors = {"fixed_table": (image.address + TABLE_RVA, image.address, image_end, RX),
               "retained_slot": (image.address + RETAINED_RVA, image.address, image_end, RX)}
    for name in ("weapon", "skeleton0"):
        buffer = fixture.buffers[name]
        if not buffer.readonly or buffer.size <= 0 or buffer.address != buffer.base + PAGE_BYTES + 64:
            raise ValueError("control is not a read-only OwnedBuffer payload")
        allocation_end = buffer.base + buffer.pages + 2 * PAGE_BYTES
        if buffer.base < image_end and image.address < allocation_end:
            raise ValueError("control allocation overlaps image")
        cursors[name] = (buffer.address, buffer.base, allocation_end, RO)
    pages = []
    for rva in SPLIT_RVAS:
        address = image.address + rva
        descriptor = ops.query(address)
        validate_descriptor(descriptor, address, image.address, image_end, RX)
        data = ops.read(address, PAGE_BYTES)
        if len(data) != PAGE_BYTES:
            raise ValueError("incomplete split-page snapshot")
        pages.append((address, descriptor, data))
    before = _phase(ops, cursors, samples)
    changed = []
    try:
        for address, _descriptor, _data in pages:
            previous = ops.protect(address, RO)
            changed.append((address, previous))
            if previous != RX:
                raise ValueError("split page protection changed after admission")
            validate_descriptor(ops.query(address), address, image.address, image_end, RO)
        split = _phase(ops, cursors, samples)
        for control in ("weapon", "skeleton0"):
            if split[control]["descriptor"] != before[control]["descriptor"]:
                raise ValueError("control descriptor changed while splitting image")
    finally:
        cleanup_errors = []
        # Attempt EVERY restoration even if another page's restoration fails.
        for address, previous in reversed(changed):
            try:
                if ops.protect(address, previous) != RO:
                    raise ValueError("split-page protection changed before restoration")
            except BaseException as error:
                cleanup_errors.append(error)
        for address, descriptor, data in pages:
            try:
                if ops.query(address) != descriptor or ops.read(address, PAGE_BYTES) != data:
                    raise ValueError("split page bytes/protection/descriptor did not restore exactly")
            except BaseException as error:
                cleanup_errors.append(error)
        if cleanup_errors:
            raise RuntimeError(f"owned query probe cleanup failed ({len(cleanup_errors)} checks); stop child") from cleanup_errors[0]
    restored = _phase(ops, cursors, samples)
    for name in cursors:
        if restored[name]["descriptor"] != before[name]["descriptor"]:
            raise ValueError("original cursor descriptor did not restore exactly")
    fixture.verify()
    return {"schema": "tll-owned-query-layout-v1", "classification": "OWNED_QUERY_ONLY",
            "timing_scope": "perf_counter_ns around VirtualQuery only; Python/FFI included, not preparation",
            "retail_or_preparation_calls": 0, "payload_writes": 0,
            "protections_restored": True, "page_bytes_unchanged": True,
            "samples_per_cursor_per_phase": samples + 1, "layout": layout,
            "pe_headers_sha256": hashlib.sha256(headers).hexdigest(),
            "split_pages": [{"rva": address - image.address, "bytes": PAGE_BYTES,
                "sha256": hashlib.sha256(data).hexdigest(), "original_descriptor": descriptor,
                "temporary_protect": RO} for address, descriptor, data in pages],
            "before": _summarize_phase(before, distribution),
            "split": _summarize_phase(split, distribution),
            "restored": _summarize_phase(restored, distribution)}


def measure(image, fixture, samples, distribution) -> dict:
    """Run only after all retail/preparation cases on the caller-owned fixture."""
    from p99_native_retarget_audit import RetargetImage, source
    # RetargetImage's constructor admits the complete executable SHA before any
    # mapping. Do not accept a generic executable mapping or a game-module base.
    if (type(image) is not RetargetImage or ctypes.sizeof(ctypes.c_void_p) != 8 or
            image.size != IMAGE_BYTES or image.function_table != image.address + 0x6DB3000):
        raise ValueError("query probe requires a build-pinned owned RetargetImage")
    result = _measure_owned(image, fixture, samples, distribution, _WindowsQueries(image))
    result["executable_sha256"] = source.native.EXPECTED_EXE_SHA256
    result["image_pin"] = "exact RetargetImage type; constructor SHA admission plus owned PE section validation"
    return result
