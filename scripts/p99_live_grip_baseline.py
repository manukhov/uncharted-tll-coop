"""Compare PID17512's surviving right getters with authored bind/pose models.

Offline only. Fits are numerical discriminators, NOT recovered native weights,
proof of clip selection, or authority to install a pose. In particular the
product RW^-1*RP does not identify palm28 and prop30 separately. Native chain
scale is not captured, so whole-chain and hypothetical prop-only scale controls
are reported separately. The latter is not a known native scaling operation.
"""
from __future__ import annotations

import hashlib
import json
import math
import struct

import p99_direct_grip_audit as direct
import p99_grip_composition_audit as grip
import p99_native_source_audit as authored
import p99_npc_grip_candidate as npc

# Exact float32 getter snapshots; fourth P lane is opaque, NOT a scale.
# Source: reports/locomotion_hook.log, PID17512 P99-REACH CAPTURED pairs1..4.
# Long log lines were truncated only AFTER all four full getter arrays.
GETTER_PAIRS = (
    ((-759.49462890625, 4.747552871704102, 383.2938537597656, 6.615396499633789,
      -0.6294786930084229, 0.10217863321304321, 0.5025228261947632, 0.5837693810462952),
     (-759.4837036132812, 4.6767168045043945, 383.2693786621094, 8.661327362060547,
      0.14315108954906464, 0.8003418445587158, -0.5246785879135132, 0.25233471393585205)),
    ((-759.4940795898438, 4.747611999511719, 383.2938232421875, 6.615326881408691,
      -0.6289114356040955, 0.10311882197856903, 0.5031880140304565, 0.5836424231529236),
     (-759.4832153320312, 4.6767730712890625, 383.2693786621094, 8.661246299743652,
      0.14260061085224152, 0.8006007075309753, -0.5249804258346558, 0.2511952817440033)),
    ((-759.4935913085938, 4.747669696807861, 383.2937927246094, 6.615257263183594,
      -0.6283451914787292, 0.10405650734901428, 0.5038507580757141, 0.5835142135620117),
     (-759.4828491210938, 4.6768269538879395, 383.26934814453125, 8.661165237426758,
      0.14205148816108704, 0.8008529543876648, -0.5252852439880371, 0.25006216764450073)),
    ((-759.4929809570312, 4.747729301452637, 383.2937927246094, 6.615187644958496,
      -0.6277760863304138, 0.1049983948469162, 0.5045152306556702, 0.5833838582038879),
     (-759.4823608398438, 4.676882743835449, 383.26934814453125, 8.66108512878418,
      0.14150023460388184, 0.8011007905006409, -0.5255950093269348, 0.24892741441726685)),
)
GETTER_PAIRS_SHA256 = "6e6afc1e39dd817c5c284011e3deebca48cd642f0c65b860b14692c6052aadb0"


def capture_context():
    """Scope proved by the PID17512 observer, not an inferred animation state.

    p99_support_constraint_dwell_ready checks owner/weapon continuity for350ms;
    the reach observer takes its first four admitted calls. Neither helper reads
    the Hands/WeaponPartial roots or their phases/weights. WeaponOut2 plus the
    dwell is therefore NOT a verified steady-animation baseline. The saved log
    has no timestamps/frame numbers with which to reconstruct sample spacing.
    """
    return {
        "capture_pid": 17512,
        "sample_count": 4,
        "selection": "first_four_complete_admitted_terminal_calls",
        "controller_weapon_state": 2,
        "owner_weapon_continuity_dwell_ms": 350,
        "maximum_continuity_gap_ms": 100,
        "sample_times_or_frame_numbers": None,
        "sample_duration_ms": None,
        "hands_root_observed": False,
        "weapon_partial_root_observed": False,
        "transition_tail_excluded": False,
        "steady_animation_proven": False,
        "transient_animation_proven": False,
    }


