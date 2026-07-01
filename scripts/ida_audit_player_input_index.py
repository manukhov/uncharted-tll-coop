import ida_funcs
import ida_lines
import ida_name
import ida_pro
import ida_ua
import idautils
import idc


OFFSETS = {0x39A0, 0x39B8, 0x4010}
OUTPUT = r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\player_input_index_member_refs.txt"


def main():
    with open(OUTPUT, "w", encoding="utf-8") as output:
        for function_ea in idautils.Functions():
            hits = []
            for ea in idautils.FuncItems(function_ea):
                insn = ida_ua.insn_t()
                if ida_ua.decode_insn(insn, ea) <= 0:
                    continue
                for operand in insn.ops:
                    if operand.type == ida_ua.o_void:
                        break
                    if operand.type in (ida_ua.o_displ, ida_ua.o_phrase):
                        if operand.addr in OFFSETS:
                            line = ida_lines.tag_remove(
                                idc.generate_disasm_line(ea, 0) or ""
                            )
                            hits.append((ea, operand.addr, line))
                            break
            if not hits:
                continue
            name = ida_name.get_ea_name(function_ea) or "<unnamed>"
            output.write(f"FUNCTION 0x{function_ea:X} {name}\n")
            for ea, offset, line in hits:
                output.write(f"  0x{ea:X} member=0x{offset:X}: {line}\n")
            output.write("\n")
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
