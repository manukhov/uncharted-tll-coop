import json
import os

import ida_auto
import ida_funcs
import ida_idaapi
import ida_loader
import _ida_loader
import ida_nalt
import ida_name
import ida_pro
import idaapi
import idautils


TARGETS = (
    "logicalPlayerID < g_maxLocalPlayers",
    "Attempting to set health on a remote player",
    "spawn-player",
    "net-spawn-coop-buddy",
    "net-force-respawn-player",
    "net-get-net-player-from-joypad",
    "net-set-coop-succeeded",
    "net-set-coop-failed",
    "net-switch-coop-mode-enabled",
    "CreateNetMenu",
    "CreateLevelDebugMenu",
    "Debug DMENU",
    "Debug Console",
    "manual-cam",
    "process-list",
    "Select Player",
)


def describe_xref(xref):
    function = ida_funcs.get_func(xref.frm)
    if function is None:
        return {
            "xref": f"0x{xref.frm:X}",
            "function": None,
            "function_name": None,
        }

    return {
        "xref": f"0x{xref.frm:X}",
        "function": f"0x{function.start_ea:X}",
        "function_name": ida_name.get_name(function.start_ea) or None,
    }


def main():
    ida_auto.auto_cancel(0, ida_idaapi.BADADDR)

    strings = list(idautils.Strings())
    results = {
        "input": ida_nalt.get_input_file_path(),
        "image_base": f"0x{idaapi.get_imagebase():X}",
        "targets": {},
    }

    for target in TARGETS:
        matches = []
        target_lower = target.lower()

        for item in strings:
            value = str(item)
            if target_lower not in value.lower():
                continue

            matches.append(
                {
                    "address": f"0x{item.ea:X}",
                    "value": value,
                    "xrefs": [
                        describe_xref(xref)
                        for xref in idautils.XrefsTo(item.ea)
                    ],
                }
            )

        results["targets"][target] = matches

    output_path = os.environ.get(
        "TLL_IDA_REPORT",
        r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\tll_xrefs.json",
    )
    with open(output_path, "w", encoding="utf-8") as output:
        json.dump(results, output, indent=2)

    _ida_loader.save_database(
        r"C:\Users\Vladimir\Desktop\TLLCoopResearch\ida\tll.i64",
        ida_loader.DBFL_KILL | ida_loader.DBFL_COMP,
    )
    ida_pro.qexit(0)


main()
