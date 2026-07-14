# Player aim pose +Z and stable blend ramp (2026-07-14)

Status: **RUNTIME PARTIAL: axis/blend confirmed, visual tracking rejected**.

## Exact hypotheses

1. The relayed route pose quaternion must be expanded as local positive Z in the
   aim-target path. The current negative-Z helper reverses the target by exactly
   180 degrees.
2. The retained Player's authored state resets `PlayerAimController+0x8EC`
   between update passes. A mod-owned 180 ms monotonic ramp, written immediately
   before directional-weight publication and after the native blend tick, removes
   the broken aim-entry pose without an instantaneous `blend=1` snap.

## Competing hypotheses

1. The route pose quaternion uses camera negative Z and the remaining 180-degree
   error is in Player body-forward ownership.
2. Positive Z is correct, but another downstream animation node negates it.
3. The transition is broken only by the reversed target, not by blend ownership.
4. The blend oscillation is harmless and a downstream animation-graph state is
   solely responsible for the broken transition.

## Independent evidence

1. `PlayerAimController_GetTargetPointFromReticle 0x140CB1720` expands its route
   pose quaternion as `q*(0,0,+1)*inverse(q)`: `2*(xz+wy)`, `2*(yz-wx)`, and
   `1-2*(x*x+y*y)`, then adds the result to the pose position. This is the exact
   producer whose target reaches the ordinary and scoped ADS dispatch.
2. `Quaternion_RotatePositiveZ 0x14014D9F0` independently uses the same formula
   for the Player body-forward argument passed to aim-basis slot `+0x58`.
3. The rejected runtime logged relayed pose negative Z almost exactly opposite
   native body positive Z, with `AIM-WORLD-BASIS yawPre` commonly near 180 degrees.
   That is the precise signature predicted by a single axis-sign inversion.
4. `PlayerAimController_UpdateBlendState 0x140CAFA20` writes `+0x8EC`, while
   `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0` multiplies the
   four directional outputs by that field. Runtime showed the retained Player's
   value oscillating `0 -> ~0.5 -> 0 -> 1` despite a sustained ADS request.
5. The relay already controls both call boundaries: the blend-tick hook runs
   immediately after `0x140CAFA20`, and the world-basis hook runs immediately
   before `PlayerAimController_ApplyWorldAimBasis 0x140CAF110`, whose synchronous
   call chain reaches the publisher. A monotonic value written at those
   boundaries is the exact value consumed downstream; no broad Player state
   promotion is required.

## Narrow change

- Add a positive-Z quaternion helper and use it only for relayed pose-seed
  aim-target, IK target and world-basis construction. Head-look and optional root
  facing retain their separate negative-Z camera convention.
- Replace native puppet blend ownership with an exact 180 ms monotonic ramp. A
  gap over 250 ms starts a new ramp; sustained ADS reaches and holds one.
- Disable the mouse right-stick relay by default after its explicit runtime
  rejection.

## Falsifiable prediction

On sustained ADS, `AIM-WORLD-BASIS yawPre` must move from the previous near-180
degree opposition to the native 60-degree arc, and changing camera yaw/pitch must
change the remote aim direction. `AIM-BLEND-TICK` must rise monotonically across
multiple samples and then stay at one, without returning to zero while ADS is
held. A still-near-180 yaw rejects the positive-Z hypothesis; monotonic logs with
a broken entry select a downstream graph state; non-monotonic post-hook logs
reject the call-boundary ownership hypothesis.

## Runtime result

PIDs 13036/17280 matched both internal discriminators:

- the first positive-Z `AIM-WORLD-BASIS` entered at `yawPre=59.1` instead of the
  old near-180-degree opposition, and relayed pose quaternion changes produced
  substantially different forward vectors and target points;
- each ADS entry produced a monotonic post-hook sequence such as
  `0 -> 0.261 -> 0.433 -> 0.606 -> 0.783 -> 0.956 -> 1`, with no reset while
  ADS remained held.

The visible prediction still failed: the user rotated the camera and reported
that the remote aim did not move. The output explains why this test cannot be
called complete: as camera yaw crossed the stock arc, the downstream weights
mostly saturated at `R=1` while `yawPre` ranged beyond 60 degrees (up to roughly
122 degrees in this run). Positive-Z reconstruction and stable blend are
**CONFIRMED**, but they are insufficient. The remaining candidate is ownership
of camera-relative body/sector turn before directional weights are clamped, or a
downstream turn-in-place graph input. Scoped hair/weapon visibility was not
tested in this run.
