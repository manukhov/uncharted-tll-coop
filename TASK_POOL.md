# Task Pool

## Primary Objective

Полноценный кооп кампании:

- Chloe on host
- Nadine as real second player
- independent input/camera
- host-authoritative campaign state

## Active Track

### P0: Rebind the real second-player lifecycle

- [ ] Find the consumer/drain path for the deferred queue rooted at `0x1437E9700`.
- [ ] Prove when the queued callback `0x1409F9B20` actually executes.
- [ ] Prove that this callback reaches inner bringup block `0x1409FA94E`.
- [ ] Prove that bringup rebinds `ctxB` through `service->virtual +0x20(&ctxB)`.
- [ ] Explain why live campaign can still end up with `ctxB+0x8 == 0` after the known restart path.

### P1: Turn Nadine into a real second player on host

- [ ] Confirm the promotion/rebind point that moves Nadine out of buddy semantics into a real player-like ownership path.
- [ ] Bring up Nadine as a valid gameplay participant for:
  - traversal
  - interaction
  - combat
  - script-facing actor qualification
- [ ] Attach independent camera ownership.
- [ ] Validate checkpoints / death / respawn on host.

### P2: Convert host-side second player into actual co-op

- [ ] Feed player-2 input from the second PC into the real Nadine player path.
- [ ] Replicate the minimum host-authoritative state needed for stable co-op.
- [ ] Bring up second-PC camera/render path.
- [ ] Validate playable two-PC loop in campaign.

## Current Anchors

Use these first:

- shutdown:
  - `0x1409FD360`
- restart scheduling:
  - `0x1409FBA60`
  - `0x1409F9B20`
  - `0x1409FA94E`
- deferred queue:
  - `0x1415B43B0`
  - `0x1415B4870`
  - `0x1415B47C0`
  - `0x1415B4560`
  - `0x1437E9700`
- bind/service:
  - `0x143E0EF00`
  - `0x143E0EF08`
  - `0x143880318`
- net-player path:
  - `0x140A90550`
  - `0x140A9AAD0`
  - `0x140B347C0`

## Parked

Do later, not now:

- [ ] Clean packed `ida/tll.i64` rebuild.
- [ ] Return to buddy-control polish.
- [ ] Revive old `slot1_probe*.dll` style runtime probing.
- [ ] Camera polish before real second-player ownership exists.
- [ ] Second-PC rendering work before host-side second-player path is proven.

## Already Closed Enough

Do not reopen these as the first move:

- [x] Startup bind exists:
  - `0x1409F9B20`
  - `0x1409FA94E`
  - `0x143880318 -> virtual +0x20`
- [x] `ctxB` missing owner is not explained by "startup never ran"
- [x] `0x1409FD360` is already typed as upper shutdown/teardown
- [x] `0x1409FBA60` already narrowed to deferred restart scheduling
- [x] restart work already narrowed to queue root `0x1437E9700`
- [x] old `slot1_probe*.dll` loop is intentionally retired
- [x] `BuildRespawnTransformBlock` is not the current primary blocker
- [x] low net-player seams `0x140A90550 / 0x140A9AAD0 / 0x140B347C0` are not the first-return track right now

## Rule

If a task does not directly move the project toward **полноценный кооп**, it should stay parked.
