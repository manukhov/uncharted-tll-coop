import json

import ida_auto
import ida_idaapi
import ida_funcs
import ida_loader
import _ida_loader
import ida_name
import ida_pro
import idautils


TARGETS = {
    0x140048440: "RegisterScriptCommand_SpawnPlayer",
    0x14007D040: "RegisterNetSpawnCoopBuddyCommand",
    0x140B56090: "HandleNetSpawnCoopBuddy",
    0x140B580C0: "SelectRetainedCoopPlayerLookHash",
    0x140F73910: "ScriptSpawnPlayer",
    0x140F73C90: "CopySpawnDescriptor_0x184",
    0x1410F14C0: "CommitResolvedPlayerSpawn",
    0x1410F1910: "SpawnPlayerFromRetainedDescriptor",
    0x1410F3350: "SpawnPlayerFallback",
    0x141D22420: "ResolveRetainedRosterEntity",
    0x143EEBE80: "g_RetainedPlayerSlotTable",
}


def describe_xref(xref):
    function = ida_funcs.get_func(xref.frm)
    function_start = function.start_ea if function else None
    return {
        "xref": f"0x{xref.frm:X}",
        "type": xref.type,
        "function": f"0x{function_start:X}" if function_start is not None else None,
        "function_name": ida_name.get_name(function_start) if function_start is not None else None,
    }


def describe_ref(from_ea):
    function = ida_funcs.get_func(from_ea)
    function_start = function.start_ea if function else None
    return {
        "ref": f"0x{from_ea:X}",
        "function": f"0x{function_start:X}" if function_start is not None else None,
        "function_name": ida_name.get_name(function_start) if function_start is not None else None,
    }


def main():
    ida_auto.auto_cancel(0, ida_idaapi.BADADDR)

    results = {}
    for ea, label in TARGETS.items():
        results[f"0x{ea:X}"] = {
            "label": label,
            "xrefs_to": [describe_xref(xref) for xref in idautils.XrefsTo(ea)],
            "code_refs_to": [describe_ref(ref) for ref in idautils.CodeRefsTo(ea, True)],
            "data_refs_to": [describe_ref(ref) for ref in idautils.DataRefsTo(ea)],
        }

    output_path = r"C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\retained_spawn_xrefs.json"
    with open(output_path, "w", encoding="utf-8") as output:
        json.dump(results, output, indent=2)

    _ida_loader.save_database(
        r"C:\Users\Vladimir\Desktop\TLLCoopResearch\ida\tll.i64",
        ida_loader.DBFL_KILL,
    )
    ida_pro.qexit(0)


main()
