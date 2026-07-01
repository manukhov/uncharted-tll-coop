import json

import ida_funcs
import ida_name
import ida_pro
import idautils


TARGETS = (
    "player-move-stick.cpp",
    "player-disable-move-stick",
    "player-force-move-stick-dir",
    "move-stick-speed",
    "StickMoveInterface",
)
OUTPUT = r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\move_stick_string_xrefs.json"


def main():
    results = {}
    strings = list(idautils.Strings())
    for target in TARGETS:
        matches = []
        for item in strings:
            value = str(item)
            if target.lower() not in value.lower():
                continue
            refs = []
            for xref in idautils.XrefsTo(item.ea):
                func = ida_funcs.get_func(xref.frm)
                refs.append(
                    {
                        "xref": f"0x{xref.frm:X}",
                        "function": f"0x{func.start_ea:X}" if func else None,
                        "name": ida_name.get_ea_name(func.start_ea) if func else None,
                    }
                )
            matches.append(
                {"address": f"0x{item.ea:X}", "value": value, "xrefs": refs}
            )
        results[target] = matches
    with open(OUTPUT, "w", encoding="utf-8") as output:
        json.dump(results, output, indent=2)
    ida_pro.qexit(0)


if __name__ == "__main__":
    main()
