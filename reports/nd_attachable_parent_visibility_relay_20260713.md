# NdAttachable parent visibility relay (2026-07-13)

Status: **REJECTED by runtime; deeper DrawControl discriminator pending**.

## Runtime result (PIDs 13996/4476)

The relay installed and observed the slot-1 Player-owned attachment path, but the
reported state stayed:

```
parentFlags=0x0 parentGate=255/255 childGate=255/255->255 repaired=0
```

The user still saw both remote Chloes without hair after a location reload.
Parent visibility inheritance is therefore not the hair gate. The repair branch
never selected and has been removed.

## Exact hypothesis

Checkpoint reconstruction leaves the retained slot-1 Player body visible but its
hair `NdAttachableObject` hidden because
`NdAttachableObject_PostJointUpdate_Async 0x141ED28E0` calls
`0x141ED62D0`, which copies the parent DrawControl effective visibility byte into
the child through `DrawControl_SyncParentVisibility 0x141754DA0`. A slot-1 parent
with `DrawControl+0x1C != 0` but inherited gate `+0x1D == 0` makes the child
effective visibility zero every post-joint update.

## Competing hypotheses

1. Parent visibility inheritance at child `DrawControl+0x1D` hides hair.
2. Parent `DrawControl+0x18` camera/main-view bits 0/1 hide the child later in
   `0x141ED62D0`.
3. Hair has its own mesh/category state unrelated to either parent field.
4. The hair attachable is absent rather than present-but-hidden.

The prior runtime test rejects the authored event path as sufficient:
`Player+0x1618/+0x1619` false-to-true replay ran three times on both copies and
iterated 1/5 attachments, but both remote Chloes remained bald.

## Independent static evidence

1. **Dataflow:** `0x141754DA0(child,parent)` writes
   `child DrawControl+0x1D = parent[0x1C] & parent[0x1D]`, then recomputes every
   child instance active byte at `instance+0x1A0`. Passing null parent writes
   `0xFF`, preserving only the child's own visibility gates.
2. **Owner graph:** `0x141ED62D0` resolves the attachable parent from
   `NdAttachableObject+0x4E0/+0x4E8`, reads its DrawControl at `parent+0x198`, and
   passes child/parent DrawControls at callsite `0x141ED63F7`.
3. **RTTI/call graph:** `0x141ED62D0` has one caller,
   `NdAttachableObject_PostJointUpdate_Async 0x141ED28E0`; hair/look items and
   weapons are confirmed `NdAttachableObject` instances with independent
   `+0x198` DrawControls.
4. **Competing-bit separation:** after the parent-mask call, `0x141ED62D0`
   separately propagates parent `DrawControl+0x18` bits 1 and 0 using
   `SyncObjectBit02ByActiveRetainedCameraSlots 0x141754C10`,
   `sub_141754340`, and `ClearFlockingFlag0x40AndCompanionBit0 0x141754820`.
   This lets runtime logs distinguish the inherited-byte hypothesis from the
   camera/main-view-bit hypothesis.

## Narrow discriminator

Patch only the `E8` at `0x141ED63F7`, verifying target `0x141754DA0`. Call the
original first. If parent equals retained screen-1 Player `+0x198`, parent own
visibility `+0x1C` is nonzero, inherited `+0x1D` is zero, and main-view bit 0 is
clear, call the same helper again with a null parent. Log parent/child
`+0x18/+0x1C/+0x1D` before and after.

Prediction: logs show `parent own!=0 inherited=0`, repaired child `+0x1D=0xFF`,
and hair reappears. If parent inherited is already nonzero, hypothesis 1 is
rejected and the logged `+0x18` bits select hypothesis 2 or 3.

## Next discriminator

`DrawControl_SyncParentVisibility 0x141754DA0` proves that final render activity
is computed from four layers: DrawControl `+0x1C/+0x1D`, mesh bytes
`+0x243/+0x245`, DrawControl flags `+0x18`, and the resulting render-instance byte
`+0x1A0`. Its caller remains the exact Player-owned `NdAttachableObject` path at
`0x141ED63F7`. Separately, `PlayerUpperDrawControlLifecycleAndAmbientOccluderStage
0x141BB4280` writes Player look/state ids `+0x170/+0x190` and passes both to
`0x141756F10`; prior runtime character-swap work confirmed `+0x170` selects the
hair/look item.

The relay now logs a state-change-only `ATTACH-DRAW` snapshot containing those
look ids, mesh counts, both mesh gates, instance counts, final active count,
main-view exclusion count, and category-suppression count. It no longer mutates
visibility.

Falsifiable prediction:

- valid `look`, nonzero meshes/instances, but reduced `meshGate` or `active`
  selects the corresponding DrawControl writer;
- valid gates but `mainViewExcluded` or `categorySuppressed` nonzero selects the
  instance category path;
- zero `look`, mesh count, or instance count after reload selects look
  reconstruction rather than visibility.
