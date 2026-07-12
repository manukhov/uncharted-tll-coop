# Native pickup gesture producer: PlayerPickupController (2026-07-13)

Status: **RUNTIME CONFIRMED**.

## Precise hypothesis

The remote bend can be reproduced without a WeaponPickup or interact dispatcher by
relaying the exact 32-byte third argument accepted by
`PlayerPickupController_StartOrQueue 0x140F49CA0`. Receiver calls the same native
method on `puppet+0x1EB8`, with object argument null (an explicitly supported path).
The puppet's existing Player update then advances its controller and native
`dra-pickup-*` animation graph.

## Competing hypotheses

1. **PlayerPickupController Start/Queue is sufficient**: queueing native state on
   the puppet starts the bend.
2. Queueing succeeds, but the puppet never calls `PlayerPickupController_Update`.
3. The null-object path queues state but cannot select/complete the visible gesture.
4. The bend comes from authored ProcessPickup anim params rather than this controller.

## Independent evidence

1. Script/data flow: `interact-with-me` hash `0x4825A9F1` enters the `Pickup`
   decision branch and calls Player+0x1EB8 vtable +0x38 at `0x140DD0A3C`.
   That slot is `0x140F49CA0` (data xref `0x142A34190`).
2. Implementation data flow: `0x140F49CA0` copies arg3 as 32 bytes into the
   controller queue at `+0x130`, stride `0x30`, increments count `+0x430`, and
   selects authored `dra-pickup-stand/crouch/cover/hg/shimmy-*` hashes into `+0x434`.
   Every object dereference is guarded; object=null is a native valid path.
3. Animation identity: `0x140F4B150` starts the gesture and observes
   `signal-pickup`; `0x140F4CD20` is self-identified by the
   `PlayerPickupController::PostAnimBlending` assertion in player-pickup.cpp:897.
4. Completion path: `0x140F4D0E0` publishes `player-pickup` through
   `DispatchOrResolveActorStateDescriptorIntoRuntimeSlot`, then clears the queue.
   This controller path is independent of the later GiveWeapon branch
   (`0xFB1091A3`, return `0x140DD4208`).

## Implementation and prediction

Protocol PickupPacket v4 adds flag `kPickupFlagControllerStartArgs` and the exact
32-byte arg3. Sender swaps vtable slot `0x142A34190`; after the original local
Start/Queue accepts the action, it sends the POD. Receiver calls the original on
`puppet+0x1EB8` with object=null from the puppet game-thread update.

- success: `PICKUP-CONTROLLER-SEND`, then `PICKUP-CONTROLLER-APPLY queued=1`, and
  the puppet bends once;
- hypothesis 2/3: sender line appears, receiver repeatedly reports `queued=0`, or
  reports `queued=1` with no visual gesture;
- missing sender line rejects the mapped callsite for this campaign pickup.

Inventory remains entirely WeaponGivePacket + SLOT-MIRROR. The old descriptor and
dispatcher recipes are bypassed for v4 controller packets.

## Runtime result

Two-copy loopback test, PIDs 1748 and 9036, with commit `6c8072b`:

- sender emitted four consecutive `PICKUP-CONTROLLER-SEND` packets (`seq=1..4`);
- receiver emitted `PICKUP-CONTROLLER-APPLY queued=1` for every packet;
- the user confirmed the remote character visibly performs the pickup bend.

The successful null-object replay also rejects competing hypotheses 2 and 3: the
puppet does tick this controller, and the relayed POD is sufficient to select and
display its native gesture without a live WeaponPickup object.
