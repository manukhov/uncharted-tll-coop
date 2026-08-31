#!/usr/bin/env python3
"""Generate exact one-sample P99 Chloe-order source rows from shipped assets.

This is a deterministic, non-circular generator.  It discovers D350/B8 through
the paged-PAK registration/fixup graph, derives their complete sparse row sets
from the opcode-22/24 destination maps, joins those rows by skeleton StringId
through the authored Player->Chloe and Chloe->Nadine DC maps, and only then
emits the 21 source-row pairs used by the runtime.  Every executable and asset
input, structural table, sparse program, semantic manifest, and decoded row is
build-pinned.  The script never launches or attaches to the game.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct

import native_anim_pose_audit as native


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "scanner" / "src" / "nadine_p99_chloe_source_rows.h"

ANIMATION_ENTRY_NAME = "anim-player-movement.pak"
RETARGET_ENTRY_NAME = "skel-retargets.bin"
ANIMATION_ENTRY_SIZE = 60_978_256
ANIMATION_ENTRY_SHA256 = (
    "337cb2c4cfdc51cb71d2291f7af7f4bff478e21c3a53e986a22bfc22248f470c"
)
RETARGET_ENTRY_SIZE = 12_627
RETARGET_ENTRY_SHA256 = (
    "227fa31a5deab04561f37c3fdd2a38b429ab426889cfe0498bbd3f93cb1e0a50"
)

PLAYER_DATABASE_ID = 0x27E57F43
PLAYER_SKELETON_ID = 0xFD6DBE03
CHLOE_DATABASE_ID = 0x6C2B2396
CHLOE_SKELETON_ID = 0x4B0F5DE3
NADINE_DATABASE_ID = 0xED25C708
NADINE_SKELETON_ID = 0xC39CA709

PROTO_BASE_ID = 0x1003D654
CHLOE_BASE_ID = 0xB70B7EAF
NADINE_BASE_ID = 0xC67A06F1
PLAYER_CLIP_ID = 0xD350A452
CHLOE_CLIP_ID = 0xB8FC6B25

SKEL_TABLE_OBJECT_ID = 0xFE014FD2
SKEL_TABLE_TYPE_ID = 0x8E53B36E
EXPECTED_JOIN_COUNT = 21
EXPECTED_POLICY_MANIFEST_SHA256 = (
    "11af5389f7b47ac48cca71b10a03abbe215ffcfd0772d9e806c61605e1c38502"
)
EXPECTED_RICH_MANIFEST_SHA256 = (
    "5107aff8bee5752b0af5153a7e07d3d055b06415139ddf4d75854d6ef71189b0"
)
EXPECTED_PLAYER_ROWS_SHA256 = (
    "9d3a72550481469533c9c1df82f864d5027c75e1fbe7dc7f2e317f0781bba727"
)
EXPECTED_CHLOE_ROWS_SHA256 = (
    "1735098f297a40b996325966f95307ed07fc7c98276aa27877967dc098be2ecf"
)
EXPECTED_COMBINED_ROWS_SHA256 = (
    "0aefc3ea21b16d2059160052888b4508a4a213ba9f2d42ec1cd2d82ff5ccb0b2"
)


@dataclass(frozen=True)
class Page:
    file_offset: int
    size: int


class PagedPak:
    """Resolve the build's page-local pointers using its metadata fixups."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        if len(data) < 0x2C:
            raise ValueError("truncated paged PAK")
        page_count = native.u32(data, 0x10)
        page_table = native.u32(data, 0x14)
        if not page_count or page_count > 0x10000:
            raise ValueError(f"invalid page count {page_count}")
        if page_table + page_count * 0xC > len(data):
            raise ValueError("truncated page table")
        pages: list[Page] = []
        for index in range(page_count):
            file_offset, size, _flags = struct.unpack_from(
                "<III", data, page_table + index * 0xC
            )
            if file_offset + size > len(data):
                raise ValueError(f"page {index} extends beyond the PAK")
            pages.append(Page(file_offset, size))
        self.pages = tuple(pages)

        fixup_descriptor = page_table + page_count * 0xC
        if native.u32(data, 0x18) != 8:
            raise ValueError("unexpected PAK fixup-record stride")
        if native.u32(data, 0x1C) != fixup_descriptor:
            raise ValueError("PAK fixup descriptor is not after the page table")
        if fixup_descriptor + 0x10 > pages[0].file_offset:
            raise ValueError("truncated fixup descriptor")
        fixup_page, fixup_table, fixup_count, fixup_reserved = struct.unpack_from(
            "<IIII", data, fixup_descriptor
        )
        if fixup_page != 0 or fixup_reserved != 0:
            raise ValueError("unexpected fixup descriptor")
        if (
            fixup_table < fixup_descriptor + 0x10
            or fixup_table + fixup_count * 8 > pages[0].file_offset
        ):
            raise ValueError("fixup table is outside PAK metadata")
        fixups: dict[tuple[int, int], list[int]] = defaultdict(list)
        for index in range(fixup_count):
            offset = fixup_table + index * 8
            source_page, target_page, pointer_local = struct.unpack_from(
                "<HHI", data, offset
            )
            if source_page >= page_count or target_page >= page_count:
                raise ValueError(f"fixup {index} has an invalid page")
            if pointer_local + 8 > pages[source_page].size:
                raise ValueError(f"fixup {index} points outside its source page")
            fixups[(source_page, pointer_local)].append(target_page)
        self.fixups = fixups

    def absolute(self, page: int, local: int) -> int:
        if page < 0 or page >= len(self.pages):
            raise ValueError(f"page {page} is outside the PAK")
        descriptor = self.pages[page]
        if local < 0 or local >= descriptor.size:
            raise ValueError(f"page {page}+0x{local:X} is outside the page")
        return descriptor.file_offset + local

    def page_location(self, absolute: int) -> tuple[int, int]:
        for index, page in enumerate(self.pages):
            if page.file_offset <= absolute < page.file_offset + page.size:
                return index, absolute - page.file_offset
        raise ValueError(f"offset 0x{absolute:X} is outside every page")

    def resolve_pointer(self, absolute: int) -> tuple[int, int, int]:
        source_page, source_local = self.page_location(absolute)
        if source_local + 8 > self.pages[source_page].size:
            raise ValueError("pointer field crosses its source page boundary")
        targets = self.fixups.get((source_page, source_local), [])
        if len(targets) != 1:
            raise ValueError(
                f"pointer page {source_page}+0x{source_local:X} has target "
                f"pages {sorted(targets)}"
            )
        target_page = targets[0]
        target_local = native.u64(self.data, absolute)
        target_absolute = self.absolute(target_page, target_local)
        return target_page, target_local, target_absolute

    def find_in_page(self, page: int, needle: bytes) -> tuple[int, ...]:
        descriptor = self.pages[page]
        start = descriptor.file_offset
        end = start + descriptor.size
        hits: list[int] = []
        cursor = start
        while True:
            hit = self.data.find(needle, cursor, end)
            if hit < 0:
                return tuple(hits)
            hits.append(hit - start)
            cursor = hit + 1


@dataclass(frozen=True)
class AnimationSpec:
    clip_id: int
    skeleton_id: int
    registration: tuple[int, int]
    slot: tuple[int, int]
    descriptor: tuple[int, int]
    raw_location: tuple[int, int]
    raw_offset: int
    object_size: int
    flags: int
    group_count: int


