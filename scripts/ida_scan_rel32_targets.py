import os
import struct

import ida_segment
import ida_bytes
import ida_name
import ida_funcs
import ida_lines
import idc


TARGETS = {
    0x1410F3B20: "PromoteRetainedPlayerSlot",
    0x1410C9F20: "InitializeEmbeddedPlayerAuthoredDescriptorState100",
    0x1410C9F50: "ZeroEmbeddedPlayerAuthoredDescriptorKey_8_10",
    0x1416860A0: "BoundTransform_SetWorldTransformRelativeToParent",
    0x141E7C5F0: "PostRefreshEmbeddedPlayerAuthoredDescriptorState100",
    0x141E7C600: "ComparePostRefreshDescriptorCarriers_141E7C600",
    0x141686120: "RefreshEmbeddedDescriptorStateFromExplicitVector_141686120",
    0x1417C32A0: "InitializeSelectorBackedProcessSpawnPacket",
    0x1417C35A0: "InitializeExtendedSelectorBackedRuntimeStatePacket_1428D5D60",
    0x1418C4840: "ApplyExternalStateAndTransformBlockIntoRuntimeStateSlot",
    0x140A90E80: "GetMeNetPlayerTrackerByScreenIndex",
    0x140B31F60: "NetPlayerTracker_SpawnPlayer_Candidate",
    0x140B346B0: "NetPlayerTracker_SpawnPlayerWrapper_Candidate",
    0x140A90F40: "FindNetPlayerTrackerEntryByRuntimePlayerObject_Candidate",
}

OUT_PATH = r"C:\users\vladimir\desktop\TLLCoopResearch\ida\scan-rel32-player-packet-targets.log"


def emit(out, text=""):
    out.write(text + "\n")


def func_name_at(ea):
    func = ida_funcs.get_func(ea)
    if not func:
        return "no_func"
    name = ida_name.get_ea_name(func.start_ea) or "<unnamed>"
    return f"{name} @ 0x{func.start_ea:X}"


def disasm_at(ea):
    line = idc.generate_disasm_line(ea, 0) or ""
    return ida_lines.tag_remove(line)


def scan_target(out, target, label):
    emit(out, f"TARGET 0x{target:X} {label}")
    found = 0
    seg = ida_segment.get_first_seg()
    while seg:
        start = seg.start_ea
        end = seg.end_ea
        data = ida_bytes.get_bytes(start, end - start)
        if data:
            for i in range(len(data) - 5):
                op = data[i]
                if op not in (0xE8, 0xE9):
                    continue
                disp = struct.unpack_from("<i", data, i + 1)[0]
                src = start + i
                dst = src + 5 + disp
                if dst != target:
                    continue
                mnemonic = "call" if op == 0xE8 else "jmp"
                emit(out, f"  {mnemonic} from 0x{src:X} ({func_name_at(src)}): {disasm_at(src)}")
                found += 1
        seg = ida_segment.get_next_seg(seg.start_ea)
    if not found:
        emit(out, "  no direct rel32 hits")
    emit(out, "")


def main():
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8", newline="\n") as out:
        for target, label in TARGETS.items():
            scan_target(out, target, label)
    idc.qexit(0)


if __name__ == "__main__":
    main()
