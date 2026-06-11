import os

import ida_funcs
import ida_lines
import ida_name
import ida_ua
import idaapi
import idc


TARGETS = [
    0x140BBFD70,
    0x140B44EC0,
    0x141685C30,
]

OUT_PATH = r"C:\users\vladimir\desktop\TLLCoopResearch\ida\dump-netrespawn-disasm-latest.log"


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


def main() -> None:
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8", newline="\n") as out:
        import sys
        sys.stdout = out
        for ea in TARGETS:
            dump_function(ea)
    idc.qexit(0)


if __name__ == "__main__":
    main()
