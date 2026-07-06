import os
import sys

import idapro


def parse_targets():
    raw = os.environ.get("TLL_IDA_TARGETS", "")
    targets = []
    for part in raw.replace(",", " ").split():
        part = part.strip()
        if part:
            targets.append(int(part, 0))
    return targets


def main():
    db_path = os.environ.get("TLL_IDA_DB", r"C:\Users\Vladimir\Desktop\TLLCoopResearch\ida\tll_mcp.i64")
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")

    idapro.open_database(db_path, run_auto_analysis=False)

    import ida_funcs
    import ida_hexrays
    import ida_lines
    import ida_name
    import ida_pro

    if not ida_hexrays.init_hexrays_plugin():
        print("Hex-Rays unavailable")
        ida_pro.qexit(1)
        return

    targets = parse_targets()
    if not targets:
        print("No TLL_IDA_TARGETS provided")
        ida_pro.qexit(1)
        return

    for ea in targets:
        func = ida_funcs.get_func(ea)
        if not func:
            print(f"TARGET 0x{ea:X}: no function")
            continue
        name = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
        print(f"TARGET 0x{ea:X}: {name} start=0x{func.start_ea:X} end=0x{func.end_ea:X}")
        try:
            cfunc = ida_hexrays.decompile(func.start_ea)
        except Exception as exc:
            print(f"DECOMPILE FAILED: {exc!r}")
            continue
        for line in cfunc.get_pseudocode():
            print(ida_lines.tag_remove(line.line))
        print("")

    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
