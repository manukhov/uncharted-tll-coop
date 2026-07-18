# Remote firearm ammo and weapon-HUD ownership (2026-07-18)

Status: **STATIC**. Build `dw` is compiled; the written runtime prediction has not
yet been executed.

## Hypothesis and competitors

Hypothesis A: the receiver retained puppet owns an independent live magazine; if
it reaches zero, the stock fire path takes its empty/unloaded exit before a
button-replayed shot can reach `PlayerFireController_Fire`.

Competitors:

- B: the HUD number is display-only and a different state blocks fire;
- C: Fire/projectile dispatch succeeds and a later visual path rejects the shot.

Static evidence selects A for the reported zero-ammo symptom. B is rejected
because the HUD reads the same live `ProcessWeapon` fields used by fire. C has no
support: projectile dispatch precedes ammo consumption and the exact earlier
empty gate already explains the missing shot.

## Live ProcessWeapon ammo

- `ProcessWeapon+0x900`: signed int16 magazine.
- `ProcessWeapon+0x902`: signed int16 reserve.
- `ProcessWeapon+0x908`: loaded-state byte; zero is rejected by vtable `+0x868`.
- `ProcessWeapon+0x909`: related dirty/state byte cleared by native setters.

Independent evidence types:

1. Direct getters/predicates: vtable `+0x840/+0x848/+0x858/+0x860/+0x868`
   resolve to `0x141103850/0x141103830/0x141103840/0x141103870/0x141103880`.
2. Writers: initialize/set/reload functions `0x141517C90`, `0x141517CF0`,
   `0x141517D80`, `0x1415180C0`, `0x14151E0B0`, `0x14151E2F0` update the same
   fields and state bytes.
3. Consumer/call graph: `ProcessWeapon_PrepareFireFromArgs 0x1415182E0` calls
   projectile dispatch `0x141518FC0` at `0x141518BF3`, then
   `ProcessWeapon_ConsumeShotAmmo 0x14151E740`, whose ordinary firearm path
   decrements `+0x900` at `0x14151EB13`.

The decisive pre-Fire gate is `PlayerFireController_BuildFireArgsAndFire
0x140E2BCD0`: vtable `+0x848` magazine is tested at `0x140E2BD2D` and `<=0`
returns before Fire; vtable `+0x868` unloaded-state is tested at `0x140E2BD41`.

## Why the receiver starts with different ammo

`WeaponInfo_InitializeDefaults 0x1401B7600` writes DWORD `-1` at
`WeaponInfo+0x84` (`0x1401B766B`). The whitelist give fallback preserves it.
`PlayerWeaponController_GiveWeapon 0x1410A4D40` reads it at `0x1410A588A`; `-1`
selects authored/random total-ammo derivation through `0x1410A589B..0x1410A591F`
and passes that independent result to CreateAndAddWeapon at `0x1410A5C3E`.

## Weapon-HUD owner

`PlayerHudScreen_WeaponAmmoPublisher 0x14135D2B0` reads screen index
`state+0xF8`, selects `g_PlayerHandleForHudPublish[screen]`, resolves that
Player's current weapon, reads total through vtable `+0x840` and magazine through
`+0x848`, publishes `selected-weapon-id`, `total-ammo`, `clip-size`,
`loaded-ammo`, `show-weapon-hud?`, and spawns `ss-hud-weapon`.

The retained-puppet caller path is explicit:

`PlayerActiveAsyncJob` descriptor `0x143071B70` -> callback `0x140CA1630` ->
`PlayerHud_ScreenStatePublishUpdate(player)` at `0x140CA1655` -> read
`player+0x4010` -> `g_PerScreenPlayerHudState[screen]` -> publisher call
`0x140C921A2`.

Therefore screen 1 legitimately publishes the puppet's weapon HUD, but in this
single-viewport co-op architecture that process overlays the local viewport. The
reticle gates did not cover this separate publisher.

## Build dw and prediction

`FirePacket` v4 appends the sender's pre-shot magazine/reserve. The receiver
validates the art and writes the snapshot only to the retained puppet's matching
weapon, restoring `+0x908=1/+0x909=0`; retained slot 0 is explicitly excluded.
Native fire consumes the one local round. A separate entry detour skips
`0x14135D2B0` only when `state+0xF8 != 0`.

Prediction: with sender magazine `N>0` and receiver puppet magazine zero, one
shot must log matching `ammo=N/R`, `FIRE-AMMO-APPLY target!=local0`, one visible
projectile, and `ammoPost=N-1/R`. `WEAPON-HUD gated: screen!=0` must appear, the
local screen-0 HUD must remain correct, and switching to the receiver-local
player must show its ammo unchanged.

## IDB handoff verification

`scripts/ida_apply_labels.py` applied 1073 function names to `ida/tll.i64` and
reported `save_database returned True`. After refreshing `ida/tll_mcp.i64`, MCP
resolved the new names for all getters, setters, reload/consume functions,
`PlayerFireController_BuildFireArgsAndFire`,
`PlayerHudScreen_WeaponAmmoPublisher`, and
`PlayerActiveAsyncJob_PublishHudState`.
