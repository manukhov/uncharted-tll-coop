# Puppet ADS aim — build bt runtime (2026-07-14)

Status: **REJECTED** as visible fix; upstream chain improved, render path still wrong.

## Session

- Build: `2026-07-14bt` (`teleport_hook.cpp` commit `9ca0354`)
- PIDs: **4164** (sender, STRICT-AIM-SEND), **8300** (receiver puppet)
- User: visual ADS aim on puppet **still fixed** («всё ещё нет»)

## Hypothesis (bt)

FgAnimData `+0x560` skinning is never allocated on puppet; mirroring palette
`+0x230` → `+0x560` after aim-add mirror will reach `CharacterSkeletonEval`.

## Runtime evidence (PID 8300, receiver)

| Probe | Result |
|-------|--------|
| `AIM-FGANIM-SKIN-ALLOC` | **YES** — `fg=0x1118b17400 buf=0x6f0000 bytes=31248` |
| `AIM-FGANIM-SKIN-COPY` | **YES** — multiple frames, `joints=651` |
| `AIM-FGANIM-MIRROR` | **YES** — `fgAdd` tracks `pubAdd` (e.g. seq=335: both `(0,0.9773,0.564373)`) |
| `AIM-RENDER-DISC` | `samePtr=0`; `rootYaw≈47.6°` stuck while `peerYaw` sweeps -57°..-135° |
| `AIM-FGANIM-DISC` | `fgSkinValid=1` after alloc; `fgSkinTot` sometimes 220–278 or `inf` |
| `AIM-PALETTE` | `fgPalTot` large motion (e.g. total=25279 seq=400) |
| GunMove path | `AIM-DISPATCH-ENTRY name=GunMove`, world-basis/weights active |

Sample lines (locomotion_hook.log):

```
[pid=8300] AIM-FGANIM-SKIN-ALLOC fg=0x1118b17400 buf=0x6f0000 bytes=31248
[pid=8300] AIM-FGANIM-SKIN-COPY joints=651 pal=0x1018000810 skin=0x6f0000 bytes=31248
[pid=8300] AIM-RENDER-DISC peerYaw=-57.0773 rootYaw=47.6077 yawDelta=-104.685 fgAdd=(0,0.9773,0,0.564373)
```

## Anomalies

- Some `AIM-FGANIM-SKIN-COPY` lines show `skin=0x3f3d235b` / `0x3da3c2da` (float
  garbage) — native or timing may overwrite `+0x560` between alloc and copy.
- `fgSkinTot=inf` on several samples — skinning baseline/compare unstable after alloc.

## Conclusion

1. **CONFIRMED:** bt completes the FgAnimData path (mirror + alloc + skin copy).
2. **REJECTED:** FgAnimData `+0x560` skinning as the visible ADS aim owner.
3. **REJECTED (reconfirmed):** `Player+0x140` root — `rootYaw` frozen ~47° with live
   `pubAdd`/`fgAdd`; post root blend already removed in bt.
4. **Preferred next:** inject puppet palette into **SkelTable render workspace**
   (`CharacterSkeletonRenderJob_Entry 0x141B5CE90` → global `qword_146919C40`,
   `CharacterSkeletonRender_Update 0x141B61530`) before
   `CharacterSkeletonEval_PerJoint 0x141B757F0`. Static chain in
   `reports/player_aim_palette_render_chain_20260714.md`.

## Falsifiable next probe

Pre-render hook on puppet ADS: copy `Player+0x1A8` palette `+0x230` into SkelTable
owner `+0x230` (or per-joint workspace at `+0x14B8`). If visual tracks peer →
hypothesis 1 (object split) CONFIRMED; if still fixed → DrawControl/post-eval path.