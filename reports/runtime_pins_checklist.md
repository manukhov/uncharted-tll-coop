# Runtime pins — one loaded-save session checklist (2026-07-05)

All static design is done; these are the remaining LIVE-MEMORY reads. Do them in one
session with a save loaded. Grouping = the context each read needs.
Tool: `scanner/build/tll_read.exe --pid <PID> <addr> <len>` (+ the mod's diagnostic
logs). Two copies only where noted.

Legend: [A]=any gameplay, [V]=needs a vehicle in scene, [S]=needs a sniper/scoped
rifle, [2]=needs both copies + a peer.

---

## Already CONFIRMED statically (do NOT need a read — listed so we don't re-check)
- NetAnimCmdGenerator offset = **Player+0x1B30** (notes 2026-06-29).
- is-scope-visible query = 0x140C177F0; per-screen HUD state index at state+0xF8.
- Fire ray = pose seed at CameraManager::GetHandle(idx) -> *(mgr+0xB0)+0x60
  (32 bytes: pos[4]+quat[4]).
- Task-graph command list base 0x146A514A0, count 0x146A51568, kMax 5, stride 0x28.
- Camera matrix in the per-screen cache entry at +0x350.

---

## 1. Animation tier-4 capture [A]
- [ ] Confirm `*(localPlayer+0x3FC8)` is the anim-command source handle (non-null,
      plausible ptr) while the player animates. (FillAnimCmdBitStream a4.)
- [ ] Confirm the AddSnapshot gate byte at `generator+0xD1E0` (Player+0x1B30+0xD1E0)
      and its idle vs active value.
- [ ] Determine the minimum event-buffer size for the standalone capture call
      (fields used up to +0x600; alloc >= 0x620 and verify no OOB).
- [ ] Width of the `+0x2C0..` flag block written by FillPlayer (weapon/ragdoll/IK
      NetSnapshot flags) — read the range that changes across states.
- Unblocks: "Tier-4 full-fidelity animation sync" recipe.

## 2. Aim/fire/grenade DIRECTION source [2][S useful]
- [ ] While the Second player fires the WRONG way, capture its actual shot/aim
      vector and compare against: CameraManager(0) matrix, CameraManager(1) matrix
      (0x14697AD50 slot ptrs, +0x1B0), and `Player(second)+0x1570` aim-target.
      Whichever matches = the write target (pose-seed hook vs cache vs player field).
- Unblocks: choosing the exact wrong-aim fix (pose-seed relay + resolver hook).

## 3. Task-graph command schema [A]
- [ ] Dump `g_GameTaskGraphCommandList` (0x146A514A0, 5*0x28 bytes) + count
      (0x146A51568) right after completing an objective / crossing a story beat.
      Map command TYPE (+0 = 1/2/3) -> verb, and read +0x18/+0x20/+0x24 semantics.
- Unblocks: Option-1 progression relay (which command to relay + AddCommand apply).

## 4. Interactable inspect flag [A]
- [ ] Read an interactable's state object -> `state+0xD0 bit 1` before/after
      inspecting; confirm StringId resolves the same object across both copies.
- Unblocks: Option-1 inspect/interact flag relay.

## 5. Vehicle [V][2]
- [ ] Vehicle NdGameObject: transform (world pos at kWorldPositionOffset 0x240,
      quat 0x140) + velocity offset — confirm the base-object layout on a car.
- [ ] Passenger seat: AP/index; the VehicleCtrl pointer on the car; the puppet Npc
      handle.
- [ ] Seat parenting: read a seated rider's world transform vs the vehicle's —
      confirm the rider follows the car (no separate sync needed).
- [ ] Passenger button: can `Npc_TeleportIntoVehicleSkill` (0x1406B14D0) be invoked
      directly on the puppet, or must AI be briefly re-enabled? Test seat entry.
- Unblocks: passenger button + two-copy vehicle transform relay.

## 6. Sniper-scope model cull [S][2]
- [ ] Dump the puppet's DrawControl state while it is scoped-ADS vs not; diff to
      find the visibility field that flips (IDrawControl flag / near-fade distance).
      Force it "visible" each frame on the puppet.
- Unblocks: keep aim stance AND visible puppet (vs the pose-only sidestep).

## 7. Weapon display (mostly closed) [A]
- [ ] Confirm the HELD (in-hand) weapon uses a common hand AP (so net-anim /
      UpdateFlagsAndWeapons can fix the in-hand weapon even though holstered stays
      asset/skeleton-bound).
- Unblocks: partial weapon-display improvement via the anim path.

---

## Session order suggestion
1. Load a normal combat save (covers 1,3,4,7 and, with a scoped rifle, 2,6).
2. Add the vehicle reads (5) at a car section.
3. Items 2/6 and any [2] item need both copies injected + a peer.
