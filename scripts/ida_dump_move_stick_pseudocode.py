import ida_funcs
import ida_hexrays
import ida_lines
import ida_name
import ida_pro


TARGETS = (
    0x140EFE460,
    0x140EFEFB0,
    0x140F03500,
    0x140F0A950,
    0x140F0CC50,
    0x140F0D820,
    0x1410F9F90,
)
OUTPUT = r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\pseudo_move_stick_core.txt"


def main():
    ida_hexrays.init_hexrays_plugin()
    with open(OUTPUT, "w", encoding="utf-8") as output:
        for ea in TARGETS:
            func = ida_funcs.get_func(ea)
            name = ida_name.get_ea_name(func.start_ea)
            output.write(
                f"=== 0x{func.start_ea:X}: {name} "
                f"[0x{func.start_ea:X}..0x{func.end_ea:X}] ===\n"
            )
            cfunc = ida_hexrays.decompile(func.start_ea)
            for line in cfunc.get_pseudocode():
                output.write(ida_lines.tag_remove(line.line) + "\n")
            output.write("\n")
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
