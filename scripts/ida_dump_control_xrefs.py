import ida_funcs
import ida_lines
import ida_name
import ida_xref
import idc

TARGETS = [
    0x141DE55E0,
    0x141DE5630,
    0x141DE5660,
    0x141DE5690,
    0x141DE57F0,
    0x141DE5980,
    0x141DF0000,
    0x141DF00A0,
]

def describe_xref(frm: int) -> str:
    func = ida_funcs.get_func(frm)
    if func:
        owner = ida_name.get_ea_name(func.start_ea) or '<unnamed>'
        owner = f'{owner} @ 0x{func.start_ea:X}'
    else:
        owner = 'no_func'
    disasm = idc.generate_disasm_line(frm, 0) or ''
    disasm = ida_lines.tag_remove(disasm)
    return f'  xref from 0x{frm:X} ({owner}): {disasm}'

for ea in TARGETS:
    func = ida_funcs.get_func(ea)
    name = ida_name.get_ea_name(func.start_ea) if func else '<no_func>'
    print(f'TARGET 0x{ea:X}: {name}')
    frm = ida_xref.get_first_cref_to(ea)
    if frm == idc.BADADDR:
        print('  no code xrefs')
    while frm != idc.BADADDR:
        print(describe_xref(frm))
        frm = ida_xref.get_next_cref_to(ea, frm)
    print('')

idc.qexit(0)
