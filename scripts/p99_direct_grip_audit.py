"""Compare profile-present R124 with the archived conditional Chloe grip chain.

Pure scalar reconstruction only. Uses the dependency-proven profile matrix slice
and approximate packed-locator algebra; never produces native records or writes
game state. R157 remains the pinned conditional AGZ basis. This is a comparison
of the D350/B8 subtree, not proof of its live ownership or rendered skin contact.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import p99_grip_composition_audit as grip
import p99_locator_basis_audit as locator
import p99_profile_matrix_audit as matrix
import p99_preparation_fixture as fixture

source = fixture.source
ROOT = Path(__file__).resolve().parents[1]


def column_major_to_mat34(values):
    values = locator.finite_tuple(values, 16, "column-major affine transform")
    if values[3:12:4] != (0., 0., 0.) or values[15] != 1.:
        raise ValueError("nonaffine exported profile transform")
    return tuple(values[column * 4 + row] for row in range(3) for column in range(4))


def direct_base(player, target_reference, mode, joint_locator, parent_locator):
    if mode == 3:
        return tuple(player)
    if mode != 1:
        raise ValueError("comparison supports only the selected mode1/mode3 rows")
    q = grip.qmul(grip.qmul(grip.qconj(parent_locator.normalized), player[4:8]),
                  joint_locator.normalized)
    # The consumer uses raw target reference S/T, not sampled profile S/T.
    # The latter is an input to the locator builder, a distinct role.
    return grip.row(target_reference[:3], q, target_reference[8:11])


def compare_row(player, additive, target_reference, mode, joint_locator, parent_locator,
                row157_record, archived_chloe_base, archived_proposed):
    if len(row157_record) != 46 or row157_record[44] != mode:
        raise ValueError("direct/composed semantic modes disagree")
    direct = direct_base(player, target_reference, mode, joint_locator, parent_locator)
    composed = grip.retarget(row157_record, archived_chloe_base, target_reference)
    direct_composite = grip.blend(direct, grip.retarget(row157_record, additive,
                                                      target_reference, additive=True))
    return direct_composite, {
        "base_rotation_delta_degrees": grip.angular_degrees(direct[4:8], composed[4:8]),
        "composite_rotation_delta_degrees": grip.angular_degrees(direct_composite[4:8], archived_proposed[4:8]),
        "base_ST_component_delta": max(abs(direct[i] - composed[i]) for i in (0, 1, 2, 8, 9, 10)),
        "composite_ST_component_delta": max(abs(direct_composite[i] - archived_proposed[i])
                                              for i in (0, 1, 2, 8, 9, 10)),
    }


def audit(asset, archive, dc_archive, prefix=grip.DEFAULT_PREFIX, metadata=grip.DEFAULT_METADATA):
    data = Path(asset).read_bytes()
    basis = matrix.audit(data)
    transfers = grip.load_metadata(metadata)
    parser = source.native.load_audit_module()
    actor = parser.Psarc(Path(archive), None)
    player_bytes = actor.extract(source.PLAYER_SKELETON.entry_name)
    player, player_inverse, player_parents = locator.load_basis(player_bytes, source.PLAYER_SKELETON)
    nadine, bind_inverse, nadine_parents = locator.load_basis(data, source.NADINE_SKELETON)
    retarget_data = parser.Psarc(Path(dc_archive), None).extract(source.RETARGET_ENTRY_NAME)
    row124 = fixture.row124_mapping(retarget_data)
    records = source.resolve_runtime_records(row124, player, nadine)
    map_proof = grip.validate_row124_records(records, transfers)
    inverse = {joint: locator.affine_inverse(column_major_to_mat34(values))
               for joint, values in basis["forward_matrices"].items()}
    payloads = {}
    for key, suffix in (("input", ".full-input.bin"), ("stages", ".outputs.bin"),
                        ("output", ".full-output.bin")):
        payloads[key] = Path(str(prefix) + suffix).read_bytes()
        grip.pinned(payloads[key], key)
    sources = grip.parse_sources(grip.DEFAULT_HEADER.read_text(encoding="utf-8"))
    direct_rows, proposed_rows, comparisons = [], [], []
    control_rows, control_comparisons = [], []
    for index, transfer in enumerate(transfers):
        source_joint, target_joint = transfer["player_global"], transfer["nadine_global"]
        source_parent, target_parent = player_parents[source_joint], nadine_parents[target_joint]
        if source_parent < 0 or target_parent < 0:
            raise ValueError("selected distal row unexpectedly needs root handling")
        joint = locator.build_locator_quaternions(player_inverse[source_joint], inverse[target_joint])
        parent = locator.build_locator_quaternions(player_inverse[source_parent], inverse[target_parent])
        chunk = payloads["input"][index * 188:(index + 1) * 188]
        reference = grip.read_row(chunk, 140)
        proposed = grip.read_row(payloads["output"], index * 48)
        direct, comparison = compare_row(sources[2 * index], sources[2 * index + 1], reference,
            3 if index == 1 else 1, joint, parent, chunk[46:92],
            grip.read_row(payloads["stages"], index * 96), proposed)
        direct_rows.append(direct)
        proposed_rows.append(proposed)
        comparisons.append({"semantic": transfer["semantic"], "target_global": target_joint,
                            **comparison})
        # Deliberately invalid as an authored R124 operand: change only the
        # target basis to bind pose to isolate the sampled profile's effect.
        # Never interpret this algebra control as live profile absence.
        control_joint = locator.build_locator_quaternions(
            player_inverse[source_joint], bind_inverse[target_joint])
        control_parent = locator.build_locator_quaternions(
            player_inverse[source_parent], bind_inverse[target_parent])
        control, control_comparison = compare_row(
            sources[2 * index], sources[2 * index + 1], reference,
            3 if index == 1 else 1, control_joint, control_parent, chunk[46:92],
            grip.read_row(payloads["stages"], index * 96), proposed)
        control_rows.append(control)
        control_comparisons.append({"semantic": transfer["semantic"],
                                    "target_global": target_joint, **control_comparison})
    parents = grip.parent_indices(transfers, "nadine")
    direct_frames = grip.prop_relative(grip.distal_frames(direct_rows, parents))
    proposed_frames = grip.prop_relative(grip.distal_frames(proposed_rows, parents))
    control_frames = grip.prop_relative(grip.distal_frames(control_rows, parents))
    for index, comparison in enumerate(comparisons):
        comparison["prop_relative_origin_delta_asset_units"] = math.sqrt(sum(
            (direct_frames[index][8 + j] - proposed_frames[index][8 + j])**2 for j in range(3)))
        comparison["prop_relative_rotation_delta_degrees"] = grip.angular_degrees(
            direct_frames[index][4:8], proposed_frames[index][4:8])
        control_comparisons[index]["prop_relative_origin_delta_asset_units"] = math.sqrt(sum(
            (control_frames[index][8 + j] - proposed_frames[index][8 + j])**2 for j in range(3)))
    return {
        "classification": "APPROXIMATE_PROFILE_PRESENT_R124_VS_CONDITIONAL_CHLOE_CHAIN",
        "retail_or_game_execution": False, "runtime_record_output": False,
        "live_grip_or_skin_contact_proven": False, "row124_profile": "E51542CA present",
        "row157_basis": "archived AGZ conditional no-profile, not live proof",
        "row124_map": map_proof,
        "profile_slice_required_globals": basis["required_globals"],
        "comparisons": comparisons,
        "max_base_rotation_delta_degrees": max(row["base_rotation_delta_degrees"] for row in comparisons),
        "max_composite_rotation_delta_degrees": max(row["composite_rotation_delta_degrees"] for row in comparisons),
        "max_prop_relative_origin_delta_asset_units": max(row["prop_relative_origin_delta_asset_units"] for row in comparisons),
        "bind_basis_control": {
            "classification": "ANALYTIC_BIND_BASIS_CONTROL_NOT_AUTHORED_R124",
            "valid_profile_present_operand": False,
            "live_profile_absence_proven": False,
            "changed_input": "Only target locator inverse-global basis; all maps, sources, S/T and additive operands unchanged",
            "comparisons": control_comparisons,
            "max_base_rotation_delta_degrees": max(row["base_rotation_delta_degrees"] for row in control_comparisons),
            "max_composite_rotation_delta_degrees": max(row["composite_rotation_delta_degrees"] for row in control_comparisons),
            "max_prop_relative_origin_delta_asset_units": max(row["prop_relative_origin_delta_asset_units"] for row in control_comparisons),
            "interpretation": "Near cancellation isolates profile-basis contribution within this model; separate half quantization need not cancel exactly"},
        "limits": ["Profile stream and separated-scale propagation are scalar models, not newly executed native stages",
                   "Locator equations match84 archived native locators within1e-6 but are not bit exact",
                   "Native float32/reciprocal/sqrt/half and MXCSR effects remain distinct from scalar arithmetic",
                   "Only D350/B8 subtree compared; live final owner, left support arm and skinned contact not established",
                   "No runtime writes, record fixture generation or game-test authorization"]}


def main():
    base = source.native.DEFAULT_GAME / "Uncharted4_data/build/pc/thelostlegacy"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset", type=Path, default=ROOT / "reports/actor_packages/nadine-dlc-base.pak")
    parser.add_argument("--archive", type=Path, default=base / "actor77.psarc")
    parser.add_argument("--dc-archive", type=Path, default=base / "dc1.psarc")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = audit(args.asset, args.archive, args.dc_archive)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
