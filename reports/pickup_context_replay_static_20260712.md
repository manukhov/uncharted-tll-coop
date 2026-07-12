# Pickup animation: context-scoped descriptor replay (2026-07-12)

Status: **STATIC candidate**. No runtime test was run.

## Precise hypothesis

The missing receiver-side pickup gesture can be authored without a mirrored live
`WeaponPickup`. Relay the two ids consumed by `Player_PublishRequestedInteract`
(`interactable+0x38` and `interactable+0xD8`), switch `ContextProcess` to the
retained puppet with `0x1416A2ED0`, call the publisher with a minimal zeroed
surrogate carrying only those fields, then restore the previous context.

Expected observation once runtime is allowed: the puppet enters the pickup overlay
once even when the receiver cannot resolve the original pickup runtime id. Inventory
must remain owned by `WeaponGivePacket` and `SLOT-MIRROR`.

## Competing hypotheses

1. The Player-side `player-requested-interact` descriptor is sufficient for the
   visual overlay; the world-side `interact-with-me` event is only interaction
   coordination.
2. Both descriptors are required; replaying only the Player-side descriptor does
   not bend the puppet.
3. A nearby-list/command-state side effect is still required after descriptor
   publication, so the surrogate publish remains inert.

## Independent static evidence

1. `Player_PublishRequestedInteract 0x140C87D70` reads exactly two values from its
   interactable argument: dwords at `+0x38` and `+0xD8`. It does not dereference any
   other field before building the `player-requested-interact` descriptor. The
   first argument is a real per-screen dispatcher blob and is only updated at
   `+0x35B0` after publication.
2. The string has only two code producers. Besides the generic publisher,
   `Player_PublishZiplineRequestedInteract 0x1410BCDF0` builds the same descriptor
   from two authored `zipline` ids and publishes it without entering
   `Player_InteractDispatcher`. This rejects the claim that the generic dispatcher
   is the only valid producer of this Player-side state.
3. `ThreadContext_GetCurrentProcess 0x1416A2E80` reads TLS tag/object
   `+0x90/+0x98`. `ThreadContext_SetCurrentProcess 0x1416A2ED0` hashes
   `ContextProcess`, writes those exact fields, and returns the TLS block. Stock
   descriptor publishers repeatedly use the balanced sequence `get old -> set
   target -> virtual +0xD0 -> restore old`, so puppet targeting does not require a
   global or persistent TLS mutation.
4. The old receiver path fails earlier for a concrete cross-copy reason:
   `Player_ResolveWeaponPickupInteractAnim 0x140C7FF40` needs the live pickup/art
   definition, while the receiver commonly has no matching ground object after
   inventory mirroring. A two-id surrogate removes that dependency without using
   the rejected MP anim-snapshot serializer.

## Build boundary

Do not implement this as a persistent TLS write. Use an SEH-guarded balanced scope:

1. Validate the retained slot-1 Player and screen-1 dispatcher blob.
2. Build a zeroed surrogate at least `0xDC` bytes long; write only sender-captured
   dwords `+0x38/+0xD8`.
3. Save `ThreadContext_GetCurrentProcess()`.
4. Set context to the puppet, call `Player_PublishRequestedInteract(dispatcher,
   surrogate)`, and restore the saved context on every exit path.
5. Keep duplicate inventory suppression and sequence deduplication unchanged.

This candidate has the required static evidence quota, but alternatives 1-3 are
distinguished only by a later runtime test. It is not CONFIRMED.
