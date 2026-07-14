# Puppet ADS aim — SkelTable workspace static map (2026-07-14)

Status: **STATIC — PerJoint matrix source narrowed to workspace+0x558; owner+0x40B8 is timing.**

## Corrected hypothesis (build cb)

Visible puppet ADS aim is produced inside `CharacterSkeletonEval_PerJoint
0x141B757F0`, which reads **per-joint workspace** matrix rows at `workspace+0x558`
(and related +0x578/+0x588 bands), not from:

- `FgAnimData+0x560` skinning heap (REJECTED bt/ca runtime),
- `SkelTable owner+0x230/+0x560` as heap pointers (REJECTED bx),
- `SkelTable owner+0x40B8` as inline palette (STATIC: engine clock counter).

## Independent static evidence

### 1. Update → PerJoint callsite

`CharacterSkeletonRender_Update 0x141B61530` per-joint loop @ `0x141B63CD4`:

- `workspace = *([owner+0x14B8]) + joint_index * 0x6C0`
- `mov rcx, owner; mov rdx, workspace; call PerJoint` @ `0x141B63CE6`

### 2. PerJoint matrix consumption

`CharacterSkeletonEval_PerJoint 0x141B757F0` loads eval matrices from **rdx workspace**:

- `vmovups ymm0, [workspace+0x558]` (+0x578 xmm, +0x588/+0x5A8/+0x5C8 ymm bands)
- `workspace+0xB8` (184) = instance/render-node pointer used to attribute joint to character
- `workspace+0` = joint id (DWORD)

### 3. owner+0x40B8 is NOT palette

At `0x141B616B7` Update calls `sub_141B59230` → callback @ `owner+0x1AB0` returns a
**monotonic counter**. Update stores it at `owner+0x40B8`, then:

- `delta = new - [owner+0x40B0]`
- `*[owner+0x40C0] = delta * g_EngineClockToSeconds` (frame delta seconds)

PerJoint only propagates the counter QWORD into `workspace+0x280` for timing — not
bone matrices.

### 4. Consistency with rejected runtime paths

| Path | Runtime |
|------|---------|
| FgAnimData mirror + skinning memcpy | REJECTED ca (lag + no visual) |
| pubState / SkelTable heap inject | REJECTED by/bz |
| `Player+0x140` root snap | REJECTED 2084/7800 |
| Upstream `pubAdd==ctrlAdd` + world-basis | CONFIRMED ca |

Failure remains **between palette production and workspace matrix input to PerJoint**.

## Build cb probe (lightweight inject)

Hook `0x141B63CE6` (pre-PerJoint). When slot-1 puppet peer ADS active and
`workspace+0xB8` resolves to puppet/draw-control/fganim, copy 48 B from
`Player+0x1A8` palette `+0x230` at `joint_id` into `workspace+0x558`.

- No 651-joint scan, no pubState memcpy.
- Log: `AIM-WS-INJECT joint=N`.
- Boot marker: `2026-07-14cb`.

## Falsifiable prediction

During peer ADS on receiver:

- If `AIM-WS-INJECT` lines appear with changing `joint=` and visual tracks peer →
  workspace path CONFIRMED; refine joint filter / matrix layout.
- If logs appear but visual fixed → PerJoint overwrites workspace+0x558 internally
  or DrawControl post-pass owns visible aim.
- If no logs → `workspace+0xB8` puppet attribution filter wrong; dump instance ptr.