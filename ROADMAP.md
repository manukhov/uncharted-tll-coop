# Shared-world co-op roadmap

Updated 2026-10-26. Implementation direction, NOT a list of working features.
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
2. **Reliable journal:** world/life/event/revision/causal-action IDs; commit ACK
   only after native commit, separate from storage receipts/refusals. Explicit
   snapshot-to-journal cutover. Root samples may
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

## Implemented foundation / current limits (2026-10-26)

SOURCE ONLY: ENC2v2 keeps frozen numeric initial-life event delivery/reporting
separate from stopped native root admission. Storage receipt, native HumanDead
result and typed refusal are distinct. Dedicated socket-closed ACK is separate
from native-work quiescence. This does NOT authorize writes after stop or make
B enemies die. Object compile/packet shapes/isolated event API checks passed;
no new DLL linked, game launched or visible behavior validated.

Still <=32 initial lives/once-world/one death per life; no dynamic baseline,
journal, resnapshot, action stream or one-authority combat yet. Both ENC2 peers
must use v2. READY/binding/receipt counters are not full-world readiness.

Baseline design: canonical owned VALUE model at atomic journal cut S; baseline
ACK precedes deltas>S. A multi-frame live census is not an atomic snapshot.
Native publication supplies dynamic generations; B needs owner-aware create or
typed MissingLife refusal. Bounded overflow requires an explicit resnapshot
transition, never clearing absorbing native stops or merely enlarging32 rows.

## Evidence and immediate native gates

- `14027A270 PublishAttackEventToTarget` is campaign-reachable; regular bullets
  preserve a local shooter weak handle in fresh typed AttackInfo. Neither raw
  AttackInfo nor its damage scalar is a canonical network result.
- Skill selection `1404201E7 -> 14041F1A0`, current Skill execution `+28` at
  `14041CBD4`, and ordered OnExit `+38` are ownership boundaries to investigate.
  The surrounding helper/lookaim/animation update must remain active.
- Selector eligibility=false performs ordered EXIT, including ScriptSkill
  cleanup, not a safe think skip. CombatSkill OnExit `14066EB00` does not stop
  independent shooting FSM `1405ECDA0`, which runs before the selector. Nested
  BehaviorOpen child cleanup and separate fire callback ownership are unresolved.
- Plain MoveTo `141DE4D20 ->141E15030` owns value fields at accepted copy
  `141E1564E`; early PointWs conversion is not acceptance. Restricted params
  profile/PointWs aim descriptor are resolved, not yet integrated replica input.
  Native nav-query phase is after actual acquire `141DE8911` on the same fiber,
  not D20 entry or later NavSMUpdate. Full contracts are in COOP_STATUS.md.
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
