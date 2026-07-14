# Player aim render discriminator runtime (2026-07-14)

Status: **RUNTIME — publish CONFIRMED; peer/root decoupling CONFIRMED; visual fixed**.

PIDs: receiver **21416**, sender **11208** (pubAdd build). Prior: **19260/21396**.
Log: `reports/locomotion_hook.log`.

## User visual

Puppet still aimed at one fixed point during ADS (same as prior PIDs 13148/21228).

## AIM-RENDER-DISC summary (11 samples)

| seq | peerYaw | rootYaw | yawDelta | ctrlAdd (L,R,D,U) |
|-----|---------|---------|----------|-------------------|
| 507 | -26.7 | 24.0 | -50.7 | 0, 0.80, 0.56, 0 |
| 522 | -113.1 | -3.3 | -109.9 | 0, 0.99, 0.59, 0 |
| 537 | -21.0 | 13.9 | -34.9 | 0, 1.00, 0.59, 0 |
| 554 | 16.0 | 37.6 | -21.6 | 0, 1.00, 0.75, 0 |
| 570 | -54.9 | 14.4 | -69.3 | 0, 1.00, 0.80, 0 |
| 773 | -32.7 | 19.7 | -52.4 | 0, 1.00, 0.60, 0 |
| 1239 | -71.2 | 62.9 | -134.2 | 0, 1.00, 0.66, 0 |

**fgAdd** logged as `(0,1,0.2,0)` on every sample (first-build probe bug: read
`Player+0x1A8` contiguous `+0x188` instead of publish target
`[[Player+0x1A0]+0x58]+0x10` with D/U/L/R field layout).

## Conclusions

1. **CONFIRMED peer/root decoupling:** peer camera yaw swings >80° while puppet
   `Player+0x140` root yaw stays in a ~20–40° band (or slow drift to 63°). Large
   negative `yawDelta` on every sample. Visible fixed aim is consistent with body
   root not tracking peer camera — not with frozen upstream weight publication.
2. **CONFIRMED controller additives track camera:** `ctrlAdd` R/D change with peer
   yaw; `blend=1` on most samples. Upstream `PlayerAimController` is alive.
3. **INCONCLUSIVE fgAdd/publish copy:** first probe read the wrong object/layout.
   Rebuilt probe adds `pubAdd` from exact publish chain and LRDU-normalized fields.
4. **PARTIAL palette motion:** gated `AIM-PALETTE` shows changing joint deltas on
   indices 383–625 (magnitudes 2–4) and occasional large garbage deltas on
   inactive joints. Sane subsets move during camera sweeps, so pure
   `0x141AB97C0` eval failure is weakened but not ruled out without valid-joint
   mask.

## Competing hypotheses after this run

| Hypothesis | Verdict |
|------------|---------|
| Root/render owner wins over peer camera | **Strengthened** — rootYaw ≠ peerYaw |
| Controller→FgAnimData publish gap | **Open** — fix probe, re-run |
| Palette eval never runs | **Weakened** — some joints do move |
| Post-blend IK overwrite | Still open |

## PubAdd build result (PIDs 11208/21416)

User visual: puppet still fixed in ADS, does not rotate with peer camera.

**CONFIRMED publish path:** `pubAdd` matches `ctrlAdd` on every sample (e.g.
seq=827 `ctrlAdd=(0,0.966,0.640,0)` `pubAdd=(0,0.966,0.640,0)`). Publish target
`pubState=0x10e0e67930` is **not** `Player+0x1A8` FgAnimData (`samePtr=0`,
`fg=0x1118b17400`). `fgAdd=(0.2,0,0,1)` on FgAnimData is a stale template.

**CONFIRMED peer/root decoupling (primary visual cause):**

| seq | peerYaw | rootYaw | yawDelta |
|-----|---------|---------|----------|
| 1107 | -101.4 | -4.0 | **-97.4** |
| 1139 | -89.5 | 3.2 | **-92.7** |
| 1251 | -71.0 | 17.2 | **-88.2** |
| 827 | -43.7 | 17.5 | **-61.3** |

Brief alignments exist (seq=935 yawDelta=0.7; seq=1752 yawDelta=0.08) but most
ADS sweeps keep `rootYaw` in a ~15–35° band while peer moves >80°.

**PARTIAL palette motion:** sane joint deltas 2–3.5 on indices 272–625 during
same run; does not rescue visual when root heading is wrong.

## Conclusions after pubAdd build

1. **REJECTED** controller→publish copy failure.
2. **REJECTED** reading `Player+0x1A8+0x188` as publish surface.
3. **CONFIRMED** `Player+0x140` root yaw does not track peer camera during ADS.
4. **CONFIRMED** user visual fixed point matches root decoupling, not upstream
   weight starvation.

## Next step

Re-open body-yaw ownership: `GunMoveController` turn relay `0x140E8F990` →
`+0x150` → `0x140E8FD00`, with post-ADS `apply_puppet_transform_post` root blend
skipped. Prior knee-slide rejection remains; try with current publish chain proven
healthy, or explicit `Player+0x140` yaw snap from peer pose seed during ADS only.