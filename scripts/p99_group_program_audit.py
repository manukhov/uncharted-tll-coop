#!/usr/bin/env python3
"""Audit the exact C39 group-local P99 consumer programs without running tll.

This is a build-pinned STATIC contract, not runtime admission. Operator layouts
come from their native handlers; see the 2026-09-17 COOP_STATUS entry. The audit
checks complete opcode coverage, encoded base selection, writes to selected
JointParams, and the group-0 palm -> shared scratch -> group-1 parent handoff.
It does not establish live owner lifetime, bindings, or visual correctness.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source


@dataclass(frozen=True)
class Operator:
    ordinal: int
    relative: int
    opcode: int
    payload: tuple[int, ...]


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def operators(data: bytes, raw: int, relative: int) -> tuple[Operator, ...]:
    cursor = raw + relative
    result = []
    for ordinal in range(256):
        header = u16(data, cursor)
        opcode, words = header & 0xFF, header >> 8
        if opcode == 0:
            if header != 0:
                raise ValueError("unexpected terminator payload")
            return tuple(result)
        payload = struct.unpack_from(f"<{words}H", data, cursor + 2)
        result.append(Operator(ordinal, cursor - raw, opcode, payload))
        cursor += 2 + 2 * words
    raise ValueError("unterminated joint program")


def writes(data: bytes, program: int, op: Operator) -> list[tuple[int, int]]:
    """Return every encoded destination and byte width for the pinned opcodes.

    The expanded tail can be written by native operators. Checking only the
    parameter prefix, or treating an unimplemented opcode as read-only, would
    not establish that the selected palm/prop/finger inputs survive.
    """
    p = op.payload
    base_index = 2 if op.opcode == 52 else 1
    expected_words = 4 if op.opcode in (47, 52) else 3
    if len(p) != expected_words or p[base_index] != 3 or p[base_index + 1] & 3:
        raise ValueError(f"operator {op.ordinal}: unexpected encoded bases")
    desc = program + (p[base_index + 1] & 0xFFFC)
    count = p[0]
    out = []

    if op.opcode in (40, 53, 54):
        for i in range(count * 4 if op.opcode == 40 else count):
            destination = u16(data, desc + 4 * i)
            if op.opcode == 40:
                destination = (4 * destination) | 1
            out.append((destination, {40: 64, 53: 16, 54: 4}[op.opcode]))
    elif op.opcode in (41, 44):
        fields = ((10, 12, 14, 26, 28, 30, 42, 44, 46, 58, 60, 62)
                  if op.opcode == 41 else (8, 24, 40, 56))
        for batch in range((count + 3) // 4):
            for field in fields:
                dest = u16(data, desc + 64 * batch + field)
                if dest != 0xFFFF:
                    out.append((dest, 4 if op.opcode == 41 else 16))
    elif op.opcode == 47:
        out.extend((u16(data, desc + 4 * i + 2), 4) for i in range(count))
    elif op.opcode == 49:
        out.extend((u16(data, desc + 4 * i + 2), 16) for i in range(count))
        header = desc + 12 * count
        pair_count, batch_offset, batch_count = struct.unpack_from("<III", data, header)
        out.extend((u16(data, header + 16 + 2 * i), 16) for i in range(pair_count))
        for batch in range((batch_count + 3) // 4):
            out.extend((u16(data, header + batch_offset + 16 * batch + field), 16)
                       for field in (2, 6, 10, 14))
    elif op.opcode == 50:
        out.extend((u16(data, desc + 8 * i + 6), 16) for i in range(count))
        modes = desc + 24 * count
        aux_count = u32(data, modes + 4 * count)
        addresses = modes + 4 * (count + 4)
        for record in range(aux_count):
            out.extend((u16(data, addresses + 8 * record + 2 * field), 16)
                       for field in range(4))
    elif op.opcode == 51:
        out.extend((u16(data, desc + 4 * i + 2), 16) for i in range(count))
        header = desc + 4 * count + 2 * ((count + 7) & 0xFFF8)
        for record in range(u32(data, header)):
            out.extend((u16(data, header + 16 + 16 * record + 4 * pair), 16)
                       for pair in range(4))
    elif op.opcode == 52:
        for i in range(count):
            out.extend((u16(data, desc + 16 * i + 4 * field), 16)
                       for field in range(4))
        desc += 16 * count
        for i in range(p[1]):
            out.extend((u16(data, desc + 16 * i + 4 * field), 4)
                       for field in range(4))
    elif op.opcode in (128, 129):
        out.extend((u32(data, desc + 64 * i + 60), 4)
                   for i in range(count))
    elif op.opcode in (130, 131, 132):
        count_field, offset_field, stride, dest_field = {
            # Op132: 141A95FC5 reads u16(descriptor+0x46), not +0x4C.
            130: (8, 22, 4, 0), 131: (4, 10, 8, 4), 132: (4, 70, 8, 4),
        }[op.opcode]
        output = desc + u16(data, desc + offset_field)
        out.extend((u32(data, output + stride * i + dest_field), 4)
                   for i in range(u32(data, desc + count_field)))
    else:
        raise ValueError(f"unimplemented opcode {op.opcode}")
    return out


def audit(path: Path) -> None:
    data = path.read_bytes()
    skeleton = source.load_skeleton(data, source.NADINE_SKELETON)
    raw = skeleton.spec.raw_offset
    program_table = raw + u32(data, raw + 0x34)
    expected_histograms = (
        {40: 10, 41: 1, 44: 3, 47: 6, 49: 4, 50: 3, 51: 3, 52: 1,
         53: 1, 54: 1, 128: 8, 129: 4, 130: 2, 131: 2, 132: 2},
        {40: 1, 53: 1},
    )
    selected = ((28, 30), tuple(range(1, 38, 2)))
    programs = []
    for group_index in range(2):
        group = raw + u32(data, raw + 0x2C) + 32 * group_index
        if data[group + 0x0F] != 1:
            raise ValueError("expected one program per target group")
        relative = u32(data, program_table + 8 * u16(data, group + 6) + 4)
        if relative != (0x53E0, 0x8D90)[group_index]:
            raise ValueError("program table target changed")
        parsed = operators(data, raw, relative)
        if dict(Counter(op.opcode for op in parsed)) != expected_histograms[group_index]:
            raise ValueError("operator inventory changed")
        destination_count = 0
        for op in parsed:
            decoded = writes(data, raw + relative, op)
            if op.opcode == 132:
                expected = {
                    39: (0xEC0, 0xEC4, 0xEC8),
                    40: (0xEF0, 0xEF4, 0xEF8, 0x2E2, 0x34E, 0x3B2),
                }.get(op.ordinal)
                if group_index != 0 or expected is None or decoded != [(v, 4) for v in expected]:
                    raise ValueError("custom132 destination table changed")
            for encoded, width in decoded:
                destination_count += 1
                if encoded & 3:
                    continue
                # Custom destinations are full 32-bit encoded operands.
                offset = encoded & (0xFFFFFFF0 if width >= 16 else 0xFFFFFFFC)
                if any(offset < 48 * (joint + 1) and offset + width > 48 * joint
                       for joint in selected[group_index]):
                    raise ValueError(f"operator {op.opcode} overwrites selected JP")
        if destination_count != (870, 100)[group_index]:
            raise ValueError("decoded destination coverage changed")

        # Process cmd17 uses a separate base table: original JP, optional
        # scalar bank, auxiliary, scratch. It rounds its scalar pairs to four.
        scalar_count = u16(data, group + 0x0A)
        scalar_pairs = (raw + u32(data, raw + 0x2C)
                        + 16 * (2 * ((u16(data, group + 8) + 3) // 4)
                                + (u16(data, group + 0x16) + 3) // 4)
                        + u16(data, group + 0x0C))
        if scalar_count != (27, 0)[group_index]:
            raise ValueError("cmd17 scalar count changed")
        if group_index == 0 and scalar_pairs != 0x541E0:
            raise ValueError("cmd17 scalar table moved")
        for i in range((scalar_count + 3) & ~3):
            destination, origin = struct.unpack_from("<HH", data, scalar_pairs + 4 * i)
            if destination & 3 != 1 or origin & 3 != 2:
                raise ValueError("cmd17 accesses a non-scalar-bank base")
        programs.append((relative, parsed))
        print(f"group={group_index} param={skeleton.groups[group_index].parameter_count} "
              f"expanded={skeleton.groups[group_index].workspace_count} "
              f"program=0x{raw+relative:X} operators={len(parsed)} "
              f"decoded_destinations={destination_count} selected_JP_writes=0 "
              f"cmd17_scalar_pairs={scalar_count}")

    relative0, ops0 = programs[0]
    export = ops0[49]
    if (export.opcode, export.relative, export.payload) != (53, 0x5576, (248, 3, 0x35A0)):
        raise ValueError("group-0 export operator changed")
    pairs = raw + relative0 + export.payload[2]
    actual = tuple(struct.unpack_from("<HH", data, pairs + 4 * i)
                   for i in range(125, 129))
    if actual != ((0xFD3, 0x741), (0xFE3, 0x751), (0xFF3, 0x761), (0x1003, 0x771)):
        raise ValueError("palm matrix export changed")
    relative1, ops1 = programs[1]
    if ops1[0].opcode != 40 or ops1[0].payload != (10, 3, 0x20):
        raise ValueError("group-1 propagation operator changed")
    records = raw + relative1 + 0x20
    right_roots = []
    for index in range(40):
        k, parent = struct.unpack_from("<HH", data, records + 4 * index)
        if parent == 0xFDB:
            right_roots.append(k // 16 - 1)
    if right_roots != [1, 9, 17, 25, 33]:
        raise ValueError(f"right-finger shared parents changed: {right_roots}")
    print("palm28 W+0x740..0x77F -> scratch+0xFD0..0x100F -> "
          "finger roots1/9/17/25/33; all53 operators covered")
    print("STATIC PASS; no native execution, runtime admission or visual claim")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset", type=Path,
                        default=source.ROOT / "reports/actor_packages/nadine-dlc-base.pak")
    audit(parser.parse_args().asset)


if __name__ == "__main__":
    main()
