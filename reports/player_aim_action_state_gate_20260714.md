# Player aim action-state gate (2026-07-14)

Status: **STATIC; read-only discriminator prepared on paper**.

## Exact hypothesis

During the kneeling-slide failure, the puppet's active authored Player state at
`Player+0xC0` is not a gun-move state that reaches
`PlayerGunMoveShield_UpdateAndDispatch 0x140E84440`, or that dispatcher takes one
of its pre-aim exits. This is why fresh strict ADS can drive the generic blend and
peer IK while producing no `PlayerAimController+0x58` world-basis call.

## Competing hypotheses

1. The automatic stationary `aim-press` relay changes the active state to a
   non-gun or no-weapon/crouch state; the dispatcher is not selected.
2. A valid gun-move state remains active and the full dispatcher runs, but a
   cover/interact/traversal/weapon virtual exits before aim eligibility.
3. The dispatcher reaches `Player_CheckAimActionEligibility`, but eligibility
   remains false. This is weakened because the exact relay hook would report a
   forced false-to-true result under the same fresh ADS guard.
4. Final palette/render consumption is wrong. This remains a separate historical
   possibility, but cannot explain the latest run's zero directional weights and
   missing world-basis call.

## Independent static evidence

1. **Exact active-state data flow.** `Player_IsCurrentActionStateHash
   0x1416A5790` reads `Player+0xC0`, resolves `*(*(Player+0xC0))`, then compares
   the dword at `[*active+0x10]+0x18` to the requested authored hash. This is a
   direct read-only identity surface for the currently selected state.
2. **Convergent callers.** `PlayerGunMoveShield_UpdateAndDispatch` uses the
   predicate with `"GunMoveShield"` immediately before its late aim section.
   `GunMoveController` builder `0x140E8D140` independently uses the same predicate
   to select `MoveShield`, `GunMoveShield`, `GunMoveGrapple`, `GrenadeMove`, and
   the corresponding `*player-ap-strafe-*-set*` animation graphs.
3. **RTTI/vtable hierarchy.** MSVC RTTI resolves the relevant update wrappers as
   `Player::GunMoveShield` (vtable `0x142A27658`, `+0x20=0x140E86930`),
   `GunMoveHeavyWeapon` (`0x142A279D0`, `+0x20=0x140E879C0`), `GunMoveCarry`
   (`0x142A278A8`, `+0x20=0x140E88650`), and `GunMoveCarryNpc`
   (`0x142A27C20`, `+0x20=0x140E89060`). Their wrappers have distinct early
   exits before converging on the common dispatch path.
4. **Rejected screen/weapon alternative.** The apparent screen predicate
   `0x140EFB750` resolves a HUD Player handle and calls
   `PlayerWeaponController+0xE0 = 0x1410B2480`. That virtual specifically tests
   selected slot 1 for weapon class `ProcessShield` (or the network cached
   shield byte). Neighboring `+0xD0/+0xD8/+0xE8` methods publish
   `shield-drawn-tweaks` and set/clear the same byte. It is not a generic
   lifecycle, weapon-clone, or screen-readiness gate.
5. **Runtime structural selection.** PIDs 200/13948 had fresh strict ADS,
   changing pose/IK targets and blend one, but zero directional weights and no
   world-basis call. The user saw the kneeling-slide loop, and the read-only
   palette probe cannot have authored that state.

## Falsifiable read-only prediction

On the next earned runtime, sample only the puppet's validated `Player+0xC0`
active hash and the entry/exit boundary of the gun-move wrapper/common dispatcher.

- If the hash is not one of the gun-move states while kneeling and no common
  dispatcher entry occurs, hypothesis 1 wins.
- If a gun-move hash and dispatcher entry occur but no aim-eligibility boundary
  occurs, hypothesis 2 wins; the exact last passed early gate becomes the next
  static target.
- If eligibility is reached and forced yet no world-basis call follows,
  hypothesis 3 must be refined inside the post-eligibility branch.
- The discriminator must not write Player state, input, animation, or palette.

No runtime test is launched from this report. The next implementation step is a
throttled read-only action-state/dispatch marker using the proven field and exact
function boundaries.
