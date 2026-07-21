#!/usr/bin/env python3
"""Given vtable RVAs of tll.exe, print the MSVC RTTI class name for each."""
import pefile, sys, struct
BIN="/mnt/c/Program Files (x86)/Steam/steamapps/common/Uncharted Legacy of Thieves Collection/tll.exe"
pe=pefile.PE(BIN, fast_load=True); base=pe.OPTIONAL_HEADER.ImageBase; raw=pe.__data__
secs=[(base+s.VirtualAddress, base+s.VirtualAddress+s.Misc_VirtualSize, s.PointerToRawData) for s in pe.sections]
def off(va):
    for a,b,o in secs:
        if a<=va<b: return o+(va-a)
    return None
def rva_off(rva): return off(base+rva)
def u32(o): return struct.unpack_from("<I", raw, o)[0]
def u64(o): return struct.unpack_from("<Q", raw, o)[0]
def name_for_vtable(vrva):
    o=rva_off(vrva)
    if o is None: return "(vtable rva not mapped)"
    col_va=u64(o-8)             # ptr to CompleteObjectLocator
    co=off(col_va)
    if co is None: return "(COL not mapped)"
    type_rva=u32(co+12)         # pTypeDescriptor (RVA)
    to=rva_off(type_rva)
    if to is None: return "(typedesc not mapped)"
    # TypeDescriptor: +0 vfptr(8), +8 spare(8), +16 name
    end=raw.find(b"\0", to+16)
    return raw[to+16:end].decode("ascii","replace")
for a in sys.argv[1:]:
    rva=int(a,16)
    print(f"0x{rva:x}: {name_for_vtable(rva)}")
