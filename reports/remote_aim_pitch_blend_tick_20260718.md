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

## Runtime `dy`: apply boundary rejected; named consumer not tested

Fresh processes PID 20512 (role A) and PID 12628 (role B) loaded build
`2026-07-18dy`. The user held ADS and swept vertically. The observed remote pose
still moved only horizontally; there was no vertical weapon/upper-body motion.

The result rejects the `dy` boundary, not the named-channel consumer:

- sender logged only `AIM-PITCH-CAPTURE up=0.0000 down=0.0000`, with no
  `AIM-PITCH-CAPTURE-ACTIVE`;
- receiver emitted neither `AIM-PITCH-BLEND-APPLY` nor `AIM-PITCH-APPLY`;
- receiver nevertheless computed a live local sample
  `y=-0.115522 up=0.270986 down=0 left=0 right=0.320994 blend=0.95`.

Thus the receiver already has a non-zero vertical directional weight, but the
`dy` named-mode guard prevents its blend-tick write. No successful named commit
occurred, so this run cannot classify the named channels as a visual consumer.

## Static `dz`: alternate Aim4/Aim6 post-publisher write

### Exact hypothesis

`PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0` has two mutually
exclusive publication modes. The retained puppet takes the alternate mode: the
stock function writes the constant `0.2` to even channels `Aim0/Aim2/Aim4/Aim6`.
Build `dz` lets that original publication finish, then replaces only `Aim4` and
`Aim6` with the receiver-local `up*blend` and `down*blend` through the same native
registry setter used by the stock alternate branch.

### Competing hypotheses

1. **Selected:** alternate `Aim4/Aim6` are the live vertical channels, but the
   stock puppet branch replaces directional values with symmetric `0.2`.
2. The alternate Aim channels are updated correctly but are not rendered by the
   retained puppet's current action graph. A successful non-zero native set with
   no visible vertical motion rejects the selected hypothesis.
3. The named branch is the intended consumer. `dy` did not test it because the
   puppet's resolved branch-control byte was nonzero; keep it unclassified.
4. Direct active-state/FgAnim `+0x188..+0x194`, full-body pitch, and broad pose/root
   ownership remain runtime-rejected alternatives.

### Independent evidence

1. **Runtime dataflow:** receiver build `dy` computed `up=0.270986` at
   `blend=0.95` while showing horizontal-only motion. This proves that a useful
   vertical value reaches the receiver-side publisher arguments.
2. **Exact branch disassembly:** after resolving Player `+0x1A0` through
   `0x1410D5D40`, `0x140CB41F0` tests the resolved `+0x38` control byte. Zero calls
   `PlayerAimController_CommitNamedAimChannel`; nonzero jumps to
   `0x140CB46F3..0x140CB472F` and iterates offsets `0,2,4,6`.
3. **Constructor identity:** `PlayerAimController 0x140CAE670` hashes `Aim0` through
   `Aim7` and stores their ids at controller `+0x8C8..+0x8E4`. The alternate loop
   indexes exactly this table using controller `+0x8E8` plus the even offset.
4. **Setter semantics:** `AnimParamRegistry_SetExistingFloatChannel 0x14172C8E0`
   finds an active 0x20-byte registry record by id, writes the float at entry
   `+0x30/+0x34`, copies generation `+0x28` to `+0x38`, and marks `+0x3C/+0x3D`
   dirty. It is both the stock alternate consumer and the `dz` write primitive.
5. **Constant identity and ordering:** data `0x142BE8DFC` is float `0.2`; the stock
   alternate loop passes it to all four even channels. The hook runs only after
   the original publisher returns, so the selected `Aim4/Aim6` values cannot be
   overwritten again by that invocation.

### Falsifiable runtime prediction

With fresh role-A and role-B processes on `2026-07-18dz`, a large role-A ADS sweep
up and down must produce receiver `AIM-PITCH-ALT-APPLY-ACTIVE` with a non-zero
`up` or `down`, live `blend`, and native `setUp/setDown` change results. The remote
weapon/upper body must follow vertically while horizontal yaw and confirmed
projectile origin/direction remain unchanged. On ADS release, the original
publisher resumes sole ownership and its stock `0.2` baseline must prevent a stuck
override. A successful active set with no visible vertical motion rejects
alternate `Aim4/Aim6` as the rendered vertical consumer.

### Implementation status

Release target `tll_teleport_hook_v2.dll` builds successfully with init tag
`2026-07-18dz`. The real `ida/tll.i64` labels `0x14172C8E0`
`AnimParamRegistry_SetExistingFloatChannel` and `0x142BE8DFC`
`kAimAlternateChannelBaseline`; headless application reported
`save_database returned True`. After refreshing `ida/tll_mcp.i64`, MCP resolves
the new function name and its stock callers at `0x140CAF693` (named commit) and
`0x140CB4724` (alternate publisher), and resolves the data name at
`0x142BE8DFC`.
