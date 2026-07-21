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

## Runtime `dz`: transition boundary rejected; consumer still untested

Fresh PID 9360 (role A) and PID 3108 (role B) loaded `2026-07-18dz`. The user
again observed horizontal-only motion with no vertical response. Receiver PID
3108 emitted only
`AIM-PITCH-ALT-APPLY seq=361 up=0 down=0 blend=0 setUp=0 setDown=0`, and never
emitted `AIM-PITCH-ALT-APPLY-ACTIVE`. Later, the already-proven vtable boundary
recorded `Aim blended-pitch sample y=-0.136097 up=0.191499 down=0 ...
blend=0.866667`.

Thus `dz` did not make a nonzero setter call. It rejects direct callsite
`0x140CB4176` as the active steady-state boundary, not `Aim4/Aim6` as a rendered
consumer.

## Static `ea`: steady-state smoother/publisher boundary

### Exact hypothesis

The nonzero puppet weights take `0x140CB416F`, not the transition-only direct
publisher at `0x140CB4176`. Build `ea` hooks the E8 at `0x140CB416F`, calls the
original smoother/publisher completely, then uses its already-published raw
`up/down` fields and live blend for the `Aim4/Aim6` native setter overwrite.

### Competing hypotheses and boundaries

1. **Selected:** `0x140CB416F -> 0x140CB47A0` is the narrow steady-state boundary;
   post-call controller fields are the exact smoothed values that reached stock
   publication.
2. Detour the global publisher entry `0x140CB41F0`. It would see both branches but
   also every reset/prime caller and is broader than the single proven edge.
3. Apply after `PlayerAimController_ApplyWorldAimBasis 0x140CAF110`. This sees the
   same final fields but is later and wraps the entire slot-`+0x58` calculation.
4. Keep `0x140CB4176`. Runtime and the mode-transition control flag reject it as
   the steady-state boundary; it remains valid only for transition diagnostics.
5. Even a successful alternate setter may not be consumed by the retained
   puppet's action graph. This is the direct visual falsifier for `ea`.

### Independent evidence

1. **Runtime boundary split:** the same receiver produced only a zero marker at
   `0x140CB4176`, then a nonzero post-world-basis sample with high blend and no
   active marker.
2. **Exact control flow:** `0x140CB4168` tests the high byte of the mode-change
   state. Nonzero calls direct publisher at `0x140CB4176`; zero calls
   `0x140CB47A0` at `0x140CB416F`. The flag is set when controller mode byte
   `+0x8A1` changes, making the direct path transitional.
3. **Smoother dataflow:** `0x140CB47A0` reads blend `+0x8EC` and previous LRDU at
   `+0x8B8..+0x8C4`, advances signed horizontal and vertical scalar states at
   `+0x8A4/+0x8AC`, and splits them back into directional values.
4. **Guaranteed stock publication:** the smoother tail-jumps at `0x140CB491A` to
   `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0`. Returning to
   the wrapper therefore means both smoothing and the stock named/alternate branch
   are complete.
5. **Vtable/call graph:** `PlayerAimController_ApplyWorldAimBasis 0x140CAF110`
   slot `+0x58` dispatches slot `+0x50` at `0x140CAF2AD`; concrete
   `PlayerAimController_ComputeAndPublishAimAdditives 0x140CAECB0` calls the
   directional computation at `0x140CAED72`.
6. **Consumer identity:** both the stock alternate publisher and `ea` use
   constructor-defined `Aim4/Aim6` ids and
   `AnimParamRegistry_SetExistingFloatChannel 0x14172C8E0`.

### Falsifiable runtime prediction

On `2026-07-18ea`, a role-A vertical ADS sweep must produce receiver
`AIM-PITCH-SMOOTH-ACTIVE` with raw and post-blend vertical values matching the
high-blend diagnostic. If `mode=alternate`, at least the changed axis should have
a nonzero setter result and the remote upper body/weapon should pitch. A changed
active setter with no visual motion rejects alternate `Aim4/Aim6` as the rendered
consumer. If `mode=named` and setters are unchanged, stock publication already
contained the directional values, moving the blocker strictly below the registry.
No marker despite another high-blend sample would reject relay installation or
the static callsite identity. Horizontal yaw and projectile alignment must remain
unchanged.

### Implementation status

Release `tll_teleport_hook_v2.dll` builds successfully with tag
`2026-07-18ea` and contains the `0x140CB416F` installation and
`AIM-PITCH-SMOOTH-ACTIVE` markers. The real IDB now names `0x140CB47A0`
`PlayerAimController_SmoothAndPublishDirectionalAimWeights`; headless label
application reported `save_database returned True`. After refreshing the MCP
copy, address lookup resolves the new name and callers `0x140CAF357` (reset/prime)
and `0x140CB416F` (steady-state directional computation).

