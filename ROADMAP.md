# TLL Co-op Mod Roadmap

Target build: `tll.exe 1.4.21058.0`

Current IDB status:

- single active database: `ida/tll.i64`
- all reproducible labels/comments must still be mirrored in
  `scripts/ida_apply_labels.py`
- packed `tll.i64` still reproduces corruption warnings on open, so "one base"
  does not yet mean "clean base"

## Goal

Run The Lost Legacy campaign on two PCs:

- player 1 controls Chloe with the normal game controller;
- player 2 controls the campaign's existing Nadine;
- each player has an independent camera;
- campaign scripts, combat, checkpoints, and saves remain host-authoritative.

## Chosen Architecture

Keep Chloe and the campaign simulation on the host, but treat the old
`NadineBuddy` path only as a diagnostic bridge. The real target is no longer
"remote-drive the buddy better", but "promote Nadine into a player-like
gameplay participant" that world systems can address the same way they address
Chloe.

The host mod still owns the authoritative campaign state. A lightweight client
captures player-2 input and eventually renders or controls a second game
instance, but the long-term implementation goal is:

- player 2 owns a Nadine-compatible full gameplay path;
- world interaction, traversal, combat, and triggers can resolve Nadine through
  the same gameplay interfaces used for Chloe;
- campaign state remains host-authoritative.

Teleport is reserved for spawn, checkpoint recovery, and large corrections.
The native NPC locomotion system remains useful only as a temporary
reconnaissance path.

Current architectural adjustment:

- the old `NadineBuddy` locomotion path remains a fallback and debug bridge;
- the main research track has shifted upward into:
  - retained player/network-player spawn
  - dual-camera retained binding
  - companion camera-rig/controller family
  - nested action/control objects
  - authored control-selection/task-node descriptors
- the real goal is now explicitly:
  - make Nadine a full second-player actor
  - let the world react to Nadine as it reacts to Chloe
  - attach camera/input/action ownership to that path

## Confirmed Primitives

- [x] Locate live `Player` and `NadineBuddy` objects.
- [x] Read stable world position at object offset `+0x240`.
- [x] Read orientation quaternion at object offset `+0x140`.
- [x] Disable automatic buddy catch-up teleport with RVA `0x3D441AD`.
- [x] Native Nadine teleport through vtable slot `0xC48`.
- [x] Build a navigation goal with RVA `0x1DF2D60`.
- [x] Issue native NPC `MoveTo` with RVA `0x3F6890`.
- [x] Confirm normal running animation and navigation to an external goal.

Detailed evidence and signatures are in `reports/initial-findings.md`.

## Phase 1: Diagnostic Local Nadine Control

Objective: establish a host-side diagnostic control bridge and prove that Nadine
can be driven externally.

- [x] Poll keyboard input state.
- [x] Convert WASD/stick input into a camera-relative direction.
- [x] Refresh a navigation goal while input is held.
- [x] Stop cleanly when input is released.
- [ ] Select walk/run speed from input magnitude or a run button.
- [x] Prevent campaign AI from replacing remote movement commands.
- [x] Add `F8` to enable/disable remote-control mode.
- [x] Restore campaign AI when remote-control mode is disabled.

Confirmed diagnostic behavior:

- all four world-relative directions and diagonals work;
- all four camera-relative directions and diagonals work while rotating the
  gameplay camera;
- rapid direction changes and stopping work;
- one live `NadineBuddy` instance is locked when control is enabled;
- native `RequestDisableAi`/`RequestEnableAi` isolate manual control and restore
  normal buddy behavior;
- a 3 m goal refreshed every 250 ms avoids excessive path replanning.

Acceptance criteria:

- Nadine can walk, run, turn, and stop continuously for at least five minutes.
- Movement follows terrain and does not use transform writes.
- Enabling and disabling control does not crash or require a checkpoint reload.
- This phase is considered complete only as a prototype baseline, not as the
  final gameplay solution.

## Phase 2: Player-Path Reverse Engineering

Objective: identify the player-like ownership/action path that can make Nadine a
full gameplay participant instead of an externally steered buddy.

- [~] Finish the authored control-selection/task-node descriptor layout.
- [~] Define the descriptor mode enum currently observed as `0/1/2/3`.
  - Current working mapping:
    - `0` = state-output / authored build path
    - `1` = candidate-selection / choose-promote path
    - `2` = candidate-selection with stronger global-byte gating
    - `3` = alternate authored build path
- [~] Identify where Chloe enters the same control-selection/action-routing
      layer.
- [~] Confirm how gameplay systems qualify an actor as a valid participant for:
      traversal, interaction, combat, and script triggers.
- [~] Map the handoff between retained player spawn and the action/controller
      stack.
- [ ] Determine whether Nadine can be rebound onto an existing player-like path
      or requires a Nadine-compatible second-player representation.

Current Phase-2 status:

- retained-player spawn, dual-camera binding, companion rig, nested control,
  late-decision, and materialized gameplay-state clusters already converge into
  one owner/runtime family;
- we have now reached a confirmed gameplay-facing action/perception path:
  - target selection through `+0x1FC0`
  - target commit into owner caches
  - world-space probe fallback
  - authored knowledge/state resolution
  - owner perception bitfield at `+0x1D59`
  - sink publish and manager-wide broadcast through `+0x1FA0`
  - final dispatch into live actors via `virtual +0xD0`;
- this means Phase 2 is no longer blocked on locomotion RE. The main remaining
  work is to bridge this owner/perception stack into full player action
  ownership: interact, traversal, melee, weapons, and script-facing actor
  qualification.

Acceptance criteria:

- We can point to a concrete player-like stack that owns movement, action
  routing, and world-facing participation.
- We know whether Nadine must be promoted, rebound, or mirrored onto that
  stack.

## Phase 3: Full Nadine Gameplay Participation

Objective: make Nadine behave like a Chloe-class gameplay actor on the host.

- [ ] Attach direct locomotion/action ownership to Nadine through the chosen
      player-like stack.
- [ ] Attach independent camera ownership.
- [ ] Enable traversal, interact, cover, dodge, melee, and basic combat.
- [ ] Validate that world queries and script triggers resolve Nadine correctly.
- [ ] Validate that checkpoints, death/respawn, and mission state include
      Nadine as a first-class participant.

Current blockers before entering live Phase-3 runtime experiments:

- identify the exact handoff from the owner perception/knowledge stack into
  player-facing action ownership;
- confirm which world-facing systems already treat Nadine as a valid actor and
  which still assume Chloe-only ownership;
- locate the promotion/rebind point where Nadine can be lifted from companion
  semantics into a true second-player gameplay path.

Current evidence that Phase 3 is becoming reachable:

- a dedicated player-tagged melee observer family now sits on top of the same
  runtime handle/object identity layer;
- that family already uses:
  - authored knowledge/state resolution via `0x14123F260`
  - live actor resolution from retained/runtime pairs
  - actor-side virtuals `+0x5C8`, `+0x9B0`, `+0xBE0`
  - readiness/scalar/transform methods tied to a live actor;
- that same family now also reaches:
  - actor-side state tables via `0x14189E170`
  - actor-side target/transform extraction via `0x14189F410`
  - recursive candidate-entry selection via `0x14193CD10`
  - selector validation via `0x14193CDB0`;
- and the same `+0x15C8` nested-control object is now confirmed to route
  non-melee gameplay actions too, including `npc-holster-weapon` through
  `virtual +0xA0` / `+0xB8`;
- a larger current-action gate at `0x140484C20` now sits above the same
  nested-control object and the `+0x1FB0/+0x1FF8/+0x1FA8` gameplay-state
  bundle, which is consistent with `weapon/aim/holster`-class gating;
- a second combat/action-state handler at `0x14058B900` now ties actor-graph
  combat-state selection back into the nested-control family through
  `0x141DE5A10`, `virtual +0x178`, and sibling nested virtuals;
- and a second observer-style publish path at `0x14124ED20` now sits on top
  of the same live-actor/scalar infrastructure as the melee observer family,
  using the primary retained player plus `0x141DF0000` and publishing through
  the sink at `0x143FDF520`.
- a sibling observer path at `0x14124F700` now confirms that this is a small
  observer family, not a one-off path: it uses the same live-actor/scalar
  infrastructure but publishes through sink `virtual +0x90`;
- an aiming/HUD-side runtime path is now grounded too:
  - `0x1413571B0` combines player state, camera state, and authored scalars
    like `is-aiming`, `player-live-time`, `grenade-throw-time`,
    and `weapon-accuracy`
  - then publishes them through object-local sinks around `+0x1D98`
    and the large state block at `+0xF0`
  - and `+0x1D98` is now confirmed as a broader presentation/observer sink:
    - `virtual +0x80` consumed by multiple runtime/visual paths
    - `virtual +0xB0 / +0xB8` used for structured state pull/publish
    - and it sits inside a formal initialized block `+0x1D80..+0x1DE0`
  - but the first clearly named owner for that block is currently
    `BuddyMeleeCoordinator::AttackInternal`, so this is a rich downstream
    behavior/publish layer, not yet proven to be the final generic
    Chloe-class player owner
  - and the same global sink `0x143FDF520` is also used by a
    `ghud.cpp`-anchored runtime path (`Camera Inside`, `GrenadeMove`,
    `vis: blind`), which strengthens the claim that this is a broad
    HUD/presentation publish surface
  - helper cluster `0x1417F9150 / 0x1417FA5A0` now shows that this sink is
    actually a slot-based keyed registry/bus with pointer/value arrays and a
    finalize/refresh step
  - early `Iggy Alloc/Free` telemetry also publishes into `0x143FDF520`, so
    the sink is broader than gameplay/HUD alone and should be treated as a
    runtime telemetry/observer bus rather than as a direct player-owner object
  - current-action gating now exposes explicit weapon readiness reasons through
    the same `+0x15C8` action stack:
    - `weapon-is-down`
    - `not-aiming`
    - `no-shooting-logic`
    - `shooting-logic-not-ready`
  - and the same aiming/HUD family also publishes reticle-facing records:
    - `reticle-show-time`
    - `update-reticle-aspect`
  - `fire-weapon-at-target` is currently only confirmed as a registrar, but
    `weapon-fire` itself already appears in live runtime code paths, which is
    the more useful bridge for eventual Nadine combat ownership
  - those `weapon-fire` paths now split into two useful layers:
    - a lower buffered fire-state/sample writer around `0x1403EF6D0`
    - and a higher retained-player/world-geometry observer path around
      `0x141436EA0`
    - under that higher path, owner-local helpers `0x141432B90 / 0x141434E90 /
      0x141434DA0` now read `owner + 0x624` through cached
      `weapon-art-defs(-online)-hash` registries, which makes this layer look
      like weapon observer/state qualification rather than direct fire
      execution
    - the same owner-local layer now has a normal `id -> record` lookup at
      `0x141614E40` plus a small getter family for record fields
      `0x0/0x8/0x10/0x18/0x20`, and the higher `0x141436EA0` path round-trips
      through that record family before it publishes final state
    - that higher path now ends in a concrete produced state record tagged by
      `UsingJointDifference` via `0x141520750`, which packages the derived
      vector plus the current actor/runtime pair for downstream consumers
    - `UsingJointDifference` now appears reused by multiple owner families, not
      only the `weapon-fire` path, so the produced-state format looks like a
      shared downstream record family rather than a combat-only special case
    - the heavy consumer `0x141520890` is now anchored by
      `calc-start-phase-*` / `DcCalcStartPhaseUsingJointDifference`, which
      suggests this shared record family already feeds a broader
      start-phase/locomotion-style actor state-transition subsystem
    - the first caller cluster for `0x141520890` is now mapped as a local
      multi-branch transition family over actor/runtime data, while the nearby
      `0x140A40AB0` path looks like a separate spawner/group selection helper
- special-tag state application helpers `0x140437F80 / 0x1404380F0` now show
  how the same combat path materializes candidate state objects for tags
  `0xAC710200 / 0xAC710202` and commits them through `0x141DE4F90`.
