#!/usr/bin/env python3
"""STATIC matrix-production audit of two pinned candidate NPC skeletons.

Read-only PSARC mapping, in-memory decompression, no retail code execution.
This proves storage/dataflow in authored programs, not live skeleton selection,
freshness of JointParams inputs, native command execution, cache valid-bit
ordering, object lifetime, physics refresh, or enemy-write admission.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import mmap
from pathlib import Path
import struct

from native_anim_pose_audit import DEFAULT_GAME  # Constant only; never MappedImage.
from p99_group_program_audit import operators, propagation_records, writes
from psarc_asset_audit import Psarc


DEFAULT_ARCHIVE = DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy/actor77.psarc"
COLLIDER_JOINTS = (3, 4, 5, 6, 19, 22, 23, 24, 25, 34)
EXPECTED_INVENTORY = {
    40: 11, 41: 1, 44: 2, 47: 4, 49: 6, 50: 5, 51: 2, 52: 1,
    53: 1, 54: 1, 128: 6, 129: 3, 130: 1, 131: 3,
}


@dataclass(frozen=True)
class Candidate:
    entry: str
    raw: int
    skeleton_id: int
    size: int
    sha256: str


CANDIDATES = (
    Candidate("npc-normal-base.pak", 0x5EB80, 0xBD7EEE2F, 659136,
              "a72079fd5f3b691ea790f153808c8b3a42079be355668220048028a518f1b5a8"),
    Candidate("npc-medium-base.pak", 0x1980, 0xFC65A17A, 258720,
              "d8b5452fc413d977c13c0f33729155d09ff24b8fcfb69052856f95c78271d13a"),
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def extent(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f"{label}: extent outside supplied bytes")
    return data[offset:offset + size]


def audit_program(program: bytes, workspace_count: int, parameter_count: int,
                  required: tuple[int, ...] = COLLIDER_JOINTS,
                  expected_inventory: dict[int, int] | None = EXPECTED_INVENTORY) -> dict:
    """Hash-independent semantic core, also used by synthetic mutation tests.

    Program bytes MUST already be restricted to their directory extent. Existing
    decoders cover destinations for these exact opcode layouts, not every input.
    Bank0 is expanded JP storage N*48 (14217BF30), NOT merely prefix P*48.
    Cmd15 allocates bank1 at141ABA8CC..8EE and initializes reserved matrix0
    at141ABA929/935/942/957; N matrices start at64*(local+1). Cmd24 exports
    N rows starting at workspace+40. This audit does not execute either command.
    Capacities of
    banks2/3 are not established here; report their decoded extents, not bounds.
    """
    if not 0 < parameter_count <= workspace_count <= 4096:
        raise ValueError("invalid workspace/parameter range")
    if len(set(required)) != len(required) or any(j < 0 or j >= workspace_count for j in required):
        raise ValueError("required collider index outside workspace range")
    try:
        ops = operators(program, 0, 0)
        inventory = dict(Counter(op.opcode for op in ops))
        if any(op.opcode not in EXPECTED_INVENTORY for op in ops):
            raise ValueError("unexpected opcode")
        if expected_inventory is not None and inventory != expected_inventory:
            raise ValueError("operator inventory changed")
        produced: dict[int, int] = {}
        counts: Counter = Counter()
        maxima: dict[int, int] = {}
        trace = []
        decoded_writes = 0
        records_total = 0
        for op in ops:
            destinations = writes(program, 0, op)
            for encoded, width in destinations:
                if width not in (4, 16, 64) or not 0 <= encoded <= 0xFFFFFFFF:
                    raise ValueError("unsupported write width or encoded address")
                bank = encoded & 3
                offset = encoded & (0xFFFFFFF0 if width >= 16 else 0xFFFFFFFC)
                limit = {0: workspace_count * 48, 1: (workspace_count + 1) * 64}.get(bank)
                if limit is not None and (offset > limit or width > limit - offset):
                    raise ValueError(f"bank{bank} write exceeds allocation bound")
                if bank == 1 and op.opcode != 40:
                    raise ValueError("non40 writer targets matrix bank")
                counts[bank] += 1
                maxima[bank] = max(maxima.get(bank, 0), offset + width)
            records = ()
            if op.opcode == 40:
                records = propagation_records(program, 0, op)
                if len(records) != len(destinations):
                    raise ValueError("propagation/write decoder disagreement")
                for (local, parent), destination in zip(records, destinations):
                    if local < 0 or local >= workspace_count:
                        raise ValueError("propagation row exceeds workspace range")
                    if destination != ((64 * (local + 1)) | 1, 64):
                        raise ValueError("propagation write width/offset changed")
                    # Pinned native encoding: bank1, scale flag8, 64-byte row.
                    if parent & 0x3F != 9:
                        raise ValueError("parent is not aligned matrix bank1 with pinned flags")
                    parent_offset = parent & 0xFFF0
                    if parent_offset + 64 > (workspace_count + 1) * 64:
                        raise ValueError("parent matrix exceeds workspace range")
                    parent_local = parent_offset // 64 - 1
                    if parent_offset and parent_local not in produced:
                        raise ValueError("matrix consumed before its parent producer")
                    if local in produced and produced[local] != parent:
                        raise ValueError("repeated propagation row changes parent")
                    produced[local] = parent
                records_total += len(records)
            decoded_writes += len(destinations)
            trace.append({"ordinal": op.ordinal, "opcode": op.opcode,
                          "payload": list(op.payload), "writes": destinations,
                          "propagation_records": records})
    except (struct.error, IndexError) as exc:
        raise ValueError("truncated program or descriptor outside declared extent") from exc
    if set(produced) != set(range(workspace_count)):
        raise ValueError("missing matrix producer for one or more workspace rows")
    if any(j not in produced for j in required):
        raise ValueError("required collider matrix lacks producer")
    trace_bytes = json.dumps(trace, sort_keys=True, separators=(",", ":")).encode("ascii")
    return {
        "workspace_count": workspace_count, "parameter_count": parameter_count,
        "operator_count": len(ops), "operator_inventory": inventory,
        "operators": [{k: row[k] for k in ("ordinal", "opcode", "payload")} for row in trace],
        "decoded_destination_count": decoded_writes,
        "decoded_writes_by_bank": dict(counts), "observed_max_end_by_bank": maxima,
        "verified_capacities_by_bank": {0: workspace_count * 48, 1: (workspace_count + 1) * 64},
        "other_banks_capacity_unproven": [2, 3], "non40_matrix_writes": 0,
        "op40_records_including_repeats": records_total,
        "unique_produced_local_rows": len(produced), "all_workspace_rows_produced": True,
        "required_collider_indices": list(required), "required_collider_rows_produced": True,
        "workspace_matrix_offset": "64*(local+1)",
        "native_export_index_shift": -1,
        "program_sha256": digest(program), "decoded_trace_sha256": digest(trace_bytes),
    }


def audit_candidate(data: bytes, candidate: Candidate) -> dict:
    if len(data) != candidate.size or digest(data) != candidate.sha256:
        raise ValueError(f"{candidate.entry}: size/SHA-256 differs from pinned candidate")
    header = extent(data, candidate.raw, 0x40, "raw skeleton header")
    if struct.unpack_from("<II", header, 4) != (candidate.skeleton_id, candidate.skeleton_id):
        raise ValueError("raw skeleton IDs changed")
    group_relative = struct.unpack_from("<I", header, 0x2C)[0]
    directory_relative = struct.unpack_from("<I", header, 0x34)[0]
    if group_relative != 0x40:
        raise ValueError("group descriptor table moved")
    group = extent(data, candidate.raw + group_relative, 32, "group0 descriptor")
    first, program_index = struct.unpack_from("<H", group, 0)[0], struct.unpack_from("<H", group, 6)[0]
    count, parameters = struct.unpack_from("<HH", group, 0x10)
    if (first, count, parameters, group[0x0F], program_index) != (0, 90, 55, 1, 0):
        raise ValueError("group0 range/program selection changed")
    directory = extent(data, candidate.raw + directory_relative, 8, "program0 directory")
    _, declared_size, relative = struct.unpack("<HHI", directory)
    if (declared_size, relative) != (0x40B0, 0x5340):
        raise ValueError("program0 declared extent/relative offset changed")
    program = extent(data, candidate.raw + relative, declared_size, "declared program0")
    result = audit_program(program, count, parameters)
    return {
        "entry": candidate.entry, "bytes": len(data), "sha256": digest(data),
        "raw_skeleton_offset": candidate.raw, "skeleton_id": f"{candidate.skeleton_id:08X}",
        "candidate_only": True, "group": 0, "first": first, "program_index": program_index,
        "program_count": group[0x0F], "program_relative": relative,
        "declared_program_bytes_u16_2": declared_size,
        "header_sha256": digest(header), "descriptor_sha256": digest(group),
        "program_directory_sha256": digest(directory), **result,
    }


def audit_archive(path: Path, oodle: Path | None = None) -> dict:
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapping:
        archive = Psarc(path, oodle, data=mapping)
        candidates = [audit_candidate(archive.extract(spec.entry), spec) for spec in CANDIDATES]
    return {"schema": 1, "classification": "STATIC", "candidate_only": True,
            "archive": str(path), "native_execution": False, "runtime_admission": False,
            "live_active_skeleton_proven": False, "jp_input_freshness_proven": False,
            "cache_valid_bit_order_proven": False, "body_refresh_proven": False,
            "candidates": candidates}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--oodle", type=Path)
    args = parser.parse_args()
    print(json.dumps(audit_archive(args.archive, args.oodle), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
