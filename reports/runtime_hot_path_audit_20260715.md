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