- vehicle participation is now grounded too:
  - authored drive-event strings `set-driver`, `add-passenger`,
    `passenger-entered`
  - a concrete passenger-entry builder on `hero-passenger-a-enter-front`
  - and existing scripted entry skills `MoveToAndEnterVehicle` /
    `TeleportIntoVehicle`
  - plus `VehicleCtrl::NotifyPlayerEntering`, which now clearly republishes a
    packaged `vehicle-entry-def` record into the live actor layer via
    `virtual +0xD0`
  - plus an embedded seat-state subobject at `owner + 0x690`, with:
    - `0x14059D740` passenger-entry state build/commit
    - `0x14059DAB0` exit-side dispatch
    - `0x14059DC30` capability/hash gate into nested control `+0xA0`
    - `0x14059F1E0` outer owner update around the same seat-state
  - plus explicit vehicle qualification helpers:
    - `0x14039EC50` -> `DriveableVehicle`
    - `0x14039EB80` -> `RailVehicle`
    - nearby selector cluster also exposes `DriveableBoat`
- this is the strongest concrete bridge so far from the generic
  perception/knowledge owner stack into a real player-facing combat subsystem.

Acceptance criteria:

- Nadine can move through the world, interact, traverse, and participate in
  combat through player-like systems rather than NPC-only systems.
- Core world systems react to Nadine as they react to Chloe.

## Phase 4: Network Transport

Objective: send second-player input from another PC into the player-like Nadine
stack on the host.

- [ ] Define a versioned input packet: sequence, timestamp, movement, look,
      buttons, and acknowledgements.
- [ ] Implement a small UDP transport with timeout and reconnect handling.
- [ ] Feed received input into the chosen player-like Nadine control path.
- [ ] Send host snapshots: transform, velocity, action state, health, weapon
      state, and checkpoint generation.
- [ ] Add interpolation, prediction, and correction thresholds where needed.
- [ ] Authenticate the session with a short host-generated token.

Acceptance criteria:

- Stable LAN control for at least 30 minutes.
- Lost or reordered packets do not leave actions stuck.
- Host remains authoritative over Nadine and campaign state.

## Phase 5: Second-PC Rendering

Objective: give player 2 an independent view and presentation path.

- [ ] Allow or isolate a second game process and its save/config files.
- [ ] Load the matching level/checkpoint on the client.
- [ ] Suppress client-side campaign authority and conflicting scripts.
- [ ] Spawn or select a client-visible Nadine representation.
- [ ] Drive the client camera independently.
- [ ] Replicate enemies, traversal state, combat effects, and cutscene state.

Acceptance criteria:

- Both PCs show the same encounter with acceptable visual consistency.
- Player 2 sees Nadine from an independent camera.

## Phase 6: Campaign Compatibility

- [ ] Checkpoint load, death, revive, and respawn.
- [ ] Vehicles and parented character transforms.
- [ ] Boosts, doors, dual interactions, and contextual traversal.
- [ ] Stealth and combat encounters.
- [ ] Cutscenes, forced cameras, QTEs, and character swaps.
- [ ] Chapter transitions and save/load.
- [ ] Per-scene fallback rules when Nadine is absent or script-locked.

## Immediate Next Build

Continue Phase 2 reverse engineering on top of the confirmed diagnostic
controller:

1. Continue from the confirmed perception/knowledge path:
   - owner `+0x1D59`
   - owner `+0x1FA0`
   - sink/broadcast helpers
   - live-actor `virtual +0xD0`
2. Identify the next bridge from that path into:
   - interact/traversal
   - melee/weapon ownership
   - script-facing gameplay qualification
3. Use the newly confirmed `MeleeScriptObserver` class family as the first
   concrete player-facing probe above the knowledge layer.
4. Use the newly confirmed hook/interact family as the second probe:
   - `interact-hook`
   - `interactable-def`
   - `winch-hook-attach`
   - `attach-winch-hook`
   - `winch-hook-secure`
   - `pickup-winch-hook`
   - `player-attach-winch-hook`
   - `player-grab-winch-hook`
5. Keep the authored descriptor and control-selection enum work moving in
   parallel where it still feeds these systems.
6. Use the newly confirmed combat/observer branch as the third probe:
   - `0x140484C20`
   - `0x14058B900`
   - `0x14124ED20`
   - `0x14124F700`
   - `0x1413571B0`
   - `0x140437F80 / 0x1404380F0`
   - shared scalar query `0x141DF0000`
7. Use the newly confirmed vehicle-entry branch as the fourth probe:
   - `0x140394B20` (`set-driver`)
   - `0x140395000` (`add-passenger`)
   - `0x140395430` (`passenger-entered`)
   - `0x14059D740` (`hero-passenger-a-enter-front`)
   - `0x1414115B0` (`vehicle-entry-def` -> actor `+0xD0`)
   - `0x14059DAB0` (exit-side / seat-state sibling)
   - `0x14059DC30` (seat-state capability/hash gate)
   - `0x14059F1E0` (outer vehicle-action owner around seat-state `+0x690`)
   - `0x14039EC50 / 0x14039EB80` (vehicle qualification)
   - `MoveToAndEnterVehicle / TeleportIntoVehicle`
8. Only after that, choose the runtime experiment that tries to promote Nadine
   into a full player-like participation path.

Networking remains important, but it is no longer the blocker. The blocker is
full gameplay participation.

## Current RE Direction

The stable host hook and UDP input path are in place, but the current runtime
movement backend is still the native NPC `MoveTo` command. That remains useful
only as a prototype and instrumentation path.

Latest reverse-engineering result:

- direct blind calls into lower-level writers such as `0x1403F6CB0` are not
  safe enough for live iteration;
- the real locomotion path uses a larger command block rooted at
  `NadineBuddy + 0x2470`;
- that block is interpreted by `ProbeControlRoutingThresholdsAndSelectStateId`
  into small control modes and probe flags;
- those modes then feed authored control-selection/task-node descriptors from
  `ResolveGlobalTaskNodeThresholdRecord`;
- the same descriptor layer is consumed by larger gameplay selection/update
  systems, not only locomotion.
- the current strongest convergence point is now the shared materialized-state
  lifecycle at `+0x1F80 -> +0x30`, where both:
  - actor/runtime builders such as `BuildOwnerMaterializedStateObjectFromActorRuntime`
  - and direct locomotion/control paths around `+0x24C8/+0x24CC`
  rebuild and swap the same `0x4D0` materialized state-object variant.

Current strongest convergence point:

- the owner/runtime stack is no longer just a control-selection path;
- it now clearly reaches:
  - action-target preparation
  - AI knowledge / vox / melee-with-player semantics
  - interact/hook/winch action families
  - weapon/aim/holster-flavored current-action gating
  - actor-observer publish paths built on live actor identity and
    `0x141DF0000`
  - vehicle-entry and passenger-seat authored event/state paths
  - shared nested action dispatch through `+0x15C8` for both melee/holster and
    `player-grab-winch-hook`
  - owner perception/action bits
  - sink publication
  - manager-wide broadcast to live actors
- the most promising route is therefore above NPC locomotion and inside this
  gameplay-facing owner/perception stack.
- the shared `UsingJointDifference` / `calc-start-phase` family now also looks
  actor-facing, because it already publishes structured state through actor
  `virtual +0xD0` and feeds a heavier runtime phase/move-mode transition
  consumer (`0x1402EF0A0 -> 0x1402EF420`).
- that heavier consumer is now confirmed to continue past publish into actual
  actor-side transition application:
  - branch/state-id selection from record fields `+0x90/+0x94/+0x9C/+0xA0/+0xA8`
  - phase/orientation basis rebuild through `0x1415D23B0`,
    `0x142177E50`, and `0x1401781E0`
  - transition-instruction dispatch through `0x141BE20C0`
  - actor-side transition writes through:
    - `0x141BE2D90` -> `+0x50/+0x54/+0x58`
    - `0x141BE2E70` -> `+0x5C`
    - `0x141BE2F50` -> `+0x2C`
  - structured transition finalization through `0x141BE4EA0`
  - and follow-up fanout across retained player slots `0/1`
- the new split below that family is also useful:
  - `0x141BDFE70` remains in actor-side transition resolution/qualification
  - inside that path, `0x14209A740 / 0x14209A8B0` are now mapped as owner-local
    dual-hash authored-state resolvers rather than ad hoc helpers
  - external consumers now pin part of the node layout:
    - `node + 0x30` behaves like a long capability/category mask filtered by
      mode-specific constants
    - `node + 0x12 bit3` behaves like a short enable/override gate before
      observer publish logic
  - the direct `0x141BDFE70` callsites now read as a coherent structured
    transition-result family:
    - actor-local consumer that caches both returned transition records and
      live entity handle/id pairs
    - local looped sibling over authored id arrays
    - global/singleton sibling over the `0x146920CA0` owner context
    - higher composite builder that feeds entity-derived state back into a
      second transition-result resolution pass
  - correction: the `0x146920CA0 / 0x146920CF0 / 0x146920CF8` singleton family
    is ParticleManager-owned, not the core player-ownership manager:
    - `0x141BD4DD0` is `ParticleManager::Init`
    - `0x141BD8BE0` sits on `ParticleManager::UpdateEvents`
    - the same authored-state node/mask layer is therefore shared into
      particle/event systems, but this branch is no longer the primary route
      toward Nadine's player ownership
    - one reusable result from that branch remains useful:
      `0x141A81B80` expresses the policy model directly as
      `context+0x130 bits 8/9` vs `node+0x30 long masks`
  - the better ownership-facing anchor is now `0x141BDC640`, which already
    combines:
    - local pending-entry queues (`+0x668/+0x670/+0x6F0/+0x6F8`)
    - actor/runtime identity
    - authored-state resolution through `0x14209A8B0`
    - structured transition-result consumption through `0x141BDFE70`
  - correction: descending into the sibling methods shows that this whole local
    owner family is still ParticleManager / ParticleRootData-owned, not Nadine
    promotion:
    - `0x141BDDBE0` is the central local particle-root refresh/commit wrapper
    - `0x141BDD400` builds the local particle-root runtime subobject at
      `owner + 0x30`
    - `0x141BDCED0` refreshes that same root-state image from
      `+0x994..+0xA2C`
    - `0x141BDC640` processes pending transition entries inside that same
      particle-root owner
    - `0x141BDD3E0` is only a compact callback adapter into this wrapper
    - the family also owns explicit particle-root state banks at
      `+0x3B0/+0x430/+0x4B0/+0x530/+0x630`
    - `0x141BD79D0` is `ParticleManager::FreeRootData`-side logic
    - `0x141BD8020` is `ParticleManager::CreateRenderData`-side logic
    - `0x141BD74B0` is `ParticleManager::UpdateFreeRetiredRootData`-side logic
  - sibling `0x141BDF510` is now better understood as a
    `ParticleRootData` allocation/retiring/active branch under the same
    transition family, not as the core player-ownership allocator
  - `0x141F84640 -> 0x141BEB0F0` now reads specifically as
    `camera-shake / explosion-shake / shoot-shake` follow-up
  - so the ownership-facing reverse focus should stay on the former before
    spending more time on the latter

Immediate next investigation:

1. Stop treating the `0x141BDC640 / 0x141BDDBE0 / 0x141BDE250` family as the
   main promotion path; keep it only as shared authored-state/transition
   infrastructure reference.
2. Treat the currently mapped `0x141BDFE70` direct-consumer cluster around
   `0x146920CA0/CF0` as particle-root flavored until proven otherwise, even
   when it touches live entity handles.
3. Return to genuinely gameplay-facing intersections of the same transition
   family:
   intersects:
   - `interact-hook`
   - `interactable-def`
   - `winch-hook-*`
   - `weapon/aim/fire`
   - `melee`
4. Determine where these resolved transition records and actor-state writes
   intersect with player-only or Chloe-only systems.
5. Only then follow the already-identified world-effect branch
   (`0x141F84640 -> 0x141BEB0F0`) to separate gameplay qualification from
   camera-shake / presentation execution cleanly.
6. Use the locomotion/NPC-command work only as fallback instrumentation if the
   player-like gameplay path stalls.

Current status update:

- The particle-root branch has now been explicitly demoted.
  - `0x141BDC640 / 0x141BDDBE0 / 0x141BDE250`
  - and the remaining direct `0x141BDFE70` consumers under
    `0x146920CA0/CF0`
  should be treated as `ParticleManager / ParticleRootData`-flavored shared
  infrastructure, not as Nadine/Chloe promotion.

- The active gameplay-facing track is now:
  - `move-mode-transition`
  - `player-enter-balance-mode`
  - `transition-player-into-appropriate-grapple-reaction`
  - `melee-r1-hold`
  - `melee-r1-release`
  - `melee-lmouse-release`
  - `melee-rmouse-hold`
  - `melee-rmouse-release`

