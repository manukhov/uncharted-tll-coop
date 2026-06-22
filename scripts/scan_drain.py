#!/usr/bin/env python3
"""Scan the scheduler functions that reference global 0x1437E9700 and flag the
ones that look like the drain/worker side of the per-lane queue.

Heuristics, derived from the enqueue 0x1415B4560:
  - lane array lives at [state + 0x4fc0]
  - lane count is at [lane + 0], task pointers from [lane + 0xc8]
The drain reads the lane array, decrements a count, and dispatches via an
indirect call. We flag references to disp 0x4fc0 / 0xc8, count decrements, and
indirect calls.

Func starts are read from reports/xrefs_1437E9700.txt.
"""
import re
import sys

import capstone
from disasm_va import Image

XREF_FILE = "reports/xrefs_1437E9700.txt"


def load_func_starts():
    starts = set()
    with open(XREF_FILE) as f:
        for line in f:
            m = re.search(r"func_start=0x([0-9a-fA-F]+)", line)
            if m:
                starts.add(int(m.group(1), 16))
    return sorted(starts)


def scan_func(img, va, max_len=0x1200):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    code = img.read(va, max_len)
    if not code:
        return None
    flags = {
        "lane_arr": False,   # disp 0x4fc0
        "lane_slot": False,  # disp 0xc8
        "dec_mem": False,    # dec/sub qword [..]
        "ind_call": 0,       # call qword [reg+disp]
        "refs_global": False,
        "stride2200": False,
    }
    end = va + max_len
    seen_ret_pad = 0
    for insn in md.disasm(code, va):
        op = insn.op_str
        if "0x4fc0" in op:
            flags["lane_arr"] = True
        if "0xc8" in op and ("rdi" in op or "rsi" in op or "rbx" in op or "rax" in op or "rcx" in op):
            flags["lane_slot"] = True
        if "0x2200" in op:
            flags["stride2200"] = True
        if insn.mnemonic in ("dec", "sub") and "ptr" in op and "[" in op:
            flags["dec_mem"] = True
        if insn.mnemonic == "call" and op.startswith("qword ptr ["):
            flags["ind_call"] += 1
        # rip-relative to the global
        for o in insn.operands:
            if o.type == capstone.x86.X86_OP_MEM and o.mem.base == capstone.x86.X86_REG_RIP:
                tgt = insn.address + insn.size + o.mem.disp
                if tgt == 0x1437E9700:
                    flags["refs_global"] = True
        if insn.mnemonic in ("ret", "int3"):
            seen_ret_pad += 1
            if seen_ret_pad > 2 and insn.address - va > 0x40:
                break
        else:
            seen_ret_pad = 0
    return flags


def main():
    img = Image()
    starts = load_func_starts()
    print(f"; scanning {len(starts)} funcs referencing 0x1437E9700")
    cands = []
    for va in starts:
        f = scan_func(img, va)
        if not f:
            continue
        score = (f["lane_arr"] * 3 + f["lane_slot"] * 2 + f["dec_mem"] +
                 (1 if f["ind_call"] else 0) + f["stride2200"])
        if f["lane_arr"] or (f["lane_slot"] and f["ind_call"]):
            cands.append((score, va, f))
    cands.sort(reverse=True)
    for score, va, f in cands:
        tags = [k for k, v in f.items() if v and k != "ind_call"]
        print(f"0x{va:X}  score={score}  ind_call={f['ind_call']}  {' '.join(tags)}")


if __name__ == "__main__":
    main()
