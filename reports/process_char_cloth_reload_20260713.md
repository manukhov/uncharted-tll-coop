# ProcessCharCloth lifecycle after location reload (2026-07-13)

## Status

`STATIC` diagnostic boundary; runtime prediction not yet executed.

## Observation

After a location reload the remote characters are bald, while the new
`PLAYER-DRAW` snapshot keeps the complete parent render sets:

- remote Nadine: five of five meshes, all mesh gates active;
- remote Chloe: two of two meshes, all mesh gates active;
- both authored hat ids and all tracked hat process/mesh slots are zero.

This rejects a dropped base-look mesh and the optional
`PlayerBase_RebuildHatAttachmentsFromIds` path as direct causes of this reload
failure. Hair is rendered by a separate attachable/process lifecycle.

## Exact hypothesis

The slot-1 Player is recreated with a valid parent DrawControl, but its
`ProcessCharCloth` child is either not recreated or is initialized against a
stale parent/cloth body. At `0x141ED63F7`, RBX is the current
`NdAttachableObject`; the child and parent DrawControls are RCX/RDX.

## Competing hypotheses

1. The `ProcessCharCloth` object is absent after reload.
2. It exists, but its `+0x4E0/+0x4E8` parent handle or `+0x238` cloth body is
   stale/null.
3. It exists and is initialized, but its child DrawControl loses meshes or an
   own/inherited visibility gate.
4. All process and draw state remains intact; the failure is later in cloth
   simulation/output-joint scheduling.

## Independent static evidence

1. RTTI and construction: `ProcessCharCloth_Constructor 0x141F3DEE0` installs
   vtable `0x142BAF780`; `ProcessCharCloth_Factory 0x141F41470` allocates the
   exact 0x980-byte object.
2. Registration path: `RegisterProcessCharClothFactory 0x1401186C0` registers
   the exact `ProcessCharCloth` string with that factory.
3. Data flow: `ProcessCharCloth_Init 0x141F3E060` consumes descriptor data,
   resolves the parent handle at `+0x4E0/+0x4E8`, creates cloth/render state,
   and marks `+0x870`; `ProcessCharCloth_UpdateRoot 0x141F3E6E0` resolves the
   same parent and updates cloth roots.
4. Attachment path: `NdAttachableObject_PostJointUpdate_Async 0x141ED62D0`
   explicitly hashes `ProcessCharCloth`, resolves its parent, and calls
   `DrawControl_SyncParentVisibility 0x141754DA0` at `0x141ED63F7`.

## Instrumentation and falsifiable prediction

The read-only relay at `0x141ED63F7` now forwards RBX as a third argument and
emits `CHAR-CLOTH` only when the object vtable is exactly `0x142BAF780`.

- If hypothesis 1 is correct, a normal load emits `CHAR-CLOTH`, while the first
  post-reload `PLAYER-DRAW` for the new parent has no matching `CHAR-CLOTH`.
- If hypothesis 2 is correct, post-reload `CHAR-CLOTH` remains but shows a
  null/changed parent handle, cloth body, or initialization byte.
- If hypothesis 3 is correct, process ownership is intact but child mesh counts,
  gates, or active instances differ.
- If the line remains structurally identical before and after reload, the first
  three hypotheses are rejected and analysis moves to cloth output scheduling.

No process spawn/rebuild is called by this diagnostic.

## Initial runtime control

Before reload, PID 14696 emitted three distinct `ProcessCharCloth` objects for
the remote Chloe. All three resolved the same live parent handle and parent
DrawControl; each had a non-null cloth body, `initialized=1`, child mesh count
`1/1`, gates `255/255`, and four of four active instances. Their mesh ids were
`0x42f16666`, `0x3280a3b4`, and `0x05f33116`.

This validates the exact-vtable discriminator and establishes the healthy
baseline. The logger uses a fixed per-object atomic cache so alternating cloth
objects do not create per-frame output. The post-reload half of the prediction
is still pending.