## Runtime `ea`: existing-channel mutation rejected

The user again observed horizontal-only remote ADS. Receiver PID 16948 reached
the intended steady-state boundary with a useful vertical value:

`AIM-PITCH-SMOOTH-ACTIVE seq=486 mode=alternate rawUp=0.1169 up=0.0708 blend=0.6056 setUp=0 setDown=0`.

An independent later sample reached `up=0.380015` at `blend=0.872222`. Therefore
the boundary and receiver-local vertical computation are both live. Exact
`AnimParamRegistry_SetExistingFloatChannel 0x14172C8E0` semantics return one even
when the existing value is unchanged; zero means that no active matching entry
with a non-null object was found. The run rejects the direct existing-entry
mutation used by `ea`. It does not reject `Aim4/Aim6` as a rendered consumer,
because neither channel was activated or sent to the animation action graph.

## Static `eb`: activate the stock named AnimStateLayer lifecycle

### Exact hypothesis

The retained puppet has preallocated but inactive type-2 animation-layer slots.
Its alternate publisher attempts to mutate `Aim4/Aim6` as if they were already
active, so the writes are no-ops. At the proven post-smoother boundary, build
`eb` uses the complete stock `PlayerAimController_CommitNamedAimChannel` path for
post-blend down/up. That path activates a reserved descriptor, initializes its
`AnimStateLayer`, writes the weight and queues the authored `aim-add-*` direct
fade.

### Competing hypotheses

1. **Selected:** activation/direct fade is the missing lifecycle edge; the full
   stock named commit makes the authored vertical additive visible.
2. The layer activates and its direct fade is accepted, but the retained
   puppet's authored state is absent or never reaches `ProcessAnimCmdListSegment`.
3. The registry/controller is replaced later in the frame, so a successful
   activation disappears before palette publication.
4. A late weapon-attachment stage overwrites only the weapon. This cannot explain
   stationary upper-body/arm joints and is downstream of the palette.
5. The Aim IDs are wrong for the puppet. This is weak: the constructor and both
   stock publication modes independently select the same `base+4/base+6` IDs.

### Independent static evidence

1. **Runtime plus exact setter semantics:** `ea` reached the steady-state edge
   with nonzero pitch, but both setter returns were zero. The setter scans only
   active descriptors and cannot activate or allocate one.
2. **Descriptor dataflow:** `AnimParamRegistry_ActivateTypedChannelSlot
   0x14172A370` scans inactive 0x20-byte descriptors, prefers matching type/id,
   falls back to a type-2 id-zero slot, writes id/context and sets `+0x14=1`.
   `0x14172A910` calls it, binds registry context at object `+0x540` and invokes
   the `AnimStateLayer` vtable slot `+0x60`.
3. **Independent Player lifecycle path:** Player vtable slot `+0x710` at
   `0x1410CE260`, callsite `0x1410CF109`, preallocates fifteen inactive type-2
   descriptors with id zero. The type-2 constructor `0x14189B7B0` installs the
   `AnimStateLayer` vtable and writes object type `2` at `+0x8`.
4. **Exact aim call graph:** for a nonzero missing channel,
   `PlayerAimController_CommitNamedAimChannel 0x140CAF470` resolves
   `Player+0x1A0`, calls `0x14172A910`, writes the named-channel value/generation,
   then calls `AnimStateLayer_RequestDirectFade 0x14189CC80` with the supplied
   `aim-add-*` hash. Its zero branch safely clears only an existing type-2 entry.
5. **Second subsystem:** `NetCharacterFireAnimationController::PushLayer
   0x140A1A510` independently uses the same ensure, weight and direct-fade
   protocol for `net-fire`; this rules out an aim-only interpretation of the
   registry helpers.
6. **Downstream consumer:** `0x141A932F8` processes animation commands and
   `0x141A93410..0x141A9341E` publishes the current palette at FgAnimData `+0x230`;
   `0x14181A9B0` copies it to upload `+0x240`. Weapon attachment helper
   `0x1410EDFB0` only consumes hand/wrist transforms and does not own Aim layers.

### Safety guard and implementation

The stock commit dereferences the result of `0x14172A910`; it is unsafe if no
compatible descriptor exists. Build `eb` first verifies, per channel id, either
an active matching type-2 descriptor, an inactive matching type-2 descriptor, or
an inactive type-2 id-zero descriptor. It then calls down with
`(base+6, 0xE8651C16, 2)` and up with `(base+4, 0x7283B601, 3)`, both using the
receiver's raw weight multiplied once by live blend. This is not the `dy` test:
`dy` rejected alternate mode before calling named commit, while `ea` called only
the incomplete existing-entry setter.

