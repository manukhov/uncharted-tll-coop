# GunMoveController foot-state gates (2026-07-15)

## Build dh runtime result

**REJECTED as a visible foot fix.** The user reports the same stationary planted-
foot rotation, the same aim-walk slide, and only the prior moderate positional
desynchronization. Therefore neither of build dh's discriminators owns the
remaining animation failure:

- setting the ADS post-transform nudge from `0.05 m` to zero did not remove the
  slide, so the separate post-update convergence step is not its primary cause;
- changing only `GunMoveController+0x150` from camera negative-Z to render
  positive-Z did not make the feet turn or alter the moving gait.

## Refined static model

`GunMoveController_SelectAimIdleTurnState 0x140E8F720` has two disjoint branches.

1. When movement magnitude `controller+0x160` is non-zero, it compares normalized
   movement `+0x140` with camera direction `+0x150`, but returns only moving states
   0/1. It cannot select stationary turn states 3/4 on this branch.
2. When `+0x160` is zero, it can return turn states 3/4 only if the resolved
   `move` settings byte at `+0xF4` is enabled and active `FgAnimData+0xD4` exceeds
   `0.1`. Otherwise it returns ordinary aim-idle state 2 before direction sign is
   tested.

The moving state builder `GunMoveController_BuildActionState 0x140E8D140` loads
the owner `Player*` from controller `+0x108` at `0x140E8D18D`. It then copies 80
bytes from **Player** `+0x100..+0x14F` into the authored `player-ap-strafe-*`
request at `0x140E8E46B..0x140E8E49C` (alternate branch
`0x140E8E672..0x140E8E6A3`). It does not copy controller `+0x150`.

The copied Player payload is the native transform snapshot:

- `+0x100` is the embedded transform descriptor/header;
- `+0x110/+0x120` are parent/world translation and quaternion;
- `+0x130/+0x140` are parent-local root translation and quaternion.

`Player+0x140` is therefore the leading stale authored input. The late phase-0x40
FgAnim hook changes the visible world heading only after action evaluation, while
the strafe request retains the old local-root heading.

The stationary runtime field has also been resolved. `GunMoveController_
UpdateTurnYawFromDirection 0x140E8FD00` writes active `FgAnimData+0xD4` at
`0x140E8FF97` as the absolute clamped yaw rate. The selector threshold `0.1`
corresponds to about 6 degrees/second. `GunMoveController_UpdateActionState
0x140E90590` calls this writer only when the prior child state is neither 2 nor
103, before selecting the next state. Thus `+0xD4` is most likely turn
continuation/settling hysteresis, not a cold-idle turn initiator.

## Independent evidence

1. **Control flow:** `0x140E8F720` splits first on `+0x160`; its moving branch
   returns only 0/1, while the zero-magnitude branch contains the 2/3/4 selector.
2. **Field data flow:** `GunMoveController_UpdateAimMovementDirection
   0x140E8EE90` writes adjusted direction to `+0x130`, normalized direction to
   `+0x140`, and magnitude to `+0x160`.
3. **Action construction:** the GunMove-family case in `0x140E8D140` names the
   authored `player-ap-strafe-*` sets, loads controller owner `+0x108`, and copies
   Player `+0x100..+0x14F` into them.
4. **Runtime discrimination:** build dh changed `+0x150` and removed the ADS
   post-transform nudge, but neither visible foot defect changed.
5. **Writer/call graph:** `0x140E8FD00` computes `FgAnimData+0xD4` from yaw delta
   and frame time; `0x140E90590` suppresses that writer in child states 2/103 and
   calls the selector before the action builder.

## Competing hypotheses

1. **Stale Player root snapshot:** `Player+0x140` still describes the pre-FgAnim
   local-root heading when `0x140E8D140` creates the strafe request. This is the
   leading moving-slide and wrong-side entry hypothesis.
2. **Turn continuation mistaken for initiation:** the stationary selector cannot
   leave cold child state 2 through `+0xD4`, because the writer is suppressed in
   that state. A separate initiator or a one-shot native-rate seed is required;
   forcing the field continuously would confuse hysteresis with ownership.
3. **Missing action advancement:** the correct authored graph is built but the
   puppet does not advance its child foot state. This remains possible, but is
   weaker because the visible legs do move during aim-walk.
4. **Position correction:** rejected as the primary cause by build dh. Moderate
   positional desynchronization remains, but it is not correlated with the
   unchanged foot slide.

The next narrow experiment must affect only the snapshot consumed while
`0x140E90590 -> 0x140E8D140` constructs an action: temporarily supply relayed yaw
at `Player+0x140`, let the builder copy it, then restore the native Player root.
This is distinct from the rejected sustained pre/post root writers and avoids
owning location lifecycle state. Stationary turn initiation remains unchanged in
that discriminator.

## Build di falsifiable prediction

Build di hooks only the direct builder call at `0x140E9063B` inside
`GunMoveController_UpdateActionState 0x140E90590`. Once the 250 ms controller
admission opens for the retained slot-1 Player, it converts peer camera world yaw
through parent quaternion `Player+0x120`, temporarily stores the resulting local
quaternion at `Player+0x140`, calls `GunMoveController_BuildActionState`, and
restores the original root immediately afterward. The selector and native yaw
writer run entirely outside the temporary substitution.

If the stale action-snapshot hypothesis is correct, run-to-ADS no longer raises
the weapon first in the old heading and moving aim-walk feet align materially
better with translation. The log must contain bounded `AIM-ACTION-BASIS` markers,
and location reload must not reproduce the old sustained-root kneeling state.

The stationary planted-foot turn is explicitly expected to remain: build di does
not seed `FgAnimData+0xD4` or force child states 3/4. If entry/gait are unchanged
despite the marker, stale `Player+0x140` is rejected and missing child-action
advancement becomes the stronger explanation. Any root drift, wrong facing, or
kneeling after lifecycle transition rejects the temporary snapshot boundary.
