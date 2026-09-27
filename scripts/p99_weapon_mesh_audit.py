#!/usr/bin/env python3
"""Read-only, pinned P99 main/grip-material geometry in weapon bind-root space.

No retail game function is mapped or called. PSARC extraction uses the existing
archive reader/Oodle decompressor over a read-only mapping. This is an ownership
and bind-frame audit, NOT live presentation, skin contact, or a grip-only ROI.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import mmap
from pathlib import Path
import struct

from pak_skin_influence_audit import PagedPak, find_mesh_headers, read_joint_names
from nadine_other_weapon_clearance_audit import positions
from psarc_asset_audit import Psarc, string_id


DEFAULT_ARCHIVE = Path(
    r"C:\UNCHARTED Legacy of Thieves Collection (2016-2017-2022)"
    r"\Uncharted Legacy of Thieves Collection\Uncharted4_data\build\pc"
    r"\thelostlegacy\actor77.psarc")
P99_ENTRY = "p99-silencer-dlc.pak"
PISTOL_ENTRY = "pistol-base.pak"
P99_SHA256 = "af2982391ffa7212fdbae08da13a56084736b6c53f817d3c1a5c0ed698c65866"
PISTOL_SHA256 = "934840e417e2b57af11ad17369b2b391894e5d64bfb8d558b4c1194186a27838"
MODEL = "p99-silencer-dlc"
MODEL_ID = 0x255BEBA0
DATABASE_ID = 0xE97C9972
SKELETON_ID = 0x50497451
ROOT_ID = 0x19D64D2A
GRIP_MATERIAL = (
    "objects/weapons/p99/p99-u4-mp/p99-silencer-dlc/p99-u4-dlc-grip:default,"
    "ab54ae2f-550e-427c-8091-e2506633f659,physical-shader.fx")
FRAME_MODEL = "conditional-p99-weapon-bind-root-v1"


@dataclass(frozen=True)
class BytesSource:
    data: bytes

    def read_bytes(self) -> bytes:
        return self.data


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _span(pak: PagedPak, offset: int, size: int) -> None:
    require(size > 0, "empty span")
    page, local = pak.absolute_to_page(offset)
    require(local + size <= pak.pages[page].size, "span crosses PAK page")


def _metadata(pak: PagedPak, record: int, member: int) -> str:
    page, _ = pak.absolute_to_page(record)
    local = struct.unpack_from("<Q", pak.data, record + member)[0]
    require(local < pak.pages[page].size, "metadata string exceeds page")
    absolute = pak.pages[page].file_offset + local
    end = pak.data.find(b"\0", absolute,
                        pak.pages[page].file_offset + pak.pages[page].size)
    require(end >= absolute, "unterminated metadata string")
    return pak.data[absolute:end].decode("ascii")


def _validate_skeleton(pak: PagedPak) -> dict:
    d = pak.data
    require(_metadata(pak, 0x740, 0) == "skel.pistol-base" and
            _metadata(pak, 0x740, 8) == "JOINT_HIERARCHY", "skeleton resource type")
    require((u32(d, 0x764), u32(d, 0x768)) == (DATABASE_ID, SKELETON_ID),
            "skeleton database/id")
    require(pak.resolve_absolute(0x780) == 0x800 and
            d[0x800:0x804] == b"\x05\x01HA" and u32(d, 0x804) == SKELETON_ID,
            "raw skeleton ownership")
    require(struct.unpack_from("<HH", d, 0x810) == (23, 17), "skeleton dimensions")
    require(0x800 + u32(d, 0x820) == 0x880 and
            0x800 + u32(d, 0x828) == 0x1330, "root reference offsets")
    require(pak.resolve_absolute(0x798) == 0x1998, "joint name table")
    names = read_joint_names(pak, 0x1998, 23)
    require(names[0] == (ROOT_ID, "root") and string_id(b"root") == ROOT_ID,
            "root semantic")
    local = struct.unpack_from("<12f", d, 0x880)
    inverse = struct.unpack_from("<12f", d, 0x1330)
    require(local == (1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0),
            "root local is not identity")
    require(inverse == (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0),
            "root inverse bind is not identity")
    return {"descriptor": "740", "raw": "800", "database": f"{DATABASE_ID:08X}",
            "skeleton": f"{SKELETON_ID:08X}", "joint_count": 23,
            "parameter_count": 17, "joint_name_table": "1998",
            "root_semantic": f"{ROOT_ID:08X}", "root_name": "root",
            "root_local_offset": "880", "root_local_sqt": list(local),
            "root_inverse_bind_offset": "1330", "root_inverse_bind": list(inverse)}


def _validate_ownership(pak: PagedPak) -> dict:
    d = pak.data
    for address, name in ((0xB0CA0, "PACKAGE_ITEM_LOOKUP"),
                          (0xB06A0, "ARTGROUP_DATA"),
                          (0xB0D00, "FOREGROUND_SECTION_2"),
                          (0xB0A40, "GEOMETRY_1")):
        require(_metadata(pak, address, 8) == name, "typed resource ownership")
    row = pak.resolve_absolute(0xB0CD0)
    require(row == 0xB0CD8 and u32(d, row) == MODEL_ID and
            string_id(MODEL.encode()) == MODEL_ID and
            pak.resolve_absolute(row + 8) == 0xB06A0, "model lookup ownership")
    require(_metadata(pak, 0xB06A0, 0) == MODEL, "artgroup model name")
    collections = []
    require(pak.resolve_absolute(0xB0A90) == 0xAD150, "geometry collection table")
    headers = []
    for index, (fg, collection, mesh_list) in enumerate((
            (0xB0D00, 0xAE7B0, 0xAF9E0),
            (0xB0C40, 0xAE840, 0xAFA10),
            (0xB0BE0, 0xAE8D0, 0xAFA40))):
        model = MODEL + (f"-lod{index}" if index else "")
        name = "pmCollection0." + model
        require(_metadata(pak, fg, 0) == model and
                _metadata(pak, fg, 8) == "FOREGROUND_SECTION_2" and
                pak.read_string_pointer(fg + 0x20) == name and
                (u32(d, fg + 0x28), u32(d, fg + 0x2C)) ==
                (DATABASE_ID, SKELETON_ID), "foreground skeleton/LOD ownership")
        require(pak.resolve_absolute(0xAD150 + 8*index) == collection and
                pak.read_string_pointer(collection + 0x10) == name and
                u32(d, collection + 4) == 3 and
                pak.resolve_absolute(collection + 0x18) == mesh_list,
                "collection ownership")
        owned = [pak.resolve_absolute(mesh_list + 16*j) - 8 for j in range(3)]
        require(owned == [0xAC518 + 0xB0*(3*index+j) for j in range(3)],
                "submesh ownership")
        headers.extend(owned)
        collections.append({"lod": index, "foreground": f"{fg:X}",
                            "collection": f"{collection:X}",
                            "mesh_list": f"{mesh_list:X}",
                            "headers": [f"{h:X}" for h in owned]})
    require([h.absolute for h in find_mesh_headers(pak, 23)] == headers,
            "complete 23-joint mesh inventory")
    return {"lookup": "B0CA0", "lookup_row": "B0CD8", "model": MODEL,
            "model_id": f"{MODEL_ID:08X}", "artgroup": "B06A0",
            "geometry": "B0A40", "collection_table": "AD150", "lods": collections}


def _read_main_mesh(pak: PagedPak) -> tuple[dict, list, list]:
    d = pak.data
    mesh = next(h for h in find_mesh_headers(pak, 23) if h.absolute == 0xAC5C8)
    require((mesh.vertex_count, mesh.index_count, mesh.vertex_stream_count) ==
            (1567, 6870, 4), "main mesh dimensions")
    require(pak.read_string_pointer(mesh.absolute + 0x10) ==
            MODEL + "|geo_grp|main|mainShape_shader1_merged_partition0",
            "main mesh name")
    material = pak.resolve_absolute(mesh.absolute + 0x58)
    require(material == 0xAE140 and pak.resolve_absolute(material) == 0x95C18 and
            pak.read_string_pointer(material) == GRIP_MATERIAL and
            pak.read_string_pointer(material + 8) == "physical-shader.fx",
            "main grip material ownership")
    require(pak.resolve_absolute(mesh.absolute + 0x40) == 0xADD70 and
            pak.resolve_absolute(0xADD78) == 0xAFA84 and
            d[0xAFA84:0xAFA88] == b"\0\0\x08\x0A" and
            pak.resolve_absolute(0xADD80) == 0xE745C,
            "main half4 position stream")
    _span(pak, 0xE745C, 1567*8)
    vertices = positions(pak, mesh)  # Existing pure half/f32 helper; no mapped main.
    index_base = pak.resolve_absolute(mesh.absolute + 0x50)
    require(index_base == 0x118C18, "main index buffer")
    _span(pak, index_base, 6870*2)
    indices = struct.unpack_from("<6870H", d, index_base)
    require(max(indices) < len(vertices), "main index outside vertex span")
    triangles = [list(indices[i:i+3]) for i in range(0, len(indices), 3)]
    sparse = pak.resolve_absolute(mesh.absolute + 0x70)
    require(sparse == 0xAE980 and pak.resolve_absolute(sparse + 0x10) == 0x11F3A0 and
            pak.resolve_absolute(sparse + 0x18) == 0x126118,
            "main sparse influence ownership")
    _span(pak, 0x11F3A0, len(vertices)*8)
    for vertex in range(len(vertices)):
        count, offset = struct.unpack_from("<II", d, 0x11F3A0 + vertex*8)
        require(count == 1 and offset % 4 == 0, "main singleton influence layout")
        _span(pak, 0x126118 + offset, 4)
        require(u32(d, 0x126118 + offset) == 0x003FFFFF,
                "main raw singleton influence golden")
    report = {"header": "AC5C8", "native_descriptor": "AC5D0",
              "material": "AE140", "material_name_offset": "95C18",
              "material_name": GRIP_MATERIAL, "position_buffer": "E745C",
              "position_encoding": "half4, spatial xyz; all w=0",
              "index_buffer": "118C18", "vertex_count": len(vertices),
              "index_count": len(indices), "triangle_count": len(triangles),
              "raw_singleton_influence": "003FFFFF", "sparse_descriptor": "AE980",
              "sparse_vertex_rows": "11F3A0", "sparse_records": "126118",
              "skin_weight_semantics_validated": False,
              "aabb_asset_units": [[min(v[i] for v in vertices),
                                     max(v[i] for v in vertices)] for i in range(3)]}
    return report, vertices, triangles


def audit_assets(p99: bytes, pistol: bytes, *, include_geometry: bool = False) -> dict:
    require(len(p99) == 1978080 and hashlib.sha256(p99).hexdigest() == P99_SHA256,
            "P99 asset size/SHA256")
    require(len(pistol) == 86976 and hashlib.sha256(pistol).hexdigest() == PISTOL_SHA256,
            "pistol-base asset size/SHA256")
    weapon = PagedPak(BytesSource(p99))
    skeleton = _validate_skeleton(PagedPak(BytesSource(pistol)))
    ownership = _validate_ownership(weapon)
    mesh, vertices, triangles = _read_main_mesh(weapon)
    result = {"frame_model": FRAME_MODEL, "coordinates": "weapon-bind-root-local",
              "live_frame_proven": False,
              "additional_authored_mesh_transform_proven_absent": False,
              "geometry_scope": "Entire main partition using the grip material; not an isolated contact ROI.",
              "right_prop_frame_condition": (
                  "A rightProp30 comparison additionally requires identity posed weapon root and "
                  "a verified same-Nadine weaponWorld/rightPropWorld relation. Historical ACI "
                  "identity samples are conditional, not proof for Chloe or every live state."),
              "limits": "No posed skin, collision/contact verdict, render shader displacement, or live acceptance.",
              "assets": {P99_ENTRY: P99_SHA256, PISTOL_ENTRY: PISTOL_SHA256},
              "ownership": ownership, "skeleton": skeleton, "main_mesh": mesh}
    if include_geometry:
        result["vertices_asset_units"] = [list(v) for v in vertices]
        result["triangles"] = triangles
    return result


def load_assets(archive: Path = DEFAULT_ARCHIVE) -> tuple[bytes, bytes]:
    with archive.open("rb") as stream, mmap.mmap(stream.fileno(), 0,
                                                access=mmap.ACCESS_READ) as mapping:
        pak = Psarc(archive, None, data=mapping)
        return pak.extract(P99_ENTRY), pak.extract(PISTOL_ENTRY)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--include-geometry", action="store_true")
    args = parser.parse_args()
    print(json.dumps(audit_assets(*load_assets(args.archive),
                                 include_geometry=args.include_geometry), indent=2))


if __name__ == "__main__":
    main()