- Confirmed concrete runtime nodes on that track:
  - `0x140EF6ED0`
    - hashes `move-mode-transition`
    - pairs it with `player-enter-balance-mode`
    - publishes a small runtime record
  - `0x140F7D28A`
    - sibling move-mode transition producer in a separate local/indexed path
  - `0x141271D20`
    - gameplay-facing melee/button/transition matcher
  - `0x141273B20`
    - live melee/grapple command dispatcher
  - `0x141273FA0`
    - 16-slot queued command insertion/rotation path
  - `0x1412743F0`
    - scalar update path for existing queued commands
  - `0x141274470`
    - constructor for the large `Melee Command Queue` owner

- New convergence point:
  - the only clean external callsite to `0x141274470` currently found stores
    the constructed object into `owner + 0x1F60`
  - the same outer builder also mounts sibling runtime objects at:
    - `owner + 0x1F68`
    - `owner + 0x1FF8`
  - that is a concrete bridge back into the older `+0x1Fxx` gameplay-state
    region already mapped elsewhere in the project

- New high-value owner node:
  - `0x1412746B0`
    - anchors to `ProcessNpcMeleeManager`
    - seeds authored ids for:
      - `player-attacks`
      - `player-throws`
      - `*camera-melee-settings*`
      - `*camera-melee-settings-net*`
      - `stealth-edge`
    - and initializes wide melee/gameplay owner state

- This is currently the best concrete evidence that the older `+0x1Fxx`
  gameplay-state cluster is meeting a real player/melee action owner, not just
  generic shared infrastructure.

- The `ProcessNpcMeleeManager` side is now confirmed as a real mounted owner
  family:
  - `0x141274E20` = teardown/reset path on the same family
  - `0x141283A20` = freeing-dtor style sibling
  - small cached actor-pair helper at `0x141274EC0`
  - so this branch already has constructor/init/attach/teardown semantics and
  uses live actor identity, which makes it a much better promotion target
  than the earlier particle-root branch.

- One level higher, the outer builder around `0x1410CB2BA..0x1410CB8F0` now
  looks like a real mounted `player-melee-control` subgraph:
  - it mounts owned subobjects at:
    - `+0x1D80`
    - `+0x1DD8`
    - `+0x1DF8`
    - `+0x1E08`
    - `+0x1E20`
    - `+0x1E50`
    - `+0x1E58`
    - `+0x1F60`
    - `+0x1F68`
    - `+0x1F70`
    - `+0x1FF8`
  - `+0x1F60` is now confirmed to hold the melee command queue owner
  - `+0x1F68` and `+0x1FF8` are sibling mounted gameplay/melee objects
  - this is currently the strongest convergence point between the older
    `+0x1Fxx` gameplay-state mapping and explicit player-melee action owners

- New refinement inside that subgraph:
  - `+0x1F68` is its own mounted class family
    - size `0x140`
    - table/vtable anchor `0x142A6A500`
    - ctor/dtor helpers around `0x14125C430 / 0x14125C4A0 / 0x14125C4D0`
  - `+0x1F70` is another mounted class family
    - size `0x130`
    - table/vtable anchor `0x142A6A930`
    - ctor/dtor helpers around `0x14125E440 / 0x14125E4B0 / 0x14125E4E0`
  - so the `+0x1Fxx` region is now clearly a composed runtime class graph, not
    a single opaque state blob

- Behavioral refinement:
  - `+0x1F68` already participates in live camera/presentation publish flow
    and exposes at least virtual `+0x48` and `+0x60`
  - `+0x1F70` already participates in state/transition selection and exposes at
    least virtual `+0x30/+0x38/+0x40/+0x48`
  - `+0x1F60` remains the explicit queued command/action owner via virtual
    `+0x210`
  - this is now reading like a mounted melee/control state machine, not just
    neighboring runtime classes

- This is now the best route toward Nadine promotion because it is already
  operating on concrete player-facing verbs and queued command state, not on
  generic shared authored-state policy.

- New traversal/control convergence inside the same mounted graph:
  - `+0x1DF8`
    - size `0x560`
    - ctor `0x140DE5CC0`
    - table anchor `0x142A1F690`
    - seeded from `player-fall.cpp`, `DiveFall`, `free-rope-grip`,
      `desired-fall-state`, `time-in-air`
  - `+0x1E08`
    - size `0x220`
    - table anchor `0x142A19ED8`
    - hard anchors to `player-drag-edge.cpp`
    - carries `drag-drop-*`, `drag-climb-*`, `player-climb-up`, edge IK state
  - `+0x1E50`
    - size `0x550`
    - table anchor `0x142A227A0`
    - hard anchors to `player-drag-rope.cpp`
    - carries `drag-rope-*`, `rope-climb`, `wall-rope-jump`,
      `move-to-drag-rope`, `free-rope-to-drag-rope`
  - `+0x1E58`
    - size `0x710`
    - ctor `0x140E14490`
    - table anchor `0x142A216C8`
    - carries edge/corner/wall-rope/free-rope/staff authored states
  - `+0x1E20`
    - size `0x2C0`
    - ctor `0x140FCCD20`
    - table anchor `0x142A3C490`
    - anchored to `water-current`, `slide-barrier`,
      `player-slide-barrier-settings`, `*default-water-forces*`

- Current reading is now broader than `player-melee-control`:
  - the mounted `+0x1D80..+0x1FF8` region is a real player-control graph
    spanning combat and traversal together
  - this is the strongest current structural route toward promoting Nadine from
    companion actor to full gameplay actor

- New owner-level refinement above that graph:
  - the same outer build region also mounts named player-facing neighbors at:
    - `+0x1D60` root owner (`0x1DF0`, table `0x142A268C0`)
    - `+0x1D68` (`0x2D0`, table `0x142A288D8`)
    - `+0x1DC0` (`0x4560`, ctor `0x141F9D610`)
    - `+0x1DC8` (`0x2B0`, table `0x1428ED7B0`)
    - `+0x1DD0` (`0x540`, ctor `0x14024A090`)
    - `+0x1E28` (`0x1E0`, table `0x142A0E148`)
    - `+0x1E30` (`0x120`, table `0x142A2D680`)
    - `+0x1E38` (`0x460`, table `0x142A2D550`)
    - `+0x1E48` (`0x2B0`, table `0x142A32C08`)
  - named anchors in those siblings already include:
    - `player-look-at-ik.cpp`
    - `player-menu.cpp`
    - `player-perch.cpp`
    - `player-bump-bend-align.cpp`
  - this makes the outer owner read like a broad player subsystem assembler,
    not just a local melee/traversal manager

- Current best structural hypothesis:
  - Nadine promotion is more likely to require rebinding into this large outer
    player owner family than patching one isolated melee or traversal subobject

- New root-owner result:
  - `0x1410CA720` now reads like a real player owner assembly pass
  - it mounts not only melee/traversal pieces, but also:
    - player anim settings
    - investigate / inspect
    - look-at IK
    - player menu / weapon menu observers
    - perch
    - bump-bend-align
    - flame / flashlight
    - plus the already mapped combat+traversal graph
  - this is currently the strongest Chloe-class ownership candidate in the
    binary

- New caller-chain result:
  - `+0x1D60` root owner has a direct in-place ctor at `0x140E5DDF0`
  - `0x1410CA720` now has a confirmed higher-level caller at `0x1410DD0FC`
  - this gives the first concrete upward path above the broad player-owner
    assembly graph
  - next promotion/rebind work should start from `0x1410DD0FC` and the larger
    outer runtime owner it is initializing

