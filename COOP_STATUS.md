# TLL Campaign Co-op — Status & Findings (2026-06-28)

## 2026-07-15 production hot-path cleanup — build **cp**

**STATIC / runtime pending:** Composer's melee work ended at the confirmed stable
`co` baseline with both melee flags false. Before reopening melee, build `cp`
removes production-unneeded diagnostics that were still loading both instances:

- Numpad7 no longer leaves `g_logging_enabled=true`;
- runtime force logging, hot input/joypad/weapon snapshots, and the completed
  spawn hardware breakpoint are disabled;
- sender live-controller comparison and fire-gate state dumps are removed while
  their functional weapon capture/fire suppression remain;
- the NdAttachable cloth hook keeps only the cheap ProcessCharCloth identity
  signal required by the working native `ChangeLookId` repair;
- pass-through GunMoveShield entry/wrapper probes are no longer installed.

Release build succeeds. Functional pickup, weapon, transform, cloth, and aim relay
owners remain installed. Runtime prediction and evidence:
`reports/runtime_hot_path_audit_20260715.md`. Do not call this CONFIRMED until a
fresh two-instance run shows boot-only log growth and unchanged co-op behavior.

## Stable baseline (2026-07-15) — build **co** `2026-07-14co`

**CONFIRMED (user):** puppet weapon give/retrieve works again on dual-instance
coop. **FIXED:** do not install `PlayerInputCommandState IsCommandActive`
melee force-hook (`install_player_input_is_command_active_force`); that 16-byte
patch on the native `0x183ABF0` path broke `GiveWeapon` / `HELD-WEAPON-APPLY`
(`gave=0` while `gunOk=1 giveAllowed=1`).

**Active flags (do not re-enable without new static quota + runtime plan):**
- `g_enable_puppet_melee_button_relay = false` — no MELEE-SEND/RECV, no melee
  puppet force in joypad hook
- `g_enable_puppet_melee_exec_relay = false` — no queue push / TryAnim / publish
- Joypad3 force-hook: jump / evade / crouch / cover only (melee branches gated off)
- Log throttle: global 2 s on `JOYPAD3-ANALOG` / `PRE-CONSUMER-MOVE`

**Melee anim sync:** PARKED until a design avoids patching
`PlayerInputCommandState IsCommandActive` globally. Prior cg/cj/cm paths REJECTED
(crash, hang, weapon regression). Static queue path (`0x141273FA0`,
`controller+0x150`) retained in code but unreachable while flags false.

**Next agent:** stay on **co** for locomotion / weapon / cover / cloth work. Melee
is out of scope until explicitly re-opened.

## Turn note (2026-07-15, Cursor - melee removed co)

**REJECTED (cn, user):** melee still non-functional; weapon give broke on puppet.
`HELD-WEAPON-APPLY gave=0` with `gunOk=1 giveAllowed=1`. Suspect
`PlayerInputCommandState IsCommandActive` melee force-hook (16-byte patch on
`0x183ABF0` path) interfering with native weapon/command queries.

**co:** `g_enable_puppet_melee_button_relay=false` — no MELEE-SEND/RECV, no
puppet melee force, **do not install** PlayerInputCommandState force-hook;
joypad hook keeps jump/evade/crouch/cover only. Exec path stays parked. Boot
`2026-07-14co`. **CONFIRMED (user):** weapon give restored.

## Turn note (2026-07-15, Cursor - melee cm crash, cn stable)

**REJECTED (cm runtime, PIDs 16612/22284):** user punched → copy 2 (22284) exited
silently (no crash dialog). Log: `MELEE-SEND` → `MELEE-RECV cmdActive=0 force=0x0`
→ `MELEE-EXEC deferred` → `MELEE-EXEC drain` then **zero lines from 22284** (no
`MELEE-EXEC hash=`). Death inside `seh_puppet_melee_exec_path` queue push.
`locomotion_hook.log` was **16 MB** with **34k** `JOYPAD3-ANALOG` + **34k**
`PRE-CONSUMER-MOVE` (thread_local 500ms throttle ineffective across threads).

**cn:** park `g_enable_puppet_melee_exec_relay` (ci-stable button layer); fix push
for next increment — native payload from `PlayerInputCommandState+0x7C/+0x90`,
`PlayerMeleeController` vtable check (`0x2A6C2E8`), pre/post step logs; global
2s rate limit on joypad/move spam. Boot `2026-07-14cn`. Next: stable dual boot,
then re-enable exec with `MELEE-EXEC pre/post-push` bisect.

## Turn note (2026-07-15, Cursor - melee cm deferred queue-only)

**REJECTED (cl, PID 22608):** second instance died at boot — log stops after DLL
init + 8 `EXCEPTION` probes, never `MELEE-HASHES` (no punch, exec not reached).
Intermittent dual-instance failure; same class as ch instance-2 boot death.

**cm:** MELEE-RECV only sets `pending_puppet_melee_hash`; drain next remote
`PLAYER-INPUT-UPDATE` tick. Queue push only; try_anim + publish parked.

## Turn note (2026-07-15, Cursor - melee ck hang, cl queue-only)

**REJECTED (ck runtime, PIDs 11188/23352):** user punched → both instances froze;
first copy (11188) closed silently. Log: `MELEE-RECV cmdActive=1` on 11188 then
**zero further lines** from that PID (no `MELEE-EXEC`). 23352 kept logging.
Suspect: synchronous `0x141272A50` full dispatcher from input hook (heavy
Joypad_IsCommandActive loop / hook recursion).

**cl:** park `g_enable_puppet_melee_full_dispatch`; exec uses direct
`PushOrRotateMeleeCommandQueueEntry` only + step log + `MELEE-EXEC begin` marker.

## Turn note (2026-07-15, Cursor - melee ck queue dispatch fix)

**REJECTED (cj, user):** `MELEE-EXEC hash=0xd338e42f ok=0` every punch — button
relay works, no puppet anim. Root cause: exec called vtable `+0x210` (`0x141279F10`)
as validate-with-128-byte-buffer; static proof shows `+0x210` is string→hash resolver
that forwards to validate at `+0x1F8` with `int*`, not `init_command` blob.

**ck:** replace wrong validate with native queue path:
`DispatchPlayerMeleeButtonHoldReleaseAndGrappleReactionCommands` (`0x141272A50`)
on `PlayerMeleeController+0x150` queue base, fallback
`PushOrRotateMeleeCommandQueueEntry` (`0x141273FA0`) with relayed hash
(`melee-lmouse-press` not hardcoded square). Then inline TryAnim bypass; skip
`validate_move` when queue push succeeded (air punch zero stick). Step log:
`MELEE-EXEC … ctrl/dispatch/queue/anim/publish=`. Boot `2026-07-14ck`.

**Runtime pending:** two-instance air punch → `dispatch=1` or `queue=1`, `anim=1`,
visible peer puppet melee. If `dispatch=0 queue=1 anim=0` → branch_check gate on
`Player+0x1DE0`; if `anim=1` no visual → FgAnim publish path.

## Turn note (2026-07-15, Cursor - melee ch crash, ci stable path)

**REJECTED (build 2026-07-14ch):** two-instance test PIDs 19116/3452 — second copy
crashed during boot after DLL init (`get-blended-camera aim gate installed`); log
stopped at 8 `EXCEPTION` probes, never `MELEE-HASHES` or net. First instance kept
playing. Suspects: `install_player_melee_try_anim_bypass` on `0x1410FC9F0` and
`apply_peer_melee_animation` direct vtable `+0xC78/+0xC80`.

**ci fix:** `g_enable_puppet_melee_exec_relay=false` — TryAnim bypass parked; keep
**cg** button relay (`PlayerInputCommandState` @ `+0x39A0`, Joypad3 force,
`MELEE-SEND`/`MELEE-RECV`). Boot string `2026-07-14ci`. Next: stable two-instance
boot, then incremental anim (net-gate bypass only, edge-triggered, no vtable spam).

## Turn note (2026-07-13, Codex - native deferred cloth repair)

Runtime on PIDs 7480/13620 confirmed the reload failure: each final remote Player
kept a complete active parent DrawControl, but no `ProcessCharCloth` child matched
the final parent generation. Intermediate parent generations did have healthy
cloth. This selects the absent-child hypothesis and rejects stale cloth state or
hidden cloth meshes for this reproduction.

Static analysis found the stock non-reentrant repair boundary. The six-player
PreFrame loop at `0x14015CE1C..0x14015CE7D` consumes `Player+0x3D10`, calls
`Player_ChangeLookId 0x1410FE840`, then clears the field. `ChangeLookId` performs
the full DrawControl/composite-body rebuild and drives `NdPropControl::Init` into
the native prop/process spawn path. The runtime hook now queues the current
`Player+0x170` look only after the same remote Player previously had cloth, its
new parent is stable for 1.5 seconds, and matching cloth has been absent for 0.75
seconds. Prediction and competitors are recorded in
`reports/process_char_cloth_reload_20260713.md`.

First repair runtime was asymmetric: deferred `ChangeLookId` recreated healthy
cloth for Nadine, proving the stock repair boundary, but Chloe's retained Player
pointer changed and the old gate discarded healthy evidence before the final
missing-cloth test. The gate now preserves slot-level evidence across Player
replacement and separately rejects a timeout on the same healthy Player/parent.

The same run exposed respawn transform desync. `StatePacket` already carries bit
4 as teleport, and receive stored it in `pending_authoritative_teleport`, but no
consumer existed. In free-run mode a stationary displacement below four metres
also receives no correction. The fix now detects Player replacement and reload
gaps on send, consumes the one-shot marker post-update, and forces one snap.
Static evidence, competitors, and prediction are in
`reports/respawn_transform_sync_20260714.md`.

Second runtime refined both results. Both final Players queued deferred
`ChangeLookId`, and Chloe recreated all three expected cloth mesh processes, but
the user still saw her bald: full look rebuild is not sufficient. `CHAR-CLOTH`
now records the resolved parent object/current-puppet match plus omitted
main-view/category instance suppression before any further write.

Respawn send/snap events also fired, but `PUPPET-POS` proved native locomotion
then grew stationary error back to four metres and repeated the cycle. Pointer
replacement was noisy false evidence during reload, so the sender now keys only
spatial discontinuity; the bounded free-run correction now also converges a
stationary peer after the snap. Revised prediction is in the same report.

## Turn note (2026-07-13, Codex - reload hair is ProcessCharCloth)

Fresh reload snapshots on PIDs 20348/21444 retained the complete parent
DrawControls: Nadine remained 5/5 meshes and Chloe 2/2, with all gates active.
Both hat ids and the 16 tracked hat process/mesh slots were zero. **REJECTED:**
missing base-look meshes and `PlayerBase_RebuildHatAttachmentsFromIds` as the
direct cause of this baldness.

Static analysis identifies the independent lifecycle:
`RegisterProcessCharClothFactory 0x1401186C0` registers factory `0x141F41470`;
constructor `0x141F3DEE0` installs vtable `0x142BAF780`; exact-source
`ProcessCharCloth_Init 0x141F3E060` resolves parent handle `+0x4E0/+0x4E8`,
builds cloth state, and marks `+0x870`; `ProcessCharCloth_UpdateRoot
0x141F3E6E0` consumes that parent. The read-only `0x141ED63F7` relay now passes
the preserved RBX attachable and logs exact-vtable `CHAR-CLOTH` state. Runtime
prediction and competitors are in `reports/process_char_cloth_reload_20260713.md`.
Initial runtime on PID 14696 found three healthy remote-Chloe cloth children,
each initialized with a live parent/body, one mesh, and 4/4 active instances.
The post-reload comparison remains pending.

## Turn note (2026-07-13, Codex - parent hat lifecycle discriminator)

PIDs 18080/19668 reproduced bald remote players after reload. Valid look ids
survived, while every child DrawControl observed at `0x141ED63F7` retained both
mesh gates and fully active instances. **REJECTED:** child DrawControl
visibility/activity as the cause.

Static analysis now pins the actual parent lifecycle. Exact-source
`PlayerBase_SpawnHatWithIdFromMap 0x140CC4BF0` writes process handles to
Player `+0x1758` and added parent DrawControl mesh ids to `+0x1858`.
`PlayerBase_RebuildHatAttachmentsFromIds 0x140CC5890` kills/removes those same
entries from Player `+0x198`, then respawns `+0x15AC/+0x15B0`; Player init,
`ChangeLookId`, and direct hat-id setters independently call it.

The read-only relay now emits throttled `PLAYER-DRAW` state changes for the
parent DrawControl plus both tracking tables. Prediction and competing
hypotheses: `reports/player_hat_lifecycle_reload_20260713.md`. Next runtime:
capture one line before and after a single location reload. Do not call the
rebuild from the async attachment hook; if the tables drop, schedule the stock
rebuild from the Player update/main-thread path.

## Turn note (2026-07-13, Codex - bald reload parent gate rejected)

User runtime on PIDs 13996/4476 still showed both remote Chloes bald after a
location reload. The installed `0x141ED63F7` relay repeatedly observed
`parentFlags=0`, parent/child gates `255/255`, and never selected its repair.
**REJECTED:** `DrawControl+0x1D` parent inheritance as the reload hair cause.

The relay is now read-only and logs state changes as `ATTACH-DRAW`: Player visual
ids `+0x170/+0x190`, child mesh counts, mesh gates `+0x243/+0x245`, render-instance
counts, final active byte `+0x1A0`, main-view `0x40`, and category suppression
`+0x1A4 bit 3`. Prediction and static evidence are recorded in
`reports/nd_attachable_parent_visibility_relay_20260713.md`. Next runtime must
compare the initial snapshot with the first snapshot after reload; do not add
another visibility write until that discriminator selects a writer.

## Turn note (2026-07-13, Cursor - reload kneeling + bald lifecycle)

**Runtime REJECTED:** POST camera-yaw snap plus
`Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320` emitted
`AIM-FACING-REFRESH ok`, but after a location reload the puppet slid forward/back
on its knees. This reproduces the earlier root-refresh locomotion failure. Removed;
ADS root facing is soft-blend only again.

**Bald remote after location reload:** the main-view relay identified slot 1 by
`retained_player_object(1)`. During reconstruction, `PlayerMainViewVisibilityUpdate
0x140C990A0` can run on the new Player before the retained table updates, so the
false `+0x1618` request escaped and permanently applied `DisableMainView` to hair
and attachments. Fix under test:

1. identify remote Player by authored `Player+0x4010 == 1`;
2. after slot-1 update, if `+0x1618/+0x1619` are not both visible, submit one Show
   transition through the original function;
3. expect `PUPPET-MAIN-VIEW restored after lifecycle` after reload.

**Runtime rejection (PIDs 4620/5512):** loading a checkpoint still made both
remote Chloes bald and produced no `PUPPET-MAIN-VIEW` line. Thus the checkpoint
does not leave `+0x1618/+0x1619` false; it creates hidden attachments after the
Player has already reached visible/visible, so no natural edge is emitted.

**Replacement under test:** watch `Player+0x1B10` attachment collection identity
and count. After it stabilizes for 750 ms, replay an explicit
`+0x1619=0, +0x1618=1` transition through the stock function up to three times.
Static disassembly proves that true transition calls weapon-controller slot
`+0x378` and iterates `+0x1B10` publishing Show events. Expected log:
`PUPPET-MAIN-VIEW lifecycle replay attempt=N attachments=M`.

**Runtime REJECTED (PIDs 17832/20784):** all three stock Show replays ran on
both copies (`attachments=1/5`), but the remote hair remained invisible. The
authored `DisableMainView/EnableMainView` event path is not the final hair gate.

**Next static path / build under test:** `NdAttachableObject_PostJointUpdate_Async
0x141ED28E0` resolves parent `+0x4E0/+0x4E8` and at `0x141ED63F7` calls
`DrawControl_SyncParentVisibility 0x141754DA0(child,parent)`. That helper copies
`parent DrawControl+0x1C & +0x1D` into child `+0x1D` and recomputes each child
instance active byte `+0x1A0`. The new verified callsite relay logs
`ATTACH-VIS`; only when slot-1 parent has own gate nonzero, inherited gate zero,
main-view bit clear, and child own gate nonzero does it use the native null-parent
case (`child+0x1D=0xFF`). See
`reports/nd_attachable_parent_visibility_relay_20260713.md`.

## Turn note (2026-07-13, Cursor - runtime aim log + facing refresh)

**Built + injected** (latest): snap camera yaw POST + `0x141BB8320` refresh.

**Prior session runtime (PIDs 12852 host / 17036 recv):**
- Pipeline CONFIRMED live: `AIM-BLEND blend=1`, changing `AIM-IK`/`AIM-WORLD-BASIS`
  `fwd`, pose-seed override, eligibility/permission forced.
- Still no visible sync: `+0x140` body tracked camera in logs, but mesh stayed
  pinned — derived transform not refreshed (matches earlier diagnosis).
- Weight probe had wrong stride (`U=1e21`); fixed to contiguous `+0x8B4..+0x8C0`.

**New build under test:** POST snap (`apply_peer_aim_facing(..., true)`) +
`seh_refresh_player_derived_world_transform`; PRE keeps soft blend only.
Log marker: `AIM-FACING-REFRESH ok`.

## Turn note (2026-07-13, Cursor - all-weapon aim sync)

**Symptom (user):** not scoped-only — any weapon ADS: puppet initially aims the
wrong way, then stays pinned to one world point while the host moves the reticle.

**Root cause:** camera direction rides in `pose_seed`, but the puppet root was
blended to `StatePacket::quat` (body). Aim IK/target fields were written only
POST native update, so GunMoveShield consumed stale data the same frame.

**Fix under test (`teleport_hook.cpp`):**
1. `apply_puppet_aim_pre` before native Player update — camera facing, pose quat,
   IK points, `+0x1570/+0x21F0` targets, ADS flag, blend state.
2. POST pass reinforces the same (no body-quat blend while pose relay active).
3. Blend tick + world-basis sustain `+0x8A0/+0x8EC` (prior commit).

**Prediction:** pistol and rifle ADS track peer reticle yaw/pitch live; initial
bearing matches host camera within one soft-blend frame.

## Turn note (2026-07-13, Cursor - scoped free-look rotation)

**Pickup:** CONFIRMED — do not touch.

**Symptom:** receiver puppet stays aimed at one world point while host free-looks
with scoped sniper ADS. Main-view relay CONFIRMED (hair/rifle visible).

**Runtime evidence:** `AIM-WORLD-BASIS` fires with changing `fwd`/`yawPre`, but
`AIM-WEIGHTS-POST` often shows `blend=0` (published aim-add weights zeroed).

**Fix under test (uncommitted `teleport_hook.cpp`):**
1. `force_peer_puppet_aim_blend_state` — sustain `controller+0x8A0=1` and
   `+0x8EC=1` on blend tick, world-basis hook, and post-update for slot-1 puppet.
2. Blend-target relay always returns `max(original, 1.0)` during peer ADS (not
   only when native target is zero).
3. Scoped rifle: soft camera-yaw root blend (`apply_peer_aim_facing`, 0.85) +
   `force_puppet_ads_flag` while pose relay active (no snap / no `0x141BB8320`).

**Prediction:** scope free-look rotates puppet upper body via aim-add and root
yaw via soft blend; pistol ADS unchanged. Log `AIM-BLEND-TARGET` and
`blend=1` in `AIM-WEIGHTS-POST`.

**Rebuild:** `scanner/build/tll_teleport_hook_v2.dll` — inject and retest.

## Turn note (2026-07-13, Cursor - resume aim after Codex handoff)

**Pickup:** CONFIRMED via `PlayerPickupController_StartOrQueue 0x140F49CA0`
(`d696139`) — leave alone.

**Where Codex stopped on aim:**
- Pistol aim animation was visible with the PlayerAimController relay chain;
  sniper ADS kept pose. Remaining defects: free-look yaw on the root, and
  scoped hair/rifle disappearing.
- Lag isolation (`14249a3`) set `g_enable_recent_aim_hooks=false`, so the proven
  aim animation path was off at handoff. Pose-seed snapshot lag was already
  gated in `e305fec`. Pre-root yaw REJECTED (`9a039a7`, kneeling slide).
  Main-view relay `0x140C990A0` still installed, runtime pending.

**Runtime (PIDs 9200/18928):** main-view CONFIRMED — sniper no longer disappears
(`PUPPET-MAIN-VIEW-HIDE suppressed`). Aim direction REJECTED as sufficient:
`AIM-WORLD-BASIS`/`POSE-SEED-SEND` tracked camera `fwd`, but the puppet kept
aiming at one world point. Soft-blend to `StatePacket::quat` owned the root;
post-write of camera yaw never refreshed `0x141BB8320` the same frame.

**Fix under test (PIDs 4516/19608):**
1. During peer ADS, always snap `Player+0x140` to camera horizontal `-Z` and call
   `Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320` post-update
   (not pre — kneeling still REJECTED).
2. AimController body-forward matches that same horizontal camera basis; target
   keeps full camera ray (pitch in additives, yaw from root).

**Prediction:** turning the scope turns the puppet root the same way; pitch still
tracks. Kneeling/slide during ADS rejects the post+refresh pairing and selects
joint-blend ordering instead.

## Turn note (2026-07-13, Codex - camera yaw and scoped renderables)

Runtime of `0d45c6e` confirms the authored aim target relay: pistol aim animation
is visible, and sniper ADS retains the aim pose. It also separates the remaining
defects. Root facing still follows sender `StatePacket::quat`, so free camera yaw
cannot turn it. The DrawControl `0x40` clear preserves the scoped body, but hair
and the held rifle still disappear despite attached weapon state.

