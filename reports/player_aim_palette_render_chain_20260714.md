# Player aim palette → render chain (2026-07-14)

Status: **STATIC — visible ADS aim owner narrowed to skeleton skinning path, not Player root.**

## Exact hypothesis

Remote puppet ADS aim stays at one world point because the **rendered** upper-body
pose is produced downstream of `ProcessAnimCmdListSegment` skinning evaluation and
`CharacterSkeletonEval_PerJoint`, not from `Player+0x140` root yaw. Published
`aim-add-*` weights reach `[[Player+0x1A0]+0x58]+0x10` (confirmed runtime), but the
puppet's visible aim may consume a **different FgAnimData/skinning buffer** than the
`Player+0x1A8` object sampled by `AIM-PALETTE`.

## Competing hypotheses

1. **Object split (preferred):** publish-state FgAnimData receives live
   `aim-add-*` and drives anim-cmd eval, but skeleton render binds to
   `Player+0x1A8` (or another generation) with stale `+0x560` skinning mats.
2. **Eval failure:** `ProcessAnimCmdListSegment` does not resolve aim-add parameters
   on the puppet's active animation graph node even though controller weights publish.
3. **Render overwrite:** palette/skinning updates correctly but a later DrawControl
   or world-basis pass freezes weapon aim bones.
4. **Root ownership (REJECTED):** `Player+0x140` drives visible ADS aim — disproved
   by root-yaw snap runtime (PIDs 2084/7800: `peerYaw≈rootYaw`, visual unchanged).

## Independent static evidence

### 1. Publish → anim graph (upstream, confirmed path)

`PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0`:

- Writes D/U/L/R floats to `[[[PlayerAimController+0x108]+0x1A0]+0x58]+0x10`
  at `+0x188..+0x194`.
- Publishes string parameters `aim-add-left/right/down/up` into the Player
  animation graph via `sub_14172C8E0` when the alternate Aim0..7 fast-path is off.

### 2. Palette production

`AnimManager_PerformAnimBlend 0x141A92260` (already labeled):

- Allocates/fills `48 * joint_count` bytes per joint (three `float4` rows).
- Swaps `FgAnimData+0x230` → `+0x238`, clears `+0x270/+0x278`.
- Calls `ProcessAnimCmdListSegment 0x141AB97C0` from producers `0x141A91430`,
  `0x141A91660`, `0x141A91D20`, and async jobs `AnimManager_AsyncObjectUpdateJointBlendJob`.

`ProcessAnimCmdListSegment 0x141AB97C0`:

- Source assertion: `anim-commands.cpp:822`.
- Ten callers from blend producers and async joint-blend jobs.
- Member refs show stack spill of palette rows at eval time (`+0x230/+0x270`).

Separate skinning allocation in `AnimateDisabledObjectJob` (`anim-mgr-jobs.cpp:1775`):

- `pNextFrameSkinningBoneMats` buffer at `FgAnimData+0x560`, previous at `+0x568`.

### 3. Skeleton render consumption (downstream owner)

Thin render-job entry `CharacterSkeletonRenderJob_Entry 0x141B5CE90`:

```c
if (owner && !*(owner + 0x81))
  CharacterSkeletonRender_Update(owner);  // 0x141B61530
  CharacterSkeletonRender_PostPass(owner); // 0x141B65C50
```

Scheduled as Ndjob callback from `AudioManager::RunRenderJob 0x141B5D240`
(`sub_141B5CE90` written into job record at `0x141B5D34E`).

`CharacterSkeletonRender_Update 0x141B61530`:

- `rcx` → owner (SkelTable-logged skeleton context).
- Per-joint loop: workspace `owner+0x14B8`, stride `0x6C0`.
- Calls `CharacterSkeletonEval_PerJoint 0x141B757F0(rcx=owner, rdx=workspace)`
  at `0x141B63CE6`.

`CharacterSkeletonEval_PerJoint 0x141B757F0` (~0x6112 bytes):

- Gate: `*(owner+0x80)` and joint count `*a2`.
- Heavy use of owner `+0x230` palette rows and `+0x2B0` blend timing.
- Sole direct caller: `CharacterSkeletonRender_Update`.

### 4. Player world transform is NOT the visible aim owner

`Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320` (Player vtable `+0x2B8`):

- Derives `Player+0x240/+0x250/+0x280` from `+0x130` local root and `+0x140` rotation.
- Does not read `FgAnimData+0x230` palette.
- **Runtime REJECTED** as visible ADS aim owner (root snap matched peer, visual fixed).

### 5. DrawControl is not the aim-pose writer

`Player+0x740` = `ResolvePlayerAmbientOccluderSelectionAndRefreshDrawControlMeshes`
— ambient-occluder mesh refresh only (already in initial-findings).

`PlayerUpperDrawControlLifecycleAndAmbientOccluderStage 0x141BB4280` — upper
lifecycle/init; delegates `+0x740` during bringup, not per-frame aim evaluation.

## Runtime cross-check (prior runs, not re-run this turn)

| Finding | Implication |
|---------|-------------|
| `pubAdd == ctrlAdd`, `pubState ≠ Player+0x1A8` | Publish surface split from palette probe target |
| `peerYaw ≈ rootYaw` after snap, visual fixed | Root path rejected; skeleton path remains |
| Partial `AIM-PALETTE` motion on `Player+0x1A8` | Base anim moves; does not prove aim-add reaches skinning |

## Falsifiable next probe (read-only)

During slot-1 puppet ADS with active `pubAdd`:

1. Resolve `pubState` pointer from `[[Player+0x1A0]+0x58]+0x10`.
2. Sample **both** `pubState+0x230` palette and `pubState+0x560` skinning mats vs
   `Player+0x1A8` same offsets.
3. Compare upper-body joint indices (weapon/spine chain) across peer yaw sweeps.

- If `pubState+0x560` is static while `pubAdd` tracks camera → hypothesis 1 wins;
  fix is routing publish-state skinning into render, not more root writes.
- If `pubState+0x560` tracks but visual fixed → hypothesis 3; trace DrawControl mesh
  instance transform consumer after `CharacterSkeletonEval_PerJoint`.
- If both buffers track → hypothesis 2 weakens; inspect aim-add node binding inside
  `ProcessAnimCmdListSegment` for puppet graph vs local Player.

## Labels added (ida_apply_labels.py)

| Address | Name |
|---------|------|
| `0x141AB97C0` | `ProcessAnimCmdListSegment` |
| `0x141B5CE90` | `CharacterSkeletonRenderJob_Entry` |
| `0x141B61530` | `CharacterSkeletonRender_Update` |
| `0x141B757F0` | `CharacterSkeletonEval_PerJoint` |

Apply to `ida/tll.i64` via headless `ida_apply_labels.py`; refresh MCP copy after save.

## Read-only runtime probe (2026-07-14bq)

`log_puppet_fganim_split_discriminator` emits `AIM-FGANIM-DISC` at 400 ms when peer
yaw changes ≥2° and controller weights/blend are active:

- Resolves `pubObject = [[Player+0x1A0]+0x58]+0x10` and `fgObject = Player+0x1A8`.
- Compares palette pointers `+0x230` and skinning pointers `+0x560`.
- Reports cross-object palette/skinning joint deltas plus per-object skinning motion
  vs private baseline (`pubSkinTot`, `fgSkinTot`, `crossSkin`, `crossPal`).
- No game-memory writes.

Boot marker: `2026-07-14bq (AIM-FGANIM-DISC pub vs fg skinning)`.