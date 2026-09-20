"""Pure, pinned row124 profile matrix slice; no retail code or process access.

The required 71 matrices depend only on untouched profile parameter prefixes,
op40, the identity seed, and op53's two palm exports. Other authored operators
are audited for writes but not executed. This is a scalar numerical model, NOT
an instruction-exact AnimateObject result or proof of live profile residency.

142189FB0 stores three affine rows plus a SEPARATE local scale float4. Flag8
suppresses parent scale in the rotation basis, not in child translation.
14218AC90 (cmd25) applies the remaining local scale and transposes to the
column-vector SMath::Transform layout before 14023ABB0 inverts it.
"""
from __future__ import annotations

from collections import Counter
import math
import struct

import generate_p99_chloe_source_rows as source
import p99_group_program_audit as program
import p99_native_source_audit as authored
import p99_nadine_profile_audit as profile_decoder

REQUIRED_GLOBALS = (0, *range(2, 31), *range(32, 35), *range(104, 142))
PROGRAMS = (0x53E0, 0x8D90)
HISTOGRAMS = (
    {40: 10, 41: 1, 44: 3, 47: 6, 49: 4, 50: 3, 51: 3, 52: 1,
     53: 1, 54: 1, 128: 8, 129: 4, 130: 2, 131: 2, 132: 2},
    {40: 1, 53: 1},
)
IDENTITY = (((1., 0., 0.), (0., 1., 0.), (0., 0., 1.)),
            (0., 0., 0.), (1., 1., 1.))


def rotation(q):
    """Hamilton xyzw polynomial, as op40; no implicit normalization."""
    x, y, z, w = q
    return ((1 - 2*y*y - 2*z*z, 2*x*y - 2*w*z, 2*x*z + 2*w*y),
            (2*x*y + 2*w*z, 1 - 2*x*x - 2*z*z, 2*y*z - 2*w*x),
            (2*x*z - 2*w*y, 2*y*z + 2*w*x, 1 - 2*x*x - 2*y*y))


def propagate(parent, row, disable_inherit):
    """Return native workspace meaning (3x3 basis, translation, local scale)."""
    if len(row) != 12 or not all(math.isfinite(row[i]) for i in (*range(3), *range(4, 11))):
        raise ValueError("invalid meaningful JointParams components")
    a, t, scale = parent
    r = rotation(row[4:8])
    inherited = (1., 1., 1.) if disable_inherit else scale
    basis = tuple(tuple(sum(a[i][k] * inherited[k] * r[k][j] for k in range(3))
                        for j in range(3)) for i in range(3))
    translation = tuple(t[i] + sum(a[i][k] * scale[k] * row[8+k] for k in range(3))
                        for i in range(3))
    return basis, translation, tuple(row[:3])


def export_affine(workspace):
    """Cmd25 with builder's identity root: row-major affine 3x4."""
    basis, translation, scale = workspace
    return tuple(value for i in range(3)
                 for value in (*[basis[i][j] * scale[j] for j in range(3)], translation[i]))


def export_transform(workspace):
    """Four float4 columns in the sampled SMath::Transform output buffer."""
    rows = export_affine(workspace)
    return tuple(rows[4*i+j] if i < 3 else float(j == 3)
                 for j in range(4) for i in range(4))


def parent_encoding(group, joint, parent):
    if group == 0:
        return 64 * (parent + 1) | (1 if joint in (2, 14) else 9)
    if parent in (27, 28):
        return (0xF90 if parent == 27 else 0xFD0) | 11
    if not 104 <= parent < 142:
        raise ValueError("finger parent outside closed slice")
    return 64 * (parent - 104 + 1) | 9


def _overlaps(encoded, width, bank, first, end):
    offset = encoded & (0xFFFFFFF0 if width >= 16 else 0xFFFFFFFC)
    return encoded & 3 == bank and offset < end and offset + width > first