- Strongest new anchor:
  - the caller chain above `0x1410CA720` is now directly tied to `player.cpp`
  - strings in the same post-call path include:
    - `Player::Player(void)`
    - `Player::Init(const ProcessSpawnInfo &)`
  - current best hypothesis is no longer just "player-like owner family"
  - it is now "real Player init chain", which is the best promotion target for
    Nadine found so far
  - that same init neighborhood already carries net/co-op flavored player
    identity/settings:
    - `hero-default`
    - `player-1`
    - `local-player-1`
    - `*net-player-sprint-settings*`
    - `*net-player-swim-sprint-settings*`
    - `*camera-melee-settings-net*`
    - `*net-player-anim-art-groups*`
    - `*net-player-weapon-mods-coop*`
    - `net-player-coop-id`
  - so the current best hypothesis is now stronger than just "generic player
    subsystems":
    it is a real `Player` init/promotion layer that already knows about
    slot identity and net/co-op flavored player state
  - same larger init block also allocates a `*player-ik-arm*` owner-local
    object at `outer + 0x4308` before entering `0x1410CA720`
  - and the tail of `0x1410CA720` confirms the mounted graph continues through
    `+0x1E20` and `+0x1E28`, so this is a broad Player subsystem graph rather
    than a combat-only mounted slice
  - extending that tail further now also confirms mounted Player-side
    subsystems at:
    - `+0x1E30` -> `player-menu`
    - `+0x1E38` -> `player-look-at-ik`
    - `+0x1E48` -> `player-perch`
    - plus additional still-unresolved mounted owners at
      `+0x1E98/+0x1EB0/+0x1EB8/+0x1EE0`
  - first pass on those later owners already suggests the Player init graph is
    reaching interaction/presentation territory too:
    - `+0x1E98` is a very large mounted owner (`0x1B50`) and now looks
      strongly like the mounted `player-investigate / read-mode / readable`
      owner
      - and it is now confirmed as a live subsystem in recurring per-player
        update loops via virtuals `+0x430 / +0x438 / +0x220`
      - and its virtual `+0x208` already participates in player-tagged
        gameplay gating around the `Player` descriptor block
      - surrounding strings are now explicitly anchored to
        `player-investigate.cpp`, `read-mode-enter`, and
        `player-investigate-*`
    - `+0x1EB0` is a live mounted owner (`0x4B0`) with downstream virtual use
      - current best read is now stronger: mounted Player-side
        `cover / evade / dodge-roll` owner
      - surrounding strings are explicitly anchored to
        `player-cover.cpp`, `player-cover-actions.cpp`,
        `player-cover-states.cpp`, and `player-evade.cpp`
    - `+0x1EE0` is also consumed immediately as a live sibling subobject with
      deeper virtual dispatch
      - current best read is a live activation/synchronization sibling for the
        same cover/evade family
    - and the exact assembly order is now confirmed in the same Player pass:
      - `+0x1EB0` <- `0x140DCB1A0`
      - `+0x1E98` <- `0x1410484F0`
      - `+0x1EB8` <- `0x140F49300`
      - `+0x1EE0` <- `0x140CEDCA0`
      - so investigate/read-mode and cover/evade owners are direct peers under
        one real Player gameplay owner
      - `+0x1EB8` is now structurally pinned as its own mounted Player
        subsystem with a nested helper at `+0x450`, but its exact gameplay
        role is still unresolved
    - neighboring mounted Player slots are now also structurally pinned:
      - `+0x1E60` <- inlined `0x220` ctor path with owned helper at `+0x120`
      - `+0x1E68` <- `0x14105E220` (`0x340`)
      - `+0x1DA0` <- `0x141301710` (`0x52C0`)
        - now looks like the strongest HUD / read-mode / investigate-
          presentation owner candidate in the Player graph
        - `+0x1DB8` <- `0x140D64BF0` (`0x15C0`)
        - now looks more like a substantial Player-side action/qualification
          owner than a passive state blob
        - this is now strengthened by two concrete gate paths:
          - `0x140D65030` combines `+0x1D88`, `+0x1DB8`, owner virtual `+0x8C0`,
            and a hashed fallback `0x1410F75C0`
          - `0x140D65290` combines `+0x1D88`, `+0x1E50`, `+0x1DB8`, and
            `+0x1D98`
        - current read is now stronger: `+0x1DB8` participates directly in a
          mounted Player-side qualification / execution cluster rather than
          just storing state
        - that cluster now extends downward into:
          - `0x140D654F0` aggregate execution policy gate
          - sibling spatial/state threshold helpers
            `0x140D65940 / 0x140D65540 / 0x140D65740`
          - hashed cooldown/window helper `0x140D65B20`
          - paired baseline seeding helper `0x140D64FB0`
        - and upward into nonlocal Player callers such as the path around
          `0x140F60963`, where the same family gates deeper `+0x1DB8`
          verb-specific virtuals (`+0x3B8`, `+0x50`)
        - that same nonlocal caller now also ties together:
          - `+0x1E98` investigate/read-mode-like owner
          - `+0x1E50`
          - `+0x1EB0` cover/evade owner
          - `+0x1FB0` dispatch
          - outer owner commits `+0xC78/+0xC98/+0xCB8`
      - `+0x1D88` is now structurally pinned too:
        - mounted from `0x1410B4B10` (`0x4B0`)
        - repeated entry/state tables plus helper blocks at `+0x2C0` and
          `+0x380`
        - fits its role as the sibling readiness/blocker owner in the same
          qualification family
      - `+0x1DE0` is now structurally pinned:
        - mounted from `0x1411A65C0` (`0x6090`)
        - seeds large internal tables around `+0x1AD0/+0x3460/+0x4140/+0x5DC0`
        - its ctor already calls the known mounted-player action-family hash
          checker `0x1410F75C0`
        - and nonlocal Player-side code already uses `+0x1DE0 -> virtual +0x28`
      - the larger outer caller around `0x140F60680` is now the strongest
        current convergence point above these mounts:
        - heavy `+0x1FB0` dispatch (`+0x38/+0x40/+0xB0/+0xC0/+0x108/+0x110/
          +0x118/+0x120/+0x130`)
        - owner commits `+0xBD0/+0xC78/+0xC98/+0xCB8`
        - mounted verb/policy participants:
          `+0x1DE0`, `+0x1E98`, `+0x1E50`, `+0x1EB0`, `+0x1DB8`
        - this now looks more like a real outer Player verb orchestrator than
          an isolated subsystem caller
      - `Player::Init` mount coverage around `+0x1D48..+0x1DF8` is now broad
        enough to treat as a real promotion map:
        - `+0x1D48 <- 0x140F187B0 (0x690)`
        - `+0x1D50 <- 0x14100D5A0`
        - `+0x1D58 <- vtable 0x142A236B0 (0x130)`
        - `+0x1D60 <- vtable 0x142A268C0 (0x1DF0)`
        - `+0x1D68 <- vtable 0x142A288D8 (0x2D0)`
        - `+0x1D70 <- vtable 0x142A351A8 (0x140)`
        - `+0x1D78 <- outer vtable 0x142A441A0 with nested interface vtable
          0x142A441D8 at +0x120`
        - `+0x1D80 <- 0x141072B20 (0x470)`
        - `+0x1D90 <- 0x140CAE670 (0x930)`
        - `+0x1D98 <- vtable 0x142A23568 (0x3E0)`
        - `+0x1DA0 <- 0x141301710 (0x52C0)`
        - `+0x1DA8 <- 0x140E91A00`
        - `+0x1DB0 <- vtable 0x142A28BC8 (0x1A0)`
        - `+0x1DB8 <- 0x140D64BF0 (0x15C0)`
        - `+0x1DC8 <- vtable 0x142A0CC60 (0x380)`
        - `+0x1DD0 <- 0x14024A090 (0x540)`
        - `+0x1DD8 <- vtable 0x142A0CC60 (0x380)`
        - `+0x1DE0 <- 0x1411A65C0 (0x6090)`
        - `+0x1DE8 <- 0x140FF5560`
        - `+0x1DF8 <- 0x140DE5CC0 (0x560)`
      - `+0x1D78` is now pinned as a mounted policy/helper owner with a nested
        runtime interface at `+0x120`; `Player::Init` immediately feeds the
        current actor/runtime pair into `(+0x1D78 + 0x120) -> virtual +0x20`
      - `+0x1EB8 <- 0x140F49300` is structurally pinned as another formal
        mounted Player subsystem with vtable `0x142A34158` and repeated state
        slots across roughly `+0x130..+0x408`
      - a small authored-event callback family at vtable `0x142A35090`
        (`0x140F614E0 / 0x140F615B0 / 0x140F61610`) now shows a direct ingress
        into `+0x1FB0`:
        - event dispatch from authored ids to `+0x1FB0 -> virtual +0x100/+0x128`
        - sibling helper `0x140F61490` asks `+0x1FB0 -> virtual +0x78` and then
          tail-calls `+0x48`
      - the late `Player::Init` band now directly mounts the central dispatch
        region:
        - `+0x1F98 <- 0x140FE92B0`
        - `+0x1FA0 <- 0x140E40EB0`
        - `+0x1FA8 <- 0x140F632B0`
        - `+0x1FB0 <- 0x140F5F920`
        - `+0x1FB8 <- 0x141004610`
        - `+0x1FC8 <- 0x140CD3640`
        - `+0x2010 <- 0x140D80440`
        - `+0x1FD8 <- 0x140FDA6E0`
        - `+0x1FE0 <- 0x140CEACD0`
        - `+0x1FE8 <- 0x140DA6920`
        - `+0x1FF0 <- 0x140E3E660`
      - `0x140F5F920 -> 0x140F5F980` is now structurally pinned as the
        allocating + in-place constructor pair for the real `+0x1FB0` dispatch
        object with vtable `0x142A34E10`
      - outer owner commit roles are now starting to separate by behavior:
        - `+0xBD0` behaves like a generic fallback/refresh commit
        - `+0xCB8` follows accepted specialized `+0x1FB0` / presentation
          branches
        - `+0xC78` follows successful spatial/execution branches
        - `+0xC98` behaves more like a broader active-state finalize/publish
          commit
      - the larger function at `0x1410DC160` is now pinned as the top-level
        Player construction/init pass above `0x1410DD0FC`
        - it seeds broad owner state directly across the outer Player layout
        - it creates the owner-local `+0x3BD0` object through `0x1410DE520`
        - it then calls `0x1410DD0FC -> 0x1410CA720`
        - and later continues finalization through `+0x3BD0`, `+0x1DF0`,
          `+0x1A8`, `+0x1B10`, and `+0x3D98`
      - `0x1410DE520` is now structurally pinned as the in-place initializer
        for the owner-local runtime object mounted into `outer_player + 0x3BD0`
        with vtable `0x142A4BFD8`
      - `0x1410DE740` now looks like a higher-level Player finalize/spawn
        bridge that logs through `+0x3C20` and then hands external state into
        `0x140CC1450(outer_player, external_state)`
      - `0x140CC1450` is now pinned as a real top-level Player
        `ProcessSpawnInfo` apply/finalize path
        - validates a record against owner ids `+0x108/+0x10C`
        - copies accepted state back into `outer_player + 0x110`
        - builds a transform block from `+0x110/+0x120/+0x130/+0x140`
        - commits it through `0x1418C4840`
      - `+0x3BD0` is no longer just "some owner-local object"
        - consumer `0x14102F6B0` uses `+0xB0` as bucket count
        - and `+0x20` as the bucket table
        - linked entries carry hashed ids at `entry + 0x10`
        - this makes `+0x3BD0` a real hashed owner-local runtime registry
      - `0x1410DB040 / 0x1410DBF10` now read like a top-layer Player preflight
        validator over `ProcessSpawnInfo`-style input
        - the broad validator checks outer-owner state across `+0x1A0`,
          `+0x1D88`, and `+0x3D98`
        - the small wrapper at `0x1410DBF10` adapts that result back into an
          incoming record object via `record + 0x10`
        - failure reasons include `player has two-handed weapon in hands`,
          `player is aiming`, and current move-mode gesture restrictions
      - `0x14177F7F0` is no longer treated as a Player-local helper
        - it is a generic global runtime-state slot allocator
        - claims a `0x380` slot from the global pool at `0x1466D0200`
        - seeds retained/runtime identity into `slot + 0x1A8`
        - stores type/context into `+0x208` and optional payload into `+0x210`
        - independent callsites at `0x1408F99B0`, `0x1409C81C0`, and
          `0x141BBA060` confirm it is shared infrastructure
      - `0x1418C4840` is the generic partner apply path for those slots
        - copies source links into `+0x20/+0x130/+0x138` and `+0x18/+0x1A0`
        - commits transform/presentation state into `+0x1C0..+0x1F0`
        - seeds late defaults across `+0x280..+0x320`
        - therefore `0x140CC1450` should be read as the Player-specific bridge
          over generic runtime slot materialization, not the allocator itself
      - `0x1410DE430` now looks like a sibling upper-layer owner-local fixed
        entry table beside `+0x3BD0`
        - repeated entries begin at `self + 0x20`
        - `object + 0x18 = 0x1E`
        - this means the top-level Player band owns multiple runtime
          containers above the mounted graph, not only the `+0x3BD0` registry
      - the upper Player layer around `0x1410DC160` is now clearly a virtual
        family, not just a loose caller chain
        - `0x1410DE660` stamps a sibling vptr block at `0x142A4BFC0`
        - the surrounding assembly pass allocates that object and stores it at
          `outer_owner + 0x2018`
        - the same `.rdata` family block contains `0x1410DC160` and
          `0x1410DE740`
        - `+0x3BD0` receives a neighboring vptr inside the same family band
      - the relocation pass around `0x140F63F80..0x140F64056` now proves that
        `+0x3BD0` is a formal owner-owned runtime object
        - it is rebased together with `+0x1F78`, `+0x3FE0`, `+0x3C18`,
          `+0x3C70`, `+0x3C78`, and `+0x3C80`
        - and it receives a virtual dispatch before the outer owner pointer
          itself is rebased
      - `outer_owner + 0x2018` is now also a formal upper-layer runtime object
        - getter `0x1410C6BA0` returns it directly
        - relocation pass `0x140F64F36..` dispatches through its `virtual +0x8`
        - cleanup path `0x1410CDA22..` dispatches through its `virtual +0x40`
        - this places `+0x2018` in the same lifecycle tier as the `+0x3BD0`
          registry object
      - `+0x2018` is still a live upper Player action/policy surface, but the
        narrower `PlayerWeaponAction` read was too aggressive
        - `0x141099520` now shows a real upper `PlayerGesturePermit` /
          action-policy path over the mounted graph
        - it consumes `+0x1E50`, `+0x1E58`, `+0x1DF0`, `+0x1D78+0x120`,
          `+0x4320`, and `+0x3BD0`
        - `0x141099175` then feeds the built local context into
          `outer_owner + 0x2018`
        - nearby reason strings tie this upper layer to gesture locks,
          aiming/two-handed weapon restrictions, and weapon-action state names
        - `0x14109BE19` then consumes the compact upper result and publishes a
          keyed byte/flag back into `+0x3BD0`
      - the raw `0x142A4BFC0` table behind `+0x2018` is now treated as a
        mixed upper-family block, not a clean function-only vtable read
      - `0x140CB5050` now pins `+0x3BD0` as an owner-local keyed byte/flag
        registry for upper Player policy publication, not just a hashed lookup
        - current keys already span more than weapon-only state:
          `ReloadWeapon`, `RechamberWeapon`, `PlayerReloadClipWeaponAction`,
          `kRopeClimb`, `piton-stab-fx`
      - there is also a compact upper bridge object family at `0x142A4BF50`
        - `0x1410DBFA0` stamps it and stores `outer_player_owner` in `+0x8`
        - `0x1410DBFC0` exposes `owner->+0x2018`
        - `0x1410DC020` exposes `owner+0x3C20`
        - `0x1410DBFE0` exposes indexed substate under `owner+0x1A0`
        - the same wrapper is constructed in the upper init region and passed
          into `owner+0x3FD8`
      - `+0x3FD8` is now pinned as a large upper Player manager
        - getter `0x1410C6B90`
        - constructor `0x141E65C60 -> 0x141E66090`
        - size `0x5E70`
        - stores outer owner in `+0x10`
        - consumes the small bridge wrapper via `virtual +0x20`
        - cleanup calls its `virtual +0x138` before clearing the slot
        - `virtual +0x20` already looks like a real registration path:
          it stores the wrapper, calls `wrapper->virtual +0x18`, derives an
          entry count through `virtual +0x38`, allocates arrays, and builds
          per-entry handler objects keyed by authored/state tags
        - `wrapper->virtual +0x18 -> 0x141E52590` now looks like a fixed-count
          upper `gesture/joint/action` descriptor builder:
          it returns a compact object with `count=2` and a secondary array of
          two authored hash families
        - `virtual +0x60 -> 0x141E69AA0` is now pinned as the first manager
          update phase:
          it clears repeated per-entry counters/pointers and invalidates or
          refreshes registered handlers through `handler->virtual +0xD8`
        - `virtual +0x68 -> 0x141E69BD0` is now pinned as the second manager
          update phase:
          it rebuilds an 8-slot temporary aggregate from owner state and pushes
          that aggregate back into the registered handlers through
          `handler->virtual +0xE0`
        - the two known per-entry handler branches are not symmetrical:
          one tag builds a trivial compact handler shell, while the other tag
          builds a heavy `0x290` upper slot object that stores the bridge
          wrapper at `+0x288` and owner snapshot/state bands across
          `+0x1C0..+0x284`
        - the top-level Player init path around `0x1410CF114` now directly
          calls `+0x3FD8->virtual +0x0` with:
          - a compact typed packet/builder object
          - explicit typed entries `0x10`, `0x08`, `0x04`
          - a companion owner-local state region at `outer_player + 0x4260`
          - `virtual +0x0 -> 0x141E66D50` now looks like the upper manager
            bind/register pass:
            it derives authored hash families, stores a compact derived object
            into `manager + 0x838`, and pushes the typed init packet into every
            registered upper-manager handler through `handler->virtual +0x10`
        - the broad upper Player update path around `0x1410E9220` now uses
          `+0x3FD8` as a formal two-phase stage:
          first `virtual +0x60` reset/invalidate, then sibling upper Player
          work, then `virtual +0x68` aggregate rebuild/publish, then broader
          transform/presentation publication
      - `0x1410DC120` now looks like a compact upper Player state-tag
        classifier that returns a small enum `0..3 / -1` from
        `incoming_record + 0x4`
    - this strengthens the current conclusion that Nadine's promotion target is
      the outer real Player owner, not one isolated mounted subsystem; the best
      current upper anchors are now `0x1410DC160`, `+0x2018`, `+0x3BD0`, and
      `0x1410DE740 -> 0x140CC1450`
      - RTTI now hard-pins the real `Player` vtable at `0x142A4BFE8`
        and confirms `0x1410DC160` as a true `Player` virtual slot at `+0x58`
      - the real top-level `Player` alloc/ctor chain is now pinned as
        `0x1410C6F00 -> 0x1410C8FD0`, with final object size `0x49E0`
      - `0x1400AA8C0` now shows that `0x1410C6F00` is published as a startup
        factory/constructor pointer into the global slot `0x143EEBF00`
      - the `0x143EEBF00` slot is already reused in a live gameplay gate at
        `0x140410F34 -> 0x1401696B0`, so it is not inert startup data
      - the sibling global family `0x143EEBF90/0x143EEBFA0` is also live:
        `0x1410C6FC0` already routes it back into the Player-side `+0xC0`
        subobject through virtual `+0x198`
      - `0x1410DBF10`, `0x1410DE740`, `0x1410CF370`, and sibling helpers now
        also show up together in compact `.rdata` upper-Player function tables,
        which makes the current target look even more like a formal dispatch
        surface rather than a loose cluster of helpers
      - retained spawn commit `0x1410F14C0` now clearly seeds a real
        `ProcessSpawnInfo` packet (`vtable 0x14291CF98`) before accepting the
        retained/live player pair
      - there is now a second upper retained-spawn caller at `0x1413C04A0`
        which validates the returned pair from `0x1410F1910` and then notifies
        an outer owner through `virtual +0x240`
      - the upper `Player` init chain now contains named stages under
        `0x1410DC160`:
        - `0x1410CF370` -> `Player::InitializeDrawControl(...)`
        - `0x1410CF470` -> `Player::InitializeCompositeBody(void)`
      - `Player::InitializeCompositeBody` also has a second upper rebuild
        caller at `0x1410FF199`, which already touches `+0x1D88`, `+0x1F80`,
        `+0x3FD8`, and `+0x4308`
      - `0x1410DBF10` is now pinned as a concrete upper preflight adapter over
        a compact packet:
        - `[+0x0]` live actor pointer
        - `[+0xC]` threshold / minimum accepted result
        - `[+0x10]` optional out pointer for the produced failure-reason token
      - the tiny bridge wrapper `0x142A4BF50` is now structurally pinned:
        - `0x1410DBFC0` exposes `owner->+0x2018`
        - `0x1410DBFE0` exposes compact indexed upper substates under `+0x1A0`
        - `0x1410DC020` exposes `owner+0x3C20`
        - `0x1410DC030` relocates the stored owner pointer
        - `0x1410DC050` is the freeing dtor
        - `0x1401B8420` is the shared descriptor-builder virtual
      - `0x1401B8420 -> 0x141E52590` now looks like a fixed two-entry upper
        authored descriptor builder, which makes the `+0x3FD8` registration
        path look like a real bind surface rather than ad hoc helper glue
      - `0x1410DBF80` is now pinned as a small wildcard gate over the same
        bridge family:
        if `incoming+0xC == -1` it accepts immediately, otherwise it resolves
        one of the compact indexed upper substates under `owner+0x1A0`
      - `0x141E668C0` now clearly builds two distinct upper per-entry handler
        families from that descriptor:
        one trivial compact shell and one heavy `0x290` handler carrying the
        bridge wrapper at `+0x288` and owner-derived state across
        `+0x1C0..+0x284`
      - `player + 0xC0` now looks like a live Player-side helper/state
        registry, not ctor-only storage:
        `0x1410C6FC0` binds the global family `0x143EEBF90` into it, and the
        real `Player` ctor later reads the same subobject while matching
        authored tokens like `crouch` / `stealth`
      - `0x1410CF114` is now the strongest concrete upper bind packet builder:
        it feeds `+0x3FD8->virtual +0x0` with
        - a local typed packet builder
        - `r8d = 0xC8`
        - `r9 = outer_player + 0x4260`
        and then appends typed records with kinds `0x08` and `0x04`
        into a real packet list (`count` at `+0x28`, storage at `+0x30`,
        aggregate flags at `+0x48`)
      - the `0x08` / `0x04` typed entries under that path are now hard-pinned
        as `AnimSnapshotLayer::Setup` variants from the `0x142B352E0` and
        `0x142B35140` vtables and nearby `anim-snapshot-layer.cpp` strings
      - therefore `+0x3FD8` should now be read as an upper Player
        `AnimSnapshotLayer` manager:
        relevant for full player bringup and presentation, but not the first
        direct ownership/promotion hook for Nadine
      - the broad upper update path around `0x1410E9220` now confirms that
        both `+0x3FD8` and `player+0xC0` are formal update stages:
        first `+0x3FD8->virtual +0x60`, then `player+0xC0->inner->virtual +0x30`,
        then broader owner update/publish work
      - the real direct apply surface above that layer is still
        `0x1410DE740 -> 0x140CC1450`:
        `0x1410DE740` first validates `outer_player + 0x3C20`, then feeds
        external state into the top-level Player apply bridge, then exits
        through a paired cleanup
      - that top-level apply bridge now also conditionally hashes the authored
        token `proto` and, if needed, refreshes `player + 0x1A0` through
        `0x1417CD200 / 0x1417CD3D0`; this makes `0x140CC1450` look like a real
        upper Player state-application path, not just a transform-copy helper
      - the same apply path now has a more concrete `external_state` shape:
        it reads at least `+0x40/+0x48/+0x58` and seeds those links into the
        generic runtime-state slot through `0x1418C4840`
      - `0x1417CD200 / 0x1417CD3D0` now read as an authored-state
        resolve/finalize pair:
        hash token -> resolve upper state record -> store into `player + 0x1A0`
        -> finalize/activate
      - retained spawn commit `0x1410F14C0` now looks even more like part of
        the same upper bind surface:
        after seeding the real `ProcessSpawnInfo` packet it constructs a second
        local record, resolves a fallback through `0x1417651D0` when needed,
        and continues into `0x141C88050` instead of stopping at raw spawn copy
      - RTTI now hard-pins `ProcessSpawnInfo` vtable `0x14291CF98`; this is
        the same concrete packet family seeded inside `0x1410F14C0` before
        upper retained/apply work, which makes the future `external_state`
        patch target much more concrete
      - `0x1413C04A0` is now fully confirmed as an upper retained notify bridge:
        after a valid `0x1410F1910` pair it checks the live object id at
        `[pair->object]+0xBC` and then notifies the outer owner through
        `virtual +0x240`
      - the same retained branch is now clearly on the
        `TaskManager::SpawnPlayer` / `TrySpawnPlayerAtContinue` side of the
        upper spawn path:
        nearby literals under `0x1413C04A0` and `0x1410F1910` now hard-anchor
        it to `TaskManager::SpawnPlayer`, checkpointed continue state, and
        player/camera spawner lookup
      - conversely `0x1410DE740` is now more firmly on the `player.cpp` side:
        its literal block points straight into `Player::Player(void)` /
        `player.cpp` / `Player::AllocateAnimData(...)`
      - this gives a much cleaner promotion seam:
        `TaskManager::SpawnPlayer` side -> retained/player resolution ->
        direct Player apply side
      - the retained notify path is no longer best read as a one-off helper:
        sibling upper methods around `0x1413BC7E0` show the same owner family
        already using `virtual +0x238`, `virtual +0x240`, and `virtual +0x258`
        over the same `owner+0x100` runtime state
      - RTTI and direct vtable inspection now pin that outer owner as the
        `TaskManager` / `TaskManagerPlayMovie` family:
        - `TaskManager +0x238` -> `0x1413C3310`
        - `TaskManager +0x240` -> `0x1413BF7B0`
        - `TaskManagerPlayMovie +0x258` -> `0x1413BCAE0`
      - this is a useful correction, not a setback:
        `+0x240` is now concretely flashlight-oriented (`spawn-flashlight` /
        `turn-on-flashlight`), `+0x238` is a one-time `menu` / `main-menu`
        bootstrap path, and the play-movie `+0x258` override looks like a
        retained-camera / overlay activation helper
      - therefore the TaskManager-family retained surface remains the upper
        handoff boundary, but it is no longer the first direct promotion hook
        for Nadine
      - the strongest practical promotion seam remains:
        retained `ProcessSpawnInfo`-family packet on the TaskManager side
        (`0x1410F14C0 / 0x1410F1910 / 0x1413C04A0`) feeding the real
        Player-side apply/finalize path (`0x1410DE740 -> 0x140CC1450`)
      - `0x1410DE740` is now pinned as a thin, stable Player-side wrapper:
        it logs/validates through `player+0x3C20`, then does almost nothing
        except call `0x140CC1450` and run the paired finalize/cleanup
      - `0x140CC1450` now has practical field-level preconditions:
        - slot kind from `player+0x164`
        - optional override to `0x80` when global `0x143E07473` is set and
          `player+0x161A bit0` is set
        - candidate lookup by `player+0x108`
        - candidate id match against `player+0x10C`
      - the consumed upper packet is now more concrete as well:
        `0x1418C4840` immediately reads
        `external_state+0x40/+0x48/+0x58` and seeds them into the runtime
        slot, alongside the rebuilt transform block
      - so the Player-side promotion problem is narrowing to:
        make Nadine satisfy the upper owner ids/flags consumed by
        `0x140CC1450`, then feed the same concrete packet links that Chloe's
        top-level Player path already uses
      - the TaskManager-side packet family is now more concrete too:
        `0x1410F14C0` seeds a real `ProcessSpawnInfo` packet and already tags
        it with `Player`; its fallback path explicitly resolves
        `*player-health-settings*`
      - the higher retained caller `0x1410F1910` then adds a second concrete
        layer around `*player-starting-weapons*` and the
        `TrySpawnPlayerAtContinue()` checkpointed-weapon state
      - this means the retained side is already assembling real Player-facing
        packet concerns before the Player-side apply seam, which is exactly
        the kind of state Nadine will need if we want a true second Player
        path rather than another companion-only hack
      - the upper validator pair is now concrete enough to treat as a real
        gesture/verb permit gate, not just abstract preflight:
        - `0x1410DBF10` consumes a small record
          - `+0x0` live actor
          - `+0xC` minimum accepted result or `-1`
          - `+0x10` optional out pointer for the failure token
        - `0x1410DB040` then evaluates mounted Player state across
          `+0x1A0`, `+0x1D88`, `+0x1D50`, `+0x1D48`, `+0x1E58`, `+0x1FF0`,
          and `+0x3D98`
      - the concrete blocker reasons are now pinned too:
        `cinematic is playing`, `player has two-handed weapon in hands`,
        `player is aiming`, `player is in an IGC`, `player is crouched`,
        `player has flashlight/torch/flare`, `character is in melee`, and
        several gesture/movemode/weapon-switch restrictions
      - that gives a practical future checklist for Nadine:
        promotion into the Player path is not enough by itself; she must also
        satisfy the same upper verb/gesture permit conditions as Chloe
      - new hard constraint from direct ctor/base-init disassembly:
        the visible `Player`-only stages
        (`0x1410C8FD0 / 0x1410DC160 / 0x1410CF370 / 0x1410CF470`)
        still do not directly seed `player+0x108/+0x10C`
      - instead, the shared/base init stage `0x140CBFF50` already consumes a
        compact upper init packet via `arg3+0x28`, copies packet state into
        `player+0x15A4/+0x15A8/+0x15AC/+0x15B0`, merges packet bit0 into
        `player+0x161A`, and builds `player+0x1B10`
      - a second direct pass through `0x140CBFF50` now shows that it also
        copies a shared external runtime/profile source from `0x143FE1088`
        into the upper Player band `+0x18B0..+0x1910`, including
        `source+0x108 -> player+0x18E0`; this still does not explain the
        later `player+0x108/+0x10C` pair consumed by `0x140CC1450`
      - the nearby direct `+0x108` writes found so far are false friends:
        `0x1410DE520` writes the sibling owner-local `+0x3BD0` object field,
        while the `0x140CC1080` and `0x1410F21AF` regions only zero local
        packet layouts before publish
      - repeated live Player helper families at `0x1410D10D5`, `0x1410D171D`,
        `0x1410D3575`, `0x1410D4F45`, `0x1410D505C`, and `0x1410D5123` all
        re-read the same `player+0x108/+0x10C` pair, resolve a shared record
        from `0x14386FD00`, validate `record+0x50`, and refresh
        `player+0x110`
      - on the retained side, `0x1410F14C0` still looks more like a
        retained/live pair committer than the final Player apply packet:
        it stamps a local `ProcessSpawnInfo` packet, runs it through
        `0x1417C32A0 -> 0x1417C3790`, and only then harvests the returned
        object's `+0xA8/+0xBC` pair
      - the newest correction is narrower and more useful:
        the direct structural match for the upper Player apply seam
        `0x1418C4840(external_state+0x40/+0x48/+0x58)` is now the extended
        wrapper packet family at `0x1417C35A0 / vtable 0x1428D5D60`, not the
        returned materialized object from `0x1417C3790`
      - this same wrapper family is now confirmed on a second axis too:
        `0x141D21C80` builds `0x1417C32A0/0x1417C35A0` descriptors and feeds
        them directly into entity `virtual +0x538`, so the wrapper is already
        an accepted runtime-command packet on the generic entity-side verb
        interface
      - `0x1415FC800 / 0x1415FC9F0` are now pinned as internal intrusive-node
        bookkeeping for that returned materialized object family, not direct
        promotion/apply helpers
      - proper PE-backed vtable mapping tightened the real upper `Player`
        surface:
        - `+0x300 -> 0x141BB8BC0`
        - `+0x6F0 -> 0x1410DE740`
        - `+0x710 -> 0x1410CE260`
        - `+0x718 -> 0x1410CF370`
        - `+0x738 -> 0x140CC18D0`
        - `+0x850 -> 0x1410DE890`
      - this immediately corrects one stale assumption:
        `Player` virtual `+0x300` is only a compact current upper-state getter
        from the live actor, not the main top-level apply hook
      - the strongest new positive result is on the Player side:
        `Player::InitializeDrawControl` (`0x1410CF370`, vtable `+0x718`)
        already consumes the extended wrapper family
        `0x1417C35A0 / 0x1428D5D60` directly and forwards it into
        `0x141BBACC0` together with a draw/control context
      - `0x141BBACC0` is therefore better read as an upper `Player`
        wrapper/context binder over the companion-behavior graph, not as a
        purely companion-local helper
      - `Player` virtual `+0x738` (`0x140CC18D0`) only seeds
        `player+0x15B4` from `context+0x58E0`; useful for reconstruction, but
        not a promotion hook
      - `Player` virtual `+0x850` (`0x1410DE890`) is a heavier upper rebuild
        path that writes a retained/live pair into `player+0x3828` through
        `0x1417C3790`; important for full rebuild, but still downstream of the
        main direct apply seam
      - a new separate upper Player band is now pinned around
        `player+0x1A0/+0x1A8/+0x4260`:
        `Player +0x700` allocates and populates a `0x2B0` upper state owner
        through `0x141728690/0x141728720`, `+0x708` feeds the classifier/count
        consumed by that builder, and `+0x710` then publishes
        `anim-net-player`, `*player-overlay-priorities*`,
        `*player-default-overlays-count*`, `*player-default-overlays*`,
        `apReference`, `align`, `run-chloe`, and `base-read-joints-layer`
      - direct vcall scans now show that this `+0x700/+0x708/+0x710` band is
        real but separate: `+0x710` is only called from inside `+0x700`, and
        the already-pinned `+0x850` rebuild path is still only proven to order
        `+0x738 -> +0x6F0 -> +0x718`
      - a second strong upper seam now exists around `0x141BB4AA0`, which
        explicitly orders:
        `+0x738 -> +0x6F0 -> +0x700 -> +0x718`
        and therefore may be a better runtime patch surface than treating
        `+0x850` as the only meaningful upper orchestrator
      - that same seam continues further through:
        - `+0x6F8` active-state payload application into `player+0x1A8`
        - `+0x768` trivial permit gate
        - `+0x4A8` effective state-kind output
        so the practical upper sequence is now broader than just
        `+0x738/+0x6F0/+0x700/+0x718`
      - a second direct consumer at `0x1413086BC` invokes `+0x700/+0x708` and
        then continues into the mounted Player subsystem at `+0x1E98`, which
        ties the new upper state-owner band back into a concrete Player verb
        owner rather than leaving it as an isolated helper band
      - later correction from direct disassembly and Hex-Rays:
        - real `Player +0x740` (`0x141BCD340`) is a draw-control /
          ambient-occluder resolver, not an ownership seam
        - its containing stage is `0x141BB4280`, a broader Player draw-control
          finalization path that contains the sibling block `0x141BB4AA0`
        - the second upper consumer block `0x141308680` sits inside a
          concrete verb stage `0x141308220` that hashes
          `edge-aim-reload`, `idle-reach0-stamina`, and `idle-reach0`
        - `0x141BBACC0` is now pinned as a downstream upper behavior /
          draw-control binder over `player+0x198`, not as a Player-identity
          source
        - `Player +0x718` is therefore better treated as a downstream bind
          stage, not the first practical promotion hook
        - the stronger upstream shared/base handoff is now
          `0x140CBFF50 -> 0x141E994C0`
        - `0x140CBFF50` now clearly seeds:
          - `player+0x15A4/+0x15A8/+0x15AC/+0x15B0`
          - `player+0x161A`
          - `player+0x1B10`
          - and the upper band `+0x18B0..+0x1910`
          before the handoff
        - `0x141E994C0` then expands that compact packet into the broader
          upper lifecycle by seeding upper defaults, invoking `0x141BB4280`,
          continuing through `player->virtual +0x788`, and materializing
          `player+0x4A8`
      - practical effect:
        the shortlist for Nadine promotion tightens back toward the real upper
        apply/identity surfaces (`0x140CBFF50 -> 0x141E994C0`,
        `+0x6F0`, `0x1410DE740 -> 0x140CC1450`) instead of the draw-control
        slot `+0x740`, the downstream bind `+0x718`, or the verb-specific
        upper callers
      - this narrows the remaining promotion search:
        the hidden descriptor/id pair used later by `0x140CC1450` is still
        more likely to belong to the embedded authored-descriptor subobject
        rooted at `player+0x100`, because `0x1416860A0` now proves that
        `player+0x108/+0x10C` are consumed there very early to resolve
        `0x14386FD00`
      - practical effect:
        the remaining writer hunt should move earlier than
        `0x140CBFF50 / 0x141E994C0`, toward the base/packet init stage that
        seeds the embedded `player+0x100` object before `0x1416860A0`
      - `0x1416860A0` is also broadly reused outside this one init path, so
        `player+0x100` should now be treated as a shared authored-descriptor
        carrier rather than a Player-local scratch object
      - the extended retained/apply wrapper family around `0x1417C35A0`
        still matters because the same wrapper already survives into the real
        upper `Player` surface at `0x1410CF370`
      - the rebuild ordering is now concrete enough to treat as baseline:
        real `Player +0x850` does not jump straight from wrapper to promotion;
        it first seeds narrow upper context through `+0x738`, then
        materializes/attaches active upper state through `+0x6F0`, and only
        after that binds extended wrapper/context state through `+0x718`
      - the `+0x700/+0x708/+0x710` upper state-owner band is now tracked as a
        neighboring upper bringup path, but it is not yet proven to sit inside
        the real `+0x850` ordering
      - a sibling upper consumer around `0x14133939D` also runs a `+0x718`
        stage and then validates the resulting pair against the same authored
        table `0x14386FD00`, which reinforces that the wrapper/bind family and
        the stable authored pair system are one upper state layer
      - direct `.text` writer scans still do not show a simple top-level
        writer for the real `player+0x108/+0x10C` pair, so the promotion work
        should keep treating that pair as something seeded indirectly by a
        deeper shared/base handoff or packet copy, not by a visible one-line
        write in the Player-only ctor/init stages
      - new packet-level tightening:
        `0x1417C32A0` writes the shared source link into `packet+0x20`, and
        `0x1417C35A0` preserves that same `+0x20` field in the extended
        wrapper family later consumed by the earliest `Player::Init` refresh
        at `0x1416860A0`
      - retained/runtime dispatch now confirms that the same extended wrapper
        family is already accepted by generic entity-side runtime commands:
        `0x141D21C80` chooses either a base packet (`0x1417C32A0`) or the
        extended wrapper (`0x1417C35A0`) and passes it into entity virtual
        `+0x538`
      - retained slot promotion tightened further:
        `0x1410F3B20` also manually builds the same base packet plus extended
        wrapper family before passing control into the upper retained manager
        path, which makes it one of the strongest promotion-adjacent retained
        entrypoints now on the shortlist
      - that retained manager path is no longer anonymous:
        `0x140A90E80` is now pinned by its own assert strings as
        `NetGameManager::GetMeNetPlayerTracker`, with real screen-index gates
        `> -1` and `< 2`, returning tracker entries from `manager+0x3A0`
      - the helper at `0x1413BF090` is now a strong retained-side producer of
        that extended wrapper family, which makes it a better upstream bridge
        candidate than the broader retained-spawn story alone
      - the retained shortlist is now stronger and narrower:
        `NetPlayerTracker::SpawnPlayer (0x140B31F60)` already resolves
        screen-indexed tracker ownership, pulls online player-start/loadout
        state, calls `PromoteRetainedPlayerSlot`, and then immediately
        consumes the returned real `Player` object
      - the manager/tracker chain above it is now named:
        `NetGameManager::RespawnPlayer (0x140A9AAD0)` ->
        `NetPlayerTracker::NetRespawn (0x140B347C0)` ->
        `NetPlayerTracker::StartOrQueueSpawnPlayer (0x140B346B0)` ->
        `NetPlayerTracker::SpawnPlayer (0x140B31F60)` ->
        `PromoteRetainedPlayerSlot`
      - `NetGameManager::AddOrUpdateNetPlayer (0x140A90550)` now looks like
        the manager-side seed for that chain:
        it writes the effective local slot/player index into `tracker+0x1E0`,
        and `NetPlayerTracker::SpawnPlayer` later passes that exact field into
        `PromoteRetainedPlayerSlot`
      - the respawn-event layer above that pipeline is now named too:
        `NetEventRespawnPlayer::Fill/HostFill`,
        `SendNetEventRespawnPlayer`,
        `SendNetEventSignalRespawnPlayer`
      - practical effect:
        if the direct slot-1 tracker path is easier to drive from the event
        layer than from pure Player apply code, there is now a second viable
        top-down patch surface above `NetPlayerTracker::NetRespawn`
      - later refinement:
        `0x140A90A00` is not the generic public wrapper after all; it carries
        `NetGameManager::AddOrUpdateNetPlayerCinema`, while the heavier
        `0x140A90550` is the generic `AddOrUpdateNetPlayer` worker
      - practical effect:
        the manager-side shortlist should prefer:
        `0x140A90550` as the generic tracker seed surface,
        `0x140A90A00` only for the cinema/local-player flavored path,
        and `0x140A9AAD0` for respawn-driven bringup
      - new strengthening:
        one generic caller above `0x140A90550` already invokes the worker
        twice with slot arguments `0` and `1`, which is the strongest current
        evidence that the engine still contains a real dual-slot manager-side
        bringup pattern
      - later refinement:
        the event transport handler above respawn exists
        (`0x140B6BB80 -> NetGameManager::RespawnPlayer`), but it is less
        attractive as a first runtime patch surface than the manager-side
        pair:
        `0x140A90550`/`0x140A90A00` and `0x140A9AAD0`
      - practical effect:
        the best candidate for Nadine promotion may already be a built-in slot
        1 net-player bringup, not a bespoke replay of upper `Player` ctor
        stages
      - this is currently the strongest built-in co-op-facing bringup path in
        the binary because it is no longer just retained promotion in
        isolation; it is a named `NetGameManager` -> `NetPlayerTracker` ->
        real `Player` pipeline
      - the sibling helper `0x140A90F40` broadens that same two-slot family
        into a runtime-object lookup by hashed `Player` identity, which makes
        the tracker layer look like a real discoverable second-player runtime
        representation rather than a one-off spawn helper
      - the broader caller `0x140A58310` now looks like an external
        player-record bridge above the tracker layer:
        it probes existing trackers by the pair
        `record+0x18/+0x1C -> tracker+0x1DC/+0x1E0`,
        falls back to either `0x140A90A00` or `0x140A90550`,
        and then copies the wider state block `record+0x20..0x3C`
        into tracker fields `+0x86C..+0x884`
      - token anchors make those bridges more specific:
        `0x140A58190` and `0x140A58310` both build through
        `late-joiner-%d`, while `0x140A4E7DC` builds through
        `cinema-player-%d`
      - practical effect:
        the manager-side seams are no longer just "generic net-player" paths;
        they already split into authored late-joiner and cinema-player
        families
      - `0x140A90A00` is also tighter now:
        it writes `tracker+0x1DC = incoming_r8d`, forces `tracker+0x1E0 = 0`,
        copies the local `0x40` payload into tracker-local bands, hashes the
        fixed token `NetCharacter-`, and stores the resulting family/state id
        into `tracker+0x1E4`
      - raw caller scans now isolate the cinema wrapper even further:
        only four direct callers remain
        (`0x140A4DDA0`, `0x140A4E7DC`, `0x140A58190`, `0x140A58310`)
      - the useful split is:
        - thin cinema-only wrappers:
          `0x140A4DDA0` and `0x140A4E7DC`
        - broader record bridges:
          `0x140A58190` and `0x140A58310`
      - practical effect:
        the generic worker `0x140A90550` stays the broader second-player
        surface, but the cinema path now has two very narrow local seams
        above it if a smaller first patch is preferable
      - later tightening:
        raw rel32 scans show that `0x140A4DDA0` and `0x140A4E7DC` do not have
        direct code callers in `.text`, so they are most likely
        table-dispatched entries rather than clean public code seams
      - practical effect:
        the explicit bridges `0x140A58190` and `0x140A58310` remain better
        local patch candidates than those two thin wrappers
      - final tightening on this layer:
        raw rel32 scans show that `0x140A58190` and `0x140A58310` also lack
        direct `.text` code callers, just like the two cinema wrappers
      - practical effect:
        the whole cinema/late-joiner helper band is now best treated as
        table-dispatched machinery under the manager layer, while the real
        explicit public seams remain `0x140A90550` and `0x140A9AAD0`
      - `0x140A9AAD0` is now tighter:
        it is the clean explicit public respawn seam on this layer
        because it stages fresh tracker state, then either hands control to
        `NetPlayerTracker::StartOrQueueSpawnPlayer` or queues deferred respawn
        through tracker fields `+0xA00/+0xA04` with timing in
        `+0x4C0/+0x4C8`
      - `0x140A90550` is now concrete enough for field-level patching:
        it writes incoming payload into
        `tracker+0x7C0..+0x7DF`, `+0x7E0`, `+0x7B8`, `+0x1DC`, `+0x7BC`,
        sets `tracker+0x1E0` to the requested slot, derives
        `tracker+0x1E4` from the `NetCharacter-` hash family, and mirrors the
        local-player tracker back into manager bands `+0x3A0/+0x3B0`
      - `NetPlayerTracker::StartOrQueueSpawnPlayer` is also concrete now:
        it clears `+0xA7C/+0x814`, then either tail-calls
        `NetPlayerTracker::SpawnPlayer` or rewrites the same deferred queue
        fields `+0xA00/+0xA04/+0x4C0/+0x4C8`
      - `NetPlayerTracker::SpawnPlayer` now clearly consumes:
        `tracker+0x1E0` as the retained slot,
        `tracker+0x1E4` as later upper authored/runtime state id,
        and snapshots `+0x840/+0x844` into `+0x848/+0x84C` before clearing
        `+0x840`
      - immediate respawn now has one more concrete prerequisite:
        `0x140B44EC0` stages `tracker+0x500..+0x540` from a valid transform
        source, preferring `tracker+0x8/+0x10` -> `live_actor+0x100/+0x120/+0x140`
        and only falling back to `tracker+0x20`
      - stronger local seam:
        `NetPlayerTracker::NetRespawn (0x140B347C0)` already exposes a bypass
        for that requirement; with `r8b=1` it copies a caller-supplied
        `0x50` block from `r9` straight into `tracker+0x500..+0x540`
      - confirmed live bypass callers:
        `0x140BC0425` and `0x140BDB23F`
      - `0x141685C30` is now the reusable helper that builds this direct
        `0x50` respawn block from a reference transform plus compact seed
        record
      - the live tracker-side bypass caller uses:
        `rdx = tracker+0x20`
        as the reference-transform carrier and seeds the compact record with:
        sentinel dword/qword, mode at `+0xC`, zero vec4 at `+0x10`, default
        vec4 at `+0x20`
      - later correction on that same seam:
        `tracker+0x20` is not a pair record; `0x141685C30` only consumes the
        first `16` bytes at `rdx` as reference position and derives the rest
        of the `0x50` block from the compact seed plus optional authored
        override lookup
      - practical effect:
        the first local patch can feed `0x141685C30` directly from
        `Nadine+0x100` without pre-seeding `tracker+0x20`
      - a disabled-by-default scaffold for this exact direct path now exists
        in `scanner/src/teleport_hook.cpp`:
        manager anchor `0x143DD70C0`, slot-`1` tracker lookup
        `0x140A90E80`, block builder `0x141685C30`, and direct
        `NetPlayerTracker::NetRespawn`
      - stronger refinement:
        retail disassembly now pins the low tracker-create worker shape too:
        `0x140A90550(manager=0x143DD70C0, context=0x143E08C80, source_obj, slot, flag)`
      - the same scaffold now also contains a disabled attempt to create
        a missing slot-`1` tracker through that low worker using
        `sourceA[0]` as the first candidate source object
      - the remaining first blocker is now narrower:
        proving which `source_obj` is safe for `0x140A90550` on a live run
      - later correction from retail disassembly:
        `0x140A90550` is only the low allocation/populate worker; real callers
        always follow it with a post-call init band
      - current P0 therefore splits into two concrete subproblems:
        1. choose a safe `source_obj`
        2. choose the minimal stable post-call init family after
           `0x140A90550`
      - fallback seam also improved:
        `0x140A58310` late-join bridge now has a concrete wider record layout
        through `+0xC8`, so it remains a viable second patch surface if the
        lower source-object worker is unstable
      - sibling split inside the tracker:
        `tracker+0x20` is the reference-position carrier, while the optional
        live actor pair used later by `NetPlayerTracker::NetRespawn` sits at
        `tracker+0x60`
      - current focused bringup slices do not seed `tracker+0x60`, and the
        compact pair helper family is not called from the narrow
        `0x140A90550 / 0x140B31F60 / 0x140BC0425` chain
      - practical effect:
        `tracker+0x60` should be treated as a later-quality band, not as the
        first blocker for slot-`1` bringup
      - another minimization:
        in the focused dumped `NetPlayerTracker::SpawnPlayer` body, the only
        clearly recurring field from the fresh add/update payload is
        `tracker+0x1E0`
      - no direct reads from that focused body showed up for:
        `tracker+0x7C0..+0x7E0`, `tracker+0x7B8/+0x7BC`, or `tracker+0x1E4`
      - practical effect:
        the widened payload from `0x140A90550` now looks more like
        downstream/session state, while `tracker+0x1E0` remains the one
        obvious first-order slot-promotion field
      - `0x140A90A00` is now clearly local-only:
        it hard-forces `tracker+0x1E0 = 0`, then seeds the same broader
        tracker payload and derives `tracker+0x1E4` from the `NetCharacter-`
        family
      - practical effect:
        keep `0x140A90A00` only as a payload-template reference, not as a
        first slot-`1` patch seam
      - `0x140B411F0` / `0x140B412D0` turned out to be post-spawn selectors
        for `player+0x854` / `player+0x858`, with hashed defaults if zero;
        they are not the first blocker for a slot-`1` patch
      - later correction:
        the event-style caller above that seam starts at `0x140B6B970`, not
        the interior block `0x140B6BB80`
      - raw caller scan now shows `0x140A9AAD0` has only one direct `.text`
        caller: `0x140B6B970`
      - separate tightening:
        the deferred queue consumer around `0x140BC0425` actually lives in
        the broader function `0x140BBFD70`, which drives
        `NetPlayerTracker::NetRespawn`, resumes queued respawn through
        `tracker+0xA00/+0xA04`, clears the pending bit, and refreshes the
        tracker again
      - the broader direct callers above `0x140A90550` are now anchored by
        session/matchmaking tokens:
        `joined-tournament-match`, `return-to-search`, `map-chosen`,
        `matchmaking-failed`, `timed-out`, `start-game-failed`
      - practical effect:
        those heavier callers are useful proof of a live multiplayer/session
        layer in the PC binary, but they drop below the narrower
        `late-joiner-%d` and `cinema-player-%d` seams for the first local
        patch
      - correction:
        the exact call target `0x141E7C5F0` used after the early
        `player+0x100` refresh is just a stub `ret`; the real body starts at
        `0x141E7C600`, so the active early `Player::Init` work remains the
        `0x1416860A0` refresh plus the later `player->virtual +0x208` path