For a nonzero weight, the guard accepts any of those three activatable states;
for zero/clear, it requires an already-active matching type-2 descriptor. Thus a
zero axis neither claims a reserve slot nor calls native code against an invalid
registry.

The runtime marker records call admission, active descriptor state, contexts
`0xAC/0xAD`, committed values, direct-fade queue count and dirty flags. Direct
FgAnim copies, full-body pitch, broad pose/root writes and a global publisher
detour remain rejected competitors.

Release `tll_teleport_hook_v2.dll` builds successfully with tag
`2026-07-20eb` and contains `AIM-PITCH-NAMED-ACTIVE`. The real IDB labels
`0x14172A370` `AnimParamRegistry_ActivateTypedChannelSlot`, `0x14172A910`
`AnimParamRegistry_EnsureFloatAnimStateLayer`, and `0x14189CC80`
`AnimStateLayer_RequestDirectFade`; headless application reported
`save_database returned True`. The refreshed MCP copy resolves all three names
and exact function sizes `0xF5/0xA9/0x2F2`.

### Falsifiable prediction

On fresh build `2026-07-20eb`, moving up must produce
`AIM-PITCH-NAMED-ACTIVE mode=alternate callUp=1 activeUp=1 ctxUp=AD`, a committed
value matching `rawUp*blend`, nonzero direct-fade queue count and dirty state.
Moving down analogously activates context `AC`. The remote arms/upper body and
weapon must follow vertically; zero commits must return them to neutral without
changing confirmed horizontal yaw or projectile direction/origin.

If the descriptor activates and direct fade is queued but the palette/pose stays
unchanged, the authored-state consumer is rejected. If the descriptor is active
immediately after commit but absent next frame, later registry replacement wins.
If palette joints move but only the weapon remains fixed, reopen the downstream
attachment override rather than the Aim registry.

## Runtime `eb`: per-frame named/alternate ownership is REJECTED

Fresh PID 19812 (role A) and PID 20316 (role B) loaded build `2026-07-20eb`; both
aim relays installed. On ADS, the user observed a new hard regression: the remote
aim animation broke, shook continuously and jerked the weapon upward. Both
processes were stopped immediately after the report.

Receiver PID 20316 logged the first active application:

`AIM-PITCH-NAMED-ACTIVE seq=290 mode=alternate rawUp=0.1171 up=0.0709 blend=0.6056 callUp=1 activeUp=1 ctxUp=AD valueUp=0.0000 fadeUp=0 dirtyUp=1`.

This falsifies the `eb` prediction. The descriptor was activated with the
expected context, but the probed named value did not equal the supplied weight,
the expected fade-count observation was absent, and the visible pose regressed
instead of following pitch. The implementation that calls the named commit every
alternate-mode frame is therefore REJECTED.

Exact disassembly resolves the apparent dual layout more strongly than the
initial runtime note. AnimStateLayer fields are: current/evaluated weight
`+0x28`, target weight `+0x2C`, remaining fade time `+0x30`, full fade duration
`+0x34`, starting weight `+0x38`, easing/dirty mode `+0x3C`, and
release-on-zero pending `+0x3D`. The formerly named
`AnimParamRegistry_SetExistingFloatChannel 0x14172C8E0` is not a value setter.
It writes target zero, copies current to starting weight, stores the supplied
float in both fade-time fields, sets easing to one and marks release pending.
It is therefore relabeled
`AnimParamRegistry_RequestLayerFadeOutAndRelease`.

The independent registry step at `0x141A911A0` calls the layer step routine and,
when `+0x3D == 1 && +0x28 == 0`, calls virtual `+0x50` and clears the descriptor's
id, context and active byte. Thus the exact `eb` loop was: named commit activates
Aim4, stock alternate publication requests its 0.2-second fade-out/release,
the registry deactivates it at zero, and the hook activates it again. This
matches the continuous visible shake and upward snap. One-time named activation
followed by `0x14172C8E0` is also REJECTED: it only schedules that layer's
release and cannot carry pitch.

`PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0` supplies a third
independent discriminator. It always writes the real LRDU weights into the
active state at `+0x188/+0x18C/+0x190/+0x194`. The named branch also publishes
those values through Aim0..Aim7, while the alternate branch merely requests
fade-out/release of Aim0/Aim2/Aim4/Aim6 with the constant 0.2 seconds. Therefore
Aim4/Aim6 are REJECTED as the alternate vertical-data consumer; the retained
active-state values or a different alternate descriptor/downstream reader are
the remaining competitors.

## STATIC/runtime discriminator `ec`: read-only local/puppet registry snapshot

