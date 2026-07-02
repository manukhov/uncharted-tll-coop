import os
import sys

import ida_bytes
import ida_funcs
import ida_name
import ida_pro
import ida_ua
import idautils
import idc


def parse_targets():
    raw = os.environ.get("TLL_IDA_TARGETS", "")
    return [int(part, 0) for part in raw.replace(",", " ").split() if part]


def main():
    output = os.environ.get("TLL_IDA_OUTPUT")
    if output:
        sys.stdout = open(output, "w", encoding="utf-8")

    targets = parse_targets()
    if not targets:
        print("No TLL_IDA_TARGETS provided")
        ida_pro.qexit(1)
        return

    for target in targets:
        print(f"=== global 0x{target:X} ===")
        for xref in idautils.XrefsTo(target, 0):
            ea = xref.frm
            func = ida_funcs.get_func(ea)
            owner = func.start_ea if func else None
            name = ida_name.get_ea_name(owner) if owner is not None else "<none>"
            insn = ida_ua.insn_t()
            ida_ua.decode_insn(insn, ea)
            operands = []
            for index in range(8):
                if insn.ops[index].type == ida_ua.o_void:
                    break
                if idc.get_operand_value(ea, index) == target:
                    operands.append(index)
            line = idc.generate_disasm_line(ea, 0) or ""
            print(
                f"0x{ea:X} owner={hex(owner) if owner is not None else None} "
                f"{name or '<unnamed>'} target_ops={operands}: {line}"
            )
        print()

    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
