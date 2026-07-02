# TLL Campaign Co-op — Status & Findings (2026-06-28)

Goal (north star): a fully-playable **second player as Nadine** in the *Uncharted:
The Lost Legacy* campaign (`tll.exe`), via runtime DLL injection — eventually a
two-PC networked co-op.

All work is **runtime injection** (`scanner/src/teleport_hook.cpp` + `tll_injector.exe`).
No asset/.pak modding (rejected as out of scope and because it would change *both*
players, not just the clone).

---

## CURRENT PRIORITY: complete player synchronization first

The current acceptance criterion is no longer a moving positional proxy. Both
copies must reproduce the other player completely: locomotion and traversal,
look/aim, weapons and firing, melee, interactions, health, death, respawn, and
campaign lifecycle. Enemy synchronization is explicitly deferred until this is
complete.

Current blocker: the remote Player runs in place. Applying peer movement immediately
after `PlayerInputCommandState_Update 0x141E39FB0`, before the remainder of Player
method 16, still produced only centimetre-scale root changes. This rejects timing at
`0x140C9FE4F` as the missing translation stage. The real shared root writer is now
located: `Character_ApplyAnimationRootTransform 0x141DE8EC0` writes
`Player+0x130/+0x140`, but its direct branch is gated by Player virtual `+0x5E8`
(`0x141BC2EF0`). The current build only compares that predicate, `m_pNetController`
at `Player+0x348`, and root delta between local and remote Player.

The present v2 packet carries position, quaternion, velocity, both stick pairs and
the complete 64-bit button mask with press/release edges. It is still insufficient:
one-shot actions need a reliable ordered event stream and authored gameplay state.

The immediate blocker is the remote actor. `NadineBuddy + MoveTo` cannot represent
all Player actions. A second retained `Player` has the required graph and mechanics.
The earlier claim that its mere presence gates local aim/fire was not cleanly
isolated: the supposedly passive test automatically ran the slot-1 camera owner
every frame. Player presence, slot-1 input, and the second camera must now be tested
independently.

The corrected first matrix case is complete: with slot-1 input, split login, and
camera 1 all off, Chloe can aim normally while a second retained `Player` exists.
The next failure is narrower: two Player jobs concurrently mutate the shared
`joypad-2` disabled-command manager. Its fixed free count underflows to `-1/-2` and
crashes at `0x14183A090`. Both per-slot pools were initialized correctly. The mod
now serializes the original mutation function without changing command semantics.
A fresh load with slot 1 survived for more than two minutes with valid pool counts,
so this crash is closed.

The current build creates a remote retained `Player` only after fresh packets from
the second copy arrive. Role A renders that object as Nadine; role B keeps it Chloe.
State application continues while either process is in the background, and the old
NPC `MoveTo` network avatar is disabled to avoid duplicate remote actors.

Latest static correction: the real per-slot `Joypad` starts at
`FrameState+0xDB0+slot*0xBF0`; `+0xE88` is only a dispatch cursor `0xD8` bytes into
it. The last runtime comparison therefore inspected the wrong memory and its
heading/activation conclusions are rejected. The circular-motion branch's unique
global writes are the built-in Infinite Ammo/Grenades flags, not activation. The
next controlled run compares all `0xBF0` bytes at the corrected address with the
circular and scripted branches kept mutually exclusive.

That comparison is now complete: both processes reported `JOYPAD-DIFF total=0`,
so the persistent-field activation theory is rejected. A mass hardware breakpoint
on the hot copy routine terminated both processes and has been removed. The safe
`block[1]` capture is also complete: matching the circular branch's differing
right-stick values in the scripted branch still did not move the remote Player.
A later claim that `CameraManager(1)` is absent was rejected because both screen-route
entities were already confirmed live in campaign. The current static task is to find
the movement consumer that combines `Player+0x4010` with the selected route pose.

In the one-PC two-instance test, both processes were polling the same XInput user 0,
which coupled their local cameras despite no camera state being applied over the
network. Loopback mode now neutralizes controller input only in the background
process. This test-only filter is disabled for real two-PC connections.

The alt-tab false-death path is now validated fixed. When a background instance
pauses packet production, its remote replica keeps the last confirmed transform
and health instead of falling under local physics. Repeated window switching no
longer killed either player; both processes remained responsive. Four native Player
death entries (`+0xBE8..+0xC00`) are additionally guarded for a live peer, but none
was reached in the successful run, confirming transform hold as the effective fix.

