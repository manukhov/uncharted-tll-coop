"""Find immediate bit writes to memory addressed directly by a register.

This is useful for flag words at object offset zero, which ordinary member-offset
scans cannot distinguish from unrelated memory operations.
"""

import os
import sys

import ida_funcs
import ida_lines
import ida_name
import ida_pro
import ida_ua
import idautils
import idc


def parse_ints(name):
    raw = os.environ.get(name, "")
    return [int(part, 0) for part in raw.replace(",", " ").split() if part]


def main():
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")

    ranges = parse_ints("TLL_IDA_RANGES")
    immediates = set(parse_ints("TLL_IDA_IMMEDIATES"))
    if len(ranges) != 2 or not immediates:
        print("Set TLL_IDA_RANGES=start,end and TLL_IDA_IMMEDIATES")
        ida_pro.qexit(1)
        return

    start, end = ranges
    for function_ea in idautils.Functions(start, end):
        hits = []
        for ea in idautils.FuncItems(function_ea):
            insn = ida_ua.insn_t()
            if ida_ua.decode_insn(insn, ea) <= 0:
                continue
            op0, op1 = insn.ops[0], insn.ops[1]
            direct_reg_memory = (
                op0.type in (ida_ua.o_phrase, ida_ua.o_displ)
                and op0.addr == 0
            )
            immediate_match = op1.type == ida_ua.o_imm and op1.value in immediates
            if direct_reg_memory and immediate_match:
                line = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
                hits.append((ea, line))
        if hits:
            name = ida_name.get_ea_name(function_ea) or "<unnamed>"
            print(f"FUNCTION 0x{function_ea:X} {name}")
            for ea, line in hits:
                print(f"  0x{ea:X}: {line}")
            print("")

    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