@dataclass(frozen=True)
class AnimationResolution:
    spec: AnimationSpec
    registration_offset: int
    slot_offset: int
    descriptor_offset: int
    raw_offset: int


PLAYER_ANIMATION = AnimationSpec(
    PLAYER_CLIP_ID,
    PLAYER_SKELETON_ID,
    (0, 0x2FC50),
    (0, 0x5A60),
    (34, 0x96F08),
    (30, 0x88C20),
    0x1F4AF40,
    0x430,
    0,
    8,
)
CHLOE_ANIMATION = AnimationSpec(
    CHLOE_CLIP_ID,
    CHLOE_SKELETON_ID,
    (0, 0x5E580),
    (0, 0x59D0),
    (34, 0x97850),
    (30, 0x9F740),
    0x1F61A60,
    0x400,
    2,
    7,
)


@dataclass(frozen=True)
class GroupLayout:
    first: int
    processing_index: int
    workspace_count: int
    parameter_count: int


@dataclass(frozen=True)
class ProcessingLayout:
    joint_count: int
    scalar_count: int
    lane_count: int
    byte5: int
    first_parameter: int
    word8: int


@dataclass(frozen=True)
class SkeletonSpec:
    entry_name: str
    entry_size: int
    entry_sha256: str
    base_id: int
    database_id: int
    skeleton_id: int
    database_signature_offset: int
    raw_offset: int
    name_data_offset: int
    joint_table_offset: int
    joint_count: int
    group_count: int
    processing_relative: int
    groups: tuple[tuple[int, int], ...]
    header_sha256: str
    group_table_sha256: str
    processing_table_sha256: str
    joint_table_sha256: str
    joint_ids_sha256: str


@dataclass(frozen=True)
class SkeletonAsset:
    spec: SkeletonSpec
    groups: tuple[GroupLayout, ...]
    processing: tuple[ProcessingLayout, ...]
    joint_ids: tuple[int, ...]

    def location(self, global_joint: int) -> tuple[int, int]:
        matches = [
            (index, global_joint - group.first)
            for index, group in enumerate(self.groups)
            if group.first <= global_joint < group.first + group.workspace_count
        ]
        if len(matches) != 1:
            raise ValueError(
                f"{self.spec.entry_name} global joint {global_joint} has "
                f"locations {matches}"
            )
        return matches[0]


PLAYER_SKELETON = SkeletonSpec(
    "proto.pak",
    424_352,
    "fd3e58acf071fd0d3f3783c5fd29bd654d74efef42cbcf4033727340fabf99d0",
    PROTO_BASE_ID,
    PLAYER_DATABASE_ID,
    PLAYER_SKELETON_ID,
    0x2244,
    0x3FE0,
    0x42360,
    0x42520,
    702,
    9,
    0x160,
    (
        (0, 168), (168, 63), (231, 76), (307, 83), (390, 107),
        (497, 52), (549, 90), (639, 38), (677, 25),
    ),
    "375a64b1d42b8a6cf01db9512cfc8ac136332f975136d3fe6e00486cf43b5178",
    "80973c775d7a942e3e23dfab67f33c6b78cb94cee27bd603edfde02c0eeec419",
    "fc8c7fdf2c6b716ffd41d9db0147257a25d9ecac9a0738d258d9a24f35ee44ed",
    "d948ef1679873ec71737a1c17be72f045cb614f217fdfe616b24795a7d91fde4",
    "8c96a0de5e8b2a15e1e798573c534fa7246308f7dd70f3a24b005530120430fa",
)
CHLOE_SKELETON = SkeletonSpec(
    "chloe-base.pak",
    783_536,
    "13b24c6068d6ef42be0ca683be94d8d891477fa09dc2c8dd1de84672c37d5109",
    CHLOE_BASE_ID,
    CHLOE_DATABASE_ID,
    CHLOE_SKELETON_ID,
    0x58324,
    0x5A180,
    0x971F0,
    0x97388,
    651,
    8,
    0x140,
    (
        (0, 205), (205, 62), (267, 38), (305, 72),
        (377, 83), (460, 107), (567, 59), (626, 25),
    ),
    "526cf5f7e89b098bd90646e1dd614502019bfdb0233cdb8f531a6149215f1d8e",
    "2e745ce72ed1a82add4f8f9fb2aa1bca0aaae8c09fef042dd39ca32004c73b93",
    "57a02d4815966091a1d8bb6abc5c49cbcfc1c919d54c2ed36db936926a9b54dd",
    "6a524b28659e1aa93ace07ee80c8bd42ba85a5b07fd4f17240490ff196a00075",
    "200ce915c2692923efce47e7b653092edad34718fe2c1dc4770c99da8d1dd31c",
)
NADINE_SKELETON = SkeletonSpec(
    "nadine-dlc-base.pak",
    732_928,
    "2bf66e61fae156a41952d8d2659e06215dbe8f0c827a9f6a89cada0c9490b06b",
    NADINE_BASE_ID,
    NADINE_DATABASE_ID,
    NADINE_SKELETON_ID,
    0x52244,
    0x53FE0,
    0x8B9A0,
    0x8BB38,
    580,
    8,
    0x140,
    (
        (0, 104), (104, 38), (142, 119), (261, 83),
        (344, 107), (451, 52), (503, 52), (555, 25),
    ),
    "9d5b1a0f96c6525fc8c8e2d7e1ea45432d79c9d92b9e6292bcc71a8ee2f8ad0f",
    "39eab8cd37ef109e3180d34059561a21260c3763c96d8f073dc8357e0d763add",
    "d51c3bff02f4e27a820cd4eb24c77dcbe0aee4a92c90e6d6cc60d107bbf5864b",
    "fb9dcfa783e5a096a20288deac7992bcbd9f423ff1c28af98e6680dbb6df48fa",
    "7cdce1ed177918db3bc9a0eac315687913296a3120150a025ed2b9a5d86588a2",
)


@dataclass(frozen=True)
class OpcodeMapSpec:
    count: int
    sentinel_count: int
    sha256: str


@dataclass(frozen=True)
class GroupProgramSpec:
    clip_id: int
    source_group: int
    descriptor_words: tuple[int, int, int, int]
    program_bytes: int
    program_fnv1a64: int
    opcode22: OpcodeMapSpec
    opcode24: OpcodeMapSpec


@dataclass(frozen=True)
class GroupProgram:
    source_group: int
    descriptor_words: tuple[int, int, int, int]
    program_bytes: int
    program_fnv1a64: int


