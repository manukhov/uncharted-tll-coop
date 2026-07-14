# Player aim render/palette discriminator (2026-07-14)

Status: **BUILD READY; runtime pending ADS session**.

## Context

PIDs 13148/21228 proved the full upstream GunMove chain: `AIM-DISPATCH-ENTRY`,
native eligibility, `AIM-WORLD-BASIS`, permission relay, and non-zero
`AIM-WEIGHTS-POST` after blend=1. User visual confirmation: receiver puppet still
aimed at **one fixed point** during ADS.

## Hypothesis

The break is between `PlayerAimController_PublishDirectionalAimWeights
0x140CB41F0` and visible skeleton orientation — either palette evaluation
(`0x141AB97C0`) or later world/render consumption.

## Competing hypotheses

1. **Palette eval failure:** `FgAnimData+0x188..+0x194` and controller weights
   publish, but `0x141AB97C0` does not drive upper-joint matrices in
   `FgAnimData+0x230`.
2. **Root/render owner:** additives and palette update, but `Player+0x140` root
   quaternion or DrawControl world transform stays fixed.
3. **Stale palette generation:** current palette changes but renderer reads
   previous `+0x238` or another generation.
4. **Post-blend IK overwrite:** `Player_PostAnimBlending_Async 0x1410EB6A0` leg IK
   clobbers aim joints (weakened by `CharacterLegIkController` identity).

## Read-only probes (implemented)

### `AIM-RENDER-DISC`

Samples when peer ADS active, peer yaw changes ≥2°, and
`sum(|ctrlAdd|) >= 0.01` or `blend >= 0.5`:

- `peerFwd` from packet pose-seed quat
- `rootFwd` from `Player+0x140` quat
- `peerYaw`, `rootYaw`, `yawDelta`
- `ctrlAdd` = `PlayerAimController+0x8B8..+0x8C4`, `blend` = `+0x8EC`
- `fgAdd` = `FgAnimData+0x188..+0x194`

### `AIM-PALETTE` (re-enabled, weight-gated)

400 ms cadence; reports top-6 joint matrix deltas vs private baseline when the
same weight gate passes.

## Falsifiable prediction

During ADS with peer camera sweeps:

| Observation | Selects |
|-------------|---------|
| `peerYaw` changes, `rootYaw` static, weights non-zero | Hypothesis 2 |
| `ctrlAdd` non-zero, `fgAdd` zero or static | Copy gap controller→FgAnimData |
| `fgAdd` tracks, palette upper joints static | Hypothesis 1 (`0x141AB97C0`) |
| Palette upper joints move, visual fixed | Hypothesis 2/3 (render/world) |

No game memory is written.

## Build

Release DLL built 2026-07-14; injected PIDs 21396/19260. Awaiting ADS log lines.