Static correction uses three independent facts: route cameras aim down local
`-Z`; Player body forward is local `+Z` through
`Quaternion_RotatePositiveZ 0x14014D9F0`; and post-transform runs after native
Player update. The old camera helper used `+Z` and therefore had an exact
180-degree error. Default ADS now has one post-update writer derived from
`pose_seed+4 * -Z`, and slot `+0x58` receives the matching horizontal body basis.

Visibility now covers per-attachment ownership. Script handler `0x1403342C0`
resolves an arbitrary target object; its false branch calls the `0x40` writer at
`0x1403343D3` only for that target's `+0x198` DrawControl. A callsite relay now
preserves Player/weapon/hair attachables owned by the puppet, while the post pin
also restores independent mesh byte `+0x243` via
`IDrawControl_ShowMesh 0x141754A30`. Build succeeds; runtime prediction and
competitors: `reports/aim_camera_yaw_scoped_renderables_20260713.md`.

## Turn note (2026-07-13, Codex - aim additive state permission)

Runtime PIDs 14320/14884 reached `PlayerAimController` slot `+0x58`; relayed
forward vectors changed with peer yaw and pitch, but the user saw no aim animation
and the puppet root faced the wrong direction. This rejects stale target data and
selects downstream blend suppression.

Static data flow is now closed: `0x140CB32B0` sets request byte `+0x8A0` only when
`PlayerAimController_IsAimAdditiveAllowedForCurrentState 0x140CADF40` returns true;
common tick `0x140CAFA20` then raises blend `+0x8EC`; publisher `0x140CB41F0`
multiplies all four `aim-add-*` values by that blend. A narrow relay at callsite
`0x140CB348A` preserves the original result and forces only false slot-1 results
during fresh strict peer ADS.

The wrong root direction was a separate post-update writer that converted camera
`-Z` into body yaw. Default ADS now preserves the sender's network body quaternion;
camera yaw remains only behind the explicit A/B toggle. Runtime prediction and
competitors: `reports/player_aim_additive_permission_relay_20260713.md`.

Runtime PIDs 4412/17588 selected the remaining competitor: receiver logged both
`AIM-WORLD-BASIS` and `AIM-ADDITIVE-PERMISSION original=0 forced=1`; root facing
is now correct, but the user still saw no aim animation. Permission was a real
gate but not sufficient. A behavior-neutral vtable `+0x30` probe now records
`+0x8A0/+0x8EC` around the native blend tick; predictions are in
`reports/player_aim_blend_tick_probe_20260713.md`.

The follow-up PIDs 18500/17476 showed `request=0->0 blend=0->0` throughout scoped
rifle ADS, with no slot `+0x58` call. This is explained by the mod itself:
`puppet_aim_press_relay_enabled_for_peer` rejected all scoped rifles and
`SCOPED-ADS-SCRUB art=0x8075397d` cleared ADS every frame. The next build admits
scoped `0x200` only through the existing stationary movement/velocity gates and
applies the proven DrawControl main-view `0x40` clear to that interval. Details:
`reports/stationary_scoped_ads_relay_20260713.md`.

PIDs 5092/7028 admitted stationary ADS and selected the next exact blocker:
`request=1->0 blend=0.595298->0`. The only authored target source in the tick is
`0x140CAE480` at `0x140CAFB7C`, so the next relay preserves positive native values
and substitutes `1.0` only for zero slot-1 peer ADS. Wrong direction is addressed
separately by supplying GunMoveShield's stock positive-Z body basis from the sender
root quaternion. The disappearance occurred with sender pistol `0x33E29A1B`,
proving the scoped-art visibility gate was too narrow; DrawControl mask `0x40` is
now cleared for every automatic puppet ADS interval. Details:
`reports/player_aim_authored_target_body_visibility_20260713.md`.

## Turn note (2026-07-13, Codex - aim eligibility callsite relay)

Runtime PIDs 16212/1692 with `a2d8cf4`: sender emitted
`STRICT-AIM-SEND ON`, receiver repeatedly emitted caller-scoped
`AIM-POSE-OVERRIDE`, but slot `+0x58` emitted zero `AIM-WORLD-BASIS` lines. The
target hook was never reached; this confirms the upstream eligibility alternative.

**STATIC implementation:** patch only the `E8` call at `0x140E8573C` after
verifying its target is `Player_CheckAimActionEligibility 0x1410E3670`. The relay
calls the original first and changes false to true only for retained slot 1,
screen 1, fresh strict peer ADS, and valid pose seed. Stock GunMoveShield then
retains exact target/body vectors, weapon-specific limits, blend, and follow-up
updates. Prediction: `AIM-ELIGIBILITY ... forced=1` followed by
`AIM-WORLD-BASIS relay` and visible yaw/pitch. Details:
`reports/player_aim_eligibility_callsite_relay_20260713.md`.

## Turn note (2026-07-13, Codex - scoped ADS main-view flag)

**STATIC implementation:** registered command
`set-instance-flag-draw-in-main-view` uses handler `0x1403342C0`. False sets mask
`0x40` on every `Player+0x198` DrawControl child render instance through
`0x141754EF0`; true calls the exact inverse `0x141754FF0`. Independent
Disable/EnableMeshInMainView APIs use the same `instance+0x18 bit 0x40`.

The old speculative ShowMesh loop is replaced with one post-native-update call to
`DrawControl_ClearChildInstanceFlagsByMask 0x141754FF0(draw, 0x40)`, gated to the
puppet's full scoped ADS. Object compilation succeeds; final v2 link waits for the
currently running aim-test processes to release the DLL. Runtime prediction:
`SCOPED-MAIN-VIEW cleared instance flag 0x40` and Chloe stays visible. Details:
`reports/scoped_ads_main_view_flag_20260713.md`.

## Turn note (2026-07-13, Codex - PlayerAimController world-basis relay)

Implemented the previously build-ready narrow aim fix: vtable slot `+0x58` at
`0x142A0B250` (`PlayerAimController_ApplyWorldAimBasis 0x140CAF110`) is swapped
after exact pointer verification. For retained slot 1, screen 1, fresh peer ADS,
and a valid pose seed, only the target float4 is replaced with the peer camera ray.
Body-forward, mode bytes, and all four authored angular limits remain native.

Static quota is met by the GunMoveShield callsites, exact target-copy data flow,
the internal skeleton-origin resolver, and the downstream
`aim-add-left/right/down/up` publisher. Build succeeds; runtime pending. Prediction:
changing peer camera yaw/pitch changes `AIM-WORLD-BASIS relay ... fwd=...` and the
remote upper body follows. Details:
`reports/player_aim_world_basis_relay_20260713.md`.

## Turn note (2026-07-13, Codex - native PlayerPickupController relay)

**RUNTIME REJECTED:** factory-cache test with M-30 (PIDs 5084/21076) still emitted
`pickup_id=0`; no matching live WeaponPickup exists at scripted GiveWeapon time.

**RUNTIME CONFIRMED:** the actual bend producer is
`PlayerPickupController` at `Player+0x1EB8`. The `interact-with-me`/`Pickup` script
branch calls vtable `+0x38` (`PlayerPickupController_StartOrQueue 0x140F49CA0`) at
`0x140DD0A3C`. It queues a 32-byte POD arg3, selects `dra-pickup-*`, starts through
`0x140F4B150`/`signal-pickup`, post-blends at self-identified `0x140F4CD20`, and
publishes `player-pickup` on completion `0x140F4D0E0`.

PickupPacket v4 now relays that exact 32-byte arg. Receiver calls the original on
`puppet+0x1EB8` with object=null, supported by the native function. Runtime PIDs
1748/9036 produced four consecutive `PICKUP-CONTROLLER-SEND` packets (`seq=1..4`),
four `PICKUP-CONTROLLER-APPLY queued=1` results, and the user confirmed the remote
pickup bend is visible.
Details: `reports/player_pickup_controller_static_20260713.md`.

## Turn note (2026-07-13, Codex - WeaponPickup factory-cache sender candidate)

**STATIC candidate:** `WeaponPickup_Factory 0x141579150` is the registered class
factory (size `0x560`, vtable `0x142AA66C0`) and has a safe 21-byte pre-branch
prologue. The DLL now caches objects returned while the campaign loads. At the
confirmed scripted `GiveWeapon` boundary `0x140DD4208`, it selects the nearest live
matching art (`WeaponPickup+0x520`), then captures runtime id `+0xBC` and descriptor
ids `+0x38/+0xD8`. This replaces the runtime-rejected dispatcher/control source.

Prediction: sender logs `PICKUP-ID-CAPTURE ... source=factory-cache` with nonzero
ids, then receiver logs `PICKUP-CTXREPLAY published=1`. No cache match rejects the
assumption that scripted pickups have a live WeaponPickup; nonzero id with zero
class/name rejects the descriptor field assumption for this path. Details:
`reports/pickup_factory_cache_static_20260713.md`.

## Turn note (2026-07-12, Codex - pickup sender capture rejected; lag measured)

**RUNTIME REJECTED (sender capture):** Type-97 ground pickup reached the confirmed
scripted give path (`GiveWeapon ret_rva=0xDD4208`) and inventory synchronized, but
the sender emitted `pickup_id=0x0`. The receiver consequently logged
`PICKUP-ANIM-APPLY ... class=0 name=0 ctxReplay=1 published=0`. No
`PICKUP-REQ`/`PICKUP-ID-CAPTURE` occurred. The scripted `give-weapon` event carries
the WeaponInfo payload, not a live `WeaponPickup`; scanning the interact control and
six dispatcher nearby lists is therefore not a valid sender source for this path.
The context-replay hypothesis itself was not exercised because its required ids
never reached the receiver.

**Performance:** no asymmetric process leak was observed. Each game used about
`12.7 GB` private memory. Two copies on `15.9 GB` physical RAM left about `1.5 GB`
available and produced roughly `25,700 Memory Pages Input/sec`; the multi-second
stalls are system paging. Current mod log traffic measured only about `0.03 MiB/min`.
The completed pose-seed caller histogram is nevertheless disabled in stable builds
to remove hot-resolver bookkeeping and periodic diagnostic I/O.

## Turn note (2026-07-12, Codex - kneeling-slide locomotion regression)

**RUNTIME REJECTED:** commit `0459634` changed
`g_puppet_relay_aim_press` from the stable default `false` to test-default `true`.
With continuous puppet ADS (`Joypad` action `0x200`) active during movement, Nadine
slides forward/backward in a kneeling pose instead of running. This reproduces the
earlier documented locomotion conflict from global aim-press relay.

**Fix:** restore `g_puppet_relay_aim_press=false`. Keep F8 as a foreground-only A/B
toggle. The scoped mesh-visibility helpers and camera-yaw-to-body-yaw conversion
from `0459634` remain available, but no longer force full ADS by default.

## Turn note (2026-07-12, Codex - context-scoped pickup descriptor replay)

**STATIC candidate:** the receiver no longer needs to resolve a mirrored live
`WeaponPickup` to publish the Player-side pickup action. `Player_PublishRequestedInteract`
`0x140C87D70` reads only two dwords from its interactable argument (`+0x38/+0xD8`).
Relay those ids and provide a minimal surrogate object.

The target context is now exact: `ThreadContext_GetCurrentProcess 0x1416A2E80`
reads TLS `ContextProcess` at `+0x90/+0x98`; `ThreadContext_SetCurrentProcess
0x1416A2ED0` writes the tag and object. Stock publishers use a balanced
save/set/dispatch/restore scope. Independent producer
`Player_PublishZiplineRequestedInteract 0x1410BCDF0` emits the same
`player-requested-interact` descriptor without `Player_InteractDispatcher`,
rejecting the dispatcher as the only valid producer.

Build boundary when runtime/code changes are allowed: sender relays pickup
`+0x38/+0xD8`; receiver sets ContextProcess to the retained puppet, calls the
publisher with a zeroed 0xDC-byte surrogate, then restores context on every exit.
Inventory remains exclusively `WeaponGivePacket + SLOT-MIRROR`. Alternatives still
to distinguish: Player-side descriptor alone, paired `interact-with-me`, or a later
nearby/command-state dependency. No game or injection was run.

Details: `reports/pickup_context_replay_static_20260712.md`.

**Status correction:** the older `STATIC next path` below is complete and rejected
as the wall. Exp4 proved both slot0 and puppet have live FgAnimData, phase `0x38`,
matching retained owners, and reach real `Player_Update`.

## Turn note (2026-07-10, Codex - Exp 2/3 scheduler correction)

**REJECTED:** the latest Cursor/Claude direction treated `dword_143D3C0D0` as the
active Player scheduler list and `0x14042FCF0` as a Player update. Static RTTI and
call-graph checks disprove both links:

- `dword_143D3C0D0` is an `NpcManager` spot/path-search list; its tick reaches
  source-identified `Npc::UpdatePathSearch` `0x14043B830`.
- `0x14042FCF0` is `Npc/Buddy/Human` vtable `+0x410`; `0x1404901B0` is Alcazar's
  override. Real `Player` vtable `+0x410` is `Player_Update` `0x1410EC230`.
- `0x14042D120` is `Active@Npc` vtable `+0x20`, not a Player animation callback.
- `Player+0x1A0` is the upper Player state owner (`+0x700/+0x710` pipeline), not
  proven animation-instance/job state.

The factual runtime read remains useful: puppet is absent from the NPC spot list.
It does not justify registering the puppet there. The two crash-prone rejected
writes (direct `42FCF0(puppet)` and forced `+0x15A9 bit1`) are disabled in code.

**STATIC next path:** `AnimManager_AsyncObjectUpdateJointBlendJob` `0x141A92A00`
resolves a retained owner and, under job flag `0x20`, dispatches owner vtable
`+0x410/+0x418`. Trace creation/flag writers for that job and map it to slot0 vs
puppet before a read-only runtime comparison. This is the supported route toward
the missing pickup animation.

Details: `reports/real2p_exp3b_active_player_gate.txt`.

## Turn note (2026-07-10, Cursor — full pickup native recipe in hook)

**Сделано в `teleport_hook.cpp` / `hook_offsets.h`:**

- `seh_run_puppet_pickup_recipe`: `Player_ResolveWeaponPickupInteractAnim`
  (`0x140C7FF40`) → `InputScreenBlock_ActivateCommand` → optional
  `sub_141C937D0` radius prep + `InteractControl_GetAvailability` (`0x141C9A360`)
  when nearby `WeaponPickup` resolves → `Player_InteractDispatcher` tick → fallback
  `RequestInteract` + `Player_PublishRequestedInteract`.
- **Timing fix (2026-07-10 runtime):** gesture больше не армится при `STORED`;
  `queue_relay_pickup_gesture_after_mirror` ждёт `SLOT-MIRROR gave=1`, затем
  `PICKUP-GESTURE-ARM puppet_has_art=1`. `apply_puppet_pickup_gesture` /
  `apply_peer_pickup_animation` пропускают кадры пока art не на кукле.
- Sender: `capture_weapon_pickup_for_relay` **до** `GiveWeapon` (все 6 screen
  dispatchers + interact-control +0x10/+0x80) → лог `PICKUP-ID-CAPTURE pre-give`.
- Исправлен баг: `GetAvailability` вызывался с `pickup` вместо `puppet` как arg2.
- Gesture window: 700ms → 1200ms per-frame re-activate.

**Билд:** `scanner/build/tll_teleport_hook_v2.dll` OK. Inject в PID 14224/18988 —
boot lines в `locomotion_hook.log`.

**Прогноз in-game теста:** подбор M-30 на host → `GIVEWEAPON-RELAY-STORED` затем
`SLOT-MIRROR ... gave=1` → `PICKUP-GESTURE-ARM puppet_has_art=1` →
`PICKUP-ANIM-APPLY resolved_cmd=1` (если C7FF40 резолвит после give на кукле).

**RUNTIME REJECTED (2026-07-10, PID 24424/27880):** timing fix сработал
(`PICKUP-GESTURE-ARM puppet_has_art=1` после `SLOT-MIRROR gave=1`), но bend нет:
`resolved_cmd=0 dispatch=0 pickup=0 pickup_id=0x0` — `C7FF40` на кукле не резолвит
cmdId даже с M-30 в слоте; nearby `WeaponPickup` на peer отсутствует; interact SM
куклы не крутится (та же стена, что aim-pitch). **PICKUP-ANIM ПАРКНУТ** — инвентарь
через `WeaponGivePacket`+`SLOT-MIRROR` работает; дальнейшие попытки через command/
dispatcher требуют materialize slot-1 interact controller (отдельный трек).

**Не переоткрывать:** play-simple-gesture, play-animation relay, force `+0x15A9` bit1,
direct `42FCF0(puppet)`, RegisterSpotPlayer для puppet.

## Turn note (2026-07-10, Cursor — Exp 3b spot-player enumerator pinned)

**Статика:** канонический **spot-player enumerator** — глобал `dword_143D3C0D0`
(RVA `0x3D3C0D0`). `*(gw+0)` = `m_spotPlayerCount` (loop bound во всех
`i < m_numPlayers` потребителях). Список handle-пар @ `gw+0x10` (stride 16);
каждая запись требует `*(player+0xBC) == meta_screen`.

**Связь с Exp1–3:** `resolve(1)==puppet` не добавляет puppet в этот список.
Per-player early update (`vtable+0x70` → `0x1404901B0` → `42FCF0`) диспатчится
виртуально из enumerator-контекста; `sub_1403E3E30` тикает тот же список в
world frame (не прямой caller `4901B0`, но тот же набор игроков).

**Runtime READ (безопасный):** в REALSECOND observe (Numpad 7) добавлен лог
`REALSECOND-EXP3B`: `spot_count`, `max_local`, `resolver_active` (`FE1070+0x495C`),
`screen_mask` (`gw+0x5D08`), entries, `puppet_in_list`.

**Прогноз:** после split-login `spot_count=1`, `puppet_in_list=0`, `max_local=2`.

**RUNTIME CONFIRMED (2026-07-10):** две копии loopback, inject v2, Numpad 7.
Host PID 27992 после spawn puppet: `spot_count=1 max_local=2 puppet_in_list=0
bit1=0 hub+anim OK resolve(1)==puppet`. H1 подтверждена.

Детали: `reports/real2p_exp3b_active_player_gate.txt`, `locomotion_hook.log`.

**Writer (STATIC):** `NpcManager_RegisterSpotPlayer` `0x1403E84F0` — единственный
writer `*(gw+0)++` + pair @ `gw+0x10`. Вызывается из `NpcSpawn_FinishSetup`
`0x1403FF740` @ `0x140400451` (gate `[entity+0x1F70]+0x58!=0` @ `0x140400435`).
Split-login `0x140C02220` **не** пишет в spot-list. Puppet в CharacterManager
(`0x143898630`), но не проходит Register → `spot_count=1`.

**Следующий шаг:** почему net-spawn puppet не доходит до `0x140400451` или падает
на gate `+0x1F70/+0x58`; runtime hook на `3E84F0` для slot-1 spawn.


## Turn note (2026-07-10, Cursor — 421150 = real 422760 caller; +5545 bit1 gate)

**CORRECTION:** `Player_PreAnimHubAndAnchorSync` `0x140422BB0` **не** вызывает `422760`
(только sniper aim-pos). Реальная цепочка:

`sub_14042FCF0` → `sub_140421150` → `Player_SyncControlHostEntityRootOnNetSkip` `0x140422760`.

**Новый gate (STATIC):** для **живого** игрока `421150` зовёт `422760` **только** если
`player+0x15A9` (+5545) **bit1** (`& 2`). Иначе bit0 на hub entity не ставится →
утренний `144610` сбрасывает bit4 → `TickDecisionNode` не проходит.

Доп. gate: весь `421150` — no-op если `player+0x1A0` (+416) null (anim-instance).

**Открыто:** кто пишет `+5545` bit1; алиасит ли `*(player+1FB0+16)` entity с записью в
`worldMgr+0x5A0` hub (единственный живой `144610` для Player).

Детали: `reports/player_421150_entity6e_gate.txt`.

**Следующий шаг:** xref-writer на `player+0x15A9` bit1; сравнение slot0 vs puppet при
spawn; runtime `5545` + `entity+0x6E` на `421150`/`4223F0`.


## Turn note (2026-07-10, Cursor — player+0x1FF0 = Skill; 422760 clear REJECTED)

**Статика:** `player+0x1FF0` = **текущий Skill*** (AI skill think:
`RefreshOrSelectCurrentStateObject_1FF0` `0x14041F1A0`, строки `"Entering skill"` /
`"THINK! desired skill"`).

**REJECTED:** гипотеза что `422760` сбрасывает bit0x4 через `Skill+vtable+96`:
`Skill::vtable+96` → `Skill_IsAuthorityForRootTransform` `0x140664690` **всегда
return 1** (проверено Skill/ScriptSkill/IdleSkill/NetSkill). Clear-ветка
`422760` **не достижима** для стандартных Skill.

**Уточнённая стена bit0x4:**

- Единственный writer `|= 4` на hub entity: `ControlHostHub_RefreshEntity6EProbeFlags`
  `0x142144610` (нужен bit0).
- Per-player вызов на `player+0x1FB0` из `0x1403E59D0` **загейтован**
  `Player::vtable+4256` `0x1401D4F10` → **всегда 0** → цикл по игрокам не
  вызывает `144610` на `+1FB0` hub для класса Player.
- `422760` else-путь ставит только **bit0**, не bit4; `TickDecisionNode` в том же
  кадре требует bit4 → **frame-order** или отсутствие promotion на puppet hub.

**Следующий шаг:** откуда slot 0 получает bit4 на `*(player+1FB0+16)`; runtime
`entity+0x6E` slot0 vs puppet на входе `4223F0`.

Детали: `reports/player_1ff0_skill_entity6e_revision.txt`.

## Turn note (2026-07-10, Cursor — entity+0x6E gate mapped)

**Статика:** кто выставляет/сбрасывает `entity+0x6E` на control-host
`*(player+0x1FB0+16)` — **найдено**.

