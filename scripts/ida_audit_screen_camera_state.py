import os
import sys

import ida_funcs
import ida_lines
import ida_name
import ida_pro
import idautils
import idc


BASE = 0x1466BFFB0
SIZE = 0x8A0  # two 0x450-byte screen-state blocks


def main():
    output_path = os.environ.get(
        "TLL_IDA_OUTPUT",
        r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\screen_camera_state_xrefs.txt",
    )
    sys.stdout = open(output_path, "w", encoding="utf-8")
    seen = set()
    for target in range(BASE, BASE + SIZE):
        for xref in idautils.XrefsTo(target):
            if xref.frm in seen:
                continue
            seen.add(xref.frm)
            func = ida_funcs.get_func(xref.frm)
            start = func.start_ea if func else None
            name = ida_name.get_ea_name(start) if start is not None else None
            line = ida_lines.tag_remove(idc.generate_disasm_line(xref.frm, 0) or "")
            print(
                f"xref=0x{xref.frm:X} target=0x{target:X} type={xref.type} "
                f"func={hex(start) if start is not None else None} "
                f"name={name} insn={line}"
            )
    print(f"total_unique_xrefs={len(seen)}")
    sys.stdout.close()
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
