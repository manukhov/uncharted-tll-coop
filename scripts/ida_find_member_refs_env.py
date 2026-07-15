"""Find direct structure-member displacements in instructions.

Environment:
  TLL_IDA_OFFSETS   Space/comma-separated offsets (for example 0x240 0x248).
  TLL_IDA_OUTPUT    Optional output file.
  TLL_IDA_START/END Optional address bounds; defaults to all functions.
"""

import os
import sys

import ida_funcs
import ida_name
import ida_pro
import ida_ua
import idautils
import idc


def parse_ints(name):
    raw = os.environ.get(name, "")
    return [int(part, 0) for part in raw.replace(",", " ").split() if part]


def instruction_displacements(ea, offsets):
    insn = ida_ua.insn_t()
    if ida_ua.decode_insn(insn, ea) == 0:
        return []
    hits = []
    for operand in insn.ops:
        if operand.type == ida_ua.o_void:
            break
        if operand.type == ida_ua.o_displ and operand.addr in offsets:
            hits.append(operand.addr)
    return hits


def main():
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")

    offsets = parse_ints("TLL_IDA_OFFSETS")
    if not offsets:
        print("No TLL_IDA_OFFSETS provided")
        ida_pro.qexit(1)
        return

    start = int(os.environ.get("TLL_IDA_START", "0"), 0)
    end = int(os.environ.get("TLL_IDA_END", hex(idc.BADADDR)), 0)
    for func_ea in idautils.Functions(start, end):
        hits = []
        for ea in idautils.FuncItems(func_ea):
            for offset in instruction_displacements(ea, offsets):
                hits.append((ea, offset, idc.generate_disasm_line(ea, 0) or ""))
        if not hits:
            continue
        name = ida_name.get_ea_name(func_ea) or f"sub_{func_ea:X}"
        function = ida_funcs.get_func(func_ea)
        print(f"FUNCTION 0x{func_ea:X} {name} end=0x{function.end_ea:X}")
        for ea, offset, line in hits:
            print(f"  0x{ea:X} member=0x{offset:X}: {line}")
        print("")

    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
