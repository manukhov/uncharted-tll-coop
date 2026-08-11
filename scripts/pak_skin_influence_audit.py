#!/usr/bin/env python3
"""Audit sparse skin influences in The Lost Legacy paged PAK meshes.

The mesh resource stores its skin palette count at header ``+0x68`` and a
fixup-backed sparse influence descriptor at ``+0x70``.  Every vertex has
an eight-byte ``{count, byte_offset}`` row; each referenced four-byte influence
is ``{weight_u16, encoded_joint_u16}``, where ``encoded_joint >> 6`` is the
skeleton joint index.  This tool resolves the PAK page fixups, validates mesh
headers, and reports the exact joints used by every discovered mesh partition.

It is deliberately read-only and operates on already extracted PAK entries.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
import hashlib
import struct
from pathlib import Path


@dataclass(frozen=True)
class Page:
    file_offset: int
    size: int


class PagedPak:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 0x2C:
            raise ValueError(f"truncated PAK: {path}")

        page_count = struct.unpack_from("<I", self.data, 0x10)[0]
        page_table_offset = struct.unpack_from("<I", self.data, 0x14)[0]
        if not page_count or page_count > 0x10000:
            raise ValueError(f"invalid PAK page count: {page_count}")
        if page_table_offset + page_count * 0xC > len(self.data):
            raise ValueError("truncated PAK page table")

        pages: list[Page] = []
        for index in range(page_count):
            file_offset, size, _flags = struct.unpack_from(
                "<III", self.data, page_table_offset + index * 0xC
            )
            if file_offset + size > len(self.data):
                raise ValueError(f"PAK page {index} extends beyond the file")
            pages.append(Page(file_offset, size))
        self.pages = pages

        metadata_end = pages[0].file_offset
        fixups: dict[tuple[int, int], set[int]] = defaultdict(set)
        for offset in range(0, metadata_end - 7, 8):
            source_page, target_page, pointer_location = struct.unpack_from(
                "<HHI", self.data, offset
            )
            if source_page >= len(pages) or target_page >= len(pages):
                continue
            if pointer_location + 8 > pages[source_page].size:
                continue
            fixups[(source_page, pointer_location)].add(target_page)
        self.fixups = fixups

    def absolute_to_page(self, absolute: int) -> tuple[int, int]:
        for index, page in enumerate(self.pages):
            if page.file_offset <= absolute < page.file_offset + page.size:
                return index, absolute - page.file_offset
        raise ValueError(f"offset 0x{absolute:X} is outside every PAK page")

    def resolve_absolute(self, pointer_absolute: int) -> int:
        source_page, pointer_local = self.absolute_to_page(pointer_absolute)
        targets = self.fixups.get((source_page, pointer_local), set())
        if len(targets) != 1:
            raise ValueError(
                f"pointer 0x{pointer_absolute:X} has target pages "
                f"{sorted(targets)}"
            )
        target_page = next(iter(targets))
        target_local = struct.unpack_from("<Q", self.data, pointer_absolute)[0]
        if target_local >= self.pages[target_page].size:
            raise ValueError(
                f"pointer 0x{pointer_absolute:X} targets page {target_page} "
                f"+0x{target_local:X} outside the page"
            )
        return self.pages[target_page].file_offset + target_local

    def has_pointer(self, pointer_absolute: int) -> bool:
        try:
            source_page, pointer_local = self.absolute_to_page(pointer_absolute)
        except ValueError:
            return False
        return len(self.fixups.get((source_page, pointer_local), set())) == 1

    def read_string_pointer(self, pointer_absolute: int) -> str:
        target = self.resolve_absolute(pointer_absolute)
        end = self.data.find(b"\0", target, min(len(self.data), target + 0x400))
        if end < 0:
            raise ValueError(f"unterminated string at 0x{target:X}")
        value = self.data[target:end]
        if not value or any(byte < 0x20 or byte >= 0x7F for byte in value):
            raise ValueError(f"non-printable string at 0x{target:X}")
        return value.decode("ascii")


@dataclass(frozen=True)
class MeshHeader:
    absolute: int
    vertex_count: int
    index_count: int
    vertex_stream_count: int
    influence_tag: int


@dataclass
class JointUse:
    vertices: set[int]
    total_weight: int = 0
    records: int = 0


@dataclass
class JointTotal:
    vertex_references: int = 0
    total_weight: int = 0
    records: int = 0


def find_mesh_headers(pak: PagedPak, skeleton_joint_count: int) -> list[MeshHeader]:
    signature = struct.pack("<I", skeleton_joint_count)
    headers: list[MeshHeader] = []
    start = 0
    while True:
        hit = pak.data.find(signature, start)
        if hit < 0:
            break
        start = hit + 1
        header = hit - 0x68
        if header < 0 or header + 0x80 > len(pak.data):
            continue
        try:
            page, local = pak.absolute_to_page(header)
        except ValueError:
            continue
        if local + 0x80 > pak.pages[page].size:
            continue

        vertex_count, index_count, stream_count = struct.unpack_from(
            "<III", pak.data, header + 0x2C
        )
        influence_tag = struct.unpack_from("<I", pak.data, header + 0x78)[0]
        if not (0 < vertex_count <= 2_000_000):
            continue
        if not (0 < index_count <= 8_000_000):
            continue
        if not (0 < stream_count <= 64):
            continue
        if not (0 < influence_tag <= 64):
            continue
        if not all(
            pak.has_pointer(header + member)
            for member in (0x40, 0x50, 0x70)
        ):
            continue
        try:
            vertex_streams = pak.resolve_absolute(header + 0x40)
            index_buffer = pak.resolve_absolute(header + 0x50)
            sparse_descriptor = pak.resolve_absolute(header + 0x70)
            pak.absolute_to_page(vertex_streams + stream_count * 0x18 - 1)
            pak.absolute_to_page(index_buffer + index_count * 2 - 1)
            pak.absolute_to_page(sparse_descriptor + 0x20 - 1)
        except ValueError:
            continue
        headers.append(
            MeshHeader(
                absolute=header,
                vertex_count=vertex_count,
                index_count=index_count,
                vertex_stream_count=stream_count,
                influence_tag=influence_tag,
            )
        )
    return sorted(set(headers), key=lambda item: item.absolute)


def parse_sparse_influences(
    pak: PagedPak,
    mesh: MeshHeader,
    skeleton_joint_count: int,
) -> dict[int, JointUse]:
    sparse_descriptor = pak.resolve_absolute(mesh.absolute + 0x70)
    result: dict[int, JointUse] = {}
    # +0x70 points directly at this mesh's descriptor.  Descriptors for adjacent
    # LODs are often contiguous, so treating +0x78 as an array count would walk
    # into the next LOD and double-count it.
    for stream_index, stream in enumerate((sparse_descriptor,)):
        influence_record_count = struct.unpack_from("<I", pak.data, stream + 4)[0]
        if not influence_record_count or influence_record_count > mesh.vertex_count * 16:
            raise ValueError(
                f"mesh 0x{mesh.absolute:X} sparse[{stream_index}] has invalid "
                f"record count {influence_record_count}"
            )
        vertex_table = pak.resolve_absolute(stream + 0x10)
        influences = pak.resolve_absolute(stream + 0x18)
        pak.absolute_to_page(vertex_table + mesh.vertex_count * 8 - 1)
        pak.absolute_to_page(influences + influence_record_count * 4 - 1)

        for vertex in range(mesh.vertex_count):
            count, byte_offset = struct.unpack_from(
                "<II", pak.data, vertex_table + vertex * 8
            )
            if count > 16 or byte_offset & 3:
                raise ValueError(
                    f"mesh 0x{mesh.absolute:X} sparse[{stream_index}] vertex "
                    f"{vertex} has invalid count/offset {count}/0x{byte_offset:X}"
                )
            first_record = byte_offset // 4
            if first_record + count > influence_record_count:
                raise ValueError(
                    f"mesh 0x{mesh.absolute:X} sparse[{stream_index}] vertex "
                    f"{vertex} exceeds the influence record array"
                )
            for record_index in range(first_record, first_record + count):
                weight, encoded_joint = struct.unpack_from(
                    "<HH", pak.data, influences + record_index * 4
                )
                joint = encoded_joint >> 6
                if joint >= skeleton_joint_count:
                    raise ValueError(
                        f"mesh 0x{mesh.absolute:X} sparse[{stream_index}] uses "
                        f"out-of-range joint {joint}"
                    )
                use = result.setdefault(joint, JointUse(set()))
                use.vertices.add(vertex)
                use.total_weight += weight
                use.records += 1
    return result


def read_joint_names(
    pak: PagedPak, table_absolute: int, joint_count: int
) -> dict[int, tuple[int, str]]:
    names: dict[int, tuple[int, str]] = {}
    for joint in range(joint_count):
        row = table_absolute + joint * 0x10
        if row + 0x10 > len(pak.data):
            raise ValueError("joint table extends beyond the skeleton PAK")
        joint_id = struct.unpack_from("<I", pak.data, row)[0]
        name = pak.read_string_pointer(row + 8)
        names[joint] = (joint_id, name)
    return names


def find_joint_hash_table_candidates(
    pak: PagedPak,
    names: dict[int, tuple[int, str]],
    joint_count: int,
) -> list[int]:
    """Locate embedded full palettes by a stable skeleton-hash prefix.

    Sparse influence records index the mesh palette, not a free-standing name.
    The caller validates every actually weighted ordinal after parsing the
    package.  Unweighted rig slots may legitimately differ between compatible
    mesh and animation palettes, so byte identity of all entries is too strong.
    """

    if len(names) != joint_count:
        raise ValueError("the supplied joint-name table is incomplete")
    prefix_joint_count = (min)(4, joint_count)
    prefix = b"".join(
        struct.pack("<I", names[joint][0])
        for joint in range(prefix_joint_count)
    )
    hits: list[int] = []
    start = 0
    while True:
        hit = pak.data.find(prefix, start)
        if hit < 0:
            break
        if hit + joint_count * 4 <= len(pak.data):
            hits.append(hit)
        start = hit + len(prefix)
    return hits


def print_skeleton_program_audit(
    pak: PagedPak,
    skeleton_base: int,
    names: dict[int, tuple[int, str]],
) -> None:
    """Print exact group ranges and raw opcode-40 workspace mappings.

    The low two bits of an opcode-40 source word are an address-space selector;
    the remaining value is reported as its 64-byte matrix row. Selector 1 is
    group-workspace-local. Other selectors deliberately remain raw imported
    rows instead of being promoted to a skeleton-parent claim.
    """

    data = pak.data
    if skeleton_base < 0 or skeleton_base + 0x140 > len(data):
        raise ValueError("skeleton object header is outside the PAK")
    group_count = struct.unpack_from("<H", data, skeleton_base + 0x18)[0]
    program_table_rel = struct.unpack_from("<I", data, skeleton_base + 0x34)[0]
    if not 0 < group_count <= 32:
        raise ValueError(f"invalid skeleton group count: {group_count}")
    program_table = skeleton_base + program_table_rel
    if program_table + group_count * 8 > len(data):
        raise ValueError("skeleton program table is truncated")

    print(
        f"skeleton_object=0x{skeleton_base:X} groups={group_count} "
        f"programTableRel=0x{program_table_rel:X}"
    )
    for group in range(group_count):
        descriptor = skeleton_base + 0x40 + group * 0x20
        first_joint = struct.unpack_from("<H", data, descriptor)[0]
        joint_count = struct.unpack_from("<H", data, descriptor + 0x10)[0]
        program_rel = struct.unpack_from(
            "<I", data, program_table + group * 8 + 4
        )[0]
        program = skeleton_base + program_rel
        if names and first_joint + joint_count > len(names):
            raise ValueError(
                f"group {group} range {first_joint}:{first_joint + joint_count} "
                "exceeds the supplied name table"
            )
        print(
            f"  group={group} firstJoint={first_joint} "
            f"jointCount={joint_count} programRel=0x{program_rel:X}"
        )
        cursor = program
        opcodes: list[int] = []
        op40_rows: list[tuple[int, int, int, int, int]] = []
        for operator_index in range(256):
            if cursor + 2 > len(data):
                raise ValueError(f"group {group} program is truncated")
            header = struct.unpack_from("<H", data, cursor)[0]
            opcode, word_count = header & 0xFF, header >> 8
            if opcode == 0:
                break
            payload_end = cursor + 2 + word_count * 2
            if payload_end > len(data):
                raise ValueError(f"group {group} operator payload is truncated")
            payload = list(
                struct.unpack_from(f"<{word_count}H", data, cursor + 2)
            ) if word_count else []
            opcodes.append(opcode)
            if opcode == 40 and len(payload) >= 3:
                pair_count = 4 * payload[0]
                pair_table = program + (payload[2] & 0xFFFC)
                if pair_table + pair_count * 4 > len(data):
                    raise ValueError(
                        f"group {group} opcode40 pair table is truncated"
                    )
                for pair in range(pair_count):
                    destination, source = struct.unpack_from(
                        "<HH", data, pair_table + pair * 4
                    )
                    if destination == 0 or destination & 0xF:
                        continue
                    local_destination = destination // 0x10 - 1
                    if not 0 <= local_destination < joint_count:
                        continue
                    op40_rows.append(
                        (
                            operator_index,
                            first_joint + local_destination,
                            source & 3,
                            source >> 6,
                            source,
                        )
                    )
            cursor = payload_end
        else:
            raise ValueError(f"group {group} program is unterminated")
        print("    opcodes=[" + ",".join(str(value) for value in opcodes) + "]")
        for operator_index, destination, selector, source_row, raw in op40_rows:
            destination_name = names.get(destination, (0, "?"))[1]
            workspace_source = "n/a"
            if selector == 1 and source_row > 0:
                workspace_source = str(first_joint + source_row - 1)
            print(
                f"    op40={operator_index:02d} destination={destination:3d} "
                f"name={destination_name} sourceSelector={selector} "
                f"encodedSourceRow={source_row} "
                f"workspaceSource={workspace_source} raw=0x{raw:04X}"
            )


def format_joint(
    joint: int,
    vertex_references: int,
    total_weight: int,
    records: int,
    names: dict[int, tuple[int, str]],
) -> str:
    suffix = ""
    if joint in names:
        joint_id, name = names[joint]
        suffix = f" id=0x{joint_id:08X} name={name}"
    normalized_weight = total_weight / 65535.0
    return (
        f"joint={joint:3d} vertexRefs={vertex_references:6d} "
        f"records={records:6d} weight={normalized_weight:11.4f}{suffix}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mesh_paks", nargs="+", type=Path)
    parser.add_argument(
        "--skeleton-pak",
        type=Path,
        help="paged PAK containing the matching skeleton joint-name table",
    )
    parser.add_argument(
        "--joint-table",
        type=lambda value: int(value, 0),
        default=0x8BB38,
        help="absolute stride-16 joint-name table offset (default: 0x8BB38)",
    )
    parser.add_argument("--joint-count", type=int, default=580)
    parser.add_argument(
        "--skeleton-object",
        type=lambda value: int(value, 0),
        help="absolute skeleton object offset for group/program auditing",
    )
    parser.add_argument(
        "--print-skeleton-programs",
        action="store_true",
        help="print group ranges/opcodes and exact opcode-40 workspace mappings",
    )
    parser.add_argument(
        "--per-mesh-details",
        action="store_true",
        help="print per-joint counts for every mesh partition",
    )
    parser.add_argument(
        "--require-compatible-joint-palettes",
        action="store_true",
        help=(
            "require each mesh PAK to embed full joint-hash palettes whose "
            "every actually weighted ordinal matches the supplied skeleton"
        ),
    )
    parser.add_argument(
        "--select-range",
        action="append",
        default=[],
        metavar="FIRST:LAST",
        help="include used joints in an inclusive range in the final selection",
    )
    parser.add_argument(
        "--force-range",
        action="append",
        default=[],
        metavar="FIRST:LAST",
        help="include every joint in an inclusive range, even if not skin-weighted",
    )
    args = parser.parse_args()

    names: dict[int, tuple[int, str]] = {}
    if args.skeleton_pak is not None:
        skeleton_pak = PagedPak(args.skeleton_pak)
        names = read_joint_names(
            skeleton_pak, args.joint_table, args.joint_count
        )
        if args.print_skeleton_programs:
            if args.skeleton_object is None:
                raise ValueError(
                    "--print-skeleton-programs requires --skeleton-object"
                )
            print_skeleton_program_audit(
                skeleton_pak, args.skeleton_object, names
            )
    elif args.print_skeleton_programs:
        raise ValueError("--print-skeleton-programs requires --skeleton-pak")

    global_use: dict[int, JointTotal] = {}
    for path in args.mesh_paks:
        pak = PagedPak(path)
        palette_candidates: list[int] = []
        if args.require_compatible_joint_palettes:
            if not names:
                raise ValueError(
                    "--require-compatible-joint-palettes requires --skeleton-pak"
                )
            palette_candidates = find_joint_hash_table_candidates(
                pak, names, args.joint_count
            )
            if not palette_candidates:
                raise ValueError(
                    f"no {args.joint_count}-joint palette candidate found in {path}"
                )
        headers = find_mesh_headers(pak, args.joint_count)
        if not headers:
            raise ValueError(f"no validated mesh headers found in {path}")
        package_use: dict[int, JointTotal] = {}
        print(f"package={path} meshes={len(headers)}")
        for mesh in headers:
            use = parse_sparse_influences(pak, mesh, args.joint_count)
            rendered = ",".join(str(joint) for joint in sorted(use))
            print(
                f"  mesh=0x{mesh.absolute:X} vertices={mesh.vertex_count} "
                f"indices={mesh.index_count} vertexStreams={mesh.vertex_stream_count} "
                f"influenceTag={mesh.influence_tag} joints=[{rendered}]"
            )
            if args.per_mesh_details:
                for joint in sorted(use):
                    joint_use = use[joint]
                    print(
                        "      "
                        + format_joint(
                            joint,
                            len(joint_use.vertices),
                            joint_use.total_weight,
                            joint_use.records,
                            names,
                        )
                    )
            for joint, joint_use in use.items():
                package_joint = package_use.setdefault(joint, JointTotal())
                package_joint.vertex_references += len(joint_use.vertices)
                package_joint.total_weight += joint_use.total_weight
                package_joint.records += joint_use.records
                global_joint = global_use.setdefault(joint, JointTotal())
                global_joint.vertex_references += len(joint_use.vertices)
                global_joint.total_weight += joint_use.total_weight
                global_joint.records += joint_use.records

        if args.require_compatible_joint_palettes:
            target_ids = [names[joint][0] for joint in range(args.joint_count)]
            palette_bytes = args.joint_count * 4
            first_palette = pak.data[
                palette_candidates[0]:palette_candidates[0] + palette_bytes
            ]
            compatible_offsets: list[int] = []
            for offset in palette_candidates:
                candidate = pak.data[offset:offset + palette_bytes]
                candidate_ids = struct.unpack(
                    f"<{args.joint_count}I", candidate
                )
                mismatched_weighted = sorted(
                    joint for joint in package_use
                    if candidate_ids[joint] != target_ids[joint]
                )
                if mismatched_weighted:
                    raise ValueError(
                        f"palette 0x{offset:X} in {path} remaps weighted "
                        f"joints {mismatched_weighted}"
                    )
                if candidate != first_palette:
                    raise ValueError(
                        f"palette 0x{offset:X} in {path} differs from the "
                        "first compatible LOD palette"
                    )
                compatible_offsets.append(offset)
            first_ids = struct.unpack(
                f"<{args.joint_count}I", first_palette
            )
            differing_unweighted = [
                joint for joint, (actual, expected) in enumerate(
                    zip(first_ids, target_ids)
                )
                if actual != expected
            ]
            rendered_offsets = ",".join(
                f"0x{offset:X}" for offset in compatible_offsets
            )
            rendered_differences = ",".join(
                str(joint) for joint in differing_unweighted
            )
            print(
                f"compatible_joint_palettes={len(compatible_offsets)} "
                f"sha256={hashlib.sha256(first_palette).hexdigest()} "
                f"offsets=[{rendered_offsets}] "
                f"unweightedTargetDifferences=[{rendered_differences}]"
            )

        print(f"  package_joint_union={len(package_use)}")
        for joint in sorted(package_use):
            use = package_use[joint]
            print(
                "    "
                + format_joint(
                    joint,
                    use.vertex_references,
                    use.total_weight,
                    use.records,
                    names,
                )
            )

    print(f"global_joint_union={len(global_use)}")
    for joint in sorted(global_use):
        use = global_use[joint]
        print(
            "  "
            + format_joint(
                joint,
                use.vertex_references,
                use.total_weight,
                use.records,
                names,
            )
        )

    def parse_range(specification: str) -> range:
        parts = specification.split(":", 1)
        if len(parts) != 2:
            raise ValueError(f"expected FIRST:LAST, got {specification!r}")
        first, last = (int(part, 0) for part in parts)
        if first < 0 or last < first or last >= args.joint_count:
            raise ValueError(f"invalid joint range: {specification!r}")
        return range(first, last + 1)

    selected: set[int] = set()
    for specification in args.select_range:
        selected.update(joint for joint in parse_range(specification) if joint in global_use)
    for specification in args.force_range:
        selected.update(parse_range(specification))
    if selected:
        rendered = ",".join(str(joint) for joint in sorted(selected))
        print(
            f"selected_joint_union={len(selected)} paletteBytes={len(selected) * 48} "
            f"joints=[{rendered}]"
        )
        for joint in sorted(selected):
            if joint in global_use:
                use = global_use[joint]
                print(
                    "  "
                    + format_joint(
                        joint,
                        use.vertex_references,
                        use.total_weight,
                        use.records,
                        names,
                    )
                )
            else:
                print("  " + format_joint(joint, 0, 0, 0, names))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
