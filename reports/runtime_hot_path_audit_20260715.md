# 2026-07-15 STATIC: production hot-path cleanup (build cp)

## Exact hypothesis

The severe two-instance slowdown is amplified by production-unneeded runtime
diagnostics, independently of the already confirmed system paging pressure.
Specifically:

- pressing Numpad7 leaves `g_logging_enabled=true`, so ordinary diagnostics keep
  writing for the rest of the process lifetime;
- `hooked_input_scale_copy` formats JOYPAD3 telemetry after every destination-3
  conversion before its output throttle;
- the Player input vtable hook performs diagnostic `GetCommand(0x183ABF0)` and
  state reads for both retained Players every 500 ms;
- the NdAttachable post-joint callsite walks DrawControl meshes and render
  instances to diagnose cloth on a render hot path;
- the stable build still arms the completed spawn-capture hardware breakpoint on
  every game thread.

Observable prediction: a production build that removes those paths will retain
co-op state/pickup/weapon/cloth behavior, but a fresh two-instance session will
produce boot/install lines only. Numpad7 must not unlock continuous logging.

## Competing hypotheses

1. The slowdown is entirely system paging from two 10-13 GB game processes on a
   16 GB machine. This remains a real lower bound and cannot be fixed in the mod.
2. Functional packet capture, weapon reconciliation, or transform correction is
   the main mod-side cost. These paths must remain for the co-op behavior and are
   not removed by this pass.
3. A one-sided allocation leak in the DLL grows over time. The 2026-07-13 sample
   rejected this: both game processes were symmetric and log growth was then only
   about 0.03 MiB/min before later diagnostics were left enabled.

## Independent evidence

1. **Runtime counts:** the latest co session (PIDs 10800/2480) contains 242
   `PLAYER-INPUT-UPDATE`, 121 `PUPPET-POS`, 117 `REMOTE-DRIVE`, 90
   `RAW-IDS`, 86 each `JOYPAD-SLOT`/`BLOCK1`, 86 `CHAR-CLOTH`, and 57
   `WEAPON-CLONE` lines. The 500 ms gates imply roughly two minutes of sustained
   telemetry. The shared log reached 17,149,302 bytes.
2. **Source dataflow:** Numpad7 in `net_tick` assigned
   `g_logging_enabled=true` without restoring it. Normal `log_line` callers in
   Player/input/weapon paths therefore opened the persistent writer for the rest
   of the run.
3. **Hot callsites:** `hooked_input_scale_copy` is the native per-Joypad
   conversion boundary and built its 512-byte diagnostic before calling the
   throttle. `hooked_nd_attachable_sync_parent_visibility_callsite` is installed
   at the post-joint render call and its diagnostic inspected up to 32 meshes and
   16 child instances per mesh.
4. **Install-state evidence:** `kEnableSpawnCaptureDiagnostic=true` added a VEH,
   suspended every game thread, and assigned DR0 despite the spawn experiment
   already being complete. GunMoveShield entry and wrapper hooks were pass-through
   probes but remained installed in the aim relay group.

The quota is met by runtime frequency, source dataflow/callsite ownership, and
independent install/configuration state.

## Build cp implementation

- Boot logging now writes directly; it no longer toggles the global runtime switch.
- `force_log_line` is compile-time disabled in production.
- Numpad7/Numpad4 use one-shot boot logging and cannot leave logging enabled.
- Removed hot JOYPAD3, Player input, pre-consumer movement, puppet position,
  raw-weapon-id, held-weapon, and clone-weapon diagnostics.
- Removed the sender's extra live-player/controller scans and fire-gate weapon
  inspection; the functional 100 ms weapon snapshot and fire suppression remain.
- Legacy Joypad snapshots/F9 comparisons are compile-time parked.
- Cloth lifecycle keeps only the vtable/parent identity check and three atomics
  needed by `maybe_queue_remote_cloth_rebuild`.
- Spawn hardware-breakpoint capture is disabled.
- Pure GunMoveShield entry/wrapper probes are not installed.

Functional ownership retained: packet send/receive, InputScaleCopy mutation,
pickup animation scheduling, button-edge retirement, sustained ADS, weapon
give/switch reconciliation, transform convergence, cloth rebuild, and the
eligibility/additive/blend/world-basis aim relays.

Release build succeeds. Runtime status: **HYPOTHESIS ready for test**, not yet
confirmed.

## 2026-07-15 runtime follow-up: exception observer and network discriminator

The cq two-instance run opened complementary UDP receive ports as designed:
PID 24420 owned 31338 and PID 10204 owned 31339. This proves role selection and
socket binding, but it does not prove that either process has sent or accepted a
valid `StatePacket`.

After the location loaded, the always-installed `crash_observer` captured eight
first-chance access violations per process. For every captured exception it read
12 stack words, formatted a 1536-byte record, opened the shared log, wrote it,
and closed it. This is bounded startup/location-transition overhead, not a valid
explanation for sustained low FPS. The two roughly 13 GB game processes and
their paging pressure remain the strongest competitor for sustained slowdown.

Build cr makes two production changes:

- compile-time parks `crash_observer`;
- records only one-shot state-network milestones: selected role/ports, first
  unavailable local Player or invalid position, first send result, and first
  accepted receive.

Falsifiable prediction for a fresh two-instance run: after both local Players
exist, both logs must contain a successful first send and a first receive. If
send appears on both but receive does not, the failure is transport/protocol. If
both receive milestones appear, UDP connection is working and the visual failure
belongs downstream in peer snapshot or puppet application. No build-cr
`EXCEPTION` records should be emitted by this DLL.

### Runtime result: CONFIRMED

Fresh build-cr processes PID 15996 (role A, receive 31338) and PID 14588
(role B, receive 31339) initially reported an invalid local position while the
location was becoming playable. They then both sent exactly 456 bytes at
sequence 1 and both accepted the peer sequence 1 at approximately
`(-762.71, 4.07, 382.592)`. Thus socket roles, protocol size/version,
position validation, receive dispatch, and peer snapshot publication all passed
the prediction. No build-cr exception-observer record was produced.

Any remaining visual non-connection is downstream of
`publish_peer_state_snapshot`, in snapshot freshness, retained puppet creation,
or puppet apply. UDP role selection must not be changed for that symptom.

## 2026-07-15 STATIC correction: spawn capture is functional, not diagnostic

The cp cleanup incorrectly disabled `kEnableSpawnCaptureDiagnostic`. Static
dataflow now proves this capture is a production prerequisite:

1. `spawn_capture_veh` is the only writer of `g_spawn_captured`, the copied
   `g_spawn_desc`, and its context arguments.
2. The live-peer auto-spawn condition requires `g_spawn_captured` before calling
   `reissue_spawn_for_slot1`; that function consumes the captured descriptor and
   context to invoke the native descriptor spawner for index 1.
3. Runtime independently proved bidirectional peer snapshots while the user saw
   no remote Player, exactly distinguishing transport success from a blocked
   slot-1 creation gate. The pre-cp co build had this capture enabled.

Competing causes are the cq melee callsite relay and a later puppet-apply failure.
Build cs parks cq but cannot create a puppet while the descriptor gate remains
false. Build ct therefore restores the capture as functional ownership while
keeping its expensive descriptor/hash scan compile-time disabled.

Falsifiable prediction: on two fresh ct processes, boot must report descriptor
capture armed; after local Player creation and the first peer packet, each side
must report descriptor reissue and remote synchronization enabled, and the user
must see the peer. If reissue is logged but no retained Player appears, the
descriptor lifetime/spawn result becomes the next candidate. Melee remains parked
throughout this test.