GROUP_PROGRAM_SPECS = {
    (PLAYER_CLIP_ID, 0): GroupProgramSpec(
        PLAYER_CLIP_ID,
        0,
        (0x140, 0x1B470000, 0x420, 0),
        0x90,
        0x51865634305374E7,
        OpcodeMapSpec(8, 4, "969db4b62e29c4f7b9f4ba69f18ecc8473630fc461693bf0b98fa9c0f4508d21"),
        OpcodeMapSpec(8, 6, "1ea79420d4f5ff44ac7668654ff32203c4e49f489732f2d6a4c2266d3a810a8f"),
    ),
    (PLAYER_CLIP_ID, 7): GroupProgramSpec(
        PLAYER_CLIP_ID,
        7,
        (0x210, 0x00260000, 0x420, 0),
        0x220,
        0x7AA8B713792E90B8,
        OpcodeMapSpec(40, 2, "0848c49d99a2086e649213e45aeaf419c209306a36097fa7a1462d5de5b36ddc"),
        OpcodeMapSpec(24, 5, "2dcb82c511c90bd9c8202408e8b4b8a6edfcf639638aa952a9ebabba97375bf5"),
    ),
    (CHLOE_CLIP_ID, 0): GroupProgramSpec(
        CHLOE_CLIP_ID,
        0,
        (0x120, 0x1B590000, 0x3F0, 0),
        0x90,
        0xBE231073B493795A,
        OpcodeMapSpec(8, 4, "a9d56e9b1e56d48e4a0651c5641bdc8dbb8eed8ea86004ba97885a17ba83afff"),
        OpcodeMapSpec(8, 6, "75347358d7c4cc371f11548ba8ea209017d160674c5c8b10d176a58a2a43d6fd"),
    ),
    (CHLOE_CLIP_ID, 2): GroupProgramSpec(
        CHLOE_CLIP_ID,
        2,
        (0x1A0, 0x00260000, 0x3F0, 0),
        0x220,
        0x1B28AC09D1CAED0A,
        OpcodeMapSpec(40, 2, "0848c49d99a2086e649213e45aeaf419c209306a36097fa7a1462d5de5b36ddc"),
        OpcodeMapSpec(24, 5, "2dcb82c511c90bd9c8202408e8b4b8a6edfcf639638aa952a9ebabba97375bf5"),
    ),
}


@dataclass(frozen=True)
class DecodedGroup:
    source_group: int
    selected_rows: tuple[int, ...]
    rows: dict[int, tuple[int, ...]]
    program: GroupProgram


@dataclass(frozen=True)
class RetargetSpec:
    source_base_id: int
    target_base_id: int
    row_index: int
    row_offset: int
    map_offset: int
    authored_count: int
    runtime_count: int
    profile: int
    scale_bits: int
    flags: int
    row_sha256: str
    map_sha256: str


@dataclass(frozen=True)
class RetargetMap:
    spec: RetargetSpec
    authored: tuple[tuple[int, int, int], ...]


@dataclass(frozen=True)
class RuntimeRecord:
    map_index: int
    source_id: int
    target_id: int
    mode: int
    source_global: int
    target_global: int


PLAYER_TO_CHLOE = RetargetSpec(
    PROTO_BASE_ID,
    CHLOE_BASE_ID,
    20,
    0x410,
    0x2124,
    96,
    93,
    0,
    0x3F6BE76D,
    0,
    "33074deb73ed53628970cbf1b4bb75f170bb7700e87e377a0786ca5402d4018d",
    "408e29acdaad89a9d9f28f30a199b0b68206b610e8f663fd47e8e955299d6dcd",
)
CHLOE_TO_NADINE = RetargetSpec(
    CHLOE_BASE_ID,
    NADINE_BASE_ID,
    157,
    0x1DC0,
    0x2720,
    74,
    71,
    0xFC34096C,
    0x3F770A3D,
    0,
    "09f9c070919c289be7fc329aa2c3c2cf119b1e300d9509ac9a40a811eb131ffb",
    "8cabf0e8316342d566babee9b1301cefa598d02dbbb36d27ab999420b7f0b13e",
)


@dataclass(frozen=True)
class SourceRow:
    semantic_id: int
    group: int
    local: int
    global_joint: int
    words: tuple[int, ...]


@dataclass(frozen=True)
class Transfer:
    semantic_id: int
    player: SourceRow
    chloe: SourceRow
    nadine_group: int
    nadine_local: int
    nadine_global: int
    mode: int
    row20_map_index: int
    row157_map_index: int


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_blob(
    label: str, data: bytes, expected_size: int, expected_sha256: str
) -> None:
    if len(data) != expected_size:
        raise ValueError(
            f"unsupported {label} size {len(data)}; expected {expected_size}"
        )
    digest = sha256(data)
    if digest != expected_sha256:
        raise ValueError(
            f"unsupported {label} SHA-256 {digest}; expected {expected_sha256}"
        )


def find_all(data: bytes, needle: bytes) -> tuple[int, ...]:
    hits: list[int] = []
    cursor = 0
    while True:
        hit = data.find(needle, cursor)
        if hit < 0:
            return tuple(hits)
        hits.append(hit)
        cursor = hit + 1


def resolve_animation(pak: PagedPak, spec: AnimationSpec) -> AnimationResolution:
    registrations = pak.find_in_page(0, struct.pack("<II", spec.clip_id, 0))
    if registrations != (spec.registration[1],):
        raise ValueError(
            f"clip {spec.clip_id:08X} registration candidates changed: "
            f"{[hex(value) for value in registrations]}"
        )
    registration = pak.absolute(*spec.registration)
    slot_page, slot_local, slot = pak.resolve_pointer(registration + 8)
    if (slot_page, slot_local) != spec.slot:
        raise ValueError(
            f"clip {spec.clip_id:08X} slot changed to "
            f"page {slot_page}+0x{slot_local:X}"
        )
    descriptor_page, descriptor_local, descriptor = pak.resolve_pointer(slot)
    if (descriptor_page, descriptor_local) != spec.descriptor:
        raise ValueError(
            f"clip {spec.clip_id:08X} descriptor changed to "
            f"page {descriptor_page}+0x{descriptor_local:X}"
        )
    raw_page, raw_local, raw = pak.resolve_pointer(descriptor + 0x28)
    if (raw_page, raw_local) != spec.raw_location or raw != spec.raw_offset:
        raise ValueError(
            f"clip {spec.clip_id:08X} raw changed to page {raw_page}"
            f"+0x{raw_local:X} / absolute 0x{raw:X}"
        )
    if raw_local + spec.object_size > pak.pages[raw_page].size:
        raise ValueError(f"clip {spec.clip_id:08X} crosses its target page")
    validate_clip_header(pak.data, raw, spec)
    return AnimationResolution(spec, registration, slot, descriptor, raw)


def validate_clip_header(data: bytes, offset: int, spec: AnimationSpec) -> None:
    if offset < 0 or offset + 0x30 > len(data):
        raise ValueError(f"clip {spec.clip_id:08X} header is outside the entry")
    if native.u32(data, offset) != 0x41430102:
        raise ValueError(f"clip {spec.clip_id:08X} ANIM magic changed")
    if native.u32(data, offset + 4) != spec.skeleton_id:
        raise ValueError(f"clip {spec.clip_id:08X} source skeleton changed")
    if native.u32(data, offset + 8) != spec.object_size:
        raise ValueError(f"clip {spec.clip_id:08X} object size changed")
    if native.u16(data, offset + 0x0C) != spec.group_count:
        raise ValueError(f"clip {spec.clip_id:08X} group count changed")
    if native.u32(data, offset + 0x14) != 0x30:
        raise ValueError(f"clip {spec.clip_id:08X} group table moved")
    if native.u16(data, offset + 0x20) != spec.flags:
        raise ValueError(f"clip {spec.clip_id:08X} flags changed")
    if native.u16(data, offset + 0x22) != 1:
        raise ValueError(f"clip {spec.clip_id:08X} is no longer one-sample")


