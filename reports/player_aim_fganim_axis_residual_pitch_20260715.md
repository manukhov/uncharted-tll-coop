# ADS FgAnim axis and residual pitch basis (RUNTIME + STATIC, 2026-07-15)

## Observed result of build da

The corrected translation/quaternion layout made the late phase-`0x40` relay
visible and stable: during ADS the remote model now turns diagonally as the peer
camera moves. The user observed three precise remaining failures:

1. horizontal facing is exactly 180 degrees opposite the reticle;
2. vertical camera motion produces no visible pitch;
3. entering ADS can expose a folded/broken transition pose.

This runtime result confirms `0x141A8FC00 -> 0x1418C55D0` as the visible body-yaw
boundary and rejects stale render publication as the remaining yaw cause.

## Selected hypothesis

The symptoms have two independent owners. The render-world quaternion uses the
opposite horizontal axis at this FgAnim boundary. Meanwhile
`PlayerAimController_ComputeDirectionalAimWeights 0x140CB32B0` still compares the
camera target against GunMoveShield's stale body basis, so horizontal aim saturates
while the late world publication turns the visible body. The controller should
receive the normalized horizontal projection of the relayed camera `+Z`; the full
target retains `Y`, leaving pitch as the only additive residual.

The occasional folded entry has a separate conflicting writer: the mod writes
peer pose output to `Player+0x1720/+0x1730` both before and after native update,
although `Player_PostAnimUpdate 0x1410E9210` recomputes those fields every frame.
Those manual output writes must remain parked.

## Competing hypotheses

1. **Selected:** invert only the late FgAnim horizontal quaternion, align the aim
   controller's logical body basis with camera-horizontal `+Z`, and park direct
   pose-output writes.
2. Preserve the native body basis and change only the render sign. This fixes the
   180-degree body error but leaves the known 60-degree horizontal saturation, so
   it predicts no reliable vertical additive.
3. Put camera pitch into the FgAnim world quaternion. Rejected because it tilts the
   complete body and attachments rather than driving authored upper-body aim.
4. Continue writing `Player+0x1720/+0x1730`. Rejected by prior runtime as inert and
   by the exact native writer; these are output snapshots, not controller inputs.
5. Remove the FgAnim aim-add mirror. Not selected yet: prior runtime showed the
   controller/publish state changing while render-bound FgAnim fields stayed stale.

## Independent evidence

1. **Runtime boundary and axis discriminator.** Build da modifies only the remote
   FgAnim world quaternion during fresh ADS. The visible model now turns, proving
   the selected publication reaches rendering. Its exact 180-degree error selects
   an axis/sign correction, not stale packets or a missing render consumer.
2. **Directional-weight data flow.** Slot `+0x58` receives target and body-forward;
   `0x140CB32B0` normalizes `target-origin`, computes signed angles against that
   body-forward, and publishes left/right/up/down through `0x140CB41F0`. Previous
   runtime recorded right saturation at one when relative yaw exceeded 60 degrees.
3. **Native target convention.** `PlayerAimController_GetTargetPointFromReticle
   0x140CB1720` expands route quaternion local `+Z`. The relay's full target uses
   the same convention, so its normalized XZ projection is the matching logical
   body basis while its Y component remains the pitch residual.
4. **Independent output writer.** `Player_PostAnimUpdate 0x1410E9210` resolves the
   pose at return `0x1410E934B` and stores it to `Player+0x1720/+0x1730` every
   frame. Prior direct-write runtime was inert. Pre/post external writes can only
   race evaluation and are not a valid vertical control surface.

## Build db prediction

Build `2026-07-15db` negates only the XZ direction passed to the FgAnim yaw helper,
passes normalized camera-horizontal `+Z` as slot `+0x58` body-forward, retains the
full camera target and native vertical limits, and removes both calls that write
`Player+0x1720/+0x1730`. It emits one bounded marker after the first sample with
`abs(cameraY) >= 0.10`.

Predicted visible result: the remote faces the reticle rather than away from it;
left/right camera motion turns the whole body; up/down motion changes upper-body
aim; ADS entry no longer folds. If the marker reports non-zero up/down weights but
vertical pose remains absent, the blocker is below the controller in FgAnim
parameter consumption. If entry still folds after the output writes are parked,
the remaining owner is ADS/action-state forcing rather than pose-seed output.

## Build db runtime result

**CONFIRMED:** horizontal render direction is now correct. The phase-`0x40`
quaternion sign correction is complete.

**REJECTED as a complete fix:** vertical camera motion remains invisible, ADS entry
still occasionally folds, and moving while the peer holds ADS either desynchronizes
or slides with mismatched leg motion. The one-shot receiver marker captured
`cameraY=-0.23907`, raw controller `up=0.543157`, `right=1`, and `blend=0`.
Therefore camera/pitch transport and directional computation are alive; this sample
was taken before authored blend admission and cannot yet prove a blended pitch loss.