## Current Runtime Staging Rule

- A fresh runtime probe already reached a non-null built-in slot-`1` tracker before the game crashed.
- That demotes forced tracker creation below staged validation of the existing slot-`1` tracker path.
- Immediate runtime order:
  1. tracker field readback
  2. respawn-block build only
  3. `NetRespawn` only after both earlier stages survive

- runtime correction: `0x140A90E80` is no longer treated as a safe slot-`1`
  tracker getter after a probe returned a code pointer instead of a stable
  tracker object; next staging uses direct manager slot storage.
- runtime correction: direct `manager + 0x3A0 + slot*8` produced the same code-like pointer as the old callable slot getter, so slot storage itself must be typed before any further respawn staging.
- runtime-safe correction: the real per-slot object is the inline manager band
  `manager + 0x3C0 + idx*0x43D0`; subsequent respawn staging should use that
  path only.
- runtime narrowing: with the corrected inline slot object, the first confirmed
  crash surface is now `BuildRespawnTransformBlock` rather than slot lookup.
- runtime narrowing: `BuildRespawnTransformBlock` still crashes even with the corrected live-caller source pointer (`inline slot + 0x20`), so the remaining blocker is the builder's seed/context contract.

- static correction: the earlier `BuildRespawnTransformBlock` probes used the wrong seed-record layout; the next runtime pass must use the exact live caller stack shape before concluding the builder itself is unsafe.
- runtime narrowing: even the exact live seed layout does not make `BuildRespawnTransformBlock` safe; the next probe should only dump inline-slot pre-call state around `+0x20/+0x30/+0x40`.
- runtime narrowing: the global source band at `0x143DD7480 + idx*0x43D0` is separate from the inline slot object, and slot-`1` source is mostly empty in campaign; next test should hit `source0 + 0x20` before any more slot-`1` builder retries.
- runtime narrowing: even the partially initialized global source band (`source0 + 0x20`) does not make `BuildRespawnTransformBlock` safe, so the remaining missing piece is the caller-side preparation lifecycle above the builder.
- runtime correction: the helper `0x140A90E50` should now be treated as an
  inline per-slot session band getter rather than a tracker seam; live reads
  from `manager+0x3C0+idx*0x43D0` exposed map/mode string state
  (`NET_MAP_REMNANTS_FOREST_VIP_TDM`) instead of tracker/runtime fields.
