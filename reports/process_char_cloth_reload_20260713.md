# ProcessCharCloth lifecycle after location reload (2026-07-13)

## Status

`CONFIRMED` lifecycle failure; native deferred repair is ready for runtime test.

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
objects do not create per-frame output.

## Post-reload result

The runtime prediction selected hypothesis 1. In PID 7480, healthy cloth was
attached to parent DrawControl `0x10e13eb940`; the final remote-Nadine parent was
`0x10e034a4c0` with all five parent meshes active, but no `CHAR-CLOTH` object
ever matched it. In PID 13620, three healthy remote-Chloe cloth children followed
several intermediate parent generations; the final complete two-mesh parent
`0x10e0348e40` again had no matching cloth child.

Therefore the final location-reload phase destroys the `ProcessCharCloth`
children and fails to recreate them. The parent look, meshes, gates, and render
instances survive. Hypotheses 2-4 are rejected for this reproduction.

## Repair candidates

1. Queue the current look id through native `Player+0x3D10`, allowing the engine's
   PreFrame path to call `Player::ChangeLookId` after the final parent stabilizes.
2. Call `Player::ChangeLookId 0x1410FE840` directly from a Player/Nadine update.
3. Call `NdPropControl::Init` or `ProcessCharCloth_Factory` directly with a
   reconstructed descriptor.
4. Preserve old cloth children across reload and retarget their parent handles.

Candidate 1 is selected. Candidate 2 invokes a large teardown/rebuild in the
middle of object update and is unnecessarily re-entrant. Candidate 3 lacks the
authored `ProcessSpawnInfo`/prop definitions. Candidate 4 retains stale cloth
bodies and ownership across a lifecycle that intentionally destroys them.

## Independent static evidence for the repair

1. Exact source and ABI: `Player_ChangeLookId 0x1410FE840` is named by the source
   assertion `void __cdecl Player::ChangeLookId(class StringId)` and consumes the
   nonzero look id in EDX.
2. Full lifecycle data flow: it tears down the old DrawControl and prop owner,
   constructs a `ProcessSpawnInfo` at `0x1410FEBC8`, invokes Player vtable slot
   `+0x718`, and rebuilds the composite body at `0x1410FF199`.
3. Child-process materialization: after the body rebuild it calls
   `NdPropControl_Init 0x141E61010`, whose exact source assertion identifies
   `NdPropControl::Init`; that builds a prop spawn list through
   `NdPropControl_BuildPropSpawnList 0x141E61860`, and the follow-up at
   `0x141E61A40` reaches exact-source
   `NdPropControl_SpawnPropFromDefinition 0x141E63D70`.
4. Independent stock scheduling path: the engine PreFrame loop at
   `0x14015CE1C..0x14015CE7D` validates each of six global Player handles. For any
   nonzero `Player+0x3D10`, it calls `Player_ChangeLookId` with that exact dword at
   `0x14015CE69` and immediately clears the field. This is the native deferred
   context for a look rebuild.

## Repair gate and falsifiable prediction

The attachment relay publishes the last exact-vtable cloth owner, parent
DrawControl, and timestamp atomically. On the Nadine/main-game thread, the repair
requires all of the following: the same remote Player previously had healthy
cloth; its current parent DrawControl has been stable for 1.5 seconds; no cloth
has matched that parent for at least 0.75 seconds; and no deferred look is already
queued. It then writes the current `Player+0x170` look id to `Player+0x3D10` once.

If the selected repair is correct, after one location reload the log contains
exactly one `PUPPET-CLOTH queued native ChangeLookId`, the engine replaces or
refreshes the final parent, and healthy `CHAR-CLOTH` lines reappear for that final
generation within a few seconds. Hair returns on both remote avatars. It must not
produce repeated queue lines, a second remote Player, kneeling locomotion, or a
missing held weapon. If the queue line occurs but no cloth is recreated, the full
look rebuild is rejected and analysis moves to the lower `NdPropControl` boundary.

## First repair runtime and gate correction

The first two-copy run partially confirmed the selected native boundary. PID 316
queued `look=0xd27c074f`; `ChangeLookId` returned normally and fresh healthy
`ProcessCharCloth` objects appeared through the subsequent DrawControl
generations. Nadine retained hair and no crash, kneeling locomotion, or missing
weapon was observed. This confirms that deferred `ChangeLookId` can recreate the
cloth lifecycle.

PID 6840 ended with bald Chloe and no queue line. Its healthy cloth belonged to
the prior remote Player generation; the repair state reset `healthy_draw` when
the retained Player pointer changed, so the new generation could never satisfy
the "previously healthy" gate. The gate now preserves healthy slot evidence
across Player replacement while recording `healthy_player` separately. It also
requires either Player or DrawControl generation to differ from the healthy one,
preventing the transient same-parent timeout that unnecessarily queued PID 316
before its original cloth resumed.

## Second repair runtime: process recreation is not sufficient

The corrected gate queued both final parents. PID 4196 recreated Nadine cloth;
PID 3412 recreated all three Chloe cloth mesh ids on final parent
`0x10e0348e40`. Every recreated process reported a non-null body,
`initialized=1`, active mesh gates, and 4/4 active instances. The user still saw
Chloe bald. Therefore deferred `ChangeLookId` is confirmed as a process
re-materializer but **REJECTED as a complete visual repair**.

Remaining competitors are now narrow: the recreated cloth handle may resolve to
a stale Player rather than the current retained puppet; instance main-view or
category suppression may remain set; or cloth simulation/output-joint scheduling
may not restart. The next read-only snapshot records resolved parent object/id,
whether it equals the current puppet, and the previously omitted
`mainViewExcluded/categorySuppressed` instance counts. No further rebuild or
visibility write is added until that discriminator selects a writer.
