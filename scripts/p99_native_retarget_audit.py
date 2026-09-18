#!/usr/bin/env python3
"""Build-pinned P99 retarget audit in an isolated helper, never a game process.

Default operation is static preflight only. Numerical execution is explicit,
uses complete owned resource copies, and does not establish live residency or
visual correctness. Row157 may not be silently treated as profile-absent.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import math
import os
import struct
import subprocess
import sys
from pathlib import Path

import generate_p99_chloe_source_rows as source
import p99_native_source_audit as authored

ROOT = Path(__file__).resolve().parents[1]
BUILD_RETARGET = 0x1417CE210
MANAGER_GLOBAL = 0x1437E98C8
SQRTF_IAT = 0x1425D6F60
EXPECTED_RECORDS_SHA256 = "796290ec7f74c487bfd108022a5d9e50dfba4cc808bb8568d1f4f56ff6811934"
EXPECTED_OUTPUTS_SHA256 = "1fc8a73302352ecfcc3605df1ca040fd412374ec771f864df435a81ba8050e86"


class OwnedBuffer:
    """Page-guarded owned storage; read-only inputs and checked output canaries."""
    def __init__(self, data: bytes, readonly: bool = True):
        self.size = len(data)
        self.pages = (self.size + 128 + 4095) & ~4095
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.VirtualAlloc.restype = ctypes.c_void_p
        self.base = self.kernel.VirtualAlloc(None, self.pages + 8192, 0x3000, 1)
        if not self.base:
            raise ctypes.WinError(ctypes.get_last_error())
        self.address = self.base + 4096 + 64
        self.protect(4)
        ctypes.memset(self.base + 4096, 0xCD, self.pages)
        ctypes.memmove(self.address, data, self.size)
        self.initial = data
        self.readonly = readonly
        if readonly:
            self.protect(2)

    def protect(self, protection: int):
        previous = ctypes.c_ulong()
        if not self.kernel.VirtualProtect(ctypes.c_void_p(self.base + 4096),
                self.pages, protection, ctypes.byref(previous)):
            raise ctypes.WinError(ctypes.get_last_error())

    def read(self) -> bytes:
        return ctypes.string_at(self.address, self.size)

    def verify(self):
        if ctypes.string_at(self.address - 64, 64) != bytes([0xCD]) * 64:
            raise ValueError("head canary changed")
        remaining = self.pages - 64 - self.size
        if ctypes.string_at(self.address + self.size, remaining) != bytes([0xCD]) * remaining:
            raise ValueError("tail canary changed")
        if self.readonly and self.read() != self.initial:
            raise ValueError("read-only input changed")

    def close(self):
        if self.base:
            if not self.kernel.VirtualFree(ctypes.c_void_p(self.base), 0, 0x8000):
                raise ctypes.WinError(ctypes.get_last_error())
            self.base = 0


class RetargetImage(source.native.MappedImage):
    """Original code, two additional dependency slots, full registered .pdata.

    Successful ordinary paths only. Unresolved C++ EH/reporting imports make
    unexpected faults terminal; this object is used solely in a child process.
    """
    def __init__(self, path: Path, manager: OwnedBuffer):
        super().__init__(path)
        self.function_table = 0
        self.ucrt = ctypes.WinDLL(str(Path(os.environ["SystemRoot"]) / "System32" / "ucrtbase.dll"))
        self.sqrtf = ctypes.cast(self.ucrt.sqrtf, ctypes.c_void_p).value
        if not self.sqrtf or manager.size != 0xE0 or manager.read() != bytes(0xE0):
            raise ValueError("invalid precisely scoped builder dependencies")
        self._write_dependency(MANAGER_GLOBAL, manager.address, require_zero=True)
        self._write_dependency(SQRTF_IAT, self.sqrtf)
        # PE exception directory is complete, including chained runtime rows.
        pe = ctypes.c_uint32.from_address(self.address + 0x3C).value
        optional = self.address + pe + 24
        directory = optional + 0x70 + 3 * 8
        rva = ctypes.c_uint32.from_address(directory).value
        size = ctypes.c_uint32.from_address(directory + 4).value
        if (rva, size) != (0x6DB3000, 0x16BEA0) or rva + size > self.size:
            raise ValueError("exception directory differs from audited image")
        self._kernel32.RtlAddFunctionTable.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint64]
        self._kernel32.RtlAddFunctionTable.restype = ctypes.c_ubyte
        self._kernel32.RtlDeleteFunctionTable.argtypes = [ctypes.c_void_p]
        self._kernel32.RtlDeleteFunctionTable.restype = ctypes.c_ubyte
        table = self.address + rva
        if not self._kernel32.RtlAddFunctionTable(table, size // 12, self.address):
            raise RuntimeError("RtlAddFunctionTable failed")
        self.function_table = table

    def _write_dependency(self, va: int, value: int, require_zero: bool = False):
        if va not in (MANAGER_GLOBAL, SQRTF_IAT):
            raise ValueError("unadmitted dependency slot")
        address = self.address + va - source.native.PREFERRED_BASE
        if require_zero and ctypes.c_uint64.from_address(address).value != 0:
            raise ValueError("manager is not initially null")
        previous = ctypes.c_ulong()
        if not self._kernel32.VirtualProtect(ctypes.c_void_p(address), 8, 4, ctypes.byref(previous)):
            raise ctypes.WinError(ctypes.get_last_error())
        ctypes.c_uint64.from_address(address).value = value
        ignored = ctypes.c_ulong()
        if not self._kernel32.VirtualProtect(ctypes.c_void_p(address), 8, previous.value, ctypes.byref(ignored)):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.function_table:
            if not self._kernel32.RtlDeleteFunctionTable(self.function_table):
                raise RuntimeError("RtlDeleteFunctionTable failed; image must not be freed")
            self.function_table = 0
        super().close()


class AuditStats(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ("size", "retarget_calls", "blend_calls", "rejected_index")]


def validate_numeric_record(record: bytes):
    """All builder modes have finite halves/nonzero locators; only 1/3 execute later."""
    if len(record) != 46:
        raise ValueError("wrong numeric record stride")
    halves = struct.unpack_from("<18H", record)
    if any((value & 0x7C00) == 0x7C00 for value in halves):
        raise ValueError("nonfinite native builder half")
    if any(not any(value & 0x7FFF for value in halves[first:first + 4]) for first in (3, 10)):
        raise ValueError("zero native builder locator quaternion")
    if record[0x2C] > 3:
        raise ValueError("unknown builder mode")


def build_row20(image: RetargetImage, loaded: dict, bridge, mxcsr: int) -> tuple[dict, bytes, bytes]:
    mapping = loaded["maps"][0]
    src, dst = loaded["skels"][:2]
    if mapping.spec.profile != 0 or mapping.spec.flags != 0:
        raise ValueError("only actual row20 profile0 is numerically admitted")
    expected = source.resolve_runtime_records(mapping, src, dst)
    buffers = []
    def own(data: bytes, readonly=True):
        value = OwnedBuffer(data, readonly)
        buffers.append(value)
        return value
    try:
        resources, arts, refs, matrices = [], [], [], []
        for skel in (src, dst):
            data = loaded["data"][skel.spec.entry_name]
            resource = own(data)
            resources.append(resource)
            raw = resource.address + skel.spec.raw_offset
            art = bytearray(0x60)
            struct.pack_into("<I", art, 0x24, skel.spec.database_id)
            struct.pack_into("<I", art, 0x34, skel.spec.joint_count)
            struct.pack_into("<Q", art, 0x40, raw)
            struct.pack_into("<Q", art, 0x58, resource.address + skel.spec.joint_table_offset)
            arts.append(own(bytes(art)))
            refs.append(raw + source.native.u32(data, skel.spec.raw_offset + 0x20))
            matrices.append(raw + source.native.u32(data, skel.spec.raw_offset + 0x28))
        dc = loaded["retarget_data"]
        map_data = own(authored.extent(dc, mapping.spec.map_offset, mapping.spec.authored_count * 12, "map"))
        raw_row = bytearray(authored.extent(dc, mapping.spec.row_offset, 48, "row"))
        struct.pack_into("<Q", raw_row, 0x18, map_data.address)
        row = own(bytes(raw_row))
        records = own(bytes([0xA7]) * (mapping.spec.authored_count * 46), False)
        entry_data = bytearray(64)
        struct.pack_into("<Q", entry_data, 0, row.address)
        struct.pack_into("<QH", entry_data, 0x18, records.address, mapping.spec.authored_count)
        entry = own(bytes(entry_data), False)
        prototype = ctypes.WINFUNCTYPE(ctypes.c_ubyte, *([ctypes.c_void_p] * 7))
        builder = prototype(image.address + BUILD_RETARGET - source.native.PREFERRED_BASE)
        previous = bridge.P99AuditMxcsr(mxcsr)
        try:
            success = builder(entry.address, arts[0].address, arts[1].address,
                              refs[0], refs[1], matrices[0], matrices[1])
            builder_mxcsr = bridge.P99AuditMxcsr(mxcsr)
            if not success:
                raise ValueError("original BuildRetargetEntry returned false")
            entry_result = entry.read()
            if source.native.u16(entry_result, 0x20) != len(expected):
                raise ValueError("native resolved count differs from authored oracle")
            if (source.native.u64(entry_result, 0) != row.address or
                    source.native.u64(entry_result, 0x18) != records.address or
                    any(entry_result[0x28:])):
                raise ValueError("native entry changed pointer/allocation contract")
            complete = records.read()
            if complete[len(expected) * 46:] != bytes([0xA7]) * ((mapping.spec.authored_count - len(expected)) * 46):
                raise ValueError("native touched unused capacity")
            actual_by_global = {}
            for index, record in enumerate(expected):
                packed = complete[index * 46:(index + 1) * 46]
                validate_numeric_record(packed)
                tail = struct.pack("<HHHHB", record.source_global, authored.parameter_index(src, record.source_global),
                    record.target_global, authored.parameter_index(dst, record.target_global), record.mode)
                if packed[0x24:0x2D] != tail or packed[0x2D] != 0xA7:
                    raise ValueError(f"native record{index} metadata/padding differs")
                actual_by_global[record.source_global] = packed
            selected = bytearray()
            for index, transfer in enumerate(loaded["metadata"]["transfers"]):
                selected.extend(actual_by_global[transfer["player_global"]])
                selected.extend(loaded["references"][index * 96:index * 96 + 48])
            bridge_input = own(bytes(selected))
            bridge_output = own(bytes(21 * 96), False)
            stats = AuditStats(ctypes.sizeof(AuditStats), 0, 0, 0)
            result = bridge.P99AuditRow20(image.address, bridge_input.address, bridge_input.size,
                bridge_output.address, bridge_output.size, ctypes.byref(stats))
            if result != 0 or (stats.retarget_calls, stats.blend_calls) != (21, 21):
                raise ValueError(f"row20 native stage refused {result}, row{stats.rejected_index}, calls{stats.retarget_calls}/{stats.blend_calls}")
            output = bridge_output.read()
        finally:
            bridge.P99AuditMxcsr(previous)
        for buffer in buffers:
            buffer.verify()
        records_bytes = complete[:len(expected) * 46]
        report = {"mxcsr_requested": f"{mxcsr:04X}", "mxcsr_after_builder": f"{builder_mxcsr:04X}",
                  "resolved_records": len(expected), "builder_calls": 1,
                  "retarget_calls": stats.retarget_calls, "blend_calls": stats.blend_calls,
                  "numeric_records_validated": len(expected),
                  "records_sha256": source.sha256(records_bytes), "outputs_sha256": source.sha256(output),
                  "read_only_inputs_unchanged": True, "canaries_unchanged": True,
                  "native_inner_read_inputs_unchanged": True, "native_inner_canaries_unchanged": True,
                  "untouched_capacity_records": mapping.spec.authored_count - len(expected)}
        return report, records_bytes, output
    finally:
        for buffer in reversed(buffers):
            buffer.close()


def run_worker(args, loaded: dict, reports: list[dict], digest: str):
    bridge = ctypes.WinDLL(str(args.bridge.resolve()))
    bridge.P99AuditCpuReady.restype = ctypes.c_uint32
    bridge.P99AuditMxcsr.argtypes = [ctypes.c_uint32]
    bridge.P99AuditMxcsr.restype = ctypes.c_uint32
    bridge.P99AuditRow20.argtypes = [ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint32,
                                    ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(AuditStats)]
    bridge.P99AuditRow20.restype = ctypes.c_int
    if not bridge.P99AuditCpuReady():
        raise RuntimeError("CPU/OS does not admit AVX/F16C/AVX2/XMM/YMM execution")
    manager = OwnedBuffer(bytes(0xE0))
    image = None
    try:
        image = RetargetImage(args.exe, manager)
        runs = [build_row20(image, loaded, bridge, mode) for mode in (0x1F80, 0x9FC0)]
        if runs[0][1:] != runs[1][1:]:
            raise ValueError("row20 records/outputs differ between IEEE and FTZ/DAZ modes")
        if (source.sha256(runs[0][1]) != EXPECTED_RECORDS_SHA256 or
                source.sha256(runs[0][2]) != EXPECTED_OUTPUTS_SHA256):
            raise ValueError("numerical baseline differs; review host UCRT/build instead of accepting new bytes")
        manager.verify()
        result = {"schema": "tll-p99-row20-native-v1", "classification": "ISOLATED_NATIVE",
                  "executable_sha256": digest, "bridge_sha256": source.sha256(args.bridge.read_bytes()),
                  "game_launched_or_attached": False, "full_nadine_chain_tested": False,
                  "row157_live_profile_unresolved": True, "runs": [run[0] for run in runs],
                  "mxcsr_outputs_equal": True, "exception_recovery_supported": False,
                  "reviewed_numeric_baseline_matches": True,
                  "dependency_slots": [f"{SQRTF_IAT:X}:host UCRT sqrtf", f"{MANAGER_GLOBAL:X}:owned zero manager"],
                  "inherited_initialization_copies": ["142BF0700 -> 146C71CE0:32 bytes", "142BF0740 -> 146C71D00:32 bytes"],
                  "ucrt_sha256": source.sha256((Path(os.environ["SystemRoot"]) / "System32" / "ucrtbase.dll").read_bytes()),
                  "preflight": [{key: value for key, value in report.items() if key != "rows"} for report in reports]}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.with_suffix(".records.bin").write_bytes(runs[0][1])
        args.output.with_suffix(".outputs.bin").write_bytes(runs[0][2])
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
    finally:
        if image:
            image.close()
        manager.close()


def floats(data: bytes, offset: int, count: int) -> tuple[float, ...]:
    return struct.unpack(f"<{count}f", authored.extent(data, offset, count * 4, "float input"))


def determinant(matrix: tuple[float, ...]) -> float:
    # Authored inverse-global Mat34 consists of three row-major float4 rows.
    a, b, c = matrix[0:3]
    d, e, f = matrix[4:7]
    g, h, i = matrix[8:11]
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)


def preflight_mapping(mapping: source.RetargetMap,
                      src: source.SkeletonAsset, dst: source.SkeletonAsset,
                      src_bytes: bytes, dst_bytes: bytes) -> dict:
    """Validate ALL resolved builder iterations, not only the 21 P99 outputs."""
    if mapping.spec.flags != 0:
        raise ValueError("IK allocation branches are not admitted")
    records = source.resolve_runtime_records(mapping, src, dst)
    inputs = (authored.skeleton_inputs(src_bytes, src), authored.skeleton_inputs(dst_bytes, dst))
    determinants = []
    rows = []
    for record in records:
        source_parent = inputs[0]["parents"][record.source_global]
        target_parent = inputs[1]["parents"][record.target_global]
        root_pair = source_parent == target_parent == -1
        if (source_parent < 0 or target_parent < 0) and not root_pair:
            raise ValueError("unmatched negative parents are not admitted")
        parameters = (authored.parameter_index(src, record.source_global),
                      authored.parameter_index(dst, record.target_global))
        lengths = []
        for skel, data, item, joint, parameter in zip(
                (src, dst), (src_bytes, dst_bytes), inputs,
                (record.source_global, record.target_global), parameters):
            parent = item["parents"][joint]
            for matrix_joint in (joint, parent):
                matrix = floats(data, item["global_matrix_offset"] + 48 * matrix_joint, 12)
                if matrix_joint == -1 and root_pair:
                    # The original builder loads/inverts the preceding bytes,
                    # then CF7C5..CF937 discards BOTH inverses and emits identity.
                    # They need readable full-raw extent, not determinant1.
                    continue
                det = determinant(matrix)
                if not math.isfinite(det) or abs(det) < 1e-6 or abs(det) > 1e6:
                    raise ValueError(f"{skel.spec.entry_name} singular/unbounded matrix {matrix_joint}: {det}")
                determinants.append(det)
            translation = floats(data, item["reference_offset"] + 48 * parameter + 0x20, 3)
            if not all(math.isfinite(value) and abs(value) <= 10000 for value in translation):
                raise ValueError("unbounded compressed translation")
            lengths.append(math.sqrt(sum(value * value for value in translation)))
        global_t_offset = authored.builder_source_translation(src_bytes, inputs[0], record.source_global)["disk_offset"]
        global_t = floats(src_bytes, global_t_offset, 3)
        if not all(math.isfinite(value) and abs(value) <= 65504 for value in global_t):
            raise ValueError(f"row{mapping.spec.row_index} unrepresentable source-global T at{global_t_offset:X}")
        # This is a conservative bound, not a scalar implementation of native
        # float rsqrt/rounding/epsilon. Zero-length handling is audited separately.
        ratio = lengths[1] / lengths[0] if lengths[0] else 0.0
        if not math.isfinite(ratio) or ratio > 65504:
            raise ValueError("bone-length ratio can overflow packed half")
        rows.append({"source_global": record.source_global, "target_global": record.target_global,
                     "source_parameter": parameters[0], "target_parameter": parameters[1],
                     "mode": record.mode, "source_length": lengths[0], "target_length": lengths[1],
                     "conservative_ratio": ratio, "source_global_t_offset": global_t_offset})
    return {"row_index": mapping.spec.row_index, "profile": f"{mapping.spec.profile:08X}",
            "target_basis": "authored bind pose; conditional only" if mapping.spec.profile else "authored bind pose; profile zero",
            "authored_count": mapping.spec.authored_count, "resolved_count": len(records),
            "matrix_checks": len(determinants), "determinant_abs_min": min(map(abs, determinants)),
            "determinant_abs_max": max(map(abs, determinants)),
            "max_conservative_ratio": max(row["conservative_ratio"] for row in rows), "rows": rows}


def load_inputs(archive: Path, dc_archive: Path) -> dict:
    audit = source.native.load_audit_module()
    actor = audit.Psarc(archive, None)
    specs = (source.PLAYER_SKELETON, source.CHLOE_SKELETON, source.NADINE_SKELETON)
    data = {spec.entry_name: actor.extract(spec.entry_name) for spec in specs}
    skels = tuple(source.load_skeleton(data[spec.entry_name], spec) for spec in specs)
    retarget_data = audit.Psarc(dc_archive, None).extract(source.RETARGET_ENTRY_NAME)
    table, count = source.parse_retarget_table(retarget_data)
    maps = tuple(source.resolve_retarget(retarget_data, table, count, spec)
                 for spec in (source.PLAYER_TO_CHLOE, source.CHLOE_TO_NADINE))
    metadata, references, tails = authored.generate(actor.extract(source.ANIMATION_ENTRY_NAME), data,
        retarget_data, actor.extract(authored.PROFILE_ENTRY))
    return {"data": data, "skels": skels, "maps": maps, "retarget_data": retarget_data,
            "metadata": metadata, "references": references, "tails": tails}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    base = source.native.DEFAULT_GAME / "Uncharted4_data" / "build" / "pc" / "thelostlegacy"
    parser.add_argument("--archive", type=Path, default=base / "actor77.psarc")
    parser.add_argument("--dc-archive", type=Path, default=base / "dc1.psarc")
    parser.add_argument("--exe", type=Path, default=source.native.DEFAULT_GAME / "tll.exe")
    parser.add_argument("--output", type=Path, default=ROOT / "reports" / "p99-retarget-preflight.json")
    parser.add_argument("--bridge", type=Path, default=ROOT / "scanner" / "build" / "tll_p99_native_math_bridge.dll")
    parser.add_argument("--numeric-row20", action="store_true", help="run the audited first stage in a terminal-fault child")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    with args.exe.open("rb") as executable:
        digest = hashlib.file_digest(executable, "sha256").hexdigest()
    if digest != source.native.EXPECTED_EXE_SHA256:
        raise ValueError(f"unsupported executable {digest}")
    loaded = load_inputs(args.archive, args.dc_archive)
    reports = []
    for index, mapping in enumerate(loaded["maps"]):
        src, dst = loaded["skels"][index:index + 2]
        reports.append(preflight_mapping(mapping, src, dst,
            loaded["data"][src.spec.entry_name], loaded["data"][dst.spec.entry_name]))
    if args.worker:
        if not args.numeric_row20:
            raise ValueError("worker requires explicit numerical mode")
        run_worker(args, loaded, reports, digest)
        return
    if args.numeric_row20:
        # No recovery/retry inside a faulted mapped image. The parent is never
        # the game and the child receives no game handles or input facilities.
        command = [sys.executable, str(Path(__file__).resolve()), "--worker", "--numeric-row20",
                   "--archive", str(args.archive), "--dc-archive", str(args.dc_archive),
                   "--exe", str(args.exe), "--bridge", str(args.bridge), "--output", str(args.output)]
        completed = subprocess.run(command, capture_output=True, text=True, timeout=45,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        if completed.returncode:
            raise RuntimeError(f"isolated helper failed ({completed.returncode:#x}): {completed.stderr[-4000:]}")
        print(completed.stdout, end="")
        return
    result = {"schema": "tll-p99-retarget-preflight-v1", "executable_sha256": digest,
              "tll_calls": 0, "game_launched_or_attached": False,
              "row157_live_profile_unresolved": True, "mappings": reports}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"tll_calls": 0, "mappings": [{key: value for key, value in report.items()
                                                    if key != "rows"} for report in reports]}, indent=2))


if __name__ == "__main__":
    main()
