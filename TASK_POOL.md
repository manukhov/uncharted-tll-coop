# Task Pool

## Primary Objective

Полноценный кооп кампании между двумя копиями игры. Текущий порядок работ:

- локальная Chloe на первой копии и локальная Nadine на второй;
- полное представление удалённого игрока в обеих копиях;
- синхронизация всего состояния и всех действий игроков;
- синхронизация противников только после игроков.

## P0 NEXT: ADS feet and duplicate tracer follow-up (2026-08-02)

- [x] REJECT duplicate-projectile cause under build `2026-08-02e`: automatic
      side tracers persisted with zero owner-1 unmatched projectiles and exact
      ProbeKick direction/origin. Receiver-native versus relayed launch origins
      differ by 0.347--1.957 m (median 0.943 m).
- [x] Separate visual Tracer start from physical collision origin. Exact RTTI
      slot `0x142AA3238` resolves to `Tracer::Init 0x14155BE10`; its copied
      record reads direction `+0x30` and visual origin `+0x70`. Build `f` uses
      exact SpawnProjectile-scoped TLS and restores only `+0x70` during Init;
      physical `+0x20` and the correct impact path remain relayed.
- [x] Run build `2026-08-02f`: the `Tracer::Init +0x70` swap fixes only the
      launches that materialize a Tracer. Of 96 role-B selected applies, bounded
      logs contain 58 exact `init=1/dot=1` matches and 32 exact-scope misses,
      matching the visible roughly half-correct/half-side result. The hypothesis
      that every streak belongs to this Tracer is REJECTED.
