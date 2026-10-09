"""Conditional normal-mode WeaponPartial fold, not reconstructed live commands.

Native anchors: layer14189EA60, snapshot141AB6C40, zero-pose emitter141AC7F10,
masked blend142188A30. This models ONE instance in EACH of TWO tracks, an older
pose P and a newer empty root, with the snapshot lane-zero tail enabled.
It deliberately does not model additive modes, callback side effects, source
selection, retargeting, variable clip phases or the native floating-point ABI.

The older snapshot is Mix(B,P,L); the empty snapshot inherits B by one-sided
validity. Their outer fold is Mix(Mix(B,P,L),B,w). Only jointly-valid LINEAR
translation has residual coefficient L*(1-w). Nested normalized quaternion
blends must be evaluated in order, not collapsed to that scalar. The formula
also does not apply directly to a composed wrist-to-prop product, whose
translation includes a rotated child offset.
"""
from __future__ import annotations

import math

from p99_live_grip_baseline import normal_mix


def _weight(value):
    if not math.isfinite(value) or not 0 <= value <= 1:
        raise ValueError("model weight must be finite and in [0,1]")


def masked_normal_mix(left, right, weight):
    """Missing dict entries mean invalid joints, never identity/bind poses."""
    _weight(weight)
    result = {}
    for joint in left.keys() | right.keys():
        if joint in left and joint in right:
            result[joint] = normal_mix(left[joint], right[joint], weight)
        else:
            # Native normal-mode one-sided rows copy fully, including weight0.
            result[joint] = tuple((left if joint in left else right)[joint])
    return result


def two_track_empty_tail(base, older_partial, layer_weight, newest_track_weight):
    """Return analytic intermediate lanes under the explicit module assumptions."""
    _weight(layer_weight)
    _weight(newest_track_weight)
    older = masked_normal_mix(base, older_partial, layer_weight)
    newer = masked_normal_mix(base, {}, layer_weight)
    folded = masked_normal_mix(older, newer, newest_track_weight)
    result = masked_normal_mix(base, folded, 1.0)
    return {"older_snapshot": older, "newer_empty_snapshot": newer,
            "track_fold": folded, "layer_result": result}


def jointly_valid_translation_residual(layer_weight, newest_track_weight):
    """Local-row translation only; not a product/Q weight or a live measurement."""
    _weight(layer_weight)
    _weight(newest_track_weight)
    return layer_weight * (1.0 - newest_track_weight)
