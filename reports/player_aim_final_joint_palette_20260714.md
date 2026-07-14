# Player aim final joint-palette boundary (2026-07-14)

Status: **STATIC; read-only runtime discriminator pending**.

## Exact hypothesis

`FgAnimData+0x230` is the current blended 48-byte-per-joint matrix palette and
`FgAnimData+0x238` is the previous palette. The proven remote `aim-add-*` values
either fail to change the final upper-body matrices, or they do change those
matrices and a later world/DrawControl transform keeps the rendered puppet facing
one point.

## Competing hypotheses

1. The animation command evaluator does not consume the puppet's changing
   `aim-add-*` parameters, so no stable subset of final joint matrices follows
   camera yaw.
2. The final palette follows camera yaw, but the renderer combines it with a
   stale world/root transform owned after Player root and GunMoveController.
3. The current palette changes but DrawControl consumes the previous or another
   palette generation.
4. Player post-blend IK overwrites the aim joints. This is weakened by the exact
   `CharacterLegIkController` identity at `Player+0x1DC0`, but remains testable if
   changes disappear specifically after phase `+0x408`.

## Independent static evidence

1. **Exact source and data layout.** `AnimManager_PerformAnimBlend 0x141A92260`
   carries the `PerformAnimBlend(const AsyncUpdateJobData *)` assertion. Its
   fallback allocates `48 * joint_count`, writes three identity `float4` rows per
   joint, moves `FgAnimData+0x230` to `+0x238`, and publishes the new buffer at
   `+0x230`.
2. **Independent normal-path data flow.** `0x141A91490`, `0x141A91660`, and
   `0x141A91D20` all load `FgAnimData+0x230` into blend-command contexts that
   reach per-layer executor `0x141AB97C0`. The full async job
   `0x141A92A00` performs the same current/previous swap in its fallback.
3. **Upstream field identity.** `PlayerAimController_PublishDirectionalAimWeights
   0x140CB41F0` writes the four raw values to
   `[[Player+0x1A0]+0x58]+0x10 + 0x188..0x194`. Independent Player slot `+0x6F8`
   analysis identifies `Player+0x1A8` as the active upper/FgAnimData payload with
   the same field band.
4. **Runtime structural agreement.** Prior read-only testing confirmed both local
   and puppet `Player+0x1A8` objects have phase bits `0x38`, matching retained
   owners, `FgAnimData+0x10 == Player+0x1A0`, and non-null runtime payloads. An
   absent puppet animation tick is already rejected.
5. **Post-blend alternative resolved.** Player vtable `+0x408` is exact-source
   `Player_PostAnimBlending_Async 0x1410EB6A0`; its `Player+0x1DC0` controller is
   constructed with `CharacterLegIkController::vftable`. Player `+0x418` is only
   a short resource release and does not write the palette.

## Falsifiable read-only prediction

While the remote puppet stands still in ADS, sample `FgAnimData+0x230/+0x238`
only after validating both pointers and the skeleton joint count. On each fresh
peer camera sequence, report the root matrix plus the joint indices with the
largest finite current-versus-previous matrix delta.

- If a repeatable upper-joint subset changes with camera yaw while the rendered
  pose remains fixed, hypothesis 2 or 3 wins and the next trace follows the palette
  consumer/DrawControl world transform.
- If camera yaw and published weights change but no joint subset responds beyond
  ambient motion, hypothesis 1 wins and the next trace is the exact `aim-add-*`
  parameter lookup/evaluation inside `0x141AB97C0`.
- No palette or Player field is written by this discriminator.

## Read-only build under test

`log_puppet_joint_palette_motion` samples only a validated slot-1
`FgAnimData+0x230` palette during fresh peer ADS. It keeps one private 400 ms
baseline, reports the six joint indices with the largest 12-float matrix delta,
the root row, peer sequence, raw directional weights, and blend. It never writes
the palette, FgAnimData, Player, DrawControl, or animation parameters. Release
build succeeds; only the pre-existing C4456 diagnostics remain.
