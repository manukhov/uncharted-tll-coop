"""Find writes to per-thread actor context fields after FlsGetValue calls."""

import os
import sys

import ida_funcs
import ida_name
import ida_pro
import idautils
import idc


TARGET_DISPLACEMENTS = (0x90, 0x98)


def main() -> None:
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")

    seen = set()
    for func_ea in idautils.Functions():
        func = ida_funcs.get_func(func_ea)
        if not func:
            continue
        for ea in idautils.FuncItems(func_ea):
            if idc.print_insn_mnem(ea).lower() != "call":
                continue
            if "FlsGetValue" not in idc.generate_disasm_line(ea, 0):
                continue

            cursor = idc.next_head(ea, func.end_ea)
            for _ in range(40):
                if cursor == idc.BADADDR or cursor >= func.end_ea:
                    break
                line = idc.generate_disasm_line(cursor, 0)
                mnemonic = idc.print_insn_mnem(cursor).lower()
                destination = idc.print_operand(cursor, 0).replace(" ", "").lower()
                writes_target = mnemonic.startswith(("mov", "vmov")) and any(
                    f"+{disp:x}h]" in destination
                    for disp in TARGET_DISPLACEMENTS
                )
                if writes_target:
                    key = (func_ea, ea, cursor)
                    if key not in seen:
                        seen.add(key)
                        name = ida_name.get_ea_name(func_ea) or "<unnamed>"
                        print(
                            f"func=0x{func_ea:X} {name} "
                            f"fls_call=0x{ea:X} access=0x{cursor:X}: {line}"
                        )
                if cursor != ea and idc.print_insn_mnem(cursor).lower() in {
                    "ret",
                    "retn",
                }:
                    break
                cursor = idc.next_head(cursor, func.end_ea)

    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
