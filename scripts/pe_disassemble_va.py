#!/usr/bin/env python3
"""Reliable VA disassembler for tll.exe, bypassing the corruption-prone IDB.

Disassembles directly from the retail PE so analysis state in ida/tll.i64 does
not matter. Resolves rip-relative operands to absolute VAs and annotates
call/branch targets.

Usage:
    python3 scripts/pe_disassemble_va.py 0x1415B4560 [0x1409FBA60 ...]
    python3 scripts/pe_disassemble_va.py --len 0x200 0x1415B4560
    python3 scripts/pe_disassemble_va.py --bytes 0x1437E9700   # hex-dump data instead

Functions are decoded until a terminating ret/jmp followed by int3 padding, or
--len bytes, whichever comes first.
"""
import argparse
import sys

import capstone
import pefile

BIN = "/mnt/c/Program Files (x86)/Steam/steamapps/common/Uncharted Legacy of Thieves Collection/tll.exe"


class Image:
    def __init__(self, path=BIN):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.sections = []
        for s in self.pe.sections:
            va = self.base + s.VirtualAddress
            self.sections.append((va, va + s.Misc_VirtualSize, s.PointerToRawData, s.Name.rstrip(b"\x00").decode()))
        self.data = self.pe.__data__

    def va_to_off(self, va):
        for start, end, raw, _name in self.sections:
            if start <= va < end:
                return raw + (va - start)
        return None

    def read(self, va, n):
        off = self.va_to_off(va)
        if off is None:
            return None
        return bytes(self.data[off:off + n])

    def section_of(self, va):
        for start, end, _raw, name in self.sections:
            if start <= va < end:
                return name
        return None


def disasm_func(img, va, max_len=0x4000):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    code = img.read(va, max_len)
    if code is None:
        print(f"; 0x{va:X}: not mapped")
        return
    print(f"==== 0x{va:X} (section {img.section_of(va)}) ====")
    seen_ret = False
    for insn in md.disasm(code, va):
        annot = annotate(img, insn)
        print(f"0x{insn.address:X}: {insn.mnemonic:<7} {insn.op_str}{annot}")
        m = insn.mnemonic
        if m in ("ret", "jmp") :
            seen_ret = True
            continue
        if seen_ret and m == "int3":
            break
        if m != "int3":
            seen_ret = False
        if insn.address - va > max_len - 16:
            break


def annotate(img, insn):
    # rip-relative resolution
    parts = []
    for op in insn.operands:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            target = insn.address + insn.size + op.mem.disp
            extra = ""
            if img.section_of(target) in (".data", ".rdata"):
                ptr = img.read(target, 8)
                if ptr:
                    val = int.from_bytes(ptr, "little")
                    extra = f" ->[{val:#x}]"
            parts.append(f"rip=0x{target:X}{extra}")
    if insn.mnemonic in ("call", "jmp") and insn.operands and insn.operands[0].type == capstone.x86.X86_OP_IMM:
        parts.append(f"=> 0x{insn.operands[0].imm:X}")
    return "   ; " + " ".join(parts) if parts else ""


def hexdump(img, va, n=0x80):
    data = img.read(va, n)
    if data is None:
        print(f"0x{va:X}: not mapped")
        return
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hexs = " ".join(f"{b:02x}" for b in chunk)
        print(f"0x{va + i:X}: {hexs}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("targets", nargs="+")
    ap.add_argument("--len", default="0x4000")
    ap.add_argument("--bytes", action="store_true", help="hex-dump instead of disasm")
    args = ap.parse_args()
    img = Image()
    max_len = int(args.len, 0)
    for t in args.targets:
        va = int(t, 0)
        if args.bytes:
            hexdump(img, va, max_len if max_len <= 0x400 else 0x80)
        else:
            disasm_func(img, va, max_len)
        print()


if __name__ == "__main__":
    main()
