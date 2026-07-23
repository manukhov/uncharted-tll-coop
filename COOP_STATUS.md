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

**Fix under test (`coop_runtime.cpp`):**
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

**Fix under test (uncommitted `coop_runtime.cpp`):**
1. `force_peer_puppet_aim_blend_state` — sustain `controller+0x8A0=1` and
   `+0x8EC=1` on blend tick, world-basis hook, and post-update for slot-1 puppet.
2. Blend-target relay always returns `max(original, 1.0)` during peer ADS (not
   only when native target is zero).
3. Scoped rifle: soft camera-yaw root blend (`apply_peer_aim_facing`, 0.85) +
   `force_puppet_ads_flag` while pose relay active (no snap / no `0x141BB8320`).

**Prediction:** scope free-look rotates puppet upper body via aim-add and root
yaw via soft blend; pistol ADS unchanged. Log `AIM-BLEND-TARGET` and
`blend=1` in `AIM-WEIGHTS-POST`.

**Rebuild:** `scanner/build/tll_coop_runtime.dll` — inject and retest.

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

**Сделано в `coop_runtime.cpp` / `coop_runtime_offsets.h`:**

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

**Билд:** `scanner/build/tll_coop_runtime.dll` OK. Inject в PID 14224/18988 —
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

**Собрано и заинжекчено** (PID 25372 / 16372, `tll_coop_runtime.dll`). Логи по
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

**Новая статика (см. `FINDINGS_ARCHIVE.md`, запись «Per-screen HUD reticle publishers»):**
обычное перекрестие спавнится per-screen HUD паблишерами над
`g_PerScreenPlayerHudState 0x143EF1E30` (stride 0x100), **индекс экрана/слота в
`state+0xF8`**:
- `0x14135A650` `PlayerHudScreen_ReticleProcessPublisher` (спавнит `ss-hud-reticle`);
- `0x14135AF20` `PlayerHudScreen_AmmoReticlePublisher` (тоже спавнит `ss-hud-reticle`,
  вызывается из `UpdatePlayerHudAimingAndWeaponAccuracyPublishes`);
- `0x14135C050` — blindfire, жёстко screen-0 (алиасы `0x143EF1F20/0x143EF1F28`).
Доказательства индекса: `GetRetainedPlayerEntityBySlot(*(a1+248))` + джойпад
`0xBF0*(slot+2)` (совпадает со slot0→joypad2, slot1→joypad3) + фикс-алиасы screen-0.

**Фикс (coop_runtime.cpp, собрано+заинжекчено):** хуки на `0x14135A650` и
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
адреса в `FINDINGS_ARCHIVE.md`, запись "Runtime 2026-07-04 … REJECTED").

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

**Игра запущена (PID 14440/8524), DLL заинжекчена** — `scanner/build/tll_memory_reader.exe --pid N <addr> <len>`.

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
1. Обе копии с новой `tll_coop_runtime.dll`, ≥10 c после спавна.
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
- **Runtime:** built `scanner/build/tll_coop_runtime.dll` OK.
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
- **Runtime:** built `scanner/build/tll_coop_runtime.dll`; injected PIDs 10040/20400.
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
- **Runtime:** build OK (`tll_coop_runtime.dll`); two instances injected (PIDs vary).
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
- DLL: `scanner/build/tll_coop_runtime.dll`
- Запуск: `taskkill /F /IM tll.exe` → две копии → ~18s → `tll_dll_injector.exe --pid PID -- tll_coop_runtime.dll`
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
  Recorded as **HYPOTHESIS** in `FINDINGS_ARCHIVE.md`; runtime test pending.
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

All work is **runtime injection** (`scanner/src/coop_runtime.cpp` + `tll_dll_injector.exe`).
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
- Tooling: the old WSL launch/decompile helpers were retired; use
  `scripts/launch_coop.ps1 -Count 2` and native IDA MCP.
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

**Mod implementation (`coop_runtime.cpp`, `coop_protocol.h` v2):**
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
  `0x141A8FC00` with ABI `rcx=fg, rdx=translation+quaternion, r8b=0`.
- Therefore the previous post-Player-update `Player+0x140` writes could be true in
  logs while visually inert: the renderer consumed an already separated FgAnimData
  snapshot. Do not rotate/copy the full 651-joint palette for body yaw.
- Evidence, alternatives and runtime prediction:
  `reports/player_aim_fganim_world_transform_20260715.md`.

### Build cz REJECTED; build da corrects transform layout

`0x141A8FC00` calls a near relay that gates by the cached remote
`Player+0x1A8` FgAnimData and a fresh peer ADS packet. It replaces only the
quaternion in the native phase-`0x40` transform block, preserves translation and
the publish flag, then calls untouched `FgAnimData_PublishWorldTransform`.
Non-puppet and non-ADS publications are pass-through.

**cz runtime REJECTED:** remote models disappeared immediately on ADS for every
weapon. The initial wrapper wrote yaw to block `+0x00`, but exact matrix-builder
assembly proves `+0x00` is translation and `+0x10` is quaternion. Build da writes
only `transform[4..7]`, preserving native translation. Periodic diagnostics are
removed; a single first-match line is retained. The corrected runtime prediction
remains the one recorded in the report above.

### Build da runtime: yaw reached rendering; build db fixes axis and pitch basis

The user confirmed build da now turns the visible remote during ADS. The result is
precise but partial: facing is exactly 180 degrees inverted, vertical camera motion
does not affect the pose, and ADS entry can occasionally fold.

Build db negates the horizontal axis only at the late FgAnim quaternion publish.
Because that publication now owns visible yaw, PlayerAimController slot `+0x58`
receives the normalized horizontal camera `+Z` as its logical body-forward while
the full target retains camera Y for up/down additive weights. Calls that directly
write output `Player+0x1720/+0x1730` are removed from both pre/post phases; the
native `Player_PostAnimUpdate 0x1410E9210` remains their sole writer. Evidence,
competitors and runtime prediction:
`reports/player_aim_fganim_axis_residual_pitch_20260715.md`.

### Build dc runtime; build dd ready for transition test

Build dc preserved correct horizontal ADS facing, but the user isolated two
transition defects: ADS entered directly from idle is clean while run-to-ADS can
fold, and walking while ADS is held makes the puppet retain one stale aim point.
The receiver also captured a high-blend vertical sample (`blend=0.866667`,
`up=0.167019`, camera `y=-0.603211`) without visible pitch. Direct FgAnim aim-add
mirroring is therefore runtime-rejected as the vertical consumer and parked.

Static audit found the movement mismatch: input/world-basis/render-yaw were
stationary-only, but controller blend tick, eligibility, additive permission and
blend target remained forced for all peer ADS. Build dd puts the entire mod-owned
ADS chain behind one stationary predicate with a 250 ms dwell and clears the forced
ADS flag immediately on movement. Prediction: no stale aim point while walking;
run-to-ADS enters cleanly after the short dwell; stationary horizontal facing stays
correct. Vertical remains the next static task below native channel publication.
Report: `reports/player_aim_fganim_axis_residual_pitch_20260715.md`.

### Build dd partial runtime; build de adds established-ADS locomotion

The user confirmed dd no longer retains a stale aim direction during movement, but
it also drops ADS completely and shows ordinary running. The stationary check must
gate ADS entry, not the lifetime of an already established GunMove session.

Build de uses a two-phase state machine: 250 ms stationary dwell opens ADS; the
existing 180 ms controller ramp must then reach 0.95 before a moving-ADS session is
latched. Once established, movement keeps all ADS owners active until RMB release.
Run-first ADS remains blocked; a 500 ms evaluation gap or retained Player pointer
change resets the latch across loads. Prediction: stand/aim briefly, then move ->
authored aim-walk remains; start running before aim -> no folded ADS admission until
stopping. Vertical is unchanged and remains the next downstream static task. Full record:
`reports/player_aim_fganim_axis_residual_pitch_20260715.md`.

### Build de runtime; build df stages running admission through GunMove

The user confirmed established aim-walk remains active, but its fixed post-update
position nudge makes the slower gait slide, and de's stationary timer prevents
run-first ADS entirely. Build df separates button/controller admission from visual
pose ownership: after 250 ms held ADS, native input plus eligibility/permission may
transition out of Run; pose, blend tick, world basis and late FgAnim yaw open only
after a GunMove-family hash remains stable for 100 ms. The session still latches at
blend 0.95 and retains all lifecycle resets.

`PlayerGunMove_GetConfiguredMoveSpeed 0x140E86980` and the sole Player+0x3328
writer at spawn reject live speed-field mirroring. Build df instead retains the
validated `0.05 m` post nudge for ordinary locomotion and uses `0.015 m` only while
the staged aim pose is admitted. Prediction and competitors are recorded in
`reports/player_aim_fganim_axis_residual_pitch_20260715.md`. Vertical remains a
separate downstream FgAnim/palette-consumer task.

### Build df runtime; build dg aligns turn/strafe state with render yaw

df is partially confirmed: run-first ADS now enters without the old folded pose,
and aim-walk is improved. The remaining sharp full-turn entry, stationary foot
slide and partial moving foot slide share a late-owner mismatch. The phase-0x40
FgAnim hook currently snaps directly to final camera yaw after GunMove has already
selected its foot action from controller `+0x140/+0x150`.

Build dg rate-limits final FgAnim yaw to 240 degrees/second on the shortest arc and
re-enables `GunMoveController+0x150` only after df's stable GunMove timestamp, so
native `aim-idle-rotate` and strafe selection can animate the feet. It deliberately
does not re-enable the rejected post-root sync and performs no retained-table lookup
inside the GunMove hot path. Prediction and historical competitors are recorded in
`reports/player_aim_fganim_axis_residual_pitch_20260715.md`.

### Build dg runtime; build dh pre-turns before weapon pose

dg is runtime-rejected as a complete foot fix: the puppet still rotates with
planted feet, slides strongly in moving ADS, and briefly aims in the old/wrong
heading before render yaw catches up. Static re-open of `0x140E8EE90` shows why:
controller `+0x140` is normalized movement direction, while historical native
negative-Z `+0x150` converged with it and suppressed the 3/4 turn states. The final
FgAnim owner visibly uses the opposite positive-Z axis.

Build dh feeds that render-positive-Z only to the native animation selector,
pre-turns the smoothed FgAnim body after stable GunMove, and withholds weapon pose
until yaw is within 12 degrees. It also disables the ADS post-transform nudge while
retaining pre-consumer native-vector correction. This is a discriminator for
turn/strafe graph ownership versus unanimated position correction; detailed
competitors and prediction are in the aim report.

### Build dh rejected; foot-state inputs split into stationary and moving paths

The user reports no visible improvement from dh: planted-foot turning and
aim-walk slide remain, while positional desynchronization is only the prior
moderate amount. This rejects the ADS post-transform nudge as the primary slide
cause and rejects `GunMoveController+0x150` as a moving strafe parameter.

Static re-open shows why. `0x140E8F720` can choose stationary turn states 3/4
only at zero movement and behind authored settings plus active yaw-rate
hysteresis. Its `FgAnimData+0xD4` writer is suppressed in child state 2, so this
is turn continuation rather than a proven cold-idle initiator.

Moving construction at `0x140E8D140` loads controller owner `+0x108` and copies
**Player** `+0x100..+0x14F`, including local-root quaternion `Player+0x140`, into
the authored strafe graph. The late FgAnim yaw does not update that earlier
snapshot. The next narrow discriminator will substitute yaw only around action
construction and immediately restore the native root; it will not re-enable the
rejected sustained pre/post root ownership. Detailed record:
`reports/gun_move_controller_foot_state_gates_20260715.md`.

Build di implements that boundary only at the `0x140E9063B` direct builder call
inside GunMoveController vtable `+0x50`. It converts relayed world yaw to
Player-local yaw through parent quaternion `+0x120`, stages `Player+0x140` only
while `0x140E8D140` copies the action snapshot, then restores it before the native
update resumes. Prediction: wrong-side ADS entry and moving gait improve, while
stationary planted-foot turning remains for the separate cold-state initiator
pass. A lifecycle kneeling regression rejects the snapshot boundary.

Build di produced no visible progress. Installation and ADS/FgAnim were confirmed,
but its wrapper marker used production-disabled runtime logging, so action-snapshot
delivery is still unknown. Build dj changes no behavior and writes exactly one
boot marker per process on the first staged builder call, including child state,
movement magnitude and root quaternion dot. This distinguishes an inert snapshot
from an action built before or outside the admitted `0x140E9063B` path.

Build dj confirms the latter: synchronized ADS/FgAnim ran, but neither process
entered the late transition-builder wrapper. Static xrefs select vtable `+0x28`,
`GunMoveController_InitializeActionState 0x140E8E830`, whose call at
`0x140E8E8C1` builds initial state 0/103 and resets movement state. Build dk routes
both exact builder callsites through the temporary snapshot. Its one-shot marker
must now appear on ADS entry; a non-unit root dot with unchanged visuals rejects
the stale-snapshot hypothesis.

Build dk delivered the initial snapshot (`child=103`, `rootDot=0.0152348`, nearly
180 degrees different) but produced no visual progress. This rejects the action
snapshot as the visible entry/gait owner and the callsite hooks are parked again.
The user's roughly one-second wrong-side correction instead matches the final
FgAnim limiter: a new session starts from old transform yaw and needs 0.75 seconds
to cover 180 degrees at 240 degrees/second, plus admission time.

Build dl starts a new FgAnim ADS session directly at peer target yaw and retains
shortest-arc rate limiting only for subsequent camera motion. Prediction: the first
visible ADS frame faces correctly; later turning remains smooth. Planted/sliding
feet are expected to remain as a separate lower-priority child-state issue.

Build dl is runtime-rejected as the initial aim-ray fix. The weapon still begins
on the old point, an immediate shot goes to that same point, and the character only
turns afterward. This rejects final FgAnim yaw timing as the ray owner and selects
the earlier caller-scoped screen-1 target path.

Build dm opens the peer pose seed immediately only at
`GetTargetPointFromReticle`'s resolver return site `0x140CB17D6`. The full
`Player_PostAnimUpdate` seed copy at `0x1410E934B` remains behind staged GunMove
and render-yaw admission, and broad screen-1 resolver ownership remains disabled.
Bounded boot markers distinguish early target delivery from the native puppet fire
resolver path. Evidence, competitors and prediction:
`reports/player_aim_initial_ray_latch_20260715.md`.

Build dm is runtime-rejected as the visual turn fix. PID 18764 logged both
`AIM-TARGET-EARLY ... poseReady=0` and `AIM-FIRE-SEED`, but the puppet still turns
through a wrong/jittered heading and returns to its original heading on RMB-up.
This confirms target/fire delivery and exposes the ADS-only FgAnim publication
lifetime as the return cause.

Build dn will relay the local player's exact quaternion from
`FgAnimData_PublishWorldTransform 0x1418C55D0` in StatePacket v9 and apply it at
the same puppet publication boundary while packets are fresh, including after ADS
release. It removes camera-axis guessing and performs no Player root, derived
transform or movement writes. Full evidence and prediction are appended to
`reports/player_aim_initial_ray_latch_20260715.md`.

Build dn is runtime-confirmed as the visible horizontal owner. Fresh PIDs `2688`
and `8736` both logged `FGANIM-WORLD-SEND` and `FGANIM-WORLD-APPLY` with v9
packets, and the user reports materially better aiming. Three independent child
problems remain: abrupt 20 Hz/render stepping, planted feet during turn-in-place,
and no vertical pitch. Continue those as interpolation, locomotion child-state,
and downstream vertical-additive traces; do not return to rejected broad Player
root or screen-camera writes.

Build do targets only the confirmed horizontal stepping: v9 currently applies a
potentially large exact quaternion by one-frame `memcpy`, while two-instance game
updates deliver samples substantially slower than the nominal 16 ms lower bound.
The receiver will hemisphere-align, normalize, and shortest-arc smooth at the same
`0x1418C55D0` boundary, with sender sample-age and FgAnim-owner lifecycle guards.
Prediction and competitors are in
`reports/player_aim_initial_ray_latch_20260715.md`. Feet and vertical pitch remain
separate static tasks.

Build do runtime preserves horizontal response but leaves vertical pitch and
turn-in-place feet unchanged, confirming the predicted channel split. PID `17288`
logged both `AIM-TARGET-EARLY` (`0x140CB17D6`) and `AIM-FIRE-SEED`
(`0x140E2BFFF`), yet the user sees the bullet tracer fly completely away from the
reticle. Both seed substitutions are therefore delivered but rejected as the
final projectile-direction owner. Trace fire-argument/projectile launch data next;
do not alter render yaw to compensate.

Build v10 is ready for the next runtime discriminator. It appends exact native
pitch values and stationary GunMove selector inputs to the v9 packet prefix,
corrects reversed up/down active-state offsets, and pairs the foot selector with
writer suppression. Static fire tracing resolves
`ProcessWeapon::vtable+0x870 -> 0x1415182E0 -> 0x141518FC0`, but the latter
overwrites FireArgs `+0x10` and applies spread. The final common boundary is
direct callsite `0x14151B72B -> 0x141520750`, which writes post-spread `R9` to
launch record `+0x30`; tracer and collision both consume that field. FirePacket
v3 captures there and substitutes only receiver `record+0x30`, preserving its
local origin and muzzle. Do not re-enable direct PlayerFireController::Fire
replay. Full evidence and the
written prediction are in `reports/aim_pitch_foot_fire_owner_20260716.md`.

Build `dp` runtime rejects all three selected receiver boundaries. Both processes
installed the hooks and exchanged v10 packets, but vertical weapon motion, foot
steps and projectile direction were unchanged. None of the receiver markers
(`AIM-PITCH-APPLY`, `AIM-FOOT-*`, `FIRE-FINAL-DIRECTION-APPLY`) fired. Do not
retry these callsites. Next static pass must resolve the puppet's ticking pitch
consumer, cold turn-state initiation, and which of all three
`BuildProjectileLaunchRecord` callsites serves the observed instant bullet.

### 2026-07-18 recovered turn: local projectile spawn record corrected

**STATIC, quota complete; runtime not yet run.** The recovered uncommitted change
had broadened the direction hook to all three calls from
`ProcessWeapon_ProjectileEmission 0x141519D30`. Re-auditing the complete control
flow rejects that as unnecessarily broad and corrects the old ownership claim:

- `0x14151B72B` builds `v590`, which is stored in the descriptor used by
  `SendNetEventEmitProjectileBullet` and is not the local single-bullet spawn
  record. This explains why build `dp` could miss the receiver's actual local
  boundary. Do not reinstall this site.
- `0x14151BC3E` builds `v628`; the caller stores `v628` in the
  `ProjectileBulletInstant` spawn descriptor immediately before
  `ProcessWeapon::SpawnProjectile 0x141519B50`.
- `0x14151A00E` is the mutually exclusive online-definition branch reached only
  when `ProcessWeapon+0x906` projectile count is greater than one. Its record is
  stored at `SpawnInfo+0x28` for `MultiProjectileBulletInstant`.

Three independent evidence types now converge: (1) decompiled branch conditions
separate single/default and online multi-projectile paths; (2) record dataflow
separates the `v590` net-event descriptor from the `v628`/`v608` local spawn-info
pointers; (3) the typed `ProjectileBulletInstant::Init 0x14152BF20` path reads
the spawn-info pointer at `a3+0x28` and consumes launch record direction `+0x30`,
with the same class continuing into `ProjectileBulletInstant::ProbeKick
0x14152C9B0`.

The implementation now hooks only local spawn-record callsites
`0x14151A00E/0x14151BC3E`. Competing hypotheses are explicit: the old
`0x14151B72B` network-record owner is REJECTED; a below-builder tracer-only owner
remains possible but would be falsified separately; the two retained sites cover
multi-projectile and ordinary instant-bullet local construction without touching
grenade/rocket switch cases.

**Falsifiable prediction:** on an ordinary peer pistol/rifle shot the receiver
must emit `FIRE-FINAL-DIRECTION-APPLY` with `site=0x151BC3E`; a multi-projectile
weapon must use `site=0x151A00E`; neither side may report `0x151B72B`. The receiver
tracer and collision must follow the sender post-spread direction while native
origin, muzzle and event delivery remain unchanged. If apply fires at the
predicted site but only the tracer changes, common record `+0x30` ownership is
rejected; if neither retained site fires, the puppet does not use this
`ProcessWeapon` emission path.

Release build succeeds. The real IDB was saved and its MCP copy refreshed with
new STATIC labels `ProcessWeapon_SpawnProjectile 0x141519B50`,
`ProjectileBulletInstant_Init 0x14152BF20`, and
`ProjectileBulletInstant_ProbeKick 0x14152C9B0`. Runtime remains pending.

**Runtime attempt 2026-07-18, PIDs 16660/9932: INCONCLUSIVE, not REJECTED.** Both
processes installed the two local relays and remained responsive; the user still
observed the peer bullet travelling in the old wrong direction. A role-B shot
reached `AIM-FIRE-SEED`, then a role-A-focused retry was made, but neither attempt
emitted `FIRE-FINAL-DIRECTION-CAPTURE/ARM/APPLY`. Those markers were all gated by
already-prepared packet state, so their absence cannot distinguish a missed
launch-record callsite from an earlier FirePacket preparation failure.

Build `dq` adds a one-shot, behavior-neutral marker at hook entry before either
condition: `FIRE-LAUNCH-RECORD-HIT site=... roleB=... pending=...`. Written
prediction: ordinary instant fire must hit `0x151BC3E`; `pending=1` followed by
capture confirms sender integration, `pending=0` isolates the break above the
builder in `PlayerFireController::Fire` packet preparation, while no HIT on
either process rejects this `ProcessWeapon` emission family for the observed
projectile. `0x151A00E` remains the distinct multi-projectile prediction.

**Runtime CONFIRMED (build `dq`, PIDs 19036/19900):** one ordinary role-A shot
hit exactly `0x14151BC3E` in both processes. Role A logged
`FIRE-LAUNCH-RECORD-HIT site=0x151BC3E roleB=0 pending=0`; role B logged the same
site with `roleB=1 pending=0` after its puppet `AIM-FIRE-SEED`. Both processes
remained responsive. This confirms the local single-bullet callsite and rejects
the old `0x14151B72B` network record as the receiver boundary. Direction remained
unchanged because neither side had an armed final-direction packet; the
callsite/record hypothesis itself is not rejected.

Live `ReadProcessMemory` also confirms that `PlayerFireController::Fire
0x140E2C730` is patched to `hooked_player_fire` in both processes. Remaining
competitors are therefore: (A) `prepare_fire_packet` rejects the role-A context,
or (B) it succeeds on the Fire thread but the later emission reaches
`0x14151BC3E` after the `thread_local` pending state has been cleared or on a
different thread. Build `dr` logs one role-A `FIRE-PACKET-PREP ok/tid/...` and
adds `tid` to the builder HIT. Prediction: `ok=0` selects A; `ok=1` plus
`pending=0` selects B, with differing tids proving a thread handoff and equal tids
proving a later same-thread lifetime gap.

**Runtime CONFIRMED (build `dr`, PIDs 3176/14556):** role A prepared the packet
successfully on thread `2032` (`FIRE-PACKET-PREP ok=1`) and then reached the
ordinary local record builder on worker thread `20032` with `pending=0`. Role B
reached the same `0x14151BC3E` site on thread `20196`. This proves competitor B:
the packet-to-final-direction bridge crosses a real thread handoff, so the old
`thread_local FirePacket` cannot work. Packet construction, hook installation and
the resolved local builder are not the failure.

Build `ds` replaces the single-thread bridge with a process-wide, 16-entry FIFO
protected by `SRWLOCK`. It has three independent ownership/safety grounds:
(1) runtime thread IDs prove producer and consumer are different threads;
(2) `ProcessWeapon::ProjectileEmission 0x141519D30` passes the resolved
`pSourceGo` from its `a3+0x20` weak handle as builder argument 2, so consumption
is gated to `owner == retained_player_object(0)` and enemy projectiles cannot take
a packet; (3) `FirePacket` is a fixed 304-byte POD with a monotonic sequence, so a
bounded FIFO preserves rapid-fire order without sharing borrowed engine pointers.
Entries not consumed within 250 ms are sent without the final-direction flag,
preserving non-projectile variants; queue overflow also preserves the oldest
event instead of dropping it.

Competing implementations are rejected for this exam: a process-global singleton
would overwrite automatic-fire events; sending an immediate base packet followed
by a correction would replay the same shot twice at the receiver; blocking the
game thread until the worker runs would introduce a scheduler deadlock/stall
hazard. The FIFO changes only the already-confirmed sender correlation boundary.

**Falsifiable build-`ds` prediction:** one ordinary role-A shot must log
`FIRE-LAUNCH-RECORD-HIT ... pending=1` on its worker, followed by
`FIRE-FINAL-DIRECTION-CAPTURE site=0x151BC3E ... age=<250` and
`FIRE-RELAY send ... final=1`. Role B must arm the same sequence and log
`FIRE-FINAL-DIRECTION-APPLY site=0x151BC3E`. It must not log timeout/overflow for
that shot. If the local HIT has `pending=1` but no capture, the direction pointer
contract is false; if capture/send succeeds but role B does not apply, receiver
arming/ownership is false; if APPLY fires yet the bullet still travels along the
old ray, launch-record `+0x30` is not the collision/tracer owner.

**Build `ds` runtime boundary CONFIRMED (PIDs 19700/2252; visible result pending):**
the user fired several upward pistol and rifle shots. Role A prepared on thread
`4744`, reached ordinary builder `0x14151BC3E` on worker `19680` with `pending=1`,
and captured sequence 1 after 16 ms with final direction
`(0.1951, 0.9057, 0.3764)`. Role B armed a received pistol-family direction and
reached the same local builder on worker `16148`, then logged
`FIRE-FINAL-DIRECTION-APPLY site=0x151BC3E seq=6 dir=(0.1955,0.9064,0.3744)`.
Neither process logged FIFO timeout/overflow and both remained responsive. This
confirms the cross-thread sender FIFO and the complete packet/capture/receiver
apply chain; visible tracer/collision ownership still requires the user's direct
observation before promotion to behavior CONFIRMED.

The multi-shot run also exposes a separate receiver correlation limit: the first
one-shot ARM marker saw sequence 5, while the first APPLY marker consumed sequence
6. `g_peer_fire_direction` is a single protected slot, so a later packet can
replace an earlier direction before its asynchronous puppet builder consumes it.
All test shots were aimed similarly upward, so this does not falsify the current
visible-direction exam, but rapid aim/weapon changes require a receiver-side FIFO
if the user confirms that exact per-shot correlation is visibly relevant.

**REJECTED by visible runtime result:** despite the exact upward vector reaching
receiver `launch_record+0x30`, the user observed both pistol and rifle bullets
travelling strongly sideways, not upward. Therefore successful CAPTURE/ARM/APPLY
does **not** confirm `+0x30` as a standalone world-space owner of visible tracer
and collision direction. Do not retry this relay and do not promote the builder
boundary to behavior CONFIRMED. The cross-thread FIFO itself remains confirmed as
a transport mechanism, but applying only this field is behaviorally wrong and is
now parked. Test PIDs 19700/2252 were stopped after the result.

Competing static explanations reopened by this rejection are: (A) `+0x30` is a
direction expressed in a source/local basis and must be transformed for the
receiver; (B) it is world-space but is only one member of a launch transform whose
other basis/quaternion fields remain native and rotate it later; (C) it is a
secondary tracer/auxiliary vector while collision direction is owned elsewhere;
(D) the single receiver slot's sequence skew paired a direction with the wrong
projectile, although similarly upward test shots make D insufficient by itself to
explain a strong sideways result. Next work is static-only: trace every field
written by `BuildProjectileLaunchRecord 0x141520750` through
`ProjectileBulletInstant_Init 0x14152BF20` and downstream trace/collision updates,
resolve the transform basis, and select a new boundary only after the evidence
quota is met.

### 2026-07-18 post-rejection static correction: receiver object correlation

**STATIC, quota complete; build `dt` runtime not yet run.** The full dataflow
rejects the earlier local-basis/secondary-vector explanations for a correctly
owned `ProjectileBulletInstant` record and reveals a missing object discriminator:

1. Exact callsite disassembly distinguishes the two vectors that prior comments
   conflated. At `0x141519EFB`, normalized direction `xmm6` is stored in
   `var_EA8`; `sub_1414F9EE0(out=var_EB8, in=var_EA8, spread...)` produces the
   spread result. Network-record call `0x14151B72B` passes `R9=var_EB8`, while the
   ordinary local-record call `0x14151BC3E` passes `R9=var_EA8`. Build `ds`
   therefore relayed the sender's normalized local-launch direction, not the
   post-spread network-event direction.
2. `ProjectileBulletInstant_Init 0x14152BF20` loads record `+0x30`, builds the
   endpoint bound, reads its current world translation through
   `BoundTransform_GetTranslationWs_B 0x140186600`, adds record `+0x30`, and calls
   `BoundTransform_SetTranslationWs 0x141686120`. The paired rotation setter
   `0x1416861E0` explicitly converts world quaternion to parent space, proving
   these helpers maintain a WS/PS contract rather than rotating `+0x30` as an
   unknown local vector.
3. `ProjectileBulletInstant_ProbeKick 0x14152C9B0` independently reads the start
   and endpoint bound translations via the identical
   `BoundTransform_GetTranslationWs_A 0x14014E240`, subtracts
   `endpoint_ws-start_ws`, scales it by projectile range, and feeds that ray to
   the collision probes. Thus, on the intended projectile, an applied vector with
   world Y about `0.906` cannot statically produce a strongly sideways ray.
4. A second independent consumer reaches the same field: base projectile setup
   `0x141521F00` copies launch record `+0x30` at `0x14152244D`, and the carried
   tracer record is normalized again by `Tracer::Init` at `0x14155C251`. Neither
   collision nor tracer consumer reads launch record `+0x40`. RTTI TypeDescriptor
   `0x14376EE38` and vftable `0x142A9D470` tie Init slot `+0x58` and main tick slot
   `+0x1B8` to `ProjectileBulletInstant`, excluding an accidental neighboring
   class interpretation.

The contradiction is explained by ownership. `ProcessWeapon::ProjectileEmission`
resolves descriptor `pSourceGo` (`a3+0x20` weak handle) and passes it as builder
argument 2, but build `ds` accepted the first matching-art record in role B from
this generic ProcessWeapon callsite. It never required
`owner == retained_player_object(1)`. The same run also proved the single receiver
slot could advance ARM `seq=5` to APPLY `seq=6` before a worker consumed it. The
callsite is shared by all shooters, so a log saying APPLY at `0x151BC3E` did not
prove it modified the puppet projectile the user was watching.

Competing hypotheses after the quota: (A) wrong `pSourceGo` record consumption is
the primary cause and is selected by the exact missing predicate; (B) receiver
sequence overwrite is independently CONFIRMED and can mispair rapid shots, but
cannot alone turn several similarly upward vectors sideways; (C) a later
trajectory overwrite is disfavored by the Init-to-ProbeKick endpoint dataflow, but
remains the falsifier if an owner-correct apply is still visibly wrong.

Accordingly, the visible build-`ds` rejection falsifies raw ungated cross-process
transfer/correlation, not the static ownership of record `+0x30` itself. The field
returns to STATIC for an owner-correct record; only build `dt` can promote it to
behavior CONFIRMED.

Build `dt` keeps the sender FIFO, replaces the receiver single slot with a
16-entry, 500-ms FIFO, and consumes it only when builder argument 2 equals retained
puppet slot 1. It adds bounded HIT fields `owner0/owner1/peerQ`.

**Falsifiable build-`dt` prediction:** for one role-A upward pistol shot, role A
must log `owner0=1 pending=1`; role B must log `owner1=1 peerQ>=1`, then APPLY the
oldest matching packet sequence at `0x151BC3E`. No non-puppet record may consume
the queue. The visible role-B tracer/collision must travel upward. If role B first
hits with `owner1=0` and never later reaches `owner1=1`, build `ds` was modifying
an unrelated projectile and this call family is not the puppet boundary. If
`owner1=1` and the same sequence is applied but the shot remains sideways, reject
record-level mutation despite the static endpoint dataflow and move the next
boundary below Init to the concrete ProbeKick ray endpoints.

### 2026-07-18 build dt runtime: owner-correct record mutation still sideways

**REJECTED.** One ordinary role-A pistol shot aimed vertically upward falsified
the build-`dt` prediction. Sender PID 13824 reached the ordinary local-record
callsite with `pending=1 owner0=1` and captured `seq=1`
`dir=(0.0100,0.9995,0.0302)`. Receiver PID 19568 armed `seq=1`, then reached the
same `0x14151BC3E` callsite with `owner1=1 peerQ=1` and logged an applied nearly
vertical capture, `seq=2 dir=(0.0133,0.9992,0.0372)`. Both processes remained
responsive. The user nevertheless observed the bullet travelling strongly
sideways again.

This rejects missing-owner discrimination as a sufficient explanation. The
`ARM seq=1` / `APPLY seq=2` transition also shows that one physical trigger can
produce or retain more than one fire event; the receiver FIFO removed overwrite,
but exact one-to-one projectile identity is still not proven. Static ownership of
launch record `+0x30` remains strong, while the assumption that this mutated record
is the record consumed by the visible/collision projectile is now REJECTED.

Competing explanations at the next boundary are: (A) the record returned from
`0x14151BC3E` is copied or overwritten before the concrete
`ProjectileBulletInstant` consumes it; (B) duplicate/parallel projectile creation
means the modified owner-correct record belongs to a non-visible instance while a
different instance supplies the observed tracer; (C) the intended instance reaches
`ProjectileBulletInstant::ProbeKick`, but its live endpoint no longer equals the
applied vector; (D) if the live `endpoint_ws-start_ws` remains vertical while the
visible shot is sideways, collision and the observed effect are separate runtime
objects despite their static shared field.

The next runtime test is not authorized until a read-only discriminator is placed
at `ProjectileBulletInstant_ProbeKick 0x14152C9B0`: log the concrete instance,
start/end world translations, normalized `endpoint-start`, and its dot product
against the most recently owner-correct applied direction. Prediction: A/B/C give
either no matching instance or a low dot product; D gives `dot` near 1 while the
user still observes a sideways tracer.

### 2026-07-18 post-dt static correction: authoritative selected launch record

**STATIC, quota complete; build `du` runtime not yet run.** Exact post-builder
dataflow rejects the project's earlier assertion that `0x14151B72B` is network
only. The `dt` mutation was owner-correct but targeted a record that is not selected
for an ordinary single projectile:

1. `0x14151B72B` builds post-spread record `var_D18`. After optionally replacing
   the descriptor class with `MultiProjectileBulletInstant`, instructions
   `0x14151BB9F..0x14151BBAB` unconditionally store `&var_D18` at
   `SpawnInfo+0x28`. `SendNetEventEmitProjectileBullet 0x14151BBEF` consumes the
   same record, but it is not its only consumer.
2. `0x14151BC3E` builds pre-spread `var_A68`, but `cmp word ptr [rsi+0x906],1` at
   `0x14151BC43` jumps directly to spawn for count <= 1. Only count > 1 reaches
   `0x14151BE21..0x14151BE33` and overwrites `SpawnInfo+0x28 = &var_A68`.
   Therefore an ordinary pistol/rifle keeps `&var_D18`; executing BC3E does not
   prove that its result is live.
3. `ProcessWeapon_SpawnProjectile 0x141519B50` receives that completed SpawnInfo,
   and typed `ProjectileBulletInstant_Init 0x14152BF20` reads exactly its `+0x28`
   pointer before consuming record `+0x30`. This selects B72B for single and BC3E
   only for the guarded multi case.
4. The independent build-`dt` runtime matches the branch prediction: BC3E was
   reached with `owner1=1` and mutated to world-up, yet the visible single pistol
   remained sideways. That is the expected result when `SpawnInfo+0x28` still
   points to B72B's `var_D18`.

Competing explanations are now separated at a later authoritative boundary:
(A) selected-record pointer mismatch was the `dt` cause and is favored by the exact
branch; (B) selected record reaches Init but a later overwrite rotates the live ray;
(C) the live ProbeKick collision ray is corrected while the observed tracer belongs
to a separate instance/effect. Directly hooking builders cannot distinguish these.

Build `du` moves both sender capture and receiver apply to
`ProcessWeapon::SpawnProjectile`, after `SpawnInfo+0x28` has selected the actual
record. Ownership is checked through the record's `{owner+0xA8, owner+0xBC}` handle,
not by callsite. Receiver writes both duplicated direction fields `+0x30/+0x40`.
A read-only `ProjectileBulletInstant_ProbeKick` discriminator logs the concrete
puppet instance's normalized `endpoint_ws-start_ws` and dot product against the
applied sequence.

**Falsifiable build-`du` prediction:** one role-A pistol shot aimed vertically up
must produce sender `FIRE-SELECTED-RECORD-HIT owner0=1 pending=1` and
`FIRE-SELECTED-DIRECTION-CAPTURE`; role B must produce selected `owner1=1 peerQ>=1`,
`FIRE-SELECTED-DIRECTION-APPLY`, then `FIRE-PROBE-RAY` for the same sequence with
world-Y near 1 and `dot >= 0.99`. The visible role-B tracer/collision must travel
upward. APPLY without a matching ProbeKick instance rejects the selected record as
the instant-bullet input; low dot rejects Init preservation; dot near 1 with a
sideways visible tracer proves a separate visual instance/effect.

### 2026-07-18 build du runtime: selected ray confirmed, unsafe ProbeKick detour crashes

**REJECTED as a runnable build; selected-record dataflow runtime-confirmed.** One
role-A pistol shot caused both processes to terminate. Before termination, all
direction discriminators completed successfully. Sender PID 13200 selected an
`owner0=1` record and captured `seq=1 dir=(-0.3222,0.8839,0.3390)`. Receiver PID
5276 selected an `owner1=1` record, applied the same `seq=1` over native
`before=(0.7920,-0.2516,0.5562)`, and reached concrete instance `0x10E1020280`
after 16 ms. Its live ProbeKick ray was
`(-0.3222,0.8839,0.3390)` with `dot=1.00000`. This CONFIRMS that mutation of the
authoritative `SpawnInfo+0x28` record survives Init and owns the actual collision
ray; the prior BC3E-only boundary was the error.

The crash is caused by the ProbeKick entry trampoline, not by a failed direction
prediction. `ProjectileBulletInstant_ProbeKick 0x14152C9B0` begins with
`mov rax,rsp` and keeps that entry-stack pointer live through saves at
`0x14152C9CD..0x14152C9E4`. Build `du` relocated only the first 14 bytes, then the
generic trampoline's `mov rax, jump_back; jmp rax` destroyed the live stack
pointer before those saves. Both local and puppet bullets traverse this hook,
explaining the symmetric termination on one shot.

Do not retry build `du`. Build `dv` must leave the ProbeKick function entry
untouched and relay only the direct call at `ProjectileBulletInstant::Init
0x14152C8A6 -> 0x14152C9B0`; the helper may call the unpatched original normally.
This preserves the already-confirmed selected-record mutation and discriminator
without relocating a live prologue register. `TryExecuteCachedProbe 0x141531180`
can bypass this direct call through slot `+0x48`; therefore absence of a dv marker
is INCONCLUSIVE, while a present marker remains decisive.

Build `dv` implements that exact correction and compiles successfully. The
ProbeKick entry is no longer patched; a preflight-verified rel32 relay replaces
only call `0x14152C8A6`. The selected SpawnInfo relay and all packet/owner logic are
unchanged. **Runtime prediction:** the same single upward pistol shot must repeat
selected owner/capture/apply and `dot >= 0.99`, both processes must remain
responsive after ProbeKick returns, and the visible shot must follow the logged
ray. A crash before/after the marker rejects a different hook boundary and must be
recorded separately; do not attribute it to direction ownership.

### 2026-07-18 build dv runtime: projectile final direction confirmed

**CONFIRMED.** One role-A pistol shot aimed vertically upward completed without a
crash. Sender PID 11148 selected the owner0 record and captured
`seq=1 dir=(-0.0219,0.9996,0.0190)`. Receiver PID 1464 selected the owner1 record,
applied the same sequence over native `before=(0.7346,-0.1003,0.6711)`, and reached
concrete `ProjectileBulletInstant` instance `0x10E1022D00` after 16 ms. Its live
ray was `(-0.0219,0.9996,0.0190)` with `dot=1.00000`. Both processes remained
responsive, and the user reported that bullets now appeared to travel correctly.

This closes projectile final-direction ownership for ordinary single
pistol/rifle shots: sender capture and receiver mutation belong at the selected
`ProcessWeapon::SpawnProjectile` `SpawnInfo+0x28` record, with collision direction
consumed from record `+0x30`. Keep build `dv`; do not restore any earlier builder or
ProbeKick-entry hook.

The user then identified a separate pre-existing weapon/ammo defect. Drawing a
weapon in role A can publish the same weapon icon and an inconsistent magazine
count into role B's HUD. Remote fire visually consumes that displayed count, but
switching control to role B reveals that its actual ammo did not change. If role
B's displayed/puppet magazine is already zero, incoming fire may produce no visible
shot. Treat HUD publication ownership and the puppet weapon's native fire-readiness
ammo as separate hypotheses until their fields and consumers are statically traced.

### 2026-07-18 STATIC: live firearm ammo and weapon-HUD ownership; build dw

The reported zero-ammo shot loss is not a cosmetic HUD-only failure. The live
`ProcessWeapon` owns signed `int16` magazine ammo at `+0x900` and reserve ammo at
`+0x902`; `WeaponInfo+0x84` is only the initial give count. Independent static
evidence converges on that layout:

1. vtable `+0x848/+0x858/+0x840/+0x860` at
   `0x141103830/0x141103840/0x141103850/0x141103870` directly return magazine,
   reserve, their sum, and `magazine<=0`;
2. `ProcessWeapon_InitializeAmmoFromTotal 0x141517C90`,
   `SetMagazineAmmo 0x141517CF0`, `ReloadMagazine 0x14151E0B0` and
   `ReloadOneRound 0x14151E2F0` write the same two fields and keep loaded-state
   bytes `+0x908/+0x909` coherent;
3. `ProcessWeapon_ConsumeShotAmmo 0x14151E740` decrements `+0x900` at
   `0x14151EB13` after projectile dispatch.

The original mismatch is deterministic at creation time even when it looks
random in the HUD. The whitelist fallback `safe_give_weapon_art_to_puppet`
builds a default `WeaponInfo` through `0x1401B7600`, which writes DWORD `-1` to
`WeaponInfo+0x84` at `0x1401B766B`. `PlayerWeaponController_GiveWeapon
0x1410A4D40` reads that field at `0x1410A588A`; `-1` selects the authored/random
ammo derivation branch `0x1410A589B..0x1410A591F`, whose total is passed into
`CreateAndAddWeapon` at `0x1410A5C3E`. Therefore the receiver puppet is born with
an independently derived total and cannot be expected to match the sender.

The decisive native gate is in
`PlayerFireController_BuildFireArgsAndFire 0x140E2BCD0`: at `0x140E2BD2D` it
calls ProcessWeapon vtable `+0x848` and returns before
`PlayerFireController_Fire` when the result is `<=0`; at `0x140E2BD41` it also
rejects vtable `+0x868`, whose concrete implementation `0x141103880` reports
`weapon+0x908==0`. Thus competitor **A is STATIC**: a receiver puppet whose live
magazine/loaded-state reached zero takes the dry-fire path and can suppress the
button-replayed remote shot. Competitor B (HUD number only) is rejected for this
symptom, and competitor C (later projectile visual gate) is unnecessary to
explain it.

The icon/count leak has a separate exact owner:
`PlayerHudScreen_WeaponAmmoPublisher 0x14135D2B0`. It reads screen index
`state+0xF8`, selects `g_PlayerHandleForHudPublish[2*screen]` and the per-screen
HUD block, reads the selected weapon through vtable `+0x840` total and `+0x848`
magazine, publishes `selected-weapon-id`, `total-ammo`, `loaded-ammo` and
`show-weapon-hud?`, then spawns the process-global `ss-hud-weapon`. Its sole
caller is `PlayerHud_ScreenStatePublishUpdate 0x140C91970` at `0x140C921A2`,
passing `g_PerScreenPlayerHudState + 0x100*player.screen_index`. Existing co-op
gates covered only the reticle publishers, so the screen-1 puppet publisher still
overwrote the single physical viewport's weapon HUD. This is **STATIC** and has
the same deterministic correction: skip this publisher when `state+0xF8 != 0`.
The puppet route is explicit rather than inferred from the primary HUD entry:
the `PlayerActiveAsyncJob` descriptor at `0x143071B70` names callback
`0x140CA1630`, which invokes `PlayerHud_ScreenStatePublishUpdate(player)` at
`0x140CA1655` for every active Player. The latter reads that Player's
`player+0x4010` screen index, so retained screen 1 reaches the publisher even
though the process also has a separate screen-0 global HUD entry.

Build `dw` implements the two isolated corrections. `FirePacket` v4 appends the
sender's pre-shot `magazine/reserve`; immediately before native replay or the
button fallback, the receiver validates weapon art and applies the snapshot only
to retained puppet slot 1's matching `ProcessWeapon`, including `+0x908=1` and
`+0x909=0`. Retained slot 0 (the receiver-local role-B player) is explicitly
excluded. Native fire remains responsible for consuming exactly one round.
Separately, a prologue-verified detour at `0x14135D2B0` passes screen 0 through
and skips screen 1. The Release DLL builds successfully.

**Falsifiable runtime prediction for build dw:** choose different visible ammo
counts on the two local players, let the sender's retained puppet on the receiver
reach/show zero, then fire one ordinary pistol or rifle shot with sender pre-shot
magazine `N>0`. Sender and receiver must log the same `ammo=N/R`; receiver must
log `FIRE-AMMO-APPLY target!=local0 before=0/... sender=N/R`, produce one visible
shot in the confirmed direction, and return from native fire with
`ammoPost=N-1/R`. `WEAPON-HUD gated: screen!=0` must appear when the peer draws or
fires, while the receiver's screen-0 weapon icon/count remains its own. Switching
control to the receiver-local player after the shot must show its original ammo
unchanged. Any absent shot after a successful ammo apply, `target==local0`, local
ammo mutation, or screen-0 HUD disappearance rejects the corresponding part of
the fix.

**Runtime observation (user, 2026-07-18): PARTIAL CONFIRMATION.** After the
ammo/HUD build the user reports that the situation is visibly better. This
supports the correction but does not yet close it: the explicit discriminator
for receiver-local ammo remaining unchanged after control switch was not
reported, so the task stays open rather than being promoted to fully CONFIRMED.

The same observation exposed the next ballistic defect: the remote tracer/impact
is displaced roughly one metre to the left. Because build `dv` already confirmed
the selected launch direction and live collision ray with `dot=1.00000`, treat
this as a separate **origin/muzzle transform** hypothesis, not a regression to
direction-vector candidates. Reload animation sync is queued after projectile
origin. Vertical ADS pose/movement remains independently open.

### 2026-07-18 STATIC: selected projectile origin relay; build dx

The roughly one-metre parallel left offset is now statically isolated from the
already-confirmed direction relay. `FireArgs+0x00` is the world-space aim target,
not an origin: `PlayerFireController_BuildFireArgsAndFire 0x140E2BCD0` stores the
target there at `0x140E2C3AD`. The replay helper's old replacement with
`Player+0x240` was semantically false; `Character_UpdateDerivedWorldTransformFromLocalRoot
0x141BB8320` proves `+0x240` is the character-root world translation. That false
write is removed. It could only affect a gated one-unit nudge along direction at
`0x141519631..0x141519720`, so it does not explain a persistent lateral line.

The real native origin is the authored weapon socket. ProcessWeapon vtable
`+0x8C8`, concrete `0x1414FE050`, resolves `muzzle`/`silenced-muzzle` from the
weapon attachment table and falls back to the weapon bound. ProcessWeapon vslot
`+0x410` at `0x141515860` stores that transform to `ProcessWeapon+0x970/+0x980`;
`ProcessWeapon_BuildAndDispatchProjectile 0x141518FC0` reads `+0x970`, computes
the final vector and passes it through slot `+0x920` to
`ProcessWeapon_ProjectileEmission 0x141519D30`. The selected builder passes that
origin as R8 to `BuildProjectileLaunchRecord 0x141520750`.

The record duplicates R8 at `+0x20` (`0x14152076B`) and `+0x70`
(`0x1415207A0`). Instant collision Init consumes `+0x20` at
`0x14152C2B7`, then computes endpoint = start + direction at
`0x14152C808..0x14152C829`. The independent base/tracer path constructs its bound
from `+0x70` at `0x141522169..0x141522174` and reads that world translation at
`0x14152245B..0x141522470`. Therefore the reported tracer+impact offset shares
one launch origin, and a complete correction must replace both copies. The
wrong-hand hypothesis is rejected because the stock receiver resolves the exact
authored muzzle socket; the likely mismatch is between two valid instance-local
muzzle transforms. Tracer-only is rejected by the duplicate consumer dataflow.

Build `dx` extends FirePacket to v5 with the sender's exact selected-record
origin. At the already runtime-confirmed `ProcessWeapon::SpawnProjectile
0x141519B50` boundary, sender owner0 captures `record+0x20`; receiver owner1/art
matching writes it to selected record `+0x20/+0x70` before native Init. Existing
direction `+0x30/+0x40` is unchanged. v4/v3/v1 receive compatibility remains.
The direct ProbeKick discriminator now reports
`originError=|live_start-relayed-origin|`. Release
`tll_coop_runtime.dll` builds successfully.

**Falsifiable build-dx prediction:** one distant-wall pistol shot and one rifle
shot must produce matching `FIRE-SELECTED-LAUNCH-CAPTURE`,
`FIRE-FINAL-LAUNCH-ARM origin=1`, and
`FIRE-SELECTED-LAUNCH-APPLY originApply=1`. The receiver's concrete
`FIRE-PROBE-RAY` must keep `dot>=0.99` and report `originError<=0.01`; both tracer
and impact must lose the previous parallel left displacement. A correct collision
with displaced tracer rejects the `+0x70` conclusion; low origin error with a
still-displaced collision rejects selected-record start ownership; low dot rejects
direction/origin isolation. Full evidence is in
`reports/remote_projectile_origin_20260718.md`.

The first user launch after this build did **not** exercise dx: both running
processes emitted the older `FIRE-SELECTED-DIRECTION-*` markers and none of
`FIRE-SELECTED-LAUNCH-*`, `originApply`, or `originError`. The launcher had loaded
the stale main DLL (13:34, 356352 bytes) while dx existed only as the v2 artifact
(14:03, 358912 bytes). The report that the offset remained but looked smaller is
therefore not classified as confirmation or rejection. The rebuilt artifact now
logs init tag `2026-07-18dx`; replace the main DLL only after both game processes
exit, then run the written pistol/rifle discriminator.

### 2026-07-18 CONFIRMED: selected projectile origin relay

Fresh PID 7688 (role A) and PID 16512 (role B) both loaded build `dx`. Pistol and
rifle fire produced selected-launch capture/apply with `originApply=1`; concrete
receiver `FIRE-PROBE-RAY` samples reported `dot=1.00000` and
`originError=0.00000`. The user confirmed that remote tracers and bullet impacts
now coincide exactly. Selected record `+0x20/+0x70` origin ownership is therefore
CONFIRMED, with the existing `+0x30/+0x40` direction relay preserved. Projectile
direction/origin work is complete; vertical ADS pose and reload animation remain
independent open tasks.

### Build db partial confirmation; build dc transition/gait correction

User runtime confirms the corrected FgAnim quaternion now faces the reticle. Three
failures remain: no visible vertical pitch, occasional folded ADS entry, and
sliding/desynchronization while moving under peer ADS. Receiver PID 8568 captured
`Aim residual-pitch ... y=-0.23907 up=0.543157 right=1 blend=0`: pitch reaches the
controller, but the first sample precedes blend admission.

Static re-audit found the mod mirror bypassed the exact native contract in
`PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0`: it copied raw
`+0x8B8..+0x8C4` without `+0x8EC`, skipped zero, and never cleared the FgAnim copy.
Build dc applies blend, clears on exit, and adds one high-blend pitch marker.

The movement failure is an ownership mismatch: native puppet ADS/pose input is
stationary-only after the prior kneeling-slide rejection, but build db's render yaw
ran for every peer ADS packet. Build dc gives slot `+0x58`, FgAnim mirror, and late
world yaw the same stationary gate; moving locomotion is native pass-through.
Detailed evidence and prediction are appended to
`reports/player_aim_fganim_axis_residual_pitch_20260715.md`.

### 2026-07-18 STATIC: move vertical channel apply to live blend tick (build dy)

Build `dp` chose receiver callsite `0x140CB4176`, but runtime emitted no receiver
`AIM-PITCH-APPLY`; the retained puppet does not run that computed-publisher edge.
The already-installed PlayerAimController vtable `+0x30` hook does tick for slot 1
and participates in the working ADS blend. IDA closes its ordering:
`PublishRuntimeStateFromDecisionNodeLikeObject 0x140CA17E0` calls the common
controller phase `0x140C9F420` at `0x140CA2952`; that phase dispatches
Player+`0x1D90` slot `+0x30` (`PlayerAimController_UpdateBlendState
0x140CAFA20`) before later Player publication/camera/attachment work.

`0x140CAFA20` updates blend `+0x8EC` and resets/primes `Aim0..Aim7`.
`PlayerAimController_CommitNamedAimChannel 0x140CAF470` is self-contained: it
resolves controller owner `+0x108`, Player animation owner `+0x1A0`, channel ids
at controller `+0x8C8`, and handles zero/clear. Build `dy` therefore commits the
peer's post-blend `aim-add-up=base+4` / `aim-add-down=base+6` immediately after
the original blend tick and clears them on ADS exit. Direct active-state/FgAnim
copies, full-body pitch and broad pose/root writes remain rejected competitors.
Release v2 builds. The real IDB labels `0x140C9F420`
`Player_UpdateMountedControllers` and saved successfully; refreshed MCP verifies
the name/address and caller `0x140CA2952`.

**Prediction:** a large role-A up/down ADS sweep produces sender
`AIM-PITCH-CAPTURE-ACTIVE`, receiver `AIM-PITCH-BLEND-APPLY`, visible remote
upper-body/weapon pitch, and `AIM-PITCH-BLEND-CLEAR` on release without changing
confirmed horizontal yaw or projectile alignment. Non-zero apply with no visible
pitch rejects named aim parameters as the puppet's rendered vertical consumer.
Full evidence: `reports/remote_aim_pitch_blend_tick_20260718.md`.

### 2026-07-18 REJECTED/STATIC: `dy` missed alternate channel mode; build `dz`

Fresh build-`dy` processes PID 20512/12628 produced horizontal-only remote ADS;
the user saw no vertical motion. Sender logged only zero capture, and receiver
emitted neither `AIM-PITCH-BLEND-APPLY` nor `AIM-PITCH-APPLY`. This rejects the
blend-tick application boundary, not named parameters: no named commit happened.
The receiver itself computed `up=0.270986` with `blend=0.95`, proving that the
directional vertical value is already local to the puppet publisher.

Static follow-up resolves the missed discriminator inside
`PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0`. After resolving
Player `+0x1A0`, a zero branch-control byte uses the named commit path; the
puppet's nonzero mode jumps to `0x140CB46F3` and writes float `0.2`
(`0x142BE8DFC`) to channel offsets `0,2,4,6`. Constructor `0x140CAE670` identifies
these as `Aim0/Aim2/Aim4/Aim6`. Native `0x14172C8E0` locates the existing registry
entry, stores its float and marks it dirty.

Build `dz` therefore removes the rejected blend-tick commit and, after the stock
publisher returns, writes receiver-local `up*blend/down*blend` to alternate
`Aim4/Aim6` through `0x14172C8E0`. Direct active-state/FgAnim copies, full-body
pitch and broad pose/root writes remain rejected competitors; named mode remains
unclassified because `dy` never entered it.

**Prediction:** a large role-A ADS up/down sweep produces
`AIM-PITCH-ALT-APPLY-ACTIVE` with nonzero vertical value and native set results,
plus visible remote upper-body/weapon pitch. Horizontal yaw and projectile
alignment remain unchanged; ADS exit leaves no stuck override. Successful active
sets without visual pitch reject alternate `Aim4/Aim6` as the rendered consumer.
Full evidence: `reports/remote_aim_pitch_blend_tick_20260718.md`.

### 2026-07-18 REJECTED/STATIC: `dz` hooked transition only; steady-state build `ea`

Fresh build-`dz` PID 9360/3108 again showed horizontal-only remote ADS. This does
not reject `Aim4/Aim6`: PID 3108 logged only
`AIM-PITCH-ALT-APPLY ... up=0 down=0 blend=0 setUp=0 setDown=0`, with no active
setter marker. Later in the same run the post-world-basis diagnostic recorded
`y=-0.136097 up=0.191499 blend=0.866667`. The active values and the instrumented
callsite were therefore mutually exclusive.

Static control flow explains the split. At `0x140CB4168`, steady-state
`PlayerAimController_ComputeDirectionalAimWeights 0x140CB32B0` selects
`0x140CB416F -> 0x140CB47A0`; only a controller-mode transition selects the
previously hooked direct publisher at `0x140CB4176`. `0x140CB47A0` reads prior
LR/UD fields, advances signed horizontal/vertical smoothing state at controller
`+0x8A4/+0x8AC` using process delta time, splits the results back into directional
weights, then tail-jumps at `0x140CB491A` to
`PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0`.

Build `ea` hooks only the proven E8 steady-state callsite `0x140CB416F`. Its
wrapper lets both native smoothing and the tail-called publisher finish, reads
the resulting controller `+0x8C0/+0x8C4` and blend `+0x8EC`, resolves the actual
named/alternate mode, and then writes post-blend `Aim4/Aim6` through the stock
setter `0x14172C8E0`. A global publisher-entry detour and the later slot-`+0x58`
boundary were rejected as unnecessarily broad alternatives; direct
active-state/FgAnim copies remain runtime-rejected.

**Prediction:** a large role-A vertical ADS sweep must emit receiver
`AIM-PITCH-SMOOTH-ACTIVE` with nonzero post-blend `up/down`. `mode=alternate`
with a changed setter should add visible vertical motion; successful changed sets
without it reject alternate `Aim4/Aim6`. `mode=named` with unchanged setters
would prove stock publication already held the same values and move the blocker
strictly downstream. Horizontal yaw/projectile alignment must remain unchanged.
Full evidence: `reports/remote_aim_pitch_blend_tick_20260718.md`.

### 2026-07-20 REJECTED/STATIC: `ea` skipped layer activation; guarded named commit `eb`

The user still observed no vertical movement. Receiver PID 16948 nevertheless
logged `AIM-PITCH-SMOOTH-ACTIVE mode=alternate rawUp=0.1169 up=0.0708
blend=0.6056 setUp=0 setDown=0`; a later sample reached `up=0.380015` at
`blend=0.872222`. The boundary and pitch computation are therefore live.
`AnimParamRegistry_SetExistingFloatChannel` returns zero only when it cannot find
a valid active entry, so `ea` is REJECTED as an incomplete existing-entry-only
path. `Aim4/Aim6` remain unclassified because no layer was activated.

Static lifecycle tracing found the missing stock protocol. Player slot `+0x710`
preallocates fifteen inactive type-2 `AnimStateLayer` descriptors. On first
nonzero named aim weight, `PlayerAimController_CommitNamedAimChannel 0x140CAF470`
calls `AnimParamRegistry_EnsureFloatAnimStateLayer 0x14172A910`, which activates a
matching/free descriptor through `0x14172A370`, initializes virtual `+0x60`, then
queues `aim-add-up/down` through `AnimStateLayer_RequestDirectFade 0x14189CC80`.
The independent net-fire layer uses the same ensure/weight/fade sequence.

Build `eb` replaces the rejected setter with the complete named commit after the
proven `0x140CB416F` steady-state publisher, only for slot 1 alternate mode. A
preflight requires an active matching, inactive matching, or inactive id-zero
type-2 descriptor, preventing the stock null dereference when the registry is
exhausted. Its marker records active status, contexts `AD/AC`, value, fade queue
and dirty state. The real IDB labels the activation/ensure/direct-fade helpers.

**Prediction:** fresh build `2026-07-20eb` must log
`AIM-PITCH-NAMED-ACTIVE mode=alternate callUp=1 activeUp=1 ctxUp=AD` for an upward
sweep, and corresponding `activeDown=1 ctxDown=AC` downward. Accepted fade plus
visible arm/weapon pitch confirms the missing lifecycle edge; accepted fade with
no palette/pose change rejects the authored consumer. Horizontal yaw and the
confirmed projectile relay must remain unchanged. Full evidence:
`reports/remote_aim_pitch_blend_tick_20260718.md`.

### 2026-07-20 REJECTED: `eb` mixes named and alternate AnimStateLayer contracts

Fresh PID 19812/20316 loaded `eb`, but ADS broke the remote animation: it shook
and jerked the weapon upward. The processes were stopped immediately. Receiver
PID 20316 reached the new path with `up=0.0709`, activated Aim4 context `AD`, yet
reported `valueUp=0 fadeUp=0 dirtyUp=1`. The runtime prediction failed.

Exact lifecycle tracing corrects the old setter interpretation.
`0x14172C8E0` writes target `+0x2C=0`, 0.2-second remaining/full fade at
`+0x30/+0x34`, starting weight from current `+0x28`, easing `+0x3C=1`, and
release-pending `+0x3D=1`; it is now
`AnimParamRegistry_RequestLayerFadeOutAndRelease`. Registry step `0x141A911A0`
deactivates and clears the descriptor after pending weight reaches zero. Thus
`eb` repeatedly activated Aim4 while stock alternate publication faded and
released it, exactly explaining the shake. One-time activation plus the former
"setter" is also REJECTED.

`0x140CB41F0` writes real LRDU weights to active state
`+0x188/+0x18C/+0x190/+0x194`; its alternate branch only releases
Aim0/Aim2/Aim4/Aim6. Aim4/Aim6 are therefore REJECTED as alternate vertical
data carriers. Build `ec` removes all receiver named commits and is read-only:
it logs bounded local/puppet branch, base-index, active-state LRDU and full
Aim0..Aim7 descriptor/layer lifecycle fields after native publication. The
falsifier distinguishes a different alternate descriptor from a downstream
reader of active-state vertical fields, and predicts no `eb` shake. Full
evidence and prediction: `reports/remote_aim_pitch_blend_tick_20260718.md`.

### 2026-07-20 CONFIRMED/REJECTED: `ec` selects active-state downstream reader

Fresh PID 20884/11636 loaded read-only build `ec`; both aim relays and state net
installed. The user confirmed the `eb` shake/upward jerk was gone, while the
clone still had no visible vertical weapon motion. Receiver screen 1 logged six
steady samples in `mode=alternate branch=1 base=0`. At the strongest sample,
`rawUp=0.9990` and active-state `stateUp=0.9990` with blend 1, yet every
controller Aim0..Aim7 descriptor was wholly absent (`found=0`).

Therefore pitch transport through the publisher is CONFIRMED and the entire Aim
registry family is REJECTED for alternate vertical apply. The missing consumer
is strictly a reader downstream of active-state `+0x188/+0x18C`. Current static
work is tracing those reads through alternate authored pose, joint palette and
weapon attachment publication; no new runtime mutation is authorized yet. Full
evidence: `reports/remote_aim_pitch_blend_tick_20260718.md`.

### 2026-07-20 STATIC: `ed` removes the stock named/release ownership conflict

The exact downstream owner is the branch inside the stock publisher itself.
`AnimControl+0x58` owns three authored parameter buffers; `0x140CB426A..4293`
writes LRDU into the buffer at `[AnimControl+0x58]+0x10` (the third pointer),
then `JNZ 0x140CB42B6` chooses between the complete
named aim-layer lifecycle and the alternate fade-to-zero/release loop. The
puppet's branch-1 path has no Aim descriptor by design; it is not a second
vertical consumer.

Build `ed` validates and NOPs only the six-byte `JNZ`, so the stock publisher
falls through to its own four named commits. This differs from rejected `eb`:
there is one writer and no same-frame/next-frame alternate release. Three
independent proofs authorize the runtime test: exact branch/lifecycle disassembly,
the generic `AnimStateLayer+0x530 -> AnimStateInstance+0x178` buffer-copy/evaluator
chain, `ec`'s named-local/alternate-puppet split with `rawUp=stateUp=0.9990`, and
`eb`'s visible upward weapon jerk proving the named layer reaches the rendered pose.

Prediction: screen 1 gains stable Aim4/Aim6 descriptors with `release=0` and
weights correlated to `stateUp/stateDown`, plus smooth vertical weapon motion
without the `eb` shake; ADS exit returns neutral, while horizontal aim and
projectile alignment remain unchanged. Active stable descriptors without visible
pitch reject the named-overlay hypothesis. Full competitors and falsifier:
`reports/remote_aim_pitch_blend_tick_20260718.md`.

Scope warning: `ed` is a bounded runtime discriminator. The tested byte belongs
to the currently active authored base-state player-info and may vary with cover,
climb, or other action states. A successful visible result must be converted to a
retained-screen-1/controller-scoped override; the global NOP is not final policy.

### 2026-07-20 RUNTIME REJECTED: `ed` named fallthrough corrupts the model

PIDs 2608/22216 loaded `ed`; the receiver's screen-1 Aim4 became stable and active
with context `AD`, `release=0`, and weight tracking upward input through 0.6462.
The user observed the character model break completely. Thus the lifecycle
prediction passed but the visible-pose prediction failed: the named layer is
incompatible with the puppet's authored alternate graph, not its missing apply
edge. Both processes were closed. Build `ee` removes the global branch patch and
restores the stable publisher path. Do not retry named fallthrough, including a
screen-1-scoped version. Resume static tracing at the alternate
`AnimStateLayer+0x530 -> AnimStateInstance+0x178 -> 0x1418A4FD0` evaluator path.

### 2026-07-20 STATIC: `ef` reads the exact base-instance copy chain

The chain is now exact: `AnimControl::BeginStepInternal 0x141A78A60` dispatches
type-2 layers; constructor `0x14189B7B0` binds the shared parameter wrapper at
layer `+0x530`; initializer `0x1418B3F10` allocates instance destination `+0x178`
and sets `+0x190 & 0x20` from authored config; `0x1418A0EB0` conditionally copies
the whole buffer before `AnimStateInstance::PhaseUpdate 0x1418A4FD0` samples the
tree. Build `ef` is read-only and compares base descriptor/instance admission,
wrapper identity/sizes, and source/destination LRDU for screens 0/1. Written
competitors and falsifier are in the aim report; no runtime mutation is allowed.

### 2026-07-20 RUNTIME CONFIRMED/REJECTED: `ef` reaches the live evaluator instance

Fresh PIDs 6772/21488 loaded read-only build `ef`; the user performed large ADS
up/down sweeps and still observed no vertical weapon movement. All six bounded
screen-1 samples resolved base hash `7339D08C` as a present, active type-2 layer
with one live instance. Every sample reported `flags=0x28`, `copy=1`,
`srcSame=1`, and compatible source/destination sizes `1840/1840`.

The live destination followed the shared LRDU source closely across both signs:
down `0.1031 -> 0.1036`, then up `0.3690 -> 0.3365`, `0.6160 -> 0.6081`,
`0.7607 -> 0.7509`, `0.8690 -> 0.8616`, and `0.9554 -> 0.9534`. Therefore the
alternate parameter-copy chain through `0x1418A0EB0` into the active
`AnimStateInstance+0x178` is **CONFIRMED**. Missing descriptor/instance, missing
copy flag, wrapper mismatch, undersized destination, and lost LRDU are
**REJECTED** as causes of the absent pitch.

The unresolved boundary is now authored animation-state/tree selection before
or inside `AnimStateInstance::PhaseUpdate 0x1418A4FD0`: the evaluator receives
the values, but the puppet's selected alternate graph does not produce the local
vertical pose. Both processes were closed. Do not patch the copy routine or
parameter buffers; compare local and puppet active instance/config/root-tree
identity and the native snapshot/state-selection path statically before the next
runtime test.

### 2026-07-20 STATIC: `eg` fingerprints the authored state selected inside the instance

The first exact semantic boundary after the confirmed copy is
`0x1418B24F0`. It copies the current 0x80-byte state snapshot into
`AnimStateInstance+0x90`, reads the stable state id at instance `+0x98`, resolves
its authored record through `0x141926AE0`, overlays callbacks/flags, and resolves
the root snapshot node. `0x14189D720` then runs `0x1418A4670` and
`AnimStateInstance::PhaseUpdate` for each active instance. The former evaluates
`anim-state-dtgroup-func` from instance `+0xC0`; `0x1418A4D50` evaluates
`anim-state-phase-func` from `+0xE8` and passes the confirmed parameter descriptor
from `+0x178`.

Build `eg` is read-only. During nonzero vertical aim it records, separately for
screen 0 and screen 1, the snapshot state id `+0x98`, one snapshot word,
callback-presence mask, state flags, a subset of the scalar identity tuple, the
raw root-node index `+0x138`, instance flags and copy bit. It does not copy
callbacks, request a state, replay a snapshot, or write pose data.

Selected hypothesis: the puppet receives LRDU but its active base instance has a
different authored state/callback/root contract, so those fields have no vertical
meaning. Competitors are (A) identical fingerprints with the difference deeper
inside the same authored/parametric asset, (B) a transient mismatch caused only by
sampling different action phases, and (C) the weaker late-palette overwrite.
Prediction: the same role-A vertical sweep produces bounded screen-0 sender and
screen-1 receiver `AIM-BASE-STATE` lines. A stable difference in state id,
callbacks or roots selects the exact state-command frontier; identical values
reject instance selection and move the trace inside the common root node. The
model and existing horizontal/projectile behavior must remain unchanged.

### 2026-07-20 CONFIRMED/REJECTED: `eg` finds matching states but no pitch

Fresh PIDs 7684/17560 loaded read-only build `eg`; the user swept vertical ADS
with pistol and rifle and reported that the remote weapon still had no vertical
movement. Both processes were closed after capture. No crash, model corruption,
or new visible regression was reported.

Sender and receiver both reached identical state/callback/config fingerprints
while the receiver still lacked pitch:

- `state=66D4BF80`, snapshot word `BE7CB4A2`, callbacks `1D`, flags `1`, scalar
  tuple `1CEC7F3A/6C2B2396/4B0F5DE3`;
- `state=3C347132`, snapshot word `C45CF708`, callbacks `38`, flags
  `0000008000000009`, tuple `9DCD31BC/6C2B2396/4B0F5DE3`;
- `state=41C84248`, snapshot word `25CC08FD`, callbacks `1D`, flags `1`, tuple
  `5F913F74/6C2B2396/4B0F5DE3`.

Therefore an always-different state selector/callback/flag tuple is **REJECTED**
as the sole cause. It is **CONFIRMED** that the receiver can hold matching
high-level authored-state metadata and already-copied LRDU without producing the
visible vertical pose. The screen-1-only `state=2E3C4F6B` sample cannot prove a
stable mismatch because the bounded sender/receiver samples were not
frame-aligned.

Static correction after the test: `AnimStateInstance+0x130` is a
`SnapshotNodeHeap*`, while `+0x138` is only an index into that per-instance heap.
Consequently different raw `rootSnap` numbers across processes are not evidence
of different root classes. `0x1418B2EF0` selects the index and
`SnapshotNodeHeap_ResolveNodeByIndex 0x1418C2F90` resolves it before
`AnimStateInstance::PhaseUpdate`. The next read-only discriminator must compare
the resolved root vtable/RTTI RVA plus the complete config tuple: mode `+0x18C`,
apReference `+0x194`, scalar fields `+0x118/+0x11C/+0x120/+0x124/+0x12C/+0x158`,
authored-key count/content hash at `+0x68/+0x88`, and override-table/current-match
data at `+0x148`. A difference only in `+0x188` is expected activation serial,
not authored identity. Continue from this static boundary tomorrow; do not patch
the raw root index or replay the broad native snapshot.

### 2026-07-21 STATIC: `eh` separates concrete root type from authored config

The user reconfirmed that build `eg` produced no vertical weapon movement but
also no crash, model corruption, or other visible regression. The next probe
therefore preserves the same read-only boundary and adds only stable identity
fields; it does not attempt another animation fix.

Selected hypothesis: sender and receiver use different concrete root-node types
or different authored key/override routing even when the high-level state id and
callback mask match. Competitors are (A) identical resolved root/config with the
loss deeper in common-tree parameter/script evaluation, (B) allocator noise that
changes only raw pointers/indices, and (C) a transition/teardown race that makes
the sample internally inconsistent.

The static evidence quota is satisfied independently:

1. `AnimStateSnapshot_SnapshotAnimState 0x1418B24F0` writes the selected root
   index at instance `+0x138`; `AnimStateInstance::PhaseUpdate 0x1418A4FD0`
   independently reads `+0x130/+0x138`, resolves the root, and calls root vslot
   `+0x68`. Teardown `0x14189C710` uses the same pair before resetting the index
   to `-1`.
2. `SnapshotNodeHeap_ResolveNodeByIndex 0x1418C2F90` is anchored by the source
   assertion `SnapshotNodeHeap::GetNodeByIndex`: max count is `heap+0xAC`, record
   base is `heap+0xA0`, stride is 40, and a bad index reaches assert/debugbreak.
   The probe therefore reproduces this layout after bounds checks and never
   calls the native resolver.
3. MSVC-x64 RTTI identity is independently consistent for both
   `AnimStateInstance` and `NetSnapshotEffectParser`: `vtable[-1]` points to a
   signature-1 CompleteObjectLocator with a self RVA and TypeDescriptor RVA.
   Build `eh` validates all of these against the PE `SizeOfImage` and logs only
   the stable vtable/TypeDescriptor RVAs.
4. `AnimStateInstance_InitializeAuthoredBuffers 0x1418B3F10` allocates and copies
   `count*4` authored keys into `+0x68` and stores count at `+0x88`;
   `0x1418A4030` can later replace the apReference key, so `eh` hashes the live
   list twice instead of treating constructor data as immutable.
5. `AnimStateOverrideTable_FindStateRecord 0x141926AE0` scans the external table
   at instance `+0x148` backward in 24-byte records. Each enabled record uses
   `SortedIdPointerTable_Find 0x141764B50` to binary-search the current state id
   in parallel key/value arrays. The probe reproduces this read-only lookup
   twice and marks unstable/invalid results so they are excluded from comparison.

Build `2026-07-21eh` logs bounded, system-tick-aligned `AIM-BASE-CONFIG` samples
for screen 0/1: full scalar tuple, mode/apReference, authored-key count/hash,
override count/hash/current outer+inner match, and
`{rootIndex,rootVtableRva,rootTypeDescriptorRva}`. Local instrumentation caps
are 4096 keys, 1024 outer overrides, and 4096 inner keys; failures are validity
bits, not engine claims.

Falsifiable prediction: a persistent `rootVtableRva/TypeDescriptorRva` split at
aligned ticks selects a concrete-root mismatch. Equal RTTI but different key
hash or override match selects authored routing. If all stable fields match and
only raw index/activation serial differs, both selection hypotheses are rejected
and the next static trace moves below root vslot `+0x68` into common-tree
parameter binding/script consumption/pose output. Intermittent invalid samples
select only a transition race. No visible behavior change is expected; any crash
or model/animation change invalidates the probe.

### 2026-07-21 CONFIRMED/REJECTED: `eh` matches concrete root and full config

Fresh PIDs 22396/12792 loaded build `eh`; the vertical ADS sweep completed
without a crash. Five tightly aligned steady pairs (screen-0/screen-1 tick gaps
15-16 ms at 435127843/859 through 435128468/484) were fully valid and agreed on:

- state `3C347132`, snapshot word `C45CF708`, callbacks `38`, state flags
  `0000008000000009`;
- scalar tuple
  `9DCD31BC/8DFC1F97/6C2B2396/4B0F5DE3/3E999999/3F800000`, mode `02`,
  apReference `0`;
- authored keys `count=2 hash=07D401B5 valid=1`;
- overrides `count=0 hash=811C9DC5 match=0/0 valid=1`;
- concrete root vtable RVA `02B15218`, TypeDescriptor RVA `03771508`, both
  node/RTTI validity bits set, heap max count `512`;
- instance flags `28` and parameter-copy bit set.

Only raw root index (`0x16` sender vs `0x29` receiver) and activation serial
(`0x0E` vs `0x0D`) differed. Those are per-heap/per-activation values and were
predicted as non-diagnostic. Therefore concrete root type, authored key content,
override routing, mode/apReference, and the full logged scalar contract are
**REJECTED** as causes of the missing vertical pose. It is now **CONFIRMED** that
the same concrete root/config consumes a destination parameter buffer already
known to contain the relayed LRDU, yet the receiver still produces no visible
pitch.

Both processes were closed. The next boundary is below the common root virtual
call at `AnimStateInstance::PhaseUpdate 0x1418A529F` (root vslot `+0x68`). Resolve
vtable `0x142B15218`, its RTTI at `0x143771508`, and the slot target; then trace
how that concrete node binds the `instance+0x178` descriptor into generated
animation/pose commands. Do not compare or copy raw root indices, and do not add
another runtime probe until competing consumers below this slot are separated
statically.

### 2026-07-21 STATIC: `ei` resolves the root Blend VM and command output

The previous `+0x68` frontier is corrected. Vtable `0x142B15218`, COL
`0x142CDF2B0`, and TD `0x143771508` identify `AnimSnapshotNodeBlend`; registered
factory `0x14191DC30` creates the 0x80-byte class. Slot `+0x68`
`0x14191E550` only resolves `node+0x40/+0x44` and steps both children, so it is
**REJECTED** as the parameter consumer.

The live parameter boundary is root slot `+0x58`:
`AnimStateSnapshot_RefreshPhasesAndBlends 0x1418B3150` supplies the live context,
and `AnimSnapshotNodeBlend_RefreshPhasesAndBlends 0x14191E180` copies its 48 bytes,
evaluates the `node+0x58` descriptor as `anim-node-blend-func`, clamps the result,
and stores it at `node+0x60`. Independently, the local command path reaches root
slot `+0x70`; `AnimSnapshotNodeBlend_GenerateAnimCommands 0x14191E5F0` appends
`[0x60007, childA, childB, depth, node+0x4C, node+0x60]`. The probed weight is
therefore proven to reach the generated animation command rather than being an
unused cache field.

Exact child types remain runtime-authored: slot 9 `0x14191DF90` materializes
descriptor children `+0x8/+0x10`; candidates include Animation, Warper, Unary/IK
families and leaves. Build `2026-07-21ei` is read-only and bounded. It records the
root weight/flags/mode, script id, at most 64 four-byte VM instructions and 32
direct constants, plus resolved child vtable/TD RVAs and typed dynamic fields for
proved Blend/Animation layouts. Mutable owner chains and values are reread; it
calls no native resolver/VM/animation function and performs no write.

Falsifier: a stable root-weight split correlated with vertical input selects root
script/binding. Equal root weight but different child RTTI/typed dynamics selects
the concrete child subtree. A complete match rejects the immediate root and moves
the trace to slot-14 command application. Raw indices/pointers and activation
serial remain non-diagnostic. Full evidence and the exact safety caps are in
`reports/remote_aim_pitch_blend_tick_20260718.md`.

### 2026-07-21 CONFIRMED/REJECTED: `ei` matches the root VM and child topology

Fresh PIDs 11468/11736 completed the vertical ADS sweep on read-only build `ei`
without a crash or model regression. In stable state `3C347132`, both processes
had the identical valid root VM: script `CDFC7F1B`, 18 instructions, code hash
`88C7741D`, one constant `000000004666093B` (hash `35DC6391`), and direct
vertical-reference mask `00`. Root flags/mode were `0C/1`.

Both also resolved child 0 as another `AnimSnapshotNodeBlend`
(`02B15218/03771508`) with weight 1.0 and child 1 as
`AnimSnapshotNodeEmptyPose` (`02B3D590/03772710`). The root weight followed the
same time-shifted transition decay in both processes rather than vertical input:
screen 1 `up` reached about 0.6096 while the weight continued from about 0.0135
toward zero. Root slot 11 and the immediate Blend/EmptyPose pair are therefore
**REJECTED** as the vertical discriminator; the node is a generic transition.

Both processes were closed. The concrete next frontier is child 0's nested Blend
spine. First prove statically which child contributes at root weight zero from
the refresh/command semantics. Then a bounded read-only traversal may follow
only nodes whose Blend layout is already established and stop at the first
non-Blend RTTI. Do not launch another probe or guess Unary/IK/Warper layouts
before that proof.

### 2026-07-21 STATIC: `ej` proves and traces the active Blend endpoints

The endpoint route is now offset- and command-proven. At
`AnimSnapshotNodeBlend_GenerateAnimCommands 0x14191E5F0`, child 0 comes from
`+0x40`, child 1 from `+0x44`, and the engine epsilon is 0.0001
(`0x142BE8B44 = 0x38D1B717`). Near-zero weight short-circuits to child 0 at
`0x14191E751`; near-one weight short-circuits to child 1 at `0x14191E7A5`.
The generic path emits command `0x60007`; `ProcessAnimCmdListSegment
0x141AB97C0` case 7 consumes its left/right/weight fields and calls
`AnimCmdEvaluator_ApplyBlend 0x14217CF90`, which sends the same weight to the
transform and scalar pose-blend helpers. The real IDB has the new label, reports
1112 functions/19 data names, and `save_database returned True`.

Therefore the confirmed steady `ei` route starts `root weight~0 -> child0`, then
`nested Blend weight=1 -> child1`. Read-only build `2026-07-21ej` traverses only
this proven endpoint Blend layout, with an eight-node cap, cycle detection,
double-read layout validation, checked heap records/RTTI and bounded VM
fingerprints. It stops before reading any non-Blend dynamic layout. The Release
DLLs build successfully; the pre-existing C4456 warning remains unrelated.

Falsifier: stable endpoint samples must reproduce root selection 0 and nested
selection 1. The first aligned VM/weight/selected-child RTTI difference selects
the authored split. A complete match through the same first non-Blend RTTI
rejects the active Blend spine and moves the trace into that concrete node.
Mixed `stop=4` transition samples are non-diagnostic. No runtime launch is
authorized until this written prediction and the three-type static quota above
are present; both are detailed in
`reports/remote_aim_pitch_blend_tick_20260718.md`.

### 2026-07-21 CONFIRMED/REJECTED: `ej` matches the whole active Blend spine

Fresh PIDs 16476/15632 completed the vertical ADS sweep on read-only build `ej`
without a process crash. Both were closed after collection. Stable state
`3C347132` produced the same valid normalized endpoint route in both processes:

`Blend w~0 child0 -> Blend w=1 child1 -> Blend w=0 child0 -> Blend w=1 child1
-> AnimSnapshotNodeAnimation`.

At all four depths, vtable/TD, endpoint selection, flags/mode and bounded VM
fingerprints matched. The scripted nodes were respectively
`CDFC7F1B/18/88C7741D/1/35DC6391` and
`CDFC7F1B/13/C6EBF6D6/0`; both had direct vertical-reference mask `00`.
The terminal type matched as `AnimSnapshotNodeAnimation`
(`vt/TD RVA 02B14D68/037713E8`), and both probes stopped validly at that first
non-Blend node (`stop=3`). Only raw per-heap indices differed.

This result **REJECTS** the complete active Blend spine as the vertical-aim
discriminator. The next static frontier is the selected terminal Animation
node: prove its layout and trace its phase/clip fields through
`AnimSnapshotNodeAnimation_RefreshPhase 0x141918120`,
`AnimSnapshotNodeAnimation_StepPhase 0x1419180A0`, and
`AnimSnapshotNodeAnimation_GenerateAnimCommands 0x1419182E0` before defining
another runtime probe.

### 2026-07-21 STATIC: `ek` proves the terminal Animation clip/phase path

The terminal class is a registered 0x80-byte `AnimSnapshotNodeAnimation` with
vtable `0x142B14D68`. Newly labelled slot 9
`AnimSnapshotNodeAnimation_InitializeFromDescriptor 0x141917D00` establishes
source/resolved animation ids `+0x40/+0x44`, remap keys `+0x48/+0x4C`, clip
`+0x50`, phase descriptor `+0x58`, phase pair `+0x60/+0x64`, and auto-step/flip
flags `+0x68/+0x69`. Slot 10
`AnimSnapshotNodeAnimation_ResolveClip 0x141918070` resolves the clip through
`AnimationDatabase_ResolveClip 0x1417C8E20`; authored remaps pass through
`AnimSetRemap_ResolveAnimationId 0x1417CA2E0`.

Slots 11 and 13 independently write the phase pair from
`anim-node-phase-func` or clip time. Slot 14 consumes resolved id/clip/phase and
emits command `0x60006`; `ProcessAnimCmdListSegment 0x141AB97C0` case 6 then
consumes its clip and phase/frame and produces the pose. This satisfies the
four-type static quota.

Read-only build `2026-07-21ek` runs only on the selected terminal Animation
endpoint. It double-reads the proven node layout, bounded phase VM, and only the
clip offsets consumed by the generator/evaluator; it logs normalized ids/words,
never pointers, and performs no call or write. Falsifier: a clip/id split selects
authored clip selection; matching clip with phase/VM split selects phase; a full
match rejects the leaf and moves to command-case-6 context/plugin/joint-mask
consumers. Invalid/torn samples and raw indices are non-diagnostic. Full evidence
and exact safety bounds are in
`reports/remote_aim_pitch_blend_tick_20260718.md`.

The Release build succeeds with only the pre-existing C4456 warning. The real
IDB has all six new Animation construction/remap/clip labels (1118 functions,
19 data names, `save_database returned True`), and the refreshed MCP copy
resolves them.

### 2026-07-21 CONFIRMED/REJECTED: `ek` matches the Animation leaf

Fresh PIDs 18744/18868 completed the read-only `ek` sweep. Screen 0 supplied
downward input `0.0591..0.0651`; screen 1 supplied upward input
`0.0741..0.6422`. Both processes remained responsive, did not crash, and were
closed after collection.

All 28 selected-leaf samples were fully valid and identical: animation
`C7FB376F/C7FB376F`, remap keys `6C2B2396/4B0F5DE3`, phase pair zero, flags
`01/00`, clip words `0000000A/6C2B2396/4B0F5DE3/000009A0`, and no phase
descriptor/VM. The phase stayed zero throughout changing vertical input.

This **REJECTS** selected Animation clip/phase input, and therefore the generated
command-`0x60006` clip/phase payload, as the discriminator. Before probing case 6
internals, compare the complete native active type-2 layer set and evaluated
instance roots across local and puppet: local vertical may be contributed by a
separate additive layer rather than the matching `base` command. The earlier
model-breaking `ed` named-layer force remains rejected; do not reactivate it.

### 2026-07-21 STATIC: `el` inventories every evaluated type-2 layer

The safe discriminator is now fully bounded. `AnimControl_BeginStepInternal
0x141A78A60` proves registry count/array `+0x28/+0x30`, 0x20-byte records,
channel/context `+0x00/+0x10`, active/type `+0x14/+0x18`, object `+0x08`, and the
type-2 dispatch to `AnimStateLayer_Step`. Independent release traversal
`0x141A911A0` confirms the same record ownership/lifetime.

`AnimStateLayer_Construct 0x14189B7B0` proves RTTI
`02B0ABB0/03770FC0`, object type `+0x08`, and live track storage
`+0x4F8/+0x500`; `AnimStateInstance_Construct 0x1418B3E70` proves the 0x1C0
instance and RTTI `02B0C730/03771100`. `AnimStateLayer_PhaseUpdateInstances
0x14189D720` independently consumes track instance-array/live-count
`+0x00/+0x12`, so the inventory covers evaluated rather than dormant instances.
This satisfies the four-type static quota.

Read-only build `2026-07-21el` samples only stable ADS state `3C347132` above
0.25 vertical input. It double-reads and normalizes every active type-2 channel,
fade/track state, live instance state/flags and root RTTI under explicit
24-layer, 8-track and 4-instance bounds. It logs no pointers/heap indices, calls
no native function and writes nothing. Falsifier: a stable semantic layer/root
difference selects only that layer; a complete valid match rejects the full
active-layer set and returns the frontier to command-case-6 context,
skeleton/joint-mask, plugin and palette consumers. Truncated/torn samples,
order/address/index-only differences and isolated fade transients are
non-diagnostic. Both Release DLLs build with only the pre-existing C4456 warning.

### 2026-07-21 CONFIRMED/REJECTED: `el` matches all active type-2 layers

Fresh PIDs 17472/19696 produced four complete samples each and remained
responsive without a crash. Screen 0 covered up to `up=1.0000` plus
`down=0.2956`; screen 1 covered up to `up=0.9967`. Every header was
`registry=42 active2=7 captured=7 valid=1/1 truncated=0`.

Both processes had the same seven channels/contexts, live topology, instance
states/flags and root RTTI: `2EAA36BC/00 -> FEC2DB76/Animation`,
`63B48BEE/C3` and `66B82091/CB` with zero live tracks,
`7339D08C/0E -> 3C347132/Blend`, `89207057/80 -> 59FE38FD/Animation`,
`E2B990B9/C4 -> 3889CC0E/02B3D590`, and
`F574416B/00 -> 05460527/Animation`. The first screen-1 sample caught a
three-track `E2B990B9` transition, but it collapsed after 500 ms to the same
single track as every stable sample. Only completed-fade timing residues
differed.

This **REJECTS the complete active type-2 layer and evaluated-root inventory**
as the missing vertical discriminator. Do not activate/synthesize any named aim
layer. The frontier returns to `ProcessAnimCmdListSegment 0x141AB97C0` case 6:
statically separate command context, skeleton/joint-set/plugin evaluation and
later pose-merge/palette publication before defining another runtime probe.

### 2026-07-21 STATIC: case 6 separates direct evaluation from retargeting

`AnimSnapshotNodeAnimation_GenerateAnimCommands 0x1419182E0` emits an exact
24-byte `0x00060006` command: `ArtItemAnim*` at `+0x08`, output/group word at
`+0x10`, and phase float at `+0x14`. The selected `ek` clip is uniquely bounded
by `ArtItemAnim+0x24 = 0000000A`, art-skeleton id `+0x40 = 6C2B2396`, and the
underlying object at `+0x28` carrying id `+0x04 = 4B0F5DE3` and word
`+0x08 = 000009A0`.

`ProcessAnimCmdListSegment 0x141AB97C0` case 6 compares that source underlying
skeleton id with the target underlying id reached through
`AnimCmdContext+0 -> ArtItemSkeleton+0x40 -> +0x04`. Equal ids enter newly named
`AnimCmdEvaluator_ApplyAnimationDirect 0x14217C7F0`. Unequal ids look up the
source/target art-skeleton pair through `SkelTable_FindRetargetEntry
0x1417D0BD0`, validate entry feature word `+0x22` and group mapping `+0x28`,
resolve the source through `SkelTable_ResolveArtItemSkeleton 0x1417CD100`, and
call the exact source-asserted `RetargetAnimUsingBatchMgr 0x14193B2C0`.
`AnimCmdEvaluator_CopyMaskedPose 0x14217D280` then publishes the retargeted
JointParams/ValidBits into the same processing-group output as direct evaluation;
`AnimCmdEvaluator_ClearOutput 0x14217D940` handles invalid output.

The owning context is independently proven in both
`AnimManager_PerformAnimBlend 0x141A92260` and
`AnimManager_AsyncObjectUpdateJointBlendJob 0x141A92A00`: context `+0` is the
target ArtItemSkeleton from `FgAnimData+0x18`, `+0x08` is the value stored at
`FgAnimData+0x2C0`, `+0x10` points back to `FgAnimData+0x1C0`, `+0x18` is an
optional scratch allocation, and `+0x20` holds flags. Processing-group index is
argument 4; the optional five-argument callback is used only by the
cross-skeleton branch and originates in job slots 3..5.

The exact runtime hypothesis for build `em` is that the local player evaluates
the selected command directly while the retained puppet uses retargeting, or
uses a different retarget pair/group/callback whose mapping omits the vertical
upper-joint contribution. Competitors are: (1) both processes have identical
validated context, skeleton ids, group, callback RVAs and predicted branch, so
the split is later in the common pose/palette path; (2) only raw pointers or
output indices differ, which is non-semantic; (3) command/context/assets change
during reread, which invalidates the sample.

The static quota has five independent evidence types: generator command layout;
case-6 source/target comparison; fixed-stride retarget-table lookup; exact
`RetargetAnimUsingBatchMgr` source assertion plus mapping/group dataflow; and
two independent callers constructing the same context before both branches
converge on the work-buffer pose output. The written falsifier is therefore:
a stable direct/retarget, pair/group/coverage, or callback-RVA split selects only
that retarget route; a complete normalized match rejects case-6 inputs and moves
to the common output merge/palette consumer. Raw addresses, output indices and
torn reads cannot select either result; any call, write, crash or visible model
change invalidates `em`.

All six strong STATIC functions are labelled in the real IDB. The headless apply
reports 1124 functions and 19 data names with `save_database returned True`; an
independent dump resolves every new name, and the MCP copy was refreshed.

Read-only build `2026-07-21em` now detours only the verified 12-byte register-push
prologue of `ProcessAnimCmdListSegment`. Before inspecting payloads it reconstructs
the owning FgAnimData from `AnimCmdContext+0x18 - 0x1C0`, proves the inverse
context/FgAnimData fields and exact retained player/screen, requires live ADS and
at least 0.20 vertical weight, and structurally validates the entire command list
under a 4096-dword bound. It then accepts only exact six-dword case-6 commands
with the prior `ek` clip signature.

Each accepted record double-reads the command, clip, underlying animation,
target ArtItemSkeleton/underlying skeleton, fixed-stride retarget entry and first
group-coverage word. The log contains only ids/counts, direct-vs-retarget result,
normalized entry index/feature/coverage, group and in-image callback RVAs; no
pointer or output index is emitted. Four samples per screen are allowed at 500 ms
intervals. The probe calls the untouched trampoline after observation and never
calls any resolver/callback or writes game data. Both Release DLLs build with only
the pre-existing C4456 warning.

### 2026-07-21 INCONCLUSIVE: build `em` selector missed the current clip

Both injected processes remained responsive and produced no exception or model
corruption. Existing aim telemetry independently proves that the test reached
live ADS with vertical input/weight up to `1.0`, but neither process emitted an
`AIM-CASE6` sample. This is not a negative result for the case-6 hypothesis: the
runtime-selected valid leaf had flags/art-skeleton/underlying/word08 signature
`0000000B/27E57F43/FD6DBE03/000009A0`, while build `em` deliberately accepted
only the previously observed `0000000A/6C2B2396/4B0F5DE3/000009A0` signature.

The falsifier therefore did not execute and no direct/retarget comparison was
made. The next probe may admit the two exact observed signatures, preserving all
FgAnimData, player/screen, ADS, vertical, command-size and double-read guards.
It must report the selected signature as normalized ids so the two assets are
never conflated; only complete records from the same signature may be compared
between screens.

Read-only build `2026-07-21en` implements that bounded retry. Its selector is
the exact two-element set `0A/6C2B2396/4B0F5DE3/09A0` and
`0B/27E57F43/FD6DBE03/09A0`; every accepted `AIM-CASE6` record now includes all
four signature words. All prior command-list, FgAnimData ownership,
player/screen, ADS/vertical, target/retarget and double-read guards are unchanged.
No resolver/callback is invoked and no game memory is written. The renamed
single target `tll_coop_runtime.dll` builds cleanly from a fresh build directory.

### 2026-07-21 INCONCLUSIVE: build `en` proves the selector was not the blocker

Both processes remained responsive with no exception. Independent leaf telemetry
observed the exact allowed signatures on the two processes:
`0A/6C2B2396/4B0F5DE3/09A0` and `0B/27E57F43/FD6DBE03/09A0`; live vertical aim
also reached `up=1.0`. Nevertheless neither process emitted `AIM-CASE6`.

This **rejects the narrow-selector-miss explanation** for the empty case-6 probe.
It does not compare or reject the direct/retarget hypothesis because no complete
command/context record reached the falsifier. Do not broaden the runtime selector
again. Return to static analysis and prove the exact same-instance dataflow from
the selected `AnimSnapshotNodeAnimation` leaf through command generation into
`ProcessAnimCmdListSegment`, including which FgAnimData/context owns that list and
whether another dispatcher consumes this command class.

### 2026-07-21 STATIC: same-instance command path exposes the `en` context bug

The exact selected leaf and the real consumer are now connected without a copy
or alternate dispatcher. `AnimStateSnapshot_GenerateAnimCommands 0x141AB6C40`
resolves the root from `AnimStateInstance+0x130/+0x138`, invokes root virtual
slot `+0x70`, and passes its caller-owned command-list record unchanged.
`0x14189EA60` preserves that record across the active `AnimStateLayer` instance
stack; exact source-asserted `AnimControl_CreateAnimCommands 0x14172C370` passes
it through the type-2 control path; and exact source-asserted
`AnimManager_CreateAnimCmdsForObject 0x141A8FCF0` fills the per-object record
owned by `0x141A91490`. `AnimManager_PerformAnimBlend 0x141A92260` receives the
same returned pointer/count record and calls `ProcessAnimCmdListSegment` at
`0x141A92759`. The selected leaf's `0x60006` therefore cannot bypass this
consumer or be transformed into a different list on this path.

The empty `en` probe instead came from a one-qword error in our context
reconstruction. Exact `PerformAnimBlend` instructions store
`[FgAnimData+0x2C0]` at context `+0x08` (`0x141A926A6..0x141A926AD`), store
`&FgAnimData+0x1C0` at context `+0x10` (`0x141A926B5..0x141A926BC`), and put the
scratch allocation at `+0x18` (`0x141A9269E`). The independent first async path
does the same at `0x141A92DC4..0x141A92DDA`, with scratch at `0x141A92DB9`; its
second consumer reuses that context. Build `en` incorrectly subtracted `0x1C0`
from context `+0x18`, so it reconstructed FgAnimData from scratch and rejected
every valid call before parsing opcode 6. Scratch can also legitimately be null
when no scalar scratch space is required.

This satisfies five independent evidence types: the leaf virtual-slot payload;
the upward source-asserted call graph; unchanged command-record pointer/count
dataflow into the consumer; exact `PerformAnimBlend` context-construction
instructions; and an independent async caller with the same layout. The primary
hypothesis for read-only build `eo` is that correcting only this layout will
admit complete same-signature `AIM-CASE6` records. Competitors are: (1) no record
still reaches case 6, falsifying the context-layout explanation; (2) complete
same-signature normalized records match, rejecting case-6 inputs and moving the
frontier to the common pose/palette path; (3) a stable branch, retarget
pair/group/coverage or callback-RVA split selects only that retarget route.

The `eo` falsifier retains the exact two-signature selector and every command,
asset, player/screen, ADS/vertical and double-read guard. If exact leaf telemetry
again reaches at least 0.20 vertical weight but neither process emits
`AIM-CASE6`, the corrected-context hypothesis is rejected. Complete normalized
same-signature records decide between the later common output path and retarget
route as above. A crash, torn/incomplete sample, game-data write or visible model
change invalidates the test.

The three exact source-asserted functions are labelled STATIC in the single real
IDB. The headless apply reports 1127 functions and 19 data names with
`save_database returned True`; after restarting MCP, independent lookups resolve
all three names and their expected callers. Release build `2026-07-21eo` compiles
successfully.

### 2026-07-21 CONFIRMED/INCONCLUSIVE: `eo` fixes context ownership, but samples only screen 0

Fresh PIDs 9536/19372 remained responsive and produced four complete
`AIM-CASE6` records each before both processes were closed. This confirms the
corrected context layout and rejects the old `en` reconstruction: every record
passed command, clip, target, ownership and double-read guards while vertical
weight rose from about 0.20 to 1.0.

PID 19372's `0B/27E57F43/FD6DBE03/09A0` clip took the retarget branch to target
`ED25C708/C39CA709`, joint/scalar counts `580/8`, entry 124, map/coverage 1 and
callbacks `006CCB30/006CD220/006CD620`. PID 9536's distinct
`0A/6C2B2396/4B0F5DE3/09A0` clip took the direct branch to matching target
`6C2B2396/4B0F5DE3`, joint/scalar counts `651/8`, with the same callback RVAs.
These records cannot select either hypothesis because the written falsifier
requires the same four-word signature on sender and retained puppet; the two
observed branches belong to different clips.

All eight records were `screen=0`; no retained `screen=1` context passed the
probe. The probe's `player_ads_active()` requirement is a mod-added diagnostic
gate, not an input consumed by `ProcessAnimCmdListSegment`. Prior `ec` already
proved screen-1 active-state LRDU up to `0.9990`, `ef` proved its copied live
instance LRDU up to `0.9534`, and `ek` proved the selected screen-0/screen-1
Animation leaf/signature. Therefore the next bounded discriminator may omit
only this redundant ADS-bit gate for screen 1 while retaining exact player/Fg,
nonzero vertical state, signature, command and double-read requirements. Screen
0 must keep the local ADS-bit gate.

### 2026-07-21 STATIC: `ep` admits retained case 6 without trusting its HUD ADS bit

The hypothesis is exact: retained-player `Player+0x1548` is the only artificial
predicate preventing otherwise eligible screen-1 case-6 records. It is safe to
remove only for screen 1 because the probe has already proven the actual
`FgAnimData` owner and screen, and still requires finite active-state vertical
weight of at least 0.20 before parsing either exact six-dword clip signature.
`ProcessAnimCmdListSegment` itself does not read this Player HUD/state bit.

The evidence quota consists of three independent prior runtime boundaries plus
the current code path: `ec` proves nonzero screen-1 active-state LRDU; `ef` proves
that LRDU in the live evaluator-instance copy; `ek` proves the selected retained
Animation leaf and exact clip payload; and `eo` proves that every later context,
command and asset guard succeeds for the corresponding local records. Competing
hypotheses are that screen 1 misses the case-6 call at the same vertical moment,
or that it uses a different Fg owner/command despite the prior leaf evidence.

Read-only build `2026-07-21ep` therefore keeps the ADS-bit requirement for local
screen 0 and omits it only for retained screen 1. All ownership, vertical,
two-signature, command-bound, asset, retarget and double-read checks remain
unchanged. The Release DLL builds successfully. Falsifier: eligible local records
without any retained record reject the ADS-bit explanation; complete
same-signature equality rejects case-6 inputs and moves to the common
pose/palette output; a stable same-signature semantic branch, target,
retarget-map/coverage or callback split selects only that exact route. Raw
pointers/indices, different signatures, torn records, crash or visible change
cannot select a result.

### 2026-07-21 REJECTED/INCONCLUSIVE: `ep` proves the retained ADS bit was not the blocker

Fresh PIDs 21824/18588 remained responsive and state-net exchanged packets in
both directions (`remote Player synchronization enabled`). Each process again
produced four complete screen-0 records while vertical weight rose to 1.0: the
`0A` clip remained direct to `6C2B2396/4B0F5DE3`, and the `0B` clip remained
retargeted to `ED25C708/C39CA709` through entry 124. No exception was logged and
both processes were closed after capture.

There was no `screen=1` `AIM-CASE6` record after removing its ADS-bit gate, and
no screen-1 sample from the retained aim base/layer probes in this run. This
**rejects retained `Player+0x1548` as the reason** the case-6 comparison was
empty. It does not reject case 6 itself: the same-signature local/puppet
falsifier still has no retained record.

The remaining competitors are now ordered, not guessed: (1) valid command
contexts reconstruct an Fg owner that is not the retained Player's `+0x1A8`;
(2) the Fg owner does match screen 1, but active-state vertical admission is not
simultaneous with this consumer; (3) the exact selected signature is absent from
the retained consumer call despite the earlier leaf boundary; or (4) the
retained path does not reach this direct consumer family in the current run.
The next read-only probe must count these stages under hard bounds before another
payload comparison. It must not remove ownership/vertical/signature checks from
the actual `AIM-CASE6` records.

### 2026-07-21 STATIC: `eq` distinguishes retained owner, timing and signature loss

Read-only build `2026-07-21eq` adds no game-memory access beyond fields already
validated by `eo`. It preserves the exact `ep` `AIM-CASE6` eligibility and adds
only process-local atomic counters for: bounded consumer calls, validated
context layout, Fg owner match to slot 0/slot 1/neither, ADS admission,
active-state availability, vertical admission, structurally valid list, selected
signature and emitted record. At most four `AIM-CASE6-STAGES` summaries are
written per process after either screen first reaches the vertical threshold;
no pointer is logged and the Release DLL builds successfully.

The evidence quota is the exact context construction in two native caller
families, the same-instance leaf-to-consumer command path, `eo`'s successful
screen-0 records, and `ep`'s negative result after removing the only ADS-bit
predicate. Falsifier: `owner1=0` with growing validated layouts selects a
different retained Fg owner/consumer; `owner1>0` but `active1` or `vertical1=0`
selects admission timing; `vertical1>0` but `signature1=0` selects command/clip
selection; `signature1>0` without `emitted1` identifies an internal probe guard.
Only a complete retained record may resume the original same-signature
direct/retarget comparison. Counter magnitude alone is non-semantic.

### 2026-07-21 CONFIRMED: `eq` selects missing retained vertical admission

Fresh PIDs 7444/16088 remained responsive and produced all four bounded stage
summaries plus the expected local case-6 records. PID 16088 ended with
`owner=3555/4260/1142335`, `active=3555/4260`, and
`vertical=720/0`; PID 7444 ended with `owner=10632/8050/1222559`,
`active=10632/8050`, and `vertical=906/0`. Thus thousands of native consumer
calls reconstructed the exact retained slot-1 Fg owner and readable active
state, but not one saw retained up/down reach the 0.20 threshold. Local records
still reached direct `0A` and retarget `0B` exactly as before. Both processes
were closed after capture.

This **CONFIRMS that the missing stage is vertical admission before command
evaluation** and rejects different Fg ownership, absent retained consumer calls,
and the screen-1 ADS-bit gate. The case-6 direct/retarget semantics remain
unclassified because their retained input is still zero.

A source audit identifies the concrete transport break. The steady callsite
`0x140CB416F -> PlayerAimController_SmoothAndPublishDirectionalAimWeights
0x140CB47A0` returns with valid controller `+0x8C0/+0x8C4` and blend `+0x8EC`,
as independently shown by local case-6 vertical values reaching 1.0. However
`hooked_smooth_and_publish_directional_aim_weights` only logs those values. The
wire snapshot `g_local_aim_pitch` is updated only by the transition callsite
`0x140CB4176`, which logged `AIM-PITCH-CAPTURE up=0 down=0` in both `eq`
processes. Repository-wide reference audit also proves
`StatePacket::aim_pitch_up/down` have no receiver reader at all: they are written
into the packet and cleared from flags, but never applied.

The next narrow hypothesis is therefore that completing this already-versioned
transport at the same steady publisher boundary will make retained case 6 see
the peer's vertical value. The sender must store finite post-blend up/down after
the stock smoother/publisher for screen 0. On screen 1, after the same stock
publisher returns, a fresh peer packet carrying `kStateFlagAimPitch` may replace
only active-state `aim-add-up/down` at `+0x188/+0x18C`; the original publisher
owns clearing when the packet is absent or stale. This boundary precedes the
already-proven `AnimStateLayer_CopyParametersToFirstInstance` copy and avoids all
rejected named-layer, Fg mirror, root, palette and branch patches.

Competing outcomes are: the screen-1 steady hook does not execute, selecting a
different earlier scheduled apply boundary; wire apply executes and retained
case 6 becomes eligible but normalized semantics differ, selecting that exact
case-6 route; or same-signature case-6 records match and visible pitch is still
absent, selecting the common pose/palette output. A bounded runtime test must
require nonzero sender capture, nonzero receiver apply, retained case-6 admission
and no shake/model corruption before claiming progress.

Build `2026-07-21er` implements exactly that contract. The screen-0 steady hook
stores finite `controller(+0x8C0/+0x8C4) * clamp(+0x8EC)` in the existing locked
wire snapshot and emits one nonzero `AIM-PITCH-WIRE-CAPTURE`. The same hook for
the exact retained screen-1 owner requires a packet no older than 250 ms,
`kStateFlagAimPitch`, strict peer ADS and finite unit-range values, then replaces
only active-state up/down after the stock publisher and emits one nonzero
`AIM-PITCH-WIRE-APPLY`. No packet or stale packet performs no write, leaving the
stock publisher authoritative for clearing. All `eq` stage/case-6 counters remain
enabled for the falsifier. The single Release DLL builds successfully.

### 2026-07-21 REJECTED/CONFIRMED: `er` has no visible pitch; case-6 inputs match

The user observed no remote vertical weapon movement, but also no shake, model
corruption or other regression. PIDs 22784/23956 remained responsive and logged
no exception before both were closed. Neither process emitted the required
nonzero `AIM-PITCH-WIRE-CAPTURE` or `AIM-PITCH-WIRE-APPLY`, so the new steady
wire hook is **not confirmed** and cannot be credited for the runtime state.

Nevertheless this run finally produced complete same-signature local/retained
case-6 pairs. For signature `0A/6C2B2396/4B0F5DE3/09A0`, PID 22784 screen 0 and
PID 23956 screen 1 both reached vertical up near 1.0 and took the direct branch
to target `6C2B2396/4B0F5DE3` with counts `651/8/8` and callbacks
`006CCB30/006CD220/006CD620`. For signature
`0B/27E57F43/FD6DBE03/09A0`, PID 23956 screen 0 and PID 22784 screen 1 both took
the retarget branch to `ED25C708/C39CA709`, counts `580/8/8`, entry 124,
map/coverage 1 and the same callbacks. Isolated group/context-flag timing samples
were not stable across the four records and cannot select a route.

This **REJECTS case-6 source/target skeleton selection, direct-vs-retarget
branch, retarget entry/map/coverage and callback selection as the missing visible
vertical discriminator**. Actual retained active-state vertical and complete
case-6 admission are now confirmed in a no-pitch run. The frontier moves to the
common pose output after `AnimCmdEvaluator_ApplyAnimationDirect 0x14217C7F0` and
`AnimCmdEvaluator_CopyMaskedPose 0x14217D280`: trace processing-group work-buffer
ownership through command blending/merge and final FgAnimData palette/skinning
publication before defining another runtime probe.

### 2026-07-21 STATIC: common pose output terminates at FgAnimData+0x230

The direct and retarget case-6 branches now have a complete common-output path.
Command 2 constructs one processing-group evaluator whose `+0x08` pointer array
indexes joint/scalar lanes, `+0x10` indexes their 16-byte ValidBits records and
`+0x20` selects the group descriptor. Newly named
`AnimCmdEvaluator_AllocateGroupLanes 0x14217C3A0` allocates those exact lanes.
Both case-6 writers and later `AnimCmdEvaluator_ApplyBlend 0x14217CF90` use the
same `output_index * group.byte4` addressing.

Command 3 calls newly named
`AnimCmdEvaluator_FinalizeGroupDefaults 0x141AB78F0`. It intersects the inverse
joint/scalar ValidBits with the group mask, fills only the missing ranges from
the target skeleton defaults, marks those ranges valid, and then publishes the
first finalized joint lane to the common WorkBuffer at `+0x50`. Command 15
allocates the separate 64-byte-per-joint matrix workspace at WorkBuffer `+0x58`;
command 16 reaches newly named
`AnimCmdEvaluator_ExecuteSkeletonJointPrograms 0x14217B630`, which walks the
target skeleton's bounded joint-program records and dispatches their operators
over the finalized JointParams.

The palette edge is exact rather than inferred. `AnimManager_CreateAnimCmdsForObject
0x141A8FCF0` emits ten-word opcode 26 with its first destination derived from the
builder field that `0x141A91490` filled from `FgAnimData+0x230`; start/count occupy
words 8/9. `ProcessAnimCmdListSegment` case 26 bounds that range to the current
processing group and calls newly named
`AnimCmdEvaluator_WriteSkinningPalette 0x14218B170`. The helper consumes the
64-byte joint workspace plus supplied basis/inverse-bind data and writes three
float4 rows (48 bytes) per joint directly to that opcode-26 destination. The
normal `AnimManager_PerformAnimBlend` path has no later palette store because the
command list already wrote the current buffer; the fallback independently swaps
and fills the same `FgAnimData+0x230/+0x238` pair.

This satisfies four independent evidence types: the shared evaluator lane and
ValidBits dataflow; command-3 default-fill plus WorkBuffer publication; the
skeleton joint-program dispatch; and independent generator/consumer agreement
on the exact opcode-26 `FgAnimData+0x230` destination and 48-byte stride. The
remaining competitors are now concrete: (1) later command topology, blend weights
or plugin ids differ and overwrite the matched case-6 result; (2) those commands
match but ValidBits/evaluation/joint-program output diverges before the palette;
or (3) the completed current palette responds equivalently and the loss is in a
downstream render/post-palette consumer.

Read-only build `2026-07-21es` implements that discriminator without adding a
new hot-path detour. On the already proven ProcessAnim wrapper it accepts only
the two exact case-6 signatures and valid Player/Fg ownership with at least 0.20
vertical state in processing group 0, which supplied complete pairs in the
previous run. It hashes the complete opcode/word topology, every case-7
left/right/output/mode/weight tuple, and case-23/24 plugin ids; it counts command
3/16/26 stages and proves each opcode-26 destination equals current
`FgAnimData+0x230 + 48 * first_joint` under the native range. After the untouched
consumer returns, it reads that same palette, keeps a private first-sample
baseline per screen/signature, and reports total rotation/translation delta plus
the six largest joint rotation deltas. Four samples per screen/signature are
allowed at 500 ms intervals; no game memory is written.

Written falsifier: a stable same-signature topology/blend/plugin difference
selects only the late-command overwrite route. Matching command summaries with
local palette response but no corresponding retained response select the common
evaluator/ValidBits/joint-program region. Corresponding local/retained palette
response while the user still sees no pitch selects a consumer after
`FgAnimData+0x230`. Invalid destination/layout/readability guards, a missing
same-signature pair, unrelated raw pose hashes, crash or any new visible model
problem make the test inconclusive. The single Release DLL builds successfully.

### 2026-07-21 INCONCLUSIVE: `es` sampled a segment without opcode 26

The user swept vertical ADS with three weapons and again observed no remote
vertical weapon movement and no new bug. PIDs 15512/23648 remained responsive,
logged no exception and were closed after capture.

All bounded `AIM-COMMON-OUTPUT` records reported `stages=1/1/0`,
`case26_count=0`, `palette_targets_valid=0` and `written=0`. The selected
`ProcessAnimCmdListSegment` invocation therefore contained command 3 and command
16 but not the command-26 palette publication. The palette read after that
invocation sampled the shared buffer after a segment which did not write it;
its raw hashes and large joint deltas cannot be attributed to the selected
case-6 evaluation and are non-semantic.

This makes build `es` **inconclusive** under its written guard rather than a
negative palette result. The static opcode-26 destination proof remains valid,
but the runtime boundary is split across command-list segments. Vertical ADS is
parked at the user's request. If resumed, first correlate the same WorkBuffer
across segments and select the invocation that actually counts command 26;
do not compare whole-palette deltas from a command-3/16-only segment.

### 2026-07-21 STATIC: Nadine double-weapon symptom splits at the legacy look

The current priority is Nadine's weapon presentation: drawing a pistol leaves a
rifle intersecting it in the hand, and putting weapons away leaves the rifle in
the hand instead of across the shoulder. History proves that the general remote
Player inventory/draw state is not the first broken layer. Build `b3549d8`
already confirmed `PlayerWeaponController+0x120/+0x134` and native
`ApplyWeaponSwitch(-1)` holster; build `3cdb21c` confirmed slot mirroring without
flicker or locomotion regression.

The remaining symptom has two independent sources. Commit `37cb845` removed all
PlayerWeaponController weapons and the rifle still rendered, proving that this
rifle is built into the only usable Nadine Player look `0xd27c074f`. Separately,
`PlayerWeaponHolsterAction_ResolveAndBindAttachPoint 0x141095BD0` resolves each
player-owned weapon's `weapon-holster-point` from its art definition and binds it
through `Player+0x1DF0` virtual `+0xB8`; that authored AP is absent from the
companion look, so a player-owned holstered weapon can still fall back to a hand.
`Player_KickPlayerDepenetrateWeapons 0x1410EDFB0` is only the downstream wrist
depenetration transform path and is not a holster fix.

Three independent evidence types select the first discriminator: the inventory-
removal runtime result; the appearance-callsite/look-builder dataflow; and stable
DrawControl structure across many rebuilds. Chloe look `0x3f334972` owns two
parent meshes, while legacy Nadine owns five with the exact ordered ids
`6E18DCD5,4A7DF04F,A674E571,5EAAF523,C450A073`. The competing hypotheses are:
(H1) the built-in rifle is one standalone look mesh; (H2) it is merged into a
body/clothing mesh; or (H3) the visible overlap is only a ProcessWeapon attachable,
which the inventory-removal result predicts against.

Read/write-bounded build `2026-07-21et` cycles F11 through baseline and one hidden
legacy-look parent mesh at a time. It requires the complete five-id signature,
uses the stock `IDrawControl_HideMesh 0x141754510`/`ShowMesh 0x141754A30`, restores
the prior probe mesh before changing selection, and never changes inventory,
weapon state, attachment transforms or Chloe/other looks. Falsifier: exactly one
F11 index removing only the persistent rifle confirms H1 and becomes the default
suppression; rifle plus a body/clothing region disappearing selects H2 and rejects
mesh suppression; no index affecting the rifle rejects the parent-look mesh route
and moves to attachable/process identification. The player-owned shoulder AP is a
separate second stage after the baked rifle is isolated.

### 2026-07-21 REJECTED: persistent rifle is not a parent look mesh

The user cycled all five `et` mesh states. They removed Nadine's body parts in
sequence (head, torso, arms, legs and the remaining body section). After all five
parent meshes had been classified, hair, the rifle and the grenade still floated
in the scene. Both processes remained responsive and were closed after the test.

This **rejects H1 and the parent-look mesh suppression route**. It also corrects
the old wording from commit `37cb845`: the rifle is look-created and survives
PlayerWeaponController inventory removal, but it is not baked into one of the
five parent body meshes. Hair, rifle and grenade are independent children of the
Player DrawControl/attachment graph. The next read-only boundary is already
installed: at `NdAttachableObject_PostJointUpdate_Async 0x141ED28E0`, callsite
`0x141ED63F7` supplies child DrawControl, parent DrawControl and the current
attachable object. A bounded census of children whose parent has the exact Nadine
five-mesh signature can distinguish their object vtable, type id, child mesh ids,
parent handle and any ProcessWeapon art id without writing game state. Only after
the persistent rifle child is identified may a visibility or attachment fix be
defined; hiding body meshes must not be repeated.

### 2026-07-21 STATIC: bounded child-attachable census (`eu`)

Build `2026-07-21eu` disables every write from the rejected F11 parent-mesh
probe and reuses the already-installed callsite `0x141ED63F7` only as a read
boundary. The hook accepts a sample only when the parent DrawControl owns the
complete ordered Nadine signature
`6E18DCD5,4A7DF04F,A674E571,5EAAF523,C450A073`; it then records at most 32
unique child signatures: attachable vtable RVA/type id, child DrawControl mesh
ids, `+0x4E0/+0x4E8` parent handle and a guarded `+0x860` art candidate. It does
not change visibility, attachment state, inventory or transforms.

The evidence quota for this diagnostic is satisfied by three independent
types: static callsite dataflow supplies `{child DrawControl, parent DrawControl,
attachable}`; the exact five-id parent signature isolates the one legacy Nadine
look from Chloe and other draw graphs; and runtime mesh isolation proved that
hair, rifle and grenade survive as separate children after every parent body
mesh is removed. Competing outcomes are: (H1) three stable child classes/mesh
signatures expose the rifle directly; (H2) only ordinary hair/cloth children
cross this callsite and the rifle uses another attachment boundary; (H3) the
rifle is a ProcessWeapon child and exposes a known weapon art at `+0x860`.

Written prediction/falsifier: normal gameplay with Nadine visible should emit
stable `NADINE-ATTACHABLE` records for the surviving child topology without any
model change. A child carrying a known rifle art selects H3; otherwise distinct
stable vtable/mesh signatures for hair, rifle and grenade select H1 and must be
resolved statically before any visibility write. Only hair/cloth records, no
records despite the exact parent being visible, an exception, or any visual
regression makes the boundary inconclusive and forbids a suppression build.

### 2026-07-21 INCONCLUSIVE: `eu` accepted no child records

Normal gameplay with Nadine visible produced no `NADINE-ATTACHABLE` record in
either PID 23960 or PID 19948. Both processes stayed responsive, the `eu` init
tag and callsite-hook installation were present, and no exception/crash marker
appeared. They were closed after capture.

This is **not** evidence that the rifle is not an attachable. The same exact
parent matcher at the same callsite drove all five successful F11 body-mesh
states in build `et`, so the unresolved rejection is downstream of the parent
boundary: the `attachable >= 0x4F0` readability guard, child DrawControl snapshot,
or the assumption that RBX is a fully readable base object. The next build must
remain read-only and log one bounded reason record immediately after the exact
five-mesh parent match, before applying those guards. Do not broaden the parent
filter and do not re-enable visibility writes.

### 2026-07-21 STATIC: exact-parent rejection telemetry (`ev`)

Build `2026-07-21ev` moves logging immediately behind the already-proven exact
five-mesh Nadine parent matcher. It no longer requires a non-null or `0x4F0`-
readable RBX value and no longer discards a failed child DrawControl snapshot.
Each of at most 32 semantic signatures records raw RBX/child addresses, readable
extents, guarded vtable/type/parent/art fields, snapshot success and any readable
child mesh ids. All rejected F11 visibility code remains compile-time disabled.

Written discriminator: `attachReadable < 0x4EC` with a valid child snapshot
rejects the fully-readable base-object assumption; `snapshot=0` with a readable
RBX selects the child DrawControl interpretation/capture; `snapshot=1` and a
readable vtable finally yields the child classes needed to identify the rifle.
Still receiving no `NADINE-ATTACHABLE-HIT` record would contradict the prior `et`
reachability result and make timing/rebuild at this callsite the only remaining
explanation. An exception or visual change rejects the diagnostic.

### 2026-07-21 CORRECTION: `eu`/`ev` telemetry was compiled out; build `ew`

After `ev` again produced no record, the user explicitly drew and holstered both
pistol and rifle and moved the player; PIDs 2472/20636 remained responsive and
logged no exception. Source audit then found the decisive instrumentation error:
production sets `kEnableRuntimeDiagnosticLogging=false`, so `force_log_line()` is
compile-time a no-op. `et`, `eu` and `ev` all used that function for their result
lines. Their absent lines contain **no evidence** about callsite reachability,
parent matching, RBX readability or child snapshots. This supersedes the
downstream-guard interpretation in the earlier `eu` note. The visible `et` body-
mesh result remains valid because mesh writes did not depend on logging.

Build `2026-07-21ew` keeps global/high-volume runtime diagnostics disabled and
routes only the already-bounded exact-parent census (maximum 32 semantic
signatures) through the persistent writer. It makes no visibility, attachment,
inventory or transform write. Prediction and falsifier remain those of `ev`, but
an empty census is meaningful only now that the dedicated result writer is live.

### 2026-07-21 CONFIRMED: exact Nadine child identities (`ew`)

Build `ew` produced stable exact-parent records in responsive PID 19324 with no
exception or visual regression. The surviving objects are now separated without
guessing:

- `ProcessWeapon::vftable 0x142A9BF48`, art `0x33E29A1B`, child mesh
  `0x33E29A1B` = pistol;
- the same `ProcessWeapon` class, art `0x8075397D`, child mesh `0x255BEBA0` =
  the rifle which overlaps the pistol and remains in the hand;
- the same class, art `0x0B8BD6D6`, child mesh `0x836D013E` = grenade;
- `ProcessCharCloth::vftable 0x142BAF780`, child mesh `0x7368D4A2` = hair.

All three weapon-family objects for the affected exact parent share handle/id
`0x10E0201128/0x493` and resolved-parent id `0x493`; snapshots succeed and each
owns one readable child mesh. IDA independently resolves vtable `0x142A9BF48`
through constructor `0x14150F2E0` and destructor `0x141510990`, both explicitly
installing `ProcessWeapon::vftable`. The grenade art is independently established
by the confirmed grenade relay. Thus the persistent rifle is a real
`ProcessWeapon`, not parent clothing geometry or `ProcessCharCloth`.

Do not simply hide art `0x8075397D`: the same object must render when the rifle is
selected. The next discriminator is exact pointer membership in
PlayerWeaponController slots on the game thread. If the census pointer equals a
slot object while pistol/current-state is active, the failure is the downstream
`weapon-holster-point` bind. If it is absent from every slot, it is a look-created
orphan and can be suppressed only outside a real rifle selection. This pointer
comparison must precede any visibility or transform write.

### 2026-07-21 STATIC: ProcessWeapon inventory-membership correlation (`ex`)

Build `2026-07-21ex` keeps the exact-parent async hook read-only: it publishes
only `{ProcessWeapon pointer, parent DrawControl, art, first mesh id}` through a
bounded eight-slot atomic bridge. The established game-thread weapon tick then
matches the parent DrawControl against retained Player 0/1, compares the exact
pointer with all controller slots 0..22, and records slot mask, controller current
slot/draw state, hand-attached slot and active weapon pointer/art. At most 16
state changes per candidate are written. No engine vtable call is made from the
async hook and no game field is modified.

Three independent evidence types justify the discriminator: RTTI/vtable xrefs
prove the candidates are `ProcessWeapon`; the `ew` census proves the exact arts,
meshes and common Nadine parent; and `PlayerWeaponController::GetWeaponBySlot`
vtable `+0xB0` plus the confirmed `+0x120/+0x134` fields provide an independent
inventory/draw-state view on the game thread. Written prediction: while the user
cycles pistol, rifle and holster, a rifle pointer present in a stable slot while
the pistol is current/drawn confirms a normal inventory object whose authored
holster attachment failed. A rifle pointer absent from all slots while another
slot-owned rifle becomes active confirms a separate look-created orphan. No
owner match, unstable pointers, exception or visual change makes the test
inconclusive and forbids a visibility fix.

### 2026-07-21 CONFIRMED: persistent Nadine rifle is the normal slot-0 weapon (`ex`)

The completed `ex` run rejected the remaining look-created-orphan hypothesis.
Exact pointer equality on the established game-thread controller boundary mapped
rifle art `0x8075397D` to slot mask `0x1` (slot 0), pistol art `0x33E29A1B` to
slot mask `0x2` (slot 1), and grenade art `0x0B8BD6D6` to slot mask `0x4`
(slot 2). The mapping was observed for both retained owner indices. During object
recreation an old pointer briefly lost membership and was replaced by a new exact
candidate, but each stable rifle candidate again resolved to slot 0; this is
normal lifetime churn, not a second orphan object.

During the pistol transition the rifle remained the slot-0 object while the
controller reported `current=-2`, `draw=5`, `hand=0` and the pistol as the active
art. Later the controller settled to `current=-1`, `draw=0`. Together with the
user-visible rifle remaining in the hand, this selects a failure after inventory
selection: the normal rifle is not rebound to its holster attachment point.
Visibility suppression is forbidden because it would also hide the same object
when slot 0 is legitimately drawn.

The next read-only boundary is
`PlayerWeaponHolsterAction_ResolveAndBindAttachPoint 0x141095BD0`. Competing
hypotheses are: (H1) the function is not reached for the affected clone; (H2) its
`weapon-holster-point` knowledge lookup returns no authored record for legacy
look `0xD27C074F`; or (H3) the record resolves but the final Player `+0x1DF0`
virtual `+0xB8` bind rejects or misapplies it. The next probe must compare this
boundary against the unaffected Chloe path and make no attachment write.

### 2026-07-21 STATIC: outer holster action plus exact knowledge lookup probe (`ey`)

Three independent evidence types now support a bounded runtime discriminator.
First, decompilation traces the action handle at `action+0x30/+0x38` to the live
`ProcessWeapon`, reads art at `+0x860`, constructs the per-art
`weapon-holster-point` id, resolves it through
`ResolveKnowledgeRegistryEntryById 0x141765030`, and finally passes the copied
handle to Player `+0x1DF0` virtual `+0xB8`. Second, retail disassembly confirms
the exact direct lookup call at `0x141095E49` (`E8 -> 0x141765030`) and the final
indirect bind at `0x141095F0D`; the 19-byte outer prologue contains complete,
non-RIP-relative instructions. Third, runtime `ex` proves the affected rifle is
the normal live slot-0 `ProcessWeapon`, so this action boundary is downstream of
the already-correct inventory identity.

Build `ey` is read-only. Its outer detour copies that exact 19-byte prologue and
uses a register-preserving RIP-indirect trampoline jump because entry RAX still
holds the original stack pointer at `0x141095BE3`. A second near relay replaces
only the confirmed direct lookup call. Thread-local scope correlates lookup id,
entry and first payload pointer with the exact action/player/weapon, resolved
look, controller state and unchanged final bind result. Output is deduplicated
and bounded to 64 signatures; no argument, return value or game field is changed.

Written prediction/falsifier: no `NADINE-HOLSTER-BIND` record for a visible
Nadine holster transition selects H1 (outer action not reached). A record with
`lookup=0`, null entry or null payload selects H2 (category/knowledge resolution
failure). A non-null lookup record with the rifle still visibly in hand selects
H3 at or after the final bind and justifies tracing/materializing a compatible
shoulder AP. Chloe provides the unaffected comparison. Any hook-install skip,
exception, crash or new model regression invalidates the probe and forbids a
repair write.

### 2026-07-21 CONFIRMED: holster record resolves; failure is downstream (`ey`)

Both injected `ey` processes remained responsive and installed both probe
boundaries. The affected legacy-Nadine paths were reached for both a role-B local
Player (`owner=0`) and the retained remote Player (`owner=1`, look
`0xD27C074F`). For rifle art `0x8075397D` every recorded path called the exact
knowledge lookup with id `0x68F32771` and received a non-null entry and payload.
The payload pointer was identical to Chloe's unaffected rifle path, and the outer
function completed through the same final-call return value and non-null
Player+0x1DF0 controller. Pistol art `0x33E29A1B` consistently skipped this
rifle-specific lookup on both looks, providing an independent control that the
scoped callsite capture was not spuriously firing.

This **rejects H1** (action not reached) and **rejects H2** (missing/null authored
`weapon-holster-point` knowledge record for Nadine). It also corrects the older
asset-level wording: the shared per-weapon holster record exists and resolves;
what differs is its application to the legacy look's attachment graph. H3 is
selected at or after Player+0x1DF0 virtual `+0xB8`. The next static task is to
resolve that concrete vtable target and trace where it maps the shared record to
the active DrawControl/skeleton attach point. No synthetic shoulder transform is
authorized until that mapping failure is proven.

### 2026-07-21 STATIC: PlayerWeaponAnimControl holster-joint discriminator (`ez`)

Resolving the concrete RTTI/vtable target corrects the earlier description of
the final call. Player initialization creates a 0xE0-byte
`PlayerWeaponAnimControl` through `0x14108F4D0`, installs vtable `0x142A46778`
and stores the object at `Player+0x1DF0`. Its vtable `+0xB8` implementation is
`0x14108DAB0`, which only copies the supplied 16-byte weapon handle into
control `+0x30`; it does not bind a DrawControl attachment point. The holster
action also updates animation-control state through slots `+0x100/+0x110/+0xF0`
before storing that handle.

The next concrete data dependency is in the same RTTI-backed class.
`PlayerWeaponAnimControl_InitializeHolsterJoints 0x14108E700` hashes and resolves
exact skeleton joints `r_leg_gun_holster`, `r_upper_leg`, `pelvis_holster` and
`pelvis`, storing signed indices at control `+0x78/+0x7C/+0x80/+0x84` (or `-1`
when absent). `PlayerWeaponAnimControl_UpdateHolsterJointTransforms
0x14108EBF0` reads those exact pairs and writes the two 32-byte relative
transforms at `+0x90/+0xB0` only when both indices in a pair are valid.
`PlayerWeaponAnimControl_HandleNamedWeaponEvent 0x14108DE30` is the later
consumer of the stored weapon handle and publishes authored animation events to
the resolved weapon process.

This yields three competing explanations: (J1) legacy Nadine lacks one of the
four exact holster joints, leaving an index pair invalid; (J2) all indices exist
but one relative-transform cache remains default/stale because the update path
does not complete for her look; or (J3) indices and transforms are populated,
moving the failure to the later named animation event. Independent evidence is
the RTTI/vtable identity, the named-joint pseudocode/dataflow, and runtime `ey`
proof that both looks use this live control with the same rifle handle/record.

Build `ez` keeps the proven outer hook read-only and adds only guarded reads of
the four signed indices and 64 raw transform bytes from the exact control. Its
written prediction: Chloe must show four valid indices and populated transforms.
Any Nadine `-1` with the corresponding transform absent selects J1; four valid
indices but a default/stale Nadine transform selects J2; equivalent valid indices
and populated transforms reject both and select J3. Any install skip, exception,
crash or model regression invalidates the test.

### 2026-07-21 INCONCLUSIVE: `ez` exhausted its semantic-dedupe table on live transforms

Both `ez` instances remained stable and the Chloe control consistently reported
four valid indices `101,5,199,3` plus two populated, continuously changing
transform blocks. However, the transform words themselves were included in the
64-entry uniqueness signature. PID 19644 therefore consumed all 64 entries on
the first Chloe rifle transition; PID 18504 consumed 63 on Chloe rifle plus one
on Chloe pistol. No `look=0xD27C074F` sample was admitted. This does not select
J1, J2 or J3 and must not be interpreted as evidence that Nadine skipped the
action: build `ey` already proved that the same action reaches both looks.

The corrected discriminator will read `Player+0x1DF0` from the established
game-thread peer-state tick and deduplicate only stable identity/look/joint
fields. The dynamic transform words remain output-only. This guarantees a
bounded census of both retained Players independent of weapon-action frequency.

### 2026-07-21 STATIC: bounded game-thread control census (`fa`)

Build `fa` adds no game write or engine call. On the already established
game-thread peer-state tick it takes two snapshots, 750 ms apart, for each
retained Player/control/look/joint identity. It validates the RTTI vtable RVA,
records control skeleton/Player owners, stored weapon handle, four joint indices,
selectors `+0xD0/+0xD4/+0xD8`, and both transform blocks. Only stable identity,
owners and indices participate in admission; handle, selectors and transform
words are output-only. The old action-hook logger is also corrected so animated
transform words no longer consume its semantic-dedupe table.

Written prediction/falsifier: both owners must emit exactly two
`PLAYER-WEAPON-ANIM-CENSUS` records with `readable=1`, `expected=1` and
`controlOwner==player`. A legacy-Nadine negative joint selects J1. Four valid
joints but a zero/non-finite or unchanged default transform block selects J2.
Four valid joints with finite, nonzero and changing blocks reject J1/J2 and move
the failure boundary to `PlayerWeaponAnimControl_HandleNamedWeaponEvent
0x14108DE30`. A missing owner, wrong vtable, exception, crash or unrelated model
regression invalidates this probe.

### 2026-07-21 CONFIRMED: legacy Nadine lacks both authored holster joints (`fa`)

Build `fa` remained stable in both processes and independently produced the
same look-specific result. Every Chloe control (`look=0x3F334972`) had joint
indices `101,5,199,3`; its two finite transform blocks were non-default and
changed between the 750 ms samples. Every legacy-Nadine control
(`look=0xD27C074F`) had `-1,5,-1,3`: `r_leg_gun_holster` and
`pelvis_holster` are absent, while their intended parents `r_upper_leg` and
`pelvis` resolve. Both transform blocks stayed at the identity encoding
`0,0,0,0,0,0,0,0x3F800000` and `changed=0`.

The discriminator also ruled out stale/wrong control ownership: every sample
had `readable=1`, vtable RVA `0x2A46778`, `expected=1`, and
`controlOwner==player`. J1 is therefore CONFIRMED. J2 and J3 are rejected as the
primary boundary, though downstream authored-event behavior still matters after
providing a compatible fallback. The next step is to trace all consumers of the
two index/relative-transform pairs and prove whether mapping each missing child
to its resolved parent is a safe identity fallback.

### 2026-07-21 STATIC/REJECTED: identity joint fallback cannot move the weapon

A complete displacement audit of PlayerWeaponAnimControl found only three roles
for `+0x78..+0xCF`: `0x14108E700` initializes the joint indices and identity
transforms, `0x14108EBF0` conditionally derives each child transform relative to
its parent, and `PlayerWeaponAnimControl_UpdateHolsterIk 0x14108ECE0` consumes
the parent index (`+0x7C` or `+0x84`) plus that parent-relative transform
(`+0x90` or `+0xB0`). The transform helpers independently establish the math:
`0x1401990F0` derives `inverse(parent) * child`, while `0x1401998F0` composes
`parent * relative`.

Therefore substituting `r_upper_leg`/`pelvis` as each missing child would merely
make `0x14108EBF0` write identity again. `0x14108ECE0` already composes the same
parent with the current identity cache, so the proposed fallback has no
behavioral delta. REJECT it without a runtime launch. The absent marker joints
remain CONFIRMED, but they explain the missing authored IK offset rather than by
themselves proving the final visible prop-reparent failure.

The final named-event graph is now concrete. `PlayerWeaponAnimControl` vtable
`+0xE8` (`0x14108DE30`) extracts the authored `name` StringId and publishes a
structured entry through the resolved ProcessWeapon virtual `+0xD0`. That target,
`0x141BB7960`, is shared by hundreds of Process vtables, not Player-specific;
ProcessWeapon RTTI vtable `0x142A9BF48` slot 24 is `0x1401D4EE0`, which returns
the event controller at `Process+0x218`. Generic dispatcher `0x141E72540` then
executes or defers the matching script event. The old
`Player_PublishWeaponInventoryEntry` / `WeaponController_ApplyInventoryEntry`
labels were consequently rejected and replaced by generic Process-event names.

The authored mapping between `0x9E2D6552` and a native command lives in
state-script data, so `0x14197FA30` must not be called the attach handler: its
switch only formats the debug/event-log string. The physical mutation was found
independently through two registered script natives:
`Script_ReattachPlayerWeapon 0x1403421B0` and
`Script_AttachPlayerWeaponBackToHolster 0x140342EE0`. Both validate a
`ProcessWeaponBase`, call `WeaponHolster_BuildBinding`, resolve the selected id
in the owner attachment table at `+0x1E0`, and reach
`WeaponAttachment_AttachToOwnerPoint 0x141ED0370`. That function replaces the
weapon parent handle at `+0x4E0`, updates the attachment descriptor at `+0x6A0`,
selects/writes the owner attachment index at `+0x688/+0x691`, and runs the
attachment refresh virtuals. The script natives publish `set-attached` only
after this path.

Competing boundaries for the visible rifle are now: (E1) legacy Nadine never
emits `0x9E2D6552` (`reparent-weapon-to-holster`); (E2) both emit it, but
`WeaponHolster_BuildBinding 0x1414FE540` selects a different or generic
`gun_holster*` record for Nadine; (E3) event and binding match, so failure lies
after the common Process dispatcher in the attachment consumer. Evidence quota
for a read-only discriminator is satisfied by the RTTI/vtable chain, the named
event and binding pseudocode/dataflow, and the independent embedded event-name
string/logger mapping.

Written prediction: after cycling pistol and rifle on both copies, E1 produces
the reparent event for Chloe but not legacy Nadine; E2 produces the event for
both but different selected binding ids or default/changed transform payloads;
equivalent event and binding records reject E1/E2 and select E3; direct reach
and return status at `0x141ED0370` then separates a skipped state-script command
from an attach-point lookup/mutation failure. Any write, crash, missing
hook-install marker, invalid ProcessWeapon vtable/owner, or model regression
invalidates the test.

Build `fb` implements that prediction with three pass-through entry detours and
no attachment writes of its own. `NADINE-HOLSTER-EVENT` records the authored
event StringId from `0x14108DE30`; `NADINE-HOLSTER-BINDING` records the selected
id, both transforms, scalar and whether the target exists in owner `+0x1E0`;
`NADINE-HOLSTER-ATTACH` records direct `0x141ED0370` reach, target presence,
return value and pre/post parent/index state.

### 2026-07-21 CONFIRMED: legacy Nadine lacks the selected weapon attach records (`fb`)

Build `fb` stayed stable in both processes and selected a single concrete
boundary. `WeaponHolster_BuildBinding 0x1414FE540` produced the same attach id
and the same transform/scalar payload for Chloe and legacy Nadine. For pistol art
`0x33E29A1B` it selected art-specific id `0x00AD047A`; Chloe resolved it at owner
attachment index 40 (`present=1`, 28 samples), while legacy Nadine had no record
(`present=0`, `index=-1`, 16 samples). For rifle art `0x8075397D` it selected
generic `gun_holster` id `0x555EFB8D`; Chloe resolved index 8 (`present=1`, 26
samples), while legacy Nadine again had no record (`present=0`, `index=-1`, 18
samples). The independent grenade control `0x0B8BD6D6` selected `0x754D9C3B`,
which exists for both looks (Chloe index 42, Nadine index 37).

This rejects E2's wrong/different binding payload and confirms the gap in the
active look/owner attachment table at `Player+0x1E0`: legacy Nadine is missing
exactly the selected pistol and rifle attachment ids, while a neighboring
weapon-class attachment remains authored. `WeaponAttachment_AttachToOwnerPoint
0x141ED0370` independently succeeded (`result=1`, `present=1`) for hand attach id
`0x61A48E01` on both looks and both firearm arts, proving that the generic attach
mutation and hand path are healthy. No final-attach record used either missing
holster id, consistent with the native command being gated by its preceding
owner-table lookup. The event probe emitted no records for either look and is
therefore non-discriminating, not evidence for a Nadine-specific event loss.

The next task is static: recover the construction, ownership, record format and
mutation/copy APIs of `Player+0x1E0` before adding data. A compatible record may
only be materialized after proving its allocation/lifetime rules and the target
joint/transform relationship; hand id `0x61A48E01` is the current broken fallback
and is not a repair target.

### 2026-07-21 STATIC: the stock AttachSystem has safe slack and an append API

The owner field is an `AttachSystem*`, allocated as a 24-byte container at
`NdGameObject+0x1E0`. Exact source assertions identify
`Player_SetupAttachSystemFromLookData 0x1410CDF90` and
`AttachSystem_InitFromDcData 0x1418B61B0`. Setup selects `player`, `player-net`
or `player-young-drake` and calls the loader with reserve count zero. The loader
resolves `*attach-points*` DC data against the active rig, allocates
`capacity * 48` bytes and materializes only records whose authored joints exist.
The container layout is capacity `+0x0`, live count `+0x4`, records `+0x8` and rig
context `+0x10`; a record is transform `+0x0..+0x1F`, StringId `+0x20`, signed
joint index `+0x24`, flag bit0 `+0x26` and runtime dword `+0x28`.

Three independent observations make an in-place append preferable to any
allocation replacement. First, `AttachSystem_AppendRecord 0x1418B6E50` checks
`count < capacity`, validates the signed joint against the active rig and copies
exactly that record form. Second, live read-only process memory in both copies
matched `capacity=45,count=44` for Chloe and `capacity=45,count=39` for legacy
Nadine: the loader retained six unused slots after five authored records failed
joint resolution. Third, the five missing Chloe joints were recovered directly
from the live rig name table: `spine_prop_attachment` (47),
`spine_prop_alt_attachment` (48), `flashlight_attach` (106), `l_pouch_offset`
(127) and `pelvis_holster` (199). The two records selected by current firearms
are exactly `targSpineProp 0x00AD047A -> spine_prop_alt_attachment` and
`gun_holster 0x555EFB8D -> pelvis_holster`. Legacy Nadine lacks those marker
joints but contains their standard parents `spined` (18) and `pelvis` (3).
The Chloe source indices differ: `spined` is 29 and `pelvis` is 3. Across 40
live transform snapshots, `spined(29) -> spine_prop_alt_attachment(48)` stayed
rigid at RMS `~2.9e-5`; progressively higher spine ancestors drifted from
`~0.0034` to `~0.0123`. This independently selects `spined`, rather than merely
choosing the nearest plausible bone name. The two source spine markers 47/48
were also effectively coincident.
For the rifle, the earlier world-pose census likewise selected
`pelvis(3) -> pelvis_holster(199)` over every other joint (RMS `~0.0075`). That
agrees with the independent `InitializeHolsterJoints` child/parent pair and its
`UpdateHolsterJointTransforms -> Transform_MakeRelative(parent, child)` consumer.
Unlike the spine marker, this relative transform changes during the holster
animation, so the one-time composite deliberately captures the pose at repair;
placement/animation quality remains a separate runtime falsifier.

The bounded repair therefore needs no new allocation and must not copy Chloe's
raw joint indices. For each missing id it will derive the donor marker relative
to its standard parent with `Transform_MakeRelative 0x1401990F0`, compose that
with the donor record transform through `Transform_Compose 0x1401998F0`, replace
the joint index with Nadine's resolved `spined`/`pelvis`, clear the runtime dword
and append through the stock API. Competing fixes are now: (R1) direct raw-record
copy, rejected because joint 48/199 means unrelated or out-of-range data on
Nadine; (R2) rebuild/reallocate the whole container, unnecessary and riskier
because six stock slots are free; (R3) parent-remapped composite append, selected.
The ordinary look-change path clears/rebuilds this same container, so the repair
must be idempotent and repeat after a records-pointer/count change. The visible
`PlayerBase -> Character -> NdGameObject` destructor chain does not expose a
separate safe ownership replacement seam; no replacement is needed for R3.

Written prediction for build `fc`: before the append, legacy Nadine is
`39/45` and lacks both ids; afterward it is `41/45`, both lookups resolve and
`NADINE-HOLSTER-BINDING` changes to `present=1` for rifle and pistol without any
change to Chloe or grenade. Cycling rifle/pistol draw and holster must move the
inactive weapon away from `rightHand`; a crash, count outside `41/45`, a copied
joint 48/199, a missing hook marker, lost body part, broken grenade or visible
regression falsifies the repair. Placement may still need a bounded transform
adjustment, but record presence and physical reparenting must succeed first.

### 2026-07-21 REJECTED: build fc never passed its look-identity gate

Both copies stayed stable and exercised rifle/pistol draw/holster, but the
written append prediction failed before mutation: there was no
`NADINE-ATTACH-REPAIR` marker, legacy Nadine remained `present=0/index=-1` for
both ids, and the final path continued to attach to `rightHand 0x61A48E01`.
This is a mod admission bug, not evidence against the AttachSystem repair.
`ensure_legacy_nadine_attach_points_game_thread` runs immediately before the
PlayerWeaponAnimControl census; that census repeatedly saw retained Chloe
`Player+0x170=0x3F334972` and Nadine `Player+0x170=0xD27C074F`, proving the tick
and both inputs were live. The rejected gate instead called virtual vtable slot
94, whose look/appearance projection did not match either raw active-look id, so
the function returned without even emitting a failure record.

Build `fd` changes only that rejected discriminator to the exact `Player+0x170`
field already consumed by the confirmed holster binding and census paths. The
original `39/45 -> 41/45`, ids `0/0 -> 1/1`, remapped joints `48:29->18` and
`199:3->3`, no-crash/no-body-regression prediction remains otherwise unchanged.

### 2026-07-21 CONFIRMED/REJECTED: fd repairs lookup, not physical reparenting

Build `fd` passed the bounded AttachSystem portion in both processes. Each legacy
Nadine produced `status=1`, `39/45 -> 41/45`, ids `0/0 -> 1/1`, and exactly the
predicted remaps `spine=48:29->18`, `pelvis=199:3->3`. Rebuilt/player-replaced
instances subsequently reported `status=2` at `41/45`, demonstrating that the
repair survives the observed copy/rebuild path. `WeaponHolsterBuildBinding`
then resolved pistol `0x00AD047A` at index 39 and rifle `0x555EFB8D` at index 40
on legacy Nadine. Both processes remained responsive and emitted no exception.

The larger physical-reparent prediction is REJECTED. Across both processes the
final attach hook saw four legacy-Nadine mutations, all to
`rightHand 0x61A48E01`, and none to either newly-present id. The exact
`PlayerWeaponAnimControl` cache stayed `joints=-1,5,-1,3`: its missing authored
child slots were initialized before the new AttachSystem records existed and
are a separate gate downstream of lookup presence. Therefore record materializing
is retained as CONFIRMED infrastructure, but it is not by itself the visible
weapon fix. Next work must recover the stock lifecycle/refresh API for
`PlayerWeaponAnimControl+0x78..+0x84`; direct field writes remain disallowed.

### 2026-07-21 STATIC: bounded inactive-weapon recovery candidate (`fe`)

The cache-refresh alternative is rejected. `PlayerWeaponAnimControl` vslot
`+0x130` (`0x14108E700`) resolves four skeleton-joint names and never reads the
AttachSystem; repeating it also allocates/reinitializes `ArmIkChain` and replaces
control `+0x70`. Vslot `+0x138` only updates a relative transform when both
cached indices are valid. Thus fd records cannot make `-1,5,-1,3` valid, and
neither a refresh call nor direct cache writes can perform physical reparenting.

Three independent evidence types select a narrower recovery. First, both stock
script commands call `WeaponAttachment_AttachToOwnerPoint 0x141ED0370` only
after selecting an owner attach id. Its body replaces the weapon owner handle,
resolves that id in owner `+0x1E0`, writes the record index to weapon `+0x688`,
updates flags and runs attachment-refresh virtuals. Second, fb/fd runtime proved
the same API succeeds for both looks at `rightHand`, while fd now supplies the
missing target records at exact indices 39/40. Third, after fd cycling the stock
state machine issued four Nadine final calls, all still to `rightHand`, proving
that it does not automatically replay the previously skipped holster command.

Competing repairs are: (R1) rerun the control initializer/updater, rejected by
the separate skeleton namespace and unsafe IK allocation; (R2) call an entire
script native, rejected as a raw API because it depends on parser/selector and
interpolation state; (R3) wait for a replay, rejected by fd; (R4) call the stock
inner attachment mutation once for an inactive weapon still bound specifically
to the `rightHand 0x61A48E01` record, selected for a bounded test. R4 does not
claim to reproduce the script command's later transform/interpolation/event
sequence, so orientation and persistence remain explicit falsifiers.

Build `fe` gate/prediction: validate legacy Nadine, both fd target records and
the weapon's current `+0x688 -> record id` as `rightHand`. At stable `draw=0`,
repair both firearms; at `draw=2/4`, repair only slots other than `current`.
Never act in `draw=1`; a stuck `draw=5,current<0` is admitted only after at least
1000 ms unchanged and independent `GunHolstered` (for the remote puppet, only
its packet's holstered bit). Attempt once per `(weapon,target,records allocation)`.
If correct, the call returns 1, inactive pistol/rifle changes to index 39/40 and
stays there, the current slot remains on the hand, and the visible
double-weapon/stale-rifle
bug disappears without restarting draw state. A zero return, active-slot write,
immediate rightHand rollback, wrong orientation, jitter, transition restart,
crash or body/grenade regression falsifies R4.

### 2026-07-21 CONFIRMED/PARTIAL: fe reparents inactive firearms but exposes authored-pose gaps

The user verified both weapon cycles with build `fe`. The inactive rifle now
leaves Nadine's hand and moves behind her back; when the pistol is drawn, the
rifle is no longer rendered through it. This confirms the bounded stock
`WeaponAttachment_AttachToOwnerPoint` recovery as the missing physical-reparent
step and closes the original double-weapon/stale-rifle-in-hand defect. Both
processes remained responsive after the test.

The full placement prediction is only PARTIAL, not a finished presentation fix.
Four visible falsifiers remain: the pistol does not sit correctly in the hands
while ADS; the nominally free/left arm crosses the torso and reaches the weapon
for both pistol and rifle; the back rifle intersects Nadine's torso and lacks
its sling; and the holstered pistol goes to the copied Chloe back point rather
than Nadine's thigh holster. The last two placement errors are evidence against
a collision explanation: fd/fe deliberately copied Chloe's `targSpineProp` and
`gun_holster` records and their authored transforms into a different skeleton.
The sling may be a separate prop/visibility record and must not be assumed to be
part of collision or weapon attachment.

Next work is static again. First recover Nadine's actual hip/leg pistol target
and her own rifle-back transform/selector rather than tuning copied matrices.
In parallel, trace the active-weapon left-hand IK/profile inputs to decide whether
the bad grip and torso-crossing arm share one missing Nadine animation binding or
are independent defects. No transform offset or IK field should be patched until
the stock authored source and competing explanations are resolved.

### 2026-07-21 STATIC: active grip, arm pose, holster and sling have separate owners

The four visible defects do not belong to one collision/IK switch. The stock
active-weapon attach path is `WeaponAttachment_AttachToOwnerPoint 0x141ED0370`:
it resolves the Player `rightHand 0x61A48E01` record from `Player+0x1E0` and
stores its index at weapon `+0x688`. This owns the weapon's attachment placement,
not the left-arm pose. The independent GunMove path resolves vtable
`0x142A280A8 +0xB8 -> 0x140E909C0 -> ArmBlockProbes_Kick 0x140C2BCB0`; the exact
`aim-block.cpp:223` assertion and helper `0x140C2C2F0` show it consuming
`stand-p-aim`, `*player-aim-probes*`, `apReference-shoulder` and
`apReference-gun`, with `r_shoulder/spined` fallbacks. It does not read weapon
art, the look id or holster records. The older
`Player_KickPlayerDepenetrateWeapons 0x1410EDFB0` path remains a downstream
wrist-depenetration layer and is rejected as the primary persistent grip owner.

The per-look `PlayerArmIKAction` is also now concrete.
`Player_ChangeLookId 0x1410FE840` allocates a 336-byte action through constructor
`0x1418F1E60`,
stores it at `Player+0x4308`, and repeats this at initial top-level Player spawn.
The constructor configures paired `spined -> r_hand_prop_attachment` and
`spined -> l_hand_prop_attachment` chains under `*player-ik-arm*`;
`PlayerArmIKAction_UpdateProcedural 0x140C7E8D0` publishes their two stored
targets. This makes a shared look/retarget defect possible, but does not collapse
the two downstream owners: weapon placement still comes from `rightHand`, while
the crossing arm is produced by the GunMove/arm-pose side.

The thigh-holster alternatives were eliminated statically. Standard pistol art
`0x33E29A1B` has non-generic primary id `targSpineProp 0x00AD047A`, so
`WeaponHolster_ResolveAttachPoint 0x1414FEB30` returns it immediately and never
calls `WeaponHolster_SelectCharacterAttachPoint 0x1414FE7E0`. Only a zero or
generic art id enters the latter's priority
`gun_holster_pelvis 0xBA712574 -> gun_holster_leg 0x77BEFE9D -> gun_holster 0x555EFB8D`.
None of those three ids
exists in legacy Nadine's original 39-record Player attach table. Her existing
`targRUpperLeg 0x7141CB0E` is not a holster: `0x140E4ADB0` consumes it with both
ankles and both upper legs as a general body landmark, with no weapon edge.
Therefore no exact Nadine Player thigh transform can honestly be inferred from
that record. A non-primary pistol WeaponArtDef entry is the remaining authored
alternative and must be read before inventing a new attachment.

The missing rifle sling is independently tied to
`ProcessWeaponStrap_EventHandler 0x14150C9C0`, whose exact
`process-weapon-strap.cpp` assertion publishes `play-animation` for
`ak47-holster--strap` and `rifle-strap-unholstered`; sibling `0x141509D10`
publishes the unholstered strap state from the base animation state. Sling state
is therefore parked until the attachment/pose geometry is correct, but collision
is rejected as its source.

Competing active-pose hypotheses are now: (G1) Chloe and Nadine have different
`rightHand` record/local attachment transforms, producing the crooked weapon;
(G2) the rightHand attachment agrees but Nadine lacks/differs in
`apReference-gun`/`apReference-shoulder`, producing different ArmBlock output;
(G3) both boundaries agree and the final authored GunMove animation retarget is
wrong on legacy Nadine. Call/xref graphs, exact field dataflow, source strings
and the confirmed fb/fd/fe structure satisfy the three-kind evidence quota.

Build `fg` is a read-only discriminator. Before the fd append it records every
original Nadine attach record and reads the pistol/rifle WeaponArtDef `+0xD8`
holster-id list. On every stable pistol/rifle state it samples the active
`rightHand` record and weapon `+0x688` beside completed `ArmBlockProbes+0x58`,
`+0xD0/+0xD8` and `+0xF0..+0x137`. Prediction: differing rightHand data with
matching ArmBlock selects G1; matching rightHand with missing/different ArmBlock
inputs selects G2; agreement at both boundaries selects G3. The probe changes no
argument, return value, transform, IK field or animation state. A crash, visual
change or absent hook marker falsifies the diagnostic implementation.

### 2026-07-21 CONFIRMED: fg separates Nadine rightHand from the shared arm profile

The user exercised idle, drawn and ADS states with both pistol and rifle in both
instances. Both processes remained responsive and no new visual regression was
reported. PIDs `16640` and `19148` both logged the `2026-07-21fg` init marker and
an installed `ArmBlockProbes_Kick` hook, so the read-only discriminator itself
passed its runtime safety prediction.

The WeaponArt alternative-list hypothesis is rejected. Rifle art `0x8075397D`
and pistol art `0x33E29A1B` both resolved through the live cached WeaponArtDef
registry, but their `+0xD8` descriptors had `count=0` (`capacity=1`). The third
observed art `0x0B8BD6D6` was also empty. There is no hidden authored secondary
hip-holster id to select for the standard pistol.

The first untouched legacy-Nadine AttachSystem generation had exactly `39/45`
records. At the active boundary, Chloe and Nadine consistently used different
concrete `rightHand 0x61A48E01` records:

- Chloe: index `1`, joint `41`, flags `0x7FBA`, identity local transform;
- legacy Nadine: index `16`, joint `30`, flags `0`, identity local transform.

For a drawn weapon, weapon `+0x688` selected those respective indices; after
holstering, it selected the fd compatibility records (`39` for pistol and `40`
for rifle on Nadine). Thus G1 is selected at the attachment boundary exactly as
predicted, although the different joint numbers may be legitimate rig-local
indices and must be compared with native NPC Nadine before being treated as a
copy target.

G2's missing ArmBlock-input prediction is rejected. Matching pistol/rifle states
on both looks repeatedly produced the same inline profile families, including
`9DCD31BC,8DFC1F97,9E9B57EB,8DFC1F97,5EBFF070` for pistol samples and
`9DCD31BC,9DCD31BC,DF8131B3,9DCD31BC,5F061E58` for rifle samples. The completed
world-space output arrays differ, but they include each actor's distinct world
pose and therefore cannot by themselves distinguish a bad rig from normal
placement. For the torso-crossing arm the remaining candidate is G3: the common
profile reaches a different skeleton/retarget downstream.

The next boundary follows the user's stronger control suggestion: inspect the
real `NadineBuddy` NPC, whose native firearm presentation is known visually to
have the correct thigh holster, back rifle, sling and arm poses. First recover
its look/rig and weapon-attachment ownership statically; if the exact DC asset
records are runtime-only, extend the existing common attachment hook to log the
NPC's selected attach ids and concrete record data without writing either actor.

### 2026-07-21 STATIC: fh isolates the native NadineBuddy attachment owner

The user's native-NPC control is materially stronger than copying Chloe data.
`NadineBuddy_Create 0x1404A0F40` constructs the separate RTTI/vtable class at
`0x142949638`; vtable `+0x58` installs the inline `nadine-buddy` identity,
`+0x570` returns that name, and `+0xEB8` returns `nadine-default`. This is not a
legacy Nadine skin on a Player object.

`Npc_SetupAttachSystem 0x1404059E0` is NadineBuddy vtable `+0x448`. Its decompiled
dataflow initializes the pointer-owned `*(npc+0x1E0)` through
`AttachSystem_InitFromDcData 0x1418B61B0` with namespace `npc-common`, then adds
the current look/rig extension reached through `npc+0x170`. Player instead owns
the distinct `Player_SetupAttachSystemFromLookData 0x1410CDF90` path and selects
`player`/`player-net`. Independently, the `npc-holster-weapon` command at
`0x14055D800` reaches the nested controller from `npc+0x15C8`; its companion
`0x14055DA40` waits for that action. That proves native NPC command ownership,
and the deeper controller graph resolves the physical selection.
The earlier hardcoded `holster`/`backHolster` candidate at `0x1404990C0` is
rejected for NadineBuddy: its only owning vtable is a sibling NPC class, while
Nadine's same `+0x848` slot is the trivial `return 1` function `0x1402F8B20`.

`AiWeaponController_Construct 0x1405A9AE0` installs vtable `0x1429673C8` and is
published as element 7 of the control table at `npc+0x15C8`. Its virtual `+0xB8`
force-state path reaches `AiWeaponController_EnforceWeaponAttachment
0x1405AE580` at `+0x170`. That function reads the NPC table, selects
`rightHand 0x61A48E01` for drawn state (with an explicit leftHand special case),
and maps WeaponArtDef type through `0x1405E76E0` to
`pistolHolster 0x8A0249B5` or `longgunHolster 0x80E37582` for holstered state. It
then compares the resolved index with byte `weapon+0x688` and rebinds through
the weapon virtual `+0x7A8`. It does not use Player WeaponHolster or the common
`WeaponAttachment_AttachToOwnerPoint 0x141ED0370` boundary.

Thus the refined competitors are: (N1) holstering is only a visual NPC prop;
(N2) the NPC controller physically selects authored pistolHolster/longgunHolster
records; (N3) it reuses the Player/common final-attach path. N2 is selected and
N1/N3 rejected by the exact controller pseudocode and disjoint call graph. Exact
RTTI/vtable identity, independent setup/command/controller graphs, decompiled
field dataflow and the named attach constants satisfy the evidence quota.

Native active arms are also a separate pipeline. NPC virtual `+0xFA8` resolves
WeaponSkill, Nadine's `+0xFD8/+0xFE0` fire functions reach ProcessWeapon virtual
`+0x870`, and no edge reaches Player GunMove, ArmBlockProbes or
PlayerArmIKAction. The sling is independently a ProcessWeaponStrap child
(constructor `0x141509650`, vtable `0x142A9B0A0`) gated by WeaponArtDef `+0x38`
and stored at ProcessWeapon `+0xAE0`; it is neither collision nor an AttachSystem
record. The exact NPC arm animation/profile and live strap child remain runtime
asset questions.

Build `fh` is read-only. It accepts an object only when both vtable
`0x142949638` and inline name `nadine-buddy` agree, snapshots the full native
`*(npc+0x1E0)` table with every 48-byte record, observes stable ProcessWeapon
`+0x688`, and pass-through hooks the exact NPC controller boundary. Prediction:
gunState 1 must resolve `rightHand`; gunState 0 must resolve pistolHolster or
longgunHolster according to art, and the post index must name the corresponding
native record. Missing IDs or an unchanged/`0xFF` post index falsify the assumed
live asset mapping; another attach id falsifies the static mapping; calls at the
common attachment hook are explicitly not required. An absent table/boundary
marker, crash or visual change falsifies the probe itself; merely not exercising
a native weapon transition is inconclusive. No transform, attachment record,
controller, animation, visibility or return value is written.

### 2026-07-21 CONFIRMED: native Nadine pistol uses authored thigh record

The user exercised the Nadine-side instance while both `fh` processes remained
responsive. Both roles independently crossed the exact `NadineBuddy`
AiWeaponController boundary, so the observation is not a legacy Player/clone
transition. Native weapon art `0xC7CB8F0E` produced the predicted sequence:

- drawn `gunState=1`: index `42 -> 19`, `pistolHolster 0x8A0249B5 ->`
  `rightHand 0x61A48E01`;
- holstered `gunState=0`: index `19 -> 42`, `rightHand -> pistolHolster`;
- drawn again: index `42 -> 19`, `pistolHolster -> rightHand`.

Every pre/post capture was readable and the selected `weapon+0x688` index named
the predicted record after the original virtual call. This confirms N2 and the
pass-through probe. It also confirms that the common Player/final-attach hook is
not needed for the native controller path.

The native table was stable in content at `43/44` records even when its streamed
generation address changed. Its relevant records are:

- `rightHand`, index `19`: joint `30`, identity local transform;
- `pistolHolster`, index `42`: joint `99`, transform bits
  `3DA1CAC1,3D23D70A,3D1FBE77,3D23D70A,`
  `BF443E89,3CCBFD8C,3F23A8BA,3D61D9B6`.

The latter decodes approximately to `(0.079, 0.040, 0.039, 0.040)` and
`(-0.76658, 0.02490, 0.63929, 0.05514)`. This is the first exact authored Nadine
thigh placement and replaces the earlier Chloe-derived pelvis guess. However,
the legacy Player rig previously failed to resolve `r_leg_gun_holster`; joint
`99` must therefore be identified by name and resolved/remapped against the
legacy rig before copying the record. The native table in this scene contained
no `longgunHolster 0x80E37582`, and the observed NPC only carried art
`0xC7CB8F0E`; the user's Player-side rifle actions do not constitute a native
NPC rifle sample.

The census also exposed diagnostic log amplification: the engine streamed
multiple pointer generations with identical `43/44` content. Subsequent builds
must deduplicate table/state observations by semantic contents rather than
allocation addresses.

### 2026-07-21 STATIC: fi identifies the native thigh-record joint before remap

The native `pistolHolster` record cannot be copied with raw joint `99` merely
because the NPC and legacy Player share look id `0xD27C074F`. The already
confirmed Player control failed to resolve `r_leg_gun_holster`, while the NPC
AttachSystem is built from a different `npc-common` namespace plus its look
extension. The semantic joint name must be compared through each rig descriptor.

The relevant data path is concrete: AttachSystem `+0x10` owns the DrawControl;
DrawControl `+0x18` owns the rig descriptor; descriptor `+0x38` is the joint
count and `+0x58` is the stride-16 joint-name table used by
`AttachSystem_InitFromDcData`. The existing remap routine independently uses the
engine's joint transforms to derive `inverse(parent) * marker`, composes that
with the authored record transform, replaces the joint with the target semantic
parent, and clears runtime fields before `AttachSystem_AppendRecord`.

Competing outcomes are: (P1) native joint 99 is `r_leg_gun_holster`, absent in
legacy Player, while `r_upper_leg` resolves in both rigs, authorizing the proven
parent-relative remap; (P2) the same named holster joint exists in both rigs and
the exact record can target its resolved Player index; (P3) joint 99 names a
different marker or the semantic parent is missing, rejecting transfer. The
confirmed fh record, descriptor/InitFromDcData field path, Player-control
negative result and remap transform dataflow provide independent evidence types.

Build `fi` is read-only and semantically deduplicated. On an exact NadineBuddy
plus a retained legacy-Nadine Player it records the native joint-99 name id,
resolves `r_leg_gun_holster` and `r_upper_leg` in both descriptors, and emits the
record transform. Prediction: name equality plus `namedJoint=99/-1` and a valid
`upperLeg` pair selects P1; two valid named indices select P2; another name or a
missing upper-leg index selects P3. Any crash, missing marker after both actors
exist or visual change falsifies the probe. It performs no engine call or write.

### 2026-07-21 CONFIRMED/STATIC: fi rejects r_leg_gun_holster as NPC joint 99

Both `fi` instances remained responsive and emitted the same cross-rig result.
The native `pistolHolster` record still resolved to joint `99`, but descriptor
slot 99 named hash `0xADD0C871`, not the predicted
`r_leg_gun_holster 0xE231DB44`. Neither NPC nor legacy Player descriptor resolved
the latter (`namedJoint=-1/-1`). `r_upper_leg` independently resolved to joint
`5/5`, proving the descriptors were readable and the comparison did not fail
globally. P1 and P2 as written are rejected; P3 is selected.

`StringIdHash 0x14014D830` is the non-reflected table-driven CRC used for these
names. Reproducing its exact update rule yields `0xADD0C871` for
`r_leg_holster`, making that the strong STATIC preimage for NPC joint 99. The
name is distinct from PlayerWeaponAnimControl's hardcoded
`r_leg_gun_holster`; that naming mismatch explains why the native NPC can have a
valid authored thigh marker while the Player controller reports its different
marker absent.

Transfer is still not authorized until the legacy Player descriptor is queried
for the actual `0xADD0C871` name. If it also resolves to joint 99, the native
record can be copied with runtime fields cleared. If absent, the shared
`r_upper_leg` joint 5 is the only currently proven semantic remap parent; the
engine transform composition must be used rather than raw joint 99.

### 2026-07-21 STATIC: fj resolves the actual r_leg_holster hash in both rigs

Build `fj` extends the proven `fi` capture by resolving the descriptor-derived
`native_joint_name_id` itself in both the NPC and legacy Player descriptors. It
does not trust the proposed preimage and does not substitute the Player
controller's different hardcoded name.

The remaining alternatives are now exact: (Q1) actual hash `0xADD0C871`
resolves `99/99`, authorizing the native local transform on the Player's own
matching joint; (Q2) it resolves `99/-1` while `r_upper_leg` remains `5/5`,
authorizing only the existing parent-relative engine remap; (Q3) the native hash
does not resolve back to 99 or the parent pair changes, invalidating the capture
or preimage. Prediction: `actualNamed=99/99` selects Q1,
`actualNamed=99/-1 upperLeg=5/5` selects Q2, and any other tuple selects Q3.
The probe adds no call or write and keeps the semantic log bound from `fi`.

### 2026-07-21 CONFIRMED: native and Player Nadine share r_leg_holster joint 99

Both `fj` processes remained responsive and produced the identical tuple:
`recordJoint=99 actualNamed=99/99 namedJoint=-1/-1 upperLeg=5/5`. Thus the
descriptor-derived hash `0xADD0C871` resolves to joint 99 in both native NPC and
legacy Player rigs, while PlayerWeaponAnimControl's different
`r_leg_gun_holster 0xE231DB44` name is absent from both. Q1 is confirmed; Q2 and
Q3 are rejected.

The exact NPC `pistolHolster` record is therefore portable without inventing a
joint or parent transform: append its 48-byte authored record to the legacy
Player table with the same semantic joint 99, clear only `+0x28/+0x2C` runtime
fields and nonportable flag bits as the established AppendRecord path already
does, then select that new id only for the inactive pistol. Target capacity is
sufficient: original legacy Nadine is `39/45`, the two existing compatibility
records make `41/45`, and pistolHolster makes `42/45`.

The falsifiable fix prediction is narrow. After the append, an inactive standard
pistol must resolve `weapon+0x688` to a target record whose id is
`pistolHolster`, joint is 99 and transform matches the confirmed NPC bits; it
must appear on Nadine's thigh instead of her back. Drawn pistol must still return
to the unchanged original rightHand joint 30. Rifle attachment, arm pose,
ProcessWeaponStrap and all Chloe records must remain unchanged. A crash, missing
body part, jitter, back placement, wrong hand placement or any rifle/arm
regression falsifies the fix.

Build `fk` implements exactly that boundary. It waits for a live exact
NadineBuddy and legacy-Nadine Player, verifies `r_leg_holster` in both tables,
uses the established engine transform/AppendRecord path to add only
`pistolHolster`, and clears the copied runtime fields. The inactive-weapon
reconciler now selects this id only for standard pistol art; its rifle branch
remains byte-for-byte unchanged. The existing repair log records count,
presence, joint and final transform before any user test.

### 2026-07-21 REJECTED: native pistolHolster record alone does not move the Player visual

The user holstered the standard pistol in build `fk`; both game processes stayed
responsive and no additional regression was reported, but legacy Player Nadine
still placed it at the same visual location as Chloe. This falsifies the narrow
fix prediction even though both intended state changes completed successfully.

The transfer itself is confirmed. Both roles appended the NPC-authored
`pistolHolster` record to the legacy table (`41 -> 42/45`), resolved
`r_leg_holster` as joint `99 -> 99`, and retained the native transform bits
within normal floating-point roundoff. The inactive attachment call then moved
the pistol from `targSpineProp` index 39 to `pistolHolster` index 41, returned
success and verified the resulting id. A later stable sample still read
`weapon+0x688 = 41`; the engine did not roll the state back to the Chloe record.

Therefore the authored AttachSystem record and ProcessWeapon attachment byte are
not the last visual-placement owner on the Player path. The remaining competing
hypotheses are: (H1) Player holster IK/animation publishes a later transform and
uses the missing Player-specific holster joints or identity cached relative
transforms; (H2) ProcessWeapon or its mesh applies an owner-class-specific local
offset downstream of `+0x688`; (H3) the visible holstered pistol is a separate
prop/drawable while `+0x688` belongs to a hidden or non-rendered ProcessWeapon.
Do not tune the native record coordinates or repeat the same transfer. Trace
`PlayerWeaponAnimControl_UpdateHolsterIk 0x14108ECE0`, resolve the concrete
WeaponHolsterIkBlender/output consumer, and locate the final weapon/mesh world
transform write before designing another bounded discriminator.

### 2026-07-21 STATIC: final attached-weapon transform path excludes Player holster IK

`PlayerWeaponAnimControl_UpdateHolsterIk 0x14108ECE0` is not a late weapon-
placement writer. RTTI resolves its local vtable `0x142A46748` to
`WeaponHolsterIkBlender`; slots `0x14108B120/0x14108B580` extract and blend a
0x38-byte `WeaponHolsterIkData` record using `apReference-hand-r`,
`apReference_leg_holster` or `apReference_back_holster`. The result is composed
with PlayerWeaponAnimControl's cached parent-relative marker and passed to its
`ArmIkChain` at `+0x70`. The final writer in that branch is the skeleton joint
cache blend `0x14181EF50`, which writes 0x30-byte world-pose entries for arm
joints. The entire branch has no `ProcessWeapon`, parent handle `+0x4E0`, selected
record `+0x688`, AttachSystem or drawable-transform dataflow. H1, in which this
IK function overwrites the successful pistol attach, is therefore REJECTED.
The missing Nadine marker children can still explain bad arm pose, but not the
unchanged holstered weapon location.

The actual shared `NdAttachableObject` path is now bounded. The selected index
is consumed by `AttachSystem_ResolveRecordTransform 0x1418B7060`: it validates
the 48-byte record, obtains the current world transform of signed joint
`record+0x24` through `0x141687C90`, then composes that live joint transform with
the record's authored 32-byte local transform. The selected-attachment resolver
`0x141ED1780` follows parent `+0x4E0`, consumes object `+0x688` and feeds the
generic local/link state used by the attached object.

The last concrete ProcessWeapon placement boundary is even later:
`ProcessWeapon vtable+0x410 -> 0x141515860`, whose direct base call at
`0x141515A30` enters `NdAttachableObject_PostJointUpdate_Async 0x141ED28E0`.
That path reaches common vtable `+0x2B8 ->
Character_UpdateDerivedWorldTransformFromLocalRoot 0x141BB8320`, which composes
the attachment-link parent transform at `+0x110/+0x120` with object-local
translation/quaternion `+0x130/+0x140` and writes the render/world outputs
`+0x240/+0x250/+0x280`. The remaining tail only synchronizes child DrawControl
visibility/camera flags with its parent at `0x141ED63F7`; it does not replace the
world transform. The RTTI-backed ProcessWeapon vtable inherits this shared path,
and neither the attachment mutation nor final composition branches on Player
versus NadineBuddy class. No separate holstered-pistol drawable was found.

Three independent evidence kinds meet the static quota: concrete RTTI/vtable
identity separates WeaponHolsterIkBlender/ArmIkChain from ProcessWeapon;
pseudocode dataflow identifies the exact record, joint and final object-local
stages; and build `fk` independently proves that `+0x688` and the copied record
remain correct while the visual remains wrong. A second native-NPC controller
path also reaches the same ProcessWeapon selected-record contract.

The remaining competing hypotheses are now narrow. S1: joint 99 shares a
name/index but has a wrong live Player pose, so the attachment link faithfully
publishes a wrong point. S2, currently strongest: `+0x688` and record `+0x28`
are correct, but the downstream link/local fields `+0x108/+0x10C` or
`+0x130/+0x140` remain stale from `targSpineProp`, so the final writer reproduces
the Chloe placement. S3: `+0x240` moves the known ProcessWeapon correctly but
another drawable remains visible; record `+0x28` ownership and the earlier
render-child census weigh against this, but matching final data would select it.

The next build keeps the already-verified `fk` record transfer solely so the
legacy pistol selects `pistolHolster`, and adds no new gameplay-state write. A
pass-through hook at the exact ProcessWeapon base-update boundary snapshots both
the native NPC pistol and legacy Player pistol before/after `0x141ED28E0`:
parent `+0x4E0/+0x4E8`, `+0x688`, selected record including owner link `+0x28`,
attachment-link fields `+0x108/+0x10C`, transforms at
`+0x100/+0x110/+0x120/+0x130/+0x140/+0x240/+0x250/+0x280`, and DrawControl
`+0x198`. Prediction: correct record/index with an unchanged old link/local/world
selects S2; a changing and internally consistent world output that is already
wrong relative to the Player selects S1; a correct thigh world output on the
known pistol ProcessWeapon while the visual remains on the back selects S3. A
crash, changed weapon state or missing markers after both exact owners and their
holstered pistols exist falsifies the probe itself.

Build `fl` implements that discriminator at the narrower direct callsite
`0x141515A30` rather than detouring the shared base function globally. The relay
preserves the stock `NdAttachableObject_PostJointUpdate_Async` call and accepts
only RTTI-exact ProcessWeapon objects. It further gates native samples to exact
NadineBuddy art `0xC7CB8F0E`, and Player samples to standard pistol art
`0x33E29A1B` whose retained owner has look `0xD27C074F`. For each semantic
`{role, owner, weapon, art, attach index/id}` state it records at most three
samples, 750 ms apart. Each record contains the selected 48-byte AttachSystem
record, pre/post hashes/change masks, `+0x100..+0x14F`,
`+0x240..+0x29F`, owner local/world fields and DrawControl pointer. This keeps
the hot async boundary bounded while retaining enough raw bits to normalize the
two actors offline. The release DLL built successfully; gameplay verification
is still pending.

### 2026-07-21 CONFIRMED: Player joint-99 pose, not ProcessWeapon state, causes the wrong holster

The user completed the `fl` draw/holster sequence. Both processes stayed
responsive, the direct-call relay installed, and the game independently exposed
a native NadineBuddy holstered-pistol sample in both roles. This gives the exact
native-versus-Player comparison that the probe required rather than relying on
raw record identity alone.

Native and Player samples selected the same semantic input:

- `pistolHolster 0x8A0249B5`;
- joint `99`, named `r_leg_holster 0xADD0C871` in both descriptors;
- the same authored eight-DWORD record transform (only normal floating-point
  roundoff differed);
- an RTTI-exact ProcessWeapon whose child DrawControl mesh id was the pistol art
  itself (`0x33E29A1B` on Player, the native pistol mesh on NadineBuddy).

The divergence was already present in the resolved `+0x130/+0x140` transform
before `NdAttachableObject_PostJointUpdate_Async`. The base call changed no
link/local DWORD (`changed link=0`) and merely regenerated the world/output
blocks, so the final writer and a stale post-attach cache are rejected. The
visibility census independently correlates the logged weapon/draw pointer with
the rendered ProcessWeapon child, rejecting a separate visible pistol.

After removing actor world translation and yaw, the native holstered pistol was
approximately at owner-local `(-0.222, 0.835, -0.003)`, while stable Player
samples were around `(-0.154, 0.808, -0.175)` to
`(-0.088, 0.810, -0.229)`. Their relative quaternions also differed materially.
Thus equal joint name/index does not mean equal live pose: NadineBuddy's NPC
animation graph and legacy Player's animation graph publish different joint-99
transforms. S1 is CONFIRMED; S2 (stale ProcessWeapon link/local state) and S3
(separate visible prop) are REJECTED.

Do not modify the final writer, DrawControl visibility or record coordinates.
The next fix hypothesis is a semantic-parent remap: resolve `r_upper_leg` as
joint 5 in both rigs, compute the native authored holster relative to native
joint 5 as
`inverse(native upper-leg Ws) * (native r_leg_holster Ws * authored record)`,
then append that transform against Player joint 5. This uses the existing
engine-backed `raw_append_parent_remapped_attach_record` recipe and makes the
Player follow its own upper leg rather than its incompatible joint-99 pose.
Competing alternatives are pelvis remap (wrong semantic limb) and a hard world
offset (animation-dependent and already contradicted by the moving samples).
Prediction: the new record must name joint 5 and differ from the raw NPC record;
the inactive standard pistol must move from the Chloe/back position to Nadine's
thigh throughout idle motion, while drawn rightHand, rifle, arms and body remain
unchanged. A joint other than 5, an unchanged transform, jitter, body/arm damage,
or any non-pistol change falsifies the remap.

#### 2026-07-21 STATIC: semantic upper-leg remap implemented for build `fm`

`raw_append_native_nadine_pistol_holster` now resolves both the authored marker
`r_leg_holster` and the shared parent `r_upper_leg`. The existing engine-backed
helper evaluates `inverse(native joint5 Ws) * native joint99 Ws`, composes that
with the authored pistol record, and appends the result against Player joint 5.
It rejects the repair unless the output record names joint 5 and its transform
differs from the raw NPC transform. The independent evidence is: the runtime
comparison above isolates joint-99 pose as the divergence; both descriptors
resolve `r_upper_leg` to joint 5; and the exact transform/record path is the
already-used `RigGetJointTransform` / `Transform_MakeRelative` /
`Transform_Compose` / `AttachSystem_AppendRecord` sequence consumed by
`AttachSystem_ResolveRecordTransform`. The remaining risk is pose stability:
the helper samples a live native pose rather than bind pose, so any visible
jitter or sliding is explicitly a falsifier. Release build `2026-07-21fm`
compiled successfully; runtime verification is pending.

### 2026-07-21 CONFIRMED/REJECTED: build `fm` remaps the record, but non-default pistols bypass the reconciler

The user tested multiple pistols in build `fm`; all still holstered at Chloe's
back location. Both processes remained responsive and the semantic record remap
itself satisfied every written structural prediction: `marker=99:99->99`,
`parent=5->5`, `recordJoint=5`, `changed=1`. The target table retained
`pistolHolster 0x8A0249B5` at joint 5 with the remapped transform. Therefore a
bad append, raw joint 99 and a failed parent conversion are rejected for this
run, while the visual thigh-placement prediction is REJECTED.

The same run identifies a distinct coverage failure. The fired slot-1 handgun
was art `0x9A07E17F`, whereas the inactive-weapon reconciler classifies a pistol
only by exact equality with the original co-op art `0x33E29A1B`. No
`NADINE-INACTIVE-ATTACH` attempt occurred for `0x9A07E17F`, so that handgun never
entered the remapped `pistolHolster` path. The other retained arts were slot-0
`0xB6B0DD17` and grenade `0x0B8BD6D6`; the latter independently selected its
expected `0x754D9C3B` control and must not be redirected. The next step is to
replace the single-art check with the engine's WeaponArtDef handgun class at the
Player holster-binding boundary, after statically proving its type values and
the exact output consumer.

### 2026-07-21 STATIC: build `fn` reroutes every WeaponArtDef handgun binding

The exact Player boundary is `WeaponHolster_BuildBinding 0x1414FE540`. It reads
the weapon art at `ProcessWeapon+0x860`, resolves the cached WeaponArtDef, and
first asks `WeaponHolster_ResolveAttachPoint 0x1414FEB30` for an attachment id.
That resolver prefers a non-generic art-specific holster id; otherwise
`WeaponHolster_SelectCharacterAttachPoint 0x1414FE7E0` scans the target
character for `gun_holster_pelvis`, `gun_holster_leg`, then `gun_holster`.
`pistolHolster` is an NPC namespace and is never selected by this Player
fallback. This explains why adding the record alone cannot cover arbitrary
Player pistol arts.

The safe class discriminator is WeaponArtDef `+0x10`. The independent native
mapper `WeaponArtDef_SelectNpcHolsterAttachId 0x1405E76E0` maps types 0 and 3 to
`pistolHolster`, types 1 and 2 to `longgunHolster`, and all other types to zero.
The Player action independently special-cases type 10, but that alone does not
prove the live grenade's exact type. Build `fn` therefore also requires exact
slot-1 pointer membership: the earlier `ex` census confirmed rifle in slot 0,
pistol in slot 1 and grenade in slot 2 on both owners. Finally, callers such as
`Script_AttachPlayerWeaponBackToHolster 0x140342EE0` pass the returned binding
id directly to `WeaponAttachment_AttachToOwnerPoint`, establishing the output
as the physical selection boundary rather than a diagnostic hint. These three
independent dataflow/type/caller facts satisfy the pre-test evidence quota.

Build `fn` calls the original builder first, then changes only output DWORD 0
to the already-validated remapped `pistolHolster` id when all gates hold:
retained legacy-Nadine Player, exact slot-1 weapon, WeaponArtDef type 0 or 3,
and a present target record with a valid byte index. The per-art transforms and
scalar in the rest of the binding stay untouched. Long guns (types 1/2), the
slot-2 grenade, Chloe and
drawn-hand attachment code are not modified. Competing explanations are a late
Player fallback (rejected because the hook writes after the original builder),
a separate visible prop (rejected for the firearm ProcessWeapon by the earlier
render census), and an unclassified weapon type (left as an explicit no-write
case).

Prediction: on the tested `0x9A07E17F` pistol, the log must show
`type=0|3 slot1=1`, its original `source` id, final `attach=0x8A0249B5`,
`pistolPresent=1` and `redirect=1/1`; the following physical attach must select
that same id/joint 5. Every holstered handgun must move to the thigh and remain
stable through movement and another draw/holster cycle. Rifle and grenade must
retain their original ids, and hands/body must remain unchanged. A non-handgun
type for the tested pistol, `redirect=0`, a later different physical id, back
placement, jitter/sliding or any rifle/grenade/body regression falsifies the
corresponding part of this hypothesis.

### 2026-07-21 REJECTED: build `fn` is not the live remote-Nadine pistol boundary

The user retested several pistols and every one still used Chloe's back holster;
there were no crashes or other regressions. The written type prediction failed
before any write: live `WeaponArtDef+0x10` values were rifle `type=0`, standard
pistol `type=1`, and grenade `type=4`. Therefore the direct interpretation of
the argument consumed by `WeaponArtDef_SelectNpcHolsterAttachId 0x1405E76E0` as
this cached WeaponArtDef/type field is REJECTED. The strict `type=0|3` gate did
not redirect anything (`redirect=0/0`), so the failed build remained a safe
no-write test.

The owner/call coverage gives a stronger rejection. During the actual firearm
draw/holster test, `NADINE-HOLSTER-BINDING` recorded the standard pistol and
rifle only for the local Chloe look `0x3F334972`. The retained legacy-Nadine
Player look `0xD27C074F` reached the hook only for the grenade; it produced no
pistol binding row at all. Thus `WeaponHolster_BuildBinding 0x1414FE540` is a
real Player attachment consumer, but it is not the live boundary used by the
remote puppet's synchronized pistol transition in this scenario.

Do not revise the enum and retry this hook. The next analysis must follow the
already-observed remote path from `apply_peer_held_weapon_state` through
`safe_weapon_controller_draw(controller, slot, false)` and the inactive-weapon
reconciler. The exact inventory slot remains the strongest runtime classifier:
the peer census independently established rifle slot 0, pistol slot 1 and
grenade slot 2 even for picked-up art `0x9A07E17F`. Any replacement write must
be gated by the exact retained-player slot-1 pointer, skip the active slot, and
verify `weapon+0x688` after the physical attach call.

### 2026-07-21 STATIC: build `fo` repairs exact slot 1 after each inactive edge

The remote transition is now traced past the rejected binding hook.
`PlayerWeaponController_SwitchToWeapon 0x1410A1C10` delegates on a puppet to
vtable `+0x48`, `PlayerWeaponController_ApplyWeaponSwitch 0x1410A28A0`.
For `slot=-1,inHands=false`, that state machine starts
`PlayerWeaponController_StartHolsterTransition 0x1410A3EA0` and a
`PlayerPutGunAwayAction`; none of these functions writes `weapon+0x688`
synchronously. This explains why success from SwitchToWeapon does not identify
the final attachment point and why `WeaponHolster_BuildBinding 0x1414FE540`
could be absent for the retained puppet pistol.

The existing inactive reconciler is still the narrowest physical recovery, but
it had two independent coverage holes. It recognized only art `0x33E29A1B`, and
its claim key `(weapon,target,records allocation)` permanently suppressed a
second repair after the first successful draw/holster cycle. The selected stock
helper `WeaponAttachment_AttachToOwnerPoint 0x141ED0370` is not a raw field
write: it resolves the requested id in owner `+0x1E0`, updates the owner handle,
writes the selected record index to `weapon+0x688`, and refreshes attachment
state. Independent Player paths `0x1410A9DD0` and `0x1410A2150` call the same
helper for hand attachment. Earlier build `fe` then proved at runtime that the
helper returns 1 and changes both parent-owned firearms to the requested record,
while build `fm` proved that the remapped `pistolHolster` record exists at joint
5. These call-graph, field-dataflow, independent-consumer and runtime facts
satisfy the evidence quota.

Competing repairs are rejected as follows. Retrying the post-builder rewrite is
excluded by `fn` call coverage. Treating vtable `+0x48` as the final writer is
excluded by its async-action dataflow. Writing `+0x688` directly would omit the
owner/orientation refresh. Invoking the controller's broader holster vslot
`+0x3D0` would re-enter the art-specific selector that chose Chloe's point and
would also emit unrelated action/event state. The remaining minimal operation
is the already-proven direct stock attach helper after a stable inactive edge.

Build `fo` therefore classifies a handgun by identity, not art: the weapon
returned by inventory vtable `+0xB0` for exact slot 1. It acts only on a stable
legacy-Nadine Player, skips the current/active slot, requires the weapon parent
handle to resolve back to that Player, requires the remapped target record, and
accepts any current attachment id other than `pistolHolster`. A remote puppet
at transient `draw=0` is additionally blocked while its peer still requests a
draw. The one-shot claim now includes a slot-1 inactive-generation counter:
slot 1 becoming active arms it, and a later stable holstered or other-slot-active
state advances it exactly once. Thus a later engine overwrite can be corrected,
while a stable successful attachment causes no repeated call. The rejected
WeaponArtDef type writer was removed; its old hook is read-only again.

Prediction: a picked slot-1 pistol such as `0x9A07E17F` must produce one
`NADINE-INACTIVE-ATTACH` after at least 250 ms stable inactivity with its old id
changing to `0x8A0249B5`, `parentOwner=1->1`, and `verified=1`; the selected record must
remain the already-logged joint-5 remap. No slot-1 repair may occur while slot 1
is active. After another draw then holster (or switch to slot 0), the generation
must increase and exactly one new repair may occur only if the engine restored a
different id. Slot 0 and grenade slot 2 must not enter this generic rule; hands,
body and controller state must remain intact. Missing generation/verification,
an active-slot write, repeated per-frame calls, a non-slot-1 reroute, crash,
jitter or verified joint-5 selection that still renders on the back falsifies
the corresponding part of the hypothesis.

The release DLL compiled successfully with marker `2026-07-21fo`; runtime
verification is pending.

### 2026-07-21 CONFIRMED/REJECTED: `fo` re-arms correctly, but the broken action restores Chloe's point

The user completed repeated draw/holster cycles on both copies. There were no
crashes, but the pistol still appeared at the same Chloe/back location. The
user also clarified that the draw and put-away animations have been broken from
the beginning of this weapon-sync work; this is pre-existing behavior rather
than a regression introduced by `fo`.

The mechanical `fo` prediction is CONFIRMED. Both retained legacy-Nadine
Players produced verified slot-1 repairs. Initial generation 1 and later
generations 2 and 3 each changed the standard pistol from either `rightHand` or
the art-specific Chloe point `0x00AD047A` to `pistolHolster 0x8A0249B5`.
Every call returned 1, resolved the weapon parent back to the same Player
(`parentOwner=1->1`), and finished on the expected record with `verified=1`.
While the rifle was active, the pistol repair correctly ran as inactive
`slot=1 active=0 draw=2`; the generic rule did not capture grenade slot 2.
Thus exact-slot classification, stable-edge admission and per-cycle re-arming
are all confirmed.

The visual/persistence prediction is REJECTED. The same pistol objects entered
later generations with `pre_attach_id=0x00AD047A` again, proving that the engine
restored Chloe's art-specific attachment after each successful direct repair.
This is not a missing retry: `fo` did retry and verified the target on both
copies, yet the user saw no visual improvement. More importantly, the installed
`PlayerWeaponAnimControl` named-event hook recorded zero
`NADINE-HOLSTER-EVENT` rows during the whole test, while the final attach hook
recorded hand attaches. This independently matches the reported missing/broken
draw and put-away animations.

Stop changing record transforms, attachment ids and retry timing. The next P0
is the absent weapon-action animation/event pipeline. Compare a working local
Chloe draw/put-away cycle against the retained legacy-Nadine puppet from
`PlayerWeaponController_ApplyWeaponSwitch 0x1410A28A0` through
`PlayerPutGunAwayAction` / `PlayerGetWeaponInHandAction`, action admission at
`sub_142053820`, the animation layer, and
`PlayerWeaponAnimControl` named-event dispatch `0x14108DE30`. Determine why the
puppet reaches hand attach/state transitions without emitting the reparent
marker/event. The direct inactive repair should be disabled or kept diagnostic
only before that discriminator test so it cannot mask event ordering.

Both test processes were closed after log capture. IDA MCP remains stopped, the
release DLL on disk is build `fo`, and the next session should restart MCP for
static analysis before any new game launch.

### 2026-07-22 STATIC: weapon action loss is now bounded at PlayerMoveSequencer (`fp`)

The previous checkpoint called `sub_142053820` an action-admission boundary.
That interpretation is corrected. `PlayerMoveSequencer_EnqueueControllerAction
0x142053820` has no reject predicate or virtual call: it copies the pending tag
from controller `+0x100` to action `+0x1C` when required, stores the controller
at action `+0x28`, links the action into the selected intrusive-list channel and
clears the pending tag. Both `PlayerWeaponController_ApplyWeaponSwitch
0x1410A28A0` and `PlayerWeaponController_StartHolsterTransition 0x1410A3EA0`
use channel 1 for their weapon actions. Queue insertion therefore cannot itself
explain the missing animation.

The real execution boundary is `PlayerMoveSequencer_UpdatePhase 0x142051EC0`.
The sequencer owns the Player at `+0x18`, channel count at `+0x110` and channel
lists at `+0x120`. When an action enters with lifecycle state `action+0x18 == 0`,
the update changes it to 1 and calls vtable `+0x10` OnStart. Phase 0/1/2/3 then
dispatches vtable `+0x28/+0x30/+0x38/+0x40`; state 2 or 3 is removed through the
common cleanup path. Independent callers place phase 2 in
`Player_PostAnimUpdate 0x1410E9210`, phase 3 in
`Player_PostAnimBlending_Async 0x1410EB6A0`, and phase 0 in `0x140CA4B20`.

The two exact RTTI actions now resolve end to end. `PlayerGetWeaponInHandAction`
vtable `0x142A485E8` uses `PlayerGetWeaponInHandAction_OnStart 0x141096150` and
phase-2 monitor `0x141096730`. `PlayerPutGunAwayAction` vtable `0x142A48708`
uses `PlayerPutGunAwayAction_OnStart 0x141096BB0` and phase-0 monitor
`0x141096F00`. Both OnStart methods first call
`PlayerWeaponHolsterAction_ResolveAndBindAttachPoint 0x141095BD0`, which reaches
`WeaponHolster_BuildBinding`, configures PlayerWeaponAnimControl and stores the
weapon handle. Only afterwards do they request `s_weapon-holster`/grab/draw and
mirror/no-weapon variants through `PlayerWeaponAnimControl` vtable `+0x10`, now
named `PlayerWeaponAnimControl_RequestWeaponState 0x14108C2D0`. That request
targets exact animation layer `0xE2B990B9` through a selector fast path or a
direct-fade fallback. Vtable `+0x98`,
`PlayerWeaponAnimControl_GetCurrentWeaponState 0x14108D080`, supplies the state
ids that the two action monitors compare before setting lifecycle state 3.

The zero-row named-event result from `fo` is not a callback count. The hook
address, ABI and native event layout are statically correct, but the logger
discarded an entry when the weapon handle was invalid/cleared, the event lacked
a `name`, the owner was outside the retained pair, or the weapon art was not in
the default pistol/rifle/grenade whitelist. Picked pistol `0x9A07E17F` was
explicitly outside that whitelist. Thus zero `NADINE-HOLSTER-EVENT` rows alone
does not prove that `PlayerWeaponAnimControl_HandleNamedWeaponEvent 0x14108DE30`
was never entered.

Competing explanations are now separated as follows. A rejected enqueue is
REJECTED by the unconditional list-insertion dataflow. Missing Nadine holster
joints/records are REJECTED as the primary action-loss cause because both
OnStart methods reach the binding resolver before the animation layer consumes
those downstream data. The open candidates are: the retained action is removed
or replaced before its first sequencer tick; the retained Player sequencer does
not receive the required phase; OnStart runs but the animation-layer selector
or authored state fails; or the named-event logger merely hid a real callback.
RTTI/vtable identity, controller-to-sequencer call graph, direct lifecycle field
writes and the independent animation-layer/state consumers provide four
independent evidence types before runtime.

Build `fp` adds one pass-through detour at `0x142051EC0`. It snapshots channel 1
before and after the original only for retained Players, searches a bounded 16
nodes for the exact Get/Put vtables, and records lifecycle/tag/flags, controller
slot/draw state, weapon handle/art and PlayerWeaponAnimControl requested state.
Rows are deduplicated by the complete lifecycle transition, while one no-action
row per owner/phase proves that the sequencer itself ran. The named-event hook
now emits a retained-owner reached row before its old weapon/event/art filters
can hide the entry. The direct inactive slot-1 attach repair is parked in this
diagnostic build so it cannot mask native ordering. No action, sequencer or
animation method is invoked manually and no gameplay state is written by the
new probe.

Falsifiable prediction for one Chloe-versus-legacy-Nadine draw and put-away
cycle: working Chloe must show exact Get/Put actions changing `state=0->1` in
the appropriate sequencer phase, a subsequent binding/animation request, and
event or completion. If legacy Nadine has no exact action while no-action phase
rows continue, the action was cleared before the tick. If it shows `0->1`,
OnStart ran; missing binding then falsifies the old art-filtered binding logger,
while a nonzero animation request with no reached named-event row selects the
animation-layer/authored-event boundary. If no lifecycle row at all exists for
the retained Player, the scheduling-gap candidate wins. Any state mutation
outside these native before/after transitions, changed weapon behavior, crash,
or missing Chloe control sequence falsifies probe safety or coverage.

The release DLL compiled successfully with marker `2026-07-22fp`. All eight
new STATIC function names were applied to the single `ida/tll.i64`,
`save_database returned True`, and MCP lookup verified the saved names. Runtime
verification is pending.

### 2026-07-22 CONFIRMED/REJECTED: `fp` proves a shared downstream holster gap

The user completed pistol and rifle draw/put-away cycles on both copies. Both
processes remained alive and the probe installed with `actionLifecycle=1`.
Processes 18076 and 22204 were closed after log capture.

The action-loss prediction is REJECTED. Exact Get and Put actions were observed
for both local and retained Players, both Chloe look `0x3F334972` and legacy
Nadine look `0xD27C074F`, and both standard pistol art `0x33E29A1B` and rifle
art `0x8075397D`. Every covered action reached native OnStart through a
sequencer `state=0->1` transition with a valid weapon handle. Get requested
`s_weapon-grab = 0x28B0F93B`; Put requested
`s_weapon-holster = 0xDF5B68FC`. Get later changed `state=1->3` in phase 2 and
Put changed `state=1->3` in phase 0. `WeaponHolster_BuildBinding` rows were also
present for both looks, independently confirming that both OnStart methods
passed through `PlayerWeaponHolsterAction_ResolveAndBindAttachPoint`.

The broadened callback prediction also produced a decisive negative result:
there were zero `NADINE-HOLSTER-EVENT` rows after both `fp` init markers. This
time owner, weapon validity, event-name validity and art whitelist could not
suppress a reached row, so `PlayerWeaponAnimControl_HandleNamedWeaponEvent
0x14108DE30` was genuinely not entered for any retained Player in the tested
cycles. The earlier claim that a missing callback here specifically explained
Nadine's broken holster path is therefore REJECTED; the same absence occurs for
Chloe and Nadine and cannot distinguish the legacy look.

With the direct inactive repair parked, the run emitted twelve
`NADINE-HOLSTER-ATTACH` rows and every one selected
`rightHand = 0x61A48E01` while drawing. This originally looked like proof that
Put never performed a holster mutation. The later complete call graph corrects
that interpretation: `WeaponAttachment_AttachToOwnerPoint 0x141ED0370` is the
owner-changing path, while stock Player Put keeps the same owner and changes
only the existing-owner attach index through weapon vtable `+0x7A8`. The `fp`
rows therefore prove the hand path but are silent about the real Put boundary.

The written expectation that a working Chloe cycle would exercise the named
event was falsified. Do not force `0x14108DE30`, delay action completion, or
resume transform/id tuning from this result. The next static task is to audit
the other PlayerWeaponAnimControl event/state callbacks around vtable
`+0xD0..+0xE8`, then follow the actual callers of
`Script_ReattachPlayerWeapon 0x1403421B0` and
`Script_AttachPlayerWeaponBackToHolster 0x140342EE0` to the concrete state-script
or action-completion boundary. If the current-state monitor remains ambiguous,
the next runtime discriminator may read the exact value returned by
`PlayerWeaponAnimControl_GetCurrentWeaponState 0x14108D080`; it must not invoke
or overwrite the animation state.

### 2026-07-22 STATIC: `weapon-holster` AnimAction owns the stock Put rebind (`fq`)

The remaining `PlayerWeaponAnimControl` callback family is not the physical
attachment owner. Vtable `+0xC0 0x14108E470` only queries the authored `auto`
flag; `+0xC8 0x14108D3B0` tests/requests a state in layer `0xE2B990B9`;
`+0xD0 0x14108B720` relocates owned pointers; `+0xD8 0x14108D450` refreshes
`overlays-changed`; and `+0xE0 0x14108DAC0` publishes `play-animation` to a
weapon/prop through helper `0x14108D780`. None reaches the Player action factory,
the holster action, weapon `+0x688` or either attachment API. Vtable `+0xE8`
remains only the already-rejected named-property callback.

Action removal is independently excluded. Source-identified
`PlayerMoveSequencer_RemovePlayerMoveAction 0x142051D00` changes lifecycle
state 1 to 2, calls action vtable `+0x18`, then `+0x00`, unlinks the action and
calls deleting destructor `+0x08`. Both exact Get/Put vtables use
`nullsub_1352 0x140C3A100` at `+0x18`, `nullsub_166 0x140C3A0B0` at `+0x00`,
and destructors that only restore `PlayerMoveAction::vftable` and free 80 bytes.
There is no completion-time weapon mutation hidden after the phase monitor.

The missing boundary instead belongs to Player's separate `AnimActionMgr` at
`Player+0x1B8`. Player Init calls `0x142057380` to create a 16-entry manager and
installs factory `0x1410FD120`. `AnimActionMgr_AnimStateCreate 0x142057BA0`
passes the authored action StringId, Player, manager id, animation layer and
AnimInstance id to that factory. Case `weapon-holster = 0xFA06C342` allocates
the exact RTTI class `PlayerHolsterWeaponAnimAction`, vtable `0x142A46560`;
nearby independent cases create `weapon-grab`, `weapon-draw` and
`weapon-holster-finish` actions. State transition/per-frame dispatch at
`0x142057FB0/0x142058130` reaches the holster derived wrappers and its one-shot
executor `PlayerHolsterWeaponAnimAction_Execute 0x14108A640`. State end only
marks the action finished and does not execute a missed holster marker.

The executor's dataflow is concrete. It latches `action+0x30=1`, resolves the
current animation state and ProcessWeapon, and calls
`PlayerWeaponController_RebindWeaponToHolster 0x1410A9670` through controller
vtable `+0x3D0`. It then requires the weapon's current `+0x688` record to be
`targSpineProp = 0x00AD047A`, switches through weapon vtable `+0x7A8` to
`targSpinePropAnim = 0xBBACEF13`, composes the authored animation transform,
calls the controller rebind again and seeds the weapon blend blocks.
`0x00AD047A` is therefore a stock named prerequisite, not a corrupt generic
back attachment.

The controller's ordinary path calls `WeaponHolster_BuildBinding` and passes
its attach id to weapon vtable `+0x7A8`. ProcessWeaponBase and the firearm
derived vtables share `WeaponAttachment_SelectExistingOwnerPoint 0x141ED0580`,
which searches the existing owner's AttachSystem and writes the selected byte
index to `weapon+0x688` at `0x141ED06EF`. This intentionally does not call the
owner-changing `WeaponAttachment_AttachToOwnerPoint 0x141ED0370`. Registered
script commands `reattach-player-weapon` and
`attach-player-weapon-back-to-holster` remain real alternatives, but their lack
of native callers and the working stock same-owner path reject them as the
mandatory Player Put boundary.

The competing hypotheses are now: (A) the synchronized animation state never
creates `weapon-holster` in the retained Player's AnimActionMgr; (B) the factory
creates it but the state-transition/per-frame wrappers never execute it; (C)
the executor is reached but cannot resolve the current state/weapon after
latching its one-shot byte; (D) its first controller rebind runs, but the
resulting record is not required `targSpineProp`, so the guard exits; (E) the
full multi-stage selector runs but selects/writes the wrong final record.
Factory RTTI/case identity, dispatcher/vtable identity, executor field dataflow,
and the independent same-owner setter write provide four evidence types.

Build `fq` adds three pass-through probes: factory `0x1410FD120`, executor
`0x14108A640`, and logical existing-owner setter `0x141ED0580`. Factory rows
include the exact authored action and returned vtable; executor rows include all
action identity/lifecycle fields before and after; setter rows include requested
id plus `weapon+0x688` record id/index before and after. The old final attach
logger now also records caller RVA, while the direct inactive repair remains
disabled. The probe invokes no action, controller, animation or attachment
method and performs no gameplay write.

Written prediction for one pistol and rifle draw/put-away cycle on Chloe and
legacy Nadine: all three probes must install (`animAction=111`). Chloe must show
factory `0xFA06C342 -> vtableRva 0x2A46560`, executor entry, and a selector
sequence containing the authored `targSpineProp`/`targSpinePropAnim` stages plus
the final binding. For the retained Player, no holster factory row selects A;
factory without executor selects B; executor with `executed 0->1` and no setter
selects C; one setter ending on a record other than `0x00AD047A` selects D; a
multi-stage setter sequence with the wrong final id selects E. A different
sequence is a new static lead. Any changed behavior, missing install marker,
wrong factory vtable, crash or non-stock action-state mutation invalidates the
probe.

The release DLL compiled successfully with marker `2026-07-22fq`. All newly
STATIC AnimActionMgr, Player holster action, controller rebind, same-owner
selector, callback and registration names were applied to the single
`ida/tll.i64`; `save_database returned True`. The restarted MCP verified the
saved names and the executor xrefs from both derived dispatch wrappers. Runtime
verification is pending.

### 2026-07-22 RUNTIME REJECTED: `fq` entry detours crash both processes

Processes 6048 and 12956 both loaded build `fq`, reported the complete install
marker `animAction=111`, reached the ordinary network startup, and then crashed
before any `NADINE-ANIM-ACTION-FACTORY`, `NADINE-HOLSTER-ACTION-EXEC` or
`NADINE-HOLSTER-SELECT` row. Both logs ended in the same early initialization
window; no weapon input was required. This invalidates the probe implementation,
not the static AnimAction ownership model.

Exact instruction-boundary inspection identifies three independent defects in
the new entry detours. The 24-byte factory copy ended with the first `0x48` byte
of `mov [rax+8],rbx` at `0x1410FD137`; moreover its generic trampoline jump used
RAX even though the retail prologue deliberately preserved the original RSP in
that live register. The 28-byte executor copy ended with the first `0x80` byte
of `cmp byte ptr [rcx+0x30],0` at `0x14108A65B`. The selector copy included the
relative call at `0x141ED058C`, but the generic trampoline does not relocate
rel32 operands. Any one is unsafe; all three entry-detour variants are REJECTED
and must not be restored with a longer guessed byte count.

### 2026-07-22 STATIC: safe owner/callsite/vtable AnimAction discriminator (`fr`)

Build `fr` preserves the same read-only observations without relocating retail
instructions. The Player-embedded AnimActionMgr at `Player+0x1B8` stores its
factory at manager `+0x18`, therefore the game-thread installer replaces only
the retained owner's aligned function pointer at `Player+0x1D0`; the wrapper
calls untouched `Player_CreateStateAction 0x1410FD120` directly. This is scoped
to the two retained Players and re-applied only when their object identity
changes.

The executor has two exact direct transfer sites. Update tail-jumps via `E9` at
`0x14108A5F9`, while OnTransition calls via `E8` at `0x14108A62B`; both resolve
to `PlayerHolsterWeaponAnimAction_Execute 0x14108A640`. Each five-byte relative
operand now targets a nearby absolute relay, so no callee prologue or live
register is copied. Finally, IDB data and runtime-class identity agree that
`ProcessWeapon` vtable `0x142A9BF48` slot `+0x7A8` at `0x142A9C6F0` contains
exactly `WeaponAttachment_SelectExistingOwnerPoint 0x141ED0580`. Swapping that
single checked pointer observes the logical attachment mutation while calling
the untouched function body.

The evidence quota is met by: exact retail instruction boundaries and branch
targets; independent manager/vtable pointer identity; and the already-proven
callsite-relay plus checked-vtable installation patterns used by stable hooks in
this DLL. Competing instrumentation choices are the rejected function-entry
copies, hardware breakpoints (too broad and thread-sensitive), and post-state
polling (cannot distinguish a missed executor from a failed selector).

Written prediction: both copies must start with marker `2026-07-22fr`, report
`animSafe=111`, and later report one owner-local factory install per retained
Player without crashing before input. A pistol and rifle draw/Put cycle on
Chloe must produce `weapon-holster` factory, Execute and selector rows. The
legacy Nadine sequence then selects the same A-E hypotheses written above. Any
pre-input crash, site/slot mismatch, missing owner factory install, changed
weapon behavior or non-stock state mutation rejects `fr`. The release DLL
compiled successfully; runtime verification is pending.

### 2026-07-22 RUNTIME CONFIRMED: Nadine lacks the stock pistol animation point (`fr`)

Processes 17996 and 20372 loaded `fr`, installed both safe code-boundary probes
and reported `animSafe=111`. They remained stable through repeated pistol/rifle
Get and Put cycles on both copies; no behavior regression or crash was observed.
Both processes were closed after the bounded capture.

The executor result rejects action hypotheses A-C. Exact
`PlayerHolsterWeaponAnimAction` objects (`vtableRva=0x2A46560`, marker
`0xFA06C342`, layer `0xE2B990B9`) changed `executed=0->1` for both Chloe look
`0x3F334972` and legacy Nadine look `0xD27C074F` on both process roles. The
owner-local factory swap emitted no install/factory rows, so absence at that
late factory subprobe is not admissible evidence; the exact derived executor
independently proves the action had already been created and dispatched.

The selector sequence isolates the first concrete look difference. Chloe's
pistol Put changes `rightHand -> targSpineProp 0x00AD047A`, then
`targSpineProp -> targSpinePropAnim 0xBBACEF13`, then returns to
`targSpineProp`. Across both Chloe roles, every captured
`targSpinePropAnim` request changed the record to `0xBBACEF13`. Nadine reaches
the same executor and can select `targSpineProp`, but every captured pistol
request for `targSpinePropAnim` left the current id at `0x00AD047A`. Thus the
legacy look has no selectable stock animation attachment record for that id.
The rifle control independently selected generic `gun_holster 0x555EFB8D` on
both looks, matching the user-visible rifle return to the back.

This selects a refined E: the multi-stage stock selector runs, but Nadine's
pistol animation-point request cannot change the attachment record. The final
stock Put intentionally selects `targSpineProp`, which is Chloe's back-holster
destination and explains why Nadine still stores the pistol on her back despite
having a separate hip-holster record (`0x8A0249B5`, index 41) in the repaired
owner table. The next fix must map only legacy-Nadine pistol holster animation
and final Put semantics to that existing hip record. It must distinguish Get
from Put; globally redirecting every `targSpineProp` request would also alter
the draw path and is not authorized. The ineffective late factory-pointer probe
should be removed from the next build.

### 2026-07-22 STATIC: map Nadine pistol Put inside the stock AnimAction (`fs`)

The exact `fr` selector order and the executor dataflow rule out a global
attachment redirect. `PlayerHolsterWeaponAnimAction_Execute 0x14108A640` first
requires the selected record id to equal `targSpineProp 0x00AD047A`, then
explicitly requests and rechecks `targSpinePropAnim 0xBBACEF13`, and only after
that performs the final controller rebind. Rewriting the first spine request
would fail the mandatory guard; repairing after Execute is also rejected by the
earlier runtime observation that the engine overwrites the late repair.

Build `fs` therefore makes two bounded compatibility changes. First, after the
validated native-Nadine `pistolHolster 0x8A0249B5` record has been remapped to
the Player's shared `r_upper_leg` joint 5, the repair appends
`targSpinePropAnim 0xBBACEF13` as an exact transform/joint alias of that hip
record. This lets the stock animation-point selection and its id check succeed.
Second, the safe Execute callsite wrapper establishes a thread-local scope only
for legacy-Nadine's one-shot Put executor. Inside that scope the checked
ProcessWeapon vtable selector preserves the first `0x00AD047A`, observes the
`0xBBACEF13` stage, and rewrites only a later `0x00AD047A` request to
`pistolHolster 0x8A0249B5`. It additionally requires that the selected weapon
is exactly the owner's slot-1 weapon, covering different pistols without
depending on one art id. Chloe, slot-0 rifles, grenades, Get and every selector
call outside the exact executor remain unchanged. The direct inactive repair
stays disabled.

Independent evidence is: the runtime look asymmetry at the exact selector; the
executor's three-stage disassembly and mandatory id checks; the already
validated joint-5 hip record and transform; and the rejected post-action repair
showing that the intervention must occur inside Execute. Competing hypotheses
are (H1) alias plus final redirect restores the Put animation path and leaves
the pistol on the thigh; (H2) all ids become correct but a later transform owner
still leaves it visually on the back; and (H3) another selector order appears,
which is a new static lead rather than permission for a broader redirect.

Written prediction for `fs`: both copies start without a pre-input crash and
report `holsterSafe=11`. Legacy Nadine's repair reports an animation alias with
the same joint/transform as `pistolHolster`. On pistol Put, the first requested
AD remains effective AD, BB changes the record to BB, and the later requested
AD logs `effective=0x8A0249B5`, `redirect=1`, ending on the hip id. Chloe's
requested/effective ids remain identical; rifle and grenade never redirect.
The pistol must finish at Nadine's thigh and the previously broken Put/draw
presentation should improve without body, arm, rifle or grenade regressions.
Any crash, alias mismatch, missing BB transition, redirect outside slot 1, final
id other than `0x8A0249B5`, back placement or model damage falsifies H1. The
ineffective owner-local factory-pointer probe and its runtime offset were
removed rather than carried into the fix. The release DLL compiled successfully
with marker `2026-07-22fs`; runtime verification is pending.

### 2026-07-22 RUNTIME REJECTED: logical pistol hip id moves the wrong visible mesh (`fs`)

Processes 1796 and 8984 loaded `fs`, installed the safe hooks with
`holsterSafe=11` and remained stable throughout the test. The structural half
of H1 matched exactly. Legacy Nadine's table reported `animAlias=2`, pistol
index `41`, alias index `42`, joint `5 -> 5` and `transformEqual=1`. On both
retained roles, standard pistol art `0x33E29A1B` in weapon slot 1 repeatedly
selected `AD -> BB -> effective 8A`: the first `targSpineProp` request stayed
unchanged, `targSpinePropAnim` selected alias index 42, and the final request
logged `redirect=1` and ended on `pistolHolster` index 41. Rifle art
`0x8075397D` in slot 0 continued to request/select generic
`gun_holster 0x555EFB8D` at index 40 and never crossed the redirect.

The visible prediction nevertheless failed. The user saw the rifle model move
from its back location to Nadine's thigh, while the pistol still appeared on
her back. The pistol was also absent immediately after spawn and appeared only
after its first draw/Put cycle. Therefore the art/slot filter itself is not the
failure: the runtime rows preserve the already-confirmed identities
`0x33E29A1B = pistol/slot 1` and `0x8075397D = rifle/slot 0`. Instead, H1 is
REJECTED and H2 is selected: the logical ProcessWeapon attach record at
`weapon+0x688` is not the final owner of the visible Player-Nadine weapon mesh,
or the downstream DrawControl/child binding crosses those logical objects.
Do not swap slot numbers or broaden the selector redirect from this result.

The initial invisibility has a separate concrete ordering explanation. Before
the compatibility repair ran, the first legacy-Nadine selector calls for rifle
generic and pistol `targSpineProp` both returned zero and left `rightHand`
selected. Later rows, after the table had been repaired, returned one for the
same ids. `ensure_legacy_nadine_attach_points_game_thread` currently waits for
`latest_peer_state` in `apply_peer_held_weapon_state`, so it can run after the
initial weapon-holster pass. The repair must eventually move to an earlier
game-thread lifecycle boundary after `Player+0x1E0` is materialized but before
the first BuildBinding/selector calls.

Both processes were closed after capture. Build `ft` removes the BB alias and
the scoped final redirect so the visible rifle regression is parked; it retains
the crash-safe Execute/selector observation and the previously proven basic
AD/generic compatibility records. The next static task is to trace each
ProcessWeapon through `NdAttachableObject_PostJointUpdate_Async` into its actual
DrawControl/mesh child and independently locate the pre-holster Player lifecycle
boundary. No further attachment-id mutation is justified until those owners are
resolved. The rollback release compiled successfully with marker
`2026-07-22ft`; it was not launched because the next runtime test still requires
the new static owner proof.

### 2026-07-22 STATIC: exact ProcessWeapon ownership and generation discriminator (`fu`)

The post-`fs` log contains a stronger clue than an art/slot mismatch. During one
legacy-Nadine Player generation, two successive pistol objects
`0x10E100D180/0x10E100C380` and two rifle objects
`0x10E1009280/0x10E1008480` resolved through the same retained parent handle.
Both pistol objects reached `pistolHolster 0x8A0249B5`, joint 5. Earlier rows also
show old pistol/rifle/grenade objects losing all inventory slot membership while
new same-art objects became the controller's slots. The old eight-entry census
filled before the stable clone pair appeared, so it could not classify whether
the stale or current generation still owned active render instances.

Static tracing rejects normal cross-art mesh ownership. ProcessWeapon vtable
`0x142A9BF48` slot `+0x7A8 -> WeaponAttachment_SelectExistingOwnerPoint
0x141ED0580` writes only that exact object's selected byte `+0x688` at
`0x141ED06EF`. `NdAttachableObject_ResolveSelectedAttachmentTransform
0x141ED1780` reads the same byte and parent `+0x4E0/+0x4E8`, resolves the same
owner's AttachSystem through `AttachSystem_ResolveRecordTransform 0x1418B7060`,
and publishes the local result through `NdAttachableObject_UpdateSelectedAttachmentTransform
0x141ED1380` into the same object. ProcessWeapon slot `+0x410 -> 0x141515860`
calls `NdAttachableObject_PostJointUpdate_Async 0x141ED28E0` at `0x141515A30`;
its base transform path derives that same object's world outputs
`+0x240/+0x250/+0x280`. Finally `NdAttachableObject_SyncParentDrawVisibilityAndCameraFlags
0x141ED62D0` reads child DrawControl strictly from the same object at `+0x198`.
The independent `ew/fl` runtime census already correlated pistol art
`0x33E29A1B` with pistol mesh `0x33E29A1B` and rifle art `0x8075397D` with rifle
mesh `0x255BEBA0`. `PlayerWeaponAnimControl_UpdateHolsterIk 0x14108ECE0` is also
excluded: it writes only arm joint-cache world poses and never reads a
ProcessWeapon or attachment field.

Competing hypotheses are now: (H1) an old slotless generation keeps active draw
instances and is the visible back/leg weapon while the current logical object is
moved correctly; (H2) the exact active object selects the right record but its
derived world transform is not refreshed; (H3) the art/mesh identity changes in
a later generation or the user-visible firearm is a third non-inventory object.
Evidence quota is met by exact field dataflow, RTTI/vtable and callsite identity,
independent runtime art/mesh correlation, and the negative IK-owner trace.

Build `fu` therefore re-enables the already crash-safe `fs` mapping only to
reproduce the visible failure and adds no new gameplay mutation. Its bounded
read-only census keeps 32 ProcessWeapon generations and records, for pistol and
rifle separately, the exact pointer, controller slot/current membership,
selected record id, child DrawControl/mesh id, draw gates, active instance count,
and local/final transforms at the existing `0x141515A30` boundary. Written
prediction: if H1 is correct, a slotless old object remains `activeInstances>0`
while the current slot pistol reaches `8A`; if H2 is correct, the exact active
pistol has `8A` but its `+0x240/+0x250/+0x280` output does not follow the local
result; if an art owns the other art's mesh id, H3's generation-specific
ownership anomaly wins. If every active exact child has the expected art, mesh,
id and final transform, the visible firearm is outside the inventoried
ProcessWeapon children and the selector mapping is rejected as causal.

The initial pistol invisibility remains a separate ordering bug, but its static
repair boundary is now resolved. In the common lifecycle
`PlayerUpperDrawControlLifecycleAndAmbientOccluderStage 0x141BB4280`, the Player
vslot `+0x448` call at `0x141BB5043` returns to `0x141BB5049` only after
`Player_SetupAttachSystemFromLookData 0x1410CDF90` has called
`AttachSystem_InitFromDcData 0x1418B61B0` at `0x1410CE226`. The weapon controller
at `Player+0x1D88` is constructed later through `0x1410DD0FC -> 0x1410CA921`, so
the continuation is pre-controller and pre-selector. A future early repair must
gate on the exact Player vtable and raw look `Player+0x170`, not retained-slot
identity, because retained publication has not occurred yet. The `fu` release
compiled successfully with marker `2026-07-22fu`.

`fu` runtime result (two copies, PIDs 15640/14020): the user-visible result is
still wrong, but it rejects the current-slot selector/final-transform theory as
the complete cause. On the Nadine copy the initial pistol was invisible and the
rifle hung in her hand before any draw. Drawing the pistol showed pistol and
rifle interpenetrating in Nadine's own view, while the Chloe copy showed only
the pistol in hand and the rifle holstered. After one rifle draw/put cycle the
rifle moved to the thigh despite a back-holster animation; the next pistol draw
contained only the pistol, but its put-away still appeared on the back.

Both processes nevertheless agreed on the exact current-slot objects. PID
15640's pistol `0x10E08E2600` (art/mesh `0x33E29A1B`) and PID 14020's pistol
`0x10E0E86200` ended at attachment index 41/id `0x8A0249B5`, joint 5. Their
rifles `0x10E08DE700` and `0x10E0E82300` (art `0x8075397D`, mesh
`0x255BEBA0`) ended at generic index 40/id `0x555EFB8D`, joint 3. Each exact
child reported three of three active DrawControl instances, and the final-world
hashes changed across hand/generic/hip transitions. Thus H2 as written is
REJECTED: the current exact object's derived world transform does refresh. The
expected art/mesh pair on both current objects also rejects H3's cross-art child
variant.

The census only partially distinguishes H1. One immediate replacement pair
lost its slot and went from `3/3` to `0/0` child instances before the successor
pair became current, rejecting a universal "the immediately previous object is
always rendered" rule. Other later slotless generations retained `3/3`
instances after owner resolution was lost, and the bounded 32-entry table filled
before the final user sequence. H1 therefore remains open in the narrower form:
a detached older generation or one of the three still-unclassified render
instances can be the visible stale firearm. The different Nadine-view/Chloe-view
result, despite matching current logical transforms in both processes, makes
render-instance lifetime/publication the next discriminator. Do not change
attachment ids again.

### 2026-07-22 STATIC correction: weapon DrawControl entries are LODs (`fv`)

The three active entries reported by `fu` are not view-specific copies.
`IDrawControl_Relink 0x141753560` and
`IDrawControl_NotifyMeshUnloaded 0x141753200` identify `mesh+0x242` as
`m_availableLods`; each `0x20`-byte LOD entry contains live instance `+0`, freed
instance `+8`, retained `ArtItemGeo* +0x10` and resource id `+0x18`.
`IDrawControl_AddMesh 0x141751E30` allocates every LOD through
`FgDrawMgr_AllocateInstance 0x141819F30` against the single FgAnimData pointer at
`DrawControl+0`. Allocation appends the `0x280`-stride global-pool index to
`FgAnimData+0x210`, whose current/max counts are `+0x20C/+0x208`;
`FgDrawMgr_DetachInstanceFromAnimData 0x14181A5A0` removes it by swap-delete and
`FgDrawMgr_FreeInstance 0x14181A2A0` releases the same index. These source
assertions, direct field writes and the independent AddMesh/Relink paths satisfy
the static evidence quota.

The final render boundary also corrects the old `+0x240/+0x250/+0x280`
assumption. Animation-manager phase `0x40` resolves the ProcessWeapon owner from
`FgAnimData+0x1A8/+0x1B0` and publishes its composed world transform through
`FgAnimData_PublishWorldTransform 0x1418C55D0` into `FgAnimData+0x1C0`.
`FgDrawManager_PrepareInstanceForRendering 0x14181A9B0` walks that FgAnimData's
driven list and at `0x14181B3F4..0x14181B424` copies the result of
`FgAnimData_GetRenderWorldMatrix 0x1403014F0` to each `FgInstance+0x140..+0x17F`.
All three LODs therefore should receive one matrix. The getter uses `+0x1C0`
directly unless flag `0x40000` selects an additional multiplication by
`FgAnimData+0x2E0`. Visibility/camera helpers only modify instance `+0x18`,
`+0x1A0` and `+0x1A4`; they do not write the matrix.

Competing hypotheses are now: (H1) an active LOD is absent from the FgAnimData
driven list and retains stale `+0x140`; (H2) the exact current weapon's FgAnimData
owner or published `+0x1C0/+0x2E0` is wrong; (H3) a slotless old generation stays
active and can be identified by the owner id captured at `FgInstance+0x250`;
(H4) the visible firearm is a separate prop outside the inventoried
ProcessWeapon graph. The earlier "different matrices in three camera copies"
candidate is REJECTED, as is the claim that correct ProcessWeapon
`+0x240/+0x250/+0x280` proves the rendered matrix.

The next read-only build must use a rolling generation census and, for every LOD,
record its live/freed/ArtItemGeo pointers, resource id, active/flag state,
`FgInstance+0x250`, membership in `FgAnimData+0x210`, and hashes of instance
`+0x140`, FgAnimData `+0x1C0` and auxiliary `+0x2E0`. A safe post-call boundary
is the direct `FgDrawManager_PrepareInstanceForRendering` call at `0x14181B925`.
Prediction: a non-driven active LOD or unequal final matrix selects H1; three
driven equal LODs with a wrong publication selects H2; a live old owner id selects
H3; if every ProcessWeapon instance is coherent, H4 wins.

Build `fw` implements exactly this read-only discriminator. A checked E8 relay
at `0x14181B925` calls untouched
`FgDrawManager_PrepareInstanceForRendering 0x14181A9B0` first, then admits only
an RTTI-exact ProcessWeapon whose `owner+0x198 -> DrawControl+0` equals the
current FgAnimData and whose attachment parent has legacy-Nadine look
`0xD27C074F`. It publishes the exact instance-pool base to the game-thread
census. The old never-reused 32-entry candidate table is now a least-recently-
seen rolling table; each structural transition logs per-LOD live/freed/ArtItemGeo
pointers, resource id, pool index/driven membership, flags/active/category,
`FgInstance+0x250`, instance `+0x140` hash, FgAnimData `+0x1C0/+0x2E0` hashes and
the auxiliary instance pointers `+0x88/+0x90/+0x98`.

The same change fixes a diagnostic-only bounds defect in `fu`: candidate storage
had 32 entries while `last_signatures` and `log_counts` had only 8, so candidates
8..31 indexed past both memo arrays. `fw` sizes all memo arrays to 32 and resets
them whenever a rolling slot changes object. No weapon, attachment, DrawControl
or render value is written. The release DLL compiled successfully with marker
`2026-07-22fw`; runtime verification is pending under the prediction above.

`fw` runtime result (two copies, PIDs 336/23004) rejects stale LOD lifetime and
per-LOD matrix divergence. For every valid current pistol/rifle generation in
both processes, all three LODs were active, each pool index was present in the
same FgAnimData driven list (`3/64`), every `FgInstance+0x250` owner id matched
the FgAnimData/current ProcessWeapon runtime id, and all three
`FgInstance+0x140` hashes were bit-identical to the current FgAnimData `+0x1C0`
hash (`raw=1`). `DrawControl+0` also matched the FgAnimData admitted by the
post-`0x14181B925` hook. The auxiliary `+0x2E0` hash stayed constant while the
published/instance hashes changed with weapon state.

The rolling census captured repeated ProcessWeapon wrapper replacement. A
retired pistol/rifle pointer lost slot membership and its child became invalid;
the successor immediately became slot 1/2 current while retaining the same
FgAnimData addresses, pool indices and runtime owner ids. The shared live LODs
then named the successor as `fgOwner`; there was no independently active old LOD
with a different `+0x250` owner id. Therefore H1 and H3 are REJECTED for the
tested sequence. Visibility/camera selection cannot explain the placement by
choosing another LOD because all three carry the same current matrix.

The remaining split is now upstream of FgInstance: (H2) animation-manager phase
`0x40` publishes the wrong ProcessWeapon owner composition into the otherwise
coherent FgAnimData `+0x1C0`, or (H4) the visibly wrong firearm is a separate
prop outside the ProcessWeapon DrawControl graph. The next static task is to
trace the exact phase-`0x40` owner inputs around `0x141A8FC00`, including
ProcessWeapon `+0x110/+0x120` and `+0x130/+0x140`, back to the selected attachment
output, and identify any independent prop consumer before another runtime hook.
Both game processes were closed after capture.

### 2026-07-22 RUNTIME REJECTED / STATIC: corrected renderer-wide geometry owner census (`fx` -> `fy`)

The `fw` admission gate was intentionally too narrow to decide its remaining
H4: it accepted an FgAnimData only after resolving an RTTI-exact
`ProcessWeapon`, its own DrawControl and a legacy-Nadine attachment parent. A
separate prop using the same firearm geometry could never appear in that table.
The phase-`0x40` branch is not the next runtime boundary: the earlier `fl` probe
already established that the same `+0x110/+0x120` plus `+0x130/+0x140`
composition is internally coherent and that placement differences originate in
the resolved attachment/joint pose before the common final writer. Repeating
that hook would not distinguish the still-open separate-prop alternative.

Runtime `fx` produced 768 positive-control `NADINE-WEAPON-MEMBERSHIP` rows across
both processes, loaded the intended build and installed the post-render relay,
but produced zero `WEAPON-GEOMETRY-OWNER` rows and no crash. This rejects the
specific premise that the available-LOD `ArtItemGeo*` at entry `+0x10` equals
`FgInstance+0x1B0`; the census is inconclusive about H2 versus H4. Both processes
were closed after capture.

The corrected key now has four independent evidence types. First,
`ArtItemGeo_ResolveFgGeometry 0x1417CB100` reads only ArtItemGeo resource id
`+0x18`, looks it up in the global geometry registry, and returns the matched
payload's shared pointer. Second, both the LOD0 path at `0x14175224C` and the
additional-LOD loop at `0x1417525FE` pass that returned pointer as the third
argument to `FgInstance_Init 0x14192E590`. Third, the exact source assertion at
`0x14192EB04` identifies that argument as `const FgGeometry*`, and
`0x14192E830` stores it at `FgInstance+0x1B0`. Fourth, `fw` runtime established
the exact three live pistol resource ids `33E29A1B,49AE2C65,44ED0ABC` and rifle
ids `255BEBA0,FDBF86F9,F0FCA020`, their pool instances and coherent FgAnimData
owners. Therefore the live known LOD instance's `+0x1B0`, rather than the
available-LOD retained pointer, is the renderer-wide shared identity.

Build `2026-07-22fy` keeps the proven post-`0x14181B925` relay read-only. It
learns each available resolved FgGeometry pointer directly from the validated
current ProcessWeapon LOD instance and immediately scans every FgAnimData's
bounded driven list (maximum 256 entries), compares only `FgInstance+0x1B0`, and
publishes at most 32 unique matching FgAnimData owners through an atomic bridge.
Game-thread `WEAPON-GEOMETRY-OWNER` rows record owner/vtable/runtime id, optional
ProcessWeapon art and parent look, matched/active resource masks, matrix and
owner-id equality masks, categories and driven count. No weapon, attachment,
DrawControl, FgAnimData or instance field is written.

Competing predictions are now explicit. H4 predicts an active pistol/rifle mask
whose FgAnimData owner is not the already-known Nadine inventory ProcessWeapon
(`processWeapon=0` or `knownNadine=0` with the legacy-Nadine visual); that owner
becomes the next static target. H2 predicts that every legacy-Nadine firearm hit
is one of the known exact ProcessWeapons, with all three bits active and matching
the FgAnimData matrix/owner id; this rejects a second foreground prop and returns
the investigation to the local-versus-remote attachment pose upstream of
publication. Missing positive-control rows for a drawn weapon, table saturation,
crash or a visual regression makes the corrected census inconclusive. The
corrected release DLL compiled successfully. The real single IDB was updated
with `ArtItemGeo_ResolveFgGeometry` and the corrected `FgInstance_Init` comment
(`apply_labels OK: 1225 functions, 20 data names`, `save_database returned
True`); runtime verification is pending.

Runtime `fy` CONFIRMED the corrected resolver identity and REJECTED H4 in the
tested scene. Across PIDs `14008/7480` it emitted 91
`WEAPON-GEOMETRY-OWNER` rows and 793 membership rows, with no crash. Every one
of the 91 geometry hits resolved to the exact `ProcessWeapon` vtable
`RVA 0x2A9BF48`; `processWeapon=0` never occurred. The two legacy-Nadine
owners in each process were stable FgAnimData pairs for rifle art `8075397D`
and pistol art `33E29A1B`, parent look `D27C074F`, with retained id equal to
runtime id and equal nonzero resource/active/matrixMatch/ownerMatch masks. The
other hits were ordinary ProcessWeapons for look `3F334972` or one unparented
pistol owner; none was an alternate prop owner. The one-bit pistol/rifle masks
(`0x1`/`0x8`) are representative shared-FgGeometry aliases while every matching
owner still reports three driven instances; they do not indicate a missing LOD.

Therefore the visibly wrong legacy-Nadine firearm is the exact inventory
ProcessWeapon already observed by `fw`, not a separate foreground drawable.
H4 is REJECTED and H2 is selected: the incorrect placement enters the known
ProcessWeapon FgAnimData through its attachment/owner pose before the coherent
final instance writer. The next static boundary is the consumer chain from
`ProcessWeapon+0x688/+0x6A0` and parent `AttachSystem+0x1E0` into the retained
owner composition `+0x110/+0x120` plus `+0x130/+0x140` published at
`0x141A8FC00`. Both processes were closed after capture.

### 2026-07-22 STATIC: selected record, local correction and `Animating` overwrite (`fz`)

The `fy` result removes the separate-prop branch: the visible pistol/rifle
geometry belongs to the exact inventory `ProcessWeapon`. The earlier `fl`
boundary also showed that the common final FgAnimData publication is coherent.
The useful remaining split is inside the concrete ProcessWeapon attachment
update, before that common writer.

The static evidence quota is satisfied by four independent kinds. First, exact
ProcessWeapon vtable `0x142A9BF48` slot `+0x7B8` resolves to
`NdAttachableObject_UpdateSelectedAttachmentTransform 0x141ED1380`. Second,
direct disassembly at `0x141ED17F3..0x141ED1827` follows the validated parent
handle `weapon+0x4E0/+0x4E8`, reads the parent `AttachSystem` at `+0x1E0`, and
passes selected byte `weapon+0x688` to
`AttachSystem_ResolveRecordTransform 0x1418B7060`; that callee reads the signed
record joint at `+0x24` and composes its current world pose with the authored
record-local transform. Third, the exact callsite sequence
`0x141ED19AB..0x141ED19D1` passes the completed 32-byte transform in `R8`, a
distinct output buffer in `RDX`, and the weapon in `RCX` to sole callee
`0x141ED0A70`, then returns that output directly. Fourth, decompilation proves
`weapon+0x68C == -1` is an exact 32-byte copy; the other branch reads the rig at
`+0x1A8` and local/depenetration state at `+0x4F0..+0x52F` and
`+0x5F0/+0x5F4`. It never reads the previously suspected `weapon+0x6A0`, so
that descriptor is REJECTED as a placement input. The callee is named STATIC
`NdAttachableObject_ApplyLocalAttachmentCorrection` in the real IDB.

The trace also found a concrete later competitor. Source string `Animating` and
the `NdDrawableObject::GetAnimateCtrl` assertion bind `0x141ED2060` to the
animation-layer update. It first calls ProcessWeapon vslot `+0x7B8`, but an
active layer can write the same local transform again at `0x141ED2721`
(`weapon+0x130`) and `0x141ED2741` (`+0x140`). Only then does direct callsite
`0x141ED287B` compose that local state with `+0x110/+0x120`, immediately before
`FgAnimData_PublishWorldTransform 0x141ED288F`. This `Animating` overwrite is
the strongest concrete late candidate for restoring a back/thigh pose after a
correct attachment selection.

`PlayerWeaponAnimControl_UpdateHolsterIk 0x14108ECE0` is REJECTED as a competing
late weapon writer. It consumes blended `WeaponHolsterIkData` and writes only
arm-chain joint world poses through `0x14181EF50`; it never reads a
ProcessWeapon handle and never writes `+0x688`, `+0x4E0`, an AttachSystem record
or a weapon/render transform. It can still explain malformed free-arm pose, but
cannot undo the weapon attachment itself.

The alternatives and pre-runtime prediction are explicit:

- **Hrec:** `AttachSystem_ResolveRecordTransform` already produces the wrong
  world pose. `fz` will show that the discriminating difference is present in
  the `0x141ED19BB` input, while the local stage copies it unchanged or behaves
  equivalently for native and legacy Nadine.
- **Hlocal:** `0x141ED0A70` introduces the error. Legacy Nadine will show a
  stable nonzero input/output change tied to a non-`-1` `weapon+0x68C` or local
  state that is absent/different on native Nadine.
- **Hblend:** the optional `weapon+0x838` path changes the selected result inside
  vslot `+0x7B8`. The `0x141ED19BB` output will remain sound while the local
  state captured on return from `+0x7B8` will already differ.
- **Hanim:** selected update returns a stable correct `+0x130/+0x140`, but the
  exact same 32 bytes differ at `0x141ED287B`; this directly confirms the
  `Animating` layer writer at `0x141ED2721/2741`.

Build `2026-07-22fz` installs three pass-through boundaries: the verified direct
E8 `0x141ED19BB`, exact ProcessWeapon vslot `+0x7B8`, and final direct compose
callsite `0x141ED287B`. It preserves every original return value and snapshots
at most four samples per stable native/legacy weapon role. No engine call is
added and no weapon, record, pose or render field is written. Missing positive
controls, an unexpected call target, a crash or a visual regression makes the
test inconclusive. The release DLL compiled successfully. The real single IDB
was updated with the two STATIC names (`apply_labels OK: 1227 functions, 20 data
names`, `save_database returned True`), the MCP copy was restarted, and both
names resolve from the refreshed database. Runtime verification is pending.

Runtime `fz` CONFIRMED **Hlocal** and rejected `Animating` as the observed
late overwrite in the covered states. Both PIDs `7584/18788` loaded the intended
build and installed all three probes. The capture produced 306
`NADINE-LOCAL-XFORM`, seven `NADINE-ANIMATING-XFORM`, 248 final-transform, 1037
membership and 156 geometry-owner rows without a crash. Both processes were
closed after capture.

All seven paired post-selected-versus-final-local rows had `changed=0`: the
exact `weapon+0x130/+0x140` returned by ProcessWeapon vslot `+0x7B8` survived
unchanged to final compose `0x141ED287B`. Those positive controls covered native
Nadine's holstered pistol, legacy-Nadine rifle at `gun_holster`, and legacy
Player pistol/rifle generic/active states. No paired final-compose sample was
emitted for the inactive Player pistol at `pistolHolster`, so the general
animation writer remains capable in other scenes, but it is not the writer that
caused any observed paired-state displacement here.

The direct local-correction boundary is decisive. Every captured relevant
weapon used `weapon+0x68C == 0`. Converting the raw IEEE-754 input/output words
to translation distance and quaternion angle gives:

| Role / selected record | Samples | Translation introduced by `0x141ED0A70` | Rotation introduced |
|---|---:|---:|---:|
| native Nadine pistol, `pistolHolster`, joint 99 | 4 | below numerical reporting precision | below numerical reporting precision |
| Player Nadine pistol, `pistolHolster`, joint 5 | 45 | min `0.09312`, average `0.12164`, max `0.54377` m | min `20.395`, average `24.278`, max `94.425` deg |
| Player Nadine rifle, `gun_holster`, joint 3 | 55 | min `0.01674`, average `0.01676`, max `0.01678` m | stable `7.070` deg |

The native pistol's `weapon+0x4F0` transform is identity in the holstered
samples, whereas the legacy Player pistol and rifle carry stable non-identity
art-specific values there. Thus the remapped AttachSystem record is not the
only source of the visible pose: `NdAttachableObject_ApplyLocalAttachmentCorrection`
consumes a different ProcessWeapon-local contract on the Player path and adds a
large, deterministic correction after the selected record has already been
resolved. Hlocal is CONFIRMED. Hrec is not yet rejected because the copied
record may independently need refinement, but further record tuning before
fixing this proven local-stage mismatch would conflate two transforms.

The next static task is to trace writers/lifetime of `weapon+0x68C` and
`+0x4F0..+0x52F`, compare Player versus `Npc_GiveWeapon` initialization, and
find the narrow stock condition that suppresses or selects this correction for
native holstered weapons. Do not patch `+0x68C` or zero `+0x4F0` until that
contract is resolved.

### 2026-07-22 STATIC: Player `weapon-holster` action owns the late local offset (`ga`)

The writer trace resolves the apparent `fz` local-correction result one stage
further upstream. `weapon+0x68C` is initialized to `-1` by
`NdAttachableObject_Init 0x141ECEFE0`, then replaced at `0x141ECF4A1` by the
joint index resolved from init data. Both the native and Player weapons reached
`fz` with index zero, so this selector alone cannot explain their different
corrections. The discriminating state is the 32-byte primary transform at
`weapon+0x4F0/+0x500`.

Three independent evidence kinds identify its Player owner. First,
`WeaponHolster_BuildBinding 0x1414FE540` returns a concrete 0x54-byte binding:
attach StringId at `+0`, primary transform at `+0x10/+0x20`, secondary transform
at `+0x30/+0x40`, and scalar at `+0x50`. An art-specific row copies those fields
from the cached WeaponArtDef holster record; a missing row produces identity.
Second, `PlayerWeaponController_RebindWeaponToHolster 0x1410A9670` calls that
builder at `0x1410A9A02`, selects output `+0` through weapon vslot `+0x7A8`, then
copies output `+0x10..+0x2F` verbatim into `weapon+0x4F0..+0x50F` at
`0x1410A9A65/0x1410A9A99` (and independently for slot 21 at `0x1410A9B04`).
Third, native `AiWeaponController_EnforceWeaponAttachment 0x1405AE580` maps the
WeaponArtDef class to `pistolHolster`/`longgunHolster` and invokes only vslot
`+0x7A8` at `0x1405AE89A`; it never builds a Player binding and never writes
`+0x4F0`.

The one-shot Player animation action is a later, stronger writer than the
builder. `PlayerHolsterWeaponAnimAction_Execute 0x14108A640` first calls the
controller rebind, requires `targSpineProp`, selects `targSpinePropAnim`,
resolves that record's transform, and calls the controller rebind again at
`0x14108AB72`. It then resolves the newly selected final record, computes the
relative handoff transform, and writes the result directly to
`weapon+0x4F0/+0x500` at `0x14108AD4F/0x14108AD57`; the following block preserves
the old/new transforms and seeds a `0.1` blend at `+0x600..+0x647`. This is the
exact incompatibility with the current scoped selector repair: the action
computes a Chloe authored `targSpinePropAnim -> targSpineProp` handoff, but its
third selector is redirected to Nadine's copied `pistolHolster`, so the same
back-holster continuity delta is applied in the thigh record's coordinate
space.

Existing `fz` runtime ordering supports that dataflow without yet proving the
exact write instruction. On the retained legacy-Nadine pistol, the binding row
was `3D9BA5E3,...,3F7BF4C4`, the selector sequence was
`targSpineProp -> targSpinePropAnim -> pistolHolster`, and the immediately
following local stage read a different nonidentity transform
`BE2E30E9,...,3F2DE578`. The same one-shot action was positively reached and
latched `executed=0->1`. Therefore a simple stale builder payload is excluded;
the late action delta is the remaining concrete writer between those samples.

Competing hypotheses are explicit. **Haction** predicts that the first
`weapon-holster` execution changes the active slot's `+0x4F0..+0x52F` from its
pre-action value to exactly the nonidentity value later consumed by
`0x141ED0A70`, with selected id ending at `pistolHolster`; the native NPC has no
such action. **Hscript** predicts no action-local change and requires a later
`reattach-player-weapon`/event writer. **Hrecord** predicts the action-local
write occurs but the visible displacement remains after that local value is
eventually removed, leaving the copied record independently wrong. **Hbuilder**
predicts the post-action value equals the builder's primary payload and is
rejected if the two differ.

Build `2026-07-22ga` extends the already-installed pass-through action wrapper
with bounded before/after snapshots of slots 0 and 1. It logs the selected byte,
all 16 DWORDs at `+0x4F0..+0x52F`, and the two blend DWORDs at `+0x640`, only on
the first `executed=0` call. It adds no engine call beyond the already-used
inventory slot getter and writes no weapon, action, attachment, animation or
render state. Prediction: a retained-Nadine pistol holster cycle must emit one
slot-1 `NADINE-HOLSTER-LOCAL-WRITE` with nonzero `changed`, final selected index
for `pistolHolster`, and post words equal to the next
`NADINE-LOCAL-XFORM localBefore`; rifle slot 0 provides the independent Player
control. `changed=0`, a different final value, missing action reach, a crash or
any visual regression makes Haction inconclusive. Runtime verification is
pending. The release DLL compiled successfully. The four existing STATIC IDB
comments were refined with the exact binding layout and write sites
(`apply_labels OK: 1227 functions, 20 data names`, `save_database returned
True`); the restarted MCP resolves every updated comment from the single IDB.

### 2026-07-22 RUNTIME/STATIC: action write confirmed, stock blend resolved (`ga` -> `gb`)

Runtime `ga` confirms the action owner, with one correction to the written
prediction. PIDs `18288/22340` emitted 12 bounded
`NADINE-HOLSTER-LOCAL-WRITE` rows, 31 action rows, 28 selector rows and no crash
or exception. On every covered legacy-Nadine pistol cycle, the exact selector
sequence was `targSpineProp -> targSpinePropAnim -> pistolHolster`; the one-shot
action changed all eight transform DWORDs at `weapon+0x4F0/+0x500`, changed the
selected byte from rightHand index 16 to pistolHolster index 41, and seeded
`+0x640/+0x644 = 0.1/0`. The Player rifle control changed index 16 to generic
index 40 and copied the builder payload unchanged, without starting this blend.

The strict prediction that the action's post value would equal the immediately
next local-correction input was false. The next pistol sample was close but not
identical, while a later sample converged to the stable Player pistol binding
`3D9BA5E3,BCA3D70A,BD4CCCCD,0,BD6477B3,BDCD7209,3E0A06FB,3F7BF4C4`
(quaternion sign may flip without changing rotation). This does not select
Hscript: direct disassembly resolves the only intervening writer.

`NdAttachableObject_UpdateLocalAttachmentBlend 0x141ED4390` reads duration
`object+0x640`, adds `GetProcessDeltaTime()/duration` to progress `+0x644`,
clamps it to one, passes it through mode-2 smoothstep, and calls transform
interpolator `0x140221550`. That callee computes
`endpoint620 + t * (endpoint600 - endpoint620)` and stores the result into
`object+0x4F0/+0x500`; at progress one the updater clears `+0x640`. The action
had stored the computed handoff at `+0x620/+0x630` and the Player builder target
at `+0x600/+0x610`, so the observed sequence is exactly the stock Player blend
from the back-holster handoff back to the Player holster binding. Independent
callers include concrete ProcessWeapon updates such as `0x141500880`, which
invoke this shared updater once per frame. Hscript is REJECTED for the covered
interval; Haction is CONFIRMED as the immediate writer and the stock blend is
its continuation. Hrecord remains the falsifier for a local-contract repair.

The native comparison now supplies a narrow replacement contract: native Nadine
selects the same copied `pistolHolster` record through the AI controller, never
runs the Player builder/action, and `fz` measured an identity local transform
with negligible correction. Build `2026-07-22gb` therefore keeps the final
selector redirect but, only after that exact legacy-Nadine Player action has
observed `targSpinePropAnim` and redirected its final slot-1 selector, validates
that the weapon still belongs to that Player and resolves to `pistolHolster`.
It then writes native identity to the first eight DWORDs at `+0x4F0` and clears
the Player-only duration/progress at `+0x640/+0x644`. It does not touch the
attachment record, rifle slot, Chloe, native Nadine, hand state or any other
action. A dedicated repair row preserves both the stock post-action value and
the repaired value for audit.

**Falsifiable `gb` prediction:** each tested legacy-Nadine pistol holster cycle
must retain the stock nonidentity `NADINE-HOLSTER-LOCAL-WRITE`, then emit exactly
one `NADINE-PISTOL-HOLSTER-LOCAL-REPAIR applied=1` ending in
`0,0,0,0,0,0,0,3F800000` with blend `0,0`. The next selected-transform sample
for that weapon must use pistolHolster index 41 and read the same identity local;
the pistol must appear at the thigh rather than the back. Rifle cycles must emit
no repair row and retain their current behavior. A missing repair, a later
nonidentity pistol local, wrong visible placement, any rifle/Chloe change, crash
or body regression rejects this boundary; identity local with a still-wrong
thigh pose selects the independent Hrecord alternative.
The `gb` Release DLL compiled successfully.

Runtime `gb` REJECTS the local-contract repair as sufficient and selects
**Hrecord**. Both PIDs `12172/16940` remained stable and emitted respectively
four and three exact `NADINE-PISTOL-HOLSTER-LOCAL-REPAIR applied=1` rows. Every
repair followed the required legacy-Nadine slot-1 selector sequence, resolved
attachment id `0x8A0249B5` at index 41, replaced all eight primary local
transform DWORDs with identity and cleared the blend. All subsequent covered
`NADINE-LOCAL-XFORM` samples for that weapon still selected `pistolHolster`,
joint 5, and read the repaired identity; the final visible ProcessWeapon and
all three active child DrawControls were positively covered. No crash,
exception, rifle repair or body regression occurred.

The user nevertheless observed the pistol in the same wrong back position.
Therefore `weapon+0x4F0`, its blend at `+0x640`, late Animating overwrite,
separate prop/LOD and selector timing are REJECTED as causes of the remaining
placement error in the covered state. The scoped identity write has been
removed from build `gc`; retaining it would only mutate a proven-irrelevant
field. The exact final selector redirect and read-only action/local diagnostics
remain.

The remaining transform input is the copied attachment record itself. Its
current joint-5 transform was derived by
`raw_append_parent_remapped_attach_record` from the donor's *live animated*
joint 5 and joint 99 at one arbitrary initialization frame. Independent
processes produced different copied DWORDs, while the authored donor record is
fixed. That session dependence is incompatible with a stable attachment-space
record and is now the leading Hlive hypothesis. Competing Hbind uses the native
rig's authored bind/default transforms for the same joint-5-to-joint-99
conversion; Hdynamic would instead require recomputing a target-space value
from live donor and Player poses every frame. Before another runtime test,
resolve the rig descriptor's bind/rest transform storage and the stock engine
consumer that turns it into joint/model transforms; compare its joint 5 and 99
semantics against `RigPose_GetJointWorldTransform 0x141687C90` and eliminate Hdynamic or
Hbind statically.

### 2026-07-22 STATIC/RUNTIME: final pistol point is already thigh-equivalent; holster IK is the new boundary (`gd`)

The Hrecord conclusion above is superseded by an exact world-space comparison.
After normalizing the final ProcessWeapon transform by its owner root, native
Nadine's persistent pistol translation was `(-0.222, 0.835, -0.003)` metres.
The two `gb` legacy-Nadine Players were `(-0.238, 0.812, -0.014)` and
`(-0.263, 0.816, 0.008)` metres. Thus the covered final point differs by only a
few centimetres and is already thigh-equivalent; it cannot explain the user's
unchanged observation that Nadine performs the put-away motion toward her back.
The session-dependent copied record remains technically imperfect, but it is no
longer the leading cause of that animation trajectory.

Three independent evidence kinds move the boundary to the stock Player holster
IK. First, the existing runtime census consistently captured legacy Nadine's
`PlayerWeaponAnimControl+0x78` joint tuple as `-1,5,-1,3`, with identity leg and
pelvis bases at `+0x90/+0xB0`; Chloe resolved `101,5,199,3` and produced two
nonidentity bases. The independent native/Player joint-name capture showed that
Nadine's actual pistol marker is joint 99 with name id `0xADD0C871` in both rigs,
whereas the Player control hard-codes `r_leg_gun_holster = 0xE231DB44`, absent
from both. Second, `PlayerWeaponAnimControl_InitializeHolsterJoints 0x14108E700`
resolves exactly `r_leg_gun_holster/r_upper_leg` into `+0x78/+0x7C` and
`pelvis_holster/pelvis` into `+0x80/+0x84`; the missing marker indices therefore
directly explain the identity cached transforms. Its sibling
`0x14108EBF0` computes marker-relative-to-parent transforms only when both
indices are valid. Third, `PlayerWeaponAnimControl_UpdateHolsterIk 0x14108ECE0`
selects the leg pair or pelvis pair from the reference id emitted by
`WeaponHolsterIkBlender_ExtractData 0x14108B120`, composes that basis with the
authored target, and drives the arm IK chain. The extractor independently
distinguishes `apReference_leg_holster` from `apReference_back_holster`; it does
not touch ProcessWeapon attachment state.

Competing hypotheses are now explicit. **Hmarker** predicts the pistol clip
emits a valid, nonzero-weight leg reference, but the control feeds it the
observed identity leg basis because the Chloe-only marker name is missing;
joint 99 is then the exact replacement candidate. **Hbackref** predicts the
clip emits the back reference, making the missing leg marker irrelevant and
requiring the authored reference/holster-type decision to be retargeted.
**Hclip** predicts no usable extractor result (no call, `valid=0`, or zero
weight), leaving the visible hand path in the base animation clip rather than
this IK correction.

Build `2026-07-22gd` adds a read-only two-boundary discriminator. It scopes
`PlayerWeaponAnimControl_UpdateHolsterIk` only to a retained legacy-Nadine
slot-1 weapon, then records the exact `WeaponHolsterIkData` result: reference
id, valid flag, weight, target transform, joint tuple and both cached bases. It
does not alter the marker indices, cached transforms, reference id, IK target,
weapon or skeleton. **Falsifiable prediction:** a pistol put-away must emit
`NADINE-HOLSTER-IK-DATA`. A valid nonzero `leg` row with joints
`-1,5,-1,3` and identity first basis selects Hmarker; a valid nonzero `back` row
selects Hbackref; only missing/invalid/zero-weight rows select Hclip. A crash,
hook-install mismatch, non-slot-1 admission or any visual change invalidates
the probe. The Release DLL compiled successfully.

### 2026-07-22 STATIC: holster IK reads WeaponPartial, not the pistol layer (`ge`)

The full caller chain removes two previously ambiguous configuration candidates.
`PlayerPutGunAwayAction_OnStart 0x141096BB0` requests
`s_weapon-holster 0xDF5B68FC` through
`PlayerWeaponAnimControl_RequestWeaponState 0x14108C2D0`; that method targets
layer `0xE2B990B9`, whose executable StringId preimage is exactly
`WeaponPartial`, and records the requested state at control `+0x18`. Much later,
`PlayerWeaponController_RebindWeaponToHolster 0x1410A9670` calls control vtable
`+0x148` at `0x1410A9773` only for weapon category zero. The concrete method,
now `PlayerWeaponAnimControl_RequestPistolHolsterLayerState 0x14108F0B0`, requests
`s_pistol-holster` on a different layer, `0x63B48BEE`. Therefore the pistol-layer
state cannot be the source snapshot from which holster IK obtains leg/back; the
relevant authored state is `s_weapon-holster` on WeaponPartial.

The post-blend consumer confirms the ordering independently.
`Player_PostAnimBlending_Async 0x1410EB6A0` first calls control vtable `+0x138`
at `0x1410EB6EB` to refresh the marker-relative bases, then near the end calls
vtable `+0x140` at `0x1410EBAE5`. The latter constructs a
`WeaponHolsterIkBlender` and passes the active WeaponPartial layer to
`AnimStateLayer_ExtractAndBlendCustomData 0x14108F800`. That generic reducer
walks every live snapshot, calls `ExtractData`, blends transitions through
`BlendData`, and only then returns the single final `WeaponHolsterIkData` that
drives the arm chain. This also exposes a flaw in `gd`: logging each intermediate
`ExtractData` result could misclassify a transition containing both leg and back
sources; only the reducer output is decisive.

The configuration audit further narrows the code-controlled inputs.
`PlayerWeaponHolsterAction_ResolveAndBindAttachPoint 0x141095BD0` passes the
WeaponArt class and id through vtable `+0x100`; concrete methods
`0x14108E5A0 -> 0x14108E620/0x14108E690` store only class/art selectors at
control `+0xD0/+0xD4/+0xD8`. Separately,
`Player_BuildWeaponHolsterSpeedKnowledgeId 0x1410B1C80` builds only
`weapon-holster-type-normal/fast/very-fast/zipline`, so that knowledge family is
a movement-speed/context selector rather than the leg/back decision. The one
explicit positional input remains `weapon-holster-point-*`: initializer
`0x1400C28C0` defines generic/leg/pelvis, and the action commits the matching
knowledge before requesting `s_weapon-holster`. Standard pistol art instead
selects non-generic `targSpineProp`, bypasses the character scan, fails all three
table comparisons and commits no positional knowledge. This is a strong static
mechanism for WeaponPartial to fall back to its Chloe/back branch, but the
authored graph lives outside `tll.exe`, so it is not promoted to runtime fact.

The remaining competitors are now **Hpoint** (missing
`weapon-holster-point-gun_holster_leg` makes the final WeaponPartial reducer emit
back), **Hmarker** (the final reducer already emits leg, but absent
`r_leg_gun_holster` leaves its basis identity), and **Hclip** (the final reducer
is missing, invalid or zero-weight). The old **Hpistol-layer** and
**Hholster-type** candidates are STATIC REJECTED. If Hpoint wins, the stock-aligned
candidate is a scoped legacy-Nadine slot-1 leg-point knowledge input before
`s_weapon-holster`, not a global reference-id rewrite; joint 99 may still be
needed as the marker fallback once the leg branch is active.

Build `2026-07-22ge` replaces the intermediate ExtractData hook with a read-only
hook on `0x14108F800`, admits only exact requested state `0xDF5B68FC`, and logs a
deduplicated `NADINE-HOLSTER-IK-FINAL` row after all active sources are blended.
It still mutates no animation, knowledge, marker, weapon or skeleton state.
**Revised falsifiable prediction:** a legacy-Nadine pistol Put must reach the
final reducer while control `+0x18` equals `s_weapon-holster`. A valid nonzero
`back` final row selects Hpoint; valid nonzero `leg` with tuple `-1,5,-1,3` and
identity leg basis selects Hmarker; a missing reducer call or invalid/zero-weight
final row selects Hclip. Intermediate source references can no longer affect the
classification. Runtime verification remains pending. The Release DLL compiled
successfully with marker `2026-07-22ge`. Seven new STATIC function names and
comments were applied to the shared IDB (`apply_labels OK: 1235 functions, 20
data names`; `save_database returned True`) and verified after restarting MCP.

### 2026-07-22 STATIC: authored holster maps expose a Chloe-base remap before IK

The previously external-looking clip boundary is now partially resolved from the
installed game archives without extracting assets into the repository. `dc1.psarc`
contains `anim-player.bin` and `player-mesh-overlays.bin`; `actor77.psarc` contains
the corresponding animation packs. The new read-only
`scripts/psarc_asset_audit.py` parses their Oodle-compressed PSARC blocks in memory,
recomputes the game's zero-seeded non-reflected CRC32 StringIds, and reproduces the
following records.

`anim-player.bin` encodes the generic Player state with chest-named animations.
At file offset `0x1D8E0`, `p-holster-chest^idle-stand-part 0x8DCA21F2` is followed
by `s_weapon-holster 0xDF5B68FC` and action `weapon-holster 0xFA06C342`; the
unholster-side record at `0x1D760` similarly contains
`p-unholster-chest^idle-part 0x66846123`, the same state and action. In contrast,
`player-mesh-overlays.bin` contains explicit 40-byte source/replacement records:
the same chest holster id is paired with `p-holster-back^idle-stand-part
0x11438F72` at `0x7D78`, with `p-holster-leg^idle-stand-part 0x4A5559F7` at
`0x1FCD0`, and with `p-holster-waist^idle-stand-part 0x109BF2C2` at `0x32D40`.
The surrounding tables repeat the same source/replacement structure for the add,
finish, mirror, aim and unholster families. Thus back/leg/waist selection is an
authored animation-id remap, not a transform inferred from ProcessWeapon.

Native code provides an independently convergent Chloe-only boundary.
`PlayerWeaponController_MapChloeBaseWeaponOverlayId 0x14109D450` first requires
the animation owner at controller `+0x108` to pass
`PlayerAnimOwner_UsesChloeBaseSkeleton 0x1410EB3E0`, whose only accepted ids are
`chloe-base-test` and `chloe-base`. It then resolves the exact named object
`*chloe-weapon-anim-overlay-map*` and maps the input animation StringId through a
`SortedIdPointerTable`. Controller vtable `+0x3C0`, now
`PlayerWeaponController_FindWeaponOverlayRecord 0x14109D740`, searches the
controller's overlay tables with that Chloe-mapped id first and only falls back
to the original id if the mapped record is absent. RTTI and the vtable slot give
an independent consumer: `PlayerPlayWeaponOverlayAction_OnStart 0x141097450`
queries this exact `+0x3C0` method before publishing its selected weapon overlay.
The exact named-object-to-one-specific-DC-table association and reachability from
the current Put state are not yet proven, so this is not promoted to a bug fix.

The final IK semantics are now exact as well.
`AnimStateSnapshot_EvaluateReferenceSetMask 0x1418B3770` samples the requested
hand/leg/back references from the selected authored snapshot and returns bits
`1/2/4`. `WeaponHolsterIkBlender_ExtractData 0x14108B120` requires hand plus at
least one holster bit; leg has priority whenever bit 2 is present, while back is
used only when leg is absent. Missing hand or both holster references yields
`valid=0`. Neither this sampler, the reducer nor the consumer reads any
`weapon-holster-point-*` knowledge. Therefore the direct native claim "missing
leg-point knowledge selects back" is STATIC REJECTED. The broader **Hpoint** can
still exist in authored graph/script selection, but remains HYPOTHESIS.

The competitors are revised. **Hchloemap** predicts the legacy Nadine Player still
reports `chloe-base`, the hard-coded Chloe map converts the generic chest holster
animation to the back family, and the final snapshot consequently lacks a usable
leg reference. **Hpoint** predicts a different authored condition selects the back
family without that Chloe remap. **Hmarker** predicts the final snapshot already
contains leg but the hard-coded missing `r_leg_gun_holster` leaves its cached basis
identity. **Hclip** predicts no valid weighted hand-plus-holster reference set.
Build `ge` remains a valid final-IK discriminator, but a back row now proves only
that the selected final snapshot lacks leg; it does not by itself distinguish
Hchloemap from authored Hpoint. A later read-only probe must capture the
`0x14109D450` input/output and the `+0x3C0` chosen record alongside the existing
final reducer row. No game or injection was started during this static pass.
The PSARC auditor reproduced the state and chest-to-back/leg pairs against the
installed archives. Six new STATIC names plus the refined extractor comment were
applied to the single `ida/tll.i64` (`apply_labels OK: 1241 functions, 20 data
names`; `save_database returned True`) and verified through the restarted MCP.

### 2026-07-22 STATIC: the campaign Chloe DC00 object maps chest holster to back

The `player-mesh-overlays.bin` container is now decoded far enough to replace
the earlier unowned offset examples with named authored objects. Its `00CD`
header contains 411 indexed objects. Each relevant 0x30-byte record identifies
`anim-overlay-set 0x65FA07C0`, `player-mesh 0x675A8F15`, owner
`player 0xCDE4B742`, one look StringId, and a 0x28-stride replacement table.
`scripts/psarc_asset_audit.py --player-mesh-look ... --map-source ...` performs
this parse directly from the compressed PSARC entry and reports absent looks as
well as matching rows.

The campaign object is exact: object 123 (`0x4C9FB65C`) at record `0x30D8` is
tagged `player-mesh/player/chloe 0x3F334972`; its table begins at `0xDFA38`, has
979 rows, and row 3 at `0xDFAB0` maps
`p-holster-chest^idle-stand-part 0x8DCA21F2` to
`p-holster-back^idle-stand-part 0x11438F72`. The smaller
`chloe-srilanka 0x7DA5EF0B` and `chloe-srilanka-noscarf 0x1AF14BC6` objects do
not contain that source key. Neither the engine StringId for `nadine`
(`0xC7E2539B`) nor the legacy visual look `0xD27C074F` has a player-mesh object
in this DC00 at all.

Control objects disprove a character-wide rule and show that the map is
per-look/per-outfit. `vill-nadine-08`, `vill-nadine-10` and `vill-nadine-12`
share a table whose chest row maps to leg `0x4A5559F7`; `vill-nadine-16` maps the
same source to back, while `fh-chloe-08` maps it to leg. Native Nadine also has
104 `r_leg_holster 0xADD0C871` records in `anim-nadine-npc-dlc.pak`, whereas the
Player movement pack has 6793 references to Chloe's
`r_leg_gun_holster 0xE231DB44`. This independently explains why copying the NPC
skin onto a Player does not import the NPC's authored weapon route.

The current compatibility code makes this mismatch reachable in principle. The
second Player is born from Chloe's descriptor; the caller-gated appearance swap
and the later `Player+0x170/+0x190` visual-pair override replace render/look
identity only and explicitly leave the Player skeleton record untouched. Native
`PlayerAnimOwner_UsesChloeBaseSkeleton 0x1410EB3E0` therefore remains consistent
with selecting the hard-coded Chloe map even while the visible raw look is
`0xD27C074F`. Together with the generic chest state in `anim-player.bin` and the
exact campaign-Chloe chest-to-back row, **Hchloemap is now the leading STATIC
selection model**, but not yet a confirmed root cause.

One competing boundary was also eliminated. The actual put-away chain is
`PlayerWeaponController_StartHolsterTransition 0x1410A3EA0` ->
`PlayerPutGunAwayAction_OnStart 0x141096BB0`; it builds only a
`weapon-holster-type-{normal,fast,very-fast,zipline}` knowledge id and requests
`s_weapon-holster`. It does not directly call
`PlayerWeaponController_MapChloeBaseWeaponOverlayId 0x14109D450` or controller
vtable `+0x3C0`. Therefore a missing `0x14109D450` hit during Put would not by
itself reject Hchloemap: the remaining bridge may be authored-table selection at
load time or a later overlay action. The next static step is to resolve that
DC00/named-object consumer before designing another runtime discriminator. No
game or injection was started in this pass. The exact overlay-action name getter,
constructor and `PlayerWeaponController_Construct 0x14109D0E0` were added to the
single IDB (`apply_labels OK: 1244 functions, 20 data names`;
`save_database returned True`) and verified after restarting MCP.

### 2026-07-22 STATIC: Player+0x15A0 keeps the render-swapped Player on Chloe animation knowledge

The missing native-to-asset bridge is now exact. `Player_ApplyLook2MeshKnowledge
0x1410D6AD0` reads the Look2 id from `Player+0x15A0`, resolves and validates it
through `RenderLook_ResolveLook2ById 0x141800C00` and
`RenderLook_GetValidatedLook2 0x141801400`, then formats the exact registry key
`player-mesh/<Look2-name>`. The resolved record is passed to
`CommitResolvedKnowledgeRecordIntoSinkSlot 0x1417F9150`, which uses the record
key at `+0xC`, writes its value from `+0x10` into the parallel value array, stores
the record pointer and refreshes the sink. This is active Player animation
knowledge, not a passive asset lookup. The same function also installs
`mesh/default` and `female/default`, and falls back to `player-mesh/default` only
when the named Look2 record is absent.

Two independent callers establish the lifecycle. Top-level Player initialization
calls the selector at `0x1410DDB39`; `Player_ChangeLookId 0x1410FE840` calls it
again at `0x1410FF1BD` after rebuilding the visual body. `ChangeLookId` stores
its requested Look2 at `Player+0x15A0` (`0x1410FE89D`). Its separate appearance
field is `Player+0x15A4`, returned by Player vtable slot 92,
`Player_GetAppearanceHash 0x140A1DCC0`; that getter only copies `+0x15A4` to the
caller's output.

The compatibility mod currently hooks only this appearance getter. Admission is
restricted to the render look-builder return address `0x1BB4AB9`, where the hook
replaces the returned value with legacy Nadine `0xD27C074F`; all other calls use
the original getter. The later compatibility write changes only the visible
pair at `Player+0x170/+0x190`. It never changes `Player+0x15A0`, so the
Chloe-born second Player remains on the `player-mesh/chloe` animation-knowledge
record even while its rendered body is Nadine. That exact DC00 object contains
the already-proven `p-holster-chest -> p-holster-back` row at `0xDFAB0`.

This supplies three independent evidence types for the root-cause model:
(1) the campaign asset owns an exact Chloe chest-to-back row; (2) native dataflow
selects and commits `player-mesh/<Player+0x15A0>` into the animation-knowledge
sink; and (3) source audit proves the compatibility layer is render-only and
leaves that selector Chloe. Together with the existing runtime observation that
the hybrid Player uses the back motion, **Hchloemap is promoted to strong
STATIC root cause**, not CONFIRMED until a scoped repair passes a written runtime
prediction.

The fix alternatives are now bounded. Writing `Player+0x15A0/+0x15A4` to a
Nadine Look2 is rejected as a repair direction: the earlier full look rebuild
broke the body/black-screen path, and neither plain `nadine` nor legacy
`0xD27C074F` owns a player-mesh object in this DC00. Substituting an entire
`vill-nadine-*` or `fh-chloe-*` table is also unsafe because each contains
hundreds of outfit-specific mappings, and control outfits disagree between leg
and back. The leading repair candidate is therefore **Rrow**: preserve Chloe's
animation knowledge and replace only the exact chest-holster animation family
with the compatible leg family for the legacy-Nadine Player. Before implementing
it, resolve the runtime layout and consumer of the committed `anim-overlay-set`
record and its 0x28 mapping rows; do not patch the whole knowledge record or
start another game test yet. No game or injection was started during this pass.

### 2026-07-22 STATIC: anim-overlay runtime consumes the DC00 0x28 rows before clip lookup

The runtime consumer and record layout are now resolved. `AnimOverlays_NotifyDcUpdated
0x1417F92A0` is pinned by its exact source assertion in `anim-overlay.cpp`. It
re-resolves the registered overlay records after a DC update, resizes the live
state when the registry schema grows, restores compatible old entries by their
record keys and refreshes the derived state. The associated callback registration
explicitly watches both `anim-overlay-set` and `anim-overlay-layer-priorities`.
This ties the parsed DC00 object type to the live animation subsystem rather than
only to the generic knowledge registry.

The per-instance structure is also exact. `AnimOverlayState_Initialize
0x1417F9B30` stores the overlay-set count at `state+0x24`, allocates the set-pointer
array at `+0x8`, the parallel value-id array at `+0x10`, and a 64-byte/four-entry
variant history at `+0x18`; RNG and flags occupy `+0x25/+0x26`.
`AnimOverlayState_Copy 0x1417F9DD0` copies all of this state into active
AnimStateLayer instances. This provides the per-Player ownership needed for a
future scoped repair even though the underlying DC00 set objects are shared.

`AnimOverlayState_FindMatchingRow 0x1417FA6E0` exposes the authored table layout.
It walks the set pointers at `state+0x8` in registry order. Each live set has its
0x28-byte row array at `set+0`, row count at `set+0x8`, and a parallel source-id
array at `set+0x20`. It matches the current animation id against that source array,
validates the mirror selector against row flags at `row+0x26`, and returns the
row pointer together with set/row cursors. This is structurally identical to the
DC00 table audited by `psarc_asset_audit.py`, whose first two DWORDs are source
and target.

`AnimOverlayState_ResolveAnimationId 0x1417F9F00` is the actual executor. Starting
from an input animation id, it calls the row lookup, replaces the id with
`row+0x4`, applies grouped variant selection through
`AnimOverlayState_SelectVariant 0x1417FA3C0`, and follows chained replacements
for at most 100 steps. Its failure diagnostic explicitly says
`Circular remap chain ... in overlay set`, using the active set's record id.
Finally, `AnimationDatabase_ResolveClipWithOverlays 0x140516230` performs this
overlay resolution first, then calls `AnimSetRemap_ResolveAnimationId
0x1417CA2E0`, and only then resolves the final animation clip. Therefore the
campaign Chloe row `0x8DCA21F2 -> 0x11438F72` is executed before the back-family
clip/snapshot is selected; it is not inert metadata.

This closes the planned Rrow table-consumer boundary, but it also rules out an
unsafe implementation: overwriting `row+0x4` in the DC object would alter the
shared Chloe map for every owner using it. The safe seam must instead be the
legacy-Nadine Player's per-instance overlay resolution, admitted by the exact
`WeaponPartial/s_weapon-holster` path and exact chest-family source ids, while
leaving Chloe, other looks, other overlay rows and the shared DC set immutable.
The next static step is to identify that Player-owned callsite/context and prove
how its `AnimOverlayState*` maps back to the retained Player before writing a
bounded prediction. No game or injection was started.

### 2026-07-22 STATIC: Player master overlay state reaches the track-local snapshot resolver

The remaining ownership boundary is now exact. `Player_ApplyLook2MeshKnowledge
0x1410D6AD0` obtains the upper owner from `Player+0x1A0`, reads its wrapper at
`owner+0x60`, and commits the selected `player-mesh/<Look2-name>` record through
that wrapper. `PublishUpperPlayerOverlayReadJointAndReferenceState 0x1410CE260`
uses the same `owner+0x60` wrapper at `0x1410CE56F`, `0x1410CE597` and
`0x1410CE693`. When it constructs `AnimCmdGeneratorLayer`, `AnimSnapshotLayer`
and `AnimPoseLayer`, it dereferences the wrapper and stores the resulting master
`AnimOverlayState*` at each layer's `+0x18` (`0x1410CEFE1`, `0x1410CF17B` and
`0x1410CF22C`). The earlier description of this sink as
`[active_state+0x60]` was therefore imprecise; it is owned by the upper owner at
`Player+0x1A0`.

The copy into an executing track is equally concrete. In `AnimStateLayer_Step
0x1418A30A0`, the call at `0x1418A3725` invokes `AnimOverlayState_Copy
0x1417F9DD0` with `layer+0x18` as the source and the first track's state pointer
at `track+0x8` as the destination. The destination is not the master pointer; it
is a track-local copy, so pointer equality at the final resolver would have been
the wrong ownership test.

The destination then survives unchanged to the live row consumer.
`AnimStateLayer_PhaseUpdateInstances 0x14189D720` passes `track+0x8` at
`0x14189D824` into `AnimStateInstance_ResolveDtGroupAndSnapshot 0x1418A4670`.
That function forwards the argument twice to `AnimStateSnapshot_SnapshotAnimState
0x1418B24F0`, whose call at `0x1418B2940` executes
`AnimOverlayState_ResolveAnimationId` before the AnimSet remap and clip lookup.
The complete dataflow is therefore:

`Player+0x1A0 -> owner+0x60 wrapper -> master state -> layer+0x18 ->`
`AnimOverlayState_Copy -> track+0x8 local state -> snapshot -> row resolver`.

This adds three independent static evidence types: the Player/owner field
dataflow, three separately constructed layer types receiving the same master
state, and the downstream copy/caller chain into the exact resolver. It also
defines a scoped repair seam without mutating shared DC data: observe
`AnimOverlayState_Copy(dst, src)`, classify `dst` only when `src` is the retained
legacy-Nadine Player's current master state, clear that classification whenever
the same destination is copied from another source, and admit replacement only
for the exact chest-holster source-id family. A resolver-only comparison against
the master pointer is rejected because it sees the copied destination. The
remaining static task is to enumerate the smallest exact campaign-Chloe
chest-to-back family (draw, holster and mirrored/group variants) before writing
the runtime prediction. No game or injection was started.

### 2026-07-22 HYPOTHESIS: scoped Rrow idle-state discriminator (`gf`)

Build `gf` implements only the smallest proven member of Rrow. A detour at
`AnimOverlayState_Copy 0x1417F9DD0` tags a destination state only when its source
equals the current master state reached through a retained Player whose visible
look at `+0x170` is legacy Nadine `0xD27C074F`. Copying the same destination from
any other source clears the tag. `AnimOverlayState_ResolveAnimationId
0x1417F9F00` leaves every untagged call and every unrelated input unchanged. For
a tagged state and exactly `p-holster-chest^idle-stand-part 0x8DCA21F2`, it calls
the original resolver first and changes only the proven Chloe output
`p-holster-back^idle-stand-part 0x11438F72` to the proven compatible
`p-holster-leg^idle-stand-part 0x4A5559F7`. The shared DC row is never written.

**Falsifiable runtime prediction:** both copies must load without a hook-install
failure. The Nadine-owning process must log `NADINE-HOLSTER-OVERLAY-COPY`, then
on the relevant idle state log
`NADINE-HOLSTER-ROW ... input=0x8dca21f2 authored=0x11438f72`
`effective=0x4a5559f7 replaced=1`. After putting away a pistol and allowing the
idle pose to settle, Nadine must hold it at the leg rather than the back. Chloe
must still use her original back placement, and no body part, rifle or unrelated
animation may change. This first discriminator does **not** predict corrected
draw/put transition motion, because their source-id family is not yet enumerated.

The alternatives are separated as follows. A copy log without a row log rejects
the assumption that the generic idle source reaches this Player track. A row log
with `authored` other than `0x11438F72` rejects the exact campaign-Chloe map for
that live state and must not be force-replaced. A successful replacement log with
the weapon still settling on the back proves a downstream remap/attachment owner
still overrides the clip. A crash before any copy/row log points to detour ABI or
prologue handling rather than to the authored-map hypothesis.

### 2026-07-22 REJECTED: `gf` single idle-row discriminator did not reach its row

The user tested both weapon types after both `gf` processes installed the copy
and resolver detours successfully. The visible result was unchanged: the rifle
still occupied the leg/holster location and the pistol still went behind Nadine's
back. Both games remained responsive and there was no new body or animation
regression.

The runtime markers separate hook admission from row selection. PID 11968 logged
legacy-Nadine master-to-local copies for retained owner 0; PID 12256 logged the
same for retained owner 1. Thus the visual-identity check and master-state
ownership path both reached the expected local and remote Nadine Players.
Neither process logged a single `NADINE-HOLSTER-ROW`, so the exact
`0x8DCA21F2 -> 0x11438F72` replacement was never applied. The claim that this one
idle-part id alone is sufficient is **REJECTED**. This result does not yet reject
Hchloemap: the active call may use another member of the authored family, or the
64-entry append-only tag table may fill with stale track states during repeated
Player construction before the live holster track is inserted.

The asset comparison was therefore widened without another game run. Campaign
Chloe has exactly 144 rows whose resolved source names begin with
`p-holster-chest` or `p-unholster-chest`. `vill-nadine-08` has the same 144 source
ids. For every pair, the remaining 32 bytes of the 0x28 row are byte-identical;
only the target differs from a back-family id to the corresponding leg-family
id. The family includes idle, mirror-idle, stand-aim, cover, ledge and propane
variants and their add/finish/grab/part components. The first four are:

- `0x6C3F979A`: `p-holster-chest^idle-stand-add`, back `0xEEC390EF`, leg `0x6BE64FB2`;
- `0x062EC57C`: `idle-stand-finish-add`, back `0x3C68C935`, leg `0x354A9741`;
- `0x1FB7D7B3`: `idle-stand-finish-part`, back `0xA20325F5`, leg `0xA294A1FA`;
- `0x8DCA21F2`: `idle-stand-part`, back `0x11438F72`, leg `0x4A5559F7`.

`scripts/psarc_asset_audit.py` now reproduces this comparison directly by
combining the DC00 rows with names from `actor77.psarc/anim-player-movement.pak`.
The next runtime must first replace the append-only state list with a bounded
current-state mapping and log all exact family hits, tagged and untagged. It may
mutate only a tagged exact source/back pair; absence of every family source will
then reject this resolver as the live holster selection boundary.

### 2026-07-22 HYPOTHESIS: full chest-holster family discriminator (`gg`)

Build `gg` closes both holes left by `gf`. The 64-entry append-only ownership
list is replaced by a 1024-slot direct current-state map. Every
`AnimOverlayState_Copy` atomically overwrites the destination's classification,
including explicit untagging when its source is not the retained legacy-Nadine
master state; stale track addresses therefore cannot fill the table. The
resolver recognizes all 144 exact `p-holster-chest` and
`p-unholster-chest` rows reproduced from the installed campaign-Chloe and
`vill-nadine-08` DC00 objects. Each tuple contains source, Chloe-back target
and Nadine-leg target whose remaining 32 row bytes are identical.

The alternatives are now falsifiable. **Gfamily** predicts Put/draw reaches at
least one family source on a tagged state; an exact Chloe-back result is changed
to its paired Nadine-leg id and must move the Player holster motion toward the
thigh. **Gowner** predicts family hits exist but only with `tagged=0`, proving
the master-to-track ownership bridge is incomplete. **Gchain** predicts a tagged
family source whose final authored output is not its direct back target
(`exactBack=0`), selecting a downstream chained-remap seam. **Gother** predicts
zero family rows in both tagged and untagged calls during a covered Put cycle,
rejecting this overlay resolver as the live chest/back decision boundary rather
than authorizing another blind row mutation.

**Written runtime prediction:** both copies must load marker `2026-07-22gg`,
install the two overlay detours, report `family=144 stateSlots=1024`, and emit
the retained Nadine `NADINE-HOLSTER-OVERLAY-COPY` marker. Drawing and putting
away the standard pistol must then either produce a classifying
`NADINE-HOLSTER-FAMILY-ROW`, or its complete absence selects Gother. Only a row
with `tagged=1 exactBack=1` may report `replaced=1`; it must end at the
paired leg id and visually move the put-away motion to the thigh. Untagged rows,
non-back outputs, rifle, Chloe and every unrelated animation remain unchanged.
A crash, hook-install failure, body regression, rifle mutation or replacement
outside the exact tagged pair invalidates the probe.

### 2026-07-22 RUNTIME/STATIC: `gg` selects master-state row shadow (`gh`)

Both `gg` processes remained responsive and installed the full-family probe.
The covered legacy-Nadine Put cycles reached the exact 144-row family, so
Gother is REJECTED. PID 15384 copied master `0x10E0AB18B0` to local
`0x10E0AF2550`, then resolved all four covered holster sources
`AFB364A4, 66846123, 8DCA21F2, 1FB7D7B3` on state
`0x10E0AB18B0` itself. PID 1932 produced the same relation with master
`0x10E0EB12B0`. Thus the earlier static assumption that this live Put path
resolves only through the track-local copy is **RUNTIME REJECTED**: the relevant
resolver receives the retained Player's master state directly. Gowner is
selected, but no broader pointer tag is needed because
`seh_classify_legacy_nadine_master_overlay_state` already proves the exact
Player owner.

Every covered final output had `exactBack=0`: for example source
`8DCA21F2` completed as `9C559A24`, while its first Chloe row target is
`11438F72`; `1FB7D7B3` completed as `6A53B39B` instead of direct target
`A20325F5`. This selects Gchain and proves that changing the output after
`AnimOverlayState_ResolveAnimationId` is too late. No row was mutated by
`gg`.

The user's gold suppressed pistol is still a valid standard-pistol test. Its
ProcessWeapon remained art/mesh `0x33E29A1B` in slot 1; its three child
resources were `0x33E29A1B, 0x49AE2C65, 0x44ED0ABC`. The cosmetic gold and
suppressor resources therefore do not bypass the Player animation-family
boundary. The simultaneous native control also completed a real NPC holster in
PID 1932: `NpcWeaponController` state 0 changed the NPC pistol from
`rightHand/index 19` to `pistolHolster/index 42`. It emitted no Player
family row, confirming that native NPC Nadine uses a separate animation graph;
its transition is an endpoint control, not a Player action that can be copied.

Static row-consumer evidence now fixes the safe mutation seam.
`AnimOverlayState_FindMatchingRow 0x1417FA6E0` returns the exact 0x28 row
pointer at output `+0x10`; `AnimOverlayState_ResolveAnimationId` reads the
target at `row+0x4`, processes the row's grouping/variant fields and only then
feeds the chosen target back into the next lookup, for up to 100 chained steps.
The asset audit independently proves that each Chloe-back/Nadine-leg pair has
an identical remaining 32-byte tail. Calling the resolver anew with the leg id
would skip the first row's grouping and variant-history effects, while writing
the shared DC row would affect Chloe globally; both alternatives are rejected.

Build `gh` therefore detours only `AnimOverlayState_FindMatchingRow`. After
the stock lookup, it requires an exact family source, exact Chloe-back target
and a state equal to the retained visible legacy-Nadine Player's master state.
It copies the full 0x28 row into a thread-local 64-row ring, changes only DWORD
`+0x4` to the paired Nadine-leg target, and returns that shadow pointer. The
resolver detour is now read-only and records the final chained result; the stale
copy/tag table and its post-resolution mutation are removed. The ring exceeds
the resolver's static maximum of 16 rows in one grouped selection.

**Falsifiable `gh` prediction:** both copies load marker `2026-07-22gh` and
report `family=144 shadowRows=64`. A legacy-Nadine pistol Put emits
`NADINE-HOLSTER-ROW-SHADOW` on the exact master state for the covered family
members, with target changing from the logged Chloe-back id to the paired
Nadine-leg id. The subsequent `NADINE-HOLSTER-FAMILY-RESULT` reports
`master=1` and a chained output different from the corresponding `gg`
back-chain result. The arm/put-away motion must move to Nadine's thigh. Chloe,
native NPC Nadine, rifle, grenade, body and unrelated overlays remain unchanged.
A missing shadow, non-master shadow, unchanged final chain, back motion, crash
or any control-owner mutation rejects the proposed seam.

### 2026-07-22 RUNTIME REJECTED: `gh` reaches leg clips but not the arm path (`gi` rollback)

Both `gh` processes remained responsive and the scoped row shadow reached the
exact legacy-Nadine master states. The replacement was not swallowed by another
Chloe row: for input `0x8DCA21F2`, the prior `gg` chain ended at named clip
`p-holster-backflip^idle-mirror-stand-part 0x9C559A24`, while `gh` ended at
`p-holster-leg^idle-mirror-stand-part 0x93AB6B11`. The matching finish path
changed from backflip `0x6A53B39B` to leg
`p-holster-leg^idle-mirror-stand-finish-part 0xA67333CE`. The asset names, exact
row-shadow log and final resolver output independently prove that the full
overlay chain changed to the intended leg family.

The visual prediction nevertheless failed. The user still saw the put-away
motion go behind Nadine's back and reported that the animation had become more
jerky. Simultaneously, both exact slot-1 Player-Nadine paths reached
`NADINE-HOLSTER-IK-FINAL` for `s_weapon-holster` with
`reference=0, weight=0, valid=0` and joint tuple `-1,5,-1,3`. Thus Hmarker and a
remaining chest-to-back overlay edge are rejected for this covered transition;
the leg clip does not produce a usable final holster-IK contribution. The
campaign Chloe map is real, but rewriting it is **RUNTIME REJECTED as a visual
fix**. The row shadow is removed in build `gi`; the resolver-family hook remains
read-only (`mutation=0`) so the reported jitter is not retained.

The next boundary is after overlay resolution, not another DC row. Static call
order is exact:
`AnimationDatabase_ResolveClipWithOverlays 0x140516230` calls
`AnimOverlayState_ResolveAnimationId 0x1417F9F00`, then
`AnimSetRemap_ResolveAnimationId 0x1417CA2E0`, then tail-calls
`AnimationDatabase_ResolveClip 0x1417C8E20`. Competing hypotheses are now
**Hlookup** (the terminal leg id cannot resolve a clip in the active animation
set), **Hblend** (the leg clip resolves but its snapshot has zero final weight or
loses to another track), and **Hcontent** (the resolved leg clip itself supplies
no usable custom holster data). Before any further mutation, trace the returned
clip and active snapshot ownership/weight at these boundaries and compare a
stock back positive control. No game or injection is required until this static
dataflow is pinned and a bounded read-only prediction is written.

### 2026-07-22 STATIC: terminal leg clips are authored; bounded lookup discriminator (`gj`)

The complete asset chain confirms that `gh` did not pair incompatible rows.
`player-overlays.bin` object `0x0CEA0E76`, category
`weapon-base-layer-mirror 0x1BA911E4`, runs before `player-mesh` and maps
`8DCA21F2 -> 2E881D58` plus `1FB7D7B3 -> 253A3A91`. The campaign Chloe object
`0x4C9FB65C` then maps the former through back
`933CEF1E -> 9C559A24` and the latter through back finish
`3C29021C -> 6A53B39B`. The `gh` destinations `93AB6B11` and `A67333CE` are
terminal leg clips: none of all 406 player-mesh objects contains either as a
source row. This shorter path is authored, not a truncated chain.

The animation pack supplies an independent positive control.
`anim-player-movement.pak` contains `93AB6B11` at offsets `0x1341E0` and
`0x2265228`, and `A67333CE` at `0x13BB40` and `0x2270B50`; the non-mirrored leg
clips are present as well. Pack membership does not prove that the active
AnimSet lookup keys resolve them, so Hremap and Hnull remain live.

Static decompilation makes a non-mutating counterfactual safe.
`AnimSetRemap_ResolveAnimationId 0x1417CA2E0` only binary-searches the supplied
AnimSet and writes its three-DWORD output. `AnimationDatabase_ResolveClip
0x1417C8E20` only queries the resolved id and optional fallback registries.
Build `gj` therefore leaves the stock back animation untouched and brackets
`AnimationDatabase_ResolveClipWithOverlays 0x140516230` only for an exact
legacy-Nadine master state and idle Put sources `8DCA21F2/1FB7D7B3`. After the
normal lookup returns, it passes the already runtime-proven corresponding leg
terminal through the same active AnimSet and clip lookup into stack-local
outputs. It logs source, stock overlay terminal/clip, post-AnimSet leg id, both
lookup keys and leg clip pointer. It writes no animation, overlay, snapshot,
weapon or Player state.

**Falsifiable `gj` prediction:** both copies load marker `2026-07-22gj`, report
the overlay family probe with `mutation=0`, and install the read-only clip lookup
probe. One legacy-Nadine pistol Put emits `NADINE-HOLSTER-CLIP-LOOKUP` with
`overlaySeen=1, mapped=1, safe=1` and a non-null stock clip. A post-remap id
different from `legTerminal` selects Hremap. An equal id with `legClip=0`
selects Hnull. An equal id with a non-null leg clip, combined with `gh`'s exact
leg terminal and invalid final IK, selects Hvalid-noIK and moves the next static
boundary to active snapshot/reference ownership. The visible animation must be
the stable stock back motion with no new jitter because `gj` performs no
selection mutation. A crash, missing install, visual change, unsafe call or any
row shadow invalidates the probe.

### 2026-07-22 RUNTIME INCONCLUSIVE: `gj` missed the live resolver caller; chained remap discriminator (`gk`)

Both `gj` processes loaded the expected marker and installed the read-only
family and wrapper probes without a hook failure. The test reached exact
legacy-Nadine master states for both idle Put sources: `8DCA21F2` resolved to
`F5765D3B` and `1FB7D7B3` resolved to `AD6A50CF` on the covered non-mirrored
path. `NADINE-HOLSTER-ACTION-EXEC` also completed for the same retained Player.
However, neither process emitted `NADINE-HOLSTER-CLIP-LOOKUP`. Therefore `gj`
does not distinguish Hremap, Hnull or Hvalid-noIK; absence of that line is not
evidence that either leg clip is missing.

The failed admission has a static explanation. Cross-references show that
`AnimOverlayState_ResolveAnimationId 0x1417F9F00` has many direct callers in
addition to `AnimationDatabase_ResolveClipWithOverlays 0x140516230`.
`AnimStateSnapshot_SnapshotAnimState 0x1418B24F0` is one exact example: its call
at `0x1418B2940` consumes the track overlay state, then its next call at
`0x1418B2954` passes the returned terminal directly to
`AnimSetRemap_ResolveAnimationId 0x1417CA2E0` and stores all three returned
DWORDs in the snapshot. Independently, the remap decompile proves a pure
binary search/default-key write, and `AnimationDatabase_ResolveClip 0x1417C8E20`
is already the read-only consumer of that three-DWORD tuple. These facts reject
the assumption that the relevant family event must be nested beneath the
`0x140516230` wrapper and satisfy the evidence quota for moving the probe to the
shared resolver-to-remap edge.

Build `gk` removes the unreachable wrapper detour. On an exact master-state
idle source, the existing resolver hook now arms a thread-local record with the
source, stock terminal, owner and resolver return RVA. The immediately following
`AnimSetRemap_ResolveAnimationId` detour consumes that record only when its input
equals the captured terminal. It first preserves the original stock result,
then runs the paired leg terminal through the same AnimSet into stack-local
storage and performs read-only stock/leg clip lookups. No game animation,
snapshot, overlay row, weapon or Player field is written.

**Falsifiable `gk` prediction:** both copies load marker `2026-07-22gk` and
install `legacy-Nadine holster remap lookup probe`. One pistol Put must emit a
family line with `callerRva`, followed by `NADINE-HOLSTER-REMAP-LOOKUP` with the
same source/terminal, `mapped=1`, `stockSafe=1`, `legSafe=1` and non-null stock
clip. Return RVAs `0x18B2945/0x18B2959` select the snapshot path; other paired
RVAs identify the actual direct caller without widening admission. A leg id
changed by the active set selects Hremap; unchanged id plus null `legClip`
selects Hnull; unchanged id plus non-null `legClip` selects Hvalid-noIK and
moves the boundary to snapshot weight/reference ownership. Missing chained
output, unsafe lookup, crash or any visible animation change invalidates the
probe.

### 2026-07-22 CONFIRMED: `gk` rejects leg remap/lookup failure

Both responsive processes reached the direct snapshot path. Every admitted
pair reported `resolverCallerRva=0x18B2945` and
`remapCallerRva=0x18B2959`, confirming the static
`AnimStateSnapshot_SnapshotAnimState` edge rather than the rejected wrapper
assumption. On the covered mirror and non-mirror transitions, the active AnimSet
left each paired leg id unchanged, reused the same lookup keys
`ED25C708/C39CA709`, and returned a non-null leg clip. Examples include
`93AB6B11 -> clip 0x105A7B0BD8`, `A67333CE -> 0x105A7BC500`,
`4A5559F7 -> 0x105A7AF5E0` and `A294A1FA -> 0x105A7BBD00`.

Thus Hremap and Hnull are **RUNTIME REJECTED** for the exact covered Put
families. Hvalid-noIK is selected: the intended leg clips exist and resolve,
but the later final holster-IK reducer still has no valid weighted reference.
Pistol placement work is parked at that snapshot/reference boundary while the
user-prioritized local rifle defect is handled.

### 2026-07-22 STATIC: role-B local rifle refresh (`gl`)

The user reported an exact view split: after holstering, the rifle remains in
Nadine's hand on the Nadine-controlled copy, while the Chloe observer already
sees it removed from the hand. Current `gk` membership telemetry independently
shows the affected role-B retained owner 1 in a stable holstered state
(`current=-1, draw=0, hand=-1`) with the exact slot-0 rifle
`art=8075397D` already selecting index 40 / `gun_holster 555EFB8D`. The visual
hand pose therefore cannot be repaired by changing inventory slot or selected
attachment id again; the locally rendered physical attachment is stale after a
correct selector update.

Three independent facts support a bounded refresh. First, runtime build `fe`
already confirmed that stock `WeaponAttachment_AttachToOwnerPoint 0x141ED0370`
moves this exact normal slot-0 rifle out of Nadine's hand. Second, current exact
pointer membership and selected-id telemetry prove the same ProcessWeapon is
still involved, not an orphan or duplicate inventory object. Third, static
decompilation shows that `AttachToOwnerPoint` has no same-id early return: it
always replaces the parent handle, calls the attachment refresh virtuals,
selects `weapon+0x688`, and runs the final update virtual. Replaying the same
`gun_holster` id is therefore a real physical refresh, not a no-op selector
write.

Build `gl` re-enables the previously parked reconciler as a rifle-only path.
It never examines or writes pistol slot 1. It skips active slot 0, requires a
stable admitted `draw=0/2/4` state (or the already proven guarded stuck-state
case), exact legacy Nadine, exact slot-0 rifle art and an owner-backed
`gun_holster` record. The old confirmed `rightHand -> gun_holster` repair is
retained. In addition, only on role B owner 1 it replays the already-selected
`gun_holster` once per rifle holster generation to refresh the local rendered
attachment.

**Falsifiable `gl` prediction:** both copies load marker `2026-07-22gl`. On the
Nadine copy, the initial holstered state or one rifle draw/put cycle emits
`NADINE-INACTIVE-ATTACH slot=0 localViewRefresh=1 result=0x1 verified=1`, and
the rifle no longer remains in her hand. Drawing slot 0 must still put it in the
hand because active slot 0 is excluded. The Chloe observer, pistol, grenade,
body and weapon animation state must remain unchanged. A zero/unsafe call,
active-slot refresh, rifle disappearance while drawn, body regression or crash
rejects the repair.

### 2026-07-22 REJECTED: `gl` targeted the observer owner, not local Nadine

Both `gl` processes remained responsive and installed the expected hooks, but no
`NADINE-INACTIVE-ATTACH localViewRefresh=1` line was emitted and the rifle still
hung from Nadine's hand immediately after spawn. The post-test membership trace
resolved the mistaken ownership assumption: on role B the local Nadine is
owner 0, while owner 1 is Nadine's observed world instance on role A. The
`g_net_role_b && owner_index == 1` gate therefore admitted no local refresh at
all. The rifle moving to the leg only after a manual draw/put cycle was native
game behavior, not evidence that `gl` had run.

### 2026-07-22 STATIC: correct initial local owner gate (`gm`)

Build `gm` changes only the role-B local-view predicate from owner 1 to owner 0.
The remaining scope is unchanged: exact legacy Nadine, exact inactive slot-0
rifle, valid owner-backed `gun_holster`, one replay per holster generation, and
no pistol processing. This is supported independently by the role-B membership
line (`owner=0 slotMask=1 current=-1 draw=0 hand=-1 selected=40/555EFB8D`), the
role-A observer membership line for the counterpart (`owner=1`), and the absence
of the predicted `gl` mutation despite both hooks and networking being live.

**Falsifiable `gm` prediction:** both copies load marker `2026-07-22gm`. Once
the role-B local Nadine reaches the initial stable holstered state, the log emits
`NADINE-INACTIVE-ATTACH owner=0 slot=0 localViewRefresh=1 result=0x1 verified=1`
without requiring a draw/put cycle, and the rifle is no longer attached to her
hand immediately after spawn. The active rifle, pistol, body and role-A observer
must remain unchanged; missing mutation, crash or a drawn-rifle regression
rejects the correction.

### 2026-07-22 REJECTED: `gm` attach succeeds but cannot change local render

The exact predicted role-B call occurred at initial spawn:
`NADINE-INACTIVE-ATTACH owner=0 slot=0 localViewRefresh=1 result=1 verified=1`
with `40/555EFB8D -> 40/555EFB8D` and an owner-resolving parent before and after.
The user still saw the rifle in Nadine's hand. This runtime result rejects both
wrong-owner and missing-attachment-refresh explanations. A selector/link replay
at `0x141ED0370` is not the boundary controlling the role-B main-view image.

The same exact slot-0 ProcessWeapon independently owns a valid child DrawControl
and FgAnimData with three active, driven LOD instances and matching retained
owner id. This rules out an unrelated orphan as the only visible rifle. The
role-A observer simultaneously renders the rifle at `gun_holster` on the thigh,
so the remaining defect is view-specific rendering, not shared inventory state.

### 2026-07-22 STATIC: local inactive-rifle render gate (`gn`)

Competing repairs are now separated. Repeating or changing the attachment call
is rejected by `gm`. Writing Fg instance `active` bytes directly would race the
render worker and is not admitted. The bounded candidate uses the already
identified stock `DrawControl_HideMesh 0x141754510` / `ShowMesh 0x141754A30`
pair on the exact role-B owner-0 slot-0 ProcessWeapon. These routines previously
produced immediate per-mesh visible changes during the legacy-Nadine body probe,
which is independent runtime evidence that this is a real render gate.

Build `gn` removes the ineffective same-id attachment replay while retaining
the proven stale-`rightHand` repair. On the game thread it requires role B,
owner 0, exact legacy-Nadine look, exact slot-0 rifle art and a self-consistent
weapon DrawControl. Only stable `current<0,draw=0` hides its meshes. Only a real
slot-0 active state restores the mesh indices that this path itself hid. It does
not inspect the Player/body DrawControl, any pistol object, role A, or any loose
weapon candidate.

**Falsifiable `gn` prediction:** both copies load marker `2026-07-22gn`. Before
any weapon input, role B emits `NADINE-LOCAL-RIFLE-VIS owner=0 action=hide` with
a non-zero changed/mask value and the rifle is absent from local Nadine's hand.
Drawing slot 0 emits the matching `action=show`, makes the rifle visible in hand,
and subsequent holstering hides it again. Chloe's copy continues to show its
current thigh placement; pistol, body and grenade remain unchanged. No hide
line, a zero mask, an invisible drawn rifle, body loss or crash rejects this
render-gate repair.

### 2026-07-22 REJECTED: `gn` game-thread hide is overwritten later

Build `gn` selected the exact role-B owner-0 slot-0 rifle and successfully
called `DrawControl_HideMesh` with `changed=1`. Nevertheless, the user still saw
the rifle in hand at spawn. The log then repeated the same successful
`action=hide changed=1` for the same weapon and DrawControl on every game tick.
Because the mesh byte was non-zero again before each call, a later stock writer
was re-showing it after every game-thread hide. This rejects both wrong-object
and ineffective-HideMesh explanations and identifies call ordering as the
remaining boundary.

### 2026-07-22 STATIC: post-parent-visibility rifle gate (`go`)

The existing exact callsite relay runs inside
`NdAttachableObject_PostJointUpdate_Async` immediately after stock
`DrawControlSyncParentVisibility` and receives the current child DrawControl and
attachable ProcessWeapon. That original function is the observed later writer
capable of undoing `gn`. Independently, the earlier legacy-Nadine mesh probe
successfully removed individual visible body meshes from this same post-call
boundary without corrupting the model, proving that a stock HideMesh call here
survives into rendering.

Build `go` changes the game-thread path into a publisher only. Stable role-B
owner-0 `current<0,draw=0` publishes the exact slot-0 rifle pointer and a
generation. The late relay requires pointer equality, rifle art, and exact
`weapon+0x198 == child_draw`, then hides only that weapon's meshes after stock
visibility sync. On slot-0 activation, the game thread clears the published
target before restoring the mesh. No Player/body DrawControl, role-A instance,
pistol or loose rifle candidate is admitted.

**Falsifiable `go` prediction:** both copies load marker `2026-07-22go`. At
initial role-B spawn the log first emits `action=arm-late-hide`, then one
`action=late-hide changed=1` for the same weapon/generation, and the rifle is no
longer visible in local Nadine's hand. Drawing the rifle emits
`action=disarm-show` and shows it; returning to stable inactive state arms and
late-hides it again. The role-A thigh rifle, pistol, body and grenade remain
unchanged. Missing late-hide, an invisible active rifle, body loss or crash
rejects the ordering repair.

### 2026-07-22 REJECTED: `go` reaches late visibility but rifle remains

Build `go` emitted both `arm-late-hide` and same-generation
`late-hide changed=1` for the exact current owner-0 slot-0 rifle. Its subsequent
membership and geometry-owner snapshots showed all three rifle LOD instances
inactive (`instances=3/0`, geometry `active=0`) while the pistol instances
remained active. The user nevertheless still saw the starting rifle in the
local hand. Therefore neither the ProcessWeapon attachment link nor its
DrawControl visibility/instance-active state is the final main-view submission
gate.

### 2026-07-22 STATIC: suppress exact inactive rifle render preparation (`gp`)

IDA decompilation identifies `0x14181A9B0` as
`PrepareInstanceForRendering(FgAnimData*, FgInstance*, bool)` from
`fg-draw-mgr.cpp`. It copies CPU skinning matrices, publishes the render-world
matrix into every driven `FgInstance`, and at `0x14181B650..0x14181B68A`
increments the per-frame queue and writes each instance into it. The existing
callsite relay at `0x14181B925` already receives the exact FgAnimData and pool
used by the successful geometry-owner census. This is downstream of the
rejected attachment and DrawControl boundaries.

Build `gp` keeps the exact game-thread publication and late visibility gate,
then adds a final admission check at that relay. It resolves
`FgAnimData+0x1A8` back through its retained handle, requires pointer equality
with the published rifle, matching retained/runtime id, ProcessWeapon vtable,
rifle art and `weapon+0x1A8 == FgAnimData`. Only then is the stock prepare call
skipped for the inactive rifle. No instance bytes are written. When slot 0
becomes active, the game thread clears the published pointer and the original
function resumes automatically.

**Falsifiable `gp` prediction:** both copies load marker `2026-07-22gp`. Initial
role-B spawn emits `arm-late-hide`, `late-hide`, and
`NADINE-LOCAL-RIFLE-FGDRAW action=skip-prepare` for the same generation; the
rifle is absent from the local hand. Drawing slot 0 emits `disarm-show`, stops
the skip and renders the rifle normally. The role-A instance, active pistol,
body and grenade remain unchanged. A reached skip with the rifle still visible
would prove a second, as-yet-unidentified FgAnim/geometry owner; missing rifle
when active, body loss or crash rejects the submission gate.

### 2026-07-22 REJECTED: `gp` intercepted only the worker prepare path

Runtime build `gp` reached the full same-generation chain on role B:
`arm-late-hide`, `late-hide changed=1`, and
`NADINE-LOCAL-RIFLE-FGDRAW action=skip-prepare` all named the same inactive
owner-0 rifle and FgAnimData. The user still saw the rifle in the local hand.
The prior claim that `0x14181B925` was the final render-admission boundary is
therefore REJECTED. `PrepareInstanceForRendering` updates render data; the
single intercepted worker callsite does not prove either exclusive preparation
or admission to a view.

### 2026-07-22 STATIC: second synchronous prepare path discriminator (`gq`)

The `gp` result has a concrete static correction. Cross-references to
`FgDrawManager_PrepareInstanceForRendering 0x14181A9B0` include not only the
worker call at `0x14181B925`, but also executable call `0x14197DC54` inside
`sub_14197D8C0` (and an unowned code xref at `0x14181C102`). The second path is
independent of the normal PostAnimProcessing batch: it resolves a retained
owner through `a1+0x108/+0x110`, obtains that owner's FgAnimData through
`owner+0x1A0 -> +0x20`, copies an authored frame into CPU skinning buffers
`FgAnimData+0x230/+0x238`, writes the 64-byte render-world matrix at `+0x1C0`,
and then calls the same prepare function with the global instance pool and
`mode=1`. Two independent data-table references at `0x142ECCC34` and
`0x142ECCCB4` register this function. These xrefs, retained-owner dataflow and
direct render-buffer writes satisfy the static evidence quota.

Competing explanations are now bounded. S1 predicts that the exact local
inactive rifle reaches `0x14197DC54`, so `gp` left an independent synchronous
preparation path intact. S2 predicts only a pre-publication legacy-Nadine rifle
hit, identifying a one-shot spawn wrapper which can survive after inventory
replacement. S3 predicts no legacy-Nadine rifle hit at this callsite, proving
that `PrepareInstanceForRendering` is merely data preparation and that the next
target must be the later view-specific instance admission/filter.

Build `gq` leaves both preparation paths pass-through and adds a checked E8
relay only at `0x14197DC54`. Its SEH-bounded read-only classifier requires exact
ProcessWeapon vtable, retained/runtime id equality, rifle art, reciprocal
`weapon+0x1A8` FgAnimData, and a parent with legacy-Nadine look. It logs owner,
parent, FgAnimData, mode, published target and `targetMatch`, once before target
publication and once per matched generation. It writes no weapon, FgAnimData,
instance or visibility state.

The real IDB now names `FgDrawManager_PostAnimProcessing 0x14181BA10` and
`AnimFramePlayback_PublishRetainedFgAnim 0x14197D8C0`; headless apply reported
`1257 functions, 20 data names`, `save_database returned True`, and the restarted
MCP resolved both names. The release DLL compiled successfully.

**Falsifiable `gq` prediction:** both copies load marker `2026-07-22gq` and
install the synchronous relay. On role B, S1 emits `action=sync-prepare mode=1
targetMatch=1` for the same weapon/FgAnimData as `skip-prepare`; S2 emits only a
legacy rifle row with `targetMatch=0` before the published generation; S3 emits
no synchronous legacy-rifle row although the normal `skip-prepare` marker still
appears. Because this build is diagnostic and pass-through at the new path, the
spawn rifle is expected to remain visible; crash or a body/pistol regression
invalidates the capture.

### 2026-07-22 RUNTIME: `gq` selects S3; preparation is not final admission

The user again saw the rifle attached to the local Nadine hand. Both copies
loaded `gq` without a crash. Role B emitted 14 exact worker
`action=skip-prepare` rows over successive published generations, but the new
synchronous relay emitted zero `action=sync-prepare` rows in either process.
This is the written S3 outcome: the retained animation-playback path at
`0x14197DC54` does not submit either the current or a pre-publication
legacy-Nadine rifle in this scene. S1 and S2 are REJECTED.

The remaining raw xref at `0x14181C102` is not another live caller. Retail
disassembly identifies a leaf block at `0x14181C0D0` which loads the global
FgInstance pool and tail-jumps to `PrepareInstanceForRendering`, but a complete
retail rel32/address-taker scan finds no call, jump or LEA to that leaf block.
The only reachable direct sites remain the instrumented worker call
`0x14181B925` and the observed-zero synchronous call `0x14197DC54`.

### 2026-07-22 STATIC: final `CullFgLods` queue discriminator (`gr`)

The final queue has now been traced instead of inferred. First,
`FgDrawManager_PrepareInstanceForRendering` increments the producer count at
render-frame queue `+0x30`, stores each admitted `FgInstance*` in the pointer
array at queue `+0x8`, and writes its corresponding 176/224-byte render records.
Second, frame handoff copies the producer count into queue `+0`; the allocation
and frame-begin paths both use the same render context pointer at `+0x10960`.
Third, exact source strings bind `RenderMgr::MainDrawCore 0x141661960` and
`CullFgLods::Spawn 0x141A2C440`: the direct call at `0x141661A57` passes
`queue+0x8`, `queue+0x18`, `queue+0`, Scene and ViewFrustum to the LOD culler.
This supplies independent producer dataflow, frame-lifetime dataflow, and exact
source/callsite identity.

Competing explanations are bounded at that consumer. R1 predicts at least one
exact target LOD pointer in the cull queue despite the worker skip and inactive
bytes, proving a stale or independently republished queue entry. R2 predicts no
exact pointer but another queued instance with the same runtime owner id or one
of the exact rifle `FgGeometry*` values, proving that the current pointer census
missed an alternate instance. R3 predicts zero exact, owner-id and geometry
matches while the rifle remains visible, proving that the visible object is not
coming through this standard `CullFgLods` submission and reopening a separate
view/child-render path rather than another DrawControl write.

Build `gr` keeps the already-visible `gp/go` suppression in place and adds one
checked E8 relay at `0x141661A57`. For at most six consumer calls per published
rifle generation it reads the final queue before calling untouched
`CullFgLods::Spawn`, compares every bounded entry against the exact target LOD
pointers, runtime owner id and resolved geometry pointers, then logs
`NADINE-RIFLE-CULL-QUEUE`. It writes no queue, instance, weapon, transform or
visibility state.

**Falsifiable `gr` prediction:** both copies load marker `2026-07-22gr` and the
CullFgLods relay. Role B emits bounded queue rows. A nonzero `targetMask` selects
R1; `targetMask=0` with nonzero `ownerHits` or `geometryHits` selects R2; all
three zero while the rifle remains in the hand selects R3. Missing role-B rows,
an unreadable/implausible queue, crash, body loss or a new pistol regression
invalidates the capture.

### 2026-07-22 RUNTIME: `gr` selects R2; a second rifle instance is rendered

The user still saw the rifle attached to the local Nadine hand immediately
after spawn. Both copies loaded `2026-07-22gr`, the checked relay at
`0x141661A57` installed, and neither process crashed. In role B every settled
queue sample had `targetMask=0` and `activeMask=0`: the three LOD instances of
the published inactive rifle were absent from the final `CullFgLods` input, so
the existing `go/gp` suppression is working and R1 is REJECTED.

From published generations 3 through 8, however, the same final queue contained
three active instances with `geometryHits=3`, while `ownerHits=0`. The stable
instances were `0x1118D10400`, `0x1118D10680` and `0x1118D10900`; each carried
one of the exact three rifle geometry identities and remained active while the
published target changed between `0x10E0CA1A00` and later weapon objects. This
is the written R2 outcome: the visible spawn rifle is rendered by a distinct
three-LOD `FgInstance` set, not by the weapon instance currently discovered and
suppressed. R3 is REJECTED.

Next static target: trace those alternate LOD instances backward from their
`PrepareInstanceForRendering` producer and resolve their owning renderable or
weapon container. Any next runtime discriminator must match the alternate
geometry set at the producer boundary; further writes to the already-absent
published target are ruled out.

### 2026-07-23 STATIC: reverse alternate FgInstance ownership (`gs`)

The `gr` addresses are three ordinary global-pool `FgInstance` slots: their
spacing is exactly the allocator stride `0x280`. The reverse ownership path is
now static and has three independent evidence types. First,
`FgDrawMgr_AllocateInstance 0x141819F30` claims
`[qword_1467C3480 + index*0x280]` and appends that exact index to the one owning
`FgAnimData+0x210` list, incrementing `+0x20C`. Second,
`FgInstance_Init 0x14192E590` stores `FgGeometry*` at instance `+0x1B0` and
copies `FgAnimData+0x1B0` into instance `+0x250` after validating the retained
pair. Thus the observed alternate value `0x7FF` is an owner identity, not a LOD
or cull flag. Third, exact source identity at
`AnimMgr_AllocateAnimData 0x14177F7F0` proves the global FgAnimData pool is
`qword_1466D0200`, has `0x600` slots of stride `0x380`, and seeds
`+0x1A8/+0x1B0` directly from `Process+0xA8/+0xBC`.

The earlier generic names at `0x14177F7F0/0x1418C4840` are corrected by their
exact source assertions to `AnimMgr_AllocateAnimData` and `FgAnimData_Init`;
`IDrawControl_Construct 0x141751BB0` independently stores that same FgAnimData
at DrawControl `+0` before `IDrawControl_AddMesh` allocates its LOD instances.

Competing outcomes are now bounded. O1: each geometry-matched alternate index
occurs in exactly one live FgAnimData driven list and its retained pair resolves
to one concrete Process; that owner is the actual spawn-rifle container. O2:
the active queued index occurs in no live driven list, proving stale/released
pool publication. O3: an index occurs in multiple lists or resolves an invalid
retained pair, proving ownership aliasing/lifetime corruption rather than a
normal second drawable.

Build `gs` extends only the existing read-only `CullFgLods` discriminator. For
each geometry match it derives the global instance index, scans only allocated
FgAnimData slots, records list position/count, retained handle/id, resolved
owner pointer/vtable/runtime id, optional ProcessWeapon art and matrix equality,
then calls stock `CullFgLods::Spawn` unchanged. It writes no pool, list,
instance, owner, transform or visibility state. The release DLL compiled
successfully. The three exact names were applied to the single `ida/tll.i64`
(`apply_labels OK: 1260 functions, 20 data names`;
`save_database returned True`) and independently resolved through the restarted
MCP.

**Falsifiable `gs` prediction:** both copies load `2026-07-23gs`; role B again
shows `targetMask=0` and three alternate `geometryHits`. Exactly one
`fg/owners=1` plus a valid object whose runtime id equals retained id selects
O1. `fg=0/owners=0` selects O2. `owners>1`, an invalid object, or unequal
retained/runtime ids selects O3. Missing positive controls, a crash, body/pistol
regression or a visibly absent spawn rifle invalidates the capture.

### 2026-07-23 CONFIRMED: visible spawn rifle is stale pool publication (`gs`)

Both copies loaded `2026-07-23gs`; role A was PID 7752 and local-Nadine role B
was PID 2044. The user confirmed the rifle was still attached to Nadine's hand
immediately after spawn and that the existing character/weapon behavior did not
regress, so the read-only capture is valid.

Every role-B sample across generations 13 and 14 reproduced all positive
controls: the tracked current rifle remained absent from the final queue
(`targetMask=0`, `activeMask=0`), while exactly three active geometry-matched
instances remained at global FgInstance pool indices `1000/1001/1002`
(`geometryHits=3`). All three independently resolved `fg=0`, `owners=0`,
`handle=0`, `object=0` in every sample. Their copied instance owner id was
`0x717`, but no allocated FgAnimData driven list contained any of those indices.

This selects O2. The visible spawn rifle is not owned by a second live
FgAnimData/Process at the final cull boundary: released FgInstance slots remain
published in the renderer queue after their sole FgAnimData owner has dropped
them. O1 (one live owner) and O3 (multiple/invalid live owners) are REJECTED.
Further owner lookup or writes against the current Player rifle cannot remove
this artifact. The next static target is the FgAnimData/FgInstance release path:
identify where indices are removed from `FgAnimData+0x210`, where
`qword_1467C3480` slots are released, and which final render queue is supposed
to discard the retired indices.

### 2026-07-23 STATIC: bound stale queue entries to deferred deletion (`gt`)

The release path is now exact and supplies three independent evidence types for
the next read-only discriminator. First, `FgDrawMgr_FreeInstance
0x14181A2A0` immediately clears the slot in both used masks
`0x1467C3080/0x1467C3600`, enqueues `{renderFrame,index}` in
`m_deletedInstanceQueue`, sets pending-delete mask `0x1467C3A00`, sets
`FgInstance+0x254 bit 1`, and then calls
`FgDrawMgr_DetachInstanceFromAnimData 0x14181A5A0`. Second, exact source identity
at `FgDrawMgr_FrameBegin 0x14181A760` waits for the GPU EOP tick from three
render frames earlier, calls `FgInstance_Reset 0x14192F0E0`, and clears that
pending-delete bit. Third, `FgDrawMgr_ForAllUsedInstances 0x14181C1B0`
independently constructs the union of both used masks and explicitly excludes
the pending-delete mask before invoking any callback. The final cull queue
therefore violates the manager's own definition of a usable instance if the
three `gs` matches carry this pending state.

Competing outcomes are bounded. T1: all three geometry matches have both used
bits clear and pending-delete plus `FgInstance+0x254 bit 1` set; this proves
they are deferred-retirement entries and justifies compacting only those paired
`FgInstance*/FgLodData` records at the final cull boundary. T2: a used bit is
still set and pending state is clear; the owner census missed a live detached
instance and cull filtering is not justified. T3: the manager masks and
per-instance retire bit disagree; this indicates lifetime corruption and
requires tracing the writer rather than filtering.

Build `gt` remains read-only and extends each existing final-queue match with
instance state `+0x18`, retire byte `+0x254`, and the three manager-mask bits.
It leaves the queue, paired LOD data, pool and original `CullFgLods::Spawn` call
unchanged.

**Falsifiable `gt` prediction:** both copies load `2026-07-23gt`; local-Nadine
role B reproduces `targetMask=0`, `geometryHits=3`, `owners=0`. Exactly
`used=0/0`, `pending=1` and `retire&2=2` on all three matches selects T1.
`used!=0` with no pending state selects T2. Any disagreement between pending
mask and retire bit selects T3. Crash, missing controls or a visual regression
invalidates the capture.

### 2026-07-23 CONFIRMED: spawn rifle is a live standalone FgInstance set (`gt`)

Both copies loaded `2026-07-23gt`; role A was PID 7040 and local-Nadine role B
was PID 11288. The user confirmed that the spawn rifle remained visible with no
new regression and clarified the visual symptom: Nadine does not hold it in an
animated grip; it looks like an independently glued object.

Role B reproduced every positive control through generations 18-20. The current
Player rifle remained absent (`targetMask=0`, `activeMask=0`), while the same
three geometry-matched pool slots `1000/1001/1002` remained in the final queue
with `owners=0`. All three nevertheless had primary-used membership
`used=1/0`, no pending-delete membership (`pending=0`), and no instance retire
flag (`retire=0`). Their copied owner id was `0x956`, distinct from the current
target runtime id `0x6A4`; slots 1000/1001 carried state `0x21228A38` and slot
1002 carried `0x21228E38`.

This selects T2. The visible spawn rifle is a live, manager-owned standalone
three-LOD FgInstance set which the current FgAnimData census does not own. T1
(deferred retirement) and T3 (mask/retire disagreement) are REJECTED. The
earlier O2 wording that classified these slots as released/stale is superseded:
`owners=0` was correct, but it did not imply release. Do not compact the cull
queue by pending-delete state and do not mutate the current Player rifle.

The next static split is: U1, a direct
`FgDrawMgr_DetachInstanceFromAnimData 0x14181A5A0` caller deliberately leaves a
primary-used standalone instance; U2, `FgDrawMgr_AllocateInstance 0x141819F30`
can allocate without an FgAnimData owner; U3, the `gs` owner census omitted a
second legitimate ownership container. Resolve the attach/detach call graph and
trace the primary-used-bit lifetime before another runtime test.

### 2026-07-23 STATIC: ownerless slot requires an abnormal owner lifecycle (`gu`)

The U1/U2/U3 split is now narrower and meets the static evidence quota for one
more read-only discriminator. First, `FgDrawMgr_AllocateInstance
0x141819F30` asserts that its FgAnimData argument is non-null and unconditionally
appends the allocated index to `FgAnimData+0x210`; U2 is REJECTED. Second, the
only non-free code callers of
`FgDrawMgr_DetachInstanceFromAnimData 0x14181A5A0` are the two compiled
`IDrawControl::NotifyMeshUnloaded` paths. Exact source identity at
`IDrawControl_NotifyMeshUnloaded 0x141753200` proves that path sets
`FgInstance+0x18 bit 1`, stores the pointer in LOD
`m_pFreedInstance +0x8`, and clears current `m_pInstance +0`. The `gt` matches
instead have `state&2=0`, so they are not ordinary parked unloaded LODs.

Third, the symmetric lifecycle independently rules out normal destruction:
`IDrawControl_Relink 0x141753560` creates and attaches a replacement before
freeing `m_pFreedInstance`; the per-mesh release helper `0x141752B30` frees both
LOD pointers; and the all-mesh path `0x141752FB0` is called before
`AnimMgr_FreeAnimData 0x14177FA70` in normal object teardown and
`Player_ChangeLookId 0x1410FE840`. `AnimMgr_FreeAnimData` itself only clears the
FgAnimData pool/category masks and never walks `+0x210`. Therefore a
primary-used, ownerless, non-detached instance must either have lost its
FgAnimData through a bypassed teardown or belong to a live runtime object whose
ownership state is inconsistent.

The remaining discriminator follows the copied owner id `0x956` through the
confirmed game-object handle registry used by
`ResolveGameObjectHandleById 0x140A989C0`, then checks any resolved
ProcessWeapon DrawControl's `0x250`-stride meshes and `0x20`-stride LOD records
for the three exact instance pointers. W1: id `0x956` resolves to a live
ProcessWeapon and the matches occur at LOD `m_pInstance +0`; this selects a live
object whose FgAnimData allocation/ownership state was dropped. W2: the live
object contains the matches only at `m_pFreedInstance +0x8`; this selects
detached-state corruption. W3: the id no longer resolves and no live DrawControl
contains the pointers; this selects orphaned instances from a destroyed or
replaced ProcessWeapon. A reused non-weapon object for `0x956` is a fourth,
explicit invalid-owner-id outcome. No render or object write is justified
before this result.

### 2026-07-23 RUNTIME EVIDENCE / REJECTED METHOD: old rifle id is absent (`gu`)

Both copies loaded `2026-07-23gu` (role A PID 9784, local-Nadine role B PID
5700). When role B reached generation 2, its final cull queue again contained
three geometry-matched instances, pool slots `443/444/445`. They retained
copied owner id `0x727`, were primary-used (`used=1/0`), non-pending and
ownerless in the complete allocated-FgAnimData census. The current inactive
rifle was a different live object, runtime id `0x4CD`.

The authoritative game-object registry lookup for `0x727` returned no handle
or object (`registry=0/0`) for all three entries. This is direct support for W3:
the visible geometry belongs to a destroyed or replaced ProcessWeapon, while
its three FgInstances survived. It also matches the user's independent visual
description that the rifle is not held by the animated hands and instead looks
like a separate object glued to Nadine.

The run is not promoted to CONFIRMED because the local-Nadine copy visually
froze immediately after loading. Calling
`ResolveGameObjectHandleById 0x140A989C0` from the render/cull hook is therefore
REJECTED as a runtime technique even though it returned one complete
discriminator line. Both processes were closed, and build `gv` removes the
registry call and all associated DrawControl traversal. Do not call the
game-object registry from `CullFgLods::Spawn` again.

The next fix must address the leaked owner lifecycle or retire the exact
ownerless FgInstances through a proven FgDrawMgr path. Do not alter the current
rifle (`0x4CD`) and do not hide geometry solely by art id: the positive
criterion must retain the exact conjunction already observed (role B, current
Nadine rifle absent, same three LOD geometries, foreign owner id, no allocated
FgAnimData owner, primary-used and non-pending).

### 2026-07-23 STATIC PROPOSAL / RUNTIME REJECTED: final cull compaction (`gw`)

The freeze in `gu` excludes game-object registry access from the render/cull
hook, but it does not invalidate the already read-only final-queue census. The
next hypothesis is exact: at `RenderMgr_MainDrawCore 0x141661A57`, remove only
the three queue records satisfying the complete stale-rifle conjunction and
compact the paired `FgInstance*` / `FgLodData` arrays before the stock
`CullFgLods_Spawn 0x141A2C440` call. This suppresses publication only; it does
not free a pool slot, call an object method, or write the current rifle.

Three independent static evidence types establish the boundary and its layout:

1. Exact `CullFgLods::Spawn` source identity shows that arguments 3/4/5 are
   `FgInstance**`, `FgLodData*`, and count. It stores those two input pointers
   and count in its staging record at `+0x100/+0x108/+0x110`; the submitted
   worker later consumes them at job offsets `+0x80/+0x88` with count `+0xC8`.
2. The worker `CullFgLods_ExecuteBatch 0x141A2C880` independently consumes
   `instances[index]` at an 8-byte stride and `lodData[index]` at an exact
   `176 (0xB0)` byte stride (`0x141A2C9CF`, `0x141A2C9FE`,
   `0x141A2CA01`, `0x141A2CE6E`). The two arrays are therefore a positional
   pair and must be compacted together.
3. The sole intercepted callsite loads count from `[rbx]`, the instance array
   from `[rbx+8]`, and the LOD array from `[rbx+0x18]`, calls Spawn, then keeps
   only the returned job handle. In-place compaction preserves the lifetime
   required by the asynchronous worker; temporary replacement buffers would
   not.

The positive runtime guard is deliberately stricter than art matching: role B;
a valid current local Nadine rifle; its target and active LOD masks both zero;
exactly three geometry matches; every match different from the target and
carrying the same nonzero foreign owner id; every match absent from all
allocated `FgAnimData+0x210` driven lists; primary-used set, secondary-used and
pending-delete clear; no target-owner hits; three distinct queue indices and
pool instances; each record carries the complete three-LOD geometry mask
`0x7`; and the state family is `2 x 0x21228A38 + 1 x 0x21228E38`. Any
failed clause leaves both arrays and count byte-for-byte unchanged.

Competing hypotheses remain explicit. F1: those three records are the
standalone stale rifle, so paired compaction removes only the glued spawn
rifle. F2: despite the zero-owner census, they are a legitimate remote weapon;
then a correctly rendered Chloe rifle will disappear and the filter is
REJECTED. F3: the arrays are not positional pairs or the stride is wrong; the
worker disassembly and callsite ABI reject this statically, while any cull/body
regression or crash rejects it at runtime.

**Falsifiable `gw` prediction:** both copies load without a freeze; role B logs
one bounded `NADINE-RIFLE-CULL-FILTER removed=3` row for the spawn generation;
the rifle glued to local Nadine's hand is absent immediately after spawn.
Nadine's body, pistol and current rifle state, plus Chloe's legitimate drawn or
holstered weapon, remain visible and functional. Any freeze/crash, removal
count other than three, missing body/weapon, or persistence of the glued rifle
rejects F1.

Runtime selected the rejection. Both copies loaded `2026-07-23gw`; role A was
PID 15636 and local-Nadine role B was PID 9788. The strict guard repeatedly
passed in role B: for owner id `0x743`, generations 5-10 logged exact
`NADINE-RIFLE-CULL-FILTER removed=3 before=956 after=953`. The user nevertheless
confirmed that the rifle remained glued to Nadine and that the Nadine copy
became extremely laggy. F1 is therefore REJECTED: removing this exact triplet
from these Spawn submissions does not remove the visible artifact.

The log also explains the performance failure. The same generation emitted
many `sample=1` rows, proving that the callsite is reached by multiple threads
with independent `thread_local` caches. Each thread repeated the full
FgAnimData owner census; armed workers additionally scanned and compacted the
large paired arrays. This hot multi-thread mutation is REJECTED, regardless of
whether the remaining artifact is submitted by another pass or driven through
a different render boundary.

Both game processes were closed. Build `gx` removes the runtime consequence by
leaving the `CullFgLods` callsite entirely unpatched; it does not merely disable
the compaction branch. Do not add another cull mutation. Continue from the
earlier lifecycle result: trace where the obsolete ProcessWeapon owner is
replaced or destroyed without normal DrawControl/FgInstance teardown.

### 2026-07-23 RUNTIME CORRECTION: the `gw` triplet is live remote-Chloe data

The complete `gw` log supersedes the unsafe `gu` registry inference. The
filtered owner id `0x743` repeatedly resolves through the existing read-only
geometry census to a live exact ProcessWeapon whose parent is remote Chloe
(`look=0x3F334972`), art is the rifle `0x8075397D`, and whose own FgAnimData
has three driven resources. Its ProcessWeapon pointer changes across rebuilds,
while FgAnimData `0x1118AE3D80`, retained/runtime id `0x743`, art, parent and
look remain stable. Those rows occur before, between and after the filtered
queue rows.

Therefore the earlier claim that an absent registry result proves a destroyed
local-Nadine rifle owner is **REJECTED**. Calling the registry from the cull
worker was already unsafe, and its W3 output was misleading. The three
ownerless pool slots can coexist with a newer live triplet for the same remote
weapon, but removing them does not affect the visible local-hand artifact.
They are correlated remote render data, not a sufficient visual cause. The
second direct `CullFgLods_Spawn` wrapper at `0x141A35220` has no code callers
(its only xref is unwind data), so it is not a missed render submission path.
No further cull hook is justified.

The severe lag also exposed dormant rejected work outside `gw`: both
`PrepareInstanceForRendering` callsites were still patched, and the worker path
performed full Nadine/weapon-geometry owner scans even with global diagnostic
logging disabled. The parent-visibility relay likewise still ran the rejected
attachable census and late HideMesh repair. Build `gy` disables those paths,
the rejected same-id inactive-rifle replay, and all old Nadine
holster/transform probes. It retains only the small ProcessCharCloth
parent-visibility observation needed by the confirmed remote hair/cloth
repair. Thus the next run cannot repeat the `gw` cull census or the `gp/fy`
renderer-wide scans.

### 2026-07-23 STATIC: cold ProcessWeapon initialization discriminator (`gy`)

The user's stable observation is more specific than the rejected render
theories: on role B the rifle is already glued to the local hand immediately
after spawn, yet a normal rifle draw/put cycle moves that same weapon to the
thigh. The role-A observer already sees the thigh placement. The next
candidate is therefore the initial attachment descriptor consumed before the
first Player weapon action, not another render admission gate.

Three independent static evidence types establish a cold read-only boundary:

1. `ProcessWeapon_Initialize 0x14150F640` contains one direct E8 at
   `0x14150F6AF` to `ProcessWeaponBase_Init 0x1414FAF40`; the caller immediately
   checks the returned result before doing weapon-specific materialization.
2. `ProcessWeaponBase_Init` reads the descriptor at `context+0x28` and directly
   calls `NdAttachableObject_Init 0x141ECEFE0` at `0x1414FB4C2`. This is the
   concrete ProcessWeapon base-init path, not a virtual render callback.
3. `NdAttachableObject_Init` independently proves the field contract: the
   descriptor parent handle/id at `+0x40/+0x48` feeds object
   `+0x4E0/+0x4E8`; local transform `+0x20..+0x3F` is copied to
   `+0x4F0..+0x50F`; and initial attachment byte `+0x66` reaches
   `NdAttachableObject_SetParentAndAttachmentIndex 0x141ED0190`, which stores
   the selected byte at object `+0x688`. The same init also consumes attachment
   data/id/joint at `+0x50/+0x58/+0x5C` and flags at `+0x63`.

Competing hypotheses are explicit. I1: role-B local Nadine's initial rifle
descriptor selects `rightHand` index 16, while later stable controller state
has already moved the logical ProcessWeapon to index 40 / `gun_holster`; the
main-view artifact is an initialization-era attachment/cache. I2: the
descriptor already selects index 40 and its post-init state is also 40, moving
the cause strictly downstream of initialization. I3: no local rifle reaches
this callsite, selecting a saved/replacement path outside normal
`ProcessWeapon_Initialize`. If both role A and role B initialize identically at
16, the transient is stock behavior and I1 is not specific enough to explain
the Nadine-only artifact.

Build `gy` patches only that one cold E8 and calls stock first. For at most 64
co-op pistol/rifle initializations it records role, art, resolved parent and
look, descriptor attachment fields, initial/post selected byte, and hashes of
the descriptor/post local transform. It deliberately keeps non-Player rows so
an early local initialization cannot be lost merely because the retained-player
table is not committed yet. It performs no attachment, DrawControl, FgAnimData,
pool, registry or cull write.

**Falsifiable `gy` prediction:** both copies load the `2026-07-23gy` marker
without the extreme role-B lag. Before any manual weapon input, role B emits a
`NADINE-WEAPON-INIT` row for rifle art `0x8075397D`, with a valid player parent
and `selected=X->Y`. `16->16` or `16->40` selects an initialization-era
rightHand input (I1, subject to the role-A control); `40->40` selects I2; no
local-player rifle row selects I3. The visible rifle is expected to remain
wrong in this read-only build. Any freeze, crash, body/weapon regression,
renderer-wide log flood, or visible mutation invalidates the probe.

### 2026-07-23 RUNTIME: `gy` selects a missing post-init holster transition

Both copies loaded `gy` and remained responsive through scene materialization:
role A PID 14868 and role B PID 13584. The install log confirms that neither
`CullFgLods` nor either FgDraw prepare callsite was patched, the rejected
holster/transform probes were absent, and only the cold init callsite plus the
independently required cloth relay remained. The process-local init output was
bounded to seven rows on each side; there was no renderer-wide flood.

The discriminator is symmetric and exact. The role-A local Chloe rifle
initialized as:

`art=8075397D parent=retained0 look=3F334972 attachId=61A48E01
selected=255->1`.

The role-B local Nadine rifle initialized as:

`art=8075397D parent=retained0 look=D27C074F attachId=61A48E01
selected=255->16`.

The remote controls repeat the inverse pair: role-B remote Chloe selected
index 1 and role-A remote Nadine selected index 16. Pistol rows follow the same
per-look result. In every row the descriptor and post-init local-transform
hashes are identical (`AE617165`), parent handle/id is preserved, result is
zero, and the requested attachment id is confirmed `rightHand 0x61A48E01`.

I2 and I3 are therefore REJECTED: the local rifle does reach normal
`ProcessWeapon_Initialize`, and it does not begin at `gun_holster` index 40.
The narrow version of I1 (a role-B-only bad init/cache) is also rejected,
because the role-A remote Nadine has the same `255->16` initialization yet is
later displayed at the thigh. The selected result is a missing **post-init**
holster transition on the local role-B Player. Initial weapon creation
correctly resolves `rightHand` to the character-specific index (Chloe 1,
legacy Nadine 16); the remote co-op apply path later holsters its Nadine weapon,
and a manual local draw/put does the same, but local spawn never performs that
later transition.

Do not rewrite the init descriptor or force index 40 inside
`ProcessWeaponBase_Init`: the required compatibility `gun_holster` record is
added later from the retained Chloe donor. The next candidate must reuse the
stock Player weapon state machine only after that record exists, with a
one-shot local-Nadine spawn generation and positive completion at
`current<0, draw=0, selected=40`.