The local and puppet controllers provably do not share the same animation graph
contract: role A logged local screen 0 in `named` mode, while receiver PID 20316
logged puppet screen 1 in `alternate` mode. Mirroring named-layer activation
into the alternate graph is therefore structurally invalid.

Build `2026-07-20ec` removes both receiver named commits, including the older
transition hook, and makes no animation-state mutation. After the already-proven
native transition and steady publishers it records at most six active-pitch
samples per screen. Each sample contains the resolved branch byte, double-buffer
base index, active-state LRDU values, and exact descriptor/object state for
Aim0..Aim7: id, active, type, context, current/target/start weights, remaining and
full fade duration, easing and release-pending state.

Competing hypotheses are now explicit:

1. A different active Aim descriptor/context in the alternate graph tracks
   vertical input; its weight must correlate with `stateUp/stateDown` on screen 1.
2. No Aim descriptor tracks the alternate vertical value; the real consumer is
   downstream of the active-state `+0x188/+0x18C` fields.
3. Aim4/Aim6 stay active with nonzero directional targets despite the static
   release path, which would falsify the current interpretation.

The evidence quota for this read-only run is satisfied by three different
types: exact publisher and release/deactivation disassembly, data-flow from the
publisher arguments into the active-state fields, and the prior runtime
local-`named`/puppet-`alternate` mode split. No animation write is needed to
distinguish the competitors.

### Falsifiable prediction

On a fresh `ec` run, aiming vertically must not reproduce the `eb` shake because
the receiver named commits are gone. Both processes must emit bounded
`AIM-REGISTRY-SNAPSHOT` records. Screen 0 must report `mode=named`, screen 1
`mode=alternate`, and screen 1 must show nonzero `stateUp` or `stateDown`. If no
alternate Aim descriptor correlates with those values and Aim0/2/4/6 are absent,
inactive, or release-pending, hypothesis 2 wins and the next static target is the
reader of active-state `+0x188/+0x18C`. A correlated different descriptor selects
hypothesis 1. Active directional Aim4/Aim6 would instead select hypothesis 3 and
force reanalysis before mutation.

## Runtime `ec`: alternate Aim registry route is REJECTED

Fresh PID 20884 (role A) and PID 11636 (role B) loaded build
`2026-07-20ec`; both native aim relays and the state network installed. The user
confirmed that ADS animation was normal again: the `eb` shake/upward jerk was
gone. The clone's weapon still did not move vertically, as expected from a
read-only discriminator.

Receiver PID 11636 produced six steady-state screen-1 samples in structural
`mode=alternate branch=1 base=0`. The strongest upward sample was:

`blend=1 rawUD=0.9990/0 stateLRDU=0/0.9998/0.9990/0`.

Thus the received/smoothed upward value survived exactly into active-state
`stateUp`; no network, input, blend, or publisher boundary remains missing. In
the same sample every controller Aim id A0..A7 reported `found=0`, not merely
inactive or release-pending. The other five samples showed the same absence while
`rawUp/stateUp` ranged from `0.5670` to `0.9933`.

This falsifies hypotheses 1 and 3 and confirms hypothesis 2: the alternate graph
does not carry vertical direction through any of this controller's Aim0..Aim7
registry descriptors. `0x140CAF470`, `0x14172A910`, `0x14189CC80` and
`0x14172C8E0` are all excluded as receiver vertical-apply routes. The remaining
frontier is now strictly downstream of active-state `+0x188/+0x18C`: trace every
reader through the authored alternate pose, joint palette and weapon attachment
chains before proposing another mutation. Both game processes were closed after
capturing the result.

## STATIC `ed`: keep the stock publisher on its named-channel branch

The downstream trace changes the interpretation of the `ec` result. The four
LRDU floats are in the parameter buffer selected by `[AnimControl+0x58]+0x10`
(the third of three buffer pointers): `0x141729480` allocates the three authored
parameter buffers, and `0x140CB426A..0x140CB4293` writes the four floats into
that third buffer. The same
publisher then chooses the only visible aim-pose protocol at
`0x140CB42B4/0x140CB42B6`:

- branch byte zero falls through to four stock
  `PlayerAimController_CommitNamedAimChannel 0x140CAF470` calls for
  `aim-add-left/right/down/up`;
- branch byte nonzero jumps to `0x140CB46F3` and only asks Aim0/Aim2/Aim4/Aim6
  to fade to zero and release through `0x14172C8E0`.

There is therefore no missing alternate Aim descriptor to create. Screen 1 is
being kept on the explicit release path even though the receiver has supplied
valid current LRDU weights. Build `ed` removes the six-byte conditional jump at
`0x140CB42B6`, leaving the stock named fallthrough authoritative for every
PlayerAimController. Local screen 0 already has branch byte zero, so its control
flow is unchanged; the screen-1 puppet stops fighting the same named layers with
the alternate release path.

