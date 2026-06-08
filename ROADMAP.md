# TLL Co-op Mod Roadmap

Target build: `tll.exe 1.4.21058.0`

## Goal

Run The Lost Legacy campaign on two PCs:

- player 1 controls Chloe with the normal game controller;
- player 2 controls the campaign's existing Nadine;
- each player has an independent camera;
- campaign scripts, combat, checkpoints, and saves remain host-authoritative.

## Chosen Architecture

Keep Chloe and the campaign simulation on the host. Reuse the existing
`NadineBuddy` instance and replace selected AI decisions with remote-player
commands.

The host mod owns Nadine's authoritative state. A lightweight client captures
player-2 input, renders or controls a second game instance, and exchanges
input/state packets with the host.

Teleport is reserved for spawn, checkpoint recovery, and large corrections.
Normal movement uses the native NPC locomotion system.

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

## Phase 1: Local Nadine Control

Objective: control Nadine from the host PC before adding networking.

- [ ] Poll a dedicated keyboard or controller input state.
- [ ] Convert WASD/stick input into a camera-relative direction.
- [ ] Refresh a short navigation goal in front of Nadine while input is held.
- [ ] Stop cleanly when input is released.
- [ ] Select walk/run speed from input magnitude or a run button.
- [ ] Prevent campaign AI from immediately replacing remote movement commands.
- [ ] Add a hotkey to enable/disable remote-control mode.
- [ ] Preserve scripted AI control when remote-control mode is disabled.

Acceptance criteria:

- Nadine can walk, run, turn, and stop continuously for at least five minutes.
- Movement follows terrain and does not use transform writes.
- Enabling and disabling control does not crash or require a checkpoint reload.

## Phase 2: Local Camera and Basic Actions

Objective: establish a complete second-player control loop on one machine.

- [ ] Find Nadine aim/look orientation controls.
- [ ] Find camera creation, ownership, and viewport functions.
- [ ] Prototype an independent Nadine-follow camera.
- [ ] Add crouch, jump/traversal, interact, cover, dodge, and melee.
- [ ] Find weapon equip, aim, fire, reload, and throwable paths.
- [ ] Define which actions remain AI-assisted when no direct player primitive
      exists.

Acceptance criteria:

- A second controller can navigate a combat arena and participate in combat.
- Nadine's camera and actions do not redirect Chloe's camera or input.

## Phase 3: Network Transport

Objective: send player-2 input from another PC to the host.

- [ ] Define a versioned input packet: sequence, timestamp, movement, look,
      buttons, and acknowledgements.
- [ ] Implement a small UDP transport with timeout and reconnect handling.
- [ ] Feed received input into the same local-control interface from Phase 1.
- [ ] Send host snapshots: transform, velocity, animation/action state, health,
      weapon state, and checkpoint generation.
- [ ] Add interpolation, prediction, and correction thresholds.
- [ ] Authenticate the session with a short host-generated token.

Acceptance criteria:

- Stable LAN control for at least 30 minutes.
- Lost or reordered packets do not leave actions stuck.
- Host remains authoritative over Nadine and campaign state.

## Phase 4: Second-PC Rendering

Objective: give player 2 a separate game window and camera.

Preferred investigation order:

1. Run a second TLL instance as a presentation client and mirror host state.
2. Reuse retained Uncharted 4 multiplayer replication where practical.
3. Use host-side split rendering only as a diagnostic fallback.

Tasks:

- [ ] Allow or isolate a second game process and its save/config files.
- [ ] Load the matching level/checkpoint on the client.
- [ ] Suppress client-side campaign authority and conflicting scripts.
- [ ] Spawn or select a client-visible Nadine representation.
- [ ] Drive the client camera independently.
- [ ] Replicate enemies, traversal state, combat effects, and cutscene state.

Acceptance criteria:

- Both PCs show the same encounter with acceptable visual consistency.
- Player 2 sees Nadine from an independent camera.

## Phase 5: Campaign Compatibility

- [ ] Checkpoint load, death, revive, and respawn.
- [ ] Vehicles and parented character transforms.
- [ ] Boosts, doors, dual interactions, and contextual traversal.
- [ ] Stealth and combat encounters.
- [ ] Cutscenes, forced cameras, QTEs, and character swaps.
- [ ] Chapter transitions and save/load.
- [ ] Per-scene fallback rules when Nadine is absent or script-locked.

## Immediate Next Build

Replace the one-shot `+3 m` locomotion test with a toggleable WASD controller:

1. Read keyboard input in the injected DLL.
2. Every 100-150 ms, create a short goal 1.5-2.5 m from Nadine.
3. Rotate that direction relative to Chloe's camera once the camera basis is
   identified; use world axes for the first diagnostic build.
4. Stop issuing goals when no movement key is held.
5. Log command rate and current target for crash diagnosis.

The first diagnostic controls movement only. Camera, combat, and networking
are deliberately deferred until continuous locomotion is stable.

