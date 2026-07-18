# Remote projectile origin / muzzle offset (2026-07-18)

Status: **STATIC**, runtime test not yet run.

## Observation and exact hypothesis

After selected-record direction relay was runtime-confirmed (`dot=1.00000`), the
user observed that the retained clone's tracer and bullet marks travel on a line
roughly one metre to the left of the sender's line.

The exact hypothesis for build `dx` is:

> The receiver creates its selected launch record from its own authored weapon
> muzzle transform. That is a valid native muzzle, but it is not the sender's
> exact world-space launch origin. Relaying the sender selected-record origin and
> replacing both receiver origin copies at `record+0x20` and `record+0x70` before
> native projectile initialization will remove the parallel tracer/impact offset
> without changing the already-confirmed direction.

## Competing hypotheses

1. **Selected-record origin differs between instances — STATIC.** Capture the
   sender's authoritative selected record at `ProcessWeapon::SpawnProjectile` and
   relay its world-space origin.
2. **`FireArgs+0x00` is origin and must be rebased — REJECTED.** It is the
   world-space aim target. The old replay helper incorrectly replaced it with
   `Player+0x240`, which is character-root world position.
3. **Receiver resolves the wrong hand/root instead of the weapon muzzle —
   REJECTED.** The concrete ProcessWeapon vmethod resolves the authored `muzzle`
   or `silenced-muzzle` attachment and has a weapon-bound fallback. The mismatch
   is between two valid instance-local muzzle transforms, not an incorrect socket
   class.
4. **Only the tracer visual is displaced — REJECTED for the reported case.** The
   instant collision path and the base projectile/tracer path consume duplicate
   copies of the same launch origin. The user also observes both tracer and impact
   displacement.

## Static evidence quota

### 1. FireArgs and character-root data ownership

`PlayerFireController_BuildFireArgsAndFire 0x140E2BCD0` loads the controller aim
target at `0x140E2BDE5`, compares it with the pose-seed position at
`0x140E2BFFF..0x140E2C04E`, and stores that target at FireArgs base
`0x140E2C3AD`. Therefore `FireArgs+0x00` is an aim target, not a projectile origin.

`Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320` derives a
character world transform from local root `+0x130`, quaternion `+0x140`, and the
parent descriptor transform, then writes the translation to `Player+0x240` at
`0x141BB8602`. It is the character root, not a hand, muzzle, or camera point.

The old `prepare_puppet_fire_args_from_sender` write of `Player+0x240` over
`FireArgs+0x00` was therefore semantically false and is removed in build `dx`.
It also cannot explain a persistent lateral offset: its only downstream read in
the traced emission path is the gated distance test at
`0x141519631..0x141519720`; the resulting adjustment is one unit along
`FireArgs+0x10`, not sideways.

### 2. RTTI/vtable muzzle producer and origin dataflow

ProcessWeapon RTTI vftable `??_7ProcessWeapon@@6B@` is at `0x142A9BF48`.
Its vtable slot `+0x8C8` resolves to `0x1414FE050`. That method hashes `muzzle`
(or `silenced-muzzle` for the silenced/network condition), searches the weapon
attachment table at `ProcessWeapon+0x1E0`, resolves its transform through
`0x1418B7060`, and falls back through `0x141686030` to the weapon bound at
`ProcessWeapon+0x100`.

`ProcessWeapon` vslot `+0x410`, concrete `0x141515860`, calls the muzzle resolver
at `0x141515A17` and stores its 32-byte result to
`ProcessWeapon+0x970/+0x980` at `0x141515A22`. `0x141518FC0` reads `+0x970` at
`0x141519124`, computes the final origin in `xmm7`, and passes `&xmm7` as R9 to
vtable slot `+0x920` at `0x141519755`. The ProcessWeapon emission wrapper
`0x1414F7800` preserves that vector into its fourth argument; concrete
`ProcessWeapon_ProjectileEmission 0x141519D30` keeps it in R14 and passes it as
R8 to `BuildProjectileLaunchRecord` at `0x14151B728`.

This proves that stock receiver fire legitimately uses its receiver-local weapon
muzzle. It does not prove that the two game instances have bit-identical muzzle
world transforms, and the observed parallel offset says they do not.

### 3. Record layout and two independent consumers

