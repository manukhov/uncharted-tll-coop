import os

import ida_bytes
import ida_lines
import idc


RANGES = [
    (0x1410C8FD0, 0x1410CA080),  # Player ctor and +0x100 init neighborhood
    (0x1410DC930, 0x1410DCAA0),  # early Player::Init around +0x100 refresh
    (0x140CC1450, 0x140CC1796),  # Player apply bridge
    (0x1410DE740, 0x1410DE79D),  # thin wrapper
    (0x1410F14C0, 0x1410F183C),  # retained packet commit
    (0x1410F1910, 0x1410F1C80),  # early retained spawn path
    (0x1410F36C0, 0x1410F3870),  # retained slot helper using base packet
    (0x1410F3B20, 0x1410F3D10),  # retained slot promotion helper
    (0x140B31F60, 0x140B32D20),  # direct caller of PromoteRetainedPlayerSlot
    (0x140A90E80, 0x140A91120),  # retained promotion manager lookup/apply
    (0x140AA4BC0, 0x140AA4E20),  # retained promotion manager finalize
    (0x1417C32A0, 0x1417C3430),  # selector-backed packet init
    (0x1417C35A0, 0x1417C361E),  # extended runtime-state packet init
    (0x141D21C80, 0x141D21E10),  # retained entity runtime dispatch
    (0x1413BF090, 0x1413BF1D0),  # upper retained helper calling wrapper init
    (0x1416860A0, 0x14168611B),  # embedded +0x100 refresh
]

OUT_PATH = r"C:\users\vladimir\desktop\TLLCoopResearch\ida\dump-player-packet-disasm.log"


def emit(out, text=""):
    out.write(text + "\n")


def dump_range(out, start, end):
    emit(out, f"RANGE 0x{start:X}-0x{end:X}")
    ea = start
    while ea < end:
        line = idc.generate_disasm_line(ea, 0) or ""
        line = ida_lines.tag_remove(line)
        size = ida_bytes.get_item_size(ea)
        if size <= 0:
            size = 1
        emit(out, f"0x{ea:X}: {line}")
        ea += size
    emit(out, "")


def main():
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8", newline="\n") as out:
        for start, end in RANGES:
            dump_range(out, start, end)
    idc.qexit(0)


if __name__ == "__main__":
    main()
