# Initial puppet aim-ray latch (2026-07-15)

## Build dl runtime result

Build dl removed the final FgAnim session ramp by initializing a new ADS session
directly at the peer target yaw. The user reports no improvement: the puppet still
raises the weapon toward the old point, its shot goes to that same point, and only
then does the character turn toward the current peer reticle. This **REJECTS**
FgAnim world-yaw initialization as the primary owner of the initial aim ray.

## Precise hypothesis

The receiver's caller-scoped pose-seed substitution at
`ResolveScreenRoutePoseSeedOrDefault 0x141C077B0`, return site `0x140CB17D6`
inside `PlayerAimController_GetTargetPointFromReticle 0x140CB1720`, opens too
late. `hooked_pose_seed_resolver` currently requires
`puppet_aim_pose_relay_enabled_for_peer`, which consists of the 250 ms aim-button
dwell, a stable GunMove-family action for 100 ms, and
`g_puppet_render_aim_yaw_ready`. During that window native screen 1 supplies its
stale target. The aim controller and an early shot therefore consume the same old
point before the late render/pose gate opens.

Build dm will substitute the fresh peer pose seed immediately at **only**
`0x140CB17D6` while a strict peer ADS packet is present. It will continue to gate
the full 32-byte `Player_PostAnimUpdate` copy at `0x1410E934B` behind the existing
action/render admission. The broad screen-1 resolver/source override remains off.

## Independent evidence

1. **Native call/data flow:** `GetTargetPointFromReticle 0x140CB1720` calls the
   screen pose-seed resolver at return site `0x140CB17D6` and derives the world
   target consumed by the visible aim controller. The sole fire builder
   `0x140E2BCD0` independently resolves the same per-screen seed before
   `PlayerFireController_Fire 0x140E2C730`.
2. **Current source gate:** packet publication copies a fresh screen-0 route seed
   on every strict-RMB state packet, but the receiver's aim-site substitution calls
   `puppet_aim_pose_relay_enabled_for_peer`. That gate deliberately waits for
   button dwell, GunMove dwell, and render-yaw readiness, so transport is available
   before target substitution is admitted.
3. **Structural/runtime discrimination:** build dk proved a nearly 180-degree stale
   Player root snapshot but changing it had no visible effect. Build dl then
   removed the exact 0.75-second FgAnim yaw ramp and again had no effect. The shot
   following the old point, rather than the later corrected body heading, selects
   the earlier screen target/ray path over both root/action and final render yaw.
4. **Historical safety result:** broad idx-1 resolver/source writes froze locomotion,
   while the exact `0x140CB17D6` caller site was identified specifically to avoid
   that shared-camera side effect. Build dm changes no other resolver caller.

## Competing hypotheses

1. **Sender seed is stale:** screen-0 route seed itself may lag physical RMB or
   camera motion. This predicts that an early receiver substitution is delivered
   but its stock-to-peer quaternion dot remains near one and the wrong point does
   not change.
2. **Fire resolver gate is not reached:** native puppet fire may bypass the
   `0x140E2BCD0` owner guard or consume a previously cached target downstream.
   This predicts an early-target marker but no fire-path resolver marker when the
   wrong shot occurs.
3. **A separate target cache latches once:** `GetTargetPointFromReticle` may be
   called with the fresh seed but a downstream action stores the first native
   point until transition completion. This predicts both target override delivery
   and a materially different peer seed without an immediate visual/shot change.
4. **Late FgAnim/body ownership:** rejected as primary by build dl. It can still
   explain foot turning and gait, but not a shot that follows the old target.

## Build dm falsifiable prediction

On the first peer ADS packet, before GunMove/render admission completes, the
receiver must emit one bounded `AIM-TARGET-EARLY` marker. The first native puppet
shot must emit `AIM-FIRE-SEED`; both markers include the resolver index, caller and
stock-to-peer quaternion dot.

If the late target gate is the cause, the puppet's weapon begins in the current
peer reticle direction and a shot fired immediately after RMB goes there, without
waiting for the one-second body correction. Run-first ADS must retain the build-dl
transition behavior and location reload must not reproduce kneeling or frozen
locomotion.

