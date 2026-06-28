import os
import sys

import ida_bytes
import ida_name
import ida_pro
import idautils


def main():
    output = os.environ.get("TLL_IDA_OUTPUT")
    if output:
        sys.stdout = open(output, "w", encoding="utf-8")

    terms = [
        term.lower()
        for term in os.environ.get("TLL_IDA_NAME_TERMS", "").split("|")
        if term
    ]
    slot_count = int(os.environ.get("TLL_IDA_SLOT_COUNT", "0"), 0)

    for ea, name in idautils.Names():
        if terms and not any(term in name.lower() for term in terms):
            continue
        print(f"0x{ea:X} {name}")
        for slot in range(slot_count):
            target = ida_bytes.get_qword(ea + slot * 8)
            target_name = ida_name.get_ea_name(target) or "<unnamed>"
            print(f"  +0x{slot * 8:X}: 0x{target:X} {target_name}")

    ida_pro.qexit(0)


main()
