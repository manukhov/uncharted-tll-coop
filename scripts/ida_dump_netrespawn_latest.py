import os

import ida_funcs
import ida_hexrays
import ida_lines
import ida_name
import ida_xref
import idc


PSEUDOCODE_TARGETS = [
    0x140BBFD70,
    0x140B44EC0,
    0x141685C30,
]

XREF_TARGETS = [
    0x141685C30,
    0x140B44EC0,
]

OUT_PATH = r"C:\users\vladimir\desktop\TLLCoopResearch\ida\dump-netrespawn-latest.log"
OUT = None


def emit(text: str = "") -> None:
    global OUT
    if OUT is not None:
        OUT.write(text + "\n")


def dump_function(ea: int) -> None:
    func = ida_funcs.get_func(ea)
    if not func:
        emit(f"TARGET 0x{ea:X}: no function")
        emit("")
        return
    name = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
    emit(f"TARGET 0x{ea:X}: {name} start=0x{func.start_ea:X} end=0x{func.end_ea:X}")
    try:
        cfunc = ida_hexrays.decompile(func.start_ea)
    except Exception as exc:
        emit(f"decompile failed: {exc}")
        emit("")
        return
    for line in cfunc.get_pseudocode():
        emit(ida_lines.tag_remove(line.line))
    emit("")


def dump_xrefs(ea: int) -> None:
    emit(f"XREF TARGET 0x{ea:X}")
    frm = ida_xref.get_first_cref_to(ea)
    if frm == idc.BADADDR:
        emit("  no code xrefs")
    while frm != idc.BADADDR:
        func = ida_funcs.get_func(frm)
        if func:
            owner = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
            owner = f"{owner} @ 0x{func.start_ea:X}"
        else:
            owner = "no_func"
        disasm = idc.generate_disasm_line(frm, 0) or ""
        emit(f"  xref from 0x{frm:X} ({owner}): {ida_lines.tag_remove(disasm)}")
        frm = ida_xref.get_next_cref_to(ea, frm)
    emit("")


def main() -> None:
    global OUT
    if not ida_hexrays.init_hexrays_plugin():
        with open(OUT_PATH, "w", encoding="utf-8", newline="\n") as out:
            out.write("Hex-Rays unavailable\n")
        idc.qexit(1)
        return
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    OUT = open(OUT_PATH, "w", encoding="utf-8", newline="\n")
    for ea in PSEUDOCODE_TARGETS:
        dump_function(ea)
    for ea in XREF_TARGETS:
        dump_xrefs(ea)
    OUT.close()
    idc.qexit(0)


if __name__ == "__main__":
    main()
