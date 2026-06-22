import os,sys
import ida_bytes,ida_name,ida_pro
out=os.environ.get("TLL_IDA_OUTPUT")
if out: sys.stdout=open(out,"w",encoding="utf-8")
PVT=0x142A4BFE8  # Player vtable
NVT=0x142949638  # NadineBuddy vtable
for label,vt in [("Player",PVT),("NadineBuddy",NVT)]:
    print(f"=== {label} vtable 0x{vt:X} ===")
    for off in [208,752]:
        fn=ida_bytes.get_qword(vt+off)
        print(f"  +{off} [slot {off//8}] -> 0x{fn:X} {ida_name.get_ea_name(fn)!r}")
ida_pro.qexit(0)