def observed_rows():
    payload = b"".join(struct.pack("<8f", *r) for pair in GETTER_PAIRS for r in pair)
    if hashlib.sha256(payload).hexdigest() != GETTER_PAIRS_SHA256:
        raise ValueError("pinned getter fixture changed")
    result = []
    for wrist, prop in GETTER_PAIRS:
        inverse_q = grip.qconj(grip.qnorm(wrist[4:8]))
        result.append(grip.row(
            rotation=grip.qmul(inverse_q, grip.qnorm(prop[4:8])),
            translation=grip.rotate(inverse_q, tuple(prop[i]-wrist[i] for i in range(3)))))
    return result


def normal_mix(first, second, weight):
    """Explicit shortest-hemisphere NLERP/linear-T control, not native execution."""
    if not math.isfinite(weight) or not 0 <= weight <= 1:
        raise ValueError("weight outside [0,1]")
    for row in (first, second):
        if (len(row) != 12 or any(not math.isfinite(row[i]) for i in (*range(3), *range(4, 11)))
                or any(abs(s-1) > 1e-6 for s in row[:3])):
            raise ValueError("model requires finite unit-scale local rows")
    a, b = grip.qnorm(first[4:8]), grip.qnorm(second[4:8])
    sign = -1 if sum(x*y for x, y in zip(a, b)) < 0 else 1
    return grip.row(
        rotation=grip.qnorm(tuple((1-weight)*x+weight*sign*y for x, y in zip(a, b))),
        translation=tuple((1-weight)*first[8+i]+weight*second[8+i] for i in range(3)))


def local_pair(bind, candidate, weight):
    return tuple(normal_mix(bind[joint], candidate[joint], weight) for joint in (28, 30))


def compose_pair(palm, prop, prop_translation_scale=1):
    if not math.isfinite(prop_translation_scale) or prop_translation_scale < 0:
        raise ValueError("invalid scalar control")
    return grip.blend(palm, grip.row(rotation=prop[4:8], translation=tuple(
        prop_translation_scale*x for x in prop[8:11])))


def fit_weight(function):
    """Deterministic bounded golden-section fit, also considering both endpoints."""
    lo, hi = 0., 1.
    fraction = (math.sqrt(5)-1)/2
    c, d = hi-fraction*(hi-lo), lo+fraction*(hi-lo)
    fc, fd = function(c), function(d)
    for _ in range(100):
        if fc < fd:
            hi, d, fd = d, c, fc
            c = hi-fraction*(hi-lo)
            fc = function(c)
        else:
            lo, c, fc = c, d, fd
            d = lo+fraction*(hi-lo)
            fd = function(d)
    return min((0., 1., (lo+hi)/2), key=function)


def scalar_fit(predicted, actual):
    denominator = sum(x*x for x in predicted)
    if denominator < 1e-20:
        raise ValueError("zero scale-fit vector")
    scale = sum(x*y for x, y in zip(predicted, actual))/denominator
    return {"fitted_scale": scale,
            "residual_asset_units": math.dist(tuple(scale*x for x in predicted), actual)}


def compare(bind, candidate, observed):
    def objective(weight):
        row = compose_pair(*local_pair(bind, candidate, weight))
        return grip.angular_degrees(row[4:8], observed[4:8])**2
    weight = fit_weight(objective)
    palm, prop = local_pair(bind, candidate, weight)
    composed = compose_pair(palm, prop)
    rotated_prop = tuple(composed[8+i]-palm[8+i] for i in range(3))
    offset_goal = tuple(observed[8+i]-palm[8+i] for i in range(3))
    return {
        "numerically_fitted_weight_not_native_observation": weight,
        "rotation_residual_degrees": math.sqrt(objective(weight)),
        "translation_residual_no_scale": math.dist(composed[8:11], observed[8:11]),
        "whole_chain_scale_control": scalar_fit(composed[8:11], observed[8:11]),
        "hypothetical_prop_only_scale_control": scalar_fit(rotated_prop, offset_goal),
        "hypothetical_prop_only_fixed_095_residual": math.dist(
            compose_pair(palm, prop, .95)[8:11], observed[8:11]),
    }