def load_skeleton(data: bytes, spec: SkeletonSpec) -> SkeletonAsset:
    require_blob(spec.entry_name, data, spec.entry_size, spec.entry_sha256)
    database_signature = struct.pack(
        "<II", spec.database_id, spec.skeleton_id
    )
    database_hits = find_all(data, database_signature)
    if database_hits != (spec.database_signature_offset,):
        raise ValueError(
            f"{spec.entry_name} database/skeleton signature changed: "
            f"{[hex(value) for value in database_hits]}"
        )
    raw_signature = struct.pack(
        "<III", 0x41480105, spec.skeleton_id, spec.skeleton_id
    )
    raw_hits = find_all(data, raw_signature)
    if raw_hits != (spec.raw_offset,):
        raise ValueError(
            f"{spec.entry_name} raw skeleton signature changed: "
            f"{[hex(value) for value in raw_hits]}"
        )

    paged = PagedPak(data)
    loader = spec.database_signature_offset - 4
    if native.u32(data, loader) != 0xE:
        raise ValueError(f"{spec.entry_name} skeleton loader tag changed")
    _raw_page, _raw_local, linked_raw = paged.resolve_pointer(loader + 0x20)
    _name_page, _name_local, linked_name = paged.resolve_pointer(loader + 0x30)
    _joints_page, _joints_local, linked_joints = paged.resolve_pointer(loader + 0x38)
    if linked_raw != spec.raw_offset:
        raise ValueError(
            f"{spec.entry_name} database links to raw 0x{linked_raw:X}, "
            f"expected 0x{spec.raw_offset:X}"
        )
    if linked_name != spec.name_data_offset:
        raise ValueError(
            f"{spec.entry_name} database links to name data 0x{linked_name:X}, "
            f"expected 0x{spec.name_data_offset:X}"
        )
    if linked_joints != spec.joint_table_offset:
        raise ValueError(
            f"{spec.entry_name} database links to joints 0x{linked_joints:X}, "
            f"expected 0x{spec.joint_table_offset:X}"
        )
    if native.u32(data, spec.database_signature_offset + 0x0C) != spec.group_count:
        raise ValueError(f"{spec.entry_name} database group count changed")
    if native.u32(data, spec.database_signature_offset + 0x10) != spec.joint_count:
        raise ValueError(f"{spec.entry_name} database joint count changed")

    raw = spec.raw_offset
    if native.u16(data, raw + 0x10) != spec.joint_count:
        raise ValueError(f"{spec.entry_name} raw joint count changed")
    if (
        data[raw + 0x16] != spec.group_count
        or native.u16(data, raw + 0x18) != spec.group_count
    ):
        raise ValueError(f"{spec.entry_name} raw group count changed")
    if native.u32(data, raw + 0x2C) != 0x40:
        raise ValueError(f"{spec.entry_name} group-table relative offset changed")
    if native.u32(data, raw + 0x30) != spec.processing_relative:
        raise ValueError(
            f"{spec.entry_name} processing-table relative offset changed"
        )
    if sha256(data[raw : raw + 0x40]) != spec.header_sha256:
        raise ValueError(f"{spec.entry_name} raw header bytes changed")

    group_table = raw + 0x40
    processing_table = raw + spec.processing_relative
    derived_joint_table = (
        raw
        + native.u32(data, raw + 0x0C)
        + 0x58
        + spec.group_count * 0x28
    )
    if derived_joint_table != spec.joint_table_offset:
        raise ValueError(
            f"{spec.entry_name} derived joint table moved to "
            f"0x{derived_joint_table:X}"
        )
    group_bytes = data[group_table : group_table + spec.group_count * 0x20]
    processing_bytes = data[
        processing_table : processing_table + spec.group_count * 10
    ]
    joint_bytes = data[
        spec.joint_table_offset :
        spec.joint_table_offset + spec.joint_count * 16
    ]
    if len(group_bytes) != spec.group_count * 0x20:
        raise ValueError(f"{spec.entry_name} group table is truncated")
    if len(processing_bytes) != spec.group_count * 10:
        raise ValueError(f"{spec.entry_name} processing table is truncated")
    if len(joint_bytes) != spec.joint_count * 16:
        raise ValueError(f"{spec.entry_name} joint table is truncated")
    for label, actual, expected in (
        ("group table", sha256(group_bytes), spec.group_table_sha256),
        ("processing table", sha256(processing_bytes), spec.processing_table_sha256),
        ("joint table", sha256(joint_bytes), spec.joint_table_sha256),
    ):
        if actual != expected:
            raise ValueError(
                f"{spec.entry_name} {label} SHA-256 {actual}; expected {expected}"
            )

    groups = tuple(
        GroupLayout(
            native.u16(data, group_table + index * 0x20),
            native.u16(data, group_table + index * 0x20 + 2),
            native.u16(data, group_table + index * 0x20 + 0x10),
            native.u16(data, group_table + index * 0x20 + 0x12),
        )
        for index in range(spec.group_count)
    )
    derived_ranges = tuple(
        (group.first, group.workspace_count) for group in groups
    )
    if derived_ranges != spec.groups:
        raise ValueError(
            f"{spec.entry_name} skeleton groups changed: {derived_ranges}"
        )
    cursor = 0
    for group in groups:
        if group.first != cursor:
            raise ValueError(f"{spec.entry_name} groups are not contiguous")
        cursor += group.workspace_count
        if group.processing_index >= spec.group_count:
            raise ValueError(f"{spec.entry_name} processing index is out of range")
    if cursor != spec.joint_count:
        raise ValueError(f"{spec.entry_name} groups do not cover every joint")

    processing = tuple(
        ProcessingLayout(*struct.unpack_from(
            "<HHBBHH", data, processing_table + index * 10
        ))
        for index in range(spec.group_count)
    )
    joint_ids = tuple(
        native.u32(data, spec.joint_table_offset + index * 16)
        for index in range(spec.joint_count)
    )
    normalized_ids = b"".join(struct.pack("<I", value) for value in joint_ids)
    if sha256(normalized_ids) != spec.joint_ids_sha256:
        raise ValueError(f"{spec.entry_name} normalized joint IDs changed")
    return SkeletonAsset(spec, groups, processing, joint_ids)


