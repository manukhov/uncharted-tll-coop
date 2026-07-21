"""Find code operands whose displacement matches TLL_IDA_OFFSETS."""

import os
import sys

import ida_funcs
import ida_name
import ida_pro
import ida_ua
import idc
import idautils


def parse_values(name):
    raw = os.environ.get(name, "")
    return {int(part, 0) for part in raw.replace(",", " ").split() if part.strip()}


def main():
    offsets = parse_values("TLL_IDA_OFFSETS")
    follow_slots = parse_values("TLL_IDA_FOLLOW_CALL_SLOTS")
    output = os.environ.get("TLL_IDA_OUTPUT")
    if output:
        sys.stdout = open(output, "w", encoding="utf-8")
    if not offsets:
        print("No TLL_IDA_OFFSETS provided")
        ida_pro.qexit(1)
        return

    for ea in idautils.Heads():
        insn = ida_ua.insn_t()
        if not ida_ua.decode_insn(insn, ea):
            continue
        for op_index, op in enumerate(insn.ops):
            if op.type == ida_ua.o_void:
                break
            if op.type not in (ida_ua.o_displ, ida_ua.o_phrase):
                continue
            if op.addr not in offsets:
                continue
            func = ida_funcs.get_func(ea)
            start = func.start_ea if func else None
            name = ida_name.get_ea_name(start) if start is not None else None
            disasm = idc.generate_disasm_line(ea, 0) or ""
            print(
                f"offset=0x{op.addr:X} ea=0x{ea:X} operand={op_index} "
                f"func_start={hex(start) if start is not None else None} "
                f"func_name={name or None} insn={disasm!r}"
            )
            next_ea = ea
            for _ in range(4):
                next_ea = idc.next_head(next_ea)
                if next_ea == idc.BADADDR:
                    break
                next_disasm = idc.generate_disasm_line(next_ea, 0) or ""
                print(f"    next=0x{next_ea:X} insn={next_disasm!r}")
            if follow_slots:
                next_ea = ea
                for _ in range(40):
                    next_ea = idc.next_head(next_ea)
                    if next_ea == idc.BADADDR:
                        break
                    next_func = ida_funcs.get_func(next_ea)
                    if not next_func or next_func.start_ea != start:
                        break
                    next_insn = ida_ua.insn_t()
                    if not ida_ua.decode_insn(next_insn, next_ea):
                        continue
                    if idc.print_insn_mnem(next_ea) != "call":
                        continue
                    call_op = next_insn.ops[0]
                    if call_op.type == ida_ua.o_displ and call_op.addr in follow_slots:
                        call_disasm = idc.generate_disasm_line(next_ea, 0) or ""
                        print(f"    follow_call=0x{next_ea:X} insn={call_disasm!r}")

    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