- [x] Resolve the apparent no-Tracer half: `ProcessWeapon_SpawnProjectile`
      0x141519B50` submits `ProcessWeapon_MaterializeProjectileJob 0x141519B20`
      and blocks in `Ndjob_WaitForCounter 0x1415B5300`; Tracer Init therefore
      runs on a worker outside the caller's TLS for those launches.
- [x] Run bounded build `2026-08-02g` for one short automatic burst. Classify each
      prior `MISS` by exact Tracer entry/readable-seed/owner/direction counters;
      do not treat accepted-patch count as total Init count again. Prediction:
      the side-streak half reports `entries=0`, matching authored per-emission
      `allow-tracer=0`. Confirmed: 63/63 entries passed owner+direction and all
      32 capped misses had `entries=0`.
- [x] Run one short automatic burst on build `2026-08-02h`. Worker-side
      `source=job` was confirmed, but the added 5 cm copied-origin gate was
      REJECTED: all 32 capped misses passed entry/seed/owner/direction and failed
      only origin, regressing visible lines to the side.
- [x] Run one short automatic burst on build `2026-08-02i`. Both caller-thread
      and worker Tracer candidates now reach the visual-only `+0x70` swap; the
      user confirmed that all Chloe automatic-fire tracers originate correctly.
- [x] Run build `2026-08-02j` in both directions. Nadine role B sent 99 shots
      and Chloe role A received exactly 99; Chloe sent 9 and Nadine received
      exactly 9. Both receiver paths applied owner1 launch direction/origin and
      Tracer visual correction, with no echo send or crash.
- [ ] Only if the worker relay leaves true no-entry side streaks: trace the ribbon/tracer
      child instruction inside retained muzzle SFX at ProcessWeapon
      `+0x998/+0x9A8`, below the shared flash/audio actor boundary.
- [x] Run build `2026-08-02k` once: stand still in ADS and rotate about 90
      degrees left/right. The exact `PlayerCommonAction` vtable `+0x10`
      discriminator observes `OnStart` directly, independent of which
      `PlayerMoveSequencer_UpdatePhase` callsite reached lifecycle 0. Confirm
      sender-only `PlayerUpdatePlayAnimFade` edges for
      `aim-idle-rotate[-cc]`. Result: both responsive PIDs produced zero
      `AIM-STEP-ONSTART` and zero `AIM-STEP-PH0` rows while the local Player
      visibly stepped. F3 is selected: these actions are not the live visible
      foot boundary, so the native builder must not be forced and the rejected
      `0x140E90605/0x140E9060F` selector snapshots must not be retried.
- [x] Trace horizontal turn-in-place inside the runtime-confirmed base state
      `strafe-idle-ap 0x3C347132` / asset graph `anim-player.bin+0x66048`.
      The full graph has no horizontal turn selector; the concrete late owner is
      `MoveLegIkNew` under `CharacterLegIkController`, which updates both per-leg
      target/history blocks, performs ground probes and commits the resulting
      joints.
- [x] Implement build `2026-08-02l` at exact `CharacterLegIkController` vtable
      slot `0x142BBB478`: cache the already-smoothed puppet world quaternion at
      the confirmed phase-`0x40` publication, convert through parent
      `Player+0x120`, stage only `Player+0x140` during the one stock
      `CharacterLegIkController_Update 0x141F9E6F0` call, then restore all 16
      bytes. The DLL builds successfully and the rejected OnStart/phase-zero
      probes are disabled.
- [x] Run one stationary ADS turn on Chloe with build `l`. Prediction: the
      bounded `AIM-LEG-IK-STAGE owner=1 ... restored=1` marker accompanies
      visible remote Nadine foot replanting/steps, while smooth body/weapon yaw,
      locomotion, reload pose, collision and post-ADS heading remain unchanged.
      **REJECTED:** receiver PID 12108 logged `frameAgeMs=0`, `deltaDeg=3.58026`
      and `restored=1`, but the user observed no foot movement and no other
      regression. Build `m` disables the scoped root stage.
- [x] Trace the private ankle-world captures and the
      `MoveLegIkNew+0x3B80/+0x3B90` ground-probe origins and distinguish the
      remaining candidates. Exact `PostAnimUpdate 0x1421043F0`
      captures two ankle JointSet world transforms before the late FgAnim yaw;
      its job passes only private stack copies through `0x142104AB3` into
      `UpdateFeetPlanted`, which composes them with the read-only static contact
      references `+0x3A90/+0x3AB0` and stores the resulting translations at
      `+0x3B80/+0x3B90`. The same call passes those origins to
      `FindFootPlantGround 0x142103E10`; the final controller commit only copies
      valid solved joints. **STATIC REJECTED as sufficient:** those origins feed
      only later GroundModel terrain/contact queries. The authored foot-effect
      with exactly three parameters and
      reference-set-mask admission already ran before the origins were stored,
      so proposed build `n` was removed without a runtime test; build `m` remains.
- [x] Trace the lower step-admission boundary inside
      `MoveLegIkNew_UpdateNextFootsteps 0x142102DF0`: producer and ownership of
      the authored three-parameter `foot-effect`, exact inputs to
      `AnimStateSnapshot_EvaluateReferenceSetMask` at `0x142103A0D`, and all
      downstream readers/validity of per-leg target/history blocks written at
      `0x142103AAE..0x142103ACB` / `0x142103BB4..0x142103BCC`. The effect is an
      authored clip cue; `payload+0x8 == 3` is its parameter count, and
      `+0x2C/+0x30 >= 0` is the only admission-valid gate for both output blocks.
- [x] Reject build `o` probe packaging before gameplay: both self-launched copies
      faulted writing `0x142101F6A` because the two callsites share one code page
      and the installer restored RX after the first write. Build `p` protects,
      patches and restores the one contiguous range; probe semantics are unchanged.
- [x] Run build `p` read-only discriminator. Visible remote feet still did not
      replant, but both processes emitted zero `AIM-FOOT-ADMISSION` rows despite
      successful installation, so H1--H4 remain unclassified. Park the probe;
      do not treat the visible result as evidence about `foot-effect`.
- [x] Restore production logging profile in build `q`: disable the global crash
      observer and compile out resolved AIM layer/registry plus Nadine attachment
      inventories while retaining all confirmed functional relays/repairs. The
      prior run wrote 1,869 diagnostic rows, including 809 full attachment records
      and 16 exception stack dumps.
- [x] Resolve the build-`p` zero-row owner gate. `UpdateNextFootsteps` source
      arg2 is optimized dead: first caller leaves `[AnimControl+0x58]` in RDX,
      second does not set RDX, and `0x142102EDD` overwrites it before any read.
      Prove the real owner chain through controller `+0x8` -> job `+0x40` and
      select sole Character-bearing callsite `0x142104AB3`.
- [x] Run one stationary ADS turn with build `r`. Require lifetime
      `AIM-FOOT-JOB-REACH owner=0/1` before classifying bounded admission rows:
      F1 local admits/puppet differs; F2 local visible step but phases stay `-1`;
      F3 both admit/match and the fault is below admission. Any missing reach,
      pose regression or renewed lag rejects the observer packaging.
      **CONFIRMED F1:** local Chloe PID 17208 admitted in 38/160 rows (30
      `mask=3`), while remote Chloe owner 1 in PID 17052 stayed `mask=0`,
      `phase=-1/-1` in all 26 rows; both owner reach markers fired and neither
      process emitted an exception.
- [x] Trace every first downstream reader and reset/write lifetime for
      `MoveLegIkNew+0x2C/+0x30`, the numeric 0x50-byte target/history records at
      `+0x40/+0x90` and `+0xE0/+0x130`, and their GroundModel consumers. Reject
      unsafe full-record or phase-only candidates and select an exact receiver
      write seam after stock admission but before the first required consumer.
      **STATIC/IMPLEMENTED (`s`):** exact stock-first seam `0x14210202E` is
      immediately before first final phase read `0x142102033`; relay phases,
      recomputed `+0x3BD0` and all four numeric Locators per admitted leg, while
      preserving process-local 16-byte headers and every native GroundModel.
      TLS ownership is bounded by outer Character-bearing job call
      `0x142104AB3`; source capture sequence prevents repeat application.
- [x] Run one stationary ADS left/right turn on Chloe with build `s`, observing
      from Nadine. **REJECTED before mutation:** both responsive processes
      captured local `seq=1 mask=2`, neither emitted APPLY, and remote Chloe
      still slid with fixed feet. This does not test Locator sufficiency.
- [x] Audit the complete capture-to-packet-to-apply timing/dataflow before the
      next build. **STATIC/IMPLEMENTED (`t`):** raw `lea rcx,[rsi+27A0h]`
      confirms the destination gate; complete 800-byte v15 send/receive rejects
      truncation. The 100 ms event was instead overwritten by later zeroed
      latest-only StatePackets before the independently scheduled puppet worker.
      Latch the newest capture through the current strict-ADS session, dedupe by
      source sequence and clear on ADS release.
- [x] Run the single build-`t` stationary ADS turn. **Transport CONFIRMED,
      numeric admission REJECTED:** PID 15252 captured `seq=1 mask=3`, PID 6220
      applied the same event, both stayed responsive without exceptions, but
      remote Chloe still slid with fixed feet.
- [x] Audit the post-`0x14210202E` path statically. Separate missing
      receiver-local record handles/contact references, absent GroundModel/query
      state, and later solve/apply overwrite. Trace exact writes and first reads
      through all three GroundModels, `FindFootPlantGround`, the per-leg solve
      buffers and final joint application before proposing another runtime test.
      Handles have numeric fallback, invalid GroundModel continues through the
      native solve fallback, the aggregate model has no later MoveLeg reader, and
      Update is immediately followed by final commit. **STATIC/IMPLEMENTED (`u`):**
      StatePacket v16 replaces the rejected admission event with only valid
      quaternion rotations from controller `+0x3E30/+0x3E78` after stock Update.
      The puppet applies them by ordinal after its stock Update and before stock
      `CharacterLegIkController_CommitJointTransforms`; receiver joint indices,
      heap pointers, translations and validity storage remain native.
- [x] Run one stationary ADS turn with build `u`. Require bounded sender
      `AIM-LEG-IK-CAPTURE` and receiver `AIM-LEG-IK-APPLY` with identical
      sequence/count/masks. Visible remote feet must replant instead of sliding;
      no pose, locomotion or performance regression is allowed. If APPLY fires
      but sliding remains, test companion numeric translation/final render
      overwrite next and do not retry foot-effect/GroundModel admission.
      **REJECTED:** PID 7180 captured `count=8/8 mask=ff/ff`, PID 13384 applied
      the same complete two-chain payload, but remote Chloe still slid with fixed
      feet. Build `v` disables the rejected shared-vtable hook.
- [x] Trace the pre-IK live pose producer feeding controller
      `+0x3E30/+0x3E78`. `AnimManager_PerformAnimBlend 0x141A92260` passes
      `FgAnimData+0x100` through `0x141A91490`; `0x141A8FCF0` emits opcode 27,
      whose `ProcessAnimCmdListSegment 0x141AB97C0` case raw-copies finalized
      48-byte JointParams rows into that persistent local pose. Reset then seeds
      both nine-joint chains from it; Update entry `0x141F9E6F0` is the exact
      post-Reset/pre-solve boundary. `+0x230/+0x238` are the separate skinning
      buffers, not a double buffer for `+0x100`.
- [x] Run one stationary ADS left/right turn with build `w`. Require matching
      `AIM-LEG-BASE-CAPTURE count=8/8` and `AIM-LEG-BASE-APPLY count=8/8`, visible
      remote foot replants, no pose corruption and no renewed lag. APPLY with
      continued sliding rejects W1 and selects downstream palette/render
      overwrite W4; do not widen to all joints or retry rejected MoveLeg seams.
      **CONFIRMED:** the user sees native-looking remote replants; both PIDs
      produced capture and apply markers with `8/8`, without reported regressions.
- [x] Confirm build `q` performance: PIDs 15444/6220 remained responsive with
      zero exception/AIM/attachment diagnostic rows, and the user confirmed the
      recent gameplay lag is gone with normal behavior.

## P0: Ground weapon world lifecycle and pickup prompt (`hy`, 2026-07-27)

- [x] **`hy` runtime REJECTED:** M30/Mazur replacement in both directions emitted
      no pickup/GiveWeapon/world-lifecycle markers, so the selected scripted
      `GiveWeapon` sender boundary was not reached.
- [x] **`hy` HUD runtime REJECTED:** the defect moved from Nadine to Chloe and no
      SUPPRESS marker appeared; `Player+0x4010` is not a proven physical-screen
      owner and suppress-without-destroy leaves a stale shared record.
- [x] Trace the actually reached ground-weapon replacement boundary, preserving
      the still-STATIC native consume/drop Apply reconstruction for a later test.
- [x] Resolve the WeaponPickup FHUD record key and explicit destroy/expiry path;
      gate by local retained Player/copy role rather than `Player+0x4010`.
- [x] **`hz` STATIC / IMPLEMENTED:** replace the rejected sampled slot-delta
      draft with exact `WeaponInteractControl::OnInteract 0x141505BB0`, source
      kill return `0x150683E`, and old-weapon detach return `0x10A68B1` under one
      TLS transaction; serialize the resulting native consume/drop as `COPW`.
- [x] **`hz` STATIC / IMPLEMENTED:** hook the shared `0x1408AEA00` entry (base
      and both weapon-control vtables), gate by retained Player identity, and
      explicitly destroy remote-only keyed FHUD records before `0x141357D60`.
- [x] **`hz` runtime REJECTED before interaction:** both copies crashed after
      installing the four global entry detours; no ground/HUD transaction marker
      was reached, so the lifecycle model remains STATIC but that packaging must
      not be rerun.
- [x] **`ia` STATIC / IMPLEMENTED:** repackage source kill, old detach and FHUD
      update at exact callsites `0x141506839`, `0x1410A68AC`, `0x141356BF1`, and
      patch both concrete weapon HUD vtable slots `0x1429BF588`, `0x142AA6F70`.
- [x] **`ia` runtime CONFIRMED (boot phase):** fresh A/Chloe PID 15724 and
      B/Nadine PID 21884 remained responsive for 30+ seconds with both exact
      installers active and no crash-observer exception.
- [x] **`ia` interaction runtime REJECTED:** Nadine PID 21884 crashed after the
      interaction-admit marker and before detach/source/send; Chloe's prior HUD
      defect remained and no suppress/destroy marker identified its owner.
- [x] **`ib` STATIC / IMPLEMENTED:** the `ia` trampoline returned with
      `mov rax,target; jmp rax` after copying stock `mov rax,rsp`; stock
      `lea rbp,[rax-0x4F8]` therefore derived its frame from `.text`. Replace the
      EH-bearing entry detour with sole `WeaponInteractControl+0x10` slot
      `0x1429BF4B8`; retain the independently ABI-verified detach/kill callsites.
- [x] **`ib` STATIC / IMPLEMENTED:** the surviving prompt is produced by script
      native `hud-draw-interactable-on-object 0x140328C20`, which zeroes the
      weak key, writes fallback id `record+0x28`, and bypasses both rejected HUD
      slots at exact publish call `0x1403290FD`. Gate only that call by the
      already-confirmed primary physical HUD state and log both key forms.
- [x] **`ib` boot/interaction discriminator:** fresh Chloe PID 8004 and Nadine
      PID 22232 remained
      responsive with both `ib` installers active and no exception/skip marker;
      **interaction PARTIAL CONFIRM / REJECT:** Chloe now completes INTERACT ->
      SOURCE-COMMIT -> SEND, confirming the entry repair, but Nadine deadlocks
      before native Apply in our own `ResolveGameObjectHandleById` preflight.
      `DROP-ENTER` also produced no DROP-COMMIT, and the exact script HUD gate
      emitted no marker. Replace the unsafe receiver registry lookup and resolve
      the true concrete control slot; `0x1429B6958` is RTTI-proven
      `CarryObjectInteractControl` and must not be patched.
- [x] **`ic` STATIC / IMPLEMENTED:** consume an authored ground ProcessWeapon
      only through the retained puppet's generation-validated per-screen
      dispatcher weak entry (exact stable ID first, unique-art fallback), then
      call the same `ProcessKillTree 0x1416A5C20` terminal used by stock
      `0x140BCE150`. Never call the global registry or either native world Apply
      from Player update; the unsafe dropped-spawn branch is parked. The receiver
      pickup-animation fallback is dispatcher-only for the same reason.
- [x] Run the written `ic` test. Both copies survived boot and installed the
      five-site HUD source discriminator. Approaching the M30 identifies its real
      FHUD source without suppression. Picking it up on Chloe must yield one
      `WORLD-PICKUP-CONSUME-DIRECT ... killed=1` on Nadine, no freeze, no stale
      ground model, and natural HUD expiry. A missing unique dispatcher match or
      a killed object which remains visible separates the next receiver branch.
      **Cold boot CONFIRMED:** role-A/Chloe PID 17060 and role-B/Nadine PID 2848
      remain responsive after 30+ seconds with all `ic` relays installed; live
      interaction PARTIAL CONFIRM / REJECT: neither copy freezes, but both
      screen-1-only consumes expire and the old M30 remains. Both senders emit
      DROP-ENTER but flags=0x1/dropped=0. HUD source is conclusively only
      `pickup-control 0x1408AF179` on the primary state; the user reports the HUD
      bug gone.
- [~] **`id` RUNTIME PARTIAL CONFIRMED / REJECTED:** merge all six dispatcher
      lists with pointer dedupe, exact stable-ID priority and unique-art-only
      fallback. Sender accepts the exact old ProcessWeapon only after stock detach
      sets `+0x690 bit 3`, relays its raw art and never calls the campaign-unsafe
      dropped-event ctor/mapper/resolver. For the matching flags=3 transaction,
      receiver SLOT-MIRROR no longer pre-kills the old slot object: stock
      `PlayerWeaponController_GiveWeapon 0x1410A4D40` must detach it and install the
      picked weapon atomically. Keep native registry Apply forbidden. Run the
      written `id` prediction in `COOP_STATUS.md` in both directions. **Cold boot
      CONFIRMED:** role-A/Chloe PID 23768 and role-B/Nadine PID 15392 remained
      responsive for 30+ seconds with no exception/skip/incomplete marker.
      Live replacement confirms raw detached-art relay and the stock receiver
      `GiveWeapon` drop (`gave=1 detached=1 slotReleased=1`): Mazur now appears.
      The old M30 remains and both all-screen consumes expire, rejecting the
      assumed dispatcher-list ownership. Read-only inspection proves the exact
      `pickup-control 0x1408AF179` weak record resolves to the surviving local
      M30 with matching generation, ProcessWeapon vtable and art. Replace only
      the consume candidate source; retain the confirmed sender/drop path.
- [~] **`ie` RUNTIME CONFIRMED / follow-up required:** cache only the exact
      `pickup-control 0x1408AF179` interactable weak/generation key after primary-
      screen, exact-ProcessWeapon, stable-ID and art validation. Revalidate stock
      `weak+0x8` and `object+0xBC` at flags=3 consume; prefer exact stable ID and
      otherwise require one unique same-art candidate observed within 15 seconds.
      The four rejected generic/tail HUD diagnostics are removed. Run the written
      `ie` prediction: M30 candidate marker, `WORLD-PICKUP-CONSUME-HUD ...
      killed=1`, confirmed stock Mazur drop, and no old M30/duplicate/crash.
      **Cold boot CONFIRMED:** role-A/Chloe PID 21532 and role-B/Nadine PID 23908
      loaded `ie`, installed the exact source capture, selected the expected
      roles, and remain responsive with no exception/skip/incomplete marker.
      Repeated live swaps now emit `CONSUME-HUD ... killed=1` and the user
      confirms every picked model disappears. Preserve this path. Remaining:
      stock replacement still waits for the generic 500-ms slot debounce, and
      Chloe retains a separate stale M30 FHUD record beside the new Mazur prompt.
      Scope the next change to the exact pending flags=3 transaction and its
      consumed weak+generation key.
- [x] **`if` mixed runtime result:** fresh PID 16580
      (A/Chloe) and PID 9620 (B/Nadine) loaded the committed DLL, stayed
      responsive and installed the scoped hooks without exceptions.
      **CONFIRMED:** exact pending flags=3 replacements completed in 0-141 ms with
      `gave=1 detached=1 slotReleased=1`; the user confirms the visible delay is
      gone. Preserve the scoped debounce bypass. **REJECTED:** exact consumed-key
      `FHUD_DestroyInteractScript 0x141357CA0` plus the `0x1408AF179` tombstone did
      not clear Chloe's stale M30 prompt; several successful consumes had
      `hudMatched=0`, and no suppress marker appeared.
- [x] **Trace Chloe stale-prompt ownership statically:** follow the active visible
      Flash script and physical slot back to the submitted interact record. Explain
      why the consumed ProcessWeapon weak+generation is absent from the four-slot
      store while the M30 offer remains. Do not change runtime until the new
      hypothesis distinguishes script ownership, cached display state, and a
      different publisher/copy of the record with three independent evidence
      types.
- [x] **`ig` runtime PARTIAL CONFIRM / REJECT:** the evidence quota was met and
      the exact slot-map path was exercised. Later swaps reported
      `hudScanned=1 hudWeakMatched=1 hudMapMatched=1 hudDestroyed=1`, confirming
      that the typed map can identify and destroy an eligible live script. The
      original M30 still failed: Nadine's first pickup produced receiver
      `captured=1 recent=0 live=0 matching=0`, so the finder rejected the sole
      FHUD candidate at its arbitrary 15-second age gate before any live-object
      validation. The user also reported a later crash; logs contain no terminal
      exception or WER record, so its boundary is unresolved and `ig` does not
      pass stability acceptance.
- [x] **`ig` STATIC / IMPLEMENTED basis:** the evidence quota is met.
      `FHUD_UpdateSingleInteractScript 0x141358030` writes exact `interact-id`
      (`ProcessWeapon+0xD8`) and `pickup-weapon-id` (`ProcessWeapon+0x860`) into
      the live slot map at `+0x808`, independently of the copied record at
      `+0x830`. Select the exact live script by both typed keys before the world
      kill, then call stock destroy for that mask. Run the written Chloe
      M30-to-Mazur prediction; preserve the confirmed sub-141-ms stock drop.
      Fresh PID 23320 (A/Chloe) and PID 22516 (B/Nadine) loaded the committed
      build, stayed responsive and installed the scoped hooks without failure.
- [x] **`ih` runtime REJECTED for visible HUD and stability:** the old candidate
      passed every guard and the initial M30 transaction reported
      `killed=1 hudWeakMatched=1 hudMapMatched=1 hudDestroyed=1`, yet Chloe still
      displayed the stale M30 offer. A checkpoint reload removed the offer through
      stock reset. Chloe later crashed without a terminal observer/Windows dump.
      This rejects the selected four-slot record as owner of the visible stale UI
      and makes further explicit FHUD mutation unsafe.
- [x] **`ii` STATIC / IMPLEMENTED rollback:** remove the rejected
      `FHUD_DestroyInteractScript 0x141357CA0` calls and consumed-key publisher
      tombstone entirely. Preserve the exact live weak/generation/vtable/id/art
      search only to kill the duplicate ground ProcessWeapon, plus the confirmed
      sub-141-ms stock dropped-weapon replacement. Log `hudMutation=disabled`.
      **Cold boot CONFIRMED:** fresh PID 2684 (A/Chloe) and PID 4716 (B/Nadine)
      loaded `ii`, selected the expected roles, installed the exact world-weapon
      hooks and remained responsive. Behavioral stability after an initial M30
      pickup remains pending.
- [ ] Before any new HUD patch, trace the checkpoint/reset lifecycle from the
      visible Flash view or script cache back to the state which survives exact
      physical-slot destruction. The next hypothesis must explain
      `hudDestroyed=1` with a visible prompt and why checkpoint reload clears it;
      do not scan or destroy another guessed store at runtime.

## P0: Full Player Synchronization (CURRENT)

- [~] **Grenade direction / duplicate held visual (`hn`, 2026-07-26):** `hm`
      now shows a remote throw animation visually, but the user still sees a
      wrong arc/flight and another grenade during hold. STATIC quota is met.
      Exact resolver-call tracing proves ResolveContext/Solve are insufficient:
      UpdateArc and Release re-read the route quaternion at a 17-return-RVA
      grenade-only whitelist. `hn` publishes the sender seed during hold plus a
      500 ms false-edge grace and overrides quaternion-only for screen 1.
      Separately, the old post-Start Bind order was based on a record-offset
      mistake: slot 0 is `+0x128`, while StartArc clears record 2 at `+0x148`.
      `hn` binds once after the successful scheduler CanStart and before StartArc,
      restoring native order and removing the mid-hold ProcessWeapon presentation
      mutation. Run the written `COOP_STATUS.md` prediction; if the extra visual
      remains, inspect ProcessWeapon vs ProcessGrenadeInHand DrawControl state
      read-only and do not suppress the authored held object.
      **RUNTIME PARTIAL CONFIRM / REJECT:** one hold produced one pre-Start Bind,
      one StartArc and one ammo decrement. Sender active/release seed markers and
      receiver idx-1 active/release overrides all appeared; the user confirms the
      arc is now correct. The extra origin explosion remains. There is still no
      authored remote marker/ProjectileGrenade Init; state 4 returns to state 0
      and GRNE enters its 2.5-second defer, so the delayed receiver explosion is
      the fallback, not a native duplicate. Keep GRNE until P1-P4 identifies why
      StartThrowAction never emits the marker. Next build is bounded read-only
      logging of `+0x540`, cover/net reset sites, dispatch/state 8 and reset RVA.
      **STATIC `ho`:** controller snapshots now include `+0x540/+0x530`; a
      pass-through FastGrenade vtable `+0x60` wrapper classifies exact reset
      return RVAs `E201EB` cover, `E2034B` net gate, `E22333` state-7 timeout.
      Written prediction in `COOP_STATUS.md`; no branch or fallback is changed.
      **RUNTIME REJECTED / PARTIAL:** three state-4 false edges all had
      `mode540=2`, excluding state-7 P1, then returned to state 0 with one ammo
      decrement and no native marker. No `GRENADE-RESET` row appeared, so the
      strict retained-controller pointer re-resolution made the reset probe
      incomplete and P2/P3/P4 remain unresolved. Next use a short false-edge
      release window to admit the raw reset argument and wrap
      `ReleasePostArcHold 0x140E1FF90` for immediate pre/post state; keep the
      test read-only and leave GRNE/pose/projectile behavior untouched.
      **STATIC `hp`:** `E22333` is not state-7-only; state 8 shares that reset
      when its authored action-state lookup is absent. A 1.5-second exact
      false-edge window now logs raw reset arguments for only
      `E201EB/E2034B/E22333`, and a suspended-thread reloc-free prologue hook
      logs `ReleasePostArcHold` pre/post state. Written Q1/Q2/Q3/Q4 prediction is
      in `COOP_STATUS.md`; this build changes no grenade semantics.
      **RUNTIME CONFIRMED / CORRECTED:** two throws entered release at state 4,
      `mode540=2`, `netModeHigh=0` and exited synchronously at state 8 with a
      nonzero result. They then reached state 0 without any `ResetToIdle` hook,
      one ammo unit per throw, no marker and deferred GRNE. Static re-audit
      proves the successful live-action route is state `8 -> 9` at `E223DB`,
      then a direct state-0 store at `E22475`; the old Q3 requirement for
      `E22333` was wrong. Cover/net abort and missing-action reset are rejected.
      **STATIC CORRECTION:** the proposed marker probe is rejected before
      runtime. `0x1404885C0` is RTTI-proven
      `BehaviorThrowGrenade::Throw` vtable `+0x28`, reached through NPC/AI
      state tables and followed by `ai-enemy-combat-projectile-tell`;
      `0x14040CEF0/0x14040D852` is its AI-only projectile continuation. The
      Player state-8 path instead reaches FastGrenade vtable `+0x140` /
      `PlayerGrenadeController_Release 0x140E1B900`, whose sole
      `0x140E1C3A0` materializer creates `ProjectileThrowable` or ordinary
      `ProjectileGrenade` and sends the native event. Existing Init
      classification is armed only around the wrong AI callsite, so a native
      retained-Player ProjectileGrenade can fall into sender capture, echo a
      GRNE and fail to suppress the original deferred event. Next build logs
      native Init plus 1.2 seconds of pos/vel only during the exact peer release
      window, and logs delayed GRNE execution; it changes no classification or
      projectile/fallback semantics.
      **VISUAL CORRECTION:** after the throw the user sees only the grenade
      warning/HUD icon directly ahead, on both copies; no second physical model
      is visible. Do not use the icon as evidence of a duplicate projectile or
      held object, and trace the HUD/tell path separately.
      **IMPLEMENTED `hq`:** during the exact retained-Player release window,
      log every non-fallback `ProjectileGrenade::Init` and sample that object's
      position/velocity for at most 1.2 seconds; also expose the delayed
      `GRENADE-EVENT-EXEC`. This is a bounded read-only R1-R4 discriminator and
      intentionally leaves capture, GRNE echo/suppression, physics, fuse and
      HUD behavior unchanged. Test exactly one throw after both copies report
      the `2026-07-26hq` build tag.
      **RUNTIME CONFIRMED R1:** receiver native Init appeared during Release,
      then moved with the correct launch velocity, gravity and collision. It
      nevertheless fell into sender capture, echoed GRNE back to the thrower's
      copy (immediate custom Execute), while the original GRNE later executed
      on the receiver after 2.5 seconds. Those two zero-native-velocity
      fallbacks explain the icon/explosion on both copies. Next scope FastGrenade
      vtable `+0x140` around retained Player Release, classify only the Init
      reached synchronously inside it as peer-native, skip sender capture and
      positively suppress the original inbound GRNE. Do not use the broad time
      window as the semantic gate because simultaneous throws must remain safe.
      **IMPLEMENTED `hr`:** pass-through FastGrenade `+0x140` wrapper opens a
      thread-local scope only when controller `owner@+0x108` is retained Player
      slot 1. Native Init inside it sets the positive native timestamp and exits
      before sender capture, preventing the reverse echo; the original inbound
      GRNE then takes the existing `GRENADE-EVENT-SUPPRESS` path. No projectile,
      fuse, pose or HUD fields are changed. One-throw prediction and negative
      `GRENADE-EVENT-EXEC` condition are recorded in `COOP_STATUS.md`.
      **RUNTIME CONFIRMED / CLOSED:** repeated throws in both directions produced
      native OWNED -> SUPPRESS ordering, moving collision-valid tracks and zero
      custom `GRENADE-EVENT-EXEC`; the user confirms the face-level warning icon
      and origin explosion are gone.

- [x] **Traversal wall-hang lifecycle (CLOSED, 2026-07-27):** preserve the
      confirmed native locomotion and collision baseline. **UPWARD RUNTIME
      CONFIRMED (`hs`):** the user confirms the remote now completes ordinary
      climb-up; PID 8320 SEND and PID 21184 FORCE markers prove the exact
      `edge-jump-a` boundary. Static basis retained:
      ordinary `PlayerEdgeController` slots 18/19 query exact authored
      `edge-jump-a` with `edge=0`; slot 57 `0x141315B00` creates/enqueues the
      ledge/climb-up `PlayerCommonAction`; independent
      `PlayerEdgeClimb2AnimAction::Update 0x1412C9000` queries the same command.
      `PlayerEdgeState_RefreshTraversalProbes 0x14132D340` independently writes
      `Edge+0x48/+0x49`, which `PlayerEdgeState_TryTransitions 0x141334B80`
      consumes before calling climb slots 57/59; its local-input comparison is a
      probe-path selector, not a safe bypass target.
      Current sender/hook carried only generic jump/jump-hold, while raw face A
      is forbidden by the confirmed jump->evade bleed. **REMAINING DOWNWARD CASE:**
      sender lets go but remote remains hanging. **STATIC QUOTA MET / IMPLEMENTED
      `ht`:** `PlayerEdgeState_HandleDropCommand 0x1413330C0` is called before
      general transitions and queries exact `drop`, edge=0 at `0x141333301`,
      `0x141333463` and `0x14133405C`; its successful branch publishes the
      release state, consumes `drop`, calls `PlayerEdgeState_StartFallFromEdge
      0x14133D800`, and returns handled. That helper constructs/enqueues the
      native `PlayerFallMoveUpdate`/`PlayerCommonAction`; controller slot 166
      `0x14131F560` dispatches `climb-release-edge` and publishes
      `player-left-edge`; virtual `0x141324450` publishes `player-release-edge`.
      `drop-hold` slot 23 is only an auxiliary competing-transition gate and is
      explicitly not relayed. A dedicated protocol cue captures/forces only
      exact `drop` on Joypad 3 with bounded SEND/FORCE markers. Test one ordinary
      grab-and-let-go: native fall wins T1; FORCE while hanging selects downstream
      edge context; fall then reattach selects later state/reconciliation; no
      FORCE selects packet publication. **RUNTIME PARTIAL (`ht`):** the exact
      command sometimes releases the remote Player, but one of four logged
      sender rises vanished before the receiver FORCE hook while later attempts
      still reached it. The static packet/update audit proves the protocol-only
      bit was overwritten by the following 60-Hz release packet even though
      native pressed edges were accumulated; the exact hook does not consume
      that native mask. **IMPLEMENTED `hu`:** receipt of exact `drop`
      held/pressed now arms a receiver-only 400 ms deadline used solely by the
      exact `drop`, `edge=0` hook. `drop-hold`, crouch and raw face buttons remain
      untouched. Test five grab-and-let-go cycles: every received event must log
      LATCH then FORCE (fast-release rows may show `force=0 ... latched=1`) and
      every remote fall must complete exactly once, with no spontaneous fall
      after the deadline. **RUNTIME PARTIAL (`hu`):** user saw 7/10, but all ten
      distinct sender actions reached receiver LATCH (seq 610..3535), eliminating
      packet loss as the residual cause. Static re-audit found the missing third
      consumer: `PlayerEdgeState_HandleDropCommand` calls
      `PlayerInputCommandState_IsCommandActive` at `0x14133405C` after hashing
      the same `drop` and setting `edge=0`; the two earlier queries use Joypad,
      while the production global PlayerInput hook is parked. **IMPLEMENTED
      `hv`:** one target-validated E8 relay forces only this exact third call for
      retained Player slot 1 during the live drop deadline and logs one
      STATE-FORCE per reached event. Test ten numbered releases. 10/10 closes
      the lifecycle; a failed number with STATE-FORCE selects downstream context,
      while failure without it selects a pre-third handler branch. **RUNTIME
      PARTIAL (`hv`):** four-or-five attempts produced two visual failures but
      only two sender DROP-SEND events; both published events reached receiver
      LATCH and both Joypad FORCE queries, while neither took the third state
      branch. The residual loss is therefore before publication, not at the
      new consumer. **IMPLEMENTED `hw`:** the sender now captures a true local
      exact-drop result at the two confirmed PlayerEdge Joypad return RVAs or
      the third PlayerInput callsite and holds only the protocol event for 250
      ms. This bridges the one-frame native command across the 60-Hz sender
      sampler without broad physical-button capture. Test ten numbered releases;
      each must log LOCAL-CAPTURE -> SEND -> LATCH and fall once. **RUNTIME
      CONFIRMED / CLOSED:** user reports 10/10 as Chloe and 10/10 as Nadine.
      Logs contain two ten-event bidirectional LOCAL-CAPTURE -> SEND -> LATCH
      chains (receiver sequences 891..1897 and 2184..3112), with both copies
      responsive. Ordinary climb-up and deliberate wall-hang release are now
      closed.

- [x] **Ordinary evade / running-roll synchronization (`hx`, CLOSED 2026-07-27):**
      STATIC quota met. `PlayerMoveStick_MainUpdate 0x140F03500` consumes exact
      `evade-start` through `PlayerEvadeController` slot 10
      (`0x140DC5670`) at `0x140F06408`, then requires slot 14 eligibility
      (`0x140DC5F50`) and slot 15 `VehicleCheck` (`0x140DC5C10`) before calling
      accepted action slot 16 (`0x140DC67D0`) at `0x140F0656A`. Slot 16 selects
      authored roll variants and enqueues `PlayerCommonAction`; competing slot
      13 hashes `evade-roll` but is not called by the ordinary path. The old
      60-Hz raw query capture and generic forcing of both hashes are removed.
      Sender now latches only an actually accepted local slot-16 action;
      receiver forces only the exact slot-10 `PlayerMoveStick` callsite and
      consumes the packet sequence on remote slot 16. **PENDING RUNTIME:** ten
      numbered running rolls as Chloe and ten as Nadine must yield 20/20 exact
      LOCAL-CAPTURE -> SEND -> LATCH -> FORCE -> REMOTE-START chains, with no
      remote-only roll and no jump/crouch bleed. FORCE without REMOTE-START
      selects divergent remote gates; REMOTE-START without visible motion
      selects later locomotion correction. **RUNTIME CONFIRMED:** user reports
      successful bidirectional rolls; logs contain 14 complete Chloe-to-Nadine
      and 13 complete Nadine-to-Chloe LATCH -> exact FORCE -> REMOTE-START
      chains, with both copies responsive and no received event lacking the
      accepted remote slot-16 action.

- [~] **Melee animation stage exam (build cw, 2026-07-15):** cv also produced no
      remote animation, so command-family selection alone is insufficient. cw
      emits only one-shot stages at Joypad square force, controller `+0x210`, and
      sole TryAnim callsite `0x140F07A0A`. One tap classifies the remaining gap
      without bypassing or manually invoking the action path.

- [x] **Restore remote Player creation (build ct, 2026-07-15):** cp wrongly
      disabled the sole descriptor capture that writes `g_spawn_captured`; the
      live-peer slot-1 spawn gate therefore could never open despite healthy UDP.
      ct restored the one-shot capture with heavy descriptor scanning off; user
      confirmed both visible peers returned. The optional flag is now removed so
      production cannot disable this functional prerequisite again.

- [x] **Build cr network discriminator (2026-07-15):** fresh PIDs 15996/14588
      selected A:31338/B:31339, both sent 456-byte sequence-1 packets, and both
      accepted the peer packet at the same position. UDP and snapshot publication
      are confirmed. If the peer is visually missing, trace snapshot freshness
      and puppet apply without changing role selection. Crash observer is parked.

- [x] **Melee R1 callsite relay (build cq/cu, 2026-07-15):** static quota met,
      runtime rejected R1 sufficiency: stable visible peers, no melee animation.
      Natural owner tick `0x141279DF0` calls dispatcher `0x141272A50`; exact
      `melee-r1` E8 `0x141273AE2` feeds native queue insert `0x141273FA0`, and
      vtable `+0x220` independently queries literal `melee-r1` through `+0x1F8`.
      R1 then enters `player-r1-attacks` and local target rating, so cv moves
      remote consumption to the independent square path. Global PlayerInput
      detour and manual dispatcher/queue/anim paths remain parked.

- [x] **Melee square sender/consumer relay (build cv-cy, 2026-07-15):** cv was
      stable but invisible. cw then proved one free attack never reached even the
      receiver square-force stage, rejecting lmouse/R1-only local capture. cx
      captured original-true `melee-square` on retained slot 0's live Joypad from
      `Player+0x39B8`; sender capture and receiver queue `+0x210` were observed and
      the user confirmed visible melee. cy removes all temporary stage probes.

- [~] **Grenade native-physics and throw-animation sync (CURRENT, 2026-07-23):**
      build `hc` confirmed that restored `GRNK` ownership reproduces the sender's
      visible arc, but rejected per-frame `set_position=true`: the remote grenade
      passes through walls and ground because those writes bypass collision. The
      generic relayed `0x100/0x400` input also produced no remote throw animation.
      The transform-pinning experiment is parked. Static quota for `he` is met:
      `EmitProjectileProcess 0x14154AAE0` stores the local launch vector at shared
      descriptor `+0x50`, while `NetEventEmitGrenade_Execute 0x140BE63A0` reaches
      that same descriptor/materializer without filling it. The exact authored
      input is `grenade-arc` hash `0x45ABCE38`; its release starts
      `grenade-starting-throw`, and marker callsite `0x14040D852` emits the
      projectile. Build `he` seeds descriptor `+0x50`, relays the exact command
      boolean in StatePacket v11, and suppresses only the puppet marker's duplicate
      projectile. **RUNTIME REJECTED:** all `grenade-arc` transitions and non-zero
      descriptor writes were reached, but no authored marker followed and the
      grenade remained at spawn. Therefore both proposed boundaries are real but
      insufficient. Static correction `hf`: descriptor `+0x50` is consumed as a
      launch-relative spawn transform, not NetGrenade rigid velocity. The global
      Joypad force also entered FastGrenade `+0xA0`, set inhibit bit
      `controller+0x1A0 bit2`, and could make `+0xA8 CanStart` reject its own
      action. `hf` relays `grenade-arc` only at scheduler/state1/state2 direct
      callsites, logs CanStart/Start, lets the authored marker create an ordinary
      local `ProjectileGrenade`, and suppresses the custom inbound GRNE only after
      positively observing that native Init. The local selector then owns
      ActiveUpdate, `m_velocity@+0x5D0`, collision/bounce and natural fuse. Build
      `hf` was RUNTIME REJECTED before gameplay: both copies exited inside the
      three-callsite installer, before its success marker or the later vtable
      probe. Retail E8 bytes/targets and relay range are valid; the leading cause
      is a torn hot rel32 write while another game thread executes the per-frame
      scheduler/state call. Next build must suspend the other process threads
      across the atomic patch window or use one correctly relocated common
      detour; do not rerun the same live E8 writer. Build `hg` now performs all
      validation/allocation first, then requires every other process thread to
      be suspended only while the three sites are protected/patched/flushed,
      restores protection and resumes all handles on every exit. Its boot-stage
      prediction was RUNTIME REJECTED: both copies validated all sites and
      allocated the near thunk, then exited before the relay-installed line.
      This rejects torn rel32 writes. Build `hh` changes no grenade semantics:
      it enables the already-early crash observer, publishes the original
      target before any suspend/patch, and records lock-free stages 1..11 plus
      first-wrapper entry/return RVA. Its written discriminator is ready for a
      clean two-copy injection without grenade input. **RUNTIME CONFIRMED:**
      PID 4168/8272 both stayed alive, installed the scoped callsite relay,
      FastGrenade vtable probe, native marker/Init hooks and role A/B networking
      without a VEH exception. The original-target publication race is the
      leading `hg` crash cause, though timing instrumentation prevents calling
      it unique. **RUNTIME REJECTED behavior:** the puppet reaches scheduler,
      CanStart and StartArc state 1, but release returns directly to state 0;
      no authored marker/Init follows. The custom GRNE then waits 2.5 seconds,
      starts a fresh 3-second fuse and remains at its spawn point because the
      fallback applies no relayed velocity. Static decompilation selects the
      release gate `sub_140E20850`: FastGrenade vtable `+0xD0`
      (`sub_140E1AD40`) must resolve the selected pointer/handle record at
      controller `+0x128+slot*0x10` and its object virtual `+0x840` must report
      a positive count. **STATIC RESOLVED:** constructors zero all six records;
      loadout path `0x140E17070` creates a grenade `ProcessWeapon` and
      `PlayerGrenadeController_BindGrenadeWeaponSlot 0x140E17720` copies its
      weak handle into the registry-selected left/right slot. `+0x840` is
      `ProcessWeapon_GetTotalAmmo 0x141103850` over `weapon+0x900/+0x902`.
      **RUNTIME CONFIRMED `hi`: active-record K1.** Local Nadine slot 0 is a valid
      ProcessWeapon with ammo `3+0`; retained puppet Nadine reaches state 1
      with logged records 0/1 entirely zero. K2 is rejected for the active
      record; K3 remains until records 2..5 are inspected. Read-only `hj` logs
      all six plus ProcessWeapon art id. All invalid selects a missing puppet
      loadout/factory result; another valid record selects the native slot
      setter. **RUNTIME CONFIRMED:** opposite-direction Chloe throw showed local
      slot 0 ProcessWeapon art `0x0B8BD6D6`, ammo `3+0`, while every puppet
      record 0..5 remained zero through state 1. K3 is rejected; prior Nadine
      and current Chloe results prove symmetric K1. **STATIC `hk`:** StatePacket
      v12 relays only selected art/slot/total. After successful stock StartArc
      (which clears `controller+0x148` before writing state 1), the receiver
      reuses an exact-art ProcessWeapon found through controller vtable `+0xB0`
      or creates exactly one through native `+0x130` only when manager slot 2 is
      empty, then binds through `0x140E17720`. GiveWeapon, forged cross-process
      handles and occupied-slot replacement are rejected. Test the written
      L1/L2/L3/L4 prediction. **RUNTIME PARTIAL CONFIRM / REJECT:** the receiver
      created and bound one exact ProcessWeapon, later reused that same object,
      and never duplicated the factory result. However, one sender hold
      (`active=1` seq 43191 through `active=0` seq 43329) produced four receiver
      StartArc cycles (seq 43191/43222/43251/43281) and consumed ammo 3 -> 0,
      matching the user's extra grenades/explosions. Sender chatter and duplicate
      creation are rejected. Static scheduler `0x1410E78E6` is a per-frame level
      query, so the next build must consume the rising edge once at scheduler
      entry after successful state 1 while continuing to expose the held level
      to state 1/state 2 until the actual release. First prove exactly one
      StartArc per long hold; trajectory and the later authored marker remain
      separate downstream outcomes. **STATIC `hl`:** a new per-network-press
      latch is cleared only on the true rising edge and consumed after original
      StartArc reaches state 1. Only the scheduler callsite is then suppressed
      for the remainder of that hold; state 1/state 2 retain the peer level
      through the real falling edge. Runtime prediction: one rise/fall, one
      `GRENADE-SCHEDULER-CONSUME`, one StartArc and at most one ammo decrement,
      with no repeated held grenades before release. A single remaining wrong
      trajectory is deliberately a separate downstream result. **RUNTIME
      PARTIAL CONFIRM / REJECT:** `hl` now gives exactly one StartArc and one
      ammo decrement per network cycle, creates once and reuses thereafter.
      But the receiver reaches state 2, returns to state 0 before the real
      sender false, never reaches the authored marker, and the deferred GRNE
      then materializes the separate exploding grenade the user sees. Static
      correction: state-2 deadline calls `0x140E1F5B0`, whose ordinary path
      writes state 4; state 4 has a fourth exact `grenade-arc` query at
      `0x140E2227D` that `hl` omitted. Its unrelayed false prematurely calls
      `0x140E1FF90` and enters release/state 7. Next build must relay this
      fourth site with the held level while leaving the scheduler consumed and
      leaving GRNE/trajectory untouched. **STATIC `hm`:** the fourth exact E8
      is now part of the same suspended cold-install transaction and is
      classified as `state4`; only scheduler uses the consumed latch. Written
      prediction: one StartArc, state1=1, state2=1, state4=1 while held,
      state4=0 only on the real release, then one authored marker/native Init
      and GRNE suppression. Missing state4 selects either the timer branch or
      the pre-query current-action predicate `0x140E24ED0`; correct state4
      edges without a marker select state7/action context. Trajectory is not
      changed. The visible pre-fallback object is independently confirmed as
      the expected record-3 `ProcessGrenadeInHand` written at `0x140E1A1D3`,
      with lifecycle kills at `0x140E1A803/0x140E1AABC`; do not suppress it.
      A true native duplicate requires authored marker/Init plus a later GRNE
      Init for the same event, which `hl` did not show. **RUNTIME PARTIAL
      CONFIRM / REJECT `hm`:** state4=1 is reached while held and state4=0
      appears exactly at the real falling edge; one StartArc and one ammo
      decrement remain correct. The controller still resets to state0 with no
      authored marker/Init, and GRNE defers. Therefore the next build is
      bounded/log-only across `0x140E1FF90 +0x540`, StartThrowAction cover/net
      resets, dispatch/state8 and common reset; do not force a branch yet. The
      visible trajectory is also completely wrong. Static leading cause is
      receiver-local screen/camera/pose input to SolveTrajectory; distinguish
      that from wrong owner route or a separate HUD trajectory record before
      extending StatePacket. Never restore collision-bypassing transform
      pinning.

- [~] **Breakable / destructible env relay (2026-07-11):** STATIC map + mod v1 landed.
      Campaign grenade fence uses `SimpleBreakable_ExplosionDamage` `0x88E6F0` /
      post-site `0x88DC83`, not MP `SendNetEventBreakableAttack*` (no `BRK-MASK` in
      campaign). Relay: deferred `SendNetEventSyncBreakable` → `BRKS` v2
      (`scene_hash`, `world_pos`) → peer `NetEventSyncBreakable::Execute` `0xA65160`
      + resolver override. **REJECTED runtime:** all hooks install after REX prologue
      fix, but zero `BRK-*` log lines and peer wall stays intact. Next: confirm hook
      fire in-game or pivot to `sub_140A657A0` / Havok break path.

- [~] **Puppet interact promotion Exp5c:** the four direct dispatcher transitions
      are patched through a near relay + ordinary ABI wrapper. This preserves the
      frame-from-entry-rsp function and logs dispatcher screen index. Next runtime
      result: screen 1 means the gate is inside dispatcher state; screen 0 only
      means the puppet's owning decision controller is skipped above it.

- [ ] Получить устойчивый объект удалённого игрока в обеих копиях:
      Nadine на стороне Chloe и Chloe на стороне Nadine.
- [x] Чисто проверить второй retained `Player` без ввода слота 1, второй камеры и
      split login: прицеливание Chloe работает, само наличие Player не является
      оружейным запретом.
- [x] Устранить гонку двух Player в локальной политике геймпада. Подтверждено:
      общий `joypad-2` уходит в `free_count=-1/-2` и падает в `0x141839FB0`.
      Сериализация оригинальной функции выдержала повторную загрузку и более двух
      минут работы; счётчики пула остались корректными.
- [ ] Отделить удалённый `Player` от локального ввода, камеры и проверки числа
      локальных игроков, сохранив его модель, скелет и граф действий.
- [x] Разделить камеры в проверке двух копий на одном ПК: фоновая копия больше не
      читает общий XInput-геймпад. Для двух отдельных ПК фильтр не применяется.
- [x] Устранить ложную смерть при переключении окон: удалённый `Player`
      удерживается на последнем подтверждённом положении и здоровье, пока фоновая
      копия приостановила пакеты. Повторное переключение прошло без смерти.
- [ ] Заменить текущий `NadineBuddy + MoveTo` на непосредственное применение
      состояния удалённого игрока.
- [ ] Снимки: положение, поворот, скорость, режим движения, направление взгляда
      и прицела, текущее оружие, здоровье.
- [~] **Fire relay (v7):** native `PlayerFireController_Fire` replay on puppet when
      `puppet_native_fire_allowed`; sustained `0x800` never from StatePacket (FirePacket
      only). Load guards: GiveWeapon vtable `+0x158` + 5 s warmup.
- [~] **Aim sync:** default-on pose-only relay REJECTED 2026-07-09: strong running
      jitter with continuous `AIM-TARGET-WRITE`. Keep `g_puppet_aim_reticle_relay`
      OFF by default; Numpad `/` is hard-disabled after accidental ON reproduced
      the turn/run jitter. Puppet full ADS remains OFF / manual `F8`. Do not
      re-enable broad screen-1 pose-seed/source writes; that path
      freezes locomotion facing.
- [x] **Puppet melee sync — PARKED (2026-07-15, baseline co):** Entire relay OFF
      (`g_enable_puppet_melee_button_relay=false`, exec false). **CONFIRMED:** cn
      broke weapon give via `PlayerInputCommandState IsCommandActive` hook; **co**
      restores guns (user). cg button layer was CONFIRMED but anim never shipped;
      cm/ck/ch REJECTED (crash/hang). Do not re-enable without alternative to
      global `0x183ABF0` patch. Static anchors kept: queue `0x141273FA0` @
      `controller+0x150`, TryAnim `0x1410FC9F0`, net gate `g_NetModeFlags+3`.
- [~] **Production performance baseline (build cp, runtime pending):** fixed the
      Numpad7 permanent logging enable; compile-time parked force logs, hot
      input/Joypad/weapon diagnostics, spawn DR0 capture, and pass-through
      GunMoveShield probes. Cloth repair retains a cheap vtable/parent heartbeat
      without DrawControl mesh scans. Test two fresh copies before reopening
      melee; see `reports/runtime_hot_path_audit_20260715.md`.
- [~] **Puppet ADS visual aim (PARKED 2026-07-14):** Upstream CONFIRMED; downstream
      eval/render gap. **REJECTED:** palette/skinning/pubState/PerJoint workspace paths.
      **cd:** stable hook, user visual unchanged. Inject OFF in `ce`.
      Reports: `player_aim_palette_render_chain_20260714.md`,
      `player_aim_workspace_static_20260714.md`.
- [~] **Head / camera look sync:** direct `PlayerLookAtIkController` SetLookPoint
      remains REJECTED because it was inactive for the fault and forced spine
      twist. Build `x` also reached the final StrafeIK Locator boundary in both
      directions but was visually unchanged. Next static owner is the independent
      `controller+0x300` Vector or a later head/neck render writer.
- [ ] **Puppet controller model (NEW):** STATIC map in
      `reports/player_decision_node_controllers.md`. Nearby-list writer DONE;
      decision-node scheduler DONE (`0x14042D120` / `0x1404223F0` / `0x140CD3760`).
      Next: `player->vtable+1512` + `player+0x1808` selection per slot +
      decision-node scheduler for screen 1. Unblocks pickup gesture / aim-pitch /
      interact SM — not per-feature replay.
- [~] Положение, поворот, скорость и здоровье уже передаются. Сырые данные по
      цепочке `Player+0x1A8 -> +0x10` проверены и отвергнуты: это 32-байтовые
      записи эффектов с межпроцессными указателями, не кости персонажа.
- [ ] После восстановления движения: в одной копии игры получить 4096-битный поток команд
      анимации локального игрока через `0x140BC4E60`, разобрать его штатным
      путём и подать в `NetAnimCmdGenerator` клона по `Player+0x1B30`.
- [x] Найти настоящий источник выстрела кампании:
      `PlayerFireController::Fire = 0x140E2C730`, владелец в
      `controller+0x108`, оружие передаётся отдельным аргументом. Сетевое событие
      пули `0x140A46D70` в кампании не вызывается.
- [~] Здоровье игрока добавлено в `StatePacket` v3. Исправлена начальная ложная
      смерть удалённого `Player`: его HealthSystem создавался с `0/100`, теперь
      перед включением ввода получает фактическое здоровье второй копии. Текущее
      здоровье Player находится в `HealthSystem+0x38` (не enemy-смещение `+0x3C`).
      При alt-tab последнее подтверждённое здоровье сохраняется после истечения
      свежести пакета; действия удалённого игрока при этом обнуляются. Дополнительно
      клон замораживается на последнем положении, а четыре локальных входа смерти
      `Player +0xBE8..+0xC00` блокируются, пока настоящий игрок сообщает HP > 0.
- [~] Надёжные события: выстрел, перезарядка, смена оружия, ближний бой, прыжок,
      уклонение, взаимодействие, получение урона, смерть и возрождение.
      СТАТИКА 2026-07-05 (реестр «Reliable-events map»): все кнопочные действия
      резолвятся из масок Joypad `+0xD0/+0xD8/+0xE0` (дерив из `block+0x518`), т.е.
      прыжок/уклонение/перезарядка/ближний бой/смена оружия/взаимодействие приходят
      БЕСПЛАТНО как ввод (нативное действие+анимация в каждой копии). Отдельного
      кода на событие не нужно. Остаётся согласовать побочные эффекты: направление
      (pose-seed v8), какое оружие/боезапас (held-weapon relay + pre-give), цель
      ближнего боя/взаимодействия (фаза врагов + позиция + возможный InteractEvent).
      Урон/смерть/возрождение — HP уже в StatePacket v3 (см. выше).
- [~] Гранаты (план готов, статика 2026-07-05, реестр «Grenade sync»): направление
      дуги/броска = тот же per-screen pose seed, что у пуль
      (`PlayerGrenadeController_UpdateAimArcAndToss 0x140E17AC0`, 10× выборка по
      `owner+0x4010`) → общий фикс: StatePacket v8 c 32-байтным сидом
      (`*(CameraManager(0)+0xB0)+0x60`) + хук
      `ResolveScreenRoutePoseSeedOrDefault 0x141C077B0` (override при idx==1).
      Кнопка броска уже релеится (биты 0x100/0x400 не срезаются). Остаётся:
      боезапас гранат у Второго игрока (pre-give / Infinite-Grenades флаг для
      теста), проверка `CheckActionExclusions 0x140E2ADF0` (глотает authored
      Grenade в части состояний), возможная утечка HUD-дуги
      (`general/hud/grenade-arc`) — гейтить по `state+0xF8` как ретикл.
- [~] Выстрел: UDP `FirePacket` + штатный импульс кнопок. Прежняя неверная маска
      `0x90000` доходила до Joypad 3, но не соответствовала командам огня. Исправлена
      асинхронная смена оружия: после `requested=1 attached=0` код теперь ждёт
      фактический `attached=1`, а не прекращает проверку. Текущий шаг: различить
      возвраты `0x140E2ACA0` (активное оружие/нажатие/запрет действия) и
      `0x140E2B220` (состояние оружия/боезапас/задержка). Прямой повтор
      `PlayerFireController::Fire` остаётся выключен из-за прежних падений.
      Последняя проверка локализовала разрыв раньше этих проверок: удалённый
      контроллер стрельбы не получал фронт. Таблица команд дала правильные маски
      `aim=0x200`, `fire=0x800`; фронт огня теперь сохраняется после обновления
      движения до отдельного обновления контроллера стрельбы. Проверка `0xA00`
      ожидает `FIRE-GATE input press=1`.
- [~] Подать сетевую маску кнопок в штатный ввод слота 1. Первый слой добавлен:
      удерживаемые прицеливание/огонь идут через `FrameState+0x6FC8+0x28`, при
      устаревшем пакете ввод очищается. Надёжная доставка одиночных событий ещё
      требуется.
- [x] Сравнить полный объект `Joypad` слота 1 по правильному адресу
      `FrameState+0xDB0+0xBF0`: обе копии дали `JOYPAD-DIFF total=0`, устойчивого
      поля активации там нет. Массовая аппаратная точка на горячем обработчике
      завершила обе игры и отвергнута.
- [x] Безопасно сняты восемь чисел `block[1]`. Круговая ветка дала
      `[cos,sin,0.5,-0.72]`, программная `[sx,sy,0.5,0]`; воспроизведение
      правого стика `0.5/-0.72` в программной ветке клон не оживило. Значения
      блока ввода не являются недостающим включателем.
- [x] Найден настоящий слой чтения движения: `StickMoveInterface` vtable
      `0x142A30740`, основной обработчик `PlayerMoveStick_MainUpdate
      0x140F03500`.
- [x] Подтверждена привязка: в обеих копиях основной Player имеет
      `joypad_index=2, screen_index=0`, клон — `joypad_index=3,
      screen_index=1`. Номера retained-таблицы 0/1 нельзя использовать как
      номера объектов `Joypad` 2/3.
- [x] Player-метод №16 `0x140C9F8B0` вызывается и для клона, но до исправления
      маршрута публиковал нулевое движение: Joypad 3 терял оси внутри
      `Input_ScaleCopyVirtual 0x14183B410`.
- [x] Найден и подтверждён последний блокирующий фильтр: фоновая копия имела
      `byte_1437EA62C=0`. Узкий обход только на время обработки Joypad 3 сохраняет
      оси в `Joypad3+0x48`, после чего штатный `PlayerInputCommandState` публикует
      ненулевые `Player+0x39C8/+0x3A1C/+0x3A30`. Joypad 2 не изменяется.
- [x] Заменить погоню клона за сетевой позицией на передачу
      штатного результата движения локального Player. Определить минимальный
      устойчивый набор полей из `PlayerInputCommandState` и точку их применения
      к клону после `0x140C9F8B0`, не смешивая движение с коррекцией позиции.
- [x] Статическая граница определена: сырой ввод `Player+0x39C8`, величина
      `Player+0x3A1C`, мировой вектор `Player+0x3A30`; единственный расчёт
      выполняет `0x141E397C0`, потребитель — `0x140F03500`.
- [~] Пакет v4 передаёт правильный мировой вектор, но прежнее применение после
      метода №16 оказалось запоздалым: `0x140C9FE4F` рассчитывает состояние, а
      вызов в `0x140C9FE76` потребляет его до возврата метода. Это объясняет
      последний результат «бежит на месте». Текущая проверка подставляет поля
      сразу после `0x140C9FE4F`, до потребителя, без поправки координат.
- [~] Проверка до остатка метода №16 отвергнута: обе копии получали величину
      `1.0` и правильный мировой вектор, но удалённый Player оставался у места
      появления. RTTI также отверг старый перехват `0x14042FB40 -> 0x141DE8EC0`:
      это путь Npc/NavCharacter, не Player. Текущая проверка пишет сетевую позицию
      в родительских локальных координатах в `Player+0x130` до общего метода
      `0x141BB8320`, чтобы он штатно обновил `+0x240/+0x250/+0x280` и коллизию.
- [~] Применение через `+0x130` вернуло движение, но последний запуск оставил
      рывки и визуальное расхождение примерно `1-1.5 м`; внутренний остаток
      `PUPPET-POS` доходил до `7-10 м`. Статически подтверждены прямое
      преобразование `0x14014E240`, обратное преобразование внутри `0x1416862B0`
      и конечная запись `0x141BB8320`. Следующая проверка сравнивает сетевую цель,
      точный повторный расчёт движком и фактический `Player+0x240`; коэффициенты
      сглаживания до разделения этих причин не менять.
- [~] Точная проверка завершена: в обеих копиях при движении и в покое
      `target == predicted == actual`, обе ошибки равны нулю. Преобразование и
      поздняя запись исправны. Остался дискретный поток примерно `20-24` пакета/с,
      который раньше без промежуточных положений подавался на каждый кадр.
      Добавлено выравнивание между двумя подтверждёнными позициями с задержкой
      `75 мс`; требуется визуальная проверка рывков и остановки.
- [~] Выравнивание `75 мс` отвергнуто: сильные рывки остались у обоих персонажей.
      История локализовала регрессию между последней плавной версией `25637e8` и
      включением коллизии `a06c79c`; точная покадровая позиция появилась позже как
      попытка компенсировать расхождение двух физических симуляций. Текущая
      контрольная сборка возвращает политику `25637e8` и отключает коллизию,
      наблюдатели столкновения, прямую подачу перед потребителем и запись корня.
- [~] Контрольная сборка убрала рывки, но оставила `2.98/3.42 м` ошибки и неверное
      направление корпуса. Причина локализована: старый слот-16 уже не подставляет
      сетевой мировой вектор, а новый перехват перед потребителем был ошибочно
      отключён вместе с опытами записи корня. Он возвращён с ограниченной поправкой
      позиции; поворот во время движения снова принадлежит штатной локомоции.
- [x] Текущий эталон движения подтверждён в обеих копиях: рывков нет, направление
      корпуса верное, конечная ошибка `0.043/0.071 м`, `hardSnap=0`, поворот в
      покое сходится до `orientationDot=1`. Не менять вместе следующие владельцы:
      мировой вектор до потребителя, штатная локомоция позиции/поворота при беге,
      ограниченная поправка только при движении, сетевой кватернион только в покое.
- [ ] Коллизию игроков и анимацию столкновения возвращать только после остальных
      действий и отдельной проверкой поверх зафиксированного эталона. Ветки
      `4<->5`, bump-наблюдатели и точная запись `+0x130` сейчас намеренно отключены.
- [x] Убрать крупное расхождение без жёсткой покадровой записи координат:
      ограниченное схождение во время движения уменьшило измеренный остаток до
      примерно `0.10/0.14 м`.
- [~] Поправка уменьшила расхождение, но выявила вращение на месте: свободный
      режим выходил до применения `Player+0x140`, а микропоправки продолжали
      включать движение около цели.
- [~] Проверенный сетевой поворот применяется независимо от режима позиции,
      микропоправки отбрасываются. Вращение на месте стало заметно меньше;
      окончательная длительная проверка ещё нужна.
- [x] Найдена причина асимметричного скольжения в покое: при нулевом вводе
      записывался ненулевой мировой вектор поправки, поэтому физика двигала тело
      без анимации. Поправка координат теперь действует только во время настоящего
      движения; в покое остаётся лишь аварийный перенос при ошибке свыше 4 м.
- [x] Первый предел `0.015` был недостаточен: измерены остатки `0.29 м` и
      `1.35 м`; при движении ошибка уменьшалась, в покое оставалась неизменной.
      Политика заменена на 8% ошибки, максимум `0.05`, мёртвая зона `0.03`.
- [x] Усиленное схождение подтвердилось: остаток снизился примерно до `0.10 м` и
      `0.14 м`; дальнейшую доводку сантиметров отложить до исправления поворота.
- [~] Плавное сетевое направление визуально стало лучше: вместо полной записи
      кватерниона
      каждый кадр применяется нормализованное сближение 18% по кратчайшей дуге.
      Вращение в покое не вернулось; точное запаздывание ещё не измерено.
- [x] Изоляция общей клавиатуры подтверждена: локальный прыжок больше не запускает
      клона в том же процессе. Joypad 3 получает действия только через переданные
      индивидуальные маски; Joypad 2 сохраняет обычную клавиатуру.
      Первый запуск не проверил гипотезу: обе копии упали в `0x14183875D`, потому
      что обратный переход трамплина затёр живой `RAX=entry RSP`. Переход заменён
      на не меняющий регистры `jmp [rip+0]`; повторить тот же прогноз.
- [~] Штатные маски кнопок Joypad 2 передаются и подаются в `source+0x518` до
      обработки Joypad 3. Добавлено накопление коротких фронтов нажатия и
      отбрасывание старых пакетов; требуется проверка прыжка, приседания, переката,
      ближнего боя и переходов удержания/отпускания.
- [~] Коллизия игроков: старая запись `collider+0xC4` отвергнута. Обычный Player
      использует слой 4, сетевой персонаж слой 5; глобальная матрица запрещает
      `4↔4`, `4↔5`, `5↔5`. Реализована узкая схема: клон получает 5 штатным
      установщиком, разрешается только `4↔5`; физическая коллизия подтверждена.
- [~] Убрать телепорты после столкновения: подтверждено, что старый порог 4 м
      срабатывал на расхождение двух физических миров (`4.19/5.72 м`). Сценарный
      перенос теперь помечается отправителем отдельным флагом; обычная ошибка
      контакта не вызывает перенос. Новый запуск выявил постоянную ошибку в покое
      `1.07/1.28 м`, потому что обе ветки коррекции отключались при нулевом вводе.
      Медленное сведение в покое подтверждено: остаток упал до `0.146-0.150 м`.
      Усиление до `20%/0.20 м` отвергнуто: ошибка достигла `6.103 м` при движении,
      после остановки модель доскальзывала без анимации. Настройка коэффициентов
      больше не рассматривается как решение. Точная запись одновременно в
      `Player+0x130/+0x240` дала миллиметровую позицию, но сломала анимацию даже
      без обнуления скорости. Статически доказано: `0x141BB8320` использует
      `+0x130` как вход корневого движения и выводит мировой результат в `+0x240`.
      Запись только внешнего `+0x240` также отвергнута: удалённый Player бежал на
      месте. Обычная покадровая запись координат отключена; сначала проверяется
      перенос движения в правильной фазе метода №16.
- [ ] Добавить штатную реакцию руками при столкновении. Найден
      `PlayerBumpController_BuildBumpActionCandidate 0x140CE2520`; прежняя
      атрибуция жестов функции `0x140CE9530` исправлена. Удалённый Player уже
      штатно поддерживает свой индекс `SimpleGridHash` через `0x1410EC230`, поэтому
      повторно регистрировать его нельзя. Следующий шаг: только чтением записать
      цель/дистанцию из `Crowd::ProbeResult` и результат `0x140CE8E10`, разделив
      отсутствие цели, отказ геометрии контакта и вытеснение уже начатого действия.
      Верхний обработчик через `0x142A0EF08` вызывается, глобальный флаг равен
      нулю. После исправления владельца `0x140CE8E10` достигается для обоих слотов,
      но при расстоянии `0.851872 м` запрос всегда пуст: цель `0`, дистанция
      `FLT_MAX`. Новый кандидат в `Crowd_UpdateProbe 0x1406DD350` обязан иметь бит
      возможности с индексом из `0x143D0A724`; следующий журнал только читает этот
      бит у противоположного Player, не меняя состояние.
- [~] Фоновый ввод Joypad 3 больше не подменяет общий флаг фокуса
      `0x1437EA62C`. Узкий обход проверки внутри `0x14183B410` сохраняет ввод
      второго игрока, но оставляет системному обработчику настоящий фокус окна.
      Проверить, что курсор освобождается при переключении окон.
- [ ] Сложные режимы: укрытия (**CONFIRMED `bk` 2026-07-08**), лазание, уступы, верёвка, транспорт и совместные
      действия.
- [ ] Жизненный цикл кампании: контрольные точки, сцены и переходы между главами.
- [ ] Проверить длительную работу двух копий без аварийного завершения и
      накопления расхождения.

Сейчас `StatePacket` v4 передаёт положение, поворот, скорость, здоровье, движение
осей и полную 64-битную маску кнопок с фронтами нажатия/отпускания. Найденный
глобальный блок ввода в кампании остаётся нулевым, поэтому поля действий пока не
содержат настоящего локального ввода. Для разовых действий нужен отдельный
надёжный упорядоченный поток с номерами и подтверждениями.

## P0.5: Build-ready design backlog (STATIC done 2026-07-05; pending runtime pins)

Все пункты ниже разобраны статически end-to-end; детали и адреса — в
`FINDINGS_ARCHIVE.md`, рантайм-чтения собраны в
`reports/runtime_pins_checklist.md`. Коммиты указаны для быстрого grep лога.

**Боевые действия / направление:**
- [~] Направление aim/fire/гранаты: fire-ray = per-screen **pose seed**
      `CameraManager::GetHandle(idx) -> *(mgr+0xB0)+0x60` (32Б pos+quat), резолвер
      `ResolveScreenRoutePoseSeedOrDefault 0x141C077B0`. Фикс: relay сида (StatePacket
      v8, +32Б) + хук резолвера, override при idx==1. Гранаты используют ТОТ ЖЕ сид
      (`PlayerGrenadeController_...UpdateAimArcAndToss 0x140E17AC0`). (`820d89a`,
      `425c163`, `4cd55ca`, `d6c4a41`, `a873085`) Runtime-пин #2.
- [x] Снайпер-скоуп HUD-гейт: по локальному ADS (`Player+0x1548`), `286b6f1`.
- [~] Снайпер-скоуп «Хлоя пропадает»: DrawControl-скрытие меша куклы при scoped-ADS;
      форсить видимым. Runtime-пин #6 (`26bb1ad`). Обход: pose-only.
- [~] Эффекты/взрывы/трассеры/импакты: НЕ отдельный канал — нативные следствия
      настоящих снарядов в каждой копии (`add9bf2`). Гранаты/анимация броска —
      нативно из реле кнопки (`ee01be5`, `b698b67`).

**Надёжные события:** карта — все кнопочные действия приходят бесплатно через реле
маски Joypad (`+0xD0/+0xD8/+0xE0`); мили-цель локальна → фаза врагов (`d934aa8`).

- [x] Звук: НЕ отдельный канал — шаги/фоли = anim-EFF (EffectControl), играют с
      анимацией куклы; тир-4 `NetSnapshotEffectParser` переигрывает их точно;
      выстрелы/взрывы нативно; диалоги = DialogManager; 3D-локализация верна через
      позицию куклы. Опциональный SoundEvent-релей только для не-аним/не-экшн
      one-shot'ов (пока не нужен) (`418757d`).

**Нарратив (вариант 1 «Хлоя — драйвер сюжета»), всё без net-сессии:**
- [~] Прогрессия: релей команд `GameTaskGraphMgr` + apply через
      `AddCommand 0x141CB95B0` (список `0x146A514A0`, 40Б записи). Runtime-пин #3.
      (`80d4e18`, `666c7f7`)
- [~] Осмотры/взаимодействия: релей флага `state+0xD0 бит1`
      (`ScriptQuery_InteractableHasBeenInspected 0x1408B1350`). Runtime-пин #4.
      (`f15063b`)
- [~] Подавление триггеров follower'а: `suppress-interactable-system 0x1408AC710`
      + per-command joypad disable (`1474ab9`).
- [x] Диалоги/бантер: бесплатно per-copy (DialogManager — не AI), кукла остаётся
      объектом с отключённым AI (`b6c2687`).
- [~] Камера головоломки: follower видит через синхронизированную сцену (Path B)
      или релей транформа экрана-0 (Path A). (`ad342b1`)

**Spectate / асимметричный кооп:**
- [~] Spectate/pause-hold локального игрока (move-stick/transitions off + подавление
      + существующие 4 death-хука `Player+0xBE8..+0xC00` + пин). (`7e67a1a`)
- [~] Игрок 2 наблюдает за игроком 1 в периоды отсутствия Нэдин (пролог): детект
      через `g_last_nadine_object` + идентичность локального == Хлоя; spectate +
      релей камеры копии 1; переход при спавне/деспавне Нэдин. (`e0cd5bc`)

**Машина:**
- [~] Пассажирское сиденье: `Npc_TeleportIntoVehicleSkill 0x1406B14D0` (кукла — Npc).
      Двухкопийная модель: водитель ведёт нативно на своей копии, релей транформа
      машины, пассажир едет через parenting сиденья. Runtime-пин #5.
      (`aa5f550`, `58fecf2`, `441a3e1`)

**Анимация (полная синхронизация):**
- [~] Тир 1 (input-driven) — уже работает через реле ввода. Тир 2 (скрипты) — с
      нарративной синхронизацией. Тир 3 (рагдолл/downed/оружие) — релей флагов
      NetSnapshot. Тир 4 (полная точность) — ДОСТИЖИМ: capture
      `FillAnimCmdBitStream 0x140BC4E60` напрямую (512Б) из `Player.AnimControl`,
      apply через `AddSnapshot`→`ProcessPendingSnapshots` на Player-embedded
      генераторе `Player+0x1B30`. Runtime-пин #1. (`59d93be`, `b8e564e`, `9017fdc`)
- [ ] Оружие Нэдин: СТЕНА — asset/skeleton-AP (холстер-точка из дефа оружия требует
      сустав на скелете look'а). Не code-fixable (`487a3c3`).

## P1: Enemy Synchronization (DEFERRED)

- [ ] Не продолжать до закрытия P0.
- [x] Сохранить найденные безопасные пути урона и `npc-die` в коде, отчёте и IDA.
- [x] Выключить отправку/применение противников и передачу их урона по умолчанию.

## Previous Tracks (reference)

### ACHIEVED (2026-06-15): Real second playable local player in campaign

Driving `SpawnPlayerFromRetainedDescriptor(desc, idx=1, ctx, ...)` (captured args
from a real Chloe spawn via HW-breakpoint, reissued with index=1, hotkey F12)
produced a SECOND CONTROLLABLE PLAYER in single-player campaign (a Chloe clone).
No crash. This validates the converged model and is the project's first real
second player. Repro in `FINDINGS_ARCHIVE.md` "MAJOR BREAKTHROUGH".

### P-next: Turn the second player into Nadine co-op
- [x] Runtime check: clean `block[1]` feed and clean `FrameState+0x6FC8+0x28`
      branch1 feed are insufficient. They either do not move the slot-1 clone,
      or bleed into Chloe. Do not repeat this input-struct path as primary.
- [x] Runtime check: spawn descriptor + immediate ctx/a4/out/stack do NOT carry
      direct `chloe-base`, `coop-chloe`, or `nadine-base` hashes. Text hits
      containing `nadine` in ctx pointer targets are level/script names, not the
      player model selector. Do not repeat this descriptor/near-ctx hash scan.
- [x] Runtime check: patching slot0 `PlayerSnapshot+0x144` to `nadine-base`
      before reissuing slot1 spawn is unsafe/unproven. Reissue returned, then
      `tll.exe` was gone; no post-reissue slot dump possible. Do not use this as
      the primary spawn-as-Nadine path.
- [x] Static: find the builder/materializer that fills `player+0xB0`
      (`PlayerSnapshot`) and writes `snapshot+0x140/0x144 = chloe-base`.
      Post-spawn `PlayerSnapshot+0x144 = nadine-base` is stable but visually
      ineffective; the Nadine model must be selected before or during Player
      materialization. Result: `PlayerSnapshot` is downstream. The live source
      is `player+0x190`, seeded in `0x141BB4280` from an upper visual-state pair
      before `Player+0x6F0` builds the visual graph.
- [x] Runtime: one-shot `player+0x190` override at `0x141BB4B14` during slot-1
      spawn fires and writes Nadine's state id. Result was partial: only hair
      remained on the clone, which proves `player+0x170` and `player+0x190`
      must be patched as one visual pair.
- [x] Runtime: validate the paired `coop_runtime.cpp` override:
      `0x141BB4B01 -> player+0x170` and
      `0x141BB4B14 -> player+0x190`. Logs confirmed both
      `VISUAL look seed hit ... PATCHED` and
      `VISUAL state seed hit ... PATCHED`.
- [x] Visual check: paired override fired in live runtime but still produced
      floating Nadine hair only. `tll_input_slot_dump` confirmed slot1 has Nadine
      `+170/+190`, so those two fields are necessary but not sufficient.
- [ ] IMMEDIATE: identify the body/rig source still left on the player path.
      Current hard clues:
      `player+0x174` is copied from `player+0xD8` at `0x141BB4A50`;
      slot1 still has `+174=0xcde4b742` and `+180=0xde7baff5`;
      the visual graph is produced through `player->virtual +0x6F0` and
      `+0x700`, with result stored at `player+0x1A8`.
- [ ] Make 2nd player look/character = Nadine (descriptor character/look fields,
      or post-spawn retarget) instead of Chloe clone.
- [ ] Independent camera for player 2 (screen-1 route now has a valid slot-1 actor).
- [ ] Route separate input (PC2) to player 2.
- [ ] Stability: build a clean spawn descriptor instead of reusing captured stack copy.
- [ ] Checkpoints/death/respawn behavior with 2 players.

### P0: Rebind the real second-player lifecycle  [CLOSED — reframed]

- [x] Find the consumer/drain path for the deferred queue rooted at `0x1437E9700`.
  - Queue = generic **ndjob** worker pool; always drains. Not the blocker.
- [x] Prove when the queued callback `0x1409F9B20` actually executes.
  - Scheduled only by `NdiNetInit` (`0x1409FBA60`, `game/net/flow.cpp`), gated on
    `PAGE_MULTIPLAYER_CONNECTING`. Campaign never runs it.
- [x] Explain why live campaign can still end up with `ctxB+0x8 == 0`.
  - ctxB rebind is multiplayer-net-init gated; campaign never re-binds it.

Reframed conclusion: do NOT chase the ctxB owner bind in campaign (that means
booting multiplayer net). Pivot second-player bringup to **ctxA**
(`0x143E08C80`) / net-player manager, which `AddOrUpdateNetPlayer` already uses
as the real source context.

### P0b: Second-player bringup via ctxA path (new active track)

- [x] Map how `AddOrUpdateNetPlayer (0x140A90550)` populates a player slot from
      a ctxA source entry. ABI: rcx=manager 0x143DD70C0, rdx=ctxA 0x143E08C80,
      r8=source_entry, r9d=slot, [rsp+0x20]=is_local; rax=slot (10 × 0x43D0).
      Source resolved by name/id against ctxA's 12-entry table (ctxA+0x800,
      stride 0x1C0). Remote path = `sub_140A58190` (is_local=0); also a
      "late-join" variant `0x140A90A00`.
- [x] DECISIVE runtime check: NetGameManager is **DORMANT** in campaign.
      `tll_netplayer_probe.exe` in confirmed campaign gameplay -> 0/10 slots,
      Chloe not registered through the manager. ctxA net-player path is
      multiplayer-only. => P0b net route abandoned for campaign co-op.

P0b verdict: both net-player seams (ctxB owner via NdiNetInit, ctxA via
NetGameManager) are multiplayer-dormant in campaign. Do NOT pursue them; that
means booting multiplayer net infra. New primary track = P1 (Nadine elevation).

### P0c: Activate the engine's native split-screen / local-2-player in campaign [DONE]

- [x] Runtime activation probe: `CameraManager(1)` is live in campaign.
- [x] Runtime activation probe: the second screen-route entity / second view path
      resolves live in campaign.
- [x] Find `g_maxLocalPlayers` global: `0x1437CF310` (live low dword limit = 1).
- [x] Decode split-screen activation chain:
      - `net-is-second-player-available?` = `0x140C021C0` (gate byte `0x143071038`)
      - `net-login-split-screen` = `0x140C02220`
      - `net-logout-split-screen` = `0x140C02420`
- [x] Prove native local-player-1 boot in campaign.
      Runtime diagnostic on game thread: with
      `max_local_players=2` + `avail_byte=1`, login path succeeds in campaign
      (`login_state=3`, query=1, cam0/cam1 resolved).

Verdict: the engine's own local-2-player / split-screen machinery CAN be raised
in campaign without a full multiplayer session.

### P1: Populate retained-player slot 1 in `g_RetainedPlayerSlotTable 0x143EEBE80` (CURRENT primary)

RESOLVED model (reader + writer both pinned):
- Campaign local-player resolution funnels:
  `(*(0x143FE1070))->vtable+0xA8 (0x140298D70)` -> split gate `0x143E07473`
  inactive -> `0x1410F0F60 ResolveRetainedPlayerHandleBySlot(index)` ->
  reads pair `0x143EEBE80 + index*0x10` {ptr+0, id+8}, validates
  `ptr->(+8)->+0xBC==id`.
- Slot 0 = Chloe; slot 1 empty in campaign => `local-player-1 == 0`.
- WRITER of these pairs = `0x1410F14C0 CommitResolvedPlayerSpawn` (retained
  player-spawn commit; ProcessSpawnInfo packet, materialize via 0x1417C3790,
  store +0xA8/+0xBC into 0x143EEBE80).
- Earlier leads now closed as NOT the blocker: CharacterManager registration
  (Nadine already char 1), resolver-struct `0x143FE1070 +0xE8/+0x104` id slots
  (different registry), AddCharacter `0x141D63420`.

- [x] DIRECT runtime test: wrote Nadine pair into slot 1 (0x143EEBE90/98).
      Pair format was VALID (matches reader), but game CRASHED -> consumers read
      slot 1 and need a full materialized player runtime, not a raw pair.
- [x] Decoded `0x1410F14C0 CommitResolvedPlayerSpawn` store contract:
      slot index = arg r8 (->r12); pair `{obj+0xA8,obj+0xBC}` where obj =
      `0x1417C3790` materialized player object; built after ProcessSpawnInfo
      packet (0x1417C32A0, vtable 0x14291CF98) + record resolve (0x1417651D0).
- [x] Mapped full spawn pipeline (index-parameterized):
      `ScriptSpawnPlayer 0x140F73910` (script command via LEA @0x14009AE01;
      player index in esi) -> `SpawnPlayerFromRetainedDescriptor 0x1410F1910` /
      `SpawnPlayerFallback 0x1410F3350` -> `CommitResolvedPlayerSpawn 0x1410F14C0`
      (slot=r8) -> materialize `0x1417C3790` -> store pair into 0x143EEBE80[index].
      Engine structurally supports spawning into slot 1; raw poke crashed because
      it skipped this pipeline.
- [x] GRAND SYNTHESIS: spawn slot index = SCREEN-ROUTE index (not a plain player
      number). `ScriptSpawnPlayer` hardcodes index 0 (xor esi,esi); slot index is
      derived via `0x141C077B0(route index, assert<=1)` reading two-screen route
      table `0x14697AD50`. spawn + local-player slot table 0x143EEBE80 + camera
      all hang off the SAME route 0/1. Route 1 inactive in campaign => slot 1
      empty everywhere; raw slot poke crashed because route-1 entity not stood up.
- [x] CORRECTION: `CameraManager::Init 0x141C04F70` builds BOTH route entities
      (loop ebx 0..1) into `0x14697AD50`. Route entity 1 ALREADY EXISTS
      (slot1 {0x10E0200188, 0x1A} live). Blocker is NOT a missing entity.
- [ ] ROOT GATE (narrowed): mark route 1 ACTIVE. `0x141C077B0` requires
      `route->+0xB0` child with `+0x55!=0` and `+0x56!=0` plus a bit via selector
      `0x14697AE34`. Need runtime diff route0(0x10E0200160 active) vs
      route1(0x10E0200188 inactive): +0xB0 child, +0x55/+0x56, selector bit.
- [ ] Find the route-activation function that sets those + whether split-screen
      login drives it; then let the spawn pipeline fill slot 1 (no poke).

### P1-old: Nadine camera ownership (subsumed by P1 above)

- [ ] Prove campaign-native independent camera ownership for Nadine.
- [~] Type the screen-route owner family that carries `route+0x124`.
      Current strong anchor:
      - `0x14073C8A0` = per-screen camera-overlay priority updater
      - `0x14073CDD0` = per-screen camera-overlay follow-state builder
      - dynamic state bank `0x143D763E0` with stride `0x2B0`
      - static tuning bank `0x1430732F8` with stride `0xC0`
      - RTTI-adjacent owner family:
        `CameraManagerCustom / ICameraManagerCustom / CameraControlStick`
      - new split:
        - `0x143053850` = CameraManagerCustom-typed global bundle
        - `0x14073B6E0` = alloc/init wrapper for live `0x1FA0` overlay object
        - `0x140772E20` = shared CameraControlStrafe-typed large constructor base
      - `CameraManagerCustom` live vtable policy slots:
        - `+0x10 = 0x14073C8A0`
        - `+0x18 = 0x14073CDD0`
        - `+0x20 = 0x14073C400`
        - `+0x28 = 0x14073B650`
        - `+0x30 = 0x14073C620`
        - `+0x38 = 0x14073C760`
- [ ] Identify the resolver-owner contract behind `0x143FE1070->virtual +0xA8`.
- [~] Identify the campaign caller that can drive
      `0x141C0DFD0 / 0x141C075DC` with a Nadine-compatible override.
      Strong candidate found:
      - gameplay camera-priority cluster around
        `0x14073C9E3 / 0x14073CB4D / 0x14073CBF6 / 0x14073CCBC`
      - repeatedly selects camera child through `0x141C0D4B0`
      - compares against descriptor/sentinel objects
      - pushes stack-built descriptors through `0x141C0DFD0`
      - now typed as stock camera-control overlays including:
        `kCameraStrafe`, `kCameraSwim`, `kCameraZoom`,
        `kCameraMelee`, `kCameraMeleeOverride`,
        `kCameraContextualLook`
- [~] Keep the route-local camera lifecycle pinned as the main owner path:
      - `0x141C04C50`
      - `0x141C04CE0`
      - `0x141C077B0`
      - `0x141C078D0`
      - `0x141C06F20`
      - `0x141C0E9F0`
- [ ] Prove whether `screen 1` has a distinct route updater, or only a
      descriptor-only attach path above `0x141C0DFD0`.
      Current correction:
      - direct `0x141C078D0` gameplay callers checked so far still resolve `screen 0`
      - but `0x14073E330` is a generic per-screen route push
      - and `0x1413CB6F0..0x1413CB80C` already drives that seam in a stock
        two-screen loop for the `kCameraContextualLook`-side descriptor family
      - sibling loop `0x1413CF013..0x1413CF14A` does the same under the
        `CameraControlFollow` string/type family
      - downstream of that, `0x141C0E590 -> 0x141C0ECC0` already performs
        mode-based route-child selection and heavy route-state recompute
- [ ] Identify the active follow/target/owner object that drives Nadine's view.
- [~] Determine whether the screen-route family can be cleanly re-targeted to Nadine.
      Current concrete seam:
      - `0x141C04F80` builds the two-entry route table `0x14697AD50`
      - `0x141C06420` resolves a live route entity from that table
      - `0x141C0DFD0` resolves actor through `(*(qword*)0x143FE1070)->virtual +0xA8(route+0x124)`
      - `0x141C075DC` writes the attached target pair into `route+0x2A8/+0x2B0`
- [ ] Determine whether split/local-player camera seams can stably HELP this path without treating split-screen as solved architecture.
- [ ] Separate stock `local-player-1` resolver-owner path from split/session summary helpers; current resolver bundle = `0x143FE1070`, stock player-2 feed = `ctxB`.
- [ ] Keep the manager0 retained-camera lifecycle ready as fallback:
      `0x1410F3350 -> 0x1410F3DD0 -> 0x141C078D0 -> 0x141C074E0`
- [ ] Treat the retained/director bridge conservatively:
      the currently proven live bridge uses `0x1410F0D10(0)` twice before
      `returned_entity+0x36C0 -> 0x140237760`; no symmetric slot-`1`
      director bridge is proven yet.
- [ ] Keep `0x140C34AC0` in the right layer:
      it advances contribution/ring state from the already-seeded secondary
      pair `+0x70/+0x78` and is not a writer for `+0x60/+0x68` or
      `+0x70/+0x78`.
- [ ] Split the pair-writer search correctly:
      - upward from `0x140C30290`, now knowing that
        `0x1410DDED3` passes the top-level `Player` itself as `rdx`, so the
        remaining question there is how to substitute or mirror that canonical
        Player pair for Nadine
      - separately upward from the still-unseeded primary pair
        `director+0x60/+0x68`
- [ ] If the route-entity family stalls, identify where the manager0 retained-camera lifecycle can be reused
      or adapted for Nadine.
- [x] Decode exact campaign-visible manual-camera cluster:
      - `Manual Camera 1/2`
      - `Joypad 1/2 Controls Game`
      - `Swap Joypad Screens`
      - `Bind Manual Cam To Player(/Parent)`
- [x] Reclassify `0x141C07E20`:
      manual/debug follow writer, not yet the final gameplay camera seam.

### P1a: Nadine direct control (parked but open)

- [~] Find Nadine's real control/locomotion ownership layer (NOT goal-based `MoveTo`).
      Quick probes done: `Type7` writes but does not move; `Type4` gave no usable effect;
      owner mode `+0x1748` and setter `0x141DE5A10` stay within buddy-family behavior.
- [x] Reframe the strongest current control-side seams above raw `MoveTo`:
      - `0x141DE9E60(owner, record, mode, flag)` = record-driven owner apply
      - shared `owner+0x15B8` mode/snapshot layer
        (`0x141E0ED10 / 0x141E0EDF0`)
      - indirect `BehaviorScavenge` materialized-state install path under
        `owner+0x1F80` (`0x1404B7F00 / 0x140473590`)
- [x] Separate the prepared NPC action layer from the stronger live control layer:
      - `0x1403F5B60` plus the surrounding `0x140545000..0x14056B000` writers
        now read as prepared NPC action/behavior commands
      - they are useful, but not the first clean seam for direct playable control
- [~] Keep the strongest known live non-script branch in front:
      - `0x14058B900`
      - `0x14058BA40` = mode-1 snapshot apply into `owner+0x15B8`
      - `0x14058BC70` = mode-2 snapshot apply plus nested-control follow-up
      - both already drive `0x141E0EDF0 / 0x141E0ED10`
- [x] Reclassify the higher `0x1406B3D70` family:
      - vtable block resolves to `Patrol@ScriptSkill`
      - `0x1406B3D70 / 0x1406B3640 / 0x1406B3940` are AI patrol logic,
        not the main direct-control seam for a playable Nadine
- [x] Reclassify the second direct caller above `0x141DE9E60`:
      - `0x140486EB0` belongs to `BehaviorTeleport`
      - so this path is also AI behavior logic, not raw player input
- [ ] Shift the main control frontier down to the shared controller layer:
      - `0x140416B20`
      - `0x141DE9E60`
      - `0x141E0ED10`
      - `0x141E0EDF0`
      - `ActionPack` lifecycle under `owner+0x15C0`:
        - `0x141E18350`
        - `0x141E18450`
        - `0x141E18700`
        - `0x141E189C0`
      - concrete shared snapshot/publish block `0x141E18A70..0x141E18BA3`
        (`source+0x230/+0x240/+0x250` -> `(owner+0x1D0)+0x15B8`)
      - sibling owner-local publish block `0x141E18D00`
        (`owner+0x258/+0x2F8/+0x300` -> same publication tail)
- [ ] Find the real producer of the source object for `0x141E18A70`:
      - now known to be a live `ActionPack` object
      - who materializes and selects it
      - who fills backup handles `owner+0xE90` and `owner+0x258`
      - who chooses between stop/stand, resume, and the other `ActionPack`
        branches over the same machinery
      - how the higher chooser at `owner+0x80` (`0x141E1A040`) routes Chloe
        into the moving-state branch `0x141E15C80`
      - how Chloe's live command-state mode map is written:
        `1=StopAndStand`, `2=MoveToLocation`, `3=MoveToActionPack`,
        `4=MoveAlongSpline`
      - who fills `source+0x230/+0x240/+0x250`
      - and whether Chloe's producer can be reused for `NadineBuddy`
- [ ] Treat `0x141E14890` as the new shared setter above those branches:
      - it accepts `owner`, `mode id`, and a prepared descriptor
      - writes:
        - `owner+0x444`
        - `owner+0x290/+0x2A0/+0x2B0`
        - `owner+0x2D0`
      - but still does not write `owner+0x80` directly
- [ ] Treat `owner+0x80` as likely downstream mirror, not the only live ingress:
      - ordinary gameplay callers already feed `0x141E14890` directly
      - current direct mode feed observed in callers:
        - `0`
        - `1`
        - `2`
        - `3`
        - `4`
      - descriptor sources already seen:
        - `owner+0x260`
        - `owner+0x290`
        - stack-built temporary descriptors
- [ ] Treat `0x141E1A000` as the broader ordinary-Chloe movement update loop:
      - correction: `0x141E1A000` is an internal label inside
        `0x141E19F90 = NavStateMachine::Update(bool)`
      - it runs `0x141E1BA20`
      - conditionally dispatches `owner+0x80` through `0x141E15C80` or `0x141E158B0`
      - mirrors the resolved descriptor into `owner+0x120..+0x1B0`
      - refreshes `owner+0x228/+0x218`
      - then handles traversal/action-pack follow-up and publication flags
- [ ] Treat `0x141E1AA20` as the second-stage path/result chooser inside that loop:
      - works from `owner+0x444`
      - compares `owner+0x2F8`, `owner+0xE90`, and `owner+0x258`
      - releases invalid/disappeared action-packs
      - rebuilds compact descriptors through `0x141E06C00`
      - feeds replacement modes `0`, `1`, `2`, and `4` into `0x141E14890`
- [ ] Treat `0x141E1E4F0` as the strongest ordinary moving-state family above the setter:
      - anchored by `UpdateStateMoving` strings
      - feeds `0x141E14890` with modes `2`, `3`, and `0`
      - checks arrival, waypoint changes, and moving-command success/failure
- [ ] Keep neighboring families separated so they do not become false primary seams:
      - `0x141E1F300` = post-root / interrupted-state update
      - `0x141E18120` = abandon interrupted state
      - `0x141E20280` = enter-animation update
- [ ] Climb above `0x141E14890` through the ordinary-Chloe chooser:
      - `0x141E1BA20`
      - unpackers `0x141E06940 / 0x141E06C00`
      - determine how it chooses live mode ids `0..4`
      - then test whether the same feed can be redirected into `NadineBuddy`
- [ ] Shift the next climb one level higher:
      - use the now-found outer edge
      - `0x141DE8CB1 -> 0x141E19F90`
      - treat the surrounding block `0x141DE8B80..0x141DE8E60` as
        `NavCharacter::Update`-side control flow
      - determine whether that is the real Chloe movement tick we want to redirect
- [ ] Climb above the `BehaviorScavenge` registration/dispatch family instead
      of blind flat-caller hunting:
      - `RegisterBehaviorScavengeMetadataRoots` (`0x1404B8180`)
      - metadata root `0x142952020`
      - live installer `0x1404B7F00`
- [ ] Climb above `0x14058B900` and identify the live owner/state family that
      chooses its mode-1 and mode-2 branches; this is now a better first
      playable-switch frontier than more work on prepared NPC command writers.
- [ ] Prefer the stronger prepared-state branch over metadata-root chasing:
      - `NavCharacter::SetStrafeMode`-style branch around `0x1404BA9C0`
      - `owner->virtual +0xC48`
      - `0x141DE4D20 -> 0x141E15030`
      - `0x141DE5190`
      - `0x141DE4F90`
- [ ] Resume from the nested-control tag / fallback `+0xD30` layer when camera work is blocked.
- [ ] Expand verbs later:
  - locomotion
  - traversal
  - interaction
  - combat
  - script-facing actor qualification
- [ ] Validate checkpoints / death / respawn on host.

### P1b: Split/local-player seam as SUPPORTING path

- [x] `local-player-1` is empty before split login.
- [x] `local-player-1` can be resolver-bound to live `NadineBuddy`.
- [x] stock campaign resolver can still return `0x0` for `local-player-1` even
      after split login is active; Nadine appears there only through the hook
      on resolver vfunc `+0xA8`.
- [x] split render layer reacts (artifact / blink) when activated.
- [x] stock split refresh still sources player-2 content through the old
      `ctxB` / `NetPlayerData` family rather than a campaign-native Nadine owner.
- [x] `ctxA` and `ctxB` are separate startup-built context classes.
- [x] `ctxB` is the stock player-2 source-selection / summary controller, not the
      final local-player actor resolver.
- [x] second-camera attach path already uses the same resolver seam as the hook:
      `0x141C0DFD0 -> (*(qword*)0x143FE1070)->virtual +0xA8`
- [x] `0x141C04F80` builds a two-entry screen-route pair table at `0x14697AD50`.
- [x] `0x141C06420` resolves a live screen-route entity from that table; it is
      not a raw `CameraManager(index)` getter.
- [x] `0x141C0D4B0` selects a preferred camera child from a route entity's
      `+0x100` child list.
- [x] `0x141C0DFD0` already has a non-manual gameplay caller cluster around
      `0x14073C9E3 / 0x14073CB4D / 0x14073CBF6 / 0x14073CCBC`.
- [x] that cluster now resolves to a concrete per-screen camera-overlay family:
      `0x14073C8A0 / 0x14073CDD0`.
- [x] the overlay family is no longer anonymous:
      `CameraManagerCustom / ICameraManagerCustom / CameraControlStick`
      sit in the same RTTI/vtable block.
- [x] manager0 has a mature gameplay-facing retained-camera lifecycle:
      `0x1410F3350 / 0x1410F3DD0 / 0x141C078D0 / 0x140EDE5E0`
- [ ] Determine whether second camera / HUD can be made stable enough to HELP the
      primary camera-ownership question, without treating split-screen as solved architecture.

### P2: Convert host-side second player into actual co-op

- [ ] Feed player-2 input from the second PC into the real Nadine player path.
- [ ] Replicate the minimum host-authoritative state needed for stable co-op.
- [ ] Bring up second-PC camera/render path.
- [ ] Validate playable two-PC loop in campaign.

## Current Anchors

Use these first:

- shutdown:
  - `0x1409FD360`
- restart scheduling:
  - `0x1409FBA60`
  - `0x1409F9B20`
  - `0x1409FA94E`
- deferred queue:
  - `0x1415B43B0`
  - `0x1415B4870`
  - `0x1415B47C0`
  - `0x1415B4560`
  - `0x1437E9700`
- bind/service:
  - `0x143E08C80`
  - `0x143E0EF00`
  - `0x143E0EF08`
  - `0x143880318`
  - `0x14194D5F0`
  - `0x14194E3B0`
  - `0x14194E510`
  - `0x14194C870`
  - `0x14194C980`
- net-player path:
  - `0x140A90550`
  - `0x140A9AAD0`
  - `0x140B347C0`
- local-player resolver/routing:
  - `0x143FE1070`
- second-camera manual path:
  - `0x141C0DFD0`
  - `0x14073E330`
  - `0x1413CB6F0`
  - `0x141C0E590`
  - `0x141C0ECC0`
  - `0x140238500`
  - `0x140238800`
  - `0x140C30290`
  - `0x141C04CE0`
  - `0x141C07420`
  - `0x141C074E0`
  - `0x141C07640`
  - `0x141C07E20`
  - `0x143D76C10`
- retained-camera gameplay path:
  - `0x1410F3350`
  - `0x1410F3DD0`
  - `0x141C078D0`
  - `0x140EDE5E0`

## Parked

Do later, not now:

- [ ] Clean packed `ida/tll.i64` rebuild.
- [ ] Return to buddy-control polish.
- [ ] Revive old `slot1_probe*.dll` style runtime probing.
- [ ] Camera polish before real second-player ownership exists.
- [ ] Second-PC rendering work before host-side second-player path is proven.

## Already Closed Enough

Do not reopen these as the first move:

- [x] Startup bind exists:
  - `0x1409F9B20`
  - `0x1409FA94E`
  - `0x143880318 -> virtual +0x20`
- [x] `ctxB` missing owner is not explained by "startup never ran"
- [x] `0x1409FD360` is already typed as upper shutdown/teardown
- [x] `0x1409FBA60` already narrowed to deferred restart scheduling
- [x] restart work already narrowed to queue root `0x1437E9700`
- [x] old `slot1_probe*.dll` loop is intentionally retired
- [x] `BuildRespawnTransformBlock` is not the current primary blocker
- [x] low net-player seams `0x140A90550 / 0x140A9AAD0 / 0x140B347C0` are not the first-return track right now
- [x] `ctxB` is not the final local-player actor/camera owner path; it is the stock
      player-2 source-selection / summary family feeding split refresh.
- [x] stock second-screen gameplay routing exists below `CameraControlFollow`, but
      the current stable frontier is the mounted animated-camera owner at
      `player+0x36C0`, not the older split summary layer
- [x] `player+0x36C0` now has a concrete class identity:
      `PlayerCameraAnimationDirector`
- [x] `player+0x100` hidden authored-descriptor refresh is now separated from
      the canonical Player live pair `+0xA8/+0xBC`
- [x] The canonical `+0xA8/+0xBC` pair is now explained as generic
      runtime-object identity:
      - base ctor `0x1416A3330` clears `+0xA8` and seeds `+0xBC`
      - materializer `0x1417C3790` later writes the live node into `+0xA8`
        and recomputes `+0xBC`
- [x] `0x140238800` directly touches `PlayerCameraAnimationDirector +0x60/+0x68`,
      but only as a consume-and-clear path:
      - it uses the transient pair during route/descriptor update
      - then writes a zero inline pair back into `+0x60/+0x68`
- [x] the formal `PlayerCameraAnimationDirector` vtable surface is now typed
      enough to rule out internal pending-pair setters:
      - `+0x08 = 0x140C303E0` only adjusts contribution-ring base `+0xD8`
      - `+0x28 = 0x140C30760` and `+0x30 = 0x140C30930` only derive timed
        outputs from the stable pair `+0x70/+0x78`
      - `+0x38 = 0x140C35CF0` is a no-op
      - `+0x18/+0x20` only return helper code pointers
- [x] `0x140237E60` belongs to the same fallback/reset side of the mounted
      animated-camera lifecycle
- [x] The broad gameplay caller around `0x1410E9D80..0x1410EA0D2` does not
      seed a non-zero director pending pair before `0x140238800`; its
      post-call `(ptr,id)` loop goes through `0x1403D4070` into `player+0x100`
      / hidden-descriptor work instead
- [x] One live `PlayerCameraAnimationDirector` predicate path already comes
      back through retained slot `0` via `0x1410F0D10(0)` and then
      `returned_entity+0x36C0 -> 0x140237760`
- [ ] Find which runtime-materialization path produces the `Player` object
      whose generic identity later seeds `PlayerCameraAnimationDirector`, then
      test whether director stable camera state can be redirected to
      `NadineBuddy` through that path
- [ ] Find the upstream **non-zero** seeder for the transient primary pair
      `PlayerCameraAnimationDirector +0x60/+0x68` before
      `0x140238800` consumes and clears it; the current best assumption is
      that this seeder lives in an external camera/animation helper above the
      director rather than in the director's own virtual interface
- [x] The first stronger-than-`MoveTo` control ingress is now pinned on the
      shared `owner+0x15B8` layer:
      - `0x141E0ED10` switches locomotion-controller mode at `+0x34`
      - `0x141E0EDF0` applies a full snapshot into `+0x40/+0x50/+0x60`
      - a shared caller around `0x141E18B03` feeds both from
        `source+0x230/+0x240/+0x250`
- [x] `0x141DE9E60(owner, record, mode, flag)` remains the stronger real
      control/application bridge above raw `MoveTo`:
      - validates the incoming record against `0x14386FD00`
      - copies `record+0x30/+0x40` into owner `+0x130/+0x140`
      - runs owner virtual `+0x1E8/+0x208`
      - then feeds the shared `owner+0x15B8` mode/snapshot layer
- [ ] Climb above that `owner+0x15B8` mode/snapshot path and determine which
      live source path materializes the incoming state record for
      `0x141DE9E60`, then determine whether it can be redirected from
      Chloe-like control input to `NadineBuddy` for the first playable switch
      demo

## P0 - ADS body turn after melee confirmation (2026-07-15)

- [x] Reject the false skeleton-render path at `0x141B5CE90/0x141B61530/
      0x141B757F0`; it is AudioManager ARM.
- [x] Correct the decimal/hex offset error: final FgAnimData matrices are current
      `+0x230`, previous `+0x238`, not hex `+0x560/+0x568`.
- [x] Resolve the typed consumer: `FgDrawManager_PrepareInstanceForRendering
      0x14181A9B0` copies CPU `+0x230` to upload `+0x240` and separately binds the
      `FgAnimData+0x1C0` render-world matrix.
- [x] Establish body-yaw ownership: phase `0x40` publishes translation+quaternion
      through `FgAnimData_PublishWorldTransform 0x1418C55D0`; no all-joint rewrite
      is required for whole-body rotation.
- [x] Reject cz block-order bug: writing yaw to transform `+0x00` overwrote
      translation and made the model disappear on ADS. Exact layout is
      translation `+0x00`, quaternion `+0x10`; da writes only `float[4..7]`.
- [ ] Run the earned two-instance exam using only the `0x141A8FC00` phase-`0x40`
      callsite relay. Expected: peer ADS camera yaw turns the whole model; release
      restores native publication; no pose, weapon, hair or locomotion regression.

## P0 - Projectile final direction, local spawn record (2026-07-18)

- [x] REJECT the prior `0x14151B72B` network-only conclusion. Its `var_D18`
      record is both sent at `0x14151BBEF` and unconditionally stored at
      `SpawnInfo+0x28` by `0x14151BBAB` for the single-projectile path.
- [x] Correct `0x14151BC3E` ownership: its `var_A68` pointer replaces
      `SpawnInfo+0x28` only behind `[ProcessWeapon+0x906] > 1` at
      `0x14151BE21..0x14151BE33`; merely reaching BC3E does not make it live.
- [x] Resolve the separate `projectile_count > 1` online branch to
      `0x14151A00E` and `MultiProjectileBulletInstant`.
- [x] Run build `dr`: packet preparation succeeds on thread `2032`, while the
      ordinary local builder runs on worker `20032`; this CONFIRMS the
      cross-thread handoff and rejects `thread_local` correlation.
- [x] Build `ds` crossed the sender worker handoff: capture at `0x151BC3E` after
      16 ms, packet final direction received, and role B applied it at its own
      `0x151BC3E` without timeout/overflow; both processes stayed responsive.
- [x] REJECT standalone `launch_record+0x30` world-direction ownership: build `ds`
      applied the exact upward vector, but the user saw pistol and rifle bullets
      travel strongly sideways. The transport FIFO works; this receiver field
      interpretation does not.
- [x] Static-only: trace the complete launch record and its basis/transform from
      `BuildProjectileLaunchRecord 0x141520750` through
      `ProjectileBulletInstant_Init 0x14152BF20` and downstream tracer/collision
      consumers. Distinguish local-basis, compound-transform and secondary-vector
      hypotheses before selecting another runtime boundary.
- [x] Complete that trace: local call `0x14151BC3E` passes normalized `var_EA8`;
      Init builds `endpoint_ws=start_ws+record+0x30`; ProbeKick derives its ray
      from `endpoint_ws-start_ws`. This rejects a hidden local-basis rotation for
      the correctly owned record.
- [x] Run build `dt`: receiver reached BC3E with `owner1=1 peerQ=1` and applied a
      nearly world-up vector, but the user again saw a strongly sideways pistol
      shot. This rejects BC3E mutation as sufficient for single projectiles.
- [x] Resolve the authoritative boundary: capture/apply the pointer already
      selected at `ProcessWeapon::SpawnProjectile` `SpawnInfo+0x28`, and add a
      read-only `ProjectileBulletInstant::ProbeKick` endpoint/dot discriminator.
- [x] Run build `du`: selected owner/capture/apply reached concrete ProbeKick with
      the same `seq=1` and `dot=1.00000`, CONFIRMING the authoritative collision
      ray, but both processes terminated because the 14-byte entry trampoline
      clobbered ProbeKick's live `RAX=entry_rsp`.
- [x] Build/run `dv`: leave ProbeKick entry untouched and patch only direct call
      `0x14152C8A6 -> 0x14152C9B0`; require the same selected-record/dot markers,
      both processes responsive, and visible upward travel. Runtime achieved
      `seq=1`, `dot=1.00000`, both responsive, and user-visible correct travel.

## P0 - Remote firearm ammo and HUD ownership (2026-07-18)

- [x] Identify the authoritative current-magazine/reserve fields on the live
      weapon used by `PlayerFireController` readiness and projectile emission.
- [x] Trace the sender(s) of weapon icon/ammo into the process-global or per-screen
      HUD state and prove why the retained puppet can publish into role B's HUD.
- [x] Separate three competitors before runtime: (A) receiver puppet magazine zero
      blocks native shot creation; (B) HUD count is display-only while a different
      weapon field blocks fire; (C) the FirePacket is consumed but a later visual
      path is gated by ammo.
- [x] Extend the wire protocol only after the field owner is proven. FirePacket v4
      payload is selected weapon art plus magazine/reserve snapshot, applied only
      to retained puppet slot 1 and never to the local role-B player's inventory.
- [x] Write a falsifiable test requiring role-A/role-B ammo markers, one visible
      remote shot at receiver-local zero, and no mutation of role B's local ammo.
- [ ] Run build `dw` and confirm one visible zero-baseline remote shot,
      `ammoPost=N-1/R`, screen-1 weapon-HUD gating, and unchanged local ammo.

## P0 - Remote projectile origin / muzzle offset (2026-07-18)

- [x] Trace the selected launch record's origin/endpoint fields from
      `ProcessWeapon::SpawnProjectile` into `ProjectileBulletInstant` and compare
      them with the firing puppet's live muzzle/hand transform.
- [x] Separate (A) sender world origin copied through FireArgs, (B) receiver
      puppet muzzle resolved from the wrong hand/weapon attachment, and (C) a
      tracer-only visual offset while collision impact is correct.
- [x] Before runtime, predict both tracer and impact displacement from one exact
      origin field; do not alter the already-confirmed final direction relay.
- [x] Implement build `dx`: FirePacket v5 relays selected-record origin and writes
      both collision start `+0x20` and base/tracer origin `+0x70`; remove the false
      `Player+0x240 -> FireArgs+0x00` target overwrite. Release v2 DLL builds.
- [x] Run build `dx` with pistol and rifle. Matching capture/arm/apply produced
      `originApply=1`, ProbeKick `originError<=0.01`, retained `dot>=0.99`, and no
      parallel left offset in either tracer or impact. PID 16512 reported
      `originError=0.00000`, `dot=1.00000`; user confirmed exact visual alignment.

## P0 - Remote vertical ADS pose (CURRENT, 2026-07-27)

- [x] Reject `0x140CB4176` as the receiver apply boundary: build `dp` exchanged
      pitch fields but the puppet never reached `AIM-PITCH-APPLY`.
- [x] Resolve the confirmed ticking alternative: Player controller phase
      `0x140C9F420` dispatches PlayerAimController slot `+0x30` / `0x140CAFA20`
      before downstream Player publication.
- [x] Implement build `dy`: after the stock blend tick, commit peer post-blend
      `aim-add-up/down` through self-contained native `0x140CAF470`; clear both on
      ADS exit. Release v2 builds; real IDB and refreshed MCP name `0x140C9F420`
      `Player_UpdateMountedControllers`. Static evidence and competitors are in
      `reports/remote_aim_pitch_blend_tick_20260718.md`.
- [x] Run the written `dy` discriminator. User observed horizontal-only motion;
      sender had no active capture, receiver had no blend/apply marker, but the
      receiver computed `up=0.270986` at `blend=0.95`. Reject the `dy` boundary;
      the named-channel consumer was not reached and remains unclassified.
- [x] Resolve and implement build `dz`: the puppet takes `0x140CB41F0`'s alternate
      branch, which writes float `0.2` to `Aim0/Aim2/Aim4/Aim6` through native
      setter `0x14172C8E0`. After the original publisher, replace `Aim4/Aim6` with
      receiver-local directional weights. Static evidence, competitors and the
      discriminator are in `reports/remote_aim_pitch_blend_tick_20260718.md`.
- [x] Run build `dz`: user again observed horizontal-only motion. Receiver logged
      only transition-frame `AIM-PITCH-ALT-APPLY ... up=0 down=0 blend=0`, then a
      separate steady-state `up=0.191499 blend=0.866667`; no active setter marker.
      Reject `0x140CB4176` as the steady-state boundary, not `Aim4/Aim6`.
- [x] Resolve and implement build `ea`: normal steady state takes callsite
      `0x140CB416F -> 0x140CB47A0`, which smooths signed LR/UD and tail-jumps at
      `0x140CB491A` to the stock publisher. Relay only that callsite, wait for the
      original smoother/publisher, then overwrite `Aim4/Aim6` from the published
      controller fields through native `0x14172C8E0`.
- [x] Run the written `ea` discriminator. User again observed horizontal-only
      motion, but receiver logged nonzero steady-state pitch in alternate mode
      with `setUp=0 setDown=0`. Reject the existing-entry-only path: the exact
      setter returns zero because `Aim4/Aim6` are not active, so the rendered
      consumer was not tested.
- [x] Resolve and implement build `eb`: Player preallocates inactive type-2
      `AnimStateLayer` slots; stock named commit ensures/activates one and queues
      the authored direct fade before changing its weight. At the proven
      steady-state boundary, use guarded stock named commits for down/up even in
      alternate mode and log active descriptor/context/value/fade/dirty state.
- [x] Run the written `eb` discriminator. Aim4 activated with context `AD`, but
      the supplied `0.0709` weight probed as zero and ADS began shaking/jerking
      upward. Reject per-frame named commit in alternate mode. Disassembly proves
      `0x14172C8E0` is a fade-to-zero/release request, and `0x141A911A0`
      deactivates its descriptor at zero. The activate/fade/release/reactivate
      loop exactly accounts for the regression; one-time activation plus this
      function is also rejected.
- [x] Reject Aim4/Aim6 as alternate vertical data carriers. `0x140CB41F0` writes
      actual LRDU to active state `+0x188..+0x194`, but alternate mode only asks
      Aim0/Aim2/Aim4/Aim6 to fade out and release. Local screen 0 is `named` while
      puppet screen 1 is `alternate`, so their authored graph contracts cannot be
      mixed.
- [x] Implement read-only build `ec`: remove both receiver named commits and log
      bounded local/puppet mode, branch byte, base index, active-state LRDU, and
      Aim0..Aim7 descriptor/layer lifecycle fields after native publication.
- [x] Run the written `ec` discriminator. The `eb` shake disappeared, but the
      clone remained vertically fixed. Receiver alternate screen 1 preserved
      `rawUp=0.9990` exactly as active-state `stateUp=0.9990`; all Aim0..Aim7
      descriptors were absent (`found=0`) across six samples. Confirm transport
      through the publisher and reject the whole Aim registry as alternate apply.
- [x] Trace the active-state owner into `AnimControl`: `+0x58` holds three
      authored parameter buffers allocated by `0x141729480`; publisher LRDU is
      written to the third pointer at `[AnimControl+0x58]+0x10` before one
      explicit named-vs-release branch.
- [x] Resolve and implement build `ed`: validate/NOP `JNZ 0x140CB42B6`, keeping
      the stock publisher on its complete named aim-layer path. Unlike rejected
      `eb`, no second writer fights the stock alternate fade/release loop.
- [x] **REJECTED (`ed`, PIDs 2608/22216):** Aim4 became stable/active with
      `release=0` and tracked upward weight, but the character model broke
      completely. Named fallthrough is incompatible with the puppet's authored
      alternate graph; do not retry it globally or screen-1-scoped.
- [x] Remove the `ed` publisher-branch patch in stable build `ee`.
- [x] Trace the alternate copy chain: `AnimControl::BeginStepInternal
      0x141A78A60` -> type-2 layer step; layer ctor `0x14189B7B0` binds `+0x530`;
      instance init `0x1418B3F10` allocates `+0x178` and authors flag `+0x190 &
      0x20`; `0x1418A0EB0` copies before `AnimStateInstance::PhaseUpdate
      0x1418A4FD0`.
- [x] Implement read-only build `ef` base-instance discriminator for screens 0/1:
      descriptor/instance admission, copy flag, wrapper identity/sizes and
      source/destination LRDU. It contains no named-branch or pose mutation.
- [x] Run the written `ef` falsifier. Screen 1 resolved active base hash
      `7339D08C`, one live type-2 instance, `flags=0x28`, `copy=srcSame=1`, and
      matching `1840/1840` buffers. Destination LRDU followed source through
      down `0.1031 -> 0.1036` and up to `0.9554 -> 0.9534`, while the weapon
      remained vertically fixed. Confirm the complete copy into the live
      evaluator instance; reject descriptor/flag/wrapper/buffer loss.
- [x] Resolve the first authored-selection boundary. `0x1418B24F0` copies the
      state snapshot, resolves state id at instance `+0x98`, overlays authored
      callbacks/flags and selects the root; `0x14189D720` feeds it through
      `0x1418A4670` and `0x1418A4FD0`. The phase callback at `+0xE8` receives the
      already-confirmed parameter descriptor.
- [x] Implement read-only build `eg`: log bounded screen-0/screen-1 state id,
      snapshot word, callback-presence mask, flags, a scalar-tuple subset and raw
      root index during nonzero vertical aim. It performs no animation writes or
      snapshot replay.
- [x] Run the written `eg` falsifier. The user still saw no vertical pose, but
      sender/receiver matched on three state/callback/flag/scalar fingerprints.
      Reject an always-different high-level state selector as the sole cause.
- [x] Correct the root-field interpretation statically: instance `+0x130` is a
      `SnapshotNodeHeap*`, `+0x138` is its per-instance node index, selected by
      `0x1418B2EF0` and resolved by `0x1418C2F90`. Raw indices cannot be compared
      across processes as root identity.
- [x] Implement read-only build `eh` for resolved root vtable/RTTI RVA and the
      complete config tuple: mode/apReference, all scalar fields, double-read
      authored-key hash, and double-read override hash/current outer+inner match.
      It manually bounds the heap/table layouts and never invokes native
      resolver/override functions or writes animation state.
- [x] Run the written `eh` falsifier. Five screen-0/screen-1 pairs aligned within
      15-16 ms matched root vtable RVA `02B15218`, TD RVA `03771508`, state,
      callback/flags, all scalars, mode/apReference, key hash, override result,
      heap max, instance flags and copy bit. Only root index and activation serial
      differed; reject concrete-root/config routing as the missing-pitch cause.
- [x] Resolve concrete root vtable `0x142B15218` as
      `AnimSnapshotNodeBlend`. Vslot `+0x68` / `0x14191E550` only steps children;
      reject it as a parameter consumer. Actual input evaluation is vslot
      `+0x58` / `0x14191E180`, which evaluates `anim-node-blend-func` into
      `node+0x60`; vslot `+0x70` / `0x14191E5F0` emits that value in command
      `0x60007`. RTTI/factory, call/dataflow, VM layout and downstream command
      evidence satisfy the static quota.
- [x] Implement read-only build `ei`: bounded root weight/VM fingerprint and
      resolved immediate-child RTTI/typed dynamic fields. It calls no native
      resolver, VM or animation method and writes no game state.
- [x] Run the written `ei` falsifier with stable center/up/down ADS motion. Compare
      aligned screen-0/screen-1 root weight, VM code/constants, child RTTI and
      typed child dynamics. Ignore raw heap indices/pointers. A full match moves
      the next static trace to slot-14 command application.
- [x] Trace the nested Blend spine below root child 0. Prove which command
      child contributes at weight zero; then implement a bounded read-only walk
      over only the validated Blend layout, stopping at the first non-Blend RTTI.
      The `ei` result matched root VM/code/constants, child RTTI and typed child
      dynamics; root weight was a vertical-independent transition decay.
- [x] Run the written `ej` falsifier. Stable samples must select root child 0,
      then nested Blend child 1. Compare normalized depth VM/weight/selected RTTI
      across screens; ignore mixed `stop=4` transitions. A full match moves the
      next static trace into the first common non-Blend class. Result: the full
      four-Blend spine matched and ended at `AnimSnapshotNodeAnimation`
      (`02B14D68/037713E8`); only raw heap indices differed.
- [x] Statically prove the selected `AnimSnapshotNodeAnimation` layout and trace
      its clip/phase data through `RefreshPhase 0x141918120`, `StepPhase
      0x1419180A0`, and `GenerateAnimCommands 0x1419182E0`. Enumerate phase/clip
      versus later evaluator/palette competitors and satisfy the three-type
      evidence quota before adding another bounded read-only probe. Result:
      0x80-byte layout, ids/remap/clip fields, phase VM/pair and command `0x60006`
      consumption are offset- and vtable-proven; build `ek` is read-only.
- [x] Run the written `ek` falsifier. Compare normalized Animation source/resolved
      ids, remap keys, clip-consumed words, phase VM and current/previous phase
      during stable center/up/down ADS. An id/clip split selects authored clip
      selection; a phase-only split selects `anim-node-phase-func`; a complete
      match moves the static trace into command-case-6 context/plugin/joint-mask
      consumers. Ignore raw indices/pointers and any invalid/torn sample. Result:
      all 28 valid samples matched exactly and phase stayed zero through up/down.
- [x] Before instrumenting command case 6, statically prove a bounded inventory
      of every active type-2 `AnimControl` registry layer and its first evaluated
      `AnimStateInstance` root. Compare local and puppet semantic layer ids/types,
      active/track state and normalized root RTTI; do not force or synthesize the
      model-breaking named aim layer. A matching complete layer inventory returns
      the frontier to case-6 context/plugin/joint-mask consumers; a concrete
      differing layer becomes the only next tree to trace. Result: registry,
      concrete layer/track, live instance and evaluated-root layouts have four
      independent evidence types; read-only build `el` double-reads every bounded
      chain and exposes only normalized semantic fields.
- [x] Run the written `el` falsifier with strong vertical ADS on both copies.
      Compare valid inventories as sets keyed by channel id/context: layer type
      and fade words, live track/instance state and normalized root RTTI. Ignore
      raw ordering/pointers/root indices and isolated fade transients. A stable
      concrete difference selects only that layer; a complete match moves to
      command-case-6 context/skeleton/joint-mask/plugin/palette consumers.
      Result: all eight samples were complete with the same seven active
      channels and stable live instance/root signatures. One early three-track
      `E2B990B9` transition collapsed after 500 ms; only fade-time residues
      differed. The complete active-layer inventory is rejected.
- [x] Trace `ProcessAnimCmdListSegment 0x141AB97C0` case 6 statically in both
      directions. Prove the complete command payload and owning command context,
      resolve skeleton/joint-set/plugin inputs and the downstream pose-merge or
      palette publication edge, enumerate competing boundaries, and satisfy the
      three-type evidence quota before defining any `em` runtime probe. Result:
      five evidence types prove exact `0x60006` payload/context, the underlying
      skeleton-id direct branch, SkelTable retarget pair/group mapping and both
      branches converging on the processing-group pose output. Six strong STATIC
      labels were saved and verified in the real IDB.
- [x] Implement read-only build `em` at the proven case-6 boundary. Select only
      the exact `ek` clip signature; reconstruct and double-read the owning
      FgAnimData/context; log normalized source/target ids, direct-vs-retarget
      prediction, processing group, entry feature/coverage summary and callback
      RVAs under strict per-screen bounds. Never call a resolver/callback or log
      pointers/output indices. Result: the verified 12-byte entry detour validates
      the full bounded list, exact retained FgAnimData/screen and every selected
      asset/skeleton/entry chain before four rate-limited samples per screen; both
      Release DLLs build with only the pre-existing warning.
- [x] Run the written `em` falsifier during strong up/down ADS on both copies. A
      stable branch, retarget pair/group/coverage or callback split selects that
      exact route; a complete normalized match rejects case-6 inputs and moves
      to the common pose merge/palette consumer. Ignore raw-address/index-only
      differences and invalidate torn, incomplete, crashing or visible-changing
      samples. Result: **INCONCLUSIVE selector miss**, not a rejection. Both
      processes were stable and vertical ADS reached `1.0`, but current runtime
      leaf `0B/27E57F43/FD6DBE03/09A0` did not match `em`'s deliberately narrow
      `0A/6C2B2396/4B0F5DE3/09A0` selector, so no `AIM-CASE6` records were
      eligible.
- [x] Extend the read-only selector to the two exact observed case-6 leaf
      signatures and rerun the same falsifier. Keep the signatures distinct in
      normalized output and compare only complete same-signature records across
      screens; retain all ownership, ADS/vertical, list-bound and double-read
      guards. Result: build `en` accepts only the exact `0A/6C2B2396/4B0F5DE3/09A0`
      and `0B/27E57F43/FD6DBE03/09A0` set, logs the full signature and preserves
      every prior read-only guard; the fresh single-DLL target builds cleanly.
- [x] Run build `en` during strong up/down ADS on both copies. Compare only
      complete records carrying the same four-word signature; a stable branch,
      pair/group/coverage or callback-RVA split selects the retarget route, while
      a complete normalized match rejects case-6 inputs and moves to the common
      output merge/palette consumer. Result: **INCONCLUSIVE boundary miss**. Both
      exact signatures and `up=1.0` were independently observed, both processes
      stayed stable, but no `AIM-CASE6` record reached the falsifier. The earlier
      selector-miss explanation is rejected; direct/retarget remains untested.
- [x] Statically prove the same-instance path from the selected
      `AnimSnapshotNodeAnimation` leaf through its `0x60006` command into the
      actual command-list consumer and owning FgAnimData/context. Resolve whether
      `ProcessAnimCmdListSegment` is bypassed for this leaf or the current context
      reconstruction selects a different owner before defining another runtime
      probe. Do not widen the signature filter again. Result: the exact
      source-asserted call chain preserves one command-list record into
      `ProcessAnimCmdListSegment`; no bypass exists. `en` instead reconstructed
      FgAnimData from scratch at context `+0x18`, while two independent callers
      prove `&FgAnimData+0x1C0` is context `+0x10` and scratch is `+0x18`.
- [x] Run read-only build `eo` during strong up/down ADS on both copies. It fixes
      only the statically proven context layout and keeps the exact two-signature
      selector. No `AIM-CASE6` despite eligible leaf/vertical telemetry rejects
      the layout explanation; complete same-signature normalized equality moves
      to the common pose/palette path, while a stable branch/retarget/callback
      split selects that exact route. Any crash, write, torn sample or visible
      model change invalidates the test. Result: the corrected layout produced
      eight complete records and is confirmed, but all were screen 0. The two
      local processes used different signatures/branches, so the required
      same-signature local/puppet comparison remains unexecuted.
- [x] Implement read-only build `ep`: keep the local screen-0 ADS-bit
      requirement, but remove that mod-only bit gate for retained screen 1 after
      exact Player/Fg ownership is proven. Still require at least 0.20 finite
      active-state vertical weight, one of the two exact signatures, bounded
      command/assets and all double-reads. Result: only the screen-1 ADS-bit test
      is omitted; the single Release DLL builds successfully.
- [x] Run `ep` with strong vertical ADS on both copies. Compare only complete
      same-signature screen-0/screen-1 records across the two processes. No
      screen-1 record despite eligible leaf/vertical telemetry rejects the gate
      explanation; equality moves to common pose/palette output, while a stable
      semantic branch/target/retarget/callback split selects only that route.
      Result: eight complete screen-0 records, zero screen-1 records despite
      live bidirectional state-net. Removing the retained ADS-bit gate changed
      nothing, so that explanation is rejected; case-6 semantics remain untested.
- [x] Implement bounded read-only build `eq` with per-stage case-6 counters:
      structurally valid list, valid context layout, derived Fg match to retained
      slot 0/1/other, valid active-state vertical, and exact selected signature.
      Emit only a small normalized summary; do not log pointers, widen the two
      signatures, or weaken the actual `AIM-CASE6` record guards. A zero slot-1
      Fg match selects owner/context tracing; a slot-1 match without vertical
      selects timing/admission; a vertical slot-1 match without signature selects
      command selection; a fully eligible call without output is a probe bug.
      Result: process-local counters emit at most four summaries only after
      vertical activity; the Release DLL builds successfully.
- [x] Run `eq` with strong vertical ADS on both copies and classify the first
      zero slot-1 stage exactly as written above. Do not alter animation state or
      define another payload probe until the missing stage is selected. Result:
      slot-1 owner and active-state counts reached 4260/8050, but retained
      vertical was exactly zero in both processes; vertical admission is the
      first missing stage.
- [x] Implement build `er` on the proven steady publisher boundary: capture
      finite local screen-0 `up/down * blend` after stock `0x140CB47A0`; on
      screen 1 read a fresh `kStateFlagAimPitch` packet and replace only active
      state `+0x188/+0x18C` after the stock publisher. Add one bounded nonzero
      capture/apply marker per process. Do not touch named channels, FgAnimData,
      roots, palettes, case-6 payloads or other LRDU fields. Result: exact
      owner/freshness/range guards and one-shot markers are implemented; the
      single Release DLL builds successfully.
- [x] Run `er` with strong vertical ADS in both directions. Require nonzero wire
      capture and receiver apply plus retained `AIM-CASE6`; reject the boundary
      if any marker is absent. If retained case-6 appears, compare only complete
      same-signature local/puppet records and separately verify visible pitch,
      horizontal yaw/projectile alignment, ADS exit, and model stability. Result:
      wire markers were absent, so the new hook is not confirmed. However both
      same-signature local/retained case-6 pairs were complete and semantically
      matched while the user saw no vertical pitch and no other regression.
      Reject case-6 inputs and move to their common pose/palette output.
- [x] Statically trace the common pose result after direct
      `AnimCmdEvaluator_ApplyAnimationDirect 0x14217C7F0` and retarget copy
      `AnimCmdEvaluator_CopyMaskedPose 0x14217D280`. Resolve processing-group
      work-buffer ownership, later command blend/merge consumers, and the exact
      publication into FgAnimData palette/skinning buffers. Enumerate output
      overwrite versus joint-mask versus late-publication competitors and meet
      the three-type evidence quota before another runtime probe. Result: command
      3 finalizes inverse ValidBits/default ranges into WorkBuffer JointParams,
      command 16 executes skeleton joint programs into a 64-byte matrix workspace,
      and command 26 converts it directly into the generated
      `FgAnimData+0x230 + 48*first_joint` destination. Four STATIC labels are saved;
      build `es` records normalized late-command semantics and post-return palette
      response without writes.
- [x] Run read-only build `es` from a low vertical value through a strong up/down
      sweep for both pistol and rifle so both exact signatures get four local and
      retained group-0 samples. A stable same-signature topology/blend/plugin hash split
      selects late command overwrite; matching summaries with a retained-only
      missing palette response selects evaluator/ValidBits/joint-program output;
      corresponding palette responses with no visible pitch move the frontier
      after `FgAnimData+0x230`. Any palette guard failure, crash or new model
      regression invalidates the test. Result: all records had stages `3/16/26 =
      1/1/0`, zero valid palette targets and zero writes. The selected wrapper
      invocation did not contain opcode 26, so palette deltas were unrelated and
      the test is INCONCLUSIVE. No visible pitch and no regression across three
      weapons. Parked; on resume correlate WorkBuffer across command-list segments
      and probe only the segment that actually publishes opcode 26.
- [x] **Runtime ownership discriminator and static writer trace (2026-07-27):**
      the user observed
      that moving Nadine's local camera vertically immediately moves Chloe's
      remote weapon in the same direction even while Chloe's real aim on her own
      copy is different. Existing logs agree: Nadine receiver screen 1 carried
      `up=0.8248..0.8929` while Chloe sender screen 0 was near `up=0.18` and later
      `down=0.2505`. The vertical consumer is therefore alive; the retained
      channel is sourced from receiver-local camera pitch instead of remote-owner
      pitch. Exact ABI/dataflow proves `0x140CB416F` passes pre-blend
      `XMM3=up` and stack arg5 `down` into `0x140CB47A0`, which smooths them and
      tail-jumps to publisher `0x140CB41F0`. The old hook called native with the
      receiver-local inputs, then wrote peer pitch only to active state, too late
      for controller smoothing and publication. Build `iu` captures sender raw
      inputs on both transition/steady callsites and substitutes only retained
      screen-1 up/down before native. Rejected named layers, graph forcing and
      pose-copy remain parked.
- [x] Run the `iu` opposite-direction discriminator. Receiver PID 10820 replaced
      `localUD=0.1069/0` with peer `0/0.1306`; the following screen-1 controller,
      active state, live evaluator and bounded residual/blended samples all
      retained peer down near `0.13`. A later stale/missing sample explicitly
      zeroed receiver-local up `0.1845`. Nevertheless remote Chloe still followed
      Nadine's local free-camera pitch, including outside ADS. Confirm raw-pitch
      transport and live evaluator admission, but reject `0x140CB416F`, the LRDU
      publisher and evaluator copy as the final visible-pose owner.
- [x] Trace the late visual owner statically before another game launch. Resolve
      both sides of `Player_PostAnimUpdate 0x1410E9210`, the scoped pose-seed
      returns at `0x140CB17D6/0x1410E934B`, writers/readers of
      `Player+0x1720/+0x1730`, and weapon/hand attachment updates
      `0x1410EDFB0/0x1410ECAF0`. Distinguish late pose-seed overwrite, direct
      camera/target consumption, FgAnim world ownership and a later LRDU writer;
      require three independent evidence types and a written prediction before
      any mutation. Static result: PostAnim copies the screen route directly to
      `+0x1720/+0x1730`; the wrist solver has no camera/LRDU read and publishes an
      already-resolved skeleton transform through `*(Player+0x3600)`; FgAnim
      world `+0x1C0` is independent of the joint-palette path.
- [x] Run read-only build `iw` with peer Chloe aiming strongly down while Nadine
      moves her local non-ADS camera up, then reverse. Compare only bounded
      `AIM-LATE-OWNER` rows: peer/controller/pose/root plus the raw FgAnim 3x3
      matrix and two wrist float4 values. Local pose only identifies the inert
      screen-route cache; first local correlation in FgAnim selects world
      publication, first correlation in wrist selects skeleton/post-palette, and
      no sampled correlation moves the search after attachment/render space.
      Local controller UD selects a later LRDU writer. No writes or visible
      behavior changes are allowed. **REJECTED:** Chloe's animation collapsed
      and slid along the floor before any `AIM-LATE-OWNER` row was emitted. Build
      `ix` removes the sampler completely. Next discriminator must capture
      arguments at a proven native writer/callsite; do not traverse the late
      FgAnim/wrist pointers from the post-update hook again.
- [x] Verify the `ix` rollback independently. Chloe's normal animation returned,
      while her remote vertical weapon pose still followed Nadine's local
      non-ADS camera. PID 15396 simultaneously logged peer-correct native
      `UD=0/0.1055` over local `UD=0/0.0338`, so rollback and original pitch leak
      are separate.
- [x] Select the next discriminator statically at an existing native writer or
      exact callsite with live arguments. Prefer the smallest boundary that
      separates FgAnim world publication from joint-palette/wrist output; record
      its ABI and three independent evidence types before adding any probe.
      Selected `0x141ABAE8C -> AnimCmdEvaluator_WriteSkinningPalette
      0x14218B170`: exact ABI is joint count, 64-byte workspace, 48-byte bind
      rows, common 3x4 basis and stack palette destination; the callee writes
      three float4 rows per joint to the final `FgAnimData+0x230` range.
- [x] Run bounded build `iy`. Peer Chloe aims clearly down/straight; on the
      Nadine receiver, without ADS, move local camera sky -> ground -> sky.
      Require normal animation and `AIM-PALETTE-WRITER` rows with stable peerY
      and changing routeY. Workspace-correlated deltas select pre-writer pose
      contamination; palette-only deltas select bind/basis conversion; stable
      workspace and palette while the visual moves select post-palette
      render/attachment. **INCONCLUSIVE:** animation remained normal but no
      `AIM-PALETTE-WRITER` row was emitted. Probe removed completely.
- [~] Separate the per-process shared camera/look source by Player ownership.
      Runtime shows both local and retained characters following the same local
      camera vertically on each copy, while `iu` still delivers peer-correct
      screen-1 LRDU. **STATIC / IMPLEMENTED `ja`:** Player vslot `+0xE80`
      (`0x1410D1B20`) returns a float4 camera look-at point, its exact stock
      fallback `0x1410D25C0` reads `g_LocalPlayerInputManager_Ptr`, and the same
      Player's `+0xEA0` consumes it before native filtering and the broad
      `Player+0x38C0` field. Call stock once and retarget only the retained
      Player's returned vector from the receiver camera quaternion into the
      fresh peer quaternion. `SetLookPoint/SetLookLine` were eliminated for lack
      of an ordinary per-frame caller; direct `+0x38C0` writes were eliminated
      because `ProcessJumpSpace` also reads the field. Runtime prediction is in
      `COOP_STATUS.md`. **`ja` INCONCLUSIVE:** behavior unchanged and no success
      marker, but external memory proves the live Player vtable slot was patched.
      **`jb` RUNTIME REJECTED:** the symptom is unchanged and the active session
      contains zero unconditional `PLAYER-CAMERA-BOUNDARY` rows, although both
      live vtable slots point into the DLL and screen-1 `AIM-TARGET-EARLY` /
      `AIM-PITCH-PRECALL-APPLY` markers are present. Therefore active gameplay
      calls neither patched boundary in this state. Build `jc` removes the
      entire `+0xE80/+0xEA0` relay and telemetry. Continue statically from the
      later common head/look animation layer or `Player+0x36C0`; do not revive
      these vslots as the visible owner without new reachability evidence.
      **`jd` STATIC / IMPLEMENTED:** the actual animation-manager phase reaches
      `Player_PostAnimBlending_Async -> Player+0x1E38 ->
      PlayerLookAtIkController_UpdateIk 0x140EDA680`. A read-only wrapper captures
      both authored target slots and the selected/effective target around one
      unchanged native call for slot 0/1. Opposite local/peer camera signs will
      determine whether shared pitch enters before head IK or whether the entire
      LookAtIK path can be excluded in favor of the later weapon-arm controller.
      **`jd` RUNTIME REJECTED / `je` cleanup:** both Player controllers were
      reached on both processes while local/peer camera Y had strong opposite
      signs, but both authored lifetimes stayed `-1`, all eased weights were
      zero, selected mode/enable were zero and every target row stayed sentinel
      NaN. The user still saw the fault. LookAtIK and its camera-derived vslot
      `+0x40` are not the visible owner; `je` removes the whole probe. Continue
      statically from `PlayerWeaponAnimControl_UpdateHolsterIk 0x14108ECE0` and
      its upstream writer graph. **`jf` STATIC / IMPLEMENTED:** that holster IK
      path is authored three-joint arm-only code with no camera/route input, and
      `0x1410F4E80` belongs to a non-animation host-anchor/target path. The exact
      aim chain instead publishes correct per-Player LRDU and then dispatches
      the target-bearing `aim-at` event at `0x140CAF008` through hard-coded
      animation-process registry record 0. Native `0x140C70760` independently
      maps a Player to the correct outer process in the same 32x`0xB0` registry;
      `0x140C70900` uses that mapping for AnimControl and `play-animation`.
      Build `jf` reroutes only fresh screen-1 ADS events to that owner process,
      preserves screen 0 and logs one bounded `AIM-EVENT-ROUTE` discriminator.
      The opposite-camera prediction and competing H1/H2/H3 outcomes are in
      `COOP_STATUS.md`. **`jf` RUNTIME REJECTED / `jg` cleanup:** both hooks
      installed, but the only route attempt reported `mapped=0 index=UINT_MAX`
      and called the unchanged native process; the user saw no change. Caller
      dataflow proves `0x140C70760` accepts a child `Process*`, not `Player*`:
      callers obtain it from generic actor vslot `+0xF48` or a Process factory
      and destroy it with `ProcessKillTree`. Build `jg` removes the complete
      event callsite/TLS relay. Do not retry a Player-key lookup; trace record 0
      and the actual `aim-at` consumer before another runtime mutation.
      **STATIC REJECTED:** that trace is complete. The same 32x`0xB0` registry
      dispatches `internal-event-torch-light`, `internal-event-torch-put-out`,
      `detach`, `flicker-and-die` and `override-radius`; `0x140C73040` identifies
      itself as `DcPlayerSpawnTorch` from `game/player/flashlight.cpp` and emits
      `turn-on-torch`. `aim-at` is a flashlight effect target, not the Player
      head/upper-body/weapon pose owner. Continue from the active base
      animation/evaluator graph and do not revive this event path.
      **STATIC / IMPLEMENTED `jh`:** Player_Update supplies an exact live
      per-Player boundary: vslot `+0xE58` (`0x1410ED500`) is called
      unconditionally at `0x1410ECA2B` and writes named chest/neck/AimAt/
      SecondAimAt points. Its camera branch uses Player+0x4010 and the routed
      pose seed; its fallback copies chest/neck. Native-reader audit finds only
      targeting/projection/notification/fast-grapple/host-anchor consumers plus
      debug drawing, so a visible authored consumer is not yet proven. The
      read-only wrapper records slot-0/1 before/after fields and compares
      them with local/peer camera signs. Local-correlated non-fallback AimAt
      selects contamination at this boundary but still requires a later
      substitution falsifier; chest/neck equality rejects it. Separately,
      `PlayerAimingMachine` is STATIC REJECTED: it is embedded in CameraEdgeAim/
      CameraControlZoom, completely overwrites copied camera-0 orientation from
      per-machine inputs and outputs only CameraLocation. **`ji` RUNTIME
      CONFIRMED / `jj` RUNTIME REJECTED:** the repaired writer probe proves that
      screen-1 `+0x38F0/+0x3900` follows the receiver-local route rather than the
      opposing peer seed while the camera branch is active. However the exact
      `NetAnimCmdGenerator::UpdateFlagsAndWeapons 0x140A07160` vslot `+0x68`
      wrapper produced zero screen-0/1 entries on both responsive campaign
      processes. Therefore its static `SecondAimAtPos -> Player+0xF130` copy is
      not the live puppet consumer; the `jj` hook and offsets are removed. H2
      is now **STATIC CONFIRMED** at the native-consumer level: the exhaustive
      `SecondAimAtPos`/slot `+0x2D8` inventory resolves only AI knowledge,
      authored NPC LookAim, targeting/action/debug paths and the runtime-inactive
      NetAnim consumer. The paired `hub+0xA19/+0xA20` route is NPC-only; Player
      uses a different vslot `+0xE38`. No consumer reaches Player/FgAnim/joints.
      Treat `+0x38F0/+0x3900` as contaminated targeting metadata, not the visible
      pose owner. Continue from a campaign-live Player AnimControl/base-state
      command source toward FgAnim/joints; do not patch `0x1410ED500`,
      `+0xF130/+0x1D59`, the NPC hub, or `PlayerRunNGunController` without new
      reachability and receiver-identity evidence.
- [x] Trace the campaign-live Player command source to the final palette writer.
      `Player+0x1A0 AnimControl -> AnimControl_CreateAnimCommands 0x14172C370 ->
      AnimManager_CreateAnimCmdsForObject 0x141A8FCF0 ->
      AnimManager_PerformAnimBlend 0x141A92260 -> ProcessAnimCmdListSegment
      0x141AB97C0 -> opcode 26 -> 0x141ABAE8C ->
      AnimCmdEvaluator_WriteSkinningPalette 0x14218B170 -> FgAnimData+0x230`.
      Reject `Player+0x3FC8` as the MP-only pArtGroups table and `Player+0x1B30`
      as a NetCharacter-only generator location. The four-part evidence quota,
      competitors and corrected `iy` gate analysis are in `COOP_STATUS.md`.
- [x] Run read-only build `jk`: leave the remote owner's free-look camera at a
      stable down/straight direction, switch to the receiver, stay out of ADS
      and move its local camera sky -> ground -> sky. Fresh synchronized
      free-look camera or strict-ADS pose seed is accepted. Require normal
      animation and bounded
      `AIM-PALETTE-WRITER-JK` rows. Workspace/local correlation selects
      pre-writer contamination; stable workspace with bind/basis/palette change
      selects conversion; all stable while the visual moves selects a
      post-`FgAnimData+0x230` consumer. **INCONCLUSIVE:** normal animation and
      two exact writer rows confirm ABI/admission, but the global counter sampled
      different groups and reset the baseline for both; no delta was measured.
- [x] Run build `jl` with the same stationary remote free-look and receiver
      sky -> ground -> sky sweep. Require at least two
      `AIM-PALETTE-WRITER-JL` rows from the same `group/first/count`; per-group
      workspace versus bind/basis/palette deltas then select P1/P2/P3. Zero
      repeated group samples is admission failure only. **RUNTIME NARROWED:**
      groups repeated safely and bind/common-basis hashes stayed exactly constant,
      rejecting P2, but alternating palette destination pointers still reset
      every copied baseline. P1 versus P3 remains unresolved.
- [x] Run double-buffer-safe build `jm` with Chloe camera held down and Nadine
      sky -> ground -> sky while both characters stand still. Require repeated
      non-reset `AIM-PALETTE-WRITER-JM` rows from one group. Correlation between
      `dirDelta local`, total workspace/palette delta and stable peer Y selects
      P1; idle-scale/non-correlated workspace while the puppet visibly follows
      local camera selects P3. Bind/basis must remain zero-delta. **RUNTIME
      CONFIRMED P1:** on PID 15964 the retained screen-1 workspace changed at
      Nadine-local `+1.0436/-0.5039/-0.3756` vertical edges while Chloe peer Y
      remained near `-0.21`; groups 1/2/7 all changed, bind/basis stayed exactly
      constant, and the final palette followed workspace. The mirror PID saw
      the same Nadine edges through peer Y. The false input is therefore already
      in the workspace consumed at `0x141ABAE8C`, not introduced by opcode-26
      conversion or a post-palette consumer.
- [x] Statically enumerate every `ProcessAnimCmdListSegment 0x141AB97C0` opcode
      handler that can mutate the shared 64-byte/joint workspace before opcode
      26. Opcode 16 at `0x141ABA973` is the normal broad producer: it maps final
      JointParams (`SegmentContext+0x50`) and matrix workspace (`+0x58`) into the
      authored skeleton-program evaluator `0x14217B630`. Opcode 15 only allocates
      and initializes the root row; the normal post-case16 opcode-24 plugins and
      opcode 26 read the workspace. Direct dataflow, generator order, resolved
      built-in operator dispatch and plugin hashes satisfy the evidence quota;
      J1/J2/J3 and the written discriminator are in `COOP_STATUS.md`.
- [x] Run bounded build `jo`: Chloe camera held down; on Nadine's copy, both
      characters stationary and out of ADS, move the local camera sky -> ground
      -> sky. Both processes remained responsive but emitted no boundary rows:
      INCONCLUSIVE admission failure. Static reinspection rejected the guard
      equating zeroed `SegmentContext+0x08` with the outer AnimCmdContext.
- [x] Run corrected build `jp` with the same Chloe-down / Nadine
      sky -> ground -> sky sequence. Require normal models and repeated
      `AIM-JOINT-BOUNDARY-JP` rows. Both processes remained responsive with no
      faults but emitted zero rows. REJECT the same-segment scope: fresh
      selected-aim inventory was again `stages=1/1/0` in group 0, while the
      confirmed opcode-26 writer runs separately in groups 1/2/7.
- [x] Replace the rejected cross-segment wait with a read-only case-16-return
      discriminator. Arm any ownership-validated retained screen-1 segment that
      contains opcode 16, snapshot JointParams `+0x50` before/after and the
      current-group workspace at `+0x58 + 0x40` after native return, and log by
      processing group. Do not require or hook opcode 26 for this boundary.
- [x] Run bounded build `jq`: Chloe camera held down; on Nadine's copy, both
      characters stationary and out of ADS, move the local camera
      sky -> ground -> sky. Require normal models and repeated
      `AIM-JOINT-BOUNDARY-JQ` rows. Both processes remained responsive and
      fault-free but emitted zero rows: INCONCLUSIVE because the marker followed
      several unreported dynamic guards; do not infer Q3 from absence alone.
- [x] Add a bounded read-only `jr` admission audit around the already installed
      outer segment scope and exact case-16 callsite. Separately report owner/
      opcode/direction admission, raw callsite reachability, live group
      `first/+0x10/+0x12` values, buffer admission and program-inventory/finite
      status. Preserve the native call exactly once and write no game data.
- [x] Complete the static late-command audit before running `jr`. Case 3 publishes
      final JointParams at `SegmentContext+0x50`; case 16 and every resolved
      built-in/custom joint operator are closed over the four supplied buffers
      and contain no Player/camera/global read. Campaign type 16 is an output-only
      `GestureJointReader` emitting decimal command 23, not NetAnim. Decimal
      command 17 independently connects the optional `FgAnimData+0x120` scalar
      bank and one uncleared cross-group scratch block through encoded scalar
      selectors. Constructor/reset guarantee `+0x120` starts null and no semantic
      nonzero writer with a proven FgAnimData base was found; alias/whole-copy and
      live authored read-before-write remain OPEN. Exact evidence, competitors
      S1/S2/S3 and rejected alternatives are in `COOP_STATUS.md`.
- [x] Run bounded build `jr`: hold Chloe's camera down; on Nadine's copy keep
      both characters still and out of ADS while moving only the camera
      sky -> ground -> sky. Require normal models and at least one screen-1
      `AIM-JOINT-ADMISSION-JR inner` row; classify R1/R2/R3/R4 exactly as written
      in `COOP_STATUS.md` before changing any animation state. Result:
      INCONCLUSIVE by the written rule: both processes produced armed outer rows
      but zero inner/post/boundary rows. Static correction proves the inner
      marker compared the FgAnim skeleton at outer `+0x00` against the distinct
      joint-program skeleton loaded from outer `+0x40`.
- [x] Implement read-only build `js`: carry both skeleton identities through the
      TLS scope, compare case-16 `SegmentContext+0x28` with the program skeleton,
      and emit a bounded raw admission row before the old target/group filters.
      Preserve the native call exactly once and write no game state.
- [x] Run `js` with the same opposed-camera sequence. Require normal models, an
      armed outer row and at least one `AIM-JOINT-ADMISSION-JS raw` row. Exact
      target/group/segment outcomes and the follow-up post/boundary decisions are
      written in `COOP_STATUS.md`. Result: both processes emitted 12/12 raw rows;
      segment/pointers/group/buffers passed, but target equality failed. The
      logged `context+0x40` value was the JointParams allocation, not a skeleton.
- [x] Correct the static dereference and implement read-only build `jt`.
      `0x141AB9852` performs `RAX=[context]` then `R15=[RAX+0x40]`; carry that
      FgAnim-target child as the program skeleton and compare it with
      `SegmentContext+0x28`. Preserve native execution exactly once.
- [x] Run `jt` with the same opposed-camera sequence. Require `raw target=1` on
      both processes, then classify the first post/boundary stage exactly as
      written in `COOP_STATUS.md`. Result: both processes passed exact skeleton,
      group and buffer admission; all valid boundaries left JointParams
      bit-identical while complex programs changed workspace. Group 0 was lost
      only because its mixed raw words failed the obsolete finite-float filter.
- [x] Design and implement read-only build `ju` from the classified `jt` result.
      Hash mixed group-0 data as raw words, retain non-finite counts, decode
      opcode-54 authored source/destination base pairs, and inventory custom
      opcodes 128--133. Preserve the native call exactly once and write no game
      state.
- [x] Run `ju` with both characters still and out of ADS: hold Chloe's camera
      down, then on Nadine's copy move only the camera sky -> ground -> sky.
      Require normal models, responsive processes and group-0 boundary rows on
      both sides. Classify H54/Hcustom/Hupstream using the exact prediction in
      `COOP_STATUS.md`; do not clear scratch or substitute a scalar. Result: all
      140 bounded group rows were captured with zero in-call JointParams word
      changes. Opcode 54 exclusively copies base 2 -> shared scratch (base 3),
      so H54-direct is rejected while Hcustom versus Hupstream/builtin remains.
- [x] Resolve the exact encoded reads/writes of the live custom opcodes 128--132
      statically. Then design a bounded read-only per-operator boundary that
      identifies the first workspace-changing opcode and distinguishes it from
      an already-different pre-custom workspace. Do not patch an operator table
      or run another game until the three-evidence quota and a written
      falsifiable prediction are recorded in `COOP_STATUS.md`. Result: op129,
      op130 and op131 are the only common live candidates; exact dispatcher,
      payload descriptors and encoded destinations are recorded there.
- [x] Implement read-only build `jv`: preflight and wrap builtin opcode 54 plus
      custom slots 128--133 through the confirmed global tables, sample only the
      10/12-custom target groups, compare bounded workspace/JointParams before
      and after each original call, and expose unwrapped gaps. Each original
      handler remains exactly once.
- [x] Run the written `jv` stationary camera sequence and classify the first
      workspace writer as H129/H130/H131, Nadine-only H128, Hbuiltin or Hupstream.
      Require paired begin/end rows, zero `deltaJP`, normal models and responsive
      processes before accepting the result. Result: 24/24 paired samples and
      288/288 readable steps had zero direct JointParams/workspace changes in
      op54/custom 128--133. Two isolated one-op gaps identify builtin opcode 40:
      Chloe changed 48 words starting at 448; Nadine changed 15 starting at 704.
      Live descriptors and static worker stores reproduce both exact offsets.
- [x] Trace opcode 40's exact isolated inputs before another runtime test.
      Competing hypotheses are H40-JP (its 48-byte local JointParams record is
      already receiver-camera-dependent), H40-parent (its selected 64-byte
      source workspace matrix is), or both. Resolve the live descriptor records
      and downstream matrix ownership statically, then implement only a bounded
      read-only before/after capture of those inputs and output. Result: op40
      propagates Chloe joints `26->27`, `96->97`, `102->103` and Nadine
      `42->43`; the earlier parameter-count prefix does not cover all exact
      inputs. Build `jw` wraps only slot 40 and records those inputs/output.
- [x] Run the written `jw` camera-distinct sequence and classify H40-JP,
      H40-parent or H40-both from exact per-record deltas. Require paired rows,
      `targets=1`, four records, no reject, normal models and responsive
      processes. No pose/matrix substitution is authorized by this test.
      Result: 13/13 paired calls, 52/52 readable records, no rejects. Parent
      matrices and outputs changed for every chain. JointParams also changed for
      Chloe joints 97/103 and Nadine joint 43, but stayed bit-identical for Chloe
      joint 27. On Chloe's receiver the changing inputs followed receiver-local
      camera while remote Nadine was stationary, proving the contamination is
      already upstream of opcode 40.
- [x] Resolve the static parent-writer candidate set for the rendered actors.
      Exact actor-package records identify earlier opcode-40 destinations for
      Nadine-puppet parents `26/96/102` and Chloe-puppet parent `42`. IDA store
      analysis shows op44/49/50/51/53 are also full-row-capable, while op41/47
      are scalar scatter. Prior `jv` ordering leaves only group-1 op49/op50 as
      possible later overwrites after the final earlier op40.
- [x] Run the written `jx` parent-writer discriminator. Require paired admitted
      group-1/2 scopes, exactly one target op40, no rejects, normal models and
      responsive processes. Classify P40-final versus a group-1 op49/op50
      overwrite from exact parent-row before/after deltas. Missing op40 parent
      destinations selects Pother/mixed and authorizes no fix. Result: 8/8 paired
      samples per receiver, no rejects. Earlier op40 instances changed all 16
      words of Nadine-puppet joints `26/96/102` and Chloe-puppet joint `42` in
      every sample. Later group-1 op49/op50 changed zero words; P40-final is
      confirmed and P49/P50 rejected.
- [x] Resolve the exact inputs of the confirmed parent-producing op40 instances:
      Nadine-puppet JointParams `26/96/102`, source joint 25 and selector-1
      workspace prefix `+0`; Chloe-puppet JointParams `42` and source joint 41.
      Trace their last writers/initializers and eliminate entry-state versus
      authored-operator alternatives before a new discriminator. Result: bounded
      PAK/runtime maps expose the full sequential op40 chains
      `scratch->24->25->26`, `prefix->96/102`, and
      `35->36->41->42`; command 15 initializes the prefix immediately before
      case16, while command 3 defaults or preceding authored commands initialize
      fresh per-segment JP lanes. Previous-frame entry state is excluded and
      case17 is ordered after case16.
- [x] Run the written `jy` exact-input discriminator with only builtin slot 40
      wrapped. Require the five Nadine and three Chloe chain rows, exactly one
      target op40, no rejects, normal models and responsive processes. Classify
      S-JP, S-src or S-both from per-chain JP/source/output deltas; no pose
      substitution is authorized. First run was structurally valid for one
      baseline pair (`5/5`, `3/3`, zero rejects) but inconclusive: the older
      broad boundary counter reached 20 and prevented later exact scopes. Build
      `jz` separated that exhausted logging quota from exact admission. The
      repeated run produced 12/12 scopes per process with zero rejects. Result
      is mixed by chain: Nadine `24->25->26` and Chloe final joint 42 are S-src;
      Nadine 96/102 are S-JP; Chloe 36/41 are S-both, with the source contribution
      dominant at joint 36.
- [ ] Trace the newly confirmed upstream inputs before another runtime test.
      Resolve the last writer of Nadine selector-3 scratch `+0x1090` feeding
      joint 24 and the last writer of Chloe entry joint 35. Separately resolve
      authored/default JointParams producers for Nadine 96/102 and Chloe 36/41.
      Competing hypotheses are a shared receiver-camera pose-root writer,
      independent authored JP/source paths, or a mixed convergence. Require
      caller/callee, exact read/write dataflow and command-order evidence before
      designing a discriminator or substitution. Static progress: the Nadine
      source is now the exact group-0 chain
      `prefix->0->14->15->16->17->18->32->33->34`, copied by opcode 53 to
      scratch `+0x1090`; Chloe joint 35's last writer is group-1 opcode-40 index
      24. All intervening full-row writers were excluded from their descriptors.
      The direct Gesture-camera theory is rejected at its claimed anchors:
      `InputManager+0x50` is a frame epoch and grid `+0x7FC..+0x808` is authored
      min/max data. Exact JointParams ownership remains OPEN.
- [x] Implement read-only build `ka` at the sole
      `AnimCmdEvaluator_FinalizeGroupDefaults` callsite `0x141AB9D18`. Capture
      pre/post MSB-first ValidBits and 48-byte hashes for Nadine group-0 joints
      `0/14/15/16/17/18/32/33/34`, Nadine group-2 `96/102`, and Chloe group-1
      `35/36/41`; compare the finalizer-post records with the existing case-16
      records. Classify V-authored, V-default or V-post exactly as written in
      `COOP_STATUS.md`. Call the native finalizer once and write no game state.
      Result: the direct-call relay is restricted to skeleton/group signatures
      `580/0`, `580/2`, `651/1`, keeps twelve independent camera-distinct
      samples, and compares finalizer-post records at segment/opcode-40 entry.
      Release builds successfully; runtime classification remains pending.
- [x] Run the written `ka` camera-distinct sequence. Require all three exact
      actor/group signatures, one finalizer call, complete case-16 masks, normal
      models and responsive processes. Classify every joint as V-authored,
      V-default or V-post before considering any pose substitution. Result:
      Nadine group 0 completed 12/12 samples and proves V-authored with no late
      change for `0/14/15/16/17/18/32/33/34`. Nadine group 2 and Chloe group 1
      rejected only the probe's incorrect global-as-local indexing: their local
      evaluator counts were 96 and one. Both processes remained responsive.
- [x] Correct build `kb` using the finalizer's local range
      `group[3]..group[3]+group[0]`. Probe Nadine `96/102` in producer group 1
      and Chloe `35/36/41` in producer group 0, retain the post-finalizer record
      until the later target-group opcode 40 consumes it, and apply the corrected
      prediction in `COOP_STATUS.md`. Keep the native call exactly once and do
      not modify ValidBits or pose data. Result: `kb` maps each global joint to
      its local lane, preserves sampled records across evaluator groups under a
      diagnostic lock, and compares them at both producer case 16 and the exact
      later opcode-40 consumer. Release builds successfully.
- [x] Run the corrected `kb` sequence and require ranges `0+66`, `66+38`,
      `0+89`, complete producer and consumer masks, one finalizer call, normal
      models and responsive processes. Classify authored/default/post ownership
      separately for Nadine `96/102` and Chloe `35/36/41`. Result: all ranges and
      producer masks completed without rejects. Nadine targets are V-authored;
      Chloe is V-authored in 10/12 and default-filled in two transitions. Exact
      later op40 records nevertheless differ for Chloe 12/12 and Nadine 11/11.
      This confirms a local-lane to later-consumer discontinuity while rejecting
      the earlier `segment-global` address label as an established contract.
- [x] Inventory the exact live command order with read-only build `kc`. For only
      skeletons 580/651 and groups 0/1/2, report context flags, ordered command
      topology, and positions/counts of commands 3/13/14/15/16. Apply the written
      M14-external/M14-default/Mlane/Mother discriminator in `COOP_STATUS.md`;
      do not hash or substitute pose data until this boundary is selected. Result:
      both actors run an identical short group-0 list with six-word command 14
      strictly between commands 3 and 16 under flags zero. It occurs between the
      main group-0 and group-1/2 calls. Main group-1/2 lists contain no command 14
      and all case-13 copies precede command 3. Select M14-external for Chloe's
      group-0 -> group-1 path, Mlane for Nadine's group-1 -> group-2 path, and
      reject M14-default plus late Mother.
- [x] Implement the split read-only `kd` discriminator. At the exact case-14 call
      `0x141ABA7F5`, compare Chloe source/destination records `35/36/41` and carry
      them to group-1 op40. Separately log Nadine command-3 lane publication and
      group-1/2 resolved addresses for `96/102`. Decouple the comparison from the
      old `jz` admission gate, call every native function once, and change no pose
      data. Apply the exact prediction in `COOP_STATUS.md`. Result: release build
      `kd` installs the bounded case-14 call relay, captures Nadine's group-1 lane
      after the existing exact command-3 finalizer call, logs group-1/2 case-16
      address candidates, and compares both actors at opcode 40 independently of
      `jz`. Every wrapped native is invoked once and no game state is written.
- [x] Run `kd` with both screen-1 actor signatures. Require twelve exact samples,
      complete Chloe `35/36/41` and Nadine `96/102` consumer masks, readable
      group metadata, normal models and responsive processes. Apply the written
      M14-external versus later-writer and group-relative Mlane versus overwrite
      classifications; record rejects even if the probe remains stable. Result:
      all twelve series and masks completed without rejects. Chloe case 14 copies
      36/36 records bit-identically into the same rows later consumed by op40, but
      all 36 are changed before consumption, selecting a later writer. Nadine
      group 1 publishes local rows at `+0x5A0/+0x6C0`, while group-2 op40 consumes
      distinct descriptor rows at `+0x1200/+0x1320`, confirming Mlane.
- [x] Resolve Chloe's exact post-case14 writer statically. Distinguish command 15,
      the command-16 preamble and earlier joint-program operators by exact call
      graph, payload order and destination semantics before another runtime test.
      Separately identify the producer of Nadine's descriptor-selected
      `+0x1200/+0x1320` rows; do not modify the valid group-1 lane. Result: reject
      the shared-lifetime premise. Case-14 and the target op40 run in separate
      `ProcessAnimCmdListSegment` invocations; command 2 allocates fresh lanes and
      command 5 releases them before the next list. Repeated bases are arena reuse,
      so none of the three proposed same-object writers explains the difference.
- [x] Correct the opcode-40 index model before another carry. `k` encodes a
      target-group-local row (`k/16-1`), while skeleton-global joint is
      `group.first+local`: Nadine group-2 `96/102 -> 238/244`, Chloe group-1
      `35/36/41 -> 240/241/246`. Reject the `ka` through `kd` cross-group producer
      attribution; it followed unrelated global joints with the same numbers.
- [x] Resolve the target-list WorkBuffer layout. Chloe group 1 starts from a
      one-record processing lane and command 15 expands it in place to 62 records;
      Nadine group 2 expands 96 records to 119. Runtime pointer deltas are exactly
      `62*48` and `119*48`. Local rows `35/36/41` and `96/102` are unwritten
      expansion-tail bytes, not current-list authored/default JointParams.
- [x] Implement a bounded read-only expansion probe at
      `AnimCmdEvaluator_ExpandJointParamsToTargetWorkspaceCount 0x14217BF30`.
      For only Chloe group 1 and Nadine group 2, snapshot base, finalized prefix
      and exact target tail before/after the native call, then compare the saved
      tail with later opcode-40 reads. Require one native call, identical base,
      unchanged prefix/tail, exact later matches, normal models and no faults.
      Result: build `ke` patches the exact direct callsite `0x141ABA89A`, admits
      only the two asset-proven signatures, preserves one native call, and keeps
      its raw records thread-local to the current list. Release builds.
- [x] Run the written `ke` discriminator. Require twelve complete samples for
      each active actor, unchanged bases/prefix/tails, complete `0x7/0x3`
      opcode-40 masks, normal models and responsive copies. Classify
      H-expand-carry, H-expand-replace or H-late-writer without applying pose
      data. Result: 12/12 samples per actor have identical bases, zero prefix and
      tail changes, and complete exact-address masks. All 60 later consumer rows
      differ, selecting H-expand-carry across command 15 plus H-late-writer
      inside command 16. Both copies remained responsive.
- [x] Trace the exact command-16 JointParams writers after expansion. Runtime
      `kf` completed 12/12 samples per actor with zero rejects and responsive
      copies, but correctly disproved the expected `22/8` call counts: exact PAK
      program records start at `0xF030/0x8F40` with omitted opcodes `52,49,50`.
      Static destination decoding plus `kf` masks now accounts for every word of
      Chloe rows `35/36/41` and Nadine rows `96/102`. Custom operators do not
      address or change them. The direct builtin `resolved=000` diagnostic is
      rejected because it decoded against the outer context instead of the
      nested `{JointParams,workspace,aux,scratch}` table.
- [x] Trace the upstream encoded inputs of the confirmed JointParams writers.
      Exclude opcode 52's authored constants, then follow the exact sources of
      Nadine/Chloe opcode 50, Chloe opcode 47 `0xF04C`, opcode 51 `0xF10C`, and
      opcode 44 `0xF142` through nested selector-2/3 buffers to their last writer.
      Competing outcomes are S50-derived, S47/51/44-derived, and S-mixed. Do not
      launch again until the static source graph leaves a bounded, falsifiable
      live-alias choice. Result: every target scalar control resolves to an
      opcode-52 authored constant. Opcode 53 exports the live group-0 outputs;
      exact PAK streams and exhaustive builtin/custom destination decoding leave
      only the op40 JointParams chain and three source-C quaternions as live
      competitors. Build `kg` records those inputs around untouched native calls.
- [x] Run the written `kg` discriminator. Keep one actor untouched while moving
      only the receiver camera sky -> ground -> sky, then repeat reciprocally.
      Require twelve exact samples, Chloe `calls=13 mask=003FF`, Nadine
      `calls=10 mask=3FC00`, zero rejects, normal models and responsive copies.
      Classify U-JP-chain, U-sourceC or U-mixed exactly as written in
      `COOP_STATUS.md`; do not substitute a quaternion or matrix in this build.
      Result: 12/12 exact ends per actor, zero rejects, and every chain link
      matched its preceding output (`108/108` Chloe, `84/84` Nadine). Both roots
      use one stable identity source while root JointParams/output change 11/11;
      U-JP-chain is confirmed. All three source-C quaternions also change 11/11,
      selecting overall U-mixed and rejecting U-sourceC-only.
- [ ] Resolve the main group-0 pre-command-3 pose DAG which authors the confirmed
      op40 JointParams and Nadine source-C records. Decode exact destination and
      source lanes for commands 6/7/8/10/13/23 before command 3, identify the last
      writer of the first root record and each live quaternion, and trace that
      lane back to receiver-local versus retained-remote ownership. Use caller/
      callee, worker store semantics and exact command-order/operand evidence;
      do not launch or substitute pose data until a bounded seam remains. Static
      progress: commands 6/7/8/9/10/13 have exact lane schemas; command 3 finalizes
      only lane zero. Command 23 dispatches by StringId through
      `Game_ProcessAnimPhasePluginFunc 0x1406CCB30` and includes both the output-only
      GestureJointReader and real IK pose writers, so it cannot be treated as one
      semantic class. Further static closure: LegFix/ankle write only the two
      authored leg-chain joint sets and HandFix writes only the selected
      shoulder/elbow arm chain; none reads Player/camera/aim state, so these
      plugins cannot introduce the observed common head/spine/arms pitch.
      `AnimNodeGesture_EmitSelectedClipCommand 0x141FFEA20` is an exact command-6
      producer and `0x141FFA900` emits the associated command-7 grid blends, but
      the live `kh` heap clips are not yet tied to one retained Gesture owner.
- [x] Implement bounded read-only build `kh`: admit only screen-1 flags-zero main
      group-0 lists whose command 3 occurs after position 20, double-validate the
      immutable stream, log all pre-command-3 command-6/7/8/9/10/13/23 tuples plus
      callback RVA, and trace lane-zero ancestry conservatively through the known
      schemas. Stop at command-23 barriers instead of guessing a payload layout;
      preserve one native outer call and write no game state.
- [x] Run the written `kh` reciprocal camera sequence. Require complete main-list
      inventories for both actors, callback RVA `0x006CCB30`, no rejects, normal
      models and responsive copies. Classify D-clip, D-blend/copy, D-plugin or
      D-external/mixed exactly as written in `COOP_STATUS.md`; do not suppress a
      plugin or substitute a pose lane in this build. Result: eight complete,
      stable inventories resolve every known lane leaf to command 6 and expose
      only five live plugin IDs. Gesture/output plugins are read-only candidates;
      LegFix, ankle blend and HandFix are the remaining typed pose writers, so
      D-clip is confirmed for native edges and D-plugin/mixed remains overall.
      Three longer Nadine lists hit only the probe's 96-entry diagnostic cap;
      both processes stayed responsive and no visible regression occurred.
- [x] Supersede the remaining `kh` plugin-mask branch with the narrower authored
      Gesture target seam. Static call/data flow now closes
      `GestureTargetAimAt_GetTarget 0x1406F26D0 -> Gesture_GetComputeArgs
      0x141FD3180 -> ComputeArgs -> AnimNodeGesture_StepAngles 0x141FFEDB0 ->
      cached angles +0x80/+0x84`; RTTI/vtable identity and the
      target factory independently bind CRC `0xE83A8507` to TargetAimAt. The
      getter resolves the exact owner and consumes its +0x18F0/+0x1900/+0x1910
      target ring through `0x141DE6500`. Detailed LegFix/ankle/HandFix masks no
      longer block a target-only correction and remain stock.
- [x] Run the consolidated `ki` exam. Both vtable installs succeeded and both
      copies remained responsive, but visible receiver-camera ownership was
      unchanged and neither PID emitted the post-gate
      `GESTURE-AIM-TARGET relay applied` marker. Active screen-1
      `7339D08C/3C347132` still traversed nonzero up/down values. The implemented
      TargetAimAt seam is therefore REJECTED as a sufficient live correction;
      its vtable mutation and dead runtime code are removed without discarding
      the independently proven static Gesture identities.
- [x] Close the redundant live-base writer task. The exact owner chain is
      controller `+0x108 -> Player+0x1A0 -> +0x58 -> +0x10`, and
      `PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0` writes both
      controller `+0x8C0/+0x8C4` and active-state `+0x188/+0x18C`. Build `iu`
      already delivered peer-correct values through this state/evaluator while
      visible pitch still followed the receiver camera, so this boundary is
      REJECTED as the rendered correction seam.
- [x] Resolve the concrete target-provider and its ordinary upstream writer.
      The live base layer always dispatches a null custom TargetPoint; node
      `+0xE8` is authored bytecode. Archive cross-reference proves 11 fixed
      generic Player nodes use `LookAt`, none uses `AimAt`, and only one of five
      dynamic descriptors carries a target lambda. Exact RTTI trackers at
      `Player+0x21E0/+0x2260` feed `Player_GetLookAtTargetPoint`; normal
      `Player::Move::Update` unconditionally reaches `PlayerStrafeLookAt`, whose
      default request reads the owner screen route and writes the first tracker.
- [x] Reverse-own the sole dynamic target lambda. Exact qword edges lead
      `0x11A5A0 <- 0x10CE40 <- 0x10A4C8 <- 0xEECB8 <- 0x737C8 <- prone state
      record 0x149C0`; matching fallback id `0xFAFAA661`, the fixed weight-one
      Blend semantics and exact selector/inline-target VM fields independently
      confirm that ownership. The proven live `strafe-idle-ap` state uses the
      separate graph `0x66048`, so G2 is REJECTED for ordinary ADS.
- [x] Reverse-own all 17 Gesture descriptors with a complete bounded graph
      walk. None of the 11 fixed LookAt or five dynamic descriptors reaches
      `strafe-idle-ap`. Its sole node is fixed `0xA6448`, gesture
      `0x7855F662`, with settings target type zero and no target callback.
      BaseLayer supplies no custom TargetPoint; the config factory rejects zero
      and the per-node path constructs a plain fallback. G1 is therefore
      REJECTED, not a correction seam.
- [x] Keep runtime tests parked. Follow the selected `strafe-idle-ap+0x66048`
      Animation endpoint through command evaluation into the late visible-pose
      DAG and find a post-command head/spine/arms writer with an owner
      screen-route or camera dependency. G3, a non-Gesture producer or
      post-command writer, is the sole surviving topology. Do not repatch
      PlayerStrafeLookAt, Gesture targets, LRDU, active-state weights, LookAtIK
      or the flashlight-only LookAtOrCamera. STATIC result: the late writer is
      `Player+0x1D50 -> PlayerStrafeController_UpdatePostAnimBlend 0x14100C400
      -> PlayerStrafeController_UpdateStrafeIk 0x14100B370`; its exact source
      worker derives pitch from a routed-camera Locator and writes `headb/spined`.
- [x] Resolve descriptor type 1 as exact RTTI `AnimSimpleLayer`. Its command
      generator `0x141894A90`, step `0x1418966C0`, instance phase helper
      `0x1418F3AA0` and layer fade helper `0x141896950` close all ordinary
      command-6 phase / command-7 weight ancestry over clip metadata, delta time
      and fade config, with no Player/camera/input read. Player's sole named
      persistent Simple layer is `FlinchPartial`; directional aim is type 2.
      Reject the ordinary type-1 clip/time/fade path as the steady camera source.
- [x] Resolve all concrete Player fields and vtable targets called by
      `Player_PostAnimBlending_Async 0x1410EB6A0`, especially `+0x1D50`,
      `+0x1DD8`, `+0x1FB0`, `+0x1FE0` and `+0x1FF0`. Audit joint scope and every
      Player screen-route/camera dependency; keep limb-only and traversal-only
      solvers separate from a common head/spine/arms writer. Leg, hand,
      wall-hand, bear-climb, pickup, bump, flashlight and holster paths are
      scoped accordingly; `+0x1FB0` is an event/host-anchor hub. Only the
      PlayerStrafeController path combines routed-camera data with a common
      `headb/spined` post-blend correction.
- [x] Close the correction seam statically before another build. Exact authored
      `*reticle-offset*` dataflow proves that full `pose_seed` quaternion
      replacement would erase the final GunMove reticle contribution. The exact
      `PlayerStrafeController` vtable `+0x60` ABI separates horizontal direction
      from Locator pitch/right axis. Select a versioned one-scalar relay: capture
      sender screen-0 final Locator positive-`Z` `forward.y`, then on receiver
      screen 1 rebuild only the copied Locator quaternion while preserving its
      native right axis. Camera-state prefix and raw `out+0x350` writers are not
      part of this seam.
- [x] Implement the bounded `PlayerStrafeController` final-forward-Y relay only
      after this static handoff: add an explicit-validity StatePacket v14 tail field
      (do not repurpose v13 `aim_pitch_up/down`), hook exact vtable target
      `0x14100C400`, gate both sides on owner screen and current action
      `GunMove 0x12D842C0`, keep the receiver packet bound at 250 ms, clamp the
      transported forward-Y inside `(-1,+1)`, and call the original with an
      unmodified Locator outside fresh strict ADS. Build `2026-08-02b` compiles;
      its vtable and native-math prologue preflight skips unknown binaries.
- [x] Run the written `2026-08-02b` discriminator: hold the receiver camera
      opposite while the sender alternates sky/ground in exact GunMove ADS.
      Require `STRAFE-IK-Y-CAPTURE` and `STRAFE-IK-Y-APPLY`, vertical puppet
      head/spine/weapon following the sender, unchanged horizontal aim/tracers,
      and immediate native behavior after ADS/GunMove exit. Record CONFIRMED or
      REJECTED before changing another seam. CONFIRMED in both directions:
      Nadine->Chloe applied `native=-0.90994 peer=+0.31339`; Chloe->Nadine
      applied `native=+0.46220 peer=-0.58747`, and the visible puppet followed
      the sender rather than the receiver camera without a crash.
- [x] Close the non-ADS head-direction seam statically. Ordinary
      `Player_BuildStrafeIkReferenceLocator 0x1410F9050` keeps the Locator seeded
      from the owner's `Player+0x4010` screen route; exact vtable `+0x60` then
      feeds its quaternion to `PlayerStrafeController_UpdateStrafeIk
      0x14100B370`, which writes literal `headb/spined`. The direct LookAtIK,
      broad camera/root paths and later common head writer are excluded.
- [x] Run one bounded build-`x` non-ADS discriminator: keep both characters
      stationary, hold the observer camera opposite and move only the sender
      sky -> ground. Require `STRAFE-IK-LOCATOR-CAPTURE` plus
      `HEAD-STRAFE-LOCATOR-APPLY`, sender-correct remote head/upper-spine
      direction, no twist/locomotion change, and unchanged ADS behavior.
      **REJECTED:** both PIDs captured and applied the exact Locator, but the
      visible remote head still followed the observing copy.
- [x] Trace every writer of `PlayerStrafeController+0x300` and the tail after
      `0x14100C400`. The steady exact setter `0x14100AC40` receives only the
      `PlayerStrafeToEntryAction` producer's explicit
      `(sin(yaw),0,cos(yaw),0)` XZ Vector; it has no camera reader and cannot
      carry vertical pitch. No later common Player head/neck/spine writer exists.
- [x] Close the non-ADS `PlayerStrafeLookAt` discriminator before building.
      Normal Move unconditionally publishes its owner-screen camera-derived
      point into `Player+0x21E0`; exact `idle` node `+0xB1098` and ordinary
      run node `+0x877C8` are fixed LookAt Gestures whose native target consumer
      reaches the Gesture angle/workspace path. The prior targetless-Gesture
      rejection remains exact for ADS only. Build `2026-08-03z` substitutes
      peer `camera_quat` only at resolver return sites `0x141000C30` and
      `0x1410015C5`, preserving the stock target, limits, blend and solver.
- [x] Run the one written build-`z` discriminator: stationary non-ADS,
      observer camera opposite, sender sky -> ground. Require both
      `HEAD-STRAFE-LOOK-SEED` sites in each process plus sender-correct remote
      head direction and no deformation. Both sites with unchanged visuals
      reject the non-ADS fixed LookAt seam and select a downstream palette/
      render overwrite; do not widen the hook or repeat the test. **REJECTED:**
      both sites fired in both PIDs with materially different peer/native
      quaternions, both copies stayed responsive, and the visible head still
      repeated the observer camera.
- [x] Trace the exact non-ADS fixed-LookAt output from
      `AnimNodeGesture_StepAngles` through its workspace/command rows into
      `JointParams`, the skeleton palette and render conversion. At every merge,
      distinguish a zero/inactive authored LookAt weight from a later overwrite.
      Exact idle child1 is selected at weight approximately 1, all nine named
      `relaxed-idle-lookat-*` grid clips exist, and no common late palette/render
      writer exists. `GestureTargetLookAt_GetTarget 0x1406F2360` is the final
      RTTI-exact world-target boundary before angles/grid selection; build `zb`
      relays only its screen-1 non-ADS output and leaves rejected IK/Strafe seeds
      excluded.
- [x] Run the single written build-`zb` discriminator: stationary idle, observer
      camera held opposite, sender sky -> ground -> sky. Require bounded
      `GESTURE-LOOK-TARGET-APPLY` with peer forward Y changing sign, visible
      sender-correct remote head direction, and no twist, locomotion, ADS, crash
      or performance regression. APPLY without visual change rejects the direct
      target and selects exact clip `ValidBits` versus the first subsequent
      state/simple/type-16 layer overwrite; do not widen the hook. **REJECTED /
      ADMISSION UNPROVEN:** both PIDs installed the exact slot, but the user saw
      no change and neither PID emitted APPLY. The same log has no post-install
      state-send/receive rows, so this does not falsify provider output.
- [x] Resolve whether `GestureTargetLookAt_GetTarget 0x1406F2360` runs per frame
      or only during Gesture cache construction. The old node+0xD8 sign gate
      proves provider/GetComputeArgs is cache-create only. Live
      `GestureLayer_PostAlignUpdatePerGestureNode 0x1420F4700` retrieves the
      saved ComputeArgs and calls `AnimNodeGesture_StepAngles 0x141FFEDB0` once
      per frame epoch; its exact `Gesture::ApplyJointReaderFunc` stores Player
      at +0x28 and cached world target at +0x30.
- [x] Decode the nine idle LookAt clip joint masks and first later overwrite
      candidates. All nine author the identical set
      `{0,2-13,16-20,33-35,40-42,58-62}` / mask `BFFCF80070E0003E`: neck 42 is
      present, head 43/44 and spine 26-29 are not. The steady single idle state
      has no state-transition cmd7; the next common overwrite candidate is the
      inter-layer cmd7 emitted at `0x14172C647`.
- [x] Run the single build-`zd` cached-target discriminator: stationary non-ADS,
      observer held opposite, sender sky -> ground -> sky. Require bounded
      `GESTURE-CACHED-TARGET-APPLY` with changing `forward.y` plus sender-correct
      remote head direction. APPLY without visual change selects exact joint-42
      provenance across the clip output and `0x14172C647`; no APPLY identifies
      a gesture/owner/packet gate. **PARTIAL / REJECTED:** both PIDs applied
      fresh, strongly changing peer vectors and observer-camera repetition
      stopped, but the two copies did not match. Closure+0x30 is reference-local,
      while `zd` incorrectly wrote a world point and forced `w=1`.
- [x] Run build `ze`, which preserves the confirmed live StepAngles seam but
      inverse-rotates peer world forward by the exact current reference-world
      quaternion `Player(+0x120 * +0x140)`, scales the reference-local vector by
      the stock local norm and preserves stock `w`. Exact sender match confirms
      E1; a stable inversion/axis swap selects quaternion convention E2; an
      independently moving mismatch selects the joint-42/layer-merge fork E3.
      **REJECTED AS A COMPLETE FIX:** the user still sees no synchronization;
      logs prove changing admitted targets but did not prove that the native
      epoch gate let those particular StepAngles calls reach the solver.
- [x] Run build `zf` once with sender sky -> ground -> sky. Its exact native
      stale-epoch admission must emit `GESTURE-ANGLE-CONSUME consumed=1`, an
      epoch transition and changing `node+0x80/+0x84`. No rows selects the wrong
      per-epoch invocation; consumed invariant/wrong-axis angles select the
      target mapping; consumed sender-correct angles with no visible motion
      select the first joint-42 layer overwrite at/after `0x14172C647`.
      **CONFIRMED / F3 SELECTED:** both processes advanced the exact native epoch
      with `consumed=1`; angles followed opposite sender sweeps through authored
      limits near `(-89,-49)` and `(89,+49)`, yet the visible head did not sync.
- [ ] Starting at `AnimControl_CreateAnimCommands 0x14172C370`, resolve the exact
      registry/layer source, mask and weights feeding inter-layer cmd7 at
      `0x14172C647`. Find the first post-LookAt writer of neck joint 42 and prove
      its Player/screen ownership before changing runtime behavior. Do not revisit
      packet, quaternion, StepAngles or clip-membership hypotheses.
- [ ] After expansion provenance is confirmed, choose between explicit
      sender-tail transport and a deterministic intended prior-list carry. Trace
      target children Chloe local `43` (global 248) and Nadine local `97/103`
      (global 239/245); Nadine local 27 needs no input trace because `jw` proved
      its 48-byte record bit-identical across the camera sweep.

## P0 - Nadine legacy-look weapon attachment (PARKED, 2026-07-27)

- [x] Recover the prior Nadine weapon history and keep the confirmed general
      weapon-display fixes. `ApplyWeaponSwitch(-1)`, slot mirror, draw-state
      capture and edge-triggered holster are working layers; do not repeat the
      rejected per-frame holster or gun-state loops.
- [x] Separate the persistent legacy-look rifle from player inventory. Commit
      `37cb845` proved it remains after removing every controller weapon; current
      DrawControl logs pin the exact five-mesh `0xd27c074f` signature versus two
      Chloe meshes. The independent player-owned holster AP gap remains at
      `PlayerWeaponHolsterAction_ResolveAndBindAttachPoint 0x141095BD0`.
- [x] Build a bounded standalone-mesh discriminator. Build `et` uses F11 to cycle
      baseline and one hidden legacy-look parent mesh, restores the previous mesh,
      requires all five exact ids, and touches no inventory/attachment state.
- [x] Run `et`: focus the window that renders Nadine and press F11 up to six
      times. Record which `NADINE-MESH-PROBE index/id` removes the persistent
      rifle and whether any body/clothing disappears with it. A rifle-only index
      becomes the default suppression; a combined mesh or no affected index
      rejects this route before any holster-AP write. Result: all five indices
      removed body parts in sequence; hair, rifle and grenade remained floating.
      REJECT parent-look mesh suppression. The rifle is a separate look-created
      attachable, not one of the five body meshes.
- [x] Build `eu` at the existing `NdAttachableObject_PostJointUpdate_Async` callsite
      `0x141ED63F7`, capture a bounded read-only census only when the parent
      DrawControl matches all five Nadine ids. Record unique child object vtable
      RVA, type id, child mesh ids, `+0x4E0/+0x4E8` parent handle and plausible
      `+0x860` ProcessWeapon art. Use the surviving hair/rifle/grenade trio to
      identify the persistent rifle child before changing visibility or binding.
- [x] Run `eu` in normal gameplay with Nadine visible. Require stable
      `NADINE-ATTACHABLE` child signatures and no visual regression. Resolve each
      vtable/mesh signature statically; if only hair/cloth reaches the callsite,
      reject this boundary rather than hiding another guessed mesh. Result:
      INCONCLUSIVE, no records in both responsive processes, no crash. Correction:
      `force_log_line()` was compiled out, so this says nothing about accepted
      records or guards.
- [x] Build `ev`, a bounded read-only exact-parent hit/rejection census before the
      `attachable >= 0x4F0` and child-snapshot guards. Record readable extent,
      child mesh header/snapshot success and raw RBX value so the next test
      distinguishes an undersized object, wrong object base and child capture
      failure.
- [x] Run `ev` with the legacy Nadine parent visible and force pistol/rifle
      draw/holster plus movement. Result: no lines and no regression, but source
      audit proved its `force_log_line()` result path was compile-time disabled;
      no callsite conclusion is allowed.
- [x] Build `ew` with only the maximum-32 exact-parent census routed through the
      persistent writer; keep global diagnostics and all visibility writes off.
- [x] Run `ew` with the legacy Nadine parent visible. Classify the first
      `NADINE-ATTACHABLE-HIT` records by `attachReadable`, `snapshot`, vtable and
      child mesh ids before selecting any rifle visibility/attachment action.
      CONFIRMED: `ProcessWeapon` rifle art `0x8075397D`, mesh `0x255BEBA0`;
      pistol `0x33E29A1B`; grenade `0x0B8BD6D6`/mesh `0x836D013E`; hair is
      `ProcessCharCloth`/mesh `0x7368D4A2`.
- [x] Build `ex`: capture the exact ProcessWeapon pointers at the async census,
      then compare
      them with PlayerWeaponController `GetWeaponBySlot` results only on the game
      thread. Log owner player index, exact slot membership, current slot,
      draw-state, hand-attached slot and active weapon. Rifle-in-slot while the
      pistol is current selects the holster-AP failure; no slot membership selects
      a look-created orphan. No visibility or transform write in this probe.
- [x] Run `ex` and cycle pistol, rifle and holster once. Correlate the exact
      `0x8075397D` pointer with slot mask and controller state before choosing
      holster-AP repair versus orphan suppression. Result: rifle is the normal
      slot-0 object (`slotMask=0x1`), pistol is slot 1 and grenade slot 2 on both
      owners. REJECT look-created orphan; select the downstream holster bind.
- [x] Instrument `PlayerWeaponHolsterAction_ResolveAndBindAttachPoint
      0x141095BD0` read-only and compare legacy Nadine with Chloe. Distinguish
      function-not-reached, null `weapon-holster-point` knowledge record, and a
      resolved record rejected/misapplied by the final Player+0x1DF0 `+0xB8`
      bind. Do not write an attachment yet.
      Build `ey` installs an exact-prologue outer detour plus the direct
      `0x141095E49 -> ResolveKnowledgeRegistryEntryById` callsite relay. It logs
      at most 64 deduplicated results and preserves all inputs/returns/state.
- [x] Run `ey`; on each copy draw pistol, holster, draw rifle, holster. Require
      the holster-probe install line, stable gameplay and bounded
      `NADINE-HOLSTER-BIND` records. Compare `look`, `lookup/entry/payload` and
      `bindResult` for Chloe versus legacy Nadine before any attachment write.
      Result: both Nadine owners and Chloe call rifle lookup id `0x68F32771` and
      receive non-null entry/payload; the payload is shared. REJECT H1/H2 and
      select H3 at/after Player+0x1DF0 virtual `+0xB8`.
- [x] Resolve the concrete Player+0x1DF0 vtable `+0xB8` target and trace the
      shared `0x68F32771` record into the active look's attachment graph. Compare
      its resolved attach-point/joint or failure status for Chloe and look
      `0xD27C074F` before introducing any fallback transform. Result: the object
      is RTTI `PlayerWeaponAnimControl`; `+0xB8`/`0x14108DAB0` only stores the
      weapon handle. Its `+0x130` initializer resolves four exact holster joints,
      and `+0x138` builds transforms only for valid index pairs.
- [x] Run `ez` and repeat pistol/rifle holster on both copies. INCONCLUSIVE:
      Chloe consistently had joints `101,5,199,3` and two live transform blocks,
      but dynamic transform words were incorrectly part of the 64-entry dedupe
      signature. The table filled before a legacy-Nadine sample; select no J1/J2/J3.
- [x] Run build `fa` with the corrected game-thread census of `Player+0x1DF0`
      for both retained Players. CONFIRMED J1 in both processes: Chloe has
      `101,5,199,3` and two changing transforms; legacy Nadine has
      `-1,5,-1,3` and two unchanged identity transforms. Vtable and control
      ownership match, ruling out a stale/wrong PlayerWeaponAnimControl.
- [x] Trace every consumer of control `+0x78..+0x84` and `+0x90..+0xCF` before
      writing a fallback. REJECT the proposed child-index-to-parent substitution:
      `0x14108EBF0` would only regenerate identity, while
      `PlayerWeaponAnimControl_UpdateHolsterIk 0x14108ECE0` already composes that
      identity with the same parent indices at `+0x7C/+0x84`. It cannot change
      the current target or visible attachment.
- [x] Resolve the named-event path after PlayerWeaponAnimControl. Vtable `+0xE8`
      publishes the authored event through the ProcessWeapon common Process
      `+0xD0` sink `0x141BB7960`; ProcessWeapon slot 24 returns the generic event
      controller at `+0x218`, which dispatches at `0x141E72540`. Correct the old
      weapon-inventory-specific labels in the IDB.
- [x] Resolve the concrete attachment mutation independently of authored
      state-script data. `Script_ReattachPlayerWeapon 0x1403421B0` and
      `Script_AttachPlayerWeaponBackToHolster 0x140342EE0` both build the binding
      and call `WeaponAttachment_AttachToOwnerPoint 0x141ED0370`, which replaces
      the parent handle and selects the owner `+0x1E0` attach record.
- [x] Run the next read-only discriminator: compare Chloe and legacy Nadine for
      (a) emission of named event `0x9E2D6552`
      (`reparent-weapon-to-holster`) and (b) `WeaponHolster_BuildBinding
      0x1414FE540` output: selected `gun_holster*` id, authored transforms and
      scalar; also record whether final `0x141ED0370` is reached and succeeds.
      Build `fb` installs all three pass-through hooks. Cycle pistol/rifle draw
      and holster on both copies. CONFIRMED: binding id and payload match across
      looks, but legacy Nadine lacks pistol id `0x00AD047A` and rifle id
      `0x555EFB8D` in owner `+0x1E0`; grenade control `0x754D9C3B` exists for both.
      The final attach function succeeds for hand id `0x61A48E01`; the missing
      holster ids are gated before mutation. The empty event channel is
      non-discriminating because it was empty for both looks.
- [x] Trace the owner `+0x1E0` attachment-table constructor, owner, 48-byte record
      layout, look-change/copy path and the supported in-place mutation API. Resolve the
      StringId preimages for `0x61A48E01`, `0x00AD047A` and `0x754D9C3B`, and
      enumerate which authored shoulder/back records already exist on legacy
      Nadine. Do not write until allocation and lifetime are proven. Result:
      exact `AttachSystem::InitFromDcData 0x1418B61B0` and append API
      `0x1418B6E50`; container is `capacity,count,records,rig` at `+0,+4,+8,+16`.
      Live Chloe is `45/44`, Nadine `45/39`, so six engine-owned slots are free.
      Preimages: `0x61A48E01=rightHand`, `0x00AD047A=targSpineProp`; grenade id
      has no executable string preimage. Missing source marker joints are
      `spine_prop_alt_attachment` and `pelvis_holster`, with standard Nadine
      parents `spined` and `pelvis`.
      Look changes clear/rebuild the container and may replace its records
      pointer; the visible destructor chain exposes no separate replacement
      seam, so the repair must be idempotent and use the existing allocation.
      Forty live snapshots select Chloe `spined` joint 29 as the stable parent
      of marker 48 (RMS `~2.9e-5`); target Nadine `spined` is joint 18.
      The pelvis pair is independently selected by the authored holster-control
      child/parent indices and live census (RMS `~0.0075`), though its relative
      transform animates and the first repair intentionally captures one pose.
- [x] Build and run `fc`: compose each Chloe marker relative to its standard
      parent with the authored attachment transform, remap the joint to Nadine's
      `spined`/`pelvis`, clear runtime state and append the two 48-byte records
      through `AttachSystem_AppendRecord`. Require `39/45 -> 41/45`, both ids
      present, stable body/grenade, and rifle/pistol holster reparenting away from
      `rightHand`. Do not allocate/replace the container or copy joint 48/199.
      REJECTED before mutation: the virtual-look admission gate matched neither
      retained player, so no repair marker appeared and Nadine stayed `39/45`.
      The immediately-following census proved both raw `Player+0x170` look ids
      and the game-thread tick were valid.
- [x] Build and run `fd` with the gate changed only to raw active look
      `Player+0x170`. Require the original `39/45 -> 41/45`, both ids present,
      joints `48:29->18` and `199:3->3`, successful final reparenting, stable
      body/grenade and no exception.
      PARTIAL: append and lookup passed exactly in both processes, with pistol at
      index 39 and rifle at 40 and no exception. Physical reparenting failed:
      every observed Nadine final attach still targeted `rightHand`, while the
      pre-existing `PlayerWeaponAnimControl` cache remained `-1,5,-1,3`.
- [x] Trace the constructor, look-change lifecycle and stock refresh path for
      `PlayerWeaponAnimControl+0x78/+0x7C/+0x80/+0x84`. Determine how its missing
      spine/pelvis child joints are recomputed after AttachSystem materialization;
      do not patch these output/cache fields directly. Require three independent
      static evidence types and a new written final-reparent prediction.
      STATIC result: no safe refresh exists. `+0x130` resolves the unchanged
      skeleton namespace and reallocates ArmIkChain; `+0x138` skips invalid pairs.
      Cache patch/update is rejected as unrelated to physical attachment.
- [x] Build and run `fe`: once per records allocation, call stock
      `WeaponAttachment_AttachToOwnerPoint` only for an inactive firearm whose
      current resolved attach id is still `rightHand`. Stable `draw=0/2/4` only;
      admit stuck `draw=5,current<0` after 1000 ms plus confirmed holstered state.
      Never touch `current`, never act in draw transition 1, and verify result,
      post index/id and persistence before making a visible claim.
      CONFIRMED/PARTIAL: the rifle now leaves Nadine's hand and no longer overlaps
      the drawn pistol. Placement fidelity is not solved: pistol targets the
      copied Chloe back point, rifle intersects the torso, and active grips/left
      arm are wrong.
- [x] Run scoped build `gl` for the role-B-only stale rendered rifle attachment.
      Current telemetry already has slot 0 on `gun_holster 0x555EFB8D` while the
      Nadine-controlled copy still shows it in the hand; role-A Chloe sees the
      world instance correctly. `gl` replays stock `AttachToOwnerPoint` only for
      inactive exact slot-0 rifle, once per holster generation, and never touches
      pistol slot 1. Require `localViewRefresh=1 result=1 verified=1`, rifle out
      of the local hand, normal rifle draw, and no body/pistol regression.
      REJECTED: the role-B local Nadine is owner 0, not owner 1, so the refresh
      gate never admitted the affected object and the rifle remained in hand at
      spawn.
- [x] Run corrected build `gm` with the local role-B owner-0 gate. Require the
      initial stable holstered state to emit `owner=0 slot=0 localViewRefresh=1
      result=1 verified=1` before any draw/put cycle, remove the rifle from the
      local hand, and preserve normal rifle draw plus pistol/body state.
      REJECTED for visibility: the exact call emitted `result=1 verified=1`, but
      the rifle remained in the local hand. Attachment selection is not the
      role-B main-view render boundary.
- [x] Run build `gn`, hiding only the exact local owner-0 slot-0 rifle
      DrawControl while `current<0,draw=0`, and restoring only meshes hidden by
      this path when slot 0 becomes active. Require a non-zero
      `NADINE-LOCAL-RIFLE-VIS action=hide` at spawn, matching `show` on draw,
      no rifle in the inactive local hand, and no pistol/body/role-A regression.
      REJECTED by ordering: the exact mesh was successfully hidden with
      `changed=1` every tick, but a later stock visibility writer re-enabled it
      before rendering and it remained visible.
- [x] Run build `go`, publishing the exact inactive local rifle on the game
      thread and applying HideMesh from the existing attachable relay after
      stock `DrawControlSyncParentVisibility`. Require `arm-late-hide` followed
      by one same-generation `late-hide changed=1`, no spawn rifle in the local
      hand, matching `disarm-show` on rifle draw, and no body/pistol regression.
      REJECTED for visible output: both late markers occurred and the exact
      rifle reported `instances=3/0`, but the starting rifle remained visible.
- [x] Run build `gp`, skipping stock `PrepareInstanceForRendering 0x14181A9B0`
      only when FgAnim retained owner, runtime id, art and pointer all match the
      published inactive local rifle. Require `skip-prepare` for the same
      generation, no spawn rifle in the local hand, normal rendering after
      `disarm-show`, and no pistol/body/role-A regression.
      REJECTED: the exact same-generation skip occurred, but the rifle remained
      visible. Static xrefs show this intercepted only worker callsite
      `0x14181B925`; a separate synchronous caller exists at `0x14197DC54`.
- [x] Run read-only build `gq` and classify the synchronous
      `PrepareInstanceForRendering` caller at `0x14197DC54`. Require either an
      exact `sync-prepare mode=1 targetMatch=1`, a pre-publication legacy-rifle
      `targetMatch=0` only, or no sync row alongside the worker skip; do not
      mutate the second path until this discriminator chooses among S1/S2/S3.
      Result: S3. Role B emitted 14 worker skips and zero synchronous rows while
      the rifle remained in the local hand; S1/S2 are REJECTED.
- [x] Run read-only build `gr` at the final `CullFgLods::Spawn` call
      `0x141661A57`. With the existing inactive/skip suppression still active,
      classify a nonzero exact `targetMask` as stale/independent queue admission
      (R1), an alternate same-owner/geometry hit as R2, or all-zero matches with
      the visible rifle as a separate render path (R3). Require bounded role-B
      rows, no crash and no body/pistol regression.
      Result: R2. The visible rifle remained; the exact target LODs were absent
      (`targetMask=0`, `activeMask=0`), but generations 3-8 consistently queued
      three other active instances with the same three geometry identities
      (`geometryHits=3`, `ownerHits=0`). R1/R3 are REJECTED.
- [x] Establish the static reverse map for the alternate spawn-rifle LOD set
      selected by `gr`: global FgInstance index -> allocated FgAnimData
      `+0x210` driven list -> retained `Process+0xA8/+0xBC` pair. Exact source
      identities correct `0x14177F7F0` to `AnimMgr_AllocateAnimData` and
      `0x1418C4840` to `FgAnimData_Init`.
- [x] Run read-only build `gs` and resolve the three geometry-matched alternate
      LODs to their live FgAnimData and retained Process. Result: O2 CONFIRMED
      on local-Nadine role B. Pool indices `1000/1001/1002` remained in the
      final queue with `geometryHits=3`, but every sample returned
      `fg=0/owners=0`; the current rifle remained absent and no visual
      regression occurred. O1 and O3 are REJECTED.
- [x] Trace the static release path from FgAnimData's driven-instance list to
      the global FgInstance pool. `FgDrawMgr_FreeInstance 0x14181A2A0` clears
      both used masks, marks the slot pending-delete, detaches it, and queues a
      three-render-frame deferred `FgInstance_Reset` performed by
      `FgDrawMgr_FrameBegin 0x14181A760`. `ForAllUsedInstances 0x14181C1B0`
      independently excludes pending-delete slots.
- [x] Run read-only build `gt` and classify final-queue slots
      `1000/1001/1002`: T1 requires both used bits clear plus matching
      pending-delete mask and `FgInstance+0x254 bit 1`; T2 retains a used bit
      without pending state; T3 is a disagreement between manager mask and
      instance retire state. Keep the existing target-absent, geometry-hit,
      owner-zero and visual positive controls.
      Result: T2 CONFIRMED. All three slots remained `used=1/0`,
      `pending=0`, `retire=0`, with `owners=0`; the user clarified that the
      rifle is visually glued rather than held by the animated hands. T1/T3
      are REJECTED, and pending-delete queue filtering is not justified.
- [x] Trace creation and ownership of the apparent standalone spawn-rifle LOD set.
      Distinguish a direct detach-without-free path (U1), allocation without an
      FgAnimData owner (U2), and a second owner container omitted by the `gs`
      census (U3). Resolve the attach/detach call graph and all primary-used
      mask writers before constructing another runtime discriminator.
      STATIC: U2 is REJECTED because allocation requires and records a non-null
      FgAnimData. Normal NotifyMeshUnloaded is also excluded by the observed
      clear `FgInstance+0x18 bit 1`; normal Relink/remove/destruction frees both
      LOD pointers before `AnimMgr_FreeAnimData 0x14177FA70`.
      SUPERSEDED by the complete `gw` log: the matching geometry belongs to a
      live remote-Chloe rifle generation and its removal does not affect the
      local-hand artifact. Do not resume this renderer branch.
- [x] Build read-only `gu`: resolve the copied owner id through
      `ResolveGameObjectHandleById 0x140A989C0`. Runtime evidence selected W3:
      the three instances carried old id `0x727`, while the current rifle was
      `0x4CD`, and the authoritative registry returned no handle/object.
      However, the local-Nadine copy visually froze immediately after load, so
      the render-thread registry call is a REJECTED method and the result is not
      promoted to CONFIRMED. Build `gv` removes it completely.
- [x] Correct the stale-owner theory from the complete `gw` log. Owner id
      `0x743` belongs to a live remote-Chloe rifle ProcessWeapon/FgAnimData
      generation; removing its older queue triplet did not alter the local
      Nadine artifact. The unsafe `gu` registry inference and another cull
      submission/fix are REJECTED.
- [x] Test bounded build `gw` at the final-cull boundary. Compact
      the in-place `FgInstance*` and paired `0xB0`-stride `FgLodData` arrays
      only when the complete role-B stale-rifle conjunction identifies exactly
      three distinct LOD records. Require `removed=3`, no freeze/crash, the
      glued spawn rifle absent, and all legitimate character/weapon rendering
      unchanged. Any failed guard must call stock with the original arrays and
      count. REJECTED: role B repeatedly logged exact `removed=3` for
      generations 5-10, but the rifle remained visible and the copy became
      extremely laggy. The same generation reached many worker-local caches,
      repeating the census/large-array work. Build `gx` leaves the final-cull
      callsite entirely unpatched; do not mutate this boundary again.
- [x] Run cold read-only build `gy`. It disables rejected `gm..gw`
      attachment/render/cull diagnostics and all old Nadine holster/transform
      probes while retaining the lightweight confirmed cloth observation. The
      only new hook is direct E8 `0x14150F6AF` from
      `ProcessWeapon_Initialize` to `ProcessWeaponBase_Init`. Before any weapon
      input, compare role-B local rifle descriptor selected byte and post-init
      `ProcessWeapon+0x688` against the role-A control: `16` means rightHand,
      `40` means gun_holster. Require no extreme lag, no crash or visual
      regression, and only bounded `NADINE-WEAPON-INIT` rows. Exact I1/I2/I3
      predictions are recorded in `COOP_STATUS.md`.
      Result: both local and remote Nadine rifles initialize normally at
      `rightHand` index 16. The local role-B copy alone misses a later holster
      transition; I2/I3 and a role-B-specific bad init are REJECTED.
- [x] Test `gz/ha` one-shot stock controller rebind on the cold local-Nadine
      rifle. `gz`'s no-hand admission was circular; `ha` admitted and verified
      exact `rightHand 16 -> gun_holster 40` with the same owner and no lag.
      The rifle nevertheless remained glued in the local view. Both direct
      attachment APIs are REJECTED as the main-view fix.
- [x] Run build `hb`: after 750 ms of the same strict cold-spawn state, invoke
      `PlayerWeaponController_StartHolsterTransition 0x1410A3EA0` once with the
      exact slot-zero ProcessWeapon. Keep only the two-callsite holster AnimAction
      executor probe. Classify B1/B2/B3 from the written prediction in
      `COOP_STATUS.md`; reject on any repeat, stuck draw-state 5, crash, surprise
      draw, or unrelated character/weapon regression.
      Result: B3. The one-shot start changed `draw=0->5`, exact
      `weapon-holster` executed `0->1`, and the rifle changed `16->40`, but the
      local image remained glued to the hand. Park the cold Put; a future return
      must isolate the preceding Draw phase instead.
- [x] Recover Nadine's stock Player holster mapping statically. Result: standard
      pistol art directly selects `targSpineProp` and bypasses character
      pelvis/leg selection; the original Nadine table contains no
      `gun_holster_pelvis`, `gun_holster_leg` or generic `gun_holster`, and
      `targRUpperLeg` is a locomotion/body landmark rather than a weapon holster.
      No stock Player thigh transform was found; inspect the live WeaponArtDef
      holster-id list before constructing a compatibility record.
- [x] Trace active pistol/rifle placement and arm-pose selection for legacy
      Nadine. STATIC result: active weapon placement is the `rightHand` attach
      record/weapon `+0x688`; the torso-crossing arm is a separate
      GunMove -> ArmBlockProbes path, with a possible common upstream authored
      animation/retarget defect. Three evidence types and a written G1/G2/G3
      discriminator are recorded in `COOP_STATUS.md`.
- [x] Build and run `fg`, a read-only comparison of Chloe/Nadine original attach
      records, WeaponArtDef holster lists, active `rightHand` record and completed
      ArmBlock output. CONFIRMED: WeaponArt alternative lists are empty; Chloe and
      legacy Nadine use different rig-local `rightHand` records (joint 41/flags
      `0x7FBA` versus joint 30/flags `0`), while their matching pistol/rifle states
      reach the same ArmBlock inline profile families. No crash or new visual
      regression occurred.
- [ ] Trace native NPC `NadineBuddy` firearm presentation before changing Player
      data. Recover its look/rig, active and holstered weapon attach ids/records,
      arm-pose owner and strap process. If authored DC records are unavailable in
      the IDB, add only a bounded read-only NPC branch to the already-installed
      common weapon-attachment hook and compare it with legacy-Nadine Player.
      STATIC: native Nadine is a distinct RTTI class with a pointer-owned
      `Npc::SetupAttachSystem` table initialized from `npc-common` plus current
      look/rig data. `AiWeaponController +0x170` physically selects rightHand,
      pistolHolster or longgunHolster and rebinds through ProcessWeapon virtual
      `+0x7A8`; both Player WeaponHolster and a tempting hardcoded sibling-NPC
      holster/backHolster path are rejected. Native arms use WeaponSkill rather
      than Player GunMove/ArmBlock, and the sling is a separate art-gated
      ProcessWeaponStrap child. Build `fh` snapshots the native table and
      pass-through logs the exact controller pre/post index. CONFIRMED for the
      native pistol: `rightHand` index 19/joint 30 and `pistolHolster` index
      42/joint 99 with its full authored transform. The scene's native table had
      no longgunHolster and the native NPC carried only art `0xC7CB8F0E`.
- [x] Resolve the native `pistolHolster` joint-99 name against the legacy Nadine
      Player rig before transferring it. If the named joint is absent, remap the
      authored record through its proven semantic parent; never copy raw joint
      99 or runtime fields blindly. Build `fi` compares both rig descriptors and
      `r_upper_leg` using only guarded reads; written P1/P2/P3 prediction is in
      `COOP_STATUS.md`. CONFIRMED: joint 99 is not `r_leg_gun_holster`; its hash
      `0xADD0C871` has the strong preimage `r_leg_holster`, while r_upper_leg is
      joint 5 in both rigs. Build `fj` now resolves the actual descriptor-derived
      hash directly in both rigs under a written Q1/Q2/Q3 prediction. CONFIRMED:
      `actualNamed=99/99`; append the exact native pistolHolster record with
      runtime fields cleared and redirect only inactive standard pistols to it.
      Build `fk` completed both writes and left `weapon+0x688` on the new
      `pistolHolster` record, but the user still saw the Chloe holster location.
      REJECTED as a visual fix: AttachSystem selection is not the last Player
      placement owner, so do not repeat or coordinate-tune this record transfer.
- [x] Trace the final Player holstered-weapon visual transform after
      `weapon+0x688`. `PlayerWeaponAnimControl_UpdateHolsterIk 0x14108ECE0` is
      arm-joint IK only and cannot overwrite ProcessWeapon placement. The actual
      shared path is `AttachSystem_ResolveRecordTransform 0x1418B7060` (live
      joint transform times record-local transform), then
      `NdAttachableObject_ResolveSelectedAttachmentTransform 0x141ED1780` and
      its generic local/depenetration stage `0x141ED0A70`. No separate prop or
      owner-class branch was found.
- [x] Build the written S1/S2/S3 discriminator from `COOP_STATUS.md`: retain the
      proven `fk` pistolHolster selection and pass-through-capture both native
      and Player pistol around the ProcessWeapon direct base call into
      `NdAttachableObject_PostJointUpdate_Async 0x141ED28E0`. Compare selected
      record ownership, link/local fields and final `+0x240/+0x250/+0x280` to
      distinguish wrong joint pose, stale weapon link, and a separate drawable.
      Build `fl` patches only direct callsite `0x141515A30`, gates exact native
      and Player pistol owners, and logs at most three spaced samples per
      semantic attachment state. Release build passed.
- [ ] Run `fl`: verify both processes boot with the final-transform probe
      installed, provoke native Nadine's drawn/holstered pistol state if
      available, and draw then holster the standard pistol on legacy Player
      Nadine. Compare `NADINE-FINAL-XFORM role=0` against Player role 1/2; do not
      infer from raw world positions without the logged owner transform.
- [x] Run `fl` and classify S1/S2/S3. CONFIRMED S1: native and Player use the
      same `pistolHolster` record/joint 99, but normalized `+0x130/+0x140`
      transforms differ before the final base update. The base writer only
      regenerates output, and the same logged ProcessWeapon owns the rendered
      pistol mesh. REJECT stale post-attach state and separate-prop explanations.
- [ ] Remap native `pistolHolster` through the shared semantic parent
      `r_upper_leg` joint 5 using the already-proven engine MakeRelative/Compose
      recipe. Require the appended record to target joint 5 with a non-raw
      transform; redirect only inactive standard pistols and preserve drawn
      rightHand, rifle, arms and body under the written prediction. STATIC build
      `fm` is ready; verify `recordJoint=5 changed=1` before judging geometry,
      and treat jitter/sliding as a live-pose-remap falsifier. Runtime confirmed
      `recordJoint=5 changed=1`, but the visual stayed on the back. The active
      tested handgun was picked-up art `0x9A07E17F`; the exact-art reconciler
      only recognizes `0x33E29A1B`, so it made no attach attempt. Generalize at
      the proven Player holster-binding output using WeaponArtDef handgun type,
      explicitly excluding long guns and grenade type.
      STATIC build `fn` now rewrites only binding DWORD 0 after the original
      builder for legacy Nadine, exact slot 1 and types 0/3; it preserves the
      per-art transforms. Require
      `type=0|3 slot1=1 attach=0x8A0249B5 redirect=1/1` plus a physical joint-5
      selection before accepting the visual result. REJECTED: live values were
      rifle type 0, pistol type 1 and grenade type 4, so the assumed enum mapping
      was wrong and the strict gate wrote nothing. More importantly, the hook
      saw pistol/rifle bindings only for local Chloe; retained legacy Nadine
      reached it only for the grenade. Trace the puppet's actual
      `SwitchToWeapon(..., false)` / inactive-reconcile path and classify the
      handgun by its exact slot-1 pointer instead of art id or this enum.
      STATIC build `fo`: the controller path ends in an asynchronous
      PlayerPutGunAwayAction and does not synchronously write `weapon+0x688`.
      Reuse the runtime-proven direct stock attach helper, but gate it on exact
      inactive slot 1, resolved owner and a stable transition generation.
      Re-arm once after each slot-1-active -> inactive edge so a later cycle is
      repairable without a per-frame call. Require old-id -> `0x8A0249B5`,
      `parentOwner=1->1`, `verified=1`, a generation increment on the second cycle,
      no active-slot call and no slot-0/slot-2 generic reroute. CONFIRMED
      mechanically on both copies through generations 1/2/3, but REJECTED as a
      visual/persistent fix: each later generation again began at `0x00AD047A`,
      and the pistol remained on the back despite every call verifying
      `0x8A0249B5`. Do not tune the point or retry timing further.
- [ ] Restore the missing draw/put-away action animation and reparent-event path
      for the retained puppet. User confirms both animations were broken from
      the beginning, and build `fo` emitted zero `NADINE-HOLSTER-EVENT` rows
      despite hand attaches and controller transitions. Statically compare a
      working Chloe cycle with legacy Nadine from ApplyWeaponSwitch through
      PlayerPutGunAwayAction / PlayerGetWeaponInHandAction, `sub_142053820`, the
      animation layer and named-event dispatch `0x14108DE30`. Disable the direct
      inactive repair before the next discriminator runtime test. STATIC
      correction: `PlayerMoveSequencer_EnqueueControllerAction 0x142053820` is
      unconditional queue insertion, not an admission predicate. Real OnStart
      is owned by `PlayerMoveSequencer_UpdatePhase 0x142051EC0`, which changes
      action state `0->1`, invokes vt+0x10 and later phase-specific monitors.
      Build `fp` parks the inactive repair and adds a bounded before/after
      channel-1 lifecycle probe plus an unfiltered retained-owner named-event
      reached bit. Test one Chloe-versus-Nadine draw/put-away cycle and classify
      action-cleared-before-tick, missing sequencer phase, anim-layer failure or
      logger filter suppression from the written prediction in COOP_STATUS.
      CONFIRMED/REJECTED: both looks and both firearm arts reached Get/Put
      `state=0->1`, valid binding and requested grab/holster states, then their
      normal phase monitor changed `1->3`. Zero unfiltered named-event entries
      proves `0x14108DE30` was not reached for either look, while all twelve
      physical attach rows were rightHand and none were holster. Reject a
      Nadine-specific action-loss or this named callback as the discriminator.
      Audit PlayerWeaponAnimControl vtable `+0xD0..+0xE8` and trace the true
      completion boundary. STATIC correction `fq`: callbacks `+0xC0..+0xE8`
      own only state/overlay/prop-animation/named-property work and exact Get/Put
      cleanup callbacks are null. Stock Put instead uses the separate
      `Player+0x1B8` AnimActionMgr: factory `0x1410FD120` creates
      `weapon-holster` action `0x142A46560`, executor `0x14108A640` calls
      controller `0x1410A9670`, and weapon vtable `+0x7A8 -> 0x141ED0580`
      selects the existing-owner attach record at `weapon+0x688`.
      `0x00AD047A` is confirmed `targSpineProp`, a required stock intermediate,
      and absence of direct `0x141ED0370` holster calls in `fp` is not a failure.
      Build `fq` was RUNTIME REJECTED: both processes crashed before input because
      its generic entry trampolines copied two partial instructions and one
      unrelocated rel32 call. Do not restore those entry hooks. Replacement `fr`
      uses owner-local factory pointer `Player+0x1D0`, exact executor E9/E8 sites
      `0x14108A5F9/0x14108A62B`, and checked ProcessWeapon vtable slot
      `0x142A9C6F0`; it retains the same A-E discriminator without copying retail
      prologues. RUNTIME `fr`: stable on both copies. Exact holster action
      executed for both looks, rejecting A-C. Chloe pistol selected
      `targSpineProp -> targSpinePropAnim -> targSpineProp`; every Nadine
      `targSpinePropAnim 0xBBACEF13` request left the current record at
      `targSpineProp 0x00AD047A`. The final stock spine selection explains the
      back holster. STATIC `fs` now clones the validated joint-5
      `pistolHolster 0x8A0249B5` record as the missing `targSpinePropAnim`
      alias, then scoped the checked selector to the exact legacy-Nadine Put
      executor and slot-1 weapon. RUNTIME REJECTED: all logical predictions
      passed (`holsterSafe=11`, alias joint/transform equality and pistol art
      `0x33E29A1B` AD->BB->8A/index 41), while rifle art `0x8075397D` stayed on
      generic/index 40; visually, however, the rifle moved to the thigh and the
      pistol remained on the back. H2 is selected: `weapon+0x688` is not the
      last visible mesh owner, or downstream DrawControl/child binding crosses
      the objects. Do not swap slots or broaden the id redirect. Build `ft`
      parks both fs mutations. Next: trace both ProcessWeapon objects through
      `NdAttachableObject_PostJointUpdate_Async` into their actual mesh children.
      Separately move compatibility-table materialization earlier: initial
      AD/generic selectors returned 0 before repair, explaining why the pistol
      is invisible until its first draw/Put cycle.
      STATIC `fu`: the exact-object graph rejects normal cross-art ownership:
      selected `+0x688`, local/final transforms and child DrawControl `+0x198`
      remain on one ProcessWeapon, and holster IK only writes arm joints. The
      `fs` log instead contains overlapping same-art generations and old objects
      losing slot membership. Reproduce the safe mapping with a 32-generation
      read-only census of slot/current, selected id, mesh, draw gates, active
      instances and final transforms. H1 predicts a slotless old object remains
      active while the current pistol reaches 8A; H2 predicts the exact active
      object's final transform fails to follow its selected record. The earliest
      proven initial-table repair boundary is continuation `0x141BB5049`, after
      Player vslot `+0x448` returns and before `Player+0x1D88` construction.
      RUNTIME `fu`: current exact pistol and rifle objects in both processes had
      the expected art/mesh pairs, `3/3` active child instances and changing
      final-world outputs. Both pistols ended at 8A/joint 5 and both rifles at
      generic/joint 3 even though the pistol was visibly on the back and the
      rifle on the thigh. H2 and the cross-art H3 variant are REJECTED. One
      immediate retired generation did go `3/3 -> 0/0`, but other detached
      generations retained `3/3` and the bounded table saturated, so narrow H1
      remains open. The two camera copies also disagreed about overlapping
      pistol/rifle visibility. STATIC `fv` corrects the instance model:
      `mesh+0x242` is `m_availableLods`, and the three `0x20`-byte entries are
      LODs sharing one `DrawControl+0` FgAnimData. Its driven-list indices live
      at `+0x210/+0x20C`; `FgDrawManager_PrepareInstanceForRendering
      0x14181A9B0` copies the common `FgAnimData+0x1C0` render matrix into each
      `FgInstance+0x140`. Correct ProcessWeapon `+0x240/+0x250/+0x280` therefore
      did not prove the visible matrix. Next build a rolling generation census
      and record per-LOD driven membership, `+0x250` owner id and hashes of
      instance `+0x140`, FgAnimData `+0x1C0/+0x2E0`; do not tune attachment ids.
      Build `fw` implements the read-only post-`0x14181B925` capture, rolling
      32-generation table and per-LOD fields. It also fixes the old 32-candidate
      versus 8-memo out-of-bounds defect. RUNTIME `fw`: every current pistol and
      rifle had three active/driven LODs, matching `+0x250` owner ids and three
      `FgInstance+0x140` hashes equal to FgAnimData `+0x1C0`. Replacement reused
      the same FgAnimData/pool slots for the successor while the old child became
      invalid, so stale LOD H1/H3 are REJECTED. Next statically trace phase-0x40
      owner composition around `0x141A8FC00` versus a separate visible prop.
      RUNTIME `fx`: 768 membership positive-controls but zero geometry-owner rows
      reject equality between the LOD entry's retained `ArtItemGeo*` and
      `FgInstance+0x1B0`; no crash, both processes closed. STATIC `fy`:
      `ArtItemGeo_ResolveFgGeometry 0x1417CB100` resolves resource id `+0x18` to a
      shared `FgGeometry*`; both AddMesh LOD paths pass it to `FgInstance_Init`,
      whose source assertion and store at `0x14192E830` prove `+0x1B0` contains
      that resolved pointer. Learn it from each known live LOD instance, then run
      the same read-only renderer-wide census. Positive controls must now appear;
      only then decide whether an active matching geometry has a
      non-inventory/non-ProcessWeapon owner.
      RUNTIME `fy`: 91 positive geometry-owner rows, 793 membership rows and no
      crash. All hits were exact ProcessWeapon (`processWeapon=0` count zero).
      Both legacy-Nadine pistol/rifle owners had matching retained/runtime ids,
      active/matrix/owner masks and three driven instances. H4 separate prop is
      REJECTED; continue only with H2 by tracing `ProcessWeapon+0x688/+0x6A0`
      and parent AttachSystem into the owner transform published at
      `0x141A8FC00`. Both processes were closed. STATIC `fz`: `+0x6A0` is not
      consumed by the render-transform path. `0x141ED1780` resolves `+0x688`
      through the parent AttachSystem, then sole callsite `0x141ED19BB` passes
      the completed record/joint world transform to
      `NdAttachableObject_ApplyLocalAttachmentCorrection 0x141ED0A70`. It
      copies unchanged when `weapon+0x68C == -1`; otherwise it uses the weapon
      rig and `+0x4F0..+0x52F/+0x5F0/+0x5F4`. A separate concrete competitor
      follows: source-bound `Animating` update `0x141ED2060` can rewrite
      `+0x130/+0x140` at `0x141ED2721/2741` after the selected update and before
      final compose/publish `0x141ED287B/0x141ED288F`. Run bounded `fz` and
      classify Hrec, Hlocal, Hblend or Hanim. Holster IK `0x14108ECE0` is
      statically REJECTED as a weapon-placement writer because it changes only
      arm skeleton joints. RUNTIME `fz`: Hlocal CONFIRMED. Native holstered
      pistol correction was numerically negligible, but Player-Nadine
      `pistolHolster` gained at least `0.09312 m / 20.395 deg` and
      `gun_holster` gained a stable `0.01676 m / 7.070 deg` inside
      `0x141ED0A70`; all used `weapon+0x68C == 0`. Seven paired
      post-selected/final-local rows had `changed=0`, rejecting the observed
      `Animating` overwrite for covered states. Trace writers and initialization
      contract of `+0x68C` and `+0x4F0..+0x52F` on Player versus NPC before any
      mutation. Both processes were closed.
      STATIC `ga`: `WeaponHolster_BuildBinding 0x1414FE540` returns id at `+0`
      and its primary authored transform at `+0x10`; Player controller
      `0x1410A9670` copies that transform to `weapon+0x4F0` after selecting the
      id. Native AI controller `0x1405AE580` only selects the native id and does
      not write local state. The later Player `weapon-holster` action computes
      its `targSpinePropAnim -> final record` handoff and directly overwrites
      `+0x4F0/+0x500` at `0x14108AD4F/AD57`, then seeds the blend. Because the
      scoped repair redirects only that final record to `pistolHolster`, the
      action applies a back-holster continuity delta in the thigh record's
      coordinates. Run the bounded before/after `ga` action snapshot; Haction
      requires the post value to equal the next `fz` localBefore sample before
      any corrective mutation.
      RUNTIME `ga`: the one-shot pistol action changed all eight local-transform
      words, selected `pistolHolster` index 41 and seeded duration/progress
      `0.1/0`; the rifle control copied its generic builder payload without a
      blend. The immediate pistol local sample differed only because stock
      `NdAttachableObject_UpdateLocalAttachmentBlend 0x141ED4390` had already
      advanced it. Static disassembly proves this updater smoothsteps from the
      action handoff endpoint at `+0x620` toward the Player binding endpoint at
      `+0x600`, writes `+0x4F0`, and clears the duration at completion. Hscript
      is REJECTED; Haction and its stock continuation are resolved.
- [x] Run bounded `gb`: after only the exact redirected legacy-Nadine slot-1
      action, validate `pistolHolster`, replace the incompatible Player local
      transform with the runtime-proven native identity and clear its blend.
      Require one applied repair row, identity at the next local-correction
      sample, correct visible thigh placement, no rifle repair and no body/Chloe
      regression. Identity plus wrong placement selects independent Hrecord.
      Result: REJECTED as sufficient. Both PIDs applied identity and cleared
      blend at the exact scoped action; every subsequent covered pistol sample
      retained `pistolHolster` joint 5 and identity local, with final visible
      ProcessWeapon/child DrawControls positively covered, but the user saw the
      unchanged back position. The inert mutation is removed. A later exact
      owner-relative comparison supersedes Hrecord for the visible trajectory:
      Player and native final pistol translations are thigh-equivalent within a
      few centimetres.
- [x] Run bounded read-only `ge` holster-IK discriminator, then attribute any back
      result at the newly resolved Chloe-map boundary before attempting a fix.
      Prior runtime census proves legacy Nadine's PlayerWeaponAnimControl resolves
      joints `-1,5,-1,3` and leaves both marker bases identity, while Chloe
      resolves `101,5,199,3`; native/Player Nadine share the real pistol marker at
      joint 99/name `0xADD0C871`, but Player hard-codes absent
      `r_leg_gun_holster 0xE231DB44`. Put requests `s_weapon-holster` on
      `WeaponPartial 0xE2B990B9`; final reducer `0x14108F800` is decisive.
      `AnimStateSnapshot_EvaluateReferenceSetMask 0x1418B3770` proves leg has
      priority whenever the selected snapshot contains it, and the final IK path
      never reads `weapon-holster-point-*`; the old direct-native Hpoint claim is
      REJECTED. Asset audit now proves generic `anim-player.bin` uses
      `p-holster-chest^idle-stand-part 0x8DCA21F2`, while
      `player-mesh-overlays.bin` contains explicit chest->back/leg/waist maps.
      Native helper `0x14109D450` is gated on `chloe-base`, resolves
      `*chloe-weapon-anim-overlay-map*`, and controller vtable `+0x3C0` tests the
      mapped id before the original. The DC00 parser now ties
      `player-mesh/player/chloe 0x3F334972` specifically to chest->back at
      `0xDFAB0`, while neither legacy look `0xD27C074F` nor plain `nadine` owns a
      player-mesh overlay object. Outfit controls prove this is per-look:
      `vill-nadine-08/10/12` and `fh-chloe-08` map chest->leg, but
      `vill-nadine-16` maps chest->back. The remaining native bridge is now exact:
      `Player_ApplyLook2MeshKnowledge 0x1410D6AD0` reads `Player+0x15A0`, formats
      `player-mesh/<Look2-name>` and commits the record into Player animation
      knowledge during spawn and `Player_ChangeLookId`. The compatibility hook
      changes only the `Player+0x15A4` getter result inside the render builder and
      the visible `+0x170/+0x190` pair; it never changes `+0x15A0`. The
      Nadine-rendered Chloe-born Player therefore keeps the exact Chloe
      chest->back knowledge map. Hchloemap is a strong STATIC root cause.
      `ge`: valid nonzero leg plus identity basis selects Hmarker;
      missing/invalid/zero selects Hclip; valid back proves only that the final
      snapshot lacks leg. Do not assume Put directly reaches `0x14109D450`:
      `0x1410A3EA0 -> 0x141096BB0` only builds holster-speed knowledge and requests
      `s_weapon-holster`. Do not write the unsafe whole Look2 or substitute an
      entire outfit table: legacy `0xD27C074F` has no player-mesh object and the
      existing outfit tables contain hundreds of unrelated, disagreeing rows.
      Runtime layout and consumer are now exact. `AnimOverlays_NotifyDcUpdated
      0x1417F92A0` watches `anim-overlay-set`; the per-instance state owns its set
      pointers at `+0x8` and count at `+0x24`. `AnimOverlayState_FindMatchingRow
      0x1417FA6E0` walks each set's 0x28 rows (`set+0`) through the parallel
      source-id array (`set+0x20`), and `AnimOverlayState_ResolveAnimationId
      0x1417F9F00` replaces the current id with `row+0x4` before final clip lookup.
      Do not mutate the shared DC row: that would redirect Chloe globally. The
      Player ownership path is now resolved:
      `Player+0x1A0 -> owner+0x60 wrapper -> layer+0x18 master ->`
      `AnimOverlayState_Copy -> track+0x8 local -> 0x1418B2940 resolver`.
      A final resolver sees the local copy, not the master pointer; classify and
      clear destination states at the copy boundary. Next enumerate the smallest
      exact campaign-Chloe chest-to-back family, including holster/draw and
      mirrored/group variants, then scope **Rrow** to legacy Nadine plus only
      those source ids. No state is mutated.
      Build `gf` is the bounded first discriminator: it tags track-local states
      only at `AnimOverlayState_Copy`, and only rewrites the exact idle row
      `0x8DCA21F2: 0x11438F72 -> 0x4A5559F7`. Test pistol put-away and settled
      idle placement first; do not claim transition animation fixed from this
      single source id. Written positive and negative predictions are in
      `COOP_STATUS.md`.
      Runtime REJECTED the single-row sufficiency claim: both owner-0/owner-1
      master-copy markers appeared, but no `NADINE-HOLSTER-ROW` marker did and
      the pistol remained at the back. Static comparison now proves a 144-row
      chest holster/unholster family with identical row tails and back-only versus
      leg-only targets for Chloe versus `vill-nadine-08`. Next replace the
      append-only 64-state tag list with a current bounded mapping and observe
      every exact family hit (tagged and untagged) before widening mutation.
      Build `gg` implements that discriminator: a direct 1024-slot current-state
      map replaces stale classifications, all 144 source/back/leg tuples are
      embedded from the reproducible PSARC audit, and every family hit is logged
      whether tagged or not. Only an exact tagged Chloe-back result may be
      replaced. Test one standard-pistol draw/Put cycle on legacy Nadine; classify
      Gfamily/Gowner/Gchain/Gother from the written prediction in
      `COOP_STATUS.md`.
      RUNTIME `gg`: exact family hits occurred in both processes, rejecting
      Gother. The resolver state equalled the retained Player's master pointer,
      not the tagged local copy, and every output had already passed a longer
      remap chain. The gold suppressed visual remains standard pistol art
      `0x33E29A1B`; its gold/suppressor ids are child resources, so the test is
      valid. Build `gh` replaces the stale copy/tag scheme with an exact
      master-state `AnimOverlayState_FindMatchingRow 0x1417FA6E0` shadow: copy
      the 0x28 Chloe row, change only target `+0x4` to the paired leg id, and
      preserve all group/variant tail fields plus downstream chaining. Test one
      legacy-Nadine pistol Put against the written `gh` prediction. RUNTIME
      REJECTED: the shadow changed the terminal clips from the named backflip
      family to the named leg family, but the user still saw the back motion and
      observed new jitter. The simultaneous final holster-IK reducer remained
      `reference=0, weight=0, valid=0` in both processes. Remove the row shadow;
      do not tune or widen the 144-row table.
- [ ] Resolve Hlookup/Hblend/Hcontent downstream of the proven leg overlay output.
      Trace `AnimationDatabase_ResolveClipWithOverlays 0x140516230` through
      `AnimSetRemap_ResolveAnimationId 0x1417CA2E0` and
      `AnimationDatabase_ResolveClip 0x1417C8E20`, then identify the active
      snapshot weight/owner that feeds the zero-valued final holster-IK reducer.
      Compare a stock back positive control. Build `gi` parks the rejected row
      mutation and keeps only the read-only 144-family result probe; do not run a
      new mutation before three independent static evidence types and a written
      discriminator. STATIC `gj` meets that quota without restoring the row
      mutation: the mirror category precedes player-mesh, both leg outputs are
      terminal in all 406 player-mesh objects, and the exact clips exist in
      `anim-player-movement.pak`. Its bounded wrapper probe runs the proven leg
      terminal through the same active `AnimSetRemap` and `ResolveClip` only into
      stack-local output after the stock back lookup. Test one pistol Put and
      classify Hremap/Hnull/Hvalid-noIK from the written prediction in
      `COOP_STATUS.md`; no visual change is expected.
      RUNTIME `gj` was inconclusive: exact master-state family results and the
      action executed, but the wrapper-scoped lookup line never appeared.
      Static xrefs and `AnimStateSnapshot_SnapshotAnimState 0x1418B24F0` prove
      the resolver also feeds `AnimSetRemap` directly. Build `gk` therefore
      correlates the exact family result with the next same-thread remap input,
      logs both return RVAs, and compares stock/leg remap keys and clip pointers
      without animation mutation. Run the written `gk` discriminator next.
- [ ] After firearm geometry is correct, locate the rifle sling as a separate
      prop/visibility/attachment candidate; do not block the holster mapping on it.

## P0 - Remote reload animation sync (VISUALLY CONFIRMED, 2026-07-27)

- [x] Trace the exact accepted command edge. `Player_BuildAcceptedActionInputState
      0x140C9D850` queries `reload` and `reload-release` at
      `0x140C9E611/0x140C9E632` through Player+0x39A0 with edge=0, applies native
      ProcessWeapon eligibility, and writes Player+0x47B0 accepted bytes +2/+3.
- [x] Trace native presentation: RTTI/vtables prove clip action
      `0x1410983D0/0x142A48948` and loop action
      `0x14109A5C0/0x142A48AF8`; they drive `s_weapon-reload-clip[-m]` and
      `s_weapon-reload-loop-*` through PlayerWeaponController state 3.
- [x] Reject generic mask-only, `reload-hold`, direct ammo callbacks and the
      downstream keyed `ReloadWeapon` flag as relay boundaries. Full evidence
      and the first falsifiable runtime discriminator are in `COOP_STATUS.md`.
- [x] Implement exact high-bit semantic `reload`/`reload-release` relay in `ij`, force
      only retained slot-1 PlayerInputCommandState queries with edge=0, and add
      bounded send/receive/force markers. Do not write receiver slot-0 ammo.
- [x] Reject `ij` packaging at cold boot: it used the decompiler's post-call
      result addresses `0x140C9E622/0x140C9E637`. Build `ik` corrects the two
      verified E8 starts to `0x140C9E611/0x140C9E632`; behavior was not exercised.
- [ ] Optional classification only: capture the written clip plus shotgun-loop
      action-entry markers if exact per-weapon delay data is needed. The user has
      already confirmed that remote reload is visible and works on the current
      build; do not block vertical ADS work on this diagnostic.
- [ ] `ik` runtime: remote reload is visible and first attempts reached the exact
      receiver callsite in the same packet sequence, but a later one-frame press
      vanished before serialization (`seq=2203` received release only). Add a
      bounded sender press latch and defer release until that press has crossed at
      least one send interval; then timestamp native clip/loop action entry to
      separate remaining visual startup delay from transport.
- [x] Implement `il`: retain exact local reload press for 120 ms, defer the
      160-ms release window until press ends, log the semantic packet phase, and
      wrap only the statically resolved clip/loop vtable slot-2 action entries.
      Release build compiles; the two action-entry names are saved and verified
      in the single IDB. Run the written runtime discriminator after the
      world-weapon reliability fix is ready for the same cold launch.
- [x] Current `it` visual confirmation: the user explicitly verified that remote
      reload works. The bounded current log contains installation markers but no
      clip/loop action-entry row, so visual synchronization is CONFIRMED while
      exact action-family timing remains intentionally unclassified.

## P0 NEXT - World-weapon replacement reliability follow-up (2026-07-27 `ik`)

- [x] REJECT `im/in` WeaponInfo-slot targeting. Runtime produced absent COPG and
      garbage `WeaponInfo+8` values, while static GiveWeapon and FHUD paths prove
      the manager slot comes from the resolved weapon art-definition record
      `+8`; unconditional generic endpoint blocking caused the no-drop regression.
- [x] Implement `im` HUD-independent consumed-model discovery through the fixed
      AnimMgr FgAnimData pool. Require allocation bit, generation-valid weak
      handle, exact ProcessWeapon vtable/art, world-unparented ownership and an
      exact stable ID or unique-art fallback. Broad heap/registry scans remain
      forbidden. Release build compiles; run the written `im` discriminator.
- [x] REJECT `in` owner-thread HUD cleanup. Exact primary weak/map selection and
      `destroyed=1` with zero secondary matches still left Chloe's icon visible.
- [x] CONFIRM `io` stock replacement targeting: all tested flags=3 swaps complete
      `gave=1 detached=1 slotReleased=1` in 31--78 ms. REJECT its retained-Player
      HUD gate: the visible stale prompt produced no suppression row.
- [x] REJECT the two `ip` identity assumptions while retaining its proven stock
      replacement: all dropped mappings had `remoteDropId=0 stored=0` because the
      exact context payload was zero, and every post-OnInteract HUD-twin lookup
      had `exactMatches=0`. Sequence 2 measured 2359 ms StatePacket latency;
      sequence 3 then expired with `hudMatching=0`, matching the surviving M30.
- [x] PARTIAL `iq`: explicit drop ID, immediate stock replacement and stale-state
      hold are confirmed in one direction (`latencyMs=0`, map stored, catch-up
      15 ms). Reverse replacement expired at `reason=6`: whitelist art
      `0x33E29A1B` is intentionally excluded from COPG, so its real info never
      reached the receiver. The M30 consequently did not fall out.
- [x] PARTIAL `ir`: exact nested GiveWeapon capture is confirmed across repeated
      Type 97/Arrowhead swaps (`info=1`, `flags=0x7`, 316 bytes); receiver stock
      replacement is immediate and complete. The weapons duplicated only because
      the paired-drop resolver incorrectly required the transient `+0x690 bit 3`
      after the retained weak cell had moved to the ground representation.
- [x] PARTIAL `is`: the reverse transaction now consumes through `source=drop-map
      match=paired-drop-id killed=1`, confirming the retained weak retarget fix.
      A later Mazur -> Arrowhead transaction still detached the receiver Mazur
      only after 2281 ms because exact COPW incorrectly reused the fire relay's
      two-second new-puppet grace; the user did not see it at the exchange point.
- [x] CONFIRMED `it`: 23 consecutive mixed weapon replacements detached the old
      receiver object in 0--16 ms with `DROP-MAP stored=1`; every reverse mapping
      consumed through `source=drop-map match=paired-drop-id killed=1`. The user
      confirmed the old weapon now appears at the exchange point. No expiry,
      rollback, duplicate or crash occurred. World-weapon reliability is closed.

## Rule

If a task does not directly move the project toward **полноценный кооп**, it should stay parked.

## P2 — Weapon pickup sync (добавлено 2026-07-06, после закрытия weapon-display кластера)

**Контекст:** оружейный кластер (смена/holster/спавн) решён и user-verified (commit
b3549d8). Подобранное с земли оружие пока показывается приближённо (свой длинноствол
по слоту): точный арт требует полного weapon-info блока — give с одним art@0x80 для
не-whitelisted артов НЕ прилипает (движок спавнит-и-роняет; см. REJECTED-фонтан в
`FINDINGS_ARCHIVE.md`, 2026-07-06).

**Задача A — точный арт подобранного оружия.** Реверсить схему weapon-info блока
(0xC0 байт, initialize_info 0x14... в `coop_runtime_offsets.h`
`kInitializeWeaponInfoRva`):
какие поля кроме art@0x80 обязаны быть валидны, чтобы give прилип для произвольного
арта. Кандидат: скопировать блок из реального пикапа сендера (hook на give котроллера
локального игрока), а не синтезировать.

**Задача B — анимация подбирания на кукле.** ВАЖНО: Player_TryStartBumpAction
0x140CE8E10 — это CROWD-bump (толкание NPC), НЕ подбор (проверено декомпилом
2026-07-06). Реальные якоря подбора:
- `PickUpController` строка 0x142953600, ref 0x1404C132D (фабрика/ctor контроллера —
  он крутит анимацию подбора у игрока);
- `WeaponPickup` строка 0x1429118D0, refs в sub_1400C5560 (класс объекта-пикапа
  на земле; там же "'%s' is NULL or not a WeaponPickup");
- `ProcessPickup` 0x1429397C0, `instant-pickup`/`disable-pickup` скрипт-флаги.
План: sender ловит старт своего PickUpController → шлёт событие с идентификацией
пикапа (name-hash/позиция) → receiver находит зеркальный WeaponPickup и запускает
PickUpController куклы. Сначала квота статических доказательств (`AGENTS.md`).

**Уточнение задачи B (статический разбор 2026-07-06, поздний заход):**
- Подбор идёт через ГЕНЕРАЛЬНЫЙ интеракт-диспетчер игрока sub_140C85D20
  (строки player-pressed-interact-nearby / player-interact-hold-*; 4+ вызывающих
  контроллера). Ветка пикапа: sub_140C7FF40 резолвит из def'а пикапа anim-ids
  (weapon-pickup-swap / weapon-pickup-when-no-weapon) И command-id интеракции
  (out-параметр a6).
- Проверка нажатия: sub_14183A3D0(g_LocalPlayerInputManager_Ptr + 3504 +
  3056*screen_idx, cmd_id) сканирует ДИНАМИЧЕСКИЙ СПИСОК command-id по
  +0x590 (1424) в пер-скриновом блоке — это НЕ битовая маска +0x518, которую мы
  уже релеим. Поэтому у куклы интеракции не срабатывают вовсе (объясняет и
  отсутствие случайных катсцен от куклы — сохранить это свойство!).
- Два пути релея пикапа: (1) инжект command-id в список экрана-1 (найти писателя
  списка; ср. kAddJoypadDisabledCommandsRva 0x1839FB0 — команды в этом мире
  списковые); (2) прямой вызов конца ветки пикапа (функция «start pickup action»
  после инпут-чека, ~0x140C87xxx после LABEL_181/HIBYTE(g_NetModeFlags)) с
  (player, pickup) — обходит инпут вовсе, но надо доразобрать её сигнатуру.
  Путь (2) предпочтителен: точечный (только пикап, катсцены не задеваются).
- sub_140C88130(disp, cmd_id, table): чек нажатия — сканит таблицу кандидатов
  (записи 12 байт: {cmd_id, hold_time float, +9 pressed, +11 hold-mode}; hold
  сверяется с engine clock через disp+13720). Таблица строится диспетчером из
  пер-скринового инпута каждый кадр. => Точки инжекта: (1) писатель списка
  +0x590 пер-скринового блока (до диспетчера) или (2) прямой вызов start-pickup
  (после чека). Следующий шаг: найти писателя +0x590 списка (xref на поле) И
  дотрассировать ветку диспетчера от v323=1 до вызова старта пикапа.
