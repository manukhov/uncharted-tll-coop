# Remote vertical ADS pitch at the blend-tick boundary (STATIC, 2026-07-18)

## Exact hypothesis

The sender's post-blend `aim-add-up/down` values are already correct, but the
retained screen-1 Player never reaches computed publisher callsite `0x140CB4176`.
Consequently build `dp` could capture sender pitch but could not apply it on the
receiver. The receiver's confirmed ticking boundary is instead
`PlayerAimController_UpdateBlendState 0x140CAFA20`, vtable slot `+0x30`.

Build `dy` commits the two peer named channels immediately after the stock blend
tick returns: `aim-add-up` at `base+4` and `aim-add-down` at `base+6`. It uses the
existing `PlayerAimController_CommitNamedAimChannel 0x140CAF470`, and commits zero
once when the peer ADS gate closes.

## Competing hypotheses

1. **Selected:** named aim channels are valid, but receiver application was placed
   at a computed-publisher callsite that does not tick for the puppet. Apply them
   after the already-live blend tick.
2. The named channels are not rendered for the retained Player. This remains the
   direct falsifier: successful non-zero blend-tick commits with no visible pitch
   reject the selected consumer.
3. Copy controller active-state or FgAnimData `+0x188..+0x194` directly. Runtime
   already rejected this even with `blend=0.866667` and non-zero vertical weight.
4. Put pitch in the confirmed FgAnim world quaternion. Rejected because that
   rotates the complete body and attachments instead of the authored upper-body
   additive.
5. Re-enable broad screen-1 pose/root writes. Rejected by prior locomotion jitter,
   folded transitions and native overwrite evidence.

## Independent static evidence

1. **Runtime reachability plus vtable identity.** The existing hook on
   `PlayerAimController` vtable slot `+0x30` runs for the retained puppet and its
   forced `+0x8EC` blend is part of the working horizontal ADS session. In the same
   build, receiver `AIM-PITCH-APPLY` at `0x140CB4176` never fired.
2. **Call-graph ordering.** `PublishRuntimeStateFromDecisionNodeLikeObject
   0x140CA17E0` calls `Player_UpdateMountedControllers 0x140C9F420` at
   `0x140CA2952`. That phase dispatches Player+`0x1D90` vtable slot `+0x30`, then
   returns before later Player publication, route-camera and attachment updates.
3. **Blend/reset dataflow.** `PlayerAimController_UpdateBlendState 0x140CAFA20`
   updates controller `+0x8EC` and resets/primes the `Aim0..Aim7` family. Applying
   after the original therefore survives that frame's reset instead of racing it.
4. **Self-contained named commit.** `0x140CAF470` resolves owner from
   controller `+0x108`, animation owner from Player `+0x1A0`, and the selected
   channel from controller `+0x8C8 + 4*index`. It implements both non-zero dirty
   publication and zero/clear semantics without a caller stack or TLS actor.
5. **Channel identity.** Publisher `0x140CB41F0` independently maps
   `aim-add-up=0x7283B601` to `base+4` and
   `aim-add-down=0xE8651C16` to `base+6`, after multiplication by `+0x8EC`.

## Implementation status

Release target `tll_teleport_hook_v2.dll` builds successfully and contains init
tag `2026-07-18dy`. The real `ida/tll.i64` now names `0x140C9F420`
`Player_UpdateMountedControllers`; headless label application reported
`save_database returned True`. After refreshing the MCP copy, both name and
address queries resolve that function at size `0x199`, with caller
`0x140CA2952` in `PublishRuntimeStateFromDecisionNodeLikeObject`.

## Falsifiable runtime prediction

With fresh role-A and role-B processes running build `2026-07-18dy`, hold ADS on
role A and move the reticle substantially up and down.

1. Sender logs `AIM-PITCH-CAPTURE-ACTIVE` with one non-zero channel.
2. Receiver logs `AIM-PITCH-BLEND-APPLY` with the same post-blend values and a
   live sequence; the old receiver-only `AIM-PITCH-APPLY` marker is not required.
3. The remote weapon/upper body follows vertical reticle movement while confirmed
   horizontal FgAnim yaw and projectile direction/origin remain unchanged.
4. Releasing ADS logs `AIM-PITCH-BLEND-CLEAR` and removes the vertical additive.

Non-zero capture without blend apply rejects receiver guard/channel readiness.
Non-zero blend apply with no visible pitch rejects named aim parameters as the
rendered puppet vertical consumer. A stuck pose after the clear marker rejects the
zero/clear timing. Any complete-body tilt rejects isolation from world quaternion.
