# Task Pool

## Primary Objective

Полноценный кооп кампании между двумя копиями игры. Текущий порядок работ:

- локальная Chloe на первой копии и локальная Nadine на второй;
- полное представление удалённого игрока в обеих копиях;
- синхронизация всего состояния и всех действий игроков;
- синхронизация противников только после игроков.

## P0 CURRENT: locate the final rendered Nadine arm-pose owner (2026-08-11)

- [x] Reject the local-only presentation candidates that do not own the final
      drawn pose: holster/back transforms, the flashlight/debug kill switch,
      `auto -> run-gun-idle`, a forced nonempty WeaponPartial, individual Chloe
      hands clips and blanket `WeaponIKFeather` suppression.
- [x] Close the local-pose scheduling boundary. `AnimManager` performs the
      normal blend and Player vtable `+0x408 = Player_PostAnimBlending_Async
      0x1410EB6A0` before the subsequent JointBlend job consumes the persistent
      local-pose array. This is not the final rendered-palette boundary.
- [x] Implement a narrow A-to-B return channel for the already-correct remote
      Nadine pose: role A captures only C39 joints `19..30` after stock
      postblend, endpoint-validates and sends a 616-byte `ARMP` packet, and role B
      reapplies the fresh matching-art rows after its own stock postblend.
- [x] Make admission fail closed on exact Player/Fg/skeleton/weapon ownership,
      P99 or Mazur art, `PlayerWeaponController::WeaponOut (state 2)`,
      `FastGrenadeController::Idle (state 0)`, zero movement/input, no cover,
      350 ms anchored dwell, endpoint-validated session and at most 32
      StatePacket sequence lag. Normal head-only FreeLook remains allowed.
- [x] Recompute each copied world locator through the native
      `JointCache_RecomputeWorldLocatorFromLocalPose 0x141688320`; the later
      JointBlend invalidates/rebuilds descendants, so fingers/helpers inherit
      the corrected primary paired arm chains.
- [x] Run the bounded stationary `abt` exam after release build, strict review,
      IDB label save, documentation commit and push. Both hooks installed and
      normal StatePacket traffic flowed, but role A emitted zero `CAPTURE/SEND`
      rows and role B emitted zero `RECV/APPLY` rows. Classify the result as
      **INCONCLUSIVE / fail-closed admission**, not as rejection of the postblend
      pose boundary: no arm-row mutation ran.
- [x] Statistically rank the pre-capture gates. Fresh logs already support the
      role-A retained Player/screen/look, same-owner weapon presentation and idle
      FastGrenade controller. The leading asymmetric candidate is the common
      requirement `PlayerWeaponController+0x134 == 2`: puppet hand attachment is
      accepted separately and does not prove that its local action lifecycle ever
      reaches the authoritative local-Player state.
- [x] Run exactly one bounded read-only `abu` admission census after release
      build, independent strict review, documentation commit and push. It logs
      only the first changing source stage, at most 32 rows and no faster than
      250 ms. Draw P99, hold stationary for at least 0.5 s, then Mazur. A
      game-thread `PUPPET-HAND` row with matching art/rightHand plus an async
      `weapon-controller` row with non-2 `state` confirms the leading source
      asymmetry; an earlier skeleton/owner row or a later peer-idle/dwell row
      selects that competing gate. A postblend entry without a retained-1 match
      isolates retained identity; complete entry silence isolates current
      `+0x408` reach. Do not alter any gate until this discriminator returns a
      concrete value.
- [x] Classify `abu`: both P99 and Mazur reach exact retained Player1 postblend,
      ED25/C39/580, exact controller/owner/current slot/state 2, grenade state 0,
      same-parent ProcessWeapon and physical rightHand record `0x61A48E01`.
      The first stable rejection is `peer-idle` with zero movement/velocity and
      only `action=0x9`. Historical idle Joypad rows independently identify
      `0x9` as the normal PC neutral pattern; aim adds `0x200` (`0x209`).
- [x] Run one bounded `abv` visual exam after build/review/commit/push.
      Admit only exact neutral input patterns `0` or `0x9` on both source and
      target; every additional action bit remains rejected. Detailed `abu`
      census was disabled. Result: zero `CAPTURE/SEND/RECV/APPLY`; the neutral
      fix is necessary but not sufficient because full valid weapon packets
      alternate with producer-suppressed metadata packets and the latter reset
      the 350 ms source dwell.
- [x] Run the final bounded `abw` local-row exam. Role A emitted
      `CAPTURE/SEND`, role B emitted matching `RECV/APPLY applied=1`, and the
      local grip remained visually unchanged. Classify the twelve-row
      `Fg+0x100` transfer after Player `+0x408` as **RUNTIME REJECTED** for this
      defect; disable it in build `abx`.
- [x] Close the actual final visible boundary statically. The later
      `AnimManager_AsyncObjectUpdateJointBlendJob 0x141A92A00` consumes
      `Fg+0x100`, executes command segments, and only then publishes the current
      48-byte-per-joint skinning palette at `Fg+0x230`. The renderer copies that
      palette in `FgDrawManager_PrepareInstanceForRendering 0x14181A9B0`.
      Exact callsite `0x14181AB33` performs the only current-to-upload copy while
      the native `Fg+0x220` lock is held; the upload buffer is not rewritten
      later in the function.
- [x] Prove the retained Player body-Fg identity at the renderer seam and derive
      the smallest exact C39 arm/hand/helper matrix set. The read-only
      `scripts/pak_skin_influence_audit.py` validates all 97 mesh partitions and
      four LOD palettes in each visible Nadine body/shirt/head package. Exactly
      88 joint matrices (4224 bytes) have direct arm/finger/shoulder-helper skin
      weights; every weighted ordinal has the same StringId in C39. Elbow,
      palm and weapon-attachment locators have zero direct body weights and are
      intentionally absent from this late body-palette repair.
- [x] Implement STATIC build `aby`: preserve stock `memcpy` at `0x14181AB33`,
      then capture role-A retained-Nadine's 88 current final matrices or replace
      only role-B's upload-buffer copies under the still-held native lock. The
      exact Player/Fg/skeleton/art/WeaponOut/grenade/idle/dwell admission remains
      unchanged. Transport is ARMP v2: four endpoint-authenticated 1104-byte
      fragments reassembled atomically by `{session,sequence,art,sourceState}`.
      No `Fg+0x100`, `Fg+0x230`, attachment locator, weapon Fg, head, spine or leg
      state is changed. Release build and relay-object ABI inspection pass.
- [x] Run and reject `aby`. Role A emitted final-palette
      `CAPTURE/SEND fragments=4`; role B emitted matching `RECV` and repeated
      `APPLY ... joints=88 applied=1` with sequence lag 1--3. The user observed
      no change in the local-Nadine grip, so `Fg+0x240` replacement is
      **RUNTIME REJECTED** as a sufficient visible boundary. `applied=1` proved
      only successful write/readback, not a nonzero source/target delta.
- [x] Close and reject the hidden per-instance-palette alternatives after the
      negative `aby` exam. Both retained Player Fg objects had the alternate
      pair `+0x250/+0x258=0`; each owned 20 driven instances, and every instance
      had `+0x260/+0x268/+0x270=0`, count `+0x30=580`, current pointer
      `+0x78==Fg+0x240`, and CPU pointer `+0x80==Fg+0x230`. All 88 selected CPU
      rows still differed (`88/88`, aggregate absolute delta `101.106161`).
      `SkinningDeformer::Skin 0x141744210` consumes the current bone buffer from
      InstanceJointData and `0x141744100` encodes that pair into GPU descriptors.
      Alternate-pair, mesh-local-remap and zero-delta explanations are therefore
      rejected for this exam.
- [x] Correct the former exclusive-pixel-owner claim. The five resolved
      `nadine-rural` leaves each allocate four LOD instances into the measured
      retained DrawControl Fg list, giving the exact 20-instance runtime census.
      Historical build `et`, however, matched only the ordered five-mesh
      DrawControl signature from a global visibility hook; it had no role,
      retained-Player, screen or Fg equality gate. Its hides prove body-mesh
      membership, not that the externally observed correct Nadine grip was
      produced by the exact Player1 palette later used as the `aby` donor.
      `Fg+0x230` is allocated from registered context 82
      `CPU_ACCESS_ONLY`, while `Fg+0x240` comes from context 72 `GPU_UPLOAD`.
      The rejected `aby` wrote the latter with plain CPU stores only after the
      native upload, so its readback did not prove effective GPU visibility.
- [x] Close the full normal render chain after the native upload. The locked
      `0x14181AB33` copy publishes through
      `FgInstance_BuildPerFrameRenderRecord 0x141930990`,
      `FgInstance_PublishPerFrameStateToMeshInstance 0x141931810`,
      `SubMeshSkinning` and `SkinningDeformer_SkinSubMesh 0x1418778F0` into
      `SkinningDeformer_EncodeBoneBufferDescriptors 0x141744100`. No supported
      later body-bone pointer overwrite remains.
- [x] Establish the exact visual producer of the externally correct Nadine grip
      before choosing another donor. Join the role-A retained Player1 body, any
      surviving native NadineBuddy/body DrawControl and the actual main-view
      submission in one frame, then compare the semantic P99/Mazur arm rows in
      that exact producer against local Player0. The asynchronous `88/88` delta
      does not prove that the transported rows encoded the grip difference.
      Runtime `aca` confirms both owners are independently selected in the same
      primary colour view at different world positions: native `6/53` selected
      instances/submeshes and Player1 `4/59`, repeated after object recreation.
      Therefore retained Player1 is a real rendered donor candidate, not an
      unrendered body; native-only and overlapping-owner explanations are
      rejected.
- [x] Close the symmetric local pixel-owner premise with read-only build `acc`.
      The first two complete role-B samples selected local Player0 at `6/62`
      instances/submeshes while all 16 native instances had `0/0`; every
      selected Player0 instance was direct, active and world-matched. A later
      recreated generation selected both at different worlds (`native 6/53`,
      `Player0 4/59`, `sameWorld=0`). This confirms that `abw/aby` wrote the real
      local body producer and rejects native substitution or an overlapping
      duplicate as the reason their pixels did not change.
- [x] REJECT `abz` before runtime. The engine seam at `0x14181AB33` is an
      ordinary imported three-argument `memcpy`; staging the same 88 rows in
      `Fg+0x230` only makes that copy produce the same `Fg+0x240` bytes that the
      rejected `aby` wrote immediately after it. Allocator names alone do not
      prove a distinct cache/publication transaction. The current resolved
      `VCRUNTIME140` path for the exact 0x6CC0-byte copy uses cached AVX stores,
      not its large-copy `vmovntdq/sfence` path, so another visual exam is not
      earned. The experimental source-swap code was removed.
- [x] Close the exact late deformer latch. After per-frame publication,
      `0x141967C3C..0x141967C4E` copies InstanceJointData `+0x18 -> +0x00`,
      `+0x28 -> +0x08`, and `+0x90 -> +0x10`; SubMeshSkinning consumes the
      latched `+0x00/+0x08` buffers. The true companion path is frame record
      `+0x68 -> jointData+0x28`; correct the former claim that `+0x90` was the
      companion bone-buffer pointer.
- [x] Reject a stale-Chloe/numeric-joint WeaponIK chain as the semantic cause.
      `WeaponIKFeather_InitializeForPlayer 0x1418F1610` builds fresh left/right
      shoulder-to-hand-prop paths on the concrete current Player Fg skeleton
      through `JointChain_BuildFromNamedAncestorToDescendant 0x1418203D0` and
      caches semantic offsets through `JointChain_FindNamedJointOffset
      0x14181D750`. C39 has the exact interleaved shoulder/elbow/wrist/palm/
      hand-prop hierarchy, so neither a missing palm nor retained Chloe ordinal
      explains the local-only pose.
- [x] Close the remaining two `WeaponIKFeather` scalar/control inputs. Exact
      PlayerWeaponController vtable `+0x240 = 0x1410ACB70` returns the generic
      hand-side selector at controller `+0x444`, initialized to 1 and changed
      only by SwitchWeaponHand lifecycle. ProcessWeapon vtable `+0x1E0 =
      NdGameObject_GetAnimScale 0x141BB6900` reads the generic FgAnimData
      animation scale at `+0x140` (default/initial value 1.0). The solver only
      flips its left/right interpolation for the former and multiplies its
      scalar weight by the latter; neither contains a P99/Mazur spatial target.
- [x] Close the first surviving semantic boundary at the sole ordinary call
      `PlayerWeaponIKAction_Update+0x8CC -> WeaponIKFeather_Apply
      0x1418F2700`. `JointSet_AllocateAndSeedBuffers 0x14181E780` seeds the
      solver from the current JointCache and
      `JointSet_CommitJointTransformsAndReset 0x14181ECD0` writes the result
      back before the later, already-rejected body-palette paths. Build `ace`
      captures exact C39 local rows `19..30` immediately before and after stock
      Apply for selected retained Player1/Player0. The hook is read-only, calls
      stock exactly once, preserves every register/stack argument in object-code
      review and emits at most one P99 and one Mazur sample per owner.
- [x] Complete the automatic WeaponIK boundary exam. A role-B Player0 boundary plus
      no role-A Player1 boundary selects a local-action lifecycle split. Boundaries
      on both sides with differing pre rows select upstream authored/retargeted
      composition; matching pre rows but differing post rows/scalars select the
      Feather solve/control boundary; matching pre and post rows rejects this
      semantic owner and authorizes no mutation. Do not patch a row, scalar,
      action or attachment until one of those mutually exclusive outcomes is
      observed for P99 and Mazur.
- [x] Classify the first `ace` run as partial. Mazur produced complete symmetric
      Player1/Player0 boundaries, rejecting a remote lifecycle bypass; both
      changed only joints `19..26`. P99 hit the census but failed a diagnostic
      network-quaternion predicate before stock and the attempt was consumed.
      Build `acf` removed quaternion normalization, separates claim from
      successful emission and retries after capture failure.
- [x] Reject the remaining `acf` twelve-float content filter. Fresh stationary
      P99 reached exact role-A owner1 and role-B owner0 boundaries repeatedly,
      but both failed only the pre-capture. A 48-byte JointCache row stores its
      Locator at rotation `+0x10` and translation `+0x20`; the leading 16 bytes
      are not ordinary transform floats. Build `acg` copies the exact raw row
      after retained-owner, generation, pointer and readable-span validation and
      performs no content interpretation or game write.
- [x] Run the missing stationary P99 `acg` pair. Both sides produced complete
      pre/post boundaries with identical stock scalars/result. Both changed only
      joints 19..26; cross-owner rotation differed by at most 2.04 degrees before
      and 3.15 degrees after, translations were identical through joint 29 and
      hand-prop joint 30 differed by only 0.834 mm. Correct the earlier
      row-independent interpretation: composing the parent chain leaves about
      6.8 mm / 2.13 degrees at both final hand endpoints and different two-hand
      separation. This rejects lifecycle/different controls, not the upstream
      arm pose; stock Feather materially closes the hands. Build `ach` disables
      the completed census.
- [x] Implement the separate ProcessWeapon discriminator as build `aci`. The
      exact stationary P99/rightHand census runs at the
      already-installed direct callsite `0x141ED19BB`, comparing role-A Player1
      and role-B Player0 resolved hand input, weapon-local correction and stock
      output. It admits only exact P99/rightHand joint30/WeaponOut/current-slot
      ownership and takes four read-only samples per owner. Do not re-enable
      broad rejected transform probes and do not mutate any arm row, clip,
      selector or scalar.
- [x] Run the written `aci` stationary drawn-P99 exam. Both owners produced all
      four exact P99/rightHand/joint30/WeaponOut samples. Their primary
      `weapon+0x4F0/+0x500` corrections were the same identity transform and
      stock output equalled the resolved hand input to within at most
      `5.67e-6` degrees, with zero translation delta. **Hweapon-local is
      REJECTED:** the drawn P99 rigid body follows joint 30; the remaining local
      grip defect is upstream in the arm/hand pose or its semantic timing, not
      an art-local weapon offset. Build `acj` disables the completed census.
- [x] Correct the old `zn` special-pair false negative statically. Exact Mazur
      object `8B9C0AA6` maps ordinary source states `BE7CB4A2/EB712BFC` to the
      atomic pair `56C57AD2 + 283966ED` with blends `0.8/0.7`; the second clip is
      the Player-compatible two-arm lane `[21..28]`. Native
      `PlayerWeaponAnimControl_RequestCustomWeaponPart 0x14108B940` retains both
      IDs at control `+0x20/+0x24` and Player `+0x1CC/+0x1D0`. Independent VM
      selectors owned by `custom-weapon-part F5D4FF02` and
      `custom-weapon-part-with-feather 11EE475B` read those Player fields.
      The feather root reads `Player+0x1D4` as its blend scalar, generates both
      child command streams and appends native command 7 with that weight.
      Historical `zo` selected the pair's unique first ID; zero later `zn`
      calls only proved its event-time hook missed persistent creation.
      **Hmissing-pair is STATIC REJECTED; do not force 47C0A266, 283966ED, a
      clip weight or another late pose transfer.**
- [x] Close the exact Mazur distal-hands overwrite before snapshot creation.
      Campaign-Chloe player-mesh selector `3F334972` owns exactly one type-1
      row `56C57AD2 -> E117618A`; vill-Nadine owns none. The source is Player
      eight-group joints `[29..32]`, the target is Chloe seven-group joints
      `[38..41]`, while paired `283966ED` correctly owns Player `[21..28]`.
      `AnimStateSnapshot_TransformState 0x1418B2A40` materializes this row via
      `AnimOverlayState_FindMatchingRow 0x1417FA6E0` before the ordinary
      resolver. Build `ack` skips only this exact row by advancing the stock
      32-byte cursor, scoped to the unique retained local-Nadine track/master,
      current Mazur and controller state 2/4. Shared rows, clip IDs, command
      weights and IK remain untouched. Three evidence types and the exact
      falsifiable prediction are recorded in `COOP_STATUS.md`.
- [x] Run `ack`: both detours and retained owner0 copy markers were live, but
      zero exact skip markers appeared and the visual pose was unchanged.
      **REJECT only the admission/timing.** Static call order proves
      `RequestCustomWeaponPart` materializes the pair during controller Draw=1,
      before `ApplyState` publishes WeaponOut=2/current slot. Build `acl`
      admits state 1 by exact Mazur inventory ownership and additionally
      requires live Player `+1CC/+1D0/+1D4 = 56C57AD2/283966ED/0.7`; states
      2/4 retain strict current-slot equality. No broad state or art relaxation.
- [x] Run `acl`: fresh PIDs `2180/18004` again installed both hooks and emitted
      fourteen owner0 overlay-copy rows, but zero row-skip markers; the pose was
      unchanged. **REJECT `ack/acl` completely.** The custom-pair fields are
      transition/request storage and do not close the steady owner. Remove the
      mutation and its `0x1417FA6E0` offsets rather than widening it.
- [x] Close the common steady Mazur bundle and implement `acm`. Historical live
      WeaponOut rows on the exact retained master are the `run 25A767CF` table
      from `player-anim-settings-chloe.bin`: base `41C84248->5F913F74`, hands
      `77A8107C/7FB80CC8->7D11D4FD`, randomness/breathing to `C7FB376F`, and
      run transitions to `1CEC7F3A/01321972`, while Mazur Player-arms
      `283966ED` survive unchanged. The shipped generic `player-anim-settings.bin`
      `run` object supplies the exact Player counterpart: `2AD31AAC`,
      `19CFD6FF`, or the unchanged Player source. PAK masks independently prove
      Player eight-group versus Chloe seven-group lanes. `acm` replaces only
      those exact chained resolver outputs for a physically drawn/rightHand
      Mazur on the actual legacy-Nadine owner in each process; P99, holster,
      ADS/transitions, real Chloe and shared DC rows remain stock. Full evidence
      and prediction are in `COOP_STATUS.md`; release build succeeds.
- [x] Run the earned `acm` exam. Fresh PIDs `18100/7176` emitted the exact
      idle-base replacement on both owners (`41C84248 -> 2AD31AAC`, unique
      row/rightHand/state2), but the grip was visually unchanged. The same
      segment emitted zero idle-hands/run-tense-hands replacements. **RUNTIME
      REJECT `acm`:** a per-leaf resolver mutation is non-atomic because the
      already-live Hands snapshot can remain cached while base/run/breathing
      leaves rebuild. Delete the repair and do not interpret this as a measured
      complete Player-bundle replacement.
