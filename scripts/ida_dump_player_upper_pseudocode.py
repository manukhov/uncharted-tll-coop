import ida_funcs
import ida_hexrays
import ida_lines
import ida_name
import idc


TARGETS = [
    0x141BCD340,
    0x141BBA2A0,
    0x141BBA490,
    0x1410CF370,
]


def dump_function(ea: int) -> None:
    func = ida_funcs.get_func(ea)
    if not func:
        print(f"TARGET 0x{ea:X}: no function")
        return
    start = func.start_ea
    name = ida_name.get_ea_name(start) or "<unnamed>"
    print(f"TARGET 0x{ea:X}: {name} start=0x{start:X} end=0x{func.end_ea:X}")
    cfunc = ida_hexrays.decompile(start)
    for line in cfunc.get_pseudocode():
        print(ida_lines.tag_remove(line.line))
    print("")


def main() -> None:
    if not ida_hexrays.init_hexrays_plugin():
        print("Hex-Rays unavailable")
        idc.qexit(1)
        return
    for ea in TARGETS:
        dump_function(ea)
    idc.qexit(0)


if __name__ == "__main__":
    main()
