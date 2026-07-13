# Player aim additive permission relay (2026-07-13)

Status: **RUNTIME REJECTED as sufficient; permission is real but not final blocker**.

## Hypothesis

The puppet reaches `PlayerAimController_ApplyWorldAimBasis 0x140CAF110` and
computes changing directional weights, but its active animation-state descriptor
rejects aim additives through
`PlayerAimController_IsAimAdditiveAllowedForCurrentState 0x140CADF40`. Therefore
`controller+0x8A0` is never requested, `controller+0x8EC` remains zero, and all
four published `aim-add-*` values are multiplied to zero.

## Competing hypotheses

1. The current-state permission is false; admitting only the slot-1 peer ADS call
   at `0x140CB348A` raises the native blend and makes the aim pose visible.
2. Permission is already true and the blend rises, but the puppet animation graph
   does not consume the published Aim0..Aim7 channels.
3. The animation graph consumes the channels, but a later phase overwrites the
   pose.
4. The incorrect facing is caused by the same additive pipeline. Static data
   rejects this: root orientation is written separately after Player update.

## Independent evidence

1. `PlayerAimController_ComputeDirectionalAimWeights 0x140CB32B0` has exactly one
   call to `0x140CADF40`, at `0x140CB348A`. It writes request byte `+0x8A0 = 1`
   only when that call returns true.
2. `PlayerAimController_UpdateBlendState 0x140CAFA20`, mounted at vtable `+0x30`
   and called by the common Player controller phase, moves `+0x8EC` toward
   `PlayerAimController_GetAuthoredAimBlendTarget 0x140CAE480` only while `+0x8A0`
   is set; otherwise it moves the blend toward zero.
3. `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0` multiplies each
   of `aim-add-left/right/down/up` by `controller+0x8EC` before publication.
4. `0x140CADF40` selects authored flags from the active Player animation-state
   descriptor and explicitly rejects many movement-mode values. Its only callers
   are the controller state initializer `0x140CAE030`, reset/prime slot `+0x68`
   (`0x140CAF2F0`), and the directional compute path above.
5. Runtime PIDs 14320/14884 repeatedly reached slot `+0x58` with changing peer
   yaw/pitch vectors but produced no visible aim pose. This rules out stale target
   data and isolates the remaining suppression after directional computation.

## Root-facing correction

The post-Player-update transform code independently replaced the peer's body
quaternion with yaw derived from the peer camera whenever ADS was held. That made
the `bodyForward` input converge on the camera ray and could reverse the visible
root under differing camera/body axis conventions. Default ADS now keeps the
relayed `StatePacket::quat`; the old camera-facing path remains only behind its
explicit A/B toggle.

## Implementation

The DLL verifies the `E8` call at `module+0xCB348A` resolves exactly to
`module+0xCADF40`, then redirects only that call through a rel32-near relay. The
wrapper calls the original first and preserves true. It changes false to true only
for retained slot 1, screen 1, a peer packet no older than 250 ms, strict peer ADS,
and a valid relayed pose seed. No controller fields or animation parameters are
written directly.

## Runtime prediction

- During peer ADS, a previously rejected state emits
  `AIM-ADDITIVE-PERMISSION original=0 forced=1`, followed by the existing changing
  `AIM-WORLD-BASIS relay` lines.
- The remote upper body raises its weapon and follows peer yaw/pitch while its root
  continues to follow the peer body direction.
- If no permission line appears but animation becomes visible, the original state
  was already permitted and the root correction was sufficient.
- Permission and world-basis lines without visible animation select hypothesis 2
  or 3; the next discriminator is logging `+0x8A0/+0x8EC` around slot `+0x30`.

## Runtime result

PIDs 4412/17588 emitted both
`AIM-WORLD-BASIS relay target=(-848.573,49.107,539.231)` and
`AIM-ADDITIVE-PERMISSION original=0 forced=1`. The user confirmed that root
facing now looks correct, validating the separate body-quaternion correction, but
the aim animation is still absent.

This proves the current-state permission was false and the relay changed it, but
rejects hypothesis 1 as sufficient. Hypotheses 2/3 remain. The next diagnostic
hooks the already-mounted blend tick without changing behavior and records
`+0x8A0/+0x8EC` before and after the original call.