### Competing hypotheses

1. **Selected:** the puppet's branch byte selects a network/alternate release
   contract whose missing native animation snapshots are not present in the
   retained campaign puppet. Keeping the stock publisher on its named branch
   supplies the already-authored visible aim layers without a second writer.
2. The LRDU parameter buffer has an independent alternate pose reader below
   `AnimControl`; forcing the named branch would add no vertical motion.
3. A late palette, joint, or weapon-attachment owner suppresses vertical motion
   even when a stable named aim layer is active.
4. Full `NetAnimCmd` snapshot replay is the required network contract. It remains
   broader and its earlier malformed apply path crashed, so it is not selected
   while the stock publisher has a narrower complete lifecycle.

### Independent evidence quota

1. **Exact control flow and data flow:** disassembly at
   `0x140CB426A..0x140CB42B6` writes LRDU, resolves the base-state player-info
   record, and uses one `JNZ` to choose named commit versus fade/release.
2. **Independent lifecycle:** `0x140CAF470 -> 0x14172A910 -> 0x14189CC80`
   activates a reserved type-2 layer, stores the weight and queues the authored
   `aim-add-*` fade. The alternate target `0x14172C8E0` only fades that layer to
   zero and releases it.
3. **Independent downstream consumer:** `0x1418A0EB0` and `0x1418A30A0`
   copy the full parameter buffer from `*(AnimStateLayer+0x530)+0x10` to
   `*(AnimStateInstance+0x178)+0x10` when instance flag `+0x190 & 0x20` is set;
   `0x1418A4FD0` then submits that instance state to the evaluator. The authored
   holder is therefore not an output-only diagnostic buffer.
4. **Runtime structural discriminator:** `ec` proved local screen 0 is named,
   puppet screen 1 is alternate, all puppet Aim0..Aim7 descriptors are absent,
   and `rawUp=stateUp=0.9990` survives into the publisher buffer.
5. **Runtime visible consumer:** `eb` activated Aim4 context `AD` and immediately
   produced an upward weapon jerk. Its continuous shake is exactly explained by
   the next frame's stock alternate fade/release followed by reactivation. This
   proves that a named layer reaches the visible weapon pose; it does not support
   a missing late attachment owner.

### Falsifiable runtime prediction

On fresh build `2026-07-20ed`, receiver startup must log
`named aim-publisher branch installed (0x140CB42B6 fallthrough)`. A role-A ADS
vertical sweep must make the remote arms/weapon move smoothly up and down, with
no `eb` shake or repeated snap. `AIM-REGISTRY-SNAPSHOT` on screen 1 should now
show Aim4/Aim6 present and active, `release=0`, and weights correlated with
`stateUp/stateDown` instead of all `found=0`. ADS exit must return the pose to
neutral. Horizontal alignment and projectile direction/origin must stay unchanged.

If descriptors become active and stable but vertical motion is still absent,
hypothesis 2 wins and the next trace resumes at the command evaluator. If the
old shake returns, the branch override is insufficient and another release
writer exists. If horizontal/projectile alignment changes, the patch is too
broad and must be removed.

This NOP is deliberately a bounded discriminator, not the final production
policy. `0x1410D5D40` resolves the currently active authored `base` state, so the
tested byte can vary across cover, climb, and other action states; static analysis
does not establish it as a permanent screen/network flag. If the visible test
passes, replace the global branch edit with a retained-screen-1/controller-scoped
decision and retain the original branch for all other controllers and states.

The real `ida/tll.i64` was updated through `scripts/ida_apply_labels.py` and saved
successfully (`apply_labels OK: 1084 functions, 19 data names`; `save_database
returned True`). Independent name resolution verified
`AnimControl_ResolveActiveBaseStatePlayerInfo = 0x1410D5D40`, alongside the
existing publisher and fade/release labels. The MCP copy was then refreshed from
the saved database and its server restarted.

## RUNTIME REJECTED `ed`: named fallthrough corrupts the character model

Fresh PIDs 2608 (role A) and 22216 (role B) both loaded build `ed`, installed the
publisher fallthrough patch, and established the loopback roles. On the receiver,
screen 1 remained authored `mode=alternate branch=1`, while Aim4 became a stable
active type-2 layer with context `AD`, `release=0`, and its weight tracked the
incoming upward value (`target=0.5489`, then `0.6337`, `0.6445`, `0.6462`). This
satisfied the descriptor/lifecycle half of the prediction.

The visible half was decisively falsified: the user reported that the character
model broke completely. The test was stopped and both processes were closed.
Therefore the missing named-vs-release branch is **not** the remote pitch apply
contract. A stable named Aim4 layer is structurally incompatible with the
puppet's authored alternate graph; this is stronger than the earlier `eb` shake
and rejects both global and screen-1-scoped forcing of this named path.