**Setter (global tick):** `GameplayWorld_RefreshControlHostEntity6EFlags`
`0x1403E59D0` ← `sub_1403E4AF0` (world frame tick) → для каждого Player с
vtable+4256: `ControlHostHub_RefreshEntity6EProbeFlags` `0x142144610` на
`player+0x1FB0`. Требует `entry+0x6E bit0` и `entry+108 >= 0`, затем `|= 4`
и условно `|= 8`.

**Clearer (per-player anim-phase, до decision-node tick):**
`Player_PreAnimHubAndAnchorSync` `0x140422BB0` →
`Player_SyncControlHostEntityRootOnNetSkip` `0x140422760`: когда
`player+0x1FF0` (gameplay-state selector) существует, его `vtable+96` false, и
`Player_ShouldSkipDirectRootTransformApply` true → **`entity+0x6E &= 0xFA`**
(сбрасывает bit0x4). Сразу после этого в том же `AnimPhaseOwnerUpdate`
`Player_TickDecisionNodeFromControlHost` требует bit0x4 → **puppet на net-skip
пути не тикает `+0x1808`**.

**Вторичная стена:** `InputObject_MatchesActiveLocalJoypad` `0x14023AEA0` —
`object+0x50` vs `g_LocalPlayerInputManager+0x48/+0x50` (смещения +72/+80 в
деcompile); блокирует `entry+24` на nearby-list.

**Метки IDB:** +5 функций (`422760`, `422BB0`, `3E59D0`, `142144610`, `1B7500`);
939 total, `save_database OK`.

**Следующий шаг (статика):** что за объект `player+0x1FF0` на puppet vs slot 0
(`RefreshOrSelectCurrentStateObject_1FF0` `0x14041F1D0`); порядок global tick vs
anim-phase в кадре; joypad indices в co-op.

**Рантайм-прогноз (квота почти набрана):** hook `0x1404227B0` — puppet каждый
кадр; slot 0 нет. На входе `0x1404223F0` у puppet `entity+0x6E & 4 == 0`.

Детали: `reports/player_decision_node_controllers.md`,
`reports/entity_6e_*.txt`. Метки: `ida_apply_labels.py`.

## Turn note (2026-07-10, Cursor — vtable+1512 + Active decision node)

**Статика:** `player->vtable+1512` = `Player_ShouldSkipDirectRootTransformApply`
`0x141BC2EF0` (тот же слот `+0x5E8`, что и root-transform). **Не** жёсткий
off-switch: обе ветки `Player_AnimPhaseOwnerUpdate` вызывают
`Player_TickDecisionNodeFromControlHost`. Net-путь: `NetController_ShouldSkipAnimPhaseDirectPath`
`0x1419136E0` (child `vtable+24`).

- Locomotion decision node: `PlayerActiveDecisionNode_Tick` `0x14126B830`
  (vtable `0x142E47BC0`) → `BuildInteractDispatchState` + dispatch.
- `player+0x1808`: clear `0x141DE181A`, store `0x141DE2604` via `vtable+0xDE8`;
  leaf impl `0x14126C6A0` void — откуда берётся указатель, **открыто**.
- **Стена puppet (уточнено):** скорее `entity+0x6E` control-host (bit0x4/0x8) и
  joypad-gate на nearby probes, не vtable+1512.

**Следующий шаг:** кто выставляет `entity+0x6E` bits для slot 1; joypad indices
`g_LocalPlayerInputManager+0x48/+0x50`.

Детали: `reports/player_decision_node_controllers.md`. Метки: `ida_apply_labels.py`.

## Turn note (2026-07-10, Cursor — decision-node scheduler mapped)

**Статика:** scheduler decision-node найден.

- `Player_AnimPhaseOwnerUpdate` `0x14042D120` → `Player_TickDecisionNodeFromControlHost`
  `0x1404223F0` → `(*(player+0x1808))->vtable+0` (активный decision node).
- `Player_DecisionNodePublishGate` `0x140CD3760` → `PublishRuntimeStateFromDecisionNodeLikeObject`
  (= `Player::Active::Update`) после проверки `player+0x1FC8` vtable+168.
- Offsets: `+0x1808` decision node, `+0x1FB0` (8112) dispatch hub, `+0x1FC8` (8136)
  scripted interact, `+0x1F28` (7976) interact sub-controller.
- **Стена puppet:** scheduler не тикает (vtable+1512 / control-host flags), хотя
  global nearby-list tick (`0x1401611F0`) работает → `ok=1 dispatch=0`.

**Следующий шаг:** идентифицировать `player->vtable+1512` и что пишет `player+0x1808`
для slot 1.

Детали: `reports/player_decision_node_controllers.md`.

## Turn note (2026-07-10, Cursor — nearby-list writer + joypad gate)

**Статика завершена:** писатель nearby-list найден.

- `InteractDispatcher_RebuildNearbyList` `0x140C837F0` — сбрасывает count@+0,
  собирает кандидатов (`0x141C94340` + sphere-cast пути), пишет 32-байт entries@+8
  через `InteractDispatcher_AppendNearbyEntry` `0x140C817F0`.
- Глобальный тик: `InteractDispatcher_TickAllScreenBlobs` `0x1401611F0` →
  `InteractDispatcher_TickNearbyList` `0x140C87FB0` на `0x143EC4EE0` (screen 0) и
  `0x143EC84A0` (screen 1 в co-op). `sub_140C858F0` — только prune, не writer.
- `Player_InteractDispatcher` ставит `dispatcher+0x3570=1` в эпилоге → rebuild на
  следующем кадре.
- **Ведущая гипотеза `dispatch=0`:** `InputObject_MatchesActiveLocalJoypad`
  `0x14023AEA0` блокирует sphere-cast на entry → `entry+24` не выставляется для
  joypad 3 / screen 1 puppet.

**Следующий шаг:** scheduler decision-node (`player+0x1F28` / `+0x1FB0`); runtime
подтверждение joypad-индексов на `g_LocalPlayerInputManager+0x48/+0x50` только после
квоты (или как часть materialize-puppet-controller теста).

Детали: `reports/player_decision_node_controllers.md`. Метки в `ida_apply_labels.py`.

## Turn note (2026-07-10, Cursor — продолжение Codex/Claude: карта decision-node контроллеров)

**Где остановились агенты (2026-07-10):**
- **Codex:** direct-start pickup fallback в билде, runtime не подтверждён; направление =
  materialize puppet как stock local-player-1 controller.
- **Claude:** pickup-anim + aim-pitch = одна стена (per-player контроллеры куклы);
  baseline чистый, 3 экспериментальных тогла OFF.
- **Работает:** локомоция, cover `bk`, jump/roll `bh`, weapon give/display,
  fire direction (pose-seed), health, transport.

**STATIC (этот ход):** разобрана цепочка interact decision-node → dispatch.
9 функций (`reports/player_decision_node_controllers.md`, дамп
`reports/full_slot1_controller_decompile.txt`) вызывают
`Player_BuildInteractDispatchState` + `Player_InteractDispatcher` из vtable-ticked
decision-node объектов (`player+0x1F28` и др.). Ключевые:
`PlayerInteractController_UpdateReviveAndDispatch` `0x140EC75D0`,
`PlayerScriptedInteractController_UpdateAndDispatch` `0x140CD3C00`.
Все гейтятся `!BYTE2(dword_143073DA8)`.

**Почему `dispatch=0` на кукле:** команда активируется (`ok=1`), но dispatcher не
находит eligible nearby — список на `0x143EC4EE0+0x35C0*screen` вероятно не
заполняется для transform-pinned puppet. Direct-start обходит это, ища `WeaponPickup`
вручную.

**Следующий статический шаг:** найти писателя nearby-list диспетчера и scheduler
decision-node тиков для screen 1 (не ещё один runtime pickup тест).

## Turn note (2026-07-10, Codex - screen-1 interact tick and direct pickup action)

Static analysis corrected the apparent shared-controller root. The event list at
`Hud+0x320 -> +0x1930` is a **HUD pickup-notification queue**, not the character
interaction state machine. Both 0x368-byte Hud objects are constructed and
initialized, while the stock frame wrapper updates only screen 0; ticking screen 1
was implemented once, then removed from the active source because it does not own
the visible pickup bend.

The real interaction path is now mapped one level deeper:

- `Player_BuildInteractDispatchState` (`0x140C805A0`) builds the required 0x15C-byte
  command/pose block and snapshots the indexed screen's `+0x590` command list.
- `Player_InteractDispatcher` (`0x140C85D20`) consumes that block and, for a weapon
  pickup, queries `InteractControl_GetAvailabilityForPlayer` (`0x141C9A360`), then
  calls control vtable `+0x28` `RequestInteract` and publishes the request.
- Runtime with the missing builder+dispatcher call restored for screen 1 produced
  `PICKUP-GESTURE ... ok=1 dispatch=0`: the command is active, but the dispatcher
  does not select an eligible nearby interaction. This rejects the claim that the
  absent dispatcher tick alone was the wall.

The original fallback found the same-art `WeaponPickup`, but stopped after the
availability query and therefore never submitted the action. The corrected relay
now calls `RequestInteract` and publishes the request. Duplicate `OnInteract` /
GiveWeapon completion remains suppressed; inventory is still owned by
`WeaponGivePacket + SLOT-MIRROR`. The distinguishing log is now
`availability=1 requested=1 published=1`; visual confirmation is pending.

## Turn note (2026-07-10, Codex — direct authored pickup-animation relay REJECTED; full slot-1 controller model opened)

**Runtime result:** two instrumented builds hooked the local Player script-event
handler (`0x140DCDEA0`) and relayed both `play-simple-gesture` (`0xC1F3F991`) and
direct `play-animation` (`0x09A3F435`). Across repeated real M-30 pickups,
`GiveWeapon ret_rva=0xDD4208` and `WeaponGivePacket + SLOT-MIRROR` fired, but neither
animation event appeared on the sender. Therefore the visible bend is not carried
by either authored Player event and this direct relay cannot synchronize it.

The experimental packet/hook/apply code was removed; the stable gameplay path is
unchanged. Reusable IDA tools were added for CRC preimages and narrow disassembly
ranges. **New direction requested by user:** materialize the puppet as the stock
local-player-1 controller model, identifying the scheduler/ownership difference
between the already-full retained slot-1 `Player` (`screen=1`, Joypad 3) and the
engine's dormant local second-player path. Pickup/aim should then be consequences
of the same ticking per-player controllers rather than per-feature replay.

## Turn note (2026-07-10, Claude — pickup-anim + aim-pitch мапнуты до архитектурной стены; забанкали рабочее)

**Итог хода.** Диагностировал застревание Codex по анимации подбора и размотал
питч-прицеливание до конца. ОБЕ фичи упираются в одну стену: у transform-pinned
куклы с выключенным AI её per-player контроллеры (interact-стейт-машина, aim-стейт/
камера) либо не крутятся, либо конфликтуют с локомоцией.

**Анимация подбора:** триггер Codex (RequestInteract 0x1408C8A40) НЕ вызывается при
наземном подборе (идёт через скрипт give-weapon 0x140DD4208). Нашёл настоящий рецепт:
`Player_ResolveWeaponPickupInteractAnim → cmdId` + `InputScreenBlock_ActivateCommand`
(0x14183A650) активирует interact-команду в +0x590, обходя блокер кнопок. Технически
`ok=1`, но кукла не анимируется — interact-стейт-машина не обрабатывает (AI off). Даже
удержание команды 700мс не помогло. `g_puppet_pickup_gesture` OFF.

**Питч-прицеливание:** цепочка `стик→aim-углы→camera pose-seed(blk+0x60)→player+0x1720/
+0x1730→рендер`. Все уровни (player+0x1570/+0x1730, camera seed, resolver output,
camera-блок +0x60/+0x230/+0x240) = ВЫХОДЫ, пересчитываются каждый кадр. Чистого
driveable pitch-входа нет; писать общий screen-1 seed = freeze локомоции. Материализация
screen-1 камеры (0x238800) безопасна, но инертна. Все aim-тоглы OFF.

**Забанкано и работает:** смена/holster/подбор-дисплей оружия (WeaponGivePacket +
SLOT-MIRROR), направление огня, горизонтальный aim (через facing-blend), локомоция с
разворотами, транспорт, здоровье. Baseline чистый, 3 экспериментальных тогла OFF,
диагностика Numpad+ (AIMROW/CAMROW) оставлена.

**Следующий ход (вне стены):** взять ко-оп фичу, не завязанную на per-player
контроллеры куклы (см. TASK_POOL.md). Питч/жест возвращать ТОЛЬКО при
фундаментальной смене модели куклы (реальный второй-игрок aim-стейт вместо
transform-pin) — это большой отдельный проект, не инкремент.

## Turn note (2026-07-09, Codex — aim sync default-ON REJECTED, stable OFF restored)

**Aim sync resumed.** IDA MCP is reachable at `http://172.21.144.1:8745/mcp`; verified
`decompile_function` for `PlayerAimController::GetTargetPointFromReticle`
`0x140CB1720`, `Player_PostAnimUpdate` `0x1410E9210`, and
`ResolveScreenRoutePoseSeedOrDefault` `0x141C077B0`.

**Runtime rejection:** default-ON `g_puppet_aim_reticle_relay=true` produced strong
running jitter. Logs showed continuous `AIM-TARGET-WRITE` / `POSESEED-CALLERS` on
both processes while the user observed the run jerk. Restored
`g_puppet_aim_reticle_relay=false` by default; Numpad `/` is now hard-disabled for
this path so accidental retests cannot reintroduce the jitter mid-run. Full puppet
ADS input remains **OFF** by default and is still an explicit `F8` A/B toggle.

**Safety:** this does **not** re-enable the REJECTED screen-1 broad pose-seed/source
write. `g_puppet_aim_override_frame` stays false, and the old
`write_peer_seed_to_screen1_resolver_source()` path is not called. Scoped-rifle
remote aim now scrubs puppet scoped ADS unless full ADS relay is explicitly enabled.

**Next:** aim sync needs a narrower aim-additive input or movement-aware gating before
it can be default-on. Stable locomotion has priority; leave the pose-only visual relay
OFF unless explicitly reworking it in code.

## Turn note (2026-07-07, Cursor — PAUSE aim relay; новый P0 = синхронизация поворота головы)

**Aim/ADS relay — на паузе** (билд `as`, user-test не дал стабильного ADS). Не трогать
F8/pose-seed до отдельного запроса.

**Новая задача (user):** когда на своей копии крутишь камеру и персонаж смотрит в
сторону взгляда — на второй копии это тоже должно быть **видно** (голова/верх корпуса,
не обязательно полный ADS).

**Что уже есть в протоколе:**
- `StatePacket.quat` @ `Player+0x140` — **корпус**, всегда шлётся; пуппет блендит его.
- `camera_quat` + flag `0x0100` — ориентация `CameraManager(0)+0x1B0`, сейчас **только
  при strict RMB/ADS** (иначе пуппет крутился при беге).
- `look_stick` — относительный дельта yaw/pitch камеры vs корпус; на приёмнике **не
  кормится** в right stick (jitter, 2026-07-05).

**Почему сейчас не работает «голова следует за камерой»:** при free-look камера
отъезжает от `quat`, а в пакет уходит только корпус. Нативный путь —
`PlayerLookAtIkController` на **`Player+0x1E38`** (vtable `0x142A2D550`).

**STATIC (2026-07-07, готово):** `reports/lookat_field_map.md` — поля `+0x3F0`
(look point), `+0x420` (enable), virtual **SetLookPoint** `0x140EDA5F0`,
**UpdateIk** `0x140EDA680`. `+0x1DD0` = wall-hand IK, `+0x1E48` = perch.

**Head look sync — PARKED (2026-07-07, user revert):** `g_puppet_head_look_sync=false`
(buil `bc`). IK path worked but puppet spine twist unacceptable.

**Jump/roll relay (2026-07-08bh):** `bg` user-rejected: standing jump silent; standing roll silent; run+jump showed as roll. Root cause CONFIRMED in logs: `TRAVERSAL-PAD-MASKS jump=0x20 evade=0x80` (authored face pads). Sender ORd those into the packet (`LOCOMOTION-SEND trav=0x20`), and the Joypad3 force-hook matched `0x20` as evade / `0x80` as crouch -> jump->roll bleed.

**Fix `bh`:** protocol + force-hook use ScaleCopy INTENT bits only (`jump=0x4000000`, `evade=0x800000`, `crouch=0x400000`). Face pads stripped on send and receive. Init `2026-07-08bh`. Predict: trav=0x4000000 for jump, trav=0x800000 for roll; standing jump+roll work; run+jump is jump not roll.

**Cover relay (2026-07-08bi→bj→bk):** `bj` stuck puppet near walls: `try-enter-cover` (approach probe) was latched as in-cover. **Fix `bk`:** sustained `0x0800` = `Player+0x1510` bit0 only; one-shot `0x1000` = `cover-enter` command edge (not try-enter). No try-enter on puppet force-hook. Init `2026-07-08bk`.


---

## Turn note (2026-07-07, Cursor — peer-only puppet aim, build `as`)

**Продолжено с остановки Claude/Codex** на aim-relay (user: F8 даёт ложную анимацию
прицела с пистолетом; в настоящий ADS/прицел не входит; направление не то).

**REJECTED (повторно зафиксировано в коде):** per-frame
`write_peer_seed_to_screen1_resolver_source` (blk+0x60) + `g_puppet_aim_override_frame`
→ замораживает facing локомоции куклы (коммит `93d1420`).

**Билд `2026-07-07as`:**
1. Все пути «peer целится» = `peer_packet_aim_active` (`0x200` + `pose_seed` + свежий
   пакет <250 ms) — убирает ложный aim при пистолете в руках без RMB пира.
2. Убран REJECT-путь screen-1 seed write из `drive_remote_slot1_actions_tick`.
3. F8 + peer RMB → scoped resolver override на `0x10E934B` / `0xCB17D6` (как Numpad
   `/`, но без ручного тоггла) + `apply_puppet_sustained_aim_input` (joypad `0x200`).
4. На входе в peer-aim — one-shot `materialize_screen1_camera_once` (0x238800).
5. F8 в коопе не переключает legacy WASD-NPC.

**Модель (user-approved):** пуппет на копии 1 целится **только** когда пир реально
жмёт RMB на другой копии. Локальный RMB на этой копии пуппета не крутит.

**Тест:** copy1 F8 ON → copy2 Nadine RMB (пистолет) → на copy1 Nadine входит в
stance + смотрит на peer; copy1 Chloe RMB → Nadine **не** целится; F8 без RMB нигде
→ нет aim-анимации. Лог: `2026-07-07as`, `AIM-POSE-OVERRIDE site=0x10E934B`.

**Открыто:** вертикальный pitch / reticle HUD; снайпер scope vanish; weapon fountain.

---

## Turn note (2026-07-07, Cursor — aim-поза: writer chain + build `af`)

**Продолжено с остановки Claude/Codex на aim-pose** (коммиты `c3825a2`…`d69a7ad`):
live-дифф нашёл `Player+0x1720/+0x1730`, пост-запись инертна, POSESEED-CALLERS
гистограмма собиралась.

**STATIC (квота выполнена):**
1. `sub_1410E9210` (`Player_PostAnimUpdate`) вызывает resolver **ret_rva=0x10E934B**,
   затем `vmovups` в `+0x1720/+0x1730` — это и есть писатель aim-позы.
2. Тот же `0x10E934B` — доминирующий hit в POSESEED-CALLERS при ADS куклы.
3. Согласовано с `pseudo_camowner.txt` и xref `*(owner+0x4010)` screen index.

