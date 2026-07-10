import os
import sys

import ida_bytes
import ida_pro
import idautils


def parse_hashes():
    raw = os.environ.get("TLL_IDA_HASHES", "")
    return {int(part, 0) for part in raw.replace(",", " ").split() if part.strip()}


def engine_crc(text):
    value = 0
    for byte in text:
        index = (byte ^ (value >> 24)) & 0xFF
        value = ((value << 8) ^ ida_bytes.get_dword(0x142BEA360 + index * 4)) & 0xFFFFFFFF
    return value


def main():
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")

    targets = parse_hashes()
    if not targets:
        print("No TLL_IDA_HASHES provided")
        ida_pro.qexit(1)
        return

    matches = 0
    for item in idautils.Strings():
        raw = ida_bytes.get_strlit_contents(item.ea, item.length, item.strtype)
        if not raw:
            continue
        value = engine_crc(raw)
        if value in targets:
            print(f"0x{value:08X} 0x{item.ea:X} {raw.decode('utf-8', errors='replace')!r}")
            matches += 1

    print(f"matches={matches}")
    ida_pro.qexit(0)


main()
