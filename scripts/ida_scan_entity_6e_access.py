"""Scan disassembly for byte accesses to struct member +0x6E (110 dec).

Writes TLL_IDA_OUTPUT (default reports/entity_6e_byte_access.txt).
"""
import os
import re

import ida_funcs
import ida_lines
import ida_name
import idaapi
import idautils
import idc

OUT = os.environ.get(
    "TLL_IDA_OUTPUT",
    r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\entity_6e_byte_access.txt",
)

PAT = re.compile(r"\+6[Ee]h\]", re.I)
WRITE_HINT = re.compile(r"\b(or|mov|and|xor)\b", re.I)
READ_HINT = re.compile(r"\b(cmp|test|and)\b", re.I)


def func_label(ea: int) -> str:
    fn = ida_funcs.get_func(ea)
    if not fn:
        return "no_func"
    name = ida_name.get_ea_name(fn.start_ea) or f"sub_{fn.start_ea:X}"
    return f"{name}@0x{fn.start_ea:X}"


def main() -> None:
    hits = []
    for head in idautils.Heads():
        if not idaapi.is_code(idaapi.get_full_flags(head)):
            continue
        dis = idc.generate_disasm_line(head, 0) or ""
        dis = ida_lines.tag_remove(dis)
        if not PAT.search(dis):
            continue
        kind = "access"
        if WRITE_HINT.search(dis):
            kind = "write?"
        elif READ_HINT.search(dis):
            kind = "read?"
        hits.append((head, kind, dis, func_label(head)))

    with open(OUT, "w", encoding="utf-8") as f:
        f.write(f"entity+0x6E byte accesses: {len(hits)} hits\n\n")
        for ea, kind, dis, fl in sorted(hits):
            f.write(f"0x{ea:X} [{kind}] ({fl})\n  {dis}\n\n")
    idc.qexit(0)


if __name__ == "__main__":
    main()
