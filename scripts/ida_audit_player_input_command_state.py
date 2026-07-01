import os
import sys

import ida_bytes
import ida_funcs
import ida_name
import ida_pro
import ida_segment
import ida_ua
import idautils
import idc


def main():
    output = os.environ.get("TLL_IDA_OUTPUT")
    if output:
        sys.stdout = open(output, "w", encoding="utf-8")

    print("=== functions around PlayerInputCommandState accessors ===")
    func = ida_funcs.get_func(0x141E39800)
    if func is None:
        func = ida_funcs.get_next_func(0x141E39800)
    while func is not None and func.start_ea < 0x141E3A100:
        print(
            f"0x{func.start_ea:X}..0x{func.end_ea:X} "
            f"{ida_name.get_ea_name(func.start_ea) or '<unnamed>'}"
        )
        func = ida_funcs.get_next_func(func.start_ea)

    for target in (0x141E39C80, 0x141E39D10, 0x141E39FB0):
        print(f"\n=== xrefs to 0x{target:X} ===")
        for xref in idautils.CodeRefsTo(target, False):
            owner = ida_funcs.get_func(xref)
            print(
                f"0x{xref:X} owner=0x{owner.start_ea:X} "
                f"{ida_name.get_ea_name(owner.start_ea) or '<unnamed>'}"
                if owner is not None
                else f"0x{xref:X} owner=<none>"
            )

    print("\n=== callers of PlayerJoypad initializer 0x141E39BB0 ===")
    for ref in idautils.XrefsTo(0x141E39BB0, 0):
        print(f"xref=0x{ref.frm:X} type={ref.type}")
    for xref in idautils.CodeRefsTo(0x141E39BB0, False):
        owner = ida_funcs.get_func(xref)
        owner_start = owner.start_ea if owner else xref
        print(
            f"call=0x{xref:X} owner=0x{owner_start:X} "
            f"{ida_name.get_ea_name(owner_start) or '<unnamed>'}"
        )
        begin = max(owner_start, xref - 0x60)
        end = min(owner.end_ea if owner else xref + 0x20, xref + 0x20)
        for ea in idautils.Heads(begin, end):
            print(f"  0x{ea:X}: {idc.generate_disasm_line(ea, 0) or ''}")
    text = ida_segment.get_segm_by_name(".text")
    blob = ida_bytes.get_bytes(text.start_ea, text.end_ea - text.start_ea)
    for offset in range(0, len(blob) - 5):
        if blob[offset] not in (0xE8, 0xE9):
            continue
        displacement = int.from_bytes(blob[offset + 1:offset + 5], "little", signed=True)
        ea = text.start_ea + offset
        if ea + 5 + displacement != 0x141E39BB0:
            continue
        owner = ida_funcs.get_func(ea)
        owner_start = owner.start_ea if owner else ea
        print(
            f"raw-ref=0x{ea:X} opcode=0x{blob[offset]:02X} "
            f"owner=0x{owner_start:X} {ida_name.get_ea_name(owner_start) or '<unnamed>'}"
        )
        for head in idautils.Heads(max(owner_start, ea - 0x60), ea + 0x20):
            print(f"  0x{head:X}: {idc.generate_disasm_line(head, 0) or ''}")

    print("\n=== direct writes using displacement 0x7C/0x90 in accessor region ===")
    for ea in idautils.Heads(0x141E39800, 0x141E3A100):
        if not idc.is_code(ida_bytes.get_flags(ea)):
            continue
        insn = ida_ua.insn_t()
        if ida_ua.decode_insn(insn, ea) == 0:
            continue
        text = idc.generate_disasm_line(ea, 0) or ""
        if "7Ch" not in text and "90h" not in text:
            continue
        print(f"0x{ea:X}: {text}")

    print("\n=== PlayerJoypad threshold fields +0x74/+0x78 ===")
    for start, end in ((0x140EBE000, 0x140EC1000), (0x141E38000, 0x141E3A800)):
        for ea in idautils.Heads(start, end):
            if not idc.is_code(ida_bytes.get_flags(ea)):
                continue
            insn = ida_ua.insn_t()
            if ida_ua.decode_insn(insn, ea) == 0:
                continue
            if not any(idc.get_operand_value(ea, op) in (0x74, 0x78) for op in range(3)):
                continue
            owner = ida_funcs.get_func(ea)
            owner_start = owner.start_ea if owner else ea
            print(
                f"0x{ea:X} owner=0x{owner_start:X} "
                f"{ida_name.get_ea_name(owner_start) or '<unnamed>'}: "
                f"{idc.generate_disasm_line(ea, 0) or ''}"
            )

    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
