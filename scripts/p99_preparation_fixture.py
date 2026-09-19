"""Owned AHB preparation graph; constructing it never calls retail/game code.

Use only with an already build-pinned RetargetImage in a terminal-fault child.
The three native lookup functions have a statically closed read-only graph;
this fixture does not call LoginSkeleton, build records, or claim live residency.
"""
from __future__ import annotations

from contextlib import contextmanager
import ctypes
from pathlib import Path
import struct

import p99_native_retarget_audit as audit

source = audit.source
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ARTIFACT_PREFIX = ROOT / "reports" / "p99-full-chain-agz-final"
BASE = source.native.PREFERRED_BASE
BUCKETS = 0x1466EC3E0
SENTINEL = 0x1466EC440
BUCKET_COUNT = 0x1466EC470
INITIALIZED = 0x1466EC474
LOCK = 0x1466EC4B0
SKEL_EPOCH = 0x1438401CC
ANIM_EPOCH = 0x1438401BC
ENTRY_COUNT = 0x1438401D8
ENTRIES = 0x143864A00
RETAINED = 0x143EEBE80
NORMAL_JOB_FLAG = 0x143682F4B
PLAYER_ID = 0x123
WEAPON_ID = 0x456
LOOK = 0xD27C074F
VTABLES = (0x142A4BFE8, 0x142A47F90, 0x142A227A0, 0x142A9BF48)


def bucket_for(database_id: int) -> int:
    value = 0x811C9DC5
    for byte in struct.pack("<I", database_id):
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return value % 384


def pinned_records(prefix: Path) -> tuple[bytes, bytes]:
    values = []
    for suffix, count, digest in (
            (".records.bin", 93, audit.EXPECTED_RECORDS_SHA256),
            (".row157-records.bin", 71, audit.EXPECTED_CONDITIONAL_ROW157_SHA256)):
        data = Path(str(prefix) + suffix).read_bytes()
        if len(data) != count * 46 or source.sha256(data) != digest:
            raise ValueError(f"not reviewed AGZ native records: {suffix}")
        for offset in range(0, len(data), 46):
            audit.validate_numeric_record(data[offset:offset + 46])
        values.append(data)
    return tuple(values)


def validate_artifacts(prefix: Path = DEFAULT_ARTIFACT_PREFIX):
    first, second = pinned_records(prefix)
    for suffix, size, expected in ((".full-input.bin", 3948, audit.EXPECTED_FULL_INPUT_SHA256),
            (".full-output.bin", 1360, audit.EXPECTED_FULL_OUTPUT_SHA256)):
        data = Path(str(prefix) + suffix).read_bytes()
        if len(data) != size or source.sha256(data) != expected:
            raise ValueError(f"not reviewed AGZ fixture: {suffix}")
    return first, second


def row124_mapping(data: bytes):
    """Preserve pinned authored profile/scale; numerical payload is UNUSED."""
    table, count = source.parse_retarget_table(data)
    offset = table + 124 * 48
    row = data[offset:offset + 48]
    source_id, target_id = struct.unpack_from("<II", row, 0x0C)
    map_offset = source.native.u64(row, 0x18)
    authored_count, profile, scale, flags = struct.unpack_from("<4I", row, 0x20)
    if (count != 175 or source_id != source.PROTO_BASE_ID or
            target_id != source.NADINE_SKELETON.base_id or authored_count != 74 or
            profile != 0xE51542CA or scale != 0x3F666666 or flags != 0):
        raise ValueError("row124 authored discriminator changed")
    mapping = audit.authored.extent(data, map_offset, authored_count * 12, "row124 map")
    spec = source.RetargetSpec(source_id, target_id, 124, offset, map_offset,
        authored_count, 71, profile, scale, flags, source.sha256(row), source.sha256(mapping))
    return source.resolve_retarget(data, table, count, spec)


