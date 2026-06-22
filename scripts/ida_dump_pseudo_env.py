import os, sys
import ida_funcs, ida_hexrays, ida_lines, ida_name, ida_pro

def main():
    out = os.environ.get("TLL_IDA_OUTPUT")
    if out:
        sys.stdout = open(out, "w", encoding="utf-8")
    if not ida_hexrays.init_hexrays_plugin():
        print("Hex-Rays unavailable")
        ida_pro.qexit(1); return
    raw = os.environ.get("TLL_IDA_TARGETS", "")
    targets = [int(p, 0) for p in raw.replace(",", " ").split() if p.strip()]
    for ea in targets:
        f = ida_funcs.get_func(ea)
        if not f:
            print(f"=== 0x{ea:X}: NO FUNCTION ==="); continue
        nm = ida_name.get_ea_name(f.start_ea) or "<unnamed>"
        print(f"=== 0x{ea:X}: {nm} [0x{f.start_ea:X}..0x{f.end_ea:X}] ===")
        try:
            cf = ida_hexrays.decompile(f.start_ea)
            for ln in cf.get_pseudocode():
                print(ida_lines.tag_remove(ln.line))
        except Exception as e:
            print(f"(decompile failed: {e})")
        print("")
    ida_pro.qexit(0)

main()