- tooling update: a new external read-only dumper target
  `tll_runtime_code_dump.exe` is ready for the next gameplay session. It dumps
  live code bytes for `0x140BBFD70`, `0x140B44EC0`, `0x141685C30`,
  `0x140B42310`, and `0x141C8D740`, which is now the preferred next step over
  any more injected builder probes.
- runtime correction: the repeated builder crashes were caused by the probe's
  C++ struct padding, not by `0x141685C30`. After switching to a raw packed
  byte seed, `BuildRespawnTransformBlock` succeeds for `source0+0x20`,
  `source1+0x20`, and `Nadine+0x100`.
- runtime narrowing: the blocker has moved from the respawn block format to the
  receiver object for `NetRespawn`. A direct call with
  `rcx = *(manager+0x3A8)` enters `0x140B347C0` and does not return, so that
  pointer band should still be treated as upper indirection rather than a
  confirmed tracker object.
- runtime narrowing: the first direct call into
  `NetGameManager::AddOrUpdateNetPlayer(slot=1, source0, flag=0)` also does not
  return. The remaining unknown is now the exact `source_obj` /
  pre-initialization contract for the built-in slot-`1` manager path.
- static correction: real retail callers pass an actual source entry into
  `0x140A90550`, not the raw global base `0x143DD7480`. The next runtime pass
  should therefore use `0x143DD7480 + 0x43D0`-style entry pointers instead of
  the base address.