- [x] Resume statically at the first atomic command owner of Player joints
      `21..32` after `AnimStateSnapshot_SnapshotAnimState 0x1418B24F0`.
      Close the command-6/7 merge and same-frame weights for
      `custom-weapon-part`, `custom-weapon-part-with-feather`, the adjacent
      Hands lane and the terminal layer merge. Do not retry the
      rejected post-`+0x408` local-row transfer, final-palette transfer,
      Feather suppression or ProcessWeapon local correction. Any next runtime
      proposal must first distinguish an authored target/input error from a
      later pose-command ownership error with three independent evidence types.
- [x] Reject final command-6 pointer substitution before runtime. P99 terminal
      `B8FC6B25` has eighteen authored predecessors and command 6 has already
      discarded the source/layer identity, so it cannot prove D350 provenance.
      Mazur's exact `56C57AD2 + 283966ED` mode-1/0.7 pair is uniquely owned by
      transition state `11EE475B`; stable WeaponOut samples instead have empty
      `s_no-weapon 3889CC0E`, so the proposed 350 ms steady repair statically
      predicts zero eligible pairs. Top-level evaluator command 24 is an opaque
      joint-plugin writer, not a harmless packed-quaternion opcode. Remove the
      speculative implementation and do not launch it.
- [ ] Trace the persistent steady grip owner instead of another clip swap.
      Close the exact construction and consumers of the left/right targets in
      `PlayerWeaponIKAction_Update -> WeaponIKFeather_Apply 0x1418F2700`, plus
      the writer/clear lifecycle of Player `+0x1CC/+0x1D0/+0x1D4`, for both P99
      and Mazur. Compare the local and retained Nadine semantic inputs before
      the solver and final command DAG. Do not revive blanket Feather
      suppression: the next mutation requires a concrete differing field or
      locator and three independent evidence classes.
- [ ] Do not promote `abt` to an always-on general repair until independent
      melee/flinch/interact overlays are positively excluded. Weapon state 2 and
      grenade state 0 close their own action lifetimes but do not prove that
      every unrelated Player partial action is idle.
- [ ] Keep the sling separate. Manual `ShowMesh` is REJECTED because Nadine has
      no authored `strap2..14` guide chain; exposing it produces a floating,
      non-simulated strap and is not part of the arm-pose test.

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
      **PARKED AFTER AXIS CORRECTION:** F3 was selected too early. The admitted
      `camera_quat` is route-pose positive-Z, while `zf` expanded negative Z and
      systematically clamped gesture yaw at `+/-89`. Resume this merge audit
      only if corrected build `zg` produces reasonable sender angles without a
      visible result.
- [x] Run build `zg`: stationary non-ADS sender sky -> ground -> sky while the
      observer camera remains elsewhere. Require `consumed=1`, removal of the
      systematic backward `+/-89` yaw saturation, sender-correct visible head
      direction and no observer-camera repetition, deformation, crash or lag.
      **CONFIRMED:** the user reports correct synchronization. Both PIDs retained
      `consumed=1`; ordinary non-saturated horizontal angles returned. Positive
      Z is the final route-camera convention for cached fixed LookAt.
- [ ] After expansion provenance is confirmed, choose between explicit
      sender-tail transport and a deterministic intended prior-list carry. Trace
      target children Chloe local `43` (global 248) and Nadine local `97/103`
      (global 239/245); Nadine local 27 needs no input trace because `jw` proved
      its 48-byte record bit-identical across the camera sweep.

## P0 CURRENT - Nadine legacy-look weapon attachment (reopened 2026-08-09)

- [x] Correct the two reversed built-in firearm identities before another
      runtime test. Installed `sidbase.bin` and `weapon-art.bin` resolve
      `0x8075397D = pistol-silenced-dlc` and
      `0x33E29A1B = sniper-rifle-bolt`; the exact runtime child of the former is
      independently `0x255BEBA0 = p99-silencer-dlc`. Historical
      `WeaponArtDef+0x10` values and the native NPC holster mapper agree:
      `0x8075397D` is type 0/handgun and `0x33E29A1B` is type 1/long gun. The
      source constants are corrected; all older Nadine entries that call
      `0x8075397D` the rifle or `0x33E29A1B` the pistol retain their raw evidence
      but their semantic labels are superseded.
- [x] Implement build `2026-08-09aaj`: retarget the already proven cold stock
      rebind from the misidentified P99
      to the actual `0x33E29A1B` Mazur LDR, selected by art rather than hard-coded
      slot. Retarget the safe Put selector from the actual rifle to the actual
      `0x8075397D` pistol and redirect its generic `gun_holster` request to the
      copied native-Nadine `pistolHolster`. Remove the obsolete BB alias. Build
      succeeds; both paths have exact one-shot ownership and inverse draw guards.
- [x] Run the single written `aaj` visual test. Before input, the local Nadine
      Mazur must leave `rightHand` for `targSpineProp`; after one P99 draw/Put,
      only the P99 must move from `gun_holster` to `pistolHolster`. Do not test
      the separate arm-retarget or sling issues in this pass. **RUNTIME
      CONFIRMED:** exact art `0x33E29A1B` emitted a one-shot verified
      `rightHand -> targSpineProp` rebind across four retained Player generations;
      the user saw the Mazur move to the back after the first few initialization
      seconds. One suppressed-P99 draw/Put visibly returned exact art
      `0x8075397D` to Nadine's thigh. Physical attachment geometry is now frozen.
      The same test confirmed the separate remaining defects: the Player
      animation carries the pistol through the torso and leaves the arms/hands
      intersecting the body.

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
- [x] Test the now-resolved preceding Draw boundary before the parked cold Put.
      `PlayerDrawWeaponAnimAction_CommitCurrentWeapon 0x14108A400` passes the
      weapon's exact 16-byte weak handle to PlayerWeaponController vtable
      `+0x248` (`PlayerWeaponController_CommitDrawnWeaponHandle 0x1410AAF20`).
      That slot finds slot 0, writes controller `+0x124`, executes the physical
      `EquipWeaponIndex 0x1410A9DD0`/`player-grabbed` path, then commits
      `+0x120`. A bounded build may call that verified slot once in the exact
      cold role-B state, require both indices become 0, and only then invoke the
      proven Put. Written Cdraw-commit/Cfull-draw/Cunsafe predictions are in
      `COOP_STATUS.md`; never toggle ProcessWeapon `+0x8F4` for this rifle.
      Result: Cfull-draw. The exact one-shot row showed
      `current=-1->0->0`, `requested=-1->0->0`, `draw=0->0->5`,
      `matched=0`, `commitVerified=1`, `putCall=1`; both copies remained
      responsive, but the local rifle stayed glued. The terminal Draw commit is
      REJECTED as sufficient and disabled in `zr`.
- [x] Resolve the complete Get/Draw action setup that precedes the rejected
      terminal commit. Start from `PlayerGetWeaponInHandAction_OnStart
      0x141096150`, its phase-2 monitor `0x141096730`, the
      `PlayerDrawWeaponAnimAction` object and the move-sequencer enqueue path.
      Trace which persistent controller/ProcessWeapon/animation state is written
      before vtable `+0x48` and later consumed by Put. Do not synthesize or enqueue
      an action until three independent static evidence types separate a full
      native action from a smaller exact missing writer. Resolved in `zs`: exact
      `ApplyWeaponSwitch 0x1410A28A0` enqueues Get plus channel-4
      `PlayerWeaponIKAction`, enters literal `kStateDraw=1`, and
      `PlayerWeaponController_ApplyState 0x1410AAFB0` waits for Get completion
      before enqueuing `PlayerPlayWeaponOverlayAction`, publishing
      WeaponPartial/NewIkWeapon through vtable `+0x300`, and entering literal
      `kStateWeaponOut=2`. Three evidence types and competitors are recorded in
      `COOP_STATUS.md`.
- [x] Run the bounded `zs` discriminator. On exact cold role-B local Nadine,
      invoke the complete stock Draw once, wait for stable `current=0, draw=2`,
      dwell 250 ms for overlay/IK publication, then invoke stock Put once.
      `action=draw -> weapon-out -> put -> complete` plus disappearance of the
      glued rifle confirms Ffull-cycle; the same complete rows with persistence
      reject the controller lifecycle and select Fexternal-context. RUNTIME
      REJECTED as sufficient: the exact four rows completed, ending at
      `current=-1 draw=0 index=40 attachId=0x555EFB8D`, but the local role-B
      Nadine view still showed the independently glued rifle. At the same time
      role-A Chloe saw that same Nadine rifle correctly on her back. This proves
      the remaining artifact belongs to a local-main-view presentation path,
      not weapon selection, authored Draw/Put, IK/overlay setup or attachment.
- [x] Test the omitted outer controller boundary in build `zw`. Outer Draw
      reached `current=0 draw=2`, but outer Put rejected the `slot=-1` sentinel
      (`call=0`, all controller fields unchanged) and the local rifle remained
      glued. Combined with `zs`, which already completed the inner `-1` Put,
      this rejects both controller entry levels as the missing operation. Build
      `zx` disables the cycle. Continue above `SwitchToWeapon` at the real local
      input/action producer; do not retry outer/inner Put or attachment ids.
- [x] Trace the local-main-view weapon presentation lifecycle statically. Start
      from `Player_UpdateMainViewVisibilityForAttachmentsAndWeapons 0x140C990A0`,
      `PlayerWeaponController_DisableWeaponsInMainView 0x1410A01F0`, and the
      adjacent controller vtable slot `+0x378 = 0x1410A08A0`. Resolve the writers
      of `Player+0x1618/+0x1619`, the exact `DisableMainView`/`EnableMainView`
      ProcessWeapon consumers, and the first state that differs between local
      role-B Nadine and role-A's correct observed Nadine. Do not launch until
      three independent evidence types separate this path from another owner.
      COMPLETE for `zu`: the false writer is exact `DontRenderThisFrame`; RTTI
      resolves `ProcessWeapon_EventHandler 0x1415116C0` and
      `ProcessWeaponBase_EventHandler 0x1414FBE10`, including strap forwarding.
- [x] Run the bounded `zu` discriminator at cold role-B Nadine spawn. Require one
      successful exact-rifle `DisableMainView` row. Disappearance only in the
      Nadine local view selects the inventory weapon's separate main-view
      representation; unchanged pixels select Player look/mesh or another actor.
      Do not draw the rifle unless testing the authored restore path. Release
      build is ready; IDB labels are saved and independently verified through
      the restarted MCP. RUNTIME REJECTED: role-B PID 4744 emitted the exact
      `published=1` row for slot-zero art `0x8075397D`, remained responsive, and
      the user saw no change. The pixels therefore do not belong to that
      inventory ProcessWeapon's main-view representation.
- [x] Classify the proposed separate local Nadine look prop. STATIC REJECTED:
      `ResolvedLook_ResolveProps2 0x141805C60` gives legacy `nadine-rural` only
      the `nadine-srilanka-hair-cloth` prop; body/shirt/holster assets contain no
      rifle; `Player+0x3518` is the exact `add-belt-attachment` script actor; hat
      slots were runtime-zero. Correct the overbroad `zu` owner inference:
      `DisableMainView` is rejected, but `fy` still proves all matching rifle
      geometry belongs to exact ProcessWeapon actors.
- [x] Reject build `zw`: the outer `PlayerWeaponController_SwitchToWeapon +0x40`
      successfully drew slot zero but synchronously rejected the negative Put
      sentinel. Together with `zs`'s completed inner Draw/Put, this closes both
      same-slot controller entry levels; do not retry them.
- [x] Run bounded build `zy`, the first test of the distinct state-2 cross-slot
      path `PlayerWeaponController_StartCrossSlotSwitch 0x1410A4130`. On exact
      cold local role-B Nadine, execute pistol-out -> rifle-out -> native rifle
      Put once. Require the ordered six markers, final `current=-1/draw=0`, no
      duplicate or crash, and ask whether the rifle finally leaves the local
      hand. Visible repair selects Xcross; exact completion with unchanged pixels
      selects Xsame and forbids another controller-lifecycle retry. RUNTIME
      REJECTED: two complete exact sequences ended at attachment index 40 and
      `current=-1/draw=0`, but the rifle remained glued in the local Nadine view.
      Build `zz` disables the cycle.
- [x] Identify the distinct local-screen render submission for the glued rifle.
      `RenderMgr_MainDrawCore` schedules exact per-`SceneWindow`
      `MeshCull_CullFgSceneJob 0x141A32700` after common `CullFgLods`; its
      `MeshCull_CullFgViewJob 0x141A2DDD0` emits packed 9-byte final submesh rows
      with the unchanged source `FgScene` index at `+7`, and
      `MeshCull_FinalizeCullFgScene 0x141A3B1F0` forwards the same 61 lists to
      rendering. Three static evidence types and all competing owner outcomes
      are recorded in `COOP_STATUS.md`.
- [x] Run bounded read-only build `aab` at untouched cold role-B Nadine spawn.
      Do not draw or switch weapons. Keep the glued rifle visible for at least
      five seconds and require one target plus three per-view samples with
      `truncated=0`. Classify the selected `FgScene` row as Vstale, Vcurrent,
      Vother or Vkey using exact target/geometry masks, draw-entry count and
      retained ProcessWeapon owner. Any lag/crash or scan truncation rejects
      only the packaging; do not mutate cull flags in this test. Build `aaa`
      installed nothing because it used the post-call address `0x141661BE9`;
      loaded bytes prove corrected E8 start `0x141661BE4` reaches
      `MeshCull_WaitForCullFgScene 0x141A35BE0`. RUNTIME REJECTED: the rifle
      remained glued, while the supposedly three-sample probe repeated on
      multiple render threads/windows because its limiter was `thread_local`.
      The matching target and same-geometry candidates all reported zero final
      rows in the sampled contexts, but this cannot classify Vkey until the
      primary local `SceneWindow` is selected statically. Build `aac` disables
      the relay. Next: resolve the MainDrawCore caller/context topology and a
      static primary-window/pass discriminator before another launch.
- [x] Run corrected bounded read-only build `aad` at untouched cold role-B
      Nadine spawn. Static caller tracing now proves `RenderMgr_MainDraw
      0x141665B40` is reached by both top-level render modes, calls
      `RenderMgr_MainDrawCore` exactly once, and the core passes the same inline
      primary `SceneWindow` at frame `+0x10` into `SpawnCullFgScene` before
      assembling GBuffer, Forward and post passes. `aad` replaces the rejected
      thread-local limiter with one process-global SRW-locked three-sample gate
      and logs the exact SceneWindow/config pointers. Require exactly samples
      1/2/3, one window pointer, 500 ms spacing and `truncated=0`; any extra row
      rejects packaging. Any target/geometry candidate with `drawEntries>0`
      keeps the primary foreground path open; all six candidates at zero in all
      three accepted samples excludes the current rifle geometry key from the
      local main foreground lists and moves the search to a local-player overlay
      or a different render key. CONFIRMED: role B emitted exactly three samples,
      one nonzero SceneWindow/config pair and no fourth sample. REJECTED
      INTERPRETATION: the flat decoder used 9-byte stride on an outer table of
      32-byte DrawablePrototype records. Thus the three zero `drawEntries`
      results do not classify Fexternal and do not close the main foreground
      path. The corrected nested layout and evidence are recorded in
      `COOP_STATUS.md`.
- [x] Run corrected bounded read-only build `aaf` at untouched cold role-B
      Nadine spawn. `SceneRendering_Render 0x14186C8A0` and
      `SceneRendering_EmitDrawablePrototypeSubmeshes 0x141AB5780` prove that
      each outer PageTable row is 32 bytes, with `FgInstance**` at `+8`, an
      inner 9-byte row array at `+0x10`, count at `+0x1C & 0xFFF`, and the scene
      index at inner row `+7`. Require exactly three samples, one window/config,
      nonzero `drawables` and `submeshes`, `truncated=0`, and no fourth sample.
      A nonzero per-candidate `selectedSubmeshes` selects the matching
      Fcurrent/Fsame/Fother branch; all six at zero in three valid samples while
      the rifle remains visible selects Fexternal. Crash, stall, truncation,
      zero inner rows or extra samples rejects only the probe packaging.
      CONFIRMED Fcurrent-selection: stable generation 7 produced exactly three
      complete scans (507, 412 and 374 inner rows; no fourth sample). Exact
      current pool instances 1056/LOD0 and 1058/LOD2 each had three selected
      submeshes in every sample while the untouched rifle remained glued to
      Nadine's hand.
      This proves selection, not exclusive pixel ownership: same-geometry twins
      were also selected in two samples. Build `aag` disables the census.
- [x] Correct the false `FgInstance+0x1A0` writer frontier. Static audit proves
      `MeshCull_CullFgViewJob 0x141A2DDD0` never reads that byte: it filters on
      `FgInstance+0x18` and cull metadata behind `+0x1D0`. The `go`/`aaf`
      difference therefore does not imply a later stock writer.
- [x] Discriminate the exact current rifle's per-view row at the proven
      pre-cull boundary. `MeshCull_BuildFgViewDescriptors 0x141A30330` plus
      `MeshCull_SelectFgViewDrawLists 0x141A30B80` map LOD0/list0 to view type 3
      and LOD2/list3 to type 1. `MeshCull_CullFgViewJob` rejects type 3 by
      `FgInstance+0x18 bit 0x40`; exact source consumer
      `RenderSunlightShadow 0x141657DA0` proves type 1/list3 is the shadow path.
      Compare Dcurrent (exact LOD0 owns pixels), Dtwin (same-geometry triplet or
      another selected row owns pixels), and Dunsafe. Do not mutate `+0x1A0` or
      retry common-cull/attachment/controller paths.
      IMPLEMENTED build `aah`: at the exact pre-`CullFgLods_Spawn` main-draw
      callsite, require the published current LOD0 pointer to occur in the live
      queue, then set only `FgInstance+0x18 bit 0x40` while local role-B legacy
      Nadine has exact slot-zero rifle `current<0, draw=0`; clear it when drawn.
      Heavy `gw` capture stays disabled. Written outcomes are Dcurrent
      (glued image disappears, drawn rifle restores), Dtwin (exact flag changes
      but image remains), and Dunsafe (crash/collateral/missing restore).
      REJECTED by `aah`: PID 2552 produced four exact `queued=1` rows and changed
      LOD0 flags `0x21228A38 -> 0x21228A78`; the user saw no visual change and
      neither copy crashed. Dcurrent is false. The originally written Dtwin
      branch does not identify an owner: `gw` had already removed the complete
      pool-443/444/445 triplet with no visual effect and later bound it to the
      live remote-Chloe rifle generation. Build `aai` disables the probe. Do not
      request another runtime ownership discriminator.
- [ ] STATIC ONLY: identify the local-only presentation/cache boundary for the
      glued rifle. `RenderGBuffer 0x141653680` and `RenderForwardPass
      0x14165BDC0` both consume foreground output list 0 / view type 3 through
      `SceneRendering_Render`, so the `aaf` current LOD0 row is a real colour
      submission and the earlier depth-only premise is rejected. `aah` proved
      only a transient write before asynchronous `CullFgLods_Spawn`; it did not
      prove that `MeshCull_CullFgViewJob` later consumed bit `0x40`. Trace that
      mutable flag lifetime and the ProcessWeapon local presentation path that
      can survive the current triplet being absent in `gr`. Pool 443,
      attachment APIs and weapon-state cycles remain rejected. Do not launch
      until the exact colour-owner dataflow and repair are statically closed.
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
- [x] Test build `zh`, which supersedes the per-row Hlookup branch with the
      engine's authored knowledge boundary. Static DC00 comparison proves the
      four compatible female/Nadine looks share one exact 323-row table; its 153
      overlaps with Chloe are precisely 144 holster/unholster mappings plus nine
      entries in the table's leading weapon/grip block. The runtime commits
      shipped record `0x6567DEA7`
      into only the retained legacy-Nadine `player-mesh` slot and combines it
      with the already proven slot-1 final selector redirect to native
      `pistolHolster`. Written positive/negative predictions are in
      `COOP_STATUS.md`. Test pistol Put and active pistol/rifle grip first. Cold
      spawn rifle and ProcessWeaponStrap are explicitly out of scope and should
      remain unchanged in `zh`. RUNTIME REJECTED as sufficient: both processes
      committed record `0x6567DEA7` into slot 87 (`status=1`, then stable
      `status=2`) and installed both exact hip hooks, but the user saw no change
      in pistol motion, free arm, cold rifle or strap.