No early marker rejects the `0x140CB17D6` delivery assumption. A marker with no
behavior change selects sender staleness or a downstream target latch. A correct
visual target but wrong shot, especially with no `AIM-FIRE-SEED`, splits the fire
path from the aim-controller target and requires tracing the actual relayed-button
fire caller before any broader camera write.

## Build dm runtime rejection

Receiver PID 18764 emitted both expected markers:

- `AIM-TARGET-EARLY idx=1 site=0xcb17d6 stockPeerDot=0.974651 poseReady=0`;
- `AIM-FIRE-SEED idx=1 site=0xe2bfff stockPeerDot=0.928003`.

The user nevertheless reports unchanged behavior: the puppet jerks and turns
before reaching the correct aim direction, then returns to its original heading
as soon as RMB is released. The early target and native fire resolver substitutions
are therefore **CONFIRMED delivered but REJECTED as the visual turn owner**.

The release behavior exposes the actual ownership split. The current
`hooked_fganim_publish_world_transform` replaces the puppet publication only while
`peer_packet_aim_active` and staged action admission are true. On the first
non-ADS publication it clears `smoothed_yaw_valid` and calls the native publisher
with the old stock transform. Nothing commits the final visible heading, so the
model necessarily returns to its pre-ADS publication.

## Build dn hypothesis: symmetric FgAnim world quaternion relay

The sender and receiver already execute the same confirmed render boundary:
animation-manager callsite `0x141A8FC00` calls
`FgAnimData_PublishWorldTransform 0x1418C55D0`, whose quaternion input is the
second float4 of the 32-byte transform and whose output `FgAnimData+0x1C0` is read
by `FgDrawManager_PrepareInstanceForRendering 0x14181A9B0`.

Build dn will capture the local player's quaternion at this exact sender callsite,
relay it in StatePacket v9, and substitute it for the puppet at the same receiver
callsite whenever the packet is fresh. This removes camera-axis derivation,
parent/local-root conversion and the ADS-only lifetime. It does not write
`Player+0x140`, derived transform `+0x240/+0x280`, movement input, or broad screen-1
camera state.

### Independent evidence

1. **Render dataflow:** `0x1418C55D0` publishes the world transform consumed by
   the draw manager; builds da onward proved changing only its quaternion visibly
   turns the remote model.
2. **Exact source lifetime:** the current hook applies camera-derived yaw only
   under peer ADS/action admission and explicitly resets on the first publication
   outside that condition. The observed RMB-up return is the direct predicted
   consequence.
3. **Runtime exclusion:** dm delivered early reticle and fire seeds with materially
   different stock/peer quaternions, yet neither removed the visual turn/return.
   Builds dk/dl independently rejected action-root snapshot and FgAnim session
   initialization as sufficient.
4. **Safety discrimination:** sustained pre-derived `Player+0x140` writes caused
   kneeling after lifecycle restart and remain parked. A same-boundary FgAnim
   publication relay changes render world orientation only and leaves native
   locomotion/collision ownership intact.

### Competing hypotheses

1. **Authored action transition owns the spin:** exact world quaternion relay may
   remove the world-heading excursion while the upper-body GunMove transition
   still jerks. That outcome moves the next trace to the action blend, not root.
2. **Sender FgAnim publication is not the local visible owner:** a missing capture
   marker or invalid quaternion rejects the retained local Player+0x1A8 identity.
3. **20 Hz packet stepping:** exact samples may follow correctly but step. This
   selects interpolation at the same render boundary after ownership is confirmed.
4. **Persistent Player root is required:** rejected as the visible ADS owner by
   root-snap runtime, but it may still affect later locomotion. The build deliberately
   avoids it until symmetric FgAnim delivery is tested.

### Build dn falsifiable prediction

Both processes must emit one `FGANIM-WORLD-SEND` marker and the receiver must emit
`FGANIM-WORLD-APPLY`, including stock-to-peer quaternion dot and ADS state.

If asymmetric camera-derived publication is the cause, the puppet mirrors the
sender's native body turn without the apparent full spin, continued camera changes
remain aligned, and RMB release no longer returns it to the old heading. Normal
running and location reload remain free of kneeling because no locomotion/root
field is written.

No sender marker rejects local FgAnim identity. Markers with unchanged world turn
select the authored upper-body/action transition. Correct direction with visible
20 Hz stepping confirms ownership and calls for bounded interpolation, not another
input or root writer.
