"""Find instructions that write to struct offset +0x1730 (aim quat on Player).

Scans .text for ModRM disp32 == 0x1730 on write opcodes, then maps each hit to
its enclosing function and decompiles a short window. Output: reports/aim1730_writers.txt

Usage (headless):
  set TLL_IDA_DB=ida\\tll.i64
  set TLL_IDA_OUTPUT=reports\\aim1730_writers.txt
  idat.exe -A -Sscripts\\ida_find_player_1730_writers.py ida\\tll.i64
"""
import os
import sys

import idapro

TARGET_DISP = 0x1730
# PlayerAimController neighborhood + broader player update band.
SCAN_RVA_LO = 0x8A0000
SCAN_RVA_HI = 0xCC5000


def main():
    db_path = os.environ.get(
        "TLL_IDA_DB", r"C:\Users\Vladimir\Desktop\TLLCoopResearch\ida\tll.i64"
    )
    output_path = os.environ.get(
        "TLL_IDA_OUTPUT",
        r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\aim1730_writers.txt",
    )
    out = open(output_path, "w", encoding="utf-8")
    sys.stdout = out

    idapro.open_database(db_path, run_auto_analysis=False)

    import idaapi
    import ida_bytes
    import ida_funcs
    import ida_hexrays
    import ida_lines
    import ida_name
    import ida_pro
    import ida_segment
    import ida_ua

    image_base = idaapi.get_imagebase()
    lo = image_base + SCAN_RVA_LO
    hi = image_base + SCAN_RVA_HI

    if not ida_hexrays.init_hexrays_plugin():
        print("Hex-Rays unavailable")
        ida_pro.qexit(1)
        return

    disp_le = bytes(
        [
            TARGET_DISP & 0xFF,
            (TARGET_DISP >> 8) & 0xFF,
            (TARGET_DISP >> 16) & 0xFF,
            (TARGET_DISP >> 24) & 0xFF,
        ]
    )

    # Write-ish opcode prefixes (x64): mov store, movaps/xmm, movss, movsd, xor [mem]
    write_prefixes = {
        0x88,
        0x89,
        0xC6,
        0xC7,
        0x0F,  # SSE/AVX stores often start with 0F
    }

    hits = []
    ea = lo
    while ea < hi:
        if not ida_bytes.is_mapped(ea):
            ea += 1
            continue
        # Fast scan: look for little-endian disp32 0x1730 within next 12 bytes.
        chunk = ida_bytes.get_bytes(ea, 12)
        if chunk is None:
            ea += 1
            continue
        idx = chunk.find(disp_le)
        if idx < 0:
            ea += 1
            continue
        hit_ea = ea + idx - 4 if idx >= 4 else ea
        # Re-align: disp32 starts 4 bytes before the pattern in [reg+disp32] forms.
        for cand in range(ea, ea + idx + 1):
            insn = ida_ua.insn_t()
            if ida_ua.decode_insn(insn, cand) <= 0:
                continue
            for op in insn.ops:
                if op.type == ida_ua.o_displ and op.addr == TARGET_DISP:
                    hits.append(cand)
                    break
                if op.type == ida_ua.o_displ and getattr(op, "value", 0) == TARGET_DISP:
                    hits.append(cand)
                    break
        ea += 1

    # Dedupe and sort.
    hits = sorted(set(hits))
    print(f"=== disp +0x{TARGET_DISP:X} write hits in RVA {SCAN_RVA_LO:X}..{SCAN_RVA_HI:X} ===")
    print(f"total insn hits: {len(hits)}\n")

    seen_funcs = set()
    for insn_ea in hits:
        fn = ida_funcs.get_func(insn_ea)
        fn_ea = fn.start_ea if fn else insn_ea
        if fn_ea in seen_funcs:
            continue
        seen_funcs.add(fn_ea)
        rva = insn_ea - image_base
        fn_rva = fn_ea - image_base
        fn_name = ida_funcs.get_func_name(fn_ea) or ida_name.get_name(fn_ea) or "?"
        print(f"--- insn @ 0x{insn_ea:X} (rva=0x{rva:X}) in {fn_name} @ 0x{fn_ea:X} (rva=0x{fn_rva:X}) ---")
        try:
            cfunc = ida_hexrays.decompile(fn_ea)
            if cfunc:
                sv = cfunc.get_pseudocode()
                for i, line in enumerate(sv):
                    text = ida_lines.tag_remove(line.line) if hasattr(line, "line") else str(line)
                    if "1730" in text or "5936" in text:
                        print(f"  [{i}] {text}")
        except Exception as exc:
            print(f"  decompile error: {exc}")
        print()

    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
