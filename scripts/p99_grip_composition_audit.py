"""Pure scalar audit of P99 composition; reads frozen artifacts, never retail code.

Independent Hamilton-quaternion algebra, not an instruction-exact float oracle.
STATIC 141AE2717..2803 (mode1 absolute), 141AE2638..2712 (mode1 additive),
141AE28F3..28FC (mode3 copy), and 142191AE0 (mode2, weight1) give:
    R_abs(Q) = conjugate(E) * Q * A
    R_add(B) = conjugate(A) * B * A
where A/E are normalized half quaternions at record+6/+14hex. Absolute
mode1 copies target reference S/T; additive mode1 supplies S=1,T=0. The
expanded target-reference quaternion is NOT used by this admitted mode.

For the pinned unit-scale, zero-translation additive rows, weight1 Blend(X,B)
has S=X.S, Q=X.Q*B.Q, T=X.T. Thus R_abs(Blend(X,B)) and
Blend(R_abs(X),R_add(B)) agree for mode1; mode3 copies, so also agrees.
This alone does not establish R124_abs == R157_abs composed with R20_abs.
The generic rigid-composition helper below supports synthetic algebra/geometry;
frozen stages do not validate a general nonzero-additive-T native implementation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PREFIX = ROOT / "reports/p99-full-chain-agz-final"
DEFAULT_METADATA = ROOT / "reports/p99-native-source-inputs.json"
DEFAULT_HEADER = ROOT / "scanner/src/nadine_p99_chloe_source_rows.h"
COMPONENT_TOLERANCE = 1e-5
ALGEBRA_TOLERANCE = 1e-12
PINS = {
    "input": (3948, "1e54a246eddfa42597ed8fb1b912c8ee19a2c550b1fa6c2ea73d1507aac9eb7b"),
    "stages": (2016, "1fc8a73302352ecfcc3605df1ca040fd412374ec771f864df435a81ba8050e86"),
    "output": (1360, "c91e639d918eb2492212b0c41d9d7f152217aecfd67eaa27ecc8d4a85aab4209"),
}
METADATA_SHA = "332a6804ac5f652a529f01bce458121000c01a18fa62edfa1ec2c8ea0c84e2e1"
SOURCE_SHAS = (
    "9d3a72550481469533c9c1df82f864d5027c75e1fbe7dc7f2e317f0781bba727",
    "1735098f297a40b996325966f95307ed07fc7c98276aa27877967dc098be2ecf",
)


def pinned(data: bytes, key: str) -> None:
    size, digest = PINS[key]
    if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f"changed frozen {key} artifact")


def qmul(a, b):
    x, y, z, w = a
    X, Y, Z, W = b
    return (w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z)


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qnorm(q):
    if len(q) != 4 or not all(math.isfinite(x) for x in q):
        raise ValueError("nonfinite quaternion")
    length = math.sqrt(sum(x*x for x in q))
    if not math.isfinite(length) or length == 0:
        raise ValueError("zero/overflow quaternion")
    return tuple(x/length for x in q)


def qerror(a, b):
    # Keep magnitude errors visible; only the physically equivalent sign is free.
    return min(max(abs(x-y) for x, y in zip(a, b)),
               max(abs(x+y) for x, y in zip(a, b)))


def rotate(q, v):
    # Polynomial vector rotation, v + 2*(w*(q.xyz cross v) + q.xyz cross (q.xyz cross v)).
    # Unlike q*v*conjugate(q), this preserves the native form for near-unit q.
    x, y, z, w = q
    X, Y, Z = v
    c = (y*Z-z*Y, z*X-x*Z, x*Y-y*X)
    d = (y*c[2]-z*c[1], z*c[0]-x*c[2], x*c[1]-y*c[0])
    return tuple(v[i]+2*(w*c[i]+d[i]) for i in range(3))


def row(scale=(1., 1., 1.), rotation=(0., 0., 0., 1.), translation=(0., 0., 0.)):
    return (*scale, 0., *rotation, *translation, 0.)


def read_row(data: bytes, offset: int = 0):
    result = struct.unpack_from("<12f", data, offset)
    if not all(math.isfinite(result[i]) for i in range(12) if i not in (3, 11)):
        raise ValueError("nonfinite meaningful row lane")
    if abs(sum(x*x for x in result[4:8])-1) > COMPONENT_TOLERANCE:
        raise ValueError("row quaternion outside audited unit envelope")
    return result


def row_error(a, b):
    return max(qerror(a[4:8], b[4:8]),
               *(abs(a[i]-b[i]) for i in (0, 1, 2, 8, 9, 10)))


def blend(base, additive):
    # Scalar composition model, weight1 only. Native frozen comparison exercises
    # only additive S=1/T=0, not general translation or weighted interpolation.
    moved = rotate(base[4:8], tuple(base[i]*additive[i+8] for i in range(3)))
    return row(tuple(base[i]*additive[i] for i in range(3)),
               qmul(base[4:8], additive[4:8]),
               tuple(base[i+8]+moved[i] for i in range(3)))


def retarget(record: bytes, source, reference, additive=False):
    if len(record) != 46 or record[44] not in (1, 3):
        raise ValueError("only complete mode1/mode3 records admitted")
    # Native decodes both locators even for mode3. Do not hide invalid ones.
    A = qnorm(struct.unpack_from("<4e", record, 6))
    E = qnorm(struct.unpack_from("<4e", record, 0x14))
    if record[44] == 3:
        return source
    if additive:
        return row(rotation=qmul(qmul(qconj(A), source[4:8]), A))
    return row(reference[:3], qmul(qmul(qconj(E), source[4:8]), A), reference[8:11])


def parse_sources(header: str):
    body = header.split("kRows{{", 1)[1].split("}};", 1)[0]
    blocks = re.findall(r"\{((?:0x[0-9A-Fa-f]+u,?\s*){12})\}", body)
    if len(blocks) != 42:
        raise ValueError("expected 21 paired source rows")
    raw = [struct.pack("<12I", *(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)u", block)))
           for block in blocks]
    for parity, digest in enumerate(SOURCE_SHAS):
        if hashlib.sha256(b"".join(raw[parity::2])).hexdigest() != digest:
            raise ValueError("changed embedded source values")
    return [read_row(value) for value in raw]


def parent_indices(transfers: list[dict], skeleton: str):
    if len(transfers) != 21:
        raise ValueError("expected all 21 semantic rows")
    globals_ = [entry[f"{skeleton}_global"] for entry in transfers]
    if len(set(globals_)) != 21:
        raise ValueError("duplicate global joint")
    lookup = {joint: i for i, joint in enumerate(globals_)}
    parents = [-1]
    for index, entry in enumerate(transfers[1:], 1):
        parent = lookup.get(entry[f"{skeleton}_parent_global"], -1)
        if parent < 0 or parent >= index:
            raise ValueError("distal subtree is not closed/topologically ordered")
        parents.append(parent)
    if parents[1] != 0:
        raise ValueError("prop is not a direct palm child")
    return parents


def distal_frames(rows, parents):
    """Rigid frames relative to palm; valid only for this unit-scale fixture.

    Normalize quaternions ONLY for physical geometry; component comparison above
    does not normalize away native norm errors. Palm's wrist ancestry cancels.
    """
    if len(rows) != 21 or len(parents) != 21 or parents[0] != -1:
        raise ValueError("invalid distal subtree")
    frames = [row()]
    for index in range(1, 21):
        if not 0 <= parents[index] < index:
            raise ValueError("invalid distal parent")
        local = rows[index]
        if max(abs(v-1) for v in local[:3]) > COMPONENT_TOLERANCE:
            raise ValueError("geometry fixture must have unit scale")
        frames.append(blend(frames[parents[index]], row(rotation=qnorm(local[4:8]), translation=local[8:11])))
    return frames


def prop_relative(frames):
    prop = frames[1]
    inverse = qconj(qnorm(prop[4:8]))
    return [row(rotation=qmul(inverse, qnorm(item[4:8])),
                translation=rotate(inverse, tuple(item[8+j]-prop[8+j] for j in range(3))))
            for item in frames]


def angular_degrees(a, b):
    relative = qmul(qconj(qnorm(a)), qnorm(b))
    return math.degrees(2*math.atan2(math.sqrt(sum(x*x for x in relative[:3])), abs(relative[3])))


def validate_row124_records(records, transfers):
    if len(records) != 71 or len(transfers) != 21:
        raise ValueError("row124 record/selection count changed")
    selected = []
    for index, transfer in enumerate(transfers):
        semantic = int(transfer["semantic"], 16)
        matches = [record for record in records if record.source_id == semantic]
        if len(matches) != 1:
            raise ValueError("row124 missing/duplicate selected semantic")
        record = matches[0]
        if (record.target_id != semantic or record.source_global != transfer["player_global"] or
                record.target_global != transfer["nadine_global"] or record.mode != (3 if index == 1 else 1)):
            raise ValueError("row124 semantic joint/mode disagreement")
        selected.append(record)
    return {"resolved_records": len(records), "selected_semantics": len(selected),
            "selected_mode1": 20, "selected_mode3": 1,
            "prop": {"semantic": f"{selected[1].source_id:08X}",
                     "source_global": selected[1].source_global,
                     "target_global": selected[1].target_global, "mode": selected[1].mode,
                     "authored_map_index": selected[1].map_index}}


def audit_row124_map(transfers, archive=None, dc_archive=None):
    # Import definitions only. Never construct PreparationFixture, RetargetImage,
    # MappedImage or OwnedBuffer; no numerical builder/profile lookup is invoked.
    import p99_preparation_fixture as fixture
    source = fixture.source
    base = source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
    archive = Path(archive) if archive is not None else base / "actor77.psarc"
    dc_archive = Path(dc_archive) if dc_archive is not None else base / "dc1.psarc"
    parser = source.native.load_audit_module()
    actor = parser.Psarc(archive, None)
    player = source.load_skeleton(actor.extract(source.PLAYER_SKELETON.entry_name), source.PLAYER_SKELETON)
    nadine = source.load_skeleton(actor.extract(source.NADINE_SKELETON.entry_name), source.NADINE_SKELETON)
    retarget_data = parser.Psarc(dc_archive, None).extract(source.RETARGET_ENTRY_NAME)
    mapping = fixture.row124_mapping(retarget_data)
    result = validate_row124_records(source.resolve_runtime_records(mapping, player, nadine), transfers)
    result.update({"classification": "FRESH_PINNED_AUTHORED_MAP_ONLY",
                   "profile_preserved": f"{mapping.spec.profile:08X}",
                   "scale_bits_preserved": f"{mapping.spec.scale_bits:08X}",
                   "retarget_entry_sha256": source.sha256(retarget_data),
                   "row_sha256": mapping.spec.row_sha256,
                   "map_sha256": mapping.spec.map_sha256,
                   "numerical_record_builder_or_profile_execution": False})
    return result


def audit_payloads(inputs: bytes, stages: bytes, output: bytes, sources, transfers):
    for value, key in ((inputs, "input"), (stages, "stages"), (output, "output")):
        pinned(value, key)
    if len(sources) != 42:
        raise ValueError("source row count")
    parents = {name: parent_indices(transfers, name) for name in ("player", "chloe", "nadine")}
    if parents["player"] != parents["chloe"] or parents["chloe"] != parents["nadine"]:
        raise ValueError("semantic subtree parent mismatch")
    errors = {name: 0. for name in ("row20", "blend", "row157", "complete_chain")}
    composites, finals, receiver_forms = [], [], []
    equivariance = 0.
    reversed_order = 0.
    max_norm_error = 0.
    for index in range(21):
        chunk = inputs[index*188:(index+1)*188]
        first, second = chunk[:46], chunk[46:92]
        chloe_ref, nadine_ref = read_row(chunk, 92), read_row(chunk, 140)
        expected_mode = 3 if index == 1 else 1
        if first[44] != expected_mode or second[44] != expected_mode:
            raise ValueError("unexpected semantic mode")
        player, additive = sources[index*2:index*2+2]
        if additive[:3] != (1., 1., 1.) or additive[8:11] != (0., 0., 0.):
            raise ValueError("frozen additive unit-scale/zero-translation premise changed")
        native_base, native_composite = read_row(stages, index*96), read_row(stages, index*96+48)
        native_final = read_row(output, index*48)
        scalar_base = retarget(first, player, chloe_ref)
        errors["row20"] = max(errors["row20"], row_error(scalar_base, native_base))
        errors["blend"] = max(errors["blend"], row_error(blend(native_base, additive), native_composite))
        errors["row157"] = max(errors["row157"], row_error(retarget(second, native_composite, nadine_ref), native_final))
        scalar_final = retarget(second, blend(scalar_base, additive), nadine_ref)
        errors["complete_chain"] = max(errors["complete_chain"], row_error(scalar_final, native_final))
        alternative = blend(retarget(second, scalar_base, nadine_ref),
                            retarget(second, additive, nadine_ref, additive=True))
        equivariance = max(equivariance, row_error(scalar_final, alternative))
        reversed_order = max(reversed_order, qerror(qmul(additive[4:8], native_base[4:8]), native_composite[4:8]))
        max_norm_error = max(max_norm_error, *(abs(sum(v*v for v in r[4:8])-1)
                                              for r in (native_base, native_composite, native_final)))
        composites.append(native_composite)
        finals.append(native_final)
        receiver_forms.append(alternative)
    if max(errors.values()) > COMPONENT_TOLERANCE or equivariance > ALGEBRA_TOLERANCE:
        raise ValueError(f"independent scalar agreement failed: {errors}, algebra={equivariance}")
    if output[48:96] != stages[144:192]:
        raise ValueError("mode3 prop did not exactly preserve Chloe composite")
    chloe_frames = prop_relative(distal_frames(composites, parents["chloe"]))
    nadine_frames = prop_relative(distal_frames(finals, parents["nadine"]))
    alternative_frames = prop_relative(distal_frames(receiver_forms, parents["nadine"]))
    geometry_error = max(row_error(a, b) for a, b in zip(nadine_frames, alternative_frames))
    if geometry_error > COMPONENT_TOLERANCE:
        raise ValueError("composed distal frame disagreement")
    per_finger = []
    for index in range(2, 21):
        a, b = chloe_frames[index], nadine_frames[index]
        per_finger.append({"semantic": transfers[index]["semantic"],
                           "position_delta_asset_units": math.sqrt(sum((a[8+j]-b[8+j])**2 for j in range(3))),
                           "rotation_delta_degrees": angular_degrees(a[4:8], b[4:8])})
    return {
        "classification": "PURE_ALGEBRA_AND_FROZEN_NATIVE_ARTIFACT_COMPARISON",
        "native_or_game_execution": False,
        "component_tolerance": COMPONENT_TOLERANCE, "algebra_tolerance": ALGEBRA_TOLERANCE,
        "native_stage_component_max_errors": errors,
        "native_stage_max_quaternion_norm_squared_error": max_norm_error,
        "reversed_blend_order_component_error": reversed_order,
        "native_nonzero_additive_translation_validated": False,
        "row157_composition_equivariance_component_error": equivariance,
        "nadine_prop_relative_geometry_component_error": geometry_error,
        "prop_final_matches_chloe_composite_all48_bytes": True,
        "prop_is_direct_palm_child_all_three_skeletons": True,
        "prop_current_vs_proposed": "Identical operations/inputs when row124 prop mode3 (STATIC COOP_STATUS30384); not a skin-contact claim",
        "finger_geometry_chloe_vs_nadine": per_finger,
        "limitations": [
            "Row157 additive branch is a static equation, not newly executed native code",
            "Double algebra is not instruction-exact F16/rsqrt/SIMD/weighted-slerp emulation",
            "Quaternion sign is equivalent; norms are checked before geometry-only normalization",
            "Reference scale/translation are receiver-specific; finger frame differences are not automatically defects",
            "Direct R124 versus composed R20/R157 is outside this audit; use the separately versioned p99_direct_grip_audit",
            "No proof of live operands, final animation ownership, skin contact, or corrected visual grip",
        ],
    }


def load_metadata(metadata):
    metadata_bytes = Path(metadata).read_bytes()
    if hashlib.sha256(metadata_bytes).hexdigest() != METADATA_SHA:
        raise ValueError("changed independently authored parent metadata")
    return json.loads(metadata_bytes)["transfers"]


def audit(prefix=DEFAULT_PREFIX, metadata=DEFAULT_METADATA, header=DEFAULT_HEADER):
    transfers = load_metadata(metadata)
    inputs, stages, output = (Path(str(prefix)+suffix).read_bytes()
                              for suffix in (".full-input.bin", ".outputs.bin", ".full-output.bin"))
    sources = parse_sources(Path(header).read_text(encoding="utf-8"))
    return audit_payloads(inputs, stages, output, sources, transfers)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=DEFAULT_PREFIX)
    parser.add_argument("--metadata", type=Path, default=DEFAULT_METADATA)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--row124-map", action="store_true", help="also resolve original authored row124 from pinned disk assets; no native execution")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--dc-archive", type=Path)
    args = parser.parse_args()
    result = audit(args.prefix, args.metadata, args.header)
    if args.row124_map:
        result["row124_map"] = audit_row124_map(load_metadata(args.metadata), args.archive, args.dc_archive)
    elif args.archive is not None or args.dc_archive is not None:
        parser.error("archive overrides require --row124-map")
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
