import os
import sys

import ida_bytes
import ida_name
import ida_pro


def main():
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")

    vtable = int(os.environ["TLL_IDA_VTABLE"], 0)
    count = int(os.environ.get("TLL_IDA_VTABLE_COUNT", "32"), 0)
    print(f"vtable=0x{vtable:X} count={count}")
    for slot in range(count):
        target = ida_bytes.get_qword(vtable + slot * 8)
        name = ida_name.get_ea_name(target) or "<unnamed>"
        print(f"[{slot:3d}] +0x{slot * 8:03X} -> 0x{target:X} {name}")
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