Build `ee` removes the branch patch entirely and restores the stable `ec`
publisher behavior. The next static frontier is the generic alternate data path:
trace the full buffer copy from `AnimStateLayer+0x530` to
`AnimStateInstance+0x178`, the `+0x190 & 0x20` admission flag, and evaluator entry
`0x1418A4FD0`. No further runtime test is authorized until that path identifies
the exact alternate pose destination and excludes late palette/attachment owners.

## STATIC `ef`: read-only base-instance parameter-copy discriminator

### Exact hypothesis and competitors

The alternate publisher already writes LRDU to the shared AnimControl parameter
buffer. The remaining narrow failure may be before evaluation: the puppet's
active `base` type-2 layer can lack an active instance, the instance's authored
parameter-copy bit `+0x190 & 0x20`, or a compatible destination buffer. Build
`ef` changes no animation state; it records that exact chain for local screen 0
and puppet screen 1.

Competing outcomes are:

1. **Selected diagnostic candidate:** screen 1 is missing the active base
   descriptor/instance, copy bit, or compatible destination, so LRDU never enters
   its live `AnimStateInstance` parameter buffer.
2. Source and destination LRDU match while the visible pitch remains absent; the
   puppet's active alternate animation tree does not semantically map those
   fields, pointing to missing native animation-state/snapshot selection.
3. Source/destination and the evaluator are correct, but a later palette or
   attachment owner erases pitch. This remains weaker because prior palette and
   residual-pitch probes found no corresponding upstream vertical pose.

### Independent static evidence quota

1. `AnimControl::BeginStepInternal 0x141A78A60`, identified by source-file and
   function assertion strings, sorts active registry layers and dispatches type 2
   to `0x1418A30A0`.
2. `AnimStateLayer` constructor `0x14189B7B0`, anchored by RTTI/vtable and the
   independent `AnimStateInstanceTrack::Allocate` assertion, binds the shared
   parameter wrapper at layer `+0x530` and allocates the active-instance tracks.
3. `AnimStateInstance` initializer `0x1418B3F10` allocates the destination wrapper
   at `+0x178`; it derives flag `+0x190 & 0x20` directly from an authored config
   byte, not a network guess.
4. Exact disassembly at `0x1418A0EB0` traverses the first active layer track and
   performs `memcpy([instance+0x178]+0x10, [layer+0x530]+0x10, destSize)` only
   under that bit. Both `0x1418A2E50` and `0x1418A30A0` call it.
5. `0x1418A4FD0` is independently anchored by the literal
   `AnimStateInstance::PhaseUpdate`; it samples the animation tree using the
   instance parameter descriptor at `+0x178`.

### Falsifiable read-only prediction

Fresh build `2026-07-20ef` must leave the model stable and log `baseInst` for both
screens. If candidate 1 is correct, screen 1 differs from screen 0 at descriptor,
instance, `copy`, wrapper identity/size, or destination LRDU; specifically its
destination must fail to follow source `stateLRDU`. If screen 1 reports
`found=active=inst=copy=srcSame=1`, compatible sizes, and destination LRDU tracks
source while the weapon remains vertically fixed, candidate 1 is rejected and
the trace moves to authored animation-state/snapshot selection. Any pose change
would invalidate the read-only probe itself; no write or branch override remains
in `ef`.

The real IDB now carries the six STATIC names above. Headless application reported
`apply_labels OK: 1090 functions, 19 data names` and `save_database returned
True`; independent resolution verified all six addresses, after which the MCP copy
was refreshed and restarted.

## RUNTIME CONFIRMED `ef`: LRDU reaches the active `AnimStateInstance`

Fresh PIDs 6772 (role A) and 21488 (role B) loaded read-only build `ef`. The user
performed several large vertical ADS sweeps; the clone's weapon still did not
move vertically, and the probe caused no model regression. Six bounded receiver
samples all resolved the base descriptor hash `7339D08C` with
`found=1 active=1 type=2 tracks=1/2 inst=1`. The instance consistently reported
`flags=0x28 copy=1 srcSame=1 sizes=1840/1840`.

The copied destination tracked the live shared source across both directions:

- down: source `0.1031`, destination `0.1036`;
- up: source/destination `0.3690/0.3365`, `0.6160/0.6081`,
  `0.7607/0.7509`, `0.8690/0.8616`, and `0.9554/0.9534`.

Candidate 1 is therefore **REJECTED** and the full generic parameter-copy path is
**CONFIRMED** at runtime: the alternate publisher's LRDU reaches the live
`AnimStateInstance+0x178` buffer before evaluation. It is not valid to patch
`0x1418A0EB0`, force `+0x190 & 0x20`, replace wrappers, or enlarge buffers.