`BuildProjectileLaunchRecord 0x141520750` copies R8 to `record+0x20` at
`0x14152076B..0x141520770` and duplicates R8 to `record+0x70` at
`0x1415207A0..0x1415207A5`. Direction R9 is independently copied to
`record+0x30/+0x40`.

The collision consumer is exact: `ProjectileBulletInstant::Init 0x14152BF20`
passes `record+0x20` to `BoundTransform_SetTranslationWs` at
`0x14152C2AD..0x14152C2B7`, then builds endpoint = start + `record+0x30` at
`0x14152C808..0x14152C829`. `ProjectileBulletInstant_ProbeKick 0x14152C9B0`
reads projectile bounds `+0x130` and `+0x570` and subtracts them at
`0x14152CA08..0x14152CA3E`.

The base/tracer consumer is independent: base projectile setup copies
`record+0x20` into projectile state and sets the start bound at
`0x141522089..0x141522164`; it separately sets the bound at projectile `+0x3A0`
from `record+0x70` at `0x141522169..0x141522174`. Tracer setup reads that bound's
world translation at `0x14152245B..0x141522470`. Consequently a complete relay
must replace both `+0x20` and `+0x70`; writing only `+0x20` could align collision
while leaving the base/tracer origin inconsistent.

### 4. Boundary and prior runtime discriminator

`ProcessWeapon::SpawnProjectile 0x141519B50` receives the already-selected
record pointer at `SpawnInfo+0x28`. This boundary is after the alternative record
builders and before native projectile Init. Owner-handle and weapon-art matching
already prevented unrelated projectiles from consuming queued direction packets.

The same selected-record boundary is runtime-confirmed for direction: build `dv`
applied sender direction to the receiver record, and the concrete instant-bullet
ray reached `dot=1.00000`. Extending the already-proven packet/owner correlation
with the adjacent origin fields avoids global writes to ProcessWeapon transforms.

## Build dx implementation

- `FirePacket` v5 appends `final_origin[4]` and flag
  `kFireFlagFinalOrigin`; v4/v3/v1 receive compatibility remains.
- Sender capture reads selected record `+0x20` after owner0 correlation.
- Receiver apply writes the validated origin to selected record `+0x20` and
  `+0x70` after owner1/art correlation and before original SpawnProjectile.
- Direction relay remains unchanged at `+0x30/+0x40`.
- Probe logging adds `originError = |live_start - relayed_origin|`.
- The false `Player+0x240 -> FireArgs+0x00` substitution is removed.

`tll_teleport_hook_v2.dll` builds successfully with MSVC Release.

## Falsifiable runtime prediction

Fire one ordinary pistol shot and one rifle shot at a distant wall while the two
players are separated enough to see the old parallel offset.

For each shot:

1. Sender must log `FIRE-SELECTED-LAUNCH-CAPTURE` with finite origin.
2. Receiver must log `FIRE-FINAL-LAUNCH-ARM ... origin=1` and
   `FIRE-SELECTED-LAUNCH-APPLY ... originApply=1` for the same sequence.
3. `originBefore` should differ from `originAfter` when the old defect is present.
4. The concrete `FIRE-PROBE-RAY` must keep direction `dot >= 0.99` and report
   `originError <= 0.01` world units.
5. Both the visible tracer and bullet mark must lose the previous roughly
   one-metre parallel left displacement.

`originError <= 0.01` with a still-displaced collision rejects the selected
record's start ownership. Correct collision with a still-displaced tracer rejects
the `+0x70` base/tracer consumer conclusion. No origin flag/apply rejects packet
version or owner/art correlation. Any direction regression (`dot < 0.99`) rejects
the isolation of origin from direction.

## First launch attempt was not build dx

The user's first post-build observation (offset still present but apparently
smaller) is **not a dx runtime result**. Both live processes loaded the older main
`tll_teleport_hook.dll`: the log contains only
`FIRE-SELECTED-DIRECTION-CAPTURE/APPLY` and no v5
`FIRE-SELECTED-LAUNCH-*`, `originApply`, or `originError` markers. At launch time
the main DLL was timestamped 13:34 and 356352 bytes, while the built dx artifact
was `tll_teleport_hook_v2.dll`, timestamped 14:03 and 358912 bytes. Build dx must
replace the main artifact after both processes exit before the prediction can be
evaluated. The init marker is now explicitly `2026-07-18dx`.
