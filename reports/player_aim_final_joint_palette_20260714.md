# Player aim final joint-palette boundary (2026-07-14)

Status: **RUNTIME INCONCLUSIVE; latest run selected an upstream dispatch failure**.

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

### First runtime: inconclusive guard failure

PIDs 16724/1964 produced changing sender pose seeds, receiver world-basis values,
and directional weights, but no `AIM-PALETTE` line. Therefore the camera/ADS
stimulus was valid while at least one palette-layout guard rejected the sample.
This does not select any behavioral hypothesis. The next read-only build adds a
once-per-second `AIM-PALETTE-STATUS` line naming the failed guard and reporting
only pointer addresses, joint count, and readable span.

### Guard result

PIDs 16828/17052 identified the sole rejection as `stage=joint-count`:
the live Chloe skeleton has 651 joints, above the probe's conservative 512-joint
private-buffer limit. `FgAnimData`, skeleton, current palette, and previous palette
were all non-null and stable in role across changing allocations. The probe limit
is raised to 1024; no game-memory layout or behavior changes.

### 651-joint runtime result: palette branch not reached cleanly

PIDs 200/13948 passed every layout guard and emitted repeated `AIM-PALETTE`
samples while the sender camera seed and receiver `AIM-IK-POINT` changed. The
receiver's four directional fields nevertheless remained exactly
`weights=(0,0,0,0)`, and this PID pair emitted neither `AIM-WORLD-BASIS` nor
`AIM-WEIGHTS-POST`. `AIM-BLEND-TICK` was active and commonly reached one, so
packet ADS and the generic blend controller were alive while the directional
GunMoveShield chain was absent.

The full-palette difference is not a valid aim discriminator in this run. Of 651
entries, the finite count varied between 497 and 651; changing palette
allocations exposed entries with implausible magnitudes up to `1.0e19`, while
other frames produced ordinary aggregate deltas. A valid-joint/active-layer mask
would be required before assigning those indices to rendered bones. More
importantly, the upstream zero weights make that refinement premature.

The user simultaneously observed Chloe sliding in a kneeling pose. This probe is
strictly read-only, so it cannot be the direct writer of that state. Current
competing hypotheses are:

1. the automatic stationary `aim-press` relay admits ADS during a native
   movement/correction transition, switches the puppet to a kneeling action set,
   and the action selector no longer dispatches `Player::GunMoveShield`;
2. `Player::GunMoveShield` still dispatches, but an early action/movement gate
   bypasses the `PlayerAimController+0x58` calls;
3. a location/progress lifecycle leaves another gun-move action active and the
   released synthetic ADS command does not restore the normal state.

Packet dropout is weakened: the palette logger itself requires a fresh strict
peer-ADS packet, and it sampled continuously. A final-render owner is not selected
by this run because no directional command reached the palette producer. The next
step is static tracing of the action selector and early exits around
`PlayerGunMoveShield_UpdateAndDispatch 0x140E84440`; no further runtime test is
earned yet.
