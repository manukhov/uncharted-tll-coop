#!/usr/bin/env python3
"""Full production P99 source preparation on owned fixtures, NEVER a game.

Execution requires --execute and takes place in a timeout-bounded child. Timings
include audit instrumentation and uncontended owned registries; they cannot
admit a live 1 ms budget, resource lifetime, hook delivery or visible grip.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys

import p99_native_retarget_audit as native

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_ROWS_SHA256 = "c3613114c77ed16949d7de7d374131e86a4ab5ce57a3695a8f0b9ba2bee122e7"
PHASES = ("select_ticks", "owner_ticks", "capture_ticks", "math_ticks",
          "postcheck_ticks", "seal_ticks")
COUNTERS = ("virtual_queries", "resolve_calls", "find_calls", "retarget_calls",
            "blend_calls", "rejected_calls")


class Stats(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ("size", "version", "completed_iterations", "failed_stage")] + [
        (name, ctypes.c_uint64) for name in
        ("qpc_frequency", "total_ticks") + PHASES + COUNTERS + ("virtual_query_ticks",)]


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_stats(stats: Stats, output: bytes) -> dict:
    if stats.size != ctypes.sizeof(Stats) or stats.version != 1:
        raise ValueError("bridge statistics ABI differs")
    if stats.completed_iterations != 1 or stats.failed_stage or not stats.qpc_frequency:
        raise ValueError("preparation did not complete one full successful iteration")
    if (stats.resolve_calls, stats.find_calls, stats.retarget_calls,
            stats.blend_calls, stats.rejected_calls) != (12, 12, 42, 21, 0):
        raise ValueError("native call counts differ from the complete preparation path")
    if not stats.virtual_queries or not stats.total_ticks:
        raise ValueError("missing page checks or total timing")
    if not 0 < stats.virtual_query_ticks <= stats.total_ticks:
        raise ValueError("page-query timing is absent or outside total preparation time")
    if any(getattr(stats, name) > stats.total_ticks for name in PHASES):
        raise ValueError("inclusive phase exceeds the enclosing preparation time")
    if len(output) != 1008 or hashlib.sha256(output).hexdigest() != EXPECTED_ROWS_SHA256:
        raise ValueError("complete preparation output differs from the pinned AGZ rows")
    return {"total_ms": stats.total_ticks * 1000 / stats.qpc_frequency,
            "virtual_query_ms": stats.virtual_query_ticks * 1000 / stats.qpc_frequency,
            "phases_ms": {name.removesuffix("_ticks"): getattr(stats, name) * 1000 /
                          stats.qpc_frequency for name in PHASES},
            "counters": {name: getattr(stats, name) for name in COUNTERS}}


def distribution(values: list[float]) -> dict:
    if not values or any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("invalid timing sample")
    ordered = sorted(values)
    return {"count": len(values), "min_ms": ordered[0],
            "median_ms": statistics.median(ordered),
            "p95_ms": ordered[math.ceil(len(ordered) * .95) - 1],
            "max_ms": ordered[-1], "over_1ms": sum(value > 1 for value in values)}


def bind_bridge(path: Path):
    bridge = ctypes.WinDLL(str(path.resolve()))
    bridge.P99PrepareAudit.argtypes = [ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint32,
        ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(Stats)]
    bridge.P99PrepareAudit.restype = ctypes.c_int
    return bridge


def check_negative_cases(bridge, image, fixture, output):
    cases = []
    for field in ("weapon_generation", "fg_raw_joint_buffer", "fg_local_pose", "player_look",
                  "record20_numeric", "record157_numeric", "record157_last_numeric", "lock",
                  "record20_padding", "record157_padding"):
        address, width = fixture.fields[field]
        original = ctypes.string_at(address, width)
        payload = (bytes(width) if field == "fg_local_pose" else
                   bytes([original[0] ^ 1]) + original[1:])
        with fixture.patch(address, payload):
            stats = Stats(size=ctypes.sizeof(Stats), version=1)
            result = bridge.P99PrepareAudit(image.address, fixture.job, 1,
                output.address, output.size, ctypes.byref(stats))
            if field.endswith("_padding"):
                if result != 0:
                    raise ValueError(f"native-unwritten padding unexpectedly refused: {field}")
                validate_stats(stats, output.read())
            else:
                if (result != -5 or stats.completed_iterations != 0 or
                        stats.retarget_calls != 0 or stats.blend_calls != 0 or
                        output.read() != bytes(output.size)):
                    raise ValueError(f"negative case did not refuse before all math: {field}")
                if field.startswith("record") and stats.failed_stage != 8:
                    raise ValueError(f"numeric drift did not hit exact operand identity: {field}")
            output.verify()
            cases.append({"field": field, "result": result, "stage": stats.failed_stage,
                          "retarget_calls": stats.retarget_calls, "blend_calls": stats.blend_calls})
        fixture.verify()
    return cases


def run_worker(args) -> dict:
    from p99_preparation_fixture import PreparationFixture
    if digest(args.bridge) != args.bridge_sha256:
        raise ValueError("bridge changed after parent admission")
    if digest(args.math_bridge) != args.math_bridge_sha256:
        raise ValueError("CPU/MXCSR bridge changed after parent admission")
    if digest(args.exe) != native.source.native.EXPECTED_EXE_SHA256:
        raise ValueError("unsupported game binary")
    loaded = native.load_inputs(args.archive, args.dc_archive)
    bridge = bind_bridge(args.bridge)
    cpu = ctypes.WinDLL(str(args.math_bridge.resolve()))
    cpu.P99AuditCpuReady.restype = ctypes.c_uint32
    cpu.P99AuditMxcsr.argtypes = [ctypes.c_uint32]
    cpu.P99AuditMxcsr.restype = ctypes.c_uint32
    if not cpu.P99AuditCpuReady():
        raise RuntimeError("CPU/OS AVX/F16C/AVX2 admission failed")
    manager = native.OwnedBuffer(bytes(0xE0))
    image = None
    runs = []
    try:
        image = native.RetargetImage(args.exe, manager)
        for count in (175, 4096):
            with PreparationFixture(image, loaded, table_count=count) as fixture:
                output = native.OwnedBuffer(bytes(1008), False)
                try:
                    for mode in (0x1F80, 0x9FC0):
                        old_mode = cpu.P99AuditMxcsr(mode)
                        try:
                            samples = []
                            for _ in range(args.samples + 1):
                                stats = Stats(size=ctypes.sizeof(Stats), version=1)
                                result = bridge.P99PrepareAudit(image.address, fixture.job, 1,
                                    output.address, output.size, ctypes.byref(stats))
                                if result != 0:
                                    raise RuntimeError(f"prepare refused result={result} stage={stats.failed_stage} "
                                                       f"counts={[(name, getattr(stats, name)) for name in COUNTERS]}")
                                samples.append(validate_stats(stats, output.read()))
                                output.verify()
                            fixture.verify()
                            counter_set = {tuple(item["counters"].values()) for item in samples}
                            if len(counter_set) != 1:
                                raise ValueError("operation counts changed across identical fixture samples")
                            runs.append({"table_count": count, "mxcsr": hex(mode),
                                "first_sample": samples[0],
                                "warm_total": distribution([item["total_ms"] for item in samples[1:]]),
                                "warm_virtual_query": distribution([item["virtual_query_ms"] for item in samples[1:]]),
                                "warm_phases": {name.removesuffix("_ticks"): distribution([
                                    item["phases_ms"][name.removesuffix("_ticks")] for item in samples[1:]])
                                    for name in PHASES},
                                "counters_per_preparation": samples[0]["counters"],
                                "samples": samples,
                                "fixture": fixture.metadata})
                        finally:
                            cpu.P99AuditMxcsr(old_mode)
                    runs[-1]["negative_cases"] = check_negative_cases(bridge, image, fixture, output)
                finally:
                    output.close()
        manager.verify()
    finally:
        if image is not None:
            image.close()
        manager.close()
    return {"schema": "tll-p99-owned-preparation-v1", "classification": "ISOLATED_NATIVE",
            "executable_sha256": native.source.native.EXPECTED_EXE_SHA256,
            "bridge_sha256": args.bridge_sha256, "math_bridge_sha256": args.math_bridge_sha256,
            "final_rows_sha256": EXPECTED_ROWS_SHA256,
            "game_launched_or_attached": False, "live_budget_admitted": False,
            "visual_grip_confirmed": False, "live_profile_absence_proven": False,
            "full_production_prepare_body": True,
            "timing_scope": "QPC before P99GroupJob initialization through prepare_p99_group_values return; audit instrumentation included",
            "excluded": ["hook ingress/FLS/native animation job", "post-prepare budget charge/logging",
                         "overlay application", "live registry contention/lifetime/page layout"],
            "phase_timings": "inclusive and nested; do not sum; first sample only first per layout/mode, not all cold-process",
            "exception_recovery_supported": False, "runs": runs}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    base = native.source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
    parser.add_argument("--archive", type=Path, default=base / "actor77.psarc")
    parser.add_argument("--dc-archive", type=Path, default=base / "dc1.psarc")
    parser.add_argument("--exe", type=Path, default=native.source.native.DEFAULT_GAME / "tll.exe")
    parser.add_argument("--bridge", type=Path, default=ROOT / "scanner/build/tll_p99_preparation_bridge.dll")
    parser.add_argument("--math-bridge", type=Path, default=ROOT / "scanner/build/tll_p99_native_math_bridge.dll")
    parser.add_argument("--output", type=Path, default=ROOT / "reports/p99-preparation-ahb.json")
    parser.add_argument("--samples", type=int, default=128)
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--bridge-sha256", help=argparse.SUPPRESS)
    parser.add_argument("--math-bridge-sha256", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 1 <= args.samples <= 512:
        parser.error("samples must be between1 and512")
    if args.worker:
        if not args.execute or not args.bridge_sha256 or not args.math_bridge_sha256:
            parser.error("worker requires explicit execution and parent-pinned helpers")
        result = run_worker(args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({key: value for key, value in result.items() if key != "runs"} |
            {"runs": [{key: value for key, value in run.items() if key not in ("samples", "fixture")}
                      for run in result["runs"]]}, indent=2))
        return
    if digest(args.exe) != native.source.native.EXPECTED_EXE_SHA256:
        raise ValueError("unsupported game binary")
    from p99_preparation_fixture import validate_artifacts
    validate_artifacts()
    if not args.execute:
        print(json.dumps({"classification": "STATIC_PREFLIGHT", "native_calls": 0,
                          "execution_requires": "--execute", "runtime_admission": "NOT_EARNED"}))
        return
    command = [sys.executable, str(Path(__file__).resolve()), "--worker", "--execute",
               "--bridge-sha256", digest(args.bridge), "--math-bridge-sha256", digest(args.math_bridge)]
    for option in ("archive", "dc_archive", "exe", "bridge", "math_bridge", "output", "samples"):
        command.extend(["--" + option.replace("_", "-"), str(getattr(args, option))])
    completed = subprocess.run(command, capture_output=True, text=True, timeout=60,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    if completed.returncode:
        raise RuntimeError(f"isolated helper failed ({completed.returncode:#x}): {completed.stderr[-5000:]}")
    print(completed.stdout, end="")


if __name__ == "__main__":
    main()
