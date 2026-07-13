# Player aim blend tick probe (2026-07-13)

Status: **RUNTIME: scoped ADS producer is absent; blend tick itself is healthy**.

## Question

After `0x140CB348A` is admitted and the caller writes request byte `+0x8A0`, does
the common PlayerAimController tick raise ADS blend `+0x8EC`, or is the authored
target itself zero for the puppet's active animation state?

## Static basis

1. Player common controller phase `0x140C9F420` calls vtable slot `+0x30`; RTTI
   vtable `0x142A0B1F8` resolves that slot at `0x142A0B228` to
   `PlayerAimController_UpdateBlendState 0x140CAFA20`.
2. `0x140CAFA20` snapshots `+0x8A0`, obtains the target only from
   `PlayerAimController_GetAuthoredAimBlendTarget 0x140CAE480`, advances `+0x8EC`
   toward target or zero, then clears a consumed true request.
3. `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0` independently
   multiplies all four authored aim weights by `+0x8EC`; therefore the visible
   graph cannot distinguish a valid directional solve from zero blend.
4. Runtime PIDs 4412/17588 proved the compute path reached the permission call and
   changed its false result to true, but still showed no pose.

## Probe

Swap only vtable slot `+0x30` after verifying its exact original pointer. The
wrapper snapshots request/blend, calls the original once, and snapshots them again.
It logs only for the retained slot-1 owner while fresh peer ADS is active. No field
is modified by the wrapper.

## Prediction

- `request=1->0, blend=0->0`: the authored target returned by `0x140CAE480` is
  zero; next static task is the active base-state descriptor/input it reads.
- `request=1->0, blend=0->positive`: the controller blend works; missing animation
  is downstream in Aim0..Aim7 graph consumption or later pose overwrite.
- `request=0->0` on every tick despite repeated permission lines: the request is
  cleared between compute and common tick or compute is not sustained; resolve the
  exact scheduler order before another behavioral change.

## Runtime result

PIDs 18500/17476 logged a sustained sequence of
`AIM-BLEND-TICK request=0->0 blend=0->0`. The same interval had fresh strict peer
ADS and repeated pose-seed overrides, but no `AIM-WORLD-BASIS`, permission, or
eligibility calls. This is not an inter-phase request clear: directional compute
never ran.

The active art was scoped rifle `0x8075397D`. The receive path explicitly returned
false from `puppet_aim_press_relay_enabled_for_peer` for every scoped rifle and
then emitted `SCOPED-ADS-SCRUB art=0x8075397d`, clearing the puppet ADS field every
frame. The missing request is therefore explained upstream by our own scoped guard.
