import ida_funcs
import ida_hexrays
import ida_lines
import ida_name
import ida_pro

OUT = r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\pseudo_look_pipeline_now.txt"
TARGETS = [0x14197D4BF, 0x141980D20, 0x14029A420]


def main():
    with open(OUT, "w", encoding="utf-8") as out:
        if not ida_hexrays.init_hexrays_plugin():
            out.write("Hex-Rays unavailable\n")
            ida_pro.qexit(1)
            return
        for ea in TARGETS:
            func = ida_funcs.get_func(ea)
            if not func:
                out.write(f"=== 0x{ea:X}: NO FUNCTION ===\n\n")
                continue
            name = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
            out.write(f"=== 0x{ea:X}: {name} [0x{func.start_ea:X}..0x{func.end_ea:X}] ===\n")
            try:
                cf = ida_hexrays.decompile(func.start_ea)
                for line in cf.get_pseudocode():
                    out.write(ida_lines.tag_remove(line.line) + "\n")
            except Exception as exc:
                out.write(f"(decompile failed: {exc})\n")
            out.write("\n")
    ida_pro.qexit(0)


main()
