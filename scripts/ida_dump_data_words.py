"""Dump named qwords and dwords around addresses from TLL_IDA_TARGETS."""

import os
import sys

import ida_bytes
import ida_name
import ida_pro


def parse_targets():
    raw = os.environ.get("TLL_IDA_TARGETS", "")
    return [int(part, 0) for part in raw.replace(",", " ").split() if part]


def main():
    output = os.environ.get("TLL_IDA_OUTPUT")
    if output:
        sys.stdout = open(output, "w", encoding="utf-8")

    targets = parse_targets()
    if not targets:
        print("No TLL_IDA_TARGETS provided")
        ida_pro.qexit(1)
        return

    before = int(os.environ.get("TLL_IDA_DATA_BEFORE", "0x40"), 0)
    after = int(os.environ.get("TLL_IDA_DATA_AFTER", "0x80"), 0)
    for target in targets:
        print(f"TARGET 0x{target:X}")
        start = (target - before) & ~7
        end = (target + after + 7) & ~7
        for ea in range(start, end, 8):
            qword = ida_bytes.get_qword(ea)
            low = ida_bytes.get_dword(ea)
            high = ida_bytes.get_dword(ea + 4)
            qname = ida_name.get_ea_name(qword) or ""
            low_name = ida_name.get_ea_name(low) or ""
            high_name = ida_name.get_ea_name(high) or ""
            marker = " <TARGET>" if ea <= target < ea + 8 else ""
            print(
                f"  0x{ea:X}: q=0x{qword:016X} {qname} "
                f"d0=0x{low:08X} {low_name} d1=0x{high:08X} {high_name}{marker}"
            )
        print("")

    ida_pro.qexit(0)


main()
