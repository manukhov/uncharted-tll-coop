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
