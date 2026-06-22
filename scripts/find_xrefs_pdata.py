#!/usr/bin/env python3
"""Reliable call/jmp/lea xref finder: decode each .pdata function, report
instructions whose branch/lea target == any given VA."""
import pefile, capstone, sys, struct
BIN="/mnt/c/Program Files (x86)/Steam/steamapps/common/Uncharted Legacy of Thieves Collection/tll.exe"
pe=pefile.PE(BIN, fast_load=True); base=pe.OPTIONAL_HEADER.ImageBase; raw=pe.__data__
secs=[(base+s.VirtualAddress, base+s.VirtualAddress+s.Misc_VirtualSize, s.PointerToRawData) for s in pe.sections]
def va_off(va):
    for a,b,o in secs:
        if a<=va<b: return o+(va-a)
    return None
pdata=[s for s in pe.sections if s.Name.rstrip(b'\0').decode(errors='ignore')=='.pdata'][0]
po=pdata.PointerToRawData; funcs=[]
for i in range(0,pdata.Misc_VirtualSize,12):
    beg,end,_=struct.unpack("<III", raw[po+i:po+i+12])
    if beg or end: funcs.append((base+beg, base+end))
targets={int(t,0) for t in sys.argv[1:]}
md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail=True; M=capstone.x86
hits=[]
for fs,fe in funcs:
    o=va_off(fs)
    if o is None: continue
    for insn in md.disasm(raw[o:o+(fe-fs)], fs):
        if insn.id in (M.X86_INS_CALL, M.X86_INS_JMP, M.X86_INS_LEA):
            for op in insn.operands:
                if op.type==M.X86_OP_IMM and op.imm in targets:
                    hits.append((fs,insn.address,insn.mnemonic,op.imm))
                elif op.type==M.X86_OP_MEM and op.mem.base==M.X86_REG_RIP:
                    t=insn.address+insn.size+op.mem.disp
                    if t in targets: hits.append((fs,insn.address,insn.mnemonic+"(rip)",t))
for fs,va,mn,t in hits[:60]:
    print(f"func 0x{fs:x} @0x{va:x}: {mn} -> 0x{t:x}")
print(f"total {len(hits)}")
