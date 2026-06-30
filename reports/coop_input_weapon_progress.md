# Co-op progress: clone driving, weapons, grenades — findings (2026-06-28)

Status of the effort to make the remote slot-1 Player (the peer's avatar) show weapons
in hand, fire, throw grenades, and animate. NOT committed — working notes.

## Decisions / direction
- Confirmed architecture pivot: the "2-player gating wall" was DISPROVEN (commit a7a8340)
  — two full local Players coexist once the joypad command-pool race (module+0x1839FB0)
  is serialized with an SRW lock. So the remote player is now a REAL retained slot-1
  Player, not the old NadineBuddy NPC avatar.
- User chose **Path 1 (drive the clone with the peer's full input stream)** over
  per-item replication: feed the slot-1 Player the peer's sticks+buttons+aim so the
  clone's OWN engine logic draws weapons, throws grenades, fires and animates. One
  mechanism fixes weapon-in-hand + firing + grenades + animation at once.

## Launch procedure (important; prior failures were launch bugs)
- Two tll.exe launched directly (bypass Steam). Roles auto-assign by bind order: first to
  bind 31338 = role A (local Chloe), second binds 31339 = role B (local Nadine).
  coop_peer.txt = 127.0.0.1.
- INJECT EARLY (before/at the menu, then load the save) so the spawn-capture + look-build
  + appearance hooks fire when the save loads. Injecting after the player already spawned
  → no descriptor capture, no Nadine appearance, no clone ("two Chloes, no clone").
- The scripted background two-instance inject SILENTLY FAILED for the 2nd instance several
  times (no DLL log lines for that PID). Verify each PID logs "co-op role"; re-inject any
  silent one directly with tll_injector.exe --pid <PID>.
- The remote slot-1 clone auto-spawns only when BOTH instances are in live gameplay and
  exchanging fresh peer packets (peer_is_fresh); needs g_spawn_captured (the descriptor).

## What works now
- Clone spawns in both instances ("remote Player auto-spawned for live peer", HP seeded).
- Clone is no longer "stuck": after feeding the per-slot sticks the user reported "с клоном
  всё ок" (moves/animates). NOTE: see the critical caveat below — this motion is very
  likely the POSITION PIN (apply_puppet_transform_post), not stick-driven locomotion,
  because the input block we fed reads all-zero (next section).

## What does NOT work yet
- Weapon in hand not shown on the clone; firing not replicated; grenades/explosions not
  replicated.

## CRITICAL finding: the input block at 0x66E4220 is DEAD (all zero)
- `kInputBlock0HeadingRva = 0x66E4220` was assumed to be the slot-0 resolved input block
  (sticks @ +0x00..+0x0C, buttons @ +0x518). Runtime scan (button_field_diag dumping every
  non-zero dword across the full 0x530 block, every 400ms, while the user moved/aimed/
  fired/threw) shows the ENTIRE block is **all zeros at all times**.
- Consequence: send_local_player_state was reading sticks AND buttons from a dead block →
  pkt.move_stick / look_stick / input_buttons were effectively 0. So:
  - No real input was ever replicated. REMOTE-ACTIONS never logged (buttons always 0).
  - The clone's apparent movement came from position replication (pin), not input.
- The per-slot input struct candidate `FrameState(*(mod+0x3FE1088)) + 0x6FC8` is ALSO not
  live input: it reads a static template `[+0x00]=0 [+0x04]=0xffff [+0x08]=0x101
  [+0x0C]=0x3f000000(0.5f) ...` that never changes with input. So +0x6FC8 for slot 0 is
  config/defaults, not the live per-frame input.
- => The earlier "per-slot input format calibrated / buttons mapped" claim was wrong; the
  real live input is somewhere else.

## Weapon subsystem findings (per-item path — set aside in favor of Path 1)
- Weapon controller getter = Player vtable[24] = sub_1401D4EE0 = `return *(player+0x218)`.
  At runtime `*(player+0x218)` is **NULL even during a give** (vtable[26] hook fired with
  player==slot0, *(+0x218)=0) → created lazily / only when actually armed+drawn. Health
  (+0x4A8) reads fine on the same object, so the object is the right Player.
- Give/switch entry = Player vtable[26] = sub_141BB7960 (CONFIRMED: the script
  give-player-weapon handler sub_140F95850 publishes via
  PublishStructuredStateBlockToActorVirtualD0 = vtable+0xD0 = slot 26). It routes to the
  equip core sub_141E72540(controller, &weaponId(u32), flag). With null controller the
  give is DEFERRED via sub_141623050 (schedules sub_141BB7760 to retry).
- vtable[26] is called PER-FRAME for EVERY inventory weapon (~10 ids cycling, ~2900x each
  over a run) — inventory maintenance, NOT a "held weapon" signal. Capturing the held
  weapon from it does not work. The apply sink (apply_peer_weapon_to_slot1) does fire but
  switches through the whole inventory and nothing draws.
- Conclusion: per-item weapon replication is the wrong layer; Path 1 (drive input) is right.

## Per-slot apply RE (the mechanism to feed slot-1 once the live input is found)
- FrameState = *(g_module + 0x3FE1088), RTTI FrameState, vtable RVA 0x28FB968.
- Per-slot input/camera apply = FrameState **vtable[4]** (+0x20) = sub_140289150
  (CONFIRMED via vtable dump: vtable+0x20 = 0x140289150). Signature
  (mgr, int, inputblock, slotstruct, slot). Processes camera heading + buttons/sticks
  into the resolved block and calls Input_ScaleCopyVirtual.
- Branch sub_1402893D0 (taken when the per-slot struct gate is set) reads the per-slot
  input struct (rcx): +0x10 buttons(u32), +0x14/+0x18 left stick, +0x1C/+0x20 right stick,
  +0x24 accumulator; and writes the resolved block: +0x00..+0x0C sticks, +0x508 newly,
  +0x510 released, +0x518 current buttons, +0x520 prev. (This layout is correct in the
  abstract; the problem is the concrete block at 0x66E4220 is not the live one in
  single-player — the engine routes slot-0 input through the main path, leaving the
  per-slot/split block zero.)
- Per-slot enable gates: byte @ FrameState+0x70B8+slot, struct @ FrameState+0x6FC8+0x28*slot.

## NEXT STEP (planned, not done)
- Hook FrameState vtable[4] (sub_140289150) to capture the LIVE arguments the engine
  passes for slot 0 (and any active slot): the real `inputblock` pointer and the
  `slotstruct` — i.e., find WHERE the live resolved input actually lives (it is NOT
  0x66E4220). Then:
  1. SOURCE: read the peer's live sticks+buttons from the real block/struct.
  2. SINK: feed slot-1 via the same per-slot struct + gates + slot-1 player binding, so the
     clone's own update graph runs (weapon/grenade/fire/anim).
- Open question to resolve at the same time: in single-player only slot 0 is "active"; need
  to confirm whether activating slot 1 (gates + binding + feeding its real block) makes the
  engine run the clone's full input-driven graph, and reconcile with the position pin
  (input-driven movement vs pinned transform — likely keep pin for position, feed input for
  animation/actions only).

## Code state (uncommitted, working tree)
- StatePacket v4 + weapon_id (currently fed from g_local_active_weapon_id, which is the
  cycling vtable[26] id — unreliable; effectively a no-op visually).
- send_local_player_state reads sticks/buttons from 0x66E4220 (DEAD block → zeros).
- drive_remote_slot1_actions_tick feeds the per-slot struct @ FrameState+0x6FC8+0x28 with
  peer sticks+buttons (but peer values are ~0 due to the dead source block).
- apply_peer_weapon_to_slot1 via Player vtable[26] (g_sync_weapon) — fires but ineffective.
- Diagnostics live: button_field_diag (BTN-DIAG, g_btn_diag), hooked_give_weapon
  (vtable[26], WEAPON local/GIVEWPN), dump_weapon_controller ('[' key, WPN-DUMP),
  g_enable_client_death_publish toggle ('~').
- Reload guard world_stable()/reset_world_caches_if_player_changed() in net_tick (good).

## CRASH ROOT-CAUSE: native anim serializer hits a campaign data wall (2026-06-30)

The in-process ANIM-MIRROR test (Scroll Lock) CRASHES on the first toggle. Root
cause found statically (no further runtime needed):

- Crash is INSIDE `SelectSpawnerOrGroupEntryByValue 0x140A40AB0`, called from
  `FillAnimCmdBitStream` at the return site `0x140BC56E1`. (The "RIP" in
  `pseudo_anim_mirror_crash.txt` is that return address, not the fault.)
- `FillAnimCmdBitStream(event,bitstream,Player,artGroups,1,7)`: prologue asserts
  `Player` (r8) and `artGroups` (r9->r14) non-null (`pArtGroups` assert @0x7e7). It
  reads the player's live anim from `Player+0x1A0` (0x140BC4F40) and builds the
  command list itself — so the command list is NOT the problem.
- Per anim command (`movzx eax,[rsi]`, type 6 branch) it calls
  `SelectSpawnerOrGroupEntryByValue(out,out,value=*(cmd+0x18),cmd, a5=r14=artGroups)`.
  Inside, for every NON-null entry `v12` of the 32-pointer art-group table it derefs
  `v14 = *(QWORD*)(*(QWORD*)(v12+0x48)+0x10)` then `*(DWORD*)v14`. When `a4+56`
  chain is null, `v9=0`, so EVERY populated entry takes this deref.
- The art-group entries built in CAMPAIGN by `Player_InitializeNetAnimArtGroups
  0x1410D6530` (gate byte `0x3E07473` = HIBYTE(dword_143E07470), correct) resolve by
  id via `NetSnapshot_GetAnimArtGroup 0x140B87820` -> Knowledge Registry, but come
  back STRUCTURALLY INCOMPLETE in single-player: `entry+0x48` (the spawner/group
  sub-table) is null/garbage. => deref crash.

CONCLUSION (STATIC, high confidence): the native MP animation serializer cannot run
in the SP campaign because the `*net-player-anim-art-groups*` art-defs are not loaded
with their spawner/group sub-tables in single-player. Force-building the table via the
MP-mode gate produces id-resolvable but incomplete entries. This is a DATA-AVAILABILITY
wall, not a fixable offset. Using the generator's own table (generator+0x50) instead of
Player+0x3FC8 does not help — same `GetAnimArtGroup` registry source.

GUARD ADDED (no crash anymore): `mirror_local_animation_snapshot_to_clone` now walks
the exact chain (`entry`, `entry+0x48`, `*(+0x48)+0x10`) for all 32 entries and bails
with `ANIM-MIRROR campaign art-group wall: populated=N/32 malformed=M ...` instead of
calling FillAnimCmdBitStream when any populated entry is malformed. Toggle is OFF by
default; the build is safe.

IMPLICATION FOR DIRECTION: the native snapshot-replication path (Codex) is blocked by
MP asset availability. The asset-free alternative is the user's originally chosen
**Path 1 — drive the clone with the peer's INPUT** so the clone's OWN campaign anim
graph runs (no MP art-defs needed). See memory coop-architecture-direction
(camera-owner drive @ +0x36C0 via sub_140238800 makes fed input animate the clone).

## CORRECTION: 0x66E4220 is NOT dead — it was the neutralized BACKGROUND instance (2026-06-30)

Reversed `InputManager_NormalizeStick 0x141A99FD0(pad, dst=module+0x66E4220)` and the
per-slot apply branch `FrameState_PerSlotInputApply_Branch 0x1402893D0`. Confirmed the
LIVE slot-0 resolved input block layout at `module+0x66E4220`:
- +0x10/+0x14 = normalized RAW left stick (LX/LY), /32768 clamped (NormalizeStick writes here)
- +0x18/+0x1C = normalized RAW right stick (RX/RY)
- +0x00/+0x04 and +0x08/+0x0C = camera-adjusted L/R pairs (via sub_141A99CE0)
- +0x518 (1304) = buttons u64, built from pad+0x36 (XINPUT wButtons) with a **base
  0x8000000000000000** (bit 63 ALWAYS set once written) | per-button bits.

KEY: because +0x518 always has bit 63 set when the writer ran, an "all zeros" reading
of the whole block is IMPOSSIBLE if NormalizeStick executed. So the earlier
"0x66E4220 reads ALL ZEROS / dead block" result was NOT a property of the block — it
was the **background instance** in the one-PC loopback test, whose controller is
deliberately neutralized (loopback filter) so NormalizeStick never runs for it. In
real two-PC co-op each instance is foreground on its own PC -> its own 0x66E4220 is
live. The "dead block" conclusion is RETRACTED; partly also a wrong-offset read
(scanned +0x00/+0x04, but the raw stick is at +0x10/+0x14).

Per-slot apply (sub_1402893D0) CONFIRMED: input struct rcx +0x10 buttons, +0x14/+0x18
L-stick, +0x1C/+0x20 R-stick, +0x24 accumulator -> writes output block sticks to BOTH
+0x00..+0x0C AND +0x10..+0x1C, buttons +0x518/+0x508(pressed)/+0x510(released)/+0x520(prev).
Slot 0 (Chloe) does NOT go through this branch (main input path); activated slot 1 DOES
(prior breakthrough: setting slot-1 gate made the clone move) -> feeding slot-1 here
drives the clone's OWN locomotion/anim graph.

IMPLICATION for apply_puppet_transform_post: keep it as the position authority, and
ADD input-driven animation — feed the slot-1 per-slot input struct from the peer's
replicated sticks/buttons (read locally from 0x66E4220 at the CORRECT offsets above) so
the clone animates run/walk/aim via its own graph while ..._post pins world position
(soft-correct to avoid fighting the input-driven motion).

## CONSUMER SIDE CONFIRMED: slot-1 input -> clone locomotion/anim, pin-compatible (2026-06-30)

Traced the full input->animation chain for a slot-bound Player:
1. Per-slot input struct @ FrameState+0x6FC8+0x28*slot (buttons +0x10, L-stick +0x14/+0x18,
   R-stick +0x1C/+0x20, accumulator +0x24).
2. Input_PerPlayerDispatch_MgrArg 0x14179E6C0 / Input_FocusDispatch 0x14179E310: loop over
   6 slots, for each call mgr.vtable[4]=sub_140289150(mgr,*mgr,&block[0x530*localIndex],
   slotstruct,slot). localIndex = slotstruct[+24]; block stride 0x530 (166 qwords).
3. sub_140289150: if per-slot struct gate (FrameState+0x6FC8+0x28*slot) set ->
   sub_1402893D0 reads the struct, writes the resolved block @ 0x66E4220+0x530*slot, then
   Input_ScaleCopyVirtual.
4. Input_ScaleCopyVirtual 0x14183B410(dst=slotstruct, src=block, scaleSel): writes a
   SENSITIVITY-SCALED copy of the block into the slotstruct dst (scaled L-stick -> dst+0x10).
   slotstruct = mgr+0xE88+slot*0xBF0 — this is what the Player reads each frame.
5. The Player bound to the slot consumes the slotstruct move-intent -> locomotion -> anim
   graph (run/walk/aim); velocity is the OUTPUT written to Player+0x260 during the LegIk pass.

KEY CONCLUSION: the run/walk anim blend is driven by the STICK move-intent (slotstruct),
NOT by the actual world position or the +0x260 velocity output. Therefore hard-pinning the
world transform in apply_puppet_transform_post does NOT block input-driven animation. The
earlier "no run animation in this mode" was because net mode fed NO slot-1 input
(g_drive_slot1=false) and zeroed velocity — not an inherent pin/anim conflict.
RUNTIME CORROBORATION (initial-findings 2026-06-16): setting the slot-1 gate alone made the
spawned clone RUN in a circle (animated locomotion purely from slot-1 input).

DESIGN (confirmed sound): keep apply_puppet_transform_post as position authority; ADD a
slot-1 input feed derived from the peer's move direction (stick from peer velocity/heading)
+ slot-1 gates, so the clone's own graph animates run/walk/aim/weapon while ..._post pins
the authoritative position (soft-lerp so the input-driven local motion doesn't fight it).

## SINK fully specified + spin cause found (2026-06-30, disasm of sub_140289150)

Control flow of the per-slot apply sub_140289150(mgr,vtbl,block,slotstruct,slot):
- 0x1402891C3 `cmp byte [FrameState+0x70B8+slot],0`; if SET -> DEMO SPIN branch: heading
  = cos/sin(EngineClock_ticks * g_EngineClockToSeconds) written as the stick, then the
  gate **clears itself** (0x14028929C `mov byte [..+0x70B8],0`) = ONE-SHOT.
  => The old "clone runs in a circle" was the DLL RE-SETTING this byte EVERY frame
     (g_drive_slot1 set the 0x70B8 gate per tick) -> demo spin re-triggered each frame.
     This byte gate must NOT be used for clean control.
- 0x140289380 `lea rax,[rdi+rdi*4]` (slot*5); 0x140289384 `lea rcx,[FrameState+0x6FC8]`;
  0x14028938B `cmp byte [rcx+rax*8],0` = struct[+0x00] enable @ FrameState+0x6FC8+0x28*slot;
  if non-zero -> sub_1402893D0(struct) reads real input. Else plain Input_ScaleCopyVirtual.

PER-SLOT INPUT STRUCT (write to drive slot 1 = FrameState+0x6FC8+0x28*1 = FrameState+0x6FF0):
- +0x00 : enable byte = 1  (REQUIRED — gates the real-input branch)
- +0x0C : float threshold (compared to the +0x24 accumulator)
- +0x10 : buttons (u32)
- +0x14 : left stick X   +0x18 : left stick Y (-1 = forward)
- +0x1C : right stick X  +0x20 : right stick Y
- +0x24 : accumulator (engine-managed; don't clobber)
- DO NOT set FrameState+0x70B8+slot (that is the demo-spin one-shot).

For a PINNED clone: facing comes from the quat pin; run animation comes from stick
MAGNITUDE. Feed left stick = forward with magnitude = peer_speed/max_speed -> clone plays
forward-run in its pinned facing. Direction refinement (camera-relative stick) is optional
and can use the clone's camera-owner yaw later; not needed for basic run/walk animation.

NET: every piece to develop apply_puppet_transform_post with animation is now reversed —
SOURCE (local 0x66E4220 offsets), SINK (FrameState+0x6FF0 struct above), CONSUMER
(confirmed animates), PIN-COMPAT (anim is stick-driven, not position-driven).

## Fire/weapon from buttons: chain mapped, trigger is virtual-dispatched (2026-06-30)

Fire execution chain (climbed from the confirmed core):
  PlayerFireController_Fire 0x140E2C730
   <- sub_140E2BCD0 (execute-shot: effects, camera shake, projectile materialize; gates
      Fire on v120 = weapon/ammo via *(player+0x108) vtable+0x8C0)
   <- sub_140E29300 (called ONLY via vtables 0x142DF8E34 / 0x142DF8EC0 / 0x146E384D0 /
      0x142A23590 -> a virtual method on the fire/weapon controller).
So the "decide to fire" (read the R2/trigger button) happens in the weapon controller's
per-frame tick, dispatched VIRTUALLY -> the same indirect-call wall noted for the
control->player binding. Not statically resolvable past here without runtime.

ARCHITECTURAL CONCLUSION: the clone is a real Player owning its full weapon/fire
controller. Its own controller tick reads ITS per-slot resolved input (block[localIndex=1]
+0x518 buttons) and fires — the SAME per-slot path that already drives its locomotion
(proven: slot-1 input made the clone run). Locomotion routing per-slot strongly implies
actions (fire/aim/jump/reload/grenade) route identically. DEFINITIVE confirmation = runtime
exec-bp on PlayerFireController_Fire while feeding the slot-1 fire button (R2 bit in
struct+0x10 -> block+0x518), checking it fires with rcx/owner = the clone (not slot 0).
This is a runtime check, not static.

## Camera-yaw: NOT needed for the pinned clone; do NOT drive sub_140238800 (2026-06-30)

sub_140238800 (UpdatePlayerAnimatedCameraControlOwnerAndRouteState, owner=player+0x36C0)
is a HEAVY worker that rebuilds route-child state and MATERIALIZES gameplay camera objects
under FrameState 0x143FE1088 (CameraAnimationDirector / animated-camera-control). Driving
it per-frame for the clone spins up a second gameplay camera with side effects (the same
second-camera coupling that confused the earlier aim-isolation tests). For a PINNED clone
this is unnecessary and counter-productive: facing comes from the quat pin, and the
run/walk leg animation comes from stick MAGNITUDE. The camera yaw only affects which
locomotion blend quadrant (forward vs strafe vs back) is chosen from the stick direction;
feeding a forward stick gives forward-run, and any quadrant refinement is a runtime tuning
step, not a static blocker. DECISION: do NOT drive the clone's camera owner; feed
forward-aligned stick + keep the quat pin.

## REVERSE COMPLETE for apply_puppet_transform_post + animation
Everything needed is now reversed; remaining unknowns (clone fires from slot-1 button;
exact blend quadrant) are RUNTIME-only. Implementation spec:
- SOURCE (local, own instance): read 0x66E4220 left stick +0x10/+0x14, buttons +0x518; or
  derive run magnitude from peer velocity (already computed from position delta).
- SINK (per frame, slot 1 = clone): struct @ FrameState+0x6FF0: +0x00=1 (enable),
  +0x14/+0x18 = left stick (forward = magnitude peer_speed/max), +0x1C/+0x20 = right stick,
  +0x10 = buttons (fire/aim/grenade/reload from peer). Do NOT set FrameState+0x70B8+1.
- POSITION: keep apply_puppet_transform_post as authority but SOFT-LERP world/local/quat
  toward peer (don't zero velocity hard) so input-driven motion doesn't fight the pin;
  hard-snap on large deltas (teleports/scripts). Keep alt-tab freeze + HP apply.
- Do NOT drive sub_140238800 (no second camera).

## IMPLEMENTED (2026-06-30, built OK)
Per the reversed spec, in scanner/src/teleport_hook.cpp:
- drive_remote_slot1_actions_tick now ALSO writes the L/R sticks into the slot-1 struct
  (FrameState+0x6FF0): +0x14=0, +0x18=-mag (mag = clamp(|peer horiz velocity|/kCloneRunSpeedNorm,
  0,1)), +0x1C/+0x20 = peer.look_stick. Enable +0x00=1, buttons +0x10, spin gate 0x70B8 forced 0,
  slot1_index bound to 1. (Previously only buttons were fed -> clone "stuck".)
- apply_puppet_transform_post: hard pin + zero-velocity REPLACED by soft lerp (0.35) toward peer
  pos + hard-snap on >4m delta; quat authoritative; velocity left to the clone's locomotion.
- aux thread net branch: g_puppet_net_apply=true (enables both the slot-1 feed and the soft
  position application) when a clone exists and not play-as-clone.
- kCloneRunSpeedNorm=4.0 (tunable). Run magnitude derives from peer VELOCITY (position-delta),
  which replicates even under the loopback background-input filter -> locomotion anim testable on one PC.

FALSIFIABLE PREDICTION (two instances): when the peer moves, the remote clone plays
walk/RUN animation (legs cycle), facing the peer's direction, holding the authoritative
position smoothly (no freeze, no flicker, no spin). If wrong: clone slides without leg
animation (stick not consumed), or spins (spin gate leaked), or jitters (lerp vs locomotion
fight). Buttons (fire/aim) may NOT replicate under one-PC loopback (background input block
neutralized) but WILL on real two-PC; movement anim works either way (velocity-driven).

## RUNTIME DEBUG SESSION (2026-06-30) — input-drive attempts, findings + open wall

Implemented the reversed input-drive and ran 7 two-instance loopback tests. Results:

CONFIRMED FIXED:
- StatePacket velocity was always 0: send_local_player_state read Player+0x260 (reads 0 on
  the retained proxy). Now derived from POSITION DELTA in send -> peer.velocity is correct.
- The per-slot INPUT pipeline WORKS end to end: writing the slot-1 struct
  (FrameState+0x6FC8+0x28) is consumed by the engine's per-slot apply and lands in the
  resolved block[1] (0x66E4220+0x530). Diagnostic readback confirmed block1 sx/sy == fed
  stick (one frame delayed), btn=0x500. So input reaches the resolved block.

KEY RUNTIME FINDINGS:
- F9 byte-gate spin test: forcing FrameState+0x70B8+1 (one-shot, re-set each frame) with the
  struct branch OFF makes the clone RUN IN A CIRCLE = it TRANSLATES + ANIMATES. So the clone
  DOES consume slot-1 input and can move/animate. The byte-gate path WORKS; the struct path
  does not, even though both write block1 + call Input_ScaleCopyVirtual.
- SLOTSTRUCT dump (mgr+0xE88+slot*0xBF0): slot-0 (Chloe) is ALL ZEROS even while she runs ->
  Chloe uses the MAIN input path, NOT the per-slot slotstruct. The clone (slot-1) DOES use the
  slotstruct. Comparing slot-1 moving (byte-gate) vs not (struct), the differing fields are:
  +0x08/+0x10 (a counter, 0x40500->0x50500) and +0x54/+0x5C (0x4389d9f1 ~ an angle/heading).
- Input_ScaleCopyVirtual writes slotstruct+0x54 = block+0x0C; sub_1402893D0 writes
  block+0x0C = struct+0x20 (right-stick Y). So the byte-gate fills slotstruct+0x54 (spinning,
  from engine time) and the struct path left it 0 (right stick zeroed).

REJECTED THIS SESSION:
- "feed the heading via the right stick (struct+0x20 = atan2(sy,sx), struct+0x1C=0.5)" so
  slotstruct+0x54 carries a constant heading -> clone runs straight. RESULT: clone JUST STANDS
  (no movement, no anim). So slotstruct+0x54 alone (right-stick value) is NOT the movement
  activator, or the convention/units are wrong, or the +0x08/+0x10 counter (a timestamp the
  byte-gate also writes) is required too. The "struct branch != byte-gate branch" gap is NOT
  closed.
- Soft-lerp pin + byte-gate did NOT hold the clone in place (it still ran in a circle); and a
  pin fundamentally fights locomotion animation (which needs actual translation/rotation). So
  "pinned + animated run" via apply_puppet_transform_post looks structurally hard: the run
  blend needs real movement.

OPEN WALL / NEXT (for Codex):
- WHY does the byte-gate branch drive the clone's locomotion but the struct branch
  (sub_1402893D0) does not, when both produce a populated block[1] + ScaleCopy to the same
  slotstruct? Capture slot-1's FULL slotstruct (0xBF0 bytes) under byte-gate vs struct and diff
  ALL fields (not just first 0x60) — the activator is a field the byte-gate sets that the
  struct path leaves 0 (candidates: the +0x08/+0x10 timestamp pair, +0x54/+0x5C heading, or a
  flag past +0x60). Then set that field on the struct path.
- Alternative: drive the clone's MOVEMENT via the byte-gate mechanism but neutralize the spin
  by controlling the heading source (FrameState+0xE0 is a shared frame counter -> can't freeze;
  find a per-slot heading input instead).
- Reconsider: locomotion animation needs real translation; the position pin
  (apply_puppet_transform_post) cancels it. A free-run + gentle-correction model (not a hard
  pin) is likely required for visible run/walk.

Code state (committed): drive_remote_slot1_actions_tick feeds slot-1 struct (sticks from peer
velocity, heading via right stick, buttons, both gates), with diagnostics (REMOTE-DRIVE,
SLOTSTRUCT, F9 byte-gate spin test, 'P' free-run toggle). apply_puppet_transform_post has a
g_clone_freerun mode (no position write, snap-only) vs soft-lerp pin. send computes velocity
from position delta. None of these yet produce a visible animated/moving clone in two-instance.

## STATIC CORRECTION (2026-06-30) — previous SLOTSTRUCT comparison was misaligned

The exact dispatch instructions at `0x14179E7A3..0x14179E807` correct the object layout:

- `rbx = FrameState+0xE88 + slot*0xBF0` is only the loop cursor;
- the fourth argument passed to `FrameState_PerSlotInputApply` is `rbx-0xD8`;
- therefore the real per-slot `Joypad` object is
  `FrameState+0xDB0 + slot*0xBF0`;
- the input-block selector is cursor `+0x18`, equivalently `Joypad+0xF0`.

This agrees independently with the `Joypad` constructor/reset family
`0x141836640/0x1418369C0/0x141836BB0` (object size through `+0xBE8`) and the earlier
confirmed command-pool address `FrameState+0xDB0+0xBF0*index`.

**REJECTED:** the earlier interpretation of differences at apparent
`slotstruct+0x08/+0x10/+0x54/+0x5C`. Those values came from memory `0xD8` past the
real object. Consequently the right-stick-as-heading experiment derived from `+0x54`
has no valid static basis.

**STATIC:** the only global writes unique to the circular-motion gate are
`BYTE1(dword_143ECEAA4)` and `HIBYTE(dword_143ECEAA4)`. Debug-menu construction at
`0x140306DE0` identifies those bytes as `Infinite Ammo` and `Infinite Grenades`.
They are cheats, not slot activation. Enabling both the circular-motion gate and the
scripted-input branch merely runs `Input_ScaleCopyVirtual` twice and contaminates the
comparison.

The implementation now keeps the circular-motion gate off during normal replicated
input, zeroes the unsupported right-stick heading, and captures/diffs all `0xBF0`
bytes of the correctly addressed slot-1 `Joypad`. The next runtime test must compare
that result under the two mutually exclusive branches; no field should be promoted to
an activator until the corrected diff and its writers agree.

## RUNTIME CORRECTION (2026-06-30) — no persistent Joypad activator

With both remote Players alive, the corrected full-object capture completed in both
processes. Each reported `JOYPAD-DIFF total=0`: after allowing either branch to run,
the sampled `0xBF0`-byte slot-1 `Joypad` contained no persistent difference. This
does not prove that transient callback-time values are identical, but it falsifies
the proposed persistent activation-field theory and closes the full-structure poke.

An attempted hardware execution breakpoint at the common
`Input_ScaleCopyVirtual` exit (`0x14183C9C6`) was **REJECTED**. It was armed across
63/67 threads, produced no capture record, and both processes then terminated. The
probe has been completely removed; do not repeat mass debug-register installation
for this hot function.

Static disassembly also narrows the remaining difference. Both branches call the
same `Input_ScaleCopyVirtual` with the same block and `Joypad`; helper
`0x140289A90` is pure bounded blend arithmetic. Apart from the unrelated ammo/grenade
cheats, the circular branch differs only in the eight source floats and button edge
fields it writes to `block[1]`. The next safe probe logs those eight floats from the
previous circular frame without breakpoints, then reproduces their valid ranges in
the scripted branch.

## ROOT CAUSE FOUND: remote clone has no screen-1 camera (2026-06-30, continued)

User's decisive clue: each player runs+animates fine in ITS OWN window (local, slot-0,
with its camera), but appears as a TELEPORTING remote clone in the other window
(symmetric for both Chloe and Nadine). play-as-clone works because swap_local_player_slots
just swaps the two retained-table entries -> the clone becomes slot-0 and inherits the
existing camera.

Ground-truth BLOCK1 capture (byte-gate f9=1 vs struct f9=0):
- byte-gate (clone MOVES): block axes = [cos, sin, 0.5, -0.72, (dup)], btn=0x500
- struct (clone STANDS):  block axes = [sx, sy, 0.5, 0, (dup)], btn=0x500
The only block diff was right-stick Y (+0x0C): -0.72 vs 0. REJECTED: feeding
struct+0x20 = -0.72 (and +0x1C=0.5) to match the byte-gate's block did NOT move the
clone. So the input VALUES are not the activator (consistent with Codex's JOYPAD-DIFF=0
on the corrected FrameState+0xDB0 Joypad).

REAL ROOT CAUSE (static): the remote clone is slot-1 / screen-1. Its movement heading
comes from a CameraManager. CameraManager::Get = ResolveScreenRouteEntityByIndex
0x141C06420 supports index 0..1 (kNumScreens=2, table xmmword_14697AD50[2]), but in
single-player campaign only SCREEN 0 is materialized; CameraManager(1) is NULL. Our
get_camera_axes reads CameraManager(idx)+0x1B0 (rotation quat) to convert stick->world.
With no screen-1 camera the clone's locomotion cannot resolve a world move direction ->
no translation -> and locomotion run/walk animation needs real translation -> clone
just stands / teleports (position snap). This is the dormant split-screen camera system.

WHY the byte-gate still moved it (circle): TBD — its spinning heading is written into the
block directly; it may bypass the per-screen camera lookup or fall back to screen 0's
camera. Worth confirming, but the structural blocker for clean directional movement is the
missing screen-1 camera.

NEXT (static): (a) reverse how the player's locomotion resolves its move heading — does it
read CameraManager(playerScreenIndex) or a player-local camera owner (Player+0x36C0)? (b)
determine the minimal way to give slot-1 a screen-1 camera (or a heading field) set to the
peer's facing, WITHOUT enabling full split-screen rendering. Player+0x36C0 = per-player
camera-control owner (owner+0x84 = screen index); sub_140238800 materializes screen-1's
camera (heavy). Candidate: write the clone's camera-owner heading directly = peer yaw.
- Alternative architecture to weigh: drive the clone's locomotion/anim STATE directly
  (apparent ground speed -> run blend) bypassing the camera+input chain, while pinning
  position. Needs the locomotion blend's speed input field (distinct from the +0x260
  velocity OUTPUT).