- [x] Test build `zi`: repair only the exact missing legacy-Nadine
      `PlayerWeaponAnimControl+0x78` marker from `-1` to the descriptor-proven
      `r_leg_holster` joint 99, requiring vtable `0x142A46778`, exact retained
      owner and parent joint 5. The stock `0x14108EBF0` refresh then builds the
      `+0x90` basis consumed only by the leg branch of `0x14108ECE0`. The bounded
      final reducer probe distinguishes valid leg, valid back and invalid/zero
      output. Use one standard-pistol Put; classify Hmarker-full/Hreference/
      Hsolver from the written prediction in `COOP_STATUS.md`. If output remains
      invalid, remove both the now-inert marker test and the rejected full-table
      mutation before tracing the exact reference-set producer. **RUNTIME
      REJECTED:** both PIDs applied `-1/5 -> 99/5`, stock produced a nonidentity
      leg basis, but the reducer stayed exactly `reference=0/weight=0/valid=0`.
      The user saw no holster improvement and a slight new arm distortion. Build
      `zj` removes the marker, full player-mesh and terminal attachment writes.
- [x] Resolve the exact snapshot reference producer before another mutation.
      Compare stock Chloe and legacy Nadine at the same standard-pistol Put.
      Distinguish **Hnormal-invalid** (both final masks zero), **Hclip-missing**
      (Nadine leaf clip lacks hand/leg ids), **Hblend-suppression** (leaf succeeds
      but a Blend node drops the result), and **Hwrong-snapshot** (the reducer
      samples a different state/leaf). Static quota must include the concrete
      snapshot-node vtable slot, clip `+0x30` named-reference table dataflow and
      the active root/child topology. The eventual runtime test is read-only and
      must contain one Chloe plus one Nadine Put, not another guessed repair.
      **STATIC / IMPLEMENTED (`zk`):** exact RTTI slot identities, evaluator
      disassembly and clip `+0x30` id-array dataflow satisfy the quota. A scoped
      read-only evaluator hook logs the unmodified returned mask, root
      Blend/Unary topology and every reachable animation leaf's resolved id and
      local `hand/leg/back` presence mask. Run exactly one standard-pistol Put on
      Chloe and one on Nadine; classify the four written outcomes in
      `COOP_STATUS.md`. No visual change is expected. **RUNTIME CONFIRMED:**
      Hnormal-invalid won: both exact looks returned mask zero, every readable
      live leaf had one unrelated reference and local presence mask zero, and
      Chloe still performed the correct stock motion. The named-reference
      reducer is optional here; Hclip-missing, Hblend-suppression and
      Hwrong-snapshot are rejected as Nadine-specific causes. The bounded hook
      is removed.
- [~] Resolve the real `WeaponPartial` arm/weapon content before another test.
      Decode joint ValidBits for the eight exact live `ak47-*--part/--add`
      clips, their authored `--strap` siblings, and the matching
      `p-holster-leg/back` player-mesh clips. Trace which AnimStateLayer emits
      each command and which later merge first owns the arm/spine rows.
      Competing hypotheses: **Wpart-overwrite** (leading: WeaponPartial
      `--part` overwrites the base arm motion), **Wbase-only** (the selected
      player-mesh base itself lacks required arm rows), **Wstrap-layer** (the
      missing sling is a separate uninstantiated layer), and **Wstance** (the
      observed cover-low-l/generic split is only stance state). Require three
      evidence kinds and a prediction that separates them before runtime.
      **STATIC:** the new paged-PAK mask audit proves all live WeaponPartial
      parts own joints 21..35, including right shoulder plus both elbows; adds
      own 16..20, 40..42 and 58..62. Leg/back base pairs cover the same union but
      put joints 33..35 in the add, so Wbase-only is rejected as sufficient and
      the later WeaponPartial can re-own the broken arm. Strap siblings instead
      have one group with local joints 0..15 and travel through the separate
      ProcessWeapon `play-animation` route, confirming Wstrap-layer. The direct
      producer is PlayerPlayWeaponOverlayAction -> controller vtable +0x3C0 ->
      PlayerWeaponAnimControl vtable +0x28 / 0x14108B940 ->
      custom-weapon-part. Leading next discriminator is the still-active
      Chloe-base pre-map at 0x14109D450: Chloe contains direct ak47/grip-opener
      rows that vill-nadine-08 lacks. Capture its input/output and the final
      selected record on one controlled idle pistol cycle before bypassing it.
      **STATIC / IMPLEMENTED (`zm`):** a reloc-free outer selector detour plus
      the exact inner E8 map callsite relay logs unchanged input, mapped id and
      final record for retained Chloe/Nadine only. Runtime classification is
      Warm-map-live versus Warm-fallback versus Warm-miss; no visual change is
      expected. **RUNTIME REJECTED:** the retained legacy-Nadine path produced
      256 rows across both views for three inputs; every exact Chloe-map call
      returned zero and the final `+0x3C0` record was null. The Chloe map is not
      the selected content owner, so its bypass is forbidden and the `zm` hooks
      are removed. The exact Chloe control did not enter the bounded sample,
      which does not weaken the Nadine-specific rejection.
      **STATIC / IMPLEMENTED (`zn`):** vtable/RTTI identity, the direct OnStart
      callsite dataflow and the callee's `+0x18`/WeaponPartial writes prove that
      PlayerWeaponAnimControl vtable `+0x28` receives the final caller-fallback
      id. A bounded read-only vtable relay now logs that id and forwards all
      seven arguments unchanged for exact retained owners. Run one pistol and
      one rifle draw/put cycle on Nadine; classify Wfinal-live, Wfinal-clear,
      Wfinal-same or Wfinal-miss. No visible change is expected. **RUNTIME
      REJECTED:** both copies installed the relay and completed the cycles with
      zero admitted `+0x28` calls; Wfinal-miss proves this special custom-part
      branch is not the live draw/put path. **STATIC / IMPLEMENTED (`zo`):**
      the sibling vtable `+0x08` target `0x14108C0C0` is the explicit normal
      OnStart branch, stores the final `R8D` id at control `+0x18`, and starts or
      fades exact WeaponPartial `0xE2B990B9`. A bounded four-argument relay
      records that unchanged request. Run the same single pistol/rifle cycle and
      classify Wnormal-live, Wnormal-clear, Wnormal-same or Wnormal-miss.
      **RUNTIME CONFIRMED:** Wnormal-live won in both processes and views. Exact
      legacy Nadine reached `s_weapon-out 0x7405AACB`, a deliberate zero clear,
      and `s_weapon-holster 0xDF5B68FC`; every nonzero request became control
      `+0x18`. The normal state producer/cache is correct and its probe is now
      disabled. Continue statically at the Nadine animation-overlay set and the
      later WeaponPartial/ProcessWeapon content owners; do not repeat this test.
      **STATIC:** native Nadine's correct hip-holster chain is now exact in
      `anim-overlays-female-buddy.bin`, but it is consumed only by
      `GameCharacter_BuildNpcAnimTreeWithOverlays 0x141DE36E0`, shared vtable
      slot `+0x710` on NPC-derived classes and absent from Player. Its authored
      clips have seven groups and NPC masks, versus eight groups and incompatible
      masks in the live Player WeaponPartial clips. **Nnpc-direct is REJECTED**:
      do not inject the NPC set or clips into the Chloe-born Player. Split the
      remaining work: first trace **Nprop-lifecycle** for the cold-spawn rifle
      and pistol hip attachment; separately retain **Nretarget-player** versus
      the much larger **Nvisual-puppet** for the arm/torso pose. Sling remains
      the independent `ProcessWeaponStrap` route.
      **STATIC / IMPLEMENTED (`aak`, no runtime yet):** `aaj` redirected the
      P99 selector inside `PlayerHolsterWeaponAnimAction_Execute 0x14108A640`,
      making its mandatory `targSpineProp` check fail at `0x14108A798` and
      skipping the second rebind, local-transform/blend handoff and
      `weapon+0x8F4=1`. The repair now lets the stock executor complete and
      performs the already proven P99-hip selector only in a separate concrete
      `PlayerWeaponController_RebindWeaponToHolster 0x1410A9670` call after the
      exact P99 moved from `rightHand` and the completion byte was set. This
      removes a mod-created stale-state source but does not claim to solve the
      older arm trajectory. Do not run this packaging change by itself; finish
      the final WeaponPartial owner/retarget proof and test both together once.
      **STATIC / IMPLEMENTED (`aal`):** the final type-2 layer cmd7 and its
      case-7 evaluator prove that exact WeaponPartial overwrites jointly valid
      base shoulder/elbow rows with mode 1, weight 1. Four complete four-leaf
      Player families now resolve through stock `AnimSetRemap` to compatible
      `p-holster-leg` donors only while the current retained legacy-Nadine
      `WeaponPartial 0xE2B990B9` builds commands. Put joint unions are identical;
      Get releases only `l_palm` to the base lane. Exact owner/control/registry
      guards, fresh resolver output, recursion protection and a compile-time
      unique allowlist exclude Chloe/NPC/strap/other WeaponPartial states.
      Release build passes. Run the one combined P99 draw/Put prediction recorded
      in `COOP_STATUS.md`; do not split it into more diagnostic launches.
      **RUNTIME REJECTED (`aal`):** both processes installed the combined
      `aak+aal` package and reached co-op network startup, then crashed during
      checkpoint load before any replacement row completed. The AnimStateLayer
      wrapper's five-argument ABI is independently correct, but this run cannot
      distinguish the global command-scope detour, a failure inside the scoped
      remap resolver, or the separately untested `aak` post-handoff executing on
      cold state. Build `aam` disables both new paths and restores exact confirmed
      `aaj` behavior without requesting another test. Before any future launch,
      isolate one failing boundary statically and capture provenance before the
      original resolver/post-rebind can fault.
      **STATIC REJECTED (postmortem):** `AnimStateLayer_CreateAnimCommands` calls
      only `AnimStateSnapshot_GenerateAnimCommands`, which consumes an already
      resolved root and never reaches `AnimSetRemap`. The remap actually occurs
      earlier at `AnimStateSnapshot_SnapshotAnimState+0x464` / `0x1418B2954` and
      is stored at snapshot `+0x88/+0x90/+0x94`. Therefore the `aal` TLS scope
      could never admit its 16 replacements; do not retarget the same wrong phase.
      All `aal` and untested `aak` code is removed, leaving scanner behavior
      identical to confirmed `aaj` except the `aam` marker. Future work must scope
      exact legacy-Nadine WeaponPartial ownership during snapshot creation without
      reintroducing a global command-generator detour.
      **STATIC:** the correct boundary is now exact. Type-2 phase update
      `0x14189D720` passes the live `track+0x8` overlay through
      `0x1418A4670` into the direct SnapshotAnimState call `0x1418A46D0`.
      Snapshot tree construction reaches animation-node vtable slot 9 at
      `0x1418B2FAB`, and exact `AnimSnapshotNodeAnimation` calls the leaf remap
      at `0x14191801C`. The leaf receives the identical AnimSet pointer later
      used by the runtime-proven `gk` top-level remap. Fresh
      `AnimControl_FindActiveType2LayerById 0x14172B3E0` plus live-track
      comparison eliminates the rejected cache. All 32 source/donor clips are
      present as eight-group registrations in the same movement PAK. Implement
      only an atomic pair of exact E8 relays, TLS-bound to the same AnimSet;
      never restore the global resolver or command-generator detours.
      **STATIC / IMPLEMENTED (`aan`):** the two exact E8 relays are now atomic
      and fail-closed. Snapshot admission revalidates the current Nadine Player,
      control, stock type-2 lookup and live track; leaf admission also requires
      the identical AnimSet. A donor is copied only when stock source/donor ids
      remain exact and their two nonzero lookup keys match; otherwise output is
      stock. Release build passes. The single written test in `COOP_STATUS.md`
      separates scope miss, safe fallback and a later owner. Do not combine this
      run with the deleted/unisolated `aak` post-rebind branch.
      **RUNTIME REJECTED (`aan`) / IMPLEMENTED (`aao`):** both copies stayed
      responsive but the exact leaf emitted no rows. Live read-only state proves
      every Nadine/control/registry/layer gate passes and the sole live
      WeaponPartial track has stock-null `track+0x8`; `aan` incorrectly rejected
      null before ownership comparison. Phase update forwards null unchanged,
      SnapshotAnimState explicitly accepts optional overlay, and leaf slot 9
      gets AnimSet independently from `v77[2]`. `aao` removes only that redundant
      null guard while retaining exact track equality and all other admission and
      fail-closed resolver checks. Run only the revised single P99 draw/Put
      prediction recorded in `COOP_STATUS.md`.
      **RUNTIME REJECTED (`aao`):** both copies stayed responsive and the pose
      remained unchanged, but the revised pair still emitted zero replacement
      rows. Historical exact-state `HOLSTER-REF-LEAVES` captures already prove
      the Put source ids are live, so the miss is snapshot lifetime/order rather
      than clip inventory: retained ownership appears after the one-time leaf
      initializer and the cached tree is reused. Build `aap` disables the pair.
      Next, operate only after `AnimStateLayer_PhaseUpdateInstances 0x14189D720`
      on the exact retained Nadine WeaponPartial instance, validate its
      `SnapshotNodeHeap* +0x130` and root `+0x138`, and update matching existing
      animation leaves before command generation. Live memory also proves the
      visibly hand-glued P99 is already logically on `pistolHolster`; restore
      the stock holster executor tail before the hip rebind instead of changing
      attachment ids or returning to renderer/cull probes.
      **STATIC / IMPLEMENTED (`aaq`):** the sole type-2 phase callsite
      `0x14172CB32` now calls stock first and then scans only the freshly
      revalidated retained-Nadine `WeaponPartial` instances. Exact state ids
      split Put from Get families; heap `+0xA0/+0xAC`, instance
      `+0x130/+0x138/+0x140` and animation-leaf vtable/fields are all
      independently proven. A donor is committed only when source/donor resolve
      through the same AnimSet and keys match; stock `0x141918070` must resolve
      the new clip or every changed field is rolled back. Release build passes,
      and IDB label `AnimControl_PhaseUpdateType2Layers 0x14172CAB0` is saved.
      Do not launch `aaq` alone: first remove the known `aaj` early exit by
      allowing the stock P99 holster executor to complete before the scoped hip
      rebind, then use one combined falsifiable visual test.
      **STATIC / IMPLEMENTED (`aar`):** the combined package now preserves the
      entire stock P99 executor and admits the hip selector only in a separate
      concrete rebind after exact action identity, same P99 generation,
      `rightHand -> targSpineProp`, `executed=0->nonzero` and nonzero
      `weapon+0x8F4` all verify. The result must remain the same owner/P99 and
      exact `pistolHolster`; every mismatch fails closed to the completed stock
      back endpoint. This removes the mod-created stale-handoff source and runs
      beside the `aaq` post-phase live-snapshot leg retarget. Release build
      passes. Perform only the single combined cold-spawn plus one P99 draw/Put
      prediction recorded in `COOP_STATUS.md`; do not split it into more tests.
      **RUNTIME REJECTED (`aar`):** both copies remained stable, but every exact
      cold P99 row was `rightHand -> gun_holster`, `stock=0`, `call=0`; the user
      still saw the pre-existing hand-glued P99. The strict guard confused the
      executor's mandatory intermediate spine id with its final result. Static
      control flow now proves the first rebind must be bridged
      `gun_holster -> targSpineProp` to pass `0x14108A798`, after which the
      direct Anim record, second rebind and handoff tail can run. Implement a
      two-phase exact-P99 selector: spine bridge only during stock executor,
      require both scoped controller rebinds, then hip mapping only in a
      separate post-executor concrete rebind. Do not rerun the unchanged guard.
      **STATIC / IMPLEMENTED (`aas`):** exact-P99 selector phases now match the
      executor's real protocol. Stock phase maps two generic controller rebinds
      to the mandatory spine intermediate while leaving the direct Anim request
      untouched; only after exactly two hits, same action/object/generation,
      final spine and completed handoff does a one-hit post phase map generic to
      the thigh. Every mismatch is stock/fail-closed. Release build passes.
      Test only cold empty-hand spawn first and require the exact counter/id
      prediction in `COOP_STATUS.md` before any manual draw/Put test.
      **RUNTIME CONFIRMED (`aas`, real P99 only):** both views repeatedly matched
      `rightHand -> targSpineProp -> pistolHolster`, `bridge=2`,
      `hipRedirect=1`, `stock/call/verified=1`; Mazur-back remained verified.
      The remaining empty-hand object is visibly smaller than a drawn P99 and
      resembles the outfit's holstered pistol mesh. Stop changing the current
      ProcessWeapon attachment. Statically identify whether those pixels belong
      to a separate appearance/mesh-overlay prop, an alternate ProcessWeapon
      child, or a stale local-view presentation before another launch.
      **STATIC / IMPLEMENTED (`aat`):** the appearance alternative is now
      rejected: exact `nadine-rural` contains only head/shirt/body plus two
      leather holster meshes, no pistol leaf, and neither holster has a single
      vertex weighted to the hand. The native Nadine Glock is NPC/cinematic art
      with no cold Player producer; historical rows contain zero Player owners.
      Exact `aas` rebind selects the hip and copies correction state but never
      runs ProcessWeapon vtable `+0x7B8`, while phase `0x40` republishes only the
      cached current local transform. `aat` therefore calls that exact stock
      refresh once after the verified P99 hip handoff, only when phase `0x10` is
      absent and phase `0x40` is pending, under balanced ProcessWeapon TLS and
      strict vtable/owner/generation/record/blend/transform/FgAnimData guards.
      Release build passes. Run only the written cold-spawn prediction in
      `COOP_STATUS.md`; require `refresh=ok/1/1 tls=1/1` and nonzero local
      `changed` before doing any manual draw/Put cycle.
      **RUNTIME REJECTED (`aat`) / DISABLED (`aau`):** both copies stayed
      responsive and every first exact P99 refresh passed all guards, restored
      TLS and changed all eight `+0x130/+0x140` dwords (`changed=0xFF`) without
      attachment/owner drift. The small object nevertheless remained at local
      Nadine's hand. The synchronous current-local repair is not the pixel
      owner and is disabled. Statically split a missing late Fg publication
      from a second drawable; start with the two simultaneously resolved
      `nadine-holster[-game]` meshes, their differing Look2 view flags and the
      regular mesh's deform joints 143--165. Do not launch another diagnostic
      until that owner is exact.
      **STATIC (`aau` continuation):** both holster leaves are now excluded.
      Normal gameplay's exact DrawControl selector exposes only
      `nadine-holster-game`; that mesh is weighted to body/thigh and
      `r_leg_holster`, while the hidden cinematic leaf uses only belt joints
      143--165. Both have zero hand weights, StringId remap has no ordinal/hand
      fallback, and neither PAK contains firearm geometry. Do not force Fg phase
      `0x10`: it runs the broader Animating-layer/callback pipeline. Implement
      only the bounded read-only exact-P99 discriminator at the existing phase
      `0x40` callsite and immediately after stock
      `PrepareInstanceForRendering`; compare `weapon+0x130/+0x140`,
      `FgAnimData+0x1C0` and every driven P99 instance matrix. A matching chain
      selects another drawable; a missing/overwritten link selects its exact
      scheduler/publication boundary.
      **STATIC / IMPLEMENTED (`aav`):** reuse the already proven exact-P99
      `+0x7B8` treatment only once per weapon/record generation, then observe
      stock phase `0x40` after `0x141A8FC00` and stock worker Prepare after
      `0x14181B925`. The first row compares refreshed/current local, composed
      input and pre/post `Fg+0x1C0`; the second compares that Fg matrix with all
      driven instance matrices. Both relays call stock first, are bounded to
      four samples and leave the rejected heavy probes disabled. Release build
      passes. Run only untouched cold spawn: no manual draw/Put is needed. A
      complete matching chain with the artifact still visible closes
      `Hother-drawable`; the first missing/mismatching link identifies the exact
      publication repair boundary.
- [ ] Run one bounded `abe` appearance test. The donor-independent native
      `spineb` alias must be created before the hip record, restore Mazur
      `rightHand -> targSpineProp` and restore the confirmed P99
      `bridge=2/hipRedirect=1/verified=1` Put path. P99 fit is role-B/local-only,
      retains the four-centimetre lift and adds two centimetres body-back. Only
      after Mazur remains on the corrected back endpoint classify the authored
      ProcessWeaponStrap child: visible means it followed the parent; still
      absent selects its separate draw/pose consumer.

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