- runtime narrowing: even `0x143DD7480 + 0x43D0` is too weak in campaign. The
  next probe should use the caller-produced fast-path source candidate at
  `0x143E08C80 + 0x1D00`, which is what `0x14194D5F0(..., r8b=0)` materializes
  before the dual-slot `AddOrUpdate` calls.
- runtime narrowing: even the exact `BuildNetPlayerSourceList` step is still
  incomplete by itself. The retail dual-slot caller first runs
  `*(0x143E08C88)->virtual + 0x78` against `0x143E08C80`, and only then calls
  `0x14194D5F0`. That upstream context-preflight call is now the next seam.
- runtime narrowing: in the current campaign state the `0x143E08C88` owner is
  actually null, so that whole seam is inactive. The next upper candidate is
  the sibling live context family `0x143E0EF00/08`, which nearby branches use
  instead of `0x143E08C80/88`.
- structural correction: `0x143E08C88` and `0x143E0EF08` are not standalone
  owner globals; they are the embedded `+0x8` owner fields inside the concrete
  shared-context objects `0x143E08C80` and `0x143E0EF00`. The next static task
  is to find who populates that `+0x8` field, not who writes some external
  manager pointer.
- structural correction: `0x143E0EF18` is likewise not a standalone global; it
  is `ctxB + 0x18`, the current-selected pointer inside `0x143E0EF00`.
  `0x140B1E400` only stages pending selection into `0x143E10E90` and
  `0x143E0F618..+0x38`; it does not commit `ctxB + 0x18`. So the remaining
  real blocker is now the missing bind/commit path `ctxB + 0x8 -> ctxB + 0x18`
  in campaign.