Candidate 2 becomes the selected static frontier: local and puppet use different
authored animation-state/tree semantics, or the puppet lacks the native animation
command/snapshot selection that makes these LRDU fields meaningful. Candidate 3,
a late palette/attachment overwrite, remains possible but weaker because the
parameter chain is intact and earlier downstream probes found no upstream
vertical pose. The next discriminator must first compare local/puppet active
instance identity, authored config, and root-tree ownership read-only; the prior
malformed broad snapshot replay remains rejected until its exact native contract
and ABI are statically re-established.

## STATIC `eg`: authored state/callback fingerprint after the confirmed copy

### Exact hypothesis and competitors

The selected hypothesis is now narrower than full snapshot replay: screen 0 and
screen 1 can carry different authored base-state identities inside their live
`AnimStateInstance`. In that case identical LRDU bytes legitimately produce
different results because the puppet state lacks the local phase/dtgroup/root
semantics.

Competing outcomes are:

1. **Selected candidate:** puppet state id, callback-presence mask, flags or root
   identifiers differ stably from the sender's aiming instance.
2. The fingerprints match; the missing pitch lies inside the common authored
   root/parametric asset rather than state selection.
3. A difference is only transient because sender and receiver samples cover
   different transition/action phases; it must not be promoted to a write.
4. A late palette/attachment owner erases a valid vertical result. This remains
   weaker than candidates 1/2 because no earlier probe found an upstream vertical
   pose.

### Independent static evidence quota

1. `0x1418B24F0` copies a 0x80-byte state snapshot to
   `AnimStateInstance+0x90`, reads the state id at absolute `+0x98`, resolves the
   authored record via `0x141926AE0`, overlays callback pointers/flags, resolves
   the root snapshot node, and is anchored by the source assertion
   `AnimStateSnapshot::SnapshotAnimState`.
2. `0x14189D720` walks every active layer track/instance, maps the track snapshot
   through `0x1418A4670`, then calls `AnimStateInstance::PhaseUpdate
   0x1418A4FD0`. This independently ties the fingerprint to the live evaluator,
   not an inactive configuration record.
3. `0x1418A4670` gates `anim-state-dtgroup-func` on instance `+0xC0` and carries
   the 48-byte parameter descriptor. `0x1418A4D50` separately gates
   `anim-state-phase-func` on `+0xE8` and passes the same descriptor from
   `instance+0x178` into script evaluation.
4. Runtime build `ef` confirmed that `+0x178` already contains the incoming LRDU
   while the visible weapon remains fixed, so state/callback semantics are the
   first unclassified boundary after a known-good copy.

### Falsifiable read-only prediction

Build `2026-07-20eg` records `AIM-BASE-STATE` only during nonzero vertical aim,
separately for sender screen 0 and receiver screen 1. A stable mismatch in
`state`, `callbacks`, `stateFlags`, or `roots/rootSnap` selects candidate 1 and
authorizes tracing only the command that selects that exact field. Matching
fingerprints reject instance selection and select candidate 2. A mismatch that
appears only during entry/exit selects candidate 3 and requires aligned
steady-state sampling. No visible pose, horizontal aim, projectile, HUD or model
change is expected; any such change invalidates the read-only probe.

### Runtime result: matching high-level state metadata, still no pitch

Fresh PIDs 7684/17560 loaded `2026-07-20eg`. The user performed vertical ADS
sweeps with pistol and rifle and again observed no vertical movement on the
remote weapon. The read-only probe caused no reported crash or model corruption;
both processes were closed after the capture.

Three fingerprints appeared on both sender and receiver:

| state `+0x98` | snapshot word | callbacks | state flags | logged scalar tuple |
|---|---|---:|---:|---|
| `66D4BF80` | `BE7CB4A2` | `1D` | `1` | `1CEC7F3A/6C2B2396/4B0F5DE3` |
| `3C347132` | `C45CF708` | `38` | `0000008000000009` | `9DCD31BC/6C2B2396/4B0F5DE3` |
| `41C84248` | `25CC08FD` | `1D` | `1` | `5F913F74/6C2B2396/4B0F5DE3` |

This **REJECTS** the selected hypothesis that the puppet always lacks the
sender's high-level state/callback/flag contract. It **CONFIRMS** that matching
high-level state metadata plus confirmed destination LRDU is still insufficient
for visible pitch. The receiver also logged `state=2E3C4F6B`, but the bounded
samples were not frame-aligned, so that isolated sample is not a stable-state
mismatch result.

### Static correction and next boundary

