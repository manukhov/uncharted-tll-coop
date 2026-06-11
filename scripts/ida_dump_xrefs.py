import ida_funcs
import ida_lines
import ida_name
import ida_xref
import idc


TARGETS = [
    0x1417AEDE0,
    0x1417AEE60,
    0x1417AEED0,
    0x1417AF120,
    0x141844170,
    0x141844DE0,
    0x141845590,
    0x141845640,
    0x1418457A0,
    0x141845990,
]


def describe_func(ea: int) -> str:
    func = ida_funcs.get_func(ea)
    if not func:
        return "no function"
    name = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
    return f"{name} start=0x{func.start_ea:X} end=0x{func.end_ea:X}"


def describe_xref(frm: int) -> str:
    func = ida_funcs.get_func(frm)
    if func:
        owner = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
        owner = f"{owner} @ 0x{func.start_ea:X}"
    else:
        owner = "no_func"
    disasm = idc.generate_disasm_line(frm, 0) or ""
    disasm = ida_lines.tag_remove(disasm)
    return f"  xref from 0x{frm:X} ({owner}): {disasm}"


def main() -> None:
    for ea in TARGETS:
        print(f"TARGET 0x{ea:X}")
        print(f"  {describe_func(ea)}")
        frm = ida_xref.get_first_cref_to(ea)
        if frm == idc.BADADDR:
            print("  no code xrefs")
        while frm != idc.BADADDR:
            print(describe_xref(frm))
            frm = ida_xref.get_next_cref_to(ea, frm)
        frm = ida_xref.get_first_dref_to(ea)
        if frm == idc.BADADDR:
            print("  no data xrefs")
        while frm != idc.BADADDR:
            print(f"  dref from 0x{frm:X}")
            frm = ida_xref.get_next_dref_to(ea, frm)
        print("")
    idc.qexit(0)


if __name__ == "__main__":
    main()