- new commit-side narrowing: the larger ctxB state machine around
  `0x140B1E890..0x140B1FA61` does hand the staged candidate forward.
  Branch `0x140B1ECBC` loads `0x143E10E90`, calls
  `*(0x143880318)->virtual + 0x48(&ctxB, pending)`, then clears the pending
  pointer. The remaining unknown is now that global service's `+0x48`
  implementation rather than the presence of any local commit handoff at all.
- service-layer correction: `0x143880318` is now pinned as a mode-dependent
  global selection service, not an opaque singleton. Startup
  `0x1409FA94E..0x1409FAA23` installs either `0x141973750` or `0x1419796F0`,
  binds through `0x1409EB500`, then runs `virtual +0x8`; teardown
  `0x1409FDD70..0x1409FDDEE` calls `virtual +0x18`, then `virtual +0x0`, and
  clears the globals. The remaining commit-side unknown is the service's
  `virtual +0x48`.
- new ctxB-object narrowing:
  - visible ctxB vtable slots now cover dispatch/rebuild/reset/publish paths,
    but still show no plain direct writer for `ctxB + 0x18`
  - both decoded service `virtual +0x48` variants also look like queue/entry
    workers rather than a simple `ctxB + 0x18 = pending`
  - current strongest remaining writer candidates are:
    - `ctxB + 0x8 -> owner->virtual +0x50`
    - deeper callbacks under the `0x141975870 / 0x14197AE50` service-entry
      families
- bind-side correction:
  - the first real writer to `ctx + 0x8` is no longer unknown
  - service `virtual +0x20` variants `0x1419739E0 / 0x141979A60` both store
    `ctx + 0x8 = service`
  - but current retail startup only binds `0x143E08C80`, `0x143E0AC40`, and a
    third `rsi` context, not `ctxB = 0x143E0EF00`
  - current blocker is therefore narrower:
    find the first `service->+0x20(&ctxB)` call-site, or prove campaign uses
    the later `ctxB->+0x28 / service->+0x48` path without standard bind
- active campaign-path correction:
  - `0x140B1EE70` is now the real live `ctxB` request path:
    - `ctxB->virtual +0x28 = 0x140B1DF30`
    - then `service->virtual +0x28`
  - the local token is fixed:
    - `0x140B1DF30 -> eax = 0xA`
  - `0x140B204E0` remains a neighboring heavy payload builder, but is no
    longer mapped to `ctxB->virtual +0x28`
  - variant B `service->virtual +0x48 = 0x14197AE50` is now the strongest
    visible commit worker:
    - packages `ctx+0x1ED0/+0x1ED4` plus pending candidate
    - writes `ctx + 0x18 = pending`
    - waits through repeated `service->virtual +0x10`
    - resolves through `ctx + 0xB8 / +0xBC`
  - next blocker is now narrower:
    - find the real campaign writer for `ctxB + 0x8`
    - and separately prove whether campaign ever forces variant B at all
- startup variant-switch now narrowed:
  - `0x1409FA94E` first installs variant A, copies the upper packet through
    `0x1409EB500`, and runs `service->virtual +0x8`
  - `0x1430641DC` is the `Net Init Error Testing` selector, not a normal mode
    enum
  - if init succeeds and `0x1430641DC != 0x14`, startup keeps variant A
  - only failing init or `0x1430641DC == 0x14` forces variant B and overwrites
    `0x143880318`
  - so the remaining runtime question is tighter:
    does campaign ever leave variant A outside that explicit fallback/test gate
- exact vtable narrowing:
  - `.rdata` now pins the real slots:
    - variant A `+0x28 = 0x141974E10`
    - variant B `+0x28 = 0x14197ABC0`
  - the live campaign call at `0x140B1EE70` is therefore exact, not inferred
  - both variants accept the real `(service, &ctxB, token, 1)` shape
  - the remaining runtime question is now only which variant is actually live
    at `0x143880318`
- post-bind consumer narrowing:
  - `0x140B21E70` and `0x140B21F10` both require `ctxB+0x18` and `ctxB+0x8`
    and then immediately call `0x14194D5F0(&ctxB, out_list, 0)`
  - `0x140B21E70` additionally resolves entries through `0x14194E510` and
    succeeds once a resolved object has byte `+0x42 != 0`
  - this makes `0x14194D5F0 / 0x14194E510` the first pinned consumer band
    immediately downstream of the still-missing owner bind
- owner-interface narrowing:
  - bound `ctxB+0x8` is now known to implement a wider live contract:
    - `virtual +0x50`
    - `virtual +0x58`
    - `virtual +0x70`
    - `virtual +0x78`
  - `0x140AEAAFC` already shows `owner->virtual +0x58(&ctxB, 1)` before
    another `0x14194D5F0` rebuild
  - `0x140835A95 / 0x14083E5AE` show `owner->virtual +0x70(&ctxB, source_id)`
  - `0x140C11697` shows `owner->virtual +0x78(...)`
  - so the missing bind is now better typed: we are looking for the first
    campaign path that binds a concrete owner implementing this whole interface
- fallback-owner narrowing:
  - `0x14194D5F0` single-entry fallback does a raw clone from `ctxB+0x8`:
    - `owner+0x8..+0x27 -> ctxB+0x1D00..+0x1D1F`
    - `owner+0x28 -> ctxB+0x1D20`
  - startup helper `0x1409EB500` seeds those exact bytes into the live global
    selection service object at `0x143880318`
  - this is now the strongest local evidence that the intended fallback owner
    behind `ctxB+0x8` can be the service object itself, not a separate
    service-entry pointer
  - the bind search therefore narrows again:
    find the first real campaign `service->virtual +0x20(&ctxB)` call-site
- local-wrapper widening:
  - tiny ctxB wrappers now pin additional owner slots:
    - `0x14194DB30 -> owner +0x58`
    - `0x14194DB70 -> owner +0x60`
    - `0x14194DB90 -> owner +0x98`
    - `0x14194DBB0 -> owner +0xA0`
    - `0x14194DBD0 -> owner +0xA8`
    - `0x14194DBF0 -> store ctxB+0x200, then owner +0x78`
  - this widens the proven owner-facing surface, but still does not expose the
    first non-null campaign bind into `ctxB+0x8`
- manager-path correction:
  - the strongest dual-slot caller above `0x140A90550` does read `ctxB`
    through `0x14194D5F0` and compares `ctxB`/`ctxA` source availability
  - but its actual `AddOrUpdateNetPlayer` calls still pass:
    - `rdx = &0x143E08C80`
    - `r8  = source entry from ctxA`
  - so `ctxB` currently looks like an upper gating/selection context, not the
    direct manager-side source context for slot creation
  - practical consequence:
    - keep the `ctxB` bind search for the shared-context selection layer
    - but do not conflate it with the low `AddOrUpdate` source contract
- startup-bind correction:
  - retail startup in `0x1409F9B20` does bind `ctxB`
  - exact sequence:
    - `0x1409FA6DE`: `rsi = 0x143E0EF00`
    - `0x1409FAA54`: `service->virtual +0x20(&0x143E08C80)`
    - `0x1409FAA68`: `service->virtual +0x20(&0x143E0AC40)`
    - `0x1409FAA78`: `service->virtual +0x20(rsi)` where `rsi == &ctxB`
  - a `.pdata`/Capstone scan over retail `.text` now shows exactly these
    three global-service bind calls and no additional later
    `service->virtual +0x20(&ctxB)` site
  - the blocker therefore moved:
    not "where is the bind path in the binary", but
    "what later lifecycle clears or bypasses the bound owner so live campaign
    can still end up with `ctxB+0x8 == 0`"
- upper-shutdown classification:
  - `0x1409FD360` is broader than a local ctxB/service helper:
    it clears both shared-context owner globals and tears down
    `0x143880318`
  - its heavy direct callers are now typed as front-end/net lifecycle
    transitions:
    - `0x1409EE7D0` -> `added-to-stage` / `popped`
    - `0x1409F78B0` -> `update`
    - `0x1409F7C60` -> `update` / `popped`
    - `0x1409EE9B0` -> multiplayer front-end shutdown flow
    - `0x141CE32E9` -> `edx == 4` high-level state gate
  - the restart side is now tighter too:
    - `0x1409FBA60` does not directly jump into the bind block
    - but it does take the address of `0x1409F9B20` and hand it to
      `0x1415B43B0 -> 0x1415B4870 -> 0x1415B47C0` as a callback record
    - `0x1409FA94E` is therefore better read as an inner bringup block
      inside that scheduled outer callback
    - that callback is now confirmed deferred:
      `0x1415B43B0` allocates the batch,
      `0x1415B4870` copies the `0x80`-byte records,
      `0x1415B47C0` finalizes them,
      and `0x1415B4560` enqueues the batch into a `0x2200`-stride per-lane
      queue under `0x1437E9700`
  - so the remaining lifecycle question narrows again:
    which consumer drains that deferred lane queue, and under what runtime
    conditions the scheduled `0x1409F9B20 -> 0x1409FA94E` callback actually
    executes after shutdown and rebinds `ctxB`
- ctxB-vtable correction:
  - direct `0x1429EF890` decode now pins:
    - `ctxB->virtual +0x20 = 0x1407DF190`
      - simple getter for `ctxB+0x18` gated by byte `+0xC8`
    - `ctxB->virtual +0x38 = 0x140B204A0`
      - toggles `ctxB+0x200` and tail-calls `owner->virtual +0x78`
    - `ctxB->virtual +0x40 = 0x140B204D0`
      - returns inverted bit state from `ctxB+0x200`
  - this cuts off the false hypothesis that upper `ctxB->+0x20` callers are
    latent owner-bind paths
