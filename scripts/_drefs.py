import os,sys
import idautils,ida_funcs,ida_name,ida_pro
out=os.environ.get("TLL_IDA_OUTPUT")
if out: sys.stdout=open(out,"w",encoding="utf-8")
raw=os.environ.get("TLL_IDA_TARGETS","")
targets=[int(p,0) for p in raw.replace(","," ").split() if p.strip()]
for t in targets:
    print(f"=== refs to 0x{t:X} ===")
    for frm in idautils.DataRefsTo(t):
        f=ida_funcs.get_func(frm)
        print(f"  DREF 0x{frm:X} in {hex(f.start_ea) if f else '?'} {ida_name.get_ea_name(f.start_ea) if f else None}")
    for frm in idautils.CodeRefsTo(t,0):
        f=ida_funcs.get_func(frm)
        print(f"  CREF 0x{frm:X} in {hex(f.start_ea) if f else '?'} {ida_name.get_ea_name(f.start_ea) if f else None}")
ida_pro.qexit(0)