Enemy snapshots, enemy damage relay, and mirrored enemy death remain preserved but
are disabled by default during player work.

---

## Previous Direction: Variant B (superseded reference)

The earlier conclusion that a second retained Player inherently breaks local aim
was disproved by the clean matrix test. The actual delayed crash was the concurrent
joypad command-pool mutation described above. This section remains only as history;
the active route now uses a full remote Player in each process.

**Variant B (correct co-op model):** each PC runs its OWN single full local Player;
the other player is a lightweight non-Player **avatar** (visual mirror via the net),
NOT a local Player. On player-2's PC the local player is made **Nadine** (full Chloe
mechanics + Nadine body) — the original goal, achieved on the 2nd player's own machine.
Each machine has 1 local player => no gating.

- **Step 1 — DONE & VALIDATED:** apply the appearance hook to the LOCAL player (not a
  clone) -> the local player renders as Nadine AND weapons/aim/everything work. Flag
  `g_local_make_nadine` (Player vtable[92] -> 0xd27c074f); no clone is spawned.
- **Step 2 — TODO:** lightweight remote avatar (non-Player; base on NPC NadineBuddy or
  render-only), driven by replicated state; does not trip player-count gating.
- **Step 3 — TODO:** net layer (replicate transform + pose/input); dev/test via a
  fake-peer loopback since Steam blocks two `tll.exe` on one PC.

(The "clone" sections below are the prior approach, kept for reference.)

---

## ✅ WHAT WORKS (validated in-game)

### 1. Second player spawn (the "clone")
A real second `Player`-class actor is spawned in campaign via the retained-spawn
pipeline (`SpawnPlayerFromRetainedDescriptor`, RVA `0x10F1910`), reissued with
`idx=1` using spawn args captured by a HW breakpoint. The clone is a genuine
`Player` (vtable RVA `0x2A4BFE8`) — full player mechanics when controlled.

### 2. Clone renders as NADINE ★ (the headline result)
- The player **appearance hash** lives at `object+0x15A4` (returned by Player
  `vtable[92]` = `sub_140A1DCC0`), and feeds the look builder
  (`sub_141BB5E00` → `sub_141804170`, `ndlib/render/look.cpp`) → appearance-record
  lookups → **mesh**. This is the look/mesh layer, *separate* from the skeleton
  record.
- Chloe appearance hash = `0x3f334972` (`hash("chloe")`); Nadine = `0x0f6d80f3`
  (`hash("nadine")`).
- **FIX**: hook Player `vtable[92]` → `hooked_appearance`; during the clone's spawn
  window (`g_appearance_swap_active`, set around the reissue `spawn()` call) force
  the appearance hash to Nadine's. Main Chloe (slot 0) is untouched. Logged as
  `APPEARSWAP armed ... 0xd27c074f`.
