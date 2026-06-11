import os
import sys

import ida_funcs
import ida_idaapi
import ida_name
import ida_pro
import idautils


def parse_targets():
    raw = os.environ.get("TLL_IDA_TARGETS", "")
    targets = []
    for part in raw.replace(",", " ").split():
        part = part.strip()
        if not part:
            continue
        targets.append(int(part, 0))
    return targets


def describe_func(ea):
    func = ida_funcs.get_func(ea)
    if not func:
        return None, None
    return func.start_ea, ida_name.get_ea_name(func.start_ea) or None


def main():
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")
    targets = parse_targets()
    if not targets:
        print("No TLL_IDA_TARGETS provided")
        ida_pro.qexit(1)
        return

    for ea in targets:
        start, name = describe_func(ea)
        print(f"TARGET 0x{ea:X}: func_start={hex(start) if start else None} name={name}")
        for xref in idautils.XrefsTo(ea):
            func_start, func_name = describe_func(xref.frm)
            print(
                f"  xref=0x{xref.frm:X} type={xref.type} "
                f"func_start={hex(func_start) if func_start else None} "
                f"func_name={func_name}"
            )
        print("")
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
