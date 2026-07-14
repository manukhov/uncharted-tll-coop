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

## Build under test

The runtime candidate patches only `GunMoveController::vtable+0x38`. Its hook
calls stock `0x140E8F990` first, validates controller owner == retained slot 1,
fresh packet age <= 250 ms, strict peer ADS and a valid pose quaternion, then
replaces controller `+0x150` with normalized horizontal pose positive-Z. Every
other call returns the stock result unchanged. The release DLL builds successfully;
the only compiler diagnostic is the pre-existing C4456 at lines 5878/5911.

### First runtime attempt: hook scaffolding rejected before ADS

PIDs 18480/14436 both exited when the second copy entered gameplay. Neither PID
emitted `AIM-TURN-DIR`, so the `+0x150` substitution and its body-turn prediction
were not exercised. The first hook build performed an unnecessary
`retained_player_object(1)` table lookup from every GunMoveController call while
the retained table was transitioning. That hot-path lookup is **REJECTED**. The
next build identifies the already-resolved owner at controller `+0x108` by its
own confirmed `Player+0x4010 == 1` screen index and does not touch the retained
table. The direction hypothesis remains STATIC and untested.

### Second runtime attempt: internal turn passed, visual result rejected

PIDs 10600/12916 survived gameplay entry and ADS. Receiver PID 10600 logged a
changing staged direction while PID 12916 rotated its camera, ending at
`peerPosZ=(-0.803093,-0.595853)`. Inside the native update, GunMoveShield later
reported body basis `(-0.803219,0,-0.595684)` and `yawPre=0`. The stock screen-1
result stayed almost fixed around `(0.075,-0.997)`, so the `+0x150` cause and
native `0x140E8FD00` root turn are internally **CONFIRMED**.

The user nevertheless saw no visual aim progress, and after a location/progress
transition Chloe resumed the kneeling forward/back slide. Therefore the first
complete `+0x150` build is **REJECTED as a visual fix**.

## STATIC correction: remove the post-native ADS root cancellation

The failure has a second exact owner in mod source:

1. `hooked_update` calls `g_original_update(object)` and only afterwards calls
   `apply_puppet_transform_post(object)` for retained slot 1.
2. During fresh peer ADS, `apply_puppet_transform_post` takes its default branch
   and blends `Player+0x140` toward `StatePacket::quat` by 0.35 every frame.
3. `StatePacket::quat` is the sender's local body root, not its route-camera yaw.
   Thus this post writer cancels the native camera turn that had already produced
   the matching body basis inside `g_original_update`.
4. The resulting state-machine/root disagreement predicts both observations: the
   renderer receives the cancelled root, while `aim-idle-rotate` keeps trying to
   complete and corrupts locomotion across a lifecycle transition.

Competing explanations are now separable. "Native turn never ran" is rejected by
the matching internal body basis. "Relayed axis is wrong" is rejected by the same
match. The remaining alternative is that render ignores the final Player root
even after the post cancellation is removed.

The next build leaves the installed GunMoveController relay in ownership of ADS
root yaw and skips only the post-ADS `pkt.quat` blend while that relay is installed.
Position reconciliation, respawn snaps, non-ADS orientation and every non-slot-1
controller remain unchanged. Prediction: visible aim follows camera, native turn
finishes instead of looping, and location change does not reproduce kneeling-slide.
If internal body still aligns but the visible model does not, trace the final
post-anim/render skeleton owner; do not add another Player root writer.