def fnv1a64_bytes(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def parse_group_program(
    data: bytes,
    animation: AnimationResolution,
    spec: GroupProgramSpec,
) -> tuple[dict[int, tuple[int, ...]], GroupProgram]:
    if animation.spec.clip_id != spec.clip_id:
        raise ValueError("group-program spec is attached to the wrong clip")
    clip = animation.raw_offset
    clip_end = clip + animation.spec.object_size
    group_table = native.u32(data, clip + 0x14)
    descriptor = clip + group_table + spec.source_group * 0x10
    if descriptor + 0x10 > clip_end:
        raise ValueError("group descriptor is outside the ANIM object")
    descriptor_words = struct.unpack_from("<4I", data, descriptor)
    if descriptor_words != spec.descriptor_words:
        raise ValueError(
            f"clip {spec.clip_id:08X} group {spec.source_group} descriptor "
            f"changed to {descriptor_words}"
        )
    program = clip + descriptor_words[0]
    if program < clip or program + 2 > clip_end:
        raise ValueError("group program is outside the ANIM object")
    cursor = program
    payload_end = program
    destinations: dict[int, tuple[int, ...]] = {}
    seen: list[int] = []
    opcode_specs = {22: spec.opcode22, 24: spec.opcode24}
    for command_index in range(3):
        if cursor + 2 > clip_end:
            raise ValueError("constant-program header crosses the ANIM object")
        header = native.u16(data, cursor)
        opcode = header & 0xFF
        command_words = header >> 8
        if command_index == 2:
            if opcode != 0 or command_words != 0:
                raise ValueError(
                    f"clip {spec.clip_id:08X} group {spec.source_group} "
                    "terminator changed"
                )
            payload_end = max(payload_end, cursor + 2)
            cursor += 2
            break
        expected_opcode = (22, 24)[command_index]
        if opcode != expected_opcode or command_words != 3:
            raise ValueError(
                f"clip {spec.clip_id:08X} group {spec.source_group} "
                f"command {command_index} changed to {opcode}/{command_words}"
            )
        if cursor + 8 > clip_end:
            raise ValueError("constant-program command crosses the ANIM object")
        count, selector, payload_relative = struct.unpack_from(
            "<3H", data, cursor + 2
        )
        opcode_spec = opcode_specs[opcode]
        if count != opcode_spec.count or selector != 3:
            raise ValueError(
                f"clip {spec.clip_id:08X} group {spec.source_group} opcode "
                f"{opcode} count/selector changed to {count}/{selector}"
            )
        map_offset = program + payload_relative
        map_end = map_offset + count * 2
        value_end = map_end + count * 6
        if map_offset < cursor + 8 or value_end > clip_end:
            raise ValueError("constant-program payload is out of range")
        map_bytes = data[map_offset:map_end]
        map_digest = sha256(map_bytes)
        if map_digest != opcode_spec.sha256:
            raise ValueError(
                f"clip {spec.clip_id:08X} group {spec.source_group} opcode "
                f"{opcode} map SHA-256 {map_digest}; expected {opcode_spec.sha256}"
            )
        destinations[opcode] = struct.unpack_from(f"<{count}H", map_bytes)
        payload_end = max(payload_end, cursor + 8, value_end)
        seen.append(opcode)
        cursor += 8
    if seen != [22, 24]:
        raise ValueError("constant-program opcode order changed")
    program_bytes = payload_end - program
    if program_bytes != spec.program_bytes:
        raise ValueError(
            f"clip {spec.clip_id:08X} group {spec.source_group} program size "
            f"0x{program_bytes:X}; expected 0x{spec.program_bytes:X}"
        )
    program_hash = fnv1a64_bytes(data[program:payload_end])
    if program_hash != spec.program_fnv1a64:
        raise ValueError(
            f"clip {spec.clip_id:08X} group {spec.source_group} program FNV "
            f"{program_hash:016X}; expected {spec.program_fnv1a64:016X}"
        )
    return destinations, GroupProgram(
        spec.source_group, descriptor_words, program_bytes, program_hash
    )


def selected_rows_from_destinations(
    destinations: dict[int, tuple[int, ...]],
    sentinel_row: int,
    spec: GroupProgramSpec,
) -> tuple[int, ...]:
    if set(destinations) != {22, 24}:
        raise ValueError(f"unexpected sparse opcodes {sorted(destinations)}")
    sentinel = sentinel_row * 0x30
    vector_counts = Counter(destinations[22])
    quaternion_counts = Counter(destinations[24])
    if vector_counts[sentinel] != spec.opcode22.sentinel_count:
        raise ValueError("opcode-22 sentinel multiplicity changed")
    if quaternion_counts[sentinel] != spec.opcode24.sentinel_count:
        raise ValueError("opcode-24 sentinel multiplicity changed")

    vector_scale: set[int] = set()
    vector_translation: set[int] = set()
    quaternion: set[int] = set()
    for destination, count in vector_counts.items():
        if destination == sentinel:
            continue
        if count != 1 or destination >= sentinel:
            raise ValueError("opcode-22 destination is duplicated or out of range")
        row, component = divmod(destination, 0x30)
        if component == 0:
            vector_scale.add(row)
        elif component == 0x20:
            vector_translation.add(row)
        else:
            raise ValueError(
                f"opcode-22 destination 0x{destination:X} is not S/T aligned"
            )
    for destination, count in quaternion_counts.items():
        if destination == sentinel:
            continue
        if count != 1 or destination >= sentinel:
            raise ValueError("opcode-24 destination is duplicated or out of range")
        row, component = divmod(destination, 0x30)
        if component != 0x10:
            raise ValueError(
                f"opcode-24 destination 0x{destination:X} is not Q aligned"
            )
        quaternion.add(row)
    if not vector_scale or not (
        vector_scale == vector_translation == quaternion
    ):
        raise ValueError(
            "sparse program does not author the same complete S/Q/T row set"
        )
    return tuple(sorted(vector_scale))


def decode_group(
    image: native.MappedImage,
    data: bytes,
    animation: AnimationResolution,
    skeleton: SkeletonAsset,
    source_group: int,
) -> DecodedGroup:
    spec = GROUP_PROGRAM_SPECS[(animation.spec.clip_id, source_group)]
    static_destinations, program = parse_group_program(data, animation, spec)
    layout = skeleton.groups[source_group]
    processing = skeleton.processing[layout.processing_index]
    if processing.joint_count != layout.parameter_count:
        raise ValueError(
            f"{skeleton.spec.entry_name} group {source_group} processing/"
            "parameter counts differ"
        )
    selected = selected_rows_from_destinations(
        static_destinations, processing.joint_count, spec
    )

    first, opcodes, native_destinations, frame_count, stride = native.decode_phase(
        image, data, animation.raw_offset, 0.0, 0, source_group
    )
    second, opcodes_again, native_destinations_again, frame_count_again, stride_again = (
        native.decode_phase(
            image, data, animation.raw_offset, 1.0, 0, source_group
        )
    )
    if (
        opcodes != [22, 24]
        or opcodes_again != opcodes
        or native_destinations != static_destinations
        or native_destinations_again != static_destinations
        or frame_count != 1
        or frame_count_again != 1
        or stride != 0
        or stride_again != 0
    ):
        raise ValueError(
            f"clip {animation.spec.clip_id:08X} group {source_group} native "
            "decode disagrees with the static program"
        )
    rows: dict[int, tuple[int, ...]] = {}
    for local in selected:
        if local >= layout.parameter_count:
            raise ValueError("selected source row exceeds the parameter table")
        native.validate_target_pose(first, local, local)
        begin = local * 0x30
        end = begin + 0x30
        if first[begin:end] != second[begin:end]:
            raise ValueError(
                f"clip {animation.spec.clip_id:08X} group {source_group} "
                f"row {local} is not constant"
            )
        rows[local] = struct.unpack_from("<12I", first, begin)
    return DecodedGroup(source_group, selected, rows, program)


def collect_sources(
    skeleton: SkeletonAsset, decoded: tuple[DecodedGroup, ...]
) -> dict[int, SourceRow]:
    sources: dict[int, SourceRow] = {}
    for group_result in decoded:
        layout = skeleton.groups[group_result.source_group]
        for local in group_result.selected_rows:
            global_joint = layout.first + local
            semantic_id = skeleton.joint_ids[global_joint]
            if semantic_id in sources:
                raise ValueError(
                    f"{skeleton.spec.entry_name} selected semantic ID "
                    f"{semantic_id:08X} is duplicated"
                )
            sources[semantic_id] = SourceRow(
                semantic_id,
                group_result.source_group,
                local,
                global_joint,
                group_result.rows[local],
            )
    return sources


def parse_retarget_table(data: bytes) -> tuple[int, int]:
    require_blob(
        RETARGET_ENTRY_NAME,
        data,
        RETARGET_ENTRY_SIZE,
        RETARGET_ENTRY_SHA256,
    )
    if data[:4] != b"00CD" or native.u32(data, 4) != 1:
        raise ValueError("skel-retargets.bin DC00 header changed")
    object_count = native.u32(data, 0x14)
    object_table = native.u32(data, 0x18)
    matches: list[tuple[int, int]] = []
    for index in range(object_count):
        record = object_table + index * 0x10
        if record + 0x10 > len(data):
            raise ValueError("DC object table is truncated")
        object_id, type_id = struct.unpack_from("<II", data, record)
        if object_id == SKEL_TABLE_OBJECT_ID and type_id == SKEL_TABLE_TYPE_ID:
            payload = native.u64(data, record + 8)
            matches.append((record, payload))
    if matches != [(0x20, 0x38)]:
        raise ValueError(f"SkelTable DC object resolution changed: {matches}")
    payload = matches[0][1]
    table_offset = native.u64(data, payload)
    row_count = native.u32(data, payload + 8)
    if table_offset != 0x50 or row_count != 175:
        raise ValueError(
            f"SkelTable row table changed to 0x{table_offset:X}/{row_count}"
        )
    if table_offset + row_count * 0x30 > len(data):
        raise ValueError("SkelTable row table is truncated")
    return table_offset, row_count


def resolve_retarget(
    data: bytes, table_offset: int, row_count: int, spec: RetargetSpec
) -> RetargetMap:
    matches: list[tuple[int, int]] = []
    for index in range(row_count):
        offset = table_offset + index * 0x30
        source_base = native.u32(data, offset + 0x0C)
        target_base = native.u32(data, offset + 0x10)
        if source_base == spec.source_base_id and target_base == spec.target_base_id:
            matches.append((index, offset))
    if matches != [(spec.row_index, spec.row_offset)]:
        raise ValueError(
            f"retarget {spec.source_base_id:08X}->{spec.target_base_id:08X} "
            f"resolution changed: {matches}"
        )
    row = spec.row_offset
    map_offset = native.u64(data, row + 0x18)
    authored_count = native.u32(data, row + 0x20)
    profile = native.u32(data, row + 0x24)
    scale_bits = native.u32(data, row + 0x28)
    flags = native.u32(data, row + 0x2C)
    if (
        map_offset != spec.map_offset
        or authored_count != spec.authored_count
        or profile != spec.profile
        or scale_bits != spec.scale_bits
        or flags != spec.flags
    ):
        raise ValueError(
            f"retarget row {spec.row_index} metadata changed: "
            f"map=0x{map_offset:X} count={authored_count} "
            f"profile={profile:08X} scale={scale_bits:08X} flags={flags:08X}"
        )
    row_bytes = data[row : row + 0x30]
    map_bytes = data[map_offset : map_offset + authored_count * 12]
    if len(map_bytes) != authored_count * 12:
        raise ValueError(f"retarget row {spec.row_index} map is truncated")
    if sha256(row_bytes) != spec.row_sha256:
        raise ValueError(f"retarget row {spec.row_index} record bytes changed")
    if sha256(map_bytes) != spec.map_sha256:
        raise ValueError(f"retarget row {spec.row_index} authored map changed")
    authored = tuple(
        struct.unpack_from("<III", map_bytes, index * 12)
        for index in range(authored_count)
    )
    return RetargetMap(spec, authored)


def unique_joint_lookup(skeleton: SkeletonAsset) -> dict[int, tuple[int, ...]]:
    positions: dict[int, list[int]] = defaultdict(list)
    for index, semantic_id in enumerate(skeleton.joint_ids):
        positions[semantic_id].append(index)
    return {key: tuple(value) for key, value in positions.items()}


def resolve_runtime_records(
    mapping: RetargetMap,
    source: SkeletonAsset,
    target: SkeletonAsset,
) -> tuple[RuntimeRecord, ...]:
    source_positions = unique_joint_lookup(source)
    target_positions = unique_joint_lookup(target)
    records: list[RuntimeRecord] = []
    for map_index, (source_id, target_id, mode) in enumerate(mapping.authored):
        source_matches = source_positions.get(source_id, ())
        target_matches = target_positions.get(target_id, ())
        if len(source_matches) > 1 or len(target_matches) > 1:
            raise ValueError(
                f"retarget row {mapping.spec.row_index} map {map_index} "
                "references an ambiguous joint ID"
            )
        if not source_matches or not target_matches:
            continue
        if mode > 0xFF:
            raise ValueError("retarget mode no longer fits the runtime record")
        records.append(
            RuntimeRecord(
                map_index,
                source_id,
                target_id,
                mode,
                source_matches[0],
                target_matches[0],
            )
        )
    if len(records) != mapping.spec.runtime_count:
        raise ValueError(
            f"retarget row {mapping.spec.row_index} resolves {len(records)} "
            f"runtime records; expected {mapping.spec.runtime_count}"
        )
    return tuple(records)


def one_record(
    records: tuple[RuntimeRecord, ...], semantic_id: int, label: str
) -> RuntimeRecord:
    matches = [record for record in records if record.source_id == semantic_id]
    if len(matches) != 1:
        raise ValueError(
            f"{label} semantic ID {semantic_id:08X} has {len(matches)} records"
        )
    return matches[0]


def derive_transfers(
    player_sources: dict[int, SourceRow],
    chloe_sources: dict[int, SourceRow],
    nadine: SkeletonAsset,
    row20_records: tuple[RuntimeRecord, ...],
    row157_records: tuple[RuntimeRecord, ...],
) -> tuple[Transfer, ...]:
    if set(player_sources) != set(chloe_sources):
        raise ValueError(
            "D350 and B8 sparse programs select different semantic joint sets"
        )
    if len(player_sources) != EXPECTED_JOIN_COUNT:
        raise ValueError(
            f"sparse programs select {len(player_sources)} semantic joints; "
            f"expected {EXPECTED_JOIN_COUNT}"
        )
    transfers: list[Transfer] = []
    ordered_player = sorted(
        player_sources.values(), key=lambda source: source.global_joint
    )
    for player in ordered_player:
        row20 = one_record(row20_records, player.semantic_id, "row20")
        chloe = chloe_sources.get(row20.target_id)
        if chloe is None:
            raise ValueError(
                f"row20 target {row20.target_id:08X} is not authored by B8"
            )
        if (
            row20.source_global != player.global_joint
            or row20.target_global != chloe.global_joint
        ):
            raise ValueError("row20 runtime indices disagree with source assets")
        row157 = one_record(row157_records, chloe.semantic_id, "row157")
        if row157.source_global != chloe.global_joint:
            raise ValueError("row157 source index disagrees with Chloe skeleton")
        nadine_group, nadine_local = nadine.location(row157.target_global)
        if nadine.joint_ids[row157.target_global] != row157.target_id:
            raise ValueError("row157 target index disagrees with Nadine skeleton")
        if not (
            player.semantic_id
            == row20.source_id
            == row20.target_id
            == chloe.semantic_id
            == row157.source_id
            == row157.target_id
        ):
            raise ValueError("required transfer is not a same-StringId semantic join")
        if row20.mode != row157.mode:
            raise ValueError("two-stage retarget modes differ for one semantic join")
        transfers.append(
            Transfer(
                player.semantic_id,
                player,
                chloe,
                nadine_group,
                nadine_local,
                row157.target_global,
                row20.mode,
                row20.map_index,
                row157.map_index,
            )
        )
    if len({transfer.nadine_global for transfer in transfers}) != len(transfers):
        raise ValueError("semantic join produces duplicate Nadine targets")
    return tuple(transfers)


def validate_derived_hashes(transfers: tuple[Transfer, ...]) -> None:
    policy = b"".join(
        struct.pack(
            "<IHHHBBBB",
            transfer.semantic_id,
            transfer.player.global_joint,
            transfer.chloe.global_joint,
            transfer.nadine_global,
            transfer.player.group,
            transfer.chloe.group,
            transfer.nadine_group,
            transfer.mode,
        )
        for transfer in transfers
    )
    rich = b"".join(
        struct.pack(
            "<IBBHHBHHBHH",
            transfer.semantic_id,
            transfer.mode,
            transfer.player.group,
            transfer.player.local,
            transfer.player.global_joint,
            transfer.chloe.group,
            transfer.chloe.local,
            transfer.chloe.global_joint,
            transfer.nadine_group,
            transfer.nadine_local,
            transfer.nadine_global,
        )
        for transfer in transfers
    )
    player_rows = b"".join(
        struct.pack("<12I", *transfer.player.words) for transfer in transfers
    )
    chloe_rows = b"".join(
        struct.pack("<12I", *transfer.chloe.words) for transfer in transfers
    )
    checks = (
        ("policy manifest", policy, EXPECTED_POLICY_MANIFEST_SHA256),
        ("rich manifest", rich, EXPECTED_RICH_MANIFEST_SHA256),
        ("Player rows", player_rows, EXPECTED_PLAYER_ROWS_SHA256),
        ("Chloe rows", chloe_rows, EXPECTED_CHLOE_ROWS_SHA256),
        ("combined rows", player_rows + chloe_rows, EXPECTED_COMBINED_ROWS_SHA256),
    )
    for label, blob, expected in checks:
        digest = sha256(blob)
        if digest != expected:
            raise ValueError(
                f"derived {label} SHA-256 {digest}; expected {expected}"
            )


def words(values: tuple[int, ...]) -> str:
    return "{" + ", ".join(f"0x{value:08X}u" for value in values) + "}"


def emit_header(
    player_animation: AnimationResolution,
    chloe_animation: AnimationResolution,
    player_groups: tuple[DecodedGroup, ...],
    chloe_groups: tuple[DecodedGroup, ...],
    transfers: tuple[Transfer, ...],
) -> str:
    player_row_bytes = b"".join(
        struct.pack("<12I", *transfer.player.words) for transfer in transfers
    )
    chloe_row_bytes = b"".join(
        struct.pack("<12I", *transfer.chloe.words) for transfer in transfers
    )
    player_fnv = fnv1a64_bytes(player_row_bytes)
    chloe_fnv = fnv1a64_bytes(chloe_row_bytes)
    lines = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "// Generated by scripts/generate_p99_chloe_source_rows.py.",
        "// Do not hand-edit: regenerate from exact actor77/DC1 assets and retail EXE.",
        "namespace coop::p99_chloe_source_rows {",
        "",
        f'inline constexpr std::string_view kRetailExeSha256 = "{native.EXPECTED_EXE_SHA256}";',
        f'inline constexpr std::string_view kEntryName = "{ANIMATION_ENTRY_NAME}";',
        f'inline constexpr std::string_view kEntrySha256 = "{ANIMATION_ENTRY_SHA256}";',
        f'inline constexpr std::string_view kPlayerSkeletonEntryName = "{PLAYER_SKELETON.entry_name}";',
        f'inline constexpr std::string_view kPlayerSkeletonEntrySha256 = "{PLAYER_SKELETON.entry_sha256}";',
        f'inline constexpr std::string_view kChloeSkeletonEntryName = "{CHLOE_SKELETON.entry_name}";',
        f'inline constexpr std::string_view kChloeSkeletonEntrySha256 = "{CHLOE_SKELETON.entry_sha256}";',
        f'inline constexpr std::string_view kNadineSkeletonEntryName = "{NADINE_SKELETON.entry_name}";',
        f'inline constexpr std::string_view kNadineSkeletonEntrySha256 = "{NADINE_SKELETON.entry_sha256}";',
        f'inline constexpr std::string_view kRetargetEntryName = "{RETARGET_ENTRY_NAME}";',
        f'inline constexpr std::string_view kRetargetEntrySha256 = "{RETARGET_ENTRY_SHA256}";',
        f'inline constexpr std::string_view kPolicyManifestSha256 = "{EXPECTED_POLICY_MANIFEST_SHA256}";',
        f'inline constexpr std::string_view kRichManifestSha256 = "{EXPECTED_RICH_MANIFEST_SHA256}";',
        f'inline constexpr std::string_view kPlayerRowsSha256 = "{EXPECTED_PLAYER_ROWS_SHA256}";',
        f'inline constexpr std::string_view kChloeRowsSha256 = "{EXPECTED_CHLOE_ROWS_SHA256}";',
        f'inline constexpr std::string_view kCombinedRowsSha256 = "{EXPECTED_COMBINED_ROWS_SHA256}";',
        f"inline constexpr std::uint32_t kPlayerDatabaseId = 0x{PLAYER_DATABASE_ID:08X}u;",
        f"inline constexpr std::uint32_t kPlayerSkeletonId = 0x{PLAYER_SKELETON_ID:08X}u;",
        f"inline constexpr std::uint32_t kChloeDatabaseId = 0x{CHLOE_DATABASE_ID:08X}u;",
        f"inline constexpr std::uint32_t kChloeSkeletonId = 0x{CHLOE_SKELETON_ID:08X}u;",
        f"inline constexpr std::uint32_t kNadineDatabaseId = 0x{NADINE_DATABASE_ID:08X}u;",
        f"inline constexpr std::uint32_t kNadineSkeletonId = 0x{NADINE_SKELETON_ID:08X}u;",
        f"inline constexpr std::uint32_t kPlayerClipId = 0x{PLAYER_CLIP_ID:08X}u;",
        f"inline constexpr std::uint32_t kChloeClipId = 0x{CHLOE_CLIP_ID:08X}u;",
        f"inline constexpr std::uint32_t kPlayerClipRawOffset = 0x{player_animation.raw_offset:X}u;",
        f"inline constexpr std::uint32_t kChloeClipRawOffset = 0x{chloe_animation.raw_offset:X}u;",
        f"inline constexpr std::uint16_t kPlayerToChloeRetargetRowIndex = {PLAYER_TO_CHLOE.row_index};",
        f"inline constexpr std::uint16_t kChloeToNadineRetargetRowIndex = {CHLOE_TO_NADINE.row_index};",
        "inline constexpr std::uint16_t kFrameCount = 1;",
        "inline constexpr std::uint16_t kFrameStride = 0;",
        "",
        "struct SourceRowPair {",
        "    std::uint32_t semantic_id = 0;",
        "    std::uint16_t player_global = 0;",
        "    std::uint16_t chloe_global = 0;",
        "    std::uint16_t nadine_global = 0;",
        "    std::uint8_t player_group = 0;",
        "    std::uint8_t chloe_group = 0;",
        "    std::uint16_t player_local = 0;",
        "    std::uint16_t chloe_local = 0;",
        "    std::array<std::uint32_t, 12> player_words{};",
        "    std::array<std::uint32_t, 12> chloe_additive_words{};",
        "};",
        "",
        "struct SemanticJoinProvenance {",
        "    std::uint32_t semantic_id = 0;",
        "    std::uint8_t player_group = 0;",
        "    std::uint8_t chloe_group = 0;",
        "    std::uint8_t nadine_group = 0;",
        "    std::uint8_t mode = 0;",
        "    std::uint16_t player_local = 0;",
        "    std::uint16_t chloe_local = 0;",
        "    std::uint16_t nadine_local = 0;",
        "    std::uint16_t row20_map_index = 0;",
        "    std::uint16_t row157_map_index = 0;",
        "};",
        "",
        "struct GroupProgramProvenance {",
        "    std::uint8_t source_group = 0;",
        "    std::array<std::uint32_t, 4> descriptor_words{};",
        "    std::uint32_t program_bytes = 0;",
        "    std::uint64_t program_fnv1a64 = 0;",
        "};",
        "",
        "inline constexpr std::array<GroupProgramProvenance, 2>",
        "    kPlayerGroupPrograms{{",
    ]
    for decoded in player_groups:
        program = decoded.program
        lines.append(
            "        {"
            f"{program.source_group}, {words(program.descriptor_words)}, "
            f"0x{program.program_bytes:X}u, "
            f"0x{program.program_fnv1a64:016X}ull"
            "},"
        )
    lines.extend(
        (
            "    }};",
            "inline constexpr std::array<GroupProgramProvenance, 2>",
            "    kChloeGroupPrograms{{",
        )
    )
    for decoded in chloe_groups:
        program = decoded.program
        lines.append(
            "        {"
            f"{program.source_group}, {words(program.descriptor_words)}, "
            f"0x{program.program_bytes:X}u, "
            f"0x{program.program_fnv1a64:016X}ull"
            "},"
        )
    lines.extend(
        (
            "    }};",
            "",
            f"inline constexpr std::array<SemanticJoinProvenance, {len(transfers)}>",
            "    kSemanticJoin{{",
        )
    )
    for transfer in transfers:
        lines.append(
            "        {"
            f"0x{transfer.semantic_id:08X}u, "
            f"{transfer.player.group}, {transfer.chloe.group}, "
            f"{transfer.nadine_group}, {transfer.mode}, "
            f"{transfer.player.local}, {transfer.chloe.local}, "
            f"{transfer.nadine_local}, {transfer.row20_map_index}, "
            f"{transfer.row157_map_index}"
            "},"
        )
    lines.extend(
        (
            "    }};",
            "",
            f"inline constexpr std::array<SourceRowPair, {len(transfers)}> kRows{{{{",
        )
    )
    for transfer in transfers:
        lines.extend(
            (
                "    {",
                f"        0x{transfer.semantic_id:08X}u, "
                f"{transfer.player.global_joint}, {transfer.chloe.global_joint}, "
                f"{transfer.nadine_global},",
                f"        {transfer.player.group}, {transfer.chloe.group}, "
                f"{transfer.player.local}, {transfer.chloe.local},",
                f"        {words(transfer.player.words)},",
                f"        {words(transfer.chloe.words)},",
                "    },",
            )
        )
    lines.extend(
        (
            "}};",
            "",
            f"inline constexpr std::uint64_t kPlayerRowsFnv1a64 = 0x{player_fnv:016X}ull;",
            f"inline constexpr std::uint64_t kChloeRowsFnv1a64 = 0x{chloe_fnv:016X}ull;",
            "",
            "}  // namespace coop::p99_chloe_source_rows",
            "",
        )
    )
    return "\n".join(lines)


