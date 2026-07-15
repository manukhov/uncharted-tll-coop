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
2. When `+0x160` is zero, it can return turn states 3/4 only if both the resolved
   `move` settings byte at `+0xF4` and the Player runtime field at `+0xD4` pass.
   Otherwise it returns ordinary aim-idle state 2 before direction sign is tested.

The moving state builder `0x140E8D140` independently copies 80 bytes from
controller `+0x100..+0x14F` into the authored `player-ap-strafe-*` action request:
32 bytes from `+0x100`, 32 from `+0x120`, and 16 from `+0x140`. It does not copy
`+0x150`. Consequently build dh could affect stationary selection only; it had no
data path into the moving strafe action parameters.

## Independent evidence

1. **Control flow:** `0x140E8F720` splits first on `+0x160`; its moving branch
   returns only 0/1, while the zero-magnitude branch contains the 2/3/4 selector.
2. **Field data flow:** `GunMoveController_UpdateAimMovementDirection
   0x140E8EE90` writes adjusted direction to `+0x130`, normalized direction to
   `+0x140`, and magnitude to `+0x160`.
3. **Action construction:** the GunMove-family case in `0x140E8D140` names the
   authored `player-ap-strafe-*` sets and copies only `+0x100..+0x14F` into them.
4. **Runtime discrimination:** build dh changed `+0x150` and removed the ADS
   post-transform nudge, but neither visible foot defect changed.

## Competing hypotheses

1. **Stationary gate mismatch:** the puppet remains in state 2 because settings
   `+0xF4` or runtime `+0xD4` differs from a fully promoted local Player. This is
   the leading stationary-turn hypothesis; owners and writers still need tracing.
2. **Stale strafe action basis:** one or more values in controller
   `+0x100..+0x140` describe the pre-FgAnim body basis, so the authored gait is
   evaluated against a heading different from the final rendered yaw. This is the
   leading moving-slide hypothesis; exact writers still need tracing.
3. **Missing action advancement:** the correct authored graph is built but the
   puppet does not advance its child foot state. This remains possible, but is
   weaker because the visible legs do move during aim-walk.
4. **Position correction:** rejected as the primary cause by build dh. Moderate
   positional desynchronization remains, but it is not correlated with the
   unchanged foot slide.

No further runtime is earned yet. The next static step is to resolve the two
stationary gates and the writers/semantics of `+0x100..+0x120`, then write a
prediction that distinguishes gate failure from stale strafe basis.