- Earlier wrong levers (recorded so we don't repeat): `+0x170/+0x190` = hair/skeleton
  metadata (hair-only / broke body); Look2-rdx swap fed the wrong arg; record-resolve
  redirect broke the body; co-op gate crashed.

### 3. Clone collision
- The clone's composite/collision body (`player+0x238`, 12 colliders) is **built and
  byte-identical to Chloe's** (`InitializeCompositeBody` RVA `0x10CF470` runs for it).
- The "walks through walls / ghost" symptom was an **artifact of the transform-drag**
  (we used to teleport the clone by writing its position = bypasses collision sweeps).
- **FIX / rule**: drive the clone by its **own locomotion via input** (not by writing
  its transform) → it collides with walls/props/NPCs correctly. The old
  `apply_puppet_transform_post` drag is disabled by default.
- Remaining minor: clone passes through **Chloe** (player-vs-player) but collides with
  NPCs — likely a havok same-group filter; usually intentional in co-op.

### 4. Clone animation (incl. from player-1's view)
- The clone animates fully when driven by its locomotion. The "frozen from player-1's
  view" symptom was ALSO the transform-drag (a teleported clone can't animate). With
  locomotion-driven input it animates regardless of which camera is active.

### 5. Control of the clone (local)
- `NUMPAD3` (play-as-clone): camera → clone (via `swap_local_player_slots`) + scripted
  slot-1 input drives it (`drive_slot1_tick`, reads WASD).

---

## ⚠️ OPEN / KNOWN-LIMITED (local single-PC artifacts)

These are **artifacts of testing on one PC with one input set**, NOT architectural
problems for the real two-PC co-op:

- **WASD moves both** Chloe and the clone: the game routes all local input to the
  active player (Chloe). Reliably suppressing Chloe locally failed (input-suppression
  is mistimed; position-pin crashed on freed objects). In **networked** co-op this
  does not occur — each PC drives only its own local player; the remote clone is
  driven by **replicated input** injected only into slot-1.
- **Janky movement direction**: locally we mapped the stick through the clone's
  unsynced screen-1 camera. In networked co-op we replicate **world-space** movement
  (camera-independent), so this specific jank does not recur (but the network drive
  must be built correctly).
- **FPS drops ~2x when the camera is switched to the clone**: tied to the camera-swap
  (a second viewport/camera). In networked co-op the camera **never** switches to the
  remote player (each PC's camera stays on its own player), so this does not occur.
- **Clone is one-shot**: it does not re-spawn after a location reload (safe respawn
  needs fresh-but-valid spawn args + an asset-stream wait, and must never write freed
  objects — a naive version caused a bald-Chloe clone + crash).

---

## ❌ HARD WALLS (engine architecture / dormant systems)

- **Body via data**: a Player's base body is the `"Player"` char-class (= Chloe),
  data-bound. Every runtime lever to change the *base* body failed; the **look/
  appearance** layer (above) is what we use instead. (.pak modding would change both
  players — rejected.)
- **Manual enemy kill via `AiHitController`**: closed as unsafe. HP lives at
  `enemy+0x4A8 -> HealthSystem`, current HP `+0x3C`, but HP=0 does not itself
  transition the enemy to a corpse. `Die(false)` only knocks down; replayed
  `TakeHit` crashes; `Die(true)` crashes. Static reverse confirms the proper
  HealthSystem setter still only updates HP/history and does not perform death.
  The native upper path is now found: `0x14027A270(AttackInfo*, target)` publishes
  the authored Attack event, `0x14040E7C0` extracts its AttackInfo, and character
  vtable `+0x1170` (`0x140409F70`) performs armor/health/knockdown/death logic.
  AttackInfo is polymorphic and large (`0x860` base, `0x880` melee, `0x8C0`
  projectile); the old `0x100`-byte replay was invalid and is disabled. Each
  type has a virtual heap clone at `+0x8`. The complete
  clone -> `0x14027A270` -> virtual delete prototype is implemented and builds.
  More importantly, the fresh generic `AttackInfo` path has now been validated
  live: it removed enemy health, returned normally, and required no captured
  template. Nadine's live process handle was accepted as the source. The test
  switch is disabled again after validation. Publisher lifetime is confirmed
  synchronous. Static accessors show the stale fields explicitly:
  attacker handle at `+0x08/+0x10`, damage-point count at `+0x24`, points at
  `+0x30` stride `0x50`, each point's hit-object handle at `+0x30/+0x34`, and
  point damage at `+0x40`. The fresh-object path is now recovered: total damage
  is `AttackInfo+0x20`, ordinary armor calculation is `0x1402942A0`, and an
  unmapped zero-handle point preserves full health damage. The probe now chooses
  the nearest live enemy by 3D distance, logs HP before/after, and triggers only
  once per physical key press; this target-selection correction still needs one
  in-game confirmation. Production relay must still provide validated target
  identity across the network.
- **Two-instance enemy death:** a client kill reached the host and synchronized
  back, proving the complete damage relay route executes. The original Y=`-10000`
  death representation has been removed. A process-tagged run then isolated the
  remaining client crash exactly: access violation at `RequestDisableAi+0x17`
  (`RVA 0x3F5D47`) while applying an enemy snapshot. Client enemy synchronization
  no longer calls `RequestDisableAi`; a follow-up role-A termination immediately
  after the same call for Nadine showed that runtime AI toggling itself is unsafe
  during scene construction. All automatic AI enable/disable calls are now off.
  Health/transform replication and native MoveTo remain active. A discarded
  pointer-identity cooldown and an over-broad focus gate temporarily suppressed
  synchronization; background copies now keep sending read-only state while only
  the foreground copy mutates its world. Final death presentation still needs a
  stable implementation and confirmation.
- **Current combat correction (2026-06-28):** role B had been relaying enemy damage
  while suppressing the same hit locally, which explains enemies disappearing on the
  host but surviving on the client. Role B now both relays and applies its native hit;
  locally dead ragdolls are no longer overwritten by enemy snapshots. The NPC damage
  vtable hook is no longer installed on role A, where it was unnecessary and was the
  main remaining suspect in a delayed host-only combat termination. Live confirmation
  is pending.
- **Current world-sync correction:** `RequestDisableAi` was statically confirmed to
  dereference unfinished NPC subobjects without checks. Calls now require the complete
  `npc+0x1FC0` / `npc+0x1F88` chain to be readable. Client enemy pausing is currently
  off because it also suppresses all reactions to local Nadine; pausing NadineBuddy
  is disabled after a host fatal exit.
  The same run caught an initial `FLT_MAX` player position reaching native movement;
  all player/enemy positions are now bounded and checked at every network boundary.
  Production
  state application also no longer stops merely because its game window is in the
  background; the foreground test remains only on manual diagnostics. Live two-window
  confirmation is pending.
- **Enemy death presentation:** lethal base `AttackInfo` is closed after a client
  fail-fast during its kill branch. Replicated lethal damage now mirrors campaign
  `npc-die`: full HealthSystem damage followed by a new pointer-free 0x70-byte
  HitDescription and `AiHitController::Die(true)`. `npc-kill` was rejected because
  it only destroys the process tree. Live confirmation is pending.
- **MoveTo hardening:** the native navigation-frame validator `0x141DEF6C0` is now
  mandatory; the NPC clock/controller/radius/target are checked and refresh returns
  to the previously stable 250 ms interval.
- **Co-op mode gate** `HIBYTE(dword_143E07470)` + `byte_143E03568`: flipping it (mid-
  session OR at init before load) **CRASHES** — campaign levels have no co-op data/
  spawners, so co-op mode dereferences uninitialized co-op session state.
- **Split-screen / 2nd local player**: `net_login_split_screen` / `max_local_players=2`
  are invoked but the 2nd screen/player does not materialize in campaign (dormant,
  like the co-op gate). `0x238800` (materialize 2nd camera) returns failure.

---

## 🧭 REALISTIC SCOPE (agreed direction)

Fully replicating Chloe's **entire** ability set on the clone — weapons, weapon/ammo
**pickup** from enemies & crates, rope/lasso, vehicle entry, interactions — is, in
effect, **completing a full second playable player**, which the campaign engine was
never built for. It is **not a single fix** but a **long, multi-session build with
runtime iteration**, and parts of it run into **dormant/crash-prone systems** (the
co-op gate, split-screen, 2nd-local-player registration).

Likely common root for "world interaction" gaps (weapons/pickup/interacts): the clone
is spawned as an actor but is **not registered as a 2nd "local player"** that the
gameplay/interaction systems iterate over (they serve player 0). Two strategies going
forward:
- **Leverage**: register the clone as a genuine 2nd local player (could unlock several
  abilities at once — but risks the same dormancy/crashes as split-screen).
- **Per-ability**: wire each system manually (e.g. weapons via the `give-weapon`
  handler `sub_140F95850`, which resolves the player by slot via
  `GetRetainedPlayerEntityBySlot(slot)` and a weapon-art-def registry). Slower but
  safer.

The clean long-term target remains **two-PC networked co-op** (replicated input →
clone locomotion), which structurally avoids the local input/FPS artifacts above.

---

## Key addresses / offsets (quick reference)
- Player vtable RVA `0x2A4BFE8`; NadineBuddy vtable `0x2949638`; shared update slot 87.
- Appearance hash: `object+0x15A4`; getter `vtable[92]` = `sub_140A1DCC0` (RVA `0x0A1DCC0`).
  Chloe `0x3f334972`, Nadine `0x0f6d80f3`.
- Spawn: `SpawnPlayerFromRetainedDescriptor` `0x1410F1910`; appearance hook gated to
  the reissue window.
- Collision body: `player+0x238` (`InitializeCompositeBody` `0x1410CF470`).
- Look builder `sub_141BB5E00`; look-apply `sub_141804170` (`ndlib/render/look.cpp`).
- give-weapon handler `sub_140F95850`; `GetRetainedPlayerEntityBySlot(slot)`.
- Co-op gate `dword_143E07470` (RVA `0x3E07470`) + `byte_143E03568` (RVA `0x3E03568`) — CRASHES.
- Tooling: `scripts/launch_inject.sh` (direct `tll.exe`, no Steam launcher; Steam
  blocks 2 instances on one PC). IDA decompile via `scripts/ida_dump_pseudo_env.py`
  with `TLL_IDA_TARGETS`/`TLL_IDA_OUTPUT`.