## P0 — Nadine local weapon presentation (post-`abi` static audit)

Preserve the accepted P99 thigh fit and corrected Mazur upper-back placement.
`abh` is REJECTED for grip: it accidentally targeted `pistol-colt C938AAA1`
reload rows, not P99 `8075397D`, and emitted no mutation. Its read-only sling
census is conclusive: the exact authored child/pose/frame are valid, but its only
mesh has primary gate zero and `0/3` active instances because legacy Nadine lacks
the dynamic `strap2/strap14` endpoints.

Build `abj` is PARTIAL: the remote owner-1 Mazur back frame is visually
CONFIRMED and must be preserved. The local grip and sling did not improve. Its
bounded evidence now identifies three independent causes: local cold rebinds
were peer-packet-gated; drawn P99 resolves `D350A452 p1h-fingers` through the
inactive Mazur overlay to `0A2F8976 lg1h-sniper-tkiv-right-hand`; and the sling
scope sampled Fg/instance state before the callback tail published it.

Build `abk` is RUNTIME PARTIAL / REJECTED. The native NadineBuddy-driven cold
service is CONFIRMED: P99/Mazur endpoint repairs completed before first peer
receive, and the user confirmed that the rifle no longer appears in the hand
after spawn. The P99 overlay mutation is REJECTED and disabled in `abl`: the
real drawn P99 already resolves `D350A452 -> D350A452`; the suspected
`D350A452 -> 0A2F8976` pair belongs only to the drawn Mazur. Because local
Nadine remains broken while Chloe sees the same remote Nadine grip correctly,
the current P0 is the local-only downstream HandFix/PlayerWeaponIKAction or
upper-body presentation path. Resolve the nine exact Mazur census pairs from
`COOP_STATUS.md`, then trace their command6/HandFix/IK ownership before any
mutation.

The sling static branch is now mutation-ready in `abl`. Exact code ordering
proves `0x141F19070` publishes Fg world first, while
`FgDrawManager_PrepareInstanceForRendering 0x14181A9B0` later copies that world
to `FgInstance+0x140..+0x17F` before checking visibility `+0x1A0`.
`IDrawControl_ShowMesh 0x141754A30` changes only the mesh gate/visibility, and
delayed `abh` rows prove the hidden instance eventually catches up. Therefore
only fail bits 13/15 are diagnostic; all identity/topology/Fg guards remain
hard, and no matrix/Prepare call is synthesized. Do not launch this alone yet:
finish the local-only grip/IK proof and combine both into one bounded test with
the written predictions in `COOP_STATUS.md`.

Build `abm` completes that grip proof and is the next single combined runtime
test. `WeaponIKFeather_Apply 0x1418F2700` is the sole downstream writer from
`PlayerWeaponIKAction_Update+0x8CC`; it commits both hand JointSets and then
feeds the later arm IK. Its exact zero-weight branch returns before any write,
and the caller ignores the result while completing normally. Suppress Apply
only for role-B retained local screen-0 legacy Nadine, exact action/feather
vtables, generation-valid same-owner P99 or Mazur, matching `Player+0x4308`
info, finite nonzero weight and controller states 1/2/4. State 1 must be covered
because its targets persist into WeaponOut. Do not mutate HandFix or another
resolver clip.

Runtime `abm` completed this discriminator. P99 improved only partially under
continuous WeaponIKFeather suppression, while Mazur remained a broken one-hand
idle pose. Treat Apply as a CONFIRMED contributor but the suppression as
REJECTED. The alleged later ordinary arm writer was corrected: `0x140C7E8D0`
is turret-only, and Apply is terminal in `PlayerWeaponIKAction_Update`. All 96
bounded rows were P99, so Mazur suppression was never exercised. Keep Mazur
stock and do not skip one hand: the paired JointSets/solver maps are consumed
together. The next admissible grip step is a read-only per-art comparison of
the fixed `WeaponIKFeatherInfo` inputs and pre/post left/right transforms, not
another scalar or blanket bypass.

The sling emitted exact `shown -> persistent` and became visible after the
expected dwell, but floated independently without rope/body physics and was not
present for remote Chloe. ShowMesh-only is REJECTED as a final repair. The next
sling task is no longer a visibility patch: all five native solver loops require
the full `strap2..strap14` owner-guide chain, and neither Nadine skeleton has
those thirteen hashes. No stock-compatible donor/mapping is currently proven.
Keep the sling override disabled for owner0 and owner1; only a reproducible
engine-authored retarget relation from captured Chloe guide locators can reopen
this branch. Both incomplete flags are disabled in `abn`; do not launch that
rollback-only build.

Static follow-up corrects one rejected premise: `dc1/skel-retargets.bin` has an
exact authored `npc-normal-base B91C4EA3 -> nadine-dlc-base C67A06F1` row with
74 mapped joints, including both arms, palms and hand-prop points. The exact
Mazur NPC selector also maps generic long-gun hands `21F2C55F` to sniper hands
`071B6E20`. This does **not** authorize a clip replacement: native firearm idle
is a base/add/aim-set/hands ensemble, one hands leaf cannot repair proximal arms,
and the full base owns joints 0--54. The Player source token `47C0A266` is also
inactive/unregistered in the current graph despite having complementary Mazur
hands/arms rows, so forcing it is REJECTED.

Next exact task: add only a bounded read-only join at
`AnimStateLayer_PhaseUpdateInstances+0x104 / 0x14189D824` (or its direct
`0x1418A46D0` SnapshotAnimState edge). For the nine documented local Mazur
resolver pairs record the owning `AnimStateInstance`, state id, root, AnimSet,
post-remap tuple, transition weight `+0x158` and phases `+0x16C/+0x168`. If the two-hand arms/hands leaves
are live and weighted, continue after command evaluation into the local IK
overwrite. If absent or zero-weight, locate the missing channel/source producer.
Do not inject NPC clips, force `47C0A266`, or relaunch the disabled `abn` build.

Build `abo` implements that join as a read-only E8 relay at `0x14189D824` after
stock snapshot resolution. It uniquely closes the instance back to the local
Player's AnimControl registry/type-2 layer/track and logs state, source/resolved
tuple, heap/root/AnimSet, transition weight `+0x158`, phases and track ordering.
The resolver source is read from exact instance `+0x11C`; `+0xB0` is only the
copied snapshot default. The track-local overlay may be null and is never
mistaken for the Player master; ownership comes from the unique match in that
Player's registry. Registry work samples one of every 16 relevant calls and is
capped at 48 scans per exact art/source; an explicit source-mask exhaustion
marker prevents silence from being misread as a missing layer. Post-join
deduplication includes full track/instance topology and reports its own bit-31
saturation, so a live
instance moving from newest to an older contributing track is logged again.
The command generator's exact rule is included: only the final instance of an
older track contributes `+0x158`; the newest track is generated at full weight.
Release build passes. Run only the written P99-then-Mazur idle discriminator;
there is no gameplay mutation in this package.

**RUNTIME INCONCLUSIVE (`abo`) / STATIC REJECTED (`abp`) / STATIC IMPLEMENTED
(`abq`):** exact `abo` marker and relay installed in PIDs `14348/440`; role-B
P99 and Mazur stock presentation paths were live, but the grip census emitted
neither a result nor its per-source budget marker. Therefore no allowlisted
source reached the registry scan and the run cannot select missing-layer. The
first `abp` reject-stage draft was not launched: it interpreted source before
proving local instance ownership and misreported intentional stride skips as
admission failures.

`abq` removes that ambiguity by using the exact outer type-2 layer callsite
`0x14172CB32`. A custom relay carries the caller's exact `RBX=AnimControl` and
`RSI=registry index`; stock runs first, then that O(1) identity and indexed
active record must close against local retained Nadine before any instance
source is read. Every local layer is sampled at 100 ms cadence, at most 32 times
independently per P99/Mazur art, with explicit exhaustion. Only that owned
layer's bounded tracks/live instances are enumerated. The census has no source
allowlist and emits unique `NADINE-GRIP-LAYER-SNAPSHOT` rows plus an explicit
512-slot saturation marker. The abandoned inner census was deleted.
All gameplay mutations remain off. Object-code review passes. Run P99 idle for
at least 3.5 s, then Mazur idle for at least 3.5 s: missing owned two-hand rows
selects the producer/channel branch; present weighted rows select the later
command/IK owner branch. Neither rows nor explicit exhaustion rejects packaging.

**RUNTIME INCONCLUSIVE (`abq`) / STATIC IMPLEMENTED (`abr`):** PIDs
`12952/4056` installed the exact outer relay and exercised the live P99/Mazur
stock paths, but emitted neither an owned snapshot nor either bounded-loss
marker. The `abq` identity gate was aimed at the request/build control
`PlayerWeaponController+0x8`. Exact ordinary grip consumption instead calls
`AnimControl_FindActiveType2LayerById(..., WeaponPartial)` at `0x141095924`
using the main Player `AnimControl*` stored at `Player+0x1A0`; the outer phase
caller independently carries that concrete object in `RBX`. `abr` changes only
the O(1) owner identity to `Player+0x1A0`; all enumeration and limits remain
read-only and unchanged. Run the same bounded P99-then-Mazur idle discriminator.
Rows/nonzero weight select the late command/IK branch; absent/zero-weight rows
select the source/channel branch. Complete silence again rejects packaging.

**RUNTIME REJECTED (`abr`):** PIDs `512/14160` again emitted no owned snapshot
or bounded-loss marker despite live P99 and Mazur presentation. Exact caller
closure shows why: `AnimControl_PhaseUpdateType2Layers 0x14172CAB0` is reached
only from two module-import observer jobs (`0x1403B7710/0x1403B78D0`) and the
dirty/DC-update overlay path `0x1417F98F0`. The hook installed before role B was
assigned and is not a steady per-frame weapon-pose seam. This rejects the
`0x14172CB32` packaging, not `Player+0x1A0`. Build `abs` disables the census.
Continue statically from the terminal ordinary path
`PlayerWeaponIKAction_Update -> WeaponIKFeather_Apply`; do not request another
runtime probe. The next launch must contain a fully supported visual fix and a
three-evidence, falsifiable prediction.

**STATIC grip closure after `abr`:** do not use another runtime discriminator.
The remaining apparent stock switches are all rejected. The per-screen byte at
`0x143073319 + 0xC0*screen` is the default-off debug setting
`hack-disable-weapon-partials`, not a remote-screen policy. Player+0x1FF0 is
exact `PlayerFlashlightController` (created at `0x140E3E660`, vtable
`0x142A23EA8`), not Skill; its `+0x18/+0xD8` branch suppresses weapon IK only for
an active flashlight hand process. Player+0x4320 is a blind-fire GestureLayer
token/latch whose only weapon link is scalar IK attenuation. The four
wrist/hand-prop locator reads in PlayerWeaponIKAction are also only distance
attenuation, while both WeaponIKFeather JointTrees are built from the common
Player primary skeleton. Therefore no screen flag, token reset, joint-index
rewrite or full action bypass has a proven correct resulting pose.

Continue statically from the ordinary steady command producer, not the rejected
event-driven phase hook: close the full P99/Mazur base/add/set/hands command
bundle reaching Player joints 21--32 through `AnimControl_CreateAnimCommands
0x14172C370 -> AnimStateLayer_CreateAnimCommands 0x14189EA60`. Compare the
whole compatible bundle with the authored NPC-Nadine retarget path; do not
substitute a single hands clip. The next game launch is allowed only for an
atomic local-only visual repair supported by three independent evidence types.

**STATIC / REJECTED (retarget map/profile split):** runtime entry 124 is
authored `proto 1003D654 -> nadine-dlc-base C67A06F1`, while the correct
NPC-firearm entry 133 is `npc-normal-base B91C4EA3 -> nadine-dlc-base`. Both use
the exact 74-row StringId map at raw `skel-retargets.bin+0x2720` and profile
`E51542CA`. Chloe entry 157 uses the same map but profile `FC34096C`; direct
ANIM decode shows only about `3.13 degrees` at the shoulders, `0.24 degrees` at
the elbows, `5.76 degrees` at the wrists and `0.8..7.1 mm` translation deltas.
Palm/hand-prop rotations and their semantic mask coverage are identical, and
all three rows disable optional arm/leg retarget IK. Together with the rejected
`abw` full local-row and `aby` final 88-row body-palette transfers, this rejects
the mixed profile as the primary owner of the gross crossed/one-hand pose. Do
not patch Nadine joint indices, profiles, scale, hands clips, command weights or
`Fg+0x240` rows. Continue only at a separate local presentation/pose consumer
not sourced from the already replaced body palette, or at a proved later
overwrite/publication mechanism. Runtime remains closed until that distinct
owner has three independent evidence types and a falsifiable prediction.

**STATIC / REJECTED (`auto -> run-gun-idle`):** do not force
`run-gun-idle 0x8D62CB37` or patch the `auto 0x11E3B022` predicate. The state is
a traversal-owned, near-full-body run-and-gun/blind-fire locomotion blend, not
ordinary drawn idle or WeaponPartial. PlayerWeaponAnimControl slots `+0xA0`
and `+0xC0` only query the authored `auto` candidate marker; all six real
callers are Get/Switch/Reload phase-2 completion monitors and none requests an
idle state or writes a pose. The steady composition intentionally returns to
base + Hands + empty WeaponPartial after the draw transition. Mutating any of
these paths changes action/event timing or fights a stock clear, not the grip.
Continue after the steady command composition into the later local pose owner;
no game launch until an atomic visual repair is statically closed.

**STATIC / REJECTED (previous phase / attachment leak):** the write at
`PlayerWeaponIKAction_Update+0x884` (`0x141095964`) targets
`AnimStateInstance+0x16C`, which is the previous phase copied from current
`+0x168` by `AnimStateInstance_PhaseUpdate`; command generation reads only
`+0x168`. It is event/transition bookkeeping, not a second pose writer. The
enabled P99 and Mazur presentation repairs are likewise holster-only by exact
selected endpoint (`pistolHolster` or `targSpineProp`) and cannot admit a drawn
`rightHand` weapon; the old P99 hip binding neutralizer remains disabled. Do
not patch `+0x16C`, reuse a hip/back transform for the hand, or request another
runtime probe. Continue statically through the two scalar inputs and lifecycle
of terminal `WeaponIKFeather_Apply`.

**STATIC IMPLEMENTED (`aca`):** before any further Nadine grip mutation,
distinguish the actual role-A visual producer. Native `NadineBuddy` and retained
screen-one `Player1` have separate vtable `+0x6F0` Fg allocators, separate
`object+0x1A8` FgAnimData, separate `object+0x198` DrawControls and exact
owner/driven-instance identities. The bounded read-only main SceneWindow census
publishes both chains on the game thread and, after stock post-cull, reports
which concrete body instances reach the final 61 draw lists. Written outcomes:
Player1-only, native-only, both (with same/different world transform), or
neither/packaging. Run only this automatic two-sample exam; do not add pose,
visibility, weapon or cull mutation until the producer is identified.

**RUNTIME REJECTED packaging (`acp`/`acq`/`acr`) / STATIC corrected next exam (`acs`, Mazur grip only):** the previous
outer-resolver and resolved-cmd6 substitutions remain REJECTED. The new exact
hypothesis appends shipped NPC Mazur high-part `B725EA01` as a stock cmd6 and
mode-1/weight-1 cmd7 into a fresh group-zero lane immediately before cmd3 in
the complete final Async command list. Evidence quota is met by the exact
Mazur overlay selector, the independent NPC-normal -> Nadine authored retarget
map and the closed final Async command/publish chain. Admission is role-B
Player0, legacy ED25/C39, exact Mazur WeaponOut, grenade-idle, motionless 350
ms, exact one-instance `Hands/7FB80CC8` root; P99, role A, ADS, transitions and
holster are excluded. The complete-list hook accepts only normal caller return
`0x141A91604`, group0, one source cmd6 and exact suffix
`3,4,15,16,17,24(248B1AAE),27,5`; the other cmd24 ids and the prepass caller
fail closed. Prediction: one injected marker and a coherent two-arm high-rifle
hold, with no P99/back/ADS changes. Marker plus unchanged pose rejects the
candidate; no marker must have a bounded admission/parser reason. Do not widen
the guard after a failed exam. Fresh PID 9460 stopped at `admission=content`
with zero injection, so the unchanged pose did not test the donor. The exact
false reject was the state-name gate: stable Hands is commonly
`FEC2DB76 default-hands`, while `FEC2DB76` and `7FB80CC8 idle-hands` share the
same direct root/source `7FB80CC8`. `acq` admitted only those two state aliases
but again stopped at `admission=content` with zero mutation. Direct PAK headers
then proved the deterministic false reject: exact B725 and the Player partial
family have one sample (`ANIM+0x22 == 1`), while the code required at least two.
`acr` accepted nonzero sample counts and took the source clip from the exact active
Hands root, requires the exact B725 donor to remain one-sample, and emits donor
phase zero. Fresh PID 17400 then passed `owner` and donor but stopped at exact
`hands-root`, again with zero mutation; the user saw the stock pose hold for
roughly two seconds and then break. Static closure shows that the shared
`7FB80CC8` raw Hands descriptor has exactly two valid runtime materializations:
direct Player `7FB/0/0 -> 27E57F43/FD6DBE03`, or Chloe-remapped
`7D11D4FD/ED25C708/C39CA709 -> 6C2B2396/4B0F5DE3`. `acs` admitted only those two
complete tuples and passed `owner`/`dwell`, but fresh role-B PID 18152 then
emitted `rejected reason=3` and zero injection. The fixed full-list grammar is
therefore REJECTED without testing the donor: it came from role-A remote Nadine
on screen 1. Static dataflow proves BuildCommandSegments copies a6 to
CreateContext+0x11 and final Async supplies a6 from `FgAnimData+0x08`; the
group-zero command-24 suffix is conditional. Next exam `act` is read-only:
repair is compile-time false and a bounded census records the exact local
group-zero mode/flags/full opcode sequence after the same strict Hands-root and
350 ms admission. Written discriminator: normal `3,4,15,16,17,27,5` confirms
local no-joint-conversion; mode-2 `...14...24,24,26,5` identifies the auxiliary
form; another structural sequence selects the third variant. Require at least
one `NADINE-MAZUR-STEADY-HANDS-CMDS ... structural=1 source=1` and zero
`injected`; the pose is expected to remain stock-broken. Do not authorize or
widen mutation until this local grammar is captured. Result: `act` captured the
exact local auxiliary list, not a variant main list: `mode=2 flags=0x9`, 58
words/14 commands, `source=0`, sequence
`0,1,2,9,3,4,14,15,16,17,24(248B1AAE),24(514C0761),26,5`. This confirms the
short command-14 external-pose import and rejects the final Async Create seam as
an authored clip owner. Static dataflow closes its source as `Fg+0x100`; the
normal authored DAG is built earlier by `AnimManager_PerformAnimBlend
0x141A92260 -> Build 0x141A92637 -> Process 0x141A92759`, before Player
post-blend/IK and the later Async import. Next `acu` exam remains read-only and
labels the same bounded rows `stage=perform|async`. Require the Async control
row above plus a structural Perform `source=1`; Perform source zero rejects the
steady Hands producer, while silence rejects the wrapper. Do not enable B725 or
any other pose write from this census alone. Result: `acu` captured three
structural Perform main variants (61/54/49 commands, all mode1/flags3/source1),
the mode2/source0 control, and an Async final main 49-command
mode1/flags3/source1 list. This closes the exact post-WeaponIK authored Hands
DAG. Build `acv` enables only final-Async mode1/flags3 mutation: after every
stock cmd23 and immediately before the sole cmd3, append one exact one-sample
`B725EA01` cmd6 and mode1/weight1 cmd7 into lane0, then preserve exact
`3,4,15,16,17,24(248B1AAE),27,5`. Require one injected marker plus a stable
two-hand Mazur hold. Marker with no visual correction rejects the donor/final
owner; no marker is packaging. P99/Perform/mode2/roleA/actions remain stock.
Result: `acv` installed but emitted only owner/dwell/hands-root and zero injected
rows; the user saw the unchanged native draw-to-broken-steady transition. Three
exact retained Player0 addresses appeared inside that one stationary WeaponOut,
so pointer-keyed dwell was not a valid presentation invariant. Next `acw` keeps
the same 350 ms session/art/body-pose anchor, validates the current generation
synchronously, and selects exactly one canonical `7FB80CC8` or `7D11D4FD` clip
directly from the exact final Async list. Require one observed-source injection
marker and a stable two-arm hold; marker plus bad/unchanged pixels rejects B725.
Result: `acw` passed owner and reached dwell but emitted no injection; the
presentation callback cadence still reset the 350-ms timer. `acx` removes only
that wall-clock condition. Exact current Player/FG generation, Mazur
WeaponOut=2, grenade0, zero movement/input/cover, final Async mode1/flags3,
one canonical Hands source and exact full-list grammar remain mandatory. Run
only the written stationary/no-ADS/no-melee/no-damage/no-interact exam. Marker
plus unchanged/bad arms rejects B725/final owner; no marker is now list/owner
packaging, not a timer.
Result: `acx` still emitted only compound `admission=owner`; its startup row was
adjacent to cold `current=-1/draw=0`, and no later donor/list/injection marker
identified the user action. Next `acy` is read-only: repair off, existing
Perform/Async census on, and a 24-row/250-ms owner-stage census over the already
read Player/FG/skeleton, controller slot/state, grenade, weapon art/parent,
expected-FG and idle/action/movement/cover fields. Use it once to choose
controller vs presentation owner vs idle gate vs list placement. Do not infer
anything from pixels and do not re-enable B725 before the exact row.
Result: the valid repeated `acy` exam used PIDs 14216/16628. Role B showed the
expected transient controller rows through Draw, then necessarily passed the
full exact owner/art/idle scope and emitted the final Async normal main list
with `mode=1 flags=3 structural=1 source=1` and the closed
`3,4,15,16,17,24(248B1AAE),27,5` suffix. The user saw the stock-broken pose, as
required by the read-only build. This rejects controller/owner/idle packaging
as persistent blockers and proves exact final-list liveness. Build `acz`
enables only the already-audited B725 insertion at that final Async list and
turns the census off. Require one injected marker and a stable two-arm Mazur
hold; marker plus unchanged pixels rejects donor/final-owner sufficiency, while
no marker must have a bounded rejection reason. P99, role A, ADS, transitions,
holster and auxiliary lists remain stock.
Result: `acz` PIDs 19616/22468 installed but emitted only `admission=owner` and
zero injection/rejection rows through the first user action and a second
40-second stationary retry. The unchanged broken pose again did not test B725.
Next `ada` keeps the same exact mutation and turns the bounded `acy` owner/list
census on simultaneously. Do not widen any gate: require either one injected
marker plus a corrected two-arm hold, or a detailed owner/list row that selects
the current packaging failure with zero writes.
Result: `ada` PIDs 21812/20616 finally emitted the exact injected marker at log
80842 (`7D11D4FD -> B725EA01`, lane4, samples337/1) in the audited final Async
source-one list. The user still saw the arms break after the first roughly two
correct Draw seconds, with later Draws broken immediately. B725/final terminal
partial is therefore RUNTIME REJECTED and compiled off in `adb`; do not retry
or move it. The current static frontier is the exact Draw-completion boundary:
Get action removal -> PlayerPlayWeaponOverlayAction enqueue -> state2 ->
RefreshWeaponPartialAndIK. Compare its payload/requested custom-part states and
final command owner against the initially correct state1 interval before any
new mutation or runtime launch.

