import ida_funcs, ida_hexrays, ida_lines, ida_name, idc
TARGETS=[0x14048B270,0x14048F180,0x14048F260,0x14048F320,0x14048F410,0x140464930,0x141C9ED50]
def dump(ea):
 f=ida_funcs.get_func(ea)
 if not f:
  print(f'TARGET 0x{ea:X}: no function'); return
 print(f'TARGET 0x{ea:X}: {ida_name.get_ea_name(f.start_ea) or "<unnamed>"} start=0x{f.start_ea:X} end=0x{f.end_ea:X}')
 try:
  c=ida_hexrays.decompile(f.start_ea)
  for line in c.get_pseudocode():
   print(ida_lines.tag_remove(line.line))
 except Exception as e:
  print(f'<decompile failed: {e}>')
 print('')
def main():
 if not ida_hexrays.init_hexrays_plugin():
  print('Hex-Rays unavailable'); idc.qexit(1); return
 for ea in TARGETS: dump(ea)
 idc.qexit(0)
if __name__=='__main__': main()
