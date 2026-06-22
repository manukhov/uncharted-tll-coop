import os,sys
import idautils,ida_name,ida_funcs,ida_pro,ida_bytes
out=os.environ.get("TLL_IDA_OUTPUT")
if out: sys.stdout=open(out,"w",encoding="utf-8")
import capstone
# 1) functions whose NAME contains these
keys=["AllocateAnimData","AnimData","ArtItemSkeleton","SetupAnim","AnimControl","SetLook","ArtGroup","BindArt","CharacterLook"]
print("=== named functions ===")
for ea in idautils.Functions():
    n=ida_name.get_ea_name(ea) or ""
    for k in keys:
        if k.lower() in n.lower():
            print(f"  0x{ea:X} {n}"); break
# 2) all copies of the assert string + their code xrefs
print("=== assert string copies + xrefs ===")
b=b"struct FgAnimData *__cdecl Player::AllocateAnimData"
for seg_start in [0x142000000]:
    pass
ea=0x140000000
import ida_search
# brute: iterate strings list
for s in idautils.Strings():
    if b"AllocateAnimData" in ida_bytes.get_bytes(s.ea, min(s.length,80)) :
        print(f"  str 0x{s.ea:X} len={s.length}")
        for x in idautils.XrefsTo(s.ea):
            f=ida_funcs.get_func(x.frm)
            print(f"      xref 0x{x.frm:X} in {hex(f.start_ea) if f else '?'} {ida_name.get_ea_name(f.start_ea) if f else ''}")
ida_pro.qexit(0)