Next `adc` closes the exact new-instance versus cached-instance fork without a
pose mutation. Static evidence is sufficient: controller/action lifetime,
queue producer/consumer dataflow, and the explicit
`AnimStateLayer_ApplyTransition 0x1418A1EA0` plus direct-fade
`0x1418A27C0` loop-current branches all agree with the user's first-Draw-good /
later-Draw-immediate asymmetry. The bounded role-B Player0 WeaponPartial hook at
`AnimStateLayer_TakeTransitions 0x1418A1350` records request state and queue
sequence plus current/output instance identity before/after stock. Exact exam:
fresh spawn -> Draw Mazur until it breaks -> Holster -> second Draw -> wait one
second. New first instance plus reused second instance confirms cached-state
reuse; a missing second request confirms request/cache loss; a new instance on
both rejects reuse and sends the next static pass to the later state-dependent
writer. `adc` must make no visible change.

Packaging correction: the first `adc` load safely skipped the hook in both
processes because the expected prologue omitted the leading `40` REX prefix on
`push rdi`. `add` copies the exact 13 bytes through `sub rsp,0E0h`, pads its
12-byte entry jump with one NOP and keeps the same read-only prediction. The
skipped `adc` run produced no lifecycle data and did not test a hypothesis.

`add` runtime result: both Draws issued the complete request sequence and every
request created a new destination instance. Loop-current reuse and a missing
second request are RUNTIME REJECTED. The only measured structural difference
was the first custom Draw changing track count 1->2 versus the repeated Draw
2->3. Do not mutate or delete that extra track: static Step dataflow proves it
is a normal fade track until its two computed weights reach zero and the stock
prune pass removes it.

Next `ade` is one bounded read-only endpoint exam. It hooks the sole stock
weight updater `AnimStateLayer_UpdateBlendWeights 0x1418A3820` after the exact
role-B Player0 WeaponPartial transition hook has published the layer pointer.
For 6.5 seconds after each request, at 100 ms cadence, record all bounded track
state ids and both timer/duration/weight triples. Fresh spawn -> Draw Mazur and
wait through the break -> Holster and wait >=3 s -> second Draw and wait 1 s.
Hsource-fade requires the first visual break to coincide with a formerly good
source weight reaching zero and that source to be absent on the second Draw;
Hdestination-fade requires a destination reaching weight one; no exact weight
endpoint selects the later-writer alternative. The build is read-only and must
not visibly change the pose.

`ade` runtime result: after the required complete Holster wait, the second Draw
started from one `s_no-weapon` track (`1 -> 2`, not `2 -> 3`). Its
`s_weapon-grab 28B0F93B` source was pruned by 109 ms, the same instance became
`s_weapon-draw 1313FB4E`, and the final clear removed `s_weapon-draw` by
453 ms and `s_weapon-out 7405AACB` by 781 ms. From 891 through 6422 ms only one
full-weight `s_no-weapon 3889CC0E` track remained. This RUNTIME REJECTS a
persistent source/destination fade, leaked track, loop-current reuse and a
missing repeated request. The first clear was interrupted by the user's
immediate Holster, so do not claim the first visible break is independently
timed to a particular intermediate weight.

The `adf` STATIC closure rejects `NewIkWeapon` as a pose owner: refresh publishes
only a 16-byte ProcessWeapon weak pair, `PlayerWeaponIKAction_Update` copies it
and immediately erases the StringId payload through `0x142052430`. Exact asset
and generator data prove `66846123 p-unholster-chest^idle-part` has eleven
samples on the Player skeleton, authors all joints 21--32, and command phase
`10.0` selects its terminal sample. Reuse the already reached final-Async
group-zero insertion seam to append this one terminal partial before command 3;
do not prolong or loop the Draw state itself. Scope is role-B Player0 legacy
Nadine, Mazur, steady WeaponOut only; P99, role A, ADS/movement/transitions and
all non-exact list grammars remain stock. Prediction: after one second idle the
log emits `NADINE-MAZUR-DRAW-ENDPOINT ... donor=0x66846123 phase=10 ... /11`
and the initially correct two-handed pose remains correct. Marker plus broken
arms rejects the donor; no marker selects bounded packaging telemetry. Full
STATIC evidence and competitors are recorded in `COOP_STATUS.md`.

The first `adf` runtime was inconclusive: both hooks installed, but role B
emitted only the generic early `admission=owner`; there was no draw-endpoint
injection/rejection marker and all logged Mazur attachments were still
holstered on `AD047A`. Do not classify the terminal Draw donor as rejected.
Build `adg` keeps the exact repair but decouples the already proven bounded
owner census from the disabled heavy command census. Next exam: role B draws
Mazur, releases every input, stays out of ADS and stands still for two seconds.
Require either `NADINE-MAZUR-DRAW-ENDPOINT injected donor=0x66846123 phase=10`
or a detailed `NADINE-MAZUR-STEADY-HANDS-OWNER` row that selects the exact
failed scope field before any further mutation.

`adg` selected that field: local Nadine remained `slot=1, Draw=1` through the
visible arm break and then auto-holstered through state 5; it never reached
WeaponOut 2, so the draw-endpoint donor still did not execute. Build `adh` is
read-only and restores the previously confirmed WeaponPartial transition/fade
trace while disabling the pose mutation. Next exam is one fresh Mazur Draw with
all input released, followed through the engine's automatic holster. Determine
whether `s_weapon-draw 1313FB4E` reaches/stalls at terminal phase, whether
`s_weapon-out 7405AACB` or `s_no-weapon 3889CC0E` is requested before the
cancel, or whether the draw request itself is lost. Do not admit controller
state 1 for mutation until this exact lifecycle is closed.

`adh` closed it: the fresh Draw was not stuck. At line 82711 it entered
`slot=1, WeaponOut=2`, line 82715 created `s_no-weapon`, and that empty track
became sole/full-weight before the delayed native Holster at line 82795. The
state-2 repair window is several seconds long. Build `adi` combines the exact
terminal Draw repair with both bounded owner and transition/fade censuses so the
next single Draw cannot be inconclusive. Require the normal lifecycle followed
by either the exact `injected donor=66846123 phase=10 .../11` marker or a named
owner/parser rejection. Marker plus broken pixels rejects the donor; keep the
later automatic holster stock.

`adi` remained packaging-inconclusive, not a donor failure. PID 16324 reached
the normal `state=2 -> s_no-weapon` window for 5.375 seconds but emitted neither
an injection nor parser rejection. Earlier owner rows exposed fire-hold
`action=0x809` and then backward movement; the final neutral stationary cycle
could not produce a new row because movement fields were absent from the
deduplication signature. Build `adj` is read-only: endpoint mutation and the
closed lifecycle/fade traces are disabled, while the final command census and
an expanded owner row log exact action, magnitude, stick, world vector and
separate idle subpredicates. Require either a concrete nonzero field or
`idleParts=1/1/1` plus `NADINE-MAZUR-STEADY-HANDS-CMDS`; do not relax the idle
scope before this discriminator.

`adj` closed the idle gate and exposed a second diagnostic packaging defect.
PID 19468 emitted seven distinct Perform command signatures with `source>0`,
then one Async `mode=2/flags=9/source=0`; those eight rows exhausted the shared
table before the later unconditional Async build at `0x141A93038` could be
observed. Build `adk` is still read-only and splits the bounded signature table
by Perform/Async stage. One neutral Draw must now produce either a later Async
`source>0` tuple with its exact mode/flags, or only source-zero Async variants.
The former earns a statically scoped final-pass package; the latter rejects this
donor seam. Do not re-enable endpoint mutation before this discriminator.

`adk` closed the temporal command handoff. In stable WeaponOut, PID 19832
produced eight structural Perform `mode1/flags3/source1..2` variants but only
the 58-word Async `mode2/flags9/source0` import. The earlier `acu` Async-main
source row was therefore transitional, while the persistent source remains in
Perform. Build `adl` enables the full-arm `66846123` terminal Draw sample only
in exact Perform group zero, before cmd27 exports `Fg+0x100`; the later stable
Async cmd14 imports that pose. Require `injected stage=perform ... phase=10
samples=.../11` and a hold that stays normal. Marker plus the same break rejects
the donor and selects intervening PlayerWeaponIK; no marker remains packaging.

`adl` was a deterministic packaging failure: repair was true and census false,
but the Perform detour was compiled only under the census flag. Create+Async
still installed and readiness/boot logging falsely reported success, while the
injector's `stage==Perform` guard was unreachable. `adm` installs Perform when
either repair or census is enabled. Repeat the same one-Draw exam and require
the exact Perform injection marker before judging pixels.

`adm` produced the exact marker and a visible partial correction: the arm no
longer crossed the torso, but one hand held the Mazur while the other remained
extended. This confirms the Perform donor and rejects donor-only sufficiency.
The exact earlier Mazur boundary measured WeaponIKFeather rewriting C39 rows
19..26, while the Player 21..32 donor converts to C39 19..30. Build `adn` uses a same-thread TLS
ticket set only by successful `66846123` insertion and bypasses Feather only for
that same role-B/local/Mazur/WeaponOut/idle Player-Fg-weapon generation. Require
the boot `paired endpoint/Feather repair READY`, then both
`DRAW-ENDPOINT injected` and `WEAPON-IK-FEATHER-SUPPRESS pairedDonor=1`.
Both markers plus two correct hands confirms the combined repair; both markers
plus an extended arm rejects Feather sufficiency and selects a later writer or
insufficient terminal-sample/helper/finger content. A suppress marker without
injection is packaging; injection without suppress also permits a stock
`apply_weight==0` no-op and must be separated from owner/order failure in the
log.

`adn` is now REJECTED as an intermittent final repair. PID 1928 produced one
exact injection and 16 paired Feather suppressions, then `reason=4`; the user
observed the corresponding brief improvement followed by a broken/twitching
hold. Reason 4 is precisely loss of the optional idle-Hands cmd6 after the
exact owner/root and final-list scope have already passed. Build `ado` permits a
zero-source list only when the independent exact Hands registry/track/instance/
Animation-root proof is live; fallback roots and source multiplicity remain
rejected. Require continuous `injected ... liveSources=0 exactHandsRoot=1` and
paired suppress markers after the initial window. If those are continuous but
the visual still breaks, reject the source-gate hypothesis and move to the
postblend/Async late-writer frontier; do not relax further command-list guards.

`ado` is runtime-rejected and crash-quarantined. Role-B PID 14492 emitted one
exact endpoint injection plus sixteen paired Feather suppressions without a
later source rejection, yet the grip stayed broken and twitching; the stock
controller then performed its normal delayed Holster. Both game copies later
terminated, with no crash-observer or WER row identifying the instruction.
Build `adp` disables the endpoint, Feather and owner-census package together.
Do not launch another grip mutation until the installed hot-hook lifetime and
the exact `56C57AD2 + 283966ED` authored Mazur pair are closed statically.

`adq` closes that prerequisite without restoring the crashing package.  The
exact custom Mazur root uses complementary one-sample Player clips
`56C57AD2[29..32] + 283966ED[21..28]`; raw masks, the shared retarget row 124,
native offline decoding of steady `25CC08FD`, and the already confirmed Perform
pixel boundary satisfy the evidence quota.  The request scalar `0.8` is a fade,
not a pose blend; both complementary clips are therefore overlaid at weight
1.0.  Only exact E8 callsites `0x141A92905` and `0x141A915FF` are patched in one
suspended/readback/rollback transaction.  Require the boot READY marker and
`NADINE-MAZUR-AUTHORED-PAIR injected ... hands=56C57AD2 arms=283966ED
weights=1/1`.  Both markers plus a stable two-hand hold confirms the expired
custom-pair hypothesis.  Both markers plus the same arm-out/twitch rejects
pair-before-stock-Feather sufficiency and moves the frontier downstream.  No
marker is packaging/admission; any crash rejects this relay/memmove package.

`adq` is runtime-rejected as a pre-postblend repair.  PID 14424 emitted READY
and the exact `56C57AD2 + 283966ED`, lane-5, weights-1/1 marker, while both
processes stayed responsive, but Nadine still held the Mazur one-handed with
the other arm away.  Disable the flag in `adq-safe`.  The next P0 is to close a
post-Player-`+0x408`, final-Async command boundary with exact list ownership;
do not retry a Perform donor, Feather bypass, or function-entry detour.

`adr` closes that exact downstream boundary without a shared vtable or function
entry hook.  `0x141A928F0` synchronously executes Perform `0x141A92905`, then
the sole Player `+0x408` call `0x141A929A1`; exact earlier Mazur rows show stock
postblend changes C39 19--26.  The final Async job builds/processes group zero
first, removes group-zero from its remaining masks, and command 14 imports the
postblend `Fg+0x100` bank before final palette publication.  Preserve the
already engine-retargeted authored-pair rows C39 19--30 after Perform, run stock
postblend, and restore only those 576 bytes before Async.  Require all three
markers: `preservation READY (three exact callsite relays)`, `injected
stage=perform`, and `restored stage=postblend rows=19..30`.  All three plus a
stable two-hand hold confirms the postblend-overwrite repair.  All three plus
the arm still away selects authored-content/omitted-helper or a later Async
writer.  Any missing marker is packaging; any crash rejects the package.

`adr` is runtime-confirmed for the steady two-hand topology.  Role-B PID 13736 emitted the
exact Perform injection and postblend restore markers, stayed responsive, and
the user confirmed that local legacy Nadine now keeps both hands on the Mazur.
The later `ads` side-by-side image refines the result: the pose is stable, but it
is a diagonal low carry rather than Chloe's horizontal chest/shoulder hold.
Keep the postblend preservation boundary; replace only its steady content.

- [x] Runtime-reject `adt` high-carry content.  Role-B PID 17648 emitted the
      exact `F4B56F62 + 5676695B` injection and postblend restore, while role A
      remained stock and both processes stayed responsive.  The user reported
      that the resulting hold was entirely unlike Chloe.  There were zero
      transition markers.  Do not try another named clip.

- [x] Runtime-reject `adu`, the no-clip stock-pose preservation exam.  The
      steady tree already produces a Chloe-authored Base `5F913F74` plus Hands
      `7D11D4FD` bundle and stock retargets it into C39 before Player postblend.
      Re-resolve and require that exact live `7D11D4FD` Hands tuple at
      admission, capture and restore; reject the `11EE475B` transition so equip
      remains entirely stock.
      Snapshot C39 local rows 19--30 only after Perform returns, run stock
      `Player +0x408`, then restore the same rows before Async cmd14 imports
      them.  Both required markers appeared on role-B PID 12656, but the user
      observed a one-hand hold.  This rejects wholesale pre-postblend C39
      19--30 preservation and proves that stock postblend contributes required
      arm/IK state.  `adu-safe` disables the package.

- [x] Run the bounded read-only `adv` Chloe/Nadine WeaponIK goal census.  Static
      closure now proves that `WeaponIKFeather_SolveWeaponFeatherIk
      0x1418F2E50`, reached at the exact Apply callsite `0x1418F29D0`, builds one
      common goal only from the live left/right hand-prop world locators.  It
      does not consume a weapon attachment transform.  Build `adv` calls stock
      once, reads the already seeded JointSet cache directly before/after it,
      and normalizes spined/shoulder/elbow/wrist/hand-prop to the same spined
      frame.  Both PIDs reached READY, entry and exact role-specific admission,
      but every attempt failed at `pre-capture`: the WS cache is refreshed only
      by the first stock `JointSet_GetJointLocWs` inside Solve.  This result is
      instrumentation-inconclusive and selects no pose hypothesis.

- [x] Run corrected read-only `adw`.  It failed closed at three-callsite
      preflight in both processes because the documented left address
      `0x1418F31D4` is the instruction after the getter call.  No relay was
      published and no pose conclusion is allowed.

- [x] Run corrected read-only `adx`.  It keeps the exact Solve relay and wraps
      only Solve's two existing WS getter calls at `0x1418F31CF/0x1418F31DF`.
      The wrappers copy after the stock refresh and return the original pointer;
      the post-Solve cache remains a passive direct read.  Require the new
      three-callsite READY marker, entry, admission, one GOAL row and ten CHAIN
      rows in both processes.  Different normalized goals select target
      construction; equal goals with different post chains select Jacobian/rest
      geometry; equal results select a later writer/weapon placement.  This exam
      makes no visual change and authorizes no mutation.  Keep stock postblend;
      do not guess another clip, suppress Feather, or splice C39 rows.
      Both complete samples arrived.  At identical steady solver scalars,
      spine-relative Chloe and Nadine goals differ by `0.282041` position units
      and `81.2885` degrees; Nadine's solver still reduces its endpoint gap to
      `0.00723289`.  This CONFIRMS a pre-solver target divergence and rejects a
      scalar/lifecycle explanation for the sampled frame, but does not yet prove
      a goal-copy repair.

- [ ] Close the exact live-goal transfer boundary statically before mutation.
      Compare role-A packet transport with same-process remote-Chloe capture;
      prove session/freshness and exact Mazur/WeaponOut ownership, reconstruct
      the donor goal under Nadine `spined` via `Transform_Compose 0x1401998F0`,
      and prove both getter return pointers are read-only through the complete
      Solve consumer.  Keep stock Jacobian maps, Solve, commits and postblend.
      A single hard-coded `adx` sample is forbidden.