def prove_slice(data, skeleton):
    """Check every authored write and exact parent encoding before any math.

    Exposed separately for mutation tests; audit() first pins the entire asset.
    This function does not independently recheck that hash.
    """
    raw = skeleton.spec.raw_offset
    parents = authored.skeleton_inputs(data, skeleton)["parents"]
    required = set(REQUIRED_GLOBALS)
    if len(required) != 71 or any(parents[j] not in required | {-1} for j in required):
        raise ValueError("row124 target parent closure changed")
    available = {-1}
    counts = Counter()
    events = []
    all_ops = []
    table = raw + program.u32(data, raw + 0x34)
    groups = raw + program.u32(data, raw + 0x2C)
    for group_index, relative in enumerate(PROGRAMS):
        group = skeleton.groups[group_index]
        descriptor = groups + 32 * group_index
        if (data[descriptor + 15] != 1
                or program.u32(data, table + 8 * program.u16(data, descriptor + 6) + 4) != relative):
            raise ValueError("group program identity changed")
        ops = program.operators(data, raw, relative)
        if dict(Counter(op.opcode for op in ops)) != HISTOGRAMS[group_index]:
            raise ValueError("complete operator inventory changed")
        all_ops.append(ops)
        local_required = {j - group.first for j in required
                          if group.first <= j < group.first + group.workspace_count}
        for op in ops:
            for encoded, width in program.writes(data, raw + relative, op):
                if _overlaps(encoded, width, 0, 0, 48 * group.parameter_count):
                    raise ValueError("operator writes sampled parameter prefix")
                if op.opcode != 40 and any(
                        _overlaps(encoded, width, 1, 64*(j+1), 64*(j+2))
                        for j in local_required):
                    raise ValueError("non-propagation operator writes required matrix")
            if op.opcode != 40:
                continue
            for index, (local, encoded_parent) in enumerate(
                    program.propagation_records(data, raw + relative, op)):
                if local not in local_required:
                    continue
                joint = group.first + local
                parent = parents[joint]
                if encoded_parent != parent_encoding(group_index, joint, parent):
                    raise ValueError("required matrix parent/scale flag differs from authored closure")
                if parent not in available:
                    raise ValueError("required parent not produced before read")
                counts[joint] += 1
                if counts[joint] == 1:
                    events.append({"global": joint, "parent": parent, "group": group_index,
                                   "operator": op.ordinal, "record": index,
                                   "encoded_parent": encoded_parent,
                                   "disable_inherit": bool(encoded_parent & 8)})
                available.add(joint)
    expected_counts = {joint: {0: 4, 34: 2, 141: 3}.get(joint, 1) for joint in required}
    if dict(counts) != expected_counts:
        raise ValueError("required matrix producer coverage or padding changed")

    export = all_ops[0][49]
    if (export.opcode, export.payload) != (53, (248, 3, 0x35A0)):
        raise ValueError("palm scratch export changed")
    desc = raw + PROGRAMS[0] + export.payload[2]
    pairs = tuple(struct.unpack_from("<HH", data, desc + 4*i) for i in range(121, 129))
    expected_pairs = tuple(((0xF90 + 16*i) | 3, (0x700 + 16*i) | 1) for i in range(8))
    if pairs != expected_pairs:
        raise ValueError("palm scratch export does not copy complete parent matrices")
    # No later write can modify the exported parent before/during g1 reads.
    for gi, ops in enumerate(all_ops):
        for op in ops:
            if gi == 0 and op.ordinal <= 49:
                continue
            if any(_overlaps(e, w, 3, 0xF90, 0x1010)
                   for e, w in program.writes(data, raw + PROGRAMS[gi], op)):
                raise ValueError("palm scratch overwritten before finger consumption")
    return {"events": events, "operator_count": sum(map(len, all_ops)),
            "required_matrix_count": len(events), "prefix_rows_untouched": 104,
            "padding_repeats": {0: 3, 34: 1, 141: 2},
            "palm_export_pairs": pairs, "non_op40_required_matrix_writes": 0,
            "root_seed": "cmd15 141ABA921 identity affine rows + scale(1,1,1,1)",
            "cmd25_root": "builder1417CE003..CE032 identity SMath::Transform"}


def audit(data: bytes, profile=None):
    skeleton = source.load_skeleton(data, source.NADINE_SKELETON)
    proof = prove_slice(data, skeleton)
    profile = profile_decoder.audit(data) if profile is None else profile
    if (profile.get("quaternion_decoder_model") != profile_decoder.DECODER_MODEL
            or profile["asset_sha256"] != source.sha256(data)
            or profile["profile"] != "E51542CA" or profile["decoded_parameter_rows"] != 426):
        raise ValueError("profile model identity mismatch")
    packed = bytes.fromhex(profile["scalar_model_rows_f32_hex"])
    if len(packed) != 426*48 or source.sha256(packed) != profile["scalar_model_rows_sha256"]:
        raise ValueError("profile model rows changed")
    rows = {}
    for joint in REQUIRED_GLOBALS:
        group, local = skeleton.location(joint)
        processing = skeleton.processing[skeleton.groups[group].processing_index]
        rows[joint] = struct.unpack_from("<12f", packed, 48*(processing.first_parameter + local))
    workspace = {-1: IDENTITY}
    for event in proof["events"]:
        joint, parent = event["global"], event["parent"]
        workspace[joint] = propagate(workspace[parent], rows[joint], event["disable_inherit"])
    forward_rows = {j: export_affine(workspace[j]) for j in REQUIRED_GLOBALS}
    if not all(math.isfinite(v) for row in forward_rows.values() for v in row):
        raise ValueError("nonfinite scalar profile matrix")
    return {"classification": "STATIC_SCALAR_REQUIRED_PROFILE_MATRIX_SLICE",
            "quaternion_decoder_model": profile_decoder.DECODER_MODEL,
            "game_or_native_execution": False, "runtime_builder_operands_ready": False,
            "asset_sha256": source.sha256(data), "profile": "E51542CA", "proof": proof,
            "required_globals": list(REQUIRED_GLOBALS), "local_rows": rows,
            "forward_affine_rows": forward_rows,
            "forward_matrices": {j: export_transform(workspace[j]) for j in REQUIRED_GLOBALS},
            "limits": ["Scalar propagation rounding and decoder rsqrt/Newton surrogate are not native bit-exact",
                       "Half subnormals retain the decoder's explicit MXCSR/DAZ limitation",
                       "Only required row124 matrices are modeled, not all580 AnimateObject outputs",
                       "No live registration, residency, runtime operand or visual grip claim"]}
