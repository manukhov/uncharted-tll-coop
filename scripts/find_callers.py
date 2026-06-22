#!/usr/bin/env python3
"""Find direct callers and address-takers of target VAs by scanning all of
.text in retail tll.exe with capstone. Bypasses the corruption-prone IDB, which
misses direct CALL xrefs for several functions.

Reports, per target:
  - CALL  sites: a `call rel32` whose target == VA
  - JMP   sites: a `jmp  rel32` whose target == VA (tail call)
  - LEA   sites: a `lea reg, [rip+x]` whose target == VA (address-taken)

Usage: python3 scripts/find_callers.py 0x1409FBA60 0x140A90550 ...
"""
import struct
import sys

from disasm_va import Image


def s32(b):
    return struct.unpack("<i", b)[0]


def main():
    targets = {int(t, 0) for t in sys.argv[1:]}
    if not targets:
        print("no targets")
        return
    img = Image()
    for start, end, raw, name in img.sections:
        if name == ".text":
            text_start = start
            text = img.read(start, end - start)
            break
    hits = {t: {"call": [], "jmp": [], "lea": []} for t in targets}
    n = len(text)
    # byte-level scan: alignment-independent, catches all direct rel32 refs.
    for i in range(n - 7):
        op = text[i]
        # call rel32 (E8) / jmp rel32 (E9): target = va+5+rel32
        if op in (0xE8, 0xE9):
            va = text_start + i
            tgt = va + 5 + s32(text[i + 1:i + 5])
            if tgt in targets:
                hits[tgt]["call" if op == 0xE8 else "jmp"].append(va)
        # lea r64, [rip+disp32]: REX.W (48/4C) 8D modrm(rm=101) rel32
        if op in (0x48, 0x4C) and text[i + 1] == 0x8D and (text[i + 2] & 0xC7) == 0x05:
            va = text_start + i
            tgt = va + 7 + s32(text[i + 3:i + 7])
            if tgt in targets:
                hits[tgt]["lea"].append(va)
    for t in sorted(targets):
        h = hits[t]
        print(f"==== 0x{t:X} ====")
        for kind in ("call", "jmp", "lea"):
            for a in h[kind]:
                print(f"  {kind.upper():4} from 0x{a:X}")
        if not any(h.values()):
            print("  (no direct sites found)")
        print()


if __name__ == "__main__":
    main()