### Static correction for build dc

Three independent facts identify the folded transition:

1. `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0` stores raw LRDU
   at controller `+0x8B8..+0x8C4` and active-state `+0x188..+0x194`.
2. The same function separately multiplies every `aim-add-*` channel by
   controller blend `+0x8EC` before publication.
3. The mod mirror preferred raw controller values, skipped zero sources, and never
   cleared FgAnim fields on ADS release. It therefore bypassed easing and retained
   a saturated pose across transitions.

Build `2026-07-15dc` mirrors `raw * clamp(blend,0,1)`, treats valid zero as a real
source, and clears FgAnim aim-add fields when the stationary pose relay closes. A
second bounded marker records the first non-trivial pitch sample with blend at least
0.85, resolving the remaining vertical competitor without periodic logging.

The gait defect has a separate, exact ownership mismatch. Native puppet aim input
and pose relays are intentionally disabled above `move_magnitude=0.08` or horizontal
velocity `0.35`, following prior kneeling-slide runtime. Build db nevertheless
overrode the late FgAnim world yaw for every strict peer ADS packet, rotating normal
locomotion as though it were an authored aim-walk state. Build dc gates slot `+0x58`,
the mirror, and the world-yaw publication by the same stationary predicate. While
moving, native locomotion owns the complete pose and world transform.

**Prediction:** stationary ADS keeps the confirmed direction, enters smoothly, and
produces a `Aim blended-pitch sample` marker if the controller sustains vertical
weights. Starting movement releases the visual ADS pose instead of sliding or
desynchronizing. If that high-blend marker contains non-zero up/down but no visible
pitch, the remaining blocker is strictly below native parameter publication.

## Build dc runtime result and build dd transition gate

**CONFIRMED:** horizontal camera-relative facing remains correct. Receiver PID
15604 recorded `blend=0.866667`, `up=0.167019`, `right=0.630614` while camera
`y=-0.603211`. Camera pitch transport, directional-weight computation, and native
blend publication are therefore all live at the same sample.

**RUNTIME REJECTED:** copying those blended values directly to FgAnimData
`+0x188..+0x194` produced no visible vertical pitch. The direct mirror is not the
rendered vertical consumer and is parked in build dd. The remaining vertical
failure is downstream of `PlayerAimController_PublishDirectionalAimWeights
0x140CB41F0`, in authored parameter/palette consumption.

The user also isolated the transition discriminator: entering ADS while already
idle is clean, but entering immediately after running folds; holding ADS while
walking makes the remote aim at one stale point. Static audit found four parts of
the same mod-owned controller chain still admitted every fresh peer ADS packet:

1. `hooked_player_aim_blend_tick` kept forcing controller `+0x8A0/+0x8EC`.
2. The eligibility callsite kept converting native false to true.
3. The additive-permission callsite kept converting native false to true.
4. The blend-target callsite kept converting the native target to one.

Build dc had already disabled input, slot `+0x58`, FgAnim mirroring, and render
yaw during movement. The four ungated hooks consequently held an internally active
ADS controller with its last stationary direction while locomotion owned the
visible body. This exactly predicts the reported fixed point.

Independent action-state runtime provides the transition evidence: retained-puppet
ADS settles to authored `GunMove` hash `0x12D842C0`, but a short
`0xE1939F43` state appears for one or two samples on entry. A single low-speed
network packet therefore cannot prove that the preceding locomotion graph has
finished blending.

### Competitors

- Keep the controller forced while moving and hide only render yaw: rejected by dc
  because it produced the stale fixed point.
- Relay full moving aim input: rejected by the confirmed kneeling-slide runtime.
- Clear arbitrary action or pose fields on movement: rejected because these are
  native outputs and would race the graph again.
- Gate the complete mod-owned ADS chain behind stable stationary dwell: selected.

### Build dd prediction

One shared predicate now gates aim input, eligibility, additive permission, blend
tick/target, slot `+0x58`, world-basis substitution, and late FgAnim yaw. Movement
or aim release resets it immediately; ADS is admitted only after 250 ms of stable
stationary packets. The last forced `Player+0x1548` ADS flag is also cleared while
peer ADS remains held but the stationary gate is closed.

Prediction: run-then-aim waits briefly and enters without folding; movement while
holding ADS no longer retains the old aim point and native gait owns the complete
pose; stationary horizontal direction remains correct. Vertical pitch should stay
absent, which would preserve the now-confirmed boundary below native aim-channel
publication rather than falsify this transition fix.
