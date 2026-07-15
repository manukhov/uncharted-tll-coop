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