- [x] Run the bounded read-only `ady` donor-location discriminator.  The exact
      Solve/getter package now records owner 0 and owner 1 separately.  In the
      role-B process, require both entry markers and then either a complete
      `owner=1 body=Chloe look=0x3F334972 joints=651` Mazur/WeaponOut goal or a
      clean entry-without-admission result.  A complete owner-1 sample selects
      same-process relay and avoids network cadence; entry without admission
      selects the endpoint-authenticated role-A packet; no owner-1 entry rejects
      that local donor entirely.  Stock Solve/getters remain unchanged and the
      exam predicts no visual difference.
      Role-B owner 1 produced a complete Chloe/651/Mazur goal.  It is within
      `0.00853095` units / `1.22331` degrees of independently sampled local
      Chloe, while the same-process Nadine goal is `0.300613` units /
      `75.9353` degrees away.  Same-process donation is CONFIRMED and the UDP
      alternative is rejected.

- [x] Implement and run one same-process live-goal relay exam.  Role B only:
      publish owner-1 Chloe's post-getter/pre-Solve common target relative to
      `spined`; after 350 ms exact steady Mazur scope on both owners and with a
      source age no greater than 100 ms, reconstruct it under owner-0 Nadine
      `spined` and return the same TLS world Locator from both exact getter
      wrappers.  Revalidate Player/Fg/skeleton/weapon identity and generations.
      Keep stock getters, JointSets, maps, Solve and commits.  Require both
      bounded CAPTURE and RELAY markers.  Two-hand Chloe-like hold confirms the
      target boundary; markers plus malformed hold select chain/proportion or a
      later writer; delayed failure selects lifetime/cadence; jitter rejects the
      unsmoothed live source.  No hard-coded target and no pose-row copy.
      Build `adz` reached the full boundary in role B: repeated Chloe capture
      and sixteen Nadine relay markers used source ages `0..32 ms`, with both
      processes responsive and no new exception/fatal row.  Nadine visibly
      started toward the correct hold and immediately broke again.  Thus the
      live target is a contributor but is REJECTED as a sufficient repair;
      cadence/packaging did not cause this failure.  Next close the first exact
      writer after the substituted getter/Solve result (remaining WeaponIK
      commit, later Player postblend, or final Async JointBlend/plugin) before
      any further runtime mutation.

- [x] Runtime-reject the bounded read-only `aea` terminal-writer discriminator.  Keep the
      proven same-process Chloe goal relay, but allow it only after the exact
      `WeaponIK_Apply` observer at callsite `0x1418F2E0B` is READY.  In role-B
      owner-0 legacy Nadine/Mazur/WeaponOut/zero-motion scope, capture raw C39
      rows `19..30` immediately before and after the sole stock terminal Apply.
      Require existing CAPTURE+RELAY markers, one terminal summary and twelve
      rows.  Changes in `19..26` confirm the second `SolveArmIk`/commit as the
      conflict; a zero mask rejects it and moves the frontier to later
      Player/Async processing.  No suppression, smoothing, row restoration or
      new clip mutation is permitted in this exam.  Both processes reached
      READY, but role B crashed before CAPTURE/RELAY or any terminal sample.
      This is a packaging failure with no pose interpretation; reject the
      internal `0x1418F2E0B` detour and replace it with correlated midpoint/final
      observation at the already-safe outer Apply and Solve callsites.

- [x] Run the bounded read-only `aec` terminal-input discriminator.  Do not
      detour `WeaponIK_Apply`.  Scope TLS at the already-proven outer Apply
      callsite `0x1410959AC`, then wrap only the four stock
      `JointSet_GetJointLocWs` calls at `0x1418F1872/1896/18A6/18B5`.  Require
      role-B owner-1 Chloe and owner-0 Nadine summaries plus four normalized
      wrist/hand-prop rows each.  A material delta selects the terminal target
      construction; near-equality selects chain/Jacobian/commit or later
      processing.  The live-goal relay and rejected `0x1418F2E0B` hook remain
      disabled; this exam makes no pose writes and predicts no visual change.
      Both processes remained responsive and role B produced the complete
      owner-0 Nadine sample, but owner-1 Chloe never reached this terminal
      boundary.  This confirms safe packaging only; the comparison is
      instrumentation-inconclusive.

- [x] Run corrected read-only `aed`.  Keep the same outer Apply plus four
      getter relays, but admit the identical exact scope on role-A owner-0 local
      Chloe as the comparator to role-B owner-0 local Nadine.  `ady` already
      bounds local-versus-retained Chloe to 0.00853 units / 1.22 degrees.  A
      complete four-row sample from each process is required before comparing
      terminal wrist/hand-prop inputs; either missing sample remains packaging.
      Both complete samples arrived.  Chloe/Nadine deltas are respectively
      `0.085187/50.511 deg`, `0.232679/99.078 deg`, `0.045186/53.607 deg`, and
      `0.151592/68.523 deg`; same-character view controls remain below 0.021
      units / 6.81 degrees.  Terminal-input divergence is CONFIRMED.

- [x] Reconstruct the exact final goal built by `WeaponIK_Apply 0x1418F1780`
      from the four getter results before `SolveArmIk 0x1418F1C43`.  Trace all
      transform helpers, coordinate spaces and `animScale`; reject each
      competing combination.  Do not relay or copy the four raw Locators.  A
      future mutation requires a single derived, normalized goal plus three
      independent static evidence types and a written falsifier.  Exact result
      for selector 1 is `Compose(rightProp, Relative(leftProp,leftWrist))`;
      right wrist is validation-only and `animScale` changes translations only.

- [x] Implement one bounded role-B terminal-target relay at the already-safe
      four getter callsites.  Publish owner-1 Chloe's exact derived target
      relative to terminal `spined`; for exact owner-0 legacy Nadine/Mazur/
      WeaponOut/idle, compose it under Nadine `spined` and algebraically feed
      only that target to the stock left-arm Solve.  Require source/target
      markers with <=100 ms age.  Markers plus Chloe-like two-hand hold confirm
      `H-terminal-target`; markers plus the same pose select chain/proportion or
      a later writer; an initial fix followed by break selects lifetime/cadence.

- [x] Run the single written `aef` terminal-target exam.  Both PIDs reached the
      combined READY marker and stayed responsive, but role B emitted zero
      terminal CAPTURE and zero RELAY rows.  The observed immediate break is
      therefore admission/packaging, not a semantic target falsifier.  Disable
      `aef` in `aeg-safe`.

- [x] Restore the exact runtime-confirmed `82e7b9e` / `adr` steady two-hand
      implementation as the active working baseline without reverting later
      static findings.  Its known residual is precise: stable low diagonal carry
      rather than Chloe's high horizontal chest/shoulder hold.  Keep all later
      rejected high-pair/stock-pose/whole-Feather mutations disabled.

- [x] Close the muzzle-angle owner before another launch. Runtime common-Solve
      samples show a large Chloe/Nadine split in the right wrist/hand-prop
      relative to `spined`; terminal `WeaponIK_Apply` solves only the left arm,
      while `0x14181F710` is the exact stock right-wrist quaternion operator and
      the weapon later consumes the right hand-prop attachment. Implement the
      narrow `aeh` semantic hand-prop relay through Nadine's own arm geometry.

- [x] Runtime-inconclusive `aeh`: both installers reached READY and role B
      emitted the exact confirmed authored-pair injection plus C39 `19..30`
      restoration, but the log contains zero Chloe CAPTURE, Nadine RELAY or
      `baselineRestore=0` markers. The user's unchanged low hold therefore came
      only from the old baseline and cannot judge `H-right-anchor`. Disable the
      relay in `aei-safe`; do not repeat its live-owner-1 admission. While
      appearance 1 is the active task, launch exactly one game copy.

- [x] Run the statically closed one-process `aej` transient-postblend exam for
      appearance 1. Disable the low `82e7b9e` authored-pair baseline. After
      stock Player `+0x408`, capture the last exact local-Nadine C39 rows 19--30
      while full-arm `s_weapon-draw 1313FB4E` is live and `s_no-weapon` is
      absent; when `s_no-weapon 3889CC0E` appears, seal and replay that same
      postblend sample. Require READY, `captured source=s_weapon-draw`, and
      `replayed trigger=s_no-weapon`. All markers plus the initial muzzle-up
      hold persisting confirms H-transient-postblend-owner; all markers plus a
      wrong hold rejects it; missing a marker is packaging/lifecycle. Launch
      exactly one role-B `tll.exe`; the user alone controls gameplay.

      Runtime result: role-B PID `4844` loaded `aej`, reached READY and stayed
      responsive, but emitted zero capture and zero replay rows. The user's
      completely broken hold was the untouched stock pose because the fallback
      baseline was disabled. This is admission/lifecycle-inconclusive and does
      not test the source content.

- [x] Run one read-only `aek` first-fail census at the same Player `+0x408`
      callsite. Keep every grip/arm-pose mutation disabled. Once per 100 ms,
      log exact retained equality, owner/controller/idle admission and the
      bounded WeaponPartial track inventory including `1313FB4E`, `11EE475B`,
      `7405AACB` and `3889CC0E`; cap output at 64 distinct rows. READY without
      any ENTRY rejects callsite reach; ENTRY only with `retained=0` rejects
      exact local-Player ownership. Controller state 1 plus a live
      source selects the state-overlap gate; state 2 with `11EE` but no `1313`
      selects the custom-Mazur source mismatch; state 2 with only `7405/3889`
      proves the postblend sample is late. This exam predicts no pose change.
      Launch exactly one role-B/Nadine process; the user alone controls gameplay.

      Runtime result: exact retained ENTRY and full lifecycle arrived. The
      source window is controller state 2 with simultaneous
      `11EE475B + 1313FB4E`, followed by `7405AACB` and `3889CC0E`. This rejects
      reach, state-overlap, art and idle failures. Read-only memory inspection
      found the final blocker: stock right-arm JointParams use NaN only in the
      non-semantic scale/translation W lanes, while the old validator required
      all 12 floats finite.

- [x] Run the corrected one-process `ael` replay. Validate only
      `scale.xyz + quaternion.xyzw + translation.xyz`, preserve the complete
      raw 48-byte rows, and keep every original owner/topology gate. Require
      READY, `captured source=s_weapon-draw`, and
      `replayed trigger=s_no-weapon`. All three plus the initial correct hold
      persisting confirms the transient postblend owner. All three plus a wrong
      hold rejects source content/row sufficiency. Launch exactly one role-B
      Nadine process; the user alone controls gameplay.

      Runtime REJECTED: PID `11804` emitted READY, capture and replay. The
      sealed source was the late Draw sample at phase `0.883051`; the user's
      screenshot showed both hands collapsed low and a nearly vertical
      muzzle-down rifle. This proves the sampled content is a transition pose,
      not Chloe's final grip. Disable it permanently; do not phase-tune.

- [x] Restore `aem-safe`: enable only the confirmed `82e7b9e/adr` authored
      two-hand baseline and disable the rejected transient replay/census. Its
      low diagonal muzzle is an explicit known limitation, not completion.

