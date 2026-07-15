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

## Build dd runtime and build de aim-session latch

**PARTIAL CONFIRMATION:** the stale fixed aim point is gone while moving, proving
that the split controller ownership identified after dc was real. **REJECTED as a
complete gait fix:** dd closes the level-sensitive stationary gate as soon as
movement begins, so the puppet exits ADS and runs normally even though the sender
continues holding RMB.

This distinguishes two operations that dd incorrectly combined:

1. Admission into ADS must remain stationary and delayed, because run-to-ADS is the
   folded transition and broad moving `0x200` previously produced kneeling slide.
2. An already completed ADS session must remain admitted during movement, because
   `GunMoveController` explicitly selects authored `*player-ap-strafe-*-set*`
   graphs and consumes movement speed (`speed-gunmove`) inside the same GunMove
   dispatcher.

The old default-on `0x200` rejection did not distinguish these phases: it injected
ADS globally while locomotion was already active. It therefore does not reject
movement after a clean stationary GunMove transition.

### Competing hypotheses

- Restore all moving ADS immediately: rejected because it recreates the old
  run-to-ADS admission path.
- Keep dd's stationary level gate: rejected by the user's no-aim running result.
- Latch at the first admitted frame: weakened because the controller/action blend
  is still at entry and can reproduce the folded pose.
- Latch only after the stationary controller blend completes: selected.

Build de keeps the 250 ms stationary admission dwell. The existing 180 ms
`force_peer_puppet_aim_blend_state` ramp sets a session latch only at blend at least
0.95. Once latched, the complete ADS chain remains enabled through movement until
RMB release. A gap longer than 500 ms resets both dwell and latch; the latch is
also bound to the retained Player pointer and resets when that object disappears or
changes, so a location transition cannot inherit the previous ADS session. Manual
F8 remains isolated and cannot seed the automatic latch.

**Prediction:** after holding ADS while stationary for roughly half a second, the
puppet continues aiming when movement starts and uses its GunMove locomotion rather
than plain running. Run-first-then-aim remains blocked until the sender stops, so
the folded entry and historical kneeling-slide admission path do not return. RMB
release exits ADS normally; location reload cannot retain the latch. Vertical aim
remains unchanged and downstream.

## Build de runtime and build df staged run-to-ADS

**PARTIAL CONFIRMATION:** a settled ADS session remains active during movement,
which confirms that the authored GunMove family supports locomotion after its
blend is established. Two defects remain: its feet slide relative to translation,
and pressing ADS after running never enters the session. The latter is the direct
prediction of build de's stationary-only admission timer, not a new native gate.

The gait-speed alternative was checked statically. `Player::GunMoveShield` vtable
slot `+0xD0` resolves to `PlayerGunMove_GetConfiguredMoveSpeed 0x140E86980`; the
common dispatcher calls it immediately before publishing `speed-gunmove`. It reads
`Player+0x3328`, but a displacement-wide audit found that field's only Player-range
write in `InitializeTopLevelPlayerOwnerFromProcessSpawnInfo 0x1410DC160` at
`0x1410DCB4D`. It is nominal spawn configuration, not a live speed channel, so
network-writing it is rejected.

### Competing hypotheses

- Mirror `Player+0x3328` as live gait speed: statically rejected by its single
  spawn-time writer and configured-speed consumer.
- Re-enable every pose hook immediately while running: runtime-rejected by the
  earlier folded/kneeling admission and does not distinguish action transition
  from an established GunMove session.
- Keep stationary-only admission: rejected by build de because run-first ADS can
  never start.
- Stage input/controller admission before render ownership: selected. The held
  `0x200` and eligibility/permission relays may move the native action machine out
  of Run, while world-basis, blend tick and late FgAnim yaw remain closed until a
  GunMove-family action hash is stable for 100 ms.

The moonwalk has a separate proportional cause. The pre-consumer path already
adds bounded error correction to `Player+0x3A30`, inside native locomotion. The
post-update path additionally translates world/local position by up to `0.05 m`
per update without advancing animation. That cap is the validated running value,
but GunMove publishes its own slower authored speed, so the same absolute nudge is
a larger fraction of ADS travel. Build df keeps `0.05 m` for ordinary running and
caps only an admitted aim-walk session at `0.015 m`.

### Build df prediction

Starting to aim while already running first keeps normal Run rendering, then
enters GunMove ADS after the held-button delay plus a stable 100 ms GunMove hash;
the transient state must never receive late FgAnim yaw. Starting from idle still
enters cleanly. During established aim-walk, feet should track translation more
closely while position continues converging; increased drift with improved feet
would select the nudge tradeoff, while unchanged sliding would move the fault to
the strafe direction/root basis. The historical folded or kneeling pose rejects
the staged admission. Vertical pitch is deliberately unchanged: the high-blend
channel already reaches native publication, and its remaining consumer is below
the FgAnim parameter boundary.

## Build df runtime and build dg turn/foot ownership