def audit():
    data = npc.DEFAULT_NADINE_ASSET.read_bytes()
    skeleton = npc.source.load_skeleton(data, npc.source.NADINE_SKELETON)
    inputs = authored.skeleton_inputs(data, skeleton)
    bind = {j: npc._target_reference(inputs, skeleton, j) for j in (28, 30)}
    direct_report = direct.audit(npc.DEFAULT_NADINE_ASSET, npc.DEFAULT_ACTOR_ARCHIVE,
                                 npc.DEFAULT_DC_ARCHIVE, include_local_rows=True)
    npc_report = npc.audit()
    candidates = {
        "conditional_direct_R124_B8": {r["target_global"]: r["direct_row"]
            for r in direct_report["local_joint_rows"]["rows"]},
        "conditional_Chloe_R157_R20_B8": {r["target_global"]: r["proposed_row"]
            for r in direct_report["local_joint_rows"]["rows"]},
        "native_NPC_4CAA7420": {r["target_global"]: r["local_row"]
            for r in npc_report["candidate_rows"]},
    }
    bind_frame = compose_pair(*local_pair(bind, bind, 0))
    samples = []
    for i, observed in enumerate(observed_rows(), 1):
        inverse_palm_q = grip.qconj(grip.qnorm(bind[28][4:8]))
        inferred_prop = grip.row(
            rotation=grip.qmul(inverse_palm_q, observed[4:8]),
            translation=grip.rotate(inverse_palm_q,
                tuple(observed[8+j]-bind[28][8+j] for j in range(3))))
        samples.append({
            "sample": i, "observed_wrist_to_prop": observed,
            "bind_rotation_error_degrees": grip.angular_degrees(bind_frame[4:8], observed[4:8]),
            "bind_translation_error": math.dist(bind_frame[8:11], observed[8:11]),
            "bind_whole_chain_scale_control": scalar_fit(bind_frame[8:11], observed[8:11]),
            "assuming_bind_palm_and_unit_chain_scale_not_observed": {
                "inferred_local_prop": inferred_prop,
                "prop_translation_scale_fit": scalar_fit(bind[30][8:11], inferred_prop[8:11]),
            },
            "fits": {name: compare(bind, candidate, observed)
                     for name, candidate in candidates.items()},
        })
    return {
        "schema": "p99-live-grip-baseline-v1",
        "classification": "NUMERICAL_DISCRIMINATOR_NOT_NATIVE_WEIGHT_OR_SOURCE_PROOF",
        "live_clip_selection_proven": False, "live_weight_proven": False,
        "live_chain_scale_observed": False, "pose_writer_admitted": False,
        "capture_pid": 17512, "capture_getter_pairs_f32_sha256": GETTER_PAIRS_SHA256,
        "capture_context": capture_context(),
        "skeleton_sha256": npc.source.sha256(data), "bind_local_rows": bind,
        "samples": samples,
        "limits": [
            "Observed product cannot identify palm28 versus prop30 separately.",
            "Near-bind rotation is not exact bind; the four samples evolve.",
            "WeaponOut2 and350ms owner continuity do not prove a steady animation root.",
            "No sample timestamps, frame numbers, layer roots or native blend weights were captured.",
            "A transition tail is a competing explanation, not an observed fact.",
            "NLERP fits are analytic controls, not reconstructed native blending commands.",
            "Native chain scale multiplies both local translations; its live value is missing.",
            "Prop-only 0.95 is a contrasting numeric fit, not a proven native operation.",
            "World float32 coordinate quantization limits translation precision.",
            "Prior 21-row audits model a full-weight subtree, not native defaults or final owner.",
            "No finger pose, skin clearance, visual repair, or mutation authority follows.",
        ],
    }


if __name__ == "__main__":
    print(json.dumps(audit(), indent=2, allow_nan=False))
