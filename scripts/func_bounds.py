#!/usr/bin/env python3
"""Find the .pdata function bounds containing given RVA(s) of tll.exe."""
import pefile, sys, struct, bisect
BIN="/mnt/c/Program Files (x86)/Steam/steamapps/common/Uncharted Legacy of Thieves Collection/tll.exe"
pe=pefile.PE(BIN, fast_load=True); base=pe.OPTIONAL_HEADER.ImageBase; raw=pe.__data__
pdata=[s for s in pe.sections if s.Name.rstrip(b"\x00").decode(errors="ignore")==".pdata"][0]
off=pdata.PointerToRawData; funcs=[]
for i in range(0, pdata.Misc_VirtualSize, 12):
    beg,end,unw=struct.unpack("<III", raw[off+i:off+i+12])
    if beg==0 and end==0: continue
    funcs.append((beg,end))
funcs.sort(); starts=[f[0] for f in funcs]
for a in sys.argv[1:]:
    rva=int(a,16)
    if rva>base: rva-=base
    i=bisect.bisect_right(starts, rva)-1
    if 0<=i<len(funcs):
        b,e=funcs[i]
        print(f"rva 0x{rva:x}: func start=0x{base+b:x} end=0x{base+e:x} (size 0x{e-b:x})")
    else:
        print(f"rva 0x{rva:x}: not found")