class PreparationFixture:
    """Read-only owned graph plus narrowly allowlisted mapped-image data slots.

    .job/.player/.fg/.target_skeleton are addresses consumed by the offline TU
    helper. .buffers exposes named OwnedBuffers; .fields exposes useful exact
    (address,width) mutation targets. `with fixture.patch(address, bytes): ...`
    temporarily mutates ONLY an owned allocation/allowlisted slot and restores
    bytes/protection. Run mutations outside timing; verify AFTER restoration.
    """
    def __init__(self, image, loaded: dict, *, table_count: int = 175,
                 artifact_prefix: Path = DEFAULT_ARTIFACT_PREFIX):
        if table_count not in (158, 175, 256):
            raise ValueError("fixture table count must be 158, 175 or 256")
        self.image = image
        self.table_count = table_count
        self.buffers = {}
        self.fields = {}
        self._image_slots = []
        self._image_pages = {}
        self._lock_protection = None
        self._closed = False
        self._allowed = {BUCKETS: 8, SENTINEL: 8, BUCKET_COUNT: 4, INITIALIZED: 1,
            LOCK: 8, SKEL_EPOCH: 4, ANIM_EPOCH: 4, ENTRY_COUNT: 4,
            RETAINED: 16, NORMAL_JOB_FLAG: 1, ENTRIES: table_count * 64}
        try:
            first, second = validate_artifacts(Path(artifact_prefix))
            payload = audit.full_chain_input(loaded, first, second)
            if source.sha256(payload) != audit.EXPECTED_FULL_INPUT_SHA256:
                raise ValueError("records/references do not reconstruct pinned AGZ inputs")
            self._make_skeletons(loaded)
            self._make_owner(loaded)
            self._make_registry(loaded)
            self._make_entries(loaded, first, second)
            self._install_image(BUCKET_COUNT, struct.pack("<I", 384))
            self._install_image(INITIALIZED, b"\x01")
            self._install_image(LOCK, bytes(8))
            self._install_image(SKEL_EPOCH, struct.pack("<I", 7))
            self._install_image(ANIM_EPOCH, struct.pack("<I", 11))
            self._install_image(NORMAL_JOB_FLAG, b"\x00")
            # MappedImage is RX throughout. Only the owned lock's data page
            # needs writes for the original adapter's CAS/XCHG. Restore it on
            # close; never make any code page writable for preparation.
            lock_page = (image.address + LOCK - BASE) & ~4095
            previous = ctypes.c_ulong()
            if not image._kernel32.VirtualProtect(ctypes.c_void_p(lock_page), 4096,
                                                   4, ctypes.byref(previous)):
                raise ctypes.WinError(ctypes.get_last_error())
            self._lock_protection = (lock_page, previous.value)
            for name, buffer in self.buffers.items():
                if not buffer.readonly:
                    buffer.initial = buffer.read()
                    buffer.readonly = True
                    buffer.protect(2)
            pages = set()
            for address, before, _after in self._image_slots:
                pages.update(range(address & ~4095, (address + len(before) + 4095) & ~4095, 4096))
            self._image_pages = {page: ctypes.string_at(page, 4096) for page in pages}
            self.metadata = {"classification": "OWNED_SYNTHETIC_PREPARATION_GRAPH",
                "profile_basis": "AGZ conditional numerical operands, not live profile absence",
                "table_count": table_count, "authored_table_count": 175,
                "row124_numeric_payload": "zero UNUSED readable extent; exact authored header/map; never consumed by math",
                "layout": "separate page-guarded read-only OwnedBuffer allocations",
                "retail_code_called_during_construction": False,
                "full_input_sha256": source.sha256(payload),
                "registry_buckets": [bucket_for(s.spec.database_id) for s in loaded["skels"]],
                "image_data_slots": [f"{address - image.address + BASE:X}:{len(after)}"
                                     for address, _before, after in self._image_slots]}
            self.verify()
        except BaseException:
            self.close()
            raise

    def _own(self, name: str, data: bytes, *, mutable: bool = False):
        if name in self.buffers:
            raise ValueError("duplicate owned allocation")
        result = audit.OwnedBuffer(data, not mutable)
        self.buffers[name] = result
        return result

    def _put(self, name: str, offset: int, fmt: str, *values):
        buffer = self.buffers[name]
        payload = struct.pack(fmt, *values)
        if buffer.readonly or offset < 0 or offset + len(payload) > buffer.size:
            raise ValueError("unadmitted fixture construction write")
        ctypes.memmove(buffer.address + offset, payload, len(payload))

    def _make_skeletons(self, loaded):
        self.arts = []
        for index, skel in enumerate(loaded["skels"]):
            spec = skel.spec
            data = loaded["data"][spec.entry_name]
            source.require_blob(spec.entry_name, data, spec.entry_size, spec.entry_sha256)
            resource = self._own(f"skeleton{index}", data)
            raw = resource.address + spec.raw_offset
            art = bytearray(0x60)
            struct.pack_into("<II", art, 0x24, spec.database_id, spec.skeleton_id)
            struct.pack_into("<I", art, 0x34, spec.joint_count)
            struct.pack_into("<Q", art, 0x40, raw)
            struct.pack_into("<Q", art, 0x58, resource.address + spec.joint_table_offset)
            self.arts.append(self._own(f"art{index}", bytes(art)))
        nadine = loaded["skels"][2].spec
        raw = nadine.raw_offset
        if source.native.u16(loaded["data"][nadine.entry_name], raw + 0x1A) != 6480:
            raise ValueError("normal-job raw scratch byte count differs")
        self.target_skeleton = self.arts[2].address

    def _make_owner(self, loaded):
        for name, size in (("player", 0x4014), ("fg", 0x2A8), ("controller", 0x280),
                ("grenade", 0x124), ("weapon", 0x864), ("parent", 0x10),
                ("retained", 0x10), ("weapon_handle", 0x10), ("job", 0x38)):
            self._own(name, bytes(size), mutable=True)
        address = lambda name: self.buffers[name].address
        nadine = loaded["skels"][2]
        refs = audit.authored.skeleton_inputs(loaded["data"][nadine.spec.entry_name], nadine)["references"]
        pose = self._own("local_pose", refs[:66 * 48])
        self.player, self.fg, self.job = address("player"), address("fg"), address("job")
        self._put("player", 0, "<Q", self.image.address + VTABLES[0] - BASE)
        self._put("player", 0xBC, "<I", PLAYER_ID)
        self._put("player", 0x140, "<4f", 0, 0, 0, 1)
        self._put("player", 0x170, "<I", LOOK)
        self._put("player", 0x1A8, "<Q", self.fg)
        self._put("player", 0x240, "<3f", 1, 2, 3)
        self._put("player", 0x1D88, "<Q", address("controller"))
        self._put("player", 0x1E50, "<Q", address("grenade"))
        self._put("fg", 6, "<B", 2)
        self._put("fg", 0x18, "<Q", self.target_skeleton)
        self._put("fg", 0x100, "<Q", pose.address)
        self._put("fg", 0x1A0, "<Q", self.target_skeleton)
        self._put("fg", 0x1A8, "<QI", address("retained"), PLAYER_ID)
        self._put("retained", 8, "<Q", self.player)
        self._put("controller", 0, "<Q", self.image.address + VTABLES[1] - BASE)
        self._put("controller", 0x108, "<Q", self.player)
        self._put("controller", 0x120, "<i", 2)
        self._put("controller", 0x134, "<I", 2)
        self._put("controller", 0x168, "<QI", address("weapon_handle"), WEAPON_ID)
        self._put("grenade", 0, "<Q", self.image.address + VTABLES[2] - BASE)
        self._put("grenade", 0x108, "<Q", self.player)
        self._put("weapon_handle", 8, "<Q", address("weapon"))
        self._put("weapon", 0, "<Q", self.image.address + VTABLES[3] - BASE)
        self._put("weapon", 0xBC, "<I", WEAPON_ID)
        self._put("weapon", 0x4E0, "<Q", address("parent"))
        self._put("weapon", 0x860, "<I", 0x8075397D)
        self._put("parent", 8, "<Q", self.player)
        self._put("job", 0, "<QQ", 1, self.fg)  # Opaque nonnull context; never executed.
        self._install_image(RETAINED, struct.pack("<QII", address("retained"), PLAYER_ID, 0))
        self.fields.update({"weapon_generation": (address("weapon") + 0xBC, 4),
            "fg_local_pose": (self.fg + 0x100, 8), "fg_raw_joint_buffer": (self.fg + 0x2A0, 8),
            "job_fg": (self.job + 8, 8), "player_look": (self.player + 0x170, 4)})

    def _make_registry(self, loaded):
        sentinel = self._own("registry_sentinel", bytes(32))
        buckets = bytearray(385 * 8)
        struct.pack_into("<Q", buckets, 384 * 8, sentinel.address)
        for index, skel in enumerate(loaded["skels"]):
            value = self._own(f"registry_value{index}", struct.pack("<QQII", self.arts[index].address, 0, 0, 0))
            node = self._own(f"registry_node{index}", struct.pack("<QQIIQ", 0, 0,
                skel.spec.database_id, 0, value.address))
            bucket = bucket_for(skel.spec.database_id)
            if struct.unpack_from("<Q", buckets, bucket * 8)[0]:
                raise ValueError("unexpected fixture skeleton hash collision")
            struct.pack_into("<Q", buckets, bucket * 8, node.address)
        bucket_buffer = self._own("registry_buckets", bytes(buckets))
        self._install_image(BUCKETS, struct.pack("<Q", bucket_buffer.address))
        self._install_image(SENTINEL, struct.pack("<Q", sentinel.address))

    def _make_entries(self, loaded, first: bytes, second: bytes):
        dc = loaded["retarget_data"]
        map124 = row124_mapping(dc)
        records124 = bytearray(71 * 46)
        p, c, n = loaded["skels"]
        # seh_validate_p99_chloe_transfer_row124 only admits this complete
        # readable span; it never reads even its metadata, much less runs math.
        # Do not manufacture a numerical oracle for that unused allocation.
        table = bytearray(self.table_count * 64)
        for mapping, src, dst, payload in ((loaded["maps"][0], p, c, first),
                (map124, p, n, bytes(records124)), (loaded["maps"][1], c, n, second)):
            spec = mapping.spec
            map_buffer = self._own(f"map{spec.row_index}", audit.authored.extent(
                dc, spec.map_offset, spec.authored_count * 12, "authored map"))
            raw_row = bytearray(audit.authored.extent(dc, spec.row_offset, 48, "authored row"))
            struct.pack_into("<Q", raw_row, 0x18, map_buffer.address)
            authored_row = self._own(f"authored{spec.row_index}", bytes(raw_row))
            records = self._own(f"records{spec.row_index}", payload)
            group_map = self._own(f"groupmap{spec.row_index}", bytes(16))  # Nonnull; preparation never dereferences it.
            offset = spec.row_index * 64
            struct.pack_into("<Q4I", table, offset, authored_row.address,
                src.spec.database_id, dst.spec.database_id, src.spec.skeleton_id, dst.spec.skeleton_id)
            struct.pack_into("<QHHIQQQ", table, offset + 0x18, records.address,
                spec.runtime_count, spec.flags, spec.scale_bits, group_map.address, 0, 0)
        self._install_image(ENTRIES, bytes(table))
        self._install_image(ENTRY_COUNT, struct.pack("<I", self.table_count))
        self.fields["table_count"] = (self.image.address + ENTRY_COUNT - BASE, 4)
        self.fields["lock"] = (self.image.address + LOCK - BASE, 8)
        for mapping, src, dst, field in ((loaded["maps"][0], p, c, "player_global"),
                                        (loaded["maps"][1], c, n, "chloe_global")):
            resolved = source.resolve_runtime_records(mapping, src, dst)
            wanted = loaded["metadata"]["transfers"][0][field]
            index = next(i for i, item in enumerate(resolved) if item.source_global == wanted)
            name = f"record{mapping.spec.row_index}"
            address = self.buffers[f"records{mapping.spec.row_index}"].address + index * 46
            self.fields[name + "_numeric"] = (address, 1)
            self.fields[name + "_padding"] = (address + 45, 1)
        # Last selected row157 numerical lane, not the last unselected native record.
        selected = source.resolve_runtime_records(loaded["maps"][1], c, n)
        wanted = loaded["metadata"]["transfers"][-1]["chloe_global"]
        record_index = next(i for i, item in enumerate(selected) if item.source_global == wanted)
        self.fields["record157_last_numeric"] = (self.buffers["records157"].address + record_index * 46 + 0x23, 1)

    def _write_image_bytes(self, address: int, payload: bytes):
        if not payload or address < self.image.address or address + len(payload) > self.image.address + self.image.size:
            raise ValueError("mapped data write exceeds image")
        kernel = self.image._kernel32
        protections = []
        try:
            for page in range(address & ~4095, (address + len(payload) + 4095) & ~4095, 4096):
                previous = ctypes.c_ulong()
                if not kernel.VirtualProtect(ctypes.c_void_p(page), 4096, 4, ctypes.byref(previous)):
                    raise ctypes.WinError(ctypes.get_last_error())
                protections.append((page, previous.value))
            ctypes.memmove(address, payload, len(payload))
        finally:
            for page, protection in reversed(protections):
                ignored = ctypes.c_ulong()
                if not kernel.VirtualProtect(ctypes.c_void_p(page), 4096, protection, ctypes.byref(ignored)):
                    raise ctypes.WinError(ctypes.get_last_error())

    def _install_image(self, va: int, payload: bytes):
        if self._allowed.get(va) != len(payload):
            raise ValueError("unadmitted mapped-image slot")
        address = self.image.address + va - BASE
        if any(address < old + len(before) and old < address + len(payload)
               for old, before, _after in self._image_slots):
            raise ValueError("overlapping/repeated image data patch")
        before = ctypes.string_at(address, len(payload))
        self._image_slots.append((address, before, payload))
        self._write_image_bytes(address, payload)

    @contextmanager
    def patch(self, address: int, payload: bytes):
        """Temporary negative-test mutation outside timing; restore in finally."""
        payload = bytes(payload)
        if self._closed or not payload:
            raise ValueError("empty patch or closed fixture")
        buffer = next((value for value in self.buffers.values()
                       if value.address <= address and address + len(payload) <= value.address + value.size), None)
        image_owned = any(start <= address and address + len(payload) <= start + len(after)
                          for start, _before, after in self._image_slots)
        if buffer is None and not image_owned:
            raise ValueError("patch target is outside owned fixture data")
        before = ctypes.string_at(address, len(payload))
        def write(data):
            if buffer is None:
                self._write_image_bytes(address, data)
            else:
                buffer.protect(4)
                try:
                    ctypes.memmove(address, data, len(data))
                finally:
                    buffer.protect(2)
        try:
            write(payload)
            yield
        finally:
            write(before)

    def verify(self):
        if self._closed:
            raise ValueError("closed fixture")
        for buffer in self.buffers.values():
            buffer.verify()
        for page, expected in self._image_pages.items():
            if ctypes.string_at(page, 4096) != expected:
                raise ValueError(f"mapped fixture page changed atRVA{page - self.image.address:X}; lock must end0")

    def close(self):
        if self._closed:
            return
        # Restore pointers BEFORE their owned targets are released. If restoration
        # fails, intentionally do not free targets; the bounded child must exit.
        for address, before, _after in reversed(self._image_slots):
            self._write_image_bytes(address, before)
        self._image_slots.clear()
        if self._lock_protection is not None:
            page, protection = self._lock_protection
            ignored = ctypes.c_ulong()
            if not self.image._kernel32.VirtualProtect(ctypes.c_void_p(page), 4096,
                                                       protection, ctypes.byref(ignored)):
                raise ctypes.WinError(ctypes.get_last_error())
            self._lock_protection = None
        for buffer in reversed(tuple(self.buffers.values())):
            buffer.close()
        self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, _type, _value, _traceback):
        self.close()
