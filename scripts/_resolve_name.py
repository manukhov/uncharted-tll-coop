import os, sys, ida_name, ida_pro, idc
out=os.environ.get("TLL_IDA_OUTPUT")
if out: sys.stdout=open(out,"w",encoding="utf-8")
for nm in os.environ.get("TLL_IDA_NAMES","").split(","):
    nm=nm.strip()
    if not nm: continue
    ea=ida_name.get_name_ea(idc.BADADDR, nm)
    print(f"{nm} 0x{ea:X}")
ida_pro.qexit(0)