def generate(
    animation_entry: bytes,
    skeleton_entries: dict[str, bytes],
    retarget_entry: bytes,
    image: native.MappedImage,
) -> str:
    require_blob(
        ANIMATION_ENTRY_NAME,
        animation_entry,
        ANIMATION_ENTRY_SIZE,
        ANIMATION_ENTRY_SHA256,
    )
    paged_animation = PagedPak(animation_entry)
    player_animation = resolve_animation(paged_animation, PLAYER_ANIMATION)
    chloe_animation = resolve_animation(paged_animation, CHLOE_ANIMATION)

    player_skeleton = load_skeleton(
        skeleton_entries[PLAYER_SKELETON.entry_name], PLAYER_SKELETON
    )
    chloe_skeleton = load_skeleton(
        skeleton_entries[CHLOE_SKELETON.entry_name], CHLOE_SKELETON
    )
    nadine_skeleton = load_skeleton(
        skeleton_entries[NADINE_SKELETON.entry_name], NADINE_SKELETON
    )

    player_groups = tuple(
        decode_group(
            image,
            animation_entry,
            player_animation,
            player_skeleton,
            group,
        )
        for group in (0, 7)
    )
    chloe_groups = tuple(
        decode_group(
            image,
            animation_entry,
            chloe_animation,
            chloe_skeleton,
            group,
        )
        for group in (0, 2)
    )
    player_sources = collect_sources(player_skeleton, player_groups)
    chloe_sources = collect_sources(chloe_skeleton, chloe_groups)

    table_offset, row_count = parse_retarget_table(retarget_entry)
    row20 = resolve_retarget(
        retarget_entry, table_offset, row_count, PLAYER_TO_CHLOE
    )
    row157 = resolve_retarget(
        retarget_entry, table_offset, row_count, CHLOE_TO_NADINE
    )
    row20_records = resolve_runtime_records(
        row20, player_skeleton, chloe_skeleton
    )
    row157_records = resolve_runtime_records(
        row157, chloe_skeleton, nadine_skeleton
    )
    transfers = derive_transfers(
        player_sources,
        chloe_sources,
        nadine_skeleton,
        row20_records,
        row157_records,
    )
    validate_derived_hashes(transfers)
    return emit_header(
        player_animation,
        chloe_animation,
        player_groups,
        chloe_groups,
        transfers,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=native.DEFAULT_GAME / "tll.exe")
    parser.add_argument(
        "--archive",
        type=Path,
        default=(
            native.DEFAULT_GAME
            / "Uncharted4_data"
            / "build"
            / "pc"
            / "thelostlegacy"
            / "actor77.psarc"
        ),
    )
    parser.add_argument(
        "--dc-archive",
        type=Path,
        default=(
            native.DEFAULT_GAME
            / "Uncharted4_data"
            / "build"
            / "pc"
            / "thelostlegacy"
            / "dc1.psarc"
        ),
    )
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    audit = native.load_audit_module()
    actor = audit.Psarc(args.archive, None)
    animation_entry = actor.extract(ANIMATION_ENTRY_NAME)
    skeleton_entries = {
        spec.entry_name: actor.extract(spec.entry_name)
        for spec in (PLAYER_SKELETON, CHLOE_SKELETON, NADINE_SKELETON)
    }
    retarget_entry = audit.Psarc(args.dc_archive, None).extract(
        RETARGET_ENTRY_NAME
    )
    image = native.MappedImage(args.exe)
    try:
        generated = generate(
            animation_entry, skeleton_entries, retarget_entry, image
        )
    finally:
        image.close()
    if args.check:
        if (
            not args.output.exists()
            or args.output.read_text(encoding="utf-8") != generated
        ):
            raise SystemExit(f"generated header is stale: {args.output}")
        print(f"verified {args.output}")
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding="utf-8", newline="\n")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
