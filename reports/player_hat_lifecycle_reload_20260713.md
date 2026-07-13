# Player hat/hair lifecycle after location reload (2026-07-13)

## Current runtime result

The PIDs 18080/19668 test reproduced bald remote players after a location
reload. Their resolved Player look ids remained valid (`0xd27c074f` for Nadine
and `0x3f334972` for Chloe). Every child DrawControl observed at the exact
`NdAttachableObject` parent-sync callsite `0x141ED63F7` retained gates `255/255`,
one readable mesh with both mesh gates set, and active render instances.

**REJECTED:** a present child `NdAttachableObject` being hidden by its own
DrawControl, mesh gate, main-view flag, category flag, or final active byte.
The hair either disappeared from the Player-owned render set or was never
rematerialized after reload.

## Exact hypothesis

Location reload retains the remote Player object and its authored visual ids,
but does not run the PlayerBase hat/hair rebuild that normally repopulates the
Player parent DrawControl. The falsifiable state is:

- Player `+0x15AC` or `+0x15B0` remains nonzero;
- Player hat process/mesh tables at `+0x1758/+0x1858` lose entries;
- the corresponding mesh id disappears from Player `+0x198` DrawControl;
- child attachment DrawControls stay active.

## Competing hypotheses

1. The tracked hat tables are cleared and not rebuilt after reload.
2. The tracked tables and parent DrawControl meshes remain present, but a
   parent mesh/instance visibility gate suppresses hair.
3. The parent DrawControl remains complete and active; the missing visual is a
   separate `ProcessCharCloth` materialization outside this hat path.

## Static evidence

1. **Source identity:** assertion strings inside `0x140CC4BF0` name
   `PlayerBase::SpawnHatWithIdFromMap` and bound both resolved prop/set ids and
   meshes to 16 `kMaxHatProps` entries.
2. **Dataflow:** `0x140CC4BF0` writes spawned process handles at
   `0x140CC4F9D` to `Player+0x1758 + 0x10*i`; at `0x140CC556B` it writes added
   mesh ids to `Player+0x1858 + 4*i`. The mesh materialization ends through
   `0x1418069F0`.
3. **Inverse dataflow:** `0x140CC5890` walks the same 16 tables, calls
   `ProcessKillTree` for live process handles, calls the DrawControl
   `IDrawControl::RemoveMesh` path on `Player+0x198`, and clears each tracked
   mesh id before rebuilding.
4. **Independent callers:** Player initialization calls `0x140CC5890` when
   `Player+0x15AC/+0x15B0` is nonzero; `Player::ChangeLookId 0x1410FE840`
   calls it after reconstructing the Player DrawControl; other callers first
   write `+0x15AC` or `+0x15B0` and then call it immediately.

These facts establish that hair/hat meshes belong to the Player parent
DrawControl, not the child DrawControls sampled by the previous relay.

## Read-only discriminator

The verified `0x141ED63F7` relay still calls the original function unchanged,
but now samples only the parent DrawControl at most four times per second. A
state-change-only `PLAYER-DRAW` line includes:

- Player look/state/appearance and both hat ids;
- live process and mesh counts from `+0x1758/+0x1858`;
- parent DrawControl mesh ids, gates, instances, and final activity flags.

Prediction: hypothesis 1 produces stable nonzero hat ids with tracked counts or
matching parent mesh ids dropping after reload. Hypothesis 2 keeps ids/counts
but changes a parent gate/activity aggregate. If all fields remain identical,
hypotheses 1 and 2 are rejected in favor of the separate cloth/process path.

Status: **STATIC discriminator ready; no behavior mutation.**
