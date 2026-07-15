# Player ADS body turn: FgAnimData render-world publication (STATIC, 2026-07-15)

## Selected hypothesis

The visible remote body does not follow ADS camera yaw because the renderer consumes
the world matrix already published at `FgAnimData+0x1C0`. Rewriting
`Player+0x140` after the Player update changes the gameplay object quaternion but
does not replace this render snapshot. The narrow correction point is the stock
phase-`0x40` call at `0x141A8FC00` to
`FgAnimData_PublishWorldTransform 0x1418C55D0`.

## Competing hypotheses

1. **Selected:** the phase-`0x40` FgAnimData world publication is stale and must
   receive the peer camera's horizontal yaw.
2. The final 651-joint palette needs a global yaw baked into every 3x4 matrix.
   Rejected as the primary body-turn surface because the renderer has a separate
   per-instance world matrix and binds the palette independently.
3. `Player+0x140` is consumed directly by rendering. Rejected as sufficient by
   runtime: logs showed the root quaternion following peer yaw while the visible
   model remained fixed.
4. A later renderer uses an unrelated owner/generation. Disproved for the normal
   path by `PrepareInstanceForRendering`, but remains the explicit failure class
   if the published matrix changes and the visible model does not.

## Independent evidence

1. **Exact source identity.** `0x1418C50C0` contains the exact
   `FgAnimData::PrepareSkinningMtxUploadBuffer` assertion in `anim-data.cpp`.
   `0x14181A9B0` contains repeated `PrepareInstanceForRendering` assertions in
   `fg-draw-mgr.cpp`.
2. **Concrete data flow.** `0x14181A9B0` copies CPU matrices from decimal
   `+560` (`+0x230`) to upload storage at decimal `+576` (`+0x240`), then calls
   `0x1403014F0`. That getter reads the 64-byte matrix at `FgAnimData+0x1C0`,
   which the preparation function writes into each `FgInstance` world transform.
3. **Call graph and phase ownership.** Animation jobs call
   `AnimManager_DispatchRetainedOwnerPhase(0x40, fg)`. At `0x141A8FC00` the
   dispatcher passes `rcx=fg`, a native quaternion+translation block in `rdx`,
   and `r8d=0` to `0x1418C55D0`; that helper publishes through `0x1418C54D0`.
   The later `FgDrawManager_PrepareInstancesJob 0x14181B890` invokes
   `PrepareInstanceForRendering` for each FgAnimData.
4. **ABI/field assembly.** `0x1418C55E9` saves `r8b`; `0x1418C55F0..0x1418C55FC`
   passes transform `+0x10` and `+0x00` as translation/quaternion to the native
   matrix builder. `0x1418C54DD..0x1418C5504` writes four rows to
   `+0x1C0..+0x1F0`; `0x14030150C` later addresses exactly `+0x1C0`.

## Falsifiable runtime prediction

Patch only the direct phase-`0x40` callsite and preserve the native function,
translation, scale, and publish flag. While the peer holds ADS and rotates the
camera, the hook count and desired horizontal yaw must change and the whole visible
remote body must turn with it. Joint/aim animation remains owned by the existing
native relay, and releasing ADS must immediately restore the unmodified native
publication.

If the counter/yaw changes but the model stays fixed, hypothesis 4 wins and the
next target is render-instance generation rather than joints. A crash before the
first matched publication indicates callsite/ABI failure. Kneeling slide, hair or
weapon disappearance would indicate that the correction escaped the world matrix
and touched pose/visibility state, which this design must not do.

## Build cz implementation

Build `2026-07-15cz` patches only the direct `E8` at `0x141A8FC00` through a
near relay. The wrapper first compares `rcx` with the current remote Player's
cached `FgAnimData`; every non-matching object calls the untouched native
`0x1418C55D0` immediately. A matching object is modified only for a peer ADS
packet no older than 250 ms. The wrapper copies the native 32-byte transform,
replaces its quaternion with horizontal peer-camera yaw, preserves translation
and `r8b`, then invokes the original function. It records only counters/yaw in
the animation phase; the Player thread emits at most one diagnostic line per
second.
