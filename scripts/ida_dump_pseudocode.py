import ida_funcs
import ida_hexrays
import ida_lines
import ida_name
import idc


TARGETS = [
    0x140423330,
    0x140423440,
    0x14048BF60,
    0x1404C1060,
    0x1404C10C0,
    0x1404C1120,
]


def dump_function(ea: int) -> None:
    func = ida_funcs.get_func(ea)
    if not func:
        print(f"TARGET 0x{ea:X}: no function")
        return
    name = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
    print(f"TARGET 0x{ea:X}: {name} start=0x{func.start_ea:X} end=0x{func.end_ea:X}")
    cfunc = ida_hexrays.decompile(func.start_ea)
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
