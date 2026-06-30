import json
import os

import ida_funcs
import ida_name
import ida_nalt
import ida_pro
import idaapi
import idautils


def main():
    targets = [
        item.strip().lower()
        for item in os.environ.get("TLL_IDA_STRINGS", "").split("|")
        if item.strip()
    ]
    results = {
        "input": ida_nalt.get_input_file_path(),
        "image_base": f"0x{idaapi.get_imagebase():X}",
        "targets": {},
    }
    strings = list(idautils.Strings())
    for target in targets:
        matches = []
        for item in strings:
            value = str(item)
            if target not in value.lower():
                continue
            xrefs = []
            for xref in idautils.XrefsTo(item.ea):
                function = ida_funcs.get_func(xref.frm)
                xrefs.append({
                    "xref": f"0x{xref.frm:X}",
                    "function": f"0x{function.start_ea:X}" if function else None,
                    "function_name": (
                        ida_name.get_ea_name(function.start_ea) if function else None
                    ),
                })
            matches.append({
                "address": f"0x{item.ea:X}",
                "value": value,
                "xrefs": xrefs,
            })
        results["targets"][target] = matches

    output_path = os.environ["TLL_IDA_OUTPUT"]
    with open(output_path, "w", encoding="utf-8") as output:
        json.dump(results, output, indent=2)
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
