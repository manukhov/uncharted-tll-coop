import os
import sys

import ida_auto
import ida_funcs
import ida_name
import ida_pro
import idautils


FUNCTION_TARGETS = {
    0x1410F0D10: "GetRetainedPlayerEntityBySlot",
    0x1410F0DD0: "FindRetainedPlayerSlotIndex",
    0x1410F0F60: "ResolveRetainedPlayerHandleBySlot",
    0x140298D70: "ResolveLocalPlayerVFunc_A8",
    0x140289150: "Input_PerSlotApply",
    0x1402893D0: "Input_ApplyScriptedPerSlotState",
    0x14179E310: "Input_PerPlayerDispatch",
    0x14179E6C0: "Input_PerPlayerDispatch_MgrArg",
    0x140C021C0: "NetIsSecondPlayerAvailable",
    0x140C02220: "NetLoginSplitScreen",
}

DATA_RANGES = {
    (0x143EEBE80, 0x143EEBEDF): "g_RetainedPlayerSlotTable",
    (0x1437CF300, 0x1437CF320): "g_maxLocalPlayers neighborhood",
    (0x143E07460, 0x143E07480): "split/coop state neighborhood",
    (0x1466E4220, 0x1466E61DF): "g_InputStateBlocks[0..5]",
}


def describe(from_ea):
    function = ida_funcs.get_func(from_ea)
    if function is None:
        return f"0x{from_ea:X} <no function>"
    start = function.start_ea
    name = ida_name.get_name(start) or "<unnamed>"
    return f"0x{from_ea:X} in 0x{start:X} {name}"


def main():
    output_path = os.environ.get("TLL_IDA_OUTPUT")
    if output_path:
        sys.stdout = open(output_path, "w", encoding="utf-8")

    ida_auto.auto_wait()

    for target, label in FUNCTION_TARGETS.items():
        print(f"\n=== callers of {label} 0x{target:X} ===")
        seen = set()
        for xref in idautils.XrefsTo(target):
            if xref.frm in seen:
                continue
            seen.add(xref.frm)
            print(describe(xref.frm))

    for (start, end), label in DATA_RANGES.items():
        print(f"\n=== references into {label} 0x{start:X}..0x{end:X} ===")
        seen = set()
        for target in range(start, end + 1):
            for xref in idautils.XrefsTo(target):
                key = (xref.frm, target)
                if key in seen:
                    continue
                seen.add(key)
                print(f"target=0x{target:X} {describe(xref.frm)}")

    sys.stdout.flush()
    ida_pro.qexit(0)


main()
