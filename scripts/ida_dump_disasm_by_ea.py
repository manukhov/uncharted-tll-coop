import os
import sys

import ida_funcs
import ida_lines
import ida_name
import ida_ua
import idaapi
import idc


def parse_targets():
    raw = os.environ.get("TLL_IDA_TARGETS", "")
    targets = []
    for part in raw.replace(",", " ").split():
        part = part.strip()
        if not part:
            continue
        targets.append(int(part, 0))
    return targets


def dump_function(ea: int) -> None:
    func = ida_funcs.get_func(ea)
    if not func:
        print(f"TARGET 0x{ea:X}: no function")
        return
    name = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
    print(f"TARGET 0x{ea:X}: {name} start=0x{func.start_ea:X} end=0x{func.end_ea:X}")
    cur = func.start_ea
    while cur < func.end_ea:
        line = idc.generate_disasm_line(cur, 0) or ""
        print(f"0x{cur:X}: {ida_lines.tag_remove(line)}")
        size = idaapi.get_item_size(cur)
        if size <= 0:
            insn = ida_ua.insn_t()
            size = ida_ua.decode_insn(insn, cur)
            if size <= 0:
                break
        cur += size
    print("")


def main():
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")
    targets = parse_targets()
    if not targets:
        print("No TLL_IDA_TARGETS provided")
        idc.qexit(1)
        return
    for ea in targets:
        dump_function(ea)
    idc.qexit(0)


if __name__ == "__main__":
    main()
