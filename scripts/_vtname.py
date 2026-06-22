import os,sys
import ida_bytes,ida_name,ida_pro,idc
out=os.environ.get("TLL_IDA_OUTPUT")
if out: sys.stdout=open(out,"w",encoding="utf-8")
def rtti_name(vt):
    col=ida_bytes.get_qword(vt-8)  # complete object locator
    if col==0 or col==0xffffffffffffffff: return None
    base=0x140000000
    try:
        td_rva=ida_bytes.get_dword(col+12)
        td=base+td_rva
        # type descriptor: +0x10 = mangled name (char[])
        name=idc.get_strlit_contents(td+16)
        if name: return name.decode(errors='replace')
    except Exception as e:
        return f"err:{e}"
    return None
for va in [0x143EEC400,0x143DC76D0,0x142A3CFA0,0x142934700,0x142A4BFE8,0x142949638]:
    n=ida_name.get_ea_name(va)
    print(f"0x{va:X}: name={n!r} rtti={rtti_name(va)!r}")
    # also dump first 3 vtable slots (function targets) and their names
    for i in range(3):
        fn=ida_bytes.get_qword(va+i*8)
        print(f"    [{i}] 0x{fn:X} {ida_name.get_ea_name(fn)!r}")
ida_pro.qexit(0)
