# Aim camera yaw and scoped renderables (2026-07-13)

Status: **RUNTIME REJECTED as sufficient**.

## Runtime discriminator

The `0d45c6e` test finally exposed the aim animation: pistol ADS is visible and
sniper ADS keeps the authored pose. Two failures remain independent:

- rotating the sender camera does not turn the puppet root;
- scoped-rifle ADS leaves the body/pose visible but removes Chloe's hair and the
  held rifle.

This confirms the authored blend-target relay and rejects it as the remaining
direction/visibility cause. `HELD-WEAPON-APPLY` still reports the rifle attached,
so disappearance is render visibility rather than an inventory detach.

## Competing yaw hypotheses

1. `StatePacket::quat` is the wrong root source during free-camera ADS.
2. The old camera-facing path used the wrong quaternion axis/handedness.
3. Native Player update overwrites a correct root after the mod writes it.

Hypotheses 1 and 2 both apply; 3 is rejected by call order. The sender copies
`Player+0x140` to `StatePacket::quat` but sends the route-camera quaternion
separately in `pose_seed+4`. GunMoveShield uses
`Quaternion_RotatePositiveZ 0x14014D9F0` for body forward, while the confirmed
camera/aim ray is quaternion local `-Z`. The old `camera_forward_horizontal`
computed local `+Z`, producing an exact 180-degree error. Finally,
`apply_puppet_transform_post` runs after the original Player update.

The implementation converts `pose_seed+4 * -Z` to a horizontal Player `+Z` yaw
quaternion and applies it only post-update. PlayerAimController slot `+0x58`
receives the same horizontal body basis so the directional additive does not apply
camera yaw a second time; pitch remains in the relayed target.

## Competing visibility hypotheses

1. Scoped script sets `instance+0x18 bit 0x40` on separate Player, hair, and
   weapon DrawControls.
2. A later attachment update propagates the Player flag to child DrawControls.
3. Scoped ADS also hides individual meshes through DrawControl mesh byte `+0x243`.
4. The rifle is detached or switched out of hands.

Attached-state logs reject hypothesis 4. The script handler at `0x1403342C0`
resolves an arbitrary game object and passes only that object's `+0x198`
DrawControl to the flag writer at callsite `0x1403343D3`; the writer never walks
attachment owners. `NdAttachableObject` independently owns `+0x198` and stores its
attachment parent pair at `+0x4E0/+0x4E8`. This makes hypothesis 1 the strongest,
while the exact `IDrawControl_ShowMesh 0x141754A30` inverse retains hypothesis 3.

The implementation uses two narrow barriers during fresh remote scoped ADS:

- a verified `0x1403343D3` callsite relay copies its live RDI target object into
  arg3, then suppresses/clears mask `0x40` only for the puppet, its active/attached
  weapon, or an attachable whose parent pair points to the puppet;
- post-update validates Player and Weapon `+0x198/+0x1A8` DrawControls, clears
  `0x40`, and calls ShowMesh only for entries whose `+0x243` is hidden.

## Runtime prediction

Turning a stationary ADS camera by 90 degrees turns the puppet root in the same
direction, not backward, while the pistol aim animation remains. Scoped ADS emits
`SCOPED-DRAW-FLAG suppressed` for the puppet and at least one attachable, plus
`PUPPET-ADS-VISIBILITY ...`; hair and the rifle stay visible. Calls only for the
Player select a later propagation writer. No suppressed attachment calls and zero
hidden meshes with continued loss select another attachment-local render flag.

## Runtime result (PIDs 14868/18564)

The discriminator selected the last branch, not either implemented visibility
barrier. During the scoped disappearance interval:

- `PUPPET-ADS-VISIBILITY playerMeshes=0 weaponMeshes=0 weaponControls=1` repeated;
- `SCOPED-DRAW-FLAG suppressed` never appeared;
- `HELD-WEAPON-APPLY` continued to report the sniper logically attached;
- the body and authored scoped pose remained visible while the hair attachable and
  rifle disappeared.

Therefore the scoped path does **not** reach the registered script writer at
`0x1403343D3`, and neither Player nor Weapon mesh byte `DrawControl entry+0x243`
is being hidden. The `instance+0x18 bit 0x40` and mesh-visible-byte hypotheses are
**REJECTED** as the cause of this symptom. The remaining candidates are an
attachable owner/view-route exclusion, a camera/category render cull, or another
attachment-local instance state outside the tested DrawControl fields.

The aim data path itself was live: `AIM-WORLD-BASIS` followed the relayed camera
`-Z`, `AIM-ADDITIVE-PERMISSION original=0 forced=1` appeared, and sender
`POSE-SEED-SEND` quaternions changed while the camera turned. The user nevertheless
saw no corresponding body turn. This rejects stale/static camera data and moves
the yaw investigation to the consumption timing of the post-update
`Player+0x140` write versus the animation/render transform used for the frame.

