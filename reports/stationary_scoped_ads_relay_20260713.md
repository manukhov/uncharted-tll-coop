# Stationary scoped ADS relay (2026-07-13)

Status: **RUNTIME REJECTED as sufficient**.

## Hypothesis

Allowing the existing automatic aim-button relay for a nearly stationary scoped
rifle will enter the native GunMoveShield/PlayerAimController path, while retaining
the existing movement threshold avoids the previously rejected kneeling-slide
locomotion conflict. Clearing DrawControl child flag `0x40` during this same scoped
ADS interval should prevent Chloe from disappearing from the receiver's main view.

## Competing hypotheses

1. Stationary scoped `0x200` reaches slot `+0x58`, raises `+0x8EC`, and displays
   the native aim animation without hiding the mesh.
2. `0x200` reaches the controller but the authored blend or animation graph still
   suppresses the pose.
3. The `DrawControl 0x40` clear is too early or incomplete and the scoped puppet
   still disappears.
4. Even stationary scoped ADS corrupts locomotion/weapon state independently of
   the previously observed moving-ADS conflict.

## Independent evidence

1. Runtime PIDs 18500/17476 had strict peer ADS and a live pose seed, but no
   `AIM-WORLD-BASIS`; logs simultaneously showed scoped art `0x8075397D` and
   `SCOPED-ADS-SCRUB` every second.
2. `puppet_aim_press_relay_enabled_for_peer` explicitly rejected
   `peer_holds_scoped_rifle(peer)` before its stationary movement/velocity checks.
   Thus the native aim producer was intentionally disabled for this weapon class.
3. Non-scoped admission has already reached slot `+0x58` and the permission call,
   proving the downstream controller is mounted when native `0x200` is present.
4. Script command `set-instance-flag-draw-in-main-view` proves scoped visibility is
   controlled by DrawControl child-instance mask `0x40`; exact inverse
   `DrawControl_ClearChildInstanceFlagsByMask 0x141754FF0` is already installed.
5. The earlier kneeling-slide rejection used sustained full ADS while moving. The
   automatic relay separately rejects `move_magnitude > 0.08` and horizontal
   velocity above `0.35`, providing a distinct stationary-only test.

## Implementation

- Remove only the scoped-weapon exclusion from the automatic stationary aim-button
  predicate; retain every freshness, movement, and velocity gate.
- Disable the legacy per-frame scoped ADS scrub by default.
- Run the proven DrawControl main-view flag clear whenever the same stationary
  aim-button predicate is active, rather than only under the broad manual F8 relay.
- Leave broad moving/full ADS (`g_puppet_relay_aim_press`) off by default.

## Runtime prediction

Stationary scoped aim emits `AIM-WORLD-BASIS`,
`AIM-ADDITIVE-PERMISSION` when the authored state rejects it, then
`AIM-BLEND-TICK request=1->0 blend=0->positive`. It also emits
`SCOPED-MAIN-VIEW cleared instance flag 0x40`, not `SCOPED-ADS-SCRUB`. Chloe stays
visible and raises/follows the rifle. Beginning to move drops automatic `0x200`,
so the blend returns toward zero without the old kneeling slide.

## Runtime result

PIDs 5092/7028 reached the complete native path: `AIM-WORLD-BASIS` and
`AIM-ADDITIVE-PERMISSION original=0 forced=1` appeared repeatedly. The blend-tick
probe then showed the decisive transition, for example
`request=1->0 blend=0.595298->0`. The authored blend target is therefore zero for
the puppet's active base state; admission alone cannot keep the pose weighted.

The sender art in the disappearing interval was pistol `0x33E29A1B`, so the
scoped-art-only DrawControl clear did not run at all. The user observed both wrong
direction and disappearance. This rejects hypotheses 1 and 3's narrow scoped-art
assumption and motivates three independent corrections documented in
`reports/player_aim_authored_target_body_visibility_20260713.md`.
