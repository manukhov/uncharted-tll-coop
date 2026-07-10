import os
import sys

import ida_lines
import ida_pro
import ida_ua
import idaapi
import idc


def main():
    start = int(os.environ["TLL_IDA_RANGE_START"], 0)
    end = int(os.environ["TLL_IDA_RANGE_END"], 0)
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")

    current = start
    while current < end:
        line = idc.generate_disasm_line(current, 0) or ""
        print(f"0x{current:X}: {ida_lines.tag_remove(line)}")
        size = idaapi.get_item_size(current)
        if size <= 0:
            instruction = ida_ua.insn_t()
            size = ida_ua.decode_insn(instruction, current)
            if size <= 0:
                break
        current += size

    ida_pro.qexit(0)


main()