## Replacement static hypotheses (build after `f3bc85e`)

### Yaw

1. **Leading:** root yaw is correct but written after
   `Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320`, while
   `PlayerAimController::ApplyWorldAimBasis 0x140CAF110` is simultaneously told
   that body-forward already equals camera-forward. The current transform pass
   therefore receives neither derived root rotation nor directional yaw.
2. The rendered skeleton uses an independent root that never consumes
   `Player+0x140`.
3. The packet camera quaternion is stale or uses the wrong axis.

Evidence quota for hypothesis 1 is met by three independent kinds:

- vtable identity and disassembly pin `g_original_update` to common slot `+0x2B8`,
  `Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320`; its
  `0x141BB83E0` read of `Player+0x140` precedes the old post-transform yaw write;
- the already confirmed locomotion path requires `Player+0x250/+0x270` velocity
  before the same update so the graph can sample it that frame, establishing the
  scheduling rule independently;
- runtime `AIM-WORLD-BASIS` logs showed the substituted horizontal body basis
  equal to changing camera `-Z`, rejecting stale data and proving that the
  additive path was deliberately receiving approximately zero yaw delta.

The narrow change writes the same validated camera `-Z` yaw immediately before
the remote Player's `0x141BB8320` derived-transform pass and keeps the post write
as authority. It introduces no second orientation source. This is not yet proof
that the asynchronous joint-blend phase samples after this hook: real
`Player_Update 0x1410EC230` is dispatched independently by
`AnimManager_AsyncObjectUpdateJointBlendJob 0x141A92A00` through Player vtable
`+0x410`. Therefore the ordering fix is **STATIC**, not CONFIRMED as a complete
visual fix.

### Runtime correction after location restart

The PRE-root experiment is **REJECTED**. After a location restart the remote Chloe
slid back and forth in a kneeling locomotion pose. This matches the pre-existing
2026-07-07 rejection recorded directly beside the slot-1 drive: a pre-update
camera-root write makes native locomotion fight a second orientation source. The
PRE helper and both calls are removed; post-transform camera facing is again the
only root owner.

### Scoped visibility

1. **Leading:** scoped ADS writes Player `+0x1618 = 0`; the per-frame transition
   function `0x140C990A0` mistakes the retained remote Player for the local
   first-person actor and publishes authored `DisableMainView` events.
2. NdAttachableObject camera/category propagation at `0x141ED62D0` hides hair and
   the weapon without authored events.
3. A separate mesh/category byte (`+0x245` or DrawControl `+0x18/+0x1D`) is the
   final cull source.

Evidence quota for hypothesis 1 is met independently:

- dataflow: `0x140C990A0` reads current `Player+0x1618` and previous `+0x1619`,
  emits `DisableMainView` on the false branch and `EnableMainView` on the true
  transition, then copies current to previous and resets current to one;
- graph/strings: the function is called directly from `Player::Active::Update`
  after screen camera-state update and contains four xrefs each to the exact
  authored `DisableMainView` and `EnableMainView` strings;
- vtable identity: its `Player+0x1D88` target is the confirmed
  PlayerWeaponController; slot `+0x370 -> 0x1410A01F0` loops all weapon slots and
  publishes `DisableMainView`/`DisableCastShadows`, while `+0x378 -> 0x1410A08A0`
  owns the inverse visibility update;
- structural agreement: the same function iterates Player `+0x1B10` attachables
  and publishes the corresponding main-view events, explaining simultaneous hair
  and rifle loss while body pose and logical weapon attachment remain intact.

The relay changes only a fresh remote scoped-rifle request from false to true at
the entry of `0x140C990A0`. The rejected `0x1403343D3` hook is no longer installed,
and the rejected per-frame `+0x243` mesh restoration is no longer called.

The first runtime session also exposed a broader lifecycle case: after every
location restart both copies saw the other retained Player without hair, while
`PUPPET-MAIN-VIEW-HIDE` never logged under the scoped-only condition. Therefore a
false `+0x1618` request is not scoped-specific; reconstruction can issue it before
fresh peer ADS exists. The remote retained Player must always belong to this
copy's shared main view, so the relay now consumes false requests for slot 1
throughout its lifecycle. The exact authored event path remains the same; runtime
must now show the suppression at restart.

## Falsifiable prediction

During remote scoped-rifle ADS the receiver must log
`PUPPET-MAIN-VIEW-HIDE suppressed`; hair and the held rifle remain visible. No
such log with continued disappearance rejects hypothesis 1 and selects the
NdAttachableObject category path, whose next discriminator is callsite
`0x141ED640A`. Turning a stationary ADS camera should now turn the rendered body
through the refreshed derived transform. A changing pre-root with a static body
selects the independent `Player_Update`/joint-blend ordering or another
render-root consumer, rather than packet or handedness errors.
