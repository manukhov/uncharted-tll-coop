#!/usr/bin/env python3
"""Bounded, read-only remaining-weapon depth audit (retail TLL 1.4.21058.0).

This is NOT a live torso collision solver or a proof of visual clearance.
It reuses the existing PSARC/PAK readers and retail Transform_Compose, admits
normal-registry art/model/primary tuples, and compares only mesh vertices
rigidly weighted to rifle-base root 0.  The anatomical vertical interval is
Nadine's bind pelvis-to-neck interval relative to spined, not a raw gun bbox.

For each projected weapon vertex, a 2-D triangle lookup finds the backmost
surface of the bind-pose body/shirt.  deficit = max(bodyDepth - weaponDepth).
The reported candidate is max(0, 0.05 + deficit(weapon) - deficit(Mazur)).
Consequently Mazur reproduces its accepted 5 cm *by calibration*, even if the
bind-torso proxy itself predicts penetration.  That residual and the spread
between the two axes MUST remain visible in the output.  Mixed torso skin,
animated non-root weapon parts, and other poses are not covered by this audit.

The second axis is reconstructed from the accepted Mazur cold row of PID20364,
reports/locomotion_hook.log:101171 (2026-08-27agb): exact authoredFrame Q bits
and the printed horizontal bodyBack vector.  It is not the Type95 screenshot's
same-frame axis; the latter was not captured.  No game process is opened,
launched, controlled, or modified, and no extracted assets are written.

Example: py -3.12 -B scripts/nadine_other_weapon_clearance_audit.py --art 27617D0D
         py -3.12 -B scripts/nadine_other_weapon_clearance_audit.py --art 3019CBEE
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
import hashlib
import math
from pathlib import Path
import struct

from chloe_mazur_holster_pose_audit import MARKER_BITS, compose, matrix_to_quaternion
import native_anim_pose_audit as native
from pak_skin_influence_audit import PagedPak, find_mesh_headers
from psarc_asset_audit import Psarc, string_id


BASE = native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
MAZUR = 0x33E29A1B
AD = 0x00AD047A
RIFLE_SKELETON_ID = 0xDFCE7D05
BODY_SKELETON = 0x53FE0
BODY_JOINTS = 580
SPINE_SKIN_JOINTS = frozenset((15, 16, 17, 18, 37, 38, 39))
ACCEPTED_FRAME_Q_BITS = (0x3E0D71F4, 0xBF54D839, 0x3F02044D, 0x3E365B49)
ACCEPTED_BODY_BACK = (0.0472585, 0.0, -0.016329)
EXPECTED_PRIMARY = {
    "P1": "3D9BA5E3 BCA3D70A BD4CCCCD 00000000 BD6477B3 BDCD7209 3E0A06FB 3F7BF4C4",
    "P2": "3DAC0831 BCFDF3B6 BD0B4396 00000000 BD6477B3 BDCD7209 3E0A06FB 3F7BF4C4",
    "P3": "3DC6A7F0 BD449BA6 BDA1CAC1 00000000 BD4DB3EA BDCBE230 3E0A9AE4 3F7C0861",
    "P6": "3D958106 3B449BA6 BD27EF9E 00000000 3CA1B283 BDB7B501 3E117CA0 3F7C4F51",
}


@dataclass(frozen=True)
class Weapon:
    model: str
    profile: str
    rig_fields: tuple[int, ...]


@dataclass(frozen=True)
class MeshOwnership:
    model_id: int
    lookup: int
    lookup_row: int
    artgroup: int
    foreground: int
    geometry: int
    headers: tuple[int, ...]


# These are exact model tuples, not weapon-class or old relay-name aliases.
# In particular, art 3019CBEE names ruger-semiauto-rifle, NOT the FAL DLC mesh.
WEAPONS = {
    MAZUR: Weapon("sniper-rifle-bolt", "P1", (0xA4ACC, 0xA4B4C, 0xA4BCC)),
    0x27617D0D: Weapon("qbz-95-rifle", "P3", (0x43CAC, 0x43D0C, 0x43DCC)),
    0x3019CBEE: Weapon("ruger-semiauto-rifle", "P1", (0x43BEC, 0x43C6C, 0x43E2C)),
    0x23C8B0AD: Weapon("ak-assault-rifle", "P2", (0x6FF8C, 0x7006C, 0x7012C)),
    0xC14CB0B7: Weapon("fal-burst-rifle-dlc", "P1", (0x46B2C, 0x46BAC, 0x46CAC)),
    0x354646B8: Weapon("shotgun", "P6", (0x5D64C, 0x5D6AC, 0x5D70C)),
}


OWNERSHIP = {
    0x27617D0D: MeshOwnership(0xA99E5C9F, 0x43D40, 0x43D78, 0x43700,
                            0x43DA0, 0x43960,
                            (0x411C8, 0x41278, 0x41328, 0x413D8)),
    0x3019CBEE: MeshOwnership(0x3D6F8C72, 0x43D40, 0x43D78, 0x435A0,
                            0x43E00, 0x43AA0,
                            (0x40CC0, 0x40D70, 0x40E20, 0x40ED0, 0x40F80, 0x41030)),
}


@dataclass(frozen=True)
class MemorySource:
    name: str
    data: bytes

    def read_bytes(self) -> bytes:
        return self.data

    def __str__(self) -> str:
        return self.name


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def normalized(values):
    length = math.sqrt(dot(values, values))
    if not math.isfinite(length) or length < 1.0e-8:
        raise ValueError("invalid direction/quaternion")
    return tuple(value / length for value in values)


def conjugate(q):
    return -q[0], -q[1], -q[2], q[3]


def multiply(a, b):
    x, y, z, w = a
    xx, yy, zz, ww = b
    return (w * xx + x * ww + y * zz - z * yy,
            w * yy - x * zz + y * ww + z * xx,
            w * zz + x * yy - y * xx + z * ww,
            w * ww - x * xx - y * yy - z * zz)


def rotate(q, vector):
    return multiply(multiply(q, (*vector, 0.0)), conjugate(q))[:3]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def bind_world(pak: PagedPak, skeleton: int, joint: int):
    inverse = skeleton + u32(pak.data, skeleton + 0x28)
    values = struct.unpack_from("<12f", pak.data, inverse + joint * 48)
    rotation = tuple(tuple(values[column * 4 + row] for column in range(3))
                     for row in range(3))
    translation = tuple(-dot(row, (values[3], values[7], values[11]))
                        for row in rotation)
    return translation, matrix_to_quaternion(rotation)


def positions(pak: PagedPak, mesh):
    streams = pak.resolve_absolute(mesh.absolute + 0x40)
    found = []
    for index in range(mesh.vertex_stream_count):
        stream = streams + index * 0x18
        count, stride, size = struct.unpack_from("<HHI", pak.data, stream)
        if count != 1 or size != mesh.vertex_count * stride:
            continue
        meta = pak.resolve_absolute(stream + 8)
        tag = pak.data[meta:meta + 4]
        if (stride, tag) == (8, b"\x00\x00\x08\x0a"):
            fmt = "<4e"
        elif (stride, tag) == (12, b"\x00\x00\x0c\x06"):
            fmt = "<3f"
        else:
            continue
        buffer = pak.resolve_absolute(stream + 0x10)
        rows = [struct.unpack_from(fmt, pak.data, buffer + vertex * stride)
                for vertex in range(mesh.vertex_count)]
        if not all(all(math.isfinite(value) for value in row) and
                   (len(row) == 3 or row[3] == 0) for row in rows):
            raise ValueError("invalid position stream")
        found.append([row[:3] for row in rows])
    if len(found) != 1:
        raise ValueError(f"expected one position stream at 0x{mesh.absolute:X}")
    return found[0]


def positive_joints(pak: PagedPak, mesh):
    descriptor = pak.resolve_absolute(mesh.absolute + 0x70)
    table = pak.resolve_absolute(descriptor + 0x10)
    records = pak.resolve_absolute(descriptor + 0x18)
    record_count = u32(pak.data, descriptor + 4)
    for vertex in range(mesh.vertex_count):
        count, offset = struct.unpack_from("<II", pak.data, table + vertex * 8)
        if count > 16 or offset & 3 or offset // 4 + count > record_count:
            raise ValueError("invalid sparse influence range")
        joints = set()
        weight_sum = 0
        for index in range(count):
            weight, encoded = struct.unpack_from("<HH", pak.data, records + offset + index * 4)
            if weight:
                joints.add(encoded >> 6)
                weight_sum += weight
        if joints == {0} and weight_sum != 65535:
            raise ValueError("root-only vertex is not fully weighted to root 0")
        yield joints


def verify_mesh_ownership(pak: PagedPak, weapon: Weapon, expected: MeshOwnership):
    """Check the typed package lookup/collection path, not just PAK filename."""
    def metadata_string(record, member):
        page, _ = pak.absolute_to_page(record)
        offset = pak.pages[page].file_offset + u64(pak.data, record + member)
        return pak.data[offset:pak.data.index(b"\0", offset)].decode("ascii")

    for record, record_type in ((expected.lookup, "PACKAGE_ITEM_LOOKUP"),
                                (expected.artgroup, "ARTGROUP_DATA"),
                                (expected.foreground, "FOREGROUND_SECTION_2"),
                                (expected.geometry, "GEOMETRY_1")):
        if metadata_string(record, 8) != record_type:
            raise ValueError(f"{weapon.model}: typed resource descriptor changed")
    collection_name = "pmCollection0." + weapon.model
    row = pak.resolve_absolute(expected.lookup + 0x30)
    if (row != expected.lookup_row or u32(pak.data, row) != expected.model_id or
            string_id(weapon.model.encode()) != expected.model_id or
            pak.resolve_absolute(row + 8) != expected.artgroup or
            metadata_string(expected.artgroup, 0) != weapon.model or
            metadata_string(expected.foreground, 0) != weapon.model or
            pak.read_string_pointer(expected.foreground + 0x20) != collection_name):
        raise ValueError(f"{weapon.model}: actual model lookup changed")
    table = pak.resolve_absolute(expected.geometry + 0x50)
    owned = []
    collection_names = set()
    expected_names = {collection_name, collection_name + "-lod1", collection_name + "-lod2"}
    for index in range(3):
        collection = pak.resolve_absolute(table + index * 8)
        name = pak.read_string_pointer(collection + 0x10)
        if name not in expected_names:
            raise ValueError(f"{weapon.model}: collection ownership changed")
        collection_names.add(name)
        meshes = pak.resolve_absolute(collection + 0x18)
        for member in range(u32(pak.data, collection + 4)):
            owned.append(pak.resolve_absolute(meshes + member * 16) - 8)
    if (collection_names != expected_names or tuple(sorted(owned)) != expected.headers or
            tuple(sorted(mesh.absolute for mesh in find_mesh_headers(pak, 22))) != expected.headers):
        raise ValueError(f"{weapon.model}: measured meshes are not its exact owned LOD set")
    print(f"OWNERSHIP model={weapon.model} modelId={expected.model_id:08X} "
          f"lookup=0x{row:X} artgroup=0x{expected.artgroup:X} "
          f"foreground=0x{expected.foreground:X} geometry=0x{expected.geometry:X} "
          f"ownedHeaders={','.join(f'{header:X}' for header in expected.headers)}")


def authored_binding(data: bytes, art: int, expected: Weapon) -> bytes:
    if data[:4] != b"00CD":
        raise ValueError("weapon-art entry is not DC00")
    roots = [u32(data, 0x20 + index * 16 + 8)
             for index in range(u32(data, 0x14))
             if u32(data, 0x20 + index * 16) == 0x61D589D9]
    if len(roots) != 1:
        raise ValueError("expected the normal weapon-art registry, not online")
    root = roots[0]
    capacity, keys, values = u32(data, root), u64(data, root + 8), u64(data, root + 0x10)
    slots = [index for index in range(capacity) if u32(data, keys + index * 4) == art]
    if len(slots) != 1:
        raise ValueError(f"art 0x{art:08X} is missing or ambiguous")
    definition = u64(data, values + slots[0] * 8)

    def model_string(member):
        pointer = u64(data, definition + member)
        if not pointer:
            return ""
        return data[pointer:data.index(b"\0", pointer)].decode("ascii")

    model, online_model = model_string(0x18), model_string(0x20)
    if model != expected.model:
        raise ValueError(f"model changed for {art:08X}: {model!r}")
    descriptor = u64(data, definition + 0xD8)
    primary = u64(data, descriptor + 8)
    if u32(data, primary) != AD or u64(data, descriptor + 0x10) != 0:
        raise ValueError("unexpected primary endpoint/alternative records")
    binding = data[primary + 0x10:primary + 0x30]
    expected_bits = tuple(int(word, 16) for word in EXPECTED_PRIMARY[expected.profile].split())
    if struct.unpack("<8I", binding) != expected_bits:
        raise ValueError(f"primary profile changed for {art:08X}")
    print(f"ART {art:08X} definition=0x{definition:X} model={model} "
          f"modelId={string_id(model.encode()):08X} onlineModel={online_model!r} "
          f"descriptor=0x{descriptor:X} primary=0x{primary:X} profile={expected.profile} "
          f"secondary=0x{u64(data, primary + 0x30):X}/0x{u64(data, primary + 0x38):X} "
          f"bits={' '.join(f'{value:08X}' for value in expected_bits)}")
    return binding


def body_triangles(load, spine_position, spine_quaternion):
    triangles = []
    strict_spine = Counter()
    for name in ("nadine-rural-shirt.pak", "nadine-rural-body.pak"):
        pak = load(name)
        for mesh in find_mesh_headers(pak, BODY_JOINTS):
            vertices = positions(pak, mesh)
            joints = list(positive_joints(pak, mesh))
            selected = {index for index, values in enumerate(joints)
                        if values & SPINE_SKIN_JOINTS}
            for values in joints:
                if len(values) == 1 and values <= SPINE_SKIN_JOINTS:
                    strict_spine[next(iter(values))] += 1
            if not selected:
                continue
            points = [rotate(conjugate(spine_quaternion),
                             tuple(row[i] - spine_position[i] for i in range(3)))
                      for row in vertices]
            buffer = pak.resolve_absolute(mesh.absolute + 0x50)
            indices = struct.unpack_from(f"<{mesh.index_count}H", pak.data, buffer)
            if mesh.index_count % 3 or max(indices) >= len(points):
                raise ValueError("invalid body triangle indices")
            for index in range(0, len(indices), 3):
                row = indices[index:index + 3]
                if any(vertex in selected for vertex in row):
                    triangles.append(tuple(points[vertex] for vertex in row))
    print(f"BODY triangles={len(triangles)} strictSpineVertices={dict(strict_spine)} "
          "WARNING=mixed-skin-bind-torso-is-not-the-live-torso")
    return triangles


def projected_triangle_grid(triangles, back, up, right, step):
    grid = defaultdict(list)
    for triangle in triangles:
        a, b, c = [(dot(right, row), dot(up, row), dot(back, row)) for row in triangle]
        denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(denominator) < 1.0e-12:
            continue
        for x in range(math.floor(min(row[0] for row in (a, b, c)) / step),
                       math.floor(max(row[0] for row in (a, b, c)) / step) + 1):
            for y in range(math.floor(min(row[1] for row in (a, b, c)) / step),
                           math.floor(max(row[1] for row in (a, b, c)) / step) + 1):
                grid[x, y].append((a, b, c, denominator))
    return grid


def deficit(points, grid, back, up, right, step):
    maximum = -math.inf
    worst = None
    hits = 0
    for point in points:
        x, y, z = dot(right, point), dot(up, point), dot(back, point)
        depth = None
        for a, b, c, denominator in grid.get((math.floor(x / step), math.floor(y / step)), ()):
            first = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / denominator
            second = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / denominator
            if first >= -1.0e-7 and second >= -1.0e-7 and first + second <= 1 + 1.0e-7:
                value = first * a[2] + second * b[2] + (1 - first - second) * c[2]
                depth = value if depth is None else max(depth, value)
        if depth is not None:
            hits += 1
            if depth - z > maximum:
                maximum, worst = depth - z, (x, y, z, depth)
    if not hits:
        raise ValueError("weapon has no projected overlap with the torso proxy")
    return maximum, hits, worst


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=BASE / "actor77.psarc")
    parser.add_argument("--dc-archive", type=Path, default=BASE / "dc1.psarc")
    parser.add_argument("--exe", type=Path, default=native.DEFAULT_GAME / "tll.exe")
    parser.add_argument("--art", action="append", help="normal-registry art hex; repeatable")
    parser.add_argument("--grid-step", type=float, default=0.0125,
                        help="triangle lookup cell size in m; not geometric sampling")
    args = parser.parse_args()
    if not math.isfinite(args.grid_step) or not 0.002 <= args.grid_step <= 0.1:
        parser.error("--grid-step must be in [0.002,0.1] m")
    arts = [MAZUR] + ([int(value, 16) for value in args.art] if args.art else
                     [0x27617D0D, 0x3019CBEE, 0x23C8B0AD, 0xC14CB0B7, 0x354646B8])
    arts = list(dict.fromkeys(arts))
    if any(art not in WEAPONS for art in arts):
        parser.error("art has no audited exact model/profile tuple")
    archive = Psarc(args.archive, None)
    cache = {}

    def load(name):
        if name not in cache:
            raw = archive.extract(name)
            print(f"PAK {name} bytes={len(raw)} sha256={hashlib.sha256(raw).hexdigest()}")
            cache[name] = PagedPak(MemorySource(name, raw))
        return cache[name]

    rifle = load("rifle-base.pak")
    if rifle.data[0x34C0:0x34C4] != b"\x05\x01HA" or u32(rifle.data, 0x34C4) != RIFLE_SKELETON_ID:
        raise ValueError("rifle skeleton changed")
    root = struct.unpack_from("<12f", rifle.data, 0x3540)
    inverse_root = struct.unpack_from("<12f", rifle.data, 0x39E0)
    if (root[:3] != (1, 1, 1) or root[4:8] != (0, 0, 0, 1) or
            root[8:11] != (0, 0, 0) or
            inverse_root != (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0) or
            u32(rifle.data, 0x3FF8) != string_id(b"root")):
        raise ValueError("common rifle root is not the audited identity")
    print("RIFLE skeleton=0x34C0 rootLocal=0x3540 inverseRoot=0x39E0 rootIdentity=1")
    skeleton = load("nadine-dlc-base.pak")
    spine_position, spine_quaternion = bind_world(skeleton, BODY_SKELETON, 18)
    body_back = rotate(conjugate(spine_quaternion), (0, 0, -1))
    body_up = rotate(conjugate(spine_quaternion), (0, 1, 0))
    limits = [dot(body_up, rotate(conjugate(spine_quaternion),
                                tuple(bind_world(skeleton, BODY_SKELETON, joint)[0][i] -
                                      spine_position[i] for i in range(3))))
              for joint in (3, 32)]
    print(f"ANATOMICAL_ROI pelvisToNeckCm={limits[0]*100:.9f},{limits[1]*100:.9f}")
    triangles = body_triangles(load, spine_position, spine_quaternion)
    dc = Psarc(args.dc_archive, None).extract("weapon-art.bin")
    print(f"DC weapon-art.bin sha256={hashlib.sha256(dc).hexdigest()}")
    marker = struct.pack("<8I", *MARKER_BITS)
    marker_q = normalized(struct.unpack_from("<4f", marker, 16))
    image = native.MappedImage(args.exe)
    try:
        clouds = {}
        for art in arts:
            spec = WEAPONS[art]
            binding = authored_binding(dc, art, spec)
            pak = load(spec.model + ".pak")
            if any(u32(pak.data, offset) != RIFLE_SKELETON_ID for offset in spec.rig_fields):
                raise ValueError("model's common-rifle skeleton reference changed")
            if art in OWNERSHIP:
                verify_mesh_ownership(pak, spec, OWNERSHIP[art])
            if art == 0x354646B8:
                print("COVERAGE shotgun excludes unclassified non-22-joint headers "
                      "596B8,598C8,59AD8; this is not whole-model coverage")
            vertices, total = [], 0
            for mesh in find_mesh_headers(pak, 22):
                points = positions(pak, mesh)
                joints = list(positive_joints(pak, mesh))
                total += len(points)
                vertices.extend(point for point, values in zip(points, joints) if values == {0})
            transform = struct.unpack("<8f", compose(image, marker, binding))
            quaternion = normalized(transform[4:])
            points = [tuple(transform[i] + value for i, value in enumerate(rotate(quaternion, point)))
                      for point in vertices]
            clouds[art] = [point for point in points if limits[0] <= dot(body_up, point) <= limits[1]]
            print(f"MESH {art:08X} validatedSkinVertices={total} strictRoot0={len(vertices)} "
                  f"anatomicalROI={len(clouds[art])}")
        accepted_q = normalized(struct.unpack("<4f", struct.pack("<4I", *ACCEPTED_FRAME_Q_BITS)))
        accepted_back = rotate(marker_q, rotate(conjugate(accepted_q), normalized(ACCEPTED_BODY_BACK)))
        accepted_up = rotate(marker_q, rotate(conjugate(accepted_q), (0, 1, 0)))
        results = defaultdict(list)
        for label, back, up in (("bind-body", body_back, body_up),
                                ("accepted-axis-bind-torso-rigid", accepted_back, accepted_up)):
            right = cross(up, back)
            grid = projected_triangle_grid(triangles, back, up, right, args.grid_step)
            print(f"AXIS {label} spinedBack={back} spinedUp={up} gridCells={len(grid)}")
            reference = None
            for art in arts:
                gap, hits, worst = deficit(clouds[art], grid, back, up, right, args.grid_step)
                if reference is None:
                    reference = gap
                candidate = max(0.0, 0.05 + gap - reference)
                results[art].append(candidate)
                print(f"RESULT {label} art={art:08X} deficitCm={gap*100:.6f} "
                      f"candidateCm={candidate*100:.6f} samples={hits} "
                      f"worstRightUpWeaponBodyCm={tuple(round(value*100, 6) for value in worst)}")
            print(f"CONTROL {label} acceptedMazur5cmProxyClearanceCm={(0.05-reference)*100:.6f} "
                  "WARNING=calibration-not-no-clipping-proof")
        for art, values in results.items():
            print(f"SUMMARY art={art:08X} model={WEAPONS[art].model} profile={WEAPONS[art].profile} "
                  f"candidateCm={','.join(f'{value*100:.6f}' for value in values)} "
                  f"upperCandidateCm={max(values)*100:.6f} spreadCm={(max(values)-min(values))*100:.6f}")
        print("LIMITS normal-registry exact models only; strict-root0 only; torso is mixed-skin "
              "bind proxy; two axes are not a pose envelope; vertex queries do not cover "
              "every triangle interior; no runtime/visual success is claimed.")
    finally:
        image.close()


if __name__ == "__main__":
    main()
