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
