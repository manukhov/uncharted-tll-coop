# Puppet aim authored target, body basis, and visibility (2026-07-13)

Status: **RUNTIME PARTIAL; authored blend confirmed, direction/visibility superseded**.

## Exact hypotheses

1. `PlayerAimController_GetAuthoredAimBlendTarget 0x140CAE480` returns zero for the
   puppet's active base state. Replacing only that zero result at callsite
   `0x140CAFB7C` during fresh peer ADS with the native default target `1.0` will let
   `+0x8EC` remain positive and expose the directional aim animation.
2. GunMoveShield computes `bodyForward` before the mod's post-update root correction,
   so its basis can disagree with the visible network body. Supplying positive-Z
   from `StatePacket::quat` makes the signed additive angles use the sender's actual
   body basis.
3. Main-view exclusion is not scoped-art-specific. Clearing DrawControl child mask
   `0x40` during any automatic puppet ADS interval prevents the observed pistol-ADS
   disappearance too.

## Competing hypotheses

- A later animation phase overwrites valid positive blend and directional weights.
- The relayed camera quaternion uses a different handedness despite the statically
  confirmed route-camera `-Z` convention.
- Disappearance is caused by another render/animation flag written after the
  post-update DrawControl clear.

## Independent evidence

1. Runtime PIDs 5092/7028 repeatedly had `request=1`, positive pre-tick blend up to
   `0.595298`, and exactly zero post-tick blend. This selects the authored-target
   branch, not a missing producer or missing tick.
2. `PlayerAimController_UpdateBlendState 0x140CAFA20` has exactly one direct source
   for its target (`0x140CAE480` at callsite `0x140CAFB7C`), while the four final
   `aim-add-*` writes are all multiplied by `+0x8EC`.
3. `Quaternion_RotatePositiveZ 0x14014D9F0` is the stock GunMoveShield body-forward
   producer. Slot `+0x58` compares that body basis against the target direction;
   `StatePacket::quat` is independently the sender Player root quaternion already
   used by the confirmed facing correction.
4. The root quaternion is asserted in `apply_puppet_transform_post`, after native
   Player/GunMoveShield update. Therefore the incoming body-forward argument can be
   stale even when the final visible root faces correctly.
5. The disappearing test's sender art was pistol `0x33E29A1B`; no scoped-only
   main-view-clear log appeared. The registered draw-in-main-view command itself is
   generic and manipulates DrawControl child bit `0x40` without checking weapon art.

## Implementation

- Patch only the `E8` at `0x140CAFB7C`, verifying target `0x140CAE480`. Preserve
  every positive native result; return `1.0` only for retained slot 1, screen 1,
  fresh peer ADS, and a zero/non-finite original result.
- In the existing slot `+0x58` hook, preserve the relayed camera target but replace
  body-forward with normalized `StatePacket::quat * +Z`, matching the stock helper.
- Apply `DrawControl_ClearChildInstanceFlagsByMask(draw, 0x40)` for any automatic
  stationary puppet ADS, not only arts classified as scoped rifles.

## Runtime prediction

Receiver emits `AIM-BLEND-TARGET original=0 forced=1`, then blend-tick samples with
positive post-tick values instead of `...->0`. `AIM-WORLD-BASIS` includes a relayed
`body=(...)`; the visible upper-body aim follows sender camera relative to the
correctly facing body. `PUPPET-ADS-MAIN-VIEW cleared instance flag 0x40` appears for
both pistol and rifle ADS, and the puppet remains visible. If blend is positive but
pose is absent/wrong, the remaining blocker is downstream graph consumption or
camera-quaternion convention; if clear logs appear while the mesh disappears, the
remaining writer is later than this post-update phase or uses another flag.

## Runtime result

The user confirmed visible pistol aim and a retained sniper aim pose, establishing
that the zero authored-target relay exposes the animation. Root yaw still did not
follow the camera because the post writer continued to use `StatePacket::quat`
(sender body), not the independently relayed camera quaternion.

The generic main-view clear was only partially sufficient: the scoped body and
animation remained, but hair and the held rifle disappeared. This rejects a single
whole-Player `0x40` cull as the complete mechanism. The next static implementation
intercepts the per-object flag writer and restores distinct per-mesh visibility on
validated Player/Weapon DrawControls; see
`reports/aim_camera_yaw_scoped_renderables_20260713.md`.
