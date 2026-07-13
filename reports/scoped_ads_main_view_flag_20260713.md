# Scoped ADS main-view exclusion (2026-07-13)

Status: **STATIC implementation, runtime pending**.

## Hypothesis

Full scoped ADS on the puppet executes the authored script command
`set-instance-flag-draw-in-main-view(false)`. Its handler sets render-instance
flag `0x40` on the puppet DrawControl, excluding Chloe from the receiver's main
viewport. Clearing precisely that flag after the puppet native update preserves
the ADS state while keeping the model visible.

## Competing hypotheses

1. DrawControl child instance flag `0x40` is the visibility gate.
2. Mesh-visible byte `mesh+0x243` is changed by scoped ADS.
3. A later camera render-mask or near-fade system culls the puppet independently
   of DrawControl.
4. `SniperReticleManager_QueryScopeVisible 0x140C177F0` hides the model.

## Independent evidence

1. The literal `set-instance-flag-draw-in-main-view` is registered at
   `0x14003B880` with handler `0x1403342C0`. The handler resolves the current
   Player DrawControl at `Player+0x198`; false calls `0x141754EF0(draw, 0x40)`,
   while true calls `0x141754FF0(draw, 0x40)`.
2. `DrawControl_SetChildInstanceFlagsByMask 0x141754EF0` walks
   DrawControl `+0x8/+0x14`, mesh stride `0x250`, child stride `0x20`, and writes
   `instance+0x18 |= mask`. `0x141754FF0` performs the exact inverse.
3. Independent APIs `IDrawControl_DisableMeshFromMainView 0x1417545F0` and
   `IDrawControl_EnableMeshInMainView 0x141754B10` use the same
   `instance+0x18 bit 0x40`, fixing its main-view exclusion semantics.
4. The scope-visible query's only consumer is HUD reticle update
   `0x140C193A0`; it controls the overlay and has no DrawControl path. Direct
   HideMesh xrefs instead lead to CrashBandicoot spinning, not scoped ADS.

The dynamic authored scoped-ADS script that emits this registered command is not
represented as a direct executable xref, so the finding remains STATIC until the
runtime visual test.

## Implementation

The old speculative per-mesh ShowMesh/EnableMeshInMainView loop is removed.
`keep_puppet_scoped_mesh_visible`, already called after the puppet native update,
now invokes the exact inverse:

`DrawControl_ClearChildInstanceFlagsByMask 0x141754FF0(drawControl, 0x40)`

It remains gated to fresh peer ADS, a scoped/long rifle, full puppet ADS relay,
and the verified `Player+0x198` DrawControl identity. It does not alter ADS state,
mesh-visible bytes, weapon state, or receiver HUD.

## Runtime prediction

- Success: `SCOPED-MAIN-VIEW cleared instance flag 0x40` appears during full
  sniper ADS and Chloe remains visible on the receiver.
- Local scope behavior and ADS animation remain intact.
- If the log appears but Chloe still disappears, flag `0x40` is not sufficient;
  investigate the late camera render-mask/near-fade candidate next.
