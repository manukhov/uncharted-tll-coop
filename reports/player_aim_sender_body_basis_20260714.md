# Exact sender body basis for relayed aim (2026-07-14)

Status: **REJECTED statically; no runtime test performed**.

## Exact hypothesis

Candidate hypothesis: the world-basis hook compares the relayed sender camera
target with the receiver puppet's native body-forward argument, and replacing only
that argument with `StatePacket::quat * +Z` would recreate the sender's exact stock
GunMoveShield basis. Static composition analysis below rejects this candidate.

## Competing hypotheses

1. The receiver native body-forward is already the visible sender-equivalent
   basis; saturation is entirely downstream in Aim0..Aim7 consumption.
2. `StatePacket::quat` is a retained proxy orientation that does not match the
   sender transform sampled by GunMoveShield during ADS.
3. Sender body basis is correct, but full visual tracking additionally requires a
   separate native turn-in-place decision node.
4. The remaining fixed pose is a publication-timing problem after
   `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0`.

## Independent evidence

1. **Mod data trace:** `send_local_player_state` copies packet quaternion directly
   from retained sender `Player+0x140`; receiver post-update already treats it as
   the sender body/root orientation, independently from `pose_seed+4` camera.
2. **Stock disassembly:** `PlayerGunMoveShield_UpdateAndDispatch 0x140E84440`
   calls `BuildWorldBasisVectorFromRuntimeTransform(Player+0x100)` at
   `0x140E8582F`, then `Quaternion_RotatePositiveZ 0x14014D9F0` at
   `0x140E85841`. Local-root quaternion `+0x40` inside that transform block is
   `Player+0x140`, but the helper also reads and composes parent `Player+0x120`.
3. **Call graph/ABI:** the generated vector is passed unchanged as `r8` to both
   heavy and ordinary ADS slot `+0x58` calls at `0x140E858CE/0x140E85925`.
   `PlayerAimController_ApplyWorldAimBasis 0x140CAF110` then forwards it to the
   signed yaw calculation in `0x140CB32B0`.
4. **Ordering:** the receiver's native `r8` is computed during puppet
   GunMoveShield update, while network root reconciliation occurs later in
   `apply_puppet_transform_post`. It is therefore not guaranteed to represent the
   sender's body sector even when the final root eventually converges.
5. **Runtime discriminator:** with confirmed positive-Z target and blend, sender
   camera changes reached the hook but native-body relative yaw repeatedly crossed
   60 and 90 degrees, leaving `R=1`. This is the expected signature of comparing
   target and body from different actors/update phases.

## Static audit result

The candidate omits one stock transform component. `Player+0x140` is local root
rotation relative to optional parent quaternion `Player+0x120`.
`BuildWorldBasisVectorFromRuntimeTransform` reads and composes both at
`0x14017822A..0x140178285`; GunMoveShield rotates the composed world orientation,
not root alone. Consequently:

`stockBodyForward = RotatePositiveZ(Compose(parent+0x120, localRoot+0x140))`

but the candidate produced only:

`candidateBodyForward = RotatePositiveZ(localRoot+0x140)`

They are equal only when parent rotation is identity. This concrete field-level
difference rejects the candidate before runtime. Preserve GunMoveShield's native
composed body-forward and trace the owner that turns that body/sector during ground
ADS.

## Rejected change

- Constructing a hook-local float4 from normalized `peer.quat * +Z` was removed
  before commit/runtime.
- No Player, root, movement, camera or animation field was changed.

## Cancelled runtime prediction

During a slow horizontal camera sweep, `AIM-WORLD-BASIS` must show a changing
sender-derived body distinct from `nativeBody`, and `yawPre` should remain in or
regularly return to the ordinary 60-degree sector as the sender body turns. This
prediction was not run because the static audit proved the proposed basis is not
the stock basis. Running it could only test an already falsified approximation.
