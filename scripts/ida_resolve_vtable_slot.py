"""Resolve data references inside MSVC vtables to RTTI class and slot offset."""

import os
import sys

import ida_bytes
import ida_name
import ida_pro
import idc


BASE = 0x140000000
MAX_SCAN = 0x4000


def rtti_name(vtable):
    locator = ida_bytes.get_qword(vtable - 8)
    if not locator or locator == 0xFFFFFFFFFFFFFFFF:
        return None
    type_rva = ida_bytes.get_dword(locator + 12)
    if not type_rva:
        return None
    value = idc.get_strlit_contents(BASE + type_rva + 16)
    if not value or not value.startswith((b".?AV", b".?AU")):
        return None
    return value.decode("ascii", errors="replace")


output = os.environ.get("TLL_IDA_OUTPUT")
if output:
    sys.stdout = open(output, "w", encoding="utf-8")

raw = os.environ.get("TLL_IDA_TARGETS", "")
targets = [int(part, 0) for part in raw.replace(",", " ").split() if part]
if not targets:
    print("No TLL_IDA_TARGETS provided")
    ida_pro.qexit(1)

for target in targets:
    resolved = False
    for delta in range(0, MAX_SCAN + 8, 8):
        candidate = target - delta
        name = rtti_name(candidate)
        if name:
            value = ida_bytes.get_qword(target)
            print(
                f"ref=0x{target:X} vtable=0x{candidate:X} "
                f"slot=+0x{delta:X} target=0x{value:X} "
                f"target_name={ida_name.get_ea_name(value)!r} rtti={name!r}"
            )
            resolved = True
            break
    if not resolved:
        print(f"ref=0x{target:X} unresolved within 0x{MAX_SCAN:X} bytes")

ida_pro.qexit(0)
