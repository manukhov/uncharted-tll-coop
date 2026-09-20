"""Approximate scalar reconstruction of packed retarget locator quaternions.

STATIC 1417CF634..CFA6C builds the parent locator (+14h quaternion), and
1417CFA70..CFE78 builds the joint locator (+06h quaternion). Both convert an
inverse-global Mat34 into a forward basis, then form conjugate(source)*target.
The builder packs XYZW with vcvtps2ph immediate0; it does NOT normalize before
packing. DecodePackedReferenceLocator141AE1F10 normalizes the decoded halves.

This reproduces the algebra and matrix-to-quaternion branch/tie convention,
not the instruction-by-instruction float32/rcpps/sqrtf/SIMD calculation. It
does not emit runtime records or call retail code. Frozen row20 and conditional
row157 records are independent validation data, not row124 sampled references.
Never substitute a bind-pose target for the profile-present row124 input.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import struct

import generate_p99_chloe_source_rows as source
import p99_grip_composition_audit as frozen

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSETS = ROOT / "reports/actor_packages"
DEFAULT_ARCHIVE = (source.native.DEFAULT_GAME /
                   "Uncharted4_data/build/pc/thelostlegacy/actor77.psarc")
LOCATOR_TOLERANCE = 1e-6


def finite_tuple(values, size: int, label: str) -> tuple[float, ...]:
    result = tuple(float(value) for value in values)
    if len(result) != size or not all(math.isfinite(value) for value in result):
        raise ValueError(f"invalid {label}")
    return result


def affine_inverse(values) -> tuple[float, ...]:
    """Invert all of a row-major affine Mat34, including nonuniform scale.

    Despite its historical InvertRigid name, 14023ABB0 uses cross products,
    determinant and refined reciprocal. A transpose-only inverse is incorrect.
    Native loads three rows then transposes to four SIMD columns before call.
    """
    values = finite_tuple(values, 12, "Mat34")
    matrix = tuple(values[4 * row:4 * row + 3] for row in range(3))
    a, b, c = matrix
    determinant = (a[0] * (b[1] * c[2] - b[2] * c[1])
                   - a[1] * (b[0] * c[2] - b[2] * c[0])
                   + a[2] * (b[0] * c[1] - b[1] * c[0]))
    if not math.isfinite(determinant) or determinant == 0:
        raise ValueError("singular or nonfinite affine determinant")
    inverse = []
    for row in range(3):
        result = []
        for column in range(3):
            rows = [i for i in range(3) if i != column]
            columns = [i for i in range(3) if i != row]
            minor = (matrix[rows[0]][columns[0]] * matrix[rows[1]][columns[1]]
                     - matrix[rows[0]][columns[1]] * matrix[rows[1]][columns[0]])
            result.append((-1) ** (row + column) * minor / determinant)
        inverse.extend((*result, -sum(result[i] * values[4 * i + 3] for i in range(3))))
    return finite_tuple(inverse, 12, "inverse Mat34")


def quaternion_from_basis(values) -> tuple[tuple[float, ...], str]:
    """142177E50 and largest-X/Y/Z 1421787A0/88B0/89B0, row-major view.

    Positive trace wins; otherwise strict greater-than comparisons choose the
    first largest diagonal (X before Y before Z on ties). No normalization.
    """
    values = finite_tuple(values, 12, "forward Mat34")
    matrix = tuple(values[4 * row:4 * row + 3] for row in range(3))
    trace = sum(matrix[i][i] for i in range(3))
    if trace > 0:
        length = math.sqrt(trace + 1)
        factor = .5 / length
        q = ((matrix[2][1] - matrix[1][2]) * factor,
             (matrix[0][2] - matrix[2][0]) * factor,
             (matrix[1][0] - matrix[0][1]) * factor, .5 * length)
        branch = "positive-trace"
    else:
        axis = 0
        if matrix[1][1] > matrix[axis][axis]:
            axis = 1
        if matrix[2][2] > matrix[axis][axis]:
            axis = 2
        second, third = (axis + 1) % 3, (axis + 2) % 3
        radicand = 1 + matrix[axis][axis] - matrix[second][second] - matrix[third][third]
        if radicand < 0 or not math.isfinite(radicand):
            raise ValueError("invalid quaternion radicand")
        length = math.sqrt(radicand)
        factor = .5 / length if length != 0 else 0.
        q = [0.] * 4
        q[axis] = .5 * length
        q[second] = (matrix[second][axis] + matrix[axis][second]) * factor
        q[third] = (matrix[third][axis] + matrix[axis][third]) * factor
        q[3] = (matrix[third][second] - matrix[second][third]) * factor
        branch = "largest-" + "xyz"[axis]
    return finite_tuple(q, 4, "basis quaternion"), branch


def multiply(left, right) -> tuple[float, ...]:
    x, y, z, w = finite_tuple(left, 4, "left quaternion")
    X, Y, Z, W = finite_tuple(right, 4, "right quaternion")
    return finite_tuple((w * X + x * W + y * Z - z * Y,
                         w * Y - x * Z + y * W + z * X,
                         w * Z + x * Y - y * X + z * W,
                         w * W - x * X - y * Y - z * Z), 4, "product quaternion")


def conjugate(q):
    x, y, z, w = finite_tuple(q, 4, "quaternion")
    return -x, -y, -z, w


def normalized(q):
    q = finite_tuple(q, 4, "quaternion")
    length = math.sqrt(sum(value * value for value in q))
    if not math.isfinite(length) or length == 0:
        raise ValueError("zero or nonfinite quaternion norm")
    return tuple(value / length for value in q)


def pack_quaternion(q) -> bytes:
    # Round the final scalar components to float32 before nearest-even half,
    # matching the conversion ABI but not preceding native SIMD operations.
    q = finite_tuple(q, 4, "quaternion")
    try:
        singles = struct.unpack("<4f", struct.pack("<4f", *q))
        result = struct.pack("<4e", *singles)
    except (OverflowError, struct.error) as error:
        raise ValueError("quaternion cannot be represented as finite halves") from error
    decoded = struct.unpack("<4e", result)
    normalized(decoded)  # Finite and nonzero, including signed-zero variants.
    return result


@dataclass(frozen=True)
class LocatorQuaternion:
    unquantized: tuple[float, ...]
    packed: bytes
    decoded: tuple[float, ...]
    normalized: tuple[float, ...]
    source_branch: str
    target_branch: str


def build_locator_quaternions(src_inverse, dst_inverse) -> LocatorQuaternion:
    """Pure one-locator reconstruction from two inverse-global Mat34 values."""
    source_q, source_branch = quaternion_from_basis(affine_inverse(src_inverse))
    target_q, target_branch = quaternion_from_basis(affine_inverse(dst_inverse))
    q = multiply(conjugate(source_q), target_q)
    packed = pack_quaternion(q)
    decoded = struct.unpack("<4e", packed)
    return LocatorQuaternion(q, packed, decoded, normalized(decoded), source_branch, target_branch)


IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.)


def parent_locator(src_matrices, dst_matrices, src_parent: int, dst_parent: int):
    """Closed pure-model root handling; never Python-index a matrix at -1.

    Only matched roots or two in-range parents are supported. This is stricter
    than the native builder: CF634/CF6FF load parent matrices before its root
    branch, so this helper is NOT proof that a truncated native array is safe.
    """
    if type(src_parent) is not int or type(dst_parent) is not int:
        raise ValueError("parent indices must be integers")
    if src_parent == dst_parent == -1:
        return build_locator_quaternions(IDENTITY, IDENTITY)
    if not (0 <= src_parent < len(src_matrices) and 0 <= dst_parent < len(dst_matrices)):
        raise ValueError("unsupported root pairing or out-of-range parent")
    return build_locator_quaternions(src_matrices[src_parent], dst_matrices[dst_parent])


def sign_error(left, right):
    left, right = finite_tuple(left, 4, "quaternion"), finite_tuple(right, 4, "quaternion")
    return min(max(abs(a - b) for a, b in zip(left, right)),
               max(abs(a + b) for a, b in zip(left, right)))


def load_basis(data: bytes, spec):
    skeleton = source.load_skeleton(data, spec)  # Full asset SHA and structure.
    raw = spec.raw_offset
    matrix_offset = raw + source.native.u32(data, raw + 0x28)
    parent_header = raw + source.native.u32(data, raw + 0x3C)
    parent_offset = parent_header + source.native.u32(data, parent_header + 0x14)
    matrices = tuple(struct.unpack_from("<12f", data, matrix_offset + 48 * i)
                     for i in range(spec.joint_count))
    parents = tuple(struct.unpack_from("<i", data, parent_offset + 16 * i + 4)[0]
                    for i in range(spec.joint_count))
    if any(parent < -1 or parent >= len(matrices) for parent in parents):
        raise ValueError("authored parent outside matrix array")
    return skeleton, matrices, parents


def audit(assets=DEFAULT_ASSETS, prefix=frozen.DEFAULT_PREFIX,
          metadata=frozen.DEFAULT_METADATA, archive=DEFAULT_ARCHIVE):
    transfers = frozen.load_metadata(metadata)
    inputs = Path(str(prefix) + ".full-input.bin").read_bytes()
    frozen.pinned(inputs, "input")
    bases = []
    actor = None
    for spec in (source.PLAYER_SKELETON, source.CHLOE_SKELETON, source.NADINE_SKELETON):
        path = Path(assets) / spec.entry_name
        if path.exists():
            data = path.read_bytes()
        else:
            if actor is None:
                actor = source.native.load_audit_module().Psarc(Path(archive), None)
            data = actor.extract(spec.entry_name)  # Existing asset decoder, not retail execution.
        bases.append(load_basis(data, spec))
    reports = []
    for index, (source_name, target_name) in enumerate((("player", "chloe"), ("chloe", "nadine"))):
        _, source_matrices, source_parents = bases[index]
        _, target_matrices, target_parents = bases[index + 1]
        errors, normalized_errors, wrong_errors = [], [], []
        byte_mismatches = 0
        for row_index, transfer in enumerate(transfers):
            record = inputs[row_index * 188 + 46 * index:row_index * 188 + 46 * (index + 1)]
            source_joint = transfer[source_name + "_global"]
            target_joint = transfer[target_name + "_global"]
            if (struct.unpack_from("<H", record, 0x24)[0] != source_joint or
                    struct.unpack_from("<H", record, 0x28)[0] != target_joint or
                    record[44] != (3 if row_index == 1 else 1)):
                raise ValueError("frozen record semantic/global/mode disagreement")
            for parent, offset in ((False, 6), (True, 20)):
                src = source_parents[source_joint] if parent else source_joint
                dst = target_parents[target_joint] if parent else target_joint
                result = (parent_locator(source_matrices, target_matrices, src, dst) if parent
                          else build_locator_quaternions(source_matrices[src], target_matrices[dst]))
                actual = struct.unpack_from("<4e", record, offset)
                errors.append(sign_error(result.decoded, actual))
                normalized_errors.append(sign_error(result.normalized, normalized(actual)))
                byte_mismatches += result.packed != record[offset:offset + 8]
                if src >= 0 and dst >= 0:
                    sq, _ = quaternion_from_basis(affine_inverse(source_matrices[src]))
                    tq, _ = quaternion_from_basis(affine_inverse(target_matrices[dst]))
                    wrong_errors.append(sign_error(multiply(tq, conjugate(sq)), actual))
        if max(errors) > LOCATOR_TOLERANCE or max(normalized_errors) > LOCATOR_TOLERANCE:
            raise ValueError("scalar locators disagree with frozen native records")
        if not wrong_errors or max(wrong_errors) < .1:
            raise ValueError("fixture does not distinguish multiplication order")
        reports.append({"row": (20, 157)[index], "locator_count": len(errors),
                        "max_quantized_component_error": max(errors),
                        "max_normalized_component_error": max(normalized_errors),
                        "packed_byte_mismatches": byte_mismatches,
                        "wrong_order_component_error": max(wrong_errors)})
    return {"classification": "APPROXIMATE_SCALAR_VS_FROZEN_NATIVE_LOCATORS",
            "retail_or_game_execution": False, "runtime_record_output": False,
            "row124_profile_evaluated": False, "tolerance": LOCATOR_TOLERANCE,
            "mappings": reports,
            "limitations": ["Not an instruction-exact float32/rcpps/sqrtf/SIMD oracle",
                            "Half conversion is nearest-even after final float32 rounding; earlier arithmetic is double",
                            "Row157 validation remains the archived conditional no-profile fixture",
                            "Profile-present row124 requires complete sampled post-cmd16 target inverse globals",
                            "Matched-root pure handling is not native negative-index buffer admission",
                            "No live operands, visual grip, skin/contact or runtime safety claim"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=DEFAULT_ASSETS)
    parser.add_argument("--prefix", type=Path, default=frozen.DEFAULT_PREFIX)
    parser.add_argument("--metadata", type=Path, default=frozen.DEFAULT_METADATA)
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    args = parser.parse_args()
    print(json.dumps(audit(args.assets, args.prefix, args.metadata, args.archive),
                     indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
