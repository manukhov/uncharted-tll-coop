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

    offsets = set(parse_ints("TLL_IDA_MEMBER_OFFSETS"))
    ranges = parse_ints("TLL_IDA_RANGES")
    if not offsets or len(ranges) != 2:
        print("Set TLL_IDA_MEMBER_OFFSETS and TLL_IDA_RANGES=start,end")
        ida_pro.qexit(1)
        return

    start, end = ranges
    for function_ea in idautils.Functions(start, end):
        function = ida_funcs.get_func(function_ea)
        if not function:
            continue
        hits = []
        for ea in idautils.FuncItems(function_ea):
            insn = ida_ua.insn_t()
            if ida_ua.decode_insn(insn, ea) <= 0:
                continue
            for operand in insn.ops:
                if operand.type == ida_ua.o_void:
                    break
                if operand.type in (ida_ua.o_displ, ida_ua.o_phrase) and operand.addr in offsets:
                    hits.append((ea, operand.addr))
                    break
        if hits:
            name = ida_name.get_ea_name(function_ea) or "<unnamed>"
            print(f"FUNCTION 0x{function_ea:X} {name}")
            for ea, offset in hits:
                line = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
                print(f"  0x{ea:X} member=0x{offset:X}: {line}")
            print("")

    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
