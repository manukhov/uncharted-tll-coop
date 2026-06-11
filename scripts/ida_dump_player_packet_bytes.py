import os

import ida_bytes
import idc


RANGES = [
    (0x1410DC980, 0x1410DCA05),
    (0x1410C9F50, 0x1410C9F60),
    (0x141E7C5F0, 0x141E7C690),
    (0x140401D70, 0x140401DE0),
    (0x14153B520, 0x14153B590),
]

OUT_PATH = r"C:\users\vladimir\desktop\TLLCoopResearch\ida\dump-player-packet-bytes.txt"


def main():
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="ascii", newline="\n") as out:
        for start, end in RANGES:
            data = ida_bytes.get_bytes(start, end - start) or b""
            out.write(f"RANGE 0x{start:X}-0x{end:X}\n")
            out.write(data.hex() + "\n\n")
    idc.qexit(0)


if __name__ == "__main__":
    main()
