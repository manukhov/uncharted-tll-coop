"""Decode shipped default Hands before comparing P99's small partial layer.

Disk-only and scalar. Group zero contains only constant22/24 commands even
though the complete Chloe/Player ANIM headers advertise337/301 samples.
R20/R157 numerical records remain conditional AGZ operands, not observed live
profile lookup. A near match or a small fitted weight is not a recovered command.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct

import p99_grip_composition_audit as grip
import p99_live_grip_baseline as live
import p99_npc_support_constraint as support


SPECS = {
    "chloe_default_hands": {
        "name": "clo-uneasy-idle-hands", "id": 0x7D11D4FD,
        "entry": "anim-chloe-relaxed-movement.pak", "entry_bytes": 5301728,
        "entry_sha": "e25ff53fd6f4de161714ba39599b3d46189d4cc099d8858765fee0c2e4f99a9d",
        "descriptor": (4, 0x24E0), "raw_location": (1, 0xE5400), "raw": 0x1F0920,
        "size": 0x2E80, "skeleton": 0x4B0F5DE3, "samples": 337, "groups": 7,
        "raw_sha": "e943884491e17ab8fb042eb1d32e036f84fc07e1125c9b2c371e886e0d9288b8",
        "group0": (0x120, 0x1B590000, 0x990, 0),
        "program_sha": "fe11324f5cd4321242f60f82fcc7bc78299413599e88f5ac724cff6e23e6777f",
        "source_globals": (38, 39, 40, 41), "sentinel": 4272,
    },
    "player_default_hands": {
        "name": "idle-hands", "id": 0x7FB80CC8,
        "entry": "anim-player-misc.pak", "entry_bytes": 7015344,
        "entry_sha": "b5c1d01a92407bc04e1a0fdce544b771518b5d948b59ecd1883d3da7d5ebefc6",
        "descriptor": (6, 0x50610), "raw_location": (4, 0xF90D0), "raw": 0x4FB610,
        "size": 0x5CA0, "skeleton": 0xFD6DBE03, "samples": 301, "groups": 8,
        "raw_sha": "1cfd7e0d8283510862fb4f07347ac574270a626dbe5e437861e7b48073621613",
        "group0": (0x140, 0x1B470000, 0xA40, 0),
        "program_sha": "8b47c139a1a5bab4eb7fc8454a1b7972db4ac91b94b2543951c379680d166437",
        "source_globals": (29, 30, 31, 32), "sentinel": 3408,
    },
}


def decode_clip(data, spec, parser):
    source = support.source
    source.require_blob(spec["entry"], data, spec["entry_bytes"], spec["entry_sha"])
    if parser.PACKED_QUATERNION_MODEL != support.DECODER_MODEL:
        raise ValueError("corrected opcode24 decoder changed")
    pak = source.PagedPak(data)
    descriptor_page, descriptor_local = spec["descriptor"]
    descriptor = pak.absolute(descriptor_page, descriptor_local)
    # Named descriptors store same-page name/type offsets, not fixed-up pointers.
    strings = []
    for field in (0, 8):
        at = pak.absolute(descriptor_page, source.native.u64(data, descriptor + field))
        page = pak.pages[descriptor_page]
        end = data.find(b"\0", at, min(at + 512, page.file_offset + page.size))
        if end < 0:
            raise ValueError("unterminated descriptor name/type")
        strings.append(data[at:end])
    if strings != [spec["name"].encode("ascii"), b"ANIM"] or parser.string_id(strings[0]) != spec["id"]:
        raise ValueError("default Hands descriptor identity changed")
    page, local, raw = pak.resolve_pointer(descriptor + 0x28)
    if (page, local) != spec["raw_location"] or raw != spec["raw"]:
        raise ValueError("default Hands raw link changed")
    if (source.native.u32(data, raw) != 0x41430102 or
            source.native.u32(data, raw + 4) != spec["skeleton"] or
            source.native.u32(data, raw + 8) != spec["size"] or
            source.native.u16(data, raw + 0xC) != spec["groups"] or
            source.native.u32(data, raw + 0x14) != 0x30 or
            source.native.u16(data, raw + 0x20) != 0 or
            source.native.u16(data, raw + 0x22) != spec["samples"]):
        raise ValueError("default Hands ANIM header changed")
    if hashlib.sha256(data[raw:raw + spec["size"]]).hexdigest() != spec["raw_sha"]:
        raise ValueError("default Hands raw payload changed")
    if struct.unpack_from("<4I", data, raw + 0x30) != spec["group0"]:
        raise ValueError("group-zero descriptor changed")
    program = raw + spec["group0"][0]
    if hashlib.sha256(data[program:program + 160]).hexdigest() != spec["program_sha"]:
        raise ValueError("constant group-zero program changed")
    maps = []
    for offset, opcode, payload in ((0, 22, 0x20), (8, 24, 0x60)):
        if struct.unpack_from("<4H", data, program + offset) != (0x300 + opcode, 8, 3, payload):
            raise ValueError("group zero is no longer exactly constant22/24")
        maps.append(struct.unpack_from("<8H", data, program + payload))
    if source.native.u16(data, program + 16) != 0:
        raise ValueError("group-zero constant program no longer terminates")
    globals_ = spec["source_globals"]
    if (set(maps[0]) != {48*j + f for j in globals_ for f in (0, 32)} or
            set(maps[1]) != {48*j + 16 for j in globals_} | {spec["sentinel"]} or
            maps[1].count(spec["sentinel"]) != 4):
        raise ValueError("default Hands destination map changed")
    mask_at = raw + 0x30 + spec["groups"] * 16
    if struct.unpack_from("<QQ", data, mask_at) != (sum(1 << (63-j) for j in globals_), 0):
        raise ValueError("default Hands group-zero valid mask changed")
    decoded = source.native.decode_constant_pose(parser, data, raw)
    rows = {j: decoded[j] for j in globals_}
    for row in rows.values():
        support._unit_rigid(row, "decoded default Hands")
    return rows, maps


def audit(actor_archive=support.DEFAULT_ACTOR_ARCHIVE, include_fit=True):
    source = support.source
    parser = source.native.load_audit_module()
    actor = parser.Psarc(Path(actor_archive), None)
    inputs = Path(str(grip.DEFAULT_PREFIX) + ".full-input.bin").read_bytes()
    grip.pinned(inputs, "input")
    transfers = grip.load_metadata(grip.DEFAULT_METADATA)
    if [(r["player_global"], r["chloe_global"], r["nadine_global"]) for r in transfers[:2]] != [(30, 39, 28), (32, 41, 30)]:
        raise ValueError("palm/prop semantic join changed")
    observed = live.observed_rows()
    clips = {}
    for label, spec in SPECS.items():
        rows, maps = decode_clip(actor.extract(spec["entry"]), spec, parser)
        target = {}
        for index, transfer in enumerate(transfers[:2]):
            chunk = inputs[index*188:(index+1)*188]
            value = rows[transfer["chloe_global"] if label.startswith("chloe") else transfer["player_global"]]
            if label.startswith("player"):
                value = grip.retarget(chunk[:46], value, grip.read_row(chunk, 92))
            target[transfer["nadine_global"]] = grip.retarget(chunk[46:92], value, grip.read_row(chunk, 140))
        pair = support.compose(target[28], target[30])
        clips[label] = {
            **{key: value for key, value in spec.items() if key != "samples"},
            "header_sample_count": spec["samples"],
            "header_sample_count_is_not_group0_variability": True,
            "group0_opcodes": [22, 24, 0], "group0_destination_maps": maps,
            "group0_is_constant": True, "source_rows": rows,
            "conditional_retarget_path": "R157" if label.startswith("chloe") else "R20_then_R157",
            "conditional_target_rows": target, "conditional_wrist_to_prop": pair,
            "samples": [{"sample": i, "rotation_gap_degrees": support._angular_degrees(pair, current),
                         "position_gap_asset_units": support._origin_distance(pair, current)}
                        for i, current in enumerate(observed, 1)],
        }
    result = {
        "schema": "tll-p99-default-hands-baseline-v1",
        "classification": "STATIC_CONDITIONAL_DEFAULT_HANDS_COMPARISON",
        "capture_pid": 17512, "capture_getter_pairs_f32_sha256": live.GETTER_PAIRS_SHA256,
        "conditional_record_input_sha256": grip.PINS["input"][1], "clips": clips,
        "native_profile_choice_observed": False, "live_clip_selection_proven": False,
        "live_layer_weight_proven": False, "live_local_rows_observed": False,
        "runtime_writer_admitted": False, "visual_fix_proven": False,
        "limits": [
            "Only group0 is decoded;337/301 header samples do not make this constant group animated.",
            "R157 remains the pinned conditional AGZ basis, not proved live profile selection.",
            "Player path is conditional R20 thenR157, NOT direct profile-present R124.",
            "Four measurements are PID17512, not the prior truncated PID21244 diagnostic.",
            "Near-equality of product does not identify palm28 and prop30 separately.",
            "Numeric fitting does not identify live clip provenance or native layer weights.",
        ],
    }
    if include_fit:
        direct_report = live.direct.audit(live.npc.DEFAULT_NADINE_ASSET,
            actor_archive, live.npc.DEFAULT_DC_ARCHIVE, include_local_rows=True)
        candidate = {r["target_global"]: r["direct_row"]
                     for r in direct_report["local_joint_rows"]["rows"]}
        chloe_candidate = {r["target_global"]: r["proposed_row"]
                           for r in direct_report["local_joint_rows"]["rows"]}
        default = clips["chloe_default_hands"]["conditional_target_rows"]
        result["default_to_conditional_R124_D350_B8_numeric_fit_NOT_native_weight"] = [
            {"sample": i, **live.compare(default, candidate, current)}
            for i, current in enumerate(observed, 1)]
        result["default_to_conditional_Chloe_R157_R20_B8_numeric_fit_NOT_native_weight"] = [
            {"sample": i, **live.compare(default, chloe_candidate, current)}
            for i, current in enumerate(observed, 1)]
    return result


if __name__ == "__main__":
    print(json.dumps(audit(), indent=2, allow_nan=False))