**PARTIAL CONFIRMATION:** staged admission fixes the previous run-first failure and
no longer produces the folded pose. The user reports that aim-walk is better, but
entry performs a sharp apparent full turn, turning in place slides the feet, and
moving aim-walk still only partly matches its foot cycle. The receiver captured a
high-blend pitch sample during the same healthy GunMove session, so these are not
an action-selection failure.

Three independent static facts select a late-yaw ownership conflict:

1. `hooked_fganim_publish_world_transform` replaces the published quaternion with
   the final camera yaw in one call. It has no shortest-arc/rate limit and cannot
   request a locomotion animation.
2. `GunMoveController_SelectAimIdleTurnState 0x140E8F720` compares controller
   `+0x140/+0x150` and returns states 3/4; builder `0x140E8D140` maps those states
   to authored `aim-idle-rotate` / `aim-idle-rotate-cc` actions.
3. `GunMoveController_ApplyCameraYawToMoveDirection 0x140E8F990` is called before
   state update and supplies `+0x150`, while the current FgAnim override happens
   after animation evaluation. The renderer and the foot/strafe graph therefore
   see different body headings in the same frame.

### Competitors and historical boundary

- Position nudge alone: weakened because df already lowered it to `0.015 m`, yet
  feet slide during pure turning in place where translation is zero.
- Re-enable the former GunMove relay as visible/root ownership: rejected by the
  2026-07-14 runs and later kneeling-slide lifecycle regression.
- Directly rotate Player root after update: rejected by visual runtime and
  lifecycle corruption.
- Use native GunMove only for action selection, retain the proven final FgAnim
  owner, and smooth that owner: selected. The relay is admitted only after the df
  stable-GunMove timestamp and performs no retained-table lookup. The old post-root
  synchronizer remains disabled.

### Build dg prediction

ADS entry follows the shortest horizontal arc at no more than 240 degrees/second,
so it cannot snap or take an apparent 360-degree path. Large camera turns while
stationary should select authored turn-in-place and visibly step the feet; moving
ADS should choose strafe steps closer to the rendered heading. Ordinary running,
pre-GunMove transition frames and lifecycle reload remain untouched. A return of
the kneeling slide rejects the narrowed native selector. Smooth body yaw with
unchanged feet would reject `+0x150` as an animation-state owner and leave the
remaining fault inside the strafe graph's movement-direction parameters.

## Build dg runtime and build dh render-basis discriminator

**PARTIAL CONFIRMATION:** rate limiting prevents the former malformed transition,
but the user still sees plain body rotation without foot stepping and strong slide
during aim-walk. Entry now exposes the exact ordering defect: the weapon first aims
in the old/wrong heading and the body immediately turns to the correct camera
heading. The native negative-Z `+0x150` relay therefore did not align the foot
graph with the final render owner.

The result resolves the axis ambiguity left by the older body-turn experiments:

1. `GunMoveController_UpdateAimMovement 0x140E8EE90` stores the normalized input
   direction at controller `+0x140` and its magnitude at `+0x160`.
2. `GunMoveController_SelectAimIdleTurnState 0x140E8F720` compares `+0x140` with
   `+0x150`; historical negative-Z runtime made the two converge and reported
   `yawPre=0`, which explicitly suppresses states 3/4 and foot turning.
3. The confirmed visible FgAnim boundary requires the opposite camera axis
   (positive Z). dg therefore left the graph at zero relative turn while rendering
   a different heading after animation evaluation.

### Build dh changes and competitors

- The `+0x150` selector now receives normalized peer camera positive-Z, matching
  the final rendered heading. This revisits the historically rejected axis only
  as an animation selector: it is not used for post-root sync or direct visible
  ownership.
- Render yaw begins after stable GunMove but before ADS pose publication. A new
  readiness latch opens blend/world-basis/weapon pose only when the smoothed body
  is within 12 degrees of the target, preventing the weapon from aiming backward
  during pre-turn.
- The remaining `0.015 m` post-transform ADS nudge is disabled completely. The
  already confirmed pre-consumer correction inside `Player+0x3A30` remains, so
  this distinguishes foot/root ownership from unanimated positional convergence.

Competing outcomes are explicit. If feet step on the spot but moving gait still
slides, turn-state selection is fixed and the remaining owner is strafe movement
parameters. If neither improves, `+0x150` is rejected as any visible foot-state
owner. If position drift grows while gait improves, the post nudge is confirmed as
the gait/convergence tradeoff. Kneeling slide or wrong compass direction rejects
the positive-Z selector despite the staged lifecycle guard.

### Build dh prediction

On a large initial camera offset, the body turns first and the weapon raises only
near the final heading; no backward aim frame is visible. Stationary rotation enters
authored foot turning instead of rotating planted feet. Moving ADS loses the extra
post-transform translation and should show materially less slide, at the cost of
possibly larger replica position drift. Vertical pitch remains unchanged.
