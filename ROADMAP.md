# Shared-world co-op roadmap

Updated 2026-10-25. Implementation direction, NOT a list of working features.
Addresses/findings: `COOP_STATUS.md`. Immediate work: `TASK_POOL.md`.

## P0: one shared campaign world

The user rejects the current enemy-position/death prototype as full-world
sync. Preserve confirmed first-load Player visibility/movement, native menu
lifetime safeguards and stock-Nadine removal. Appearance remains parked.

Architecture decision: **A owns world simulation; B sends player actions and
presents authoritative results.** Camera/input/local presentation stay local;
movement prediction does not authorize a second enemy/damage/script simulation.

| Shared state | Authority | Replica responsibility |
| --- | --- | --- |
| NPC spawn generations and retirement | A | Bind/create/retire corresponding native lives |
| NPC decisions, targets, movement actions and fire | A | Present authoritative actions; no independent substitute decision |
| Shots from both Players and world damage | A | Predict permitted visuals without independently committing the same hit |
| NPC health, armor, knockdown and death | A | Apply typed confirmed consequences exactly once |
| Player health, death and revive | A, separate native adapter | Never reuse NPC vtable offsets/health operations |
| Shared pickups, ammo grants and interactions | A | Submit actor-qualified intents; reconcile confirmed inventory/object state |
| Breakables, doors, physical changes and campaign tasks | A | Apply keyed revisions in the correct local native phase |

## Implementation order

1. **World/lifecycle baseline:** paired epochs, authority identity, complete
   initial manifest and snapshot baseline. Logical IDs include world/entity kind/
   spawn generation; local weak handles bind them, never cross the wire. Support
   new waves and dynamic native owners, not only the first32 lives. Missing rows
   and HP0 are not inferred deaths. Delayed loading needs a current baseline,
   not stale root replay or a larger teleport threshold.
2. **Reliable journal:** world/life/event/revision/causal-action IDs; ACK only
   after native commit. Explicit snapshot-to-journal cutover. Root samples may
   drop; critical gameplay events must not disappear with a deferred root census.
   Safety refusal must be explicit, not apparent full-world readiness.
3. **Campaign replica control:** replace B decisions with A action inputs before
   suppressing autonomous skills/fire. Keep helper/animation/reaction/physics
   work. Account for pending controller actions and authored ScriptSkill/cutscenes.
   Do not freeze the last skill or repeat RequestDisableAi.
4. **One combat resolver:** A resolves both players' actual shots once. B hit
   reports, if used, are correlated proposals, not additional scalar damage.
   Shot IDs need projectile/pellet/child-hit ordinals and propagation BEFORE
   synchronous Init/worker hits. Implement B consequence suppression and typed
   result playback together, including armor/reaction/death, separate Player
   states and environmental damage. FirePacket delivery is not this contract.
5. **World objects and scripts:** typed breakable/pickup/door/interaction/task
   adapters. Resolve IDs to local objects; do not replay native Event/AttackInfo/
   task records as bytes. Distinguish task completion from reset/restart; define
   clock semantics and one-owner script/trigger policy. Both players' interactions
   must produce one shared consequence.
6. **Recovery:** snapshot/journal catch-up across streaming, late load, menus and
   world changes. No stale pointers, reset stopped flags or resurrected write
   admissions. Resnapshot/rejoin must be designed native transitions.

## Evidence and immediate native gates

- `14027A270 PublishAttackEventToTarget` is campaign-reachable; regular bullets
  preserve a local shooter weak handle in fresh typed AttackInfo. Neither raw
  AttackInfo nor its damage scalar is a canonical network result.
- Skill selection `1404201E7 -> 14041F1A0`, current Skill execution `+28` at
  `14041CBD4`, and ordered OnExit `+38` are ownership boundaries to investigate.
  The surrounding helper/lookaim/animation update must remain active.
- NPC virtual `+E48` returns actor `+15A0`. Campaign ctor zeros it; generator/
  `net-anim-layer` construction is multiplayer-only. NetSkill is authored29-only.
  No safe campaign replica toggle is established; custom campaign action inputs
  or a fully proved owned generator/layer package are required.
- Campaign task queue `141CB95B0 -> 141CB9820` is not a world serializer.
  Correct fields: `+4 graph ID`, `+8 subnode ID`; `+24` is clock-related VALUE.
  Native local resolution/reset/timing semantics must be preserved.
- `1417F8B00 EventQueue::Post` contains local typed values/process handles.
  `SpawnerReplicationManager` duplicates authored spline/template spawners;
  neither is a ready network replication bus.

## Rejected shortcuts / acceptance

Two loads starting together do not establish deterministic AI/RNG/physics/scripts.
Extending the30s experiment, raising cost limits, clearing terminal stops, enabling
Online flags or copying memory cannot supply missing ownership. Full-world mode
must remain unclaimed until the native adapters and lifecycle contract exist.

Do not launch another root-only exam as the requested result. Each integrated
feature still requires the AGENTS.md evidence quota, native phase/lifetime proof
and falsifiable prediction. Acceptance requires sustained combat from BOTH
players, simultaneous hits, armor/knockdown/death, new waves, shared interactions
and task progression, plus delayed load and menu/world transitions without severe
lag/crashes/desync. Builds, hooks, packet counters and an idle pair are insufficient.
