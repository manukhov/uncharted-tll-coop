# PlayerAimController world-basis relay (2026-07-13)

Status: **RUNTIME reached; target relay works, visible blend remains blocked**.

## Hypothesis

The puppet's directional aim pose is fixed because its screen-1 reticle target is
stale. Replacing only the target-point argument at
`PlayerAimController_ApplyWorldAimBasis 0x140CAF110` with the peer camera ray will
drive the native `aim-add-left/right/up/down` graph without replacing route-camera
state, Player root orientation, authored limits, or downstream pose fields.

## Competing hypotheses

1. Slot `+0x58` target substitution is sufficient for visible yaw and pitch.
2. The puppet never reaches the ground ADS slot `+0x58` because its action
   eligibility or aim-press state is inactive.
3. Slot `+0x58` publishes correct weights, but a later puppet animation phase
   suppresses or overwrites them.
4. Root/camera orientation rather than directional aim-additive weights owns the
   visible pose. Existing static data rejects this as the primary mechanism.

## Independent evidence

1. Both ordinary and heavy ground ADS paths call PlayerAimController vtable
   `+0x58` at `0x140E85925` and `0x140E858CE`. The slot address is
   `0x142A0B250`, containing `0x140CAF110`.
2. `GunMoveShield_CopyAimTargetPoint 0x140E84350` copies the reticle-derived
   float4 unchanged into the first vector argument. The body-forward vector is
   independently derived from Player root orientation.
3. `0x140CAF110` obtains the skeleton aim origin through slot `+0x80`, preserves
   the supplied directional limits, and dispatches slot `+0x50` into
   `PlayerAimController_ComputeDirectionalAimWeights 0x140CB32B0`.
4. `0x140CB32B0` normalizes `target-origin`, computes signed yaw/pitch, and calls
   `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0`, which writes
   the exact `aim-add-left/right/down/up` animation parameters.
5. `PlayerAimController_GetTargetPointFromReticle 0x140CB1720` builds its point
   from the owner's screen-route pose seed. Screen 1 is stale for the puppet;
   the relayed screen-0 peer pose seed is fresh and already used by the fire ray.

## Implementation

The DLL swaps only vtable slot `0x142A0B250`, after verifying it still points to
`module+0xCAF110`. The hook passes through unless all of these hold:

- controller owner `+0x108` is retained slot 1;
- owner screen index `Player+0x4010` is 1;
- the peer packet is at most 250 ms old and represents strict ADS;
- peer pose position and quaternion are valid.

On success it creates a hook-local float4:

`peerPosePosition + normalize(peerPoseQuaternion * -Z) * 180`

and forwards the untouched body-forward vector, both mode bytes, and all four
native float limits to the original function. It never mutates caller memory.

## Runtime prediction

- Success: `AIM-WORLD-BASIS relay` appears while the peer aims, its `fwd` values
  change with camera yaw/pitch, and the remote upper body follows them.
- Hypothesis 2: no relay line while the peer holds ADS.
- Hypothesis 3: relay lines and changing vectors appear, but the visible pose
  remains fixed.
- Local-player aim, locomotion, and HUD must remain unchanged because the owner
  and screen guards pass through for slot 0.

## First runtime result

Two-copy test (PIDs 16212/1692, commit `a2d8cf4`) delivered strict peer ADS and
repeatedly applied the caller-scoped pose seed, but produced zero
`AIM-WORLD-BASIS relay` calls. The target substitution itself was not reached.
This confirms competing hypothesis 2: the puppet fails the upstream ground
`Player_CheckAimActionEligibility` path. Follow-up implementation is documented
in `reports/player_aim_eligibility_callsite_relay_20260713.md`.

## Second runtime result

Two-copy test (PIDs 14320/14884, commit `6e32b91`) reached the slot repeatedly.
`AIM-WORLD-BASIS relay` changed with peer yaw and pitch, including forward vectors
from `(0.288,0.132,-0.948)` through `(0.316,0.247,-0.916)`. The user still saw no
aim animation and reported an incorrect root facing direction.

This rejects hypothesis 1 and selects hypothesis 3. The target data is live, but
the downstream directional weights are visually suppressed. The root-facing
symptom is separate: post-update code was replacing the peer body quaternion with
a camera-derived yaw while ADS was held.
