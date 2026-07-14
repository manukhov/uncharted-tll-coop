# Player aim rendered-body basis relay (2026-07-14)

Status: **RUNTIME REJECTED as sufficient; late 0xCB0300 full-seed theory rejected**.

## Exact hypothesis

The peer camera target reaches `PlayerAimController_ApplyWorldAimBasis
0x140CAF110`, but the mod replaces GunMoveShield's body-forward argument with
positive-Z derived from `Player+0x140`. The mod also soft-writes camera yaw to
that field after the native derived-transform pass. The controller therefore
measures the camera against a memory root which the visible skeleton has not
sampled; relative yaw collapses while the rendered body remains unchanged.

## Competing hypotheses

1. The sender camera quaternion or pose seed is stale.
2. The slot `+0x58` relay is not reached for the retained puppet.
3. The aim blend or current-state permission remains zero.
4. Directional weights are computed correctly but overwritten downstream.

The current evidence rejects 1-3. Static analysis also found a later stale-seed
consumer inside `KickTargetPointJob`; candidate 4 remains only if both the native
body basis and late seed relay produce changing final weights without visible
direction.

## Independent evidence

1. **Native dataflow:** `PlayerGunMoveShield_UpdateAndDispatch 0x140E84440`
   builds body-forward from the Player runtime transform at `0x140E8582F` and
   `Quaternion_RotatePositiveZ 0x140E85841`, then passes it to vtable slot
   `+0x58` at `0x140E858CE/0x140E85925`.
2. **Mod dataflow:** `hooked_player_aim_world_basis` discarded that argument and
   reconstructed body-forward from `Player+0x140`.
   `apply_peer_aim_facing_to_player` wrote camera yaw to the same field in the
   automatic pre/post ADS paths, while `0x141BB8320` owns the derived-transform
   sampling order.
3. **Runtime discriminator:** PIDs 20712/19956 showed changing sender pose quats,
   live `AIM-WORLD-BASIS`, blend `1`, and forced permission. The logged body basis
   followed the camera until relative yaw fell to `5.44` and `-2.67` degrees even
   though the rendered character still aimed at one point.
4. **Publish layout:** `PlayerAimController_PublishDirectionalAimWeights
   0x140CB41F0` writes left/right/down/up to controller
   `+0x8B8/+0x8BC/+0x8C0/+0x8C4`. The old diagnostic started at `+0x8B4`, so its
   labels were shifted.
5. **Late target owner (refined after runtime):** `Player_PostAnimUpdate 0x1410E9210` calls controller
   vtable `+0xE0` at `0x1410E9F11`. The slot resolves to
   `PlayerAimController_KickTargetPointJob 0x140CB0200`, whose resolver call at
   `0x140CB02FB` returns to `0x140CB0300` and feeds controller-owned target job
   buffers. Instruction-level follow-up shows `0x140CB0319` consumes only the
   first 16-byte route position before the stack local is repurposed; the
   quaternion half is not a yaw owner.

## Narrow change

- Preserve the original GunMoveShield body-forward pointer in the slot `+0x58`
  relay.
- Do not soft-write camera yaw into the root during automatic peer ADS; retain
  sender body orientation as the locomotion/root authority.
- Correct the immediate directional-weight diagnostic offsets.
- Relay the complete 32-byte peer pose seed at late return site `0x140CB0300`
  under the same slot-1, fresh peer ADS, and valid-pose guards.

No route-camera cache, Player aim-output field, derived transform, or animation
graph parameter is written directly.

## Falsifiable prediction

With the peer standing and holding ADS, rotating the camera changes
`AIM-WORLD-BASIS yawPre` relative to the native visible-body basis instead of
settling near zero. Corrected `AIM-WEIGHTS-POST` shows one of left/right changing
for horizontal turns and one of down/up changing for pitch. The rendered upper
body follows the camera. If corrected weights change but the pose remains fixed,
downstream reset/animation-channel consumption wins candidate 4. If yaw still
collapses, GunMoveShield's runtime-transform basis is itself being changed before
the slot call.

## Runtime result

PIDs 20312/5656 reached the native body-basis path with fresh peer data, forced
permission, and blend one. Camera forward changed, but horizontal error frequently
entered at `150..179` degrees and the user saw no directional tracking plus a
broken upper-body transition. The late site did not repair direction. This rejects
the complete-seed `0xCB0300` override and preserving the native body basis as a
complete fix. The large relative angle also explains why a full-strength additive
can visibly fold while the root remains static.