The earlier `roots/rootSnap` description was too strong. Instance `+0x130` is a
`SnapshotNodeHeap*`; `+0x138` is only the root node index within that heap.
`AnimStateSnapshot_SelectRootNodeIndex 0x1418B2EF0` selects it, and
`SnapshotNodeHeap_ResolveNodeByIndex 0x1418C2F90` resolves it before the root
virtual dispatch in `AnimStateInstance::PhaseUpdate`. Different raw indices
across processes therefore do not prove different root classes.

The next read-only comparison must record `{rootIndex, rootVtableRVA,
rootTypeDescriptorRVA}` and the full authored-config tuple: mode `+0x18C`,
apReference `+0x194`, scalars `+0x118/+0x11C/+0x120/+0x124/+0x12C/+0x158`,
authored-key count/content hash from `+0x68/+0x88`, and override count/current
state match from `+0x148`. `+0x188` is a per-activation serial and is explicitly
non-diagnostic. Matching root/config moves the fault boundary below the common
tree into parameter binding, script consumption, or the final pose sink.

## STATIC `eh`: resolved root RTTI and complete config fingerprint

The user explicitly reconfirmed the `eg` result: no vertical movement and no
new bugs. `eh` remains diagnostic-only.

### Hypothesis and competitors

The selected hypothesis is a concrete root-node or authored-routing mismatch
below the already matching high-level state metadata. The alternatives are:

1. Same root RTTI but different authored key list or active override result.
2. Full stable root/config match, placing the fault inside common-tree parameter
   binding, script consumption, or later pose output.
3. Differences only in raw pointer/root index/activation serial, which are
   allocator/activation noise rather than semantic identity.
4. Inconsistent repeated reads during transition or teardown; such samples must
   be discarded and cannot authorize a write.

### Independent static evidence

1. Writer/consumer/lifetime paths converge on `{instance+0x130,+0x138}`:
   `0x1418B24F0` selects and stores the root index, `0x1418A5246` resolves it in
   steady `AnimStateInstance::PhaseUpdate` before root vslot `+0x68`, and
   `0x14189C710` consumes the same pair during teardown before writing `-1`.
2. `SnapshotNodeHeap_ResolveNodeByIndex 0x1418C2F90` exposes the exact layout and
   source assertion: `maxRecordCount@+0xAC`, records pointer `@+0xA0`, 40-byte
   records, root pointer at record `+0`. Both invalid-index paths assert and
   debugbreak, so the probe duplicates only the reads after its own bounds checks.
3. MSVC RTTI was validated on two independent current-binary classes. Their
   vtables lead through `vtable[-1]` to signature-1 CompleteObjectLocators whose
   `pSelfRva` resolves back to the COL and whose `pTypeDescriptorRva` resolves to
   the expected names. The probe parses PE `SizeOfImage`, verifies the same
   invariants, and records only RVAs.
4. `0x1418B3F10` allocates/copies the authored uint32 key list and stores it at
   `+0x68` with count `+0x88`; `0x1418A4030` can rewrite the live apReference key.
   Pointer/count/hash are therefore reread and accepted only when stable.
5. The pointer at instance `+0x148` is passed to `0x1418B24F0` as its override
   table. `AnimStateOverrideTable_FindStateRecord 0x141926AE0` scans enabled
   24-byte records backward; `SortedIdPointerTable_Find 0x141764B50` binary
   searches state `+0x98` in `count@+0`, keys `@+0x8`, values `@+0x10`. `eh`
   reproduces this lookup twice, compares both fingerprints/results, and marks
   unstable results invalid for analysis.

### Read-only implementation and falsifier

Build `2026-07-21eh` emits at most 16 `AIM-BASE-CONFIG` samples per screen at
150 ms spacing and includes system `tick` for cross-process alignment. Fields
are state/callback/flags, six scalar values, mode `+0x18C`, apReference `+0x194`,
non-diagnostic serial `+0x188`, key count/hash/valid, override
count/hash/outer-slot/inner-index/valid, and root
index/vtable-RVA/TypeDescriptor-RVA/node-valid/RTTI-valid/heap-max.

The probe calls no animation, root, override, script, snapshot, or pose function.
It performs no writes. It caps keys at 4096, outer overrides at 1024, and inner
keys at 4096, checks every span, keeps the existing outer SEH boundary, and
rereads mutable owner chains before accepting a sample.

Prediction:

- stable different root vtable/TD RVAs at aligned ticks selects concrete root
  mismatch;
- equal RTTI with different key hash or override hit selects authored routing;
- full stable equality rejects both and moves the trace below root vslot `+0x68`;
- differences only in root index/serial are explicitly non-diagnostic;
- invalid repeated reads indicate transition/teardown only;
- any crash, model damage, or visible behavior change invalidates `eh`.