**Реализовано (build `2026-07-07af`):**
1. Resolver override расширен: call-site `0x10E934B` → полный peer pose-seed 32B;
   `0xCB17D6` (reticle) — quat+puppet origin. Тоггл **Numpad /** =
   `g_puppet_aim_reticle_relay`.
2. **Shift+Page Up** — write-bp на live `Player+0x1730` (верификация writer RIP).
3. Скрипт `scripts/ida_find_player_1730_writers.py`.

**Фальсифицируемый прогноз:** Numpad `/` ON, Chloe ADS двигает прицел → Nadine верх
корпуса следует; локальный aim Chloe **не** замирает; fire ray без регрессии.

**Параллельно (оружие, build `ad` в working tree):** fountain-fix не подтверждён
user-test — отдельная ветка.

---

## Turn note (2026-07-07, Cursor — продолжение Codex: class-slot swap в relay give, build `t`)

**Продолжено с места остановки Codex** (коммиты `3d7d52e`…`7aabc23`):
статика нашла корень фонтана = M-30 садится в manager-слот 1 (коллизия с пистолетом),
план = REMOVE старого оружия в peer-слоте перед GiveWeapon.

**Реализовано (build `2026-07-07t`):**
1. `seh_remove_weapon_in_slot` — **исправлен** примитив: `GetWeaponBySlot` (vt+0xB0) +
   `ProcessKillTree`, не vt+0x188 (это `GetWeaponByClass`, class-index ≠ slot-index).
2. `apply_peer_weapon_give` — перед one-shot relay give: `peer_slot_of_art(art)` из
   raw snapshot (слоты 0/1/16/17/2/3/20/22) → remove чужой art в том же слоте → give.
3. Диагностика: `GIVEWEAPON-RELAY-APPLY target= removed= removedArt= slotAfter=`.

**Стабильная ветка сохранена:** `g_relayed_block_give` default OFF (Numpad -);
one-shot pickup relay через `WeaponGivePacket` "COPG" + sender hook GiveWeapon+9
(`ret_rva=0xDD4208`) работает без пер-тик re-give из store.

**Фальсифицируемый прогноз теста:**
- Подобрать M-30 на copy 1 → `GIVEWEAPON-RELAY-SEND` / на copy 2
  `GIVEWEAPON-RELAY-APPLY target=16 removed=16 removedArt=0x8075397d slotAfter=16`
  (net-режим: long-gun класс 16 → слот 16, не 1).
- Фонтан из Надин **не** должен появиться; пистолет остаётся в слоте 17/1.
- Если `target=-1` (snapshot отстаёт) — remove пропускается; тогда возможен
  частичный регресс → нужен fallback по def-классу.

**Следующий ход:** runtime confirm; при успехе — включить `g_relayed_block_give` с
give-once memo; при `target=-1` — fallback remove по WeaponArtDef+8 через
`sub_141094420` net remap.

## Turn note (2026-07-06, Cursor — стабилизация build `s`, fountain откат)

**Зафиксировано по запросу пользователя** после регрессии «фонтан оружия» на билдах
`o`/`p`/`q`.

**Откат (build `2026-07-06s`):**
- Убран **WeaponInfo relay** — хук `PlayerWeaponController::GiveWeapon`, пакет
  `WeaponInfoPacket`, `apply_peer_weapon_info`.
- `puppet_weapon_give_allowed` снова **строгий whitelist** (3 art: pistol / rifle /
  scoped pistol). Подобранное с земли оружие **не синхронизируется в руках** — это
  осознанный trade-off ради стабильности.
- **Оставлено:** pickup relay (`OnInteract` → `PickupPacket` → native Apply),
  `force_log_line` для `PICKUP-*` / `HELD-WEAPON-*`, verified give memo, pose-seed
  fire path (native fire OFF), held-weapon slot sync для whitelist arts.

**Следующий ход (когда продолжат):** статически понять, почему `GiveWeapon` с полным
`WeaponInfo` для pickup arts (`0x3019cbee` и др.) снова вызывает fountain на puppet;
возможные пути — relay только через `NetEventSyncWeaponPickups` без повторного
`GiveWeapon`, или отдельный draw path без inventory mutation.

## Turn note (2026-07-06, Cursor — продолжение Claude: fire-caller pose-seed + pickup relay, build OK)

**Собрано и заинжекчено** (PID 25372 / 16372, `tll_teleport_hook.dll`). Логи по
умолчанию OFF (F11).

**Сделано поверх незакоммиченного WIP Claude:**
1. **StatePacket v8** — sender шлёт `pose_seed` (32 байта, flag `0x0200`) из
   `CameraManager(0)→*(+0xB0)+0x60` (уже было в WIP).
2. **Weapon pickup relay** — sender хук `WeaponPickupInteractControl::OnInteract`
   `0x14157AA70` → UDP `PickupPacket`; receiver `NetEventSyncWeaponPickups_Apply`
   `0x140BCE440` на игровом потоке (WIP Claude, **ещё не runtime-проверено**).
3. **Fire direction (redesign по LESSON 2026-07-05):** широкий override resolver
   заменён на **fire-caller gate**:
   - хук `PlayerFireController_BuildFireArgsAndFire` `0x140E2BCD0` выставляет
     `thread_local g_pose_seed_fire_only_override` только когда owner == puppet
     (slot 1);
   - хук `ResolveScreenRoutePoseSeedOrDefault` при этом guard подменяет **только
     quat** (`seed+0x10`), позицию луча оставляет от puppet resolver;
   - `g_puppet_relay_pose_seed` default **OFF**, toggle **VK_APPS** (context menu).

**Прогноз теста (pose-seed):** VK_APPS ON → puppet стреляет по направлению прицела
пира, aim camera **не** замирает (в отличие от broad override 2026-07-05).

**Прогноз теста (pickup):** поднять оружие на copy A → в логе (F11) `PICKUP-SEND` /
на copy B `PICKUP-APPLY`; pickup исчезает у второй копии. Если object id не
совпадают между инстансами — нужен name-hash fallback.

**Открыто:** Chloe vanish при ADS снайпера на copy 2; weapon attach `attached=0`;
grenade arc — тот же pose seed, но отдельный caller (пока не захукан).

**Следующий ход:** runtime confirm pose-seed (VK_APPS) + pickup; при успехе —
grenade caller; sniper vanish — статика scope camera / model cull screen-1.

## Turn note (2026-07-05, Claude — FIRE RAY источник найден: pose seed CameraManager+0xB0→+0x60, фикс build-ready)

**Статика замкнула цепочку «стреляет не в ту сторону» до конца** (реестр: "FIRE RAY
SOURCE FOUND"). Оба камера-резолва ВНУТРИ Fire — посторонние (шум выстрела для NPC
и стат `unzoomed-sniper-shots`). Реальный луч: единственный вызывающий Fire —
`PlayerFireController_BuildFireArgsAndFire 0x140E2BCD0` — сидит луч через
`ResolveScreenRoutePoseSeedOrDefault(&seed, *(player+0x4010))` `0x141C077B0` =
`CameraManager::GetHandle(idx)`: `blk=*(mgr+0xB0)`, гейты `blk+85/+86` + бит
`dword_14697AE34`, **сид = 32 байта `blk+0x60` = [pos float4][quat float4]**;
при провале гейтов — **pos(0,0,0)+identity** → ровно наблюдаемое «не туда».

**Фикс к следующему ходу (build-ready):**
1. Sender: читать свой сид целиком (`*(CameraManager(0)+0xB0)+0x60`, 32 байта) и
   слать в StatePacket (нужно новое поле — camera POSITION в сиде есть, в текущем
   пакете нет).
2. Receiver: хук `ResolveScreenRoutePoseSeedOrDefault` — при `idx==1` и peer-aim
   писать эти 32 байта в out и return (движковое состояние не трогаем, гейты не
   нужны).
Прогноз: выстрелы Второго игрока ложатся по прицелу пира в обеих копиях.

**IDB:** +PlayerFireController_BuildFireArgsAndFire, +ResolveScreenRoutePoseSeed
OrDefault (детальный комментарий), 870 функций, save True; MCP обновлён.
Параллельно ранее: снайпер-скоуп гейт (`286b6f1`), см. прошлые заметки.

## Turn note (2026-07-04, Claude — LAG root cause = per-frame VirtualQuery, CONFIRMED FIXED)

**Причина сильных лагов найдена и устранена (подтверждено пользователем).** Без мода
игра летала, с модом — фризы при низком CPU/GPU (главный поток ЖДАЛ). Корень:
`readable_bytes` звал `VirtualQuery` на каждую проверку указателя (280 мест,
десятки/кадр); `VirtualQuery` берёт процессный лок адресного пространства, который
потоки стриминга/аллокатора игры держат постоянно → контенция → стоп главного
потока каждый кадр.

- **Фикс (`10ece75`):** `readable_bytes` fast-path = SEH-проб по байту на страницу
  (без syscall/лока), возвращает доступный размер до `0xB000`. Тумблер
  `g_readable_bytes_fast` (VK_NEXT / Page Down) — откат на `VirtualQuery` для A/B.
- **Логи (`ea77bcb`/`46ab041`):** постоянный дескриптор вместо open/close на строку;
  `g_logging_enabled` по умолчанию OFF, F11 включает. (Оказалось вторичным.)
- **Две копии на 16 ГБ RAM** свопят (по 10 ГБ каждая) — отдельное ограничение
  локального теста, не связано с этим лагом.

**Хоткеи (актуально):** VK_HOME = DLL inert; F11 = логи вкл/выкл; VK_NEXT = fast/slow
readable_bytes; F8 = puppet aim-press (ADS) relay; F7 = legacy move-gate.

**Игра запущена (PID 6276), новый DLL заинжекчен, лаги ушли.** Кооп-задачи
(HUD-гейт скоупа для не-основного экрана, реле направления взгляда) — следующий ход.


## Turn note (2026-07-04, Cursor — crosshair dup: ADS-флаг гейт REJECTED, новый гейт по screen index в per-screen HUD паблишерах)

**Тест (PID 21040/4488):** гейты `is-scope-visible?` + reticle-паблишеров, условие
`remote_player_ads_active()` (`Player+0x1548 != 0`) — прицел всё ещё дублируется, и
в логе **ноль** строк `SCOPE-VISIBLE gated`/`RETICLE-HUD gated`, при том что
марионетка держала aim (`JOYPAD3 current=0x209`). → **REJECTED:** `+0x1548` ставится
только при scoped-RIFLE ADS; при обычном (пистолетном) прицеле он 0. Как общий
предикат «удалённый целится» НЕ использовать.

**Новая статика (см. initial-findings, запись «Per-screen HUD reticle publishers»):**
обычное перекрестие спавнится per-screen HUD паблишерами над
`g_PerScreenPlayerHudState 0x143EF1E30` (stride 0x100), **индекс экрана/слота в
`state+0xF8`**:
- `0x14135A650` `PlayerHudScreen_ReticleProcessPublisher` (спавнит `ss-hud-reticle`);
- `0x14135AF20` `PlayerHudScreen_AmmoReticlePublisher` (тоже спавнит `ss-hud-reticle`,
  вызывается из `UpdatePlayerHudAimingAndWeaponAccuracyPublishes`);
- `0x14135C050` — blindfire, жёстко screen-0 (алиасы `0x143EF1F20/0x143EF1F28`).
Доказательства индекса: `GetRetainedPlayerEntityBySlot(*(a1+248))` + джойпад
`0xBF0*(slot+2)` (совпадает со slot0→joypad2, slot1→joypad3) + фикс-алиасы screen-0.

**Фикс (teleport_hook.cpp, собрано+заинжекчено):** хуки на `0x14135A650` и
`0x14135AF20` — скип паблиша, если `*(state+0xF8) != 0`
(`hud_state_is_non_primary_screen`, лог `RETICLE-HUD gated: screen!=0` /
`RETICLE-AIM-HUD gated: screen!=0`). Не зависит от оружия/ADS. Гейт
`is-scope-visible?` (снайперский оверлей) оставлен по remote-ADS `+0x1548` — для
винтовки флаг работает (диф это показал).

**Прогноз теста:** пир держит прицел (любое оружие) → на приёмнике поза
прицеливания у марионетки ЕСТЬ, перекрестия НЕТ, в логе `RETICLE-*HUD gated:
screen!=0`; свой локальный прицел рисуется как обычно. Если перекрестие всё же
остаётся при наличии gated-строк — есть ещё один HUD-путь (искать соседей
`0x1413B42E0`/`0x1413B4670` семьи).

**Игра запущена (PID 22372 role A / 11672 role B), новый DLL заинжекчен.**
IDB размечен (`apply_labels OK: 864 functions`, `save_database returned True`),
MCP-копия обновляется.

---

## Turn note (2026-07-04, Cursor — crosshair dup CONFIRMED fixed; native-fire краш → disabled; sniper-scope puppet vanish открыт)

**Результаты рантайма (PID 22372/11672):**
1. **Дублирование прицела — CONFIRMED FIXED** (screen-index гейт, `RETICLE-HUD
   gated: screen!=0` в логе).
2. **Краш приёмника при стрельбе — причина найдена, пофикшено:** `EXCEPTION
   rva=0x152CB38` = `ProjectileBulletInstant::ProbeKick` (`sub_14152C9B0`), сразу
   после `FIRE-RELAY native seq=3 art=0xc3181ebd`. Native `PlayerFireController::Fire`
   replay на кукле возвращает OK внутри SEH, но отложенный projectile-probe на
   след. тике падает ВНЕ `__try` на мусорном указателе из sender-aim-векторов в
   `packet.fire_args`. **Фикс:** `g_native_fire_relay = false` — вся стрельба через
   button-pulse (стабильно, использует собственное состояние куклы).
3. **Хлоя пропадает на копии 2 при снайперском прицеле копии 1 — ОТКРЫТО
   (HYPOTHESIS):** только винтовка `0x8075397d`. Вероятно scope-камера/модельный
   cull при scoped-ADS куклы, отдельно от HUD-гейта. Следующий ход: трейс
   sniper-ADS → cull модели screen-1, гейт этого пути, ЛИБО pose-only для снайпера.

**Собрано+заинжекчено (PID 19868 role A / 26028 role B).** Прогноз теста:
стрельба любым оружием больше не крашит приёмник (`FIRE-RELAY button`, нет
`FIRE-RELAY native`, нет ProbeKick); прицел не дублируется. Снайпер-исчезновение
Хлои — известный открытый баг.

---

## Turn note (2026-07-04, Claude — RUNTIME: block-1 fix REJECTED, корень = состояние марионетки, HANDOFF) [SUPERSEDED выше]

**Тест (PID 14440 отпр. / 8524 приёмн.):** прицел всё ещё дублируется, снайперский
прицел дублируется **без виньетирования**, «целится/стреляет не в ту сторону».
→ гипотеза «утечка = биты aim/fire/look в общем блоке 1» **REJECTED** (детали и
адреса в `initial-findings.md`, запись "Runtime 2026-07-04 … REJECTED").

**Что доказал лог:** фикс блока-1 работает как задумано (`REMOTE-ACTIONS branch=0x9`
без 0x200/0x800; `JOYPAD3 current=0x209` только внутри конверсии; блок восстановлен),
НО симптом остаётся → утечка НЕ через биты ввода. Три установленных факта:
1. Обе копии стреляют винтовкой `art=0x8075397d` (не пистолет) → снайперский оверлей =
   марионетка целится винтовкой.
2. `JOYPAD3 right=(0,0)` всегда — реле правого стика (look) НЕ доходит до джойпада 3 →
   направление марионетки только из camera-facing → «не туда». Отдельный баг.
3. `g_PlayerHandleForHudPublish 0x143EEBE80` в обеих копиях = ОДИН объект (лок. игрок);
   reticle-менеджер `0x140C193A0` читает его, а не марионетку.

**РЕШЕНО чтением живой памяти (прицел удержан):** локальный игрок и его per-screen
HUD корректны — скоуп течёт через **глобальный singleton `ss-hud-sniper-reticle-manager`**.
- `g_PlayerHandleForHudPublish 0x143EEBE80` → `0x10e0a2d180` = **локальный слот 0
  (Nadine)**, а не марионетка (`live1=0x10e0ddf600`). Значит подмены хэндла нет.
- Марионетка (джойпад 3 / слот 1) держит `current=0x209`, `right=(0.028,0.286)` —
  это ОНА в скоуп-прицеле с винтовкой. (look-реле, кстати, доходит при прицеле;
  прошлый `right=(0,0)` был из окна без прицела. Неверное направление =
  деривация look/facing, отдельный баг.)
- `is-scope-visible?` (`sub_140C177F0`) читает глобальный менеджер через реестр
  `dword_146466FE0`, поля `mgr+168/+188` — НЕ per-player. Марионетка входит в
  scoped-ADS → глобальный scope-visible → полноэкранный оверлей на вьюпорте
  приёмника (без виньетки, т.к. это сырой оверлей менеджера, а не композит ADS
  локального игрока).

**Фикс на следующий ход (фальсифицируемо):** марионетка не должна поднимать
глобальный scope/ADS. Приоритетно:
1. Релеить прицел как ПОЗУ, не вооружая scoped-ADS/zoom оружия марионетки.
   Прогноз: поза прицеливания есть, скоупа на приёмнике НЕТ. Если скоуп остаётся —
   менеджер поднимается самим aim-press → пункт 2.
2. Гейт `is-scope-visible?`/менеджера: игрок с `screen_index != 0` (марионетка)
   не может поднять scope-visible.
НЕ трогать биты ввода (блок-1 REJECTED). look/направление — чинить отдельно.

**Игра запущена (PID 14440/8524), DLL заинжекчена** — `scanner/build/tll_read.exe --pid N <addr> <len>`.

---

## Turn note (2026-07-04, Claude — crosshair dup гипотеза: shared input block 1) [SUPERSEDED выше]

**User test result:** прицел всё ещё дублировался на копию-приёмник при прицеливании
пира → гипотеза «scrub локального джойпада достаточно» **REJECTED**.

### Root cause (STATIC, реестр: "Input Block Selector Bus")
- `FrameState_ConstructJoypadSlots 0x14179D780` задаёт селекторы блоков
  (`Joypad+0xF0`) = `{0,1,0,1,0,1}`: **слот 3 (джойпад марионетки) читает блок 1 —
  тот же, что слот 1 = штатный лэйн локального игрока №2** (mode 3 у слотов 0/1).
- Все наши записи aim `0x200`/look в branch слота 1 и в source хука ScaleCopy
  (dest=3) оседали в общем блоке 1 → движок читал их как «локальный игрок 1
  целится» → strafe-камера/ретикл на локальном вьюпорте приёмника.
  (`is-aiming` HUD-publish `0x1413571B0` требует камеру `CameraControlStrafe`
  vtable `0x143054560` на экранном маршруте — см. реестр.)

### Fix (в этом коммите, build OK)
- `hooked_input_scale_copy` (dest=3): **сэндвич** — сохранить блок-1 `+0x518` и
  правый стик `+0x08/+0x0C`, записать peer aim/fire + look, вызвать оригинал
  (джойпад 3 успевает вывести command state), **восстановить** блок. Peer-ввод
  существует на общей шине только внутри конверсии.
- `drive_remote_slot1_actions_tick`: look_stick больше не пишется в branch
  (`+0x1C/+0x20`); aim/fire срезаются из branch-маски `branch1+0x10` (едут только
  через сэндвич). Движение/gate-биты не тронуты. Скраб локального пэда оставлен.

### User test checklist (next turn)
1. Обе копии с новой `tll_teleport_hook_v2.dll`, ≥10 c после спавна.
2. Chloe держит прицел → на копии 2: поза прицеливания у марионетки, лог
   `JOYPAD3-ANALOG ... current=` содержит `0x200`, и **нет** ретикла/зума у Нэдин.
3. `REMOTE-ACTIONS branch=` больше не содержит `0x200/0x800` — это норма.
4. Выстрелы: как раньше (FIRE-RELAY native / пульс) — путь не менялся, но
   проверить, что пистолетный выстрел доходит (fire теперь только через сэндвич).
5. Если ретикл вернулся → гипотеза остатка в блоке 1 неверна; следующий кандидат —
   прямой читатель Joypad-объекта слота 3 в HUD/камере (гейт на стороне читателя).

## Turn note (2026-07-03, Cursor — fire dedup + load guards + aim anim restore, HANDOFF)

**Зафиксировано в этом коммите.** Следующий ход: user-test aim pose + crosshair isolation.

### Fire relay (CONFIRMED fixes)
- **Double-fire crash:** sustained peer `0x800` in `StatePacket` reached Joypad 3 while
  native `FIRE-RELAY` also fired → `rip=0x0`. Fix: `remote_puppet_actions_from_peer`
  strips fire from state; fire only via `FirePacket`; `suppress_puppet_fire_buttons(400)`
  after native success; block `try_start_fire`/`check_shot_readiness` on puppet while
  suppressed.
- **Load crash (no shot on copy 2):** `GiveWeapon` vtable `+0x158` null during spawn →
  `EXCEPTION rip=0x0` art `0x33E29A1B`; native fire too early after `gunOk=1`. Fix:
  `puppet_weapon_give_vtable_ready`, 5 s puppet warmup, 5 s give-fail backoff,
  `puppet_native_fire_allowed` (+1 s after confirmed attach) gates native replay.

### Aim animation (restored this commit, needs user test)
- **Regression cause:** `kRemoteViewportButtonMask` incorrectly stripped aim `0x200`
  along with scope HUD bits → no sustained aim-press on puppet joypad → no aim pose.
- **Fix:** split masks — relay aim `0x200` + peer `look_stick` on branch `+0x1C/+0x20`;
  strip only scope bits (`0x10000..0x80000`); `scrub_local_viewport_aim_hud_leak()`
  clears aim/scope from **local** joypad when peer aims but local player does not
  (prevents crosshair duplication without killing puppet anim).
- **Still gated:** fire `0x800` never sustained from state packet (by design).

### User test checklist
1. Load both copies, wait ≥10 s after level spawn.
2. Chloe hold aim (no shot) → remote Chloe aim pose on copy 2; **no** crosshair on
   Nadine viewport; log `REMOTE-ACTIONS branch` should include `0x200`.
3. Chloe fire 3–5 pistol shots → copy 2 survives; one `FIRE-RELAY native` per shot.
4. If crosshair dup returns → tune `scrub_local_viewport_aim_hud_leak` only (do not
   re-strip `0x200` from puppet).

## Turn note (2026-07-03, Cursor — copy-2 crash on Chloe pistol fire)

- **CONFIRMED (logs PID 24340/9980):** copy 2 (role B) died right after first relayed
  pistol shot. Last copy-2 lines: `REMOTE-ACTIONS held=0x809` → `FIRE-GATE
  input/readiness result=1` on remote player `0x10e0e13f80` →
  `FIRE-RELAY native seq=1 art=0x33e29a1b hands=1`; copy 2 then stops logging while
  copy 1 continues.
- **CONFIRMED:** same session had earlier `EXCEPTION rip=0x0` on pistol art
  `0x33E29A1B` during `HELD-WEAPON-APPLY` / GiveWeapon (SEH survived); final crash
  correlates with **double delivery** — native `safe_replay_player_fire` plus peer
  fire button `0x800` still armed on Joypad 3 in the same frame window.
- **Root cause:** `apply_queued_fire_events` native success still set
  `g_fire_button_hold_until_ms`, and `drive_remote_slot1_actions_tick` also forwarded
  peer `input_buttons` fire (`0x809`) → second native `PlayerFireController::Fire`
  on the remote player → `rip=0x0` AV.
- **Fix:** on native relay success, call `suppress_puppet_fire_buttons(250)` instead
  of extending button hold; strip `0x800` in `drive_remote_slot1_actions_tick` and
  `Input_ScaleCopyVirtual` while suppressed; require `puppet_weapon_sync_ready`;
  use packet `controller_offset` + weapon-art match before replay; clear suppress on
  native-fail so button fallback still works.
- **Runtime:** built `scanner/build/tll_teleport_hook_v2.dll` OK.
  **User test:** Chloe pistol shot → copy 2 survives; log shows one `FIRE-RELAY native`
  and **no** `FIRE-GATE` fire-press on remote player in the same 250 ms window.

## Turn note (2026-07-03, Cursor — rifle fire relay drop + aim direction fix)

- **Root cause:** copy 2 (role B / Nadine local) also logged `PLAYER-FIRE` and sent
  fire packets with rifle art `0x8075397d` while Chloe held pistol — receiver
  button fallback armed rifle → sniper scope overlay after first pistol shot.
  Button-only relay ignored `FirePacket.fire_args` aim direction.
- **Fix:** `send_fire_packet` / `hooked_player_fire` gated to role A + retained slot 0
  only; `coop_fire_packet_matches_peer_weapon` drops rifle packets when peer holds
  pistol; rifle give/switch blocked when peer has pistol; `apply_peer_aim_facing_to_player`
  before button pulse; aim `0x200` armed when peer aims or recent aim + gun out;
  native pistol replay enabled (`g_native_fire_relay`) with full `0x100` fire_args via
  `puppet_fire_ready`.
- **User test:** Chloe pistol first shot → no sniper scope on Nadine; log should show
  `FIRE-RELAY drop: rifle art but peer holds pistol` for stale rifle packets;
  `FIRE-RELAY native` for pistol when ready; muzzle direction matches Chloe crosshair.

## Turn note (2026-07-03, Cursor — block+0x518 button send fix)

- **CONFIRMED (logs PID 10040/20400):** `REMOTE-ACTIONS held=0x0` while Chloe aims —
  sender read Joypad `+0xE0` (layer bits like `0x20000`) instead of resolved block
  `+0x518` (command descriptors: aim=`0x200`, fire=`0x800`). Receiver synthesized only
  brief `branch=0x200` from `g_fire_aim_hold_until_ms` (400ms post-fire), not sustained
  aim-press → **no Nadine aim animation** while Chloe holds aim.
- **CONFIRMED:** `JOYPAD3-ANALOG right=(0,0)` every frame — mouse look never landed in
  `look_stick`; `camera_quat` path existed but aim-press never stayed active without
  correct held mask in the packet.
- **CONFIRMED:** `capture_held_weapon_art_id` reported rifle `0x8075397d` while pistol
  drawn in hands (slot 1) → peer sent rifle art intermittently; pistol visual switch lagged.
- **Fix:** sender now packs `input_buttons` from `joypad_resolved_input_block+0x518`;
  receiver `Input_ScaleCopyVirtual` (dest=3) sustains aim `0x200` while `peer.gun_state==1`
  or fire-relay hold; applies peer `look_stick` to source block axes; pistol-in-hands
  (slot 1) wins over rifle active-weapon query.
- **Runtime:** built + injected PIDs 11616/21412. **User test:** hold aim on Chloe → log
  should show `held=0x200` (not `0x0`) on receiver `REMOTE-ACTIONS`; Nadine aim pose;
  pistol switch → `HELD-WEAPON-SEND art=0x33e29a1b`; shoot direction follows camera.

## Turn note (2026-07-03, Cursor — Nadine aim direction + aim anim sync)

- **CONFIRMED (logs PID 4480/16108):** `JOYPAD3-ANALOG right=(0,0)` on every frame while
  Chloe fires with `src=0x90000` — aim/fire **buttons** reach Nadine's Joypad 3, but
  **camera/right-stick look was never relayed**. Nadine used her local CameraManager yaw,
  so shots and aim pose did not match Chloe's crosshair.
- **CONFIRMED:** weapon-mismatch gate stripped **both** aim (`0x200`) and fire (`0x800`) when
  `puppet_can_fire_art` failed (`WEAPON-CLONE armed=0`). That blocked sustained aim-press
  animation even while Chloe held aim.
- **Fix (StatePacket v7):** sender now packs `camera_quat[4]` (Chloe CameraManager(0) @+0x1B0,
  flag `0x100`) plus `look_stick` right-stick pair; receiver applies peer camera in
  `drive_remote_slot1_actions_tick` and `Input_ScaleCopyVirtual` (dest=3), forwards right
  stick to branch `+0x1C/+0x20`, relays aim continuously while peer holds `0x200` (fire still
  gated on weapon match), blends body quat harder while peer aims.
- **STATIC:** shot direction should follow peer camera quat once both instances run v7 DLL.
  Aim animation needs sustained `0x200` + gun-out; rifle draw on Nadine may still lag
  (`HELD-WEAPON-APPLY`) — separate from look relay.
- **Runtime:** built `scanner/build/tll_teleport_hook_v2.dll`; injected PIDs 10040/20400.
  **User test:** Chloe aim (hold) → Nadine aim pose; Chloe shoot → Nadine muzzle direction
  matches; log should show non-zero `right=(…)` on JOYPAD3-ANALOG and peer camera applied.

## Turn note (2026-07-03, Cursor — rifle puppet give/switch unblock)

- **Root cause (prior run):** `puppet_weapon_give_allowed` blocked rifle
  `0x8075397d`; `puppet_weapon_switch_allowed` blocked `slot<=0` and rifle art.
  Peer rifle packets showed `HELD-WEAPON-APPLY slot=-1 gave=0 select=0` — puppet had
  no rifle inventory; fire relay still worked via buttons.
- **Fix (this build):** allow rifle give/switch (slot 0); `ensure_puppet_has_peer_weapons`
  pre-gives coop arts from peer raw-id snapshot (flag `0x40`); apply path gives + re-resolves
  slot when missing; rifle-out forces switch even when `weapon_selected_mode==0`; sender
  prefers rifle art when pistol hand-attached but active/requested slot is rifle.
- **Runtime:** build OK (`tll_teleport_hook_v2.dll`); two instances injected (PIDs vary).
  **Needs user test:** pick up rifle → expect `HELD-WEAPON-APPLY art=0x8075397d slot=0
  gave=1 select=1` on puppet side; visual should show rifle (Nadine holster overlap may
  still apply).

## Turn note (2026-07-03, Cursor — weapon display: hand-attached art vs GetActiveWeapon)

- **Likely bug:** `capture_held_weapon_art_id` used `Player_GetActiveWeapon` (+0x8C0)
  first; it can keep reporting rifle `0x8075397d` while pistol is physically attached
  (slot 1 / `0x1410A49E0`). Peer then never switches puppet draw to pistol.
- **Fix:** sender/receiver now prefer `capture_weapon_selected_mode` (hand-attached
  slot) → `get_weapon_from_slot` → art@+0x860; holster previous attached slot before
  `SwitchToWeapon` to new slot; holster on `peer_gun==0`.
- **Still parked:** Nadine look `0xd27c074f` holster prop-attach — holstered rifle
  mesh may overlap pistol even when logical slot is correct (asset-level).

## Turn note (2026-07-03, Codex — fire button reaches Joypad, internal gates next)

- **CONFIRMED:** the received `0x90000` mask reaches the clone's real Joypad 3:
  `REMOTE-ACTIONS branch=0x90000` and then
  `JOYPAD3-ANALOG src/pressed/current=0x90000`. Network delivery and per-slot
  input application are no longer candidates for the missing shot.
- **CONFIRMED:** pistol switching completes asynchronously. The first frame logged
  `requested=1 attached=0`; a later frame logged `requested=1 attached=1` without
  another peer state change. The old early return therefore left `gunOk=0` after
  successful attachment. The receiver now keeps checking until the expected slot
  is physically attached, then arms fire readiness without repeatedly switching.
- **STATIC:** the remaining chain is
  `PlayerFireController_UpdateInputAndFire 0x140E28A30` ->
  `PlayerWeaponController_CanUseWeapon 0x1410B15A0` ->
  `CheckFireInput 0x140E2ACA0` -> `CheckActionExclusions 0x140E2ADF0` ->
  `CheckShotReadiness 0x140E2B220` -> native `Fire 0x140E2C730`.
  Read-only return diagnostics were added to the two final fire-controller gates.
- **CONFIRMED / REJECTED:** the remote pulse produced no `FIRE-GATE` line at all,
  while local shots produced `input=1`, `readiness=1`, then native `PLAYER-FIRE`.
  The break is therefore before the fire controller, not in weapon readiness.
- **STATIC + live data:** authored command records resolve `aim-press` to mask
  `0x200/type 4`, and all `fire-hold/press/release` forms to mask `0x800` with
  types `2/4/8`. The old synthetic masks `0x80000/0x10000` were unrelated.
  Additionally, `hooked_player_input_state_update` cleared the generic press latch
  before the separately scheduled fire controller queried Joypad `+0xD0`.
  Fire now uses `0x800` and remains latched only until `CheckFireInput`; the 400 ms
  helper hold keeps only aim `0x200`, preventing manufactured repeated shots.
- **Next prediction:** remote pulse `0xA00` yields Joypad `pressed/current=0xA00`
  and then `FIRE-GATE input press=1`. Its return and the readiness return identify
  any remaining native rejection.

## Previous turn note (2026-07-03, Cursor — fire relay PAUSE, button path partial)

**Статус: ПАУЗА.** Краши устранены; визуальная синхронизация выстрела на кукле **ещё нет**.

### Что подтверждено рантаймом (PID 10428/15952, 15028/9984, …)
- **CONFIRMED:** `FIRE-RELAY button seq=N pulse=0x90000` — UDP-пакет доходит, button
  fallback срабатывает (ранний `return` для пистолета при `!fire_ready` убран).
- **CONFIRMED:** `REMOTE-ACTIONS branch=0x90000` на 1–2 кадра — `drive_remote_slot1_actions_tick`
  получает aim+fire в `branch1+0x10`.
- **CONFIRMED:** `HELD-WEAPON-APPLY gave=1 select=1 slot=1 gunOk=1 hands=1` после
  `safe_give_weapon_art_to_puppet` + `SwitchToWeapon` (пистолет `0x33e29a1b`).
- **CONFIRMED:** кукла slot-1 привязана к **joypad 3** (`PLAYER-INPUT-BIND joypad_index=3`).
- **CONFIRMED:** `WEAPON-CLONE ids=[0,0,…]` на кукле до give; `GiveWeapon` vtable `+0x158`
  иногда `rip=0` (SEH ловит, игра не падает).
- **REJECTED (this path):** `g_native_fire_relay=false` — native `PlayerFireController::Fire`
  на кукле давал `EXCEPTION rip=0x0` (пистолет) и краш на винтовке.

### Что НЕ работает (блокер)
- **`block3 btn=0x0`** даже при `branch=0x90000` — кнопки не доходят до resolved input
  block, который потребляет Player. Диагностика раньше смотрела `0x66E4220+0x530`
  (block[1]); кукла на joypad 3 → нужен `+0x530*3`. Добавлен `push_joypad_block_buttons`
  (прямая запись `block+0x518` held / `+0x508` press) — **ещё не дало визуала**
  (`block3 btn` остаётся 0 в логе).
- **`puppet_can_fire_art` часто false:** `active=0x0` или `mode=0` при holstered pistol
  на кукле; винтовка (`0x8075397d`) blocked для `SwitchToWeapon` slot≤0 на кукле.

### Следующий ход (после паузы) — статика перед игрой
1. **Проверить штатную границу `Input_ScaleCopyVirtual`:** статика подтвердила, что
   `0x1402893D0` сам переносит DWORD `branch+0x10` в resolved block и затем вызывает
   `Input_ScaleCopyVirtual`. Прямая запись Cursor в block удалена как перезаписываемая;
   QWORD-запись в branch исправлена на DWORD (раньше портила stick X по `+0x14`).
2. Если импульс доходит до Joypad `+0xD0`, проверить запрет команды fire для куклы и
   наличие активного fire-controller, а не вызывать per-slot apply второй раз.
3. **Альтернатива:** снова осторожно native fire только при `gunOk=1 hands=1 select=1
   mode>0 active_art match` (не `hands=20`, не rifle).
4. **Винтовка** — отдельная задача (slot 0 blocked на кукле).

### Сборка / инжект
- DLL: `scanner/build/tll_teleport_hook_v2.dll`
- Запуск: `taskkill /F /IM tll.exe` → две копии → ~18s → `tll_injector.exe --pid PID -- tll_teleport_hook_v2.dll`
- Peer: `reports/coop_peer.txt` = `127.0.0.1`
- Лог: `reports/locomotion_hook.log`

## Turn note (2026-07-03, Cursor — fire relay per-character controller offset)
- **Root cause (prior runtime):** `FIRE-RELAY learned controller offset=0x15cf30` (Chloe) /
  `0x15adb0` (Nadine) logged on send, but **zero** `FIRE-RELAY applied` — receiver used
  global `g_player_fire_controller_offset` from local slot-0 only; puppet is the *other*
  character so `get_player_fire_controller` failed silently.
- **This turn:** `FirePacket` v2 adds per-shot `controller_offset`; receiver
  `resolve_fire_controller(player, preferred)` tries packet offset then known
  `{0x15cf30, 0x15adb0}`; `apply_queued_fire_events` switches weapon by art-id before
  replay, sanitizes pointer QWORDs in `FireArgs[0x20..]`, logs throttled skip reasons.
  v1 wire compat preserved (`controller_offset=0` → fallbacks). **Build OK.**
  **Runtime test pending** — need in-campaign fire with both instances injected.

## Turn note (2026-07-03, Cursor — held weapon draw/select fix)
- **Runtime CONFIRMED (prior build):** `GiveWeapon` puts weapon on puppet's back;
  `read_gun_state` / `safe_request_gun_state` always fail (`gun=-1`, `gunOk=0`);
  `weapon_selected_mode` (manager `vtable+0x78` = controller `+0x130` in-hands slot)
  changes on peer switch but was never applied on receiver.
- **This turn:** sender `infer_gun_state()` — fallback when `+0x15C8` throws: aim bit
  `0x80000` and/or nonzero in-hands slot. Receiver: `find_weapon_slot_by_art_id` +
  `PlayerWeaponController::SwitchToWeapon` `0x1410A1C10` (vtable `+0x40`) on mode/
  art/gun change; logs `select=` and `slot=`. **Runtime test pending.**

## Turn note (2026-07-03, Cursor — held weapon sync v6)
- **Held-weapon path implemented** (`StatePacket` v6, flag `0x80`): sender captures
  active `Weapon+0x860` via `Player` vtable `+0x8C0` (fallback: selected manager slot)
  plus `gun_state` from `Player+0x15C8` gun-state controller. Receiver applies ONE
  `GiveWeapon` per art-id change (not full inventory spam) then requests
  `GunHolstered`/`GunOut` via `+0x15C8 -> +0x58 -> +0xA0`. Logs:
  `HELD-WEAPON-SEND` / `HELD-WEAPON-APPLY`. Old F6 authored-give + NetSnapshot
  publish remain OFF. **Runtime test pending** — Nadine holster attach may still
  break visuals even if inventory path succeeds.

## Turn note (2026-07-03, Cursor — continues Claude)
- **Claude last commit:** `Nadine weapon: final root cause = look lacks player holster
  prop-attach config` — holster issue **PARKED** at asset level; weapon-give and
  publish remain gated off (F6).
- **This turn:** added UDP `FirePacket` relay — sender hooks
  `PlayerFireController::Fire`, learns `Player`→controller offset from first local
  shot, replays captured `FireArgs` on remote retained `Player` each net tick.
  Recorded as **HYPOTHESIS** in `initial-findings.md`; runtime test pending.
- **Still open P0:** jump/roll/melee via button latch (needs in-game verify);
  anim mirror blocked by campaign art-group wall; Nadine holster display.

## Turn note (2026-07-03, Claude)
- **Grenade-on-move: FIXED & CONFIRMED.** The peer's clone threw grenades while
  the peer only ran = synthetic `0x500` gate OR-ed into `branch1+0x10` (the clone
  joypad HELD mask; press edges derived from it by real-input branch `0x1402893D0`).
  Locomotion is stick-driven and does not need it. Removed; F7 restores it.
- **Weapon replication (Codex authored-give): GATED OFF (F6), default baseline.**
  It spilled weapons (the Nadine clone can't attach given weapons → they drop) and
  can't switch the held weapon. Publish path gated too. Stable non-spilling co-op is
  the default now.
- **Nadine weapon display: root cause CONFIRMED, look-swap fix DISPROVEN.** The
  appearance-hash swap (`hooked_appearance`, vtable[92]) changes the SKELETON +
  joint-override + prop-attach via the look builder `0x141BB4280`, not just the mesh,
  so weapons mis-attach (pistol renders as rifle, empty back, crooked aim). Engine
  StringId hash confirmed the domain (chloe 0x3f334972 matches) but real
  nadine=0xc7e2539b BLACK-SCREENS, and every other look variant breaks the skin; only
  the legacy `0xd27c074f` renders Nadine (with broken weapons). So the fix is NOT a
  look hash — it must correct the weapon attach ON 0xd27c074f (deep, open).
- **Repro tooling (single copy, no peer):** F5 = force local player to Nadine
  (`g_force_local_nadine`); F4 = cycle Nadine look candidates; F8 = WEAPON-REGION/
  SCAN/GUN-STATE dumps; F6 = authored weapon-give on/off; F7 = legacy move gate.
- **Nadine weapon — root cause fully nailed, but fix is asset-level (not a quick code
  hook).** ALL weapons pile at the HAND overlapping (pistol pokes out of the rifle,
  etc.); the held weapon is correct but HOLSTERED weapons have no valid attach and fall
  back to the hand. Per-frame update `sub_1410EC230` → joint resolver `sub_1410B5560`
  resolves only standard humanoid joints (targSpine/targChest/…, all present on Nadine),
  so it is NOT a missing joint. The holster is "standard joint + per-weapon OFFSET from
  the look's prop-attach CONFIG", and Nadine's companion look does not define the PLAYER
  weapon-holster prop-attach configs → fallback to hand. That config is asset data loaded
  with the look; no clean runtime hook found (apReference fns are anim-spec parsers;
  player-hide-weapons is script-command indirection). Realistic options: per-weapon draw
  suppression (weapon-draw list still unfound), inject the holster prop-attach config into
  Nadine's look (deep asset-struct RE), or player-2 = Chloe. Details in memory
  `nadine-weapon-skeleton`. Dedicated multi-session effort.



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

Remote movement is now the protected stable baseline. Both copies passed visual
testing with smooth running and correct facing; final horizontal residuals were
`0.043 m` and `0.071 m` without a hard snap. Sender world movement plus bounded
correction is applied immediately after `0x140C9FE4F`, before the real consumer.
Native locomotion owns position and facing while moving; network quaternion
blending runs only at idle. Do not replace this with exact `+0x130/+0x240` writes.

Player layer `4<->5` collision, both bump probes, and authoritative root pinning
are parked because that sequence caused physics divergence and packet-rate jitter.
The next blocker is full action/state reproduction: traversal, jump/roll, weapons,
firing effects, melee and interactions. Player collision returns later as an
isolated task against this movement baseline.

The present `StatePacket` v5 carries position, quaternion, velocity, both stick
pairs, the complete 64-bit button mask with press/release edges, health, and the
native `NetSnapshotWeapons` region. Weapon appearance publication is implemented
on the receiver; sender capture was hard-disabled because `FillWeaponState` throws
on the retained-table proxy. Static pass (2026-07-03): capture must use the live
resolver player (`+0x1D88` manager). The full helper still cannot be called, so
the sender now reproduces only its campaign-live inventory prefix. The proposed
attachment bypass at `0x14023AEA0` was rejected statically: that predicate gates
only an optional refresh, not the transform writes. Runtime test is pending.

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
## 2026-07-10: puppet animation tick confirmed; pickup request sequence fixed

**CONFIRMED (two-instance read-only Exp4):** both slot 0 and the network puppet
have live `Player+0x1A8` `FgAnimData` objects. Their retained owner pairs resolve
back to the expected Player, `FgAnimData+0x10 == Player+0x1A0`, runtime payload
`+0x230` is non-null, and phase bits `flags & 0x38` are all set. Thus the animation
manager does schedule the puppet and dispatches Player vtable `+0x410` to
`Player_Update` `0x1410EC230`. The old "second Player does not tick" explanation
is rejected.

**Pickup correction:** the prior fallback mislabeled `0x141C9A360` as
`Player_StartInteractAction`. It only queries availability through control vtable
`+0x8`; it never submitted the interaction. The relay now matches stock dispatch:
availability `== 1` -> `WeaponPickupInteractControl::RequestInteract` vtable
`+0x28` (`0x1408C8A40`) -> `Player_PublishRequestedInteract` (`0x140C87D70`). The
existing puppet-side `OnInteract` inventory suppression remains active so this
authors the animation without applying the weapon twice. Visual confirmation of
the pickup bend is the next runtime check.
## 2026-07-11: NetEventEmitGrenade receiver path found; relay build ready

The last grenade "descriptor/spawn ceiling" is superseded. `0x1429DCCE0` was the
class-name string, not a vtable. RTTI resolves `NetEventEmitGrenade` vtable
`0x1429FF448`; its `Execute` method `0x140BE63A0` already performs the complete
`ProcessSpawnInfo` + descriptor materialization required on the receiver.

The sender constructor `0x140BE5D30` and symmetric serializer/deserializer
`0x140BE6260/0x140BE6140` prove that event bytes `+0x18..+0xB5` are the portable
POD recipe. A versioned UDP `GrenadeEventPacket` now relays those bytes, rebuilds
the event with default ctor `0x140BE5C80`, and executes it on the game thread.
Peer-spawned `ProjectileGrenade::Init` is echo-suppressed.

First runtime attempt reached a real local throw but logged `built=0`, before any
packet send. The next build separates `ProjectileGrenade_ResolveOwner` from the
event constructor and logs the owner pointer, distinguishing resolver failure from
constructor ABI/state failure. Two copies (PIDs 26696/7396 at handoff) are injected
and waiting for one in-save grenade throw.
## 2026-07-11: grenade Execute fault isolated; interact callsite probe installed

**Descriptor/process wall:** native `NetEventEmitGrenade` POD relay now reaches
`built=1 sent=1` and peer `RECV`. Campaign-unsafe
`WeaponArtId_ToNetworkIndex` was removed from capture; raw grenade art
`0x0B8BD6D6` is transported separately and temporarily mapped through fixed index
18 during receive. Setup stages 1..3 pass; runtime stopped at `EXEC stage=4`, before
peer `ProjectileGrenade::Init`, proving the remaining exception is inside
`NetEventEmitGrenade_Execute 0x140BE63A0`. Current build logs exact exception RVA.

**Puppet-controller wall:** four stock transitions to
`Player_InteractDispatcher 0x140C85D20` were mapped: tail-jmp `0x140C8829B` and
calls `0x140CD4526`, `0x140D74893`, `0x140EC7CBA`. All encode screen index in
`RCX = 0x143EC4EE0 + 0x35C0*screen`. A near relay now forwards them through an
ordinary ABI wrapper and calls the original entry normally, avoiding the broken
partial-prologue trampoline class. Current injected PIDs 29520/30660 report the
four-site probe installed; runtime screen 0/1 samples await save load.

**Runtime result:** screen 0 dispatched repeatedly; screen 1 never did. The puppet
is conclusively skipped above `Player_InteractDispatcher`, so the next target is
the common decision-controller scheduler, not dispatcher internals.

Grenade peer Execute fault was captured at RVA `0xA97393`, exception
`0xC0000094`, in the empty-registry ownerless lookup `sub_140A97380`. The synthetic
event now selects the bounded NetCharacter-owner branch with an unmatched id; that
branch safely continues to descriptor materialization without the division.

## 2026-07-11: grenade v2 arc fix — relay pos/vel + per-frame peer re-apply (build OK, runtime pending)

**Symptom:** peer spawns grenade but it detonates instantly (no visible arc). Root cause:
serialized `NetEventEmitGrenade` POD carries spawn transform but not live launch velocity.

**v2 fix (uncommitted, built):**
- `GrenadeEventPacket` v2 adds `pos[4]` + `vel[4]` (`kGrenadeEventVersion = 2`).
- Sender waits for `|vel|² > 0.25` before `GRENADE-EVENT-SEND` (avoids zero-vel packet).
- Receiver after successful `EXEC`: one-shot `seh_force_grenade_velocity` on peer-spawned
  object (`g_peer_spawned_grenade` from Init hook during `g_applying_peer_grenade`).
- **New:** `reapply_peer_grenade_arc()` forces pos/vel each frame for ~1.2s (physics was
  overwriting the one-shot write).

**Log tags:** `GRENADE-EVENT-SEND/RECV` (with pos/vel), `GRENADE-EVENT-EXEC` (`arcForced`,
`spawned`), `GRENADE-ARC-FORCE`, `GRENADE-TRACK`.

**Runtime test (injected PIDs 20068 / 9300):** load same campaign save on both → throw one
grenade on host → peer should show `GRENADE-ARC-FORCE` + changing `GRENADE-TRACK` pos over
~750ms. Success = visible arc on peer; failure = still instant pop → next step is fuse sync
(`SendNetEventGrenadeStartFuse`) or hook physics integrator.

## 2026-07-11: grenade v2 arc fix — root cause from logs (position pin + zero fuse time)

**Runtime log (PIDs 20068/9300) proved relay path works:** `built=1 sent=1`, peer `EXEC
ok=1 arcForced=1`, but `GRENADE-ARC-FORCE` showed **frozen pos** at spawn every frame while
host `GRENADE-TRACK` advanced. Cause: `reapply_peer_grenade_arc` rewrote `pos@+0x240` to the
initial relayed coords each tick, pinning the object in place (instant ground hit / pop).

**Second bug:** manual payload build did `memset(event+0x24,0)`; native
`NetEventEmitGrenade_BuildFromProjectile` stores fuse/spawn time there from `grenade+0x3F2 *
g_EngineClockToSeconds`. Zero/stale timestamp likely made Execute treat fuse as elapsed.

**v3 fix (built):**
- Bootstrap once: `pos+vel`; per-frame sustain: **vel only** (`seh_write_grenade_launch_state`).
- Sender: copy fuse timestamp from `grenade+0x3F2`; receiver: re-stamp `event+0x24` to peer
  engine clock before Execute.
- Peer diagnostics renamed to `GRENADE-PEER-TRACK` (pos should now change).

## 2026-07-11: grenade v4 — spawn transform patch (user: still pops at throw origin)

**User retest after v3:** peer grenade still explodes where it was thrown (no arc).

**Static root cause (confirmed via `NetEventEmitGrenade_Execute` @ `0x140BE63A0`):**
Execute materializes `ProcessSpawnInfo` from **`event+0x30`** (`vmovups [r14+30h]`).
Native sender `BuildFromProjectile` fills that from `refresh_transform(grenade+0x100)` —
hierarchy/local throw origin, **not** in-flight world pos `+0x240`. Post-spawn vel write
cannot fix wrong spawn point.

**v4 fix (built, runtime pending):**
- Sender build: write world pos `+0x240` into `event+0x30`; clear mid-flight flags
  `+0x19..0x1B`; relay `quat@+0x140` in packet v3.
- Peer `apply_pending`: `patch_peer_grenade_event_payload()` rewrites payload `+0x18`
  (= `event+0x30`) with relayed world pos + fresh fuse before Execute.
- Post-spawn bootstrap: pos+vel+quat once; per-frame vel sustain; set activation bytes
  `+0x672/+0x675/+0x1522|=4`.
- New log: `GRENADE-EVENT-PATCH` (spawn pos stamped into payload).

**Success criterion:** peer `GRENADE-PEER-TRACK` shows **changing pos**; grenade visible
in flight, explodes at landing point not throw origin.

**If still instant pop:** call `SendNetEventGrenadeStartFuse` Execute on peer post-spawn.

## 2026-07-11: grenade v5 — kinematics stream (logs: peer pos frozen, vel live)

**Runtime log proof:** `GRENADE-PEER-TRACK` pos stuck at send coords while host `GRENADE-TRACK`
advances; vel writes succeed but native physics never integrates on peer-spawned object.

**v5 fix (built):**
- New `GRNK` packet: host streams pos/vel ~16 Hz during flight after initial `GRNE` spawn.
- Peer applies kinematics directly to `g_peer_arc_grenade`; dead-reckon + gravity between samples.
- Post-spawn: `ProjectileGrenade_LaunchSetup` (`0x141532010`) + `SendNetEventGrenadeStartFuse`.
- Logs: `GRENADE-KIN-SEND/RECV`, `GRENADE-FUSE-START`.

## 2026-07-11: grenade v6 — fuse end time + derived transform sync

**Runtime log (v5):** `GRENADE-KIN-RECV` pos tracks host in memory, but user still sees
explosion at throw point. Two bugs:

1. **StartFuse fuse param is END time** (`SendNetEventGrenadeStartFuse` stores it at
   event+0x1C). We passed `get_engine_time_seconds()` (= now) → instant detonation.
2. **Visual/collision use derived transforms** (`NdGameObject` vtable +0x2B8 @
   `0x141BB8320` writes +0x240/+0x250/+0x280 from +0x130). Writing only +0x240 left
   mesh/capsule at spawn while logged pos moved.

**v6 fix (built):**
- `seh_write_grenade_launch_state`: write pos/vel to +0x130/+0x240/+0x250/+0x280
  (+0x270 vel mirror); call `kNdGameObjectRefreshWorldTransformRva`; clear premature
  landed flags on bootstrap.
- `seh_start_peer_grenade_fuse`: `fuse_end = now + 3.0s` (not `now`).

## 2026-07-11: grenade flight sync — **CONFIRMED** (user runtime)

User confirmed v6: peer grenade flies the correct arc and explodes at the landing
point (not throw origin). Working recipe = `GRNE` spawn via
`NetEventEmitGrenade_Execute` + `GRNK` kinematics stream + derived-transform write
(`+0x130/+0x240/+0x250/+0x280`, refresh `0x141BB8320`) + `fuse_end = now + 3s`.

## 2026-07-11: destructible / breakable env sync — STATIC map (next P0 candidate)

Native MP already has full breakable paths in `net-event.cpp` (campaign sender
likely gated like grenades — relay + peer Execute expected).

**Wall / SimpleBreakable damage (shooting, grenades, explosions):**
| Symbol | RVA | Role |
|--------|-----|------|
| `SimpleBreakable::EventHandler` | `0x14088D960` | gameplay origin — builds attack, calls send on host |
| `SendNetEventBreakableAttackExplosion` | `0x140A56430` | sender (224 B), host-gated `!IsHost()` |
| `SendNetEventBreakableAttackExplosionWithResults` | `0x140A56CF0` | sender (304 B) + `BreakableSyncData`, host-gated `IsHost()` |
| `NetEventBreakableAttackExplosion::Execute` | `0x140A56290` | peer: resolve net id `+24`, `PublishAttackEventToTarget` |
| `NetEventBreakableAttackExplosionWithResults::Execute` | `0x140A56910` | peer: + `BreakableSyncData` @event+`0xE0`, `"net-attack-with-results"` |
| `SendNetEventBreakableAttackProjectileWithResults` | `0x140A55960` | bullets (176 B), event type `0x3D` |
| `NetEventBreakableAttackProjectileWithResults::Execute` | `0x140A55580` | peer: sync data @event+`0x60` |

**Post-break state sync (already-broken / late join):**
| Symbol | RVA | Role |
|--------|-----|------|
| `SendNetEventSyncBreakable` | `0x140A655C0` | `SimpleBreakable*` → 96 B `NetEventSyncBreakable` |
| `NetEventSyncBreakable::Execute` | `0x140A65160` | applies `"net-breakable-sync"` via NdGameObject vtable `+0xD0` |
| `sub_140A657A0` | `0x140A657A0` | auto-send sync when shape type `+198==4` and not yet broken |

**Key fields:** `NdGameObject` net id `@+0xD8`; event net id `@+24`; `SimpleBreakable`
destruction table `@+0x238`; Havok layer under `hkdBreakableBody`.

**Related env sync (separate tasks):**
- `SendNetEventSyncChests` — opened chests
- `NetEventPropulsedAttack` — explosive props (`0x140A56EE0` ctor region)
- `SendNetEventAttackExplosion` — generic NPC explosions
- Cvar `enable-destruct-env`

**Planned approach (mirror grenade GRNE):**
1. Probe hook `SendNetEventBreakableAttackExplosionWithResults` `0x140A56CF0` — log
   full 304-byte POD `{netId, attack, BreakableSyncData}` when host breaks wall/grenade.
2. UDP `BRKW` relay (explosion WithResults) + `BRKP` (projectile WithResults, 176 B).
3. Peer game-thread `Execute` `0x140A56910` / `0x140A55580` (not bare `0x140A56290`).
4. Fallback: `NetEventSyncBreakable` Execute `0x140A65160` for snapshot-only path.

**Next step:** probe hook + one host wall-break / grenade-wall runtime log before relay code.

## 2026-07-11 (eve): breakable relay v1 — hooks installed, runtime REJECTED

**Hypothesis:** campaign stone-fence breaks are local-only; force
`SendNetEventSyncBreakable` `0x140A655C0` after host damage and relay the captured
96-byte `NetEventSyncBreakable` blob (`BRKS`) to peer `Execute` `0x140A65160` with
resolver override + `scene_hash`/`world_pos` fallback lookup.

**Campaign damage path (STATIC, disasm):**
| Step | RVA | Note |
|------|-----|------|
| `SimpleBreakable::EventHandler` | `0x88D960` | dispatches by event id |
| Grenade/explosion event `0x32330453` | — | **not** the shoot attack branch @ `0x88E044` |
| `SimpleBreakable_ExplosionDamage` (was `sub_14088E6F0`) | `0x88E6F0` | applies destruction table `+0x238` |
| Post-call notify site | `0x88DC83` | jmp after E6F0 returns |
| Local apply gate | `0x88E044` | `cmp [rsi+1Ch],0` / jnz — alternate path |
| `SendNetEventSyncBreakable` multicast call | `0xA65771` | builds tag `0x0101006B`, net id from `+0xD8` |
| Net resolver | `sub_141622BC0` `0x1622BC0` | table `0x6466FE0`; campaign peer often empty |

**Mod implementation (`teleport_hook.cpp`, `coop_protocol.h` v2):**
- UDP `BRKW`/`BRKP`/`BRKS` (`BreakableSyncPacket` adds `scene_hash@+0x38`, `world_pos`).
- Hooks: send-mask observe; sync-multicast callsite `0xA65771` → `BRKS-SEND`; resolver
  override `0x1622BC0`; local-apply relay `0x88E044`; explosion-damage fn `0x88E6F0`;
  post-site `0x88DC83`; `SendNetEventSyncBreakable` entry `0xA655C0`.
- Deferred sync: `notify_local_breakable_damage` → 250 ms → direct
  `SendNetEventSyncBreakable(0,sb)` → multicast capture → UDP.
- Peer: `BRKS-RECV` → `NetEventSyncBreakable::Execute` with override + hash/pos scan.

**Prologue trap (fixed):** live `tll.exe` 1.4.21058 prologues include leading `REX.W`
(`0x40`) on `push rsi`/`push rbp` and `SendNetEventSyncBreakable` uses
`mov [rsp+30h],-2` not `[rsp+28h]`. Wrong bytes → `install_fire_gate_detour` silently
SKIPPED (mismatch logged only when `g_logging_enabled`). See
`reports/breakable_hook_prologues.txt` vs on-disk bytes.

**Runtime (REJECTED, user test after REX fix):**
- Boot log: all breakable hooks **installed** (both PIDs).
- Grenade kinematics relay still works (`GRENADE-*`).
- **Zero `BRK-*` lines** in `locomotion_hook.log` — notify/capture path never fired in
  logged session OR fence used a path we still miss.
- User: host may break fence locally; **peer wall stays intact** ("неа").

**Next concrete step (for Codex / next turn):**
1. In-game grenade-at-fence with logging forced on; grep `BRK-LOCAL-APPLY`, `BRKS-SEND`,
   `BRKS-RECV`, `BRK-EXEC`.
2. If still zero `BRK-*`: hook `sub_140A657A0` `0xA657A0` (auto-sync on shape break) or
   `sub_14088F840` / Havok `hkdBreakableBody` path; static xref from `0x32330453`.
3. If `BRKS-SEND` but `BRK-EXEC ok=0`: dump peer `scene_hash`/`world_pos` match radius;
   try direct `PublishAttackEventToTarget` / destruction-table apply instead of Execute.
4. Add breakable symbols to `ida_apply_labels.py` once a path is CONFIRMED.

## 2026-07-11: animation/aim work resumed; breakables parked

- `g_breakable_event_relay=false`; broad breakable hooks are not installed while
  Player action-state work is active. The rejected implementation remains intact.
- Live interact return `0xE84C93` resolves to
  `PlayerGunMoveShield_UpdateAndDispatch 0x140E84440`, RTTI
  `Player::GunMoveShield`, vtable `0x142A27658`, slot `+0x148`. It owns the Player
  through action `+0x8`, reads `Player+0x39B8` joypad, and contains aim/fire/
  interact state publication.
- Completed logs contain both screen 0 and screen 1 dispatch from this action.
  Therefore the old puppet-controller promotion wall is rejected; the short
  screen-0-only sample was not a permanent scheduler gate.
- `Player+0x3BD8` was tested and rejected as an action owner: it is
  `PlayerCollision` (vtable `0x142A112B8`, update `0x141DBDA30`, ctor
  `0x140D116D0`). The temporary probe was removed after classification.
- Current aim test uses the existing F8 full `0x200` relay. Default code has it
  OFF and permits auto aim only while stationary, excluding scoped rifles. F8 was
  toggled ON in current PIDs 3744/5860. Test ordinary weapon aim while moving,
  then scoped rifle separately for the old DrawControl disappearance.

## 2026-07-11: aim yaw + scoped mesh visibility build awaiting runtime

- User confirmed full ADS reaches the puppet, but camera yaw does not rotate its
  body and scoped-rifle ADS makes Chloe disappear on the receiver.
- Root orientation fix: post-update now converts peer camera `-Z` forward to a
  horizontal body-yaw quaternion and applies it once. It no longer blends the full
  camera quaternion into the root or relies on sender body yaw.
- Scoped fix uses exact DrawControl inverses: ShowMesh `0x141754A30` and
  EnableMeshInMainView `0x141754B10` after detecting HideMesh state byte `+579`
  or per-instance main-view flag `0x40`.
- Runtime identity confirmed: `*(Player+0x198)+0 == *(Player+0x1A8)` FgAnimData;
  mesh array/count are DrawControl `+0x8/+0x14`.
- Release build succeeds. Full puppet ADS is test-default ON (F8 remains an
  emergency A/B toggle). Fresh injected PIDs `13372/7112` wait in menus; enter
  save and test ordinary aim turn plus scoped ADS. Expected log:
  `SCOPED-MESH-VISIBLE restored=N` only when the engine attempted a hide.

## 2026-07-12: STATIC correction - aim yaw is route-camera owned; mesh-hide pin is unproven

- `PlayerAimController_KickTargetPointJob 0x140CB0200` is identified by three
  independent static signals: RTTI `PlayerAimController`, vtable
  `0x142A0B1F8` slot `+0xE0`, and the source assertion naming
  `PlayerAimController::KickTargetPointJob` in `player-aim.cpp`.
- Its `"sniper"` branch resolves the owning Player's `+0x4010` screen index via
  `ResolveScreenRouteEntityByIndex`, selects the preferred camera child, checks
  the strafe/aim camera descriptor, and calls its virtual slot `+0x230`.
  Therefore writing camera yaw only to the puppet root quaternion at
  `Player+0x140` does not drive the aim controller's direction input.
- Direct executable xrefs do not connect scoped ADS to the four DrawControl APIs.
  `HideMesh/ShowMesh` are called directly only by `0x140252A70`, whose source
  assertion identifies `CrashBandicoot::UpdateSpinning`; `DisableMeshFromMainView`
  and its inverse have no direct executable callers. The `0x146E80Axx` references
  are 32-bit reflection metadata records, not an IDrawControl vtable.
- The current `ShowMesh + EnableMeshInMainView` scoped pin remains a HYPOTHESIS,
  not a static fix. Competing causes still open: a direct DrawControl state writer,
  an upper FgAnim visibility gate, or a late camera/render-mask exclusion.
- Next static step: identify the preferred aim-camera child concrete RTTI/vtable,
  resolve virtual slot `+0x230`, and trace both its orientation source and any
  player-cull publication. Do not run the game until that graph distinguishes the
  visibility candidates.

## 2026-07-12: STATIC correction - visible aim pose uses PlayerAimController aim-additives

- **REJECTED:** `off_143054560` is not a camera vtable. It is the descriptor stored
  at selected camera child `+0x1A8`. The call at `PlayerAimController_KickTargetPointJob`
  uses the selected object's real vtable slot `+0x230`; comparison with the fire
  path's `"unzoomed-sniper-shots"` counter proves that slot is only a zoom-status
  query. It is not an orientation or model-cull method.
- `Player+0x1D90` is the constructed `PlayerAimController` (RTTI vtable
  `0x142A0B1F8`). Common Player phase `0x140C9F420` calls its slot `+0x30`
  (`PlayerAimController_UpdateBlendState 0x140CAFA20`), and
  `Player_PostAnimUpdate 0x1410E9210` calls slot `+0xE0`
  (`KickTargetPointJob`). The controller is therefore mounted and ticked; the old
  blanket "Player_Update never reaches aim" wall is too broad.
- The visible directional pose has a separate, now closed pipeline:
  `PlayerGunMoveShield_UpdateAndDispatch 0x140E84440` ->
  `Player_CheckAimActionEligibility 0x1410E3670` -> aim-controller slot `+0x58`
  (`0x140CAF110`) -> slot `+0x50` (`0x140CAECB0`) -> `0x140CB32B0` ->
  `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0`.
- `0x140CB41F0` publishes the exact animation parameters `aim-add-left`,
  `aim-add-right`, `aim-add-down`, and `aim-add-up`. Constructor `0x140CAE670`
  initializes their `Aim0..Aim7` channel CRCs. ADS blend is
  `PlayerAimController+0x8EC`; the current directional weights/state are in the
  neighboring `+0x8B4..+0x8C4` block.
- Independent Player state implementations (`Player::ZeroG` vtable
  `0x142A48EF8` slot `+0x20`, and `Player::Zipline` vtable `0x142A496F0` slot
  `+0x20`) call the same `+0x58/+0x60` interface. This is a general Player aim
  animation contract, not a sniper, HUD, or camera-render special case.
- Next static step: classify the ground GunMoveShield call's arguments at
  `0x140E858CE/0x140E85925` and the exact failing branch of
  `Player_CheckAimActionEligibility` for a buddy puppet. A build should call/feed
  this native aim-additive interface, not write downstream Player pose quaternions
  or route-camera cache fields.

## 2026-07-12: STATIC - build-ready aim-target interception surface

- The normal ground ADS calls at `0x140E858CE` (heavy weapon) and `0x140E85925`
  (ordinary weapon) both dispatch `PlayerAimController` vtable slot `+0x58`
  (`0x140CAF110`). Win64 arguments are:
  `rcx=PlayerAimController`, `rdx=targetPoint`, `r8=bodyForward`, `r9b=1`, followed
  by four directional angle limits and a single-screen mode byte.
- The first argument is closed end-to-end. GunMoveShield calls controller slot
  `+0xF8` (`PlayerAimController_GetTargetPointFromReticle 0x140CB1720`) at
  `0x140E853E3`; its own slot `+0xF0`
  (`GunMoveShield_CopyAimTargetPoint 0x140E84350`) copies the resulting float4
  unchanged into the `rdx` local later passed at both ADS call sites.
- `GetTargetPointFromReticle` resolves the owner screen at `Player+0x4010`, reads
  the route pose seed, rotates pose local `+Z`, and constructs
  `cameraOrigin + cameraForward * rayDistance` (with a targetable/raycast adjustment).
  This is why the puppet aim pose remains fixed when its screen-1 target is stale.
- Slot `+0x58` obtains the origin internally through controller slot `+0x80`
  (`PlayerAimController_GetAimOriginPoint 0x140CAFD90`), which resolves the Player
  skeleton's `targHead` or `targPelvis` joint. It then forwards origin, target, and
  the incoming body-forward vector to `0x140CB32B0`.
- `PlayerAimController_ComputeDirectionalAimWeights 0x140CB32B0` computes
  `normalize(targetPoint-internalAimOrigin)`. Absolute target distance is immaterial;
  it measures signed angles against body-forward, then publishes left/right/up/down.
- Ordinary-weapon limits are the active aim-camera descriptor object's
  `+0x74/+0x78` values plus two 60-degree defaults (`0x142BE99FC = 60.0f`). The
  heavy-weapon special case changes one limit to `-7.0f` (`0x142BEA1D8`). These
  limits are independent of target direction and should remain native.
- **Minimal implementation surface (STATIC, not yet run):** swap only
  `PlayerAimController` vtable slot `+0x58` at `0x142A0B250`. In the hook, when
  `*(controller+0x108)` is the slot-1 puppet and peer ADS/pose-seed is valid, replace
  the caller-owned target float4 with
  `peerPosePosition + normalize(peerPoseQuat * +Z) * 180`; forward body-forward and
  every native limit, then
  call the original `0x140CAF110`. This drives the authored anim graph without
  overriding the shared route pose seed, root quaternion, or downstream pose caches.
- Competing approaches rejected statically: writing Player aim output fields is
  overwritten downstream; broad pose-seed override also affects locomotion; hooking
  `GetTargetPointFromReticle` entry repeats the frame-from-entry-rsp crash surface.
  Slot `+0x58` swap is class-scoped and preserves both original ground call sites.

No game process was started. Slot `+0x58`'s ABI is
`int64(controller, targetPoint*, bodyForward*, byte, float, float, byte, float, float)`;
the existing vtable-swap helper already provides the required guarded
`VirtualProtect` pattern. The DLL has no detach/uninstall path, so this global class
slot must be installed once and live until process exit. The complete guard contract
is recorded below for the later runtime edit.

### Slot +0x58 hook contract (STATIC)

1. Install only if `*(module+0x2A0B250) == module+0xCAF110`; store that pointer as
   the original and use the existing `VirtualProtect(PAGE_READWRITE)` plus
   `FlushInstructionCache` pattern. Refuse a second/different install.
2. Hook signature must preserve all nine ABI arguments exactly:
   `int64(void*, const float*, const float*, uint8, float, float, uint8, float, float)`.
   The four stack scalars are floats (proved by `vmovss` at `0x140CAF268..2A7`),
   despite Hex-Rays typing them as ints.
3. Pass through unless controller, target, and body-forward are readable; owner
   `*(controller+0x108)` equals `retained_player_object(1)`; owner screen index
   `+0x4010 == 1`; the peer snapshot is fresh; ADS bit and pose-seed flag are set;
   and the peer pose quaternion is normalized/valid.
4. Do not mutate the caller's stack local. Build a hook-local target float4 from
   peer pose position plus normalized quaternion `-Z` times 180, then call the
   original with `&localTarget`, the untouched body-forward pointer, mode bytes,
   and all four native limits.
5. On any failed guard call the original byte-for-byte. Do not invoke route-camera
   helpers, write Player fields, or alter ADS state in this hook.

## 2026-07-13: runtime rejects scoped DrawControl and mesh-byte causes

- PIDs 14868/18564 reproduced the unchanged failures after `1412840`: camera yaw
  does not visibly turn the remote body, and scoped ADS hides Chloe's hair and
  rifle while preserving the body pose.
- No `SCOPED-DRAW-FLAG suppressed` event occurred, so scoped ADS does not call the
  registered `set-instance-flag-draw-in-main-view` writer at `0x1403343D3`.
- `PUPPET-ADS-VISIBILITY playerMeshes=0 weaponMeshes=0 weaponControls=1` repeated;
  neither Player nor Weapon mesh `+0x243` became hidden. `HELD-WEAPON-APPLY`
  continued to show the rifle attached.
- **REJECTED:** `instance+0x18 bit 0x40` and mesh visible byte `+0x243` as causes
  of the hair/rifle disappearance. Continue statically through
  `NdAttachableObject` owner/view/camera culling.
- `POSE-SEED-SEND` and `AIM-WORLD-BASIS` changed with sender camera yaw, so stale
  camera data is also rejected. Trace whether the post-update `Player+0x140`
  write is sampled by the visible render skeleton or misses the native animation
  consumption window.

This contract is ready for a later code edit, but no runtime or injection is allowed
under the current static-only instruction.
## 2026-07-13 scoped visibility + pre-derived-root yaw (STATIC, runtime pending)

- Replaced the rejected scoped DrawControl `0x40`/mesh `+0x243` barriers with a
  relay at `Player_UpdateMainViewVisibilityForAttachmentsAndWeapons 0x140C990A0`.
  During fresh remote scoped-rifle ADS it consumes only the puppet's false
  `Player+0x1618` request before stock code can publish `DisableMainView` to hair
  attachments and weapons. Expected log: `PUPPET-MAIN-VIEW-HIDE suppressed`.
- Camera `-Z` yaw is now applied before
  `Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320`, which reads
  Player `+0x140` and refreshes `+0x240/+0x250/+0x280`; post-transform authority
  remains. Full Player joint-blend ordering is still an explicit alternative.
- Old scoped DrawControl callsite installation and per-frame mesh scan are parked,
  removing rejected hot-path work. Release DLLs build successfully.

### Location-restart regression correction

- **REJECTED:** PRE camera-root yaw. User runtime after location restart reproduced
  kneeling forward/backward sliding, matching the old 2026-07-07 two-orientation
  conflict. PRE helper/calls removed; post transform is again the sole root owner.
- Both copies saw the remote Player become bald on every restart, and the old
  scoped-only relay emitted no suppression. `0x140C990A0` now consumes slot-1
  `Player+0x1618 == 0` for the full retained-player lifecycle so reconstruction
  cannot leave hair/weapon attachables in authored `DisableMainView` state.
- Prediction: restart logs `PUPPET-MAIN-VIEW-HIDE suppressed`, both remote players
  retain hair, and ordinary locomotion no longer uses the kneeling slide pose.

## 2026-07-14 reload/respawn confirmed; aim direction build ready

- User runtime confirms the generation-aware deferred
  `Player+0x3D10 -> Player_ChangeLookId` repair restores final reload cloth/hair.
  Final children resolve to the current puppet with healthy bodies, gates, and
  render categories.
- Revised respawn sync is confirmed: measured discontinuity snaps once, then the
  stationary bounded nudge converges to about 2.8 cm without recurring drift.
- Current aim failure is now split from packet/blend admission. The late
  `PlayerAimController_KickTargetPointJob 0x140CB0200` resolver return
  `0x140CB0300` was not in the override allowlist, and the slot `+0x58` hook
  discarded GunMoveShield's visible-transform body basis in favor of an invisible
  late `Player+0x140` soft write.
- STATIC correction: relay the complete peer seed at `0x140CB0300`, preserve the
  native body-forward argument, remove automatic ADS camera-root soft writes, and
  read actual weights at `+0x8B8..+0x8C4`. Build/runtime prediction:
  `reports/player_aim_render_basis_relay_20260714.md`.

## 2026-07-14 aim native turn-rate build

- Runtime rejected the late `0x140CB0300` complete pose-seed relay: the job reads
  only the first 16-byte position there, and the visual direction remained fixed.
- Preserving native body-forward exposed `150..179` degree relative aim on entry;
  forcing additive blend directly to one made the transition visibly broken.
- Build-ready correction sends actual local block-0 right-stick rates, consumes
  them through the already isolated Joypad3 ScaleCopy sandwich, restores native
  aim-blend easing, and limits fallback horizontal additive to stock 60 degrees.
- Next runtime discriminator is explicit: `AIM-TURN-SEND` -> matching
  `AIM-TURN-RELAY processed`; then observe whether `yawPre` converges. Full matrix:
  `reports/player_aim_native_turn_rate_20260714.md`.

## 2026-07-14 aim pose-axis and stable-blend build

- Runtime rejected resolved block-0 `+0x08/+0x0C` as the mouse turn stream: both
  copies received strict ADS but emitted no `AIM-TURN-SEND` or
  `AIM-TURN-RELAY`. The destination-3 experiment is OFF by default.
- Static re-reading of `PlayerAimController_GetTargetPointFromReticle
  0x140CB1720` corrects the earlier route-axis claim. This exact aim-target
  producer expands pose quaternion **positive Z**, with the same formula as
  independently confirmed `Quaternion_RotatePositiveZ 0x14014D9F0`. The mod's
  negative-Z aim target explains the logged near-180-degree opposition exactly.
- The same rejected run showed native puppet `controller+0x8EC` oscillating
  between zero, approximately one half and one. Native authored-state ownership
  is rejected for the retained Player. The next build uses positive Z only in
  pose-seed aim consumers and applies a 180 ms monotonic blend at the two exact
  call boundaries. Evidence and prediction:
  `reports/player_aim_pose_positive_z_blend_ramp_20260714.md`.

### Runtime result

- PIDs 13036/17280 confirmed the narrow corrections: first-entry `yawPre`
  dropped from near 180 to 59.1 degrees and the 180 ms blend rose monotonically
  through multiple logged samples to one.
- Visual camera tracking is still **REJECTED** by the user. Relayed quaternion,
  target and forward vectors changed, but directional output frequently saturated
  at `R=1` as `yawPre` exceeded the 60-degree arc. Continue statically from the
  body/sector turn owner before the clamp, then the downstream turn-in-place graph.
- Scoped hair/weapon visibility was not exercised in this run.

## 2026-07-14 sender root-only body basis rejected statically

- `StatePacket::quat` does copy sender `Player+0x140`, but this field is only the
  local root quaternion relative to optional parent rotation `Player+0x120`.
- `BuildWorldBasisVectorFromRuntimeTransform` at `0x140E8582F` composes parent
  `+0x120` with local root `+0x140` before `Quaternion_RotatePositiveZ
  0x14014D9F0`. Therefore `peer.quat * +Z` is not the exact stock world basis and
  would discard parent/sector rotation.
- **REJECTED before runtime:** replacing GunMoveShield's native `r8` with packet
  root positive-Z. Preserve native body-forward and continue through the actual
  ground turn-in-place/sector owner. Full rejection:
  `reports/player_aim_sender_body_basis_20260714.md`.

## 2026-07-14 GunMoveController aim-turn chain (STATIC)

- `PlayerGunMoveShield_UpdateAndDispatch 0x140E84440` calls
  `GunMoveController::vtable+0x38` (`0x140E8F990`) before its aim work and later
  calls `vtable+0x50` (`0x140E90590`). The first method writes camera-relative
  world direction to controller `+0x150`.
- `0x140E8F720` compares `+0x150` against body direction and selects state 2
  (`aim-idle`) or 3/4 (`aim-idle-rotate` / `aim-idle-rotate-cc`).
- Exact native root writer `0x140E8FD00` consumes `+0x150`, smooths yaw, writes the
  owner runtime transform through `0x1416861E0`, and commits via slot `+0x208`.
- Next build overrides only the return-side `+0x150` result of `0x140E8F990` for
  fresh remote ADS using relayed pose positive-Z. Full evidence, alternatives and
  runtime discriminator: `reports/gun_move_controller_aim_turn_chain_20260714.md`.
- Build complete: the vtable hook calls stock first and changes only the validated
  slot-1 result. Release compilation succeeds; runtime prediction is now earned.
- First runtime PIDs 18480/14436 exited as the second copy entered gameplay, before
  any `AIM-TURN-DIR` line. Thus the direction override was not exercised. Reject
  the hook's hot-path retained-table lookup; v2 classifies controller `+0x108`
  owner directly by `Player+0x4010 == 1`.
- Second runtime PIDs 10600/12916 proved the internal route: staged target
  `(-0.803093,-0.595853)` reached native body basis
  `(-0.803219,-0.595684)` with `yawPre=0`. User visual result still failed and a
  location/progress transition reproduced Chloe's kneeling slide, so the build is
  REJECTED as a complete fix.
- STATIC conflict found in mod order: `apply_puppet_transform_post` runs after
  `g_original_update` and blended `Player+0x140` back toward camera-independent
  `StatePacket::quat` on every ADS frame. That cancelled the proven native turn
  before render while leaving `aim-idle-rotate` looping. Next build skips only
  this post-ADS root blend while the native GunMoveController relay is installed.
- Third runtime PIDs 20736/17048 rejected that correction: user still saw aim at
  one static point. Even without the mod post blend, body basis repeatedly returned
  near `(0.30,0,0.954)` while target changed and `yawPre` reached 110.9 degrees.
  A later animation/post-anim/render owner wins. GunMoveController relay is now
  parked (not installed by default); trace the visible joint palette only.

## 2026-07-14 final joint-palette boundary (STATIC)

- Player vtable `+0x408` is exact-source `Player_PostAnimBlending_Async
  0x1410EB6A0`. Its `Player+0x1DC0` object is a
  `CharacterLegIkController` constructed by `0x141F9D610`; this is downstream IK,
  not the missing ADS yaw owner.
- Exact-source `AnimManager_PerformAnimBlend 0x141A92260` and the normal
  joint-blend path use `FgAnimData+0x230` as the current matrix palette and
  `+0x238` as the previous palette. The fallback independently allocates
  `48 * joint_count`, fills three `float4` rows per joint, then performs the same
  current/previous swap and clears `+0x270/+0x278`.
- `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0` reaches this same
  live `FgAnimData` through `[[Player+0x1A0]+0x58]+0x10` and writes directional
  fields `+0x188..+0x194`; prior runtime confirms the puppet's animation phases
  are active. The remaining discriminator is therefore palette evaluation versus
  later world/render consumption, not another Player root write.
- Next runtime is read-only: while the puppet stands in ADS, compare current and
  previous joint matrices as camera yaw changes. Changing upper-joint matrices
  with a static rendered pose select a later world/DrawControl owner; no correlated
  palette change selects stale animation-command evaluation. Evidence and exact
  competitors: `reports/player_aim_final_joint_palette_20260714.md`.
- Read-only `AIM-PALETTE` sampling is implemented at a 400 ms cadence and the
  release DLL builds successfully. It keeps a private baseline and performs no
  game-memory writes; runtime is ready under the prediction above.
- First runtime PIDs 16724/1964 was inconclusive: pose seed, world basis, and
  directional weights changed, but no palette sample passed all layout guards.
  A throttled read-only `AIM-PALETTE-STATUS` guard report is now built to identify
  the exact rejected pointer/count assumption.
- PIDs 16828/17052 resolved the guard: live Chloe has 651 skeleton joints, so the
  probe's private 512-joint sanity buffer was too small. Fg/skeleton/current and
  previous palette pointers were valid. The private limit is now 1024; no game
  memory is written.
- PIDs 200/13948 passed the 651-joint guard and changed palette buffers during
  fresh ADS, but all four directional fields stayed zero and the run emitted no
  `AIM-WORLD-BASIS`/`AIM-WEIGHTS-POST`. Generic blend and peer IK markers remained
  active. The user also observed Chloe sliding on her knees. Because the palette
  probe is read-only, the current branch is upstream action dispatch: determine
  whether `Player::GunMoveShield` is deselected or takes an early non-aim exit.
  Full runtime result and competitors:
  `reports/player_aim_final_joint_palette_20260714.md`.

## 2026-07-14 aim action-state gate (STATIC)

- `Player_IsCurrentActionStateHash 0x1416A5790` is an exact read-only active-state
  surface: `Player+0xC0 -> active -> record+0x10 -> hash+0x18`.
- Both `PlayerGunMoveShield_UpdateAndDispatch` and builder `0x140E8D140` use it
  for authored gun-move names. RTTI also resolves four distinct gun-move wrapper
  classes with separate pre-dispatch exits, so the latest absence of world-basis
  does not yet distinguish state deselection from a wrapper/common early exit.
- The apparent screen predicate `0x140EFB750` is REJECTED as the lifecycle cause:
  it calls `PlayerWeaponController_IsShieldDrawn 0x1410B2480`, an exact
  `ProcessShield` query. Next build may only add read-only state/dispatch markers.
  Evidence and prediction: `reports/player_aim_action_state_gate_20260714.md`.
- Read-only build ready: `AIM-ACTION-STATE` samples the exact hash chain,
  `AIM-DISPATCH-ENTRY` patches only the stock call at `0x140E84483`, and
  `AIM-ELIGIBILITY reached` identifies the later gate. The completed 651-joint
  palette scanner is disabled to remove unnecessary per-frame diagnostic work.
  Release build succeeds; runtime discriminator is earned but not yet run.

## 2026-07-14 aim wrapper shield gate (STATIC, Cursor)

- `Player::GunMoveShield` vtable `0x142A27658` slot `+0x20` is exact-source
  `0x140E86930`. It calls `PlayerGunMoveShield_UpdateAndDispatch 0x140E84440`
  only when `0x140EFB750(screen_index)` succeeds; otherwise it calls
  `Player::vtable+0xBD0` and publishes runtime state without entering dispatch.
- `0x140EFB750` resolves the per-screen retained Player and queries
  `PlayerWeaponController_IsShieldDrawn 0x1410B2480` (`ProcessShield` only).
  This is therefore a weapon-class gate, not a generic screen-readiness check.
- Inside dispatch, the inverse guard at `0x140E8561E` skips the early publish
  when shield is **not** drawn and continues to `Player_CheckAimActionEligibility`
  at `0x140E8573C`. The missing world-basis on pistol ADS is therefore more
  consistent with never reaching dispatch via wrapper `+0x20` than with a late
  palette/render owner.
- Next build adds `AIM-WRAPPER shieldDrawn= dispatch=` on wrapper `0x140E86930`
  plus hash-name decoding on the existing action/dispatch markers. Runtime
  prediction: pistol puppet logs `shieldDrawn=0 dispatch=0` with a gun-move hash,
  while `AIM-DISPATCH-ENTRY` stays absent; shield/sniper should log `1/1`.

## 2026-07-14 aim action-state runtime (PIDs 13148/21228)

- **REJECTED shield-wrapper theory:** zero `AIM-WRAPPER` lines; puppet never used
  `Player::GunMoveShield` wrapper `0x140E86930`. Stable ADS state is `GunMove
  0x12D842C0`, not `GunMoveShield`.
- **REJECTED dispatch/eligibility block:** `AIM-DISPATCH-ENTRY` and
  `AIM-ELIGIBILITY original=1 forced=0` on every ADS sample.
- **CONFIRMED full upstream chain in this run:** `AIM-WORLD-BASIS`,
  `AIM-ADDITIVE-PERMISSION original=0 forced=1`, and `AIM-WEIGHTS-POST` with
  non-zero R/U once `AIM-BLEND-TICK` reaches 1. Brief entry hash `0xE1939F43`
  precedes `GunMove`.
- Contrast: PIDs 200/13948 had zero weights/no world-basis; this run differs by
  permission/blend relays and stable `GunMove`.
- **CONFIRMED visual failure (user):** receiver puppet still aimed at one fixed
  point during ADS while `AIM-WORLD-BASIS` fwd/yawPre changed and
  `AIM-WEIGHTS-POST` reached non-zero. Break is **downstream of published weights**,
  not action dispatch. Report:
  `reports/player_aim_action_state_runtime_20260714.md`.

## 2026-07-14 aim render/palette discriminator build (Cursor)

- Read-only `AIM-RENDER-DISC` compares peer camera forward vs `Player+0x140` root
  forward, controller `+0x8B8..+0x8C4` vs `FgAnimData+0x188..+0x194`, only when
  peer yaw changes ≥2° and weights/blend are active.
- `AIM-PALETTE` re-enabled with the same weight gate (palette was disabled during
  action-state pass). Release build succeeds; injected PIDs 21396/19260.
- Prediction: if `peerYaw` tracks camera but `rootYaw`/`fgAdd` stay static → later
  root/render owner; if `fgAdd` tracks controller but palette joints static →
  `0x141AB97C0` eval failure; if palette upper joints move but visual fixed →
  DrawControl/world transform. Evidence:
  `reports/player_aim_render_discriminator_20260714.md`.

## 2026-07-14 aim render discriminator runtime (PIDs 21396/19260)

- **CONFIRMED peer/root decoupling:** `AIM-RENDER-DISC` shows peerYaw swings
  (-113°..+16°) while `Player+0x140` rootYaw stays ~14–40° (`yawDelta` -35..-134).
  User visual still fixed point. Body aim cannot follow peer camera without root
  turn or successful additive eval on top of this heading.
- **CONFIRMED controller alive:** `ctrlAdd` R/D track camera with `blend=1`.
- **PROBE BUG:** first `fgAdd` read used `Player+0x1A8+0x188` contiguous; publish
  actually writes `[[Player+0x1A0]+0x58]+0x10` with D/U/L/R field layout. Rebuilt
  probe logs `pubAdd` + `samePtr`.
- **PARTIAL palette motion:** gated `AIM-PALETTE` shows sane 2–4 unit deltas on
  upper joint indices during sweeps; garbage on inactive joints still present.
  Report: `reports/player_aim_render_discriminator_runtime_20260714.md`.

## 2026-07-14 pubAdd runtime (PIDs 11208/21416)

- **CONFIRMED publish:** `pubAdd` matches `ctrlAdd` every sample;
  `pubState=0x10e0e67930` ≠ `Player+0x1A8` (`samePtr=0`). `fgAdd` on FgAnimData is
  stale template `(0.2,0,0,1)`.
- **CONFIRMED visual + root decoupling:** user reports fixed ADS aim. `peerYaw`
  swings >80° while `rootYaw` stays ~15–35° (`yawDelta` to -97°). Break is body
  heading, not weight publication.
- **Next:** re-open `GunMoveController` turn relay or explicit peer-yaw root sync
  during ADS; palette-only branch insufficient.

## 2026-07-14 body-turn build (Cursor)

- Re-enabled `GunMoveController` vtable `+0x38` relay (`0x140E8F990` → `+0x150`
  peer pose positive-Z) and post-ADS `sync_puppet_root_yaw_from_peer_camera` on
  slot-1 puppet. Post hook skips packet-quat blend while relay is installed.
- Logs: `AIM-TURN-DIR`, `AIM-ROOT-YAW-SYNC`, existing `AIM-RENDER-DISC`.
- Prediction: `yawDelta` in `AIM-RENDER-DISC` collapses toward 0 while peer sweeps;
  user sees puppet ADS aim track peer camera. Knee-slide on reload remains a
  competitor to watch. Prior partial relay rejection noted in turn-chain section.

## 2026-07-14 body-turn pos-Z runtime REJECTED (PIDs 21060/10864)

- User: still fixed in ADS; **worse** — aims nowhere near even initial reticle.
- `AIM-TURN-DIR`/`AIM-ROOT-YAW-SYNC` ran; pos-Z `+0x150` override is wrong axis
  for body turn (camera -Z in native `0x140E8F990`).
- **v2 bn:** relay uses `camera_forward_horizontal` (-Z); post root sync parked.
  Report: `reports/player_aim_body_turn_runtime_20260714.md`.

## 2026-07-14 neg-Z turn relay REJECTED (PIDs 13400/21384)

- User: unchanged visual. `peerNegZ` overrides `+0x150` but `rootYaw` stuck ~30deg.
- **REJECTED** post-native `+0x150` patch as root driver for slot-1 puppet.
- **v3 bo:** turn relay parked; post-ADS full `Player+0x140` yaw snap from peer
  camera -Z. Watch knee-slide competitor from prior runs.

## 2026-07-14 root yaw snap REJECTED (PIDs 2084/7800)

- User: puppet still one fixed direction in ADS.
- Log seq=1051: `peerYaw≈rootYaw` (0.5°) with live `pubAdd`; visual unchanged.
- **CONFIRMED:** visible aim is not `Player+0x140` root. All body-turn branches
  closed (+0x150 relay pos/neg-Z, post snap). Root snap parked in source.
- **Next:** static trace palette → render skeleton (`0x141AB97C0`, DrawControl).
  Report: `reports/player_aim_body_turn_runtime_20260714.md`.

## 2026-07-14 palette→render static trace (Cursor)

- **STATIC chain closed:** `PlayerAimController_PublishDirectionalAimWeights
  0x140CB41F0` → anim-graph `aim-add-*` strings → `ProcessAnimCmdListSegment
  0x141AB97C0` (palette `FgAnimData+0x230`) + `pNextFrameSkinningBoneMats` at
  `+0x560` → `CharacterSkeletonRender_Update 0x141B61530` → per-joint
  `CharacterSkeletonEval_PerJoint 0x141B757F0` (Ndjob entry `0x141B5CE90`).
- **REJECTED (reconfirmed):** `Character_UpdateDerivedWorldTransformFromLocalRoot
  0x141BB8320` and `Player+0x740` DrawControl AO refresh as visible ADS aim owners.
- **Preferred hypothesis:** publish-state FgAnimData (`[[Player+0x1A0]+0x58]+0x10`,
  runtime `pubState`) diverges from `Player+0x1A8` probed by `AIM-PALETTE`; render
  may skin from stale `+0x560` while `pubAdd` tracks peer camera.
- **Next:** read-only `pubState` vs `Player+0x1A8` palette/skinning discriminator;
  no further `Player+0x140` writers. Labels in `ida_apply_labels.py`; report:
  `reports/player_aim_palette_render_chain_20260714.md`.
- **Build 2026-07-14bq:** read-only `AIM-FGANIM-DISC` compares `pubObject` vs
  `fgObject` palette `+0x230` and skinning `+0x560` with cross-joint deltas and
  per-object skinning motion. Boot marker `2026-07-14bq`.

## 2026-07-14 AIM-FGANIM-DISC runtime (PIDs 12996/20040)

- **CONFIRMED split:** `pubAdd==ctrlAdd` tracks peer; `fgAdd` stuck `(0.2,0,0,1)` on
  `Player+0x1A8` while `samePtr=0`. `pubPal=0` (publish object is anim active state,
  not full FgAnimData); `fgPal` valid and `AIM-PALETTE` shows joint motion.
- **CONFIRMED:** `fgSkinValid=0` — `FgAnimData+0x560` is not a heap pointer on puppet;
  `fgSkinTot=0` every sample. Palette moves; skinning path never publishes.
- **Root cause candidate:** `ProcessAnimCmdListSegment` reads aim-add from
  `Player+0x1A8` (`fgAdd` stale), not from publish-state where controller writes.
- **Fix build 2026-07-14br:** `mirror_puppet_aim_add_to_fganim` copies publish-state
  `+0x188..+0x194` → `FgAnimData+0x188..+0x194` after world-basis publish and in
  `apply_puppet_transform_post`. Logs `AIM-FGANIM-MIRROR` when delta > 0.001.
- **CONFIRMED (PIDs 10980/4508, build br):** `fgAdd` now tracks `pubAdd==ctrlAdd`
  after mirror; was `(0.2,0,0,1)`. `fgPalTot` moves; `fgSkinTot=0` still.
- **Build 2026-07-14bs:** mirror sources controller `+0x8B8..+0x8C4` (skip zero pub);
  post-update `sync_puppet_fganim_palette_to_skinning` copies `+0x230`→`+0x560`.
  Boot marker `2026-07-14bs`. User still on `br` in last log — no `SKIN-COPY` lines.
- **Build 2026-07-14bt (next test):**
  - POST-ADS `blend_network_orientation(quat, pkt.quat, 0.35)` **removed** during
    `peer_aiming` (was canceling native camera turn after update).
  - `pointer_looks_like_heap` tightened (rejects float bit-patterns like
    `fgSkin=0x3e4613fa`; requires readable heap).
  - `ensure_fganim_skinning_buffer`: allocates `+0x560` when puppet never got
    `pNextFrameSkinningBoneMats`; logs `AIM-FGANIM-SKIN-ALLOC`.
  - Post-update mirror+skinning copy retained. Boot marker `2026-07-14bt`.
  - Watch log: `AIM-FGANIM-SKIN-ALLOC`, `AIM-FGANIM-SKIN-COPY`, no root yaw drift.

## 2026-07-14 build bt runtime REJECTED (PIDs 4164/8300) — PAUSE

- User: **visual still no** («всё ещё нет») after bt.
- **CONFIRMED on receiver PID 8300:** `AIM-FGANIM-SKIN-ALLOC`, `AIM-FGANIM-SKIN-COPY`,
  `fgAdd==pubAdd`, GunMove dispatch, world-basis/weights live.
- **REJECTED:** mirroring aim-add + allocating/copying palette→`FgAnimData+0x560`
  does not change visible puppet ADS aim.
- **REJECTED (again):** `Player+0x140` — `rootYaw` stuck ~47.6° with sweeping peer;
  post packet-quat blend already removed in bt.
- **Closed paths this sprint:** body-turn `+0x150` pos/neg-Z, post root snap, fgAdd
  mirror alone, fg skinning alloc/copy alone.
- **Next (do not re-run without new code):** PRE-render hook
  `CharacterSkeletonRenderJob_Entry 0x141B5CE90` — copy puppet `Player+0x1A8`
  palette into SkelTable workspace before `CharacterSkeletonEval_PerJoint 0x141B757F0`.
  Report: `reports/player_aim_bt_runtime_20260714.md`.
- **Code landed:** commit `9ca0354` (bt). Boot marker `2026-07-14bt`.

## 2026-07-14 build bu — SkelTable pre-render inject (Cursor)

- **LANDED:** palette inject before `CharacterSkeletonRender_Update` when slot-1
  puppet ADS active; logs `AIM-SKEL-INJECT`.
- **REJECTED (PID 6420):** 12-byte entry detour on `0x141B5CE90` split
  `sub rsp,20h` mid-instruction → immediate AV `rip=0x5F0016` on first render job.
- **Fix build 2026-07-14bv:** E8 callsite relay at `0x141B5CEA6` (pre-Update call)
  instead of entry detour. PIDs 21224/2640: hook installs, **no post-install AV**.
- Boot marker `2026-07-14bv`. PIDs 21224/2640: install OK, no post-install AV.
- **Build 2026-07-14bw (LANDED):** `AIM-SKEL-SKIP` + SkelTable buffer auto-alloc.
  User visual test: **unchanged**. Log post-boot had **zero** `AIM-SKEL-*` lines —
  inject gate never opened (suspected `player_ads_active(puppet)` false while peer
  packet aim active).
- **Build 2026-07-14bx:** gate fix; `AIM-SKEL-GATE wanted=1` + `AIM-SKEL-INJECT
  copiedPal=1` on PID 17544 during peer ADS. User visual **unchanged**.
- **REJECTED (bx runtime):** SkelTable `qword_146919C40` `+0x230/+0x560` as FgAnimData
  heap pointers — `skelPal=0x760000` (inline xmm rows, VirtualQuery invalid); false
  `copiedPal=1`.
- **Build 2026-07-14by (LANDED):** pre-render inject copies puppet `Player+0x1A8`
  palette into **publish-state** `[[Player+0x1A0]+0x58]+0x10` (`pubPal` was null in
  `AIM-FGANIM-DISC`). Logs `AIM-PUB-INJECT`. `pointer_looks_like_heap` now uses
  VirtualQuery.
- **REJECTED (by runtime, PIDs 18620/8712):** pubState palette inject at render hook —
  `AIM-PUB-INJECT copiedPubPal=1 copiedPubSkin=1` but user visual **unchanged**;
  receiver **severe lag** during peer ADS (per-call ~31 KB memcpy + VirtualQuery on
  every `CharacterSkeletonRender_Update`).
- **REJECTED (bz user test):** lag persisted + visual still fixed. Root cause: per-frame
  31KB `sync_puppet_fganim_palette_to_skinning` in **both** `hooked_player_aim_world_basis`
  (every ADS eval) and `apply_puppet_transform_post` (~60 Hz), plus `g_enable_aim_palette_probe`
  scanning 651 joints. Render-hook passthrough still installed in bz.
- **Build 2026-07-14ca (LANDED):** disable skinning/pubState memcpy paths and palette
  probe; mirror aim-add only; skip render-hook install when inject off. Boot `2026-07-14ca`.
- **CONFIRMED (user, ca):** severe receiver lag during peer ADS **mostly gone**.
- **REJECTED (user, ca):** visible puppet ADS aim **still fixed**.
- **Diagnosis:** upstream publish path is live (`pubAdd==ctrlAdd`, mirror, world-basis);
  failure is downstream — `ProcessAnimCmdListSegment` eval and/or
  `CharacterSkeletonEval_PerJoint` workspace (`owner+0x14B8`, stride `0x6C0`) / DrawControl
  post-eval. FgAnimData heap copies are closed.
- **Build 2026-07-14cb/cc (REJECTED):** pre-PerJoint inline-asm relay @ `0x141B63CE6`
  crashed `0xC0000005` @ `0x1B63CEB` (cb immediate; cc delayed). Root cause: hand-rolled
  stack/xmm save-restore on hot callsite — not MSVC-safe.
- **Build 2026-07-14cd (LANDED):** same workspace+0x558 inject logic, but callsite relay
  is C++ `hooked_character_skeleton_eval_per_joint_callsite` + FF25 trampoline (same
  pattern as render-hook). Preserves `rcx/rdx/xmm2/xmm3` via function signature. Boot
  `2026-07-14cd`. Local smoke: PID 22304 hook installed, no EXCEPTION in
  `locomotion_hook.log`.
- **REJECTED (user, cd two-instance):** build cd stable (no PerJoint crash) but puppet
  ADS aim **still not visible**. Workspace inject **PARKED** (`g_enable_puppet_workspace_palette_inject=false`, boot `2026-07-14ce`).
- **Melee sync (2026-07-14):** ADS workspace inject remains PARKED. **cf/cg**
  confirmed button relay (`MELEE-SEND`/`MELEE-RECV cmdActive=1`) but **no puppet
  anim** — Joypad force is insufficient; MoveStick uses `PlayerMeleeController`
  `+0x210` → `sub_1410FC9F0` (`g_NetModeFlags+3` gate). **ch (current):**
  `apply_peer_melee_animation` exec relay + `PlayerInputCommandState_IsCommandActive`
  puppet force + `sub_1410FC9F0` bypass → Player vtable `+0xC78`; log `MELEE-EXEC`.
  **Runtime pending:** two-instance punch air → expect `MELEE-EXEC ok=1` + visible
  puppet melee anim.

## 2026-07-15 melee cq: native dispatcher callsite relay

- Composer's global `PlayerInputCommandState_IsCommandActive` detour remains
  rejected because it broke GiveWeapon; manual dispatcher/queue/anim calls remain
  rejected by hangs, crashes, and invalid descriptors.
- Static chain is now complete: stock tick `0x141279DF0` calls dispatcher
  `0x141272A50`; `melee-r1` callsite `0x141273AE2` immediately feeds native queue
  insert `0x141273FA0`; vtable `+0x220` independently queries literal `melee-r1`
  through queue resolver `+0x1F8`.
- `0x14183E2D0` proves the lmouse subtree is gated by `Joypad+0xBE1` PC-input
  mode, so remote injection uses the unconditional R1 subtree. Local lmouse
  callsite `0x141273D3B` is capture-only.
- Build `2026-07-15cq` patches those two E8 calls only, relays a 150 ms state bit,
  and consumes one atomic press in retained slot 1's natural dispatcher tick.
- Production diagnostics remain disabled; no melee hash scan, queue call, log,
  allocation, or memory walk was added to the hot path.
- **Next runtime exam:** two fresh copies, one tap and one hold, then weapon
  pickup/switch. Expect one peer melee attempt per tap, no frame-repeat punches,
  no GiveWeapon regression, and cp-level frame pacing.

## 2026-07-15 build cr: network handshake discriminator

- cq processes did bind the expected complementary loopback ports: PID 24420 on
  UDP 31338 and PID 10204 on UDP 31339. Valid `StatePacket` exchange was not
  observable because production `log_line` is disabled.
- cr emits one-shot boot records for role/ports, first send, and first accepted
  receive. It also records once if sending is waiting for a retained local Player
  or valid position. No recurring network telemetry was added.
- The unconditional crash observer was parked. It captured only eight
  first-chance exceptions per process, so this removes possible transition
  hitches but is not claimed as the cause of sustained low FPS.
- Release build succeeds. Runtime exam: fresh two-copy launch; both sides must
  report first send and first receive before visual puppet behavior is debugged.

**Runtime CONFIRMED:** fresh PIDs 15996/14588 selected A:31338/B:31339, each sent
a 456-byte sequence-1 state packet, and each accepted the other's packet at the
same world position. The apparent initial non-connection was the interval before
each local Player published a valid position. Transport and snapshot publication
are healthy; a remaining missing puppet belongs to the downstream apply path.

## 2026-07-15 build cs: park cq melee patch to isolate connection regression

User reports the peer is still visually absent and that the pre-change build
worked. The cr processes exchanged sequence 1 and then both disappeared after
Player creation. Build cs disables only the cq E8/FF25 melee callsite relay while
retaining cp cleanup and one-shot network milestones. Prediction: stable visible
peer means cq caused the regression; absent peer means roll back cp next. Melee
sync is parked for this test.

## 2026-07-15 build ct: restore required remote Player descriptor capture

Static root cause found before the cs runtime exam: cp disabled the sole writer
of `g_spawn_captured/g_spawn_desc`, but remote auto-spawn requires that flag and
passes those captured arguments to `reissue_spawn_for_slot1`. Thus packets could
exchange while no visible peer could ever be created. Build ct restores this
one-shot functional capture, leaves cq melee parked, and removes the capture
handler's heavy descriptor/hash scan in production. Prediction and alternatives
are recorded in `reports/runtime_hot_path_audit_20260715.md`.

**Runtime CONFIRMED (user):** visible two-way peer representation returned on ct.
Root cause was cp disabling the sole `g_spawn_captured` producer. The capture
toggle is removed; production now always registers/arms the required one-shot
descriptor capture. Its heavy diagnostic scan remains off.

## 2026-07-15 build cu: melee animation exam

With visible peer creation restored and made mandatory, cu re-enables only the
two native melee press callsite relays from cq. Test one tap, then a hold, then a
weapon pickup/switch. Expected: one peer native melee attempt per tap, no repeated
queueing during hold, stable peers, and no weapon regression. Failure classes and
the downstream target are recorded in
`reports/melee_native_callsite_relay_20260715.md`.

**REJECTED (user):** cu kept both peers visible but produced no remote fighting
animation. Static follow-up proved the relay translated PC lmouse into the wrong
consumer family: R1 enters `player-r1-attacks` and target rating, while the free
melee animation consumes `melee-square` through controller vtable `+0x210`.

## 2026-07-15 build cv: preserve the square command family

cv retains local R1/lmouse capture but removes remote R1 forcing. The existing
Joypad-3 hook now consumes one fresh pending pulse only for the stock
`melee-square` query at dispatcher callsite `0x141272B99`; the native dispatcher
inserts the queue entry and PlayerMoveStick owns the downstream animation.
Manual dispatcher, queue, TryAttack, and TryAnim calls remain parked. Runtime
prediction and failure split are recorded in
`reports/melee_native_callsite_relay_20260715.md`.

**REJECTED (user):** cv still showed no remote hit animation. Build cw now adds a
one-shot stage discriminator at the existing Joypad square force, controller
vtable `+0x210`, and the sole TryAnim callsite `0x140F07A0A`. It does not bypass
or manually invoke any action. The next user tap will classify the failure as
pending-command, queue-consumer, intermediate MoveStick gate, TryAnim gate, or
downstream pose publication.

**cw runtime result:** both peers connected and spawned, but one visible local
melee tap emitted no stage line, including no receiver `square-query-forced`.
The failure is therefore before queue insertion: local capture listened only to
the sibling PlayerInput lmouse/R1 callsites, while the actual free attack is the
Joypad `melee-square` query already observed by cg.

## 2026-07-15 build cx: capture the actual local square query

cx captures an original-true `melee-square` result in the existing global Joypad
hook only when the Joypad equals the one selected by retained slot 0's live
`Player+0x39B8`. It emits one `local-square-captured` marker, retains the 150 ms
edge pulse, and leaves all manual melee execution paths parked. The receiver's
cw queue/TryAnim stage probes remain active for the next tap.

**CONFIRMED (user):** cx produced the visible remote hit. The sender logged local
square capture; the receiver forced square and retrieved it from queue `+0x210`.
Although the observed TryAnim call returned zero, the animation was visible, so
that return is not the whole path's success flag. cy removes every stage probe and
keeps only the working square relay.

## 2026-07-15 aim static correction after confirmed melee

- Melee square relay is user-confirmed and committed in `fe94dfe`.
- **REJECTED:** the old `CharacterSkeletonRenderJob_Entry 0x141B5CE90` ->
  `CharacterSkeletonRender_Update 0x141B61530` ->
  `CharacterSkeletonEval_PerJoint 0x141B757F0` map is AudioManager ARM, proven by
  exact source/function strings, its scheduler graph, and NCA audio diagnostics.
- **STATIC:** the actual current/previous final matrix buffers are
  `FgAnimData+0x230/+0x238`. Prior `+0x560/+0x568` interpretation confused
  Hex-Rays decimal offsets with hexadecimal offsets.
- Do not restore the parked audio workspace hooks. Next static step is to resolve
  the true consumer and 3x4 matrix convention for `FgAnimData+0x230`, then write
  a discriminating read-only prediction before launching two instances.
- Report: `reports/player_aim_audio_path_correction_20260715.md`.

## 2026-07-15 STATIC: ADS body yaw belongs to FgAnimData render-world publication

- `FgDrawManager_PrepareInstanceForRendering 0x14181A9B0` is the true final
  consumer: it copies CPU skinning matrices `FgAnimData+0x230` to upload storage
  `+0x240`, then obtains the independent render-world matrix through
  `FgAnimData_GetRenderWorldMatrix 0x1403014F0`.
- That getter reads `FgAnimData+0x1C0`, written by
  `FgAnimData_PublishWorldTransform 0x1418C55D0` ->
  `FgAnimData_PublishWorldMatrix 0x1418C54D0`.
- Animation-manager phase `0x40` owns the stock publication at callsite
  `0x141A8FC00` with ABI `rcx=fg, rdx=quat+translation, r8b=0`.
- Therefore the previous post-Player-update `Player+0x140` writes could be true in
  logs while visually inert: the renderer consumed an already separated FgAnimData
  snapshot. Do not rotate/copy the full 651-joint palette for body yaw.
- Evidence, alternatives and runtime prediction:
  `reports/player_aim_fganim_world_transform_20260715.md`.
