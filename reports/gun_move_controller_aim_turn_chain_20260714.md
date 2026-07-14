# GunMoveController camera-to-body turn chain (2026-07-14)

Status: **STATIC**. Evidence quota is met; runtime prediction is pending.

## Exact hypothesis

The remote reticle remains fixed because the retained slot-1 Player's native
`GunMoveController` at `Player+0x1DA8` overwrites its world turn direction at
`GunMoveController+0x150` from the receiver's screen-1 route camera. The packet
pose reaches the additive aim controller, but it never reaches this earlier body
turn input. A guarded post-call override at
`GunMoveController_ApplyCameraYawToMoveDirection 0x140E8F990` should replace only
the slot-1 `+0x150` result with the horizontal positive-Z direction of the fresh
peer pose. Stock code should then select `aim-idle-rotate`, turn the root, and
bring the additive yaw back inside its authored arc.

## Competing hypotheses

1. `PlayerAimController 0x140CAF110/0x140CB32B0` owns body yaw. Rejected: it
   computes and publishes directional additive weights but never writes the root.
2. Direct packet `Player+0x140` yaw is sufficient. Rejected: packet root omits
   optional parent rotation `Player+0x120`, and the prior pre/post root writers
   reproduced the kneeling-slide conflict after location reload.
3. The slot-1 Player never runs the downstream GunMoveController update. Still a
   runtime discriminator: if true, `+0x150` will be staged but
   `GunMoveController_UpdateTurnYawFromDirection 0x140E8FD00` will not consume it.
4. The camera packet is stale. Rejected by the preceding runtime: relayed pose
   quaternion and positive-Z forward changed while the visible aim remained fixed.

## Independent static evidence

1. **Vtable identity and call graph.** `GunMoveController::vftable` is
   `0x142A280A8`. Slot `+0x38` is `0x140E8F990`, slot `+0x50` is `0x140E90590`.
   `PlayerGunMoveShield_UpdateAndDispatch 0x140E84440` calls `+0x38` at
   `0x140E85388`, then later calls `+0x50` at `0x140E85BB2`.
2. **Field data flow.** `0x140E8F990` resolves the owner's screen route and camera,
   rotates its input by camera yaw, normalizes the vector, and stores it at
   controller `+0x150`. `0x140E8F720` compares `+0x150` with `+0x140` and returns
   states 2/3/4; `0x140E90590` feeds that result to `0x140E8D140`.
3. **Authored animation strings.** `0x140E8D140` constructs `aim-idle` for state 2,
   `aim-idle-rotate` for state 3, and `aim-idle-rotate-cc` for state 4.
4. **Exact root writer.** `0x140E8FD00` reads controller `+0x150`, computes target
   and current world yaw, smooths it through controller `+0x270/+0x274`, builds a
   yaw quaternion, applies it to the owner's runtime transform at `Player+0x100`
   through `0x1416861E0`, and commits through vtable slot `+0x208`. Its sole code
   caller is `0x140E90590` at `0x140E90605`.
5. **Axis agreement.** The relayed positive-Z formula is independently identical
   to `PlayerAimController_GetTargetPointFromReticle 0x140CB1720` and
   `Quaternion_RotatePositiveZ 0x14014D9F0`; the prior negative-Z route was
   runtime-rejected as near-180-degree opposition.

## Runtime prediction

With a narrow `0x140E8F990` post-call override enabled only when
`owner == retained_player_object(1)` and the peer has fresh ADS plus a valid pose:

- rotating the sender camera changes the staged slot-1 `+0x150` positive-Z vector;
- stock state selection enters states 3/4 and the visible body turns through the
  authored `aim-idle-rotate` animation;
- `AIM-WORLD-BASIS yawPre` converges inside the 60-degree additive arc instead of
  saturating above 90 degrees;
- ordinary locomotion, non-ADS frames, local slot 0, and reload/restart behavior
  remain unchanged;
- specifically, the puppet must not resume the kneeling forward/back slide.

If `+0x150` changes but the body does not, competitor 3 wins: the puppet is not
executing the `+0x50 -> 0x140E90590 -> 0x140E8FD00` downstream phase and the next
step is a guarded native dispatch, not another root-field write.
