import os

import ida_funcs
import ida_hexrays
import ida_lines
import ida_name
import ida_xref
import idc


PSEUDOCODE_TARGETS = [
    0x1410C8FD0,  # Player in-place ctor
    0x1410C9F20,  # embedded +0x100 init
    0x1410DC160,  # Player::Init-like virtual
    0x1410DBF10,  # upper preflight adapter
    0x1410DB040,  # upper preflight gate
    0x1410F14C0,  # retained spawn commit
    0x1410F1910,  # retained spawn apply
    0x140CC1450,  # Player apply bridge
    0x1416860A0,  # embedded +0x100 refresh
    0x141E7C5F0,  # immediate follow-up after +0x100 refresh
    0x1418C4840,  # runtime slot apply
]

XREF_TARGETS = [
    0x1410C9F20,
    0x1416860A0,
    0x141E7C5F0,
    0x1417C32A0,
    0x1417C35A0,
    0x1418C4840,
]

OUT_PATH = r"C:\users\vladimir\desktop\TLLCoopResearch\ida\dump-player-packet-path.log"
OUT = None


def emit(text: str = "") -> None:
    global OUT
    if OUT is not None:
        OUT.write(text + "\n")


def describe_func(ea: int) -> str:
    func = ida_funcs.get_func(ea)
    if not func:
        return "no function"
    name = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
    return f"{name} start=0x{func.start_ea:X} end=0x{func.end_ea:X}"


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
    except Exception as exc:  # pragma: no cover - IDA runtime only
        emit(f"decompile failed: {exc}")
        emit("")
        return
    for line in cfunc.get_pseudocode():
        emit(ida_lines.tag_remove(line.line))
    emit("")


def describe_xref(frm: int) -> str:
    func = ida_funcs.get_func(frm)
    if func:
        owner = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
        owner = f"{owner} @ 0x{func.start_ea:X}"
    else:
        owner = "no_func"
    disasm = idc.generate_disasm_line(frm, 0) or ""
    return f"  xref from 0x{frm:X} ({owner}): {ida_lines.tag_remove(disasm)}"


def dump_xrefs(ea: int) -> None:
    emit(f"XREF TARGET 0x{ea:X}")
    emit(f"  {describe_func(ea)}")
    frm = ida_xref.get_first_cref_to(ea)
    if frm == idc.BADADDR:
        emit("  no code xrefs")
    while frm != idc.BADADDR:
        emit(describe_xref(frm))
        frm = ida_xref.get_next_cref_to(ea, frm)
    frm = ida_xref.get_first_dref_to(ea)
    if frm == idc.BADADDR:
        emit("  no data xrefs")
    while frm != idc.BADADDR:
        emit(f"  dref from 0x{frm:X}")
        frm = ida_xref.get_next_dref_to(ea, frm)
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
