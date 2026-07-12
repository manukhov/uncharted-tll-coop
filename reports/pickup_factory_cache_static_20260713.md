# Pickup sender source: WeaponPickup factory cache (2026-07-13)

Status: **STATIC candidate**. Runtime test not yet run.

## Hypothesis

The scripted ground-pickup give at return address `0x140DD4208` bypasses
`RequestInteract`, `WeaponPickupInteractControl::OnInteract`, and the per-screen
dispatcher nearby lists, but the level still materializes a live `WeaponPickup`
before the script event. Cache every object returned by the registered
`WeaponPickup_Factory 0x141579150`; at the pre-GiveWeapon boundary select the
nearest still-live object whose art at `+0x520` matches the WeaponInfo art. Read
runtime id `+0xBC` plus descriptor ids `+0x38/+0xD8` from that object.

## Competing hypotheses

1. The level has a live WeaponPickup, but it is absent only from the dispatcher;
   the factory cache recovers it.
2. The scripted pickup never creates a WeaponPickup process; it is a pure script
   prop/event and the cache has no matching art.
3. A WeaponPickup exists but is destroyed before the give-weapon handler runs.
4. The cached object survives, but `+0x38/+0xD8` are zero or not the descriptor
   identity needed by the local pickup animation.

## Independent static evidence

1. Vtable xrefs identify `WeaponPickup_Factory 0x141579150`; its decompile allocates
   exactly `0x560` bytes and writes `WeaponPickup::vftable 0x142AA66C0`. The factory
   is registered from `0x1400C5560` for class name `WeaponPickup`.
2. The factory prologue through `0x141579165` is 21 bytes of position-independent
   stack/register setup, ending before the relative `jz`; it supports the existing
   detour/trampoline mechanism without relocating a branch.
3. Existing static/runtime field mapping independently confirms WeaponPickup art
   at `+0x520`, game-object runtime id at `+0xBC`, and world position at the common
   object transform offset. The prior Type-97 test confirmed the scripted give
   boundary while disproving only dispatcher/control scans.
4. Injection occurs before campaign save load, so factory interception precedes
   level object materialization. Art is read later at GiveWeapon time, not in the
   constructor, avoiding the initialization-order problem.

## Runtime prediction

On a ground pickup after loading a save with the new DLL:

- hypothesis 1: sender logs `PICKUP-ID-CAPTURE ... source=factory-cache` with
  nonzero `id/class/name`; receiver logs `PICKUP-CTXREPLAY published=1`;
- hypothesis 2/3: sender remains `pickup_id=0` and no factory-cache line appears;
- hypothesis 4: factory-cache line has nonzero runtime id but zero descriptor ids,
  or receiver publishes without a visible bend.

Inventory remains exclusively `WeaponGivePacket + SLOT-MIRROR`. The cache is
bounded (2048 pointers), lock-free, and scanned only once per confirmed scripted
pickup, not per frame.
