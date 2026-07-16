# Aim pitch, foot turn and projectile direction (2026-07-16)

## Vertical aim channels

**STATIC.** `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0`
stores raw inputs at controller `+0x8B8/+0x8BC/+0x8C0/+0x8C4`, then publishes
post-blend channels through `0x140CAF470`. Direct stores establish active-state
layout `up=+0x188`, `down=+0x18C`, `left=+0x190`, `right=+0x194`; the previous
up/down constants were reversed. Constructor/channel indices independently give
`up=base+4`, `down=base+6`, and string CRCs are `aim-add-up=0x7283B601`,
`aim-add-down=0xE8651C16`.

Build v10 wraps only computed publisher callsite `0x140CB4176`, captures sender
`up/down * controller+0x8EC`, and repeats the native named commits for screen 1.
Zero is relayed because it clears the channel. Competing direct active-state and
palette copies are rejected by earlier runtime; global `0x14172C8E0` interception
is rejected because it has unrelated callers.

## Authored turn-in-place feet

**STATIC.** `GunMoveController_UpdateActionState 0x140E90590` calls writer
`0x140E8FD00` at `0x140E90605`, selector `0x140E8F720` at `0x140E9060F`, then
builder `0x140E8D140`. Selector states 3/4 are authored
`aim-idle-rotate/aim-idle-rotate-cc`; it requires stationary `controller+0x160`,
direction `controller+0x150`, and yaw rate `active-state+0xD4 > 0.1`. The active
state is `[[Player+0x1A0]+0x58]+0x10`, not `Player+0x1A8`.

Build v10 captures the exact sender direction/rate and selector result. Receiver
temporarily supplies them only around the stock selector and restores them. The
paired writer callsite is suppressed only for a fresh stationary peer sample, so
it cannot retake Player root ownership after state 3/4. The old `+0x150`-only
vtable experiment is parked.

## Final fire direction owner

**STATIC CORRECTION.** BuildFireArgs starts its `FireArgs` at stack `var_520`.
The resolver seed is copied to `var_4C0`, therefore seed is `FireArgs+0x60` and
quaternion is `+0x70`, not the stale `+0x100/+0x6C` constants. Also,
`sub_140CADD60` is a 12-byte descriptor helper, not a ray cooker.

The downstream chain resolves the actual owner:

1. `PlayerFireController_Fire 0x140E2C730` calls weapon vtable `+0x870` at
   `0x140E2CE4A` with `(weapon, screen_index, FireArgs*)`.
2. RTTI/vtable identity gives `ProcessWeapon::vftable 0x142A9BF48 +0x870 =
   0x1415182E0`. The base implementation is `nullsub_1131 0x1408C9D90`.
3. `0x1415182E0` only copies FireArgs into `ProcessWeapon+0xB00`. The downstream
   `0x141518FC0` path overwrites `FireArgs+0x10` at `0x141519434` and again at
   `0x1415194B6`; projectile virtual `+0x920 = 0x141519D30` may overwrite it at
   `0x141519E5A`, then applies spread through `0x1414F9EE0`.
4. The direct callsite `0x14151B72B -> 0x141520750` passes receiver-local origin
   in `R8` and the post-spread direction in `R9`. The builder copies origin to
   launch record `+0x20` and direction to `+0x30/+0x40`.
5. The caller later replaces `+0x40` with the nominal vector, while
   `ProjectileBulletInstant::Init 0x14152BF20` and `ProbeKick 0x14152C9B0`
   consume `+0x30`. Tracer and collision therefore share `record+0x30`.
6. RTTI identifies the common data-driven `ProcessWeapon` class for pistol,
   rifle and sniper (`TypeDescriptor 0x143769968`, vtable `0x142A9BF48`).

This rejects pose-seed delivery alone as the final owner, matching build do
runtime. It also rejects `vtable+0x870`, intermediate `FireArgs+0x10`, and a
tracer-only direction as competing owners. FirePacket v3 defers transmission
until `0x14151B72B`, captures exact sender post-spread `R9`, and substitutes only
receiver launch-record `+0x30` once for the matching fresh shot. Native origin,
muzzle, handles, flags, nominal vector and spread machinery remain receiver-local.
Direct native Fire replay remains disabled after its deferred
`ProjectileBulletInstant::ProbeKick` crash.

## Falsifiable runtime prediction

All three relays install together but have independent outcomes:

- vertical camera movement emits `AIM-PITCH-CAPTURE/APPLY`, alternates receiver
  `aim-add-up/down`, and becomes visible; horizontal FgAnim yaw is unchanged;
- stationary ADS yaw emits `AIM-FOOT-CAPTURE/APPLY`, selects authored states 3/4
  and feet step, while moving ADS remains native;
- the first peer shot emits one `FIRE-FINAL-DIRECTION-APPLY` marker; both tracer
  and collision follow the sender post-spread vector for pistol and rifle.

If the tracer aligns but collision does not, the common `record+0x30` hypothesis
is rejected. If the marker is reached but neither changes, ownership is below the
builder. Success of one item does not confirm either of the other two. No game
launch occurred before this prediction was written.

## Runtime result: REJECTED (build dp)

PIDs `19368/18056` installed all three relay groups and exchanged 504-byte v10
state packets in both directions. Visual behavior was unchanged: no vertical
weapon motion, no turn-in-place foot steps, and the remote bullet/tracer still
left in the wrong direction.

The bounded discriminators did not emit `AIM-PITCH-APPLY`, either `AIM-FOOT`
marker, or `FIRE-FINAL-DIRECTION-APPLY`. Only a sender-side zero pitch capture
appeared. Therefore delivery to the chosen receiver boundaries is rejected, not
the UDP extension. Do not reinstall callsites `0x140CB4176`,
`0x140E90605/0x140E9060F`, or only `0x14151B72B` as fixes. Continue through the
puppet's actually ticking animation path, the cold turn-state entry, and all
three `0x141520750` projectile-record callsites.
