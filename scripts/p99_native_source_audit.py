#!/usr/bin/env python3
"""Audit authored inputs for the P99 native-value test without executing tll code.

This intentionally does NOT emit a pretend 46-byte retarget-record fixture.
SkelTable builds the first 36 bytes from global matrices and an optional sampled
target profile, not by copying the local reference pose. Until that construction
is reproduced, the exact runtime-record numerical payload remains unresolved.

The two binary outputs contain only proven data, in the existing 21-transfer
order: .references.bin is 21 * (Chloe[48] + Nadine[48]) = 2016 bytes;
.record-tails.bin is 21 * (row20[9] + row157[9]) = 378 bytes. Each nine-byte tail
is little-endian <HHHHB for runtime offsets +24/+26/+28/+2A/+2C; unowned padding
+2D is deliberately absent. Neither binary is accepted as the future 3948-byte
21 * (row20[46] + row157[46] + Chloe[48] + Nadine[48]) bridge input.

All shipped inputs are build-pinned. Asset decompression uses the existing Oodle
reader; no MappedImage, tll launch, process attach, or game function is invoked.
An optional .profile-inventory.json inventories every anim-* / *-base.pak entry
in the specified actor archive. This filename-defined closure is NOT a claim
that all game archives or all possible clip-registration producers were scanned.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PREFIX = ROOT / "reports" / "p99-native-source-inputs"
PROFILE_ENTRY = "elena-base.pak"
PROFILE_ENTRY_SIZE = 409_616
PROFILE_ENTRY_SHA256 = "d77161503375df9a06ffc81646a88692dc437947274cf172ac7481be8d9b26f2"
PROFILE_ID = 0xFC34096C
PROFILE_REGISTRATION = 0x536F8
PROFILE_SLOT = (0, 0x51A98)
PROFILE_DESCRIPTOR = (0, 0x51DE0)
PROFILE_RAW = (0, 0x41F00)
PROFILE_RAW_OFFSET = 0x43AA0
PROFILE_SOURCE_SKELETON = 0x8592680F
PROFILE_DESCRIPTOR_DATABASE = 0x46622D19
TAIL_BYTES = 9
TRANSFER_BYTES = 2 * TAIL_BYTES
REFERENCE_PAIR_BYTES = 96


def extent(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f"{label}: extent {offset:#x}+{size:#x} is outside {len(data):#x}")
    return data[offset:offset + size]


def parameter_index(skeleton: source.SkeletonAsset, global_joint: int) -> int:
    """Match builder CF4B4..CF564: accumulate parameter counts, not workspace."""
    first = 0
    for group in skeleton.groups:
        if group.first <= global_joint < group.first + group.workspace_count:
            local = global_joint - group.first
            if local >= group.parameter_count:
                raise ValueError(f"joint {global_joint} has no compressed parameter")
            return first + local
        first += group.parameter_count
    raise ValueError(f"joint {global_joint} is outside skeleton")


def record_tail(record: source.RuntimeRecord, src: source.SkeletonAsset,
                dst: source.SkeletonAsset) -> bytes:
    if record.mode not in (1, 3):
        raise ValueError(f"unadmitted P99 retarget mode {record.mode}")
    # The builder writes mode as BYTE at CF577 and never initializes +2D.
    return struct.pack("<HHHHB", record.source_global,
                       parameter_index(src, record.source_global),
                       record.target_global,
                       parameter_index(dst, record.target_global), record.mode)


def builder_source_translation(data: bytes, inputs: dict, global_joint: int) -> dict:
    """Locate CF58B..CF5FF's GLOBAL-indexed input; do not fix its indexing.

    The full native builder packs source T into +1C/+1E/+20 using source-global
    +24, unlike the later boneScale calculation using compressed +26/+2A.
    Player's selected finger globals exceed the compressed reference count.
    These are exact disk bytes at the builder's offset, not proof of a complete
    live record, and no native call may use a truncated reference-only buffer.
    """
    if global_joint < 0:
        raise ValueError("negative builder source global")
    offset = inputs["reference_offset"] + global_joint * 48 + 0x20
    extent(data, offset, 12, "builder global-indexed source translation")
    return {"disk_offset": offset, "bytes": 12, "index_domain": "source-global",
            "outside_compressed_reference_rows": global_joint >= inputs["reference_count"]}


def validate_reference(row: bytes, label: str) -> None:
    if len(row) != 48:
        raise ValueError(f"{label}: wrong reference row size")
    values = struct.unpack("<12f", row)
    if not all(math.isfinite(v) for v in values[:3] + values[4:11]):
        raise ValueError(f"{label}: non-finite reference field")
    norm = sum(v * v for v in values[4:8])
    if not 0.998 <= norm <= 1.002:
        raise ValueError(f"{label}: non-unit reference quaternion ({norm})")


def skeleton_inputs(data: bytes, skeleton: source.SkeletonAsset) -> dict:
    spec = skeleton.spec
    raw = spec.raw_offset
    count = sum(group.parameter_count for group in skeleton.groups)
    reference_offset = raw + source.native.u32(data, raw + 0x20)
    references = extent(data, reference_offset, count * 48, "compressed references")
    for index in range(count):
        validate_reference(references[index * 48:(index + 1) * 48],
                           f"{spec.entry_name} reference {index}")
    matrix_offset = raw + source.native.u32(data, raw + 0x28)
    matrices = extent(data, matrix_offset, spec.joint_count * 48, "global matrices")
    if not all(math.isfinite(v) for v in struct.unpack(f"<{len(matrices) // 4}f", matrices)):
        raise ValueError(f"{spec.entry_name}: non-finite global matrix")
    parent_header = raw + source.native.u32(data, raw + 0x3C)
    extent(data, parent_header, 0x18, "parent header")
    parent_offset = parent_header + source.native.u32(data, parent_header + 0x14)
    parents = extent(data, parent_offset, spec.joint_count * 16, "parent records")
    parent_indices = [struct.unpack_from("<i", parents, index * 16 + 4)[0]
                      for index in range(spec.joint_count)]
    if any(index < -1 or index >= spec.joint_count for index in parent_indices):
        raise ValueError(f"{spec.entry_name}: parent outside global matrix array")
    return {
        "entry": spec.entry_name,
        "sha256": spec.entry_sha256,
        "reference_offset": reference_offset,
        "reference_count": count,
        "reference_sha256": source.sha256(references),
        "global_matrix_offset": matrix_offset,
        "global_matrix_count": spec.joint_count,
        "global_matrix_sha256": source.sha256(matrices),
        "parent_offset": parent_offset,
        "parent_sha256": source.sha256(parents),
        # Not serialized; these are complete owned copies, never live addresses.
        "references": references,
        "matrices": matrices,
        "parents": parent_indices,
    }


def sparse_sources(animation_data: bytes, animation: source.AnimationResolution,
                   skeleton: source.SkeletonAsset, groups: tuple[int, ...]) -> dict:
    result = {}
    for group_index in groups:
        spec = source.GROUP_PROGRAM_SPECS[(animation.spec.clip_id, group_index)]
        destinations, _ = source.parse_group_program(animation_data, animation, spec)
        group = skeleton.groups[group_index]
        processing = skeleton.processing[group.processing_index]
        if processing.joint_count != group.parameter_count:
            raise ValueError("processing and parameter counts differ")
        rows = source.selected_rows_from_destinations(destinations, processing.joint_count, spec)
        for local in rows:
            global_joint = group.first + local
            semantic = skeleton.joint_ids[global_joint]
            if semantic in result:
                raise ValueError("duplicated sparse source semantic")
            result[semantic] = (global_joint, group_index, local)
    return result


def profile_provenance(data: bytes) -> dict:
    source.require_blob(PROFILE_ENTRY, data, PROFILE_ENTRY_SIZE, PROFILE_ENTRY_SHA256)
    pak = source.PagedPak(data)
    matches = source.find_all(data, struct.pack("<I", PROFILE_ID))
    if matches != (PROFILE_REGISTRATION,):
        raise ValueError("profile ID occurrences changed")
    if source.native.u32(data, PROFILE_REGISTRATION + 4) != 0:
        raise ValueError("profile registration reserved word changed")
    slot_page, slot_local, slot = pak.resolve_pointer(PROFILE_REGISTRATION + 8)
    desc_page, desc_local, desc = pak.resolve_pointer(slot)
    raw_page, raw_local, raw = pak.resolve_pointer(desc + 0x28)
    if ((slot_page, slot_local) != PROFILE_SLOT or (desc_page, desc_local) != PROFILE_DESCRIPTOR
            or (raw_page, raw_local) != PROFILE_RAW or raw != PROFILE_RAW_OFFSET):
        raise ValueError("profile paged registration graph changed")
    if source.native.u32(data, raw) != 0x41430102:
        raise ValueError("profile ANIM magic changed")
    size = source.native.u32(data, raw + 8)
    payload = extent(data, raw, size, "profile ANIM")
    if (source.native.u32(data, raw + 4) != PROFILE_SOURCE_SKELETON
            or source.native.u32(data, desc + 0x40) != PROFILE_DESCRIPTOR_DATABASE):
        raise ValueError("profile source skeleton/database changed")
    if raw_local + size > pak.pages[raw_page].size:
        raise ValueError("profile ANIM crosses its fixed-up page")
    return {
        "entry": PROFILE_ENTRY, "entry_sha256": PROFILE_ENTRY_SHA256,
        "registration_offset": PROFILE_REGISTRATION,
        "slot": list(PROFILE_SLOT), "descriptor": list(PROFILE_DESCRIPTOR),
        "raw": list(PROFILE_RAW), "raw_offset": raw, "raw_bytes": size,
        "raw_sha256": source.sha256(payload),
        "source_skeleton": f"{PROFILE_SOURCE_SKELETON:08X}",
        "descriptor_database": f"{PROFILE_DESCRIPTOR_DATABASE:08X}",
        "required_target_database": f"{source.NADINE_DATABASE_ID:08X}",
        "group_count": source.native.u16(data, raw + 0x0C),
        "frame_count": source.native.u16(data, raw + 0x22),
        "flags": source.native.u16(data, raw + 0x20),
        "live_residency_proven": False,
        "note": "Disk registration alone does not prove the target-database lookup result."
    }


def profile_inventory(actor) -> dict:
    """Bounded disk inventory, not an assertion about the live exact lookup.

    A four-byte match is reported as a byte occurrence only. The independently
    build-pinned Elena graph is the sole registration we know how to validate.
    ArtItemAnim+40 rebinding and registration from other archives remain outside
    this filename-selected inventory, so neither a miss nor a database mismatch
    may be upgraded to a proof that row157 uses the no-profile builder branch.
    """
    # anim-* also includes sixteen small .txt dependency manifests in actor77;
    # retain those so the stated filename closure is exact, not silently PAK-only.
    names = sorted(name for name in actor.names
                   if name.startswith("anim-") or name.endswith("-base.pak"))
    if len(names) != len(set(names)):
        raise ValueError("duplicate profile-inventory archive entry")
    entries = []
    proven = []
    needle = struct.pack("<I", PROFILE_ID)
    for name in names:
        data = actor.extract(name)
        if len(data) != actor.entry_size(name):
            raise ValueError(f"profile-inventory extracted length mismatch: {name}")
        occurrences = source.find_all(data, needle)
        entries.append({"entry": name, "bytes": len(data), "sha256": source.sha256(data),
                        "profile_id_byte_offsets": list(occurrences)})
        if name == PROFILE_ENTRY:
            registration = profile_provenance(data)
            pak = source.PagedPak(data)
            desc_page, desc_local = PROFILE_DESCRIPTOR
            overlaps = [local for (page, local) in pak.fixups
                        if page == desc_page and local < desc_local + 0x44
                        and local + 8 > desc_local + 0x40]
            if overlaps:
                raise ValueError("profile numeric database overlaps a pointer fixup")
            registration["descriptor_database_overlapping_pointer_fixups"] = overlaps
            registration["unchanged_descriptor_can_match_target_database"] = False
            proven.append(registration)
    manifest = json.dumps(entries, sort_keys=True, separators=(",", ":")).encode()
    return {
        "schema": 1,
        "profile_id": f"{PROFILE_ID:08X}",
        "target_database": f"{source.NADINE_DATABASE_ID:08X}",
        "scope": {
            "selection": "case-sensitive names starting anim- or ending -base.pak (including anim-*.txt)",
            "selected_entry_count": len(entries),
            "selected_pak_count": sum(item["entry"].endswith(".pak") for item in entries),
            "selected_uncompressed_bytes": sum(item["bytes"] for item in entries),
            "archive_name_count": len(actor.names),
            "all_selected_entries_scanned": True,
            "all_archive_entries_scanned": len(entries) == len(actor.names),
            "all_game_archives_scanned": False,
            "all_registration_producers_proven": False,
        },
        "entries_manifest_sha256": source.sha256(manifest),
        "byte_occurrence_count": sum(len(item["profile_id_byte_offsets"]) for item in entries),
        "known_registration_count": len(proven),
        "known_registrations": proven,
        "entries": entries,
        "native_profile_lookup_result": "unresolved",
        "native_no_profile_branch_proven": False,
        "live_residency_proven": False,
        "tll_function_calls": 0,
        "producer_audit": {
            "exact_lookup": "1417C8BA0 checks registry key then descriptor+40 at1417C8C6A; no inheritance fallback",
            "builder_call": "1417CDF35 calls exact lookup with target skeleton Art+24 database",
            "callback_install": "GameLoading::Initialize141CC4940 installs141CC0BF0 at config+38 (141CC4E50..57); PackageMgr::Init141625910 copies to manager+38 at1416259E7",
            "callback_dispatch": "PackageMgr::UpdateLoginPackage141627F60 case0B calls1419C4D50;1419C4F1E invokes manager+38 with resource from loaded page plus disk login-item offset, without rewriting resource database",
            "registration": "PackageLoginResItem141CC0BF0 case42 calls LoginArtGroup1417C9070 at141CC2934 with resource+40",
            "registration_callback_gate": "1417C91EB checks Art+40 before observer slot0; registry insert does not rewrite Art+40",
            "default_observer": "RTTI AnimTableObserver vtable1428D5CA8/COL142C722D8/type14374A140; slot0 1406D7FD0 writes Art+48, not+40, and calls1412F9F00 which reads/gates+40; slot2/debug validation has no+40 store",
            "unresolved": "Other archive registrations, observer installations, and resource numeric-field producers before PackageMgr doing-login state0B are not exhaustively excluded.",
        },
    }


def generate(animation_data: bytes, skeleton_data: dict[str, bytes],
             retarget_data: bytes, profile_data: bytes) -> tuple[dict, bytes, bytes]:
    source.require_blob(source.ANIMATION_ENTRY_NAME, animation_data,
                        source.ANIMATION_ENTRY_SIZE, source.ANIMATION_ENTRY_SHA256)
    specs = (source.PLAYER_SKELETON, source.CHLOE_SKELETON, source.NADINE_SKELETON)
    skeletons = tuple(source.load_skeleton(skeleton_data[spec.entry_name], spec) for spec in specs)
    player, chloe, nadine = skeletons
    inputs = tuple(skeleton_inputs(skeleton_data[spec.entry_name], skeleton)
                   for spec, skeleton in zip(specs, skeletons))
    pak = source.PagedPak(animation_data)
    player_anim = source.resolve_animation(pak, source.PLAYER_ANIMATION)
    chloe_anim = source.resolve_animation(pak, source.CHLOE_ANIMATION)
    p_sources = sparse_sources(animation_data, player_anim, player, (0, 7))
    c_sources = sparse_sources(animation_data, chloe_anim, chloe, (0, 2))
    if set(p_sources) != set(c_sources) or len(p_sources) != source.EXPECTED_JOIN_COUNT:
        raise ValueError("D350/B8 sparse semantic set changed")
    table, count = source.parse_retarget_table(retarget_data)
    row20 = source.resolve_retarget(retarget_data, table, count, source.PLAYER_TO_CHLOE)
    row157 = source.resolve_retarget(retarget_data, table, count, source.CHLOE_TO_NADINE)
    records20 = source.resolve_runtime_records(row20, player, chloe)
    records157 = source.resolve_runtime_records(row157, chloe, nadine)
    policy = bytearray()
    tails = bytearray()
    references = bytearray()
    joins = []
    for semantic, (p_global, p_group, _p_local) in sorted(p_sources.items(), key=lambda item: item[1][0]):
        c_global, c_group, _c_local = c_sources[semantic]
        r20 = source.one_record(records20, semantic, "row20")
        r157 = source.one_record(records157, semantic, "row157")
        if not (r20.source_global == p_global and r20.target_global == c_global
                and r157.source_global == c_global and r20.target_id == semantic
                and r157.target_id == semantic and r20.mode == r157.mode):
            raise ValueError("same-StringId retarget join changed")
        n_group, _n_local = nadine.location(r157.target_global)
        policy.extend(struct.pack("<IHHHBBBB", semantic, p_global, c_global,
                                  r157.target_global, p_group, c_group, n_group, r20.mode))
        tail20 = record_tail(r20, player, chloe)
        tail157 = record_tail(r157, chloe, nadine)
        tails.extend(tail20 + tail157)
        c_param = parameter_index(chloe, c_global)
        n_param = parameter_index(nadine, r157.target_global)
        c_ref = inputs[1]["references"][48 * c_param:48 * (c_param + 1)]
        n_ref = inputs[2]["references"][48 * n_param:48 * (n_param + 1)]
        references.extend(c_ref + n_ref)
        joins.append({
            "semantic": f"{semantic:08X}", "player_global": p_global,
            "chloe_global": c_global, "nadine_global": r157.target_global,
            "mode": r20.mode, "row20_map_index": r20.map_index,
            "row157_map_index": r157.map_index,
            "row20_tail_24_through_2c": tail20.hex(),
            "row157_tail_24_through_2c": tail157.hex(),
            "chloe_reference_parameter": c_param, "nadine_reference_parameter": n_param,
            "chloe_reference_sha256": source.sha256(c_ref),
            "nadine_reference_sha256": source.sha256(n_ref),
            "player_parent_global": inputs[0]["parents"][p_global],
            "chloe_parent_global": inputs[1]["parents"][c_global],
            "nadine_parent_global": inputs[2]["parents"][r157.target_global],
            "row20_builder_source_translation": builder_source_translation(
                skeleton_data[source.PLAYER_SKELETON.entry_name], inputs[0], p_global),
            "row157_builder_source_translation": builder_source_translation(
                skeleton_data[source.CHLOE_SKELETON.entry_name], inputs[1], c_global),
        })
    if source.sha256(bytes(policy)) != source.EXPECTED_POLICY_MANIFEST_SHA256:
        raise ValueError("independently derived transfer policy differs from committed manifest")
    if len(references) != 21 * REFERENCE_PAIR_BYTES or len(tails) != 21 * TRANSFER_BYTES:
        raise ValueError("wrong proven-artifact size")
    profile = profile_provenance(profile_data)
    for spec in (source.NADINE_SKELETON, source.CHLOE_SKELETON):
        if source.find_all(skeleton_data[spec.entry_name], struct.pack("<I", PROFILE_ID)):
            raise ValueError(f"new profile ID occurrence in {spec.entry_name}")
    metadata = {
        "schema": "tll-p99-authored-inputs-v1", "classification": "STATIC",
        "game_launched_or_attached": False, "tll_function_calls": 0,
        "native_record_payload_complete": False, "native_bridge_ready": False,
        "future_complete_operand_bytes": 3948,
        "transfer_policy_sha256": source.EXPECTED_POLICY_MANIFEST_SHA256,
        "animation_sha256": source.ANIMATION_ENTRY_SHA256,
        "retarget_sha256": source.RETARGET_ENTRY_SHA256,
        "row20_authored_profile": f"{row20.spec.profile:08X}",
        "row157_authored_profile": f"{row157.spec.profile:08X}",
        "transfers": joins,
        "mode_counts": dict(sorted(Counter(join["mode"] for join in joins).items())),
        "skeleton_inputs": [{key: value for key, value in item.items()
                             if key not in ("references", "matrices", "parents")} for item in inputs],
        "profile": profile,
        "artifacts": {
            "references": {"bytes": len(references), "sha256": source.sha256(bytes(references)),
                           "layout": "21 consecutive Chloe[48], Nadine[48] pairs; not records"},
            "record_tails": {"bytes": len(tails), "sha256": source.sha256(bytes(tails)),
                             "layout": "21 row20[9], row157[9] pairs; each <HHHHB at +24..+2C; no +2D"},
        },
        "builder_evidence": {
            "metadata": "1417CF494/499 globals; CF4FF/564 compressed indices; CF577 byte mode",
            "parent_locators": "1417CF634..CFA6C consumes a6/a7 parent-global inverse matrices",
            "joint_locators": "1417CFA70..CFE78 consumes a6/a7 joint-global inverse matrices",
            "additional_fields": "1417CF58B..CFF includes source T halves and compressed bone-length ratio",
            "mixed_index_domains": "CF58B..CF5FF reads a4+48*source-global+20; CFE85..CFEF reads compressed +26/+2A",
            "optional_profile": "1417CDF23..CE198 lookup/sampling; 1417C8C6A requires target Art database",
            "reference_consumer": "141AE2EBE indexes complete raw-reference array by record+2A",
        },
        "remaining": [
            "Resolve whether FC34096C exact lookup for target ED25C708 yields another registered/rebound Art or null; this lookup has no retarget-inheritance fallback.",
            "If present, reproduce sampler1418AB8F0 at time0 and its target global matrix conversion.",
            "Keep full raw source extent: global640..676 T reads exceed Player's473 compressed reference rows; do not replace with compressed436..472.",
            "Reproduce numerical builder for 42 records including finite boneScale and both packed locators.",
            "Only then emit the complete3948-byte fixture, preflight all42 records, and invoke native bridge.",
        ],
        "row20_builder_fixture_requirements": {
            "profile_zero": True,
            "entry": "Owned DC authored row + fixed-up map; preallocated records pointer+18/count+20 bypasses allocator blockCE503..CE653.",
            "art_fields": "Owned exact +24database/+34jointcount/+40raw/+58joint-name-table; not a fabricated live identity.",
            "args4_and_5": "Complete owned source/target raw assets with pointers at raw+u32(raw+20), not shortened compressed-reference-only buffers.",
            "args6_and_7": "Pointers to owned authored global Mat34 arrays raw+u32(raw+28), plus checked parent array raw+raw3C+aux14.",
            "direct_math": ["Matrix4x4_InvertRigid14023ABB0", "Quaternion_FromOrthonormalBasis142177E50"],
            "tail_dependency": "SkelTable_GenerateSegmentMapping1417D0890 reads global1437E98C8 then manager+D8 before returning on null allocator; no silent global patching assumed.",
            "other_dependencies": ["__security_check_cookie1424EAB30", "_alloca_probe1424EBF10", "assertion callback146B17C08 on invalid operands"],
            "not_executed": True,
        },
    }
    return metadata, bytes(references), bytes(tails)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    base = source.native.DEFAULT_GAME / "Uncharted4_data" / "build" / "pc" / "thelostlegacy"
    parser.add_argument("--archive", type=Path, default=base / "actor77.psarc")
    parser.add_argument("--dc-archive", type=Path, default=base / "dc1.psarc")
    parser.add_argument("--exe", type=Path, default=source.native.DEFAULT_GAME / "tll.exe")
    parser.add_argument("--output-prefix", type=Path, default=DEFAULT_PREFIX)
    parser.add_argument("--check", action="store_true", help="verify existing artifacts without writes")
    parser.add_argument("--profile-inventory", action="store_true",
                        help="also inventory all anim-* / *-base.pak entries; not a live absence proof")
    args = parser.parse_args()
    with args.exe.open("rb") as executable:
        exe_digest = hashlib.file_digest(executable, "sha256").hexdigest()
    if exe_digest != source.native.EXPECTED_EXE_SHA256:
        raise ValueError(f"unsupported tll.exe SHA-256 {exe_digest}")
    audit = source.native.load_audit_module()
    actor = audit.Psarc(args.archive, None)
    data = {spec.entry_name: actor.extract(spec.entry_name)
            for spec in (source.PLAYER_SKELETON, source.CHLOE_SKELETON, source.NADINE_SKELETON)}
    metadata, references, tails = generate(actor.extract(source.ANIMATION_ENTRY_NAME), data,
        audit.Psarc(args.dc_archive, None).extract(source.RETARGET_ENTRY_NAME), actor.extract(PROFILE_ENTRY))
    metadata["executable_sha256"] = exe_digest
    products = {
        args.output_prefix.with_suffix(".json"): (json.dumps(metadata, indent=2, sort_keys=True) + "\n").encode(),
        args.output_prefix.with_suffix(".references.bin"): references,
        args.output_prefix.with_suffix(".record-tails.bin"): tails,
    }
    inventory = None
    if args.profile_inventory:
        inventory = profile_inventory(actor)
        inventory["archive"] = str(args.archive.resolve())
        inventory["executable_sha256"] = exe_digest
        products[args.output_prefix.with_suffix(".profile-inventory.json")] = (
            json.dumps(inventory, indent=2, sort_keys=True) + "\n").encode()
    for path, blob in products.items():
        if args.check:
            if not path.is_file() or path.read_bytes() != blob:
                raise ValueError(f"stale authored-input artifact: {path}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(blob)
    print(json.dumps({"artifacts": metadata["artifacts"], "transfers": len(metadata["transfers"]),
                      "mode_counts": metadata["mode_counts"], "native_bridge_ready": False,
                      "profile_inventory_entries": None if inventory is None else inventory["scope"]["selected_entry_count"],
                      "profile_inventory_byte_occurrences": None if inventory is None else inventory["byte_occurrence_count"],
                      "tll_function_calls": 0, "check": args.check}, indent=2))


if __name__ == "__main__":
    main()
