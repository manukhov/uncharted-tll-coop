"""Pure, conditional same-Nadine unmorphed hand-surface displacement audit.

No retail image, native pose evaluator, process, or game is opened. This consumes
the corrected direct-grip report and pinned PAK bytes only. It compares two
modeled poses of the SAME bind vertices, not contact with a weapon, not a visual
improvement metric, and not a reproduction of final rendered/morphed vertices.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import pak_skin_influence_audit as skin
import p99_grip_composition_audit as grip
import p99_locator_basis_audit as locator
from p99_nadine_profile_audit import DECODER_MODEL

ROOT = Path(__file__).resolve().parents[1]
BODY_SHA256 = "bc75642917f19fcc9f42b869131df1687bd1cc179635578352a83d87d611423f"
MESH_OFFSET = 0x181AE8
JOINTS = (28, 30, *range(105, 142, 2))
IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.)
SOURCE_CLASSIFICATION = "APPROXIMATE_PROFILE_PRESENT_R124_VS_CONDITIONAL_CHLOE_CHAIN"


def affine_point(matrix, point):
    matrix = locator.finite_tuple(matrix, 12, "inverse-bind Mat34")
    point = locator.finite_tuple(point, 3, "bind point")
    return tuple(sum(matrix[4*r+c]*point[c] for c in range(3)) + matrix[4*r+3]
                 for r in range(3))


def checked_frame(values):
    values = locator.finite_tuple(values, 12, "prop-relative frame")
    if values[:4] != (1., 1., 1., 0.) or values[11] != 0.:
        raise ValueError("surface frames must have unit scale and zero padding")
    if abs(sum(v*v for v in values[4:8])-1.) > 1e-10:
        raise ValueError("surface frame quaternion must be unit")
    return values


def report_frames(report):
    if (report.get("classification") != SOURCE_CLASSIFICATION or
            report.get("quaternion_decoder_model") != DECODER_MODEL or
            report.get("retail_or_game_execution") is not False or
            report.get("runtime_record_output") is not False or
            report.get("live_grip_or_skin_contact_proven") is not False):
        raise ValueError("unsupported or unversioned pose report")
    rows = report.get("prop_relative_joint_frames", ())
    if len(rows) != len(JOINTS) or {row["target_global"] for row in rows} != set(JOINTS):
        raise ValueError("pose report must contain the exact 21 joints without duplicates")
    direct = {r["target_global"]: checked_frame(r["direct_row"]) for r in rows}
    proposed = {r["target_global"]: checked_frame(r["proposed_row"]) for r in rows}
    for frames in (direct, proposed):
        if max(abs(a-b) for a, b in zip(frames[30], grip.row())) > 1e-10:
            raise ValueError("joint30 is not the identity prop-relative frame")
    return direct, proposed


def skin_point(point, influences, frames, inverse_bind):
    """Sum w * (propRelativeJoint * inverseBind * bindVertex); NEVER renormalize.

    Weights reproduce the audited shader's individual float32 conversion/mul;
    affine/quaternion/accumulation math is scalar double, not GPU instruction exact.
    Zero-weight references do not require a modeled joint transform.
    """
    point = locator.finite_tuple(point, 3, "bind point")
    output = [0., 0., 0.]
    positive = False
    for influence in influences:
        if not 0 <= influence.weight_quantized <= skin.PACKED_WEIGHT_MASK:
            raise ValueError("invalid quantized weight")
        if influence.weight_quantized == 0:
            continue
        positive = True
        if influence.joint not in frames or influence.joint not in inverse_bind:
            raise ValueError("positive influence escapes the modeled joint set")
        frame = checked_frame(frames[influence.joint])
        local = affine_point(inverse_bind[influence.joint], point)
        moved = grip.rotate(frame[4:8], local)
        weight = influence.weight
        for axis in range(3):
            output[axis] += weight * (moved[axis] + frame[8+axis])
    if not positive:
        raise ValueError("vertex has no positive influences")
    return locator.finite_tuple(output, 3, "skinned point")


def read_positions(pak, mesh):
    """Pinned body181AE8 uses one uncompressed FLOAT3 position stream."""
    streams = pak.resolve_absolute(mesh.absolute + 0x40)
    skin._require_page_span(pak, streams, mesh.vertex_stream_count * 24)
    matches = []
    for index in range(mesh.vertex_stream_count):
        stream = streams + 24*index
        count, stride, size = struct.unpack_from("<HHI", pak.data, stream)
        meta = pak.resolve_absolute(stream + 8)
        skin._require_page_span(pak, meta, 4)
        if (count, stride, pak.data[meta:meta+4]) != (1, 12, b"\x00\x00\x0c\x06"):
            continue
        if size != 12*mesh.vertex_count:
            raise ValueError("position stream size mismatch")
        start = pak.resolve_absolute(stream + 16)
        skin._require_page_span(pak, start, size)
        matches.append(tuple(locator.finite_tuple(struct.unpack_from("<3f", pak.data,
                            start + 12*i), 3, "mesh position")
                             for i in range(mesh.vertex_count)))
    if len(matches) != 1:
        raise ValueError("expected exactly one pinned FLOAT3 position stream")
    return matches[0]


def body_ownership(pak):
    """Typed normal LOD0 lookup -> named collection -> arms mesh and palette."""
    from p99_weapon_mesh_audit import _metadata
    from psarc_asset_audit import string_id
    d = pak.data
    for at, kind in ((0x197558, "PACKAGE_ITEM_LOOKUP"), (0x195D30, "ARTGROUP_DATA"),
                     (0x195F78, "GEOMETRY_1"), (0x1975B8, "FOREGROUND_SECTION_2")):
        if _metadata(pak, at, 8) != kind:
            raise ValueError("body typed resource changed")
    if (pak.resolve_absolute(0x197588) != 0x197590 or
            struct.unpack_from("<I", d, 0x197590)[0] != string_id(b"nadine-rural-body") or
            pak.resolve_absolute(0x197598) != 0x195D30 or
            _metadata(pak, 0x195D30, 0) != "nadine-rural-body"):
        raise ValueError("body model lookup changed")
    name = "pmCollection0.nadine-rural-body"
    if (pak.read_string_pointer(0x1975D8) != name or
            struct.unpack_from("<II", d, 0x1975E0) != (0xED25C708, 0xC39CA709) or
            pak.resolve_absolute(0x1975E8) != 0x1E15C0 or
            pak.resolve_absolute(0x195FC8) != 0x18A2F8 or
            pak.resolve_absolute(0x18A2F8) != 0x190130 or
            pak.read_string_pointer(0x190140) != name or
            struct.unpack_from("<I", d, 0x190134)[0] != 12 or
            pak.resolve_absolute(0x190148) != 0x18FED8 or
            pak.resolve_absolute(0x18FEE8) != MESH_OFFSET + 8):
        raise ValueError("body normal LOD0 mesh/palette ownership changed")
    mesh_name = pak.read_string_pointer(MESH_OFFSET + 0x10)
    if mesh_name != ("nadine-rural-body|geometry_grp|body_geo_grp|rural_lod0_grp|"
                     "rural_arms_lod0|rural_arms_lodShape0_shader1_merged_partition0"):
        raise ValueError("body arms mesh name changed")
    if (pak.resolve_absolute(MESH_OFFSET + 0x58) != 0x18D658 or
            pak.read_string_pointer(0x18D658) !=
            "objects/characters/nadine/dlc/arms:default,6bf0bfd5-e2e3-4e85-a67e-a06f94fa2807,physical-shader.fx"):
        raise ValueError("body arms material changed")
    return {"lookup": "197558", "artgroup": "195D30", "foreground": "1975B8",
            "geometry": "195F78", "collection_table": "18A2F8", "collection": "190130",
            "collection_name": name, "mesh_list": "18FED8", "mesh_member": 1,
            "mesh_name": mesh_name, "material": "18D658", "normal_palette": "1E15C0"}


def body_preskin_admission(pak):
    """Reject authored blend inputs in the exact normal arms/collection path.

    Native collection+0x40 gates MeshInstance+0x10 at 14187F646..6A9.
    Independently, native mesh+0x74 (file header+0x7C) is copied to the
    render resource+0x68 at 141878F32..40. A positive count allocates its
    +0x70 shape table from native mesh+0x78 (file+0x80). Only that count AND
    table permit SubMesh+0x40 creation at 14187A636..68E. The command builder
    also requires MeshInstance+0x10 at 14188366D and SubMesh+0x40/+0x08 at
    141883698. These are two independent absent gates, not zero live weights.

    This does not prove an arbitrary runtime instance has no other position
    writers or that post-skin wind/material presentation is absent.
    """
    body_ownership(pak)
    # 141965403 feeds descriptorArray + 176*meshIndex to 141878860. The
    # normal collection's member1 pointer and index must identify that row.
    native_array = pak.resolve_absolute(0x195F78 + 0x48)
    mesh_index = struct.unpack_from("<i", pak.data, 0x18FEE8 + 8)[0]
    if (struct.unpack_from("<I", pak.data, 0x195F78 + 0x28)[0] != 37 or
            native_array != 0x181A40 or mesh_index != 1 or
            native_array + 176*mesh_index != MESH_OFFSET + 8):
        raise ValueError("pre-skin native mesh descriptor ownership changed")
    fields = ((0x190130 + 0x38, "Q", "collection blend-name table"),
              (0x190130 + 0x40, "I", "collection blend-input count"),
              (MESH_OFFSET + 0x7C, "I", "mesh blend-shape count"),
              (MESH_OFFSET + 0x80, "Q", "mesh blend-shape table"))
    for offset, kind, label in fields:
        skin._require_page_span(pak, offset, struct.calcsize("<" + kind))
        if struct.unpack_from("<" + kind, pak.data, offset)[0] != 0:
            raise ValueError(f"authored pre-skin {label} is not absent")
        # A zero page-local pointer can be valid: raw zero alone is insufficient.
        if kind == "Q" and pak.fixups.get(pak.absolute_to_page(offset)):
            raise ValueError(f"authored pre-skin {label} has a pointer fixup")
    return {"classification": "STATIC_AUTHORED_SUBMESH_BLEND_ABSENT",
            "collection": "190130", "mesh": "181AE8", "native_mesh_descriptor": "181AF0",
            "collection_blend_input_count": 0, "collection_blend_name_table": None,
            "mesh_blend_shape_count": 0, "mesh_blend_shape_table": None,
            "authored_blendshape_delta_absent": True,
            "runtime_no_other_position_writers_proven": False,
            "scope": "Normal pinned rural arms resource path only; not a live-instance or post-skin proof"}


def summarize(distances):
    distances = sorted(distances)
    if not distances or not all(math.isfinite(x) and x >= 0 for x in distances):
        raise ValueError("invalid or empty surface distances")
    return {"count": len(distances), "min_asset_units": distances[0],
            "mean_asset_units": math.fsum(distances)/len(distances),
            "rms_asset_units": math.sqrt(math.fsum(x*x for x in distances)/len(distances)),
            "p95_nearest_rank_asset_units": distances[math.ceil(.95*len(distances))-1],
            "max_asset_units": distances[-1]}


def compare_surface(positions, vertex_influences, direct, proposed, inverse_bind):
    if len(positions) != len(vertex_influences) or not positions:
        raise ValueError("vertex position/influence counts disagree")
    if set(direct) != set(proposed):
        raise ValueError("pose joint sets disagree")
    selected = set(direct)
    distances, samples, indices = [], [], []
    by_joint = {joint: [] for joint in sorted(selected)}
    sums, touched = [], 0
    for index, (point, influences) in enumerate(zip(positions, vertex_influences)):
        support = {row.joint for row in influences if row.weight_quantized > 0}
        touched += bool(support & selected)
        if not support or not support <= selected:
            continue
        a = skin_point(point, influences, direct, inverse_bind)
        b = skin_point(point, influences, proposed, inverse_bind)
        delta = tuple(b[i]-a[i] for i in range(3))
        distance = math.sqrt(math.fsum(x*x for x in delta))
        distances.append(distance)
        indices.append(index)
        sums.append(math.fsum(row.weight for row in influences))
        for joint in support:
            by_joint[joint].append(distance)
        samples.append({"vertex": index, "positive_joints": sorted(support),
                        "direct_prop_relative_asset_units": list(a),
                        "proposed_prop_relative_asset_units": list(b),
                        "delta_asset_units": list(delta), "distance_asset_units": distance})
    summary = summarize(distances)
    return {"all_vertices": len(positions), "touched_vertices": touched,
            "closed_vertices": len(indices), "excluded_touched_vertices": touched-len(indices),
            "closed_vertex_indices_sha256": hashlib.sha256(struct.pack(f"<{len(indices)}I", *indices)).hexdigest(),
            "displacement": summary,
            "weight_sum_min": min(sums), "weight_sum_max": max(sums),
            "weight_sum_renormalized": False,
            "per_positive_joint_overlapping_subsets": {
                str(joint): summarize(values) for joint, values in by_joint.items() if values},
            "largest_displacements": sorted(samples, key=lambda row: (-row["distance_asset_units"], row["vertex"]))[:20]}


def audit(report, body_path, skeleton_path):
    direct, proposed = report_frames(report)
    body = skin.PagedPak(Path(body_path))
    if hashlib.sha256(body.data).hexdigest() != BODY_SHA256:
        raise ValueError("changed Nadine body asset")
    meshes = [m for m in skin.find_mesh_headers(body, 580) if m.absolute == MESH_OFFSET]
    if len(meshes) != 1 or meshes[0].vertex_count != 6448:
        raise ValueError("pinned body mesh missing or changed")
    ownership = body_ownership(body)
    preskin = body_preskin_admission(body)
    skeleton_pak = skin.PagedPak(Path(skeleton_path))
    spec = locator.source.NADINE_SKELETON
    _, matrices, _ = locator.load_basis(skeleton_pak.data, spec)
    names = skin.read_joint_names(skeleton_pak, spec.joint_table_offset, spec.joint_count)
    if any(row["target_name"] != names[row["target_global"]][1]
           for row in report["prop_relative_joint_frames"]):
        raise ValueError("pose report joint names do not match Nadine")
    mesh = meshes[0]
    influences = tuple(skin.iter_vertex_influences(body, mesh, 580))
    referenced = {row.joint for rows in influences for row in rows}
    palettes = skin.find_joint_hash_table_candidates(body, names, 580)
    if len(palettes) != 4 or any(struct.unpack_from("<I", body.data, offset+4*joint)[0]
            != names[joint][0] for offset in palettes for joint in referenced):
        raise ValueError("body palette does not preserve referenced joint semantics")
    result = compare_surface(read_positions(body, mesh), influences, direct, proposed,
                             {joint: matrices[joint] for joint in JOINTS})
    if result["closed_vertices"] != 3040:
        raise ValueError("pinned closed surface subset changed")
    return {"classification": "APPROXIMATE_CONDITIONAL_SAME_NADINE_UNMORPHED_SURFACE_DELTA",
            "quaternion_decoder_model": DECODER_MODEL,
            "retail_or_game_execution": False, "runtime_writes": False,
            "skin_contact_or_improvement_proven": False,
            "geometry_scope": "Same bind vertices in modeled prop-relative frames; NOT actual fingertips, weapon contact or final rendered surface",
            "body_sha256": BODY_SHA256, "mesh_offset": hex(MESH_OFFSET),
            "ownership": ownership,
            "preskin_admission": preskin,
            "skeleton_sha256": hashlib.sha256(skeleton_pak.data).hexdigest(),
            "source_report_canonical_sha256": hashlib.sha256(json.dumps(report, sort_keys=True,
                separators=(",", ":"), allow_nan=False).encode()).hexdigest(),
            "palette_offsets": [hex(value) for value in palettes],
            "formula": "sum_j(weight_j * (propRelativeJoint_j * inverseBind_j * bindVertex))",
            "weight_model": "u32 joint>>22, quantized&0x3FFFFF; f32(quantized*f32bits34800002); no renormalization",
            "units": "authored asset units; no physical length calibration asserted",
            **result,
            "limits": ["Conditional D350/B8 subtree; R157 basis and live final owner remain unproven",
                       "Pinned normal arms/collection have no authored SubMeshBlend inputs or shapes; arbitrary live position writers and post-skin wind/material deformation are not reproduced",
                       "Only vertices whose positive influences lie entirely within the 21 modeled joints",
                       "Scalar pose, matrix and accumulation math is approximate, not native/GPU bit exact",
                       "Common prop-frame affine is applied per influence; moving translation after the sum differs by (sumWeights-1)*translation, so nonunit quantized sums do not prove exact world-frame cancellation",
                       "Prop-relative frames omit physical weapon mesh/attachment alignment and world placement",
                       "Displacement magnitude is not a contact, penetration, quality or improvement score",
                       "No runtime records, pose writes, game test or fix authorization"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pose-report", type=Path, required=True)
    parser.add_argument("--body", type=Path, default=ROOT/"reports/actor_packages/nadine-rural-body.pak")
    parser.add_argument("--skeleton", type=Path, default=ROOT/"reports/actor_packages/nadine-dlc-base.pak")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(json.loads(args.pose_report.read_text(encoding="utf-8")), args.body, args.skeleton)
    rendered = json.dumps(result, indent=2, allow_nan=False)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered+"\n", encoding="utf-8")
    print(rendered)


if __name__ == "__main__":
    main()