- [~] Resolve the exact final steady Chloe post-IK grip without using a second
      game process or any Draw/Grab/WeaponOut transition frame. Start from the
      already-confirmed final right-wrist/hand-prop divergence and the rejected
      pre-postblend row-copy experiments; identify a single-process semantic
      target or engine retarget relation with three independent static evidence
      classes before another visual mutation.

      Read-only `aen` is REJECTED as a discriminator. PID `8488` reached exact
      three-callsite READY, then bounded admissions 1/2/3 and no source result or
      rows. Stage 3 is before nested capture because the selector itself required
      the desired remapped `7D11D4FD` Hands root; a direct `7FB80CC8` root could
      never produce the promised incomplete result. Do not weaken or rerun it.

      The offline retail evaluation is complete. New
      `scripts/native_anim_pose_audit.py` pins the exact EXE hash, invokes the
      retail handlers for `22/24/11/27/29/30/15/16`, validates five authored
      phase samples and composes `5F913F74` rows 29--37 with constant
      `7D11D4FD` rows 38--41 through stock `Transform_Compose`. The raw Chloe
      left/right hand-prop gap is `0.51348..0.54328`, versus only `0.0293577` at
      the independent live `adx` pre-Solve boundary. Therefore the source clips
      are not Chloe's final grip and direct row/phase copying is STATIC
      REJECTED. Separately, `56C57AD2` already contains the exact
      `0A2F8976` right palm/hand-prop transform, so a missing trigger-hand lane
      cannot explain the low muzzle of the stable baseline.

      The live body `cmd6` leaf is Player `25CC08FD`, not outer `5F913F74`.
      Offline retail evaluation closes its 301-frame/stride-50 loop and shows
      near-identity additive rotations plus zero translations on Player rows
      21--28, so it also cannot be used outside stock reference/additive command
      composition. The four `PlayerWeaponIKAction_Update` calls at
      `0x14109578A..0x1410957B7` only measure wrist-to-prop distances and
      attenuate Apply weight; they are not a missing target constructor.

      Current static frontier is the post-common-Solve right-hand anchor. Two
      independent local-Chloe runs place its steady right hand-prop reference
      only `0.0102249` units / `1.88968` degrees apart; normalized midpoint
      `P=(-0.008658425,-0.355107,-0.01403325)`,
      `Q=(0.390035796,0.408591428,0.156346330,0.810235119)`. Reclose the old
      `aeh` stock `SolveArmIk`/wrist-rotation path around this single-process
      semantic reference and the confirmed 82e7b9e baseline. Require exact
      role-B owner0 Mazur/WeaponOut/idle admission, postblend retention, three
      evidence classes and a one-process written falsifier before launch.

      Build `aep` implements that exact exam. It retains the confirmed
      `56C57AD2 + 283966ED` base, composes the normalized local-Chloe right
      hand-prop reference under Nadine's live `spined`, preserves Nadine's own
      wrist-to-prop relation, runs stock right `SolveArmIk`, then leaves the
      terminal left-arm solve and commits enabled. A TLS pass key is armed only
      by the exact successful authored-pair ticket in the same synchronous
      Perform-to-`Player+0x408` pass and pins Player/Fg/weapon/skeleton plus
      generations; a mismatch restores the confirmed baseline. The written
      discriminator requires the same-pass `injected`,
      `source=static-Chloe-steady` right-prop and `baselineRestore=0` retained
      markers. Stable two-hand muzzle-up confirms the anchor; low/wrong with all
      markers rejects it; raised muzzle plus detached/jittering support hand
      selects terminal-left ordering. Launch exactly one role-B Nadine process;
      never control gameplay from automation.

      Runtime `aep` was packaging-inconclusive, not a target-value result. PID
      `4568` emitted exact steady `56C57AD2+283966ED` injection followed by the
      guarded C39 `19..30` baseline restore, but zero static-right-prop and zero
      `baselineRestore=0` markers. The screenshot therefore reproduced
      `82e7b9e` exactly because the correction never ran. The earlier Feather
      Solve and later steady-overlay ticket do not coexist in one postblend
      pass. Build `aeq-safe` disables this unexecuted mutation. Statically close
      a late solve using the persistent `Player+0x4308` paired JointSets after
      baseline restoration; do not weaken the same-pass key or reuse a stale
      action-owned JointSet.

      Build `aer` closes that late seam statically. The 0x150-byte
      `Player+0x4308` WeaponIKFeatherInfo is look-owned rather than
      action-owned; its paired JointSets can be reseeded through
      `JointSet_AllocateAndSeedBuffers 0x14181E780` and committed through
      `JointSet_CommitJointTransformsAndReset 0x14181ECD0` without the expired
      PlayerWeaponIKAction. After restoring the confirmed `56C+283` rows,
      recompute C39 `19..30` world locators in parent order, solve the right arm
      to the stable two-sample Chloe hand-prop reference, derive the left wrist
      with stock `Compose(rightProp, Relative(leftProp,leftWrist))`, solve it,
      and commit both sets before Async consumes `Fg+0x100`. Admission is
      steady-only and excludes live `11EE475B`, P99, role A, ADS and movement.
      Runtime is earned only for one role-B Nadine process. Require build/READY,
      exact steady injection, `NADINE-MAZUR-LATE-IK applied`, and restore
      `lateIk=1`; only then interpret the visual pose. The agent launches but
      never supplies gameplay input.

      Runtime `aer` was packaging-inconclusive: the exact pair marker was
      followed by `NADINE-MAZUR-LATE-IK rejected stage=3` and `lateIk=0`, so
      the user's unchanged pose did not test the anchor. Static closure found
      a deterministic guard bug: the exact JointTree constructor
      `sub_141820AF0` writes state `1` at `+0x10`, whereas `aer` required `3`;
      `JointTree_BuildFromRootToNamedLeaves 0x141820E80` writes the chain count
      independently at `+0x12`. Keep the null `+0x20` lifetime guard because
      vtable slot `+0x20 -> 0x141821500 -> 0x14181F330` frees the work arrays
      and explicitly clears the pointer. Build `aes` changes only `3 -> 1`.
      Re-run exactly one role-B Nadine process and require exact pair,
      `LATE-IK applied`, and `lateIk=1` before judging the visual result.

      Runtime `aes` remained packaging-inconclusive: the exact pair ran but
      late IK again rejected at stage 3. Read-only memory of the live
      Player+0x4308 object proved both JointTrees have state1/count8/null+0x20,
      matching constructor/build/reset statics. Build `aet` only makes the
      existing seed gate observable per side as
      `failure/state/count/before/after/readable`; it changes no pose rule.
      Run one role-B Nadine process and use that row to choose busy-lifetime,
      allocator-result, or exception. Do not interpret visuals and do not
      send gameplay input.

      `aet` selected the exception branch exactly:
      `left=8/state1/count8/null/null/0`, while right was never called. The live
      index arrays and Fg pose/world/ValidBits pointers are coherent. Build
      `aeu` logs only exception code, instruction RVA, access kind and fault
      address around the same native call. Use that one row to identify the
      missing seed context; no visual judgment or gameplay input.

      `aeu` closed that context: PID `19232` faulted at `0x1415A0EE8` while
      dereferencing null FLS slot 7, the exact `Memory::CurrentAllocator` used by
      `Memory_AllocateFromCurrentAllocator 0x1415A0E90`.  JointSet reset
      independently resolves allocator ID `14` as its native transient-owner
      case, while source-asserted Push/Pop `0x1415A15F0/0x1415A1B80` write and
      restore that same FLS slot.  Build `aev` therefore scopes only the late
      seed/solve/commit under allocator ID 14 and validates
      expected/current/popped/restored identity.  Run one role-B Nadine copy;
      require build/READY, exact pair, `LATE-IK applied allocator=14/1/1/1`, and
      restore `lateIk=1`, with no stage3 allocator or stage4 seed reject.  Only
      then judge whether both hands and the muzzle match Chloe.  The agent may
      launch but must not control gameplay.

  Original `aeh` prediction required the combined READY marker,
      fresh Chloe right-prop CAPTURE, Nadine right-prop RELAY and same-postblend
      `baselineRestore=0`. All markers plus both hands and Chloe's muzzle-up
      angle confirm `H-right-anchor`; all markers plus the old low angle reject
      it; a raised muzzle with a detached support hand selects terminal-left
      ordering. The agent may launch both copies but must not control gameplay.

  Build `aew` CONFIRMED the exact stock 0.1-second Mazur handoff in both
      directions and REJECTED the missing-action/updater hypothesis. PID 4344
      covered draw handoff0 progress 0.0757..1 and put handoff1 progress
      0.0713..1, while the fixed authored frame remained active at full weight
      in every selected-back row. Build `aex` applies the exact stock smoothstep
      only to that correction at `0x141ED19BB`: draw weight 1->0, put weight
      0->1, with the 5 cm back offset scaled identically. Require boot aex and
      monotonic `correctionWeight` in both directions. Smooth visuals confirm
      Hweighted-frame; correct markers with a remaining arm snap select the
      authored arm/backflip transition. Grip, P99 and strap remain untouched.
      The agent launches one role-B Nadine copy and never sends gameplay input.

  Build `aex` then CONFIRMED the weapon-frame half and REJECTED it as the full
      transition repair. PID 18256 produced monotonic Draw correction weight
      `~1->0` and Put `~0->1`, but the user still saw magnetic hand/arm motion.
      At the exact Draw hand switch the next command was the mod's steady
      `56C57AD2+283966ED` pair at literal `1/1`; no stock-transition marker was
      present. Do not retune the back transform. The remaining Draw defect is
      the arm owner, while Put stays an independent unresolved arm path.

  Build `aey` rejected both of its proposed classifications. PID 9900 still
      injected the full `56C57AD2+283966ED` pair at `1/1` after the Draw handoff
      and emitted no post-Create `stock-transition` marker, so remove that
      re-resolution hot-path no-op. Cold rebind was more informative: exact
      targSpineProp/owner verification passed and `weapon+838` was nonnull, but
      `+848/+84C` was terminal `1.0/0`. The blend is completed, not frozen; the
      unconditional nonnull guard itself prevented the native selected-
      attachment updater from publishing `+130/+140`.

  Build `aez` is RUNTIME REJECTED as a cold-placement repair. PID 13388 reached
      exact `targSpineProp`, terminal `weapon+838` progress/rate `1/0`,
      `refresh=ok/1/1`, TLS `1/1`, changed all eight local words, yet the user
      still saw the rifle protruding. The object is exact `LongGunJiggle`
      (vtable RVA `2A9AF48`), so do not repeat the completed-updater call alone.

  Build `afa` was admission-inconclusive, not a visual rejection. PID 4692
      reached exact cold Nadine/Mazur/targSpineProp but reported
      `weapon+898=C9`, `weapon+8F4=1`, `refresh=long-gun-jiggle-active` and
      `jiggleReset=0`; the implementation had incorrectly expected the inactive
      predicate before reproducing its finalizer. Static writer census now
      closes the missing state: constructor/holster entry write `+8F4=1`, while
      exact `transition-to-spine-anim` finalizer `0x141089E70` clears `+8F4` at
      `0x14108A259` after verifying `targSpineProp`. Per-frame
      `ProcessWeaponBase_Update 0x141500A79` consumes that byte before exact
      LongGunJiggle reset vslot `+28`.

  Build `afb` is RUNTIME REJECTED as a cold-placement repair. PID 12044 reached
      exact `rightHand -> targSpineProp`, cleared `weapon+8F4` from one to zero,
      reset the enabled terminal LongGunJiggle (`jiggleReset=1/1->0`, changed
      hash), ran the scoped selected updater (`refresh=ok/1/1`, TLS `1/1`) and
      changed all eight local words (`AFDA050B->81842671`). The user still saw
      the rifle protruding initially. Do not retry the endpoint, finalizer byte,
      LongGunJiggle reset or selected updater separately or in combination.

  Build `afc` is RUNTIME REJECTED. PID 16728 created the exact early
      `targSpineProp` alias (`39->40`, joint16, exact1), but emitted no initial
      Mazur back-frame/rebind row and the user still saw the rifle protruding.
      The cold descriptor explicitly requests `rightHand 0x61A48E01`; table
      presence cannot change that id. Correct the old `GiveWeapon -> +0x3D0`
      claim too: the call at `0x1410A5CDF` is guarded by `v218==5`, while the
      net-mode Mazur class remains 16. Do not retry `Hlate-table`.

  Build `afd` is RUNTIME REJECTED / admission-inconclusive. PID 9068 loaded the
      exact build, installed `0x141103B7E` and created the exact early spine
      alias, but emitted zero `NADINE-MAZUR-FACTORY-HOLSTER` rows: the campaign
      Mazur existed before injection or bypassed that factory seam. More
      importantly, the untouched stock object later reached selected index39,
      `targSpineProp`, handoff1, state0 and exact accepted binding `57036E96`,
      while the user still saw the cold rifle protruding. That independently
      rejects `Hmissing-cold-stock-rebind`; do not retry the factory relay.

  Build `aff` reached combined phase-publisher READY in PID16904, but emitted
      zero local-Mazur rebind and zero phase40 publication markers. The user's
      unchanged initial protrusion is therefore admission-inconclusive, not a
      visual rejection. The exact code mismatch was `rightHand`-only producer
      admission while the live cold object was already logically selected on
      canonical index39/targSpineProp.

  Build `afg` corrects only that admission and is STATIC/IMPLEMENTED, not yet
      launched. Three independent evidence classes still converge: rejected
      local-only updater/runtime (`aez/afb`), exact phase-`0x40` compose and E8
      publish chain (`141A8F9D0 -> 141A8FC00 -> 1418C55D0 -> 1418C54D0`), and
      renderer consumption of `Fg+1C0` into all driven instances. The one-shot
      producer now admits canonical rightHand OR an already-selected canonical
      targSpineProp, calls the same stock holster rebind, and requires exact
      accepted binding `57036E96` before arming. The bridge then keys stable Fg
      and generations, re-resolves the live weapon on the phase worker, repeats
      the authored local correction, composes current parent+local, and
      substitutes only that exact stock publish argument.
      No game launch is authorized until the Release artifact, IDB and commit
      are complete. Eventual single-copy prediction: exact READY plus
      `NADINE-LOCAL-MAZUR-REBIND mode=existing-spine-prop call=1 verified=1
      exactBinding=1 strapVerified=1
      phasePublishArmed=1`, then one `NADINE-COLD-MAZUR-PHASE40
      status=published refresh=ok/1/1` with a changed world hash. Those markers
      plus unchanged lower protrusion reject the phase-publication hypothesis
      and select a different visible drawable; missing markers are only
      packaging/admission. Draw/Put motion and the sling remain separate.

  Build `afg` is now RUNTIME ADMISSION-INCONCLUSIVE and must not be rerun.
      PID3184 loaded the exact build and combined READY, then emitted thirteen
      exact `existing-spine-prop` rebind rows with `verified=1`, binding
      `57036E96`, `strapVerified=1` and `phasePublishArmed=1`, but zero target
      `NADINE-COLD-MAZUR-PHASE40` rows. The unchanged lower protrusion therefore
      does not reject the corrected matrix; it rejects the assumption that the
      cold weapon is guaranteed to reach that conditional worker seam.

  Build `afh` is RUNTIME ADMISSION-INCONCLUSIVE at the final renderer consumer.
      PID10764 loaded exact `afh` and READY, and the stable weapon reached
      `refresh=ok/1/1`, but target build rejected `parent-generation` at
      locomotion log 95921/95923. It produced zero staged targets, zero applied
      rows and zero visible B3F9 substitutions, so the user's unchanged rifle
      cannot be interpreted as a visual rejection. The table-only predicate was
      stricter than stock and is REJECTED.

  Build `afi` is STATIC REJECTED before runtime. Although the stock table-or-
      cached-parent rule was exact, repeatedly rebuilding from `weapon+130`
      would rejuvenate a cached selected-world result and could pin it while the
      Player/spine moves. Direct `0x141ED19BB` publication is also pre-jiggle.
      Do not launch or restore that producer.

  Build `afj` is RUNTIME REJECTED / NO-OP at the same final renderer consumer. The
      producer is now the exact post-`ProcessWeapon +0x7B8` boundary: the nested
      `0x141ED19BB` call supplies a same-thread full-weight/depth witness, stock
      then applies LongGunJiggle and writes final local `+130/+140`, and the
      wrapper double-snapshots cached parent `+110/+120` plus that final local
      before composing the world. Exact Active caller closure is
      `0x14150DBF0 -> ProcessWeaponBase_Update 0x141500880 ->
      UpdateLocalAttachmentBlend 0x141ED4390 -> +0x7B8`; cold Fg flags
      `0x2004C0` make this a natural fresh cadence, while the Animating path
      reaches the same vslot. PID10764 independently showed repeated changing
      full-weight/depth corrected resolver outputs.

      Admission is role-B/owner0/screen0 legacy Nadine, exact Mazur,
      canonical targSpineProp/joint, Player parent, current<0/state0, exact
      attachment/runtime/Fg generations, nonzero local-write gate and exactly
      one witness. It resolves the current inventory Mazur before and after the
      build from the raw 23-entry weak table (no side-effecting GetWeaponBySlot
      vcall), requires the accepted `weapon+4F0` back binding twice, and requires
      the observed cold phase bits (`0x40` set, async `0x10` clear).
      Partial/missing correction clears the target; valid publications expire
      after 100 ms and remain publication-linearized at all three final getter
      relays. Both the 19BB relay and +0x7B8 hook require suspended installation,
      exact readback and restored page protection with fail-closed rollback;
      combined READY also requires all three render callsites. Cold refresh calls
      the verified wrapper rather than rejecting the now-hooked vslot.

      The one-copy prediction required boot `2026-08-22afj`, the installed
      post-selected source, three-site render relay and combined READY. A full
      `NADINE-MAZUR-NATIVE-BACK` correction must be followed by a staged target
      with `worldSource=post-selected-final`, accepted binding hash, weight/depth
      `1/1`, and matching nonzero parent/local/world hashes. The same generation
      and publication must reach `caller=0x181B3F9` with matching source hashes,
      `matrixHash=stock->corrected changed=1` and no stale/race rejection.
      Those exact markers plus the old protrusion reject final-consumer
      sufficiency and select another drawable/wrong authored source. Missing
      target/witness, a bounded reject, no B3F9 hit or changed=0 remains
      admission/packaging/math. Draw/Put must clear the target; P99, grip,
      transitions and sling remain separate. Launch only one role-B Nadine copy
      after source/docs/IDB are committed and pushed; gameplay input is user-only.

      Release `afj` is built from the reviewed source (`801280` bytes, SHA-256
      `E46F190F5F6413A4F75C46C2BC4055BD4A7FEC9948EFA78A5616004F972FFAE0`).
      The single IDB was saved headlessly (`1789` function labels, `61` data
      names, one exact function type, `save_database returned True`) and restarted
      MCP readback verifies the post-selected `uint8` ABI, Active update name,
      inventory weak-table warning and authored-parent table name. No game was
      launched during closure.

      PID19404 subsequently executed that discriminator completely. Publications
      2..12 all staged with accepted binding/full weight/depth and each reached
      visible `caller=0x181B3F9`, but every stock-to-corrected hash was identical
      (`changed=0`, locomotion log 96101--96153); Draw then cleared the target.
      The user's unchanged lower protrusion therefore rejects the whole
      post-selected/root-world frontier, not merely its admission. Do not retry
      `aff..afj` or tune the same world matrix.

      Direct raw Chloe clip copying is also already STATIC REJECTED: offline
      retail evaluation of `5F913F74+7D11D4FD` leaves the hand-prop endpoints
      over half a metre apart, whereas live Chloe closes them only after the
      stock additive/reference/WeaponIK pipeline. The new P0 is the cold weapon
      drawable below its now-proven-correct Fg root: trace its internal
      palette/skinning/child transform and the exact state rebuilt by Draw->Put.
      No new launch is earned until that owner is closed statically.

      The first below-root pass now proves that `sniper-rifle-bolt.pak` is a
      22-joint skinned drawable, not a rigid root-only mesh. Twelve partitions
      across three LODs are weighted to weapon joints `0,2,4,5,9,15`; the main
      body is dominated by `0/4/5`. Its only weapon-skeleton animations are
      exact action clips `6B990C0A fire--gun`, `411D111F rechamber--gun` and
      `D9D39F60 reload-mid--gun`; there is no weapon idle/draw/put/holster clip
      to copy from Chloe. The next bounded work is a read-only 22-row
      `Fg+0x230` census under the native palette lock, comparing the exact cold
      weapon generation with the same generation after Draw->Put. A delta in
      main-body rows `0/4/5` selects the palette lifecycle; identical rows
      reject it and move the frontier to child/submission state. Do not mutate
      or launch from this STATIC result alone.

      Build `afk` now implements that exact read-only discriminator and disables
      the rejected `afj` render-matrix mutation. A game-thread token admits only
      role-B retained-owner-0 legacy Nadine, the raw exact controller inventory
      Mazur, reciprocal ProcessWeapon Fg/runtime owner and 22-joint skeleton.
      The native `0x14181AB33` upload memcpy runs first; under its existing
      `Fg+0x220` lock the wrapper only copies `22*48` bytes to private arrays and
      hashes all rows, with aggregates for main body `0/4/5` and action joints
      `2/9/15`. Lifecycle is `cold` before any Draw and `post-put` only after the
      same `(Fg,runtime id)` has been observed drawn and returns to
      `current<0/draw=0`.

      Written prediction for the one-copy test: exact finite cold/post-Put
      samples in the same generation plus a persistent `0/4/5` delta select
      Hweapon-palette-lifecycle; all 22 rows identical reject it and select the
      child/submission frontier; only `2/9/15` changing is action-only. Missing
      READY/phase, generation turnover, `exact=0`, `finite=0`, or unequal native
      current/upload hashes is admission-inconclusive. Launch only one role-B
      Nadine after fresh Release build, commit/push and clean-tree verification;
      user performs one Draw->Put and all gameplay input.

      Release `afk`: `788992` bytes, SHA-256
      `ADD8EB4E1862FF98E9B62B7E8830E1EB95FD6C9ABF2C58DC873CA06D31F34EFD`;
      source `14:16:09`, DLL `14:16:30`. Build and marker readback pass; the
      palette relay's protection-failure path is readiness-false. No new IDB
      name/type was needed because this reuses the already-saved consumer seam.

      PID4332 completed the exact `afk` exam. Same `Fg=0x11191D5300`, runtime
      `520`, generation `1`, 22-joint skeleton: twelve cold and twelve post-Put
      samples were all exact/finite, native current==upload, and byte-identical
      in every row. Full hash `9C772250`, main `0/4/5=9DCB4BDE`, action
      `2/9/15=448B15F5`, identity mask `281` never changed. Thus
      Hweapon-palette-lifecycle and Haction-only-delta are RUNTIME REJECTED;
      Hpalette-identical is CONFIRMED. Park/disable `afk` and do not copy/tune
      the weapon palette or Chloe Player clips again.

      New exact P0: enumerate the cold versus post-Put child drawable/Fg and
      driven FgInstance sets, then trace each visible mesh partition's
      SkinningDeformer/base-pose/palette binding and submission membership. The
      cause must be below or beside the now-identical root and 22-row palette:
      alternate child, deformer/bind resource, or renderer submission state.

      STATIC CORRECTION / TEST-READY (`afm`): `afj` compared the cold
      post-selected world with the same cold stock getter; it did not compare a
      lifecycle-normalized root before Draw and after Put. `afk` compared those
      lifecycle phases only for the 22-row palette. Build `afm` closes that
      exact gap without mutation. After stock
      `FgDrawManager_PrepareInstanceForRendering 0x14181A9B0`, atomic relays at
      normal worker return `0x14181B92A` and synchronous return `0x14197DC59`
      snapshot the same exact ProcessWeapon/Fg generation: parent/final-local/
      binding/selected tuple, DrawControl mesh layout, all driven FgInstance
      identities and bone pointers, MeshInstance/FgGeometry, and active versus
      published InstanceJointData pointers. The stock call always runs first.

      Written discriminator: a stable relative/final-local or binding delta
      selects Hroot-local-lifecycle; equal root with changed instance/geometry/
      MeshInstance/JointData identities selects Hinstance-relink; equal
      identities with changed state/gates/selected or latched bone pointers
      selects Hsubmission/deformer-state; complete equality rejects the whole
      measured frontier. Raw absolute world drift alone is ignored. Missing
      READY, no normal `caller=0x181B92A`, generation turnover, missing post-Put,
      exact=0 or phase-internal instability is inconclusive. Run one role-B
      Nadine copy only after fresh Release, commit/push and clean-tree closure;
      user alone performs one Draw->Put and all gameplay input.

      Final Release `afm`: `797184` bytes, SHA-256
      `28B169E4DFC6277AC38CAC71B4BD6F42F427265162B00FB9AA19D052A732DFF1`;
      source `15:29:03`, object/DLL `15:29:19`. PE marker readback, Release build
      and diff check pass. No new IDB label/type is needed because this reuses
      the already-saved FgDraw/FgInstance/deformer boundaries.

      RUNTIME REJECTED (`afm`, PID3824): a real user-driven Draw->Put did not
      repair the visible rifle. The exact same Fg/runtime, three driven pool
      entries 1059/1060/1061, FgGeometry, MeshInstances, InstanceJointData,
      DrawControl layout, selected record, writer gate and LongGunJiggle
      topology persist across cold and post-Put. The binding hash change
      `57036E96 -> 5D728985` is only 2--3 ULP endpoint rounding, not a different
      transform. Hinstance-relink, Hsubmission/deformer-state and the old
      Draw->Put-repairs premise are rejected. The remaining P0 is the source
      frame: current code uses Chloe's static marker under Nadine's live spined,
      not Chloe's complete final holster pose.

      STATIC / IMPLEMENTED (`afn`): relay the same-process retained Chloe's
      complete post-selected/post-jiggle Mazur world normalized to her complete
      Player root. The exact +0x7B8 wrapper calls stock first, captures only
      role-B owner1 Chloe/Mazur/current<0/draw0/targSpineProp, computes
      `relative = MakeRelative(ChloeRoot, ChloeWeaponWorld)`, and publishes a
      locked <=100-ms donor without writing Chloe. The exact local owner0 Nadine
      wrapper computes `desired = NadineRoot * relative` and stages it through
      the already-proved three final GetRenderWorldMatrix consumers. Existing
      authored correction is only the same-pass admission witness; the Chloe
      body-relative world is the rendered result. Role/look/art/inventory/
      selected-record/runtime/Fg-phase drift, Draw/Put or stale source are
      stock fallback and clear/expire the publication.

      Written one-copy prediction: READY + `CHLOE-MAZUR-HOLSTER-DONOR` + target
      staged with the same donor sequence + visible `caller=0x181B3F9 changed=1`
      must move the rifle to Chloe's body-relative back position/orientation.
      READY with no donor and `Chloe-donor-missing` is admission-only. All exact
      markers plus unchanged pixels rejects H-Chloe-body-relative; markers plus
      a different wrong placement selects root/body-proportion semantics. User
      alone performs gameplay input after fresh Release, commit/push and clean
      tree. No new IDB symbol is introduced.

      Final Release `afn`: `807424` bytes, SHA-256
      `186BEAE5B5B3F8C04F8217E946D042247922D46DAB54388E80D015188E998950`;
      source `15:48:43`, object/DLL `15:48:55`. PE readback contains the exact
      boot, combined READY, donor, missing-donor and Chloe-body-relative apply
      markers; Release build and diff check pass.

      RUNTIME ADMISSION-INCONCLUSIVE (`afn`, PID5236): boot/READY were exact,
      but 143 process rows contained zero live-Chloe donor, zero staged target
      and zero render apply. Nine old-witness rejects had empty records,
      sentinel selected joint and zero weight/depth. The user's unchanged
      vertical rifle was therefore stock. Do not reuse the live-owner1/witness
      admission and do not call its pixels a rejection of body-relative pose.

      STATIC / IMPLEMENTED (`afo`): eliminate both missing inputs. New offline
      audit `scripts/chloe_mazur_holster_pose_audit.py` evaluates retail
      `5F913F74` and composes exact Chloe joints 25..29 plus the exact
      `targSpineProp` marker from the `chloe-base` inverse-bind root. Samples
      0/335 close byte-exact at root-relative DWORDs
      `3D627B2F/3FA9294C/BDC35992/3F800000/BF058597/3F1D1AF9/BE88E0D5/BF076D88`;
      the bind-marker convention independently lands 12.291 mm / 0.000125 deg
      from skeleton `spine_prop_attachment`. The local Nadine post-+7B8 path
      now rebases this fixed retail endpoint under her current Player root and
      reuses the proven final three-getter relay. It needs neither owner1 Chloe
      nor the old nested correction witness.

      Written one-copy prediction: READY, then target `staged` with
      `worldSource=Chloe-retail-idle-root-relative donor=1:0:0`, then visible
      `caller=0x181B3F9 changed=1` must move the cold rifle away from the current
      vertical/protruding placement to the canonical Chloe loop endpoint.
      Missing markers are admission only; exact changed markers plus unchanged
      pixels reject the rendered owner; exact changed markers plus another bad
      placement reject this root semantic. User alone supplies gameplay input.

      Final Release `afo`: `803328` bytes, SHA-256
      `D8051018B4399E96DF12FE218222FBC82A37E5C12D0736D89FE70CA8A779372A`;
      source `16:16:40`, object/DLL `16:17:14`. Offline audit, PE marker
      readback, Release build and diff check pass.

      RUNTIME REJECTED (`afo`, PID1168): packaging and visible delivery were
      exact, with 326 staged publications, 1300 changed applies and 975 changed
      applies at visible caller return `0x181B3F9`.  The user's screenshot shows
      the resulting Mazur vertical beside Nadine rather than attached to her
      back.  Reject the retail Chloe root-relative endpoint itself; do not tune
      or reuse it as a weapon world transform.

      STATIC / IMPLEMENTED (`afp`): restore only the back-attachment topology
      from user-accepted commit `82e7b9e`.  Disable the later early
      `targSpineProp` alias, weighted handoff, transition census and all three
      final renderer substitutions.  Restore retained-player admission for the
      old authored `0x141ED19BB` back frame and let the one-shot rifle rebind run
      without renderer READY.  Preserve all unrelated grip/network/P99 work.

      Written one-copy prediction: `afp` boot and the old authored back-frame
      install marker appear; retail READY/staged/applied markers are absent; the
      initial Mazur returns to the `82e7b9e` back attachment instead of the
      `afo` vertical placement.  Exact rebind/correction markers plus the same
      pixels reject restoration sufficiency; missing them is admission-only.
      User alone supplies gameplay input.

      Final Release `afp`: `771072` bytes, SHA-256
      `3370080403DBD110F0E6DEECCB45A95BB67FD99FD53B848AC07371EF0761973A`;
      source UTC `13:33:36`, object/DLL UTC `13:34:00`. PE readback contains `afp` and
      the old authored-back marker but no `afo` boot or retail READY marker;
      Release build and diff check pass. No new IDB symbol is required.

      RUNTIME CONFIRMED (`afp`, PID13784): exact Mazur rebind
      `rightHand 61A48E01 -> targSpineProp AD047A` completed with
      `verified=1/strapVerified=1`, the old authored back correction ran at full
      weight/depth, and the user confirmed that the initial rifle is now
      attached acceptably to Nadine's back. Lock this topology; Draw/Put arms,
      strap visibility and steady grip remain independent open items.

      STATIC / IMPLEMENTED (`afq`): preserve the confirmed `afp` cold-back
      topology, but replace the authored arm pair's literal `1/1` step with the
      exact stock `ProcessWeapon+0x640/+0x644` handoff clock. Draw uses
      `smoothstep(progress)` for both complementary `56C57AD2/283966ED`
      overlays; Put uses `1-smoothstep(progress)`. Re-enable the already-proved
      weighted item back-frame handoff, skip zero weight, keep late IK
      steady-only, and revalidate exact role/player/Fg/weapon/generation/
      controller/attachment/duration/progress before postblend restore.

      Written one-copy prediction: `afq` boot and authored-pair READY, unchanged
      good initial back placement, multiple `source=draw-handoff` rows with
      increasing fractional weights and matching restores, then multiple
      `source=put-handoff` rows with decreasing fractional weights and matching
      restores. Those markers plus removal of the instantaneous Draw/Put snap
      confirm H-arm-weight-step even if the final grip angle remains wrong.
      Exact markers plus the same snap reject it; missing either phase is
      admission/packaging. User alone supplies gameplay input.

      Fresh Release: `777216` bytes, SHA-256
      `7BC689CC6BD7CBD47967BEB642CFA5F71458DDFC24A4B8E6C52D51AAC3A8F44C`;
      source/object/DLL UTC `14:04:48.758 / 14:05:18.535 / 14:05:18.629`.
      PE readback contains the `2026-08-25afq` boot and native Draw/Put
      weighting READY strings. No new IDB symbol was introduced.

      RUNTIME REJECTED (`afq`, PID 4388): the arm subpath itself executed the
      full predicted increasing Draw and decreasing Put weight sequences, each
      with matching postblend restore, but the initial `afp` back placement
      regressed. The item-weight helper conflated cold state 0 with terminal
      Draw because both can expose duration0/progress1/handoffFlag0.

      STATIC / IMPLEMENTED (`afr`): gate the duration-zero item weight only on
      exact controller state1/current<0. Cold state0 and any unreadable
      controller stay at the confirmed full `afp` correction; active arm and
      item smoothstep paths remain unchanged. The fresh Release is `777728`
      bytes, SHA-256
      `B4CEAA00315B6A0D8F1DDD319922375FC3FBA1CF415A8BBB8DBCB36CBE60CB29`.
      Do not launch automatically.

      RUNTIME REJECTED (`afr`, PID 5792): the state predicate is correct but the
      cold producer is not deterministic. Initial rebind and deferred +0x7B8
      refresh reported `ok/1/1 changed=ff`, yet no authored-back row occurred
      before Draw and the user still saw the broken cold rifle. After Draw->Put,
      the same run produced repeated state0/current-1 full-weight/depth authored
      rows, proving the remaining defect is missing cold producer reach rather
      than another duration-zero weight error.

      STATIC / IMPLEMENTED (`afs`): a raw +0x130 delta no longer completes the
      forced Mazur refresh. A same-thread witness around its exact +0x7B8 call is
      completed only by the existing nested 0x141ED19BB relay for the same
      weapon with stock result, authored weight and depth all valid/full.
      Otherwise the request remains `authored-correction-pending`. A prior pass
      may already have finalized LongGunJiggle, so the exact terminal
      enabled0/request0 state is now admitted for another resolver pass instead
      of being dropped as `long-gun-jiggle-not-enabled`.

      Prediction for the next permitted one-copy exam: exact `afs` boot; before
      any Draw, either bounded pending rows or a finished refresh only with
      `authored=1/1/1`, preceded by a full state0/current-1
      `NADINE-MAZUR-NATIVE-BACK` row and the accepted initial placement. A
      finished authored0 row, no pre-Draw authored row, or the same broken
      pixels rejects this producer. Do not launch until fresh Release packaging,
      commit/push and explicit user acceptance.

      Release `afs`: `778752` bytes, SHA-256
      `624FA4FAB965D364E2D59A7CA93285D4EE1B9D081E0CB6F3DBDA5E16D587E436`;
      source/object/DLL UTC `15:54:20 / 15:55:24 / 15:55:24`. PE readback
      contains the exact `afs` boot and authored pending/result markers. No new
      IDB symbol was introduced.

      RUNTIME ADMISSION-INCONCLUSIVE (`afs`, PID 11076): exact role-B/READY and
      accepted `rightHand -> AD047A` rebind occurred, but zero full authored
      back rows preceded Draw. The Mazur pending refresh aborted **22** times as
      `scope-drift` while Player/controller/weapon/records raw addresses moved.
      Later Draw/Put fractional markers remained intact. This does not reject
      the authored pose; it proves the raw-pointer pending lifetime is wrong.

      STATIC / IMPLEMENTED (`aft`): pending refresh now retains stable
      Player/weapon runtime ids plus exact weapon Fg. Each retry reacquires the
      current retained Player, exact controller and unique raw inventory art,
      then requires the same ids/Fg and revalidates parent, AD047A and the new
      records array before replacing diagnostic pointers. This reuses the
      corrected `aav` relocation contract and the side-effect-free controller
      weak-table resolver. Different generations/endpoints remain fail-closed.

      Prediction: exact `aft`; a relocated request may finish with
      `relocations>0`, but before Draw it must first emit full state0/current-1
      `NADINE-MAZUR-NATIVE-BACK` and then `refresh=ok/1/1 authored=1/1/1`.
      Exact completion plus unchanged pixels rejects authored-frame
      sufficiency; identity abort/missing completion is admission only. Fresh
      Release is `779776` bytes, SHA-256
      `57056289273330584B70CF8826EEFB38E4EA4B13B16E6E724760BCDAB06EC540`,
      source/object/DLL UTC `16:34:34.825 / 16:34:46.794 / 16:34:46.887`.
      PE marker/readback and Release build pass; no new IDB label is needed.

      RUNTIME ADMISSION-INCONCLUSIVE (`aft`, PID19492): the relocation-only
      prediction was not reached. Nineteen verified AD047A rebinds survived
      raw allocation churn without the old `scope-drift`, but the final exact
      request timed out after 600 retries with
      `authored-correction-pending/1/1`, `authored=0/0/0` and zero native-back
      or renderer rows. IDA proves `changed=fe` at +0x130 can occur here only
      after 0x141ED1780 succeeds and calls patched 0x141ED19BB; therefore the
      relay ran but passed stock input because `mazur_corrected` stayed false.
      The unchanged pixels are admission-inconclusive, not a visual rejection.

      STATIC / IMPLEMENTED (`afu`): restore the exact accepted `afp` decision
      by disabling only item-side `kEnableLegacyNadineMazurWeightedBackHandoff`.
      The authored resolver/topology is unchanged from afp; the added weight
      helper is the unique new predicate between its resolved frame and stock
      0x141ED0A70. Keep the separate arm-pair/late-IK work enabled and park
      Draw/Put item smoothing rather than gate the accepted cold attachment.

      Prediction: exact `afu`; before Draw, a full
      `NADINE-MAZUR-NATIVE-BACK handoff=0/0 correctionWeight=1 depth=1` must
      precede `refresh=ok/1/1 authored=1/1/1`, and the visible Mazur must return
      to the user-accepted afp/82e7b9e upper-back placement. Exact markers plus
      unchanged pixels reject stock publication sufficiency; absent markers
      remain admission/package failure. This exam says nothing yet about
      transition smoothness, strap visibility or the in-hand grip. Fresh
      Release is `779264` bytes, SHA-256
      `2419409DCF8089012CCD7C25F4B93BDCF5DE88BA0E3238E68FE0B9AA6F759485`,
      source/object/DLL UTC `16:46:40.564 / 16:47:55.642 / 16:47:55.778`.
      PE marker/readback and Release build pass; no new IDB label is needed.

      RUNTIME CONFIRMED (`afu`, PID5376): the cold Mazur attachment prediction
      passed. Exact role-B Nadine rebind changed rightHand `16/61A48E01` to
      back `39/AD047A`; the first native-back row used donor
      `30:18/4F62CD9D`, `handoff=0/0`, `correctionWeight=1`, `depth=1`, and the
      deferred request finished after two attempts with
      `refresh=ok/1/1 authored=1/1/1`. All 14 rebinds had matching full
      native-back rows, and the user confirmed the initial placement is fixed.
      The parked renderer path remained unused, proving stock downstream
      publication is sufficient for this cold path. Lock the item-side weighted
      handoff flag off; keep Draw/Put smoothing, grip and strap explicitly open.
      The exact rejected subguard inside the old weight helper remains unknown.

      STATIC / IMPLEMENTED (`afv`): keep the confirmed `afu` cold correction
      unconditional, but add a separate local-role-B transition-only item blend
      at the existing `0x141ED19BB` seam. Exact active duration bits
      `0x3DCCCCCD` are the stock handoff ticket: Draw item weight is
      `1-smoothstep`, Put item weight is `smoothstep`, and the five-centimetre
      depth uses the same scalar. The already-executed arm pair remains exactly
      complementary. Controller state is diagnostic for active clocks because
      historical exact clocks can straddle state transitions.

      The old cold failure cannot recur through this design. Duration-zero
      stable/cold takes the direct full `afu` frame; malformed active metadata is
      a tri-state stock passthrough with a bounded `BACK-BLEND-REJECT` reason,
      never a global correction reject or full-frame mid-blend fallback. A
      process-wide ticket keyed by weapon/owner and both generations is armed
      only by a successful exact active Draw and consumed once at its terminal
      duration-zero callback. This distinguishes that callback from the real
      pre-seed state-1 tuple left by a prior Put. Active Put clears stale tickets.

      Written one-copy prediction: exact `afv` boot and transition-only install;
      unchanged pre-Draw full native-back/depth row plus deferred
      `authored=1/1/1`; Draw item `1->0` including ticketed terminal zero opposite
      arm `0->1`; Put item `0->1` opposite arm `1->0`; final full accepted back
      placement. Any blend rejection, terminal full row after successful active
      Draw, missing bucket or cold regression is packaging/admission failure.
      Exact complementary traces plus a remaining hand snap move the frontier to
      authored transition content/later IK. Grip and strap remain out of scope;
      the user alone performs gameplay input.

      Final independent source review found no remaining cold/continuity
      blocker. Fresh Release is `782848` bytes, SHA-256
      `AF6B02E938290F2953C065C5F2CEF53231F10329C1FEB989C7C08BCA0DE47A5E`,
      source/object/DLL UTC `17:27:50.986 / 17:30:00.522 / 17:30:00.615`.
      PE readback contains the exact `afv`, transition-only, reject and
      combined-READY markers; no new IDB symbol is required.

      RUNTIME REJECTED (`afv`, PID9308): cold and the active item clock both
      executed, with zero blend rejects, exceptions, timeouts or scope drift.
      Draw item weights reached `0.990883 -> 0.00782335` opposite the arm ramp;
      Put reached `0.0128325 -> 0.902904`. The failure was our terminal
      lifecycle. One Draw relocated the same logical weapon from
      `0x10e0ca1380` to `0x10e0ca0d00` between the last active and first
      duration-zero callback, so the raw-pointer ticket missed and weight jumped
      to `1`. A second Draw reached terminal weight `0`, then a repeated
      duration-zero callback consumed the now-empty one-shot ticket and also
      jumped to `1`. The user's unchanged visual therefore rejects `afv` as a
      complete continuity repair without rejecting active interpolation.

      Next seam: logical owner/weapon-generation identity across raw relocation,
      plus a terminal-zero hold across every repeated duration-zero/back-selector
      callback. Clear only on actual selector/phase exit, Put/stable cancellation
      or generation drift; preserve the confirmed full cold `afu` endpoint.

      STATIC / IMPLEMENTED (`afw`): replace the rejected raw one-shot ticket
      with an SRW phase latch keyed by `{FgAnimData, playerRuntimeId,
      weaponRuntimeId}`. The key is re-proved from the live inventory weak
      record, ProcessWeapon parent, `weapon+0x1A8`, Fg retained weak owner and
      Fg `+0x1B0`; raw owner/weapon/records pointers are telemetry only. Active
      Draw reserves a serial and commits it only after nonzero stock return plus
      post-call identity/clock/selector revalidation, so a concurrent Put or
      rightHand clear cannot be resurrected.

      `PlayerWeaponController_RelocateOwnerPointers 0x14109DC40` and
      `ProcessWeapon_RelocateOwnedPointers 0x141511470` only rebase pointers and
      do not reset `+0x640/+0x644`; a mid-active move therefore remains active
      instead of masquerading as terminal.

      Matching first terminal enters persistent `TerminalHold`; every repeated
      duration-zero/state1/back callback remains item weight zero until the
      common 19BB hook observes the selector leave AD047A. Same-raw admission
      requires current progress `.999`; the first callback on an independently
      relocated wrapper may follow the stock terminal clock clear and expose
      progress zero. It is authorized by the committed active Draw plus
      unchanged Fg/runtime IDs, without an empirical progress threshold.
      Exact `WeaponAttachment_SelectExistingOwnerPoint 0x141ED0580` writes
      selector `+0x688` at `0x141ED06EF`, and
      `ResolveSelectedAttachmentTransform 0x141ED1780` reaches
      19BB for rightHand too, providing the clear boundary without a new hook.
      Put, stable/cancel, nonexact duration and logical identity drift also
      clear. Cold/pre-seed with no committed latch stays full AFU.

      Written prediction: `afw`/logical-terminal install; unchanged cold full
      endpoint; complementary active Draw; relocated terminal row
      `rawMoved=1,lastActive=<committed sample>,weight=0`; at least a second
      terminal ordinal also stays zero, followed by explicit
      `selector-rightHand` clear. No readable state1/duration0 full AFU is
      allowed between committed Draw and selector exit; an unreadable first
      terminal is admission failure, while the true pre-seed state1/duration0
      row stays full. Put remains monotonic to the accepted back
      endpoint. Exact evidence plus residual snap moves to action content/stock
      duration. Frozen Release succeeds with `/O2 /Ob2 /DNDEBUG`:
      source/object/DLL UTC `19:15:35.574 / 19:15:48.609 / 19:15:48.706`, DLL
      `788480` bytes, SHA-256
      `CEBA5F6138CF3708F80CF53A5E93B0D3B17BA2758BC1A94827E7092AE54AF8CC`.
      PE readback contains exact afw/install/latch/native-back/combined-READY
      markers.

      RUNTIME REJECTED (`afw`, PID18356): the logical terminal exam passed but
      the user still saw the same magnetic motion. Draw item weights were
      monotonic `0.985623 -> 0.00622833`, complementary arm restores were
      `0.0143766 -> 0.993772`, terminal item weight stayed zero through ordinals
      `1/2/29`, survived raw relocation at `41/42`, then cleared on rightHand
      after 51 callbacks. Cold stayed `authored=1/1/1`; zero blend rejects or
      exceptions. Put item weights rose `0.0130022 -> 0.907431`, but its final
      arm injection `0.0925692` lost the matching postblend restore at the state
      change. This confirms the latch and rejects terminal/item-scalar absence
      as the Draw owner. Next trace the complete time-varying
      `s_weapon-draw`/holster transition through postblend/Async and close the
      Put-tail restore edge. `ael` already rejected replaying one late Draw
      sample; do not pick another phase, retime `+0x640`, or launch another
      endpoint blend without new static ownership proof.

      STATIC / IMPLEMENTED (`afx`): run the bounded native WeaponPartial visual
      exam with the synthetic pair and late steady IK both disabled. Static
      ownership is closed by three independent classes: the exact
      `0x141096150/0x141096BB0 -> 0x14108C2D0 -> E2B990B9` request graph; the
      seven/eleven-frame grab/draw/holster assets owning Player joints 21..32;
      and the Create -> Perform -> Player+0x408 -> Async order proving that AFW
      appended a one-frame pair after stock action commands and then restored it
      after WeaponIK/HolsterIK. Existing transition-lifecycle and fade-trace
      observers are read-only and runtime-proven; they identify the native
      state/phase while the user's pixels use unmodified stock arms.

      Written prediction: one role-B Nadine copy must boot `afx`, install both
      WeaponPartial observers and emit combined native-transition READY,
      preserve the confirmed cold
      `refresh=ok/1/1 ... authored=1/1/1`, emit no authored-pair restore/late-IK
      marker, and show multi-sample grab/draw/holster phase/fade rows for one
      Draw and one Put. Smooth elbow/hand motion confirms our late synthetic overwrite and
      earns a future steady underlay below WeaponPartial. The same snap with
      exact native samples rejects overwrite sufficiency and selects native
      retarget/action content. Missing markers are admission failure. The
      steady grip may look stock-broken in this diagnostic. The item-side AFW
      transition correction remains enabled, so judge elbow/hand continuity,
      not the rifle transform alone; grip and strap stay open. Fresh Release:
      `/O2 /Ob2 /DNDEBUG`, source/object/DLL UTC
      `20:08:26.228 / 20:09:26.243 / 20:09:26.334`, DLL `768512`, SHA-256
      `1D68B36170B1823BA2EBDABE9C9AFD4A192CD20F98D18B88A041DA270759F8BC`.
      IDB persistence is closed: `1789/61/1` labels/data/types applied to the
      single `ida/tll.i64`, `save_database returned True`, and restarted-MCP
      readback confirms `0x14189EA60`, `0x141096150` and `0x141096BB0`.

      RUNTIME REJECTED (`afx`, PID17880): the clean stock-arm package passed
      admission but did not make Draw/Put smooth. Exact boot/dual observer/
      combined READY and role-B Nadine appeared; cold AFU finished
      `authored=1/1/1`; four lifecycle requests and 85 fade rows covered
      `28B0F93B -> 1313FB4E -> 7405AACB -> 3889CC0E` and
      `DF5B68FC -> AD73C917 -> 3889CC0E`. There were zero synthetic-pair,
      late-IK, exception or timeout rows. The user explicitly reported that,
      ignoring the broken steady endpoint, the elbows/hands still moved
      sharply. Reject late synthetic overwrite as a sufficient cause. Do not
      implement the steady underlay yet. Next run only a same-pass read-only
      C39 rows `19..30` census before/after Perform `0x141A92905` and after
      Player `+0x408` `0x141A929A1`, keyed to the exact Draw/Put states, to
      separate authored DAG/retarget from WeaponIK/HolsterIK and later Async.

  Strap is next after transition classification.  Do not re-enable the rejected
      ShowMesh-only path.  Stock physics needs all five guide-lookup sites to
      resolve a complete `strap2..strap14` owner locator set; Nadine has no such
      authored subtree.  The next admissible work is a Chloe guide-locator
      capture/provider audit, not visibility forcing.

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
