import os
import sys

import ida_funcs
import ida_bytes
import ida_segment
import ida_ua
import ida_name
import ida_pro
import idautils
import idc


TARGETS = (
    0x140297AE0,  # global game/input owner constructor
    0x14029AAF0,  # GameInfo virtual +0x98
    0x14029AB40,  # GameInfo virtual +0xA0
    0x14029AB50,  # GameInfo virtual +0x90
    0x14029AE40,  # direct GameInfo+0x499C query
    0x1407E87D0,  # setting getter
    0x1407E8880,  # setting writer
    0x1407E8F70,  # setting address resolver
    0x1407FB570,  # second setting getter
    0x141585640,  # settings restore writer
    0x1418369C0,  # JoypadState_Initialize
    0x14183B410,  # Input_ScaleCopyVirtual
)


def dump_function(output, ea):
    func = ida_funcs.get_func(ea)
    if func is None:
        output.write(f"missing function at 0x{ea:X}\n")
        return
    output.write(
        f"=== 0x{func.start_ea:X}: {ida_name.get_ea_name(func.start_ea)} "
        f"[0x{func.start_ea:X}..0x{func.end_ea:X}] ===\n"
    )
    for head in idautils.Heads(func.start_ea, func.end_ea):
        line = idc.generate_disasm_line(head, 0)
        if line:
            output.write(f"0x{head:X}: {line}\n")


def main():
    path = os.environ.get(
        "TLL_IDA_OUTPUT",
        r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\joypad_slot_class_audit.txt",
    )
    output = open(path, "w", encoding="utf-8") if path else sys.stdout
    try:
        output.write("=== callers of JoypadState_Initialize ===\n")
        for xref in idautils.CodeRefsTo(0x1418369C0, False):
            owner = ida_funcs.get_func(xref)
            owner_start = owner.start_ea if owner else xref
            output.write(
                f"call=0x{xref:X} owner=0x{owner_start:X} "
                f"{ida_name.get_ea_name(owner_start)}\n"
            )
            begin = max(owner_start, xref - 0x80)
            end = min(owner.end_ea if owner else xref + 0x40, xref + 0x40)
            for head in idautils.Heads(begin, end):
                line = idc.generate_disasm_line(head, 0)
                if line:
                    output.write(f"  0x{head:X}: {line}\n")
        output.write("\n")
        output.write("\n=== vtable candidates written by 0x140297AE0 ===\n")
        ctor = ida_funcs.get_func(0x140297AE0)
        candidates = set()
        for head in idautils.Heads(ctor.start_ea, ctor.end_ea):
            for operand in range(3):
                value = idc.get_operand_value(head, operand)
                if 0x142000000 <= value < 0x143000000:
                    candidates.add(value)
        for table in sorted(candidates):
            output.write(f"candidate=0x{table:X}\n")
            for slot in range(0, 0x120, 8):
                target = ida_bytes.get_qword(table + slot)
                output.write(
                    f"  +0x{slot:X}: 0x{target:X} "
                    f"{ida_name.get_ea_name(target)}\n"
                )

        output.write("\n=== callers of GameInfo+0x499C accessors ===\n")
        for target in (0x14029AE40, 0x1407E87D0, 0x1407E8880, 0x1407E8F70, 0x1407FB570):
            output.write(f"target=0x{target:X} {ida_name.get_ea_name(target)}\n")
            for xref in idautils.CodeRefsTo(target, False):
                owner = ida_funcs.get_func(xref)
                owner_start = owner.start_ea if owner else xref
                output.write(
                    f"  call=0x{xref:X} owner=0x{owner_start:X} "
                    f"{ida_name.get_ea_name(owner_start)}\n"
                )

        output.write("\n=== strings hashing to setting id 0x51759315 ===\n")
        crc_table = [ida_bytes.get_dword(0x142BEA360 + index * 4) for index in range(256)]
        for item in idautils.Strings():
            raw = ida_bytes.get_strlit_contents(item.ea, item.length, item.strtype)
            if raw is None:
                continue
            value = 0
            for byte in raw:
                value = ((value << 8) ^ crc_table[(byte ^ (value >> 24)) & 0xFF]) & 0xFFFFFFFF
            if value == 0x51759315:
                output.write(f"0x{item.ea:X}: {raw.decode('utf-8', errors='replace')}\n")

        output.write("\n=== instructions referencing displacement 0x499C ===\n")
        text_segment = ida_segment.get_segm_by_name(".text")
        blob = ida_bytes.get_bytes(
            text_segment.start_ea, text_segment.end_ea - text_segment.start_ea
        )
        needle = b"\x9c\x49\x00\x00"
        cursor = 0
        seen = set()
        while True:
            offset = blob.find(needle, cursor)
            if offset < 0:
                break
            occurrence = text_segment.start_ea + offset
            cursor = offset + 1
            for candidate in range(max(text_segment.start_ea, occurrence - 10), occurrence + 1):
                insn = ida_ua.insn_t()
                size = ida_ua.decode_insn(insn, candidate)
                if size == 0 or not (candidate <= occurrence < candidate + size):
                    continue
                if not any(idc.get_operand_value(candidate, op) == 0x499C for op in range(3)):
                    continue
                if candidate in seen:
                    continue
                seen.add(candidate)
                owner = ida_funcs.get_func(candidate)
                owner_start = owner.start_ea if owner else candidate
                output.write(
                    f"0x{candidate:X} owner=0x{owner_start:X} "
                    f"{ida_name.get_ea_name(owner_start)}: "
                    f"{idc.generate_disasm_line(candidate, 0)}\n"
                )

        output.write("\n=== xrefs to input global gates ===\n")
        for target in (0x1437EA6BF, 0x1437EA62C, 0x1437EC6C3):
            output.write(f"target=0x{target:X} {ida_name.get_ea_name(target)}\n")
            for xref in idautils.XrefsTo(target, 0):
                owner = ida_funcs.get_func(xref.frm)
                owner_start = owner.start_ea if owner else xref.frm
                output.write(
                    f"  0x{xref.frm:X} type={xref.type} owner=0x{owner_start:X} "
                    f"{ida_name.get_ea_name(owner_start)}: "
                    f"{idc.generate_disasm_line(xref.frm, 0)}\n"
                )

        output.write("\n")
        for target in TARGETS:
            dump_function(output, target)
            output.write("\n")
    finally:
        if output is not sys.stdout:
            output.close()
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
