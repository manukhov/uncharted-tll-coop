"""Find exact 32-bit values in IDA instructions and loaded segment bytes.

Environment:
  TLL_IDA_VALUES  Space/comma-separated u32 values.
  TLL_IDA_OUTPUT  Optional output file.
  TLL_IDA_RAW     Set to 0 to skip the raw loaded-byte scan (default: 1).

The instruction pass reports only explicit immediate operands.  The raw pass is
kept separate because a byte match is evidence of storage, not of a code xref.
"""

import os
import struct
import sys

import ida_bytes
import ida_funcs
import ida_name
import ida_pro
import ida_segment
import ida_ua
import idautils
import idc


def parse_values() -> set[int]:
    raw = os.environ.get("TLL_IDA_VALUES", "")
    return {
        int(part, 0) & 0xFFFFFFFF
        for part in raw.replace(",", " ").split()
        if part
    }


def scan_immediates(values: set[int]) -> int:
    matches = 0
    for function_ea in idautils.Functions():
        function_matches = []
        for ea in idautils.FuncItems(function_ea):
            instruction = ida_ua.insn_t()
            if not ida_ua.decode_insn(instruction, ea):
                continue
            for operand_index, operand in enumerate(instruction.ops):
                if operand.type == ida_ua.o_void:
                    break
                if operand.type != ida_ua.o_imm:
                    continue
                value = operand.value & 0xFFFFFFFF
                if value in values:
                    function_matches.append((ea, operand_index, value))
        if not function_matches:
            continue
        name = ida_name.get_ea_name(function_ea) or f"sub_{function_ea:X}"
        print(f"FUNCTION 0x{function_ea:X} {name}")
        for ea, operand_index, value in function_matches:
            line = idc.generate_disasm_line(ea, 0) or ""
            print(
                f"  0x{ea:X} operand={operand_index} "
                f"value=0x{value:08X}: {line}"
            )
            matches += 1
    print(f"immediate_matches={matches}")
    return matches


def scan_raw(values: set[int]) -> int:
    patterns = {value: struct.pack("<I", value) for value in values}
    matches = 0
    chunk_size = 16 * 1024 * 1024
    for segment_ea in idautils.Segments():
        segment = ida_segment.getseg(segment_ea)
        if segment is None:
            continue
        segment_name = ida_segment.get_segm_name(segment)
        cursor = segment.start_ea
        tail = b""
        while cursor < segment.end_ea:
            size = min(chunk_size, segment.end_ea - cursor)
            current = ida_bytes.get_bytes(cursor, size) or b""
            combined = tail + current
            combined_ea = cursor - len(tail)
            for value, pattern in patterns.items():
                offset = combined.find(pattern)
                while offset >= 0:
                    ea = combined_ea + offset
                    # A three-byte tail cannot contain a complete four-byte
                    # pattern, so every combined-buffer match is new.  Keeping
                    # it is necessary for values that straddle chunk boundaries.
                    function = ida_funcs.get_func(ea)
                    owner = ""
                    if function is not None:
                        name = ida_name.get_ea_name(function.start_ea)
                        owner = f" function=0x{function.start_ea:X}/{name}"
                    print(
                        f"RAW 0x{ea:X} value=0x{value:08X} "
                        f"segment={segment_name}{owner}"
                    )
                    matches += 1
                    offset = combined.find(pattern, offset + 1)
            tail = combined[-3:]
            cursor += size
    print(f"raw_matches={matches}")
    return matches


def main() -> None:
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")

    values = parse_values()
    if not values:
        print("No TLL_IDA_VALUES provided")
        ida_pro.qexit(1)
        return

    print("values=" + ",".join(f"0x{value:08X}" for value in sorted(values)))
    scan_immediates(values)
    if os.environ.get("TLL_IDA_RAW", "1") != "0":
        scan_raw(values)
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
