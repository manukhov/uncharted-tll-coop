import ida_bytes
import ida_funcs
import ida_loader
import _ida_loader
import ida_name
import ida_pro
import idc


FUNCTIONS = {
    # --- 2026-10-24: own fresh per-primary spawn inputs, not borrowed stacks ---
    0x1413C07E0: (
        "PlayerPersistentState_CopyCheckpointFieldsThroughE10",
        "STATIC pure GameSave::CharacterPersistentState field copier. Called "
        "by TaskManager1413C0675 and independent checkpoint/save paths. Copies "
        "inline fields through DWORD+E10 (required extentE14), not just184h; "
        "native arrays advanceE20 per record. No calls, allocation, AddRef or "
        "destruction. Preserves gaps/padding; do not call this a contiguous "
        "E20 memcpy or an owner/world lifetime lease.",
    ),
    0x141589700: (
        "GameSave_ReadCheckpointValueFromSaveBuffer",
        "STATIC source GameSave::SaveDataImplementation::ReadCheckpoint, "
        "savedata.cpp:2425. Requires tag size7264/1C60; bounds-checks SaveBuffer "
        "then directly memcpy reads1C60 bytes into checkpoint at14158980F and "
        "advances its cursor. No pointer fixups or reference-count operations "
        "follow. Independent evidence that embedded CharacterPersistentState "
        "is an inline save value, not a captured raw scene-pointer graph.",
    ),
    0x14158E500: (
        "GameSaveCheckpoint_InitializeValueFields",
        "STATIC initializes checkpoint's CharacterPersistentState values at0 "
        "and+E20: HP100, first170h payload zero, count+174 zero, inline treasure "
        "bitsets+DC8 zero and flags+DC0..DC2 zero; initializes checkpoint tail "
        "clock/default continue. Does not initialize every array byte or imply "
        "that an arbitrary zero buffer is a semantically current checkpoint.",
    ),
    0x14158F5D0: (
        "GameSave_Copy29PersistentStateRecordsToGlobal",
        "STATIC copies scalar header and29 inline CharacterPersistentState "
        "records to global pointer143F21B58 under146A8C860. Each calls "
        "1413C07E0 at14158F6D7 and advancesE20 at14158F6DC. Pure inline field "
        "copy does not retain borrowed spawn-context pointers.",
    ),
    0x14158F720: (
        "GameSave_Copy29PersistentStateRecords",
        "STATIC two-argument inline value copier: scalar header then29 "
        "CharacterPersistentState records, calling1413C07E0 at14158F807 and "
        "advancingE20 at14158F80C. No allocation, AddRef or destructor calls; "
        "does not establish a current world/session for a deferred spawn.",
    ),
    0x1415C5E50: (
        "ThreadContext_GetRecursiveLockOwnerToken",
        "STATIC returns native FLS block QWORD+8 when142207E90 reports an "
        "execution index below18h; otherwise obtains current native thread ID "
        "through142204FB0/142205040 and ORs4000000000000000. Used as recursive "
        "lock ownership identity, not an active-job counter or quiescence fence.",
    ),
    # --- 2026-10-24: TaskManager-owned companion recreation, not actor+150 ---
    0x1413C2270: (
        "TaskManager_MaintainCompanionActors",
        "STATIC complete MainMenuVT142904A18+1D8 (NOT+238). Default-data "
        "policy141DA2B98 dispatches this slot when task+40&0E. Maintains Nadine "
        "weak pair manager+168/+170 at1413C2689..26EE; invalid pair calls "
        "1413BF090 at1413C26C5 with descriptor+A8, hashC785414E and nadine-buddy. "
        "NULL skips ONLY Nadine publication then continues Cutter/other actors. "
        "Later Nadine use revalidates the weak pair at1413C2C15..2C46.",
    ),
    0x141DA2B60: (
        "TaskManager_ApplyTaskDefaultActorPolicy",
        "STATIC called by1413C31B0 at1413C31D2. Resolves default data from "
        "manager+100 via141CD8A00; byte default+40&0E invokes manager virtual "
        "+1D8 at141DA2B98. Also dispatches virtual+208 under its actor/task "
        "guards. This is the companion-maintenance caller, not menu+238.",
    ),
    0x1413C31B0: (
        "TaskManager_ProcessTask",
        "STATIC common task processing (+1E0 for base TaskManager): calls "
        "TaskManager_ApplyTaskDefaultActorPolicy141DA2B60 at1413C31D2, then "
        "GetProcessDeltaTime. Separate +238 dispatch1413C32DC is menu "
        "bootstrap, NOT the +1D8 companion maintainer.",
    ),
    0x1413C4880: (
        "TaskManagerActive_Update",
        "STATIC active task update dispatches owner virtual+1E0 at1413C48DA. "
        "Base ProcessTask1413C31B0 reaches task-default policy and the "
        "+1D8 companion maintainer; not an allocation-only bootstrap path.",
    ),
    0x140BB3460: (
        "TaskManagerOnline_ProcessTask",
        "STATIC TaskManagerOnline RTTI/type143760DA8, VT1429FB410+1E0. "
        "Calls common TaskManager_ProcessTask1413C31B0 at140BB348F; its "
        "same VT+1D8 entry1429FB5E8 is1413C2270. Independent dispatch path.",
    ),
    0x141E7C850: (
        "Object_InitSpawnParentAndSpawner",
        "STATIC initializes spawn-parent and own spawner link from SpawnInfo. "
        "PSI+58 read141E7C9E3; only nonnull spawner with +51bit08 CLEAR is "
        "stored at actor+150 at141E7C9F2. Task companion Spawn arg4TRUE sets "
        "that bit, deliberately omitting this link; NULL does not prove no "
        "EntitySpawner participated. No retained-pointer lease is implied.",
    ),
    0x1417AA820: (
        "StringId_HashAppendWithSeed",
        "STATIC out DWORD RCX, seed EDX, text R8; MSB-first byte table "
        "1428A38B0 append loop. Seed0 and NadineBuddy produce C785414E, "
        "matching TaskManager maintainer's inline class-name hash loop.",
    ),
    # --- 2026-10-24: stop automatic companion recreation at its native owner ---
    0x141D22310: (
        "EntitySpawner_Kill",
        "STATIC native kill-entity/terminate helper: bool in AL, RCX=spawner. "
        "Leaf requires +52 sign clear and +51 bit10 clear. Sets +50 bit04 at "
        "141D223CB, clears +53 bit02/+51 bit40, resolves OWN +30/+38 and "
        "calls ProcessKillTree at141D223FD. ShouldSpawn rejects bit04; explicit "
        "Spawn141D2278B and Reset141D22150 can clear it. No universal story "
        "suppression claim. Called by locked ProcessMgr visitor141C2D13D.",
    ),
    0x141D21830: (
        "EntitySpawner_ShouldSpawn",
        "STATIC Group_Update141EDAD57 eligibility predicate. Rejects live own "
        "actor, +50 bit02 universally, bit04 when +51 bit10 clear, and bit08 "
        "failed-spawn state. Contains task/runtime-command/debug checks; NOT a "
        "pure accessor. EntitySpawner_Kill sets bit04 to inhibit automatic retry.",
    ),
    0x141C2D020: (
        "Script_KillEntityDependentProcessVisitor",
        "STATIC callback passed at141C2D28D to ProcessMgr_VisitProcessTreeLocked. "
        "Matching dependent locatable actor virtual+168 gets own spawner; "
        "141C2D13D calls EntitySpawner_Kill, falling back to bare KillTree only "
        "on missing/refused spawner. Native precedent for spawner-first kill "
        "inside a live ProcessMgr visitor; not a cached-resource lifetime lease.",
    ),
    0x141C2D180: (
        "Script_KillEntityOrTerminate",
        "STATIC source reason string kill-entity or terminate at141C2D264. "
        "Optional dependent ProcessMgr visitor141C2D28D; target virtual+168 "
        "then EntitySpawner_Kill141C2D2A6, bare KillTree fallback141C2D2B2. "
        "Spawner-first retirement distinguishes native removal from raw actor kill.",
    ),
    0x14004D930: (
        "RegisterNadineBuddyTypeFactory",
        "STATIC calls TypeFactory_Register1417C4650 at14004D98B with exact "
        "record143D464C0, literal NadineBuddy, creator1404A0F40 and flagsC8000. "
        "Registration, NOT an actual campaign respawn owner.",
    ),
    0x1417C4650: (
        "TypeFactory_Register",
        "STATIC source type-factory.cpp:224. Stores creator+68, literal+70, "
        "type hash+10, parent hash+18 and registration index+14. Generic "
        "SpawnProcess1417C3790 invokes creator+68; no direct Buddy ensure loop.",
    ),
    0x141EE7C60: (
        "Level_KillDependentProcesses",
        "STATIC exact Level::KillDependentProcesses, level.cpp:2796. Reason7 "
        "takes world141B409A0/141B40C10 before own level-list body collection, "
        "allocator lock141B45730, batch remove141EE7DAE, unlock141B45810, "
        "body/world release. Independent lock-order precedent, NOT a ProcessMgr "
        "visitor and NOT an isolated single-actor API for the mod.",
    ),
    0x141BC8460: (
        "NdGameObject_BatchDestroyCompositeBodyPass1",
        "STATIC exact source batch-destroy first pass. Virtual+228 invoked "
        "byActorReset_CollectCompositeBodiesPass1, gathers body handles before "
        "global world batch removal. Distinct from live OnKill self-removal.",
    ),
    0x141BC85B0: (
        "NdGameObject_BatchDestroyCompositeBodyPass2",
        "STATIC native batch-destroy second pass, virtual+230 invoked after "
        "global body removal byActorReset_ShutdownCompositeBodiesPass2. Keep "
        "menu ordering; live-world OnKill has its own body cleanup path.",
    ),
    0x140420310: (
        "Npc_KickNavigationAnalysis",
        "STATIC exact Npc::KickNavigationAnalysis. Publishes NPC+4190 counter "
        "at140420F02 through Nav::BeginSinglePathFind141E0AD50; also contains "
        "FlankSkill producer path. Counter consumers140420F60 and postrender "
        "manager drain establish the zero-counter retirement guard.",
    ),
    0x141E0AD50: (
        "Nav_BeginSinglePathFind",
        "STATIC exact Nav::BeginSinglePathFind; begins navigation work whose "
        "callback141E0AD40 reaches141FF0B00. NPC and FlankSkill store returned "
        "counters at+4190 /+4C0, waited byNpc_WaitAndCommitNavigationAnalysisJobs.",
    ),
    # --- 2026-10-24: live-world per-actor retirement, not global menu reset ---
    0x141E56C40: (
        "CompositeBody_RemoveActiveRigidBodiesFromWorld",
        "STATIC removes ONLY this composite's active bodies: brackets141B409A0/"
        "141B40C10, gathers body+10 handles from own rows, allocator lock141B45730, "
        "world batch remove14238C350 at141E56D29, allocator unlock141B45810, "
        "sets own removed-count+24 to row-count+10, releases world brackets. "
        "Reached by Character_OnKill141E9C78E and NdGO_OnKill141BB9732, also "
        "CompositeBody shutdown. Live-world OnKill owns per-actor physics cleanup; "
        "this does NOT make direct KillTree equivalent to full menu ResetActors.",
    ),
    0x141B409A0: (
        "HavokWorld_EnterScopedAccess",
        "STATIC world-present gate. Fiber-owner146917BC0 (nativeFLS+8) and "
        "depth146917BE0 make same-owner recursion explicit; otherwise waits "
        "counter146917BB0 before ownership. Acquire BEFORE ProcessMgr tree lock. "
        "Pair141B40A60; no claim it is the separate allocator lock.",
    ),
    0x141B40A60: (
        "HavokWorld_LeaveScopedAccess",
        "STATIC inverse141B409A0. Decrements nested depth146917BE0 or on last "
        "owner releases counter/owner146917BC0/gate146917BD0. Outer finally "
        "releases this AFTER body-access141B40CB0 and ProcessMgr visitor return.",
    ),
    0x141B40C10: (
        "HavokWorld_EnterScopedBodyAccess",
        "STATIC second world bracket after141B409A0. Same nativeFLS+8 owner "
        "146917BF0 recurses via146917C10; other owner waits146917BE8. "
        "Pair141B40CB0. Inner per-actor OnKill can recurse while outer world "
        "scope precedes ProcessMgr lock; do NOT outer-lock allocator141B45730.",
    ),
    0x141B40CB0: (
        "HavokWorld_LeaveScopedBodyAccess",
        "STATIC inverse141B40C10. Decrements146917C10; last owner releases "
        "counter146917BE8 and clears owner146917BF0/gate146917C00. "
        "Per-actor141E56C40 contains equivalent inline release.",
    ),
    0x141B45730: (
        "HavokHeapAllocator_LockForThisThread",
        "STATIC source-identified LockForThisThread, used around world body "
        "batch removal. NONrecursive: asserts allocator lockThreadIndex<0. "
        "Unlike fiber-recursive world brackets, never acquire outside a visitor "
        "whose OnKill takes it internally. Pair141B45810.",
    ),
    0x141B45810: (
        "HavokHeapAllocator_UnlockForThisThread",
        "STATIC inverse141B45730 after batch world body removal at141E56D35. "
        "Separate from world-access nesting locks; do not confuse ownership.",
    ),
    0x141623050: (
        "ProcessMgr_VisitProcessTreeLocked",
        "STATIC manager+0D0 fiber-owner recursive lock, loaded root Process* "
        "argumentRDX (native1464670D0), callbackR8/userdataR9. Resolves tree "
        "parent+80/firstchild+88/next+90 weak links via current record+8, skips "
        "nonlive/dead nodes, invokes callbackRCX=liveObject/RDX=userdata. "
        "Callback0 stops and releases lock. Native reset kill visitor uses "
        "KillTree on its argument; live-world caller must acquire world locks FIRST.",
    ),
    0x140407D00: (
        "Npc_OnKillProcess",
        "STATIC shared NPC virtual+190 target via NadineBuddy thunk1404921A0. "
        "Waits navigation140420F60 at140407D30, destroys knowledge+1FC0 through "
        "140639E50, unregisters weak spot actor1403E8860 at14040807D, removes "
        "pathfind request141D70550, CharacterManager141D63640 at1404081CF, "
        "own+580 body141E56C40 at1404081FA; tailNdGO_OnKill141BB95C0 at140408254. "
        "Do not call callback directly: KillTree owns flags/children/id/deferred free.",
    ),
    0x1404921A0: (
        "Npc_OnKillProcess_Thunk",
        "STATIC five-byte thunk140407D00; exact NadineBuddy142949638 slot+190 "
        "and multiple other NPC classes share it. Not a Nadine-only destructor.",
    ),
    0x1403E8860: (
        "NpcManager_UnregisterSpotActor",
        "STATIC inverse RegisterSpotActor1403E84F0, removes exact{Process+A8,BC} "
        "from NPC spot/path-search registry143D3C0D0. Called during Npc_OnKill "
        "14040807D. CORRECTION not active-Player roster/CharacterManager itself.",
    ),
    0x140639E50: (
        "AiKnowledge_Destroy",
        "STATIC exact AiKnowledge::Destroy source assertion knowledge.cpp. "
        "Npc_OnKill calls it for NPC+1FC0 at140407E09 before native body teardown.",
    ),
    0x141D70550: (
        "PathfindManager_RemoveRequest",
        "STATIC exact PathfindManager::RemoveRequest source name. Npc_OnKill "
        "calls1404080CE while removing actor-owned AI/path state; not a whole-world reset.",
    ),
    0x140420F60: (
        "Npc_WaitAndCommitNavigationAnalysisJobs",
        "STATIC waits/releases NPC+4190 counter, clears+4190/+4198 and commits "
        "results. For active FlankSkill(hash5FB40892 atskill+78,NPC+1FF0) also "
        "waits/releases skill+4C0, clears+4C0/+4C8. Npc_OnKill calls140407D30; "
        "ordinary manager drain1403E729C reaches SAME helper. Nonblocking live "
        "retirement requires BOTH applicable counters already null under visitor lock.",
    ),
    0x1403E71B0: (
        "NpcManager_DrainNavigationAnalysisJobs",
        "STATIC manager loop callsNpc_WaitAndCommitNavigationAnalysisJobs "
        "at1403E729C. Ordinary postrender schedules14015C7C0->1403E4AF0->this "
        "and waits its counter at1401667D0 before lifecycle seam140166884. "
        "Not proof every possible NPC belongs to the traversed spot list.",
    ),
    # --- 2026-10-24: Process weak identity survives heap object relocation ---
    0x1415FDE50: (
        "RelocatableHeap_MoveBlockAndRebindRecord",
        "STATIC RelocatableHeap142ABBD68 compaction family. Computes new-old "
        "delta; object virtual+8 fixup at1415FDF01, full allocation memmove at "
        "1415FDF0D, record+0=newBase at1415FDF1C and record+8=newObject at1F. "
        "SpawnProcess1417C3CC5 assertsProcess==record->m_pMem, publishes+A8 "
        "backlink at1417C3CFB. A8/BC move with object; weak record/gen survives. "
        "Native retained resolver followsCURRENT record+8, not an original raw "
        "address. Fresh B20628 owner has same record/id at a different address; "
        "actual move timing relative to reset was not captured.",
    ),
    # --- 2026-10-24: owned-replica reset phase before menu Havok close ---
    0x1413B9820: (
        "ActorReset_ResetActors",
        "STATIC exact void ResetActors(int,bool), reset-actors.cpp:266. "
        "CORRECTION replaces unrelated old retained-camera comment. Reason "
        "is forwarded to3 ProcessMgr visitor passes: body collect1413B9740 "
        "at1413B9FFC, world batch remove1413BA023, body shutdown1413B97B0 "
        "at1413BA040, kill1413B9500 at1413BA07D, thenDestroyDead1413BA76F. "
        "Current-process+110 safety check1413B9E8C must remain stock. "
        "Observer1413C6B1F invokes after explicit primary kill; not every "
        "direct reason2 caller has ordinary postrender lifetime context. "
        "Direct calls1402F3B1E/14033D013/140BB8300 do not traverse this "
        "observer seam; GraphMgr146A51570 locking is not a universal actor "
        "reset fence. Entry may sleep/yield, and DestroyDead precedes return.",
    ),
    0x1413B9740: (
        "ActorReset_CollectCompositeBodiesPass1",
        "STATIC first ResetActors visitor, native+110 admission at1413B9755 "
        "returns1413B975B; userdata+0 is reason. Admitted composite actor "
        "calls virtual+228 for batch body collection. World batch removal "
        "1413BA023 occurs before pass2, so direct KillTree is not equivalent.",
    ),
    0x1413B97B0: (
        "ActorReset_ShutdownCompositeBodiesPass2",
        "STATIC second ResetActors visitor, native+110 admission at1413B97BC "
        "returns1413B97C2, forwarded reasonRDX. Admitted composite actor calls "
        "virtual+230 after world batch removal and before final kill pass.",
    ),
    0x1413B9500: (
        "ActorReset_KillAdmittedProcess",
        "STATIC final ResetActors visitor. Native+110 call1413B9529 returns "
        "1413B952F; admitted root eventually callsProcessKillTree1416A5C20 "
        "at1413B9712. Root/descendants invalidated before laterDestroyDead; "
        "do not independently force child predicates. Not the earlier body passes.",
    ),
    0x14029B640: (
        "TaskManagerMainMenu_ProcessTask",
        "STATIC exact TaskManagerMainMenu::ProcessTask, gamemenu.cpp:224. "
        "Sets HavokClosePending146B4541E=1 at14029B82C. Main-menu task thus "
        "directly requests deferred HavokClose(0), independently of peer timeout.",
    ),
    0x14029C210: (
        "TaskManagerMainMenu_EndTask",
        "STATIC exact TaskManagerMainMenu::EndTask. At14029C2AB clears "
        "pending-close146B4541E and requests pending-init146B4541D. This is "
        "native physics reopening, not authority to reuse stale mod spawn args.",
    ),
    0x140BBCE70: (
        "TaskPlayStartMenu",
        "STATIC exact source TaskPlayStartMenu, task.cpp:90. Calls "
        "GameTaskGraphMgr_Reset(reason2) at140BBD00C, queues command at "
        "140BBD0C4, THEN sets HavokClosePending at140BBD0C9. Independent "
        "reset-before-close route, not proof of failed-run temporal writer.",
    ),
    0x141B3F3C0: (
        "HavokInit",
        "STATIC exact source HavokInit; requested through pending-init "
        "146B4541D byHavok_ServicePendingOpenClose141B3F290. Distinct from "
        "HavokClose141B3FEE0 and does not revive old retained Player identities.",
    ),
    0x1415B54C0: (
        "Ndjob_WaitAndReleaseCounter",
        "STATIC callsNdjob_WaitForCounter(a1,a2,0) at1415B54CC, then releases "
        "a1 at1415B54D4. Postrender final owned wait14016680C precedes quit/"
        "reset140166833, graphupdate14016685D and deferredHavokClose140166893. "
        "A particular counter is not a universal external-producer fence.",
    ),
    # --- 2026-10-24: terminal B14900 Havok probe fault and reset admission ---
    0x142227B60: (
        "HavokReferencedObject_AddReference",
        "STATIC hkReferencedObject addReference: nonzero WORD object+8 enables "
        "WORD+0A reference increment. Loads hkReferencedObjectLock1437AC388 at "
        "142227B7F, calls TlsGetValue atB86, then reads lock+10 atB8C. "
        "CONFIRMED B14900 second-chance AV2026-10-24: RBX(lock)=0, RAX(TLS)=0; "
        "not a null shape. Shutdown execution order remains a hypothesis.",
    ),
    0x142228190: (
        "HavokReferencedObjectLock_Construct",
        "STATIC constructor, RTTI hkReferencedObjectLock derived from "
        "hkReferencedObject. Initializes lock mode+10, nesting+14 and Windows "
        "critical section+28; allocated by singleton factory142228150.",
    ),
    0x142228150: (
        "HavokReferencedObjectLock_CreateSingleton",
        "STATIC allocates80 decimal/50h bytes and invokes142228190. Registration node "
        "142FEA2F0 binds literal hkReferencedObjectLock, this factory and "
        "output singleton slot1437AC388; independent of probe-shape identity.",
    ),
    0x142232740: (
        "HavokSingletons_Shutdown",
        "STATIC reverse-walks registration list1437AD338, releases each live "
        "singleton through142227D80 and zeros *node->slot at142232807. "
        "Node142FEA2F0 maps that writer to1437AC388. This is a real null writer, "
        "not proof it executed before the B14900 fault.",
    ),
    0x142232510: (
        "HavokBaseSystem_Quit",
        "STATIC sets referenced-object lock mode0 and calls singleton "
        "shutdown142232740 at142232524. Reached by worker141B44680 during "
        "HavokAllocClose, including nonfinal/world-close HavokClose(0).",
    ),
    0x141B44680: (
        "HavokBaseSystem_QuitWorkerCallback",
        "STATIC callback thunk into142232510; HavokAllocClose141B446E0 "
        "submits this in affinity1 job after worker TLS-quit jobs and waits "
        "for completion. No direct mod frame is required for a later native fault.",
    ),
    0x141B446E0: (
        "HavokAllocClose",
        "STATIC exact source HavokAllocClose. Dispatches worker TLS-quit "
        "callback141B44690, waits at141B4497D, then schedules base-system "
        "quit callback141B44680 and waits at141B44A8E. Called byHavokClose "
        "at141B4022B; not restricted to process-final shutdown.",
    ),
    0x141B3FEE0: (
        "HavokClose",
        "STATIC exact source HavokClose; closes Havok worlds/callbacks then "
        "HavokAllocClose141B446E0 at141B4022B. Nonfinal0 caller141B3F29F "
        "is deferred close; GameClose14014EAB0 passes1 at14014F98D. "
        "Final-shutdown flag146917C14 is not a per-Player lifetime lease.",
    ),
    0x141B3F290: (
        "Havok_ServicePendingOpenClose",
        "STATIC pending-close byte146B4541E callsHavokClose(0) at141B3F29F "
        "and clears flag at141B3F2A4; otherwise pending-open146B4541D calls "
        "141B3F3C0. Called fromGameFramePostRenderUpdate at140166893. "
        "World/menu teardown can therefore destroy singletons without process exit.",
    ),
    0x141ED7400: (
        "HavokProbeShape_Construct",
        "STATIC exact HavokProbeShape::HavokProbeShape source assertion "
        "havok-probe-shape.cpp:51. For nonnull shape calls addReference142227B60 "
        "at141ED74F9; return141ED74FE matches B14900 terminal stack. "
        "The cached shape is nonnull in that dump; missing global lock faults first.",
    ),
    0x140D0AD90: (
        "PlayerCollision_BuildOrReuseProbeShape",
        "STATIC cached probe state atPlayerCollision+570. Matching parameters "
        "reuse nonnull state+0 throughHavokProbeShape_Construct at140D0AE2C, "
        "WITHOUT world146917C20 test; creation branch checks world at140D0AE8C. "
        "B14900 cached shape1038D9F880 reached this reuse path.",
    ),
    0x140D0F400: (
        "PlayerCollision_DoMoveProbe",
        "STATIC exact PlayerCollision::DoMoveProbe source assertion "
        "player-collision.cpp:805. Calls cached probe helper140D0AD90; "
        "return140D0F9FD matches B14900 native collision stack. "
        "Caller NdPlayerCollision2::DoMove141DB9860.",
    ),
    0x141DB9860: (
        "NdPlayerCollision2_DoMove",
        "STATIC exact NdPlayerCollision2::DoMove source assertion "
        "nd-player-collision-2.cpp:2544; callsPlayerCollision_DoMoveProbe "
        "with return141DB9BE4 in B14900 terminal stack. Reached via locked "
        "wrapper141DB3E90 from post-animation ground-movement collision action.",
    ),
    0x141DB3E90: (
        "NdPlayerCollision2_DoMoveLocked",
        "STATIC wrapper locks collision+540 around141DB9860; return141DB40A7 "
        "matches B14900 stack. Local collision lock is not a Havok singleton "
        "or global world-lifetime lease.",
    ),
    0x140C412A0: (
        "PlayerAnimGroundMoveUpdateCollideApAdjust_Update",
        "STATIC exact RTTI PlayerAnimGroundMoveUpdateCollideApAdjust, "
        "vtable142A07730 slot+10. Reads Player+3BD8 collision and calls "
        "141DB3E90 at140C413C8; B14900 return140C413CD. Embedded in "
        "PlayerCommonAction+50 under post-animation sequencer phase2.",
    ),
    0x140C592B0: (
        "Action_UpdateOptionalComponents",
        "STATIC shared dispatcher: optional pointers this+40 then+30 invoke "
        "component virtual+10. PlayerCommonAction142A07690 slot+38 uses it; "
        "in B14900 ONLY, this+30 points to embedded+50 ground-collide action. "
        "Return140C592E1 matches stack throughPlayer_PostAnimUpdate1410E9210. "
        "Not a hardcoded embedded-action callback or an animation wait.",
    ),
    0x1416A5B50: (
        "Process_ShouldResetForReason",
        "STATIC Player vtable+110 reset admission used by actor-reset visitors. "
        "Reason7 accepts; reason6 accepts if+3E&40 clear; other reasons accept "
        "if signed byte+3D nonnegative; otherwise returns+3C&02. Menu reason2 "
        "comes from1409BCBAB. DumpPlayer10E0243800 flags008AD7E1 reject2; "
        "actual reset traversal/flag writer in failed run are not established.",
    ),
    # --- 2026-10-18: menu task reset and pre-invalidation OnKill admission ---
    0x141CB9C40: (
        "GameTaskGraphMgr_Reset",
        "STATIC source assertion GameTaskGraphMgr::Reset(int,bool,bool) const, "
        "task-graph-mgr.cpp:254. Under manager+200 recursive lock, invokes "
        "observer+8 at141CB9CF5 BEFORE resetting task subnodes. Exact installed "
        "TaskObserver142A7E268 resolves1413C6AF0, which calls actor reset1413B9820. "
        "Reached synchronously by141CDA945 and141CDB519, not only queued VM work. "
        "For singleton146A51370 this lock is146A51570/depth146A51580, also used "
        "by GameTaskGraphMgr_Update. Ownership token comes from1415C5E50. "
        "No active TaskManager ProcessTask drain precedes observer+8 here; "
        "shared recursive locking alone does not prove worker exclusion.",
    ),
    0x1413C6AF0: (
        "TaskObserver_OnReset_DispatchActorReset",
        "STATIC TaskObserver142A7E268 slot+8, singleton1430743C8 installed at "
        "manager146A51370+100 by140156192. Called at141CB9CF5 before task reset. "
        "Calls primary-player kill/camera helper1410F3DD0 at1413C6B15, then "
        "actor reset1413B9820(reason,bool) at1413C6B1F. Not a worker exclusion fence.",
    ),
    0x1413C7170: (
        "TaskObserver_OnTaskState2_ConditionalActorReset",
        "STATIC TaskObserver142A7E268 slot+20, invoked at141CDC613 after "
        "task+60=2 at141CDC55C. Calls1413B9820(2,0) at1413C71BE ONLY if "
        "141CDBFF0(task) succeeds and task+90 bit4 is set. Live main-menu "
        "task flags unknown; distinct from unconditional observer+8 reset.",
    ),
    0x141CDA070: (
        "PreResetter_Init",
        "STATIC exact PreResetter::Init source assertion, task-subnode.cpp:598. "
        "Copies initial user payload into process+100..117. Materialized by "
        "141CDA800 positive-duration branch at141CDB481; Active callback141CDA250 "
        "later re-enters that task transition with zero duration.",
    ),
    0x141CDA250: (
        "PreResetter_ActiveUpdateAndResetTask",
        "STATIC Active@PreResetter vtable142B6CB78 slot+20. Timer/payload test "
        "eventually calls141CDA800 with zero duration at141CDA43F, then kills "
        "its process tree. Positive initial duration does not exclude earlier "
        "conditional synchronous GameTaskGraphMgr_Reset at141CDA945.",
    ),
    0x1410D84B0: (
        "Player_OnKillProcess",
        "STATIC exact Player142A4BFE8 slot+190. KillTree sets object+40 bit4 "
        "before dispatch, but object+BC remains valid until callback returns. "
        "This callback keeps Player vtable while releasing state/children; "
        "calls Character_OnKillProcess141E9C750 at1410D8A55.",
    ),
    0x141E9C750: (
        "Character_OnKillProcess",
        "STATIC base OnKill callback called by Player1410D84B0 at1410D8A55. "
        "Performs Character cleanup and tail-calls NdGameObject_OnKillProcess "
        "141BB95C0 at141E9C79E. Distinct from deleting destructor141E991F0.",
    ),
    0x141623AD0: (
        "ProcessMgr_RemoveKilledProcessFromTokenIndex",
        "STATIC called by KillTree at1416A5F16 AFTER virtual+190 OnKill "
        "and BEFORE object+BC invalidation1416A6044. Removes token-index entry "
        "under ProcessMgr recursive lock; not itself the OnKill virtual callback "
        "and not a mod-reader lifetime lock.",
    ),
    0x1403480D0: (
        "Script_QuitToMainMenu_Request",
        "STATIC: registered for literal quit-to-main-menu14291AEC0 by14003FA20 "
        "(callback store14003FA6C). Sets byte1437DFD5E=1 and clears script "
        "result; no destruction here. Postrender140166811/81A consumes/clears "
        "request, then1413C80D0 and1409BCA80 select/enter the menu task route. "
        "Does not prove its later VM/reset or object-exclusion boundary.",
    ),
    # --- 2026-10-18: exact Pirate destruction leaves patched vtable first ---
    0x1404A3510: (
        "Pirate_ScalarDeletingDestructor",
        "STATIC: exact Pirate vtable14294B9D0 slot0. Writes Human vtable at "
        "1404A3523 BEFORE1403FEBB0 (Npc/base teardown); conditionally frees "
        "0xF460 bytes if destructor flag bit0. Deeper141DE1CE0 frees+15C8 "
        "and+15D0 controllers only after further base-vtable replacements. "
        "Rejects fresh exact-Pirate+408 dispatch during that ordinary base "
        "destruction, NOT an already in-flight call or an unload race.",
    ),
    # --- 2026-10-12: owned normal-Perform invocation, not job-pointer identity ---
    0x141782B20: (
        "AnimManager_SyncVisitPerformBlendAndDispatchPostBlendJob",
        "STATIC: synchronous iterator callback selected at141783213 and invoked "
        "at14178348A. Loads *RDX into RCX as job, advances *RDX by0x38 at "
        "141782B27, and tail-jumps141A928F0 at141782B2A. Independent asynchronous "
        "producer141781560 loads that same complete native outer callback. "
        "Replacing these callback values need not relocate its unwind-described "
        "prologue. These two producers do not prove every possible entry covered.",
    ),
    # --- 2026-10-09: snapshot metadata is not always the emitted clip ---
    0x140DB9D90: (
        "PlayerOverheadMoveController_Initialize",
        "STATIC: constructor stores RTTI PlayerOverheadMoveController vtable "
        "142A1CB90 at140DB9DBE. Player construction assigns this controller "
        "to+0x1ED0 at1410CC3D7; vtable+0x40 is140DBC020 UpdatePostAnim.",
    ),
    0x140DBC020: (
        "PlayerOverheadMoveController_UpdatePostAnim",
        "STATIC: exact PlayerOverheadMoveController::UpdatePostAnim string "
        "at140DBC888 and vtable142A1CB90 slot+0x40. Called from Player+0x1ED0 "
        "at1410EB715 before phase3 WeaponIK. Heavy branch requires controller "
        "+0x330>=0.01 and signed+0x984>=10 (140DBC2F3/301); joint writer also "
        "tests byte142FFAE9F at140DBC789, seeds JointSet140DBC8A0/8B7, calls "
        "SolveArmIk140DBC952, writes rotation140DBCA0B and commits/resets "
        "at140DBCA30/3F. This is a potential earlier pose modifier, not a "
        "proven Hands/WeaponPartial snapshot rebuild or current idle cause.",
    ),
    0x141774A10: (
        "AnimStream_AttachAnimation",
        "STATIC: exact AnimStream::Attach assertions. Called by141961790 when "
        "a candidate stream can bind the requested animation. Takes an "
        "interlocked stream lock at141774A62 and rebuilds slot-id/index arrays "
        "at+0x8/+0x18. This is a mutating, potentially waiting lookup fallback, "
        "not an accessor a diagnostic may call to inspect clip identity.",
    ),
    0x141775E90: (
        "AnimStream_SetPhaseForAnimation",
        "STATIC: streamed-clip helper used by Animation-node generator1419182E0 "
        "and SimpleLayer141894A90 when clip+0x24 mask0x04 is set. Finds/binds the "
        "animation stream through141961790 and writes normalized phase to "
        "stream+0x20 at141775F10. Phase1 first probes chunk-last and can return "
        "without this write. This is NOT a read-only metadata getter.",
    ),
    0x141776520: (
        "AnimStream_GetChunkLocalPhase",
        "STATIC: phase1 returns1 directly; otherwise141961790 finds/binds stream. "
        "Scans count+0x38, intervals+0x40 with stride0x20, selecting start<=phase "
        "and phase<end from row+4/+8. Returns (stream+0x20-start)/(end-start), "
        "or0 when no matching interval/stream. Its lookup may update stream "
        "headers; do not call it from a read-only observer.",
    ),
    0x1417762D0: (
        "AnimStream_ResolveChunkClip",
        "STATIC: streamed clip selection called at141918354/141894B83. Phase1 "
        "tries chunk-last; otherwise finds/binds stream141961790, locates the "
        "requested animation slot and interval containing stream+0x20, then "
        "returns interval+0x18 clipArray[slot]. If no interval matches, uses "
        "the first interval; absent stream falls back to database chunk-0. "
        "Returned clip can differ from Animation-node+0x50. Not read-only.",
    ),
    0x141961790: (
        "AnimStream_FindOrBindAnimation",
        "STATIC: searches active stream table14690B858/count14690B850 by "
        "animation id; optional fallback141774A10 binds a matching stream. "
        "With update flag, validates AnimStream::UpdateHeader assertions and "
        "writes stream headerArray+0x28[slot]=requested header at141961A50. "
        "This helper and its phase/clip callers are not passive observers.",
    ),
    0x1418B41A0: (
        "AnimStateSnapshot_GetValidBitsFromAnimNodeTree",
        "STATIC: exact source assertion. Resolves instance heap+0x130/root+0x138 "
        "(or sentinel fallback), invokes root virtual slot19/+0x98 at1418B4239 "
        "with instance+0x90 context and caller output. Reached by Player weapon "
        "IK prepass141090AC0 at1410915D5 for WeaponPartial; this call is not "
        "a snapshot-root rebuild or proof that the consumed pose changed.",
    ),
    0x14189B760: (
        "AnimStateLayer_GetEffectiveSnapshotHeap",
        "STATIC: returns layer+0x58 override heap when nonnull, otherwise "
        "layer+0x50. Track construction passes this selected heap to instance "
        "initialization;1418B4086 stores it at instance+0x130. Not a private "
        "immutable command buffer owned by each instance.",
    ),
    0x1418C3150: (
        "SnapshotNodeHeap_ReleaseNode",
        "STATIC: exact SnapshotNodeHeap::ReleaseNode source assertion. Bounds "
        "index against heap+0xAC, takes record heap+0xA0+index*0x28 and "
        "unlinks it through1418C3210. Reached by snapshot-node release virtuals; "
        "a later equal root index/pointer is not a node-generation certificate.",
    ),
    0x14191EB80: (
        "AnimSnapshotNodeBlend_ReleaseSubtree",
        "STATIC: Blend release virtual slot15/+0x78. Resolves children+0x40/"
        "+0x44, releases each through the same virtual slot, then releases "
        "own node index+0x18 with SnapshotNodeHeap_ReleaseNode1418C3150. "
        "ResetSnapshotRoot can therefore retire the entire current tree.",
    ),
    # --- 2026-10-09: distinguish an empty partial tail from a missing grip ---
    0x141893B00: (
        "AnimLayer_GetCurrentBlendWeight",
        "STATIC: returns float layer+0x28. AnimStateLayer vtable142B0ABB0 "
        "slot+0x10 points here; CreateAnimCommands14189EA60 reads this L and "
        "passes it to each snapshot's lane-zero command7 tail. It is distinct "
        "from instance+0x158 track/instance transition weight and from terminal "
        "WeaponIK solve weight. Also shared by other layer vtables.",
    ),
    0x141AC7F10: (
        "AnimSnapshotNodeInstanceZero_GenerateAnimCommands",
        "STATIC: vtable142B3D590 slot+0x70, constructed by141AD5DC0. Emits "
        "two DWORDs {0x00020009, outputLane}; not a no-op. Process segment "
        "case9 at141ABA485/4A9 calls14217D940 and clears lane ValidBits. With "
        "snapshot context+0x1C enabled, the subsequent141AB6C40 command7 tail "
        "inherits valid lane-zero rows. Empty root does not author all-valid "
        "identity or bind transforms and does not imply a missing Hands layer.",
    ),
    # --- 2026-09-28: native animation phase drains, not installation gates ---
    0x1401676D0: (
        "GameLoop_UpdateAndWaitFramePhases",
        "STATIC exact GameLoopUpdate source common-game-loop.cpp:5810/5830. "
        "At1401677FB submits-and-waits UpdateProcessBuckets14015F020 through "
        "1415B5210; return140167800 precedes postrender140165B50 submission "
        "at1401678DE. Normal animation callbacks and bucket5 escaped-job drain "
        "therefore complete before normal postrender HavokStep. Not global "
        "producer exclusion, package-unload retention or an installer gate.",
    ),
    0x1415B5210: (
        "Ndjob_SubmitSingleJobAndWait",
        "STATIC: constructs one callback/data record, allocates a one-job "
        "counter at0x1415B52A9, copies/submits at52C2/52CA, waits at52D7 "
        "and releases at52E4. GameLoopUpdate passes UpdateProcesses "
        "0x14015F020 at0x1401677FB; return0x140167800 is completion of "
        "that submitted callback. Counter waiting can switch worker fibers; "
        "no producer exclusion or global installer-quiescence gate is proven.",
    ),
    0x14015F020: (
        "GameLoop_UpdateProcessBuckets",
        "STATIC: counter creation at0x14015F117 records UpdateProcesses "
        "source location common-game-loop.cpp:1524. Calls "
        "GameLoop_RunAnimationUpdateBucket "
        "for ordered buckets0..8 at0x14015F058..14015F35B. Bucket5 drains "
        "the escaped bucket3 counter146AF2798 at0x14015EAA6/AAE; normal "
        "manager spawner counters are waited separately. Submitted-and-waited "
        "by0x1401677FB->0x1415B5210, before1401678DE submits postrender physics. "
        "Phase completion does not exclude "
        "package-login AnimateObject/Process paths or stop future producers; "
        "not an installer gate for the shared cmd16 callsite.",
    ),
    0x141782AB0: (
        "AnimMgr_WaitEscapedDisabledObjectJobs",
        "STATIC: reads counter146AF2798 at0x141782AB6, waits atAC5, "
        "releases atACD, clears the counter atAD2 and byte1466D0C28 atADD. "
        "AnimMgr async producer queues0x141A93EA0 at0x141782740 and "
        "publishes its counter at0x141782820 through the bucket3 output. "
        "No direct code caller of this helper found; bucket5 performs its "
        "own drain inline. Requires a valid counter, does not close job "
        "admission and is not a callable installer-quiescence API.",
    ),
    0x141A93EA0: (
        "AnimManager_AnimateDisabledObjectJob",
        "STATIC: exact AnimateDisabledObjectJob assertion at0x141A9410F, "
        "anim-mgr-jobs.cpp:1775. Queued by0x141782740 with descriptor data; "
        "atomic cursor+10 indexes the Fg pointer table+0/count+8. Invokes "
        "optional Fg+298 callback at0x141A94054, otherwise builds identity "
        "skinning matrices and publishes Fg+230/+238. Its escaped counter "
        "146AF2798 is separate from normal joint-blend spawner counters. "
        "Completion of this batch is only a phase drain, not global "
        "producer exclusion or an installer gate.",
    ),
    # --- 2026-09-18 AGW: bounded native-frame identity, not no-yield claims ---
    0x141BBDF20: (
        "NdGameObject_WaitUpdateCounter",
        "STATIC AGW: Player RTTI vtable0x142A4BFE8 slot131/+0x418. "
        "Loads owner+0x210; when nonnull calls Ndjob_WaitForCounter at "
        "0x141BBDF41, releases the counter and writes owner+0x210=0. "
        "Reached from AsyncObjectUpdateJointBlendJob after its group "
        "Process calls/palette publication. This is a real yielding owner "
        "callback; do not infer whole-job thread affinity from the two "
        "Process callsites or from the stock prologue.",
    ),
    0x141BBE3B0: (
        "NdGameObject_UpdateCompositeBodyCloth",
        "STATIC AGW: Player RTTI vtable0x142A4BFE8 slot132/+0x420. "
        "Loads owner+0x238 and, if nonnull, tailcalls "
        "CompositeBody_UpdateCloth at0x141E5AA20. Reached by the "
        "post-Process owner callback block of AsyncObjectUpdateJointBlendJob. "
        "Not evidence that arbitrary virtual callback recursion is absent.",
    ),
    0x141E5AA20: (
        "CompositeBody_UpdateCloth",
        "Exact composite-body.cpp CompositeBody::UpdateCloth assertions "
        "at lines2313/2340/2341 reference cloth-skinning bone matrix labels. "
        "STATIC AGW: reached by NdGameObject owner+0x238 wrapper in Player "
        "vslot+0x420 after AsyncObjectUpdateJointBlendJob Process/palette "
        "work. Naming identifies this consumer only, not a closed "
        "no-wait/no-reentrancy proof for its transitive call graph.",
    ),
    # --- 2026-08-27: native NPC pistol composition, not a Player donor ---
    0x1405AAC40: (
        "AiWeaponController_RefreshWeaponLayersAndHands",
        "AiWeaponController vtable 0x1429673C8 slot +0x38; constructor "
        "0x1405A9AE0 installs this exact RTTI-backed table. Reads controller+8 "
        "owner and owner+0x1A0 animation tree, refreshes weapon-partial and "
        "weapon-additive controls, then resolves hands/hands-high tokens at "
        "controller+0x5D0/+0x5F0 through 0x141729BE0. On a changed resolved "
        "animation id, updates the selected layer through 0x141894550 and "
        "0x141896A40. This is NPC layer lifecycle, not a final bone-pose writer "
        "or a proven Player pistol repair boundary.",
    ),
    0x14051B370: (
        "NpcAnimOverlay_SelectWeaponUpDown",
        "Operation identity only; declaring controller class is not resolved. "
        "Requires controller+0x9C enabled and an owner+0x1A0 -> +0x60 overlay "
        "sink. Uses owner virtual +0x7F8 and 0x142144F80 to build weapon-up-down "
        "keys for no-weapon/up/down. Argument values 0/1/2 select those records; "
        "the resolved record is committed through 0x1417F9150 at 0x14051B487. "
        "Also forwards a separate up-state scalar. Data references include "
        "0x14295C7D8 and 0x142D47F58/6C/7C; these are not a closed RTTI witness.",
    ),
    # --- 2026-08-27: distinguish pistol distal pose from native IK limits ---
    0x14181D030: (
        "JointSet_InitializeReferencePoseAndLimits",
        "Allocates the wrapper at JointSet+0x28 and its +0x20 array of "
        "0x90-byte records (count JointSet+0x12). Root reference is identity; "
        "other records take T/Q from the current receiver: JointSet+8 owner, "
        "owner+0x1A8 Fg, Fg+0x20 raw skeleton, relative table +0x20, "
        "0x30-byte rows indexed by JointSet+0x18. Copies raw row+0x20 T and "
        "row+0x10 Q into record+0x10/+0x20. Resolves the named limit profile, "
        "allocates its per-joint limit arrays and forces RefreshAuthoredJointLimits. "
        "WeaponIKFeatherInfo calls here for both receiver arm JointTrees.",
    ),
    0x14181D4A0: (
        "JointSet_RefreshAuthoredJointLimits",
        "Refreshes the generation-aware profile wrapper at JointSet+0x28; "
        "when forced or debug refresh is active, matches authored 0x48-byte "
        "profile rows to receiver semantic joint names and updates the "
        "0x90-byte limit records. Does not replace the initialized reference "
        "T/Q. WeaponIKFeather_Apply calls here at 0x1418F27FB; refresh is not "
        "proof that the later Jacobian iteration enables either limit branch.",
    ),
    0x1418C9FF0: (
        "JointLimits_ApplyJointLimit",
        "Exact joint-limits.cpp JointLimits::ApplyJointLimit assertion. "
        "Applies a limit to the supplied joint set/record. JacobianSolver_Iterate "
        "reaches this only through nonnull instance+0x10 external limits or "
        "instance+0x440 profile limits; the common WeaponIKFeather solve "
        "initializes both sources to null.",
    ),
    0x1418CCC20: (
        "JacobianSolver_BeginSolve",
        "Exact jacobian-ik.cpp BeginSolve(JacobianIkInstance*,JacobianDebugInfo*) "
        "source assertion. Prepares a solver context from the current JointSet "
        "and solver map; reads the optional profile pointer at instance+0x440. "
        "WeaponIKFeather_SolveWeaponFeatherIk calls at 0x1418F3141 after "
        "zeroing both optional limit-source pointers in each fresh instance.",
    ),
    0x1418CC700: (
        "JacobianSolver_Iterate",
        "Computes and applies a Jacobian step to the context's current "
        "JointSet/map. Optional post-step limit application checks instance+0x10 "
        "at 0x1418CCB43, otherwise instance+0x440 at 0x1418CCBA8; both null "
        "skips JointLimits_ApplyJointLimit. This excludes a hidden limit clamp "
        "in the common Feather solve, not all effects of the profile on "
        "solver-map DOF and step weights.",
    ),
    0x1418CD250: (
        "JacobianSolver_FinalizeResidualsAndBlend",
        "STATIC: finalizes solved-minus-saved T/Q into map+0x40 residual "
        "records at CD4D9/CD4DF, then optionally blends saved and solved "
        "JointSet locals using instance+0x428. CD5B9/CD5C9 skips that blend "
        "for weight>=1. WeaponIKFeather calls at1418F3349 with the same "
        "smoothstepped application weight; this is not merely teardown or "
        "a fixed 0.95 prop-translation shrink. Other IK callers share it.",
    ),
    0x1419184E0: (
        "AnimSnapshotNodeAnimation_HasAdditiveRawFlag",
        "Animation vtable 0x142B14D68 slot +0xA0. Returns true exactly when "
        "cached clip node+0x50 exists and its raw ANIM (clip+0x28) has header "
        "+0x20 bit 0x02 set. The overlay row's type1 alone does not establish "
        "additivity; Blend resolves its mode from this predicate on its children.",
    ),
    0x14191E050: (
        "AnimSnapshotNodeBlend_ResolveChildrenAndBlendMode",
        "Blend vtable 0x142B15218 slot +0x50. Resolves both child indices "
        "+0x40/+0x44 via child vslot +0x50, then queries each +0xA0 predicate. "
        "Writes mode at node+0x4C: 1 if right is not additive, 2 if only right "
        "is additive, 6 if both are additive; node+0x48 flag 0x10 forces mode1. "
        "Node+0x48 is flags, not the mode. GenerateAnimCommands 0x14191E5F0 "
        "emits node+0x4C in command7. Type1 pose rows can therefore produce "
        "normal masked or additive blends depending on the resolved children.",
    ),
    0x1418A4A50: (
        "AnimStateInstance_ResetSnapshotRoot",
        "Resolves instance+0x138 through its heap at +0x130 (or uses the null "
        "node), calls the resolved node's +0x78 slot when present, then "
        "unconditionally writes instance+0x138=-1 at 0x1418A4AC7. Later "
        "instance+0x194 handling only controls parameter cleanup, not this "
        "invalidation. PhaseUpdateInstances calls here at 0x14189D80A before "
        "0x14189D824 rebuilds the active instance snapshot. Node-local pose "
        "overlay admission is therefore revisited by the native phase path; "
        "a filtered graph need not be retained in a mod-owned cache.",
    ),
    # --- 2026-08-27: retarget compatibility and resident-asset lifetime ---
    0x1417C8BA0: (
        "AnimMasterTable_FindExactDatabaseClip",
        "STATIC AGW: reverse-searches registered art groups at 0x143840200 "
        "(count 0x1438401C8), selecting the requested database id in each "
        "0x10-byte record. Probes that group's animation hash table at +0x20 "
        "(count +0x1C), dereferences the matched slot, and returns only if "
        "ArtItemAnim+0x40 equals the requested database (0x1417C8C6A). "
        "No calls, allocation, or retarget-inheritance fallback in this body. "
        "AnimationDatabase_ResolveClip adds optional fallback outside this "
        "function; BuildRetargetEntries calls this exact lookup directly at "
        "0x1417CDF35 for raw-row+0x24 and target skeleton+0x24. A profile id "
        "found in another package/database is not proof this lookup succeeds. "
        "AGY: C8BDF/E3 reads the ORIGINAL authored LOD+0x20/count+0x1C "
        "table, compares u32 entry+0 atC8C19/C39, dereferences slot+8. "
        "Not the compact table/resource-name hash/group-row name hash. "
        "ARTGROUP_DATA case16 copies clip keys unchanged at141CC2BAD. "
        "STATIC AHA: ABI uses database ECX and clip R8D; middle RDX unused. "
        "Call-free does not mean pinned: AnimMaster writers do not acquire "
        "SkelTable lock1466EC4B0. Do not add this borrowed lookup under "
        "only that lock or infer record-build provenance from current null.",
    ),
    0x1418AB8F0: (
        "AnimUtil_AnimateObject",
        "Exact nd-anim-util.cpp:871 AnimateObject assertion gives root "
        "Transform, ArtItemSkeleton, ArtItemAnim, float time, world Transform "
        "output, JointParams output, scalar inputs/outputs and AnimateFlags. "
        "STATIC AGW: BuildRetargetEntries calls with time0/flags2 at "
        "0x1417CE068 only after a nonnull exact-database profile lookup, then "
        "inverts sampled world matrices for packed retarget construction. "
        "This is NOT an isolated leaf: it acquires global 0x146905400, uses "
        "Memory_AllocateFromCurrentAllocator, and dispatches "
        "ProcessAnimCmdListSegment. Do not call it through an uninitialized "
        "MappedImage merely because selected joint-math leaves are map-safe. "
        "STATIC 2026-09-20 profile slice: flags2 emits all8 Nadine skeleton "
        "groups although E51542CA has7 ANIM groups. Cmd27 exports cumulative "
        "parameter rows after cmd15/16, not raw constants alone. Only71 "
        "row124-required matrices are proven independent of procedural ops: "
        "unchanged g0/g1 parameter prefixes, op40 and op53 palm handoff. "
        "This pure dependency slice is not permission to execute custom "
        "operators with their profiling/FLS dependencies offline.",
    ),
    0x1417C9070: (
        "AnimMasterTable_LoginArtGroup",
        "Exact AnimMasterTable::LoginArtGroup assertion. Registers a unique "
        "art-group id/pointer in the 0x10-byte table at 0x143840200, notifies "
        "animation-table listeners, and increments the registry epoch at "
        "0x1438401BC (write 0x1417C9185). The body mutates the table, count and "
        "listeners directly and contains no CMPXCHG or other internal registry "
        "lock. Clip lookup only searches these registered groups; neither this "
        "body nor the epoch is a resource pin. AGY: sole direct caller "
        "141CC2934 is ANIM login case decimal42/0x2A. Art+0x40 database "
        "gate at1417C91EB precedes observer slot0; no clip-key synthesis.",
    ),
    0x1417C93F0: (
        "AnimMasterTable_LogoutArtGroup",
        "Notifies animation-table listeners around removal of every matching "
        "art-group pointer from the 0x143840200 table using swap-last "
        "compaction, updates count 0x1438401C8, and increments registry epoch "
        "0x1438401BC at 0x1417C94A0. The body performs those table/listener "
        "mutations directly and contains no CMPXCHG or other internal registry "
        "lock. An epoch check detects a completed change but cannot make a "
        "borrowed clip dereference safe or keep a clip/skeleton resident.",
    ),
    0x141AE1F10: (
        "RetargetPlugin_DecodePackedReferenceLocator",
        "Decodes one 14-byte reference locator containing half-float "
        "translation and quaternion components into the runtime locator and "
        "normalizes the quaternion. DoJointRetarget calls it for the two "
        "reference locators at retarget-joint record +0 and +0xE. STATIC AGV: "
        "quaternion halves at locator+6/+8/+A/+C feed norm-squared and "
        "rsqrt/Newton at 0x141AE1F7C..1FF2 unconditionally. Zero or nonfinite "
        "packed rotations can reach DoJoint's assertion before its caller "
        "validates output. Finite nonzero half components bound norm-squared "
        "to [2^-48,4*65504^2]; zero includes all signed-zero combinations.",
    ),
    0x141AE2100: (
        "RetargetPlugin_DoJointRetarget",
        "Exact DoJointRetarget source assertion. Decodes the source/target "
        "reference locators and selects correction by the single byte at "
        "record+0x2C; record+0x2D is not read. "
        "STATIC 2026-10-08 mode2 (280E): absolute Q=conj(E)*Qin*A, "
        "S=targetRef.S, T=f32(f32(source.T-half3(record+1C))+targetRef.T). "
        "Half loads2862/2873/2885, S286A, sub/add28C0/28C4, "
        "Relative28CA then Compose28DE yield Q at28E3; their T is discarded. "
        "BoneScale+22 is not used by mode2. Additive mode2 (281B..285D) "
        "uses S=1,T=0,Q=conj(A)*Qin*A. Additive is R9B captured2151, "
        "not the mode byte or group-first-index context+68. Native ANIM "
        "+20 bit2 reaches descriptor+0E at141936923, read141AE2AB3 then "
        "passed141AE2E70; pinned NPC UP/DOWN flags0 select absolute. "
        "Mode 1 includes relative-reference composition (Transform_Compose "
        "at 0x141AE27FE), not a raw quaternion copy; mode 3 copies source "
        "S/T/Q at 0x141AE28F3. Player and Chloe finger/palm maps can therefore "
        "both reach Nadine through their authored retarget entries despite "
        "different processing-group indices and reference poses. Its third "
        "argument is one concrete target-reference JointParams row, not the "
        "reference-pose base: ApplyJointRetargets forms it as whole-reference "
        "base + 48 * record[+0x2A]. Output writes are three 16-byte stores "
        "at 0x141AE2A34/2A38/2A3D. Ordinary admitted math uses local values, "
        "but assertion branches can call engine callback qword_146B17C08 "
        "and assert/debugbreak: do not describe the entire function as "
        "side-effect-free or as calling only Transform_Compose. STATIC AGV "
        "assert sites: input T/Q/S 0x141AE2197/21F2/225F, null output "
        "0x141AE230A, output S/Q/T 0x141AE2963/29BD/2A28. Fixed modes1/3 "
        "with a4=false are admitted only after expanded-row and BOTH packed "
        "reference-rotation checks. Mode1 copies target S/T at 272B/2730; "
        "mode3 copies source at 28F3..28FC. Conservative all18-half finiteness "
        "is sufficient policy, not native necessity for every unused lane. "
        "STATIC 2026-09-20 grip-order correction: for normalized decoded "
        "locator quaternions U=record+6 and V=record+14h, mode1 absolute "
        "Qout=conj(V)*Qin*U, while additive Qout=conj(U)*Qin*U "
        "(2635..2712); additive S=identity/T=zero, absolute S/T=target "
        "reference. With mode2 unit-weight Qleft*Qright this is equivariant: "
        "Rabs(X*B)=Rabs(X)*Radd(B), modulo floating point. Therefore "
        "moving the additive across this SAME retarget is not independently "
        "a grip correction; direct row124 vs composed row20/157 base mapping "
        "is a separate question. Mode3 copies both absolute and additive "
        "source values, so the P99 prop-local row is unchanged by that order "
        "or those mode3 maps. This is not a claim of skin/contact equivalence.",
    ),
    # --- 2026-08-27: node-local pose overlays before clip caching ---
    0x1400EDBC0: (
        "AnimSnapshotNodeAnimation_RegisterSnapshotCallback",
        "Hashes the authored type string anim-node-animation and calls the "
        "snapshot-node registrar at 0x141935150 with the animation class id "
        "and callback 0x141919F60. SelectRootNodeIndex dispatches this callback "
        "through +0x10 of the matching 0x18-byte type-registry entry.",
    ),
    0x141919F60: (
        "AnimSnapshotNodeAnimation_SnapshotWithPoseOverlays",
        "Source assertion identifies SnapshotAnimNodeAnimation. Arguments are "
        "snapshot, authored node descriptor, SnapshotContext, and output flags; "
        "returns a node index or -1. Creates an Animation node, checks its RTTI "
        "class, and invokes vslot +0x48 (0x141917D00) at 0x14191A0DA. When "
        "context+0 carries an AnimOverlayState, initializes a 32-byte cursor "
        "from node+0x44 with indices -1 and selector mode zero, then calls "
        "ApplyPoseOverlayRows at 0x14191A124. This occurs before the node's "
        "vslot +0x50 resolves and caches its clip; the initial direct id is "
        "not proof that no type-1 pose child is subsequently materialized.",
    ),
    0x141918C30: (
        "AnimSnapshotNodeAnimation_ApplyPoseOverlayRows",
        "Recursive node-local pose consumer: arguments are snapshot, node "
        "index, 32-byte overlay cursor, SnapshotContext, and output flags. "
        "Searches context+0 through FindMatchingRow at 0x141918CE7. Type 0 "
        "writes row+4 to node+0x44 at 0x141919B28 and searches the new source "
        "again at 0x141919C66. Type 1 creates a Blend: original-source child "
        "recurses with the returned cursor at 0x14191922F, target Animation "
        "child is initialized at 0x14191938F and recurses with a fresh target "
        "cursor at 0x1419193D4. Thus type-0 replacement can precede type-1 "
        "pose materialization, unlike ResolveAnimationId, which skips type 1. "
        "Additional variant searches call FindMatchingRow at 0x14191986C and "
        "0x141919AF9; 0x141919D78 is diagnostic only. Final AnimSetRemap at "
        "0x141919CE7 writes node+0x44/+0x48/+0x4C while preserving source "
        "+0x40, before ResolveClip caches node+0x50.",
    ),
    # --- 2026-08-26: native animation opcode-28 offline parity ---
    0x140128020: (
        "AnimJointProgram_InitializePackedQuaternionPermutationRows",
        "CRT initializer registered in the executable constructor table at "
        "0x1425EAA30. It copies the two 32-byte constant blocks at "
        "0x142BF0700/0x142BF0740 into the AVX permutation rows at "
        "0x146C71CE0/0x146C71D00 consumed by packed-quaternion operators, "
        "including opcode24 worker142185CF0 xref142185EE5 and opcode28. "
        "Rows are(3,1,2,0),(0,3,2,1),(0,1,3,2),(0,1,2,3). They swap "
        "the reconstructed D lane into the selector; do not insert D into "
        "a natural-order smallest-three list. Source blocks are NOT contiguous.",
    ),
    # --- 2026-08-21: cold ProcessWeapon factory lifecycle ---
    0x141103B70: (
        "ProcessWeapon_FactoryInitialize",
        "ProcessWeapon TypeFactor/factory vtable +0x58 callback. Calls the "
        "complete ProcessWeapon_Initialize at 0x141103B7E, translates its "
        "result code for the generic materializer, and returns without any "
        "other ProcessWeapon mutation.",
    ),
    0x1414F9BA0: (
        "ProcessWeapon_CreateFromWeaponInfo",
        "Builds the ProcessSpawnInfo packet from WeaponInfo, resolves the "
        "WeaponArtDef selector and materializes the ProcessWeapon through "
        "ResolveOrMaterializeRuntimeStateObjectFromSelectorPacket. The caller "
        "does not receive the weapon until the factory init/post-init sequence "
        "has completed.",
    ),
    0x141EB9780: (
        "NdDrawableObject_AllocateSnapshot",
        "NdDrawableObject factory vtable +0x1A8 post-init callback, identified "
        "by the embedded NdDrawableObject::AllocateSnapshot source assertion. "
        "It runs only after ProcessWeapon_FactoryInitialize returns and does "
        "not rewrite attachment selection or binding fields.",
    ),
    # --- 2026-08-21: cold ProcessWeapon LongGunJiggle lifecycle ---
    0x1414FDDD0: (
        "ProcessWeapon_GetNormalizedArtClass",
        "Resolves weapon+860 through normal art registry; reads signed art+8. "
        "Offline only maps16->0 and17/22->1; negative/missing ->-1. "
        "Init1414FB50A requires result1 before LongGunJiggle creation. "
        "Pinned P99 art8075397D atweapon-art.bin6CE8 hasclass0, so this "
        "constructor branch is excluded; Mazur33E29A1B raw17 normalizes1.",
    ),
    0x1414FD7B0: (
        "ProcessWeapon_GetHolsterBindingDescriptor",
        "Resolves weapon+860 through cached art registry and returns art+D8 "
        "binding descriptor ornull. Used with normalizedclass1 andweapon+898 "
        "bit0 by1414FB50A LongGunJiggle constructor admission.",
    ),
    0x141500880: (
        "ProcessWeaponBase_Update",
        "Per-frame ProcessWeaponBase update. For weapon+0x838 it computes the "
        "jiggle-active predicate from weapon+0x898 bit 3, the draw object and "
        "weapon+0x8F4, calls LongGunJiggle vslot +0x28, then calls the shared "
        "local/selected attachment updater.",
    ),
    0x14150DBF0: (
        "ProcessWeaponActive_Update",
        "RTTI ProcessWeapon::Active vtable slot +0x20. Calls "
        "ProcessWeaponBase_Update at 0x14150DC21, providing the natural per-tick "
        "path to UpdateLocalAttachmentBlend and ProcessWeapon vslot +0x7B8.",
    ),
    0x141F7E370: (
        "LongGunJiggle_UpdateFromAttachmentTransform",
        "LongGunJiggle vslot +0x10. Composes the selected attachment transform "
        "into the jiggle reference and advances displacement/angular state only "
        "while the object is enabled. At 0x141F7E3E8 it composes the current "
        "input with jiggle+0x10 and immediately stores reference +0x30/+0x40; "
        "it does not re-resolve the old selected record. A constant world "
        "translation supplied after 0x141ED0A70 therefore reaches the current "
        "reference; a changing translation can still excite native inertia.",
    ),
    0x141F7E9B0: (
        "LongGunJiggle_GetTransform",
        "LongGunJiggle vslot +0x20. Builds the transform consumed by "
        "NdAttachableObject_UpdateSelectedAttachmentTransform from the current "
        "reference plus jiggle angular/displacement state. Composition at "
        "0x141F7EA5E uses reference +0x30/+0x40 and dynamic +0x80/+0x90. "
        "Together with vslot +0x10 this preserves a constant world translation "
        "in both endpoints of the later interpolation at 0x141ED15DA.",
    ),
    0x141F7EA80: (
        "LongGunJiggle_SetEnabledAndReset",
        "LongGunJiggle vslot +0x28. Stores the enabled byte at +0x8; when false, "
        "copies authored/default displacement and angle into the live state and "
        "clears their velocities.",
    ),
    0x141089E70: (
        "PlayerGrabWeaponAnimAction_PublishSpineTransition",
        "PlayerGrabWeaponAnimAction vtable method. Resolves the current weapon, "
        "verifies literal targSpineProp, publishes transition-to-spine-anim, "
        "then clears weapon+0x8F4. This belongs to the authored weapon-grab/draw "
        "action and is not a cold-holster completion path.",
    ),
    0x14108A290: (
        "PlayerGrabWeaponAnimAction_MarkSpineTransitionActive",
        "Companion PlayerGrabWeaponAnimAction vtable method. Resolves the current "
        "weapon and sets weapon+0x8F4 to one before the grab action's exact spine "
        "transition method clears it.",
    ),
    # --- 2026-08-15: one-shot Player NewIkWeapon parameter mailbox ---
    0x142052250: (
        "StringIdPayloadMap_SetOrRemove",
        "Stores or removes one StringId-keyed payload in the Player animation "
        "parameter map. PlayerWeaponController_RefreshWeaponPartialAndIK uses "
        "it to publish the 16-byte NewIkWeapon weak pair.",
    ),
    0x142052430: (
        "StringIdPayloadMap_Erase",
        "Finds one StringId-keyed node, frees its payload, unlinks it from the "
        "bucket chain and decrements the map count. PlayerWeaponIKAction_Update "
        "calls it immediately after copying NewIkWeapon into action+0x40/+0x48.",
    ),
    # --- 2026-08-14: AnimStateLayer transition queue / instance reuse ---
    0x14189DCD0: (
        "AnimStateLayer_EnqueueTransitionRequest",
        "Appends one bounded AnimStateLayer change request, writing its request "
        "type/state fields and incrementing the four-entry pending queue.",
    ),
    0x1418A0F90: (
        "AnimStateLayer_RemoveTransitionRequestByIndex",
        "Removes one pending AnimStateLayer transition request and compacts the "
        "bounded request array after Taken/Failed/Invalid handling.",
    ),
    0x1418A07A0: (
        "AnimStateLayer_PruneCompletedInstances",
        "Called twice by AnimStateLayer_Step. Compacts each track after an "
        "instance reaches min(+0x158,+0x164)==0, releases removed instances, "
        "then tail-calls the empty/faded-track pruning pass.",
    ),
    0x1418A0990: (
        "AnimStateLayer_PruneEmptyOrFadedTracks",
        "Releases empty tracks and older tracks whose terminal instance has "
        "min(+0x158,+0x164)==0, compacting the layer track array at +0x4F8.",
    ),
    0x1418A1350: (
        "AnimStateLayer_TakeTransitions",
        "Source-identified AnimStateLayer::TakeTransitions. Drains the pending "
        "request queue and publishes the state instance selected by direct-fade "
        "or normal-transition processing through its output pointer.",
    ),
    0x1418A1EA0: (
        "AnimStateLayer_ApplyTransition",
        "Applies a normal authored transition. Its same-state/phase fast path "
        "loops and returns the current instance; otherwise it creates/fades a "
        "new destination instance.",
    ),
    0x1418A27C0: (
        "AnimStateLayer_CreateOrFadeStateInstance",
        "Direct state-fade path. Either logs and loops the current matching "
        "instance or allocates/inserts a new state instance and logs Setting "
        "state. A fresh track is published into layer+0x4F8 at 0x1418A2B97 "
        "and layer+0x500 is incremented at 0x1418A2B9A before snapshot creation "
        "through 0x1418A2CBE -> 0x1418A4030 -> 0x1418A4670. The first track "
        "uses layer+0x18 master overlays; other tracks use track+0x8. The new "
        "instance itself is only inserted into the track at 0x1418A2CF1, "
        "after snapshot creation: live track membership is available during "
        "pose-node construction, but live instance membership is not yet.",
    ),
    0x1418A3820: (
        "AnimStateLayer_UpdateBlendWeights",
        "Sole AnimStateLayer_Step callee that decrements instance fade timers "
        "+0x150/+0x15C and publishes blend weights +0x158/+0x164 from the "
        "durations at +0x154/+0x160.",
    ),
    0x1418A3B70: (
        "AnimStateLayer_CanLoopCurrentToState",
        "Checks current-instance state identity plus request flags and phase "
        "conditions for the direct-fade loop-current fast path.",
    ),
    0x1418A5BF0: (
        "AnimStateInstance_LoopCurrentState",
        "Refreshes phase/blend state on an existing AnimStateInstance without "
        "allocating or inserting a replacement instance.",
    ),
    # --- 2026-08-02: active post-blend foot-step owner ---
    0x141F9D610: (
        "CharacterLegIkController_Construct",
        "Constructs the 0x4560-byte CharacterLegIkController, including the "
        "embedded MoveLegIkNew at +0x1F0, then clears active ILegIk pointers "
        "+0x3E18/+0x3E20 before initialization selects concrete modules.",
    ),
    0x141F9DC30: (
        "CharacterLegIkController_Initialize",
        "CharacterLegIkController vtable slot +0x8. Receives the owning Player "
        "as arg2, stores it at controller+0x8, initializes every embedded leg-IK "
        "implementation with that same owner and selects MoveLegIkNew "
        "controller+0x1F0 into active ILegIk pointer +0x3E18 when empty.",
    ),
    0x142104AC0: (
        "MoveLegIkNew_Construct",
        "Constructs the embedded MoveLegIkNew, installs vtable 0x142BD4B00 and "
        "initializes its per-leg target/history Locators, GroundModel records "
        "and cached contact state.",
    ),
    0x141F9E150: (
        "CharacterLegIkController_ResetIkBuffers",
        "CharacterLegIkController vtable slot +0x28, called immediately before "
        "Update by Player_PostAnimBlending_Async. Resets both leg-chain work "
        "buffers and the two auxiliary joint caches without publishing joints.",
    ),
    0x141F9E190: (
        "CharacterLegIkController_CommitJointTransforms",
        "CharacterLegIkController vtable slot +0x30, called immediately after "
        "Update by Player_PostAnimBlending_Async. Copies valid 32-byte IK joint "
        "results into the current 48-byte-per-joint JointCache local pose, then "
        "resets the temporary chain caches.",
    ),
    0x141F9E6F0: (
        "CharacterLegIkController_Update",
        "Exact character-leg-ik-controller.cpp CharacterLegIkController::Update. "
        "Composes Character world translation/quaternion from Player+0x110..0x140, "
        "updates the active ILegIk implementation and mutates both leg chains before "
        "CharacterLegIkController_CommitJointTransforms publishes their joints.",
    ),
    0x141986AF0: (
        "CharacterLegIkController_InitializeLegJointSet",
        "Initializes one of the controller's left/right JointSets from authored "
        "root/end joint hashes, stores its leg selector at +0x40 and resolves "
        "the six auxiliary leg joint offsets at +0x41..+0x46.",
    ),
    0x14181E780: (
        "JointSet_AllocateAndSeedBuffers",
        "Allocates the five receiver-local JointSet work arrays behind +0x20: "
        "two 32-byte locator arrays, one 16-byte companion array, scalars and "
        "per-joint validity bytes; seeds them from the current JointCache pose. "
        "STATIC: owner=set+8, Fg=owner+1A8. EBA2..EBDE copies nonroot current "
        "Fg+100 local S/Q/T unconditionally and sets localValid=1, without "
        "reading authored references or pose ValidBits. EB5F..EB6E copies "
        "first-row S.x to work-table+28 uniform chain scale. Near-bind getter "
        "values do not prove a bind fallback here. Feather seed27F1 and "
        "terminal seeds184E/1857 reseed from the then-current JointCache.",
    ),
    0x14181F330: (
        "JointSet_FreeWorkBuffersAndClear",
        "Frees the five transient arrays and their 0x50-byte table stored at "
        "JointSet+0x20, then explicitly clears JointSet+0x20. Allocator ID 14 "
        "and the current thread allocator are treated as native transient "
        "owners and skip individual frees. This is the buffer-lifetime half "
        "of the JointTree reset vtable slot.",
    ),
    0x1415A0E90: (
        "Memory_AllocateFromCurrentAllocator",
        "Allocates through Memory::CurrentAllocator. Low-index workers load the "
        "allocator from FLS slot 7; a null slot faults at 0x1415A0EE8 before "
        "the allocator vtable dispatch.",
    ),
    0x1415A15F0: (
        "Memory_PushAllocatorById",
        "Source-asserted Memory::PushAllocator overload. Resolves the allocator "
        "ID through Memory_ResolveAllocatorById, pushes a 40-byte allocator "
        "stack record and publishes the resolved pointer to FLS slot 7.",
    ),
    0x1415A17E0: (
        "Memory_PushAllocatorDirect",
        "Source-asserted Memory::PushAllocator overload for a direct allocator "
        "pointer. Pushes the allocator stack record and publishes the pointer "
        "to FLS slot 7.",
    ),
    0x1415A1B80: (
        "Memory_PopAllocator",
        "Source-asserted Memory::PopAllocator. Removes the current 40-byte "
        "allocator stack record, restores FLS slot 7 to the new top or null, "
        "and returns the allocator that was popped.",
    ),
    0x1415A1D70: (
        "Memory_ResolveAllocatorById",
        "Resolves the compact allocator ID used by Memory_PushAllocatorById. "
        "Ordinary IDs index the global allocator table; dynamic IDs 113..118 "
        "select their context-specific allocator variants.",
    ),
    0x1415A1EB0: (
        "Memory_GetCurrentAllocator",
        "Returns FLS slot 7 for low-index workers, or the top allocator-stack "
        "record for other threads; falls back to the default allocator only "
        "when the non-FLS stack is empty.",
    ),
    0x141821500: (
        "JointTree_ResetTransientBuffers",
        "JointTree vtable slot +0x20. Calls JointSet_FreeWorkBuffersAndClear "
        "and frees the auxiliary allocation at JointTree+0x48; dispatched by "
        "JointSet_CommitJointTransformsAndReset after every stock commit.",
    ),
    0x1418212E0: (
        "JointTree_GetParentJointOffset",
        "JointTree RTTI vtable142AFC890 slot3/+18. Reads the signed parent "
        "ordinal from tree+38, indexed by the supplied joint offset.",
    ),
    0x1418212F0: (
        "JointTree_PropagateWorldInvalidation",
        "JointTree RTTI vtable142AFC890 slot6/+30. Walks the descendant range "
        "from tree+40 and clears world-valid bytes at tree+48 when the parent "
        "is invalid. Does not write any local T/Q or install a bind pose.",
    ),
    0x1418215D0: (
        "JointTree_InvalidateJointWorldSpace",
        "JointTree RTTI vtable142AFC890 slot9/+48. Clears the selected "
        "tree+48 world-valid byte and optionally invokes slot+30 to propagate "
        "invalidation. Feather calls at1418F294F after scaling prop local T; "
        "this invalidation does not overwrite the unchanged local quaternion.",
    ),
    0x141986C60: (
        "LegIkChain_ResetAndSeedFromJointCache",
        "Concrete LegIkChain vtable slot +0x78. Called once per leg by "
        "CharacterLegIkController_ResetIkBuffers; allocates and seeds the "
        "JointSet from the current owner JointCache before refreshing the "
        "authored ankle reference.",
    ),
    0x1418209F0: (
        "JointSet_RefreshWorldLocatorsThroughOrdinal",
        "Concrete LegIkChain and ArmIkChain vtable slot +0x38 used by JointSet_GetJointLocWs. "
        "Composes local 32-byte locators through the chain and advances the "
        "valid world-prefix cursor at JointSet+0x38.",
    ),
    0x141820870: (
        "JointChain_GetPreviousParentOrdinal",
        "ArmIkChain RTTI vtable 0x142B0FF58 slot3/+0x18. Returns ordinal-1 "
        "for the linear chain parent; no body collision or pole-vector logic.",
    ),
    0x141820AA0: (
        "JointChain_InvalidateWorldPrefixFromOrdinal",
        "ArmIkChain RTTI vtable 0x142B0FF58 slot9/+0x48. Lowers the cached "
        "world-valid prefix at +0x38 to the changed ordinal so dependent world "
        "locators are rebuilt. It does not alter arm bend-plane selection.",
    ),
    0x141820AD0: (
        "JointChain_AdvanceWorldPrefixThroughOrdinal",
        "ArmIkChain RTTI vtable142B0FF58 slot11/+0x58. Advances the world-valid "
        "prefix after a refreshed locator. This is not the local-valid byte "
        "array at work-table[4]; rotation helpers do not set that array.",
    ),
    0x141820150: (
        "JointChain_ComposeScaledLocalWithParentWorld",
        "Composes one chain local locator with parent world locator, scaling "
        "local translation by the unchanged uniform scalar at work-table+0x28 "
        "before rigid quaternion composition. STATIC endpoint-preserving "
        "shoulder roll is compatible with this uniform-scale path.",
    ),
    0x1418F0920: (
        "ArmIkChain_Construct",
        "Constructs the type3, 0x50-byte arm JointChain with RTTI vtable "
        "0x142B0FF58. PlayerWeaponIKAction constructor calls it for both "
        "embedded chains at 0x141094F08. Slot+0x20 frees transient work buffers "
        "through JointSet_FreeWorkBuffersAndClear after commit.",
    ),
    0x141686CD0: (
        "JointCache_InitializeAndAllocatePoseBuffers",
        "Initializes JointCache at FgAnimData+0x30. Config 2 allocates the "
        "local JointParams bank at Fg+0x100 as 48*u16(ArtItemSkeleton+0x3C) "
        "at 0x141686DF5..0x141686E28. World Locators +0x108 and matrices "
        "+0x110 use 32/64*group0.workspace_count. STATIC raw skeleton comes "
        "from ArtItemSkeleton+0x40; group0 is raw+u32(raw+0x2C), and its "
        "u16 +0x10 count is read at141686D47 for the locator/matrix allocations "
        "at141686DB9/141686DE4 (configs1/2 with nonzero count). This count is "
        "distinct from group0.parameter_count at+0x12 and the Art+0x3C local "
        "allocation count. Stores Art at cache+C0/Fg+F0 at141686D10 and "
        "raw at cache+C8/Fg+F8 at141686D31. "
        "STATIC raw+16 BYTE group count is read at141686D3A; Art+24 is a "
        "database ID, distinct from raw+4 skeleton ID. The audited normal/"
        "medium NPC assets have Art+30 and raw+16 both6, not a general "
        "native equality guarantee. Matching active Fg+18/+1A0 "
        "alone does not establish this backing-cache identity. Require "
        "actual skeleton/range consistency; do not "
        "infer Pirate extents from C39. C39 has local 66 rows, "
        "world/matrix 104 rows; neither is the full 580-joint workspace or "
        "426-row packed reference pose. Readable virtual memory beyond the "
        "3168-byte C39 local allocation grants no row ownership.",
    ),
    0x141687150: (
        "JointCache_ResetAndSeedGroup0Pose",
        "Resets group-0 world/matrix caches and validity, then at "
        "0x1416872CB..0x141687353 seeds exactly group0.parameter_count "
        "48-byte numeric JointParams from rawSkeleton+u32(rawSkeleton+0x20) "
        "into JointCache+0xD0 (Fg+0x100). C39 seeds 66 rows, not fingers "
        "in group 1. Scale.xyz/Q/translation.xyz are values; only the two "
        "scale/translation W lanes may contain nonfinite service bits.",
    ),
    0x1416878F0: (
        "JointCache_InvalidateGroup0WorldCacheAndSetRoot",
        "Prepares the group-0 matrix publication target: clears JointCache+0x40 "
        "validity bits 0 through group0.workspace_count (inclusive native helper "
        "0x14160D8E0), copies the supplied 64-byte root matrix into JointCache+0, "
        "clears byte +0xF8 and returns the matrix bank +0xE0 (Fg+0x110). "
        "Callsites 0x141A9170C, 0x141A9257C and 0x141A92BA5. Async passes "
        "Fg+0x30/Fg+0x1C0 before its group evaluation, not a finger local bank.",
    ),
    0x141687970: (
        "JointCache_ComputeJointWorldLocatorAndMarkValid",
        "STATIC reads the joint matrix at cache+E0 bank, stride64, and composes "
        "it with the copied root matrix at cache+0. Converts the result into "
        "the world Locator at cache+D8 bank, stride32, and sets the joint bit "
        "in the valid bitset at cache+40. Does not reread owner+130/+140 or "
        "validate an owner-root generation; a changed actor root alone does "
        "not refresh the copied root or invalidate cached joint worlds.",
    ),
    0x141687C90: (
        "JointCache_GetJointWorldLocatorLazy",
        "STATIC descriptive name. If cache+E0 matrix bank is present and the "
        "joint's valid bit at cache+40 is clear, calls141687970; returns the "
        "32-byte Locator from cache+D8. Uses the cache's copied root matrix "
        "at +0 and valid bits, not an owner-root generation check. A cache "
        "hit can therefore retain a world Locator from before a raw root edit.",
    ),
    0x141688320: (
        "JointCache_RecomputeWorldLocatorFromLocalPose",
        "Reads one 48-byte local-pose row from the JointCache, resolves and "
        "composes its parent world Locator, writes the joint's 32-byte world "
        "Locator, and marks that joint valid. ABI: RCX=JointCache, EDX=joint. "
        "Guard 0x14168834F requires joint < group0.parameter_count: C39 "
        "indices >=66 return without work. The scale read at 0x141688361 "
        "comes from local row zero; Q/T use the selected row+0x10/+0x20. "
        "This is not a recompute API for the complete global skeleton.",
    ),
    0x14181ECD0: (
        "JointSet_CommitJointTransformsAndReset",
        "Copies each valid numeric JointSet result into the live 48-byte-per-joint "
        "JointCache local pose using the receiver-local index array at +0x18, "
        "then dispatches the JointSet reset slot. It performs no local-bank "
        "extent check; authored indices must already belong to that bank. "
        "Its full-row stores do not authorize arbitrary global finger indices. "
        "Two-argument ABI: RCX=set, DL=flag; preserve flag and RAX result. "
        "ArmIkChain reset frees work buffers, so post-commit pointers expire.",
    ),
    0x14181F450: (
        "JointSet_ApplyRelativeJointRotation",
        "RCX=set, RDX=outQ16, R8D=ordinal, R9=worldDeltaQ16. Builds normalized "
        "conjugate(parentWS.Q)*worldDelta*parentWS.Q and PRE-multiplies local Q "
        "through14181F640. ArmIkChain invalidates world prefix, but does NOT "
        "set local-valid bytes; require already valid local rows. Applying D "
        "at shoulder then conjugate(D) at wrist restores distal world Q.",
    ),
    0x14181F640: (
        "JointSet_PreMultiplyLocalRotationAndInvalidateWorld",
        "PRE-multiplies normalized input Q into local Q at work-table[0] "
        "+0x20*ordinal+0x10. Calls vtable+0x48: for ArmIkChain this invalidates "
        "world prefix141820AA0, NOT local-valid bytes at work-table[4]. "
        "No allocation/free. Previous MarkValid name was misleading.",
    ),
    0x14181F710: (
        "JointSet_PostMultiplyLocalRotationAndInvalidateWorld",
        "Post-multiplies the selected JointSet locator rotation by the supplied "
        "normalized quaternion, stores the result in buffers[0]+0x20*index+0x10, "
        "then invalidates ArmIkChain world prefix through vtable +0x48; it does "
        "NOT set local-valid bytes. WeaponIK_Apply computes "
        "inverse(current wrist WS) * desired wrist WS and uses this exact helper "
        "to make the wrist orientation reach its target before commit.",
    ),
    0x1420FDA00: (
        "MoveLegIkNew_BindLegJointSets",
        "MoveLegIkNew initialization slot. Binds its left/right JointSet pointers "
        "to CharacterLegIkController+0x3E30/+0x3E78; the second chain is exactly "
        "the first plus the 0x48-byte JointSet stride.",
    ),
    0x1420FE790: (
        "MoveLegIkNew_DoLegIk",
        "Exact ILegIk::DoLegIk source assertion and MoveLegIkNew vtable +0x68. "
        "Consumes the two numeric final foot targets, applies per-leg rotations "
        "into the bound JointSets and returns immediately before stock commit.",
    ),
    0x141FA0F30: (
        "CharacterLegIkController_PostAnimUpdate",
        "Exact CharacterLegIkController::PostAnimUpdate source assertion. Dispatches "
        "the active ILegIk vtable +0x28 post-animation callback and resets the two "
        "leg-chain temporary buffers.",
    ),
    0x1420FFD00: (
        "MoveLegIkNew_ApplyLegToGround",
        "Exact MoveLegIkNew::ApplyLegToGround source assertion. Composes the "
        "current ankle world locator with the static per-foot contact reference at "
        "MoveLegIkNew+0x3A90+0x20*leg, uses Character+0x100 world translation and "
        "solves that leg against the retained ground result.",
    ),
    0x1421010D0: (
        "MoveLegIkNew_Update",
        "MoveLegIkNew vtable slot +0x20. Waits for the prior feet-planted job, "
        "updates both ankle chains and calls MoveLegIkNew_ApplyLegToGround exactly "
        "once for each leg before CharacterLegIkController commits the joints.",
    ),
    0x142101330: (
        "MoveLegIkNew_UpdateFeetPlanted",
        "Exact MoveLegIkNew::UpdateFeetPlanted source assertion. Rebuilds the active "
        "AnimStateSnapshot, invokes MoveLegIkNew_UpdateNextFootsteps for its two "
        "phase windows, composes the two captured ankle world locators with static "
        "contact references into +0x3B80/+0x3B90 ground-probe origins, deep-copies "
        "the aggregate GroundModel at 0x14210202E, then immediately consumes "
        "+0x2C/+0x30 admission and calls MoveLegIkNew_FindFootPlantGround.",
    ),
    0x142102690: (
        "GroundModel_DeepCopy",
        "Deep-copies one complete 0x12E0-byte GroundModel aggregate. The exact "
        "UpdateFeetPlanted call at 0x14210202E returns immediately before the "
        "first final admission-phase reads at 0x142102033/0x142102043.",
    ),
    0x142102DF0: (
        "MoveLegIkNew_UpdateNextFootsteps",
        "Exact MoveLegIkNew::UpdateNextFootsteps source assertion. Filters authored "
        "foot-effect entries with exactly three key/value parameters, resolves "
        "heel/ball and ankle references, writes the two "
        "per-leg target/history blocks and reads, but does not initialize or write, "
        "the static contact references at +0x3A90/+0x3AB0. Its source Character "
        "arg2 is optimized dead: direct callers do not materialize RDX and the "
        "callee overwrites it at 0x142102EDD before any possible use.",
    ),
    0x141918750: (
        "AnimSnapshotNodeAnimation_CollectEffectEntries",
        "AnimSnapshotNodeAnimation vtable slot +0xB8. Passes the concrete "
        "animation asset/clip owner at node+0x50 and the requested phase window "
        "to AnimationAsset_CollectEffectEntriesForWindow.",
    ),
    0x14191EF80: (
        "AnimSnapshotNodeBlend_CollectEffectEntries",
        "AnimSnapshotNodeBlend vtable slot +0xB8. Recurses into one or both "
        "selected children according to the blend weight at +0x60; it does not "
        "synthesize a turn-step event.",
    ),
    0x141AD5570: (
        "AnimSnapshotNodeUnary_CollectEffectEntries",
        "Shared Unary, HandFix, ArmIk, LegFix and IK vtable +0xB8 delegate. "
        "Forwards effect collection to the child at +0x40.",
    ),
    0x141918540: (
        "AnimationAsset_CollectEffectEntriesForWindow",
        "Collects authored animation effects across the requested phase window, "
        "delegates table decoding to AnimationEffectTable_CollectWindow and "
        "removes entries below the min-blend threshold.",
    ),
    0x1418BBCF0: (
        "AnimationEffectTable_CollectWindow",
        "Wrapper around BuildAnimationEffectEntryList for the immutable authored "
        "clip-effect table. Result entries retain process-local pointers into the "
        "asset and must not be synthesized from network data.",
    ),
    0x1421043F0: (
        "MoveLegIkNew_PostAnimUpdate",
        "Exact MoveLegIkNew::PostAnimUpdate source assertion and vtable slot +0x28. "
        "Builds the feet-planted work item consumed on the next update and schedules "
        "MoveLegIkNew_UpdateFeetPlantedJob.",
    ),
    0x142104A80: (
        "MoveLegIkNew_UpdateFeetPlantedJob",
        "Worker callback named MoveLegIkNewUpdateFeetPlantedJob by its job record. "
        "Forwards the captured Character, locators and mode into "
        "MoveLegIkNew_UpdateFeetPlanted.",
    ),
    0x14181DA50: (
        "JointSet_GetJointLocWs",
        "Exact JointSet::GetJointLocWs source assertion. Validates the joint "
        "offset, dispatches JointSet vtable +0x38 to refresh it and returns the "
        "32-byte world transform at the selected cache slot.",
    ),
    0x14181D8E0: (
        "JointSet_GetJointLocLs",
        "Exact JointSet::GetJointLocLs source assertion. Validates the chain "
        "offset and local-valid byte, then returns the selected 32-byte local "
        "transform without dispatching the world-cache refresh vslot.",
    ),
    0x142103CE0: (
        "MoveLegIkNew_GetPredictedFootPointsWs",
        "MoveLegIkNew vtable +0x88. Ensures the two foot contact offsets, rotates "
        "each by its current ankle JointSet world quaternion and adds the ankle "
        "world translation to return two predicted foot points.",
    ),
    0x142105090: (
        "MoveLegIkNew_CacheCharacterBoundTransform",
        "Copies the Character 0x50-byte BoundTransform from +0x100 into "
        "MoveLegIkNew+0x180, resolves its world translation, applies the cached "
        "root-height component and marks the cache valid at +0x1D0.",
    ),
    0x142105110: (
        "MoveLegIkNew_EnsureFootJointOffsets",
        "Lazily resolves the authored right/left ankle, heel and ball joints and "
        "initializes the two per-foot contact offsets consumed at +0x3A90/+0x3AB0.",
    ),
    0x142103E10: (
        "MoveLegIkNew_FindFootPlantGround",
        "Exact MoveLegIkNew::FindFootPlantGround source assertion. Reads the three "
        "world-space probe points beginning at MoveLegIkNew+0x3B80, submits the "
        "bounded GroundModel queries and waits for their results.",
    ),
    0x1421043A0: (
        "MoveLegIkNew_FindFootPlantGroundJob",
        "Private job callback built by MoveLegIkNew_FindFootPlantGround. Selects "
        "five or twenty samples and forwards the captured three points to "
        "GroundModel_DoFindGround.",
    ),
    0x142104EC0: (
        "MoveLegIkNew_ProjectAdmittedTargetsToGround",
        "MoveLegIkNew vtable slot +0x98. For each leg whose +0x2C/+0x30 "
        "admission phase is nonnegative, resolves the +0x40/+0x90 target record "
        "and projects its point through the corresponding embedded GroundModel.",
    ),
    0x142129D00: (
        "GroundModel_DoFindGround",
        "Exact GroundModel::DoFindGround source assertion from ik-ground-model.cpp. "
        "Builds collision probes between the supplied world points and stores the "
        "selected ground result in the caller-owned GroundModel record.",
    ),
    0x14212BB20: (
        "GroundModel_ConstructGroundFromContacts",
        "Exact GroundModel::ConstructGroundFromContacts source assertion. Builds "
        "the retained ground path/contact vectors and validity state from the "
        "collision samples; it does not admit or create a footstep target.",
    ),
    0x141897030: (
        "GetProcessDeltaTime",
        "Exact ndlib process clock source assertions name GetProcessDeltaTime. "
        "Returns the current process clock delta converted to seconds.",
    ),
    0x141728A80: (
        "AnimControl_GetJointCache",
        "Exact AnimControl::GetJointCache source assertion. Validates m_pAnimData at "
        "+0x20 and returns its joint-cache object at +0x30.",
    ),
    # --- 2026-08-02: ADS turn-in-place authored action lifecycle ---
    0x140C59180: (
        "PlayerCommonAction_OnStart",
        "PlayerCommonAction vtable slot +0x10. PlayerMoveSequencer changes "
        "action lifecycle +0x18 from 0 to 1 immediately before this call. "
        "Dispatches the animation updater at action+0x40 first, followed by "
        "the movement updater at +0x30 and optional updater at +0x38.",
    ),
    0x142053590: (
        "PlayerMoveController_ReplaceChannel0Action",
        "Removes every current action from channel zero of the controller's "
        "PlayerMoveSequencer at controller+0x110, then enqueues the supplied "
        "replacement through PlayerMoveSequencer_EnqueueControllerAction. "
        "GunMoveController_BuildActionState uses this for aim-idle-rotate and "
        "aim-idle-rotate-cc.",
    ),
    # --- 2026-07-27: ordinary Player evade/run-roll acceptance chain ---
    0x140DC31E0: (
        "PlayerEvadeController_SetStick",
        "PlayerEvadeController vtable slot 9. Source assertion names "
        "PlayerEvadeController::SetStick in player-evade.cpp; selects authored "
        "run^evade, run^evade-180 and idle^evade transitions and can enqueue "
        "PlayerCommonAction records at 0x140DC4775/0x140DC49B5.",
    ),
    0x140DC5670: (
        "PlayerEvadeController_IsEvadeStartActive",
        "PlayerEvadeController vtable slot 10. Hashes authored evade-start "
        "(0x54E63318) and calls Joypad_IsCommandActive with edge=0. Ordinary "
        "PlayerMoveStick_MainUpdate consumes it at 0x140F06408; the separate "
        "outer verb orchestrator also consumes it at 0x140F60C8C.",
    ),
    0x140DC59C0: (
        "PlayerEvadeController_IsEvadeHoldActive",
        "PlayerEvadeController vtable slot 11. Hashes authored evade-hold and "
        "queries the owning Player Joypad with held semantics (edge=0). It is "
        "not the ordinary run-roll start selector.",
    ),
    0x140DC5AE0: (
        "PlayerEvadeController_IsEvadeRollActive",
        "PlayerEvadeController vtable slot 13. Hashes authored evade-roll "
        "(0x3F1B1FF5) and queries edge=0. The ordinary PlayerMoveStick run-roll "
        "chain does not call this slot, so it must not be aliased to evade-start.",
    ),
    0x140DC5F50: (
        "PlayerEvadeController_CanStartEvade",
        "PlayerEvadeController vtable slot 14, called at 0x140F064C9 after the "
        "ordinary evade-start query. Applies controller/owner eligibility, "
        "cooldown and network cloaked-evade gates, including authored recharge "
        "and sprint settings.",
    ),
    0x140DC5C10: (
        "PlayerEvadeController_VehicleCheck",
        "PlayerEvadeController vtable slot 15. Exact player-evade.cpp source "
        "signature is PlayerEvadeController::VehicleCheck(SMath::Vector); builds "
        "a Havok sphere-cast probe along the candidate evade direction. Ordinary "
        "PlayerMoveStick requires true at 0x140F0653F before starting the action.",
    ),
    0x140DC67D0: (
        "PlayerEvadeController_StartEvadeAction",
        "PlayerEvadeController vtable slot 16 and accepted ordinary evade "
        "boundary at PlayerMoveStick callsite 0x140F0656A. Selects roll, "
        "sprint-roll or land-roll plus run^evade/idle^evade, constructs "
        "PlayerCommonAction 0x13B4BBC1 and enqueues it at 0x140DC7820.",
    ),
    # --- 2026-07-23: grenade native launch descriptor and authored action ---
    0x14154B3B0: (
        "EmitGrenadeProjectile",
        "Builds the finite grenade launch vector and passes it as R9 to "
        "EmitProjectileProcess. The authored throw-grenade animation marker "
        "reaches its sole Player projectile callsite at 0x14040D852.",
    ),
    0x14154AAE0: (
        "EmitProjectileProcess",
        "Exact projectile-process.cpp source assertion. Copies the validated "
        "16-byte R9 launch vector into the shared process-spawn descriptor at "
        "+0x50 (0x14154ACCB..0x14154ACD4). ProjectileGrenade::Init adds this "
        "field to the spawn translation before building a respawn transform; "
        "it is not a direct NetGrenade rigid-body velocity sink.",
    ),
    0x141533560: (
        "ProjectileGrenade_InitFromSpawnPacket",
        "Common ProjectileGrenade initializer (vtable +0x58). Reads packet "
        "+0x28, adds descriptor+0x50 to the bound world translation at "
        "0x141533A61..0x141533A66, then builds the respawn transform. The "
        "ordinary authored-marker route continues into native active update.",
    ),
    0x141540D60: (
        "NetGrenade_InitNetworkStateThenProjectileBase",
        "NetGrenade vtable +0x58 wrapper. Reads the same packet+0x28 descriptor, "
        "initializes network-only state from descriptor+0xE0, then calls "
        "ProjectileGrenade_InitFromSpawnPacket at 0x141540EAD.",
    ),
    0x1410E71D0: (
        "Player_ProcessWeaponAndGrenadeInputActions",
        "Player input scheduler. For grenade entry it queries exact command "
        "grenade-arc as a level every frame at 0x1410E78E6, calls "
        "FastGrenadeController vtable +0xA8 CanStart at 0x1410E7C7D, then "
        "+0x40 Start at 0x1410E7DC0. A relayed held level therefore needs a "
        "per-press consumed latch at this scheduler query, while the separate "
        "state-1/state-2/state-4 queries must retain the level through release.",
    ),
    0x140E25C10: (
        "PlayerGrenadeController_LatchArcInhibitFromJoypad",
        "FastGrenadeController vtable +0xA0. Independently queries grenade-arc "
        "through the owner's Joypad and, when active, sets controller+0x1A0 "
        "bit2 at 0x140E25D09. This is an inhibit/cancel path, not scheduler "
        "entry.",
    ),
    0x140E25540: (
        "PlayerGrenadeController_CanStart",
        "FastGrenadeController vtable +0xA8 start eligibility. Rejects global, "
        "owner/action and weapon-state gates and specifically returns false "
        "when controller+0x1A0 bit2 is set at 0x140E255F4.",
    ),
    0x140E19600: (
        "PlayerGrenadeController_StartArc",
        "FastGrenadeController vtable +0x40. Rechecks CanStart, clears and "
        "initializes held-grenade/context state (including record 2 at "
        "controller+0x148), resolves settings and trajectory, then writes "
        "controller state 1 at 0x140E19745. The selected art slot is 0 at "
        "+0x128, so its native pre-StartArc Bind survives this clear.",
    ),
    0x140E21310: (
        "PlayerGrenadeController_UpdateStateAndCommitThrow",
        "Core grenade controller update. States 1/2/4 query semantic command "
        "grenade-arc (0x45ABCE38) through owner+0x39A0 at 0x140E218DD, "
        "0x140E21A8F and 0x140E2227D. State 2 first checks the absolute-tick "
        "deadline at controller+0x1E8 and enters post-arc state 3/4. State 4 "
        "also requires its current-action predicate 0x140E24ED0 before reaching "
        "the command query. All three state queries must retain the relayed "
        "held level until true release; only scheduler entry is consumed. The "
        "ResetToIdle call returning at 0x140E22333 is shared: state 7 reaches "
        "it on deadline expiry, while state 8 reaches it when the authored "
        "action-state lookup is absent.",
    ),
    0x140E1F5B0: (
        "PlayerGrenadeController_EnterPostArcHoldState",
        "State-2 absolute-deadline continuation. Re-resolves grenade context "
        "and trajectory, initializes held/gesture state, then writes state 3 "
        "for grenade-dynamite at 0x140E1FA61 or ordinary state 4 at "
        "0x140E1FB08. State 4 owns another grenade-arc hold/release query.",
    ),
    0x140E1FF90: (
        "PlayerGrenadeController_ReleasePostArcHold",
        "State-4 false-command continuation at 0x140E222BC. Depending on "
        "controller+0x540, either starts the authored throw action immediately "
        "or stores the current deadline at +0x530 and writes state 7, whose "
        "update later starts the throw or times out to idle.",
    ),
    0x140E19D60: (
        "PlayerGrenadeController_EnsureGrenadeInHand",
        "Validates FastGrenade weak record 3 at controller+0x158/+0x160 and "
        "reattaches the same live ProcessGrenadeInHand when present. If absent, "
        "materializes exactly one ProcessGrenadeInHand and stores its weak "
        "node/generation at 0x140E1A1D3. This held visual is not a projectile.",
    ),
    0x140E1A7B0: (
        "PlayerGrenadeController_DestroyHeldGrenadeAndEffects",
        "Lifecycle cleanup for held record 3 and its child/effect records. "
        "Generation-validates the ProcessGrenadeInHand then calls "
        "ProcessKillTree at 0x140E1A803; cleanup invalidates object lifetime "
        "rather than directly zeroing controller+0x158.",
    ),
    0x140E1AA00: (
        "PlayerGrenadeController_RefreshHeldGrenadeForSelection",
        "Grenade-selection housekeeping. Kills the prior valid record-3 "
        "ProcessGrenadeInHand at 0x140E1AABC, clears related held effects, then "
        "calls PlayerGrenadeController_EnsureGrenadeInHand at 0x140E1AAFE for "
        "the newly selected ProcessWeapon.",
    ),
    0x140E21270: (
        "PlayerGrenadeController_ReleaseArcAndAdvanceThrow",
        "Release path reached when grenade-arc becomes false. Clears the arc "
        "phase and calls PlayerGrenadeController_StartThrowAction.",
    ),
    0x140E1B900: (
        "PlayerGrenadeController_Release",
        "FastGrenadeController::Release string anchor. Builds the authored "
        "grenade-player-*-thrown event, re-resolves the throw pose through the "
        "aim/blindfire helpers, calls the native released-projectile "
        "materializer 0x140E1C3A0, then destroys the held grenade/effects.",
    ),
    0x140E1C3A0: (
        "PlayerGrenadeController_SpawnReleasedProjectile",
        "Native FastGrenade release materializer called only by Release at "
        "0x140E1C2FD. Resolves the grenade art recipe, creates either a "
        "ProjectileThrowable or ordinary ProjectileGrenade process, applies "
        "the computed throw transform/launch data, and sends "
        "NetEventEmitGrenade in native online mode. This is the Player path; "
        "it is independent of the AI BehaviorThrowGrenade marker handler.",
    ),
    0x140E22ED0: (
        "PlayerGrenadeController_UpdateArc",
        "FastGrenadeController::UpdateArc string anchor. Selects the default, "
        "aimed or blindfire pose helper and publishes the current arc state.",
    ),
    0x140E1B550: (
        "PlayerGrenadeController_ProjectPointAlongCameraForward",
        "Shared UpdateArc/Release helper. Resolves the owner screen pose seed "
        "and consumes only its quaternion at seed+0x10 to project a point.",
    ),
    0x140E22730: (
        "PlayerGrenadeController_BuildDefaultArcPose",
        "Builds the default grenade arc pose and re-resolves the owner screen "
        "pose quaternion for UpdateArc.",
    ),
    0x140E229C0: (
        "PlayerGrenadeController_BuildAimedArcOrThrowPose",
        "Shared aimed-mode helper used by both UpdateArc and Release. Re-reads "
        "the owner screen pose quaternion through the common resolver.",
    ),
    0x140E22B80: (
        "PlayerGrenadeController_BuildBlindfireArcOrThrowPose",
        "Shared blindfire helper used by both UpdateArc and Release. Re-reads "
        "the owner screen pose quaternion through the common resolver.",
    ),
    0x140E20050: (
        "PlayerGrenadeController_StartThrowAction",
        "Creates authored grenade-starting-throw action 0x6A1381B4. Cover "
        "distance abort calls vtable +0x60 at 0x140E201E8 (return E201EB); the "
        "online net-can-throw-func false branch does the same at 0x140E20348 "
        "(return E2034B). Success dispatches at 0x140E2054F and writes state 8 "
        "at 0x140E205A2.",
    ),
    0x140E1D850: (
        "PlayerGrenadeController_ResetToIdle",
        "FastGrenadeController vtable +0x60. Publishes aborted-grenade-aim, "
        "destroys held grenade/effects at 0x140E1DC44, clears controller state "
        "to 0 at 0x140E1DC8F and refreshes the arc. Important callers are the "
        "StartThrowAction cover abort (return E201EB), online net gate abort "
        "(E2034B), and the shared state-7 timeout/state-8 missing-action abort "
        "(E22333).",
    ),
    0x140E17070: (
        "PlayerGrenadeController_CreateGrenadeWeaponFromLoadout",
        "FastGrenadeController vtable +0x30. Creates the grenade ProcessWeapon "
        "through Player+0x1D88 vtable +0x130, selecting its initial ammo from "
        "the loadout and net-player stats, then passes the result to vtable "
        "+0xF0 at 0x140E176D5.",
    ),
    0x140E17720: (
        "PlayerGrenadeController_BindGrenadeWeaponSlot",
        "FastGrenadeController vtable +0xF0. Uses the weapon art registry slot "
        "index and copies the ProcessWeapon weak handle from weapon+0xA8/+0xBC "
        "into one of six controller records at +0x128+slot*0x10, then performs "
        "WeaponHolster_BuildBinding/attachment presentation and controller "
        "registry side effects. Native loadout calls this before StartArc; a "
        "late mid-aim Bind can overlap its ProcessWeapon presentation with the "
        "separate ProcessGrenadeInHand. Do not forge the weak record directly.",
    ),
    0x140E1A9C0: (
        "PlayerGrenadeController_GetGrenadeWeaponBySlot",
        "Resolves one six-entry controller+0x128 weak-handle record by explicit "
        "grenade slot, validates weapon+0xBC and returns the ProcessWeapon.",
    ),
    0x140E1AD40: (
        "PlayerGrenadeController_GetSelectedGrenadeWeapon",
        "FastGrenadeController vtable +0xD0. Resolves the weak-handle record at "
        "controller+0x128+0x10*slot selected by controller+0x18C.",
    ),
    0x140E20850: (
        "PlayerGrenadeController_HasUsableSelectedGrenade",
        "State-1 release gate. Requires a valid selected ProcessWeapon whose "
        "vtable +0x840 GetTotalAmmo returns positive, plus the controller flag "
        "condition; false returns state 1 to idle without authored throw.",
    ),
    0x1404885C0: (
        "BehaviorThrowGrenade_Throw_Update",
        "BehaviorThrowGrenade::Throw vtable 0x142941800 slot +0x28. Resolves "
        "the AI/NPC animation marker throw-grenade 0x6333DA76, calls the NPC "
        "grenade emitter, then publishes ai-enemy-combat-projectile-tell. "
        "It is not the Player FastGrenade authored-action handler.",
    ),
    0x14040CEF0: (
        "Npc_EmitGrenadeFromThrowMarker",
        "AI/NPC grenade emission continuation called only by "
        "BehaviorThrowGrenade_Throw_Update. Its sole projectile callsite "
        "0x14040D852 invokes EmitGrenadeProjectile; this is not the Player "
        "FastGrenade release materializer.",
    ),
    # --- 2026-07-22: anim-overlay-set runtime consumer and layout ---
    0x140516230: (
        "AnimationDatabase_ResolveClipWithOverlays",
        "Resolves an input animation StringId through the per-instance "
        "AnimOverlayState first, then through AnimSetRemap_ResolveAnimationId, "
        "and finally looks up the resulting clip in AnimationDatabase. Its "
        "ordinary resolver handles type-0 and grouped type-2/3/4 remaps, not "
        "type-1/5 pose or state children. A direct result here does not rule "
        "out the separate node-local pose-overlay path at 0x141918C30.",
    ),
    0x1417F92A0: (
        "AnimOverlays_NotifyDcUpdated",
        "Exact source assertion names AnimOverlays::NotifyDcUpdated. Re-resolves "
        "the registered overlay records after DC changes, preserves compatible "
        "per-slot records, resizes the state when the registry schema grows, and "
        "refreshes every retained overlay slot.",
    ),
    0x1417F9B30: (
        "AnimOverlayState_Initialize",
        "Initializes one per-instance overlay state. Allocates the overlay-set "
        "pointer array at +0x8, the parallel value-id array at +0x10 and the "
        "four-entry variant history at +0x18; stores set count at +0x24 and "
        "random/flags state at +0x25/+0x26.",
    ),
    0x1417F9DD0: (
        "AnimOverlayState_Copy",
        "Copies one per-instance overlay state from source a2 to destination a1: "
        "set pointers, value ids, hash, four-entry variant history, set count and "
        "random/flags state. AnimStateLayer_Step calls it at 0x1418A3725 with "
        "layer+0x18 as the Player-owned master source and the first track's +0x8 "
        "state pointer as the destination. The copied track route may differ "
        "from the master's later current route. Values[] contains overlay "
        "object ids (set+0x10), not selectors (set+0x18); see1417F9150. "
        "Current master alone cannot attribute an already resolved leaf.",
    ),
    0x1417F9F00: (
        "AnimOverlayState_ResolveAnimationId",
        "Ordinary animation-id resolver: type-0 rows replace the current id "
        "with row+0x4; types 2/3/4 choose grouped variants. Other types, "
        "including type 1 and 5, advance the cursor without replacing the id. "
        "Follows remap chains up to 100 steps and diagnoses cycles by input "
        "animation and overlay-set id. It does not materialize pose children: "
        "that is the separate 0x141918C30 consumer, while state children are "
        "handled by AnimStateSnapshot_TransformState. Therefore an unchanged "
        "outer id is not evidence that type-1 pose rows were absent.",
    ),
    0x1417FA3C0: (
        "AnimOverlayState_SelectVariant",
        "Selects and records one grouped overlay-row variant in the four-entry "
        "history at state+0x18. Supports sequential or randomized selection and "
        "refreshes the state hash after updating the selected index.",
    ),
    # --- 2026-07-22: Player Look2 -> player-mesh animation knowledge ---
    0x140A1DCC0: (
        "Player_GetAppearanceHash",
        "Player vtable slot +0x2E0 (index 92). Returns Player+0x15A4 through "
        "the caller-provided StringId output. The legacy Nadine compatibility "
        "hook overrides this return only at NdGameObject_Init return "
        "0x141BB4AB9 (call 0x141BB4AB2), not at arbitrary per-frame reads. "
        "It does not change Player+0x15A0 or animation knowledge, nor rebuild "
        "an existing look. A selected hash alone is not rendered-mesh proof: "
        "Init can use ProcessSpawnInfo+0x98 after querying this getter. "
        "CONFIRMED limited runtime 2026-09-20look-init PID23264: exact "
        "Init callback returned D27C074F for player==retained0, followed "
        "by a user screenshot showing Nadine. This confirms the tested "
        "cold-load appearance path, not pistol grip or AFE's prior failure cause.",
    ),
    0x1410D6AD0: (
        "Player_ApplyLook2MeshKnowledge",
        "Resolves Player+0x15A0 as a Look2, formats the exact "
        "typed dashed player-mesh/Look2-name registry key, and commits the "
        "resolved record "
        "through the wrapper at [Player+0x1A0]+0x60 into the Player master "
        "animation-overlay state, with player-mesh/default as fallback. Also "
        "commits mesh/default and female/default. Called during top-level Player "
        "initialization and by Player_ChangeLookId.",
    ),
    0x141800C00: (
        "RenderLook_ResolveLook2ById",
        "ndlib/render/look.cpp Look2 registry lookup used by "
        "Player_ApplyLook2MeshKnowledge. Resolves the StringId supplied from "
        "Player+0x15A0 into the small render-look handle consumed by the type "
        "validator.",
    ),
    0x141801400: (
        "RenderLook_GetValidatedLook2",
        "Validates the resolved render-look handle as type look2 and returns "
        "the typed record whose name is used to form player-mesh/<Look2-name>.",
    ),
    # --- 2026-07-22: DrawControl LOD/FgInstance render ownership ---
    0x141751E30: (
        "IDrawControl_AddMesh",
        "Exact IDrawControl::AddMesh source assertion. Materializes the mesh's "
        "m_availableLods entries (stride 0x20) and allocates every FgInstance "
        "against the one FgAnimData pointer stored at DrawControl+0.",
    ),
    0x141751BB0: (
        "IDrawControl_Construct",
        "Exact IDrawControl::IDrawControl(FgAnimData*, unsigned int) source "
        "assertion. Stores the owning FgAnimData at +0 and allocates the "
        "fixed-stride mesh-entry array later consumed by IDrawControl_AddMesh.",
    ),
    0x1417CB100: (
        "ArtItemGeo_ResolveFgGeometry",
        "Looks up ArtItemGeo+0x18 resource id in the global geometry registry "
        "and returns the shared FgGeometry pointer stored at the matched "
        "registry payload+0x10. IDrawControl_AddMesh passes this result, not "
        "the ArtItemGeo pointer, to FgInstance_Init for every LOD.",
    ),
    0x141753200: (
        "IDrawControl_NotifyMeshUnloaded",
        "Exact IDrawControl::NotifyMeshUnloaded source assertion. Iterates the "
        "mesh m_availableLods entries and transitions their live/freed instance "
        "pointers; the entries are LODs, not camera-specific copies.",
    ),
    0x141752B30: (
        "IDrawControl_ReleaseMeshLodInstances",
        "Releases both the live and parked FgInstance pointers for every "
        "available LOD in one DrawControl mesh entry when its ArtItemGeo is "
        "present. Shared by individual removal and whole-control teardown.",
    ),
    0x141752FB0: (
        "IDrawControl_RemoveAllMeshes",
        "Walks the complete DrawControl mesh array, releases every mesh through "
        "IDrawControl_ReleaseMeshLodInstances, and removes each fixed-stride "
        "entry from the array.",
    ),
    0x1417539C0: (
        "IDrawControl_Destroy",
        "Runs IDrawControl_RemoveAllMeshes, releases the ambient-occluder "
        "selection array, and clears the remaining DrawControl-owned storage. "
        "Normal NdGameObject teardown calls this before AnimMgr_FreeAnimData.",
    ),
    0x141753560: (
        "IDrawControl_Relink",
        "Exact IDrawControl::Relink source assertion. Reallocates each LOD "
        "m_pInstance against DrawControl+0 FgAnimData, initializes it from the "
        "retained ArtItemGeo and frees the prior m_pFreedInstance.",
    ),
    0x141819F30: (
        "FgDrawMgr_AllocateInstance",
        "Exact FgDrawMgr::AllocateInstance source assertion. Allocates a 0x280-"
        "byte FgInstance from the global pool and appends its pool index to the "
        "FgAnimData driven-instance list at +0x210/+0x20C.",
    ),
    0x14181A2A0: (
        "FgDrawMgr_FreeInstance",
        "Exact FgDrawMgr::FreeInstance source assertion. Detaches the pool "
        "instance index from its FgAnimData and releases the same 0x280-byte "
        "FgInstance slot.",
    ),
    0x14181A5A0: (
        "FgDrawMgr_DetachInstanceFromAnimData",
        "Removes one FgInstance pool index from FgAnimData+0x210 with a swap-"
        "delete and decrements +0x20C; the source assertion verifies the driven "
        "instance count falls by exactly one.",
    ),
    0x14181A760: (
        "FgDrawMgr_FrameBegin",
        "Exact FgDrawMgr::FrameBegin source assertion. Waits for the GPU EOP "
        "tick from three render frames earlier, resets each eligible "
        "m_deletedInstanceQueue slot through FgInstance_Reset and clears its "
        "pending-delete bit.",
    ),
    0x14181C1B0: (
        "FgDrawMgr_ForAllUsedInstances",
        "Exact FgDrawMgr::ForAllUsedInstances source assertion. Iterates the "
        "union of both used-instance masks while explicitly excluding the "
        "pending-delete mask before invoking the callback.",
    ),
    0x14181C430: (
        "FgDrawMgr_ForAllInstances",
        "Exact FgDrawMgr::ForAllInstances source assertion. Iterates the "
        "caller-selected instance mask over the same 0x280-byte global pool.",
    ),
    0x14192E590: (
        "FgInstance_Init",
        "Initializes a newly allocated FgInstance from its FgAnimData owner and "
        "resolved FgGeometry. Stores the shared FgGeometry pointer at "
        "FgInstance+0x1B0, captures the retained owner id at +0x250, and sets "
        "the per-instance geometry/render state used by every mesh LOD. "
        "STATIC normal arms ComputeWind exclusion:14192EC79 sets a14=false "
        "for InitializeSubMeshes call14192ECBD. Forwarded141881CA9/14187FEC0 "
        "to prototype a10, keeps MeshInstance+104 mask400 clear; no wind "
        "descriptor scan/deformer creation. Generic caller14196641E passes "
        "true. Not proof against later live replacement/material shaders.",
    ),
    0x14192F0E0: (
        "FgInstance_Reset",
        "Exact FgInstance::Reset source assertion. Releases dependent render "
        "objects and clears geometry, state, owner and retire fields after the "
        "three-frame deferred-delete safety window.",
    ),
    # --- 2026-07-21: native NadineBuddy attachment ownership ---
    0x1404059E0: (
        "Npc_SetupAttachSystem",
        "Npc/NadineBuddy vtable +0x448 implementation. Initializes the pointer-"
        "owned AttachSystem at npc+0x1E0 from npc-common, then extends it from "
        "the active look/rig payload reached through npc+0x170. This is distinct "
        "from Player_SetupAttachSystemFromLookData and its player/player-net "
            "namespaces.",
    ),
    0x1403D1CD0: (
        "Npc_AllocateAnimDataForDrawable",
        "Concrete Npc/NadineBuddy/Pirate vtable +0x6F0 wrapper; exact Pirate "
        "14294B9D0+6F0 entry14294C0C0 resolves here. Calls the generic "
        "NdDrawableObject_AllocateAndInitFgAnimData allocator and, in the "
        "network mode selected by g_NetModeFlags high byte, sets FgAnimData "
        "+0x2D1. NadineBuddy vtable 0x142949638 resolves this exact slot, "
        "independently of Player::AllocateAnimData. STATIC allocator chain "
        "141BBA060->141BBA098 reaches allocation/reset; the conditional net "
        "byte does not make default phase40 a root-change detector.",
    ),
    0x1404A0F40: (
        "NadineBuddy_Create",
        "Allocates and constructs the concrete NadineBuddy object (RTTI vtable "
        "0x142949638). Its class identity and pointer-owned NPC AttachSystem make "
        "it the native firearm-presentation reference, not a Player look swap.",
    ),
    0x1404A10E0: (
        "NadineBuddy_InitializeIdentity",
        "NadineBuddy vtable +0x58 initializer; installs the inline object name "
        "nadine-buddy at object+0x8. Used with the exact vtable as the bounded "
        "runtime discriminator for the native NPC.",
    ),
    0x1404A0CD0: (
        "NadineBuddy_GetName",
        "NadineBuddy vtable +0x570 getter returning the static identity string "
        "nadine-buddy.",
    ),
    0x1404A0DA0: (
        "NadineBuddy_GetDefaultLookName",
        "NadineBuddy vtable +0xEB8 getter returning nadine-default. This is an "
        "NPC authored-look selector, not the legacy Nadine Player appearance "
        "hash.",
    ),
    0x14055D800: (
        "NpcHolsterWeaponCommand_Start",
        "npc-holster-weapon command start path. Reaches the nested weapon/action "
        "controller from owner+0x15C8 and dispatches its virtual +0xA0/+0xB8 "
        "transition; this establishes command ownership but not a physical "
        "AttachSystem record.",
    ),
    0x14055DA40: (
        "NpcHolsterWeaponCommand_Wait",
        "Companion wait/completion path for npc-holster-weapon. It observes the "
        "nested controller state after the start dispatch and does not itself "
        "materialize a 48-byte attach record.",
    ),
    0x1405A9AE0: (
        "AiWeaponController_Construct",
        "Constructs the 1712-byte NPC weapon controller with vtable "
        "0x1429673C8 and stores its owning NPC at controller+0x8. Generic NPC "
        "control setup publishes it as element 7 of the table at npc+0x15C8.",
    ),
    0x1405AE190: (
        "AiWeaponController_ForceState",
        "AiWeaponController vtable +0xB8. Updates controller state +0x3D8 and "
        "transition +0x3DC, resolves the NPC active ProcessWeapon through owner "
        "vtable +0x8C0, then invokes the attachment enforcer at virtual +0x170.",
    ),
    0x1405AE580: (
        "AiWeaponController_EnforceWeaponAttachment",
        "AiWeaponController vtable +0x170. Scans the pointer-owned NPC "
        "AttachSystem at *(owner+0x1E0); drawn state selects rightHand (or "
        "leftHand for explicit special cases), while holstered state maps "
        "WeaponArtDef type to pistolHolster/longgunHolster and rebinds the "
        "ProcessWeapon through its virtual +0x7A8 when weapon+0x688 differs. "
        "Unlike the Player controller, this path never builds an authored "
        "holster binding and never writes weapon+0x4F0/+0x500.",
    ),
    0x1405E76E0: (
        "WeaponArtDef_SelectNpcHolsterAttachId",
        "Maps NPC WeaponArtDef type 0/3 to pistolHolster 0x8A0249B5 and type "
        "1/2 to longgunHolster 0x80E37582. This is the native NPC mapping and "
        "does not call the Player WeaponHolster binding family.",
    ),
    0x140430EF0: (
        "Npc_GetActiveProcessWeapon",
        "Npc/NadineBuddy vtable +0x8C0. Resolves the active ProcessWeapon from "
        "typed child records under npc+0x1FA0 with fallback handle at "
        "npc+0x23D8/+0x23E0; shared weapon consumers use this instead of a "
        "PlayerWeaponController.",
    ),
    0x1404310D0: (
        "Npc_GiveWeapon",
        "Builds NPC WeaponInfo with the owner's handle, root/rightHand defaults "
        "and WeaponArtDef attach overrides, then materializes the shared "
        "ProcessWeapon. This is the native NPC firearm owner path.",
    ),
    0x140435630: (
        "Npc_SetLookAimMode",
        "Ingress for NPC LookAim PointWs/Locatable requests. Writes one of the "
        "priority layers rooted at npc+0x2830; native Nadine aiming does not "
        "enter Player GunMove or PlayerArmIKAction through this path.",
    ),
    0x140436790: (
        "Npc_ApplyLookAimMode",
        "Selects the active NPC LookAim layer and publishes aim outputs at "
        "npc+0x1930/+0x1940 with flags +0x1990/+0x1991. Called from the NPC "
        "update graph, independently of Player ArmBlockProbes.",
    ),
    0x141509650: (
        "ProcessWeaponStrap_Construct",
        "Constructs the ProcessWeaponStrap child (vtable 0x142A9B0A0). The "
        "strap is a weapon-mesh animation process and not holster attachment "
        "geometry.",
    ),
    0x14150A080: (
        "ProcessWeaponStrap_Initialize",
        "Initializes the materialized strap from its exact ProcessWeapon "
        "parent. Copies parent weak handle/generation from weapon+0xA8/+0xBC "
        "to strap+0x1280/+0x1288, resolves r_palm/l_palm against the weapon "
        "owner, initializes strap state +0x12A0 to zero and then synchronizes "
        "the child DrawControl with the parent.",
    ),
    0x14150A340: (
        "ProcessWeaponStrap_SimulateAndBuildJointPose",
        "Internal per-frame strap simulation called by the disabled async "
        "wrapper and by the live custom-update callback at 0x14150C910. Reads "
        "the exact ProcessWeapon parent weak pair, "
        "consumes holstered/unholstered state at strap+0x12F0, updates the "
        "strap's joint cache and authored/physics pose, then refreshes its "
        "drawable activity. The state-zero path is the holstered strap pose.",
    ),
    0x14150C8D0: (
        "ProcessWeaponStrap_DisableAsyncPhasesAndRegisterCallbacks",
        "Disables the normal FgAnimData async phases through "
        "NdGameObject_DisableAnimationAsyncPhases, then registers the custom "
        "ProcessWeaponStrap update callbacks. This makes vtable +0x408 a dead "
        "steady-state boundary for ordinary strap simulation.",
    ),
    0x14150C910: (
        "ProcessWeaponStrap_CustomUpdateCallback",
        "Live custom ProcessWeaponStrap callback. Calls "
        "ProcessWeaponStrap_SimulateAndBuildJointPose at 0x14150C990, consumes "
        "its AL result, then performs the custom post-simulation publication.",
    ),
    0x141F19070: (
        "NdGameObject_CustomUpdatePublishTransform",
        "Common tail target of ProcessWeaponStrap_CustomUpdateCallback. Receives "
        "the resolved child, FgAnimData, delta time and simulation-result byte, "
        "then completes the custom object-transform and Fg/instance publication. "
        "The tail jump at 0x14150C9B3 is the first stable boundary after both "
        "strap simulation and final frame publication.",
    ),
    0x14150C8A0: (
        "ProcessWeaponStrap_UpdateAnimatingLayersAndPublish",
        "ProcessWeaponStrap vtable 0x142A9B0A0 slot +0x408. Forces the full "
        "simulation mode in ProcessWeaponStrap_SimulateAndBuildJointPose, "
        "then executes the common post-animation and publication helpers.",
    ),
    0x14150D6D0: (
        "ProcessWeaponStrap_SyncParentVisibility",
        "Resolves the exact ProcessWeapon parent through strap+0x1280/+0x1288, "
        "feeds the parent DrawControl into "
        "DrawControl_SyncParentVisibilityAndRecomputeInstances and mirrors the "
        "parent camera/flocking visibility flags. This weak back-reference is "
        "the authoritative runtime child identity; strap+0xD8 is not the "
        "WeaponArtDef selector.",
    ),
    0x14150F640: (
        "ProcessWeapon_Initialize",
        "Initializes ProcessWeapon and conditionally materializes a "
        "ProcessWeaponStrap child when the global gate, exact strap resource "
        "selector at runtime WeaponArtDef+0x38 and weapon mesh handle permit "
        "it; the validated child weak handle/generation are stored at "
        "ProcessWeapon+0xAE0/+0xAE8. Mazur resolves +0x38 to rifle-strap-spc "
        "(StringId 0xB58FB7D7).",
    ),
    0x140FAA380: (
        "Script_DontRenderPlayerThisFrame",
        "Registered script handler reached through stub 0x1400A4260. Parses a "
        "player screen index 0..1, resolves the retained Player, and writes "
        "Player+0x1618=0. StringId 0xD8A6F55A resolves to "
        "DontRenderThisFrame.",
    ),
    0x1415116C0: (
        "ProcessWeapon_EventHandler",
        "ProcessWeapon vtable 0x142A9BF48 slot +0x70. Handles weapon-specific "
        "structured events, calls ProcessWeaponBase_EventHandler for common "
        "events, and forwards Show/Hide plus main-view/cast-shadow visibility "
        "events to the separately animated ProcessWeaponStrap at +0xAE0.",
    ),
    0x1414FBE10: (
        "ProcessWeaponBase_EventHandler",
        "ProcessWeaponBase vtable 0x142A9A630 slot +0x70. Common structured-event "
        "handler reached directly by ProcessWeapon_EventHandler. MSVC COL at "
        "0x142CD85D8 resolves the vtable owner to RTTI ProcessWeaponBase.",
    ),
    0x1414FAF40: (
        "ProcessWeaponBase_Init",
        "Concrete ProcessWeapon base initialization called once from "
        "ProcessWeapon_Initialize at 0x14150F6AF. Reads the object-init "
        "descriptor at context+0x28, resolves WeaponArtDef state, and delegates "
        "the shared parent/attachment setup to NdAttachableObject_Init at "
        "0x1414FB4C2. DWORD weapon+0x4B0 is the saved selected model StringId, "
        "not a pointer or weapon type. It is cleared at 0x1414FAF85; the "
        "ArtDef+0x18 model string (nonempty +0x20 override in online mode) "
        "resolves to the same DWORD stored in init-descriptor+0x50 and "
        "weapon+0x4B0 at 0x1414FB41E, then supplied to resource lookup at "
        "0x1414FB42E. Use live owner/Fg/resource generations together with "
        "this selector; it alone does not prove a current rendered mesh.",
    ),
    0x1418B7060: (
        "AttachSystem_ResolveRecordTransform",
        "Validates a 48-byte AttachSystem record index, reads its signed joint "
        "at record+0x24, obtains that joint's current world transform through "
        "RigGetJointTransform, and composes it with the record-local 32-byte "
        "transform. This is the first final-placement stage downstream of a "
        "ProcessWeapon selected attachment index.",
    ),
    0x141ED1780: (
        "NdAttachableObject_ResolveSelectedAttachmentTransform",
        "Shared NdAttachableObject final-transform resolver. Follows the parent "
        "handle at +0x4E0 to its AttachSystem, resolves selected index +0x688 "
        "through AttachSystem_ResolveRecordTransform, then applies the object's "
        "local/depenetration transform through 0x141ED0A70.",
    ),
    0x141ED1380: (
        "NdAttachableObject_UpdateSelectedAttachmentTransform",
        "Shared NdAttachableObject update path. Calls the selected-attachment "
        "resolver through 0x141ED19BB, then applies LongGunJiggle vslots +0x10 "
        "and +0x20 and interpolation before the terminal writes to "
        "+0x130/+0x140 at 0x141ED16FD/0x141ED1718 when +0x5F8 is enabled. "
        "ProcessWeapon inherits this at vtable +0x7B8; a wrapper after the stock "
        "return is the first exact boundary containing the final post-jiggle "
        "local transform. The stock function returns its success status in AL "
        "(uint8/char ABI).",
    ),
    0x141ED0190: (
        "NdAttachableObject_SetParentAndAttachmentIndex",
        "Stores the retained parent handle/id pair into object+0x4E0/+0x4E8, "
        "refreshes the parent-owned attachment state, stores the selected "
        "attachment byte at +0x688, updates dependent DrawControl instance "
        "flags, and runs the final attachment refresh virtuals.",
    ),
    0x141ED0A70: (
        "NdAttachableObject_ApplyLocalAttachmentCorrection",
        "Consumes the selected AttachSystem record's already-resolved 32-byte "
        "world transform. If object+0x68C is -1 it copies input to output "
        "unchanged; otherwise it derives a rig-joint-relative correction from "
        "object+0x1A8 and the local/depenetration state at +0x4F0..+0x52F and "
        "+0x5F0/+0x5F4. The sole caller is the direct E8 at 0x141ED19BB in "
        "NdAttachableObject_ResolveSelectedAttachmentTransform. This is the "
        "narrow authored-frame correction seam, but its output is still upstream "
        "of LongGunJiggle inside vslot +0x7B8 and is not the final local pose. "
        "The +0x5F0 >= 0.001 branch at 0x141ED0F79..0x141ED0F8D leads to "
        "world-pivot interpolation at 0x141ED126A; below that threshold the "
        "parent/primary/inverse-rig result returns directly. An output-only "
        "world translation after this call bypasses that already-completed "
        "blend. Runtime +0x510/+0x520 is not the authored secondary endpoint "
        "used by PlayerWeaponController_BlendSlotToSecondaryHolsterBinding. "
        "STATIC low5F0/unitS equation: A*C*inverse(RlocalRoot * "
        "inverse(cacheRootWorld)*cacheSelectedWorld). 141ED0AF4/B8C reads "
        "Fg+100 first48B JP Q/T, not a separate art offset. Atlocaljoint0 "
        "the cache relative isidentity; root-skinned geometry subsequently "
        "cancels R when render palette has the same rigid root. Samegeneration, "
        "no laterjiggle/Animating/override andunitS remain conditions.",
    ),
    0x141ED2060: (
        "NdAttachableObject_UpdateAnimatingLayersAndPublish",
        "Source-bound to the Animating layer and the "
        "NdDrawableObject::GetAnimateCtrl assertion. Calls virtual +0x7B8 for "
        "the selected-attachment update, may rewrite object+0x130/+0x140 from "
        "an active animation layer at 0x141ED2721/0x141ED2741, conditionally "
        "refreshes cached parent +0x110 from g_AuthoredTransformPointerTable, "
        "then unconditionally composes the cached parent with that local state "
        "and publishes it to FgAnimData at "
        "0x141ED288F.",
    ),
    0x141ED4390: (
        "NdAttachableObject_UpdateLocalAttachmentBlend",
        "Shared per-frame local-attachment blend updater. When object+0x640 "
        "is positive, advances progress at +0x644 by process delta divided by "
        "that duration, applies smoothstep mode 2, and interpolates the 32-byte "
        "transform from endpoint +0x620/+0x630 toward +0x600/+0x610 into "
        "object+0x4F0/+0x500. At 0x141ED4464 it clears only the duration "
        "DWORD +0x640 when progress reaches 1.0; +0x644 remains 1.0. It then "
        "calls vtable +0x7B8 at 0x141ED4487 only when AnimControl async-update "
        "phase 0x10 is not enabled.",
    ),
    0x140221550: (
        "Transform_InterpolateTranslationAndRotation",
        "Interpolates the first transform vector linearly and the second "
        "quaternion by the shortest-path spherical interpolation using the "
        "supplied weight. NdAttachableObject_UpdateLocalAttachmentBlend calls "
        "it for the authored +0x620-to-+0x600 weapon handoff.",
    ),
    0x1418A9F00: (
        "Anim_EvaluateBlendCurve",
        "Evaluates the selected animation blend curve for a normalized scalar. "
        "NdAttachableObject_UpdateLocalAttachmentBlend uses mode 2, whose "
        "branch computes smoothstep t*t*(3-2*t) before transform interpolation.",
    ),
    0x14108B120: (
        "WeaponHolsterIkBlender_ExtractData",
        "WeaponHolsterIkBlender vtable +0x10. Extracts a 0x38-byte "
        "WeaponHolsterIkData record. Requires sampled apReference-hand-r plus "
        "at least one holster reference. Leg bit 1 has priority; otherwise a "
        "present back bit 2 is selected. Missing hand or both holster refs "
        "produce valid=0. Writes the selected id at output+0x30 and does not "
        "resolve or mutate a ProcessWeapon attachment.",
    ),
    0x1418B3770: (
        "AnimStateSnapshot_EvaluateReferenceSetMask",
        "Evaluates a requested reference-id array against one authored "
        "AnimStateSnapshot root through virtual slot 25. ORs bit 1<<index for "
        "each successfully sampled reference and leaves each sampled transform "
        "in its corresponding output block. WeaponHolsterIkBlender requests "
        "hand-r, leg-holster and back-holster and consumes this exact mask.",
    ),
    0x141918820: (
        "AnimSnapshotNodeAnimation_SampleNamedReference",
        "AnimSnapshotNodeAnimation vtable slot 25 (+0xC8). Uses the concrete "
        "clip pointer at node+0x50 and requested reference id to sample one "
        "authored named-reference transform. Failure writes the identity/zero "
        "48-byte transform and returns false.",
    ),
    0x14191F220: (
        "AnimSnapshotNodeBlend_SampleNamedReference",
        "AnimSnapshotNodeBlend vtable slot 25 (+0xC8). Samples child index "
        "node+0x40, conditionally samples child +0x44 according to flags, mode "
        "and weight at +0x60, then blends successful reference transforms.",
    ),
    0x141AD5640: (
        "AnimSnapshotNodeUnary_SampleNamedReference",
        "Shared vtable slot 25 (+0xC8) for Unary and derived IK snapshot "
        "nodes. Resolves the sole child index at node+0x40 and delegates the "
        "same named-reference request to that child's slot 25.",
    ),
    0x1406D63B0: (
        "AnimSnapshotNodeWarper_SampleNamedReference",
        "AnimSnapshotNodeWarper vtable slot 25 (+0xC8). First delegates the "
        "named-reference request through child index node+0x40, then applies "
        "authored align and limb-specific warper/reference overrides.",
    ),
    0x1418A9500: (
        "AnimationClip_SampleNamedReferenceById",
        "Concrete animation-leaf reference lookup. Reads the clip from query "
        "context+0x8, then clip+0x30 -> {count, id-array+0x8, sample-pointer "
        "array+0x10}; linearly matches the requested id, samples its transform "
        "and optionally applies the matched skeleton-retarget entry's uniform "
        "scale at +0x24 to the first 16-byte position vector while preserving "
        "the second 16-byte quaternion vector.",
    ),
    0x14193AF80: (
        "AnimSnapshotNodeLeaf_RejectNamedReference",
        "Leaf/empty snapshot vtable slot 25 (+0xC8). Writes the canonical "
        "identity/zero 48-byte transform and always returns false.",
    ),
    0x141ABC630: (
        "AnimSnapshotNodeOutOfMemory_RejectNamedReference",
        "Out-of-memory snapshot-node vtable slot 25 (+0xC8); always returns "
        "false without producing a named-reference sample.",
    ),
    0x14108B580: (
        "WeaponHolsterIkBlender_BlendData",
        "WeaponHolsterIkBlender vtable +0x18. Blends two 0x38-byte holster-IK "
        "records for the arm-chain target; the output feeds skeleton joint pose "
        "updates rather than the attached weapon transform.",
    ),
    0x14108F800: (
        "AnimStateLayer_ExtractAndBlendCustomData",
        "Generic active-snapshot reducer. Walks the live snapshot trees of one "
        "type-2 AnimStateLayer, calls the supplied custom blender's ExtractData "
        "for each source and BlendData for transitions, then returns the final "
        "custom-data record. PlayerWeaponAnimControl uses it to obtain the final "
        "WeaponHolsterIkData from WeaponPartial.",
    ),
    0x14108F0B0: (
        "PlayerWeaponAnimControl_RequestPistolHolsterLayerState",
        "PlayerWeaponAnimControl vtable +0x148. Requests s_pistol-holster on "
        "layer 0x63B48BEE. This is a different layer from WeaponPartial "
        "0xE2B990B9, which supplies the holster-IK hand/leg/back references.",
    ),
    0x1410973F0: (
        "PlayerPlayWeaponOverlayAction_GetTypeName",
        "Returns the exact RTTI/factory name PlayerPlayWeaponOverlayAction.",
    ),
    0x141097400: (
        "PlayerPlayWeaponOverlayAction_Construct",
        "Constructs the 0x50-byte PlayerPlayWeaponOverlayAction from the shared "
        "Player weapon-action base, installs vtable 0x142A48828, and stores the "
        "initial overlay blend scalar at action+0x48.",
    ),
    0x141097450: (
        "PlayerPlayWeaponOverlayAction_OnStart",
        "RTTI PlayerPlayWeaponOverlayAction vtable +0x10. Resolves the shared "
        "weapon/holster binding, chooses a movement-context weapon overlay, "
        "and queries PlayerWeaponController vtable +0x3C0 for the mapped "
        "overlay record before publishing the resulting animation id.",
    ),
    0x14108B940: (
        "PlayerWeaponAnimControl_RequestCustomWeaponPart",
        "PlayerWeaponAnimControl vtable +0x28. Stores both animation ids from "
        "one paired WeaponAnimOverlayInfo at control+0x20/+0x24, stores its "
        "second blend scalar at +0x28, mirrors them at Player+0x1CC/+0x1D0/"
        "+0x1D4, requests custom-weapon-part (or its feather "
        "variant) on WeaponPartial 0xE2B990B9, and requests s_weapon-out. The "
        "second authored clip is retained by this path rather than discarded.",
    ),
    0x14108B860: (
        "PlayerWeaponAnimControl_MatchesCustomWeaponPartRequest",
        "Compares both requested WeaponAnimOverlayInfo animation ids and its "
        "second blend scalar against PlayerWeaponAnimControl+0x20/+0x24/+0x28. This "
        "is the exact paired-request identity check used by the custom weapon "
        "partial path.",
    ),
    0x14108CF70: (
        "PlayerWeaponAnimControl_GetCurrentWeaponPartAnimIdIfSettled",
        "Returns PlayerWeaponAnimControl+0x18 only after the WeaponPartial "
        "request has reached its settled state; otherwise returns zero.",
    ),
    0x14108C0C0: (
        "PlayerWeaponAnimControl_RequestWeaponPart",
        "PlayerWeaponAnimControl vtable +0x08. Stores the requested animation "
        "id at control+0x18, copies the caller pose record to the Player "
        "animation owner, and starts or fades the exact WeaponPartial "
        "0xE2B990B9 layer. This is the normal PlayerPlayWeaponOverlayAction "
        "path when its special custom-part branch is not selected.",
    ),
    0x141AD5DC0: (
        "AnimSnapshotNodeInstanceZero_Create",
        "Creates the 0x40-byte AnimSnapshotNodeInstanceZero for authored "
        "anim-node-instance-0 0x95CBA180 and installs its exact RTTI vtable.",
    ),
    0x14109D450: (
        "PlayerWeaponController_MapChloeBaseWeaponOverlayId",
        "Read-only helper used by controller overlay lookup. When the animation "
        "owner at controller+0x108 reports chloe-base or chloe-base-test, "
        "resolves *chloe-weapon-anim-overlay-map* and maps the input animation "
        "StringId through its SortedIdPointerTable. Returns zero when the "
        "feature, skeleton test, named map or key is absent.",
    ),
    0x14109D0E0: (
        "PlayerWeaponController_Construct",
        "Constructs the PlayerWeaponController, installs vtable 0x142A47F90, "
        "initializes its weapon-slot records, and clears the two cached overlay "
        "table handle blocks later searched at controller+0x468/+0x488.",
    ),
    0x14109D350: (
        "PlayerWeaponController_SetGenericWeaponOverlayId",
        "PlayerWeaponController vtable +0x3B0. Stores the generic weapon-class "
        "overlay-set id used by Player_ConfigureWeaponAnimationOverlays.",
    ),
    0x14109D3D0: (
        "PlayerWeaponController_SetSpecificWeaponOverlayId",
        "PlayerWeaponController vtable +0x3B8. Stores the art-specific weapon "
        "overlay-set id used by Player_ConfigureWeaponAnimationOverlays.",
    ),
    0x14109D8E0: (
        "PlayerWeaponController_FindHandSwitchOverlayRecord",
        "Finds the hand-switch WeaponAnimOverlayInfo for the requested animation "
        "in the controller's cached generic and art-specific overlay tables.",
    ),
    0x1410923A0: (
        "Player_ConfigureWeaponAnimationOverlays",
        "Loads the generic weapon-class and art-specific animation-overlay set "
        "ids for the current ProcessWeapon and publishes them through "
        "PlayerWeaponController vtable slots +0x3B0/+0x3B8.",
    ),
    0x14109D650: (
        "PlayerWeaponController_FindWeaponOverlayRecordByAnimId",
        "Searches the controller's two cached weapon-overlay table handles at "
        "+0x468 and +0x488 for one animation StringId and returns its overlay "
        "record when the key/value tables contain it.",
    ),
    0x14109D740: (
        "PlayerWeaponController_FindWeaponOverlayRecord",
        "PlayerWeaponController vtable +0x3C0. First maps the requested "
        "animation through the Chloe-base weapon overlay map and searches that "
        "mapped id; if no record exists, retries the original animation id.",
    ),
    0x1410ACB70: (
        "PlayerWeaponController_GetWeaponHandSideSelector",
        "PlayerWeaponController vtable +0x240. Returns controller+0x444, "
        "initialized to 1 by the controller constructor and changed only by "
        "the concrete SwitchWeaponHand entry points/actions. "
        "PlayerWeaponIKAction passes it to WeaponIKFeather as the generic "
        "left/right interpolation selector; it is not a weapon-art locator.",
    ),
    0x141BB68E0: (
        "NdGameObject_SetAnimScale",
        "Generic NdGameObject virtual setter paired with GetAnimScale in many "
        "game-object vtables. Writes the scalar to FgAnimData+0x140 when the "
        "object owns Fg animation data.",
    ),
    0x141BB6900: (
        "NdGameObject_GetAnimScale",
        "Generic NdGameObject virtual getter paired with SetAnimScale in many "
        "game-object vtables. Reads FgAnimData+0x140 or returns 1.0 when no Fg "
        "data exists. PlayerWeaponIKAction passes this scalar into "
        "WeaponIKFeather as a multiplicative scale, not a spatial weapon goal.",
    ),
    0x1410EB3E0: (
        "PlayerAnimOwner_UsesChloeBaseSkeleton",
        "Queries the animation owner's vtable +0x300 identity and returns true "
        "only for chloe-base-test or chloe-base. This gates the hard-coded "
        "*chloe-weapon-anim-overlay-map* remap in PlayerWeaponController.",
    ),
    0x14108E5A0: (
        "PlayerWeaponAnimControl_SetWeaponDescriptor",
        "PlayerWeaponAnimControl vtable +0x100. Forwards weapon class and art id "
        "to vtable +0x108, mirrors the class through +0x110, then refreshes the "
        "weapon-tweaks knowledge entry through +0xF0.",
    ),
    0x14108E620: (
        "PlayerWeaponAnimControl_SetWeaponClassAndArt",
        "PlayerWeaponAnimControl vtable +0x108. Stores weapon class at +0xD0 "
        "and WeaponArt id at +0xD4, invalidating the derived animation selector "
        "only when either changes. These fields are not a leg/back holster type.",
    ),
    0x14108E690: (
        "PlayerWeaponAnimControl_SetWeaponClass",
        "PlayerWeaponAnimControl vtable +0x110. Stores the weapon class at "
        "+0xD8 and invalidates its derived animation selector on change; it does "
        "not choose a holster attachment or IK reference.",
    ),
    0x1400C28C0: (
        "InitializeWeaponHolsterPointIdTable",
        "Static initializer for the three Player holster-point ids: gun_holster, "
        "gun_holster_leg and gun_holster_pelvis. The action-side table maps them "
        "to weapon-holster-point-* knowledge keys in that order.",
    ),
    0x1410B1C80: (
        "Player_BuildWeaponHolsterSpeedKnowledgeId",
        "Builds weapon-holster-type-normal/fast/very-fast/zipline from movement "
        "state. This knowledge family selects transition speed/context and is "
        "not the leg/back holster-point discriminator.",
    ),
    # --- 2026-07-21: Nadine active firearm grip, arm pose and strap split ---
    0x140C2BCB0: (
        "ArmBlockProbes_Kick",
        "Source assertion aim-block.cpp:223 names ArmBlockProbes::Kick(Player*, "
        "bool). Called from the GunMove controller path; builds four probe "
        "records through 0x140C2C2F0 and writes the completed result arrays at "
        "this+0xD0/+0xD8. This is an arm/aim-block producer, not a weapon "
        "attachment or holster transform owner.",
    ),
    0x140C2C2F0: (
        "ArmBlockProbes_BuildProbeInputs",
        "Helper for ArmBlockProbes_Kick. Resolves stand-p-aim, "
        "*player-aim-probes*, apReference-shoulder and apReference-gun, with "
        "r_shoulder/spined fallbacks, then returns the local probe geometry. "
        "It does not read WeaponArtDef holster data or Player look ids.",
    ),
    0x140C7E8D0: (
        "PlayerArmIKAction_UpdateProcedural",
        "Exact ikactions.cpp:257 source assertion. Publishes the two stored arm "
        "targets from the PlayerArmIKAction into its paired procedural chains. "
        "The action is recreated from the active look by Player_ChangeLookId.",
    ),
    0x140E909C0: (
        "GunMoveController_KickArmBlockProbes",
        "GunMove-controller vtable +0xB8 wrapper that reaches "
        "ArmBlockProbes_Kick at controller+0x310 for the owning Player. This "
        "separates the authored GunMove arm-pose path from ProcessWeapon's "
        "rightHand attachment path.",
    ),
    0x141509D10: (
        "ProcessWeaponStrap_PlayUnholsteredState",
        "Resolves the base animation state, builds the authored "
        "rifle-strap-unholstered event and dispatches it through the animation "
        "state object. Strap visibility is therefore a separate authored "
        "animation concern, not collision or holster attachment geometry.",
    ),
    0x14150C9C0: (
        "ProcessWeaponStrap_EventHandler",
        "Exact process-weapon-strap.cpp EventHandler assertion. Handles the "
        "strap state events by publishing play-animation for "
        "ak47-holster--strap or rifle-strap-unholstered through the owner "
        "process and updates ProcessWeaponStrap+0x12F0: holstered writes 0 and "
        "unholstered writes 1.",
    ),
    0x1418F1610: (
        "WeaponIKFeather_InitializeForPlayer",
        "Initializes the embedded WeaponIKFeather in PlayerWeaponIKAction "
        "against a Player and resolves its paired arm-chain joint indices. "
        "The action constructor passes action+0x50 and the owning Player.",
    ),
    0x14062AC40: (
        "WeaponGripModifier_FillArmJointSelection",
        "WeaponGripModifier RTTI vtable14296E2C0 slot+0x70. Enumerates eight "
        "left/right shoulder, elbow, wrist and hand-prop joints for proximal "
        "NPC IK. No finger joints are selected; this is not a distal-grip "
        "animation source selector.",
    ),
    0x14062B2B0: (
        "WeaponGripModifier_PublishNpcGripIkInputs",
        "Shared WeaponGripModifier/AiWeaponGripModifier slot+0x28 in vtables "
        "14296E2C0/14296E370. Publishes NPC anim-info+0xA0 grip IK selector/"
        "weight and enable bit2. This does not supply a right-finger pose.",
    ),
    0x14062B710: (
        "AiWeaponGripModifier_UpdateSmoothedWeight",
        "AiWeaponGripModifier RTTI vtable14296E370 slot+0xA0. Updates a "
        "smoothed scalar at modifier+0x20 for the proximal NPC grip IK path; "
        "not evidence of a missing Player P99 finger clip.",
    ),
    0x1418203D0: (
        "JointChain_BuildFromNamedAncestorToDescendant",
        "Builds a concrete joint-index path in the supplied FgAnimData skeleton "
        "from a named ancestor to a named descendant by following the skeleton "
        "parent table. WeaponIKFeather uses it for shoulder-to-hand-prop paths; "
        "the intervening palm joint is retained even though it is not one of the "
        "five cached semantic lookup names.",
    ),
    0x14181D750: (
        "JointChain_FindNamedJointOffset",
        "Finds a named skeleton joint inside a previously built JointChain and "
        "returns its chain-local offset, or -1. WeaponIKFeather caches root, "
        "shoulder, elbow, wrist and hand-prop offsets for each side through this "
        "helper rather than retaining Chloe/Nadine numeric joint ordinals.",
    ),
    0x1418F1780: (
        "WeaponIK_Apply",
        "Terminal WeaponIK apply called by WeaponIKFeather_Apply after both "
        "hand JointSets are solved and committed. For selector 1 it builds the "
        "left-arm target as Compose(rightHandProp, "
        "MakeRelative(leftHandProp, leftWrist)); rightWrist is finite-checked "
        "but does not enter the target. It then solves the left arm and applies "
        "the target wrist rotation before committing both JointSets. STATIC "
        "2026-10-08: on the rigid Nadine wrist25->palm27->prop29 chain, the "
        "relative constraint equals inverse(localPalm27*localProp29); all "
        "proximal ancestry cancels. Native NPC row149 endpoints 036F20E5/up "
        "and 5E67B1FA/down have the same palm27 but different prop29, hence "
        "different support constraints. This is not proof of Player state "
        "selection, full overlay weight or a rendered P99 repair. RUNTIME "
        "REJECTED 2026-10-08 PID9504: DOWN endpoint was substituted at getter "
        "1418F18B5, but the user reports no fix. Four APPLIED input logs do "
        "not prove post-solver output or a correct rendered elbow plane.",
    ),
    0x1418F1E60: (
        "WeaponIKFeatherInfo_ConstructForPlayerLook",
        "Constructs the 0x150-byte look/skeleton-owned WeaponIKFeatherInfo "
        "stored at Player+0x4308. It contains paired left/right JointSets and "
        "solver maps, installs no AnimAction vtable, and is rebuilt by "
        "Player_ChangeLookId.",
    ),
    0x1418F2700: (
        "WeaponIKFeather_Apply",
        "Exact armik.cpp WeaponIKFeather::Apply. Sole direct caller "
        "PlayerWeaponIKAction_Update+0x8CC passes Player+0x4308 weapon-feather "
        "info; the function resolves and commits both hand JointSets before "
        "solving the arm targets. Its first stack weight is read at "
        "0x1418F274F and zero returns false at 0x1418F2760 before any write.",
    ),
    0x1418F2E50: (
        "WeaponIKFeather_SolveWeaponFeatherIk",
        "Exact armik.cpp WeaponIKFeather::SolveWeaponFeatherIk assertion. "
        "Consumes the paired left/right JointSets and solver maps; it is not "
        "safe to suppress or substitute only one hand independently. Fresh "
        "Jacobian instances explicitly clear external limits +0x10 at "
        "0x1418F30F6 and profile limits +0x440 at 0x1418F3112 before BeginSolve "
        "0x1418F3141 and Iterate 0x1418F32FF. Therefore neither optional "
        "post-iteration JointLimits_ApplyJointLimit branch executes here; "
        "the authored profile can still influence solver-map DOF/step weights.",
    ),
    # --- 2026-07-13: native pickup gesture controller ---
    0x140F49300: (
        "PlayerPickupController_Construct",
        "Constructs the mounted Player+0x1EB8 pickup controller, including the "
        "16-entry queue at +0x130 (stride 0x30), queue count +0x430, selected "
        "dra-pickup-* gesture hash +0x434, timing and joint-state fields.",
    ),
    0x140F49CA0: (
        "PlayerPickupController_StartOrQueue",
        "Vtable +0x38. Called by the interact-with-me/Pickup script branch at "
        "0x140DD0A3C. Queues one pickup record and selects the native "
        "dra-pickup-* gesture. Explicitly supports a null object argument; arg3 "
        "is a 32-byte POD copied into the queue record.",
    ),
    0x140F4B150: (
        "PlayerPickupController_TryStartGesture",
        "Resolves and starts the queued dra-pickup-* gesture and waits for the "
        "signal-pickup marker through the controller's native animation state.",
    ),
    0x140F4BF70: (
        "PlayerPickupController_Update",
        "Per-frame pickup-controller update reached through its vtable +0x28; "
        "advances queued pickup records and starts/completes the gesture.",
    ),
    0x140F4CD20: (
        "PlayerPickupController_PostAnimBlending",
        "Confirmed by player-pickup.cpp:897 assertion. Applies pickup gesture "
        "post-blend/joint state while the native pickup action is active.",
    ),
    0x140F4D0E0: (
        "PlayerPickupController_CompleteAndPublish",
        "Completes queued pickup records and publishes the player-pickup actor "
        "state through DispatchOrResolveActorStateDescriptorIntoRuntimeSlot.",
    ),
    # --- 2026-07-12: actor thread context and context-free interact publish ---
    0x1416A2E80: (
        "ThreadContext_GetCurrentProcess",
        "Returns TLS ContextProcess at Fls block +0x98 when the +0x90 tag is set, "
        "otherwise returns the global fallback process context.",
    ),
    0x1416A2ED0: (
        "ThreadContext_SetCurrentProcess",
        "Writes the ContextProcess CRC to the current Fls block +0x90 and the "
        "supplied process object to +0x98. Stock event publishers save, set, "
        "dispatch virtual +0xD0, then restore through this helper.",
    ),
    0x1410BCDF0: (
        "Player_PublishZiplineRequestedInteract",
        "Independent producer of player-requested-interact for a retained zipline "
        "target. Builds the same two-id descriptor as the generic Player interact "
        "publisher without calling Player_InteractDispatcher.",
    ),
    # --- 2026-07-12: Player aim-additive animation pipeline ---
    0x140C9F420: (
        "Player_UpdateMountedControllers",
        "Common Player controller phase called at 0x140CA2952 from "
        "PublishRuntimeStateFromDecisionNodeLikeObject. Dispatches the mounted "
        "controllers at Player+0x1D58..+0x1FF8; Player+0x1D90 vtable slot +0x30 "
        "is PlayerAimController_UpdateBlendState. Later Player publication, "
        "camera and attachment phases run after this function returns.",
    ),
    0x14172C8E0: (
        "AnimParamRegistry_RequestLayerFadeOutAndRelease",
        "Looks up an active type-2 animation layer by channel id and requests "
        "a fade to zero followed by descriptor release: target +0x2C becomes "
        "0, remaining/full fade time +0x30/+0x34 receive the supplied duration, "
        "starting weight +0x38 copies current weight +0x28, easing +0x3C becomes "
        "1, and release-pending +0x3D becomes 1. PlayerAimController's alternate "
        "branch calls this with 0.2 seconds for Aim0/Aim2/Aim4/Aim6; it is not "
        "a directional-weight setter.",
    ),
    0x141A911A0: (
        "AnimParamRegistry_StepAndDeactivateReleasedLayers",
        "Steps registered animation controls through AnimControl_BeginStepInternal "
        "and deactivates a descriptor when its type-2 layer has release-pending "
        "+0x3D set and current weight +0x28 has reached zero. The deactivation "
        "calls vtable slot +0x50 and clears descriptor id/context/active state.",
    ),
    0x14172A370: (
        "AnimParamRegistry_ActivateTypedChannelSlot",
        "Activates a preallocated inactive 0x20-byte registry descriptor of the "
        "requested object type. Prefers a matching channel id, otherwise claims "
        "a free id-zero descriptor; writes descriptor id/context and active byte "
        "+0x14 before returning the typed object. The active-byte writes at "
        "0x14172A3EA/0x14172A45C precede EnsureFloatAnimStateLayer returning "
        "to the first CreateOrFadeStateInstance call at 0x14172A834.",
    ),
    0x14172A910: (
        "AnimParamRegistry_EnsureFloatAnimStateLayer",
        "Ensures an active type-2 AnimStateLayer for a channel id through "
        "AnimParamRegistry_ActivateTypedChannelSlot, binds registry context at "
        "object+0x540, and initializes the layer through vtable slot +0x60. "
        "PlayerAimController_CommitNamedAimChannel uses it before first nonzero "
        "aim-add publication.",
    ),
    0x14189CC80: (
        "AnimStateLayer_RequestDirectFade",
        "Queues an Animation direct-fade request in the type-2 AnimStateLayer. "
        "The function advances the bounded request queue at +0x4D4 and contains "
        "the 'Accepted direct fade request' diagnostic; named aim-add and net-fire "
        "paths call it after ensuring their layer.",
    ),
    0x141A78A60: (
        "AnimControl_BeginStepInternal",
        "Exact source assertion identifies AnimControl::BeginStepInternal. "
        "Stable-sorts the complete 0x20-byte layer-descriptor array in unsigned "
        "ascending order by descriptor+0x10, then dispatches object type 2 to "
        "AnimStateLayer_Step before released layers are deactivated.",
    ),
    0x14189B7B0: (
        "AnimStateLayer_Construct",
        "Constructs the type-2 AnimStateLayer, installs its RTTI-backed vtable, "
        "binds the shared authored parameter wrapper at +0x530, and allocates "
        "AnimStateInstance tracks through the independently asserted allocator. "
        "The vtable is 0x142B0ABB0: initial slot +0x60 -> 0x14189C8B0 -> "
        "slot +0x38 -> 0x14189C860 only resets layer state through "
        "0x14189C760; it does not construct a pose snapshot before the "
        "AnimControl layer-registry record is activated.",
    ),
    0x1418B3E70: (
        "AnimStateInstance_Construct",
        "Constructs one 0x1C0-byte AnimStateInstance and installs the RTTI-backed "
        "vtable before authored buffers and snapshot state are initialized.",
    ),
    0x1418B3F10: (
        "AnimStateInstance_InitializeAuthoredBuffers",
        "Initializes one preconstructed AnimStateInstance: allocates its authored "
        "parameter destination wrapper at +0x178 and sets +0x190 bit 0x20 from "
        "the layer record's authored copy-parameters byte. At 0x1418B3F44 "
        "stores argument 2 as the owning AnimStateLayer pointer at instance+0x60; "
        "the constructor caller 0x14189BFFD passes that layer in RDX. Stores "
        "the AnimSet pointer at instance+0x140. These back-pointers are already "
        "initialized when instance+0x90 is passed to snapshot-node creation.",
    ),
    0x1418B24F0: (
        "AnimStateSnapshot_SnapshotAnimState",
        "Source assertion identifies AnimStateSnapshot::SnapshotAnimState. Copies "
        "the 0x80-byte state snapshot to instance+0x90, resolves the state id at "
        "+0x98 into authored callbacks/flags, and selects the root snapshot node. "
        "Its local SnapshotContext contains a4 overlays at +0, a5 overlays at "
        "+8, a6 AnimSet/remap at +0x10, a7 parameter-wrapper pointer at +0x18, "
        "a9 transition context at +0x20, and a8 context at +0x28. The first "
        "SelectRootNodeIndex call at 0x1418B287B can dispatch animation callback "
        "0x141919F60 and its node-local pose-row consumer 0x141918C30. "
        "Before the ordinary animation-id resolver, call 0x1418B28B3 invokes "
        "AnimStateSnapshot_TransformState to materialize type-1/type-5 overlay "
        "rows as snapshot blend nodes keyed by the state id at snapshot+0x8. "
        "At 0x1418B2940 it resolves the requested animation through the track-local "
        "AnimOverlayState supplied as a4, calls AnimSetRemap at 0x1418B2954, then "
        "stores the outer id/key tuple at snapshot+0x88/+0x90/+0x94. That "
        "tuple is consumed by AnimStateInstance_ResolveSnapshotClipTiming; pose "
        "commands are generated from independently remapped node-local Animation "
        "fields +0x44/+0x48/+0x4C/+0x50.",
    ),
    0x1418B32E0: (
        "AnimStateInstance_ResolveSnapshotClipTiming",
        "Resolves the snapshot root and calls virtual slot 10, then resolves the "
        "outer AnimStateSnapshot tuple +0x88/+0x90/+0x94 only to derive instance "
        "timing fields +0x98/+0x9C. It is not the leaf pose-command clip owner; "
        "AnimSnapshotNodeAnimation performs its own node-local remap and clip "
        "resolution before emitting command 0x60006. The resolved ANIM header "
        "word at +0x22 is the sample count: only counts greater than one use "
        "ANIM+0x24 to derive a multi-sample duration, while one-sample partial "
        "poses are valid and evaluate at phase zero.",
    ),
    0x1418B2A40: (
        "AnimStateSnapshot_TransformState",
        "Exact source assertion identifies AnimStateSnapshot::TransformState. "
        "Before the ordinary animation-id resolver, it searches the supplied "
        "AnimOverlayState for rows keyed by snapshot+0x8, accepts authored row "
        "types 1 and 5, and materializes their targets as runtime snapshot/blend "
        "nodes. Multiple same-source type-1 rows are therefore complementary "
        "state children rather than competing direct animation remaps.",
    ),
    0x1417FA6E0: (
        "AnimOverlayState_FindMatchingRow",
        "Arguments are AnimOverlayState, output cursor, const input cursor; "
        "returns the output pointer without modifying the input or rows. The "
        "32-byte cursor has source id +0, last set/row indices +4/+8, returned "
        "0x28-byte row pointer +0x10, selector mode +0x18, and returned "
        "bool(row flags & 4) at +0x19. Matching requires cursor+0x18 == "
        "bool(row+0x26 & 2); do not infer a universal mirror meaning. Passing "
        "the returned cursor back advances past that row and preserves later "
        "matches. Pose consumer 0x141918C30 initially uses mode 0 and calls "
        "here at 0x141918CE7/0x14191986C/0x141919AF9/0x141919C66; its "
        "0x141919D78 call is diagnostic. TransformState uses mode 1 at "
        "0x1418B2AFE/0x1418B2E93, so those state-row callsites do not cover "
        "flags-zero type-1 pose overlays.",
    ),
    0x1418B2EF0: (
        "AnimStateSnapshot_SelectRootNodeIndex",
        "Selects the root node index later stored at AnimStateInstance+0x138; "
        "the index is relative to the instance SnapshotNodeHeap at +0x130. "
        "Matches the authored node type against the 0x18-byte registry at "
        "0x14387FD20 and dispatches registry-entry+0x10 with snapshot, "
        "descriptor, SnapshotContext, and output flags. The anim-node-animation "
        "entry invokes 0x141919F60, including its early pose-overlay pass.",
    ),
    0x1418C2F90: (
        "SnapshotNodeHeap_ResolveNodeByIndex",
        "Resolves a SnapshotNodeHeap entry from the heap pointer and node index. "
        "AnimStateInstance_PhaseUpdate uses it with instance+0x130/+0x138 before "
        "dispatching the root node virtual methods.",
    ),
    0x1418B3150: (
        "AnimStateSnapshot_RefreshPhasesAndBlends",
        "Source assertion identifies AnimStateSnapshot::RefreshPhasesAndBlends. "
        "Resolves the snapshot root and passes the live parameter context to root "
        "virtual slot 11 (+0x58).",
    ),
    0x14191DC30: (
        "AnimSnapshotNodeBlend_Create",
        "Registered factory for the RTTI-backed 0x80-byte AnimSnapshotNodeBlend "
        "class; constructs the two-child snapshot node from authored data.",
    ),
    0x14191DF00: (
        "AnimSnapshotNodeBlend_Construct",
        "Installs the AnimSnapshotNodeBlend vtable and initializes its two child "
        "indices and dynamic blend fields.",
    ),
    0x14191DF90: (
        "AnimSnapshotNodeBlend_SnapshotChildren",
        "Materializes authored descriptor children +0x8/+0x10 into snapshot-heap "
        "indices at node+0x40/+0x44 and saves the blend script descriptor at +0x58.",
    ),
    0x14191E3F0: (
        "AnimSnapshotNodeBlend_RefreshBreadth",
        "AnimSnapshotNodeBlend virtual slot 12 (+0x60). Resolves both child "
        "indices, recursively calls their slot 12 RefreshBreadth methods, and "
        "returns the resulting command-tree breadth.",
    ),
    0x14191E180: (
        "AnimSnapshotNodeBlend_RefreshPhasesAndBlends",
        "Virtual slot 11 (+0x58). Evaluates anim-node-blend-func with the live "
        "parameter context, clamps the result into node+0x60, then recurses into "
        "the selected child nodes.",
    ),
    0x14191E550: (
        "AnimSnapshotNodeBlend_StepChildren",
        "Virtual slot 13 (+0x68). Resolves node+0x40/+0x44 and forwards the step "
        "call to both children; it does not itself interpret animation parameters.",
    ),
    0x14191E5F0: (
        "AnimSnapshotNodeBlend_GenerateAnimCommands",
        "Virtual slot 14 (+0x70). Generates both child command streams and appends "
        "the 0x60007 blend command whose final dword is node+0x60.",
    ),
    0x1419179C0: (
        "AnimSnapshotNodeAnimation_Construct",
        "Constructs the 0x80-byte RTTI-backed AnimSnapshotNodeAnimation and "
        "installs its vtable after the common snapshot-node base constructor.",
    ),
    0x141917A80: (
        "AnimSnapshotNodeAnimation_Create",
        "Registered anim-node-animation factory. Reuses caller storage or "
        "allocates 0x80 bytes, constructs the common base, and installs the "
        "AnimSnapshotNodeAnimation vtable.",
    ),
    0x141917D00: (
        "AnimSnapshotNodeAnimation_InitializeFromDescriptor",
        "Animation-node virtual slot 9. Resolves anim-node-name-func and "
        "anim-node-flip-func, initializes source/resolved animation ids at "
        "+0x40/+0x44, resolver keys at +0x48/+0x4C, phase descriptor +0x58, "
        "phase +0x60/+0x64, and flags +0x68/+0x69. If SnapshotContext+0 "
        "carries a live overlay it preserves the direct descriptor id and "
        "zero keys; otherwise it calls AnimSetRemap and stores the receiver "
        "lookup keys. The shared default/idle Hands descriptor can therefore "
        "materialize either direct Player 7FB80CC8/0/0 or Chloe "
        "7D11D4FD/ED25C708/C39CA709. When context+0 is nonnull, the factory "
        "callback subsequently invokes 0x141918C30: the direct id observed "
        "at this initializer is not yet the final pose child graph.",
    ),
    0x141917A30: (
        "AnimSnapshotNodeLeaf_RefreshBreadth",
        "Leaf implementation of snapshot-node virtual slot 12 (+0x60); returns "
        "breadth one. Blend nodes recurse through this slot after all animation "
        "leaves were initialized by slot 9 during SelectRootNodeIndex.",
    ),
    0x141918070: (
        "AnimSnapshotNodeAnimation_ResolveClip",
        "Animation-node virtual slot 10. Resolves the clip from node+0x48, "
        "+0x4C and resolved animation id +0x44, then stores it at node+0x50.",
    ),
    0x1417CA2E0: (
        "AnimSetRemap_ResolveAnimationId",
        "Binary-searches authored remap tables for an animation id and returns "
        "the resolved id plus the two asset lookup keys consumed by the "
        "Animation snapshot node.",
    ),
    0x1417C8E20: (
        "AnimationDatabase_ResolveClip",
        "Resolves an animation clip from two asset lookup keys and a resolved "
        "animation id, with the existing optional fallback search.",
    ),
    0x141918120: (
        "AnimSnapshotNodeAnimation_RefreshPhase",
        "Animation-node virtual slot 11. Optionally evaluates anim-node-phase-func "
        "and stores the resulting phase at node+0x60.",
    ),
    0x1419180A0: (
        "AnimSnapshotNodeAnimation_StepPhase",
        "Animation-node virtual slot 13. Advances the dynamic phase at node+0x60 "
        "from delta time and the authored animation record.",
    ),
    0x1419182E0: (
        "AnimSnapshotNodeAnimation_GenerateAnimCommands",
        "Animation-node virtual slot 14. Emits the 0x60006 animation command from "
        "the selected clip and dynamic phase at node+0x60. STATIC provenance "
        "limit: if node+0x50 clip has flags+0x24 mask0x04, calls141775E90/141776520/"
        "1417762D0 to update stream phase and choose a chunk clip plus local "
        "phase; cmd6 at1419183B8 then contains that returned clip, not necessarily "
        "node+0x50. Missing clip emits cmd11, distinct from InstanceZero cmd9. "
        "Sample=(raw+0x22 sampleCount-1)*phase, optionally ceil by context+0x48; "
        "node+0x69 adds cmd8. Node ids/clip/phase alone do not prove emitted "
        "commands for streamed clips.",
    ),
    0x141AB6C40: (
        "AnimStateSnapshot_GenerateAnimCommands",
        "Exact AnimStateSnapshot::GenerateAnimCommands source assertion. Resolves "
        "the root node from AnimStateInstance+0x130/+0x138 and invokes virtual "
        "slot 14 with the caller-owned command-list record. When inner context+0x1C "
        "is nonzero, it then emits the exact command-7 tail {left=0,right=output,"
        "out=output}; this materializes lane-zero one-sided JointParams and ValidBits "
        "even when an InstanceZero root emitted cmd9 to clear its output. "
        "The tail weight is layer L from the caller (layer+0x28), not the "
        "instance+0x158 inter-instance/track transition weight. Animation ids were already "
        "remapped during SnapshotAnimState.",
    ),
    0x14172C370: (
        "AnimControl_CreateAnimCommands",
        "Exact AnimControl::CreateAnimCmds source assertion. Dispatches type-1 "
        "AnimSimpleLayer, type-2 AnimStateLayer, type-4, type-8 and bound "
        "type-16 AnimCmdGeneratorLayer records while preserving the caller-owned "
        "command-list record.",
    ),
    0x141893B90: (
        "AnimSimpleLayer_Construct",
        "Constructs the 0xB8-byte RTTI AnimSimpleLayer and its 0xB0-byte "
        "AnimSimpleInstance storage. AnimControl registers this object as layer "
        "descriptor type 1.",
    ),
    0x141728C10: (
        "AnimControl_AppendSimpleLayer",
        "Allocates AnimSimpleLayer, appends its type-1 descriptor to AnimControl "
        "and configures the requested channel, priority and capacity through "
        "the layer virtual setup slot.",
    ),
    0x14172A540: (
        "AnimControl_GetOrActivateSimpleLayerByChannel",
        "Finds the active type-1 layer for an authored channel or activates a "
        "fallback type-1 slot, then dispatches virtual +0x38 with the channel "
        "and request argument.",
    ),
    0x1418945E0: (
        "AnimSimpleLayer_FadeToAnim",
        "Resolves or creates an AnimSimpleInstance for an explicit clip request, "
        "inserts it at the front of the layer and seeds playback/fade state. "
        "The operation is request-driven rather than a camera sampler.",
    ),
    0x141894A90: (
        "AnimSimpleLayer_CreateAnimCommands",
        "Exact AnimSimpleLayer::CreateAnimCmds source assertion. Emits command 6 "
        "from each active clip and phase at instance+0x78, then command 7 with "
        "the fade weight at instance+0x94. It has no Player, Camera or Input read.",
    ),
    0x1418966C0: (
        "AnimSimpleLayer_Step",
        "Type-1 layer step. Advances clip phase, removes faded instances, updates "
        "the layer fade and authored animation effects. Dynamic values close over "
        "delta time, clip metadata and fade config, not Player camera direction.",
    ),
    0x1418F3AA0: (
        "AnimSimpleInstance_UpdatePhase",
        "Advances normalized phase at instance+0x7C from delta time, playback "
        "speed and clip metadata, writes command phase at +0x78 and updates the "
        "instance fade weight at +0x94.",
    ),
    0x141896950: (
        "AnimSimpleLayer_UpdateFade",
        "Updates layer+0x28 current weight toward +0x2C using remaining/total "
        "fade time at +0x30/+0x34, start weight +0x38 and authored curve +0x3C.",
    ),
    0x14189EA60: (
        "AnimStateLayer_CreateAnimCommands",
        "Type-2 layer command generator. Reads tracks in reverse array order "
        "[count-1..0], so commands fold oldest to newest and track zero is the "
        "final/current contributor. In the exact two-track path array[1] (oldest) "
        "generates first into lane 1 and array[0] (newest/current) generates second "
        "into lane 2. With inner snapshot context byte +0x1C equal to 1, each "
        "AnimStateSnapshot_GenerateAnimCommands call appends an internal command 7 "
        "with left=0, right=output, out=output, the caller-supplied mode and that "
        "snapshot's supplied layer weight L from virtual+0x10/141893B00 "
        "(layer+0x28), passed unchanged at14189EB0A/EB17. The layer then appends the outer fold "
        "left=1, right=2, out=1, mode=1, weight=current track's last-array "
        "instance[count-1]+0x158 (14189EB4B); with one instance this is the "
        "current instance. Within-track folds instead read the newly processed "
        "instance+0x158 at141AB6F47. Callback pointers layer+0x560/+0x568/"
        "+0x570/+0x578/+0x580 can replace ordinary folding/tails. "
        "Its terminal per-layer command 7 at "
        "0x14189EC8C..0x14189ECD6 is left=0, right=1, out=0, mode=1, weight=1; "
        "therefore a WeaponPartial layer can replace jointly valid base-pose rows. "
        "Its sole direct callee is AnimStateSnapshot_GenerateAnimCommands; it does "
        "not reach AnimSetRemap and contains no direct camera read.",
    ),
    0x140096DB0: (
        "InitializeFlinchPartialLayerId",
        "Computes the StringId of literal FlinchPartial and stores it in the "
        "single named persistent type-1 Player overlay layer id global.",
    ),
    0x1417292A0: (
        "AnimControl_AppendAnimCmdGeneratorLayer",
        "Allocates a 0x48-byte AnimCmdGeneratorLayer, appends its type-16 registry "
        "record to the AnimControl builder and leaves the concrete generator slot "
        "at layer+0x40 null until the separate bind step.",
    ),
    0x14172ACD0: (
        "AnimControl_BindAnimCmdGeneratorLayerByChannel",
        "Resolves an exact or inactive fallback registry record by authored channel, "
        "requires layer type 16, activates the fallback when needed, and invokes "
        "AnimCmdGeneratorLayer virtual +0x58 with the supplied concrete generator.",
    ),
    0x14197BE00: (
        "AnimCmdGeneratorLayer_Setup",
        "Exact AnimCmdGeneratorLayer::Setup source assertion. Stores channel/"
        "priority state and the required concrete IAnimCmdGenerator pointer at "
        "layer+0x40; asserts if that pointer is null.",
    ),
    0x14197BE80: (
        "AnimCmdGeneratorLayer_Step",
        "AnimCmdGeneratorLayer step virtual. Delegates time/state work only to the "
        "already-bound generator at +0x40, then runs the common layer step; it does "
        "not resolve a camera or Player owner itself.",
    ),
    0x141FF63B0: (
        "GestureJointReader_Construct",
        "Constructs the campaign Player's concrete type-16 IAnimCmdGenerator with "
        "GestureJointReader RTTI and authored joint ids such as spine-d/head-b. "
        "PublishUpperPlayerOverlayReadJointAndReferenceState stores it at "
        "Player+0x47F0 before binding base-read-joints-layer.",
    ),
    0x141FF68F0: (
        "GestureJointReader_GenerateAnimCommands",
        "GestureJointReader generator virtual +0x08. Emits command 0x17 (decimal "
        "23), hash gesture-joint-reader and the generator payload. This command is "
        "distinct from decimal command 17's encoded-scalar copy case.",
    ),
    0x141FF6DE0: (
        "GestureJointReader_AnimPluginCallback",
        "Source-identified GestureJointReader::AnimPluginCallback. Reads already-"
        "evaluated joint matrices, extracts authored spine/head transforms and "
        "publishes them through the JointTree/observer path. It is an output reader, "
        "not a Player-camera or vertical-pose source.",
    ),
    0x1406CCB30: (
        "Game_ProcessAnimPhasePluginFunc",
        "Game-side ProcessAnimPhasePluginFunc installed in the three-callback "
        "animation-manager table at 0x1466D0C50. ProcessAnimCmdListSegment decimal "
        "case 23 reaches it through AsyncUpdateJobData slot 3. Dispatches concrete "
        "game plugin StringIds, including GestureJointReader and several IK writers, "
        "then falls back to Nd_ProcessAnimPhasePluginFunc.",
    ),
    0x1406CD220: (
        "Game_ProcessAnimPostPosePluginFunc",
        "Game-side callback used by top-level animation command 24 after the "
        "current processing block is finalized. Dispatches several leg modes "
        "and falls back to Nd_ProcessAnimPostPosePluginFunc; command ids must "
        "be resolved individually before treating this callback as a pose writer. "
        "STATIC ABI has SIX inputs: WorkBuffer, Segment, AnimCmdContext, "
        "StringId, payload and JointSet/null. Installed at1466D0C58 by "
        "140157E84 and14177F7D7, copied into job+20 (not+28). Actual "
        "Process indirect call141ABAC66 is FF 55 28, returning141ABAC69. "
        "Fallback call1406CD2DC crosses a16-byte block boundary; it forwards "
        "all six arguments to Nd. Callback return is not a success witness.",
    ),
    0x141937D20: (
        "Nd_ProcessAnimPostPosePluginFunc",
        "Nd fallback for top-level animation command 24. The exact "
        "joint-conversion id 0x248B1AAE reads finalized 64-byte matrices from "
        "the evaluator workspace and writes converted matrices to the command "
        "destination; it does not write WorkBuffer pose lanes. Other ids include "
        "bounding-sphere and joint-retarget operations and are not interchangeable. "
        "STATIC id248B1AAE branch141937EEC reads Segment+0x38 group descriptor, "
        "first=u16(descriptor+0), count=u16(descriptor+0x10), source=Segment+0x58 "
        "workspace and destination=payload qword0. It transposes each source "
        "row at workspace+0x40+64*i into destination+64*(first+i); all four "
        "float4 stores are141937F5B/F63/F68/F6D. Count0 exits; otherwise the "
        "synchronous loop has no per-joint mask, bounds check or decline. "
        "Normal group0 destination is JointCache+0xE0, not the skin palette. "
        "Matching entry/normal return proves this range was exported, not "
        "that an arbitrary authored program initialized every source row. "
        "This branch does not clear JointCache valid bits or write its D8 "
        "world Locators. If a lazy reader populated a selected joint between "
        "reset and export, export alone cannot establish that Locator freshness; "
        "observe selected validity/Locator consistency at the consumer. "
        "STATIC counterexample to generic read-only postpose: id03D27700 tested "
        "at141937D5F loads JP from Segment+50 at141937F82; selected records "
        "write T at141937FCC, Q at141937FE8 and broadcast S at141938006. "
        "These are the actual STORE addresses, not preceding SIMD loads. "
        "P99 normal-suffix admission must retain its exact callback-id whitelist.",
    ),
    0x1406FB070: (
        "PointSwordAnim_RunIk",
        "Source-identified PointSwordAnim::RunIk callback selected by game animation "
        "plugin StringId 0x0265E9ED. Reads and writes evaluator pose data through "
        "the command-23 AnimPluginContext.",
    ),
    0x1406FD4A0: (
        "WsAnkleBlend_ExecutePlugin",
        "Source-identified WsAnkleBlend::ExecutePlugin callback selected by game "
        "animation plugin StringId 0x29BDC08C. Consumes explicit input/output lane "
        "indices from the command-23 payload and writes joint transforms.",
    ),
    0x141937780: (
        "Nd_ProcessAnimPhasePluginFunc",
        "Source-identified NdProcessAnimPhasePluginFunc fallback for decimal command "
        "23. Dispatches built-in IK plugin StringIds including Jacobian, hand-fix, "
        "AnimSnapshotNodeIK and AnimSnapshotNodeArmIk paths; several branches mutate "
        "JointParams or evaluator pose lanes, so plugin IDs must be resolved before "
        "treating command 23 as read-only.",
    ),
    0x142193790: (
        "AnimJointProgramOp11_DecodeVariableBitStreams",
        "Built-in joint-program opcode 11 handler. Reads the per-track bit-width "
        "control stream, derives the two packed stream offsets and dispatches the "
        "variable-bit decoder at 0x142183540. Offline execution against the retail "
        "image decodes the 33-track/395-bit steady 25CC08FD group-zero block.",
    ),
    0x142183540: (
        "AnimJointProgram_DecodeVariableBitStreams",
        "Worker used by joint-program opcode 11 to decode paired variable-width "
        "animation streams into the active JointParams rows. The opcode-11 control "
        "bytes for 25CC08FD total 395 bits (50 bytes), matching its encoded second "
        "stream displacement exactly.",
    ),
    0x142193970: (
        "AnimJointProgramOp15",
        "Exact animation joint-program opcode 15 dispatch-table target. Used after "
        "the variable-stream decode in the retail 5F913F74 group-zero program; its "
        "worker remains semantically unnamed.",
    ),
    0x1421939F0: (
        "AnimJointProgramOp16",
        "Exact animation joint-program opcode 16 dispatch-table target. Used after "
        "opcode 15 in the retail 5F913F74 group-zero program; its worker remains "
        "semantically unnamed.",
    ),
    0x142193E30: (
        "AnimJointProgramOp27",
        "Exact animation joint-program opcode 27 dispatch-table target used by the "
        "retail 5F913F74 group-zero program.",
    ),
    0x142193EF0: (
        "AnimJointProgramOp29",
        "Exact animation joint-program opcode 29 dispatch-table target used by the "
        "retail 5F913F74 group-zero program.",
    ),
    0x142193F50: (
        "AnimJointProgramOp30",
        "Exact animation joint-program opcode 30 dispatch-table target used by the "
        "retail 5F913F74 group-zero program.",
    ),
    0x141A8FCF0: (
        "AnimManager_CreateAnimCmdsForObject",
        "Exact CreateAnimCmdsForObject source assertion in anim-mgr-jobs.cpp. "
        "Dispatches an object's animation control into the command-list record "
        "later returned to AnimManager_PerformAnimBlend. In build mode 2 it emits "
        "the six-word command 14 at 0x141A8FF52..0x141A8FF89 from build-context "
        "qwords 7/6 (JointParams/scalar source pointers), after the preceding "
        "command-3/4 close and before command 15. "
        "Opcode 27 (0x6001B) is emitted only if the selected flags permit "
        "export and packedFirst+parameterCount <= CreateContext+0x50 "
        "(compare141A90466/branch141A9046A; nonzero count test141A9046E). "
        "Destination is context+0x48 plus 48*packedFirst "
        "when flags&4, otherwise 48*workspaceFirst (0x141A90484). Perform "
        "supplies Fg+0x100 with capacity group0.parameter_count: C39 group1 "
        "66+38 exceeds 66 and is never exported there. Normal builder return "
        "0x141A91604 receives the complete command list; the separate prepass "
        "return 0x141A920A7 must not be confused with that final group build. "
        "Local role-B Mazur runtime confirms both Perform and final Async normal "
        "mode-1/flags-3 lists carry one exact steady Hands clip and terminate in "
        "cmd3/4/15/16/17/joint-conversion-24/27/5. "
        "The final command list, not a transient one-track snapshot topology, "
        "is the synchronous liveness discriminator for the canonical Player or "
        "Chloe Hands ArtItemAnim pointer. "
        "Current WeaponOut/controller, owner generation and list grammar are "
        "evaluated in the same invocation; no cross-frame dwell is required to "
        "establish command ownership. "
        "A read-only owner-stage census must close Player/FG/controller/art/idle "
        "admission before the B725 write is enabled again. "
        "Its decimal-command-17 "
        "producer copies build-context qword 6 into the command payload and emits "
        "header 0x40011 at 0x141A90119. The command-16 header 0x00020010 is "
        "appended first at 0x141A900F5..0x141A90106; command 17 is appended only "
        "at 0x141A90111..0x141A9012C, so it cannot initialize case-16 op40 inputs. "
        "STATIC export27 at141A90474..4BE immediately precedes export26 "
        "at141A90543..5AE, without interveningworkpose mutation. A complete "
        "normal list exportslocal JP andskinpalette fromone workpose; this "
        "does not prove later ED0A70/read-render samegeneration. STATIC matrix "
        "cache export is separate: group ordinal0 plus context+0x78 &0x4 and "
        "flags+0x11 &0x1 (tests141A90130/13B/154) emit six-word cmd24 with "
        "joint-conversion id248B1AAE. The payload pointer is context+0x58 "
        "loaded141A9015C and stored141A90222/231. Neither a normal Process "
        "return nor a cmd26 palette export proves this conditional callback ran.",
    ),
    0x141A91490: (
        "AnimManager_BuildCommandSegmentsForObjectMask",
        "Builds the per-object command-generation context and dispatches "
        "AnimManager_CreateAnimCmdsForObject for every selected skeleton group. "
        "The normal PerformAnimBlend caller passes the persistent 48-byte local "
        "JointParams destination at FgAnimData+0x100. "
        "The builder copies argument bytes a5/a6 to CreateContext+0x10/+0x11; "
        "the AsyncJointBlend callers supply a6 from FgAnimData+0x08, so the "
        "group-zero post-pose command suffix is instance/screen dependent. "
        "Context qwords 5 and 6 preserve the supplied scalar-bank pointers; the "
        "normal PerformAnimBlend caller supplies FgAnimData+0x118/+0x120. "
        "STATIC arg13 is copied into CreateContext+0x58 at141A9153B; normal "
        "JointBlend supplies the E0 matrix-bank pointer returned by cache reset, "
        "not a workspace or skin-palette pointer. Context+0x78 is7 when arg14 "
        "is false, otherwise1. Group0 uses the primary context at141A915F5; "
        "nonzero groups use the alternate context. The group0 joint-conversion "
        "callback additionally requires actual flags+0x11 &0x1.",
    ),
    0x141A90E70: (
        "AnimManager_AllocateCommandSegmentsAndSharedScratch",
        "Allocates the 0x20-byte command-list segment records for every target "
        "skeleton processing group and one 128-byte-aligned shared scratch block "
        "sized by u16(rawSkeleton+0x1A), NOT ArtItemSkeleton+0x1A; raw is "
        "*(Art+40) at141A90F72/141A910F6. Art+30 DWORD supplies the "
        "32-byte segment-record count at141A90EA4/EBA and141A914BB. "
        "The scratch pointer is returned through "
        "the outer command context +0x18 and reused by every segment/group in "
        "the same blend pass; this allocator does not clear that block.",
    ),
    0x14022C820: (
        "ScriptManager_EvalChecked",
        "Validates the requested script id against descriptor+0x18 before "
        "dispatching the descriptor bytecode and argument array to the script VM.",
    ),
    0x141763FE0: (
        "ScriptVm_ExecuteBytecode",
        "Executes four-byte VM instructions from descriptor+0 using the qword "
        "constant pool at descriptor+0x8 and a bounded copied argument array.",
    ),
    0x141926AE0: (
        "AnimStateOverrideTable_FindStateRecord",
        "Scans enabled 24-byte override records in reverse and returns the first "
        "non-null record selected by the current AnimState state id.",
    ),
    0x141764B50: (
        "SortedIdPointerTable_Find",
        "Binary-searches the sorted uint32 id array at +0x8 and returns the "
        "parallel uint32 value/index entry at +0x10 for an exact match. "
        "CommitResolvedKnowledgeRecordIntoSinkSlot uses that returned index "
        "to select the sink's value and record-pointer arrays.",
    ),
    0x1418A0EB0: (
        "AnimStateLayer_CopyParametersToFirstInstance",
        "Traverses the first active instance track and, when instance+0x190 bit "
        "0x20 is authored, copies the complete buffer from [layer+0x530]+0x10 "
        "to [instance+0x178]+0x10 using the destination descriptor size.",
    ),
    0x1418A30A0: (
        "AnimStateLayer_Step",
        "Type-2 layer step selected by AnimControl_BeginStepInternal. Advances "
        "active AnimStateInstance tracks, copies the Player-owned master overlay "
        "state from layer+0x18 into the first track-local state at track+0x8 via "
        "AnimOverlayState_Copy at 0x1418A3725, performs the shared-parameter copy, "
        "then submits instances to AnimStateInstance_PhaseUpdate.",
    ),
    0x14172CAB0: (
        "AnimControl_PhaseUpdateType2Layers",
        "Walks enabled AnimControl registry entries, calls exact "
        "AnimStateLayer_PhaseUpdateInstances at 0x14172CB32 only for type-2 "
        "layers, then publishes the resolved anim-blends knowledge record.",
    ),
    0x14189D720: (
        "AnimStateLayer_PhaseUpdateInstances",
        "Walks active tracks and instances, passes each track-local "
        "AnimOverlayState pointer from track+0x8 to "
        "AnimStateInstance_ResolveDtGroupAndSnapshot at 0x14189D824, resolves each "
        "snapshot/dtgroup, then phase-updates the live instance.",
    ),
    0x1418A4670: (
        "AnimStateInstance_ResolveDtGroupAndSnapshot",
        "Refreshes instance+0x90 from the selected state snapshot, evaluates the "
        "optional anim-state-dtgroup-func at +0xC0, and forwards its third "
        "AnimOverlayState argument unchanged into "
        "AnimStateSnapshot_SnapshotAnimState for animation resolution. At "
        "0x1418A46D0 supplies the same overlay as a4/a5, *(instance+0x140) "
        "as a6, and *(instance+0x178) as a7; the latter is the parameter "
        "wrapper pointer, not the address of the +0x178 field. Consequently "
        "the animation factory's snapshot argument is instance+0x90, while "
        "instance+0x60 already identifies its owning layer.",
    ),
    0x14172B3E0: (
        "AnimControl_FindActiveType2LayerById",
        "Linearly scans the AnimControl registry at +0x28/+0x30 for an enabled "
        "32-byte entry with the requested layer id, then returns its layer only "
        "when layer+0x8 is exact type 2. PlayerWeaponAnimControl uses it for "
        "WeaponPartial 0xE2B990B9 at 0x14108C16E.",
    ),
    0x1418A4D50: (
        "AnimStateInstance_EvaluatePhaseFunction",
        "Evaluates optional anim-state-phase-func at instance+0xE8 and passes the "
        "48-byte parameter descriptor from instance+0x178 into script evaluation.",
    ),
    0x1418A4FD0: (
        "AnimStateInstance_PhaseUpdate",
        "Exact source assertion identifies AnimStateInstance::PhaseUpdate. Uses "
        "the instance parameter descriptor at +0x178 while sampling the authored "
        "animation tree during pre/post phase updates.",
    ),
    0x140CB47A0: (
        "PlayerAimController_SmoothAndPublishDirectionalAimWeights",
        "Steady-state branch selected at 0x140CB416F when the controller mode "
        "byte did not transition. ABI is RCX=controller, XMM1=left, XMM2=right, "
        "XMM3=up, stack arg5=down. These are pre-blend inputs. The function "
        "collapses previous left/right and up/down into signed axes, advances "
        "scalar smoothing state at controller +0x8A4/+0x8AC, then tail-jumps at "
        "0x140CB491A to "
        "PlayerAimController_PublishDirectionalAimWeights.",
    ),
    0x140CAFA20: (
        "PlayerAimController_UpdateBlendState",
        "PlayerAimController vtable slot +0x30. Called from the common Player "
        "controller phase at 0x140C9F420; advances ADS blend at this+0x8EC and "
        "resets/publishes Aim0..Aim7 animation channels.",
    ),
    0x140CADF40: (
        "PlayerAimController_IsAimAdditiveAllowedForCurrentState",
        "Selects the active Player animation-state descriptor and movement mode, "
        "then returns the authored permission for directional aim additives. "
        "ComputeDirectionalAimWeights sets controller+0x8A0 only when this is true.",
    ),
    0x140CAE480: (
        "PlayerAimController_GetAuthoredAimBlendTarget",
        "Returns the authored target value consumed by UpdateBlendState for "
        "controller+0x8EC, based on the active base animation state.",
    ),
    0x140CAF2F0: (
        "PlayerAimController_ResetAndPrimeAimAdditives",
        "PlayerAimController vtable slot +0x68. Resets published directional "
        "weights and primes controller+0x8A0 when the current Player state "
        "allows aim additives; called from independent Player state entries.",
    ),
    0x140C70760: (
        "FlashlightProcessRegistry_FindOuterByChild",
        "Scans 32 flashlight-process records of stride 0xB0, validates each outer "
        "Process handle, resolves its child Process handle at outer+0x190/+0x198, "
        "and returns the outer Process whose child matches the requested Process "
        "plus its registry index. Sibling users dispatch torch-light, torch-put-out, "
        "detach, flicker-and-die and override-radius events; the key is not a Player.",
    ),
    0x140C70900: (
        "FlashlightProcessRegistry_UpdateChildState",
        "Uses the same 32-entry flashlight outer/child Process registry as "
        "0x140C70760, "
        "updates state associated with the matched child Process, and can emit "
        "the `play-animation` event through the matched flashlight effect Process.",
    ),
    0x140C71510: (
        "FlashlightProcessRegistry_DispatchTorchLightState",
        "Matches a child Process in the 32-entry flashlight registry, stores its "
        "light-state bit and dispatches internal-event-torch-light or "
        "internal-event-torch-put-out through that Process.",
    ),
    0x140C719F0: (
        "FlashlightProcessRegistry_DispatchDetachOrFlickerAndDie",
        "Dispatches detach and flicker-and-die events while operating on the same "
        "32-entry flashlight outer/child Process registry.",
    ),
    0x140C729F0: (
        "FlashlightProcessRegistry_DispatchOverrideRadius",
        "Matches a child Process in the flashlight registry and dispatches the "
        "override-radius event through its outer effect Process.",
    ),
    0x140C73040: (
        "DcPlayerSpawnTorch",
        "Torch spawn routine identified by its own assertion name and "
        "game/player/flashlight.cpp source path; validates NdAttachableObject and "
        "dispatches turn-on-torch through the owning Process.",
    ),
    0x140CAECB0: (
        "PlayerAimController_ComputeAndPublishAimAdditives",
        "PlayerAimController vtable slot +0x50. Receives aim/body bases and "
        "directional limits, calls 0x140CB32B0, and emits the looking-down-sights "
        "event. Its target-bearing `aim-at` event is dispatched at 0x140CAF008 "
        "through hard-coded flashlight-process registry record 0; this is a torch "
        "effect event, not the visible Player-pose owner. Reached by the "
        "ground GunMoveShield path through slot +0x58.",
    ),
    0x140CAF110: (
        "PlayerAimController_ApplyWorldAimBasis",
        "PlayerAimController vtable slot +0x58. Resolves the authored "
        "*reticle-offset* publisher through 0x141504D50, copies its +0xA0/+0xA4 "
        "pair into the owner screen-route HUD state at +0x38/+0x3C, converts "
        "world basis vectors, then dispatches slot +0x50 to compute the "
        "directional aim-additive weights.",
    ),
    0x141504D50: (
        "ResolveReticleOffsetPublisherByNetMode",
        "Lazily resolves either *net-reticle-offset* or *reticle-offset* according "
        "to HIBYTE(g_NetModeFlags) and returns the validated publisher handle. "
        "PlayerAimController_ApplyWorldAimBasis reads float2 +0xA0/+0xA4 from "
        "the returned object for the per-screen reticle offset.",
    ),
    0x140CAF470: (
        "PlayerAimController_CommitNamedAimChannel",
        "Commits one named aim-add float channel, including zero/clear semantics, "
        "generation tracking and dirty publication. Called by the directional "
        "publisher for left/right/down/up in that order.",
    ),
    0x140CAF040: (
        "PlayerAimController_ApplyBodyBasisAim",
        "PlayerAimController vtable slot +0x60. Builds the remaining body basis "
        "through slot +0xE8, then dispatches slot +0x58.",
    ),
    0x140CAFD90: (
        "PlayerAimController_GetAimOriginPoint",
        "PlayerAimController vtable slot +0x80. Chooses targHead or targPelvis "
        "from weapon state, resolves that Player skeleton joint, and returns its "
        "world point. Slot +0x58 uses it as the origin for target-origin direction.",
    ),
    0x140CB41F0: (
        "PlayerAimController_PublishDirectionalAimWeights",
        "Publishes pre-blend aim-add-left/right/down/up to controller "
        "+0x8B8..+0x8C4 and active state +0x190/+0x194/+0x188/+0x18C. "
        "Only the named-channel branch scales the four weights by ADS blend "
        "this+0x8EC; Aim0..Aim7 CRCs are initialized by the "
        "PlayerAimController constructor 0x140CAE670.",
    ),
    0x1410D5D40: (
        "AnimControl_ResolveActiveBaseStatePlayerInfo",
        "Resolves the active authored 'base' animation state and returns its "
        "Player-info record; falls back through the "
        "'default-anim-state-player-info' token. "
        "PlayerAimController_PublishDirectionalAimWeights tests the byte behind "
        "returned record +0x38 to select named commits versus fade/release.",
    ),
    0x140CB32B0: (
        "PlayerAimController_ComputeDirectionalAimWeights",
        "Normalizes targetPoint-originPoint, measures signed horizontal/vertical "
        "angles against the Player body basis, clamps them by the supplied limits, "
        "then calls PlayerAimController_PublishDirectionalAimWeights.",
    ),
    0x140CB1720: (
        "PlayerAimController_GetTargetPointFromReticle",
        "PlayerAimController vtable slot +0xF8. Source assertion supplies the exact "
        "method name. Builds cameraOrigin + cameraForward * ray distance from the "
        "owner Player+0x4010 screen-route pose seed, with targetable collision adjustment.",
    ),
    0x140E84350: (
        "GunMoveShield_CopyAimTargetPoint",
        "Player::GunMoveShield vtable 0x142A27658 slot +0xF0. Copies the 16-byte "
        "target point unchanged before the ground ADS path passes it to the aim controller.",
    ),
    0x14014D9F0: (
        "Quaternion_RotatePositiveZ",
        "Returns q*(0,0,1)*inverse(q): x=2(xz+wy), y=2(yz-wx), "
        "z=1-2(x*x+y*y). GunMoveShield uses it to derive the body forward vector.",
    ),
    0x1410E3670: (
        "Player_CheckAimActionEligibility",
        "Shared Player gameplay-state gate used immediately before native aim "
        "dispatch by PlayerGunMoveShield_UpdateAndDispatch and Player::ZeroG. "
        "Checks camera/look suppression and weapon/action controller state.",
    ),
    # --- 2026-07-28: per-Player look-source boundary ---
    0x140ED94B0: (
        "PlayerLookAtIkController_Construct",
        "Constructs the 0x460-byte PlayerLookAtIkController and installs vtable "
        "0x142A2D550. Initializes the two authored target slots and the three "
        "IK solver records consumed by UpdateIk.",
    ),
    0x140ED95F0: (
        "PlayerLookAtIkController_SetupSkeleton",
        "Binds the controller to its owning Player skeleton and resolves the "
        "authored head/root joint chain. The *player-ik-look-point* string "
        "anchors this setup to the native look-at subsystem.",
    ),
    0x140EDA5F0: (
        "PlayerLookAtIkController_SetLookPoint",
        "Vtable slot +0x28. Stores the caller-provided weight, parameter and "
        "world point in one of two 0x40-byte authored target slots and marks "
        "that slot as point mode; it invents no camera or global state.",
    ),
    0x140EDA630: (
        "PlayerLookAtIkController_SetLookLine",
        "Vtable slot +0x30. Stores the caller-provided weight, parameter and "
        "two float4 world-line endpoints in one of the same per-controller "
        "target slots and marks that slot as line mode.",
    ),
    0x140EDA680: (
        "PlayerLookAtIkController_UpdateIk",
        "Vtable slot +0x38. Selects and eases the staged point/line slots into "
        "controller +0x3E0/+0x3F0, then solves the controller-owned head/spine "
        "joint chain. It consumes no CameraManager directly.",
    ),
    0x140EDB180: (
        "PlayerLookAtIkController_StageCameraDerivedLookTarget",
        "Vtable slot +0x40. Resolves targHead from the owning Player attach "
        "system, reads owner Player+0x4010, derives a target from "
        "ResolveScreenRoutePoseSeedOrDefault and stages it through vslot +0x28. "
        "Known xrefs are vtables/script binding; no ordinary per-frame caller "
        "has yet been proven.",
    ),
    0x1410D1B20: (
        "Player_GetCameraLookAtPoint",
        "Player vtable slot +0xE80. Returns one float4 world-space camera "
        "look-at point through the caller output pointer. Tries the scripted "
        "get-camera-look-at-point override, then calls the stock fallback.",
    ),
    0x1410D25C0: (
        "Player_GetDefaultCameraLookAtPoint",
        "Source assertions give the exact method name. Builds the Player camera "
        "look-at point and selects local input state through "
        "g_LocalPlayerInputManager_Ptr, which is process-global.",
    ),
    0x1410D37C0: (
        "Player_UpdateCameraLookAtPointState",
        "Player vtable slot +0xEA0. Calls the same Player's +0xE80 camera "
        "look-at getter, performs spring/body/stance processing, and writes the "
        "filtered point to Player+0x38C0.",
    ),
    0x1410D5710: (
        "Player_QueryNamedLookAimPointPair",
        "Player vtable slot +0x2D8. Maps LookAtPos, CameraLookAtPos and "
        "CenterPos to Player+0x38C0; distinct AimAtPos/ChestAimPos/NeckAimPos/"
        "SecondAimAtPos names use +0x38F0/+0x38D0/+0x38E0/+0x3900.",
    ),
    0x1410D5B30: (
        "Player_QueryNamedLookAimPoint",
        "Single-point CRC query used by NPC host/control helpers. Maps the same "
        "LookAtPos/AimAtPos/ChestAimPos/NeckAimPos/SecondAimAtPos names to "
        "Player+0x38C0/+0x38F0/+0x38D0/+0x38E0/+0x3900.",
    ),
    0x1406320C0: (
        "AiKnowledgeEntity_UpdateTrackedAimVectorAndNavFrame",
        "AI-knowledge path. Queries a tracked actor's AimAtPos and "
        "SecondAimAtPos through vtable+0x2D8, stores Second-Aim at this "
        "AiEntity+0x1C0, then updates its navigation frame. This is not a "
        "rendered Player-pose consumer.",
    ),
    0x140632A10: (
        "NpcLookAim_ResolveAuthoredAimPoint",
        "Authored NPC LookAim Sniper/Watch bridge. Selects NeckAimPos or "
        "SecondAimAtPos through NpcLookAim_QueryTargetNamedPoint and otherwise "
        "falls back to an attachment/world offset; it does not drive Player "
        "self-pose.",
    ),
    0x140632DC0: (
        "NpcLookAim_QueryTargetNamedPoint",
        "Helper for the authored NPC LookAim Sniper/Watch path. Resolves the "
        "target Player and queries one of two named look/aim-point CRCs through "
        "vtable+0x2D8.",
    ),
    0x1410E2840: (
        "ScreenRoute_HasPreferredAimCameraCategory",
        "Accepts a screen index, resolves its routed entity and preferred camera "
        "child, then validates the child descriptor/category and status. The "
        "Player named-aim-point writer uses it to select camera-derived points "
        "instead of chest/neck fallbacks.",
    ),
    0x1410ED500: (
        "Player_UpdateNamedChestNeckAndAimPoints",
        "Player vtable slot +0xE58, called unconditionally by Player_Update at "
        "0x1410ECA2B. Resolves targChest/targNeck into +0x38D0/+0x38E0 and "
        "writes +0x38F0/+0x3900 either from the Player screen-route pose seed "
        "plus target offset or from the chest/neck fallback.",
    ),
    0x140CB6530: (
        "PlayerAimingMachine_EvaluateEdgeAimOrientation",
        "PlayerAimingMachine vtable slot +0x08. CameraEdgeAim invokes it from "
        "CameraControl UpdateLocation. Although it copies the 0x3F8 camera-0 "
        "template, 0x141693920 immediately overwrites every orientation row "
        "from per-machine inputs before the result becomes CameraLocation.",
    ),
    0x140CB8290: (
        "PlayerAimingMachine_UpdateEdgeAimState",
        "PlayerAimingMachine vtable slot +0x10, reached by CameraEdgeAim. Its "
        "camera-0 state copy is likewise followed by a complete per-machine "
        "orientation overwrite; output remains in the CameraControl/CameraLocation "
        "path and does not write Player, FgAnim or joints.",
    ),
    # --- 2026-07-12: scoped aim route-camera owner ---
    0x140CB0200: (
        "PlayerAimController_KickTargetPointJob",
        "PlayerAimController vtable 0x142A0B1F8 slot +0xE0. Source assert names "
        "KickTargetPointJob; the sniper branch resolves Player+0x4010 through "
        "ResolveScreenRouteEntityByIndex and SelectPreferredCameraChildFromRouteEntity. "
        "The selected child +0x1A8 is a camera descriptor, not a vtable; virtual "
        "slot +0x230 is only the unzoomed-sniper status query.",
    ),
    # --- 2026-07-11: scoped puppet DrawControl visibility ---
    0x141754510: (
        "IDrawControl_HideMesh",
        "Validates meshIndex, writes mesh[meshIndex].visible byte at +0x243 "
        "(entry+579) to 0, then refreshes DrawControl state.",
    ),
    0x1417545F0: (
        "IDrawControl_DisableMeshFromMainView",
        "For every bound child of the selected mesh, sets render-instance flag "
        "0x40 at instance+0x18.",
    ),
    0x141754A30: (
        "IDrawControl_ShowMesh",
        "Inverse of IDrawControl_HideMesh: writes mesh entry +579 to 0xFF and "
        "refreshes DrawControl state.",
    ),
    0x141754B10: (
        "IDrawControl_EnableMeshInMainView",
        "Inverse of DisableMeshFromMainView: clears render-instance flag 0x40 "
        "for every bound child of the selected mesh.",
    ),
    # --- 2026-07-11: native grenade network-event receive path ---
    0x140BE5C80: (
        "NetEventEmitGrenade_DefaultCtor",
        "Initializes the 0xC0-byte receive event and its vtable. Used before "
        "deserializing the POD payload.",
    ),
    0x140BE5D30: (
        "NetEventEmitGrenade_BuildFromProjectile",
        "Sender constructor. Copies owner identity, transform, weapon/type, and "
        "the grenade descriptor block from a live ProjectileGrenade.",
    ),
    0x140BE6140: (
        "NetEventEmitGrenade_Deserialize",
        "Vtable +0x8. Reads the complete wire payload into event fields +0x18..+0xB5.",
    ),
    0x140BE6260: (
        "NetEventEmitGrenade_Serialize",
        "Vtable +0x10. Symmetric writer for the POD event payload.",
    ),
    0x140BE63A0: (
        "NetEventEmitGrenade_Execute",
        "Vtable +0x18 receiver apply. Builds ProcessSpawnInfo, invokes the native "
        "descriptor materializer, and binds the spawned grenade network id.",
    ),
    0x140BE7000: (
        "SendNetEventEmitGrenade",
        "Source-identified sender wrapper in net-event-weapon.cpp. Builds a "
        "NetEventEmitGrenade from a live ProjectileGrenade and queues it.",
    ),
    0x141544CD0: (
        "ProjectileGrenade_ResolveOwner",
        "Resolves the grenade owner from its screen index or retained owner pair.",
    ),
    # --- 2026-07-10: Player FgAnimData async lifecycle ---
    0x14015E8B0: (
        "GameLoop_RunAnimationUpdateBucket",
        "Runs one animation/process bucket. Before the next AnimMgr update, "
        "calls ProcessMgr_DestroyDeadProcesses at 0x14015E9D7. Chooses async "
        "manager 0x1417816B0 or AnimMgr_SyncUpdate; bucket 5 waits/releases "
        "the escaped counter 0x146AF2798 at 0x14015EAA6/AAE. Normal job "
        "lifetime is a phase barrier, not a refcount lease on weapon state.",
    ),
    0x1415C51E0: (
        "Ndjob_SpawnerJobAndWaitForChildren",
        "Source-identified SpawnerJob in spawner-job.cpp. Replaces child "
        "completion counters at 0x1415C5316, submits batches, waits for all "
        "children at 0x1415C53F9, and only then releases original completion "
        "counters at 0x1415C541C. AnimMgr_AsyncUpdateAndWaitForBuckets waits "
        "these spawners before returning, covering normal g0/g1 Async work.",
    ),
    0x1416229C0: (
        "ProcessMgr_DestroyDeadProcesses",
        "Under the ProcessMgr recursive lock, calls dead-process cleanup "
        "0x1416A6720 -> 0x1416A6620 -> 0x1416A62B0 -> process vtable slot0. "
        "Normal animation bucket wrapper invokes this before the next manager "
        "update, after prior native animation jobs have completed. Player "
        "destruction eventually reaches NdGameObject teardown and FreeAnimData; "
        "the pool free function itself has no job wait.",
    ),
    0x141781390: (
        "AnimManager_CreateAsyncUpdateJobFunctor",
        "Creates the async animation-manager job for an FgAnimData object. The "
        "job callback is AnimManager_AsyncObjectUpdateJointBlendJob 0x141A92A00. "
        "At 0x141781502 writes job+8=Fg; 0x141781506/50A copies *(Fg+0x10) "
        "into job+0 (AnimControl, NOT Player). Appends a 0x38-byte record. "
        "JointBlend callback LEA RAX at 0x1417815B7 flows to template+0 at "
        "0x141781601 and the copied 0x80-byte third-category record at "
        "0x141781636. Generic worker invokes record+0 with RCX=record+8 "
        "at 0x1415D11B7. This callback-value boundary leaves native unwind "
        "intact; already queued old callbacks are not rewritten. AFE source-only "
        "exam uses this ingress and sync141782B3A ONLY, with unchanged native "
        "Process/cmd16 sites; one owned preparation never publishes pose rows.",
    ),
    0x141782AF0: (
        "AnimManager_AppendSyncUpdateJobAndStepParams",
        "Sync-manager callback referenced at 0x1417831B7. Appends a 0x38-byte "
        "job to *arg2: job+8=Fg at 0x141782AF3, job+0=*(Fg+0x10) at "
        "0x141782AFE, job+0x30=0, then tail-dispatches animation parameter step. "
        "Independent producer proof for native animation job layout.",
    ),
    0x1417816B0: (
        "AnimMgr_AsyncUpdateAndWaitForBuckets",
        "Normal asynchronous animation manager. Creates three SpawnerJob "
        "records per bucket set at 0x1417820BA/212C/21B6 and waits all six "
        "spawner counters in the loop at 0x1417829B9/C6/D4 before returning. "
        "Spawner completion includes its native children, so the normal "
        "AsyncObjectUpdateJointBlendJob g0/g1 work is bounded by this phase. "
        "This does not freeze gameplay WeaponOut or arbitrary mod callbacks.",
    ),
    0x141BB7DD0: (
        "NdGameObject_DisableAnimationAsyncPhases",
        "Source-identified nd-game-object animation disable path. Clears FgAnimData "
        "phase bits 0x08/0x10/0x20 (mask 0x38).",
    ),
    0x141BB8220: (
        "NdGameObject_EnableAnimationAsyncPhases",
        "Source-identified nd-game-object animation enable path. Sets FgAnimData "
        "phase bits 0x08/0x10/0x20 (mask 0x38).",
    ),
    0x141BB88A0: (
        "NdGameObject_EnsureAnimationEnabled",
        "Ensures the owning FgAnimData is enabled when its state field at +4 is zero.",
    ),
    # --- 2026-07-10: pickup-gesture recipe (interact command activation) ---
    0x14183A650: (
        "InputScreenBlock_ActivateCommand",
        "Activates a command id in the per-screen dynamic command list at "
        "block+0x590 (block = g_LocalPlayerInputManager + 3504 + 3056*screenIdx). "
        "Sibling of InputScreenBlock_IsCommandActive (0x14183A3D0). KEY to the "
        "co-op pickup gesture: activating the weapon-pickup interact command here "
        "directly bypasses the +0x518 button routing that never reaches the puppet.",
    ),
    0x141C9A360: (
        "InteractControl_GetAvailabilityForPlayer",
        "Called by Player_InteractDispatcher before action submission. Calls "
        "interactControl vtable+0x8 with the interactable object and a retained "
        "Player handle {player+0xA8, player+0xBC}; return 1 permits the later "
        "vtable+0x28 RequestInteract call. It does not start an action itself.",
    ),
    0x140C805A0: (
        "Player_BuildInteractDispatchState",
        "Builds the 0x15C-byte state consumed by Player_InteractDispatcher. "
        "Resolves the indexed Player, copies the supplied transform, snapshots "
        "the per-screen *interact-command-list*, and queries +0x590 command state.",
    ),
    0x140C88220: (
        "Player_TickInteractDispatcherForScreen",
        "Resolves g_PlayerHandleForHudPublish[*state], then calls "
        "Player_InteractDispatcher on the matching per-screen dispatcher. The "
        "input must already be populated by Player_BuildInteractDispatchState.",
    ),
    0x140DCDEA0: (
        "Player_ScriptWeaponEventHandler",
        "Giant script weapon-event handler (0x8b2d). The ground-pickup give-weapon "
        "branch (hash 0xFB1091A3) returns to 0x140DD4208 - this is the path a real "
        "ground pickup takes, NOT WeaponPickupInteractControl::RequestInteract "
        "(runtime-proven 2026-07-10: RequestInteract hook installed but never fires).",
    ),
    # --- 2026-07-07: weapon give / slot placement (pickup display root) ---
    0x141094420: (
        "WeaponArtDef_GetCampaignNormalizedClass",
        "STATIC reads signed32 WeaponArtDef+8 at141094427. When NetModeFlags+3 "
        "is zero, aliases16->0 and17/22->1; otherwise returns raw class. "
        "GiveWeapon calls1410A5BF4 and passes EAX as slot argumentR9D into "
        "vslot130 CreateAndAddWeapon1410A8920 at1410A5C3E. Pinned P99 art "
        "8075397D has rawclass0, so ordinary Give selects slot0. Does not "
        "prove all scripted slot-placement routes or explain a historical "
        "controller-slot refusal without the actual captured value.",
    ),
    0x1410A8920: (
        "PlayerWeaponController_CreateAndAddWeapon",
        "vtable +0x130. Resolves WeaponArtDef by art, creates ProcessWeapon, "
        "initializes live ammo from the supplied total, and writes its weak "
        "handle to the caller-supplied manager slot without a duplicate lookup "
        "or visible slot bounds check. The stock grenade loadout call supplies "
        "manager slot 2 and trailing args false,false,-1,-1,null,false. Reuse an "
        "exact-art live object first and abort if slot 2 contains another object.",
    ),
    0x1410A8670: (
        "PlayerWeaponController_KillWeaponsInClasses_1_17",
        "vtable +0x168. ProcessKillTree(GetWeaponByClass(ctrl,1)) then class 17 - "
        "the REMOVE primitive. GetWeaponByClass = ctrl vtable +0x188; ProcessKillTree "
        "destroys the weapon object. Building block for a pickup SWAP (kill the "
        "puppet's conflicting-class weapon, then GiveWeapon the relayed art).",
    ),
    0x1410A8750: (
        "PlayerWeaponController_KillAllWeapons",
        "vtable +0x170. ProcessKillTree over every weapon class (remove-all).",
    ),
    0x1413B49B0: (
        "Hud_EnqueuePickupNotification",
        "Appends {art, ammo, ...} to the indexed Hud object's notification list "
        "at *(hud+0x320)+0x1890 (32-byte stride, count at +0x1930, cap 5). "
        "PlayerWeaponController::GiveWeapon selects Hud by owner Player+0x4010; "
        "this is HUD state, not the character pickup-action state machine.",
    ),
    0x1413B2340: (
        "Hud_ConstructForScreen",
        "Constructs one 0x368-byte Hud object for the supplied screen index and "
        "binds its 0x320 state pointer to the matching 0x19A0-byte static block.",
    ),
    0x1413B2730: (
        "Hud_Init",
        "Initializes the indexed Hud object, command observers, weapon tables, "
        "and sets Hud+0x21 ready. Called for both screen 0 and screen 1.",
    ),
    0x1413B2950: (
        "Hud_UpdateForScreen",
        "Updates one Hud object, publishes reticle offsets, sets the current-Hud "
        "globals, and runs Hud_UpdateChildren.",
    ),
    0x14184DE90: (
        "Hud_UpdateChildren",
        "Updates registered Hud child controllers when Hud+0x21 and Hud+0x14 are "
        "active, then calls the Hud vtable +0x80 finalizer.",
    ),
    0x1413538B0: (
        "Hud_UpdateScreen0Frame",
        "Stock frame wrapper: prerequisite, Hud_UpdateForScreen(screen 0), then "
        "the HUD frame finalizer. It does not update the screen-1 Hud object.",
    ),
    0x141355D00: (
        "FHUD_InitializePerScreenHudFlash",
        "Initializes one per-screen HudFlash state from state+0xF0, resets four "
        "0x710-stride physical interact records, and registers reload/start/stop "
        "movie observers. Startup calls it once for screen 0 and once for screen 1.",
    ),
    0x1413569E0: (
        "FHUD_UpdateScreen0HudFlashFrame",
        "Screen-0 HudFlash frame owner. Updates screen-0 HUD state and calls "
        "FHUD_UpdateInteractScriptsAndExpire at 0x141356BF1; that callee's stock "
        "expiry is the sole caller of FHUD_DestroyInteractScript.",
    ),
    0x140DCDEA0: (
        "Player_ScriptWeaponEventHandler",
        "Player script-event handler (switch on event name-hash). Hash 0xFB1091A3 "
        "branch = give-weapon: builds WeaponInfo and calls GiveWeapon (vt+0x158). "
        "The real ground-pickup give path (ret addr 0x140DD4208).",
    ),
    # --- 2026-07-06: weapon pickup sync design (see FINDINGS_ARCHIVE.md) ---
    0x14157AA70: (
        "WeaponPickupInteractControl_OnInteract",
        "Named by its own assert (weapon-pickup.cpp). vtable +0x10 of "
        "WeaponPickupInteractControl (0x142AA6E90). Executes the local weapon "
        "pickup: give-weapon, pickup-ammo-count, art registry lookups. SENDER "
        "hook point for the co-op pickup relay.",
    ),
    0x14157B690: (
        "WeaponPickupInteractControl_GetHudAmmoCount",
        "Named by its own assert (weapon-pickup.cpp).",
    ),
    0x1408C8A40: (
        "WeaponPickupInteractControl_RequestInteract",
        "WeaponPickupInteractControl vtable +0x28. Identity is confirmed by its "
        "PickupInteractControl::RequestInteract assert in process-pickup.cpp. "
        "Publishes interact-with-me to the pickup at action start.",
    ),
    0x1408AEA00: (
        "PickupInteractControl_PublishHudPrompt",
        "Shared vtable +0xE0 entry in many interact-control classes. Confirmed "
        "aliases include CarryObjectInteractControl (0x1429B6878), the true "
        "PickupInteractControl vtable (0x1429B9840), WeaponInteractControl "
        "(0x1429BF4A8), and WeaponPickupInteractControl (0x142AA6E90). a2 is "
        "the interactable GameObject and a3 is the Player: the record's "
        "+0x18/+0x20 weak key is copied from a2+0xA8/a2+0xBC. The Player handle "
        "selects a HUD index through player-manager vtable +0xD0 (not "
        "Player+0x4010), then call 0x1408AF179 refreshes the four-slot FHUD "
        "interact store through 0x141359470. Runtime io rejected the retained-"
        "Player a3 source gate: the stale Chloe prompt produced no retained "
        "owner call. The weak-resolved HUD ProcessWeapon can instead remain "
        "distinct from the same-id OnInteract source killed at 0x141506839.",
    ),
    0x140328C20: (
        "HudScript_DrawInteractableOnObject",
        "Script native registered by the exact hud-draw-interactable-on-object "
        "string xref at 0x14003A207. Parses an explicit screen/player index, "
        "builds a fallback-keyed interact record with zero object weak handle, "
        "selects g_PerScreenPlayerHudState, and publishes at call 0x1403290FD.",
    ),
    0x141359470: (
        "FHUD_PublishInteractRecord",
        "Common FHUD interact-record publisher. Validates a record object weak "
        "handle at +0x18/+0x20 by resolving weak+0x8 and comparing "
        "object+0xBC, recognizes WeaponPickup, and otherwise forwards "
        "fallback-keyed records to the physical HUD store. The authored ground "
        "weapon source callsite is 0x1408AF179; the script fallback callsite is "
        "0x1403290FD.",
    ),
    0x1413B48A0: (
        "FHUD_PublishInteractRecordIfScreenEnabled",
        "Small conditional tail-forwarder: indexes g_PerScreenPlayerHudState "
        "from owner+0x308, tests the screen FHUD enabled byte, and tail-jumps to "
        "FHUD_PublishInteractRecord at 0x1413B48C2. RCX/RDX remain the exact HUD "
        "state and interact record ABI.",
    ),
    0x140167D70: (
        "StringIdBoxedValueMap_Set",
        "MapArray<StringId,BoxedValue,1>::Set. Uses the sorted 24-byte entry "
        "array at map+0, capacity at +8 and count at +0x10; each entry has its "
        "StringId at +0 and a 16-byte BoxedValue at +8.",
    ),
    0x140168170: (
        "StringIdBoxedValueMap_LowerBound",
        "Binary lower_bound for the sorted 24-byte StringId/BoxedValue entries. "
        "Returns the insertion/match index for the caller-provided StringId.",
    ),
    0x140C888C0: (
        "FHUD_ResolvePickupWeaponArt",
        "Resolves pickup-weapon-id for an interactable and Player. Uses "
        "ProcessWeaponBase+0x860, WeaponPickup+0x520, or the grenade-specific "
        "field according to selector/type identity.",
    ),
    0x141353750: (
        "FHUD_ResetInteractRecord",
        "Resets one physical interact record. Explicitly zeroes its weak key at "
        "+0x18/+0x20 and fallback key +0x28, then restores the default record "
        "timing, transform and state fields. Called by DestroyInteractScript.",
    ),
    0x141354EA0: (
        "FHUD_UpsertInteractRecord",
        "Upserts one submitted interact record into the four-slot physical FHUD "
        "store. Iterates four 0x710-stride records, matches live weak identities "
        "through weak+0x8/object+0xBC (or fallback +0x28), copies the record and "
        "sets that slot's refresh byte; otherwise selects one free slot.",
    ),
    0x141358030: (
        "FHUD_UpdateSingleInteractScript",
        "Builds or refreshes one of four physical interact Flash scripts. The "
        "slot is store+index*0x710: active process weak handle at +0x1F8, "
        "StringId/BoxedValue parameter map at +0x808, copied record at +0x830. "
        "Publishes interact-id from object+0xD8 and pickup-weapon-id through "
        "FHUD_ResolvePickupWeaponArt before creating/reusing the Flash process.",
    ),
    0x141357CA0: (
        "FHUD_DestroyInteractScript",
        "FHUD::HudFlash::DestroyInteractScript. For one of four physical HUD "
        "slots, kills only the active Flash process through ProcessKillTree, "
        "clears the active flag, and calls FHUD_ResetInteractRecord to zero the "
        "slot's weak/fallback key. Its only code caller is stock per-frame expiry.",
    ),
    0x141357D60: (
        "FHUD_UpdateInteractScriptsAndExpire",
        "Per-frame owner of the four physical FHUD interact records. Validates "
        "object handle/generation at record +0x18/+0x20, draws refreshed slots, "
        "expires stale slots through FHUD_DestroyInteractScript, and clears each "
        "refresh byte.",
    ),
    0x141505BB0: (
        "WeaponInteractControl_OnInteract",
        "Accepted ordinary ground-weapon interaction. RDX is the source "
        "ProcessWeapon, R8 is Player. Reads source stable id +0xD8 and art "
        "+0x860, publishes give-weapon, then commits source destruction through "
        "ProcessKillTree at call 0x141506839 (return 0x14150683E). The sole "
        "runtime dispatch reference is WeaponInteractControl vtable +0x10 at "
        "0x1429BF4B8; use that slot instead of relocating this EH-bearing entry. "
        "Runtime io showed its source pointer differs from the FHUD weak target "
        "while stable id and art match. Runtime ip nevertheless found zero "
        "distinct live same-id HUD targets after every accepted interaction, so "
        "a second ProcessKillTree call at this boundary is rejected. The outer "
        "call remains active while nested PlayerWeaponController_GiveWeapon runs; "
        "after stock return it owns the source-kill, old-detach and COPW commit, "
        "providing an exact transaction scope for the nested WeaponInfo block.",
    ),
    0x141ED3630: (
        "ProcessWeapon_DetachToWorldTransform",
        "Detaches a ProcessWeapon into world space: sets +0x690 bit 3 and writes "
        "translation/rotation to +0x590/+0x5A0. GiveWeapon replacement calls it "
        "at 0x1410A68AC (return 0x1410A68B1) for the exact dropped old weapon. "
        "COPW v2+ captures and revalidates that object's +0xD8 stable id directly "
        "across this call; the next ground interaction reads the same field. "
        "Runtime ir proves +0x690 bit 3 is only a transition invariant: after "
        "detach, the retained weak cell may target a different ProcessWeapon "
        "address with the same generation/art while PickupControl publishes it "
        "as a ground candidate, so later identity must not require this bit.",
    ),
    0x1408C8950: (
        "PickupInteractControl_CompletePickup",
        "Common successful-pickup completion. Weapon-specific caller "
        "0x14157B44B passes WeaponPickup+0xD8 to SendNetEventPickupPickedUp, then "
        "sets the consumed/hidden state and invokes the native remove path.",
    ),
    0x140BCB650: (
        "SendNetEventPickupPickedUp",
        "Builds the one-shot picked-up event. The weapon pickup completion passes "
        "the canonical WeaponPickup+0xD8 object/name id as event+0x20.",
    ),
    0x140BCAD40: (
        "NetEventPickupPickedUp_Apply",
        "Resolves event+0x20 and publishes the native net-pick-up-other-client "
        "lifecycle for the consumed world object.",
    ),
    0x140C87D70: (
        "Player_PublishRequestedInteract",
        "Called immediately after PickupInteractControl::RequestInteract by the "
        "player interact dispatcher. Publishes player-requested-interact with the "
        "interactable class/name ids; target actor comes from current thread-local "
        "player state.",
    ),
    0x140C88130: (
        "PlayerInteractCommandTable_IsTriggered",
        "Checks the dispatcher command table's pressed byte or hold duration for "
        "one command id. This is the final input gate before pickup RequestInteract.",
    ),
    0x140BCDFC0: (
        "NetEventSyncWeaponPickups_Ctor",
        "Writes vftable 0x1429FCF90, type id 0x0101006A at +8, count u32 at +24; "
        "entries at +28 = {u32 WeaponPickup+0xD8 stable id, float timestamp} pairs.",
    ),
    0x140BCE0B0: (
        "NetEventSyncWeaponPickups_Serialize",
        "Bit-stream write: count 7 bits, then id:32 + float:32 per entry.",
    ),
    0x140BCE440: (
        "NetEventSyncWeaponPickups_Apply",
        "Consumes the entry list: resolves each id via the game-object handle "
        "registry (ResolveGameObjectHandleById) and drives the native "
        "net-pick-up-other-client flow (removes ground pickup, processes remote "
        "client pickup). Do not call this from retained Player update: the nested "
        "registry lock can spin forever when another engine context owns it.",
    ),
    0x140BCE150: (
        "NetEventSyncWeaponPickups_KillEligibleProcess",
        "Stock resolved-object callback used by NetEventSyncWeaponPickups_Apply. "
        "It admits Dropped/NetDropped parents and then type-gates ProcessWeapon or "
        "ProcessShield before calling ProcessKillTree at 0x140BCE408. This is the "
        "native terminal primitive after registry resolution.",
    ),
    0x140BCE850: (
        "NetEventSyncWeaponPickups_WalkFill",
        "WeaponPickup RTTI-filtered late-join fill. Appends object+0xD8 and the "
        "engine-clock timestamp, independently confirming the canonical world id.",
    ),
    0x140BCC4B0: (
        "NetEventAddDroppedPickup_CtorFromGameObject",
        "Fills the portable dropped-pickup record from an NdGameObject: stable id, "
        "network art index, transform, ammo/state and attachment metadata.",
    ),
    0x140BCC9D0: (
        "NetEventAddDroppedPickup_CtorFromProcessWeapon",
        "Fills NetEventAddDroppedPickup from a detached ProcessWeapon, but is not "
        "a campaign-safe raw serializer: it calls WeaponArtId_ToNetworkIndex and "
        "ResolveGameObjectHandleById, and copies ProcessWeapon+0xD8 only after the "
        "stock sender has normally assigned a NetDroppedPickup id. Compact art "
        "index 0 is valid despite older relay code rejecting it. Native event "
        "+0x1C is this stable id. Runtime ip rejected using COPW payload+4 as the "
        "key because the exact WeaponInteract detach path never invoked this "
        "constructor and sent a zero payload; COPW v2+ carries ProcessWeapon+0xD8 "
        "explicitly instead.",
    ),
    0x140BCD0F0: (
        "NetEventAddDroppedPickup_Apply",
        "Converts the network art index, creates the dropped ProcessWeapon at the "
        "relayed transform and registers it in the native pickup manager.",
    ),
    0x140BCD750: (
        "SendNetEventAddDroppedPickup_Object",
        "Stock drop sender reached from GiveWeapon at 0x1410A695E only when the "
        "native net-mode byte is set. Generates a NetDroppedPickup id, registers "
        "the object, constructs the event and sends it.",
    ),
    0x140BCD970: (
        "SendNetEventAddDroppedPickup_ProcessWeapon",
        "ProcessWeapon overload: constructs the same 0x80-byte dropped-pickup "
        "event via 0x140BCC9D0 and queues it to the stock net manager.",
    ),
    0x140A97600: (
        "NetGameManager_GenerateDroppedPickupId",
        "Generates the canonical NetDroppedPickup-%i-%i hash when seed id is zero; "
        "used to avoid a receiver-local world-object id collision before Apply.",
    ),
    0x140A989C0: (
        "ResolveGameObjectHandleById",
        "Generic game-object handle lookup: returns {ptr,id} handle validated by "
        "*(obj+0xBC)==id. Its global owner acquisition spins at "
        "0x140A98A00..0x140A98A04; a receiver call from retained Player update was "
        "runtime-confirmed deadlocked there, and SEH cannot catch that wait.",
    ),
    0x141579150: (
        "WeaponPickup_Factory",
        "Registered for class name WeaponPickup (registration sub_1400C5560); "
        "object size 1376, vftable 0x142AA66C0.",
    ),
    0x140C85D20: (
        "Player_InteractDispatcher",
        "Generic player interact scan/dispatch (player-pressed-interact-nearby, "
        "player-interact-hold-*). Checks commands against the PER-SCREEN dynamic "
        "command list (input block +0x590) via InputScreenBlock_IsCommandActive - "
        "NOT the +0x518 bitmask; relayed puppet buttons never trigger interacts.",
    ),
    0x140C805A0: (
        "Player_BuildInteractDispatchState",
        "Builds 0x15C-byte interact command/pose block from screenIndex + player "
        "transform; snapshots g_PlayerHandleForHudPublish[screen] and per-screen "
        "+0x590 command active flags from the player's joypad index (+0x39B8).",
    ),
    0x140EC75D0: (
        "PlayerReviveCharacter_UpdateAndDispatch",
        "Player::ReviveCharacter vtable +0x20: revive + weapon-pickup commands, then "
        "BuildInteractDispatchState -> Player_InteractDispatcher for owner screen.",
    ),
    0x140CD3C00: (
        "PlayerBoatDriving_UpdateAndDispatch",
        "Player::BoatDriving vtable +0x20: script/interact/boat-stand paths; ends in "
        "BuildInteractDispatchState -> InteractDispatcher (camera route hardcoded 0).",
    ),
    0x140CD4750: (
        "PlayerBoatDrivingController_Create",
        "Allocates the 40-byte Player::BoatDriving action controller and stores "
        "its owning Player at controller+0x10.",
    ),
    0x140EC8160: (
        "PlayerReviveCharacterController_Create",
        "Allocates the 384-byte Player::ReviveCharacter action controller, "
        "initializes move interfaces, and stores owner at controller+0x10.",
    ),
    0x140C88220: (
        "Player_InteractDispatchFromBuiltState",
        "Thin wrapper: validate g_PlayerHandleForHudPublish[screen] then call "
        "Player_InteractDispatcher on the per-screen dispatcher blob.",
    ),
    0x140C837F0: (
        "InteractDispatcher_RebuildNearbyList",
        "Clears dispatcher+0 count, gathers nearby interactables (sub_141C94340 "
        "Player-class pass + sphere-cast path), appends up to 48 x 32-byte entries "
        "at dispatcher+8 via InteractDispatcher_AppendNearbyEntry.",
    ),
    0x140C87FB0: (
        "InteractDispatcher_TickNearbyList",
        "Per-frame nearby-list maintainer: prune stale entries, rebuild when "
        "dispatcher+0x3570 flag set, refresh per-entry Havok probes. Called from "
        "InteractDispatcher_TickAllScreenBlobs on each per-screen blob.",
    ),
    0x140C817F0: (
        "InteractDispatcher_AppendNearbyEntry",
        "Writes one 32-byte nearby entry {handle,id}@+0, interact index@+16, "
        "probe-hit byte@+24; links input object at dispatcher+0x610+240*index. "
        "Skips KickProbes when InputObject joypad != active local joypad.",
    ),
    0x140C858F0: (
        "InteractDispatcher_PruneNearbyList",
        "Compacts dispatcher nearby list: drops entries whose object fails the "
        "dword_146920B54 visibility/active bit test.",
    ),
    0x140C828B0: (
        "InteractDispatcher_UpdateNearbyEntryProbe",
        "Per-entry sphere-cast probe refresh inside Player_InteractDispatcher; "
        "gated by InputObject_MatchesActiveLocalJoypad on the linked input slot.",
    ),
    0x140C82310: (
        "NearbyObject_KickProbes",
        "Builds CheckNearbyGameObjectsParams and kicks Havok sphere casts "
        "(nearby-object-searcher.cpp NearbyObject::KickProbes). Gated by "
        "InputObject_MatchesActiveLocalJoypad.",
    ),
    0x1401611F0: (
        "InteractDispatcher_TickAllScreenBlobs",
        "Global frame tick: InteractDispatcher_TickNearbyList on qword_143EC4EE0 "
        "(screen 0) and, in net co-op, qword_143EC84A0 (screen 1).",
    ),
    0x141C94340: (
        "InteractDispatcher_GatherNearbyPlayerObjects",
        "Spatial gather of Player-class interactables near the query object; "
        "returns count and fills an output pointer array (max 48).",
    ),
    0x14042D120: (
        "NpcActive_AnimPhaseOwnerUpdate",
        "Active@Npc vtable+0x20 (RTTI vtable 0x142934888). Runs the NPC "
        "anim-phase owner path; this is not a Player::Active callback.",
    ),
    0x1404223F0: (
        "Player_TickDecisionNodeFromControlHost",
        "Syncs player transform from control-host at player+0x1FB0; when host "
        "entity+0x6E bit0x4 set, first-latch (*(player+0x1808))->vtable+0(player) "
        "(player+0x1D52 bit0x4 latch).",
    ),
    0x140CD3760: (
        "Player_DecisionNodePublishGate",
        "Gate before PublishRuntimeState: validates player+0x1FC8 vtable+168, "
        "calls vtable+96 on that object, player vtable+408, then "
        "PublishRuntimeStateFromDecisionNodeLikeObject.",
    ),
    0x140C7FF40: (
        "Player_ResolveWeaponPickupInteractAnim",
        "Resolves weapon-pickup-swap / weapon-pickup-when-no-weapon anim ids and "
        "the interact command id (out a6) from the pickup art def.",
    ),
    0x14183A3D0: (
        "InputScreenBlock_IsCommandActive",
        "Scans the dynamic command-id list at block+0x590 (entries {ptr,count}); "
        "per-screen block = g_LocalPlayerInputManager + 3504 + 3056*screen.",
    ),
    0x1410A3EA0: (
        "PlayerWeaponController_StartHolsterTransition",
        "Called by ApplyWeaponSwitch (vt+0x48) slot==-1 branch on the current "
        "weapon. Removes channel-1 weapon actions, enqueues an exact-handle "
        "PlayerPutGunAwayAction and changes controller draw state to 5.",
    ),
    0x141089D20: (
        "PlayerGrabWeaponAnimAction_Construct",
        "Constructs the RTTI-backed PlayerGrabWeaponAnimAction and registers "
        "the authored weapon-grab action name.",
    ),
    0x14108A2B0: (
        "PlayerDrawWeaponAnimAction_Construct",
        "Constructs the RTTI-backed PlayerDrawWeaponAnimAction and registers "
        "the authored weapon-draw action name.",
    ),
    0x14108A400: (
        "PlayerDrawWeaponAnimAction_CommitCurrentWeapon",
        "PlayerDrawWeaponAnimAction vtable +0x48. Resolves the current weapon, "
        "copies its exact weak {cell,generation} handle from +0xA8/+0xBC, then "
        "calls PlayerWeaponController vtable +0x248 with that 16-byte handle.",
    ),
    0x1410AAF20: (
        "PlayerWeaponController_CommitDrawnWeaponHandle",
        "PlayerWeaponController vtable +0x248. Matches the supplied 16-byte "
        "weapon weak handle against the 23-entry table at controller+0x148, "
        "stores the selected index at +0x124, calls EquipWeaponIndex, then "
        "commits the same index to controller+0x120.",
    ),
    0x1410A9DD0: (
        "PlayerWeaponController_EquipWeaponIndex",
        "Concrete selected-slot presentation path reached only by the Draw "
        "commit. Resolves the indexed ProcessWeapon, binds the active hand "
        "attachment, resets its local attachment blend, emits player-grabbed, "
        "and finishes through PrepareSelectedWeaponBindingBlend.",
    ),
    0x1410A9580: (
        "PlayerWeaponController_PrepareSelectedWeaponBindingBlend",
        "Resolves the controller's selected weapon and authored holster binding; "
        "when the live selected attachment id matches that binding, copies the "
        "blend endpoints to ProcessWeapon+0x600/+0x620 and sets its duration.",
    ),
    0x14109DA70: (
        "PlayerWeaponController_AreHolsteredWeaponsHidden",
        "Controller vtable +0x3A0 getter. Returns the boolean stored at "
        "controller+0x44D, which selects the long-gun hidden-when-holstered "
        "knowledge variant.",
    ),
    0x14109DC40: (
        "PlayerWeaponController_RelocateOwnerPointers",
        "Controller vtable +0x10 relocation callback. Rebases owner Player at "
        "+0x108 and PlayerMoveSequencer at +0x110 when either pointer lies in "
        "the engine relocation range.",
    ),
    0x141DE8EC0: (
        "NavCharacter_ApplyAnimationRootTransform",
        "NavCharacter/AiCharacter vtable +0x400 post-animation path. Npc override "
        "0x14042FB40 reaches it. FALSE virtual +0x5E8 selects alternate net-animation "
        "branch with direct local-root stores 141DE9608/+0x130 and 141DE9610/+0x140; "
        "TRUE selects ordinary native/controller path through 141E12F90/141DEC4B0. "
        "Not a proven network transform setter. Exact Pirate +0x208 is nullsub_385; "
        "+0x1E8 is parent-transform change processing. Player +0x400 is 1410E9210.",
    ),
    0x140434820: (
        "Npc_EvaluateNativeControlBranchGate",
        "STATIC: RTTI-confirmed Pirate vtable +0x5E8. False for global143E02054, "
        "matching native record byte+0xBC, or nonnull NPC+0xF140 whose virtual+0x18 "
        "returns false; true otherwise. 141DE8EC0 uses true for ordinary native/"
        "controller branch, false for alternate net-animation path. Not a safe "
        "runtime switch; call to140A97450 is not read-only.",
    ),
    0x141DEB500: (
        "NavCharacter_ApplyUprightRootUnlessBaseNodeFlag400",
        "STATIC:141DEDA20 passes owner+1A0 AnimControl, not parent pointer. Finds "
        "active base; nested node+108 bit400 suppresses cleanup. Otherwise builds "
        "bound from owner+100 via141DEAF00 and applies141E7CDA0. Assumes base exists; "
        "not an external setter. Bit400 authored symbolic meaning unresolved.",
    ),
    0x141DEAF00: (
        "NavCharacter_BuildUprightBoundTransform",
        "STATIC: copies0x50-byte BoundTransform; owner+18B0 mode0 projects world "
        "quaternion to normalize(0,qY,0,qW), mode1 projects in owner parent frame, "
        "other modes preserve input. Independent1404C1D60 repeats projection. "
        "Not physics publication or translation synchronization.",
    ),
    0x141E7CDA0: (
        "ObjectTransform_ApplyBoundTransformAndNotify208",
        "STATIC: compatible resolved frames via141685930 copy0x50 bytes to "
        "owner+100 then virtual+208. Mismatch converts world pose, calls+1F0, "
        "refreshes parent cache and derives local+130/+140. Pirate+208 is empty; "
        "+1F0 resolves141E7D550. Not standalone physics synchronization.",
    ),
    0x141E7D550: (
        "ObjectTransform_ResolveBoundParentAndDispatch1E8",
        "STATIC exact Pirate+1F0. Resolves incoming BoundTransform key/generation "
        "+8/+C through14386FD00 with entry+50 check; forwards pointer or null to "
        "owner+1E8 (Pirate141DE4000). Parent rebinding, not physics refresh.",
    ),
    0x141DE9910: (
        "NavCharacter_PostAnimBlendControllersAndRigidBody",
        "STATIC exact Pirate+408. Walks eleven AnimModifierSet slots through "
        "owner+15D0 via+30 and optional owner+15C8 collection via+48; tail-calls141E9C220 "
        "at141DE99BC. Retained postblend dispatch, not a pure transform setter.",
    ),
    0x141BBDF70: (
        "NdGameObject_PostJointUpdateAsync",
        "STATIC exact NdGameObject::PostJointUpdate_Async source "
        "nd-game-object.cpp:3247. At141BBE107 calls owner virtual+2B8 "
        "(Pirate141BB8320 derived world refresh), then optional primary "
        "CompositeBody+238 through141E5AEE0 at141BBE119. Later NavCharacter "
        "modifier+38 callbacks remain; this is not a final root-writer boundary.",
    ),
    0x141BBDEE0: (
        "NdGameObject_UpdateSplashersJob",
        "STATIC descriptive name. Receives raw owner, calls source-identified "
        "SplasherSkeletonInfo::Update141D4E9D0 on owner+1F8 at141BBDEF0 and "
        "optional141D53EC0 on owner+200 at141BBDF06. Queued by141BBDF70 at "
        "141BBE06C after owner+210 counter store141BBE054. Exact Pirate+418 "
        "141BBDF20 waits141BBDF41, releases141BBDF49 and clears141BBDF4E. "
        "This is not a body transform queue or a retention lease.",
    ),
    0x1404073A0: (
        "GameCharacter_CreateAiAnimModifiers",
        "STATIC descriptive name; exact Pirate virtual+B80. Allocates modifier "
        "data at Fg+288 and populates set indices0 Eye,1 Leg,2 WeaponGrip,4 Stride "
        "and10 Climb. The other six entries remain clear on this path. Called "
        "during NavCharacter initialization through virtual+B80 at141DE2C29; "
        "not eleven locomotion-controller instances.",
    ),
    0x1403CFB80: (
        "NavCharacter_GetAnimModifierSet",
        "STATIC descriptive name; exact Pirate virtual+3C0. Returns the "
        "AnimModifierSet pointer stored at owner+15D0 (load1403CFB87), "
        "not an embedded controller array or a retention operation.",
    ),
    0x14192BD00: (
        "AnimModifierSet_ConstructElevenSlots",
        "STATIC descriptive name. Clears first0x58 bytes (eleven pointers), "
        "allocates shared0x460-byte storage at set+58 and zeros joint tree+60 "
        "and count+E8. NavCharacter initialization constructs/stores this set "
        "at141DE2C0C/141DE2C12 before creating concrete modifiers.",
    ),
    0x14192BF60: (
        "AnimModifierSet_InitializeAndBuildJointTree",
        "STATIC descriptive name. Walks eleven slots, invokes modifier+10 "
        "initialization and+70 joint-ID collection, deduplicates at most32 "
        "joints and builds the joint tree at set+60. Successful initialization "
        "publishes Fg+2C0 and dispatches modifier+18 finish callbacks. Called "
        "from NavCharacter initialization at141DE2C39.",
    ),
    0x140624400: (
        "LegModifier_PostJointUpdateFootState",
        "STATIC descriptive name; shared AiLegModifier/LegModifier/"
        "NetCharacterLegModifier virtual+38. Direct outputs are foot state, "
        "modifier+4C0/+4D0 and parent cache. This does not establish global "
        "exclusion of owner-root writers in other callbacks or descendants.",
    ),
    0x140624D60: (
        "LegModifier_GetEmbeddedFootState",
        "STATIC descriptive name; leg-modifier virtual+80. Returns the "
        "embedded state address modifier+50 at140624D64, not a separate owner "
        "or a retained pointer.",
    ),
    0x141936100: (
        "AnimModifier_IsEnabled",
        "STATIC descriptive name; modifier virtual+60 gate. At14193610A "
        "returns byte[modifier+8 data pointer +14] &1. This data-backed enable "
        "bit is not proof that the whole owner or physics body is ready.",
    ),
    0x141E53690: (
        "CompositeBody_Ctor",
        "STATIC constructs0x180-byte CompositeBody; shared allocation paths "
        "NdGameObject_InitCompositeBody141BC03F0 (+238) and "
        "Character_InitializeRagdoll141E9D490 (+580). Distinct owned fields "
        "can belong to the same Character.",
    ),
    0x141BC03F0: (
        "NdGameObject_InitCompositeBody",
        "STATIC exact source identity. Allocates0x180, calls141E53690, stores "
        "owner+238 at141BC0459 and initializes via141E54FE0. Exact Pirate "
        "vtable+450 resolves here; NPC initialization calls at14042A88F. "
        "Primary CompositeBody, not optional Character+698 single body.",
    ),
    0x141E54FE0: (
        "CompositeBody_Init",
        "STATIC exact CompositeBody::Init composite-body.cpp:685. Stores "
        "owner atComposite+8; bodies+48 stride110, bindings+58 stride50. "
        "Used by both primary owner+238 and optional ragdoll+580. "
        "Native initialization stores count to+10 AND disabled count+24 at "
        "141E55A50/5A54; a nonempty secondary can still be dormant mode0.",
    ),
    0x141E56800: (
        "CompositeBody_ShutdownBodiesAndResetCounts",
        "STATIC descriptive shutdown helper. Calls141CEFB80 for bodies at "
        "+48, stride110, at141E5684D; resets body count+10 at141E56904 and "
        "qword+60 (binding count) at141E56914. Composite and array pointers "
        "can survive this in-place reset; pointer equality is not readiness "
        "or lifetime proof. Caller controls owner-field clearing and freeing.",
    ),
    0x141E9D490: (
        "Character_InitializeRagdoll",
        "STATIC exact Character::InitializeRagdoll, m_pRagdoll assertion "
        "character.cpp:961. Allocates0x180 at141E9D515; constructs141E53690, "
        "stores owner+580 at141E9D531, initializes141E54FE0 at141E9D641. "
        "Error path calls141E56800 at141E9D659, then caller clears owner+580 "
        "at141E9D65E. NPC caller gated by+1CA9 bit10. "
        "This is ragdoll, NOT the primary CompositeBody at+238.",
    ),
    0x141E59160: (
        "CompositeBody_SyncPhysicsToGame",
        "STATIC exact source composite-body.cpp:2062/2098. PHYSICS->GAME "
        "import in +408 for primary+238 and ragdoll+580. Gated by world, "
        "bindings+60, lock+160 and body statuses/flags. Normalizes dirty "
        "bindings through141E53590 at141E59255 before the count test. At "
        "141E59371..388 skips transform import only if !(flags0&2), max body "
        "status<=1 AND !(flags0&80h); mode2 alone does not exclude import. "
        "First binding key=-1 "
        "directly writes owner+130/+140 at141E5998E/996 then+208. Alternate "
        "signed+E8>=0 with an actual matching binding uses1416860A0(owner+100) "
        "at141E59C36, also root writes, then+208 (empty for exact Pirate). "
        "Motion transitions rebuild mappings through141E57DA0/141E57950/"
        "141CF5C40; a previously empty list is not an immutable property. "
        "Can overwrite a pre+408 network root; not forward collision publishing.",
    ),
    0x141E57DA0: (
        "CompositeBody_OnApplyMotionType",
        "STATIC exact CompositeBody::OnApplyMotionType source "
        "composite-body.cpp:1498/1513/1524. Tracks desired mode3 physics-driven "
        "count+1C and mode2 game-driven count+18, then calls141E57950 to "
        "rebuild this body's joint/root mappings via141CF5C40. Desired modes "
        "are not the nativebody+160 enum. Binding eligibility can change "
        "without replacing the owning Composite pointer. Mode0 without "
        "body A0&400h is counted in Composite+24 at141E57F1A.",
    ),
    0x141CF5C40: (
        "RigidBody_ExportJointMapping",
        "STATIC exact RigidBody::ExportJointMapping source rigid-body.cpp:"
        "3759/3832/3842. Exports mappings according to requested motion mode, "
        "body flags, linkage+C6 and joint+C4. Source1 can emit root key=-1 "
        "through141E533E0 at141CF61BB; source2 emits joint mappings, with "
        "separate special source4 handling. Called by141E57950 during native "
        "mapping updates; this is not a read-only eligibility query. "
        "Mode0 with body A0&400h clear takes no-export141CF5DB4/5DC7.",
    ),
    0x1401BABB0: (
        "CompositeBody_ShutdownAndFree",
        "STATIC descriptive name. Character destructor calls this for "
        "optional owner+580 at141E993EF. Calls141E56800 shutdown at1401BABCC, "
        "frees optional Composite helpers including+118/+120 and penetration "
        "storage, then frees the0x180-byte Composite at1401BAC45. Pointer "
        "stability in external reads is not a lifetime lease.",
    ),
    0x141E5AEE0: (
        "CompositeBody_SyncGameDrivenBodies",
        "STATIC +410 callers141BBE119(primary+238),141E9C4CD(ragdoll+580). "
        "Requires Havok world. Byte+2 bit20 resets body history for missing/"
        "unworlded native bodies. Calls141E5A980 only if+120 null OR binding "
        "count+60 zero; do not assume unconditional transform publication. "
        "Primary call141BBE119 returns141BBE11E with ONE input RCX=composite; "
        "Havok world is loaded inside, not supplied as an established RDX arg.",
    ),
    0x141E5A980: (
        "CompositeBody_UpdateGameDrivenBodyTransforms",
        "STATIC when DWORD+18+DWORD+24 nonzero OR byte+2 bit10, loops+10 "
        "bodies at+48 stride110 and calls141CF57D0 at141E5A9E7. Clears "
        "request bit10. Native game-driven path, distinct from+408 import.",
    ),
    0x141CF57D0: (
        "RigidBody_UpdateGameDrivenTransform",
        "STATIC nativebody+40 required; native/desired motion gates. Under "
        "A0 bit0 clear and float+9C<=0 calls141CF0EE0(body,0) at141CF586D. "
        "Decompiler omits substantial asm tail: use disassembly for gates. "
        "Called byCompositeBody141E5A980; not arbitrary-thread permission. "
        "Unworlded mode0 bodies cannot enter the desired3-only velocity tail "
        "141CF58C2/58C7; dormant secondary is distinct from active mode2.",
    ),
    0x141CF0A00: (
        "RigidBody_GetOwnerOrJointWorldTransform",
        "STATIC obtains world pose from body owner/joint binding for141CF0EE0. "
        "Source byte+C6==1 at141CF0A3A uses owner body+68 and owner root "
        "+110/+120/+130/+140. Sources other than1/4 read signed16+C4 at "
        "141CF0BA8: >=0 uses owner+1A8 joint cache and141687C90 at141CF0D33. "
        "Source4 follows deeper nativebody+E8 chain; source4 and negative "
        "joint eligibility remain unresolved, not an owner-root fallback. "
        "Separate from explicit input transform setter141CF36B0.",
    ),
    0x141CF4EE0: (
        "RigidBody_SyncPhysicsPoseAndGetStatus",
        "STATIC descriptive name. CompositeBody141E59160 calls at141E592FA "
        "and takes max status0/1/2. Checks body+40, A0 mode/flags and native "
        "world+10; mode6000 can import swept pose through141CF3790 at "
        "141CF5459. Other branches refresh game transforms at141CF4F2F and "
        "modify flags. Not a read-only getter or status inferable from mode alone. "
        "Nonnull native body with native+10=null exits status0 at141CF4F51.",
    ),
    0x141E7E210: (
        "RagdollRuntime_ConstructDormant",
        "STATIC secondary Composite runtime constructor. Stores owner "
        "Composite at runtime+0 at141E7E248, byte runtime+68=0 at141E7E27B, "
        "allocates penetrator and stores runtime+58 at141E7EC7F. Byte+68=0 "
        "makes Character_UpdateBuoyancy exit at141EA1375. This identifies "
        "a dormant-state gate, not a general lifetime lease.",
    ),
    0x1420F03D0: (
        "RagdollPenetrator_InitializeInactive",
        "STATIC stores owning Composite at penetrator+0 at1420F0400; "
        "WORD[penetrator+3D]=0 at1420F0414 leaves active byte+3E zero. "
        "Character+410 penetration call141E9C4E4 is skipped when +3E=0. "
        "Reached through secondary runtime+58; no physics-write authority.",
    ),
    0x141CF0EE0: (
        "RigidBody_RefreshGameTransformAndQueue",
        "STATIC obtains141CF0A00 owner/joint world, converts141CF05A0, stores "
        "141CF3790 and marks146A5C380 at141CF0FEA under146A5C780 for nonzero "
        "+54 index. DOES NOT set explicit-transform A0 bit0. Desired mode4000 "
        "also uses pose-change thresholds. Not equivalent to141CF36B0 teleport. "
        "STATIC nested calls: getter141CF0F17, converter141CF0F29, store "
        "141CF0F36 returns141CF0F3B. Store receives the converted32-byte pose, "
        "not a raw joint Locator. Prologue pushRBX/subRSP,A0 makes original "
        "refresh return-slot equal nested-store entry return-slot+0xB0. "
        "Value141CF5872 there identifies normal UpdateGameDrivenTransform "
        "caller. Store return proves neither dirty queue insertion nor Havok "
        "commit; subsequent mode/threshold/index gates still apply.",
    ),
    0x141F808E0: (
        "RigidBody_ApplyKeyframedTransform",
        "STATIC ordinary mode2 FrameSyncEnd call141F81486 with explicit A0 "
        "bit0 clear. Calls141F80430 then141F80100 chain for swept pose and "
        "velocities. Distinct from explicit-transform141F80AC0 branch.",
    ),
    0x141F80430: (
        "Havok_RebaseSweptPoseAndSetKeyframe",
        "STATIC updates nativebody swept+1B0/+1C0/+1D0/+1E0 and basis via "
        "142229D50; position+1A0 store141F80680. Calls141F80100 to derive "
        "keyframe velocities; not the explicit teleport141F80770 path.",
    ),
    0x141F80100: (
        "Havok_SetKeyframeTargetAndVelocities",
        "STATIC derives desired linear/angular velocities;14237BC50/70 write "
        "native motion+E0/+F0 (=body+230/+240). Downstream of141F80430/"
        "141F808E0 normal keyframed publication; do not force-call externally.",
    ),
    0x14237BC50: (
        "HavokMotion_SetLinearVelocity",
        "STATIC stores native motion+E0, body+230 when motion=body+150. "
        "Called from keyframe target/velocity path141F80100.",
    ),
    0x14237BC70: (
        "HavokMotion_SetAngularVelocity",
        "STATIC stores native motion+F0, body+240 when motion=body+150. "
        "Called from keyframe target/velocity path141F80100.",
    ),
    0x141E98C70: (
        "Character_Construct",
        "STATIC calls NdGameObject_Construct at141E98C83 and stamps Character "
        "vtable at141E98CA2. Null-initializes optional +580 at141E98D64 and "
        "+698 at141E98D88. Shared base of NavCharacter141DE1600 and "
        "BasePlayer140CBFB30; initialization alone does not make physics ready.",
    ),
    0x141E991F0: (
        "Character_Destroy",
        "STATIC stamps Character vtable at141E9921C; reached from NavCharacter "
        "teardown141DE20F0. Optional body+698 loaded141E993C0 is shut down via "
        "141CEFB80 at141E993CF and freed as0x110 bytes at141E993DD. Separate "
        "+580 cleanup follows141E993EF. Never retain body across owner destruction.",
    ),
    0x141E9FF90: (
        "Character_RelocateOwnedPointers",
        "STATIC range relocation(delta,begin,end): optional+580 relocates via "
        "141E5AF60; optional+698 calls141CEFA00 at141EA0126 and rebases pointer "
        "at141EA0145 if in range. This is relocation, NOT body creation. "
        "Owner/body raw address is not a lifetime identity.",
    ),
    0x141CEFB80: (
        "RigidBody_ShutdownAndReset",
        "STATIC destroys underlying Havok resources, clears registered work bit "
        "at141CEFCBC, resets body+40 at141CEFD48 and frees nonzero handle through "
        "141CECD70 at141CEFD65 before resetting fields141CEE3C0. Character "
        "destructor calls this for optional+698 then frees0x110 storage. "
        "Not a safe foreign-thread callable or a proven NPC creation counterpart.",
    ),
    0x141CECD70: (
        "RigidBodyHandle_FreeHandle",
        "STATIC exact source RigidBodyHandle::FreeHandle rigid-body.cpp:228/229. "
        "Under146A59B50 lock clears table14386FD00[body+54] at141CECE60, "
        "allocation bitmap146A58F50 then body+54/index and+50/id at141CECE8A/8E. "
        "Distinct from process record/id and physics workbitmap146A5C380.",
    ),
    0x141D22240: (
        "EntitySpawner_RequestKill",
        "STATIC source EntitySpawner::RequestKill; resolves OWN spawner+30 "
        "record/+38 id, record+8 object and object+BC at141D22253..27E. "
        "Different from parent spawner+60. Mutates flags and queues kill work "
        "through141EDB150, not an observer.",
    ),
    0x140402670: (
        "Npc_PostInit",
        "STATIC exact Pirate14294B9D0 slot+60, called by SpawnProcess1417C40A0 "
        "after successful+58 Init. Adds CharacterManager at140402904 BEFORE "
        "outer EntitySpawner_Spawn publishes own actor pair141D22889. "
        "PostInit is too early to validate spawner+30/+38 reciprocity.",
    ),
    0x141EDA990: (
        "EntitySpawnerGroup_Update",
        "STATIC holds writer lock146AA1B20 from141EDAA05 to141EDAF42; calls "
        "EntitySpawner_Spawn at141EDAD75. This local lock is not a demonstrated "
        "resource-retention barrier: package logout141CC323A and group teardown "
        "141EDA8CA reset actors on separate paths without this local lock.",
    ),
    0x141EDAFF0: (
        "EntitySpawnerGroup_ResetActors",
        "STATIC resolves OWN spawner+30 record/+38 id, record+8 actor/+BC; "
        "shares reset/kill helper141D22150 with package logout141CC323A "
        "and group teardown141EDA8CA. "
        "Do not confuse current actor with parent-spawner+60 resolver; reset "
        "is a lifecycle mutator, not an observer or retention operation.",
    ),
    0x141E9C220: (
        "Character_PostAnimBlendPublishRigidBody",
        "STATIC common postblend reached by Pirate141DE9910 and Player1410EBAEE. "
        "After141BBDE60, owner+698 receives override via virtual+388/+398 or "
        "composed parent/local transform through141CF36B0 at141E9C442. Resets "
        "body history bit40 on mode switch. Finally calls +580 ragdoll "
        "physics-to-game import141E59160 at141E9C465, gated only by nonnull "
        "+580 and Havok world146917C20, not health/death.141E9C46A is the "
        "first post-import instruction; only epilogue remains to141E9C47E. "
        "This closes that invocation's import only, not global writers or "
        "lifetime, and is not permission to edit the root. "
        "Queueing is not final physics commit or permission for a foreign caller. "
        "+698 is OPTIONAL: ctor141E98D88 zeros it, no campaign producer proven. "
        "Do not confuse +580 ragdoll physics-to-game import with this queue.",
    ),
    0x141CF36B0: (
        "RigidBody_SetTransformAndMarkDirty",
        "STATIC:141CF05A0 converts input,141CF3790 stores current/history; "
        "ORs+A0 with1000001, marks optional+70 dirty. Nonzero ushort+54 sets "
        "146A5C380 bit under146A5C780 lock. Same layout identified independently "
        "by RigidBody::InitFixed. Queued request, not immediate Havok publication.",
    ),
    0x141CF05A0: (
        "RigidBody_ConvertInputTransform",
        "STATIC:32-byte transform conversion. Type byte+C6==4 uses underlying "
        "body/shape transform chain; otherwise composes input with body+D0 offset. "
        "Used before stored history/current writes; not a raw world memcpy.",
    ),
    0x141CF3790: (
        "RigidBody_StoreTransformWithHistory",
        "STATIC: flag+A0 bit80 suppresses history advance once; bit40 chooses "
        "copy-old versus seed-new history at+20/+30. Writes current xyz/+10 "
        "quaternion preserving position W. InitFixed and141CF36B0 share this "
        "layout; this helper does not commit the Havok body. STATIC ABI "
        "RCX=body, RDX=converted32-byte input, returns int EAX flags. Exact "
        "normal refresh call141CF0F36 is E8 55 28 00 00, return141CF0F3B. "
        "Preserve the full result, not bool. Current xyz/quaternion writes "
        "are unconditional here; history gates do not establish queueing.",
    ),
    0x141CEE6A0: (
        "RigidBody_InitFixed",
        "STATIC exact rigid-body.cpp:995/996 RigidBody::InitFixed assertions; "
        "requires pHkBody and hkpMotion::MOTION_FIXED. Uses141CF3790 storage. "
        "Type-provenance evidence only, not living-NPC setup guidance.",
    ),
    0x141CF7B70: (
        "RigidBody_FrameSyncEndMarkedBodies",
        "STATIC scans128qwords146A5C380, resolves indexed bodies through14386FD00, "
        "calls141F80F80 at141CF7CAD, then141CF6BB0. Called by141D800C0 at141D8049A. "
        "Work bits also retain moving bodies, not just pending transform edits. "
        "Global physics stage, not a safe network-tick entry point.",
    ),
    0x141F80F80: (
        "RigidBody_FrameSyncEnd",
        "STATIC exact rigid-body-fse.cpp:472 source assertion. Consumes transform/"
        "motion flags; ordinary dirty-pose path141F80AC0 ->141F80770. Other "
        "motion branches use142379EE0. Dirty bit clear141F8163D/41 is conditional; "
        "body type/velocity/motion branches must remain native.",
    ),
    0x141CF6BB0: (
        "RigidBody_BatchApplyChangedMotion",
        "STATIC exact rigid-body.cpp:4155 source assertion. Drains changed-motion "
        "list after dirty FrameSyncEnd pass under native world locking. "
        "Not an enemy-only operation or an arbitrary-thread update API.",
    ),
    0x141F80AC0: (
        "RigidBody_FrameSyncEndPublishTransformToHavok",
        "STATIC ordinary FrameSyncEnd dirty-transform path141F81496. Converts "
        "stored body[0]/[10] using frame146917B40, calls141F80770 at141F80CFE "
        "on underlying body+40. Distinct from motion-transition142379EE0 paths.",
    ),
    0x141F80770: (
        "HavokSetPositionRotationAndSync",
        "STATIC exact rigid-body-fse.cpp:252/253 source assertions check "
        "nextPosition.isOk3()/nextOrientation.isOk(). Calls141F7FFC0(nativebody), "
        "then14237BC00(nativebody+150,pos,quat) ->142234120(...,nativebody+170). "
        "Preserve native physics-stage ownership; not a network helper.",
    ),
    0x14237BC00: (
        "HavokMotion_SetPositionAndRotation",
        "STATIC forwards(position,rotation,motion+20) to142234120.141F80770 "
        "passes nativebody+150, so downstream motion-state starts at body+170.",
    ),
    0x142234120: (
        "HavokMotionState_StorePositionRotationAndSweptState",
        "STATIC writes position at state+30, orientations+60/+70, basis via "
        "142229D50, derived centers+40/+50 and zero+90. Caller14237BC00 maps "
        "state to nativebody+170. Not a complete direct enemy transform API.",
    ),
    0x141D800C0: (
        "HavokStep",
        "STATIC exact ndphys/havok.cpp:962/991. Waits/releases146B49FA8 at "
        "141D80132/13A. Gated dt146917DC4 > epsilon142BE8B18, InputManager+80 "
        "nonzero and world146917C20: calls141CF7B70 at141D8049A between141E5EA60/"
        "141E5F1B0, before141D7EB90 at141D80588. Counter is destruction probes, "
        "NOT animation. Animation/physics dependency still needs proof.",
    ),
    0x141B416A0: (
        "Havok_KickDestructionProbes",
        "STATIC exact KickDestructionProbes source, havok-base.cpp:1730. Creates "
        "counter146B49FA8 at141B417EF with callback141B41000; submits141B41801. "
        "Not an animation-job producer; duplicate sequence in141D7F580.",
    ),
    0x141B41870: (
        "Havok_WaitForDestructionProbes",
        "STATIC waits/releases/zeros146B49FA8 at141B4188A/92/97. Same counter "
        "awaited inline by HavokStep. Does not establish animation completion.",
    ),
    0x141D7EB90: (
        "HavokStepInternal",
        "STATIC exact ndphys/havok.cpp:363 assertion g_havok.m_dt > HK_REAL_EPSILON. "
        "Called by HavokStep after marked-body FrameSyncEnd processing.",
    ),
    0x140165B50: (
        "GameFramePostRenderUpdate",
        "STATIC source-identified frame stage. Builds callback140164F10 at1401666AF "
        "and submits via1415B5140 at1401666FC (common-game-loop.cpp:5058). Callback "
        "calls HavokStep141D800C0 at1401653A7. Outer1401676D0 waits normal "
        "process/animation buckets before submitting this stage at1401678DE; "
        "not the Player network tick or a global mutation gate.",
    ),
    0x1403D1460: (
        "GameCharacter_PostAlignControllerUpdates",
        "STATIC exact Pirate+DE0. Owner+15C8 collection receives each+50 at1403D14A0; "
        "optional+1CA0 GestureController receives+68/141E69BD0 at1403D14CC. "
        "Locomotion+50=14051FAE0; Weapon+50 is empty1401786C0. Not one final "
        "physics setter; locomotion/FSM descendants remain separately relevant.",
    ),
    0x141A2BB30: (
        "String_CopyWithMiddleEllipsis",
        "STATIC: long names retain prefix/suffix around literal142B256C4 ellipsis, "
        "last byte NUL.1417C3E1D supplies capacity32 into object+8:14+3+14 bytes. "
        "Not full authored identity; larger hashes cannot restore omitted bytes.",
    ),
    0x141841C10: (
        "Process_GetShortDebugName",
        "STATIC exact Pirate+28. Returns object+8,32-byte abbreviated debug "
        "label filled at1417C3E1D. Does not recover full spawn name.",
    ),
    0x1417C3660: (
        "SpawnInfo_GetExplicitOrSpawnerName",
        "STATIC SpawnInfo1428D5D60 slot0: explicit+30 string else+58 spawner->+20 "
        "name else caller fallback. Full-name source before debug abbreviation.",
    ),
    0x14029AEE0: (
        "GameInfo_NoSpawnerSpawnInfoOverride",
        "STATIC descriptive name; exact GameInfo vtable1429040B8 slot+E0 "
        "at142904198 returns zero. GameConfig calls constructor140297AE0 at "
        "140150646 and publishes it to143FE1070 at1401506D0. Thus this exact "
        "callback preserves EntitySpawner_Spawn's stack PSI selection; optional "
        "callback141D227A7 can still modify it. Other implementations must not "
        "inherit this conclusion without validation.",
    ),
    0x1401D5C80: (
        "Object_GetEntitySpawner",
        "STATIC exact Pirate AND NadineBuddy+168 returns object+150 (mov/ret); "
        "NadineBuddy slot1429497A0 verified. Initialized from SpawnInfo+58 "
        "at141E7C9F2 only if nonnull spawner+51 bit08 clear. Not retained or "
        "guaranteed present; pointer is not cross-copy identity.",
    ),
    0x141D22970: (
        "EntitySpawner_GetDesiredProcessId",
        "STATIC exact source assertion. Uses spawner+28 with type substitutions/"
        "indexed suffix when+51 bit10. NdGameObject_Init chooses explicit "
        "SpawnInfo+64/name hash or this id. No global/world uniqueness proof.",
    ),
    0x140BD7A50: (
        "SendNetEventNpcSpawn",
        "STATIC exact source assertion. NPC+D8 ->event+18 at140BD7B5C; "
        "spawner+28 ->event+1C at140BD7C29. Upstream140A96B00 can replace+D8 "
        "with generated network id. Not an admitted campaign replication API.",
    ),
    0x1413C9020: (
        "GameRestartCheckpoint",
        "STATIC exact source assertion. Queries restart-checkpoint1413C91A3, "
        "publishes checkpoint-restarted1413C937D, stores task143898668 at1413C938E "
        "then queues graph1413C93CA. Request only, not load-complete/all-load epoch.",
    ),
    0x141DEC4B0: (
        "NavCharacter_UpdateRootLocator",
        "STATIC exact nav-character.cpp:3478 source assertion at141DEC64C names "
        "NavCharacter::UpdateRootLocator. Reached by141DE8EC0 at141DE9133/141DE96F6/"
        "141DE979F. Tail copies local root via141E7D0D0, upright cleanup141DEB500, "
        "then copies local rotation via141E7D1D0. Not a generic packet setter; "
        "physics is queued later through Pirate+408/owner+698. Cross-phase "
        "dependency and safe remote ownership remain open.",
    ),
    0x141E7C690: (
        "NdLocatableObject_ConstructIdentityParentAndLocalRoot",
        "STATIC base constructor called by NdGameObject at141BB335F. "
        "QWORD+104=000000007FCCCCCC at141E7C6B4 leaves parent key+108=0; "
        "DWORD+10C=0 at141E7C6BF. Cached parent+110 and local+130 translations "
        "are zero; quaternion+120/+140 use1428805D0=(0,0,0,1). Key/generation "
        "zero alone does NOT prove parentless later: phase40 resolves inline "
        "table14386FD00 using DWORD+108 and may retain a prior parent cache.",
    ),
    0x141E7D0D0: (
        "ObjectTransform_CopyLocalRootAndNotify208",
        "STATIC copies supplied32bytes to object+0x130/+0x140, then tail-calls "
        "virtual+0x208. Called by NavCharacter_UpdateRootLocator at141DEDA11 and "
        "BuildInteractHookRuntimeStateFromLiveObject. Exact Pirate+208 is empty; "
        "this helper alone does not publish Pirate physics. ABI: RCX=object, "
        "RDX=pointer to contiguous32byte position/quaternion; no meaningful "
        "success return. Preserve receiver position service lane+13C when "
        "substituting only xyz. Exact Pirate+208=1401D4E10 (RET0).",
    ),
    0x141E7D1D0: (
        "ObjectTransform_CopyLocalRotationAndNotify208",
        "STATIC copies supplied16bytes to object+0x140, then tail-calls "
        "virtual+0x208. NavCharacter_UpdateRootLocator calls it at141DEDA46. "
        "Exact Pirate+208 is empty; not an independently safe network writer.",
    ),
    0x140A97450: (
        "NetProcess_FindRecordByOwnerAndTouchResolvedProcesses",
        "STATIC: gated by HIBYTE(g_NetModeFlags); searches native registry "
        "143E01AA8/143E01B28/143E01B38, validates weak pointer/id against object+0xBC. "
        "Marks EACH valid traversed object+0x72=1 before owner comparison; returns "
        "matching entry+0x18 or null. Not a read-only lookup or observation helper.",
    ),
    0x141DE4000: (
        "NavCharacter_ProcessParentTransformChange",
        "STATIC: exact Pirate vtable+0x1E8. Receives authored transform pointer, "
        "validates/reselects parent, compares current binding via1401D5320, computes "
        "old/new parent-relative delta, calls141E7D1F0 and owner+0xBD8. Not a generic "
        "physics refresh; broader side effects remain unresolved.",
    ),
    0x1403D13E0: (
        "GameCharacter_PostAnimApplyRootTransform",
        "GameCharacter vtable +0x400 wrapper. Rejects a below-world transform, "
        "updates the optional +0x1CA0 owner, then tail-calls the NavCharacter "
        "animation-root transform path.",
    ),
    0x14042FB40: (
        "Npc_PostAnimApplyRootTransform",
        "Npc, NadineBuddy and RTTI-confirmed Pirate vtable +0x400 override. Runs the shared NavCharacter "
        "root-transform apply and additional NPC post-animation state; this is not "
        "the Player post-animation method.",
    ),
    0x1410E9210: (
        "Player_PostAnimUpdate",
        "Player vtable +0x400 post-animation method. RTTI and the embedded "
        "Player::PostAnimUpdate_Async source assertion distinguish it from the "
        "Npc/NavCharacter root-application path.",
    ),
    0x1410EB6A0: (
        "Player_PostAnimBlending_Async",
        "Player vtable +0x408. Exact Player::PostAnimBlending_Async source "
        "assertion. Runs CharacterLegIkController and other post-blend IK "
        "controllers after the animation manager's phase-0x10 dispatch.",
    ),
    0x140CC7F80: (
        "PlayerBearClimbIK_Construct",
        "Constructs PlayerBearClimbIK, installs vtable 0x142A0CC60 and "
        "initializes its four traversal-only JointSets.",
    ),
    0x140CC81F0: (
        "PlayerBearClimbIK_Update",
        "Exact player-bear-climb-ik.cpp PlayerBearClimbIK::Update. It performs "
        "joint work only while PlayerBearClimbIK_IsActive reports a nonzero "
        "blend; it is not the ordinary steady weapon-hand IK owner.",
    ),
    0x140CC8360: (
        "PlayerBearClimbIK_SetBlendTarget",
        "Sets the PlayerBearClimbIK transition target consumed by its active "
        "blend/update path.",
    ),
    0x140CC8440: (
        "PlayerBearClimbIK_StopBlend",
        "Stops or fades the PlayerBearClimbIK traversal blend.",
    ),
    0x140CC8520: (
        "PlayerBearClimbIK_IsActive",
        "Concrete PlayerBearClimbIK vtable +0x20 predicate. Returns whether the "
        "controller's traversal blend scalar at +0x300 remains positive.",
    ),
    0x14100B370: (
        "PlayerStrafeController_UpdateStrafeIk",
        "Exact player-strafe.cpp:1030 source assertion names "
        "PlayerStrafeController::UpdateStrafeIk. Reads the quaternion at "
        "incoming Locator+0x10, retains positive-Z forward.y for asin and the "
        "positive-X right axis for joint rotation. Its separate Vector argument "
        "constructs StrafeDirBlender/ApStrafeDirBlender horizontal state. It "
        "does not consume the Locator position or import Locator forward x/z as "
        "the separate horizontal strafe input. "
        "resolves the headb and spined chains and commits their post-blend joint "
        "corrections. Called only by the concrete PlayerStrafeController vtable "
        "+0x60 method 0x14100C400.",
    ),
    0x141007880: (
        "PlayerStrafeController_Initialize",
        "Concrete PlayerStrafeController vtable 0x142A3FA88 slot +0x08. The "
        "Player assembly pass calls it with rdx=owning Player and r8=Player+0x3BD0; "
        "the prologue stores those values at controller+0x108/+0x110 before "
        "initializing the head/spine and strafe-IK joint state.",
    ),
    0x14100C400: (
        "PlayerStrafeController_UpdatePostAnimBlend",
        "Concrete PlayerStrafeController vtable 0x142A3FA88 slot +0x60. "
        "Reached from Player_PostAnimBlending_Async through Player+0x1D50; "
        "ABI is void(controller, const Locator*). It validates the current "
        "strafe action and passes the supplied Locator to "
        "PlayerStrafeController_UpdateStrafeIk. This is the final common native "
        "boundary before the headb/spined correction, for ADS and non-ADS alike.",
    ),
    0x14100AC40: (
        "PlayerStrafeController_SetStrafeDirection",
        "Concrete PlayerStrafeController vtable 0x142A3FA88 slot +0x80. Exact "
        "16-byte setter for controller+0x300. Its sole steady Player+0x1D50 "
        "indirect call is 0x140FF9D7B from PlayerStrafeToEntryAction, which "
        "supplies an XZ-only normalized direction; this field cannot carry "
        "vertical camera/head pitch.",
    ),
    0x140FF9920: (
        "PlayerStrafeToEntryAction_InterpolateHorizontalYaw",
        "Consumes two direction vectors, interpolates atan2-derived yaw through "
        "the action's angle smoother at +0x64, and rebuilds exactly "
        "(sin(yaw), 0, cos(yaw), 0). This is the producer proving the downstream "
        "PlayerStrafeController+0x300 vector is horizontal-only.",
    ),
    0x140FF9AF0: (
        "PlayerStrafeToEntryAction_UpdateHorizontalDirection",
        "Normalizes the action+0x80/+0x90 XZ directions, calls "
        "PlayerStrafeToEntryAction_InterpolateHorizontalYaw, then dispatches "
        "PlayerStrafeController vtable +0x80 with action+0x90. Called by the "
        "PlayerStrafeToEntryAction update paths.",
    ),
    0x140C5B970: (
        "PlayerStrafeToEntryAction_UpdateGoalDirection",
        "PlayerStrafeToEntryAction vtable +0x28. Resolves the Player+0x3FE0 goal "
        "transform, chooses goal-forward or normalized Player-to-goal XZ "
        "direction, smooths it into action+0x80/+0x90 and reaches the exact "
        "PlayerStrafeController+0x300 setter through 0x140FF9AF0.",
    ),
    0x1410F9050: (
        "Player_BuildStrafeIkReferenceLocator",
        "Builds the 32-byte Locator immediately consumed by "
        "PlayerStrafeController vtable +0x60. Seeds position from "
        "ResolveScreenRoutePoseSeedOrDefault(Player+0x4010); ordinary non-ADS "
        "keeps that receiver-screen quaternion. It then selects optional "
        "GunMoveGrapple/StrafeMelee/GunMove/GrenadeMove/ReviveCharacter. "
        "The ordinary GunMove branch builds the routed reticle point from HUD "
        "fields (+0x30+0x38, +0x34+0x3C), converts it to a world direction "
        "through CameraScreenRoute_UnprojectPointDirection, "
        "builds an orthonormal forward/up basis, converts it to a quaternion, "
        "and stores that quaternion at output+0x10.",
    ),
    0x140F68D80: (
        "CameraScreenRoute_UnprojectPointDirection",
        "Resolves CameraManager_GetBlendedCameraTransform for the supplied "
        "screen index and unprojects the supplied two-float screen point through "
        "CameraState_UnprojectScreenPoint. Returns only the normalized world "
        "direction. The ordinary GunMove strafe-IK locator calls it at "
        "0x1410F986C.",
    ),
    0x140F68E30: (
        "CameraScreenRoute_UnprojectReticleDirectionWithExtraOffset",
        "Builds a per-screen point from HUD route fields (+0x30+0x38+0x40, "
        "+0x34+0x3C+0x44), scales it by the current viewport and calls "
        "CameraScreenRoute_UnprojectPointDirection. Used by PlayerAimController "
        "target-point and projectile paths.",
    ),
    0x140F68F10: (
        "CameraScreenRoute_UnprojectReticleDirection",
        "Builds a per-screen point from HUD route fields (+0x30+0x38, "
        "+0x34+0x3C), scales it by the current viewport and calls "
        "CameraScreenRoute_UnprojectPointDirection. The ordinary GunMove branch "
        "of Player_BuildStrafeIkReferenceLocator inlines this exact calculation.",
    ),
    0x1416954F0: (
        "CameraState_UnprojectScreenPoint",
        "Unprojects a screen pixel using camera-state basis rows +0x00/+0x10/"
        "+0x20 and projection scales +0x80/+0x94, writes camera origin from "
        "+0x30, and writes a normalized world direction. At viewport center "
        "the direction is the +0x20 basis row. This consumes the camera-state "
        "derived prefix, not only its raw transform at +0x350.",
    ),
    0x141A92260: (
        "AnimManager_PerformAnimBlend",
        "Exact PerformAnimBlend source assertion in anim-mgr-jobs.cpp. Builds "
        "the current 48-byte-per-joint matrix palette and swaps FgAnimData "
        "+0x230 into previous buffer +0x238. Its normal command path reads the "
        "persistent local JointParams pointer at FgAnimData+0x100 at "
        "0x141A92547 and passes it to BuildCommandSegmentsForObjectMask; this "
        "buffer is separate from the current/previous skinning pair. Its "
        "destination capacity is group0.parameter_count at "
        "0x141A9254E..0x141A9256A (C39 66), not the full skeleton count. It reads "
        "and evaluates the normal AnimControl command DAG before the later Player "
        "post-blend phase. The final Async job's mode-2 command 14 subsequently "
        "imports this same +0x100 pose into its group WorkBuffer. It reads "
        "optional scalar-bank pointers at FgAnimData+0x118/+0x120; a special "
        "multi-group mode locally suppresses the +0x120 value without modifying "
        "the field. The inline Process loop is gated by job+0x14, Fg bytes "
        "+5/+7, job+0, Fg+0x298 and the cached-skeleton prepass. At "
        "0x141A92411 Fg+7 bit3 clear with Fg flags 0x20000 clear forces one "
        "inline group. Independently, 0x141A9238C..0x141A923BE derives the "
        "residual split "
        "from g_AnimManagerResidualGroupSplitFlags byte2, Fg+0x2D1, the "
        "underlying skeleton +0x1E word, group count, Fg+7 bit3 and flags "
        "0x20000; 0x141A92424 then limits the inline loop to one and "
        "0x141A927D9 queues the residual mask. Fallback allocates and publishes "
        "identity matrices. STATIC observation seam141A92759: RCX=[R12] "
        "command words, EDX=[R12+0xC] wordCount, R8=stack context, R9D=group; "
        "args5..7=[RDI+0x18/+0x20/+0x28], where RDI is native job reloaded "
        "at141A926E4 and R12 is the0x20-byte segment. Context+0x10=&Fg+0x1C0 "
        "allows Fg recovery and job+8 cross-check. Job/segment registers are "
        "NOT extra C++ ABI arguments. An adaptor must forward registers if used; "
        "the narrower STATIC alternative is an owned outer return-slot R: "
        "outer reserves0x28, Perform pushes0x28 and reserves0x190, and LEA "
        "141A92745 is bodySP+0x140; therefore context+0xA8==R identifies the "
        "exact wrapped invocation without RDI/R12 operands. Segment pointers "
        "are not a published post-Perform log.",
    ),
    0x1418C4FB0: (
        "FgAnimData_AllocateAndSwapSkinningBuffers",
        "Allocates two 48-byte-per-joint buffers and swaps current/previous CPU "
        "buffers at +0x230/+0x238 and upload buffers at +0x240/+0x248. The first "
        "allocation uses registered context 82, whose startup allocator is named "
        "ALLOCATION_DOUBLE_FRAME_CPU_ACCESS_ONLY; the second uses context 72, "
        "registered as ALLOCATION_DOUBLE_FRAME_GPU_UPLOAD.",
    ),
    0x1418C50C0: (
        "FgAnimData_PrepareSkinningMtxUploadBuffer",
        "Exact FgAnimData::PrepareSkinningMtxUploadBuffer assertion in anim-data.cpp. "
        "Copies the CPU skinning matrices at +0x230 to upload matrices at +0x240.",
    ),
    0x14160B3B0: (
        "RenderMemory_InitializeDoubleFrameAllocators",
        "Initializes and registers the paired double-frame CPU_ACCESS_ONLY and "
        "GPU_UPLOAD TaggedHeapAllocators. Registration writes context id 72 for "
        "GPU_UPLOAD and id 82 for CPU_ACCESS_ONLY, exactly matching the two "
        "FgAnimData skinning-buffer allocations at 0x1418C4FB0.",
    ),
    0x141A928F0: (
        "AnimManager_PerformBlendAndDispatchPostBlendJob",
        "Synchronous animation worker callback: calls PerformAnimBlend at exact "
        "callsite 0x141A92905, then resolves the retained Fg owner and dispatches "
        "Player vtable+0x408 through exact bytes FF 90 08 04 00 00 at "
        "0x141A929A1 in the same enclosing job call. The final Async joint-blend job follows "
        "separately, making these two callsites the exact Perform-to-postblend "
        "handoff for the Mazur authored-pair preservation scope. STATIC value "
        "bridge: RSI retains native job from141A92902, RBX=Fg, and RCX=RDI="
        "retained owner at141A929A1. Do not infer OS-thread/TLS continuity "
        "through Perform or treat job pointer/flags2000 as a generation id. "
        "An observation ticket must close on missing Process/owner callback "
        "and all return paths; copying native commands is not pose repair. "
        "STATIC owned-frame proof: ordinary call bridge publishes native entry "
        "return-slot R. Process context+0xA8==R; at141A929A1 a stack-neutral "
        "JMP relay into a noinline wrapper gives wrapperReturnSlot+0x30==R. "
        "Use fiber-local scope, not OS-thread TLS. The entire native function "
        "returns at141A929F5 or restores its stack then tail-jumps14015A850 "
        "at141A929EC; outer finally covers both and native unwind. Arm rows "
        "around+0x408 delimit ALL procedural postblend, not Feather alone. "
        "STATIC Pirate candidate: +408 call141A929A1 returns141A929A7 before "
        "conditional phase40 call141A929C1. Candidate ordering is after the "
        "FULL exact Pirate+408, not an interior common-Character return; "
        "requires job+14==0, current Fg.flags&40h and validated current owner. "
        "This order alone is not pose-cache freshness or write admission. "
        "Native job+20 is already initialized before Perform: async allocation "
        "141781E76..EAD uses stride38 and copies1466D0C58 at141781EE3/EEA; "
        "sync copies it at141783173/17A. It remains the same job through "
        "Perform/JointBlend, not an uninitialized future-only callback field.",
    ),
    0x1415A9040: (
        "TaggedHeapAllocator_Init",
        "Exact TaggedHeapAllocator::Init source assertion. Stores the allocator "
        "name and memory type used by the double-frame CPU_ACCESS_ONLY and "
        "GPU_UPLOAD registrations.",
    ),
    0x141930990: (
        "FgInstance_BuildPerFrameRenderRecord",
        "Copies the current FgInstance state into the frame-modulo-three render "
        "record, including instance+0x78 at record+0x58, instance+0x80 at "
        "record+0x60 and instance+0x88 at record+0x68. It stores the frame id at "
        "record+0x00 and publishes the record pointer to instance[frame % 3] at "
        "0x141930E3A. The later deformer latch uses record+0x58 as current and "
        "record+0x68 as companion.",
    ),
    0x141931810: (
        "FgInstance_PublishPerFrameStateToMeshInstance",
        "Selects instance[requestedFrame % 3] at 0x14193186C and publishes "
        "record+0x58 to MeshInstance InstanceJointData+0x18, record+0x68 to "
        "+0x28 and record+0x60 to +0x90. The later 0x141967C3C latch copies "
        "+0x18/+0x28 to the active deformer pointers +0x00/+0x08; +0x90 becomes "
        "separate +0x10 state and is not the companion-buffer pointer.",
    ),
    0x141967A70: (
        "FgInstance_PublishAndBuildDeformerCommands",
        "Publishes the requested frame record to MeshInstance InstanceJointData, "
        "builds the deformer command chain, then reselects instance[frame % 3] "
        "and asserts record+0x00 equals that same frame at 0x141967BC0..BC7. It "
        "then latches jointData +0x18->+0x00, +0x28->+0x08 and +0x90->+0x10 at "
        "0x141967C3C..0x141967C4E. SubMeshSkinning therefore sees the selected "
        "current buffer at +0x00 and the independently retained companion at "
        "+0x08; its joint count comes from the submesh descriptor, not +0x10.",
    ),
    0x141883580: (
        "MeshInstance_BuildDeformerCommandChain",
        "Walks the MeshInstance submeshes and constructs their deformer command "
        "chain using the InstanceJointData pointer at MeshInstance+0x118. "
        "Optional SubMeshBlend at14188373B precedes Skinning at1418837CF; "
        "optional RecomputeNormals/ComputeWind follow. Unmorphed bind vertices "
        "therefore do not unconditionally equal live skinning inputs. "
        "ComputeWind gate141883916..92E requires MeshInstance mask20 AND "
        "SubMesh+80. Initializer141882082..099 derives mask20 from that pointer; "
        "normal FgInstance_Init a14=false excludes its construction.",
    ),
    0x1417483F0: (
        "WindDeformer_Create",
        "Exact WindDeformer::Create assertion. Retains parameter pointer+50, "
        "resolves position/normal streams0/2 and optional color stream. "
        "Called at14187AD2E with override+1F0 or14187AE5B with type23 descriptor; "
        "both require MeshInstance+104 mask400 at14187AC9F.",
    ),
    0x141748650: (
        "WindDeformer_Deform",
        "Exact WindDeformer::Deform assertions. Post-skin deformer invoked "
        "by ComputeWind_Execute; distinct from material vertex shader/cloth. "
        "Normal FgInstance initialization excludes its command admission, "
        "not arbitrary later position writers.",
    ),
    0x141875F90: (
        "ComputeWind_Execute",
        "ComputeWind vtable142B08B40 slot0; COL142CDDE98/type143770DC8. "
        "Dispatches141748650 WindDeformer_Deform. Native command builder "
        "requires has-wind mask20 and nonnull SubMesh+80.",
    ),
    0x141875F40: (
        "SubMeshBlend_Execute",
        "RTTI SubMeshBlend vtable142B08B20 slot0. Dispatches141747CF0 with "
        "deformer+10, rendercontext, retainedblend instance+8.",
    ),
    0x141747CF0: (
        "BlendShapeDeformer_Blend",
        "Exact BlendShapeDeformer::Blend source assertion. Reads inputindex "
        "array at272B batch+60 and weights atblendinstance+8; called by "
        "SubMeshBlend_Execute before ordinary skinning.",
    ),
    0x141748060: (
        "BlendShapeInstance_InitializeInputs",
        "Caller14187F6A2 only. Collection+40 count/+38 stringpairs initialize "
        "zero weights atinstance+8, IDs+10 andanimparam mapping+18.",
    ),
    0x141747C20: (
        "BlendShapeDeformer_ResolveInputIds",
        "Caller141882105. Matches272B shapebatch StringIds+A0 against "
        "blendinstance IDs+10 and storesresolved inputindices atbatch+60.",
    ),
    0x14187F1B0: (
        "MeshInstance_InitializePrototypeState",
        "Collection prototype+40 blendinputcount gates allocation of "
        "MeshInstance+10 at14187F646. Zero branch14187F6A9 storesnull. "
        "Pinned normal Nadine arms collection190130 hascount0/null+38. "
        "Wind:14187F400 clears +104 mask400,14187F418 clears +150 override. "
        "False a10 from FgInstance skips +148 descriptor/type23 scan atF42D..463.",
    ),
    0x141878860: (
        "MeshResource_InitializeFromDescriptor",
        "Copies native mesh+74 shape count toresource+68 at141878F32; "
        "resource+70 initializednull atF40, allocated onlyifpositivecount. "
        "Shape pointers native+78 readatF90. File header is8 bytesearlier; "
        "Nadine arms181AE8+7C/+80 arezero/no fixup, native desc181AF0. "
        "Index upload:1418789AB reads native+28 count,9BD source+48; "
        "9D4/A2E/A86 allocate/upload2*count; A9E stride2,AA6 format57 "
        "DXGI_FORMAT_R16_UINT. Arms file181AE8+30=36582,+50->5C2FA4, "
        "73164B/12194triangles, indices0..6447.",
    ),
    0x14187A3B0: (
        "SubMeshInstance_InitializeDeformerResources",
        "At14187A636 creates BlendShapeDeformer onlyifresource+70 nonnull "
        "AND resource+68 shapecount positive; storesSubMesh+40 at14187A67E. "
        "Independent ofcollection blendinput gate. Normal Nadine arms "
        "authored count/table areabsent, not merelyzero liveweights. "
        "Wind:14187AC8D clears SubMesh+80; AC9F requires MeshInstance+104 "
        "mask400 before either override+1F0 AD2E or type23 AE5B creation call.",
    ),
    0x141879C90: (
        "SubMeshInstance_Construct",
        "Initializes submesh deformation members; blendshape+40 isnull "
        "at141879CBE beforeoptional resource-driven setup14187A3B0.",
    ),
    0x141876A60: (
        "SkinningModel_InitializeMeshResourcesAndShaders",
        "STATIC: initializes mesh skin resource streams; first use resolves "
        "five Cs_Skinning families mode0..7 and Cs_RopeSkinning-mode0 "
        "into146904C60, formatting %s-mode%d at141876BA3. Mesh descriptor+68 "
        "is sparse data (file header+70); sparse+8==0 at1418773B7 sets packed "
        "flag8, uploads 8-byte vertex count/offset rows at141877464 and "
        "4-byte packed versus8-byte unpacked records at1418774CB..4FD. "
        "Pinned mode1 DXIL proves joint=word>>22, weight=f32((word&3FFFFF)* "
        "f32bits34800002), no sum normalization; this CPU loader does not "
        "perform skin arithmetic. Callers1418775E0/141878860/141879DB0.",
    ),
    0x141882FB0: (
        "SubMeshSkinning_Construct",
        "Exact SubMeshSkinning source type. Installs vtable 0x142B08B10 and "
        "retains the supplied InstanceJointData pointer at +0x10. "
        "1418830AE..0D9 selects family and mode: bit0 packed4B, bit1 "
        "14386FC79 debug pass-through (not dual-quaternion), bit2 joint "
        "cache threshold optimization. Command byte0 indexes146904C60 "
        "at141967ED2..EDD. Threshold14300B138 is capped310 by141B96D2C.",
    ),
    0x141875F50: (
        "SubMeshSkinning_Execute",
        "SubMeshSkinning execution slot. Passes its retained InstanceJointData "
        "to SkinningDeformer_SkinSubMesh.",
    ),
    0x1418778F0: (
        "SkinningDeformer_SkinSubMesh",
        "Exact SkinningDeformer::Skin assertions for one submesh. Validates "
        "InstanceJointData and its latched +0x00 current bone buffer, then calls "
        "SkinningDeformer_EncodeBoneBufferDescriptors with the +0x00/+0x08 "
        "buffer pointers plus the submesh descriptor's joint count. "
        "GPU dispatch consumes selected Cs_Skinning*-modeN; CPU buffer "
        "binding alone does not prove vertex/weight arithmetic. Pinned mode1 "
        "DXIL independently establishes weighted dot4 matrix LBS, not DQ.",
    ),
    0x141744100: (
        "SkinningDeformer_EncodeBoneBufferDescriptors",
        "Converts the latched InstanceJointData +0x00/+0x08 bone-buffer pointers "
        "plus the separately supplied submesh joint count into GPU resource/"
        "descriptor inputs consumed by SkinningDeformer_Skin.",
    ),
    0x141744210: (
        "SkinningDeformer_Skin",
        "Exact SkinningDeformer::Skin source assertions. Validates pJointData "
        "and m_pCurrBoneBuf, dereferences the current and companion bone-buffer "
        "pointers, and passes them to SkinningDeformer_EncodeBoneBufferDescriptors.",
    ),
    0x1417445D0: (
        "SkinningDeformer_SkinRope",
        "Exact SkinningDeformer::SkinRope source assertion. Separate rope/sling "
        "skinning path; not the retained Nadine body-palette consumer.",
    ),
    0x14181A9B0: (
        "FgDrawManager_PrepareInstanceForRendering",
        "Exact PrepareInstanceForRendering assertions in fg-draw-mgr.cpp. Copies "
        "FgAnimData CPU matrices +0x230 to upload storage +0x240 at memcpy "
        "callsite 0x14181AB33 while holding Fg+0x220, then selects +0x250 when "
        "nonnull or +0x240 otherwise. A driven FgInstance with +0x260==0 binds "
        "that selected pointer at +0x78 (0x14181B3C6). With +0x260!=0 it resolves "
        "mesh bone StringIds, rebuilds a mesh-local 48-byte-row palette directly "
        "from Fg+0x230 at 0x14181B2C4/0x14181B367, and publishes that buffer at "
        "instance+0x78/+0x80 at 0x14181B3A1..B3A5. It separately publishes the "
        "render world matrix into each driven instance at +0x140..+0x17F. Exact "
        "GetRenderWorldMatrix calls 0x14181AD57/0x14181ADFE feed temporal data; "
        "0x14181B3F4 returns the final matrix copied to each driven instance. "
        "STATIC P99 source-boundary limit: post-cmd16 g0 values do not prove "
        "this frame's selected palette, Fg/mesh owner or +260 remap route. "
        "Historical Fg+250==0 is not an admission for a future frame.",
    ),
    0x14181B890: (
        "FgDrawManager_PrepareInstancesJob",
        "FgDrawManager worker that processes FgAnimData entries in batches of eight "
        "through FgDrawManager_PrepareInstanceForRendering.",
    ),
    0x14181BA10: (
        "FgDrawManager_PostAnimProcessing",
        "Exact FgDrawMgr::PostAnimProcessing source assertion in fg-draw-mgr.cpp. "
        "Builds and submits six worker records which batch FgAnimData entries "
        "through FgDrawManager_PrepareInstancesJob.",
    ),
    0x141931B30: (
        "FgInstance_SetBones",
        "Exact FgInstance::SetBones source assertion. Writes its two supplied "
        "bone-data values to the current frame/persistent FgInstance record at "
        "+0x60/+0x58 and writes the joint count at +0x10. It does not itself "
        "populate final InstanceJointData +0x00/+0x08.",
    ),
    0x141661960: (
        "RenderMgr_MainDrawCore",
        "Exact RenderMgr::MainDrawCore source assertion in render-mgr.cpp. Reads "
        "the current Fg render-frame queue through context+0x10960 and passes "
        "its FgInstance pointer array, FgLodData array and count to "
        "CullFgLods_Spawn at 0x141661A57.",
    ),
    0x141665B40: (
        "RenderMgr_MainDraw",
        "Per-frame RenderMgr main draw reached by both top-level render modes. "
        "Prepares every registered screen context but calls "
        "RenderMgr_MainDrawCore exactly once with the frame record whose first "
        "inline SceneWindow begins at +0x10.",
    ),
    0x141A2C440: (
        "CullFgLods_Spawn",
        "Exact CullFgLods::Spawn source assertion in mesh-cull-fg-inl.h. Accepts "
        "FgInstance**, FgLodData*, count, Scene and ViewFrustum and schedules the "
        "foreground LOD-culling work consumed by RenderMgr_MainDrawCore.",
    ),
    0x141A2C880: (
        "CullFgLods_ExecuteBatch",
        "Asynchronous CullFgLods worker. Indexes the FgInstance pointer array at "
        "8-byte stride and its paired FgLodData array at exact 0xB0-byte stride; "
        "the two arrays and count were retained by CullFgLods_Spawn.",
    ),
    0x141A35A30: (
        "MeshCull_SpawnCullFgScene",
        "Exact MeshCull::SpawnCullFgScene source assertion in mesh-cull.cpp. "
        "Allocates the 592-byte job record containing FgScene at +0x08, Scene "
        "at +0x10, SceneWindow at +0x20, CullFgLods at +0x28 and "
        "SharedCullState at +0x30, then schedules MeshCull_CullFgSceneJob.",
    ),
    0x141A32700: (
        "MeshCull_CullFgSceneJob",
        "Exact CullFgSceneJob source assertion in mesh-cull.cpp. Copies every "
        "FgInstance+0x1D0 cull record for the concrete SceneWindow, schedules "
        "CullFgView workers, then publishes 61 foreground PageTable pointers "
        "containing 32-byte DrawablePrototype records at job +0x68..+0x248.",
    ),
    0x141A2DDD0: (
        "MeshCull_CullFgViewJob",
        "Worker reached through thunk 0x141A2E780 from the exact "
        "SpawnJobsOnVisibleMeshes<CullFgView> source path. Resolves each "
        "FgScene index to FgInstance*, applies view-type flags and cull "
        "metadata, then emits 9-byte visible-submesh rows whose uint16 at +7 "
        "is the unchanged FgScene index.",
    ),
    0x141A30330: (
        "MeshCull_BuildFgViewDescriptors",
        "Builds the ordered 128-byte CullFgView descriptor array from the "
        "concrete SceneWindow config. Seeds fixed view types 3, 6 and 7, then "
        "appends configured types 1, 0, 5, 2, 4, 8, 9 and 10.",
    ),
    0x141A35BE0: (
        "MeshCull_WaitForCullFgScene",
        "RenderMgr::MainDrawCore post-SceneWindow boundary: waits for the "
        "CullFgSceneJob counter and passes its completed 61 draw lists to "
        "MeshCull_FinalizeCullFgScene. This is the narrow safe boundary for a "
        "read-only local-view geometry provenance census.",
    ),
    0x141A3B1F0: (
        "MeshCull_FinalizeCullFgScene",
        "Copies the completed CullFgSceneJob draw-list pointers into the "
        "496-byte render result and derives aggregate 0x8000/0x10000 flags "
        "by traversing 32-byte DrawablePrototype rows (page+32*slot) and their "
        "flags at +0x1C; it does not consume the inner rows at 9-byte stride.",
    ),
    0x141A30B80: (
        "MeshCull_SelectFgViewDrawLists",
        "Selects one completed PageTable pointer from each 128-byte CullFgView "
        "descriptor at descriptor+0x70 and writes the ordered set consumed by "
        "CullFgSceneJob and MeshCull_FinalizeCullFgScene.",
    ),
    0x14186C8A0: (
        "SceneRendering_Render",
        "Exact SceneRendering::Render source assertions. Traverses foreground "
        "PageTables at 32-byte stride and submits each DrawablePrototype "
        "record to SceneRendering_EmitDrawablePrototypeSubmeshes.",
    ),
    0x141AB5780: (
        "SceneRendering_EmitDrawablePrototypeSubmeshes",
        "Consumes one 32-byte DrawablePrototype: instance pointer table at +8, "
        "contiguous 9-byte submesh rows at +0x10, and count flags at +0x1C. "
        "For each row it reads uint16 scene index at +7 and resolves the exact "
        "FgInstance through the pointer table before emitting render commands.",
    ),
    0x1416525C0: (
        "RenderDepthOnlyPass",
        "Exact RenderDepthOnlyPass source assertion in render-mgr.cpp. Calls "
        "SceneRendering_Render with view type 3 and the fixed foreground draw "
        "list at output index 0.",
    ),
    0x141652A40: (
        "RenderDepthOnlyHalf2Pass",
        "Exact RenderDepthOnlyHalf2Pass source assertion in render-mgr.cpp. "
        "Calls SceneRendering_Render with view type 3 and foreground output "
        "list 0.",
    ),
    0x1416532A0: (
        "RenderProjectionDepthPass",
        "Exact RenderProjectionDepthPass source assertion in render-mgr.cpp. "
        "Calls SceneRendering_Render with view type 3 and foreground output "
        "list 0.",
    ),
    0x141653680: (
        "RenderGBuffer",
        "RenderMgr_MainDrawCore registers this callback under the exact task "
        "names RenderGBuffer/RenderGBufferUpdate. Its call at 0x141653B2E "
        "passes foreground output list 0 and view type 3 to "
        "SceneRendering_Render, proving that list 0 feeds the primary colour "
        "GBuffer as well as depth passes.",
    ),
    0x14165BDC0: (
        "RenderForwardPass",
        "RenderMgr_MainDrawCore registers this callback under the exact task "
        "names RenderForwardPass/RenderForwardPassUpdate. Calls "
        "SceneRendering_Render at 0x14165BF9D/0x14165C08A with foreground "
        "output list 0 and view type 3, independently proving a colour consumer "
        "of the same selected PageTable.",
    ),
    0x141657DA0: (
        "RenderSunlightShadow",
        "Exact RenderSunlightShadow source assertion in render-mgr.cpp. Calls "
        "SceneRendering_Render with view type 1 and the configured foreground "
        "draw-list group beginning at output index 3.",
    ),
    0x14197D8C0: (
        "AnimFramePlayback_PublishRetainedFgAnim",
        "Resolves a retained owner through +0x108/+0x110, obtains its FgAnimData "
        "through owner+0x1A0 -> +0x20, copies an authored animation frame into "
        "+0x230/+0x238 and +0x1C0, then synchronously calls "
        "FgDrawManager_PrepareInstanceForRendering at 0x14197DC54 with mode 1.",
    ),
    0x1403014F0: (
        "FgAnimData_GetRenderWorldMatrix",
        "Returns the published 4x4 matrix at FgAnimData+0x1C0, optionally multiplied "
        "on the right by the current auxiliary matrix at +0x2E0 when flag 0x40000 "
        "is set. Its three PrepareInstance callsites are 0x14181AD57, "
        "0x14181ADFE and final per-driven-instance 0x14181B3F4.",
    ),
    0x140301310: (
        "Matrix4x4_Multiply",
        "Multiplies two 4x4 matrices; used by FgAnimData_GetRenderWorldMatrix for "
        "the optional auxiliary render transform.",
    ),
    0x1418C54D0: (
        "FgAnimData_PublishWorldMatrix",
        "Publishes a 64-byte world matrix to FgAnimData+0x1C0 and updates the "
        "current transform snapshot when requested by the caller. STATIC "
        "unconditional row stores to+1C0/+1D0/+1E0/+1F0 at1418C54DD/4EA/"
        "4F7/5504. With r8b=0 this skips the copied JointCache/current update; "
        "the normal later JointBlend reset must establish that copy. Matrix "
        "publication alone does not prove fresh world Locators for all joints.",
    ),
    0x1418C55D0: (
        "FgAnimData_PublishWorldTransform",
        "Converts a translation-then-quaternion input block to a matrix, applies "
        "FgAnimData scale, and calls FgAnimData_PublishWorldMatrix. ABI is "
        "rcx=FgAnimData, rdx={translation vec4, quaternion vec4}, "
        "r8b=publish-current flag. The stock animation-manager phase-0x40 "
        "callsite is 0x141A8FC00; runtime confirms this publication owns the "
        "visible retained-player body yaw.",
    ),
    0x1417288C0: (
        "AnimControl_EnableAsyncUpdate",
        "Exact AnimControl::EnableAsyncUpdate source assertion. Validates the "
        "AnimControl+0x20 FgAnimData pointer and ORs FgAnimData flags with "
        "0x10. This only admits the full retained-owner phase-0x10 update; it "
        "does not itself create or schedule an animation job.",
    ),
    0x141728930: (
        "AnimControl_DisableAsyncUpdate",
        "Exact AnimControl::DisableAsyncUpdate source assertion. Validates "
        "AnimControl+0x20 and clears FgAnimData flag mask 0x38, not only the "
        "phase-0x10 bit; therefore it is not a scoped inverse of "
        "AnimControl_EnableAsyncUpdate.",
    ),
    0x1417289A0: (
        "AnimControl_IsAsyncUpdateEnabled",
        "Exact AnimControl::IsAsyncUpdateEnabled source assertion. Returns "
        "whether the FgAnimData reached through AnimControl+0x20 has flag "
        "0x10 set.",
    ),
    0x141756E30: (
        "DrawControl_RecomputeMeshInstanceVisibility",
        "Recomputes a DrawControl mesh row's final visibility and writes it "
        "to every driven FgInstance+0x1A0. DrawControl+0x18 bit 2 selects "
        "cinematic versus gameplay presentation: cinematic suppresses row "
        "+0x248 bit 0, gameplay suppresses bit 1.",
    ),
    0x141BBBB20: (
        "DrawControl_SetCinematicPresentationMode",
        "Toggles DrawControl+0x18 bit 2 together with FgAnimData flag 0x8000 "
        "and immediately recomputes mesh visibility. SsAnimate/cinematic "
        "lifecycle callers prove this is a gameplay/cinematic selector, not "
        "a first-person/third-person selector.",
    ),
    0x141AB97C0: (
        "ProcessAnimCmdListSegment",
        "Exact ProcessAnimCmdListSegment source assertion in anim-commands.cpp:822. "
        "Per-layer animation-command evaluator reached from PerformAnimBlend and the "
        "normal joint-blend producers; consumes AnimCmdContext and drives palette "
        "output. STATIC seven inputs: command words, count, context, group, "
        "phase callback, postpose callback, validation callback; uint32 result. "
        "JointBlend forwards callbacks from job+18/+20/+28. Cmd24's "
        "RCX is the distinct88-byte WorkBuffer allocated at141AB98D3 and "
        "retained in R13, NOT Segment+58 matrix workspace. Segment+00 "
        "stores WorkBuffer at141AB9A53; Segment+08 stores AnimCmdContext "
        "at141AB9B39; callback RCX/RDX/R8 are set at141ABAC63/5F/5C. "
        "Context+00 supplies Art then Art+40 raw at141AB9852/55. Raw+4 "
        "skeleton ID is consumed at141ABA50A, distinct from Art+24 database. "
        "Matching export context/range does not witness cmd16 execution. Cmd24's "
        "Segment pointer plus0x1628 equals the original native Process entry "
        "return-slot (not a wrapper frame). Independent bridge publication "
        "of that slot is required; deriving it from Segment is tautological. "
        "The evaluator also drives palette "
        "output including aim-add-* parameter lookup. Its stack SegmentContext keeps "
        "the joint-program skeleton at +0x28 (loaded by `RAX=[AnimCmdContext+0x00]; "
        "R15=[RAX+0x40]` at 0x141AB9852 and stored at 0x141AB9A68), shared "
        "cross-segment scratch "
        "at +0x30, target skeleton group descriptor at +0x38, the current "
        "10-byte processing/allocation descriptor at +0x40, "
        "at raw+u32(raw+0x30)+10*u16(targetDescriptor+2), NOT a 16-byte stride. "
        "Normal C39 Async parent AnimCmdContext+0x20 is zero; bit0 changes "
        "cmd14 to defaults and skips cmd17/postpose callbacks, while bits1/2 "
        "derive Segment+0x4A masks0x100/0x200. Do not confuse this with "
        "reserved-zero Segment+0x20. "
        "JointParams at +0x50 and matrix workspace at +0x58. "
        "Command 2 constructs an ephemeral evaluator with SegmentContext* at +0x00, "
        "the per-stream lane-pointer-array base at +0x08, the per-stream 16-byte "
        "ValidBits-array base at +0x10, uint32 pointer-slot capacity at +0x18 (initialized to "
        "twice command 2's logical-lane count), the current 10-byte descriptor "
        "pointer at +0x20, uint16 group ordinal at +0x28 and uint8 allocated "
        "logical-lane count at +0x2A. Descriptor word 0 is the JointParams count, "
        "word 1 the rounded scalar count, byte 4 the stream-count/lane stride and "
        "word 3 the first skeleton joint. The word-ordered parser reads the low "
        "16 bits of each header as opcode and the high 16 bits as dword count, so "
        "successive case-7 calls at 0x141ABA30A have the same ordinal as command 7 "
        "records in the list. A command 7 is word0=0x00060007, word1=left lane, "
        "word2=right lane, word3=output lane, word4=mode and word5=float weight "
        "bits; the ApplyBlend call receives outputIsRight=(word3 == word2), so a "
        "well-formed output must alias one of its two input lanes. "
        "Case 27 at 0x141ABAF3B..0x141ABAFB4 raw-copies three float4 rows per "
        "joint from finalized WorkBuffer+0x50 into the opcode's destination, "
        "which the normal builder points at FgAnimData+0x100. "
        "The distinct outer AnimCmdContext+0x00 pointer is the FgAnim target skeleton "
        "used for Player ownership; SegmentContext+0x28 is its child at +0x40, not "
        "the unrelated AnimCmdContext+0x40 buffer pointer. Each invocation owns a "
        "distinct lane lifetime: command 2 allocates its group lanes, command 3 "
        "publishes only that invocation's first finalized lane, and command 5 releases "
        "SegmentContext +0x50/+0x58/+0x60/+0x68 before returning. Equal WorkBuffer "
        "virtual addresses in consecutive invocations are allocator reuse, not evidence "
        "of a cross-list pose handoff. "
        "The workspace allocator uses (group+0x10 + 1) 64-byte rows; the leading row "
        "is initialized as the "
        "root before decimal command 16. STATIC cmd0 stores the descriptor "
        "raw+u32(raw+0x2C)+32*groupOrdinal at Segment+0x38 (141AB9A92). "
        "Cmd15 allocates (u16(descriptor+0x10)+1)*64 bytes at141ABA8EE, stores "
        "the pointer141ABA8F3, and seeds only reserved row0 at141ABA929..A957. Cmd16 walks "
        "authored programs; there is no generic all-workspace-rows initialized "
        "validation. Cmd24 skips when AnimCmdContext+0x20 bit0 is set or its "
        "callback is null (141ABAC2C..39), calls synchronously at141ABAC66 "
        "and ignores the callback return at141ABAC69. For cache publication "
        "observe actual id248B1AAE entry/normal return and destination/range, "
        "not merely Process return or a present command. Decimal command 17 "
        "runs an authored encoded-"
        "scalar copy over JointParams, FgAnimData+0x120 payload, group temp and the "
        "shared scratch. Command 0x17/decimal 23 is the separate plugin callback path. "
        "STATIC P99 source-boundary correction:141AB9B3D is LEA of the custom "
        "operator table, NOT a call. Actual cmd16 CALL141ABA973 ->14217B630 "
        "returns at141ABA978, with Segment supplied by LEA141ABA96B. Pinned "
        "Nadine g0 has51 operators: full JP24/26/49/51 survive unchanged; W22 "
        "has sole writer op40 ordinal36. Post16 before next command holds "
        "all five inputs: JP+480/4E0/930/990 (48B each), workspace+5C0 (64B). "
        "This256B slice lacks owner/generation/validity provenance; pre16 "
        "does not guarantee W22. Cmd5 releases workspace at141ABB107 and "
        "JP at141ABB120, then nulls+58/+50 at141ABB10C/125. Context/group "
        "and normal Async141A92E78 provenance are required; Perform/profile "
        "executions or later group call141A932F8 are not interchangeable. "
        "Post16 is a procedural source boundary, not a final-render claim. "
        "STATIC invocation binding: seven pushes0x38; LEA RBP,[RSP-1570] "
        "gives RBP=entryReturnSlot-15A8, before sub rsp,1670. Cmd16 LEAs "
        "141ABA96B/96F use Segment=RBP-80, status=RBP+58. Thus "
        "Segment+1628=status+1550=Process entry return-slot; it must be "
        "published independently by the original-call bridge, NOT derived "
        "from Segment itself. Segment+08 remains zero, not the parent. "
        "Exact Process epoch is distinct from Async parent-context epoch. "
        "Neither integer comparison is an atomic owner lifetime lease.",
    ),
    0x14162B380: (
        "PackageMgr_RequestPackage",
        "STATIC ADF: exact source assertion. FindExisting141629C70 receives "
        "manager/key only atB405..B40A, not Level. Existing package outside "
        "states0F/10 is reused; B4C2 increments refcount and B4CD unconditionally "
        "replaces Package+830 with a4 Level. No old-context bit20 comparison. "
        "Manager+28 callbackB625 follows overwrite; installed141CC4910 is only "
        "a diagnostic-list append, not logout/relogin. Different-name Levels "
        "sharing a package remain a materialTableOnly lifetime caveat.",
    ),
    0x141EE0BC0: (
        "Level_Load",
        "STATIC ADF: copies WantLoadInfo+1A flags to Level+2 at141EE0C04, "
        "then requests its package list with this Level as context. Bit20 "
        "is materialTableOnly, checked at both package resource login/logout. "
        "Actor request provenance is distinct from asset content provenance.",
    ),
    0x141EE66D0: (
        "Level_SetWantFlags",
        "STATIC ADF: exact source assertion identifies bit20 as "
        "m_materialTableOnly. EE66EA/66F0 test changes; EE674C asserts, "
        "EE6789 writes the full new flag word. This is a contract, NOT an "
        "unconditional refusal if assertions are suppressed.",
    ),
    0x141B85D00: (
        "LoadRegistry_WantLoads_Add",
        "STATIC ADF: same-nameId(+8) want-load requests are merged. "
        "B85EB9..B85EE1 updates only bits1/8/10, preserving material-only "
        "bit20 even if later incompatibility assertionB85EE8..B85F4D is "
        "suppressed. Does not prevent distinct Levels sharing one Package key.",
    ),
    0x141B88360: (
        "LoadRegistry_AddLevelDependentActors",
        "STATIC ADF: dependent actors receive type2 atB88428 and flags09 "
        "atB88430. Only mask12 propagates from the parent atB88462/B8846A; "
        "materialTableOnly bit20 is NOT inherited by these actor requests.",
    ),
    0x14204F4F0: (
        "LevelDef_BuildActorPackageList",
        "STATIC ADF: actor Level package list contains name-winfx, name and "
        "name-dict. Combined with dependent-actor flags09/mask12, ordinary "
        "actor loading does not request materialTableOnly bit20. This does "
        "not exclude a different text LevelDef listing the same package.",
    ),
    0x141B3A430: (
        "LevelMgr_GenerateTodoLists",
        "STATIC ADF: reuses an existing Level by nameId and updates flags "
        "through Level_SetWantFlags141EE66D0. Same-name merge preserves bit20; "
        "PackageMgr separately keys packages without Level identity.",
    ),
    0x14204E5F0: (
        "LevelDef_ReadTxtFile",
        "STATIC ADF: exact source name. Parses arbitrary package %128s "
        "entries at14204E795/E8B5/E8D0. Thus actor-list construction alone "
        "does not prove no other Level can request an identical package key. "
        "Cross-Level material-only rebind for the three pinned skeleton "
        "packages is not established as reachable.",
    ),
    0x1417CDAE0: (
        "SkelTable_LoadAuthoredRetargetEntries",
        "STATIC ADF: startup caller1401571A7 resolves *skel-retarget-tables* "
        "and appends authored48-byte rows into runtime64-byte entries at "
        "143864A00. Count check1417CDBED and AddRetargetEntry source assertion "
        "limit capacity to256, not4096. Next array143868A00 is the separate "
        "draw/ambient-occluder table. Initializes entry+18 to null; not a "
        "record-array release path.",
    ),
    0x1417CDCF0: (
        "SkelTable_AddRetargetEntry",
        "STATIC ADF: exact SkelTable::AddRetargetEntry assertion. Checks "
        "count<100hex at1417CDCF6; diagnostic limit256. Appends at "
        "143864A00+count*40hex, stores authored pointer, clears identities "
        "and entry+18, sets count/flags from authored+20. Physical next "
        "table starts143868A00:256 entries, not a generic40000hex read span.",
    ),
    0x141755BE0: (
        "IDrawControl_MeshesUseAmbientOccluders",
        "STATIC ADF: exact source assertion IDrawControl::MeshesUseAmbientOccluders. "
        "Walks meshes and separate array143868A00, stride28hex, count1438401DC, "
        "matching geometry hash at entry+1C. This neighboring array is NOT "
        "part of SkelTable retarget storage143864A00..143868A00.",
    ),
    0x1418FFD30: (
        "LevelMemAllocator_InitializePackageState",
        "STATIC ADF: initializes allocator caches, stores global PackageMgr "
        "at allocator+420 and supplied package R9 at+430 (141900085/08C). "
        "BuildRetargetEntry captures manager+D8 at1417CE515 and calls here "
        "atCE5C5 before pushing its LevelMemAllocator. This state is stack "
        "local; allocated records belong to package subpages, not its stack.",
    ),
    0x141900210: (
        "LevelMemAllocator_Allocate",
        "STATIC ADF: exact LevelMemAllocator::Allocate source assertions; "
        "RTTI type143771230/COL142CDEAA0/vtable142B11FC8 slot7(+38) resolves "
        "here from Memory_AllocateFromCurrentAllocator. Uses allocator+420 "
        "manager and+430 package in PackageMgr_AllocateLoginData at141900357 "
        "or141900422. Per-allocator cache optional; builder-created allocator "
        "starts with cache mode zero. Not an independent per-record heap lease.",
    ),
    0x14162D080: (
        "PackageMgr_AllocateLoginData",
        "STATIC ADF: exact source assertion. Existing package type8 subpage "
        "allocation first14162D12A; new path reserves/claims page then stores "
        "handle at package+4*currentPage (D1E0/D2B2), increments currentPage "
        "+80C atD2B5. Package state8 or0B..0D required; size bounded by "
        "1437E9774. LevelMemAllocator record allocations therefore enter the "
        "same package handle set released by141628670 max(+80C,+A30) loop.",
    ),
    0x14162CB20: (
        "PackageMgr_AllocateFromPackageSubPages",
        "STATIC ADF: exact source assertion. Scans package-owned handles "
        "below+80C under lock14646ECF0, admits page type8, aligns and advances "
        "used bytes without reclaiming existing allocations. Expansion claims "
        "adjacent/free subpages; new handle is appended at14162D068. Called "
        "by AllocateLoginData, not a retarget-record owner separate from Package.",
    ),
    0x14162CA60: (
        "PackageMgr_ClaimLoginSubPageSpan",
        "STATIC ADF: decodes package subpage handle (pool low4/page next12/"
        "subpage high16), aligns address and assigns span size/used bytes. "
        "Writes package+840 owner word to page-descriptor+12 at14162CAE1; "
        "remaining span owner becomesFFFF. Both package login allocation "
        "paths call this; unload141628803 retires the same owner field.",
    ),
    0x1419C4120: (
        "PackageMgr_PrepareLoadedContentAndApplyFixups",
        "STATIC ADF: loaded-content state handler calls resource-header prep "
        "1416356A0 at1419C4340 for each CONTENT page. Once loaded count+80C "
        "equals authored+A30, passes header Package+A20, LOGIN handles+A4C "
        "and CONTENT handles Package+0 to resolver141635820 at1419C4373, "
        "which calls relocator141635760. These are distinct handle arrays.",
    ),
    0x1417D0BD0: (
        "SkelTable_FindRetargetEntry",
        "Looks up the fixed-stride SkelTable retarget entry for a source/target "
        "ArtItemSkeleton StringId pair. ProcessAnimCmdListSegment case 6 validates "
        "the returned entry's feature word at +0x22 and mapping table at +0x28; "
        "named-reference sampling consumes its uniform position scale at +0x24. "
        "The live table is g_SkelTableRetargetEntries with 0x40-byte entries and "
        "g_SkelTableRetargetEntryCount elements; entry +0x18/+0x20 holds the "
        "resolved 0x2E-record pointer/count after unresolved authored names were "
        "discarded by BuildRetargetEntry. STATIC AHB: call-free/read-only scan; "
        "loads count once, compares entry+08/+0C, returns first match. No "
        "allocation, lock, callback or retention. An owned isolated fixture "
        "must exclude earlier duplicate pairs and bound the count; this says "
        "nothing about live resource lifetime or uncontended performance. STATIC "
        "AHC: table is inline image storage (LEA 143864A0C), not a movable pointer. "
        "P99 table-only page observations may span this leaf and Resolve's audited "
        "read-only closure within ONE existing registry lock scope; reread count "
        "and payload, discard observations before unlock/math/callback. No "
        "borrowed-resource or concurrent-protection lifetime guarantee. STATIC "
        "ADF: actual inline table capacity is256 (AddRetargetEntry assertion "
        "and next unrelated global143868A00), not prior synthetic4096.",
    ),
    0x141CC4940: (
        "GameLoading_Initialize",
        "STATIC AGX: installs GameLoading_PackageLoginResItem (0x141CC0BF0) "
        "at config+0x38 at 0x141CC4E50..4E57 before PackageMgr_Init. This "
        "closes the producer of the package-login callback, not all producers "
        "of loaded resource numeric fields before the doing-login state. "
        "STATIC ADE: also installs logout-resource callback 141CC2EB0 at "
        "config+40 (141CC4E62) and PreparePackageForLogout 141CC4690 at "
        "config+50 (141CC4E78); config base is the PackageMgr_Init argument.",
    ),
    0x141625910: (
        "PackageMgr_Init",
        "STATIC AGX: copies login callback config+0x38 to manager+0x38 "
        "at 0x1416259E7. GameLoading_Initialize installs 0x141CC0BF0; "
        "UpdatePackage_Status_DoingLogin invokes this exact manager slot. "
        "STATIC ADE: config+40 is copied to manager+40 at 1416259EF; "
        "config+50 to manager+50 at 1416259FF. These are concrete callback "
        "fields, not unresolved virtual slots.",
    ),
    0x141CC2EB0: (
        "GameLoading_PackageLogoutResItem",
        "STATIC ADE: resource-logout dispatcher installed in PackageMgr+40 "
        "by GameLoading_Initialize/PackageMgr_Init. Skeleton branch calls "
        "SkelTable_LogoutArtItemSkeleton at 141CC3891 unless "
        "(*(Package+830)+2)&20 is set. Predicate uses the shared context header, "
        "NOT Package+2; login 141CC296B/2998 uses the same predicate. This "
        "dispatcher alone does not prove raw/reference storage lifetime.",
    ),
    0x141CC4690: (
        "GameLoading_PreparePackageForLogout",
        "STATIC ADE: exact PreparePackageForLogout source assertion; callback "
        "installed in PackageMgr+50 at config141CC4E78/init1416259FF. Runs "
        "before per-resource +40 logout callbacks. Graphics/level teardown "
        "preparation, not the ordinary package-page release loop. No direct "
        "store to shared header+2 here; transitive alias stability not proven.",
    ),
    0x1416282B0: (
        "PackageMgr_LogoutQueuedPackages",
        "STATIC ADE: queued LogoutPackage path. For package state0B/0D, "
        "sets0F, calls manager+50 at141628499, resolves LOGIN page/offset "
        "items and synchronously invokes manager+40 at141628522. Only after "
        "all callbacks return, enqueues at manager+4B0/index+4A4, count+4A8 "
        "(1416285C8), then state10 at1416285EB. Direct page release at "
        "141628437 is state9, not the registered0B/0D branch. This orders "
        "ordinary callback completion before deferred page reclamation; it "
        "does not prove the shared skip-bit or every resource alias stable.",
    ),
    0x14162B8C0: (
        "PackageMgr_LogoutPackage",
        "STATIC ADE: immediate LogoutPackage/refcount path, independent of "
        "queued1416282B0. Registered states0B/0D synchronously dispatch "
        "manager+40 at14162BAE1 before awaiting-unload enqueue14162BB7B "
        "and state10 at14162BB93. Direct page-release14162B9D7 is state9. "
        "Shared context-header skip-bit and raw/ref page ownership remain "
        "separate lifetime proof obligations.",
    ),
    0x141628DD0: (
        "PackageMgr_DrainPackagesAwaitingUnload",
        "STATIC ADE: consumes manager awaiting-unload ring, readindex+4A0, "
        "count+4A8, storage+4B0, capacity+4B8; frame/fence checks precede "
        "PackageMgr_UnloadPackagePagesThenClear at141628F45. Dequeue updates "
        "141628F54..F74 follow release. Both ordinary logout paths enqueue "
        "only after synchronous resource callbacks. Follow disassembly at "
        "F3F/F45; a decompiler break can obscure this actual release edge.",
    ),
    0x141628670: (
        "PackageMgr_UnloadPackagePagesThenClear",
        "STATIC ADE: ordinary package-content release. Invalidates page "
        "handles at1416287A4 and page ownership at141628803, runs page "
        "release paths141628824/14162895F/141628A1A, then ClearPackage. "
        "Reached from deferred drain after logout callbacks; state9 direct "
        "callers are a separate branch. Does not by itself establish that "
        "every Art+40/raw+20 allocation belongs to these package pages.",
    ),
    0x141628A70: (
        "PackageMgr_ClearPackage",
        "STATIC ADE: exact PackageMgr::ClearPackage source assertion; "
        "removes name/nickname lookup entries and resets package bookkeeping. "
        "Called after ordinary content-page release by141628670; distinguish "
        "this bookkeeping endpoint from resource logout and page ownership.",
    ),
    0x141627F60: (
        "PackageMgr_UpdateLoginPackage",
        "STATIC AGX: package login state machine; case0x0B calls "
        "PackageMgr_UpdatePackage_Status_DoingLogin (0x1419C4D50). "
        "Earlier states/resource producers are not exhaustively closed.",
    ),
    0x1419C4D50: (
        "PackageMgr_UpdatePackage_Status_DoingLogin",
        "STATIC AGX: resource pointer formed from loaded page handle plus "
        "authored login-item offset at 0x1419C4E72..4F07; dispatches manager "
        "+0x38 at 0x1419C4F1E. Resolved installed callback is "
        "GameLoading_PackageLoginResItem. This path does not rewrite the "
        "resource numeric database; earlier state producers remain open.",
    ),
    0x141CC0BF0: (
        "GameLoading_PackageLoginResItem",
        "STATIC AGX: registered by GameLoading_Initialize through "
        "PackageMgr_Init and invoked by doing-login manager slot+0x38. "
        "AGY correction: ANIM case decimal42/0x2A, NOT hex0x42 or "
        "ARTGROUP_DATA, writes incoming ANIM+0x38=packageName, resolves "
        "ARTGROUP_DATA LOD tables and calls AnimMasterTable_LoginArtGroup "
        "at 0x141CC2934 using incoming ANIM+0x40 as database. "
        "ARTGROUP_DATA itself is case16; no local database rebinding. "
        "Case16 hashes only group-row names at141CC2A55/62, copies authored "
        "u32 clip keys at141CC2BAD and slot pointers at141CC2BB5, then sorts "
        "at141CC2BFC. Entry padding+4 is not initialized at141CC2B10/12 "
        "and cannot be required to equal zero by a generic asset parser.",
    ),
    0x1416272D0: (
        "PackageMgr_CopyRingBytes",
        "STATIC AGY: package loading ring-buffer copy using memcpy; "
        "copies disk bytes without interpreting or rewriting ANIM fields. "
        "Used during metadata/content-page loading before login state0xB.",
    ),
    0x1416356A0: (
        "Package_PreparePageResourceHeaders",
        "STATIC AGY: content page u16+0x12 count, rows+0x14 stride16. "
        "Rebases each row's name qword and descriptor+0/+8 pointers; "
        "hashes names/type strings into descriptor+0x18/+0x1C. Does not "
        "write ANIM+0x40. All35 pinned Elena resources' write ranges do "
        "not overlap profile descriptor page0+0x51DE0 database+0x40.",
    ),
    0x141635760: (
        "Package_ApplyPointerRelocations",
        "STATIC AGY: header+0x18==8 is DIRECTORY COUNT, not record stride. "
        "Signed header+0x1C offset in loginPages[0] points to eight12-byte "
        "{i32 loginPage,i32 tableOffset,i32 count} directories. Positive "
        "count selects loginPages[loginPage]+tableOffset; records8bytes "
        "are {i16 sourceContentPage,i16 targetContentPage,i32 sourceOffset}. "
        "Nonnull source is patched by target-base addition or, if target "
        "absent, sentinel0x00DEB06000DEB060. Source null skips. Login/content "
        "page arrays are DISTINCT. Empty dirs skip here but their pageIndex "
        "is still consumed later by FreeLoginPages. Elena has861 records "
        "in dir0 plus sevenzero dirs; no write overlaps ANIM database.",
    ),
    0x141635820: (
        "Package_ResolveAndApplyPointerRelocations",
        "STATIC AGY: resolves login-page and content-page handles into "
        "separate pointer arrays before calling Package_ApplyPointerRelocations "
        "at0x141635990 (ADF correction: not995). Decompiled temporary names alias; machine operands "
        "RCX/RDX/R8 at5990 preserve distinct header/login/content arguments.",
    ),
    0x1416359C0: (
        "Package_PrepareInMemory",
        "STATIC AGY: independent in-memory package preparation shares "
        "PreparePageResourceHeaders and ApplyPointerRelocations with normal "
        "loading. Login chunk size comes from1437E9774 (default0x200000 "
        "set at141CC4AF5), not content-page offsets or1MiB transfer chunks.",
    ),
    0x14162C1C0: (
        "PackageMgr_FreeLoginPages",
        "STATIC AGY: scans pageIndex of ALL eight12-byte fixup directories "
        "at0x14162C2B0..2DE, including zero-count entries, deduplicates and "
        "frees metadata/login-page handles. Content resources remain live. "
        "Do not infer unconstrained empty-directory pageIndex from the "
        "relocator's earlier count<=0 skip.",
    ),
    0x1419B25A0: (
        "VramItemUpgrader_BuildLookup",
        "STATIC AGY: builds heap-side lookup for VRAM resources. Elena "
        "TEXTURE_TABLE has one slot atpage0+51F40 pointing to payload51BC0, "
        "version2; upgrader1419B2990 returns unchanged. No alias to "
        "profile ANIM database page0+51E20 in the pinned package.",
    ),
    0x1417C8B10: (
        "AnimMasterTable_ResetObserverList",
        "STATIC AGY: resets sentinel143682F08. Startup14015504C..506F "
        "links default AnimTableObserver14303DF10/vtable1428D5CA8 as the "
        "single initial node; registration dispatch uses this list.",
    ),
    0x1417C8B30: (
        "AnimMasterTable_RegisterDefaultObserver",
        "STATIC AGY: inserts default observer node into AnimMasterTable "
        "list. No direct code/data xrefs in current IDB; this alone is "
        "not universal proof of unreachability.",
    ),
    0x1417CA7F0: (
        "AnimMasterTable_RegisterObserverNode",
        "STATIC AGY: generic linked observer-node insertion. RTTI closure "
        "finds AnimTableObserver and AnimStat::AnimObserver; the latter "
        "has no-op callbacks. No direct xrefs to this insertion helper.",
    ),
    0x1417AAF20: (
        "AnimStat_AnimObserver_OnLoginArt_NoOp",
        "STATIC AGY: RTTI AnimStat::AnimObserver vtable142AF0770 slot0. "
        "Only security-cookie check and return; slots1..6 also no-ops. "
        "Cannot rebind ANIM database through this callback.",
    ),
    0x1406D82C0: (
        "AnimTableObserver_RefreshMatchingArtGroup",
        "STATIC AGY: default AnimTableObserver vtable1428D5CA8 slot5. "
        "Resets effect pointers and reprocesses matching descriptors, "
        "without changing ArtItemAnim+0x40 database.",
    ),
    0x1406FBA30: (
        "SkelTableObserver_OnLogin_UpdateMeshPhysics",
        "STATIC AGY: default SkelTableObserver vtable1428D5C20 slot1, "
        "dispatched at1417CD78C. Iterates stride64 mesh registry143843200, "
        "requires mesh+0x28==skeleton+0x24, then calls142003B60 for Havok "
        "collision/destruction properties. Skeleton+0x40/+0x58 are inputs; "
        "no ANIM descriptor/registry insertion in resolved dataflow. "
        "TLS allocators are not a universal arbitrary-alias proof.",
    ),
    0x1417AAF50: (
        "AnimStat_SkelObserver_OnLogin_NoOp",
        "STATIC AGY: RTTI AnimStat::SkelObserver vtable142AF0758 slot1. "
        "Cookie check then return; slot0 also no-op. No database rebinding.",
    ),
    0x1417D1060: (
        "SkelTable_RegisterObserverNode",
        "STATIC AGY: generic insertion into sentinel143683078. Startup "
        "140155385..53A8 installs defaultnode14303DF48. RTTI resolves "
        "default SkelTableObserver and no-op AnimStat::SkelObserver; "
        "this generic helper has no direct code/data references.",
    ),
    0x1412F9F00: (
        "PlayerClimbCache_OnLoginAnimation",
        "STATIC AGY: called by AnimTableObserver_OnLoginArt. Resolves "
        "skeletons by package names proto/young-drake-base/chloe-base, "
        "reads their ArtItemSkeleton+0x24 databases, and compares ANIM "
        "+0x40 at1412FA236..251 before climb-cache builder1412FA530. "
        "No descriptor+0x40 store. These are skeleton Arts, NOT upper "
        "player-state records (old lookup1417CD200 label was misleading).",
    ),
    0x1406D7FD0: (
        "AnimTableObserver_OnLoginArt",
        "STATIC AGX: slot0 of RTTI AnimTableObserver vtable0x1428D5CA8 "
        "(COL0x142C722D8/type0x14374A140). Validates/clears Art+0x48 and "
        "calls 0x1412F9F00. Does not rewrite numeric Art+0x40. LoginArtGroup "
        "already gates exact Art+0x40 database at 0x1417C91EB before slot0.",
    ),
    0x1406D8030: (
        "AnimTableObserver_BeforeLoginArtGroup",
        "STATIC AGX: slot1 of RTTI AnimTableObserver vtable0x1428D5CA8. "
        "Clears observer+0x18; not an animation database rebinding path.",
    ),
    0x1406D8060: (
        "AnimTableObserver_AfterLoginArtGroup",
        "STATIC AGX: slot2 of RTTI AnimTableObserver vtable0x1428D5CA8. "
        "Post-login validation path; no store to animation Art+0x40.",
    ),
    0x1406D85E0: (
        "AnimTableObserver_ExecuteDebugValidation",
        "STATIC AGX: validation reached by AnimTableObserver post-login "
        "callback. Does not rewrite the animation descriptor database+0x40; "
        "other installed observers are not exhaustively excluded.",
    ),
    0x1417D0890: (
        "SkelTable_GenerateSegmentMapping",
        "Exact skel-table.cpp:954 GenerateSegmentMapping(RetargetEntry*, "
        "ArtItemSkeleton*, ArtItemSkeleton*) assertion. Uses resolved "
        "0x2E records' global joints+0x24/+0x28 to map source group bits "
        "into target-group bitsets at entry+0x28 (0x1417D0A7D..D0AF1). "
        "At 0x1417D08FA dereferences manager global0x1437E98C8+0xD8; "
        "a null allocator there skips mapping, but a null manager is NOT "
        "accepted. Ordinary nonnull path uses LevelMemAllocator and may "
        "allocate the mapping. Offline BuildRetargetEntry cannot be called "
        "without auditing/providing this manager dependency; this is not "
        "part of DoJointRetarget's numerical leaf call graph. AGX isolated "
        "row20 builder uses owned zero0xE0 manager, +0xD8 null, preserving "
        "the original no-allocation return. Its __GSHandlerCheck_EH unwind "
        "can reach unresolved __CxxFrameHandler3; .pdata registration alone "
        "does not establish exception recovery in an isolated mapper.",
    ),
    0x1406FBB40: (
        "SkelTablePackageObserver_BuildRetargetEntriesForPackage",
        "STATIC AHA: SkelTablePackageObserver vtable1428D5BE8 slot0, "
        "RTTI type14374A040/COL142C72238, node14303DF60. Package login "
        "state0xC dispatch1416280FD invokes this package observer after "
        "state0xB resource login. Call1406FBC8B reaches BuildRetargetEntries "
        "1417CDDC0 then1417CE198->BuildRetargetEntry. No SkelTable lock "
        "1466EC4B0 acquisition in this callback/build path. Distinct from "
        "the skeleton-resource observer1406FBA30; do not conflate them.",
    ),
    0x1417CDDC0: (
        "SkelTable_BuildRetargetEntries",
        "Exact SkelTable::BuildRetargetEntries source assertion at skel-table.cpp:482. "
        "Resolves every authored raw source/target skeleton pair, samples the "
        "optional reference-pose animation id from raw field 9, then constructs the "
        "fixed-stride runtime retarget entry consumed by case-6 animation commands. "
        "STATIC AGW: raw-row+0x24 profile is resolved by exact-database "
        "0x1417C8BA0, not AnimationDatabase_ResolveClip's optional fallback. "
        "Zero id or null lookup preserves target raw+0x20 local references "
        "and raw+0x28 inverse global Mat34 arrays. A nonnull clip is sampled "
        "by AnimateObject at time0/flags2; false stops the outer entry loop "
        "at 0x1417CE06F, it does not fall back to bind pose. Success replaces "
        "target local references and inverse world matrices before the "
        "0x1417CE198 builder call. Disk profile FC34096C in elena-base has "
        "Art+0x40=46622D19, not Nadine ED25C708; rebinding/residency remains "
        "unproven, so neither profile-present nor null may be assumed live. "
        "STATIC AHA: sole direct caller1406FBC8B is package observer "
        "1406FBB40 dispatched at package-login stateC, not protected by "
        "SkelTable registry lock1466EC4B0. Stable registry epochs alone "
        "do not prove unchanged retarget record values or historical basis. "
        "STATIC 2026-09-20: row124 profileE51542CA has an exact-target authored "
        "registration in pinned nadine-dlc-base.pak: file93E98 -> slot93278 "
        "-> descriptorA21A0 (databaseED25C708) -> ANIM2DA0 (C39CA709,7groups, "
        "one frame). This is not live residency proof, but unlike the Elena "
        "profile it cannot be dismissed as a database mismatch. Existing AHB "
        "row124 fixture has zero UNUSED numeric bytes, not built locators. "
        "For direct-base comparison reconstruct the complete sampled target "
        "references/inverse globals beforeCE198; group0-only profile decode "
        "and conditional-null row124 are not equivalent operands. STATIC "
        "full E51542CA constant decode covers426 parameters and27 separate "
        "scalars. Required globals0,2..30,32..34,104..141 form a closed "
        "71-matrix op40 slice in g0/g1; no other operator writes these "
        "matrices or their104 input parameters. Sampled world output is "
        "64-byte column-vector Transform; CE108..CE141 transposes each "
        "inverse to the builder's48-byte row-major Mat34. Scalar direct "
        "R124 versus conditional R157(R20) originally reported151.847deg, "
        "but that result is REJECTED: Python opcode24 decoder used wrong "
        "permutation/divisor. Corrected scalar max13.794632deg, palm "
        "0.005460deg; analytic bind control0.01346deg. Still not live/skin "
        "proof and not evidence of globally wrong authored profile. "
        "Corrected thumb137/139/141 local profile-vs-bindQ differs only "
        "0.0025..0.0032deg; inherited global basis differs~21.545deg via "
        "shoulder22/elbow24/wrist26. All426 profile/bind local scales are "
        "unit. A posed input need not commute with this inherited basis; "
        "do not mislabel the remaining13.79deg as a local thumb-profile bend.",
    ),
    0x1417CE210: (
        "SkelTable_BuildRetargetEntry",
        "Builds one runtime skeleton-retarget entry from the authored StringId map, "
        "uniform scale and sampled target reference pose. Raw bytes +0x2C/+0x2D gate "
        "the optional arm/leg IK structures stored at runtime +0x30/+0x38. Each "
        "resolved 0x2E-byte record stores skeleton-global source/target joints at "
        "+0x24/+0x28, compressed processing-table source-parameter index at +0x26, "
        "complete compressed target-reference index at +0x2A, and retarget mode at "
        "+0x2C as one byte. Runtime-record +0x2D is untouched tail padding; the "
        "native consumer also reads only byte +0x2C. The builder consumes the "
        "fixed-up authored-row and map pointers directly, so on-disk DC offsets "
        "cannot be reconstructed as a live contiguity invariant. These indices are "
        "not skeleton-global joints: Nadine group 1 maps global 104..141 to "
        "reference rows 66..103 (for example 105 -> 67). STATIC AGW: numeric "
        "record +0..+23 is constructed, not copied from a local reference. "
        "Parent inverse-global Mat34 inputs feed locator+0xE at "
        "0x1417CF634..CFA6C; joint inverse-global inputs feed locator+0 at "
        "0x1417CFA70..CFE78. Remaining halves hold source local translation "
        "and the bone-length ratio. Translation+1C..20 at CF58B..CF5FF "
        "uses a4+48*SOURCE_GLOBAL, unlike later compressed-index boneScale; "
        "an offline fixture must preserve the full raw asset extent, not "
        "silently substitute source parameter index or truncate to local "
        "reference rows. Metadata-only authored joins do not "
        "establish the full 46-byte runtime operand values. STATIC AGX: "
        "actual ABI reads seven pointers; args8/9 in assertion signature "
        "are unused. Nonnull entry+0x18 bypasses allocation but entry+0x20 "
        "does NOT bound writes; reserve authored_count*46 before resolving. "
        "Both parent matrices are read/inverted BEFORE source-parent<0 "
        "branch 0x1417CF7C5 selects identity correction at CF937. Root "
        "(-1,-1) requires readable matrixBase-48 bytes, not identity there; "
        "both inverse results are discarded. BoneScale source-length "
        "epsilon at CFF3E is about1e-5; prior squared-length mask uses "
        "FLT_MIN. Zero length yields scale0. AGX isolated profile0 row20 "
        "built93 records in owned96-record capacity; no live Nadine claim. "
        "STATIC AGZ: row157 authored74 resolves71, same explicit-map path "
        "CE6A6->CF327, flags+2C/+2Dzero skip D006F/D0428 IK branches. "
        "Conditional null-profile caller CDF40..79 passes authored bases; "
        "retain raw profileFC34096C rather than changing it to zero. "
        "Chloe globals25/2 squared lengths below FLT_MIN yield scale0 for "
        "Nadine targets14/2; complete resources/root prefixes are required. "
        "AGZ ISOLATED_NATIVE: conditional authored-basis row157 builds71 "
        "records and shared full21-row materializer passes42DoJoint/21Blend "
        "with identical output under MXCSR1F80/9FC0. Not live-null or grip proof. "
        "STATIC AHA: existing nonnull entry+18 reused atCE503; only null "
        "entry allocates/assigns atCE635. STATIC ADF: CE515 captures PackageMgr+D8, "
        "CE5C5 initializes LevelMemAllocator, CE5E5 pushes it. Allocation via "
        "vtable142B11FC8 slot7 reaches14162D080 and package-owned type8 subpages; "
        "the stack allocator is NOT the record lifetime. Ordinary package "
        "release141628670 includes dynamically appended handles below+80C. "
        "Explicit payload writes startCF490; "
        "no record free or registry epoch write in builder. Package callback "
        "path does not acquire SkelTable registry lock. Compare completed "
        "owned operands on each preparation rather than cache epoch alone. "
        "STATIC 2026-09-20 packed locator algebra: invert source/target "
        "inverse-global Mat34 through general affine14023ABB0, obtain "
        "basis quaternions via142177E50, then half(conj(Qsource)*Qtarget). "
        "Parent quaternion at+14h, joint at+06h. Builder does not normalize "
        "before nearest-even half packing; decoder141AE1F10 normalizes "
        "after loading. Approximate scalar oracle agrees with84 archived "
        "native locators within2.385e-7;24 are not byte-identical. Root "
        "negative-prefix loads still precede its discard branch.",
    ),
    0x141936490: (
        "RetargetPlugin_AddJointRetargetCommands",
        "Exact AddRetargetPlugin source assertion in nd-anim-plugins.cpp:181. "
        "Publishes the selected runtime retarget entry and its StringId joint map "
        "into the animation command stream before target-pose evaluation. At the "
        "nested callsite 0x141937DBA, SegmentContext+0x38 is the source-evaluation "
        "group descriptor, +0x40 is the current source processing descriptor and "
        "+0x50 is the evaluated source JointParams base. Group +0x02 selects the "
        "10-byte processing descriptor; its +0x00/+0x02 words are joint/scalar "
        "counts and byte +0x04 is the logical lane count, not a row stride. The "
        "source group's 128-bit MSB-first ValidBits follow the source pose at "
        "48*(processing joint count+1). The callback's "
        "target-reference base is the complete ArtItemSkeleton reference pose: "
        "target+0x40 raw skeleton, then raw + *(u32 *)(raw+0x20). It is not "
        "pre-offset to the current processing group.",
    ),
    0x141AE2A50: (
        "RetargetPlugin_ApplyJointRetargets",
        "Exact ApplyJointRetargets source assertion. Applies the authored joint map "
        "and sampled reference-pose correction. For each 0x2E record it indexes "
        "the group-local source/output buffers by global-first, but indexes the "
        "complete compressed target reference pose directly by the u16 at "
        "record+0x2A before calling DoJointRetarget. Therefore +0x2A cannot be "
        "substituted with record+0x28's skeleton-global target joint. It "
        "conditionally dispatches the separate arm/leg IK "
        "stages only when their runtime entry pointers exist. STATIC P99 "
        "upstream admission:141AE2D05..D39 skips a map row if the NONNULL "
        "source ValidBits lacks its MSB-first joint bit;141AE2E55 sets target "
        "validity only after that gate. R124 contains Player24->Nadine22(mode2), "
        "26->24(mode1),28->26(mode1), but D350 authors none of those source "
        "rows. B8/R157 likewise does not author these upstream rows. The21 "
        "grip targets28/30/right fingers do not determine upstream localQ. "
        "Nadine49/51 have no R124/R157 map entries; this alone does not prove "
        "they are final-reference constants in arbitrary blended/live lanes.",
    ),
    0x141AE3320: (
        "RetargetPlugin_DoRetargetArmIk",
        "Exact DoRetargetArmIk source assertion. Optional post-map arm-chain retarget "
        "stage created only when the raw retarget record's arm-IK enable byte is set.",
    ),
    0x141AE3640: (
        "RetargetPlugin_DoRetargetLegIk",
        "Exact DoRetargetLegIk source assertion. Optional post-map leg-chain retarget "
        "stage created only when the raw retarget record's leg-IK enable byte is set.",
    ),
    0x142193CE0: (
        "AnimJointProgramOp22_DecodeHalfVectors",
        "Animation joint-program opcode 22 dispatcher. Resolves its destination map "
        "and decodes half-float three-component vector blocks used by constant-pose "
        "scale and translation lanes.",
    ),
    0x142185770: (
        "AnimJointProgram_DecodeHalfVectors8",
        "SIMD body for animation opcode 22. Decodes three IEEE half components for "
        "each mapped destination in eight-row blocks and writes the float lanes. "
        "STATIC E51542CA has26 meaningful half-subnormal components, all "
        "translations. Manual conversion is MXCSR/DAZ-sensitive; ordinary "
        "Python half conversion is not an instruction-exact substitute.",
    ),
    0x142193D60: (
        "AnimJointProgramOp24_DecodePackedQuaternions",
        "Animation joint-program opcode 24 dispatcher for the engine's six-byte "
        "smallest-three quaternion representation.",
    ),
    0x1421861A0: (
        "AnimJointProgram_DecodePackedQuaternionBlocks",
        "Block wrapper for opcode 24. Walks the destination map and packed six-byte "
        "quaternion payload before invoking the eight-row SIMD decoder.",
    ),
    0x142185CF0: (
        "AnimJointProgram_DecodePackedQuaternions8",
        "Decodes A=bits2..16, B=bits17..31, C=bits32..47 of each6-byte "
        "packed quaternion. A/B use float383504F3, C37B504F3; offsetBF3504F3 "
        "(denominator32768, NOT32767). Pre-permutation lanes at142185F9B/FA1 "
        "are(C,B,A,D), with positiveD from1-((C*C+B*B)+A*A), FLT_MIN clamp "
        "and rsqrt/Newton sqrt approximation. No completed-Q normalization "
        "occurs before stores.142185FAC uses the selector row from146C71CE0: "
        "0(D,B,A,C),1(C,D,A,B),2(C,B,D,A),3(C,B,A,D). Historical Python "
        "natural insertion and32767 divisor are REJECTED. Corrected scalar "
        "decoder matches42 frozen nativeD350/B8 Q within1.193e-7, but scalar "
        "sqrt is not instruction-exact. Do not reuse prior profile hash "
        "fee2c8d5/151.847deg comparison as engine behavior.",
    ),
    0x142193DA0: (
        "AnimJointProgramOp25_CopyFloatLanes",
        "Animation joint-program opcode 25 dispatcher for mapped constant float-lane "
        "payloads. STATIC: destination bank is state+10h scalar storage, "
        "not op22/op24 state+8 JointParams. E51542CA g0 has27 logical "
        "scalars,32 physical writes and repeated padding destination112.",
    ),
    0x142186270: (
        "AnimJointProgram_CopyFloatLanes8",
        "SIMD body used by opcode 25 to copy authored constant float lanes to their "
        "mapped joint-program destinations in eight-row blocks.",
    ),
    0x1417CD100: (
        "SkelTable_ResolveArtItemSkeleton",
        "Resolves an ArtItemSkeleton from its StringId through the skeleton registry. "
        "This is a lock-free borrowed lookup: it calls the registry hash walker and "
        "returns node+0x18 value[0] without AddRef. Callers that cross a lifecycle "
        "boundary must hold g_SkelTableRegistryLock while copying the value. "
        "STATIC AHB: sole callee is 1417D1170; no imports/allocations/registry "
        "writes. Registry lock protects node removal, not package-callback "
        "in-place retarget payload rebuilds (see 1406FBB40). STATIC AHC: together "
        "with Find1417D0BD0 and walker1417D1170 this is the entire no-callback "
        "allowlist for scoped FIXED-IMAGE TABLE page observations, not authority "
        "to cache borrowed resources or survive unlock. Every payload reread stays.",
    ),
    0x1417CD4E0: (
        "SkelTable_LoginArtItemSkeleton",
        "Registers one ArtItemSkeleton under the 64-bit SkelTable registry lock. "
        "Acquisition is LOCK CMPXCHG qword 0->-1 at 0x1417CD51E, release is XCHG "
        "qword zero at 0x1417CD7A0, and successful insertion increments the separate "
        "g_SkelTableRegistryEpoch at 0x1417CD754..0x1417CD75F. STATIC ADF: "
        "Art+24 database/registry key reads1417CD52D/CD6B6 are NOT the "
        "raw+4 skeleton ID. Pinned npc-normal pair is2628A5DC/BD7EEE2F; "
        "npc-medium isF181B564/FC65A17A. Authored loader fields map to "
        "runtime Art fields with+20 shift, so loader+4 is this database key. "
        "CD681 stores the exact incoming Art pointer forwarded by141CC2995; "
        "record+08/+10 stores package name/hash atCD6AE. Pinned proto, Chloe "
        "and Nadine Art+40 fixups target their own CONTENT raw skeleton; "
        "raw+20 is an inline reference-pose offset, not a separate allocation. "
        "Source ownership does not prove Package+830 requester provenance.",
    ),
    0x1417CD7C0: (
        "SkelTable_LogoutArtItemSkeleton",
        "Removes and frees one skeleton-registry node under the same 64-bit lock. "
        "Acquisition is LOCK CMPXCHG at 0x1417CD80C, release is XCHG zero at "
        "0x1417CD9B3, and removal increments g_SkelTableRegistryEpoch at "
        "0x1417CD994..0x1417CD9A5. The record use-count does not pin the node. "
        "STATIC ADE: ordinary caller GameLoading_PackageLogoutResItem at "
        "141CC3891 returns synchronously before package enters the deferred "
        "page-unload ring. Conditional on stable level+2 bit20 and package "
        "CONTENT-page provenance of raw/ref storage; not a universal lease. "
        "STATIC ADF closes pinned source provenance and ordinary actor bit20 "
        "path; RequestPackage14162B380 can still replace Package+830 across "
        "different Levels sharing one key. No universal rebind guarantee.",
    ),
    0x1417D1170: (
        "SkelTable_FindSkeletonRegistryNode",
        "Lock-free fixed-table hash walker used by ResolveArtItemSkeleton. It does "
        "not acquire the registry lock or retain the returned node; safe snapshots "
        "must therefore enter through the owning SkelTable lock. STATIC AHB: "
        "FNV1a hashes the four little-endian ID bytes (811C9DC5/01000193), "
        "bucket modulo count. Node next+00/key+10/value+18; null ends a chain; "
        "buckets[count] is the miss sentinel. Only writes caller iterator16. "
        "No chain-cycle bound: isolated replay requires nonzero count and "
        "owned acyclic chains; it is not an arbitrary live-memory admission. "
        "STATIC AHC: no calls out or global writes; only caller iterator16 is "
        "written. Narrow table-page reuse expires at the surrounding lock "
        "scope, never retains registry-node or resource contents.",
    ),
    0x14193B2C0: (
        "RetargetAnimUsingBatchMgr",
        "Exact RetargetAnimUsingBatchMgr source assertion in retarget-util.cpp:202. "
        "Evaluates an ArtItemAnim across source and target ArtItemSkeletons using a "
        "SkelTable retarget entry and returns target JointParams/ValidBits buffers "
        "through arguments 7 and 8. It allocates the target ValidBits prefix first, "
        "then returns JointParams after 16*target scalar-group-count bytes. The "
        "caller at 0x141AB9FC4 immediately forwards those returned pointers to "
        "AnimCmdEvaluator_CopyMaskedPose, so a post-call read at that exact E8 is "
        "the bounded target-pose lifetime before work-buffer publication.",
    ),
    0x14217C7F0: (
        "AnimCmdEvaluator_ApplyAnimationDirect",
        "Direct ProcessAnimCmdListSegment case-6 evaluator used when the source and "
        "target underlying skeleton ids match. Evaluates the clip into the current "
        "processing group's work-buffer pose output.",
    ),
    0x14217D280: (
        "AnimCmdEvaluator_CopyMaskedPose",
        "Copies masked JointParams and ValidBits into the current processing-group "
        "output. ProcessAnimCmdListSegment case 6 uses it to publish the pose returned "
        "by RetargetAnimUsingBatchMgr into the same work buffer as direct evaluation. "
        "The selected ValidBits slice is base + 16*(target group descriptor uint16 "
        "+0x4 + processing descriptor byte +0x5); for legacy-Nadine group zero both "
        "offsets are authored zero, so the first 16-byte slice owns C39 group-zero "
        "validity.",
    ),
    0x14217D940: (
        "AnimCmdEvaluator_ClearOutput",
        "Clears a processing-group output slot's ValidBits and status; does not "
        "author all-valid identity/bind rows. Process segment case9 calls here "
        "at141ABA4A9 for InstanceZero output. D969/D96C clear first stream "
        "bits at evaluator+0x10; D97B/D97F clear the second stream when present; "
        "mirrored validity tails in pose storage are cleared as well.",
    ),
    0x141AB7FE0: (
        "AnimCmdEvaluator_IsLaneValidBitsEmpty",
        "Returns true only when the selected logical lane has no set bits in any "
        "of the per-stream 16-byte ValidBits records referenced by evaluator+0x10, using the "
        "current descriptor's byte-4 stream count/lane stride. ProcessAnimCmdListSegment "
        "case 7 tests word1 and then word2 with this helper; only when both inputs "
        "are empty does it clear word3 and skip AnimCmdEvaluator_ApplyBlend.",
    ),
    0x14217CF90: (
        "AnimCmdEvaluator_ApplyBlend",
        "The ProcessAnimCmdListSegment case-7 consumer for command 0x00060007. "
        "Its arguments map exactly as left=word1, right=word2, "
        "outputIsRight=(word3 == word2), mode=word4 and weight=bitcast<float>(word5). "
        "It therefore selects the right lane as destination only when outputIsRight "
        "is true and otherwise selects the left lane; word3 is not passed as an "
        "independent lane index and must alias one input in a well-formed command. "
        "It indexes the lane-pointer array referenced by evaluator+0x08 and the "
        "16-byte ValidBits array referenced by +0x10 by logical lane times descriptor "
        "byte 4, unions validity "
        "into the selected output, then invokes the transform and scalar pose-blend "
        "helpers. Mode 1 at weight 1 is right-over-left for every joint valid in "
        "the right child. The "
        "generator short-circuits weight <=0.0001 to child 0 and weight >=0.9999 "
        "to child 1 when endpoint evaluation is enabled.",
    ),
    0x142188A30: (
        "AnimJointParams_BlendTransformsByValidBits",
        "Transform-record helper used by AnimCmdEvaluator_ApplyBlend. It "
        "interpolates only joints valid in both inputs; joints valid in just "
        "one input are copied as complete 48-byte records without applying the "
        "cmd7 weight. This rule alone does not make WeaponPartial's empty state "
        "one-sided: AnimStateSnapshot_GenerateAnimCommands has already emitted its "
        "{0,output,output} tail and copied lane-zero validity into that track before "
        "the outer track fold. STATIC AGV: fixed encoding0x0108 selects "
        "mode2 table 0x142BDD940[2]=0x142191AE0; count1/masks{MSB,0} "
        "produce offsets0/48/48/48 within the owned112-byte blend blob. "
        "No per-joint weight or one-sided-copy branch under that contract. "
        "STATIC AGW: fourth argument is float in XMM3 (caller vmovss at "
        "0x1421942DE), not the decompiler's double. An isolated mapped "
        "native caller must register the entire PE exception directory; "
        "this function's unwind at0x142F86298 uses __GSHandlerCheck. "
        "STATIC AGX: dispatcher D1D..D24 pads to four indices {0,48,48,48}. "
        "Mode2 writes full left[0:96], including the discarded sentinel "
        "row at+48; it is NOT untouched padding or required finite output. "
        "Right112 stays read-only. Outer DFB stores masks at left+96/+104, "
        "still {MSB,0} for the one-row isolated fixture. STATIC 2026-09-20: "
        "unit-weight quaternion composition is Qleft*Qright (Hamilton xyzw), "
        "not reversed. Independent scalar composition is checked against "
        "the pinned AGZ native stage artifacts; this validates the convention "
        "within numerical tolerance, not a visible P99 repair.",
    ),
    0x142191AE0: (
        "AnimJointParams_BlendMode2FourLane",
        "STATIC: mode2 leaf-math dispatcher selected by 0x142188A30 "
        "through table0x142BDD940+0x10. Fixed P99 encoding0108 reaches "
        "this path. Ordinary helpers 0x14217FB70/0x1421803E0 are SIMD "
        "leaves without assertion/callback edges; security-cookie failure "
        "is an exceptional corruption path, not a numeric assertion. "
        "STATIC AGW: initial unwind0x142F86E24 uses __GSHandlerCheck; "
        "split .pdata entries chain to it. Raw-backed constants/cookie "
        "suffice for ordinary P99 math, but assertion/cookie-failure "
        "reporters use uninitialized imports/global writes in MappedImage.",
    ),
    0x14217E7C0: (
        "AnimBitset_Popcount64",
        "STATIC: branchless staged 64-bit bit population count using "
        "0x55/0x33/0x0F/0x00FF/0x0000FFFF masks (zero fast path). "
        "Used by transform blend valid-bit offset calculation. No calls "
        "or assertions; not an engine callback boundary.",
    ),
    0x142193E90: (
        "AnimJointProgramOp28_RefinePackedQuaternion",
        "Retail joint-program opcode 28 handler. It consumes the AVX "
        "permutation rows materialized by the CRT initializer at 0x140128020; "
        "offline mapped-image evaluators must reproduce those exact copies "
        "before invoking this handler.",
    ),
    0x141AB78F0: (
        "AnimCmdEvaluator_FinalizeGroupDefaults",
        "Finalizes the current processing-group lane before command 3 publishes "
        "it to the common WorkBuffer. Inverse ValidBits select missing joint and "
        "scalar ranges, which are filled from the target skeleton defaults; the "
        "same bits are then marked valid. The 0x142188FF0 -> 0x142192640 path "
        "copies one exact 48-byte JointParams record per missing joint. The joint "
        "mask spans the processing descriptor's word-0 record count, while word 3 "
        "selects the first skeleton-default record. Command 15 may later replace "
        "this allocation in place with a larger target-workspace-count block; "
        "FinalizeGroupDefaults does not initialize that expanded tail. Already-valid "
        "prefix records retain preceding authored direct/retarget/blend output. "
        "STATIC P99 helper-source boundary:141AB7993/799F invert existing "
        "ValidBits and141AB79A7 intersects the count mask before fill141AB79D7. "
        "Raw target references for g0 rows22/24/26/49/51 are valid fallbacks "
        "ONLY when their incoming final bits are clear. D350/B8 clip-local "
        "absence does not imply that condition after other Base/Hands/IK "
        "layers. Never replace unknown valid upstream Q with E51542CA "
        "locator-profile values or bind references. The fill is followed by "
        "OR missing bits into validity at141AB79DC/79E6. Cmd3 publishes JP "
        "at141AB9DD6 and discards its lane context at141AB9E2F/32. Later "
        "post16 bytes can supply actual computed values but cannot recover "
        "which came from defaults; that claim needs earlier ValidBits/lane "
        "lineage. Final Async mode2 runs cmd3 but THEN cmd14 replaces the "
        "entire66-row prefix from postblend Fg+100. That earlier cmd3 mask "
        "does not describe the imported source; do not assume cmd3 is absent.",
    ),
    0x14217B310: (
        "AnimCmdEvaluator_CopyGroupPoseToWorkBuffer",
        "Copies the current processing group's JointParams and scalar range into "
        "SegmentContext WorkBuffer pointers +0x50/+0x60. The joint source starts "
        "at suppliedPose + 48*u16(processingDescriptor+6), the packed "
        "first_parameter rather than workspace-global firstJoint. The "
        "destination is WorkBuffer+0x50, "
        "and the copied count is targetGroup+0x12. ProcessAnimCmdListSegment case "
        "14 calls it for the flags-byte-zero external-pose branch; the wrapper at "
        "0x141AB78C0 supplies the target skeleton default arrays.",
    ),
    0x141AB78C0: (
        "AnimCmdEvaluator_CopyTargetDefaultsToWorkBuffer",
        "Thin wrapper over AnimCmdEvaluator_CopyGroupPoseToWorkBuffer. It supplies "
        "the target skeleton's JointParams and scalar default arrays from the "
        "offsets at skeleton+0x20/+0x24.",
    ),
    0x142188FF0: (
        "AnimCmdEvaluator_FillMissingJointDefaults",
        "Called by FinalizeGroupDefaults for inverse-ValidBits joint ranges. "
        "Selects target-skeleton default JointParams records and forwards each "
        "missing joint to AnimJointParams_CopyRecord48.",
    ),
    0x142192640: (
        "AnimJointParams_CopyRecord48",
        "Copies one complete 48-byte JointParams record. In command-3 default "
        "finalization its source is skeleton defaults and its destination is the "
        "fresh current processing-group lane.",
    ),
    0x14217C3A0: (
        "AnimCmdEvaluator_AllocateGroupLanes",
        "Command 2 supplies the current descriptor pointer at evaluator+0x20 and "
        "group ordinal at +0x28 before this helper allocates and indexes its "
        "joint/scalar output lanes. It fills the per-stream lane-pointer array "
        "referenced by +0x08 and 16-byte ValidBits blocks referenced by +0x10, "
        "bounds pointer-slot writes by "
        "the uint32 capacity at +0x18, and records the resulting allocated logical-"
        "lane count at +0x2A. Descriptor word 0 supplies the 48-byte JointParams "
        "record count, word 1 the rounded scalar count, byte 4 the stream-count/lane "
        "stride and word 3 the first skeleton joint. "
        "This descriptor is distinct from the target skeleton group's "
        "workspace/parameter counts at SegmentContext+0x38.",
    ),
    0x141936A90: (
        "LegFixIkBinaryPluginNodeBatchCallback",
        "Source-asserted LegFixIkBinaryPlugInNodeBatchCallback. Resolves exactly "
        "two LegIkChain setups for the current skeleton, validates only their "
        "authored joint-index lists, requires the left/output pose lanes to "
        "alias, and calls LegFixIk_SolveTwoLegChains. It consumes pose lanes and "
        "skeleton setup only; it has no Player, camera or aim-controller input.",
    ),
    0x141944670: (
        "LegFixIk_SolveTwoLegChains",
        "LegFix worker reached by the source-named binary plugin callback. The "
        "outer loop is exactly two chains; each chain reads its three key joint "
        "indices and copies solved transforms back only through that chain's "
        "authored index list. It cannot introduce head, spine or arm pitch.",
    ),
    0x141988200: (
        "HandFixIkPluginCallback",
        "Exact source signature HandFixIkPluginCallback. The command-23 worker "
        "resolves the left and right wrist chains and invokes SolveArmIk for the "
        "two arms. Its inputs are plugin data plus mutable/const JointParams "
        "lanes; it has no Player or camera lookup.",
    ),
    0x141ABE820: (
        "AnimSnapshotNodeHandFixIk_Construct",
        "Constructs the RTTI anim-node-hand-fix-ik runtime node, its child link "
        "and node-local plugin data used by the command-23 HandFix callback.",
    ),
    0x141ABEB80: (
        "AnimSnapshotNodeHandFixIk_Initialize",
        "Initializes the HandFix snapshot node and its authored left/right arm "
        "JointSets before command generation.",
    ),
    0x141ABEC00: (
        "AnimSnapshotNodeHandFixIk_GenerateAnimCommands",
        "Generates the child pose commands and appends command 23 with plugin id "
        "0x98FBC8FE only when the node's authored arm-enable word is nonzero.",
    ),
    0x141ABEEA0: (
        "AnimSnapshotNodeHandFixIk_CreateOrElide",
        "Factory for raw anim-node-hand-fix-ik (StringId 0x612A780B). Evaluates "
        "hand-fix-ik-func 0x1DD77161 and returns the child directly when the "
        "authored arm mask is zero; otherwise materializes the HandFix node.",
    ),
    0x1418F0B60: (
        "SolveArmIk",
        "Source-asserted SolveArmIk. Validates shoulder, elbow and wrist offsets "
        "for one left/right arm and writes the elbow and shoulder JointParams "
        "through 0x14181F450; the wrist supplies the end target/reference. This "
        "bounds HandFix to the selected arm hierarchy. STATIC 2026-10-08: "
        "inherits n=normalize((elbow-shoulder) cross (wrist-elbow)) at "
        "1418F116F..11FC, falling back to negative elbow-world Y at1418F1202. "
        "Bends elbow at1418F1374, then shortest-arc aligns shoulder-to-wrist "
        "with shoulder-to-target at1418F1442/1501. Request has set+0, arm+8, "
        "targetP+10, outputP+20, weight+30, failUnreachable+34; no independent "
        "elbow pole or torso-clearance input. Byte143684E64 bypasses solve. "
        "Terminal WeaponIK passes failUnreachable=0, so reach is clamped, "
        "not refused; changing only the wrist target does not independently "
        "select an authored elbow roll. STATIC2026-10-09: actual reach clamp "
        "1418F113E is min(requested distance,upper+lower). The0.99 threshold "
        "1418F107F is only unreachable/refusal routing, not a bend margin. "
        "At full extension, fixed-endpoint roll cannot meaningfully move E.",
    ),
    0x1406FE2E0: (
        "Math_SolveTriangleAnglesFromSides",
        "STATIC side-to-angle helper called by SolveArmIk1418F116A. RCX=three "
        "broadcast angle outputs, RDX/R8/R9=broadcast side inputs. Sorts "
        "largest side, evaluates acos(clamp((b*b+c*c-a*a)/(2*b*c),-1,1)) "
        "at1406FE379..3C7, sine-law second angle and pi-A-B. SolveArmIk passes "
        "(clamped target distance,forearm length,upperarm length); first "
        "angle is elbow interior, approaching pi at full reach.",
    ),
    0x14181DEA0: (
        "JointSet_HasAllJointsInValidBits",
        "Tests every authored joint index in a joint-set/IK-chain descriptor "
        "against a ValidBits block and returns false at the first missing bit. "
        "LegFix uses it independently for each of its two leg chains.",
    ),
    0x141FFEDB0: (
        "AnimNodeGesture_StepAngles",
        "Updates an AnimNodeGesture's cached angle/transform state once per shared "
        "frame epoch. It invokes the Gesture::GetComputeArgs closure through "
        "computeArgs+0x70 vtable slot +8, runs the gesture solver, and stores the "
        "resulting cached angles at node+0x80/+0x84 for Step/command generation. "
        "The compared g_LocalPlayerInputManager_Ptr+0x50 qword is an epoch, not a "
        "camera vector; the literal aim selects authored gesture settings.",
    ),
    0x141FFA550: (
        "AnimNodeGesture_Step",
        "Consumes the cached gesture state produced by AnimNodeGesture_StepAngles "
        "and advances the node's authored gesture animation state. Node+0x70 is "
        "only the elapsed timer for a selected-gesture transition: it is reset "
        "when the selected clip changes and expires after the shared 0.2-second "
        "constant; it is not a vertical-aim value.",
    ),
    0x141FFA6E0: (
        "AnimNodeGesture_GenerateAnimCommands",
        "Generates the gesture node's animation commands from the cached angles, "
        "selected authored clips and transition/blend state. During a clip "
        "transition it emits old/new selections and command 7 with "
        "clamp(node+0x70 / 0.2, 0, 1); this is generic temporal blending.",
    ),
    0x141FFA900: (
        "AnimNodeGesture_EmitSelectedAnimCommands",
        "Emits one to three selected gesture clips through "
        "AnimNodeGesture_EmitSelectedClipCommand and combines them with command "
        "7 according to the authored gesture-grid interpolation weights.",
    ),
    0x141FFEA20: (
        "AnimNodeGesture_EmitSelectedClipCommand",
        "Resolves one selected gesture-grid entry at AnimNodeGesture+0x60 and "
        "emits its exact six-word command-6 record (0x60006), including the "
        "ArtItemAnim pointer, destination lane and phase.",
    ),
    0x141FFE100: (
        "AnimNodeGesture_SelectGestureAnims",
        "Selects authored gesture animation clips from the cached gesture grid "
        "using the live angle state prepared by StepAngles.",
    ),
    0x141FD1C20: (
        "Gesture_ResolveJointReferenceTransform",
        "Builds one gesture joint-reference transform. It resolves an authored "
        "joint when present, otherwise calls the optional joint-reader provider "
        "through vtable slot +8. This is distinct from the Gesture::Target slot-0 "
        "query performed directly by Gesture_GetComputeArgs.",
    ),
    0x141FD1070: (
        "GestureConfig_Get",
        "Exact source-asserted Gesture::Config::Get. Returns the process-global "
        "g_GestureConfig_Ptr and asserts if the game has not installed a config. "
        "The stock singleton uses GestureConfig_Base_vftable.",
    ),
    0x141FD2F40: (
        "GestureRegistry_ResolveGestureSettings",
        "Resolves an authored gesture settings object from the global gestures "
        "registry. GestureLayer and AnimNodeGesture callers use its target type, "
        "grid settings and behavior flags; this is asset metadata, not a live "
        "camera read.",
    ),
    0x141FD3180: (
        "Gesture_GetComputeArgs",
        "Exact source-asserted Gesture::GetComputeArgs. Requires pOwner and "
        "pTarget and calls target vtable slot 0 directly. It transforms the raw "
        "world target into the owner/reference-local basis built from Player "
        "+0x110/+0x120/+0x130/+0x140, stores that local vector at "
        "Gesture::ApplyJointReaderFunc+0x30, installs the closure at "
        "ComputeArgs+0x70, and returns the 144-byte cached record reused by "
        "live post-align updates.",
    ),
    0x141FD3680: (
        "Gesture_GetComputeArgs_BuildStepState",
        "Gesture::ApplyJointReaderFunc vtable slot +0x8, invoked by "
        "AnimNodeGesture_StepAngles. Reads the cached reference-local target at "
        "closure+0x30, resolves the current joint/reference transforms through "
        "0x141FD21D0/0x141FD25B0 and allocates the 144-byte per-step gesture "
        "solver state.",
    ),
    0x141FD3860: (
        "GestureApplyJointReaderFunc_ScalarDeletingDestructor",
        "Gesture::ApplyJointReaderFunc vtable slot 0. Restores the exact RTTI "
        "vtable and conditionally frees the 64-byte closure allocated by "
        "Gesture_GetComputeArgs.",
    ),
    0x141FD38B0: (
        "Gesture_ComputeAnglesFromTargetAndReference",
        "Sole direct caller is AnimNodeGesture_StepAngles. Computes the two "
        "cached gesture angles and accompanying transform from the resolved "
        "target, joint/reference transforms and authored limits. StepAngles "
        "does not pass the optional ComputeArgs+0x18 muzzle-reference pointer.",
    ),
    0x1406F3510: (
        "GestureMgr_CreateTargetProvider",
        "Constructs the exact target provider for a gesture: TargetLookAt for CRC "
        "0xA399FA92, TargetAimAt for CRC 0xE83A8507, or TargetScriptLambda. The "
        "native owner weak cell/id are copied from owner+0xA8/+0xBC. A zero or "
        "other settings target type with no script callback returns false; the "
        "caller may then use its plain TargetPoint fallback.",
    ),
    0x1406F26D0: (
        "GestureTargetAimAt_GetTarget",
        "Gesture::TargetAimAt vtable slot 0. Resolves and generation-validates its "
        "exact owner from target+0x8/+0x10, then returns the world target built by "
        "BuildOwnerDirectionVectorFromStateSlotsAndDescriptor.",
    ),
    0x1406F2FE0: (
        "GestureConfig_QueryOptionalAimWeaponMuzzleReference",
        "GestureConfig vtable slot +0x18. Only for AimAt CRC 0xE83A8507 and a "
        "view/context-valid owner, resolves Player_GetActiveWeapon and "
        "ProcessWeapon_ResolveMuzzleTransform and returns an optional vector. "
        "Gesture_GetComputeArgs stores it at ComputeArgs+0x18, but the direct "
        "StepAngles solver path never consumes that field.",
    ),
    0x1406F2360: (
        "GestureTargetLookAt_GetTarget",
        "Gesture::TargetLookAt vtable slot 0 with ABI Vec4*(provider, out). "
        "Resolves provider+0x8/+0x10 weak owner/generation and calls Player vtable "
        "slot +0x908, with an NPC fallback, before rotating the owner-relative "
        "point by owner+0x120 and adding world translation owner+0x110. The "
        "result is captured when the Gesture ComputeArgs cache is created or "
        "rebuilt; later frame epochs reuse the saved closure target.",
    ),
    0x14211EF90: (
        "Gesture_GetDesiredMaxBlend",
        "Native handler for StringId 0x4666093B gesture-get-desired-max-blend. "
        "Validates an AnimNodeGesture child and returns its cached desired blend.",
    ),
    0x141FFF200: (
        "AnimNodeGesture_GetDesiredMaxBlend",
        "Returns the generation-current Gesture cache +0x2A0 desired blend, "
        "falling back to 1.0 when no current cache is available.",
    ),
    0x1420F25E0: (
        "AnimNodeGestureCache_WriteLookAdditiveBlend",
        "Writes Gesture cache+0x2A0 as controller fade multiplied by one minus "
        "the maximum regular-layer disable-look-additives property weight.",
    ),
    0x141E68EB0: (
        "GestureController_UpdateLookAdditiveBlend",
        "GestureController vtable +0x50 update path and sole caller of the exact "
        "AnimNodeGesture cache desired-look-blend writer.",
    ),
    0x1420F7DD0: (
        "GestureLayer_QueryDisableLookAdditivesWeight",
        "GestureLayer property query for disable-look-additives 0x5B3C7C02; "
        "returns smoothstep(layer+0x270) for the matching property.",
    ),
    0x1420F6700: (
        "BaseGestureLayer_QueryDisableLookAdditivesWeight",
        "Base gesture-layer property-query vtable slot. Its stock implementation "
        "returns zero, so it cannot suppress fixed LookAt additives.",
    ),
    0x1406F2530: (
        "GestureTargetLookAtOrCamera_GetTarget",
        "Gesture::TargetLookAtOrCamera vtable slot 0. Resolves the owner and, "
        "when its component gate is present, calls owner vtable slot +0xE90. "
        "The only ordinary constructor xref is the PlayerFlashlightController "
        "torch-look-strafe gesture path, so this is not a generic firearm seam.",
    ),
    0x1410D4E70: (
        "Player_GetLookAtTargetPoint",
        "Player vtable slot +0x908. Chooses the first active authored look-target "
        "slot at Player+0x21E0/+0x2260; otherwise returns a point in front of the "
        "Player from its own world transform. It has no screen-route lookup.",
    ),
    0x1410D5010: (
        "Player_GetLookAtOrCameraTargetPoint",
        "Player vtable slot +0xE90. Chooses the same authored look-target slots; "
        "its fallback reads Player+0x4010, resolves that screen route through "
        "0x14015BF00 and builds a forward world target. The sole ordinary "
        "TargetLookAtOrCamera constructor is flashlight-only.",
    ),
    0x1410C6A90: (
        "PlayerLookAtTracker_IsActive",
        "PlayerLookAtTracker vtable slot 0. Returns tracker+0x70, the active "
        "flag tested by Player_GetLookAtTargetPoint for the two embedded "
        "records at Player+0x21E0/+0x2260.",
    ),
    0x1410F40D0: (
        "PlayerLookAtTracker_SetTargetPoint",
        "PlayerLookAtTracker vtable slot +0x8. Copies the incoming world point "
        "to tracker+0x60, sets tracker+0x70 active and refreshes owner-side "
        "look state through PlayerStrafeLookAt_UpdateOwnerTargetBasis.",
    ),
    0x1410F4110: (
        "PlayerLookAtTracker_ClearTarget",
        "PlayerLookAtTracker vtable slot +0x10. Clears tracker+0x70 and informs "
        "the owning PlayerLookAtController about the tracker command id.",
    ),
    0x1401B6F70: (
        "LookAtTracker_SetDirectPoint",
        "Stores an authored/direct point at tracker+0x10, clears object, attach "
        "and callback sources, then calls virtual slot +0x8. For "
        "PlayerLookAtTracker that copies the same point to +0x60 and activates "
        "the record.",
    ),
    0x1401B7020: (
        "LookAtTracker_UpdateResolvedTarget",
        "Shared ILookAtTracker update. Resolves callback, object joint/attach or "
        "direct-point sources and publishes the resulting world point through "
        "virtual slot +0x8; clears invalid sources through slot +0x10.",
    ),
    0x141000BC0: (
        "PlayerStrafeLookAt_ResolveReferencePoint",
        "Returns an authored override when available; otherwise resolves the "
        "owning Player's screen route from Player+0x4010 at exact resolver "
        "return site 0x141000C30 and builds a point from the camera quaternion "
        "and targHead attachment. This feeds authored non-ADS LookAt Gestures; "
        "the ordinary ADS gesture is independently targetless. Runtime build z "
        "replaced this exact seed from the peer in both directions without "
        "changing the visible shared head direction, so it is not a sufficient "
        "visible correction seam.",
    ),
    0x141001570: (
        "PlayerStrafeLookAt_BuildDefaultCameraRequest",
        "Builds the default PlayerLookAtTracker request. Reads the owner screen "
        "route at Player+0x4010 at exact resolver return site 0x1410015C5, "
        "compares its camera-forward basis with the reference direction and, "
        "when eligible, returns a direct-point request whose point was built by "
        "PlayerStrafeLookAt_ResolveReferencePoint. Runtime build z reached and "
        "replaced both exact pose-seed sites in both processes with no visible "
        "head-direction change; trace downstream weight/overwrite instead.",
    ),
    0x141000DA0: (
        "PlayerStrafeLookAt_Update",
        "Updates the Player+0x21E0 PlayerLookAtTracker from ScriptMove, rope, "
        "authored target and default camera branches. The ordinary fallback "
        "calls PlayerStrafeLookAt_BuildDefaultCameraRequest and publishes its "
        "camera-derived point through LookAtTracker_SetDirectPoint. This path is "
        "statically real but runtime-rejected as the sufficient visible owner "
        "after exact peer-seed replacement at both fallback calls.",
    ),
    0x140F027A0: (
        "PlayerMove_UpdateStrafeLookAt",
        "Thin ordinary Player::Move helper that calls PlayerStrafeLookAt_Update "
        "on Player+0x3230. PlayerMoveStick_MainUpdate invokes it unconditionally "
        "in the normal Move update path.",
    ),
    0x1410F4180: (
        "PlayerStrafeLookAt_UpdateOwnerTargetBasis",
        "Refreshes targHead/world-basis state and cover-dependent owner data "
        "after PlayerLookAtTracker_SetTargetPoint publishes a new target.",
    ),
    0x1420F2DC0: (
        "GestureLayer_BuildComputeArgsForNode",
        "Builds the per-node inputs used by the gesture cache-create path and "
        "calls Gesture_GetComputeArgs with the node's exact owner and target "
        "provider. PreAlign reaches this only when the old node+0xD8 cache slot "
        "has its sign bit set.",
    ),
    0x1420F3F50: (
        "GestureLayer_PreAlignUpdatePerGestureNode",
        "GestureLayer per-node pre-align update path. It first uses dispatch "
        "context+0x10 as a custom target; when that is null it asks GestureConfig "
        "for the settings target provider, and on failure constructs a plain "
        "Gesture::TargetPoint before building ComputeArgs. ComputeArgs/provider "
        "creation is gated by the old node+0xD8 cache-slot sign bit. The stock base ADS "
        "node 0xA6448 has target type zero and no target callback, while its "
        "BaseLayer context is null, so this path supplies the plain fallback, "
        "not LookAt, AimAt or a camera provider.",
    ),
    0x1420F3CA0: (
        "GestureLayer_DispatchPreAlignToAnimNodeGesture",
        "Hashes AnimNodeGesture, iterates matching nodes in the selected graph "
        "and dispatches a copied 144-byte pre-align context to "
        "GestureLayer_PreAlignUpdatePerGestureNode.",
    ),
    0x1420F4700: (
        "GestureLayer_PostAlignUpdatePerGestureNode",
        "Live post-align callback for one AnimNodeGesture. For a non-negative "
        "node+0xD8 cache slot it finds the saved 144-byte ComputeArgs record and "
        "calls AnimNodeGesture_StepAngles at 0x1420F488C; StepAngles then guards "
        "the shared frame epoch and consumes the cached target closure.",
    ),
    0x1420F6B30: (
        "BaseLayerGestureLayer_PreAlignUpdate",
        "BaseLayerGestureLayer vtable slot +0xD8. Builds the AnimNodeGesture "
        "dispatch context with context+0x10 explicitly null, so the live base "
        "layer does not provide an embedded custom TargetPoint.",
    ),
    0x1420F96E0: (
        "GestureLayer_PreAlignUpdate",
        "GestureLayer vtable slot +0xD8. Unlike BaseLayerGestureLayer, can place "
        "its embedded object+0x1C0 Gesture::TargetPoint into dispatch "
        "context+0x10 when object+0x240 enables that authored override.",
    ),
    0x1420F3AB0: (
        "AnimNodeGesture_GetGestureGridBounds",
        "Reads the four authored animation-grid bounds from the cached grid at "
        "AnimNodeGesture+0x60: min/max X at +0x7FC/+0x800 and min/max Y at "
        "+0x804/+0x808. These fields are asset bounds, not live camera direction.",
    ),
    0x141FFA020: (
        "AnimNodeGesture_TrySnapshotNode",
        "Source-identified AnimNodeGesture snapshot path. Validates the "
        "anim-node-gesture descriptor, copies authored gesture id from +0x4 and "
        "target script callback from +0x18 into node+0xD0/+0xE8, resolves final "
        "settings id at node+0xD4, and builds the cached grid at node+0x60. "
        "Node+0xE8 is authored bytecode, not live camera state.",
    ),
    0x141FF9A50: (
        "AnimNodeGesture_Construct",
        "AnimNodeGesture constructor. Installs AnimNodeGesture_vftable and "
        "initializes node+0xE8 to null before an authored descriptor can supply "
        "the optional target script callback during TrySnapshotNode.",
    ),
    0x141ECDC20: (
        "GestureTargetScriptLambda_GetTarget",
        "Gesture::TargetScriptLambda vtable slot 0. Generation-validates its "
        "owner and supplies *(*(owner+0x1A0)+0x58) as the sole script argument. "
        "The stock anim-player prone callback dereferences argument+0x10 and "
        "returns the address of the inline 16-byte vector at state-data+0x5D0, "
        "which this getter copies to its output. It performs no direct native "
        "camera/global-input read.",
    ),
    0x14011D230: (
        "RegisterAnimNodeGestureSnapshotFactory",
        "Registers the anim-node-gesture StringId with the snapshot factory "
        "that reaches AnimNodeGesture_TrySnapshotNode.",
    ),
    0x141D7B860: (
        "GestureGridCache_FindOrBuild",
        "Finds an existing gesture animation-grid cache entry or builds it through "
        "GestureGridCache_BuildFromAnimationClips for AnimNodeGesture consumers.",
    ),
    0x141D7BD70: (
        "GestureGridCache_BuildFromAnimationClips",
        "Builds a gesture animation grid from AnimationDatabase clips and computes "
        "its authored min/max X/Y bounds at +0x7FC/+0x800/+0x804/+0x808. This "
        "rejects interpreting those fields as a live local-camera vector.",
    ),
    0x14217B480: (
        "AnimCmdEvaluator_InitializeAuxiliaryEncodedScalars",
        "Command-15 auxiliary initializer. Builds encoded bases from "
        "SegmentContext+0x60, the command payload, SegmentContext+0x68 and "
        "skeleton-authored data, then calls AnimJointProgram_CopyEncodedScalars "
        "over group-authored tuple tables. Exact Chloe/Nadine group-0 tuples "
        "initialize only the auxiliary prefix; they do not author the workspace "
        "matrix-chain root, which command 15 leaves as identity.",
    ),
    0x14217AA60: (
        "AnimWorkBuffer_FindAllocationIndexByOffset",
        "Read-only exact-start lookup in WorkBuffer+0x28 pointer table. "
        "Input is arena-relative offset, not a pointer. Scans the front "
        "prefix (count+0x48) if offset<cursor+0x30, else the back suffix "
        "(count+0x4C) when offset>=cursor+0x38. Record+0 must equal offset; "
        "interior addresses fail. Returns table index or 0xFFFFFFFF. Native "
        "records are 56 bytes: offset,size,first,count,flags,type,nextFree.",
    ),
    0x14217A4D0: (
        "AnimWorkBuffer_CoalesceAdjacentAllocationsAndGetSize",
        "NOT a read-only allocation-size query. Calls exact offset lookup, "
        "then coalesces compatible adjacent records: adds bytes/count at "
        "0x14217A59A/A5A5, recycles records through +0x20 free list and "
        "compacts the pointer table/counts at A5FA/A64F. Runtime admission "
        "must not invoke this to check extent; use a bounded read-only "
        "metadata walk. P99 shadow expects JP type0, matrices type3, aux type5.",
    ),
    0x14217B630: (
        "AnimCmdEvaluator_ExecuteSkeletonJointPrograms",
        "Command-16 evaluator that walks the target skeleton's bounded joint "
        "program records and dispatches their transform operators over the final "
        "WorkBuffer JointParams into the 64-byte-per-joint matrix workspace. The "
        "outer operator context is {programStart, ownerBase, null, baseTable}; its "
        "selector-3 baseTable is the nested array {SegmentContext+0x50 JointParams, "
        "+0x58 workspace, +0x68 auxiliary, +0x30 scratch}. Built-in payload word 1 "
        "normally selects that nested table, while payload word 2 selects authored "
        "descriptors from programStart. The current group descriptor is read from "
        "+0x38. "
        "STATIC selector: raw=Segment+28 at14217B64F; group=Segment+38 "
        "at14217B656; directory=raw+u32(raw+34), first=u16(group+6), "
        "count=u8(group+F), each program=raw+u32(directory+8*index+4). "
        "Directory u16+2 is an offline capture extent, NOT this executor's "
        "native loop bound (operator termination controls execution). Cmd24 "
        "alone does not prove this cmd16 path was reached. Runtime build jt "
        "confirmed that the finalized parameterCount prefix remained bit-identical "
        "while complex authored programs changed the +0x58 workspace. Runtime ke "
        "corrected that scope: command 16 rewrites selected JointParams rows in "
        "the command-15 expanded tail, which the old prefix hash did not cover. "
        "The executor itself performs no JointParams preamble store; the writes "
        "must occur in dispatched operators. STATIC C39 g0/g1 shadow contract: "
        "SegmentContext is 0x80 bytes, dispatcher never writes it and all 53 "
        "pinned operators use nested bases (not ownerBase). Custom132 reads "
        "context+0x4A flags; no admitted worker retains the JP/context pointer. "
        "Preserve original status pointer: status is always set to zero, even "
        "when an operator was skipped, so it is not coverage evidence. Copy "
        "all 104/38 expanded rows for g0/g1. Replacing only shadow-context+0x50 "
        "preserves native allocator ownership; never replace original+0x50 "
        "because cmd5 frees it. This does not establish owner lifetime or "
        "arbitrary terminal callback safety. STATIC P99: owner-neutral executor "
        "also called at141AB7F74/14217DE27; C39 identity alone does not admit "
        "retained local Nadine. Normal Async group0 call141A92E78 and actual "
        "cmd16 return141ABA978 define the candidate procedural read window; "
        "command suffix, owner callbacks and render palette selection remain. "
        "STATIC ABI: exactly two input arguments RCX=status,RDX=Segment; "
        "entry overwrites R9 at217B64F and R8 at217B66D before using either. "
        "Returns original status pointer at217B7A5 and zeros *status at7A8. "
        "Process reloads R10/R11 aftercall then parser reloads EDX/R8/R9. "
        "Ordinary compiled two-argument wrapper can preserve the call ABI; "
        "do not move native prologue or treat this as live capture admission.",
    ),
    0x14217BF30: (
        "AnimCmdEvaluator_ExpandJointParamsToTargetWorkspaceCount",
        "Command-15 helper. Replaces an owned SegmentContext+0x50 allocation "
        "with a 48*targetGroup.workspaceCount block at the same WorkBuffer offset "
        "and republishes the same base. The allocation is tagged with target-group "
        "first/count, but this helper does not copy or zero the newly exposed tail. "
        "Chloe group 1 expands 1 to 62 records; Nadine group 2 expands 96 to 119. "
        "Runtime ke confirmed identical before/after bases, finalized prefixes and "
        "target-tail rows in 12/12 samples per actor; earlier command-16 operators "
        "then rewrite all five inspected tail rows before their opcode-40 reads.",
    ),
    0x142194C30: (
        "AnimJointProgramOp54_CopyEncodedScalars",
        "Built-in joint-program opcode 54 wrapper. payload[0] is the scalar-copy "
        "count; payload[1] resolves the four-pointer base table (normally operator-"
        "context selector 3), and payload[2] resolves the authored pair list from "
        "operator-context selector 0 or 1. It forwards those inputs to "
        "AnimJointProgram_CopyEncodedScalars. Runtime build ju decoded every live "
        "Chloe/Nadine tuple as base 2 -> base 3 shared scratch; no captured opcode-"
        "54 instance wrote workspace or JointParams.",
    ),
    0x142189370: (
        "AnimJointProgram_CopyEncodedScalars",
        "Copies each authored {destination, source} pair as one raw 32-bit word. "
        "Each encoded operand's low two bits select JointParams, workspace, the "
        "SegmentContext+0x68 base, or shared scratch; remaining bits are the byte "
        "offset. Thus a base-2/3 to base-1 tuple can change workspace without "
        "mutating JointParams, although runtime build ju found only base-2 to base-3 "
        "tuples in the live Chloe/Nadine programs.",
    ),
    0x142194470: (
        "AnimJointProgramOp40_PropagateJointMatrices",
        "Built-in joint-program opcode 40 wrapper. Payload[0] counts four-entry "
        "descriptor blocks, payload[1] normally resolves selector 3 offset 0 to "
        "the evaluator's four-base table, and payload[2] resolves authored "
        "{k, encodedSource} records. Runtime build jv isolated this opcode as the "
        "common direct writer of the target Chloe/Nadine workspace matrices. Its "
        "descriptor k values encode target-group-local rows, not skeleton-global "
        "joint IDs.",
    ),
    0x142189FB0: (
        "AnimJointProgram_PropagateJointMatrices",
        "Processes 4*blockCount authored {k, encodedSource} records. For each, "
        "combines the 48-byte JointParams record at base[0]+3*k-0x30 with the "
        "selected 64-byte parent/source matrix and stores four float4 rows at "
        "workspace base[1]+4*k. Live k values are 16*(localRow+1), so the "
        "JointParams row is k/16-1 and skeleton-global joint is the current "
        "target group's first joint plus that local row. Runtime jv destinations reproduce "
        "the exact first changed workspace words 448 for Chloe and 704 for Nadine. "
        "Runtime jx proved all four parent outputs change inside these earlier "
        "calls, while subsequent Chloe-puppet op49/op50 leave joint 42 unchanged. "
        "The descriptor loop is strictly sequential (advance at 0x14218A04C, "
        "next record at 0x14218A1E0), so same-call local-row chains "
        "scratch->24->25->26 and 35->36->41->42 consume the earlier record's "
        "completed output. Runtime jz classified the inputs: Nadine local "
        "24/25/26 and Chloe local 42 are source-driven, Nadine local 96/102 are "
        "JointParams-driven, and Chloe local 36/41 "
        "change in both inputs. Exact PAK decoding further resolves the complete "
        "group-0 chains: Chloe W+040/+680/+6C0/+700/+740/+780/+7C0/+840/+8C0/"
        "+940 and Nadine W+040/+3C0/+400/+440/+480/+4C0/+500/+540. An exhaustive "
        "destination audit finds no non-op40 overwrite of these active nodes. "
        "Therefore this worker is downstream of a mixed contamination and is not "
        "a valid blanket substitution boundary. STATIC 2026-09-20 P99 "
        "consumer audit: pinned C39 group0 op40 operators46/48 consume local "
        "palm28/prop30 and write W+740/+7C0; prop parent749 resolves palm740. "
        "Group1 consumes all19 selected right-finger odd locals1..37 in "
        "parent-before-child order; five roots1/9/17/25/33 use scratchFDB, "
        "other parents are the preceding same-finger matrix. Final records38/39 "
        "repeat local37 as SIMD padding. No later program matrix writer "
        "overlaps a selected output, excluding those exact padding repeats. "
        "This closes consumer topology, not numerical/skin correctness. "
        "STATIC profile reconstruction: workspace stores3 affine rows "
        "PLUS separate local scale S, not an ordinary4x4. For parent(A,t,S) "
        "and local(R,T,s): Anew=A*diag(flag8?1:S)*R; "
        "tnew=A*(S*T)+t; Snew=s. Do not normalize Q here. Flag8 is set "
        "on all71 required row124 joints except2/14. Authored-bind replay "
        "times authored inverse gives max identity residual3.187e-7. "
        "STATIC 2026-09-27 pistol-base: all7 pinned clips author unitS/identityQ/zeroT "
        "at localJP0; dynamic scatter padding never targets its48 bytes. Sole "
        "skeleton programBB0 has noJP0 overwrite; root op40BE6 repeats4 padding "
        "records with parent0009, reserved identity row0, and no later root "
        "matrix write. Command15 seeds reserved row0 at141ABA91D..A956. Root "
        "workspace therefore preserves authored root; no claim about arbitrary "
        "runtime callbacks, other sources or cross-consumer generation equality. "
        "STATIC C39 group1 finger slice: program5CD70 has only op40/53. "
        "Even104..140 are left fingers rooted atpalm27, odd105..141 are right "
        "fingers rooted atpalm28. Five independent right chains105/113/121/129/137 "
        "advance by2; only same-chain descendant matrices change after localQ "
        "perturbation. Group1 never writes JP or upstream palm28/prop30. "
        "This is not a proof of downstream procedural helper poses. "
        "STATIC unit-scale conditional geometry extension: g0 wrist26<-24 "
        "(op44 parent0649), palm28<-26 (op46 parent06C9), prop30<-28 "
        "(op48 parent0749) are untouched local parameters and have no "
        "non-op40 matrix overwrite. Here op44/46/48 mean program ordinals. "
        "Thus WristProp=PalmProp*inverse(LocalPalm28) reconstructs wrist26 "
        "in the normalized unit-scale D350/B8 model while preserving all21 "
        "previous prop-relative frames. This is not a general scaled workspace "
        "inverse or proof of actual live wrist/weapon alignment.",
    ),
    0x142194680: (
        "AnimJointProgramOp41_ScatterQuaternionEulerDegrees",
        "Built-in joint-program opcode 41 wrapper. Resolves 64-byte authored "
        "records from outer selector 0 and the four encoded destinations through "
        "the nested base table selected by payload word 1, then calls "
        "AnimJointProgram_ScatterQuaternionEulerDegrees. Each record can write up to "
        "twelve optional encoded scalar destinations.",
    ),
    0x14218B780: (
        "AnimJointProgram_QuaternionSoAToEulerDegrees",
        "STATIC four-lane quaternion to Euler XYZ degrees, with authored modes "
        "0..5. Pinned mode0 inverses Qy*Qx*Qz and mode4 inverses Qz*Qy*Qx; "
        "do not confuse output Y with mode0 middle rotation X. At B9E3..BA21 "
        "abs(s)>=float32 .99999 selects singular path and asin input is clamped "
        "to [-1,1]. Singular mode0 forces Z=0, mode4 X=0. No Q normalization. "
        "Caller14218BF8C; analytic Python modes0/4 are not native numeric equivalence.",
    ),
    0x14218BC40: (
        "AnimJointProgram_ScatterQuaternionEulerDegrees",
        "Op41 worker reads four local Q and authored A/B per64-byte record. "
        "BDC7..BF78 forms conj(A)*Q*conj(B), then calls14218B780 atBF8C. "
        "Scatters up to12 optional XYZ-degree destinations, FFFF skips, no "
        "normalization. Nadine g0@59404 lane3 Q24 mode0 writes ONLY Y->AUX314; "
        "lane5 Q51 mode4 writes AUX2B0/31C/380. Earlier generic scatter name "
        "was storage-level only, not the numerical operation.",
    ),
    0x1421946D0: (
        "AnimJointProgramOp44_AssembleEulerQuaternions",
        "Built-in joint-program opcode 44 wrapper. Resolves 64-byte authored "
        "records from outer selector 0 and encoded destinations through the nested "
        "base table selected by payload word 1, then calls "
        "AnimJointProgram_AssembleEulerQuaternions. Chloe payload 0xF142 is the "
        "last operator before consumer 0xF14A and writes row 41 quaternion at "
        "nested selector-0 JointParams destination +0x7C0.",
    ),
    0x14218C200: (
        "AnimJointProgram_EulerDegreesSoAToQuaternion",
        "STATIC four-lane Euler XYZ degrees to Q, authored modes0..5. "
        "Pinned mode4 degrees->half radians C2D4..C30E, sincos1421803E0, "
        "masks form Qz*Qy then call1421817F0@C4BE right-multiplies Qx. "
        "Mode3 masks C25B..C2D0 select (Qx*Qz), then C4BE right-multiplies Qy. "
        "Pinned g6 local33/34/51 use mode3; modes3/4 analytically replayed. "
        "Caller14218C7B0. No normalization or arbitrary mode generalization.",
    ),
    0x14218C530: (
        "AnimJointProgram_AssembleEulerQuaternions",
        "Op44 worker reads12 encoded XYZ-degree scalars per64-byte record, "
        "calls14218C200 atC7B0, forms A*EulerQ*B atC7D1..C9D7 and stores4 Q. "
        "No normalization. Nadine mode4 Q87@594C6 lane2 consumesAUX2E8/354/3B8, "
        "Q88@594A6 lane7 consumes2EC/358/3BC. Four stores are quaternions, NOT "
        "a semantically assembled workspace matrix. C39 group6 parents remain "
        "535=0809,536/537=12DB,554=0CC9. Pinned g6 mode3=Qx*Qz*Qy for "
        "local33/34/51; local51 lanes29/30/31 identical overwrites, not sums. "
        "Conditional analytic helper slice is closed, actual source pose is not.",
    ),
    0x142194720: (
        "AnimJointProgramOp47_ApplyScalarCoefficients",
        "Built-in joint-program opcode 47 wrapper. Payload[3] selects the generic "
        "or arity-1/2/3/4 scalar-coefficient worker; payload[1] resolves the nested "
        "base table and each four-lane batch writes four encoded scalar "
        "destinations. Chloe payload 0xF04C writes row-36 words 8 and 10.",
    ),
    0x14218CAC0: (
        "AnimJointProgram_ApplyScalarCoefficients_Generic",
        "Generic opcode-47 scalar-coefficient worker selected when payload arity "
        "is outside 1..4. Processes four encoded scalar lanes per batch.",
    ),
    0x142192B10: (
        "AnimJointProgram_ApplyScalarCoefficients_Arity1",
        "Opcode-47 one-coefficient scalar worker; writes four encoded scalar "
        "destinations per batch. Adds the polynomial of the source scalar "
        "to the OLD destination; dependency analysis must retain this "
        "read-modify-write edge. SIMD batch reads precede stores.",
    ),
    0x142192D30: (
        "AnimJointProgram_ApplyScalarCoefficients_Arity2",
        "Opcode-47 two-coefficient scalar worker; writes four encoded scalar "
        "destinations per batch. Adds the polynomial of the source scalar "
        "to the OLD destination; dependency analysis must retain this "
        "read-modify-write edge. SIMD batch reads precede stores.",
    ),
    0x142192FD0: (
        "AnimJointProgram_ApplyScalarCoefficients_Arity3",
        "Opcode-47 three-coefficient scalar worker; writes four encoded scalar "
        "destinations per batch.",
    ),
    0x1421932F0: (
        "AnimJointProgram_ApplyScalarCoefficients_Arity4",
        "Opcode-47 four-coefficient scalar worker; writes four encoded scalar "
        "destinations per batch.",
    ),
    0x142194880: (
        "AnimJointProgramOp49_TransformEncodedPoints",
        "Built-in joint-program opcode 49 wrapper. Payload word 1 resolves the "
        "nested four-base table and word 2 resolves authored streams from outer "
        "selector 0. Calls weighted point-transform14218D570 at1948EE, "
        "optional homogeneous normalization14218D800 at194900, then optional "
        "14218E120. These are the primary plus two optional "
        "auxiliary writer phases selected by the authored header. Target-stream "
        "payloads Chloe 0xF03C and Nadine 0x8F4C rewrite the translation vector of "
        "Chloe row 35 and Nadine rows 96/102 respectively.",
    ),
    0x14218D570: (
        "AnimJointProgram_AccumulateWeightedTransformedPoints",
        "STATIC primary op49 worker: encoded source0 skips; affine point "
        "A*(localS*xyz)+translation, append W1, multiply scalar weight. Source "
        "flag8 replaces destination; otherwise add OLD float4. Source localS "
        "at+30 participates, unlike op50 sourceA. Nadine g6@60E5A lane3 "
        "F5B->JP980 uses g0matrix26, pointAUX3C/4C/5C=zero, weightAUX2C=1: "
        "T553=(t26,1), followed by homogeneous normalization. Callers1948EE/190C72.",
    ),
    0x14218D800: (
        "AnimJointProgram_NormalizeHomogeneousVectors8",
        "STATIC optional op49 auxiliary: eight encoded float4 operands are "
        "all read before stores. Each vector divides by its OWN W using rcp "
        "+one Newton step; not quaternion normalization or pairwise blending. "
        "g6 base553 stream listsJP980 five times but all receive same normalized "
        "snapshot, not five cumulative transformations. Callers194900/190C84.",
    ),
    0x142190C30: (
        "AnimJointProgram_ProcessTransformedPointStreams",
        "STATIC direct stream sequencer: count a1, bank table a2, descriptor a3. "
        "Header=a3+12*count; primary14218D570 at190C72, optional header[0] "
        "eight-vector normalization14218D800 at190C84, optional header[2] "
        "14218E120 at190C96 with header-relative offset[1]. Independent layout "
        "agreement with builtin op49 wrapper142194880. Only dataxref146EDCAD4 "
        "observed; no gameplay caller or runtime role asserted.",
    ),
    0x14218E120: (
        "AnimJointProgram_WriteEncodedVectorBatches",
        "Optional opcode-49 four-output batch worker. Destination fields at "
        "+2/+6/+0xA/+0xE are encoded float4 addresses.",
    ),
    0x142194940: (
        "AnimJointProgramOp50_BuildRelativeRotationQuaternions",
        "Built-in joint-program opcode 50 wrapper. Payload word 1 resolves the "
        "nested four-base table and word 2 resolves authored streams from outer "
        "selector 0. Calls relative-basis quaternion accumulator14218F660 at "
        "1949AB and optionally tailcalls normalize/postmultiply142190910 at "
        "1949D4. Target-stream payloads Chloe "
        "0xF044 and Nadine 0x8F54 write the quaternion vector of Chloe row 35 and "
        "Nadine rows 96/102 respectively.",
    ),
    0x142181430: (
        "AnimJointProgram_BasisColumnsToQuaternion4",
        "STATIC four-lane basis->Q used by14218F660 at18FBE4. Trace SIGN bit "
        "selects W for positive zero/nonnegative; negative trace chooses largest "
        "diagonal with strict-less masks, ties X then Y then Z. Differs from "
        "scalar14218F3B0 strict-greater tie handling used by op51. Primary caller "
        "normalizes returned Q at18FC20..FC85. Analytic model not native rsqrt equivalence.",
    ),
    0x14218F660: (
        "AnimJointProgram_AccumulateRelativeRotationQuaternions",
        "Primary op50 four-lane rotation worker. Relative-basis columns: "
        "FA87..FBE4 normalize first x, z=normalize(x cross second), y=z cross x; "
        "142181430 converts basis, FC20..FC85 normalizes Q. FD12..FEA6 composes "
        "Qmatrix*(Qinput*authoredQ). FEDA..FFE8 stores oldQ*sign(dot(oldQ,newQ)) "
        "+weight*newQ; source-Q flag8 makes oldQ zero. Nadine Q85@594F6 uses "
        "A=W5C0 matrix22 first48B (not separateS+30), B=identityW0, LOCAL Q24 "
        "JP490 flag8, weightAUXBC=1 and authored identity. General B scale "
        "semantics are not numerically modeled by the pinned Python kernel. "
        "g6@60E62 lane3 (ED3,1,25B,970) instead uses sharedg0matrix24 andLOCALQ26 "
        "with independently pinned identity corrections/weight1, producingQ553.",
    ),
    0x142190910: (
        "AnimJointProgram_NormalizeAndPostMultiplyQuaternions4",
        "Op50 auxiliary four-lane worker. 1909FE..A80 norm2 and rsqrt+twoNewton "
        "steps normalize each input Q, then RIGHT multiply authored Q; no "
        "normalization after authored product. Nadine Q85 authored auxiliary Q "
        "is identity, yielding normalize(QorthonormalizedShoulder22*LOCAL_Q24). "
        "Do not omit this phase when analyzing primary14218F660 output.",
    ),
    0x1421949E0: (
        "AnimJointProgramOp51_BuildAimQuaternions",
        "Built-in joint-program opcode 51 wrapper. Initializes encoded vector "
        "destinations as weighted target positions, then optionally dispatches "
        "encoded aim/up quaternion construction through the nested table "
        "selected by payload word 1. Earlier BlendEncodedVectors name was "
        "too broad and misleading: the auxiliary worker is not ordinary "
        "quaternion interpolation. Chloe "
        "payload 0xF10C writes row 36 quaternion at nested selector-0 JointParams "
        "destination +0x6D0 in both phases.",
    ),
    0x14218D2D0: (
        "AnimJointProgram_InitializeEncodedVectors",
        "Opcode-51 primary worker. Consumes a four-byte tuple per output and "
        "writes encoded matrix translation(xyz,1)*scalarWeight to float4 "
        "destination, replacing when source flag8 is set and adding otherwise. "
        "This is temporary target POSITION data, not quaternion interpolation. "
        "Also called by opcode48 and other wrappers. Pinned C39 g0 op51 "
        "at59546 copies matrix26 position with AUXC0=1 into JP1030 (Q86 slot) "
        "before the auxiliary aim solver replaces it with a quaternion.",
    ),
    0x1421902D0: (
        "AnimJointProgram_BuildAimQuaternions",
        "STATIC opcode51 auxiliary aim/up solver, not a generic vector blend. "
        "Pinned C39 g0 op51@59546 lane2: temporary target JP1030 from matrix26, "
        "parent matrix85 atW1580, localT86 atJP1040; full inverse of parent "
        "A*diag(S) maps target-position minus parent translation into parent "
        "coordinates, then subtracts localT86. Up is matrix24 A*diag(S) "
        "times authored(0,1,0), transformed by the same parent inverse. "
        "Mode3 forms x=unitAim,y=orthogonalizedUp,z=x cross y; converts the "
        "basis through14218F3B0 and RIGHT-multiplies authored SoA lane2 Q "
        "(0,-0.7071067690849304,0,0.7071067690849304). Threshold142BE8B1C "
        "is1.19210000093517e-7. Degenerate-up branch normalizes "
        "(0,-x.z,x.y,x.x+1); antiparallel-X is degenerate, not an invented "
        "fallback axis. Scalar model is not native float32 equivalence or "
        "an actual pose. UpstreamQ85/Q87/Q88/Euler24 and fullg6 remain external.",
    ),
    0x14218F3B0: (
        "AnimJointProgram_BasisColumnsToQuaternion",
        "STATIC converts three packed float4 basis columns to one xyzw "
        "quaternion: positive-trace branch or largest-diagonal branch with "
        "strict comparisons (ties select Y, then Z). Called at1421907BB by "
        "the encoded aim solver and142190289 by another vector solver. "
        "Pure scalar basis tests cover identity and X/Y/Z half-turns; no "
        "native rsqrt/float32 error bound or arbitrary nonrigid basis claim.",
    ),
    0x1421947C0: (
        "AnimJointProgramOp48_WriteEncodedVectorOutputs",
        "STATIC built-in joint-program opcode48, table143026EA0. Calls "
        "14218D2D0 for primary vectors, optionally14218DA70 and14218E880 "
        "from its authored header counts/offset. Pinned C39 group4 program60760 "
        "ordinal4 has20 primary and20 final-batch vector destinations, all "
        "localJP. Including these writes excludes intervening shared-input "
        "overlap for group6 helpers535/536/537/554. This is a write inventory, "
        "not a numeric replay or arbitrary live alias/generation proof.",
    ),
    0x142194AB0: (
        "AnimJointProgramOp52_WriteAuthoredVectorsAndScalars",
        "Built-in joint-program opcode 52 wrapper. Payload words 0/1 count vector "
        "and scalar records, word 2 selects the nested four-base table, and word 3 "
        "selects authored descriptors from outer selector 0. It calls "
        "AnimJointProgram_WriteAuthoredVectorsAndScalars. Exact target streams "
        "begin with this opcode at Chloe payload 0xF032 and Nadine payload 0x8F42; "
        "it writes the scale and translation vectors of all five inspected "
        "JointParams rows before op49/op50 fill later components.",
    ),
    0x1421897C0: (
        "AnimJointProgram_WriteAuthoredVectorsAndScalars",
        "Opcode-52 worker. Its first loop copies four authored float4 values per "
        "16-byte descriptor to encoded vector destinations; its second loop copies "
        "four authored floats per descriptor to encoded scalar destinations. All "
        "destinations resolve through the nested base table, while sources are "
        "read only from the authored constant tail supplied by the wrapper. It "
        "reads no Player, camera or gameplay global.",
    ),
    0x142194B10: (
        "AnimJointProgramOp53_CopyEncodedVectors",
        "Built-in joint-program opcode 53. Each 16-byte descriptor batch contains "
        "four {destination, source} encoded-float4 pairs and copies four complete "
        "vectors directly. Exact group-0 streams export Chloe JointParams +0x670 "
        "and workspace +0x840/+0x940 into scratch +0x190/+0xD90/+0xE90; Nadine "
        "exports JointParams +0x400/+0x430 and workspace +0x500/+0x540 into "
        "scratch +0x160/+0x190/+0xD90/+0xDD0. STATIC 2026-09-20 P99: pinned "
        "C39 group0 op53 ordinal49 exports completed palm matrix+740..77F "
        "as four vectors into shared scratch+FD0..100F. Group1 right-finger "
        "roots use parentFDB: selector3, matrix offsetFD0 (mask~F at14218A055), "
        "flag8 disables parent-scale inheritance at14218A069. This is not "
        "a generic encoded-vector offset masked with~3. STATIC2026-09-27 "
        "group1 program5CD70 exports60 LOCAL JP vectors toscratch720..ADF, "
        "not descendant world matrices. RightQ exports:107->760,109->7C0, "
        "115->820,117->880,123->8E0,125->940,131->9A0,133->A00,137->A60, "
        "141->AC0. Nonexported Q105/111/113/119/121/127/129/135/139 changes "
        "leave all60 scratch exports unchanged with other inputs fixed. "
        "Only that shared-scratch route is excluded; arbitrary later/live "
        "helper dependencies and numeric replay remain unresolved. "
        "Later STATIC group6 buffer-read audit refines this boundary: targets "
        "535/536/537/554 read g0 exports, not720..ADF or group6 entryJP/AUX. "
        "Groups1..5 do not overwrite those exact shared leaves. Fixed-g0 "
        "finger-localQ edits leave the four helpers unchanged through this "
        "authored buffer graph only; numeric baseline remains unresolved.",
    ),
    0x142194620: (
        "AnimJointProgramOp59_ConvertJointParamsToMatrices",
        "Built-in joint-program opcode 59 wrapper. Resolves an optional authored "
        "joint-index list, a 48-byte JointParams source and a 64-byte matrix "
        "destination from the four encoded bases, then calls "
        "AnimJointProgram_ConvertJointParamsToMatrices.",
    ),
    0x14218B5F0: (
        "AnimJointProgram_ConvertJointParamsToMatrices",
        "Deterministically converts count 48-byte JointParams records (scale, "
        "quaternion and translation) into 64-byte affine matrix rows. It consumes "
        "only its supplied buffers and optional index list; it reads no Player, "
        "camera, screen route or mutable gameplay global.",
    ),
    0x141A97060: (
        "AnimJointProgramCustomOp128",
        "Custom joint-program operator 128 from the six-entry table at "
        "0x143026CB0. Each 64-byte authored record reads an encoded 16-byte "
        "quaternion and optional scalar, computes a clamped angular weight, and "
        "writes one encoded scalar at record+0x3C. Payload selector 3 resolves a "
        "nested four-pointer table first, so the record destination selector is "
        "relative to that live table rather than the top-level evaluator bases. "
        "Its mathematical output is buffer-local, but the wrapper is NOT "
        "isolated: unconditional QPC profiling divides by143077520, "
        "updates global counters and calls14160C120 (FLS/render-state "
        "access), including the tail at141A97748. Do not execute through "
        "raw mapped retail code with only owned evaluator buffers. "
        "STATIC C39 g0 op128 at5944A/5945A/59472/5947A reads localQ26 "
        "encoded4F0 and AUX400, writes AUX3E4/3EC/404/408 respectively. "
        "These are last scalar writers before g0 shared export to group6. "
        "STATIC numeric model: optional twist rotates target basis axis "
        "around the authored reference axis; acos(dot current,target) has "
        "NO native input clamp. End>start gives clamped linear1-to0 angular "
        "falloff; otherwise angle<=start gives1 else0. The four pinned "
        "ranges are0..100/90/70/70 degrees. Explicit currentQ26 is required; "
        "descriptor referenceQ is not proof of a current-pose baseline.",
    ),
    0x141A951F0: (
        "AnimJointProgramCustomOp129",
        "Custom joint-program operator 129. Each 64-byte authored record reads one "
        "encoded 16-byte quaternion, computes a signed wrapped relative angle, and "
        "writes one encoded scalar at record+0x3C through the nested four-pointer "
        "table resolved by payload selector 3. The record selector is therefore "
        "not a top-level base selector. Buffer-local math does not imply "
        "an isolated wrapper: unconditional QPC/global profiling and "
        "14160C120 FLS/render-state tail at141A95A90 prohibit raw mapped "
        "execution with only owned evaluator buffers. STATIC C39 g0 op129 "
        "at593E4 descriptor5A060 reads localQ26 encoded4F0 and writes AUX400, "
        "the last writer before g0 export and the input to four op128 weights. "
        "STATIC numeric model: hemisphere-align inputQ to reference, remove "
        "swing, normalize conj(reference)*conj(swing)*Q, extract signed "
        "2*atan2 and strict fmod wrap[-pi,pi] to degrees. Antiparallel swing "
        "fallback141A9571C..724 is xyzw(0,1,0,0), NOT a generic shortest arc. "
        "Binary64 analytic checks do not establish float32/rsqrt equivalence "
        "or supply the missing actualQ26 from the D350/B8 sparse grip.",
    ),
    0x141A945F0: (
        "AnimJointProgramCustomOp130",
        "Custom joint-program operator 130. Its descriptor supplies N encoded "
        "scalar inputs, authored centers/weights, and M encoded scalar destinations; "
        "it evaluates distance-weighted sums and writes those M outputs. The "
        "output count is descriptor dword +8 and the encoded destination dword "
        "array is addressed by descriptor word +22. No direct camera/global "
        "source is reachable.",
    ),
    0x141A94D80: (
        "AnimJointProgramCustomOp131",
        "Custom joint-program operator 131. Reads one encoded 32-byte transform, "
        "builds an affine matrix, and writes authored component codes to encoded "
        "scalar destinations through the nested four-pointer table selected by "
        "payload word 1. Its descriptor destination selector is not a top-level "
        "base selector. Correction: buffer-local result does NOT mean "
        "wrapper purity. QPC calls141A94DC0/141A9502D divide by mutable "
        "143077520; counters14388BF4C/14388BF40 are written;141A950A9 "
        "calls14160C120 with FLS/render-state dependencies unconditionally.",
    ),
    0x141A95E80: (
        "AnimJointProgramCustomOp132",
        "Custom joint-program operator 132. Its large descriptor resolves encoded "
        "matrix/scalar inputs and encoded scalar outputs for buffer-local IK/math; "
        "its output count is descriptor dword +4 and word +70 (0x46), read at "
        "0x141A95FC5, addresses a table "
        "of {component,destination} records whose encoded destination is dword +4. "
        "The earlier +76 (0x4C) output-table annotation is REJECTED. Output "
        "destinations are full 32-bit encoded values, masked 0xFFFFFFFC, "
        "not truncated 16-bit indices. C39 g0's two instances write encoded "
        "EC0/EC4/EC8 and EF0/EF4/EF8/2E2/34E/3B2. "
        "It also reads SegmentContext+0x4A flags 0x100/0x200. Its numeric "
        "output is buffer-local, but profiling/import/FLS wrapper "
        "dependencies are not isolated. Do not call a full cmd16 program "
        "through raw mapped code merely from this encoded-buffer audit.",
    ),
    0x141A97970: (
        "AnimJointProgramCustomOp133",
        "Custom joint-program operator 133. Operates only on encoded evaluator "
        "buffers and authored stream values; no camera/global source is reachable.",
    ),
    0x1421944A0: (
        "AnimJointProgramOp55_TransformMatrixRange",
        "Built-in joint-program opcode 55 wrapper. Resolves source, common 3x4 "
        "transform and 64-byte destination operands, then transforms a joint range.",
    ),
    0x14218AC90: (
        "AnimJointProgram_TransformMatrixRange",
        "Transforms count 64-byte matrix rows by one supplied 3x4 basis and writes "
        "the resulting 64-byte rows to the encoded destination array. "
        "Also used by cmd25 sampled-world export: applies the separately "
        "stored local scale to the3 affine basis rows, then transposes "
        "to four column-vector SMath::Transform float4s. For the retarget "
        "builder identity root at1417CE003..CE032, export is(A*diag(S),t).",
    ),
    0x142194560: (
        "AnimJointProgramOp57_WriteSkinningPalette",
        "Built-in joint-program opcode 57 wrapper. Resolves four encoded operands "
        "and invokes AnimCmdEvaluator_WriteSkinningPalette; it is a workspace reader, "
        "not a producer of the 64-byte workspace.",
    ),
    0x14218B170: (
        "AnimCmdEvaluator_WriteSkinningPalette",
        "Command-26 matrix converter. It combines the 64-byte joint workspace "
        "with the supplied basis/inverse-bind data and writes three float4 rows "
        "per joint to the destination generated from FgAnimData+0x230. "
        "Under command26 identity common basis at141ABAE8C, output is "
        "GlobalWithSeparateLocalScale * InverseBind, not reverse order. "
        "Three row-major float4 rows are consumed at48-byte stride by "
        "packed-mode1 GPU matrix skinning shaders.",
    ),
    0x141B5CE90: (
        "AudioManager_RenderJobEntry",
        "Ndjob callback scheduled by AudioManager_RunRenderJob. When owner+0x81 "
        "is clear, calls AudioManager_Render then AudioManager_ReportNcaStats.",
    ),
    0x141B5D240: (
        "AudioManager_RunRenderJob",
        "Exact AudioManager::RunRenderJob source assertions in gamelib/audio/arm.cpp. "
        "Schedules AudioManager_RenderJobEntry through the Ndjob work batch.",
    ),
    0x141B61530: (
        "AudioManager_Render",
        "Exact void AudioManager::Render(void) source strings in gamelib/audio/arm.cpp. "
        "The owner+0x14B8, stride-0x6C0 table belongs to audio ARM entries, not joints.",
    ),
    0x141B65C50: (
        "AudioManager_ReportNcaStats",
        "Audio-only diagnostics including the exact NCA System, Samplers, Submixing, "
        "Effects, Premastering, and Mastering report strings.",
    ),
    0x141B757F0: (
        "AudioManager_RenderArmEntry",
        "Audio ARM entry helper called at 0x141B63CE6 from AudioManager_Render. Its "
        "owner-relative fields and 0x6C0 entry layout are unrelated to skeleton palettes.",
    ),
    0x141BC2EF0: (
        "Player_ShouldSkipDirectRootTransformApply",
        "Player vtable +0x5E8 (+1512 dec). Reached virtually from the inherited "
        "NpcActive_AnimPhaseOwnerUpdate callback: "
        "return 0 -> net-anim short branch; return 1 -> control-host / decision-node "
        "continuation. Consults gameplay manager and Player+0x348 m_pNetController "
        "via NetController_ShouldSkipAnimPhaseDirectPath.",
    ),
    0x1419136E0: (
        "NetController_ShouldSkipAnimPhaseDirectPath",
        "Player+0x348 net-controller helper. Resolves child via net-controller "
        "vtable+8(net, player); returns 1 (skip anim-phase direct path) when child "
        "vtable+24 is true or child is null.",
    ),
    0x14126B830: (
        "PlayerActiveDecisionNode_Tick",
        "Decision-node vtable+0 (vtable data ~0x142E47BC0). Owner at node+8 -> "
        "Player. Calls Player_BuildInteractDispatchState + "
        "Player_InteractDispatchFromBuiltState using player+0x4010 screen index; "
        "tail is PublishRuntimeStateFromDecisionNodeLikeObject (Player::Active::Update).",
    ),
    0x140E84440: (
        "PlayerGunMoveShield_UpdateAndDispatch",
        "Player::GunMoveShield vtable 0x142A27658 slot +0x148. Owner Player is "
        "stored at action+0x8. Reads the owner's joypad index at Player+0x39B8, "
        "handles aim/fire/grenade and calls Player_InteractDispatchFromBuiltState "
        "before PublishRuntimeStateFromDecisionNodeLikeObject.",
    ),
    0x140422BB0: (
        "Player_PreAnimHubAndAnchorSync",
        "Sniper/aim-position sync only (size 0x304). Does NOT call "
        "Npc_SyncControlHostEntityRootOnNetSkip. Real 422760 path: "
        "Npc_EarlyUpdateHubAndRetainedRefs -> Npc_SyncControlHostHubAndAnchors.",
    ),
    0x140421150: (
        "Npc_SyncControlHostHubAndAnchors",
        "Called from Npc_EarlyUpdateHubAndRetainedRefs. Requires actor+0x1A0. "
        "Alive: calls Npc_SyncControlHostEntityRootOnNetSkip only when actor+5545&2. "
        "Not-alive: chest/neck aim anchors + always 422760 at epilog.",
    ),
    0x14042FCF0: (
        "Npc_EarlyUpdateHubAndRetainedRefs",
        "Npc/Buddy/Human vtable+0x410 implementation. Calls the NPC hub/anchor "
        "path; Player vtable+0x410 is 0x1410EC230.",
    ),
    0x140422760: (
        "Npc_SyncControlHostEntityRootOnNetSkip",
        "When actor+0x1FF0 exists, its vtable+96 is false, and "
        "Player_ShouldSkipDirectRootTransformApply is true: clears "
        "control-host entity+0x6E bit0x4 (&=0xFA) at *(actor+0x1FB0+16). "
        "Else writes root transform via 0x142143F60/0x142144030 and sets bit0.",
    ),
    0x1410EC230: (
        "Player_Update",
        "Player vtable+0x410 implementation (vtable 0x142A4BFE8). Distinct from "
        "Npc/Buddy/Human +0x410 at 0x14042FCF0.",
    ),
    0x140C990A0: (
        "Player_UpdateMainViewVisibilityForAttachmentsAndWeapons",
        "Called from Player::Active::Update after screen camera-state refresh. "
        "Consumes current/previous visibility bytes Player+0x1618/+0x1619, "
        "publishes DisableMainView or EnableMainView to retained body attachments, "
        "and dispatches the matching PlayerWeaponController visibility slot.",
    ),
    0x1410A01F0: (
        "PlayerWeaponController_DisableWeaponsInMainView",
        "PlayerWeaponController vtable+0x370. Iterates weapon slots and publishes "
        "DisableMainView plus DisableCastShadows to eligible weapon actors.",
    ),
    0x1410A08A0: (
        "PlayerWeaponController_UpdateWeaponMainViewVisibility",
        "PlayerWeaponController vtable+0x378. Applies authored per-weapon main-view "
        "visibility, including EnableMainView/EnableCastShadows inverse events.",
    ),
    0x141ED28E0: (
        "NdAttachableObject_PostJointUpdate_Async",
        "NdAttachableObject vtable+0x410. Its ProcessWeapon caller enters at "
        "0x141515A30 after the selected record is changed. The base path first "
        "recomputes the attached object's bound/world transform through common "
        "vtable +0x2B8, then propagates owner DrawControl camera/category state.",
    ),
    0x141ED62D0: (
        "NdAttachableObject_SyncParentDrawVisibilityAndCameraFlags",
        "Called only by NdAttachableObject_PostJointUpdate_Async. Resolves the "
        "attachment parent through +0x4E0/+0x4E8, synchronizes the child "
        "DrawControl inherited visibility through 0x141754DA0, then propagates "
        "the parent's retained-camera and main-view flag state.",
    ),
    0x141754DA0: (
        "DrawControl_SyncParentVisibilityAndRecomputeInstances",
        "Writes child DrawControl+0x1D from parent +0x1C/+0x1D (or 0xFF for "
        "no parent), combines it with child +0x1C and mesh +0x243/+0x245, and "
        "publishes the resulting active byte to each render instance +0x1A0.",
    ),
    0x141A92A00: (
        "AnimManager_AsyncObjectUpdateJointBlendJob",
        "Source-identified anim-mgr-jobs.cpp AsyncObjectUpdate_JointBlend_Job. "
        "STATIC normal cache capture141A92BA5 copies Fg+1C0 into Fg+30 before "
        "Process141A92E78/141A932F8 and owner+410141A934D5/+418141A934E8. "
        "Custom Fg+2A0 callback141A92B4D or skipped blend bypasses this normal "
        "reset; residual groups can queue141A9336B. Do not infer freshness "
        "of every Pirate body joint without group0 bounds and admission. "
        "STATIC normal work gate is (job.byte14==0 || (Fg.byte8&0x8)) and "
        "raw Fg.byte6!=0. Null job.qword0 control selects a separate identity-"
        "palette path, not normal cache evaluation. In admitted effective "
        "mode2, build141A92D47 uses mask1 set141A92CB5; only group0 executes "
        "at141A92E78 and returns141A92E7D. "
        "STATIC Process RCX at141A92E6C is the generated command-list "
        "pointer from32-byte segment-record+0; EDX at141A92E71 is DWORD "
        "word count from record+C, NOT raw skeleton program pointer/bytes. "
        "Context Art is seeded at141A92D70 and copied at141A92E2B/34. "
        "Clearing bit0 at141A92EEC/EF8 "
        "excludes group0 from subsequent prepass/inline/residual masks. "
        "Require actual cmd24 cache export; Process return alone is not enough. "
        "Owner+410 eligibility is tested separately after blend, so its call "
        "does not prove that the normal matrix-producing path executed. "
        "Independently reads the persistent optional scalar-bank pointers at "
        "FgAnimData+0x118/+0x120 before the joint-blend work. "
        "Both direct BuildCommandSegments calls forward FgAnimData+0x08 as the "
        "builder flags byte stored in CreateContext+0x11. "
        "Local role-B Mazur runtime distinguishes its 58-word mode-2/source-zero "
        "command-14 import. In mode 2 the first Build at 0x141A92D47 selects and "
        "Processes group zero at 0x141A92E78, then 0x141A92EE8..0x141A92EF8 "
        "clears bit zero from both remaining masks before the later Build at "
        "0x141A93038. Thus group zero imports the postblend Fg+0x100 bank once "
        "before the final Fg+0x230 palette publication. "
        "STATIC P99 finality limit: after owner+420 call141A93572, nonzero "
        "g_AnimCpuPaletteConstantOverrideEnabled143682F4A with Fg+230 nonnull "
        "and !(Fg.flags&0x200) makes141A935C0..141A935E0 overwrite ALL Art+34 "
        "48-byte CPU palette rows from three constant float4s. Exclude this "
        "gate explicitly for any final-palette claim; do not assume it off. "
        "Normal g0 Process141A92E78 differs from residual141A932F8 and "
        "Perform141A92759 before Player+408. The pre-stock source observer "
        "runs before either Async call and never captures post16 JP/W22. "
        "Retained presentation-Player addresses may turn over before this job; "
        "each invocation revalidates the current Player/Fg generation and the "
        "command list itself owns canonical clip liveness. "
        "When job.byte(+0x14)==0 and Fg.flags(+0)&0x20, resolves retained "
        "owner at +0x1A8/+0x1B0 "
        "and dispatches owner vtable+0x410 then +0x418. "
        "STATIC reject-only early owner candidate: native job+8 supplies Fg; "
        "compare Fg handle64@1A8/generation32@1B0 with slot0@143EEBE80/+8, "
        "without dereferencing retained cell/Player. AllocateAnimData and "
        "spawn commit copy the same Process+A8/BC pair. Matching is NOT a "
        "lease or write admission; full owner checks remain mandatory. "
        "Stock PE unwind row spans RVA1A92A00..1A9363D, xdata RVA2EDEA34, "
        "flags3 EHANDLER|UHANDLER, prologue0x3D. Moving first0x2B bytes to "
        "anonymous memory without unwind metadata is REJECTED: it moves "
        "RSP allocation and nonvolatile saves outside the registered range. "
        "This packaging defect is not proven as the AGR crash PC. STATIC "
        "AGW: unchanged prologue pushes5 registers and subtracts0x5D0, "
        "total0x5F8 below the incoming return slot. Process0 context LEA "
        "0x141A92E64 is RSP+0x110; Process1 at0x141A932E4 is RSP+0x160. "
        "Therefore context+0x4E8/+0x498 identifies this invocation's native "
        "return slot. A nested old callback can share Fg/job values but "
        "cannot share the active return-slot address. Owner callbacks "
        "+0x410/+0x418/+0x420 run after Process/palette work; +0x418 "
        "141BBDF20 really waits, so whole-job no-yield is false. AFE bounded "
        "source-only observer keeps full native prologue and calls this original "
        "job once after its read-only candidate stack expires. No Process/cmd16 "
        "replacement, frame.job publication or grip claim; full producer once, "
        "post-call1ms cap and terminal no-retry policy. Lifecycle guarantees "
        "remain scoped to ADF ordinary package path, not arbitrary Level rebind. "
        "AFE runtime23348 stopped at first preselection budget (probes1,attempts0), "
        "before full preparation. No operand/grip result; observer defaultfalse "
        "again. EXIT0/no second-chance does not attribute historical AGR crash. "
        "AFE's actor was visually Chloe, so its preselection stop is not a "
        "validated Nadine cost/result. 2026-09-20 preselect-trace keeps default "
        "observer OFF with an explicit runtime-only exam build option. Eight "
        "scalar terminal timing bins, actual raw-id/parameter reads and query "
        "count distinguish normal-job/retained/owner/registry refusal from "
        "full preparation; no extra game reads, pose writes or retry. Reader "
        "authority expires before logging; intermediate clock errors fail closed. "
        "The same combined1ms cap and original stock-once call remain. "
        "CONFIRMED limited preselect-trace PID2692: probes1 attempts0 writes0, "
        "normal accepted rawC39CA709/params6480, owner snapshot/numeric refused. "
        "Elapsed184816ticks vs limit10000; normal bin184671, owner54. "
        "The slow initial bin includes stack/value initialization, reads, "
        "queries and instrumentation/debugger delays; do not call owner the "
        "slow stage or infer full-math/grip/P99-state2 acceptance from it. "
        "2026-09-20 STATIC observer storage reduction verified in matched "
        "exam68f4c51a/PDB age3: selected frame2F90->6F0, old epoch record/group "
        "clears removed, Reader setup retained. Same full/scalar predicates; "
        "80B selection/24B normal identity do not publish a pose. Nine bins "
        "separate setup/normal; optional first-failure owner detail preserves "
        "all short-circuit reads. Original1ms/defaultOFF/both writersfalse. "
        "No live speedup/old18ms cause/grip claim; see COOP_STATUS prediction. "
        "CONFIRMED scalar examPID9852: setup25/normal175014/owner50 of175170ticks, "
        "probes1 attempts0 writes0. Residual17.5ms is normal interval, not local "
        "initialization. First owner refusal controller-slot: int32+120 readfail "
        "or outside0..17, no value captured. State+134/weapon/numeric checks not "
        "reached; do not relax gates or blame native math. No grip repair. "
        "2026-09-20 late-arm orchestration keeps this existing ingress stock-only "
        "until explicit PID/creation-bound signal after user Draw; no new retail "
        "hook/owner reads on wait worker. CommitDrawn current-slot publication "
        "does not prove WeaponOut2; no existing completed-P99 scalar callback. "
        "Original owner gates/1ms/one-shot/writersfalse preserved. No live "
        "admission or timing fix claimed from offline arm tests. "
        "PID18204 late-arm run exited0/no second-chance before any ARMED/STOP/RESULT; "
        "no arm signal sent, no source-exam result. User subsequently confirms "
        "self-closure because grip was unchanged, not a reported crash. "
        "Handled last-event PCs are not crash evidence; repair remains pending.",
    ),
    0x141A8F9D0: (
        "AnimManager_DispatchRetainedOwnerPhase",
        "Resolves the animation job's retained owner and dispatches vtable phases "
        "+0x400/+0x408/+0x410/+0x418 according to phase bits 8/0x10/0x20. "
        "For phase 0x40 it reads the owner's authored key/generation at "
        "+0x108/+0x10C and resolves qword table 0x14386FD00[key]. A matching "
        "entry generation at +0x50 refreshes the owner's cached 0x20-byte parent "
        "transform +0x110; null/mismatch retains that cache. It then unconditionally "
        "composes cached parent +0x110/+0x120 with local +0x130/+0x140 and calls "
        "FgAnimData_PublishWorldTransform at 0x141A8FC00. ProcessWeapon's later "
        "vtable+0x208 callback is nullsub_385. This is a capable stock publisher, "
        "but runtime afg's target hook could not distinguish no worker/phase reach "
        "from its silent retryable return and therefore did not provide a usable "
        "cold-path publication discriminator. STATIC phase40 may refresh the "
        "parent cache but does not write owner local root+130/+140.",
    ),
    0x1403E3E30: (
        "NpcManager_UpdateSpotActors",
        "Iterates NpcManager's spot-actor list and calls Npc_UpdatePathSearch; "
        "does not dispatch Player vtable+0x410.",
    ),
    0x1403E84F0: (
        "NpcManager_RegisterSpotActor",
        "Sole append writer for the 16-entry spot-actor handle list at "
        "dword_143D3C0D0+0x10; source string npc-manager.cpp.",
    ),
    0x14043B830: (
        "Npc_UpdatePathSearch",
        "Source-identified Npc::UpdatePathSearch(void), called from the NPC "
        "spot-list tick.",
    ),
    0x1403E59D0: (
        "GameplayWorld_RefreshControlHostEntity6EFlags",
        "Global world tick: joypad-filtered buffer refresh, per-actor loop with "
        "ControlHostHub_RefreshEntity6EProbeFlags on actor+0x1FB0 (gated; Player "
        "vtable+4256 always 0), then always 144610 on worldMgr+0x5A0 hub.",
    ),
    0x142144610: (
        "ControlHostHub_RefreshEntity6EProbeFlags",
        "Walks 112-byte entries at hub+16; when entry+0x6E bit0 set and entry+108 "
        ">=0, sets bit0x4 and conditionally bit0x8 from resolver vtable+104/+120.",
    ),
    0x1401B7500: (
        "Entity6E_MergeFlagByte",
        "Atomic helper: entity+0x6E &= ~1; entity+0x6E |= arg flag byte.",
    ),
    0x140CC08F0: (
        "PlayerBase_SetupNpcAvoidanceCheck",
        "Source string identifies PlayerBase::SetupNpcAvoidanceCheck. Configures "
        "the Player+0x1A20 avoidance helper from point/vector/scalar arguments; "
        "it is not the physical locomotion output.",
    ),
    0x141BB8320: (
        "Character_UpdateDerivedWorldTransformFromLocalRoot",
        "Common Player/NadineBuddy/Pirate/NdAttachableObject vtable slot +0x2B8. "
        "For ProcessWeapon this is the last proven placement writer downstream "
        "of the selected attachment. Derives world translation "
        "at +0x240 and an offset world POINT at +0x280 from local root +0x130, "
        "local rotation +0x140, and optional parent transform +0x110/+0x120; "
        "+0x250 copies local +0x130, and +0x280 is NOT a quaternion. "
        "Null authored parent handle does not "
        "prove identity cached +0x110/+0x120. No physics publication proven here; "
        "exact Pirate +0x208 is empty, not a physics completion callback.",
    ),
    0x141837CD0: (
        "Joypad_IsCommandActive",
        "Resolves up to two binding descriptors for a named command through "
        "Joypad_ResolveCommandBindings, then accepts the command when either "
        "descriptor passes Joypad_EvaluateCommandDescriptor.",
    ),
    0x141838050: (
        "Joypad_EvaluateCommandDescriptor",
        "Evaluates one authored command descriptor: analog threshold, digital "
        "button state, hold timing, dependencies and temporary command disables.",
    ),
    0x1418384D0: (
        "Joypad_GetCommandValue",
        "Resolves up to two command descriptors, evaluates per-Joypad gamepad "
        "bindings and process-global keyboard bindings separately, and returns "
        "the resulting analog/Boolean command value.",
    ),
    0x141838740: (
        "Joypad_EvaluateKeyboardButtonBinding",
        "Evaluates keyboard/button bindings against four held/pressed/released "
        "64-bit mask groups and per-button timestamps in LocalPlayerInputManager.",
    ),
    0x141838C90: (
        "Joypad_EvaluateGamepadButtonBinding",
        "Evaluates gamepad bindings against Joypad pressed/released/current masks "
        "at +0xD0/+0xD8/+0xE0 and per-button timestamps beginning at +0x10C.",
    ),
    0x14183AE30: (
        "Joypad_ResolveCommandBindings",
        "Binary-searches the authored command table by hash and copies up to two "
        "binding descriptors for Joypad_IsCommandActive.",
    ),
    0x141E397C0: (
        "PlayerInputCommandState_DeriveMoveVectors",
        "Derives two movement magnitudes at state+0x7C and two 16-byte world "
        "movement vectors at state+0x90 from raw pairs at state+0x28, using the "
        "resolved runtime Player transform. Called only by "
        "PlayerInputCommandState_Update.",
    ),
    0x141596D60: (
        "Input_UpdateWindowFocusFlag",
        "Writes g_InputWindowFocused according to whether the game's HWND is "
        "GetForegroundWindow(). This global gates per-player input conversion.",
    ),
    0x141598080: (
        "Input_WindowProcFocusEvents",
        "Window-message handler that sets g_InputWindowFocused for WM_SETFOCUS "
        "and clears it for WM_KILLFOCUS.",
    ),
    0x140C9F8B0: (
        "Player_UpdateInputCommandState",
        "Player vtable slot 16 (+0x80). Sole caller of "
        "PlayerInputCommandState_Update 0x141E39FB0, followed by the broader "
        "Player input/action update.",
    ),
    0x140C9D850: (
        "Player_BuildAcceptedActionInputState",
        "Builds the compact accepted action-input block supplied in RDX "
        "(Player+0x47B0 from Player_UpdateInputCommandState). For reload it "
        "queries exact PlayerInputCommandState commands reload and "
        "reload-release with edge=0 at calls 0x140C9E611/0x140C9E632 "
        "(results consumed at 0x140C9E622/0x140C9E637), applies "
        "no-reload, active-weapon and ProcessWeapon virtual eligibility, then "
        "stores accepted level/release bytes at output+2/+3.",
    ),
    0x1410983D0: (
        "PlayerReloadClipWeaponAction_Constructor",
        "RTTI-backed PlayerReloadClipWeaponAction constructor. Calls the common "
        "weapon-action base constructor, stores the reload mode at +0x48 and "
        "installs vtable 0x142A48948.",
    ),
    0x141098430: (
        "PlayerReloadClipWeaponAction_Start",
        "PlayerReloadClipWeaponAction vtable slot 2 (+0x10). Resolves and binds "
        "the mounted weapon attachment, resets the Player weapon-action owner, "
        "and builds the upper gesture permit context before clip reload update.",
    ),
    0x141098560: (
        "PlayerReloadClipWeaponAction_Update",
        "Native clip-reload action body. Drives the mounted weapon gesture, "
        "publishes show-magazine, reload-aborted and "
        "player-finished-weapon-reload events, and leaves ammo mutation to the "
        "ProcessWeapon callbacks.",
    ),
    0x1410991A0: (
        "PlayerReloadClipWeaponAction_UpdateCompletion",
        "Waits for s_weapon-reload-clip or its mirrored action to finish, emits "
        "the reload-end gameplay events, publishes the downstream ReloadWeapon "
        "owner-registry flag, and moves the action to completion state 3.",
    ),
    0x14109A5C0: (
        "PlayerReloadLoopWeaponAction_Constructor",
        "RTTI-backed PlayerReloadLoopWeaponAction constructor. Calls the common "
        "weapon-action base constructor, stores the reload mode at +0x48 and "
        "installs vtable 0x142A48AF8.",
    ),
    0x14109A620: (
        "PlayerReloadLoopWeaponAction_Start",
        "PlayerReloadLoopWeaponAction vtable slot 2 (+0x10). Resolves and binds "
        "the mounted weapon attachment, resets the Player weapon-action owner, "
        "and builds the upper gesture permit context before per-round update.",
    ),
    0x14109B6D0: (
        "PlayerReloadLoopWeaponAction_UpdateCompletion",
        "Checks live round/capacity state and reload-loop policy, then emits the "
        "reload-end gameplay events and completes the per-round reload action.",
    ),
    0x14109BBE0: (
        "PlayerReloadLoopWeaponAction_StartLoop",
        "Starts the native s_weapon-reload-loop-start or mirrored action on the "
        "mounted Player action system and marks the loop action active at +0x4C.",
    ),
    0x141095090: (
        "PlayerWeaponIKAction_UpdateWrapper",
        "PlayerWeaponIKAction vtable +0x40 wrapper. Forwards the action update "
        "to PlayerWeaponIKAction_Update and preserves the action scheduler ABI. "
        "Before forwarding, Player+0x161A bit0 gates a call1410950AC to "
        "Player_UpdateWeaponPartialBlendAndIkInputs141090AC0. At postblend "
        "this may rewrite main instance+0x168 after Perform; do not confuse "
        "that main-phase field with Hands/WeaponPartial leaf phase+0x60.",
    ),
    0x1410950E0: (
        "PlayerWeaponIKAction_Update",
        "Resolves the selected WeaponPartial plus r_hand_prop_attachment and "
        "l_hand_prop_attachment, then updates the Player weapon-hand IK owner. "
        "This action is enqueued on controller channel 4 during a full Draw.",
    ),
    0x14109DA80: (
        "PlayerWeaponController_StartWeaponIKForSlot",
        "PlayerWeaponController vtable +0x308. Removes the old channel-4 action, "
        "constructs PlayerWeaponIKAction for the selected ProcessWeapon and "
        "enqueues it before the controller enters kStateDraw.",
    ),
    0x1410A3990: (
        "PlayerWeaponController_FinalizeWeaponSwitch",
        "Commits the selected/current/requested weapon indices and controller "
        "draw state. Its final boolean selects whether EquipWeaponIndex runs.",
    ),
    0x1410A3BB0: (
        "PlayerWeaponController_FinalizeWeaponSwitchAndEquip",
        "Thin wrapper over PlayerWeaponController_FinalizeWeaponSwitch with the "
        "EquipWeaponIndex branch enabled.",
    ),
    0x1410A3BD0: (
        "PlayerWeaponController_FinalizeWeaponSwitchWithoutEquip",
        "Thin wrapper over PlayerWeaponController_FinalizeWeaponSwitch with the "
        "terminal EquipWeaponIndex branch suppressed.",
    ),
    0x1410AA7B0: (
        "PlayerWeaponController_RefreshWeaponPartialAndIK",
        "PlayerWeaponController vtable +0x300. Resolves WeaponPartial context, "
        "publishes NewIkWeapon and refreshes the active weapon overlay/IK state.",
    ),
    0x1410AAFB0: (
        "PlayerWeaponController_ApplyState",
        "Applies PlayerWeaponController state transitions. State 1 is kStateDraw; "
        "after its Get action reaches a completed phase this removes the action, "
        "enqueues PlayerPlayWeaponOverlayAction, writes state 2 kStateWeaponOut "
        "and invokes vtable +0x300. State 3 is the native reload state.",
    ),
    0x1410AD410: (
        "PlayerWeaponController_Update",
        "Main PlayerWeaponController update. Consumes owner-local weapon-action "
        "state, invokes the active ProcessWeapon virtuals and applies controller "
        "transitions through PlayerWeaponController_ApplyState.",
    ),
    0x141E39FB0: (
        "PlayerInputCommandState_Update",
        "Updates the embedded Player+0x39A0 input-command state through its two "
        "virtual providers, then derives move direction/magnitude. Sole caller is "
        "Player vtable slot 16 at 0x140C9F8B0.",
    ),
    0x141E39C80: (
        "PlayerInputCommandState_GetMoveVector",
        "Copies a 16-byte move vector selected by index; index 0 is state+0x90 "
        "(Player+0x3A30) and is consumed by PlayerMoveStick_MainUpdate.",
    ),
    0x141E39D10: (
        "PlayerInputCommandState_GetMoveMagnitude",
        "Returns move magnitude selected by index; index 0 is state+0x7C "
        "(Player+0x3A1C) and is consumed by PlayerMoveStick_MainUpdate.",
    ),
    0x14183A1C0: (
        "Joypad_AddTemporaryDisabledCommands",
        "Adds command hashes to the Joypad's 22-entry temporary disable table "
        "with an expiry timestamp. PlayerMoveStick uses it for command 'move'; "
        "this is not an analog-axis reader.",
    ),
    0x14024D7E0: (
        "Player_GetInputCommandState",
        "Returns the embedded Player input-command state at Player+0x39A0. "
        "Its +0x18 field is the Joypad index used independently from the "
        "Player+0x4010 screen index.",
    ),
    0x1402B1890: (
        "PlayerInputCommandState_GetJoypadIndex",
        "Returns dword PlayerInputCommandState+0x18 (Player+0x39B8). "
        "PlayerMoveStick uses it to select FrameState+0xDB0+0xBF0*index.",
    ),
    0x141E39D50: (
        "PlayerInputCommandState_IsCommandActive",
        "Selects the per-player Joypad through state+0x18, then tail-calls "
        "0x141837CD0 with the command hash and transition selector.",
    ),
    0x140F03500: (
        "PlayerMoveStick_MainUpdate",
        "Main StickMoveInterface update (vtable 0x142A30740 +0x60). Reads "
        "Player+0x4010 for screen routing and Player+0x39B8 independently "
        "for Joypad selection; drives move/jump/cover/traversal decisions.",
    ),
    0x140D12B30: (
        "CoverMoveUpdate_UpdatePostAnim",
        "Exact source signature: void CoverMoveUpdate::UpdatePostAnim(Player&), "
        "player-cover-actions.cpp. Reads Player+0x4010 screen index and passes "
        "it to ResolveScreenRoutePoseSeedOrDefault 0x141C077B0 while deriving "
        "cover-movement orientation. Strong static anchor for the per-screen "
        "camera-basis movement path.",
    ),
    0x140D79C00: (
        "PlayerDeathEntry_BE8",
        "Player vtable +0xBE8. Native death-entry variant; clears/updates the "
        "HealthSystem and publishes `player-died`. STATIC owner-local state+C0; "
        "event recipient is weak pair at Player+21C8, validated by record+8, "
        "object+BC and dead bit+40&1 before recipient virtual+D0 at140D79E53 "
        "under ThreadContext. Not directly retained player0, KillTree or menu; "
        "live recipient class/script unresolved. Remote replicas must not "
        "author this transition while their peer still reports HP > 0.",
    ),
    0x140D79950: (
        "PlayerDeathEntry_BF0",
        "Player vtable +0xBF0. Native player-died publisher variant. STATIC "
        "owner state+C0 and owner weak event recipient+21C8 via virtual+D0, "
        "not a direct primary-player/menu teardown call. Recipient runtime "
        "class/script remains unresolved; see PlayerDeathEntry_BE8.",
    ),
    0x140D78EC0: (
        "PlayerDeathEntry_BF8",
        "Player vtable +0xBF8. Native death-entry variant; clears/updates the "
        "HealthSystem and publishes `player-died`. STATIC owner state+C0 and "
        "weak event recipient+21C8 via virtual+D0, not directly retained "
        "player0/KillTree/menu. See PlayerDeathEntry_BE8; runtime recipient "
        "class/script unresolved.",
    ),
    0x140D792D0: (
        "PlayerDeathEntry_C00",
        "Player vtable +0xC00. Native player-died publisher variant. STATIC "
        "owner state+C0 and weak event recipient+21C8 via virtual+D0, not "
        "directly retained player0/KillTree/menu. See PlayerDeathEntry_BE8; "
        "runtime recipient class/script unresolved.",
    ),
    0x14042B400: (
        "CharacterDeathCleanupAndPublishFriendDied",
        "Shared death cleanup. If character+0x57C bit 1 is set, publishes "
        "`player-friend-died` to retained player 0. This is a consequence of "
        "an authored Player death, not the original death trigger.",
    ),
    0x141839FB0: (
        "AddOrUpdateJoypadDisabledCommands",
        "Adds or updates a disabled-command record in one per-player joypad "
        "manager. The manager is selected as g_LocalPlayerInputManager +0xDB0 "
        "+ 0xBF0*joypad_index. Two Player jobs concurrently mutated joypad-2, "
        "drove free_count at +0x538 to -1/-2, and crashed at 0x14183A090. The "
        "fixed-array mutation assumes single-Player serialization. Serializing "
        "calls preserved command behavior and survived a fresh campaign load with "
        "slot 1 for over two minutes; free_count=6 and active_count=1 remained sane.",
    ),
    0x140541EF0: (
        "ScriptNpcDieHandler",
        "Campaign npc-die command handler. Performs a native forced death: "
        "checks Pirate death state, applies maximum health damage through "
        "Pirate vtable +0xD00, resolves AiHitController through npc+0x15C8 "
        "and virtual +0x78, builds a clean 0x70-byte HitDescription, then calls "
        "AiHitController vtable +0xB8. This is the safe replicated-death model.",
    ),
    0x14055DF80: (
        "ScriptNpcKillHandler",
        "Script npc-kill handler. Routes to Process::KillTree rather than a "
        "gameplay death transition; unsuitable for replicated combat death.",
    ),
    0x1416A5C20: (
        "ProcessKillTree",
        "ndlib Process::KillTree. Recursively tears down a process hierarchy "
        "and bypasses normal health, hit, ragdoll, and gameplay death handling. "
        "Do not use this as a networked NPC/player death primitive. STATIC "
        "lifecycle: locks object+50; sets +40 enteredOnKill bit4 at1416A5E2A "
        "BEFORE virtual+190 at1416A5E33. Only AFTER callback/index removal "
        "does1416A6044 zero+BC and1416A607D publish the dead list. Identity "
        "and exact vtable alone therefore admit the earlier OnKill interval.",
    ),
    0x1402F8B40: (
        "PirateIsDeadViaHealthSystem",
        "Pirate virtual +0x860. Resolves the HealthSystem at npc+0x4A8 and "
        "returns its virtual +0x30 dead test; used by ScriptNpcDieHandler.",
    ),
    0x141DEE1D0: (
        "PirateApplyDirectHealthDamage",
        "Pirate virtual +0xD00. Forwards damage to HealthSystem virtual +0x20 "
        "and returns current health through virtual +0xB8. Used by npc-die "
        "before the explicit AiHitController death transition.",
    ),
    0x140500C20: (
        "AiHitControllerDie",
        "AiHitController virtual +0xB8. Runs the authored NPC death transition "
        "from a HitDescription. ScriptNpcDieHandler calls it with a zeroed "
        "0x70-byte description, direction (0,0,1,0), +0x48=-1 and "
        "+0x64=maxHealth.",
    ),
    0x141FCB210: (
        "HealthSystemApplyDamage",
        "HealthSystem virtual +0x20. Applies integer health damage and updates "
        "the system's current-health/death-related state. Called by "
        "PirateApplyDirectHealthDamage.",
    ),
    0x141FCB510: (
        "HealthSystemIsDead",
        "STATIC exact HealthSystem virtual +0x30. Calls effective current-health "
        "getter +0xB8 at141FCB517, then signed <=0 test at141FCB51D/51F. "
        "Does not read previous-health +0x3C and does not itself execute an "
        "actor death/ragdoll transition.",
    ),
    0x141FCAFA0: (
        "HealthSystemGetCurrentHealth",
        "STATIC exact HealthSystem virtual +0xB8. Reads signed int32 raw "
        "current HP at +0x38 (141FCAFB5), conditionally adds regeneration "
        "for positive HP below maximum, then caps against virtual +0xC0 "
        "(int32 +0x44 plus +0x48). Raw +0x38 is not necessarily the full "
        "effective return; +0x3C is PREVIOUS effective HP, not current HP.",
    ),
    0x141FCADD0: (
        "HealthSystemSetCurrentHealthWithHistory",
        "STATIC descriptive name; exact HealthSystem virtual +0x68. Calls "
        "effective current-health getter +0xB8 at141FCADE2, stores that OLD "
        "int32 value to +0x3C at141FCADE8, updates decrease/increase time "
        "at +0x20/+0x28, and tail-calls +0x1C0 with the requested new int32 HP. "
        "Previous-health +0x3C must not be used as a current-health probe.",
    ),
    0x141FCAE40: (
        "HealthSystemStoreCurrentHealthAndHistory",
        "STATIC descriptive name; exact HealthSystem virtual +0x1C0. Stores "
        "new signed int32 HP to +0x38 at141FCAE54; if history pointer +0x58 "
        "exists, updates its value/time ring with index +0x60. This storage "
        "helper does not itself execute an actor death/ragdoll transition.",
    ),
    0x141FCA9C0: (
        "HealthSystemGetMaxHealth",
        "HealthSystem virtual +0xC0. Returns the sum of the base and bonus "
        "maximum-health fields at +0x44 and +0x48.",
    ),
    0x141DEF6C0: (
        "ValidateNavigationFrame",
        "Validates a prepared navigation frame under the native NavMesh global "
        "lock. The NPC MoveTo script path requires this check before submitting "
        "a goal; invalid frames must not reach NadineNpcMoveTo_Type02.",
    ),
    0x14027A270: (
        "PublishAttackEventToTarget",
        "Native high-level attack publisher. Accepts a polymorphic AttackInfo "
        "object and a target character, builds the authored Attack event, "
        "dispatches it through the target event path, and publishes the "
        "Attack-broadcast state. This is the preferred damage relay entry.",
    ),
    0x14040E7C0: (
        "NpcHandleAttackEvent",
        "Npc Attack-event handler. Extracts AttackInfo from event+0x28, then "
        "dispatches character virtual slot +0x1170 and performs the surrounding "
        "hitmarker, last-hit, knockdown, kill, and notification logic.",
    ),
    0x140409F70: (
        "CharacterApplyAttackInfo",
        "Shared character virtual at vtable +0x1170 (slot 558). Applies a valid "
        "AttackInfo to a character, calls NpcArmor::OnHealthDamage, updates "
        "HealthSystem, and participates in knockdown/death decisions. Do not "
        "call directly with a partial copied structure.",
    ),
    0x1403D7DF0: (
        "NpcArmorOnHealthDamage",
        "NpcArmor::OnHealthDamage(Npc*, AttackInfo*, float, float). Processes "
        "armor/body-part damage and detach definitions after AttackInfo has "
        "already been built by the upper attack path.",
    ),
    0x1402791C0: (
        "DamageInfoConstructor",
        "Initializes the complete 0x7A0-byte DamageInfo: total damage, point "
        "count, and 24 zeroed points with stride 0x50.",
    ),
    0x1414F1420: (
        "CopyDamageInfo",
        "Copies a complete 0x7A0-byte DamageInfo including total damage, point "
        "count, and all 24 damage points.",
    ),
    0x1402942A0: (
        "GameArmorComputeDamage",
        "Ordinary GameArmor damage calculation. Distributes AttackInfo+0x20 "
        "over AttackInfo+0x24 points and preserves damage for points that do "
        "not map to an armor piece.",
    ),
    0x140294610: (
        "GameArmorComputeExplosionDamage",
        "GameArmor explosion-damage calculation using the derived explosion "
        "fields and armor-piece distances.",
    ),
    0x140294C30: (
        "GameArmorFindPieceForDamagePoint",
        "Maps a damage point's hit-object handle at +0x30/+0x34 to an armor "
        "piece. Returns -1 for zero, stale, non-collision, or unmapped handles.",
    ),
    0x141F38A90: (
        "GameArmorApplyDamageResults",
        "Applies prepared armor-damage result entries to armor pieces.",
    ),
    0x14027A160: (
        "AttackInfoConstructor",
        "Constructs the common 0x860-byte AttackInfo base and initializes damage "
        "points, source handle, type, flags, scalars, and auxiliary fields.",
    ),
    0x140279590: (
        "AttackInfoCopyConstructor",
        "Copies a complete common AttackInfo object, including the large damage "
        "point block and fields through +0x858. Used by derived clone helpers.",
    ),
    0x140279540: (
        "CloneAttackInfo",
        "Virtual clone for the common AttackInfo type. Allocates 0x860 bytes and "
        "calls AttackInfoCopyConstructor.",
    ),
    0x140279FB0: (
        "DeleteAttackInfo",
        "AttackInfo deleting destructor. Frees the complete 0x860-byte object "
        "when flag bit 0 is set.",
    ),
    0x1403FB340: (
        "CloneProjectileAttackInfo",
        "Virtual clone for ProjectileAttackInfo. Allocates 0x8C0 bytes, copies "
        "the AttackInfo base, then copies projectile fields through +0x8B4.",
    ),
    0x1403FB4E0: (
        "DeleteProjectileAttackInfo",
        "ProjectileAttackInfo deleting destructor used through virtual slot +0x0.",
    ),
    0x1408F89E0: (
        "CloneCloseCombatAttackInfo",
        "Virtual clone for CloseCombatAttackInfo. Allocates 0x880 bytes, copies "
        "the AttackInfo base, then copies close-combat fields through +0x878.",
    ),
    0x1408F8B20: (
        "DeleteCloseCombatAttackInfo",
        "CloseCombatAttackInfo deleting destructor used through virtual slot +0x0.",
    ),
    0x140048440: (
        "RegisterScriptCommand_SpawnPlayer",
        "Registers the retained script command entry for spawn-player. Stores a "
        "generic command callback and command metadata; the larger script-side "
        "implementation is recovered separately around 0x140F73910.",
    ),
    0x14007D040: (
        "RegisterNetSpawnCoopBuddyCommand",
        "Registers the retained network command net-spawn-coop-buddy and binds "
        "it to the large runtime handler at 0x140B56090.",
    ),
    0x1403F6890: (
        "NadineNpcMoveTo_Type02",
        "Confirmed native NPC MoveTo writer. Initializes type 0x02 locomotion "
        "command and fills nav frame, facing, move parameters, flags, and "
        "aux fields.",
    ),
    0x141DF2D60: (
        "BuildNavigationFrame",
        "Confirmed navigation-goal builder used by Nadine movement paths.",
    ),
    0x1403F5D30: (
        "RequestDisableAi",
        "Confirmed Nadine AI disable path. Tested as RequestDisableAi(npc, false, true).",
    ),
    0x1403F5C80: (
        "RequestEnableAi",
        "Confirmed Nadine AI restore path. Tested as RequestEnableAi(npc, false).",
    ),
    0x1403F5B60: (
        "InitPendingNpcActionCommand",
        "Resets and initializes the pending NPC action block at npc+0x2470. "
        "Writers in the 0x140545000..0x14056B000 family then fill command kind "
        "at +0x24F4, timing/flags around +0x24F8/+0x2500/+0x2560, and optional "
        "snapshot payloads. This is a prepared action/behavior layer, not a raw "
        "direct-input seam.",
    ),
    0x1403F61C0: (
        "WriteLocoCommand_Type13",
        "Writer for locomotion command type 0x13. Sets +0x2500, mutates +0x2560, "
        "and rebuilds nav frame via 0x14014E240 + BuildNavigationFrame.",
    ),
    0x1403F68C0: (
        "WriteLocoCommand_Type02",
        "Writer for locomotion command type 0x02. Fills nav frame, facing, move "
        "parameters, +0x2514, +0x2560, +0x2561.",
    ),
    0x1403F69B0: (
        "WriteLocoCommand_Type04",
        "Writer for locomotion command type 0x04. Stores +0x2508, move payload, "
        "and flags consumed by dedicated interpreter branch.",
    ),
    0x1403F6C20: (
        "WriteLocoCommand_Type07",
        "Writer for locomotion command type 0x07. Stores scalar parameters at "
        "+0x2558/+0x255C and clears npc+0x1D57 bit 0x2.",
    ),
    0x14041F700: (
        "CopyLocoCommandBlock",
        "Copies locomotion command block from source (&npc+0x2470) into another "
        "internal buffer. Preserves flag bytes at offsets +0xF0/+0xF1.",
    ),
    0x14041E0A8: (
        "CheckLocoCommandTimeGate",
        "Reads npc+0x2500 and npc+0x24F8 before command copy/dispatch path.",
    ),
    0x1403E0400: (
        "ValidateAcquireLocoContext",
        "Validation/acquisition helper used before locomotion command processing.",
    ),
    0x1403DF370: (
        "ProcessLocoStateBranch",
        "Large locomotion-related state handler reached by downstream command paths.",
    ),
    0x1406B2360: (
        "IsInterestingLocoCommandType",
        "Classifier on npc+0x24F4 using bitmask 0x914 over command types 0..0x0B.",
    ),
    0x1406B2C90: (
        "BuildLocoDirectionFromCommand",
        "Interpreter helper for command types 0x02/0x04/0x08/0x0B. Type 0x04 uses "
        "dedicated branch; others either use stored nav frame or fallback orientation path.",
    ),
    0x1406B591C: (
        "DispatchSpecialLocoCommandTypes",
        "Special dispatcher for command types 0x09 and 0x11 before generic "
        "locomotion processing continues.",
    ),
    0x1406AD290: (
        "ResetPendingLocoDispatch",
        "Clears pending dispatch-related state around npc+0x27D8 and npc+0x26A4.",
    ),
    0x140176990: (
        "GetNpcFlag_1D57_Bit1",
        "Getter for npc+0x1D57 bit 0x2.",
    ),
    0x140177D4F: (
        "CheckNpcFlag_1D57_Bit1_Path",
        "Downstream state path that tests npc+0x1D57 bit 0x2.",
    ),
    0x140420441: (
        "CheckNpcFlag_1D57_Bit2_Path",
        "Downstream state path that tests npc+0x1D57 bit 0x4.",
    ),
    0x140B56090: (
        "HandleNetSpawnCoopBuddy",
        "Large retained co-op buddy/network spawn handler. Resolves level or "
        "context objects, consumes roster-like state, and can resolve a live "
        "entity through 0x141D22420.",
    ),
    0x140B580C0: (
        "SelectRetainedCoopPlayerLookHash",
        "Selects the retained co-op player-look hash from the network/co-op mode "
        "field at manager+0x558. Confirmed stock modes choose coop-drake, "
        "coop-chloe, or coop-sullivan.",
    ),
    0x140F72170: (
        "PlayerRunNGunController_ConstructInPlace",
        "Constructs the PlayerRunNGunController state used by the separate "
        "run-and-gun/blind-fire locomotion family; this is not the ordinary "
        "drawn WeaponPartial or Hands controller.",
    ),
    0x140F72C80: (
        "PlayerRunNGunController_Create",
        "Allocates and constructs PlayerRunNGunController. Player assembly "
        "stores the resulting controller at Player+0x1E40.",
    ),
    0x140F722F0: (
        "PlayerRunNGunController_UpdateAimTarget",
        "Updates the special run-and-gun target from targRWrist, per-screen "
        "AimAtPos and cover geometry. This path is distinct from ordinary "
        "weapon draw and grip composition.",
    ),
    0x140F72ED0: (
        "PlayerRunToRunStateAction_Construct",
        "Constructs the run-to-run StateAction used next to the special "
        "PlayerRunNGunController transition family.",
    ),
    0x140F73010: (
        "PlayerRunToRunStateAction_UpdateTransitionSelection",
        "Selects authored run-to-run transition variants, including mirrored "
        "run-null states; it is not an ordinary drawn-weapon idle selector.",
    ),
    0x140F73910: (
        "ScriptSpawnPlayer",
        "Large retained spawn-player implementation. Builds or copies a spawn "
        "descriptor, then dispatches into 0x1410F1910 or 0x1410F3350.",
    ),
    0x1400AA8C0: (
        "RegisterPlayerCtorFactoryIntoGlobalPlayerSlotBF00",
        "Startup registration helper that publishes 0x1410C6F00 as the real "
        "Player alloc/ctor pointer through 0x1417C4650 into the global slot "
        "at 0x143EEBF00.",
    ),
    0x1400AA940: (
        "RegisterSiblingPlayerHelperFactoryIntoGlobalSlotBF90",
        "Startup registration helper adjacent to the Player ctor registration. "
        "Seeds the sibling global slot family at 0x143EEBF90/0x143EEBFA0 with "
        "0x1410C6F60 and fixed metadata size 0x8.",
    ),
    0x1401696B0: (
        "CheckEntityAgainstGlobalPlayerSlotBF00Bitset",
        "Small runtime gate that tests a live object's bitset at rcx+0x30 "
        "against selector/id data stored in the globally registered Player "
        "slot record at 0x143EEBF00.",
    ),
    0x140F73C90: (
        "PlayerPersistentState_CopyFieldsThroughE10",
        "STATIC ScriptSpawnPlayer helper copying inline "
        "GameSave::CharacterPersistentState fields, including blocks+DC8/+DF0/"
        "+E00 and DWORD+E10. Required extentE14; independent native arrays "
        "advanceE20. Old CopySpawnDescriptor_0x184 name was incomplete. Pure "
        "scalar/SIMD copy, no calls/ownership transfer; untouched gaps remain "
        "untouched. Not an input descriptor copied from spawn RCX.",
    ),
    0x1410F14C0: (
        "CommitResolvedPlayerSpawn",
        "Central retained player-spawn commit path. Accepts resolved look/"
        "transform/context data, seeds a real ProcessSpawnInfo-class stack "
        "packet with vtable 0x14291CF98, initializes it through 0x1417C32A0, "
        "builds a second local packet layer plus fallback via 0x1417651D0, "
        "now clearly tags that path with `Player` and `*player-health-settings*`, "
        "materializes a runtime object through 0x1417C3790, then harvests the "
        "returned object's +0xA8/+0xBC retained-live pair and stores it in the "
        "global retained player-slot table at 0x143EEBE80. "
        "Exact copies at 0x1410F17BB/17C6/17E9 establish bit-equivalence with "
        "Fg+1A8/1B0 written by AllocateAnimData; padding+0xC is not identity.",
    ),
    0x1410F36C0: (
        "PlayerSlots_SpawnAndPublishPrimaryPlayer",
        "Front-end path called at 0x14093C55D builds Player ProcessSpawnInfo "
        "and resolves an object. Reads Process+BC at 0x1410F3AB4 and +A8 at "
        "0x1410F3ABA, publishes this handle64/id32 pair to retained slot0 at "
        "0x1410F3AC9 and mirrors the result to caller output. Independent "
        "slot0 identity writer, equivalent to CommitResolvedPlayerSpawn.",
    ),
    0x1417C32A0: (
        "InitializeSelectorBackedProcessSpawnPacket",
        "Compact packet initializer used by retained/player runtime builders. "
        "Seeds packet fields at +0x10/+0x18/+0x20/+0x28/+0x30/+0x38/+0x48, "
        "stores the incoming selector/hash id in +0x8, resolves a selector/"
        "capability entry into +0x50, and derives a mask/limit into +0x40. "
        "The base field at +0x20 is copied directly from arg4/r9, which is "
        "important because the real Player::Init path later reads "
        "incoming_packet+0x20 before any draw-control or late mounted-owner "
        "stages run. "
        "This is the shared front-end base later consumed either directly by "
        "0x1417C3790 or through the extended wrapper family at "
        "0x1417C35A0 / vtable 0x1428D5D60.",
    ),
    0x1417C35A0: (
        "InitializeExtendedSelectorBackedRuntimeStatePacket_1428D5D60",
        "In-place initializer for the extended selector-backed packet family "
        "with vtable 0x1428D5D60. Calls 0x1417C32A0 for the shared base "
        "fields, then seeds wrapper-local state at +0x58/+0x60/+0x68/+0x70/"
        "+0x78/+0x80/+0x88..+0x98. The base +0x20 source link survives this "
        "wrap and is exactly what the early Player::Init path later passes "
        "into 0x1416860A0. This wrapper also preserves the upper packet shape "
        "at +0x40/+0x48/+0x58 used later by 0x1418C4840, which makes it a "
        "stronger external_state candidate than the materialized object "
        "returned by 0x1417C3790.",
    ),
    0x1417C3790: (
        "ResolveOrMaterializeRuntimeStateObjectFromSelectorPacket",
        "Consumes the compact packet built by 0x1417C32A0. Requires packet "
        "+0x50, checks selector/capability gates through 0x1417C4800, builds "
        "a returned runtime object family, copies packet payload blocks from "
        "+0x20/+0x40 into that object, stores the packet owner/source at "
        "object+0xA0, links the intrusive node back to the object through "
        "node+0x8, stores that node pointer at object+0xA8, and writes descriptor "
        "delta to object+0xB8 at1417C3D0E. CORRECTION: this is NOT +0xBC; local "
        "generation+BC comes from base constructor1416A3393. Name copy1417C3E1D "
        "uses32-byte middle-ellipsis helper into object+8, not full authored name. "
        "The returned object was previously considered more like a "
        "materialized runtime companion to the packet than the final "
        "Player-side external_state itself; the stronger direct shape match "
        "for 0x1418C4840 remains the extended wrapper family at 0x1417C35A0. "
        "STATIC Player-selector ordering: 0x1417C3CF7 publishes node+8=Player "
        "and 0x1417C3CFB publishes Player+A8=node before calling the concrete "
        "Player Init slot+58 at 0x1417C400B. This path does materialize the "
        "concrete Player; the earlier generic-companion wording must not "
        "exclude the Player selector case.",
    ),
    0x1415FC800: (
        "InsertOrRelinkRuntimeStateNodeBySpanIntoIndexedContainer",
        "Intrusive-node/container helper used directly inside "
        "0x1417C3790. Claims or re-links a node, stores span/count at "
        "node+0x20, links it through owner+0x70/+0x80/+0x98, and updates "
        "owner+0xA8. This is object-local bookkeeping for the materialized "
        "runtime family, not the upper Player apply seam.",
    ),
    0x1415FC9F0: (
        "UnlinkRuntimeStateNodeFromIndexedContainer",
        "Inverse intrusive-node/container helper paired with 0x1415FC800. "
        "Detaches a node from owner+0x70/+0x80/+0x98, decrements owner+0xA8, "
        "and clears node fields. Also used directly inside 0x1417C3790, "
        "which pins both helpers as internal bookkeeping for the returned "
        "materialized object family. STATIC cleanup141622A60->1416A66C0 reaches "
        "node+8=0 at1415FCAD8; this is LATER than KillTree object+BC=0 and "
        "deleting-destructor dispatch. A nonnull node+8 is not live identity.",
    ),
    0x1416A3330: (
        "ConstructBaseRuntimeObjectWithNullHandleAndFreshObjectId",
        "Lowest shared runtime-object constructor beneath the hidden "
        "descriptor/object family. Installs the base vtable, clears the "
        "canonical identity handle at +0xA8, seeds +0xBC from 0x1416A38A0, "
        "and leaves the object ready for a later materializer to attach a "
        "real owner/node link.",
    ),
    0x1416A38A0: (
        "AllocateFreshRuntimeObjectId",
        "Atomic shared id allocator used by the base runtime-object ctor at "
        "0x1416A3330 to seed the initial object id stored at +0xBC before any "
        "later materialization step rewrites that runtime identity.",
    ),
    0x1410F0D10: (
        "GetRetainedPlayerEntityBySlot",
        "Reads a 16-byte entry from g_RetainedPlayerSlotTable[slot], validates "
        "the embedded runtime id against entity+0xBC, and returns the live "
        "entity pointer when the slot is still valid. Also writes entity+0x72=1 "
        "at1410F0D4E; not a side-effect-free reader or lifetime lease.",
    ),
    0x1410F0D60: (
        "CopyRetainedPlayerSlotEntry",
        "Copies a raw 16-byte entry from g_RetainedPlayerSlotTable[slot] into "
        "caller-provided storage.",
    ),
    0x1410F0D80: (
        "FocusRetainedPlayerEntityBySlot",
        "Resolves a retained player entity by slot and jumps into a downstream "
        "focus/activation path at 0x14029ACB0.",
    ),
    0x1410F0DD0: (
        "PlayerSlots_ContainsRetainedIdentity",
        "Searches the 6-entry g_RetainedPlayerSlotTable for a matching "
        "handle64 at +0 and generation32 at +8, exact bit equality without "
        "masking or comparing padding+0xC. Returns AL=1/0, NOT the index: "
        "the old FindRetainedPlayerSlotIndex interpretation was incorrect.",
    ),
    0x1410F0E00: (
        "CopyRetainedPlayerSlot0Entry",
        "Copies g_RetainedPlayerSlotTable[0] into caller-provided storage.",
    ),
    0x140298D70: (
        "ResolveLocalPlayerVFunc_A8",
        "Resolver sub-object (*(0x143FE1070)) vtable +0xA8. Split/coop gate byte "
        "0x143E07473: if inactive (ordinary campaign) tail-calls "
        "ResolveRetainedPlayerHandleBySlot 0x1410F0F60(index). If split/coop "
        "active, scans net-player slot array 0x143DD7488 (NetGameManager+0x3C8, "
        "stride 0x43D0, dormant in campaign). So in campaign local-player "
        "resolution funnels into the 0x143EEBE80 slot table reader.",
    ),
    0x1410F0F60: (
        "ResolveRetainedPlayerHandleBySlot",
        "Resolves a retained slot entry into the live player handle/object. "
        "Campaign branch (split gate 0x143E07473 inactive) reads 16-byte pair "
        "from g_RetainedPlayerSlotTable 0x143EEBE80 + index*0x10 {ptr+0,id+8}, "
        "validates ptr->(+8)->+0xBC==id, returns ptr (marks +0x72) else 0. Slot "
        "1 empty in campaign => local-player-1==0. Writer of these pairs is "
        "CommitResolvedPlayerSpawn 0x1410F14C0.",
    ),
    0x1410F3DD0: (
        "PlayerSlots_KillPrimaryAndRefreshCamera",
        "STATIC correction: resolves retained slot0 with generation check, "
        "writes object+72=1, then calls ProcessKillTree at1410F3E04 BEFORE "
        "optional primary camera refresh141C078D0. TaskObserver_OnReset calls "
        "this at1413C6B15 before broader actor reset. Not a harmless getter.",
    ),
    0x1410F3E40: (
        "CheckRetainedPlayerSlotActive",
        "Validates a retained slot entry, resolves the live entity, and calls "
        "entity virtual slot +0x860 to test or update active state.",
    ),
    0x1410F3A80: (
        "WritePrimaryRetainedPlayerSlot0",
        "Writes a resolved (handle,id) pair into g_RetainedPlayerSlotTable[0] "
        "and mirrors the same pair into caller-provided output storage.",
    ),
    0x1410F3B20: (
        "PromoteRetainedPlayerSlot",
        "Builds or resolves a retained player record for the requested slot, "
        "stores the resulting (handle,id) pair into g_RetainedPlayerSlotTable[slot], "
        "and marks the resolved entity with byte +0x368C. The current disasm "
        "also shows that it manually builds the same base ProcessSpawnInfo "
        "packet plus extended wrapper family (`0x1417C32A0` then local vtable "
        "0x1428D5D60) before passing that state into the upper retained "
        "manager path, which makes it one of the strongest concrete promotion-"
        "adjacent retained entrypoints found so far.",
    ),
    0x1410F3F50: (
        "IsPrimaryRetainedPlayerAvailable",
        "Returns success when retained slot 0 is active/available, gated by "
        "global debug/runtime flags.",
    ),
    0x1410F3F80: (
        "HasPrimaryRetainedPlayerEntity",
        "Checks whether retained slot 0 resolves to a live entity, falling back "
        "to CheckRetainedPlayerSlotActive(0) when needed.",
    ),
    0x1410F3FC0: (
        "IsRetainedPlayerSlotUsable",
        "Validates an arbitrary retained slot index through "
        "CheckRetainedPlayerSlotActive(slot), gated by the same global runtime "
        "flags as the primary-slot helpers.",
    ),
    0x1410F8E90: (
        "RefreshAllRetainedPlayerEntities",
        "Iterates over the six entries of g_RetainedPlayerSlotTable and calls "
        "0x1410F8D80 for each live slot entity.",
    ),
    0x141C07420: (
        "SwitchRetainedCameraModeStateBlocks",
        "Retained-camera mode-switch bridge. Writes CameraManager+0x128 and "
        "updates two global camera-state blocks selected from the shared "
        "slot-state array using CameraManager+0x124 plus relative indices "
        "+2 and +4.",
    ),
    0x141C078D0: (
        "UpdateScreenRouteEntityCameraStateFromActorTransform",
        "Heavy screen-route entity update path. Walks the route's child-control "
        "list at +0x100, selects a preferred child/fallback descriptor, "
        "refreshes route-local follow state through +0x2F0, and commits the "
        "final camera descriptor through 0x141C06F20.",
    ),
    0x141C074E0: (
        "AttachScreenRouteEntityToResolvedActorPair",
        "Enables route-entity attachment mode, refreshes the attached actor "
        "pair at +0x2A8/+0x2B0 via 0x141C0DFD0, and marks the active screen "
        "lane in the shared camera state block.",
    ),
    0x141C07640: (
        "DetachScreenRouteEntityFromResolvedActorPair",
        "Disables route-entity attachment mode, clears the corresponding "
        "screen-lane state, and notifies the currently attached actor pair "
        "through virtual +0x208 when still valid.",
    ),
    # (0x141C077B0 ResolveScreenRoutePoseSeedOrDefault: superseded by the
    #  detailed 2026-07-05 entry in the fire-ray section below.)
    0x141C06F20: (
        "InsertRouteChildAndRecomputeActiveCameraDescriptor",
        "Core screen-route entity update worker. Inserts or refreshes a route "
        "child entry, carries forward the best available follow-state from the "
        "current child list, and commits the resulting active camera "
        "descriptor/follow payload back into the route entity.",
    ),
    0x141C0E9F0: (
        "SelectFirstRouteChildAcceptedByCameraManagerCustom",
        "Walks the route entity's child-control list at +0x520. Uses each "
        "child's owner callbacks at +0x70 and the CameraManagerCustom bundle "
        "callback at virtual +0x38 to choose the first acceptable child, then "
        "caches that child's descriptor payload into route-local state.",
    ),
    0x1413BA140: (
        "HandleRetainedPlayerCameraMessage",
        "Opcode/hash-dispatch handler for retained-player camera state. One "
        "branch builds camera/transform state from the current live player, one "
        "branch resolves a player/entity record and caches its (handle,id) at "
        "this+0x658, and one branch re-activates that cached retained entity.",
    ),
    0x1413B9EA0: (
        "ReactivateCachedRetainedPlayerEntity",
        "Reads cached retained-player (handle,id) from this+0x658, validates "
        "the live entity, marks it active via byte +0x72, then forwards the "
        "owning object into downstream retained/camera update paths.",
    ),
    0x1414538E0: (
        "BuildRailVehicleRetainedCameraStateRecord",
        "Builds retained rail/vehicle camera-state records, not a generic "
        "second-camera owner command. Exact engine-hash branches now match "
        "`npc-started-vehicle-jump`, `get-speed`, `on-animation-started`, "
        "`on-animation-done`, `slow-to-stop`, `rail-scrub`, `get-spline-id`, "
        "`get-surface-pat`, and `transfer-to-spline`; an additional branch "
        "resolves a retained/live pair through `get-rail-controller`, derives "
        "a transform from the live object, and forwards it into "
        "`ApplyRetainedRailControllerCameraCommand(..., edx=4)`.",
    ),
    0x141444010: (
        "ResolveRetainedVehicleClassFromPair",
        "Hashes and dispatches the retained query `get-vehicle-class` against "
        "the live object resolved from a retained (handle,id) pair, returning "
        "the resulting boxed/typed value when the pair is valid.",
    ),
    0x141444C50: (
        "ResolveRetainedRailControllerFromPair",
        "Hashes and dispatches the retained query `get-rail-controller` "
        "against the live object resolved from a retained (handle,id) pair, "
        "then returns the resulting rail-controller-like object when valid.",
    ),
    0x14145BAD0: (
        "ApplyRetainedRailControllerCameraCommand",
        "Applies a retained rail-controller/vehicle camera command. Stores "
        "the command type at +0x574, mirrors the payload into +0x9F0, toggles "
        "state bits at +0x572, hashes `go-ragdoll`, and dispatches the "
        "resulting record through the resolved live object via virtual +0xD0. "
        "Used from the rail/vehicle retained-camera state builder with command "
        "types 1/2/3/4.",
    ),
    0x1417BF750: (
        "AppendRetainedCameraRecord",
        "Appends a 0x30-byte retained-camera command/state record into a "
        "grow-only array, preserving the embedded handle/id and flag bytes.",
    ),
    0x141634F20: (
        "QueueRetainedCameraWorkItem",
        "Allocates and enqueues a retained-camera work item/object, links it "
        "into the owner's queue, then dispatches through a virtual method on "
        "the new item.",
    ),
    0x1416350A0: (
        "QueueRetainedCameraPayloadCommand_Type02",
        "Builds and enqueues a larger retained-camera command with inline "
        "payload data copied from the caller-supplied record. Marks queue bit "
        "0x2 and can dispatch immediately through the new item's vtable.",
    ),
    0x141635210: (
        "QueueRetainedCameraSimpleCommand_Type04",
        "Allocates and enqueues a simple retained-camera command of type 0x4. "
        "Stores the requested command id in the queue entry and mirrors it into "
        "the new item at +0x20.",
    ),
    0x141635310: (
        "QueueRetainedCameraSentinelCommand_Type04",
        "Allocates and enqueues a type-0x4 retained-camera command with "
        "sentinel id 0x7FFFFFFF and no explicit payload id.",
    ),
    0x1416354E0: (
        "QueueRetainedCameraSimpleCommand_Type08",
        "Allocates and enqueues a simple retained-camera command of type 0x8.",
    ),
    0x1416355B0: (
        "QueueRetainedCameraParameterizedCommand_Type10",
        "Allocates and enqueues a retained-camera command of type 0x10 with "
        "two caller-provided integer parameters copied into the new item.",
    ),
    0x1416D1330: (
        "InitRetainedCameraCommandContext",
        "Initializes a retained-camera command context, resolves an attached "
        "runtime node by command id, caches it at +0x50, and normalizes an "
        "aligned pointer/state value into +0x40.",
    ),
    0x1417A4B00: (
        "ConstructRetainedCameraCommandBus",
        "Large owner/manager constructor for the retained-camera command bus. "
        "Initializes command-record blocks, queue storage, descriptor tables, "
        "and multiple owner-side arrays/flags through roughly +0x5B0.",
    ),
    0x1417A4AB0: (
        "ResolveRetainedCameraPrimarySource",
        "Returns owner+0x58 when present, otherwise falls back to owner+0x50. "
        "Used by bus initialization and later query/dispatch helpers.",
    ),
    0x1417A5A90: (
        "ResetRetainedCameraCommandBusState",
        "Flushes descriptor buffers referenced by owner+0x4E0, clears queued/"
        "active bus state around +0x4E8/+0x500/+0x510/+0x51C, and resets "
        "several owner-side pointers while leaving subsystem enabled.",
    ),
    0x1417A5C10: (
        "HasRetainedCameraActiveRoot",
        "Checks whether the bus has active entries and whether the primary "
        "root reachable from owner+0x4F8 resolves to a non-null runtime node.",
    ),
    0x1417A5C40: (
        "DispatchRetainedCameraActiveRootVFunc10",
        "Resolves the active root from owner+0x4F8 and invokes its virtual "
        "slot +0x10 when available.",
    ),
    0x1417A5CA0: (
        "CheckRetainedCameraRouteForCommand",
        "Tests the active retained-camera route for a command id using helpers "
        "under the root node reachable from owner+0x4F8 and owner+0x530.",
    ),
    0x1417A5D50: (
        "ResolveRetainedCameraCommandResult",
        "Resolves a command-specific result/value through the active retained-"
        "camera root and writes it to caller output when found.",
    ),
    0x1417A5DD0: (
        "QueryRetainedCameraCommandInt",
        "Queries an integer-like value through the active retained-camera root "
        "using owner+0x530 and writes zero on failure.",
    ),
    0x1417AEDE0: (
        "DispatchRetainedCameraCommandBool",
        "Builds a compact retained-camera context snapshot from the root object, "
        "then tests command handling through root branches at +0x38/+0x40. "
        "Returns a boolean-like result via the downstream root handlers.",
    ),
    0x1417AEE60: (
        "ResolveRetainedCameraCommandObject",
        "Builds the same compact retained-camera context snapshot, then "
        "resolves a command-specific object/result through root branches "
        "+0x38/+0x40.",
    ),
    0x1417AF120: (
        "BuildRetainedCameraRootContextSnapshot",
        "Builds a compact 0x30-byte snapshot from a retained-camera root "
        "object, including timing/scalar state, route ids, and flags derived "
        "from the attached bus at root+0x60.",
    ),
    0x141844140: (
        "HandleRetainedCameraEventType04_UpdateGlobals",
        "Small event/callback-style stub for retained-camera event type 4. "
        "Reads a global context pointer at +0x1C0 and forwards it into "
        "UpdateRetainedCameraRouteGlobals.",
    ),
    0x141844170: (
        "ConstructRetainedCameraRouteGraph",
        "Monolithic retained-camera route-graph constructor. Allocates and "
        "links a chain/tree of typed route nodes and callbacks rather than "
        "acting as a simple runtime matcher API.",
    ),
    0x141844DE0: (
        "ConstructRetainedCameraRouteGraph_InternalBlockA",
        "Internal block inside ConstructRetainedCameraRouteGraph; one of "
        "several typed route-node allocation/link stages, not a standalone "
        "external function.",
    ),
    0x141845590: (
        "ConstructRetainedCameraRouteGraph_CommandIdLeafBlock",
        "Internal route-graph construction block associated with command-id "
        "leaf handling. This address lies inside the larger constructor body, "
        "not at a standalone function boundary.",
    ),
    0x141845640: (
        "ConstructRetainedCameraRouteGraph_LinkedDescriptorLeafBlock",
        "Internal route-graph construction block associated with "
        "descriptor-linked leaf handling. This address is an internal stage of "
        "the monolithic constructor.",
    ),
    0x1418457A0: (
        "BuildRetainedCameraRouteResolutionRecord",
        "Standalone helper that builds a compact route-resolution record from "
        "a resolved route candidate. Scans runtime registries and writes "
        "result indices/ranges into the output structure.",
    ),
    0x141845790: (
        "BuildRetainedCameraRouteResolutionRecord_Thunk",
        "Small thunk/adapter that forwards into "
        "BuildRetainedCameraRouteResolutionRecord.",
    ),
    0x141845980: (
        "UpdateRetainedCameraRouteGlobals",
        "Standalone retained-camera route-state helper reached from event/callback "
        "paths. Updates a small global state block from the supplied record/context; "
        "likely route/debug/global sync rather than the core matcher itself.",
    ),
    0x141845990: (
        "UpdateRetainedCameraRouteGlobals_InternalBlock",
        "Internal block inside UpdateRetainedCameraRouteGlobals, not a "
        "standalone query-object matcher function.",
    ),
    0x1409D6C90: (
        "BuildRetainedCameraStateWrapper",
        "Thin wrapper around BuildRetainedCameraStateRecord that stores the "
        "resulting record plus auxiliary state at object offsets +0x130..+0x170.",
    ),
    0x140745CD0: (
        "ToggleManualCamera1Binding",
        "Registered as `Manual Camera 1`. For mode/state 4, toggles "
        "screen-route entity 0 between 0x141C07640 detach and 0x141C074E0 "
        "attach based on whether route+0x2A8/+0x2B0 already resolves to the "
        "same live actor.",
    ),
    0x140746150: (
        "ToggleManualCamera2Binding",
        "Registered as `Manual Camera 2`. For mode/state 4, toggles "
        "screen-route entity 1 between 0x141C07640 detach and 0x141C074E0 "
        "attach using the same cached actor-pair validation as the screen-0 "
        "path.",
    ),
    0x14074D200: (
        "RegisterManualCameraAndJoypadHandlerCluster",
        "Builds and registers the Manual Camera 1/2, Joypad 1/2 Controls Game, "
        "Swap Joypad Screens, and Bind Manual Cam To Player(/Parent) cluster.",
    ),
    0x140746260: (
        "HandleJoypad1ControlsGameForManualCamera2",
        "Registered as `Joypad 1 Controls Game`. For screen-route entity 0, "
        "either drives 0x141C07420 when a cached actor pair is live or only "
        "toggles the lane-state bands under 0x143FE1088.",
    ),
    0x140746340: (
        "HandleJoypad2ControlsGameForManualCamera2",
        "Registered as `Joypad 2 Controls Game`. Mirrors/toggles bit 0x10 "
        "between paired objects, then either drives 0x141C07420 for a live "
        "cached actor pair or clears/toggles the same lane-state bands under "
        "0x143FE1088.",
    ),
    0x140746460: (
        "RefreshManualCameraResolvedActorCache_Primary",
        "Manual-camera owner/focus update path. Uses the cached actor pair at "
        "0x143D76C10/18, calls the cached target virtual slot +0x208 when a "
        "live actor exists, updates the base screen-route entity byte +0x153, "
        "and otherwise rebuilds the cache through 0x141C0DFD0.",
    ),
    0x1407465E0: (
        "RefreshManualCameraResolvedActorCache_Sibling",
        "Sibling manual-camera owner/focus update path that shares the same "
        "0x143D76C10/18 actor-pair cache and 0x141C0DFD0 fallback build.",
    ),
    0x14073C9E3: (
        "CommitGameplayCameraPriorityDescriptorViaRouteEntity_Internal",
        "Internal gameplay-facing caller block above 0x141C0DFD0. Uses "
        "0x141C0D4B0 to select the current route child, compares that child "
        "against descriptor/sentinel objects, then pushes stack-built "
        "descriptors through 0x141C0DFD0.",
    ),
    0x14073C400: (
        "CopyCameraOverlayDescriptorStateAndMergeFlagBits",
        "Copies the 0x50-byte camera-overlay descriptor state and only merges "
        "the low two flag bits at +0x4C, leaving the rest of the payload "
        "byte-for-byte identical.",
    ),
    0x14073B4E0: (
        "CameraManagerCustom_NoOpCallback0",
        "Top CameraManagerCustom vtable slot. Returns immediately and acts as "
        "a startup-published no-op observer callback.",
    ),
    0x14073B4F0: (
        "CameraManagerCustom_NoOpCallback8",
        "Second CameraManagerCustom vtable slot. Also returns immediately; the "
        "bundle's live behavior starts at later slots, not here.",
    ),
    0x14073B650: (
        "IsCameraManagerCustomDescriptorStateKind2",
        "Small CameraManagerCustom vtable predicate. Returns true only when "
        "the descriptor state's dword at +0xC equals 2.",
    ),
    0x14070C620: (
        "AllocOrInitCameraControlCinemaFollow3E0",
        "Alloc-or-init wrapper for a 0x3E0 CameraControlCinemaFollow object. "
        "Optionally allocates 0x3E0 bytes through 0x1415A0E90, runs the "
        "CameraControlStick base constructor, installs the "
        "CameraControlCinemaFollow vtable at 0x142983288, and clears the "
        "subclass follow-state fields around +0x350/+0x3A8/+0x3B0/+0x3B8.",
    ),
    0x14070C6E0: (
        "DestroyCameraControlCinemaFollowAndFree3E0",
        "Freeing destructor for CameraControlCinemaFollow. Restores the "
        "CameraControlStick vtable at 0x142BB1D98, clears screen index +0x124, "
        "runs shared camera-control cleanup through 0x1416A3800, and frees "
        "0x3E0 bytes when the low destructor flag is set.",
    ),
    0x14070C750: (
        "InitCameraControlCinemaFollowSubclass",
        "CameraControlCinemaFollow-flavored init wrapper. Calls "
        "InitCameraControlStickBaseForScreenAndConfig, then seeds additional "
        "follow-state fields around +0x398/+0x39C/+0x3A4/+0x3A8 for the "
        "screen-specific follow behavior.",
    ),
    0x140750D20: (
        "AllocOrInitCameraControlSpider520",
        "Alloc-or-init wrapper for a 0x520 CameraControlSpider object. Runs "
        "the CameraControlStick base constructor, installs the "
        "CameraControlSpider vtable at 0x14298B5A0, initializes the embedded "
        "subobject at +0x3A0 through 0x141D9EF50, and clears subclass state "
        "around +0x380/+0x388/+0x4B0..+0x4F0.",
    ),
    0x140750E70: (
        "InitCameraControlSpiderSubclass",
        "CameraControlSpider-flavored init wrapper above "
        "InitCameraControlStickBaseForScreenAndConfig. Builds an 8-block CRC "
        "over the static overlay template at 0x142BEA360, publishes the "
        "result through 0x141E776A0, and stores the produced handle at +0x390 "
        "with scalar defaults at +0x3B0/+0x480.",
    ),
    0x14073C620: (
        "NormalizeSpecialCameraControlDescriptorToContextualLook",
        "CameraManagerCustom vtable method. For special camera-control "
        "descriptors such as edge, cover, "
        "knocked-down, evade, monkey-bars, balance/off-balance, free-rope, "
        "shield, follow, and vehicle, rewrites the target state through "
        "0x141C04E90 using `kCameraContextualLook`. Also special-cases "
        "`kCameraStrafe` when byte +0x120 is clear.",
    ),
    0x14073C760: (
        "ShouldAcceptCameraOverlayCandidateForRouteEntity",
        "CameraManagerCustom vtable method. Overlay-compatibility gate over a "
        "route entity and candidate camera descriptor. Rejects "
        "melee/melee-override sentinels, checks the current preferred route "
        "child through 0x141C0D4B0, and combines 0x14073BF80 / 0x14073C0E0 "
        "filter results before accepting the candidate.",
    ),
    0x14073C8A0: (
        "UpdatePerScreenCameraOverlayPriorityStateAndCommitRouteDescriptors",
        "CameraManagerCustom vtable method. Per-screen gameplay camera-overlay "
        "updater keyed by route+0x124. Uses the zero-initialized dynamic "
        "state bank at 0x143D763E0 with stride 0x2B0, chooses between overlay "
        "sentinels such as `kCameraStrafe`, `kCameraSwim`, and `kCameraZoom`, "
        "and commits the resulting stack-built descriptors through "
        "0x141C0DFD0.",
    ),
    0x14073CDD0: (
        "BuildPerScreenCameraOverlayFollowStateFromRouteDescriptor",
        "CameraManagerCustom vtable method. Builds/refreshes a 0x50-byte "
        "per-screen overlay follow-state from a source descriptor and route "
        "entity. Hash-matches `CameraControlZoom` and "
        "`CameraControlScreenShot`, consumes the static tuning banks at "
        "0x1430732F8 with stride 0xC0, and outputs the resolved follow state "
        "used by the gameplay camera overlay path.",
    ),
    0x14073B780: (
        "DestroyLargeCameraControlOverlaySubclassAndFree1FA0",
        "Freeing destructor for the live 0x1FA0 overlay subclass produced by "
        "0x14073B6E0. Tears down the embedded subobject at +0x910 through "
        "0x141DA5AA0, restores the CameraControlStick-class vfptr, resets "
        "screen index +0x124, then runs the shared camera-control cleanup at "
        "0x1416A3800. Frees 0x1FA0 bytes when the low destructor flag is set.",
    ),
    0x14073C460: (
        "InitializeGlobalCameraManagerCustomBundle_3053850",
        "Startup initializer for the global CameraManagerCustom-flavored bundle "
        "at 0x143053850. Seeds the bundle's first qword with vfptr "
        "0x142987DB8 and clears the adjacent cached pointers/flags/scalars "
        "through 0x1430538B8.",
    ),
    0x14073C520: (
        "ResetGlobalCameraManagerCustomBundle_3053850",
        "Reset/teardown companion for the global bundle at 0x143053850. "
        "Re-seeds the first qword with vfptr 0x142987DB8, then frees and "
        "clears the cached heap pointers stored in the neighboring qword slots.",
    ),
    0x14073B6E0: (
        "AllocOrInitLargeCameraControlOverlaySubclass1FA0",
        "Alloc-or-init wrapper for a 0x1FA0 large camera-control subclass in "
        "the overlay/route family. Allocates 0x1FA0 when needed, runs the "
        "shared large CameraControlStrafe-flavored constructor at 0x140772E20, "
        "then overwrites vfptr 0x142987E00 on the resulting object.",
    ),
    0x14073E3C0: (
        "DoesScreenRouteCurrentCameraChildMatchDescriptor",
        "Resolves the screen-route entity by index through 0x141C06420, "
        "selects the current preferred camera child via 0x141C0D4B0, and "
        "checks whether the child's descriptor at +0x1A8 matches the "
        "descriptor pointer supplied by the caller.",
    ),
    0x141C04F80: (
        "InitializeTwoScreenRouteEntityPairTable",
        "Startup/EvaluateCameraRequests helper that loops exactly two screen "
        "indices, builds selector-backed runtime packets through "
        "0x1417C32A0 -> 0x1417C3790, and stores the resulting (ptr,id) pairs "
        "into the two-entry screen-route table at 0x14697AD50.",
    ),
    0x141C04E90: (
        "ApplyCameraTargetStateFromDescriptorWithControlFallback",
        "Packages a camera-control object's current descriptor state plus an "
        "optional live actor pair and forwards it to "
        "ApplyCameraTargetStateFromDescriptorAndActorOverride. Used by the "
        "special-camera normalization path to remap descriptors such as "
        "`kCameraContextualLook`.",
    ),
    0x1416A3800: (
        "Process_Destructor",
        "STATIC correction: exact Process::~Process source assertion, not "
        "camera-control-specific. Installs Process vtable at1416A3819, asserts "
        "level+ D0 was cleared unless global bypass, then installs Relocatable "
        "vtable at1416A387F/388F. Base of Player/NPC destruction as well.",
    ),
    0x1416A3920: (
        "CacheCameraControlProcessTokenIdIfMissing",
        "Shared camera-control finalize/init helper. If +0xD8 is zero, queries "
        "the process/control token through virtual +0x28, appends a dash plus "
        "the current global index, interns the token through 0x1416A2CE0 / "
        "0x1417AA820, and stores the resulting id at +0xD8.",
    ),
    0x1416A3C80: (
        "RelocateCameraControlEmbeddedPointersAcrossAddressRange",
        "Shared relocation helper for CameraControl-derived objects. Slides the "
        "embedded pointers at +0xB0, +0xC0, and the nested payload behind "
        "+0xC0 when they fall inside the caller-provided address range, then "
        "marks byte +0x3C bit 0x80.",
    ),
    0x141C13A80: (
        "InitCameraControlAnimatedPhotographModeSubclass",
        "CameraControlAnimatedPhotographMode-flavored init wrapper. Copies the "
        "config scalar at +0x80 into +0x398, then tail-runs "
        "InitCameraControlStickBaseForScreenAndConfig.",
    ),
    0x141F523F0: (
        "DestroyCameraControlStickAndFree320",
        "Freeing destructor for CameraControlStick. Restores the "
        "CameraControlStick vtable, clears screen index +0x124, runs shared "
        "camera-control cleanup through 0x1416A3800, and frees 0x320 bytes "
        "when the low destructor flag is set.",
    ),
    0x141F521B0: (
        "ConstructCameraControlStickBaseAndAssignUniqueScreenId",
        "Primary in-place constructor for the 0x320 CameraControlStick base. "
        "Installs the CameraControlStick vtable at 0x142BB1D98, seeds the "
        "descriptor/follow bands through +0x1A8..+0x314, and assigns a unique "
        "screen-local control id at +0x124 via the global counter pair "
        "0x146DB1628/0x146DB1630.",
    ),
    0x141F52660: (
        "InitCameraControlStickBaseForScreenAndConfig",
        "Primary CameraControlStick init path. Copies screen index from "
        "config+0xA0 into +0x128, seeds default descriptor `kCameraNone` into "
        "+0x1A8, initializes the route/follow bands around "
        "+0x1D0..+0x240/+0x300..+0x314 from per-screen globals, and finalizes "
        "the token id through CacheCameraControlProcessTokenIdIfMissing.",
    ),
    0x141F6C1A0: (
        "InitCameraControlFixedLookAroundSubclass",
        "CameraControlFixedLookAround-flavored init wrapper. Copies the config "
        "scalar at +0x80 into +0x368, then tail-runs "
        "InitCameraControlStickBaseForScreenAndConfig.",
    ),
    0x140772E20: (
        "ConstructLargeCameraControlStrafeBase1FA0",
        "Shared in-place constructor for a 0x1FA0 large camera-control family "
        "whose installed vfptr 0x14298E820 resolves by RTTI to "
        "CameraControlStrafe. Calls ConstructCameraControlStickBaseAndAssignUniqueScreenId, "
        "installs 0x14298E820, seeds extended state across +0x330..+0x5A0 and "
        "+0x1CA0..+0x1EF0, and initializes the embedded band at +0x7C0 via "
        "0x140773180.",
    ),
    0x141C04CE0: (
        "ApplyCameraTargetStateFromDescriptorAndActorOverride",
        "Finalizes a camera target-state object from an incoming descriptor plus "
        "an optional resolved actor override. Copies transform/scalar payload, "
        "refreshes the retained (ptr,id) pair, and resolves a downstream runtime "
        "object without an obvious static Player-only class check.",
    ),
    0x141C075DC: (
        "CommitResolvedRouteOverrideIntoAttachedTargetPair_Internal",
        "Internal writeback block after ResolveScreenRouteTargetAndCommitDescriptor. "
        "Stores the returned (ptr,id) into route+0x2A8/+0x2B0 and sets the "
        "per-screen activity byte at 0x14383DE48[route+0x124].",
    ),
    0x141C051F0: (
        "RefreshAllScreenRouteEntitiesFromStartupTable",
        "Iterates the two-entry screen-route table at 0x14697AD50, resolves "
        "each live route entity, notifies the global observer at 0x1438929B8 "
        "through virtual +0x8 when present, then calls route-entity virtual "
        "+0x48 on each live entry.",
    ),
    0x141C06420: (
        "ResolveScreenRouteEntityByIndex",
        "Takes a screen/route index 0..1, loads the corresponding (ptr,id) "
        "pair from the two-entry table at 0x14697AD50, validates the live "
        "entity, marks byte +0x72, and returns the resolved route entity. "
        "This is a screen-route resolver, not a raw CameraManager getter.",
    ),
    0x141C0D4B0: (
        "SelectPreferredCameraChildFromRouteEntity",
        "Scans a route entity's child list at +0x100, validates live children, "
        "and returns the preferred camera child while filtering on child mode "
        "stored at +0x1B0. The caller-provided byte flag relaxes the default "
        "mode filtering.",
    ),
    0x141C0DFD0: (
        "ResolveScreenRouteTargetAndCommitDescriptor",
        "Builds and commits a camera target descriptor into a screen-route "
        "owner. Uses the lock at route+0x2C0, resolves the actor through "
        "(*(qword*)0x143FE1070)->virtual +0xA8(route+0x124 screen_index), "
        "writes through route+0x2F0 via "
        "ApplyCameraTargetStateFromDescriptorAndActorOverride, and then "
        "notifies an external observer. The wide direct-call fanout shows "
        "this is a generic gameplay camera-route worker, not just a manual "
        "camera debug stub.",
    ),
    0x14073E330: (
        "PushCameraDescriptorToExplicitScreenRoute",
        "Generic per-screen route push. Takes an explicit screen index in ecx, "
        "a descriptor pointer in rdx, a follow-state block in r9, resolves "
        "the route through ResolveScreenRouteEntityByIndex, and forwards the "
        "pair into ResolveScreenRouteTargetAndCommitDescriptor. This is not "
        "screen-0-only; callers can feed screen 0 or 1.",
    ),
    0x1413CB6F0: (
        "RefreshContextualLookDescriptorForBothScreenRoutes",
        "Two-screen loop inside a higher camera-control owner. Iterates ebx "
        "from 0 to 1, resolves each route, requires the preferred child token "
        "at child+0x1A8 to match the `kCameraContextualLook` sentinel "
        "(0x1430535E0), then pushes a zeroed follow descriptor through "
        "PushCameraDescriptorToExplicitScreenRoute for both screens.",
    ),
    0x141C0E590: (
        "ApplyDescriptorToActiveRouteChildByMode",
        "Selects the first active route child in route+0x520 whose byte +0x78 "
        "is set and whose mode id at +0x18 matches edx, then stages that "
        "child through 0x141C0E500 and forwards the supplied descriptor into "
        "0x141C0ECC0. This is a real mode-based route-child updater, not just "
        "a descriptor cache poke.",
    ),
    0x141C0ECC0: (
        "RecomputeRouteStateFromDescriptorAndSelectedChild",
        "Heavy route-state recompute worker. Locks route+0x2C0, gathers the "
        "preferred child through 0x141C0D4B0 and 0x141C0E9F0, compares that "
        "against route+0x2F0 / route+0x300 override state, and rebuilds the "
        "effective route follow/descriptor payload. This sits downstream of "
        "screen-1-capable descriptor pushes and is the stronger candidate for "
        "stable second-view state than 0x141C0DFD0 alone.",
    ),
    0x140238500: (
        "SwitchAnimatedCameraModeForScreenRouteOwner",
        "Mounted per-screen camera-mode owner worker rooted at player+0x36C0. "
        "Resolves the screen route from owner+0x84, compares the requested "
        "mode id against owner+0x88, dispatches a virtual +0x10 mode-change "
        "callback, and then builds an apReference-extra keyed descriptor/state "
        "record around the current animation contribution id. This sits above "
        "the route-child updater and is part of the animated gameplay camera "
        "stack, not a generic route helper.",
    ),
    0x140238800: (
        "UpdatePlayerAnimatedCameraControlOwnerAndRouteState",
        "Heavy player-mounted animated-camera worker for the owner at "
        "player+0x36C0. Uses the current/secondary actor pairs at +0x70/+0x78 "
        "and +0x60/+0x68, strings 'player-animated', "
        "'animated-camera-control', 'CameraAnimationDirector', and "
        "'apReference-extra', then feeds the resulting descriptor into "
        "0x141C0E590. It also directly consumes and clears the transient "
        "primary pair at +0x60/+0x68 by copying a zeroed inline pair back "
        "into the director after the route/descriptor update. The normal and "
        "fallback paths both end up rebuilding route-child state and "
        "materializing gameplay camera objects under 0x143FE1088.",
    ),
    0x140237E60: (
        "RebuildAnimatedCameraDirectorFallbackDescriptorAndClearPendingPair",
        "Fallback/reset helper for PlayerCameraAnimationDirector. Resolves the "
        "screen route from owner+0x84, refreshes the hidden descriptor track "
        "through 0x1416860A0, marks owner+0x8C active, builds a "
        "'player-animated' fallback descriptor, and feeds it toward the route "
        "camera stack. This sits on the same reset branches as "
        "UpdatePlayerAnimatedCameraControlOwnerAndRouteState and is part of "
        "the consume-and-clear lifecycle for the transient primary pair.",
    ),
    0x140C30290: (
        "ConstructPlayerCameraAnimationDirectorSubobject",
        "In-place constructor for the PlayerCameraAnimationDirector object "
        "stored at player+0x36C0. RTTI on vtable 0x142A04C38 resolves to "
        "PlayerCameraAnimationDirector. Zeroes the primary pair "
        "+0x60/+0x68, seeds the secondary pair +0x70/+0x78 from incoming "
        "player-source rdx->+0xA8/+0xBC when that Player object is present, then "
        "initializes the mode fields +0x80/+0x84/+0x88 and the embedded "
        "contribution band at +0x90..+0xD8. This makes the ctor itself the "
        "first concrete writer of the stable secondary pair family.",
    ),
    0x140237760: (
        "DirectorHasPendingActorPairOrResolvedContribution",
        "Predicate over PlayerCameraAnimationDirector. Returns true when the "
        "pending actor pair at +0x60/+0x68 resolves to a live actor, or when "
        "the current contribution id at +0x80 is not 0xFF. Used directly by "
        "camera descriptor push paths before they fall back to other player "
        "state sources.",
    ),
    0x140C303E0: (
        "AdvanceDirectorContributionRingBaseIfCurrentEntryWithinBounds",
        "Small PlayerCameraAnimationDirector helper that only adjusts the "
        "contribution-ring base pointer at +0xD8 when the current entry falls "
        "within a supplied [r8,r9) window. This does not touch either actor "
        "pair family and is part of the contribution bookkeeping surface, not "
        "a pending-pair seeder.",
    ),
    0x140C349D0: (
        "ResetDirectorContributionStateUsingPrimaryAndSecondaryPairs",
        "PlayerCameraAnimationDirector method that validates both pair "
        "families. If the primary pair at +0x60/+0x68 resolves, increments "
        "the contribution epoch at +0xE4 and may force +0x80 back to 0xFF. "
        "Then clears the contribution ring state at +0xD0/+0xE0, revalidates "
        "the secondary pair at +0x70/+0x78, marks that live actor through "
        "+0x72, and refreshes the contribution timestamp at +0xF0.",
    ),
    0x140239100: (
        "ValidateInlineActorPairAtOffset0",
        "Small inline actor-pair validator used on PlayerCameraAnimationDirector "
        "subfields such as +0x60. Accepts (ptr,id) only when the live actor at "
        "ptr->+8 still matches the stored id at +8.",
    ),
    0x140C34AC0: (
        "AdvancePlayerCameraAnimationDirectorContributionState",
        "Contribution-ring worker for PlayerCameraAnimationDirector. Validates "
        "the secondary actor pair at +0x70/+0x78, marks the resolved actor "
        "live through byte +0x72, compares contribution age against +0xF0, "
        "and updates the contribution queue/state fields at "
        "+0xD0/+0xD8/+0xE0/+0xE4. It does not seed either actor pair family, "
        "so +0x60/+0x68 and +0x70/+0x78 must be established earlier.",
    ),
    0x140C35CF0: (
        "NoopPlayerCameraAnimationDirectorVirtualSlot38",
        "No-op PlayerCameraAnimationDirector method that immediately returns. "
        "This confirms the vtable slot at +0x38 is not part of pending-pair "
        "materialization.",
    ),
    0x141E0ED10: (
        "SetLocomotionControllerModeAndActiveStateLabel",
        "Mode-switch helper over the locomotion/controller object rooted at "
        "owner+0x15B8. Compares incoming mode against controller+0x34, maps "
        "the mode to labels such as None / NavPoly / NavLedge, checks whether "
        "the incoming owner object is currently active through its bitset, "
        "publishes that active-state change through 0x141DE05D0('SetActive', "
        "...), and finally stores the new mode back into +0x34.",
    ),
    0x141E0EDF0: (
        "ApplyLocomotionControllerSnapshotAndResolveModeAuxState",
        "Consumes a 0x30-byte snapshot block for the owner+0x15B8 controller. "
        "Copies incoming transform/state into +0x40/+0x50/+0x60, clears "
        "+0x70/+0x78, and then runs mode-specific follow-up helpers to "
        "materialize auxiliary state under +0x70 when the current mode "
        "requires it. This is a concrete snapshot/apply seam above the old "
        "MoveTo path, not just nav-goal setup.",
    ),
    0x141E18A70: (
        "ApplySharedSourceSnapshotIntoOwner15B8AndPublishDescriptor",
        "Concrete shared block above the owner+0x15B8 controller. Pulls a "
        "snapshot from source+0x230/+0x240/+0x250, resolves the controller via "
        "(owner+0x1D0)+0x15B8, applies mode through 0x141E0ED10 and snapshot "
        "through 0x141E0EDF0, then publishes the result through 0x141E16CF0 "
        "and the 0x14189AF80/0x14189AF30 descriptor-storage pair.",
    ),
    0x141E18D00: (
        "PublishOwnerStateWithoutSharedSnapshotIntoDescriptorStore",
        "Sibling owner-side block next to 0x141E18A70. Does not pull the "
        "shared source snapshot from +0x230/+0x240/+0x250. Instead it refreshes "
        "owner-local mode/state around +0x258/+0x2F8/+0x300, optionally calls "
        "0x141E16CF0, runs 0x141E14890 over owner+0x260, and then publishes a "
        "different descriptor kind through the same 0x14189AF80/0x14189AF30 "
        "storage pair.",
    ),
    0x141E1D110: (
        "AllocateSmallOwnerDescriptorNode_TypeD110",
        "Allocates a 0x20-byte helper object, stores owner back-pointer at "
        "+0x10, clears +0x18, and installs vtable 0x142B898A0 "
        "(constructor entry 0x141E1D190). Used as one of the descriptor-node "
        "types later published through 0x14189AF80.",
    ),
    0x141E1D5C0: (
        "AllocateSmallOwnerDescriptorNode_TypeD5C0",
        "Sibling 0x20-byte helper-object allocator with owner back-pointer at "
        "+0x10 and vtable 0x142B89850 (constructor entry 0x141E1D640). Used by "
        "the same publication/storage family as 0x141E1D110.",
    ),
    0x141E1F890: (
        "AllocateSmallOwnerDescriptorNode_TypeF890",
        "Another sibling 0x20-byte helper-object allocator with owner back-"
        "pointer at +0x10 and vtable 0x142B897D0 (constructor entry "
        "0x141E1F910). This is the concrete descriptor-node type passed from "
        "0x141E18A70 into 0x14189AF80.",
    ),
    0x141E17240: (
        "ResetOwnerSharedSnapshotSourceState",
        "Reset/helper block for the same owner family as 0x141E18A70. Clears "
        "the owner state bands at +0x220..+0x250, resets mode-dependent data, "
        "re-seeds controller-side state through owner+0x1D0/+0x15B8, and then "
        "continues into owner+0x15C8 follow-up work. This is the null-source "
        "fallback used before 0x141E18A00 publishes the small descriptor node.",
    ),
    0x141E181D0: (
        "CheckFirstTapWindowForActionPackKindAC710201",
        "Special gate for action-pack objects with kind tag 0xAC710201. Uses "
        "owner-side time windows at +0x228/+0x248/+0x250, compares them "
        "against the current controller time source under owner+0x1D0+0xE0, "
        "and blocks the rest of the install path when the first-tap window is "
        "still active.",
    ),
    0x141E15A00: (
        "ProcessCommandStopAndStandThroughCurrentActionPack",
        "Owner-side stop-and-stand command path. Builds a snapshot through "
        "0x141E179D0, publishes it through 0x14189AF80/0x14189AF30, then "
        "releases the current action-pack selection through 0x141E18450 and "
        "clears owner+0x2F8 back to -1. String anchors identify this family as "
        "ProcessCommand_StopAndStand.",
    ),
    0x141E15C80: (
        "UpdateStateMovingAndSeedBackupActionPack258",
        "Heavy owner-side moving-state updater. Validates current moving "
        "inputs, resolves the moving action-pack source from owner+0xF0, "
        "stores its id into owner+0x258, copies the shared snapshot into "
        "owner+0x260..+0x280, and updates nearby moving-state bands such as "
        "+0x2E0, +0x440, and +0xEB8..+0xECC. String anchors identify blocked "
        "and disappeared MoveToActionPack / MoveAlongSpline failure cases.",
    ),
    0x141E158B0: (
        "ExitCurrentOwner80CommandStateAndReleaseActionPack",
        "Owner-side exit/cleanup path for the command state described by "
        "owner+0x80. Logs the current owner+0x208 phase, refreshes owner "
        "publication state, inspects the current action-pack object through the "
        "controller, and then routes command-specific cleanup such as stop/"
        "stand and release through the shared action-pack helpers.",
    ),
    0x141E16920: (
        "PublishExitOfReservedActionPackStateAndReleaseCurrentSelection",
        "Owner-side exit path for the reserved-action-pack state "
        "(owner+0x300 == 2). Publishes either the active descriptor node or a "
        "fallback one, refreshes owner+0x238 from controller time, releases the "
        "current action-pack selection through 0x141E18450, and advances "
        "owner+0x208 into the post-exit phase.",
    ),
    0x141E18350: (
        "ValidateCurrentActionPackSourceAndSetOwner304Kind",
        "Resolves the current packed handle at owner+0x2F8, validates the live "
        "action-pack source through source virtual +0xB0 against owner+0x1D0, "
        "stores source kind source+0x60 into owner+0x304, and then runs the "
        "special kind-0xAC710201 gate plus the reserve/prepare path before "
        "marking owner+0x300 = 1 on success.",
    ),
    0x141E18450: (
        "ReleaseCurrentActionPackAndPendingHandleSlots",
        "Release/reset helper for the current action-pack selection. Releases "
        "the live source resolved from owner+0x2F8 through source virtual "
        "+0xD0, clears the five pending handle slots at owner+0x2E4..+0x2F4, "
        "and resets owner+0x300 back to 0.",
    ),
    0x141E18700: (
        "EnterReservedActionPackAndPublishSharedSnapshot",
        "Takes a validated action-pack source object, records owner+0x300 = 2, "
        "copies action-pack data and transforms, applies the shared snapshot "
        "from source+0x230/+0x240/+0x250 into owner+0x260..+0x280, and then "
        "publishes the result through the same owner-side descriptor/storage "
        "tail used by 0x141E18A70.",
    ),
    0x141E189C0: (
        "RefreshOwner238FromControllerE0TimeSource",
        "Small helper that clears the temporary bit under owner+0x1D0+0x1510 "
        "when needed and refreshes owner+0x238 from the controller time source "
        "at owner+0x1D0+0xE0+0x10.",
    ),
    0x141E14890: (
        "StoreMovementMode444AndPreparedSnapshot290",
        "Shared owner-side setter for the prepared movement snapshot. Accepts "
        "a prepared descriptor in r8 and a mode id in edx, validates actor "
        "context through owner+0x1D0 and 0x14386FD00, stores the mode in "
        "owner+0x444, copies the descriptor snapshot into "
        "owner+0x290/+0x2A0/+0x2B0, derives owner+0x2D0 from the current "
        "actor basis, and then runs the heavy follow-up path used by many "
        "ordinary gameplay callers. This is a central ingress for live "
        "movement-state updates, but it still does not write owner+0x80 "
        "directly.",
    ),
    0x141E15760: (
        "DispatchOwner80CommandStateAndPublishOwner120Mirror",
        "Small owner-side dispatcher over owner+0x80. Sets owner+0x208 = 2, "
        "routes mode 1 into 0x141E158B0 and modes 2..4 into 0x141E15C80, then "
        "copies the resolved descriptor fields from owner+0x80 into "
        "owner+0x120..+0x1B0, refreshes owner+0x228/+0x218 from controller "
        "time, and tail-calls 0x141DE0690.",
    ),
    0x141E1A040: (
        "UpdateOwner80StateDescriptorAndDispatchMovementMode",
        "Owner-side dispatcher over the state descriptor stored at owner+0x80. "
        "Uses the current mode value there to branch into moving-state update "
        "logic such as 0x141E15C80, then copies the resolved descriptor fields "
        "from owner+0x80 into owner+0x120..+0x1B0, refreshes owner time fields "
        "+0x228/+0x218, and marks publication flags.",
    ),
    0x141E1A000: (
        "UpdateOwnerMovementStateMachineAndPublishCurrentDescriptor",
        "Broader owner-side movement-state update loop above 0x141E1BA20 and "
        "0x141E1A040. First runs the large queued-entry chooser "
        "0x141E1BA20, then dispatches owner+0x80 through 0x141E15C80 or "
        "0x141E158B0 when needed, mirrors the resolved descriptor into "
        "owner+0x120..+0x1B0, refreshes owner+0x228/+0x218, advances state "
        "flags under +0x468/+0x472, and handles several follow-up cases such "
        "as disappeared traversal action-packs and reserved-action-pack "
        "cleanup. This currently looks like the strongest ordinary-Chloe "
        "movement-state loop above the prepared setter 0x141E14890.",
    ),
    0x141E19F90: (
        "NavStateMachineUpdateBool",
        "Real function start for NavStateMachine::Update(bool). Owns the broad "
        "movement-state update loop whose internal label at 0x141E1A000 runs "
        "0x141E1BA20, dispatches owner+0x80, and then continues into "
        "0x141E1AA20 and other moving/action-pack follow-up work.",
    ),
    0x141E1AA20: (
        "ProcessPathFindResultsAndSwitchPreparedMovementState",
        "Owner-side follow-up inside the broader update loop 0x141E1A000. "
        "Works from the current prepared mode at owner+0x444 together with the "
        "current, reserved, and backup action-pack handles at +0x2F8/+0xE90/"
        "+0x258, logs ProcessPathFindResults strings, releases invalid or "
        "disappeared action-packs when needed, and feeds 0x141E14890 with "
        "replacement modes 0, 1, 2, or 4 through compact descriptors built by "
        "0x141E06C00.",
    ),
    0x141E1BA20: (
        "BuildMovementModeFromQueuedEntriesAndFeedPreparedSetter",
        "Large owner-side chooser over queued entries and current action-pack "
        "state. Unpacks candidate entries through 0x141E06C00, compares them "
        "against owner+0x290 and current action-pack state, and then feeds "
        "0x141E14890 with concrete mode ids 0, 1, 2, 3, or 4 depending on the "
        "accepted path. This is currently the strongest ordinary-Chloe path "
        "above the prepared movement snapshot setter.",
    ),
    0x141E1C820: (
        "ResumeNavigationAndBuildActionPackHandoffSnapshot",
        "Owner-side resume path in the nav state machine. Scans resumable "
        "navigation entries, builds a handoff snapshot through 0x141E179D0, "
        "and then publishes the result through the same descriptor/storage tail. "
        "String anchors identify this family as ResumeMoving / Resume.",
    ),
    0x141E18A00: (
        "ResolveOwner2F8SourceAndApplySharedSnapshotIfPresent",
        "Owner-side dispatcher that resolves the current source handle stored at "
        "owner+0x2F8 via 0x141DF35F0. If the source is absent, it falls back to "
        "0x141E17240 and publishes a small descriptor node of type "
        "0x141E1D110. If the source is present, it consumes source fields "
        "+0x170, +0x230, +0x240, +0x250, updates owner fields such as +0x2FC, "
        "+0x468, and then enters the shared snapshot/apply block at "
        "0x141E18A70 using descriptor node type 0x141E1F890. The source now "
        "reads as a live ActionPack-like object, not an anonymous snapshot "
        "blob.",
    ),
    0x141E18120: (
        "AbandonInterruptedStateAndRebuildMode4Descriptor",
        "Cleanup path identified by the string 'Abandoning interrupted state "
        "%s'. Resets the owner+0x1D8 helper back to the small node type "
        "0x141E1D110, optionally releases the current action-pack selection, "
        "rebuilds a fresh descriptor from owner+0x1D0 through virtual +0x1C8, "
        "feeds mode 4 into 0x141E14890, and then republishes through "
        "0x141DE0690.",
    ),
    0x141E1E4F0: (
        "UpdateStateMovingAndSwitchPreparedModes",
        "Ordinary moving-state update family identified by UpdateStateMoving "
        "strings. Uses the current prepared mode at owner+0x444 together with "
        "the moving action-pack state, can feed mode 2, mode 3, or mode 0 "
        "back into 0x141E14890, checks arrival and waypoint changes, and then "
        "continues through success/failure handling for the moving command. "
        "This currently looks like one of the strongest ordinary movement "
        "families above the prepared movement setter.",
    ),
    0x141E1F300: (
        "PostRootLocatorUpdateAndSwitchPreparedModes",
        "Post-root / interrupted-state update family identified by "
        "PostRootLocatorUpdate strings. Builds temporary descriptors from "
        "owner+0x360/+0x370/+0x380, feeds modes 2, 1, or 0 into "
        "0x141E14890, and then drives publication plus post-root follow-up "
        "checks. Strong live path, but currently looks narrower than the main "
        "moving-state updater.",
    ),
    0x141E20280: (
        "UpdateStatePlayingEnterAnimationAndFeedMode4",
        "Action-pack enter-animation state update identified by "
        "UpdateStatePlayingEnterAnimation strings. If a live current "
        "action-pack exists it feeds mode 4 into 0x141E14890 and publishes the "
        "result; otherwise it falls into failure/exit handling. Useful for "
        "classification, but not the strongest generic movement ingress.",
    ),
    0x141261140: (
        "UpdatePlayerCameraAnimationDirectorFromAnimationContributionFlags",
        "Heavy animation-driven update path above player+0x36C0. Writes "
        "director+0x88 and optional contribution age at +0xF0, gates on bits "
        "under r13+0x50 plus local animation-token filters, and is currently "
        "the only confirmed direct caller of "
        "AdvancePlayerCameraAnimationDirectorContributionState.",
    ),
    0x141C07420: (
        "SetScreenRouteAttachLaneState",
        "Small helper that stores route+0x128 = bool and flips the paired "
        "lane-state bands under 0x143FE1088 + 0xDB0 using 0x141837530.",
    ),
    0x141C074E0: (
        "AttachScreenRouteWithManualFallbackDescriptor",
        "Attach side for the manual-camera path. Sets route+0x128 = 1, flips "
        "the paired 0x143FE1088 lane-state bands, and if route+0x2A8/+0x2B0 "
        "does not already resolve to a live actor, builds fallback descriptor "
        "0x14373A820 and pushes it through "
        "ResolveScreenRouteTargetAndCommitDescriptor.",
    ),
    0x141C07640: (
        "DetachScreenRouteManualBinding",
        "Detach side for the manual-camera path. Clears camera+0x128, flips the "
        "paired 0x143FE1088 lane-state bands, optionally calls retained target "
        "virtual +0x208(...,2), and clears per-screen activity bytes.",
    ),
    0x141C07E20: (
        "WriteManualOrDebugCameraFollowStateIntoResolvedActor",
        "Manual/debug-camera follow writer. Resolves the actor cached under "
        "camera+0x100, validates it through 0x141F53500, then writes "
        "transform/follow state into actor+0x3A0/+0x3B0 and actor+0x1D0..+0x21C. "
        "Observed callers include the manual-camera cluster plus debug-flavored "
        "`teleportplayer` / top-camera selection paths, so this is useful but "
        "not yet the final gameplay camera seam.",
    ),
    0x140EDE5E0: (
        "SwitchPrimaryCameraRetainedBindingByRetainedSlotState",
        "Gameplay-facing manager-0 retained-camera switch. If retained state is "
        "absent, calls 0x1410F3350 then detaches the primary screen-route "
        "entity through "
        "0x141C07640; if retained state is present, calls "
        "RefreshPrimaryRetainedPlayerCamera (0x1410F3DD0) then attaches through "
        "0x141C074E0. Both branches finish by resolving slot 0 via "
        "0x1410F0F60.",
    ),
    0x14015BB60: (
        "CopyLargeCameraStateStruct_0x3F8",
        "Bulk copier for a large camera-state struct spanning at least "
        "0x3F8 bytes. Includes the byte range around +0x150 and the "
        "target-state block around +0x2F0.",
    ),
    0x140882F00: (
        "GetCameraStateFlag153",
        "Tiny accessor that returns byte +0x153 from a camera-state object.",
    ),
    0x140882F10: (
        "BuildRuntimeCameraStateWithRetainedTarget",
        "Builds or refreshes a runtime camera-state object that includes "
        "spatial state, embedded retained target pairs, and flag byte +0x153.",
    ),
    0x140883530: (
        "ActivateEmbeddedRetainedTargetAndUpdateCameraState",
        "Validates the embedded retained pair at +0x80/+0x88, marks the live "
        "entity active if found, then continues into the main camera-state "
        "update path at 0x140883570.",
    ),
    0x140883570: (
        "UpdateRuntimeCameraState_GatedByPrimaryCameraFlag153",
        "Main runtime camera-state update path. Immediately queries "
        "the primary screen-route entity and gates behavior on route+0x153 "
        "before continuing normal simulation over the local camera-state "
        "object; a non-zero flag takes the early-return epilogue rather than "
        "a separate retained-specific worker path.",
    ),
    0x14088AC70: (
        "SyncCameraStateOwnerCompanionRetainedFlags",
        "Validates the retained/live pair cached at owner+0x5F0/+0x5F8, "
        "compares the companion objects at +0x198, propagates retained-camera "
        "slot activity through 0x141754C10, and then dispatches follow-up "
        "companion updates through 0x141754340 or 0x141754820.",
    ),
    0x14088AD40: (
        "UpdateCompanionCameraRigFromTrackedEntity",
        "Builds a companion-camera rig from the tracked entity at owner+0x1A8/"
        "+0x1B0, writes transforms into owner+0x108 and owner+0x420, queries "
        "four child nodes through the embedded camera-state object at +0x4E0, "
        "and marks the owner active via byte +0x440.",
    ),
    0x141BB8C30: (
        "RelocateCompanionCameraRigOwner",
        "Large relocation helper for the companion camera-rig owner. Rebases "
        "tracked pointers at +0x188/+0x198/+0x1A0, a hashed node list behind "
        "+0x1A8/+0x288, multi-pointer clusters at +0x3F8/+0x400 and +0x218, "
        "and dispatches relocation into nested child objects.",
    ),
    0x141BB95C0: (
        "NdGameObject_OnKillProcess",
        "STATIC correction: base OnKill, not companion-camera deleting "
        "destructor. Called by Character141E9C750 at141E9C79E beneath "
        "Player_OnKillProcess. Cleans nested resources, sets +40 reachedOnKill "
        "bit2 at141BB9920, unlinks/clears level+D0 at141BB9933/9938. Runtime "
        "id+BC is invalidated later by KillTree, not by this callback.",
    ),
    0x141BBDD30: (
        "PushCompanionCameraRigPoseToChildNodes",
        "Pushes the owner's cached pose/state vectors at +0x100/+0x120/+0x140 "
        "into child runtime nodes obtained from the object at owner+0x1A0. "
        "Iterates the child-node pointer table at +0x4F8/+0x500 and seeds each "
        "eligible node's transform block when bit 0x400000000000 is set.",
    ),
    0x141C07FC0: (
        "SeedCameraStateFlag153FromGlobalToggle",
        "Writes camera-state byte +0x153 from the shared global toggle at "
        "0x143682FA1.",
    ),
    0x141837530: (
        "JoypadState_UpdateEnabledBinding",
        "Updates or registers the Joypad-enabled byte at JoypadState+0xBE0. "
        "The nonzero mode hashes the literal Joypad and binds that byte through "
        "the property system; it does not write a camera-state blob.",
    ),
    0x141754C10: (
        "SyncObjectBit02ByActiveRetainedCameraSlots",
        "Reads the two-byte global retained-camera slot gate at "
        "0x14383DE48[0..1] and sets or clears bit 0x2 in the target object "
        "through 0x141753DB0 depending on whether either camera slot is active.",
    ),
    0x141754460: (
        "ClearFlockingChildByte1A0ByModeId",
        "Finds the flocking/player entry with matching mode id at +0x18 inside "
        "the companion object and clears byte +0x1A0 on each linked child node.",
    ),
    0x141754820: (
        "ClearFlockingFlag0x40AndCompanionBit0",
        "Clears bit 0x1 on the companion object and clears child-node flag "
        "0x40 in each linked flocking/player entry.",
    ),
    0x141754940: (
        "SetFlockingChildFlag0x80ByModeId",
        "Finds the flocking/player entry with matching mode id and ORs flag "
        "0x80 into each linked child node's +0x18 flags.",
    ),
    0x141754EF0: (
        "DrawControl_SetChildInstanceFlagsByMask",
        "Iterates DrawControl+0x8/+0x14 meshes (stride 0x250) and ORs the "
        "requested mask into every child render instance +0x18 flags. Mask "
        "0x40 excludes the target renderable from the main view.",
    ),
    0x141754FF0: (
        "DrawControl_ClearChildInstanceFlagsByMask",
        "Exact inverse of 0x141754EF0: iterates every DrawControl child render "
        "instance and clears the requested mask from instance+0x18. Used with "
        "mask 0x40 by set-instance-flag-draw-in-main-view(true).",
    ),
    0x1403342C0: (
        "Script_SetInstanceFlagDrawInMainView",
        "Handler registered for set-instance-flag-draw-in-main-view. Resolves "
        "an arbitrary script target object and its DrawControl at object+0x198; "
        "true clears child instance flag 0x40, false sets it. RDI retains the "
        "target object at the false-branch callsite 0x1403343D3.",
    ),
    0x141ECEFE0: (
        "NdAttachableObject_Init",
        "NdAttachableObject initialization path. Owns an independent DrawControl "
        "at object+0x198 and establishes attachment parent state around "
        "+0x4E0/+0x4E8; used by hair/look items and weapon renderables.",
    ),
    0x141BBA060: (
        "NdDrawableObject_AllocateAndInitFgAnimData",
        "Generic drawable-object vtable slot +0x6F0. Allocates FgAnimData, "
        "initializes it from the object's retained runtime identity and authored "
        "render data, and returns the allocation consumed by NdGameObject_Init "
        "at object+0x1A8.",
    ),
    0x141BBACC0: (
        "BindUpperWrapperIntoPlayerCompanionBehaviorAndDrawControlState",
        "Initializes behavior bindings between the rig owner and its companion "
        "flocking/player object at +0x198. Reads mode-dependent booleans from "
        "source descriptors, updates companion-object flags, and dispatches "
        "child-node state changes through 0x141754460/0x141754940. In the real "
        "Player path it is fed from 0x1410CF370 with the extended wrapper "
        "packet at 0x1417C35A0 / vtable 0x1428D5D60 as the third argument and "
        "a draw-control context as the fourth. Direct disassembly now also "
        "shows selector-gating through wrapper+0x10->+0x30 and owner "
        "capability checks before it mutates player+0x198 and builds late "
        "owner arrays, which makes this a downstream upper behavior/draw-"
        "control bind stage rather than the source of Player ownership or the "
        "authored id pair later used by 0x140CC1450.",
    ),
    0x141BB8BC0: (
        "GetCurrentUpperPlayerStateIdFromLiveActorAndWriteOutDword",
        "Real Player vtable slot +0x300. If player+0x190 is non-zero, resolves "
        "the live actor through 0x1417D1170, reads a compact state id from "
        "live_actor->+0x18->+0x10, and writes it to the caller-provided dword. "
        "This is a compact current-state getter used by 0x140CC1450 before the "
        "late `proto`-promotion branch; it is not itself the upper apply seam.",
    ),
    0x1410F1840: (
        "ResolveRetainedPlayerModeFromPair",
        "Seeds an output mode id from the global default at 0x143EEBE7C, then "
        "optionally overrides it from the retained pair's runtime object at "
        "+0x48 via the shared value-reader callback 0x143682D30.",
    ),
    0x1410F18A0: (
        "ResetRetainedPlayerRuntimeGlobals",
        "Clears the retained-player runtime global block rooted at "
        "0x143EEC160/0x143EEC220..0x143EEC262 and seeds the default scalar at "
        "0x143EEC240.",
    ),
    0x140159C00: (
        "LookupSelectorMetadataInRuntimeQueryTable",
        "Binary-searches the selector table at object+0x68 for the hashed "
        "runtime selector id in r8d and returns selector metadata through the "
        "out parameters. Used immediately before retained-entity virtual query "
        "dispatch.",
    ),
    0x141D21C80: (
        "DispatchRetainedEntityRuntimeCommand_0x538",
        "Validates the retained/runtime pair at object+0x60, resolves the live "
        "entity, builds a temporary descriptor through 0x1417C32A0 or "
        "0x1417C35A0 depending on the mode flag, and calls the entity-side "
        "virtual slot +0x538 with that descriptor. The extended 0x1417C35A0 "
        "wrapper therefore is not just shape-compatible with the upper Player "
        "external_state seam; it is already an accepted runtime-command "
        "packet on the generic entity-side interface. Returns a boolean-like "
        "success result.",
    ),
    0x141D21DE0: (
        "EntitySpawner_ResolveParentActorAndTouch",
        "STATIC input is EntitySpawner; +60 is its PARENT spawner, not own "
        "actor holder. Resolves parent+30 record/+38 id through record+8 and "
        "object+BC, writes object+72=1. Own actor pair is directly spawner+30/+38. "
        "Parent identity independently proven by141E7C9DE diagnostic and "
        "141D231E0 recursive parent transform. Not read-only or a retention lease.",
    ),
    0x141D21E20: (
        "EntitySpawner_ResolveEnabledParentActorAndTouch",
        "STATIC resolves PARENT spawner+60's actor, same mutating +72 touch "
        "as141D21DE0, then adds a registry-bit "
        "gate through the entity-owned bitset at entity+0x30 and global index "
        "0x146920B54. Returns null when the live entity exists but is not "
        "enabled in that registry mask.",
    ),
    0x141D21F90: (
        "BuildRetainedEntityQueryRecord_0x30",
        "Builds a 0x30-byte query/result record from the retained/runtime pair "
        "stored at object+0x60. Resolves the active live entity through "
        "ResolveLiveEntityFromRetainedPair60_GatedByRegistryBit, queries the "
        "entity through virtual slot +0x530 using selector metadata from "
        "LookupSelectorMetadataInRuntimeQueryTable, and writes a compact output "
        "record with default scalar/vector fields plus an optional returned id pair.",
    ),
    0x141D231E0: (
        "BuildRetainedEntityParentToJointTransform",
        "Builds a fallback or entity-derived transform for the retained/runtime "
        "pair at object+0x60. Resolves an attachment-capable runtime object, "
        "looks up the selector metadata for 'ParentToJoint', queries the "
        "runtime object, and composes the returned transform with the owner's "
        "local transform when successful.",
    ),
    0x1417C4800: (
        "CheckSelectorHashEnabledInBitsetRegistry",
        "Checks whether the hashed selector id in edx is enabled in the target "
        "bitset registry at rcx. Uses a global hash-bucket table at "
        "0x14383F198 and then tests the corresponding bit in the owner bitset.",
    ),
    0x141DE55E0: (
        "DispatchNestedActionStateViaControlObject_A0",
        "After a guard through owner virtual +0x5E8, resolves the nested "
        "control object from owner+0x15C8 via virtual +0x58 and dispatches the "
        "requested action state in edx through nested virtual +0xA0.",
    ),
    0x141DE5630: (
        "ForwardToNestedControlObject_A8",
        "Resolves the nested control object from owner+0x15C8 via virtual +0x50 "
        "and tail-calls its virtual +0xA8 handler.",
    ),
    0x141DE5660: (
        "ForwardToNestedControlObject_B0",
        "Resolves the nested control object from owner+0x15C8 via virtual +0x50 "
        "and tail-calls its virtual +0xB0 handler when the nested object is present.",
    ),
    0x141DE5690: (
        "SetNestedInjuredLayerFlagByCompanionTable",
        "Hashes the selector string 'injured-layer', scans the companion table "
        "at owner+0x1A0 (+0x298 entries, count +0x2A4, stride 0x18), and writes "
        "a 0/1 byte for the matching entry.",
    ),
    0x141DE57F0: (
        "GetNestedInjuredLayerFlagByCompanionTable",
        "Hashes 'injured-layer', scans the same companion selector table at "
        "owner+0x1A0, and returns whether the matching entry's flag byte is non-zero.",
    ),
    0x141DE5980: (
        "ForwardToNestedControlObject_178",
        "Resolves owner+0x15C8 via virtual +0x20 and tail-calls the returned "
        "nested object's virtual +0x178 handler.",
    ),
    0x141DF0000: (
        "QueryControlBlendScalarFromTypeOrIndexedActionState",
        "Checks the control/type object at owner+0x15C0 against 0x143738130, "
        "gates through owner virtual +0xC20, and otherwise returns either an "
        "indexed scalar from owner+0x1A38 (index owner+0x19E0) or the fallback "
        "scalar at owner+0x1760.",
    ),
    0x141DF00A0: (
        "QueryNestedControlFloat_A8OrDefault",
        "Resolves owner+0x15C8 via virtual +0x78 and tail-calls the returned "
        "object's virtual +0xA8, falling back to the default float at "
        "0x142BE8FC4 when no nested object is available.",
    ),
    0x141DE59B0: (
        "StoreBlobIntoNestedControlObject_188",
        "Resolves owner+0x15C8 via virtual +0x20 and forwards the caller blob "
        "pointer in rdx into the returned nested object's virtual +0x188, then "
        "returns the original blob pointer.",
    ),
    0x141DE59E0: (
        "GetControlTypeRuntimeId_15C0_14C",
        "Reads the control/type object at owner+0x15C0 and stores its dword "
        "field +0x14C into the caller-supplied output pointer.",
    ),
    0x141DE5A00: (
        "GetControlActionStateId_1748",
        "Returns the current dword action/state id cached at owner+0x1748.",
    ),
    0x141DE5A10: (
        "SetControlActionStateIdAndForward_168",
        "Stores edx into owner+0x1748, resolves owner+0x15C8 via virtual +0x28, "
        "and forwards the same state id into the returned nested object's "
        "virtual +0x168.",
    ),
    0x141DE5A40: (
        "ForwardStateIdToNestedControl_158",
        "Resolves owner+0x15C8 via virtual +0x20 and forwards edx into the "
        "returned nested object's virtual +0x158.",
    ),
    0x141DE5A70: (
        "ForwardStateIdToNestedControl_160",
        "Resolves owner+0x15C8 via virtual +0x20 and forwards edx into the "
        "returned nested object's virtual +0x160.",
    ),
    0x141DE5AA0: (
        "ForwardToNestedControlObject_E0",
        "Resolves owner+0x15C8 via virtual +0x20 and tail-calls the returned "
        "nested object's virtual +0xE0.",
    ),
    0x1406B3640: (
        "PatrolScriptSkill_BuildAndApplyExtendedControlRoutingPath",
        "Heavy sibling path in the Patrol@ScriptSkill router at 0x1406B3D70. "
        "Builds a large runtime descriptor stack, allocates/attaches "
        "intermediate objects, updates owner+0x2830 and owner+0x2010, clears "
        "bit 0x8 in owner+0x15AA on success, and writes a scalar into "
        "companion child field +0xD0.",
    ),
    0x1406B3940: (
        "PatrolScriptSkill_BuildAndApplyCompactControlRoutingPathByStateId",
        "Sibling path used by the same Patrol@ScriptSkill router when a "
        "low-cardinality state id in edx is available. Rebuilds local "
        "transforms/state from the owner, consults current locomotion payload "
        "fields around +0x24B0..+0x24C8, and feeds the result into the same "
        "post-routing owner updates.",
    ),
    0x1406B3D70: (
        "PatrolScriptSkill_RouteControlPathByProbeFlagAndStateId",
        "Entry router in the Patrol@ScriptSkill vtable block at 0x14297B260. "
        "Calls 0x1406B2390 to obtain a probe flag and scalar, dispatches either "
        "the extended path or the state-id path, then clears bit 0x80 in "
        "owner+0x1D57 and pushes the scalar into companion child field +0xD0. "
        "This is an AI patrol layer, not the first direct-control seam for a "
        "playable Nadine.",
    ),
    0x1406B2390: (
        "ProbeControlRoutingThresholdsAndSelectStateId",
        "Computes a distance/threshold probe from owner transform and the "
        "pending locomotion command block at owner+0x2470, writes a bool flag "
        "to the caller byte pointer, optionally writes a scalar to the caller "
        "float pointer, and returns a low-cardinality state id used by the "
        "0x1406B3D70 control router.",
    ),
    0x140462020: (
        "ExecuteAuthoredControlSelectionDescriptorModes",
        "Large consumer of the authored task-node/control-selection descriptor "
        "returned by 0x141765580. Branches on descriptor mode 0/1/2/3, builds "
        "selection masks, mutates the global byte table at 0x143D44880, caches "
        "chosen candidate ids, and eventually emits owner-local state near "
        "+0x500/+0x510.",
    ),
    0x140463A70: (
        "RefreshOwnerDecisionBitFromAuthoredDescriptor",
        "Small owner-side wrapper around 0x140462020. Refreshes owner+0x20/"
        "+0x28 from runtime timing pointers, clears owner+0x30 state bits, "
        "executes the authored descriptor consumer, and latches the returned "
        "decision bit back into owner+0x30 bit 0.",
    ),
    0x140463AC0: (
        "UpdateOwnerStateThroughAuthoredDescriptorAndRuntimeGates",
        "Large owner-side update wrapper around 0x140462020. Builds selector "
        "hash state, consults capability gates, conditionally resolves retained "
        "slot 0 and compares transforms, executes the authored descriptor path "
        "when owner+0x30 bit 0 is clear, then continues into mode/timer/object "
        "gates driven by owner+0x30 flags and runtime state.",
    ),
    0x1404641A0: (
        "ResolveTwoSelectorIdsAndPushOwnerRuntimeState",
        "Owner-family sibling in the same 0x14293E240 dispatch table. Resolves "
        "two selector ids through 0x140159C00 using runtime+0xD4 and authored "
        "hash blocks, then pushes the results into owner/runtime state via "
        "0x140461500.",
    ),
    0x1404643C0: (
        "UpdateOwnerThresholdGatedSelectorState",
        "Threshold-gated sibling in the same owner dispatch family. Samples "
        "runtime timing/state from runtime+0xA0, derives a scalar, resolves "
        "selector ids through 0x140159B40/0x140159C00, pushes state through "
        "0x140461500, and falls back to owner+0x40 state-blob notifications.",
    ),
    0x140464710: (
        "ResetOwnerRuntimeStateBlocksAndFlushPendingNode",
        "Owner-family reset/teardown sibling. Clears runtime+0x2830 and "
        "runtime+0x15E0 state blocks, emits a debug/state record if runtime+0x4038 "
        "is live, and flushes the pending node at owner+8->0x30 back into "
        "runtime+0x1F78.",
    ),
    0x140464930: (
        "BuildOwnerMaterializedStateObjectFromActorRuntime",
        "Materialize/build sibling in the same owner dispatch family. Queries "
        "the actor-like runtime object through runtime+0xA0 virtual +0xA10, "
        "builds a large state descriptor on the stack, feeds it through "
        "0x141C9ED50, materializes an intermediate object via 0x14048B270, and "
        "attaches the result back to the owner/runtime through 0x14048F260.",
    ),
    0x141C9ED50: (
        "DispatchOrResolveActorStateDescriptorIntoRuntimeSlot",
        "Consumes a structured actor-state descriptor and a target runtime slot. "
        "Either routes immediately through 0x1417F8910 when the descriptor type "
        "matches a small built-in table, or scans a live registry/roster, "
        "activates matching entities, and dispatches them back through the same "
        "0x1417F8910 path.",
    ),
    0x14048B270: (
        "ConstructMaterializedOwnerStateObject_0x4D0",
        "Ctor/builder for a 0x4D0 materialized owner-state object. Initializes "
        "the object, sets vtable 0x142941FB0, seeds the embedded subobject at "
        "+0xB0 via 0x14048AE80, and optionally copies a small 0x20-byte state "
        "header plus virtual payload from the source descriptor.",
    ),
    0x14048B200: (
        "AllocateAndConstructMaterializedOwnerStateObject_0x4D0",
        "Small allocation wrapper for the 0x4D0 materialized owner-state "
        "variant. Either forwards an existing descriptor into "
        "ConstructMaterializedOwnerStateObject_0x4D0 or allocates 0x4D0 bytes "
        "through the shared allocator and then runs the same constructor.",
    ),
    0x14048B3E0: (
        "DestroyMaterializedOwnerStateObject_0x4D0",
        "Freeing destructor for the 0x4D0 materialized owner-state variant. "
        "Tears down the embedded helper at +0xC0, releases the object-local "
        "state host, and optionally frees the 0x4D0 allocation when requested.",
    ),
    0x14048B450: (
        "TeardownMaterializedOwnerStateObject_0x4D0",
        "In-place teardown for the 0x4D0 materialized owner-state variant. "
        "Releases the embedded helper at +0xC0 and then tears down the object-"
        "local state host without freeing the parent allocation.",
    ),
    0x14048B4A0: (
        "CopyMaterializedStateHeaderAndPayload",
        "Copies the compact 0x20-byte materialized-state header and clones the "
        "embedded payload object through its virtual copy slot.",
    ),
    0x14048F180: (
        "DetachMaterializedStateObjectFromOwnerRuntime",
        "Shared detach primitive for owner+0x30 materialized state objects. "
        "Calls the old object virtual +0x68 when active, writes its state word "
        "back into runtime+0x1F78, clears owner+0x30, and opens the shared "
        "materialized-state update window through global timing/flag state.",
    ),
    0x14048F260: (
        "AttachMaterializedStateObjectToOwnerRuntime",
        "Attaches a materialized state object to owner+0x30, syncs it against "
        "runtime+0x1F78 and runtime+0xE0 timing state, toggles object flags, "
        "stores the owner pointer at object+0x28, and calls the object virtual "
        "+0x60 activation hook.",
    ),
    0x14048F320: (
        "SwapMaterializedStateObjectOnOwnerRuntime",
        "Local owner+0x30 swap helper. Detaches the previous materialized state "
        "object without the global update-window bookkeeping used by "
        "DetachMaterializedStateObjectFromOwnerRuntime, clears its owner pointer "
        "and active bits, then binds and activates the replacement object using "
        "the same object+0x28/object+0x38 setup as the main attach primitive.",
    ),
    0x14048F410: (
        "NotifyMaterializedStateObjectOfTrackedEntityChange",
        "Pushes a tracked live-entity pair into the shared materialized-state "
        "globals at 0x143D45E60, then conditionally notifies the current "
        "owner+0x30 object through virtual +0x50 or +0x48 depending on its "
        "local flags and mode bits.",
    ),
    0x14048BF60: (
        "RefreshMaterializedOwnerStateFromDescriptorAndReadiness",
        "0x4D0-variant refresh/finalize path. Resolves a descriptor from "
        "object+0xB4, calls the owner-side readiness gate at 0x140423440, "
        "sets owner+0x15AA bit 0 when the gate fails, queries the nested "
        "control family through owner+0x15C8 virtual +0x88, conditionally "
        "marks object+0x4C8, runs 0x14048EEF0, then tail-calls 0x140458630.",
    ),
    0x140423330: (
        "CheckOwnerNestedControlReadiness_Minimal",
        "Owner-side readiness gate used ahead of higher-level state updates. "
        "Requires owner+0x15C8, checks the nested control family via "
        "owner+0x15C8->(+8)->+0x28 virtual +0x60 and the collection helper "
        "0x1404C1060, then blocks on several owner virtual gates (+0x4C0, "
        "+0x7D0, +0xA28) before consulting owner+0x1808 virtual slot 0.",
    ),
    0x140423440: (
        "CheckOwnerNestedControlReadiness_WithRuntimeCooldown",
        "Stricter owner-side readiness gate. Requires owner flag "
        "owner+0x57C bit 0x2, owner+0x15C8, nested control readiness from "
        "owner+0x15C8->(+8)->+0x28 virtual +0x60, owner+0x15C8->(+8)->+0xC8 "
        "virtual +0xE0, and the collection helper 0x1404C1060. It then "
        "enforces a runtime cooldown using owner virtual +0xA10 and "
        "owner+0x2308 vs runtime+0xE0->0x10 before consulting owner+0x1808 "
        "virtual slot 0.",
    ),
    0x1404C1060: (
        "AnyNestedControlCollectionEntryReportsFlag68",
        "Iterates a small array-like collection (entries at +0x8, count at "
        "+0x10) and ORs together each non-null entry's virtual +0x68 result.",
    ),
    0x1404C10C0: (
        "AnyNestedControlCollectionEntryReportsFlag70",
        "Sibling of 0x1404C1060 that iterates the same collection and ORs "
        "each non-null entry's virtual +0x70 result.",
    ),
    0x1404C1120: (
        "BuildNestedControlCollectionFlag70Bitset",
        "Builds a two-qword bitset of collection entries whose virtual +0x70 "
        "returns true. Uses the same entry array/count layout as "
        "0x1404C1060/0x1404C10C0.",
    ),
    0x141DEF5D0: (
        "DispatchOwnerLateDecisionObjectSlot08WhenPrimaryAllows",
        "Owner-local proxy into the object at owner+0x1808. Calls virtual slot "
        "+0x0 first; when that returns false, tail-calls the same object's "
        "virtual slot +0x8 with (decision_object, owner).",
    ),
    0x141DEF610: (
        "DispatchOwnerLateDecisionObjectSlot20WhenPrimaryAllows",
        "Owner-local proxy into owner+0x1808. Calls virtual slot +0x0 first; "
        "when that returns true, tail-calls slot +0x20 with (decision_object, "
        "owner, const_float@0x142BEA0A8).",
    ),
    0x141DEF650: (
        "DispatchOwnerLateDecisionObjectSlot18WhenPrimaryAllows",
        "Owner-local proxy into owner+0x1808. Calls virtual slot +0x0 first; "
        "when that returns true, tail-calls slot +0x18 with "
        "(decision_object, owner).",
    ),
    0x141DEF690: (
        "ForwardToOwnerLateDecisionObjectPrimary",
        "Thin wrapper that resolves owner+0x1808 and jumps directly to its "
        "virtual slot +0x0 with (decision_object, owner).",
    ),
    0x140CA17E0: (
        "PlayerActive_Update",
        "Exact source assertion Player::Active::Update(void). Resolves the "
        "owning Player from active-state+0x8, updates broad gameplay state and "
        "refreshes both embedded PlayerLookAtTracker records near the end of "
        "the frame. This replaces the old generic decision-node label.",
    ),
    0x141DE1600: (
        "ConstructOwnerRuntimeClass_WithNestedControlAndDecisionNode",
        "Constructor-like path for the owner/runtime class that contains the "
        "nested control family at +0x15C8, control-mode fields around +0x1748, "
        "and the late-decision node slot at +0x1808. Installs vtable "
        "0x142B82F00 and seeds the larger state cluster through +0x19D8.",
    ),
    0x141DE1CA0: (
        "DestroyOwnerRuntimeClass_WithNestedControlAndDecisionNode",
        "Freeing destructor wrapper for the same owner/runtime class. Calls "
        "the in-place teardown at 0x141DE1CE0 and frees the 0x1C00-byte "
        "allocation when requested.",
    ),
    0x141DE1CE0: (
        "TeardownOwnerRuntimeClass_WithNestedControlAndDecisionNode",
        "In-place teardown for the owner/runtime class rooted at vtable "
        "0x142B82F00. Reinstalls the class vtable, releases the nested "
        "control collection at +0x15C8, frees the owned cluster at +0x15D0, "
        "and clears additional owned state in the +0x1700 region.",
    ),
    0x141DE36E0: (
        "GameCharacter_BuildNpcAnimTreeWithOverlays",
        "GameCharacter/Npc-family vtable slot +0x710. Resolves and commits "
        "anim-overlay-<character>-skel-<skeleton> and "
        "anim-overlay-<character>-look-<look>, then builds the exact anim-npc "
        "tree. The slot is shared by concrete NPC classes including Nadine, "
        "NadineBuddy and Buddy, but is absent from the Player vtable.",
    ),
    0x141DE3C80: (
        "RelocateOwnerRuntimeClass_WithDecisionNodeSlot",
        "Relocate/update path for the same owner/runtime class. Rebases fields "
        "including +0x15A0, +0x1738, +0x1740, +0x1808, +0x19A8 and relocates "
        "the object stored at +0x19D8 via its virtual +0x48.",
    ),
    0x141DE3AC0: (
        "VtableRelocateOwnerRuntimeClass_WithDecisionNodeSlot",
        "Vtable-facing relocate/update method for the same owner/runtime class. "
        "Rebases fields from +0x15B0 upward, including nested-control-adjacent "
        "pointers and owned state used by the control/decision stack.",
    ),
    0x141DE2180: (
        "SetupOwnerRuntimeClassOwnedObject_1728_AndActivateState",
        "Large class-local setup/activation method. Clears owner+0x174C, sets "
        "bit 0x1 in owner+0x15A8, resets owner+0x1728, and uses a global "
        "callback to construct/store an owned object back into +0x1728.",
    ),
    0x141DE99D0: (
        "RefreshOwnerRuntimeClassFromRetainedLiveObject",
        "Class-local retained/live refresh method. Clears owner+0x1500/+0x1508, "
        "resolves a retained/live object through the global retained table "
        "using owner+0x108/+0x10C, copies its leading payload into owner+0x110, "
        "and combines it with owner transforms around +0x120/+0x130.",
    ),
    0x141DE73D0: (
        "ResolveOwnerControlMode1748_WithRuntimeDecisionGates",
        "Large class-local resolver that starts from owner+0x1748 and applies "
        "additional runtime gates before returning a low-cardinality control "
        "mode. Used before dispatching nested control virtual +0x168.",
    ),
    0x141DE8B03: (
        "UpdateOwnerRuntimeStateFromDecisionNodeAndNestedControl",
        "Large owner-runtime update method that combines the late-decision "
        "object at owner+0x1808 (virtual +0x10), nested control state at "
        "+0x15C0/+0x15C8, retained/live refresh, per-entry nested-control "
        "virtual +0x38 fanout, injured-layer publish, and a final owner "
        "virtual +0xE08 call.",
    ),
    0x1403DD180: (
        "GetMaterializedStateSlotOwner",
        "Tiny getter that returns the managed materialized-state slot at "
        "owner+0x1F80.",
    ),
    0x140409160: (
        "GetAttachedMaterializedStateObject",
        "Returns the currently attached materialized state object from the "
        "managed slot at owner+0x1F80 -> +0x30.",
    ),
    0x140409170: (
        "DetachAttachedMaterializedStateObject",
        "Thin owner-side wrapper over DetachMaterializedStateObjectFromOwnerRuntime "
        "using the managed slot at owner+0x1F80.",
    ),
    0x140409180: (
        "AttachMaterializedStateObjectViaOwnerSlot",
        "Thin owner-side wrapper over AttachMaterializedStateObjectToOwnerRuntime "
        "using the managed slot at owner+0x1F80.",
    ),
    0x1404091A0: (
        "ClearAttachedMaterializedStateObject_Local",
        "Local clear path for the currently attached materialized state object. "
        "Replays the object virtual +0x68 when needed, writes the state word "
        "back into owner+0x1F78, and clears slot+0x30 without opening the "
        "global detach/update window.",
    ),
    0x1404280D0: (
        "RelocateMaterializedStateOwnerCluster",
        "Relocates the owner cluster that includes +0x1F78/+0x1F80/+0x1F88/"
        "+0x1FB0/+0x1FF8. Invokes per-object relocate virtuals and then "
        "rebases in-range pointers by the supplied delta.",
    ),
    0x1403E0400: (
        "QueryMaterializedStateSelectionRecord",
        "Builds a small output record by querying the attached materialized "
        "state object through virtual +0x30, then falling back through "
        "owner+0x1FF0 virtual +0x68 and 0x1403E02C0 when needed.",
    ),
    0x1403F9570: (
        "GetSelectorCapableAttachedMaterializedStateObject",
        "Returns owner+0x1F80->+0x30 only when the attached materialized "
        "object exists and its selector/capability id at +0x20 is enabled "
        "through CheckSelectorHashEnabledInBitsetRegistry (0x1417C4800).",
    ),
    0x14043B710: (
        "HasSelectorCapableAttachedMaterializedStateObject",
        "Boolean helper over owner+0x1F80->+0x30. Returns true only when the "
        "attached materialized object exists and its selector/capability id at "
        "+0x20 is enabled through CheckSelectorHashEnabledInBitsetRegistry.",
    ),
    0x1404B7D50: (
        "ReplaceAttachedMaterializedStateObject_WithVariant540",
        "Detaches the current owner+0x1F80 materialized-state object, allocates "
        "0x540 bytes, constructs the replacement through 0x14045C0D0, then "
        "reattaches it through AttachMaterializedStateObjectToOwnerRuntime.",
    ),
    0x1404B7E30: (
        "ReplaceAttachedMaterializedStateObject_WithVariant48",
        "Detaches the current owner+0x1F80 materialized-state object, allocates "
        "0x48 bytes, constructs the replacement through 0x14045B8D0, then "
        "reattaches it through AttachMaterializedStateObjectToOwnerRuntime.",
    ),
    0x140473590: (
        "ConstructBehaviorScavengeMaterializedStateVariant570",
        "Constructs a 0x570 BehaviorScavenge materialized-state object. Seeds "
        "its local behavior block, installs the 0x14293F7D0 vtable, initializes "
        "the +0x40 subobject, and attaches the caller-supplied snapshot/source "
        "payload through 0x140473020.",
    ),
    0x140473460: (
        "ConstructBehaviorScavengeMaterializedStateVariant570Base",
        "Shared base ctor for the 0x570 BehaviorScavenge materialized-state "
        "family. Initializes the base behavior object through 0x14048EC20, "
        "installs vtable 0x14293F7D0, initializes the +0x40 subobject, and "
        "seeds tail fields around +0x538..+0x560 before caller-specific payload "
        "copy runs.",
    ),
    0x140480230: (
        "ConstructBehaviorShootMovingMaterializedStateVariant9B0",
        "Constructs the 0x9B0 materialized-state variant tagged with the "
        "BehaviorShootMoving label. Seeds the generic base, installs its vtable, "
        "copies the caller payload into +0x40 and +0x570-side state, and "
        "materializes shot-moving flags around +0x980..+0x9A1.",
    ),
    0x14048A260: (
        "ConstructBehaviorTurnToFaceMaterializedStateVariant70",
        "Constructs the 0x70 materialized-state variant tagged with the "
        "BehaviorTurnToFace label. Seeds the generic base and copies the small "
        "turn-to-face payload into +0x40..+0x64.",
    ),
    0x1404B7F00: (
        "InstallBehaviorScavengeMaterializedStateVariant570FromLocoSnapshot",
        "Indirect BehaviorScavenge install path for owner+0x1F80. Builds a "
        "0x570 materialized-state payload from the owner's locomotion snapshot "
        "and optional actor/source input, detaches the current slot object, "
        "constructs the replacement through 0x140473590, then reattaches it "
        "through AttachMaterializedStateObjectToOwnerRuntime.",
    ),
    0x1404B80C0: (
        "DispatchBehaviorScavengeOwnerVirtualD58WithPreparedState",
        "BehaviorScavenge-side dispatcher that materializes a small state block "
        "through 0x141DE1280 and then calls owner virtual +0xD58 with the "
        "prepared payload and external source object.",
    ),
    0x1404B8180: (
        "RegisterBehaviorScavengeMetadataRoots",
        "Publishes the BehaviorScavenge label pointer and the paired metadata "
        "root at 0x142952020 into globals 0x143048978/0x143048970.",
    ),
    0x140665923: (
        "ReplaceAttachedMaterializedStateObject_WithVariant9B0",
        "Detaches the current owner+0x1F80 materialized-state object, allocates "
        "0x9B0 bytes, constructs the replacement through 0x140480230, then "
        "reattaches it through AttachMaterializedStateObjectToOwnerRuntime.",
    ),
    0x1403DB300: (
        "GetOwnerStateHelper_1FD8",
        "Tiny owner accessor that returns the helper object stored at "
        "owner+0x1FD8.",
    ),
    0x1403DB310: (
        "GetOwnerStateHelper_1FE0",
        "Tiny owner accessor that returns the helper object stored at "
        "owner+0x1FE0.",
    ),
    0x1403DB320: (
        "GetOwnerStateHelper_1FE8",
        "Tiny owner accessor that returns the actor-bound runtime helper "
        "stored at owner+0x1FE8.",
    ),
    0x1403FC050: (
        "GetOwnerStateHelper_1FD8_Duplicate",
        "Duplicate tiny owner accessor for the helper object at owner+0x1FD8.",
    ),
    0x1403FC060: (
        "GetOwnerStateHelper_1FE0_Duplicate",
        "Duplicate tiny owner accessor for the helper object at owner+0x1FE0.",
    ),
    0x1403FC070: (
        "GetOwnerStateHelper_1FE8_Duplicate",
        "Duplicate tiny owner accessor for the actor-bound runtime helper at "
        "owner+0x1FE8.",
    ),
    0x1403FC080: (
        "GetOwnerStateHelper_1FF8",
        "Tiny owner accessor that returns the helper object stored at "
        "owner+0x1FF8.",
    ),
    0x1403FC090: (
        "GetOwnerStateHelper_1FF8_Duplicate",
        "Duplicate tiny owner accessor for the helper object at owner+0x1FF8.",
    ),
    0x1404024E0: (
        "InitOwnerStateHelper_1FE8",
        "Zero-initializes the small actor-bound runtime helper later stored at "
        "owner+0x1FE8. Clears the leading handle/id pair and owned pointers at "
        "+0x28/+0x30.",
    ),
    0x140643570: (
        "BindOwnerStateHelper_1FE8_ToActorRuntime",
        "Owner-side binder for the 0x48 helper stored at owner+0x1FE8. Seeds "
        "it from the owner's retained/live pair at +0xA8/+0xBC, initializes "
        "its local state id at +0x10, clears +0x28/+0x38, and continues into "
        "the deeper actor-bound runtime setup path.",
    ),
    0x14041F1A0: (
        "RefreshOrSelectCurrentStateObject_1FF0",
        "Npc/Player skill think selector. Scans skill candidates; installs current "
        "Skill* at owner+0x1FF0; logs Entering/Exiting skill; calls Clear on failure.",
    ),
    0x140664690: (
        "Skill_IsAuthorityForRootTransform",
        "Skill vtable +96. Always returns 1 (base/Script/Idle/NetSkill). Negated in "
        "Npc_SyncControlHostEntityRootOnNetSkip clear-path — clear branch dead for "
        "standard skills.",
    ),
    0x1401D4F10: (
        "Player_Vtable4256_ReturnFalse",
        "Player vtable +4256 (0x10A0). Always returns 0; gates per-player "
        "ControlHostHub_RefreshEntity6EProbeFlags in GameplayWorld_RefreshControlHostEntity6EFlags.",
    ),
    0x140CC1DA0: (
        "Player_IsAliveForSkillAndDispatch",
        "Player vtable +2144 (0x860). Health/alive predicate; used in skill exit and "
        "3E59D0 entity6E refresh gate with player+5545 bit1.",
    ),
    0x140416AB0: (
        "GetCurrentStateObject_1FF0",
        "Tiny owner accessor that returns the currently installed gameplay-state "
        "selection object at owner+0x1FF0.",
    ),
    0x140416AC0: (
        "GetCurrentStateObject_1FF0_Duplicate",
        "Duplicate tiny accessor for the currently installed gameplay-state "
        "selection object at owner+0x1FF0.",
    ),
    0x140416AD0: (
        "GetCurrentStateObjectLabel_1FF0",
        "Returns current_state_object+0x70 from owner+0x1FF0, or a fallback "
        "label pointer when no current object is installed.",
    ),
    0x140416AF0: (
        "CopyCurrentStateSelector_1FF0",
        "Copies the current gameplay-state selector/hash from "
        "owner+0x1FF0->+0x78 into caller storage, defaulting to 0 when no "
        "current object is installed.",
    ),
    0x140416B10: (
        "GetCurrentStateSignedId236C",
        "Returns owner+0x236C, which is synchronized from the current "
        "gameplay-state object's signed id at +0x7C.",
    ),
    0x140416B20: (
        "InstallOrApplyCurrentStateRecord",
        "Owner-side install/apply entry for a current-state record. Clears any "
        "attached materialized state at owner+0x1F80->+0x30, seeds owner "
        "runtime state through 0x141DE9E60, refreshes owner transform caches "
        "at +0x4160..+0x4180, validates/logs the record, updates owner cache "
        "fields +0x4600..+0x460A, and on the zero-state branch dispatches the "
        "heavy nested-control apply path 0x141DE4F90. NOTE: this is a SHARED "
        "character virtual; it sits at NadineBuddy vtable slot +0xC48 (the slot "
        "earlier seen producing the 'native teleport' effect, which is really a "
        "state-record apply side effect). So NadineBuddy already exposes this "
        "control-apply ingress that reaches 0x141DE9E60.",
    ),
    0x141D63420: (
        "CharacterManager_AddCharacter",
        "CharacterManager::AddCharacter(rcx=manager=*(0x143898630), rdx=character). "
        "Array base manager+0, capacity manager+8, count manager+0xC; 16-byte "
        "entries {character+0xA8 ptr, character+0xBC id}. Dedupe-scans, writes "
        "array[count], increments count, then notifies observers at "
        "manager+0x10..+0x18 via node->vtable[0](node, character). This is the "
        "real CharacterManager registration path, but it is NOT the campaign "
        "blocker for second-player routing: Nadine is already registered there. "
        "The separate local-player resolver lives under 0x143FE1070.",
    ),
    0x141D63640: (
        "CharacterManager_RemoveCharacter",
        "Inverse of AddCharacter: scans the {ptr,id} array for the character, "
        "compacts it down, decrements count at manager+0xC, notifies observers "
        "via node->vtable+8(node, character), and calls character->virtual+0x810 "
        "(0x141FEBE20).",
    ),
    0x141D63C50: (
        "CharacterManager_GetCharacter",
        "Index accessor: asserts index<capacity(manager+8), NOT live count(+C); "
        "source index < m_maxCharacterCount, character-manager.cpp:231. "
        "Entry=array[index] (index<<4 + manager+0); requires nonzero record/id, "
        "resolves object=record+8 and validates object+BC==entry.id. Writes "
        "object+72=1 and returns object, NOT the record. No local manager lock "
        "or retention lease. This is the CharacterManager array accessor itself; "
        "do not conflate it with the separate local-player resolver owner at "
        "0x143FE1070.",
    ),
    0x140486EB0: (
        "BehaviorTeleport_ComputeAimBasisAndSeedOwnerStateViaDE9E60",
        "BehaviorTeleport-specific aim/transform virtual (sole vtable slot ptr "
        "at 0x142941490; RTTI = BehaviorTeleport). Validates a retained slot via "
        "0x14386FD00, builds an orientation/aim basis from runtime objects "
        "using record +0xA0/+0xC0/+0xF0 quaternion math, then seeds owner "
        "runtime state through the control bridge 0x141DE9E60. This confirms "
        "that one direct path above 0x141DE9E60 is AI behavior logic, not raw "
        "player input.",
    ),
    0x14041C7E0: (
        "ActivateCurrentStateObject_1FF0",
        "Owner-side activation path for owner+0x1FF0. Clears owner state bits, "
        "dispatches current_state_object virtual +0x30, and clears bit 0x8 in "
        "current_state_object+0x80.",
    ),
    0x14041C820: (
        "ClearCurrentStateObject_1FF0",
        "Owner-side teardown/reset path for owner+0x1FF0. Releases nested "
        "attachments, dispatches current_state_object virtual +0x38, syncs "
        "owner flags and signed ids from +0x80/+0x7C, clears owner+0x1FF0, "
        "and drops a mode bit in owner+0x1FA8.",
    ),
    0x140416F90: (
        "ReportAndClearCurrentStateObject_1FF0",
        "If owner+0x1FF0 exists, reports its +0x70 label through "
        "0x1403CF8B0 and then jumps into ClearCurrentStateObject_1FF0.",
    ),
    0x140417040: (
        "CheckOwnerReadinessViaNestedControlAndHelper1FF8",
        "Compact owner-side readiness gate. First queries nested control at "
        "owner+0x15C8 via virtual +0x50/+0xC8, otherwise falls back to owner "
        "virtual +0x8C0, touches helper owner+0x1FF8 via virtual +0xA0, and "
        "under negative mode byte in owner+0x1FA8 dispatches nested-control "
        "virtual +0x58/+0xC0 before returning success.",
    ),
    0x14041C900: (
        "ReconcileCurrentStateObjectAndNestedControl",
        "Owner-side monitor/reconcile path over current_state_object at "
        "owner+0x1FF0, nested control at owner+0x15C8, and owner caches "
        "+0x1EC0/+0x1EC4. Uses owner bytes +0x1D51/+0x1D52 as pending gates, "
        "logs current labels and state names, compares nested-control state "
        "through virtual +0x50/+0xB0, and can dispatch current_state_object "
        "virtual +0x28 or nested-control virtual +0xA0/+0xD0.",
    ),
    0x14041CBF0: (
        "ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode",
        "Large owner-side actor-state metric builder. Validates the authored "
        "descriptor at owner+0x108/+0x10C, pulls a runtime object through "
        "owner virtual +0xFB8, combines its orientation with owner transforms, "
        "applies late-decision gating through owner+0x1808 and owner virtual "
        "+0x1018, selects nested-control branches via owner+0x15C8, and "
        "stores the resulting scalar back into helper state under owner+0x1A0 "
        "and owner virtual +0xCE8.",
    ),
    0x14041FA90: (
        "RefreshOwnerRuntimeHelperBundle_1FC0_1FD8_1FE8_1FF8",
        "Owner-side update over the helper bundle after +0x1F80. Refreshes the "
        "attached materialized-state slot, conditionally updates helpers "
        "+0x1FC0/+0x1FD8/+0x1FE8/+0x1FF8, rebinds the actor-bound helper "
        "owner+0x1FE8 to the current live actor, then combines late-decision "
        "and nested-control scalar paths into the helper state under "
        "owner+0x1A0 and owner virtual +0xCE8.",
    ),
    0x14041CED0: (
        "ResolveOwnerReferenceTransformViaHelper1FC0OrRetainedPlayers",
        "Produces a reference transform for the owner by first consulting the "
        "helper at owner+0x1FC0, then selector-capable helper objects, and "
        "finally falling back to retained player slots 0/1 through "
        "0x1410F0D10 before reading the chosen actor transform at +0x100.",
    ),
    0x14041D0A0: (
        "PublishOwnerRuntimeRecordsFromHelper1FC0Candidates",
        "Large owner-side publish/refresh pass over helper owner+0x1FC0. Locks "
        "the helper, enumerates candidate entries through 0x140640360, filters "
        "them through 0x14063CF00 and retained/live matching, synthesizes large "
        "runtime record blocks, and pushes those blocks into the current live "
        "actor through virtual +0xD0.",
    ),
    0x14041D710: (
        "CheckOwnerReadinessViaHighLevelObjectOrNestedControl",
        "Owner-side readiness gate that first tries a higher object through "
        "owner virtual +0x9B0 and two subqueries via 0x14128C910, then falls "
        "back to nested control owner+0x15C8 via virtual +0x80/+0x180/+0x128, "
        "with additional modulation by owner virtual +0x4C0, global mode, and "
        "owner byte +0x1D0B.",
    ),
    0x14041D820: (
        "CheckOwnerHelper1F90Availability",
        "Availability gate over owner+0x1F90. If helper mode +0x840 is not 1, "
        "it resolves an owner object through virtual +0xE20, passes it into "
        "0x1405F3F10, and requires the returned object to have byte +0x189 "
        "set before reporting success.",
    ),
    0x14063D800: (
        "RefreshHelper1FC0HostAnchorsFromPreferredCandidate",
        "Post-pass over owner+0x1FC0 that selects a preferred candidate entry, "
        "then refreshes four runtime anchors behind owner+0x1FB0->+0x10. It "
        "computes world-space vectors from helper entries and live actors, "
        "writes them into anchor regions around +0x460/+0x4D0/+0x540/+0x5B0, "
        "and toggles per-anchor active flags at +0x6E.",
    ),
    0x1404370C0: (
        "RunHelper1FC0HostAnchorPostPass",
        "Thin owner-side wrapper that forwards owner+0x1FC0 into "
        "RefreshHelper1FC0HostAnchorsFromPreferredCandidate as part of the "
        "regular gameplay-state update loop.",
    ),
    0x140436F20: (
        "SyncHostAnchors620_690FromCurrentActorAndStateVector",
        "Owner-side anchor sync that updates two host anchor regions at "
        "owner+0x1FB0->+0x10 +0x620/+0x690 from the current actor and helper "
        "owner+0x1FF8, gated by owner bytes +0x1D54/+0x1D56 and actor tag "
        "0xAC710200.",
    ),
    0x140439E60: (
        "RunGeometryThresholdGateForNestedControlAction",
        "Heavy geometry/threshold gate that combines current-actor transforms, "
        "nested-control state from owner+0x15C8, owner transform +0x1C8, and "
        "helper-resolved reference geometry. If the final scalar falls below "
        "threshold, it seeds owner-side scalar state and triggers a nested-"
        "control action through virtual +0x78.",
    ),
    0x14043A2F0: (
        "PrepareHighLevelActionTargetsFromOwnerState",
        "Large high-level action/target preparation pass over the owner state. "
        "Combines owner byte +0x1D59, owner virtual +0x9B0, the current-state "
        "object at +0x1FF0, helper availability through +0x1FC0/+0x1F90, "
        "retained/live actor capability bits, global table lookups via "
        "+0x1E84, and candidate-array iteration to select valid target/action "
        "state.",
    ),
    0x14043AC60: (
        "ContinueHighLevelActionTargetPreparation",
        "Sibling to PrepareHighLevelActionTargetsFromOwnerState. Reuses the "
        "same owner byte +0x1D59, +0x1E84 table, owner virtual +0x438/+0x9B0, "
        "capability-bit tests, and candidate-array scans to continue the same "
        "high-level action/target-preparation family.",
    ),
    0x14043B380: (
        "UpdateOwnerDescriptorCapabilityFlag18B0",
        "Clears owner+0x18B0, resolves the authored descriptor at "
        "owner+0x108/+0x10C, follows descriptor+0x68 into a retained/live "
        "runtime object, and sets +0x18B0 if that object exposes the required "
        "capability bit.",
    ),
    0x14043B490: (
        "EnsureDefaultAnimStateNpcInfoKnowledgeRecord",
        "Owner-side helper that resolves or creates the shared knowledge record "
        "for 'default-anim-state-npc-info' through 0x1417651D0 when the "
        "required subrecord is not already present on the current owner-side "
        "runtime object.",
    ),
    0x14043B5B0: (
        "EnsureDefaultAnimStateNpcInfoKnowledgeRecord_Variant",
        "Sibling helper to EnsureDefaultAnimStateNpcInfoKnowledgeRecord that "
        "builds the same 'default-anim-state-npc-info' record id and resolves "
        "it through 0x1417651D0 from a nearby owner/runtime path.",
    ),
    0x14043B740: (
        "PublishResolvedKnowledgeRecordsIntoSink",
        "Resolves the owner's current knowledge/state record and additional "
        "records from owner+0x2388 through 0x141765030, then publishes each "
        "resolved record into an external sink via 0x1417F9150.",
    ),
    0x140425BC0: (
        "CheckOwnerDecisionGateViaActorStateScalar",
        "Higher-level owner decision gate that first tries a cached live object "
        "at +0x23D8/+0x23E0, otherwise requests a direction/target vector via "
        "0x141DE6500 and feeds it into "
        "ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode.",
    ),
    0x1404586B0: (
        "InitMaterializedStateHost",
        "Initializes the object-local materialized-state host used by the 0x4D0 "
        "variant. Seeds the embedded sub-vtable at +0x40 and clears the owned "
        "runtime/object pointers up through +0xA0.",
    ),
    0x1404588A0: (
        "TeardownMaterializedStateHost",
        "Tears down the object-local materialized-state host. Releases the "
        "owned runtime/object pointers at +0x50/+0x90 and restores the host "
        "vtable state.",
    ),
    0x141765580: (
        "ResolveGlobalTaskNodeThresholdRecord",
        "General resolver that looks up hashed/global task-node records using "
        "the strings 'global' and '*start-task-node*'. Returns either a direct "
        "registry hit or a fallback/global task node record consumed by "
        "ProbeControlRoutingThresholdsAndSelectStateId and other systems.",
    ),
    0x141765030: (
        "ResolveKnowledgeRegistryEntryById",
        "Global registry lookup for authored knowledge/state records by "
        "integer id. Searches sorted and hashed secondary containers and "
        "returns the matching registry entry payload when present.",
    ),
    0x1417651D0: (
        "ResolveOrCreateKnowledgeRegistryEntryById",
        "Wrapper over ResolveKnowledgeRegistryEntryById that returns an "
        "existing knowledge/state entry when present and otherwise enters the "
        "creation/assert path for a missing registry record.",
    ),
    0x141DF35F0: (
        "ResolveRuntimeObjectFromPackedHandle",
        "Resolves a packed runtime handle/id through the global handle tables "
        "and returns the live runtime object only when the handle and object "
        "id still agree.",
    ),
    0x141DE4F90: (
        "ApplyStateRecordThroughNestedControlAndActorRuntime",
        "Heavy owner-side apply path for a current-state record object. Builds "
        "a temporary control/runtime block from owner+0x15B8, resolves/evicts "
        "competing selection through owner+0x15C0+0x2F8, deactivates the "
        "previous nested-control node through virtual +0xC0, applies the new "
        "one through virtual +0xE0 with packed owner transforms, then commits "
        "the record-object transform through owner virtual +0xC48 and finalizes "
        "via owner+0x15C0/+0x15B8, including a final 0x141E0EDF0 snapshot push.",
    ),
    0x141DE5190: (
        "ApplyPreparedNavCharacterStateRecordWithBlendScalars",
        "Prepared-state wrapper above 0x141E15030. Copies the incoming compact "
        "record, injects two extra scalar inputs into the prepared payload, "
        "forces record kind 4, then routes the result into owner+0x15C0 "
        "through the shared nested-control sink.",
    ),
    0x141DE5430: (
        "ResolveSharedNestedControlRuntimeObjectBlock310",
        "Owner-side helper that resolves nested-control handles at +0x2F8 and "
        "+0x258 through 0x141DF35F0, verifies that both point to the same live "
        "runtime object, and then returns the follow-up control block at "
        "owner+0x15C0+0x310.",
    ),
    0x141DE6500: (
        "BuildOwnerDirectionVectorFromStateSlotsAndDescriptor",
        "Synthesizes a world/reference vector from owner state slots "
        "+0x18F0/+0x1900/+0x1910 selected by flags +0x1921/+0x1922, validates "
        "the authored descriptor at +0x108/+0x10C, "
        "refreshes owner+0x110, and combines that descriptor offset with owner "
        "transform +0x120 before writing the final vector to caller storage.",
    ),
    0x141DE6270: (
        "NavCharacter_SetBaseTargetPointWorld",
        "Refreshes the owner transform, converts the supplied world point into "
        "owner-local space, writes it to the base target slot at +0x18F0 and sets "
        "the corresponding validity byte at +0x1920.",
    ),
    0x14063CF00: (
        "CheckHelper1FC0CandidateAgainstRetainedPair",
        "Helper-side filter for owner+0x1FC0. Enumerates helper entries, "
        "matches them against an incoming retained/live pair, and rejects "
        "special entry kinds identified by state byte +0x190 before returning "
        "a bool-like match result.",
    ),
    0x14063C260: (
        "FindHelper1FC0EntryMatchingRetainedPair",
        "Helper-side lookup that scans the owner+0x1FC0 entry pool, resolves "
        "each enabled entry through virtual +0x10, and returns the first entry "
        "whose retained/live pair matches the input pair.",
    ),
    0x140640360: (
        "CollectHelper1FC0EntriesByStateKind",
        "Enumerates the entry pool behind owner+0x1FC0, filters entries by the "
        "requested state-kind byte at entry->virtual +0x10 -> +0x190, and "
        "appends matching entries into a bounded output list.",
    ),
    0x14063E890: (
        "PublishPreparedStateRecordIntoHelper1FC0",
        "Helper-side publish/apply path for a prepared owner/action record. "
        "Locks helper+0xC0, scans watcher nodes around +0x1D8 via virtual "
        "+0x28, allocates and initializes a watcher object when needed, then "
        "dispatches the incoming record through watcher virtual +0x30.",
    ),
    0x1406427E0: (
        "InitHelper1FC0PublishedRecordObject",
        "Initializes the helper-side published record object used by "
        "PublishPreparedStateRecordIntoHelper1FC0. Installs the object vtable, "
        "clears embedded retained/live pairs and helper subobjects, and seeds "
        "a unique object id.",
    ),
    0x14040DB30: (
        "CacheSelectedActionTargetSnapshot",
        "Caches a selected action target back into owner state. Copies target "
        "transform/orientation blocks into owner+0x1ED0..+0x1F30, stores the "
        "target retained/live pair at +0x1F48/+0x1F50, and sets owner byte "
        "+0x1D55 bit 0x2.",
    ),
    0x140404240: (
        "LatchOwnerPerceptionBit40FromHelperType6",
        "Helper-driven owner latch that resolves a +0x1FC0 entry, requires "
        "entry kind 6 and several inactive helper predicates, then sets "
        "owner+0x1D59 bit 0x40.",
    ),
    0x140407910: (
        "CheckCurrentActorGlobalSlotBusy",
        "Scans the global actor-slot table for the current actor identity and "
        "reports whether the matching slot is already occupied or otherwise "
        "busy according to per-slot flags and counts.",
    ),
    0x140421E70: (
        "ProcessOwnerPerceptionEventTagAndUpdateRecentState",
        "Processes owner event tags returned through virtual +0x10D8. Handles "
        "two special hashes by setting owner+0x1D59 bits 0x1/0x2, snapshotting "
        "current runtime handles into +0x2190/+0x2198, and rebuilding derived "
        "vectors at +0x2450/+0x2460 from the current descriptor and event "
        "source position.",
    ),
    0x140422100: (
        "CheckOwnerRecentPerceptionBit1Fresh",
        "Reports whether owner+0x1D59 bit 0x1 is still fresh by comparing the "
        "current runtime handle against the snapshot stored at +0x2190 within "
        "a short time window.",
    ),
    0x140422140: (
        "CheckOwnerRecentPerceptionBit2Fresh",
        "Reports whether owner+0x1D59 bit 0x2 is still fresh by comparing the "
        "current runtime handle against the snapshot stored at +0x2198 within "
        "a short time window.",
    ),
    0x14043B380: (
        "UpdateOwnerDescriptorCapabilityFlag18B0",
        "Clears owner+0x18B0, resolves the authored descriptor at "
        "owner+0x108/+0x10C, follows descriptor+0x68 into a retained/live "
        "runtime object, and sets +0x18B0 if that object exposes the required "
        "capability bit.",
    ),
    0x14043B740: (
        "PublishResolvedKnowledgeRecordsIntoSink",
        "Resolves the owner's current knowledge/state record and additional "
        "records from owner+0x2388 through 0x141765030, then publishes each "
        "resolved record into an external sink via 0x1417F9150.",
    ),
    0x1417F9150: (
        "CommitResolvedKnowledgeRecordIntoSinkSlot",
        "Sink-side slot/registry commit. Resolves a destination slot from the "
        "incoming record key, writes record+0x10 into the sink value array, "
        "writes the record pointer into the parallel pointer array, and then "
        "finalizes the sink through 0x1417FA5A0.",
    ),
    0x1417FA5A0: (
        "FinalizeOrRefreshStructuredSinkState",
        "Large sink-side finalize/refresh helper used after slot insert/clear. "
        "Builds a CRC/hash-like accumulator over sink entries and metadata and "
        "refreshes the sink's derived state.",
    ),
    0x141E63660: (
        "BroadcastKnowledgeRecordsFromOwnerManager1FA0",
        "Manager-wide outward broadcast over owner+0x1FA0. Iterates entry "
        "records, resolves live actors, builds compact hashed records, and "
        "pushes them into those actors through virtual +0xD0.",
    ),
    0x141EC4E90: (
        "RunWorldSpaceProbeBetweenTransforms",
        "World/probe-system query between two transform vectors plus an "
        "external config/output record. Validates both vectors, packages them "
        "for the world query layer, and dispatches the final probe through "
        "0x1420A7D30.",
    ),
    0x14123F260: (
        "ResolveGlobalKnowledgeStateRecordById",
        "Global table/cache-driven resolver for authored knowledge/state "
        "records. Resolves a record object by id through registry helpers and "
        "returns the record whose flag block is later consumed by owner "
        "perception/action code.",
    ),
    0x1406AD290: (
        "AdvanceControlRoutingAfterActorStateScalarSuccess",
        "Second-stage control-routing transition reached after "
        "ComputeOwnerActorStateScalarFromRuntimeAndDecisionNode succeeds. Clears "
        "transient bits around +0x27D0/+0x26A4, then evaluates a follow-up "
        "record/current-object path and nested-control gate before reporting "
        "the next control-routing status.",
    ),
    0x141DE9E60: (
        "SeedOwnerRuntimeFromIncomingStateRecord",
        "Large owner-state seeding routine that validates an incoming state "
        "record against the authored table at 0x14386FD00, passes the matching "
        "descriptor into owner virtual +0x1E8, copies record transforms into "
        "owner+0x130/+0x140, dispatches owner+0x15B8 helpers, resets broad "
        "owner state mirrors around +0x175C..+0x19FC, refreshes live retained "
        "actor state, and marks companion/runtime bits after install.",
    ),
    0x141DE4D20: (
        "ApplyPreparedNavCharacterStateRecordThroughNestedControlSink",
        "Copies a compact prepared state block into owner+nested-control state, "
        "writes owner+0x1748 from the record mode id, then forwards the payload "
        "into 0x141E15030 through owner+0x15C0.",
    ),
    0x141E15030: (
        "ConsumePreparedNavCharacterStateRecordAndRefreshNestedControl",
        "Heavy nested-control sink for prepared NavCharacter state records. "
        "Validates owner actor identity, compares the incoming record against "
        "current control state, updates owner+0x110/+0x120 live transforms, "
        "rebuilds control deltas, emits NavMesh-tagged diagnostics, and commits "
        "accepted state into owner-side nested-control storage.",
    ),
    0x1410F1910: (
        "SpawnPlayerFromRetainedDescriptor",
        "STATIC six-slot ABI: RCX=16B OUTPUT weak{record,id}, EDX=route, "
        "R8=const DC::Continue input (required extent88h), R9/arg5 unused in "
        "this retail body, arg6 at entryRSP+30=const "
        "GameSave::CharacterPersistentState input (extentE14/native strideE20). "
        "Saved Continue R15 has only scalar reads: DWORD+4/+8/+C/+10/+1C/+6C, "
        "QWORD flags+40 and float+84; no whole input-pointer handoff or writes. "
        "Only direct callers1413C076A and140F73C34 build fresh caller-local "
        "persistent values. Success copies16B1410F2A37/3266; failure zeros "
        "record/id1410F2A71/74; CommitResolvedPlayerSpawn publishes slot at "
        "1410F17E9. Historical descriptor name does not make RCX an input. "
        "Owned input copies still need primary/reset/world-generation binding; "
        "a native spawn invocation is not a global teardown exclusion fence.",
    ),
    0x140B56090: (
        "HandleNetSpawnCoopBuddy",
        "Network/retained coop-buddy spawn path. Resolves retained roster "
        "entries, materializes live entity state, and feeds player-labelled "
        "spawn descriptors into the shared retained spawn stack.",
    ),
    0x140B580C0: (
        "SelectRetainedCoopPlayerLookHash",
        "Chooses a player-labelled retained coop look/descriptor variant based "
        "on runtime state gates and global mode selection.",
    ),
    0x1410F3350: (
        "SpawnPlayerFallback",
        "Fallback retained spawn-player path. Builds default transforms, "
        "initializes shared player-labelled registries, then calls "
        "CommitResolvedPlayerSpawn and post-spawn runtime gating on the "
        "resolved live entity.",
    ),
    0x141D22420: (
        "EntitySpawner_Spawn",
        "STATIC builds/resolves SpawnInfo, PSI+58=spawner at141D22712. Game "
        "callback141D22721 and optional141D227A7 can override actual PSI. Calls "
        "Process::SpawnProcess1417C3790 at141D227B1; only AFTER Init/PostInit "
        "return copies object+A8/+BC at141D22874/7F, loads the pair141D22883 "
        "and publishes spawner+30/+38 at141D22889 unless nonzero arg4 suppresses "
        "it. Post-store141D2288E has RBX=spawner/RDI=actor; default stack PSI "
        "is native RSP+50 but need not be the selected PSI. Exact GameInfo "
        "callback14029AEE0 returns null; optional callback still needs refusal. "
        "Not a read-only roster resolver or a cross-call retention lease.",
    ),
    0x141EB8950: (
        "DestroyManagedPointerArrayNodes_2NPlus3",
        "Iterates the managed pointer array at owner+0x4A0 with logical length "
        "(2*N+3) derived from owner+0x4A8, calling each non-null node's "
        "virtual +0x28 method before tail-calling the parent teardown.",
    ),
    0x141EB7CE0: (
        "ConstructCompanionCameraRigOwner_Allocating",
        "Allocating constructor for the companion camera-rig owner. Requests "
        "a 0x4E0-byte object, installs the vtable at 0x142B99A98, and zeros "
        "the managed child-node cluster at +0x4A0..+0x4C0.",
    ),
    0x141EB7E70: (
        "ConstructCompanionCameraRigOwner_InPlace",
        "In-place constructor variant for the same 0x4E0-byte owner type. "
        "Calls the shared base constructor, installs the vtable at "
        "0x142B99A98, and clears +0x4A0..+0x4C0.",
    ),
    0x141EB7EC0: (
        "DestroyCompanionCameraRigOwner_Freeing",
        "Freeing destructor for the 0x4E0-byte companion camera-rig owner. "
        "Destroys the managed object at +0x4B8, tail-calls the base teardown, "
        "and conditionally frees the storage when the low bit of the dtor "
        "flags is set.",
    ),
    0x141EB8890: (
        "AttachManagedNodeGraphToCompanionCameraRigOwner",
        "Builds or acquires the managed node graph stored at owner+0x4B8 "
        "through virtual slot +0x790, links it back to the owner via "
        "0x141E60E80, then continues shared owner initialization through "
        "0x141BBACC0.",
    ),
    0x14087D390: (
        "InitCompanionCameraRigOwner_ModeFlag_0x10000000",
        "Thin wrapper over AttachManagedNodeGraphToCompanionCameraRigOwner "
        "that then ORs mode flag 0x10000000 into the companion object at "
        "owner+0x198 via 0x141754EF0.",
    ),
    0x1414FBC20: (
        "InitCompanionCameraRigOwner_ModeFlag_0x01000000",
        "Thin wrapper over AttachManagedNodeGraphToCompanionCameraRigOwner "
        "that then ORs mode flag 0x01000000 into the companion object at "
        "owner+0x198 via 0x141754EF0.",
    ),
    0x1402A2450: (
        "InitCompanionCameraRigOwner_ModeFlag_0x00000800",
        "Thin wrapper over AttachManagedNodeGraphToCompanionCameraRigOwner "
        "that then ORs mode flag 0x00000800 into the companion object at "
        "owner+0x198 via 0x141754EF0.",
    ),
    0x14088B250: (
        "ConstructDerivedCompanionCameraRigOwner_0x500",
        "Constructor for a 0x500-byte derived companion camera-rig owner. "
        "Calls the shared base constructor, installs vtable 0x1429B32F8, "
        "clears the inherited managed child-node cluster at +0x4A0..+0x4C0, "
        "and adds derived fields at +0x4E0/+0x4E8.",
    ),
    0x141EB89E0: (
        "RelocateManagedPointerArrayOwner",
        "Relocates pointer fields and pointer-array entries for a managed owner "
        "object. Adjusts owner+0x4B8, the nested pointer at [owner+0x4B8]+0x60, "
        "the pointer array at owner+0x4A0 sized as (2*N+3), then tail-calls "
        "the parent relocation helper.",
    ),
    0x141EB8AE0: (
        "RefreshManagedPointerArrayNodesIfClean",
        "When owner+0x4A0 and owner+0x1A0 are present and owner+0x4C0 is clear, "
        "iterates the managed pointer array at owner+0x4A0 with logical length "
        "(2*N+3) and refreshes each node through 0x141E4D760 while keeping the "
        "cached state vector at +0x110 in sync.",
    ),
    0x1411DE2E0: (
        "ConstructPlayerTaggedMeleeScriptObserver_0x1C0",
        "Player-tagged melee observer constructor/factory. CRCs the string "
        "'Player', compares it against the incoming runtime descriptor, then "
        "allocates a 0x1C0 observer object, installs vtable 0x142A69798, "
        "and seeds default runtime/transform fields for the melee script "
        "observer family.",
    ),
    0x14124AE60: (
        "SeedMeleeObserverTrackedActorPairsAndBaseScalar",
        "Copies incoming tracked actor/runtime pairs into the melee observer "
        "object at +0xB0/+0xB8 and into the linked runtime owner cache, then "
        "derives the observer's base scalar at +0xC4 from the actor-side "
        "graph under +0x1A0.",
    ),
    0x14124AFC0: (
        "UpdateMeleeObserverKnowledgeAndSettings",
        "Main update path for the player-tagged melee observer. Resolves the "
        "live actor from the retained/runtime pair at +0xB0/+0xB8, queries "
        "actor virtual slots +0x5C8/+0x9B0/+0xBE0, resolves authored records "
        "through 0x14123F260, hashes the melee-settings vs net-melee-no-ap "
        "paths, packages the state through 0x1411E00D0/0x1411E0230, and "
        "stores the resolved state id at +0xC0.",
    ),
    0x14124C060: (
        "PushMeleeObserverActorTransformIfReady",
        "Resolves the melee observer's live actor from +0xB0/+0xB8, requires "
        "the readiness gate at 0x14124C0E0, then pushes a transform from the "
        "actor-side graph into 0x14189F410.",
    ),
    0x14124C0E0: (
        "CheckMeleeObserverActorState10Ready",
        "Resolves the observer's live actor/runtime pair, marks it active, "
        "walks actor+0x1A0 through 0x14172B7F0 and returns true only when "
        "0x14189E170 reports state id 0x10 for the observer's cached state "
        "slot at +0xC0.",
    ),
    0x14124C180: (
        "GetMeleeObserverScaledWeightFromLiveActor",
        "If the melee observer passes 0x14124C0E0, resolves another actor-side "
        "node through virtual +0x70, reads node+0x168, and multiplies it "
        "against the observer's base scalar. Otherwise falls back to the "
        "default scalar at +0xC4.",
    ),
    0x14189E170: (
        "QueryActorGraphStateByteByStateId",
        "Looks up an actor-side state id in the primary 0x100-byte table at "
        "graph+0x140 and the fallback 0x1C-byte table at graph+0x460, then "
        "returns the corresponding one-byte state marker used by the melee "
        "observer readiness path.",
    ),
    0x14189F410: (
        "ExtractActorGraphTargetTransformRecord",
        "Builds a compact target/transform record from an actor graph. Uses "
        "the selector in out_record+0x8/+0xC, prefers a matching live object "
        "from 0x14386FD00, copies its first 0x20 bytes when valid, and then "
        "mixes in local actor-graph transform/state fields.",
    ),
    0x1418A5CD0: (
        "ResolveActorRootCandidateEntryByStateId",
        "Seeds a compact selector record from actor_root+0x90 through "
        "0x1418A6010, prefers actor_root->0x38 and falls back to +0x40, then "
        "dispatches candidate resolution through 0x14193D250.",
    ),
    0x14193D250: (
        "FindResolvableCandidateEntryByStateId",
        "Walks the current actor root candidate table for a requested state "
        "id and returns an entry only when its selector record is valid. The "
        "authored 'auto' key uses this generic resolver; it does not itself "
        "request run-gun-idle or write animation state.",
    ),
    0x14193CD10: (
        "FindRecursiveCandidateEntryByPointerAndStateId",
        "Recursively walks 0x40-byte candidate entries. Follows child "
        "containers when flag 0x40 is set; otherwise matches the incoming "
        "candidate pointer and rejects entries whose state id already equals "
        "the requested one.",
    ),
    0x14193CDB0: (
        "ValidateCandidateEntryAgainstSelectorRecord",
        "Validates a candidate entry against the compact selector record built "
        "by the melee observer path, including gating bits in entry+0x4 and "
        "boolean selectors at record+0x2D/+0x2E.",
    ),
    0x14055D940: (
        "HandleNpcHolsterWeaponViaNestedControlObject",
        "Builds the npc-holster-weapon command record, resolves the nested "
        "control object through owner+0x15C8->virtual +0x58, and then routes "
        "the action either through 0x141DE55E0 (+0xA0) or direct nested "
        "virtual +0xB8 depending on the local action-state value.",
    ),
    0x141C965A0: (
        "RegisterInteractSelectorRegistryEntries",
        "Bootstrap registration path for interact-related selector ids. "
        "Hashes authored strings such as 'interact-command-id' through "
        "0x14014D910 and stores global selector/type/size metadata records "
        "under 0x146C742A8..0x146C742E4.",
    ),
    0x1408BDF40: (
        "BuildInteractHookRuntimeStateFromLiveObject",
        "Gameplay-facing interact/rope runtime builder. Hashes selector ids "
        "including 'interact-hook', 'interactable-def', and "
        "'rope-holster-offset', resolves the gated live runtime object "
        "through 0x141D21E20, packages its +0xA8/+0xBC retained-runtime pair, "
        "and feeds the resulting records through owner-side build/apply paths "
        "including virtual +0xE8, 0x141ECEFE0, virtual +0x20, and 0x141C9ED50.",
    ),
    0x1408C4A80: (
        "UpdateWinchHookActionStateFromRuntimeSelectors",
        "Large winch-hook action/state update path. Consumes selector ids "
        "such as 'winch-hook-attach', 'attach-winch-hook', "
        "'winch-hook-secure', and 'interactable-def', resolves actor/runtime "
        "pairs, materializes records through 0x141C9ED50/0x141C9EB30, and "
        "writes the resulting action state back into owner/runtime fields "
        "around +0x12F0..+0x1330, +0x14F8, +0x1511/+0x1514/+0x1518, and "
        "+0x17B8.",
    ),
    0x140E964F0: (
        "RefreshHookStateRecordsAndCooldownFlags",
        "Owner-side hook-state refresh helper. Walks compact records under "
        "object+0x1A0, resets/refreshes per-entry cooldown/state fields, and "
        "clears cached output at owner+0x220/+0x228 when the current hook "
        "state no longer matches the active runtime ids.",
    ),
    0x14124E7C0: (
        "RoutePlayerGrabWinchHookStateIntoNestedActionDispatch",
        "Actor-aware bridge from the player-grab-winch-hook family back into "
        "the shared nested action dispatcher. Resolves and validates the live "
        "actor through virtuals +0x8C0/+0xFA8/+0x860, compares it against the "
        "cached actor-side object at +0x2E8, then calls 0x141DE55E0 with a "
        "binary action-state argument.",
    ),
    0x140484C20: (
        "EvaluateCurrentActionAgainstNestedControlAndStateBundle",
        "Shared owner-side current-action gate. Starts from owner+0x15C8, "
        "checks cached live runtime pairs at +0x23D8/+0x23E0, special runtime "
        "tag 0xAC710201, and owner virtual +0x7D0; if those do not short-"
        "circuit, probes nested virtual +0xA8 and falls back to 0x141DE55E0. "
        "Then continues through helper/state objects under +0x1FB0/+0x1FF8/"
        "+0x1FA8 and owner virtuals +0xFD0/+0x478.",
    ),
    0x14058B900: (
        "HandleCombatActionStateThroughNestedControlAndActorGraph",
        "Large combat/action-state handler. Resolves live actor/runtime "
        "identity, filters a state-id list, forces low-cardinality nested "
        "state through 0x141DE5A10 when owner+0x24C8<3, commits packaged "
        "records through 0x141E0EDF0/0x141E0ED10, uses actor-graph helpers "
        "0x1418B7060/0x14189E5A0/0x14189CC80, and routes follow-up work "
        "through nested virtuals +0x178/+0x170/+0xB0.",
    ),
    0x14058BA40: (
        "ApplyCombatActionMode1SnapshotAndSelection",
        "Live branch inside 0x14058B900. Applies a full snapshot into "
        "owner+0x15B8 through 0x141E0EDF0, switches controller mode 1 through "
        "0x141E0ED10, then resolves actor-graph selection data through "
        "owner+0x1E0 and 0x1418B7060.",
    ),
    0x14058BC70: (
        "ApplyCombatActionMode2SnapshotAndFinalizeSelection",
        "Sibling live branch inside 0x14058B900. Applies a blended snapshot "
        "into owner+0x15B8, switches controller mode 2, then finalizes the "
        "nested-control side through 0x141DEF910 after selecting actor-graph "
        "data via 0x1418B7060.",
    ),
    0x14124ED20: (
        "PublishActorObserverRecordFromPrimaryRetainedPlayerAndScalarQuery",
        "Observer-style publish path built on the primary retained player. "
        "Resolves slot 0 through 0x1410F0D10, fetches the shared scalar query "
        "through 0x141DF0000, compares world-space transforms and thresholds, "
        "and when enabled publishes a structured record through the sink at "
        "0x143FDF520 via virtual +0xA8.",
    ),
    0x14124F700: (
        "PublishSiblingActorObserverRecordThroughSink90",
        "Sibling observer-style publish path. Resolves live actors from "
        "retained/runtime pairs, computes transform-derived thresholds, calls "
        "the shared scalar query 0x141DF0000, and when enabled publishes a "
        "structured record through the same sink object 0x143FDF520 but via "
        "virtual +0x90 before forwarding the result to 0x1411DF370.",
    ),
    0x140437F80: (
        "ResolveAndApplySpecialTaggedStateCandidateOrFallback",
        "Special-tag state-application helper. Builds owner transform, "
        "classifies it through 0x142145490, forwards the 0x2010 case into "
        "0x1404380F0 with tag 0xAC710200, otherwise resolves a candidate from "
        "global registries and applies it through 0x141DE4F90 when "
        "candidate->virtual +0xB0(owner) succeeds.",
    ),
    0x1404380F0: (
        "ResolveAndApplySpecialTaggedStateCandidateByTag",
        "Builds an owner transform, resolves a special-tag candidate object "
        "through 0x1409DA630(tag, transform), validates it through virtual "
        "+0xB0, and commits it through 0x141DE4F90.",
    ),
    0x140394B20: (
        "QueueVehicleControllerSetDriverEvent",
        "Vehicle-controller event builder for the authored drive-event "
        "'set-driver'. Captures target vehicle state through virtual +0xF8, "
        "packages role/index data, hashes the event string, and continues into "
        "the shared vehicle-controller commit path.",
    ),
    0x140395000: (
        "QueueVehicleControllerAddPassengerEvent",
        "Vehicle-controller event builder for the authored drive-event "
        "'add-passenger'. Builds the same event record shape as the driver "
        "path but for passenger entry, then continues into the shared "
        "vehicle-controller event commit path.",
    ),
    0x140395430: (
        "QueueVehicleControllerPassengerEnteredEvent",
        "Vehicle-controller event builder for the authored drive-event "
        "'passenger-entered'. Captures the current vehicle state and packages "
        "the passenger-entered event into the same shared controller path used "
        "by set-driver/add-passenger.",
    ),
    0x14059D740: (
        "BuildPassengerFrontEntryStateAndCommitVehicleControllerRecord",
        "Concrete passenger-entry builder. Validates a live actor, measures "
        "entry transforms, hashes the authored state 'hero-passenger-a-enter-"
        "front', stores it into controller state at +0xA0, writes a role-like "
        "value at +0xB0, updates entry flags at +0x12C, and commits entry "
        "transforms back into controller fields.",
    ),
    0x1406B19F0: (
        "UseTeleportIntoVehicleScriptPathFromVehicleActionRuntime",
        "Runtime user of the TeleportIntoVehicle script-skill family. Reads "
        "pending vehicle-action state, packages the TeleportIntoVehicle string "
        "block, and feeds it into the larger scripted vehicle-entry path.",
    ),
    0x1414115B0: (
        "NotifyPlayerEnteringVehicleAndPublishEntryRecordToActor",
        "VehicleCtrl::NotifyPlayerEntering path. Hashes 'vehicle-entry-def', "
        "builds packaged entry records from the incoming VehicleEntryDef, "
        "serializes them through 0x1417F8B00, and when a matching live actor "
        "is present publishes the resulting entry record through actor "
        "virtual +0xD0.",
    ),
    0x14059DAB0: (
        "SelectVehicleExitSideAndDispatchSeatStateUpdate",
        "Sibling vehicle seat-state method. Chooses between authored exit-side "
        "strings, uses the same seat/controller object as the passenger-entry "
        "builder, dispatches through nested control virtual +0xD0, and caches "
        "a resulting value/timestamp into object+0x48.",
    ),
    0x14059DC30: (
        "GateVehicleSeatStateByCapabilityHashAndDispatchNestedControlA0",
        "Small seat-state applicability gate. Requires the target actor to "
        "carry capability bit 0x143D43FA4, matches action hash 0x465123BF, "
        "then resolves nested control through owner+0x15C8->virtual +0x58 and "
        "tail-dispatches into nested virtual +0xA0.",
    ),
    0x14059F199: (
        "CallSeatStateExitSelectorOnEmbeddedVehicleSeatState",
        "Thin outer-owner wrapper. Resolves the embedded vehicle seat-state "
        "subobject at owner+0x690, forces the binary side flag to 1, and "
        "jumps into SelectVehicleExitSideAndDispatchSeatStateUpdate.",
    ),
    0x14059F1E0: (
        "UpdateVehicleActionOwnerAndRefreshEmbeddedSeatState",
        "Large outer vehicle-action owner update. Reads the embedded seat-state "
        "object at owner+0x690, queries nested control +0xB0 through "
        "owner+0x15C8, stores the resulting low-cardinality state in "
        "owner+0x69C, checks action/capability gates, and then continues into "
        "the larger owner runtime path.",
    ),
    0x14039EB80: (
        "ResolveQualifiedRailVehicleActorFromPackedPair",
        "Resolves a live actor from a packed retained/runtime pair and then "
        "requires the selector capability 'RailVehicle' through "
        "CheckSelectorHashEnabledInBitsetRegistry.",
    ),
    0x14039EC50: (
        "ResolveQualifiedDriveableVehicleActorFromPackedPair",
        "Resolves a live actor from a packed retained/runtime pair and then "
        "requires the selector capability 'DriveableVehicle' through "
        "CheckSelectorHashEnabledInBitsetRegistry.",
    ),
    0x1413571B0: (
        "UpdatePlayerHudAimingAndWeaponAccuracyPublishes",
        "Large player/HUD runtime update. Combines player-state booleans, "
        "CameraManager state, and authored scalars such as "
        "'grenade-throw-time', 'player-live-time', 'is-aiming', and "
        "'weapon-accuracy', then publishes them through object-local sinks "
        "around +0x1D98 and the big state block at +0xF0.",
    ),
    0x14009BC80: (
        "RegisterSelector_PlayerIsAimingFirearmAtNpc",
        "Registration stub for the authored selector "
        "'player-is-aiming-firearm-at-npc?'. Not a live runtime consumer.",
    ),
    0x14090AB80: (
        "UpdateBuddyMeleeCoordinatorAttackInternalAndPublishAttackRecords",
        "Large buddy attack-behavior path anchored by nearby strings "
        "'BuddyMeleeCoordinator::AttackInternal', 'pAttackList', and "
        "'Buddy Attacking Behavior'. Seeds the +0x1D90..+0x1E70 block, "
        "resolves an object into +0x1D98, and publishes structured attack "
        "records through the global sink 0x143FDF520 via virtual +0x90/+0xC8.",
    ),
    0x141356F80: (
        "UpdateHudRuntimePublishes_CameraInside_GrenadeMove_BlindVis",
        "HUD/runtime presentation path anchored by nearby strings "
        "'Camera Inside', 'GrenadeMove', 'vis: blind', 'vis: %s', and "
        "ghud.cpp. Publishes a structured record into the global sink "
        "0x143FDF520 via virtual +0x38.",
    ),
    0x14013A110: (
        "UpdateIggyAllocFreeTelemetryAndPublishSinkEvents",
        "Allocator/telemetry runtime path anchored by nearby strings "
        "'Iggy Alloc', 'Iggy Free', and flash allocator overflow warnings. "
        "Builds small structured records, publishes them into the global sink "
        "0x143FDF520 via virtual +0x28, and continues through local object "
        "virtuals +0x38/+0x90.",
    ),
    0x1403EF6D0: (
        "BufferQualifiedWeaponFireStateSampleInOwnerRuntimeArray",
        "Live runtime user of the 'weapon-fire' string id. Matches the incoming "
        "runtime id against the hashed 'weapon-fire' token, gates on actor "
        "capability bits and object+0x1DB8 virtuals, computes a derived vector "
        "sample, and stores the result plus retained/runtime metadata into the "
        "owner's buffered state array around +0xC00..+0xC30.",
    ),
    0x141436E70: (
        "ResolveWeaponFireObserverStateFromLiveRetainedPlayer",
        "Higher-level runtime user of the 'weapon-fire' token. Builds the same "
        "hashed id, pushes it through nearby runtime helpers, resolves retained "
        "player slot 0, and changes its qualification path when the multiplayer/"
        "split byte in dword_143E07470 is active. This proves split activation "
        "affects weapon observation, but does not prove slot-1 occupancy alone "
        "blocks aim/fire. It then computes a world-space result and forwards it "
        "through 0x141434DA0 with a toggled binary state.",
    ),
    0x1404CA550: (
        "EnumerateLivePlayerActorsForClimbChecks",
        "Collects valid Player actors. In campaign it reads retained slots 0 and "
        "1 directly; in multiplayer it scans the ten NetGameManager slots. Its "
        "only direct code caller is AiClimbController::IsBlocked, so this is not "
        "the local weapon/aim gate.",
    ),
    0x1404CEFC0: (
        "AiClimbControllerIsBlocked",
        "AiClimbController::IsBlocked. Enumerates live players through "
        "0x1404CA550 and tests Edge, EdgeAimNew, PeekOver and Melee geometry "
        "against them while evaluating a climb segment.",
    ),
    0x141432B90: (
        "ResolveOwnerWeaponObserverScalarByStateId",
        "Reads owner+0x624 as a current state id, resolves a cached "
        "'weapon-art-defs-online-hash' registry through 0x1414FA800, looks up "
        "the matching record via 0x141764B50 / 0x141434E90, and returns its "
        "leading float scalar with a fallback default when the record is absent.",
    ),
    0x141434E90: (
        "ResolveOwnerWeaponObserverRecordByStateId",
        "Pointer-returning sibling of 0x141432B90. Resolves the cached "
        "'weapon-art-defs-online-hash' registry, uses owner+0x624 as the key, "
        "and returns the matching state/observer record pointer.",
    ),
    0x141434DA0: (
        "UpdateOwnerWeaponObserverStateByModeFlag",
        "Reads owner+0x624 as the current state id, optionally hashes the "
        "authored token at 0x142A854B0 ('helicopter-igc-mg'), resolves a "
        "cached 'weapon-art-defs-hash' registry through 0x1414FA380, and "
        "routes the resulting state id through 0x141614E40 under a binary "
        "mode/toggle flag.",
    ),
    0x1414FA380: (
        "GetCachedWeaponArtDefsHashRegistry",
        "Returns the active WeaponArtDef hash, not necessarily the offline "
        "registry. At 0x1414FA4BD HIBYTE(g_NetModeFlags) selects the online "
        "cache 0x1437426E0/0x1437426F0 or the offline cache "
        "0x146C90228/0x146C90238; resource owner+0x58 generation changes "
        "refresh the cached data. Callers must not retain an ArtDef/holster "
        "descriptor across a registry generation or mode change.",
    ),
    0x1414FA800: (
        "GetCachedWeaponArtDefsOnlineHashRegistry",
        "Cached accessor for the registry keyed by the "
        "'*weapon-art-defs-online-hash*' string family.",
    ),
    0x1414FA9A0: (
        "GetCachedWeaponDryFireEventRegistry",
        "Sibling cached accessor keyed by the 'frags-event-weapon-dry-fire' "
        "string family.",
    ),
    0x141614E40: (
        "LookupRegistryRecordPointerById",
        "Generic id->record lookup over a registry with record_count at +0x0, "
        "id table at +0x8, and record pointer table at +0x10. Uses open-"
        "addressing style probing and returns the matched record pointer.",
    ),
    0x1414FAC10: (
        "GetWeaponObserverRecordField00ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x0.",
    ),
    0x1414FAC50: (
        "GetWeaponObserverRecordField08ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x8.",
    ),
    0x1414FAC90: (
        "GetWeaponObserverRecordField10ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x10.",
    ),
    0x1414FACD0: (
        "GetWeaponObserverRecordField18ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x18.",
    ),
    0x1414FAD10: (
        "GetWeaponObserverRecordField20ByStateId",
        "Uses the cached '*weapon-art-defs-online-hash*' registry and the "
        "state id key to return record+0x20.",
    ),
    0x1414FAD50: (
        "GetWeaponObserverPrimaryObjectField2B0ByStateId",
        "Resolves the '*weapon-art-defs-online-hash*' record by state id and, "
        "if record+0x0 is a live object pointer, returns that object's +0x2B0 "
        "field.",
    ),
    0x141520750: (
        "BuildUsingJointDifferenceStateRecord",
        "Builds a large structured state record from input vectors, the current "
        "actor/runtime pair, and a downstream registry record. Used at the tail "
        "of the higher 'weapon-fire' observer path after owner-local observer "
        "state has been updated through 0x141434DA0.",
    ),
    0x140A40A30: (
        "BuildUsingJointDifferenceStateRecordAndResetWrapperTail",
        "Thin wrapper over 0x141520750. Builds the shared UsingJointDifference "
        "record, stores one extra pointer at +0x130, and clears/defaults tail "
        "fields around +0x140/+0x150/+0x160/+0x170.",
    ),
    0x141520890: (
        "ProcessUsingJointDifferenceStateRecordForCalcStartPhase",
        "Heavy downstream consumer of the shared UsingJointDifference record "
        "family. Anchored by nearby authored tokens 'calc-start-phase-*' and "
        "'DcCalcStartPhaseUsingJointDifference'; reuses actor/runtime pair "
        "queries and continues into a larger produced-state/application path.",
    ),
    0x140A40AB0: (
        "SelectSpawnerOrGroupEntryByValue",
        "Searches spawner/group-related tables and a fallback lookup path to "
        "resolve a matching entry/index for the requested value. Separate from "
        "the real UsingJointDifference start-phase transition family.",
    ),
    0x1417F8910: (
        "PublishStructuredStateBlockToActorVirtualD0",
        "Actor-facing publish/apply helper. If the target is valid and not "
        "blocked by target+0x40 bit 0x1, it acquires/releases guards through "
        "0x1416A2E80/0x1416A2ED0 and forwards the structured block through the "
        "target object's virtual +0xD0; otherwise it writes the fallback block.",
    ),
    0x1402EF0A0: (
        "ConsumeResolvedPhaseStateAndForwardToRuntimeHandler",
        "Heavy runtime consumer that starts from a 0x1417651D0-resolved record "
        "id and vector payload, packages several actor/runtime pairs plus a "
        "large phase/state block, resolves live actor identity, and forwards "
        "the result into 0x1402EF420.",
    ),
    0x1402EF420: (
        "ApplyPhaseTransitionPayloadToRuntimeState",
        "Heavy downstream apply path for the shared UsingJointDifference / "
        "calc-start-phase family. Selects branch/state ids from record fields "
        "+0x90/+0x94/+0x9C/+0xA0/+0xA8, rebuilds world-space phase/orientation "
        "bases, dispatches transition instructions, writes actor-side "
        "transition fields, and fans follow-up logic across retained player "
        "slots 0 and 1.",
    ),
    0x1402EE240: (
        "BuildPhaseTransitionInstructionDispatchRecord",
        "Builds a large phase-transition dispatch record from the incoming "
        "state block, hashes local descriptor tokens, resolves a live "
        "actor/runtime pair, and prepares the instruction payload later "
        "consumed by 0x1402EF420.",
    ),
    0x1401781E0: (
        "BuildWorldBasisVectorFromRuntimeTransform",
        "Resolves a runtime object through the retained/runtime table at "
        "0x14386FD00, copies its cached transform at +0x10 when valid, and "
        "emits a world-space basis vector from the object's orientation and "
        "translation.",
    ),
    0x1415D23B0: (
        "BuildOrthonormalBasisFromTwoVectorsAndPosition",
        "Normalizes two incoming vectors, cross-combines them into an "
        "orthonormal 3x3 basis, and copies the fourth input vector into the "
        "translation row. The final boolean selects handedness. Used by the "
        "GunMove strafe-IK locator to construct a basis from the unprojected "
        "screen direction and world up.",
    ),
    0x142177E50: (
        "Quaternion_FromOrthonormalBasis",
        "Exact matrix-to-quaternion conversion over the 3x3 basis fields at "
        "+0x00/+0x04/+0x08, +0x10/+0x14/+0x18 and +0x20/+0x24/+0x28. "
        "Computes sqrt(trace+1), half-scale and the three antisymmetric "
        "differences. The GunMove strafe-IK locator stores its result as the "
        "output Locator quaternion. STATIC AGX: all four branches use "
        "sqrtf via 0x1424ED268 -> IAT0x1425D6F60 (api-ms-win-crt-math "
        "sqrtf). The isolated builder must resolve this real host UCRT "
        "dependency; copying raw constants alone is insufficient.",
    ),
    0x1421787A0: (
        "Quaternion_FromOrthonormalBasis_LargestX",
        "STATIC AGX: largest-X matrix-to-quaternion branch called from "
        "Quaternion_FromOrthonormalBasis; uses sqrtf through IAT1425D6F60.",
    ),
    0x1421788B0: (
        "Quaternion_FromOrthonormalBasis_LargestY",
        "STATIC AGX: largest-Y matrix-to-quaternion branch called from "
        "Quaternion_FromOrthonormalBasis; uses sqrtf through IAT1425D6F60.",
    ),
    0x1421789B0: (
        "Quaternion_FromOrthonormalBasis_LargestZ",
        "STATIC AGX: largest-Z matrix-to-quaternion branch called from "
        "Quaternion_FromOrthonormalBasis; uses sqrtf through IAT1425D6F60.",
    ),
    0x141BE20C0: (
        "DispatchPhaseTransitionInstructionListToActorState",
        "Consumes a live actor/runtime pair plus a small instruction list, "
        "iterates 0x20-byte entries with type-like values 0/1/2, and routes "
        "them into nested transition writers such as 0x141BE1E60 or recursive "
        "self-dispatch.",
    ),
    0x141BE2D90: (
        "WriteActorTransitionTargetVector50_58",
        "Resolves the target actor through a runtime pair, sets bit 0x8 in the "
        "actor-side transition record, and writes three floats to +0x50/+0x54/"
        "+0x58.",
    ),
    0x141BE2E70: (
        "WriteActorTransitionScalar5C",
        "Sibling actor-side transition writer that resolves the same runtime "
        "pair, sets bit 0x8, and writes a scalar to +0x5C.",
    ),
    0x141BE2F50: (
        "WriteActorTransitionScaledShort2C",
        "Sibling actor-side transition writer that resolves the same runtime "
        "pair and writes a scaled short/integer field to +0x2C.",
    ),
    0x1417423B0: (
        "BuildPhaseTransitionBasisFromObjectTransforms",
        "Rebuilds a world-space phase/orientation basis from two object-backed "
        "transforms plus scalar parameters, then returns the derived block "
        "through 0x141742290.",
    ),
    0x141BE4EA0: (
        "FinalizeActorPhaseTransitionRecordFromBasis",
        "Consumes the rebuilt phase payload plus live actor/runtime data, "
        "recomputes orientation/basis state, queries actor-side objects through "
        "virtual +0x168, and finalizes a structured transition record through "
        "0x141BDFE70.",
    ),
    0x141BDFE70: (
        "ResolveActorPhaseTransitionResultState",
        "Actor-side phase-transition result resolver/finalizer. Consults owner "
        "state around +0x960/+0x987/+0x990/+0x998/+0xA28, queries authored "
        "state through 0x141764B50/0x141765030/0x14209A8B0/0x14209A740, emits "
        "explicit status codes, and may recurse into itself or sibling "
        "0x141BDF510 for multi-entry transition selection.",
    ),
    0x141BDF510: (
        "AcquireParticleRootDataSlotForTransitionEffect",
        "Sibling helper under the phase-transition family that descends into "
        "ParticleManager/ParticleRootData allocation. Computes a slot-like "
        "index from owner+0x58, probes owner-local bitsets around +0x3B0/"
        "+0x430/+0x4B0/+0x630, acquires a claim path around owner+0x390, and "
        "drives ParticleRootData allocation/retiring/active assertions.",
    ),
    0x141F84640: (
        "DispatchRetainedSlotPhaseFollowupIntoCameraShakePath",
        "Retained-slot follow-up path reached from 0x1402EF420 after phase "
        "state application. Gates on global runtime state, checks per-slot "
        "thresholds, packages records through 0x1417C3790, and continues into "
        "camera-shake / explosion-shake / shoot-shake follow-up via "
        "0x141BEB0F0.",
    ),
    0x141BEB0F0: (
        "DispatchCameraShakeOrExplosionShakeFromPhaseFollowup",
        "Downstream camera-shake/explosion-shake dispatcher under the retained-"
        "slot phase follow-up branch. Resolves effect records and routes into "
        "0x141BE8FF0/0x141BE9AE0/0x141BE9B90 using global effect registries "
        "rooted around 0x146921700 and 0x146922710.",
    ),
    0x141F83CC0: (
        "QueueOrRefreshCameraShakeEntryBySlotAndSource",
        "Maintains capped per-slot camera-shake entries in global arrays around "
        "0x146AA5A20/0x146AA5A28. Updates or appends timing/strength/source "
        "records for the camera-shake follow-up path.",
    ),
    0x14209A740: (
        "ResolveOwnerAuthoredStateNodeById_Primary90",
        "Owner-local dual-hash resolver used under 0x141BDFE70. Resolves a "
        "state node by id through the primary hash rooted at owner+0x90 and "
        "cross-validates an optional linked node through the secondary hash "
        "family at owner+0x398/+0x428.",
    ),
    0x14209A8B0: (
        "ResolveOwnerAuthoredStateNodeById_Primary98",
        "Sibling owner-local dual-hash resolver used under 0x141BDFE70. "
        "Resolves a state node by id through the primary hash rooted at "
        "owner+0x98/+0x128 and validates the linked object at node+0x18 "
        "through the secondary hash family at owner+0x398/+0x428.",
    ),
    0x141EF59C0: (
        "FilterPrimary90AuthoredStateNodesByModeMasksAndBuildObserverPayload",
        "External consumer of 0x14209A740. Iterates authored-state ids, "
        "filters resolved nodes by long capability/category masks at node+0x30 "
        "using mode-specific constants 0x200000000000 / 0x100000000000, then "
        "builds downstream observer payload objects via "
        "0x14036BD80/0x1415A0E90/0x14163F930.",
    ),
    0x141F564E0: (
        "GateObserverPublishPathByPrimary98AuthoredStateBit3",
        "External consumer of 0x14209A8B0. Canonicalizes an authored-state id, "
        "resolves the linked node, extracts bit3 from node+0x12, and uses it "
        "as an early enable/override gate before the larger observer/publish "
        "runtime path continues.",
    ),
    0x141BDC880: (
        "ConsumeTransitionResultAndCacheResolvedEntityPair",
        "Actor-local direct consumer of 0x141BDFE70. Validates the returned "
        "(ptr,id,status)-style transition record, marks the resolved object "
        "with bit 0x10, caches the transition result into the local consumer, "
        "and mirrors or resolves a live entity handle/id pair into a sibling "
        "slot.",
    ),
    0x141BDFFD0: (
        "IterateAuthoredTransitionIdsAndRefreshLocalTransitionState",
        "Looped direct consumer family around 0x141BDFE70. Rebuilds structured "
        "transition inputs from authored id arrays and refreshes local owner "
        "state around +0x984/+0x990/+0x998/+0xA28 from the returned transition "
        "results.",
    ),
    0x141BE07F0: (
        "IterateGlobalAuthoredTransitionIdsAndRefreshSingletonState",
        "Global/singleton sibling of the local 0x141BDFFD0 family. Invokes "
        "0x141BDFE70 through the global owner context rooted at 0x146920CA0 "
        "and refreshes global transition state around "
        "0x146921624/0x146921630/0x146921638/0x1469216C8.",
    ),
    0x141BE4DF0: (
        "BuildCompositeTransitionResultFromResolvedEntityState",
        "Higher transition-result builder over 0x141BDFE70. Seeds one result "
        "slot, derives extra entity/runtime state through validated handle "
        "pairs and entity->virtual+0x168, then feeds the derived state into a "
        "second transition-result resolution pass.",
    ),
    0x141BD4DD0: (
        "InitializeParticleManagerGlobals_920CA0_920CF0_920CF8",
        "Anchors to ParticleManager::Init in shared/src/gamelib/render/"
        "particle/particle.cpp. Initializes the global ParticleManager-owned "
        "context at 0x146920CA0, the separate manager-owned authored-state/"
        "event resolver at 0x146920CF0, and the compact short-id helper at "
        "0x146920CF8.",
    ),
    0x141BD8BE0: (
        "ParticleManager_UpdateEvents_FilterAuthoredStateNodes",
        "ParticleManager::UpdateEvents-side consumer of the global "
        "0x146920CA0/0x146920CF0/0x146920CF8 family. Iterates manager-owned "
        "entries, resolves authored-state ids through 0x14209A740, filters "
        "them by node+0x30 mask under the current manager mode, and feeds the "
        "resulting structured state into 0x141BE4EA0.",
    ),
    0x141BDF020: (
        "RefreshParticleManagerFrontObject",
        "Small helper over the ParticleManager-owned global context family. "
        "Clears an optional owned object at context+0x20, queries the current "
        "front object through 0x14160C120, and conditionally routes into the "
        "deeper manager-owned runtime gate around 0x141A81A90.",
    ),
    0x141A81B80: (
        "CheckContext130PolicyBitsAgainstNode30Masks",
        "Shared policy helper used from the ParticleManager-owned branch. "
        "Reads policy bits 8/9 from context+0x130 and tests them against the "
        "long authored-state capability masks 0x100000000000 / "
        "0x200000000000 at node+0x30, returning a boolean gate.",
    ),
    0x141BDC640: (
        "ProcessPendingParticleRootTransitionEntriesAndConsumeTransitionResults",
        "Large local processor above 0x141BDFE70 inside the same ParticleManager "
        "/ ParticleRootData owner family. Walks a pending-entry cluster at "
        "owner+0x668/+0x670/+0x6F0/+0x6F8, resolves authored-state ids through "
        "owner_context+0x50 -> 0x14209A8B0, falls back into "
        "0x141D21F90/0x141D231E0 when needed, then consumes and caches "
        "transition results or unlinks cleared entries from the local list.",
    ),
    0x141BDCED0: (
        "RefreshParticleRootOwnerRuntimeObject30AndDerivedStateBytes",
        "Large local refresh path in the ParticleManager/ParticleRootData owner "
        "family. Rebuilds/refreshes the runtime subobject rooted at owner+0x30, "
        "copies a broad state image from owner fields around +0x994..+0xA2C, "
        "updates derived flags/counters around +0x987..+0x98C/+0x9C1, and feeds "
        "the same local owner family later consumed by 0x141BDC640.",
    ),
    0x141BDD400: (
        "ConstructParticleRootOwnerRuntimeSubobject30AndSeedLinkedStateBlocks",
        "Constructor/builder for the large local runtime/root subobject stored "
        "at owner+0x30 in the ParticleManager owner family. Also seeds linked "
        "owned blocks around owner+0x1E0/+0x1F8/+0x200 before the later "
        "refresh/transition paths consume them.",
    ),
    0x141BDD3E0: (
        "InvokeQueuedOwnerRuntimeRefreshCallbackRecord",
        "Thin callback adapter over a compact 0x18 record. Reads owner/context "
        "at +0x0/+0x8 and a byte flag at +0x10, then tail-calls into "
        "0x141BDDbe0.",
    ),
    0x141BDDBE0: (
        "RefreshAndCommitParticleRootOwnerRuntimeShell",
        "Central refresh/commit wrapper for the current ParticleManager local "
        "owner family. Derives local flags around +0x987..+0x98C, builds "
        "owner+0x30 via 0x141BDD400, refreshes it via 0x141BDCED0, runs "
        "follow-up local methods, then commits the resulting runtime object and "
        "staging state back into the outer shell object at owner->0.",
    ),
    0x140164420: (
        "InitializeQueuedOwnerRuntimeRefreshCallbackRecord_Global",
        "Global/bootstrap producer for the compact callback record consumed via "
        "0x141BDD3E0. Allocates a 0x18 record, stores owner/context at +0x0/+0x8 "
        "and a byte flag at +0x10, then publishes callback target 0x141BDD3E0.",
    ),
    0x141BDE160: (
        "BuildQueuedOwnerRuntimeRefreshCallbackRecord_Local",
        "Local producer for the same compact callback-record form used by "
        "0x141BDD3E0. Stores owner/context pointers and byte flag, then routes "
        "execution toward the central wrapper 0x141BDDBE0.",
    ),
    0x141BDE250: (
        "ResetAndRebuildParticleRootStateBanksAndQueues",
        "Local reset/rebuild pass under the central 0x141BDDBE0 wrapper in the "
        "ParticleManager family. Clears outer-shell staging pointers, zeroes "
        "owner+0x630 and owner+0x430, rebuilds missing state entries through "
        "0x141BD7D00, and continues through root-data list/queue state around "
        "+0x798/+0x818/+0x828.",
    ),
    0x141BD79D0: (
        "ParticleManager_FreeRootDataAndPromoteBitsetState",
        "FreeRootData-side state/bank update in the ParticleManager family. "
        "Scans owner+0x5B0, cross-checks presence bits in "
        "owner+0x3B0/+0x430/+0x4B0/+0x530, updates the live root-data record "
        "from owner+0x58, then writes back promoted bits and marks owner+0x98E.",
    ),
    0x141BD8020: (
        "ParticleManager_CreateRenderDataFromRootStateBanks",
        "CreateRenderData-side derived-batch builder for the same particle root "
        "bank family. Starts from owner+0x3B0, validates consistency against "
        "owner+0x430 and owner+0x630, resolves live root-data records through "
        "owner+0x58, and writes compact tuples into produced helper storage.",
    ),
    0x141BD74B0: (
        "ParticleManager_UpdateFreeRetiredRootDataFromPendingBitsetBanks",
        "Early gate over the particle root-state bank family. Starts from "
        "owner+0x4B0 with fallback into owner+0x528, derives the next candidate "
        "slot index, checks corresponding presence bits, and advances into the "
        "free/retired root-data update pass.",
    ),
    0x140EF6EB0: (
        "BuildMoveModeTransitionRecord_PlayerEnterBalanceMode_GlobalPath",
        "Gameplay-facing runtime producer that hashes the literal "
        "`move-mode-transition`, pairs it with `player-enter-balance-mode`, "
        "resolves/publishes a small structured record through 0x143682D30 and "
        "0x1417D5F50, then seeds a follow-up runtime object/reference slot.",
    ),
    0x140F7D220: (
        "BuildMoveModeTransitionRecord_PlayerEnterBalanceMode_IndexedPath",
        "Sibling gameplay-facing move-mode transition producer. Walks a local "
        "indexed owner/list, then hashes the same `move-mode-transition` + "
        "`player-enter-balance-mode` pair and builds the same style of small "
        "structured runtime record.",
    ),
    0x141271D20: (
        "MatchPlayerMeleeButtonAndGrappleReactionTransitionHashes",
        "Gameplay-facing matcher over explicit player/melee button strings. "
        "Checks hashed names including melee-r1-hold/release, "
        "melee-lmouse-release, melee-rmouse-hold/release, and "
        "`transition-player-into-appropriate-grapple-reaction`, then routes "
        "toward the local melee command queue / command-dispatch family.",
    ),
    0x141272860: (
        "DumpMeleeCommandQueueDebugView",
        "Large debug/inspection path for the same owner family as the melee "
        "command queue. Anchors on the banner "
        "`============== Melee Command Queue ==============` and "
        "`*melee-button-names*`, then iterates/prints structured queue state.",
    ),
    0x141272A50: (
        "DispatchPlayerMeleeButtonHoldReleaseAndGrappleReactionCommands",
        "Live gameplay-facing dispatcher over the melee command queue family. "
        "Probes active button/transition hashes through 0x141E39D50 and, on "
        "match, either inserts queued command entries through 0x141273FA0 or "
        "updates existing entry scalar state through 0x1412743F0. Entry from "
        "PlayerMeleeController tick 0x141279DF0 with (controller+0x150, player).",
    ),
    0x141279DF0: (
        "PlayerMeleeController_DispatchQueuedButtonCommandsTick",
        "Once per engine-clock tick on PlayerMeleeController: reads player backref "
        "at +0x108, compares +0x400 clock stamp, then calls 0x141272A50 on the "
        "queue base at controller+0x150.",
    ),
    0x141279E50: (
        "MeleeCommandQueue_ResolveCommandAgainstPlayer",
        "Vtable +0x1F8 queue resolver. Forwards owner+0x150 queue state, the "
        "requested command descriptor, and owner+0x108 Player to 0x1412712D0.",
    ),
    0x14127A250: (
        "MeleeCommandQueue_QueryMeleeR1",
        "Vtable +0x220 query: hashes literal `melee-r1`, stores the command id "
        "through its int* argument, then invokes the queue resolver at +0x1F8.",
    ),
    0x141273FA0: (
        "PushOrRotateMeleeCommandQueueEntry",
        "Maintains a 16-slot queue of 0x40-byte melee/action command entries. "
        "If requested and an identical command id already exists with zero "
        "secondary scalar, updates that entry in place; otherwise shifts older "
        "entries backward and inserts a new front command containing payload "
        "vectors, scalar values, and a small byte state.",
    ),
    0x1412743F0: (
        "UpdateExistingMeleeCommandQueueEntryScalar",
        "Walks the same 16-slot melee command queue as 0x141273FA0, finds a "
        "matching command id, and updates only the secondary scalar at "
        "slot+0x34.",
    ),
    0x14183E2D0: (
        "Joypad_IsKeyboardMouseInputModeActive",
        "Returns false when the global input manager suppresses PC input; "
        "otherwise returns Joypad+0xBE1, maintained by the native input poll "
        "from keyboard/mouse versus gamepad activity.",
    ),
    0x141274470: (
        "ConstructMeleeCommandQueueOwner",
        "Constructor for the large owner object behind the explicit melee "
        "command queue. Installs vtable 0x142A6C2E8, zeroes the queue/control "
        "state, and anchors on the banner "
        "`============== Melee Command Queue ==============`.",
    ),
    0x1412746B0: (
        "InitializeProcessNpcMeleeManagerOwnerState",
        "Large melee/gameplay manager initializer anchored by the literal "
        "`ProcessNpcMeleeManager`. Seeds external references at "
        "owner+0x108/+0x110/+0x130, resolves/caches authored ids including "
        "`player-attacks`, `player-throws`, `*camera-melee-settings*`, "
        "`*camera-melee-settings-net*`, and `stealth-edge`, builds another "
        "owned runtime object at owner+0x820, and initializes broad local "
        "melee state around +0x6C0/+0x6D0/+0x6F0/+0x7B8/+0x840/+0x854/+0x860.",
    ),
    0x141274E20: (
        "TeardownProcessNpcMeleeManagerOwnerState",
        "Teardown/reset path on the same 0x142A6C2E8 ProcessNpcMeleeManager "
        "family. Releases owned object +0x130 and owned runtime object +0x820, "
        "then drops back to the smaller common base layout.",
    ),
    0x141274EC0: (
        "MarkCachedProcessNpcMeleeManagerTargetEntityActive",
        "Small cached actor-pair helper in the ProcessNpcMeleeManager family. "
        "Checks cached pair/state at +0x138/+0x140 and, when the object id "
        "matches, marks the resolved live entity active via entity+0x72.",
    ),
    0x141283A20: (
        "DestroyProcessNpcMeleeManagerOwnerState",
        "Freeing-dtor style sibling for the 0x142A6C2E8 ProcessNpcMeleeManager "
        "family. Reuses the same release logic for +0x130 and +0x820, then "
        "optionally frees the outer 0x870 owner object.",
    ),
    0x14125C600: (
        "PublishMountedMeleeState1F68ThroughCameraAndPresentation",
        "Large live path over outer_owner+0x1F68. Marks the local state active, "
        "pulls state data through virtual +0x48, packages actor/runtime pairs, "
        "then publishes through the primary screen-route entity / 0x141C0DFD0. "
        "Later uses virtual +0x60 on the same mounted object as a gate.",
    ),
    0x14125E650: (
        "SelectMountedMeleeStateBetween1F68_1F70_AndQueueOwner1F60",
        "Large sibling control-flow path over the mounted melee graph. "
        "Consults outer_owner+0x1F70 through virtuals +0x40/+0x48/+0x38/+0x30, "
        "mixes that with outer_owner+0x1F68, outer_owner+0x1D98, and the queue "
        "owner at +0x1F60 (virtual +0x210), then chooses the follow-up melee/"
        "control path.",
    ),
    0x140DE5CC0: (
        "ConstructMountedPlayerFallAndFreeRopeState1DF8",
        "Constructor for the mounted outer_owner+0x1DF8 object. Installs "
        "0x142A1F690 and seeds hashed/authored state from player-fall/free-rope "
        "anchors such as DiveFall, run^fall-high, desired-fall-state, "
        "free-rope-grip, and time-in-air.",
    ),
    0x140E14490: (
        "ConstructMountedEdgeRopeStaffStateSet1E58",
        "Constructor for the mounted outer_owner+0x1E58 object. Installs "
        "0x142A216C8 and seeds a broad authored edge/corner/wall-rope/"
        "free-rope/staff state set used by the same outer player-control graph.",
    ),
    0x140FCCD20: (
        "ConstructMountedWaterCurrentAndSlideState1E20",
        "Constructor for the mounted outer_owner+0x1E20 object. Installs "
        "0x142A3C490, builds a mounted 0x130 child at +0x138, and anchors to "
        "water-current / slide-barrier / player-slide-barrier-settings / "
        "default-water-forces data.",
    ),
    0x140E4ECA0: (
        "InitializePlayerMeleeControlCoreSubobject_FromOuterOwner1D60",
        "Initializer for the large embedded block under the outer root owner at "
        "+0x1D60. Seeds the 0x142A26CB0 family inside the same rdata region as "
        "player-melee-control.cpp and resets a large player-facing control/"
        "observer state block.",
    ),
    0x1410CA720: (
        "AssembleLargePlayerOwnerSubsystemGraph",
        "Large assembly pass that mounts a long run of player-facing subobjects "
        "into one outer owner. This same pass wires player-melee-control, "
        "look-at IK, inspect/investigate, perch, bump-bend-align, flame/"
        "flashlight, drag-edge, drag-rope, fall, slide, and other mounted "
        "player-control subsystems.",
    ),
    0x140E5DDF0: (
        "ConstructPlayerControlRootOwner1D60_InPlace",
        "Standalone in-place constructor for the 0x142A268C0 root owner family "
        "that later lands in outer_owner+0x1D60. Installs the root table, "
        "initializes the embedded +0x120 player-melee-control core through "
        "0x140E4ECA0, and seeds the same large state block later seen in the "
        "outer assembly pass.",
    ),
    0x1410484F0: (
        "ConstructMountedPlayerInvestigateReadableOwner1E98",
        "Large in-place constructor for the mounted outer_owner+0x1E98 Player "
        "subsystem. Current best read is the Player-investigate/read-mode/"
        "readable-object owner: it lives in recurring per-player update loops "
        "through virtuals +0x430/+0x438/+0x220, and its virtual +0x208 already "
        "participates in Player-tagged gameplay gating.",
    ),
    0x140DCB1A0: (
        "ConstructMountedPlayerCoverEvadeOwner1EB0",
        "Constructor for the mounted outer_owner+0x1EB0 Player subsystem. "
        "Current best read is the Player cover/evade/dodge-roll owner: "
        "surrounding authored/runtime anchors include player-cover.cpp, "
        "player-cover-actions.cpp, player-cover-states.cpp, player-evade.cpp, "
        "use-surface-type-probe, monkey-base, and frags-event-dodging-rolling.",
    ),
    0x140CEDCA0: (
        "ConstructMountedPlayerSubsystem1EE0_Size320",
        "Constructor for the mounted outer_owner+0x1EE0 Player subsystem. "
        "Current best read is a live sibling/activation partner for the same "
        "cover-evade family as 1EB0, but the label stays slot-level until the "
        "class role is pinned harder.",
    ),
    0x14105E220: (
        "ConstructMountedPlayerSubsystem1E68_Size340",
        "Constructor for the mounted outer_owner+0x1E68 Player subsystem. "
        "Semantic role is still unresolved; current value is structural, as "
        "this sits in the same dense Player-side region as investigate/read-"
        "mode and cover/evade owners.",
    ),
    0x141301710: (
        "ConstructMountedPlayerSubsystem1DA0_Size52C0",
        "Constructor for the mounted outer_owner+0x1DA0 Player subsystem. "
        "This is a major Player-side owner by size and placement in the real "
        "Player assembly pass, but it remains semantically unresolved.",
    ),
    0x1410B4B10: (
        "ConstructMountedPlayerSubsystem1D88_Size4B0",
        "Constructor for the mounted outer_owner+0x1D88 Player subsystem. "
        "Builds repeated entry/state storage and helper blocks at +0x2C0 and "
        "+0x380; this matches its later role as a heavy readiness/blocker "
        "owner in the mounted Player qualification family.",
    ),
    0x140D64BF0: (
        "ConstructMountedPlayerSubsystem1DB8_Size15C0",
        "Constructor for the mounted outer_owner+0x1DB8 Player subsystem. "
        "Lives in the same broad Player interaction/combat/presentation band; "
        "kept slot-level until stronger semantic anchors are found.",
    ),
    0x140D64FB0: (
        "RefreshMountedPlayerCooldownAnchorFromOwnerState",
        "Initializes a tiny owner-local helper, probes actor-root state, and "
        "refreshes a cached baseline from owner->0xE0->0x10 when the local "
        "probe or scalar threshold requires it.",
    ),
    0x140D65030: (
        "EvaluateMountedPlayerQualificationHashesAndFallback",
        "Player-side qualification gate that consults outer_owner+0x1D88, "
        "outer_owner+0x1DB8, owner virtual +0x8C0, and fallback helper "
        "0x1410F75C0 before allowing the path.",
    ),
    0x140D65290: (
        "EvaluateMountedPlayerExecutionReadinessCluster",
        "Companion gate to 0x140D65030. Combines outer_owner+0x1D88, "
        "outer_owner+0x1E50, outer_owner+0x1DB8, and outer_owner+0x1D98 "
        "through a compact Player-side readiness/execution test.",
    ),
    0x1410F75C0: (
        "CheckOwner2E0HashAgainstKnownMountedPlayerActionFamilies",
        "Fallback hash-family checker used by 0x140D65030. Builds two known "
        "hash sets, asks the owner for a +0x2E0 record, and rejects paths "
        "when that record already belongs to one of the known action families.",
    ),
    0x140D654F0: (
        "EvaluateAggregateMountedPlayerExecutionPolicy",
        "Aggregate Player-side execution policy gate. Succeeds only when a "
        "family of lower threshold/cooldown checks stays clear, including "
        "0x140D65940, 0x140D65540, 0x140D65740, and 0x140D65B20.",
    ),
    0x140D65B20: (
        "CheckMountedPlayerCooldownWindowByRegistryScalar",
        "Resolves a hashed registry scalar, scales it, and compares it against "
        "the delta between owner->0xE0->0x10 and a cached helper baseline at "
        "+0x8. Used as the cooldown/window member of the mounted Player "
        "execution policy family.",
    ),
    0x1410DD0FC: (
        "InvokePlayerOwnerAssemblyFromPlayerInitChain",
        "Direct callsite where the higher-level player.cpp-anchored init path "
        "invokes 0x1410CA720. This is currently the strongest upward link from "
        "the mounted player-control graph into the real Player construction/"
        "initialization chain.",
    ),
    0x1410DC160: (
        "InitializeTopLevelPlayerOwnerFromProcessSpawnInfo",
        "Large top-level Player construction/init pass on the outer owner. "
        "RTTI/vtable layout now confirms this is a real Player virtual method at "
        "slot +0x58 of vtable 0x142A4BFE8. Seeds broad Player state, creates "
        "the owner-local +0x3BD0 runtime object through 0x1410DE520, invokes "
        "0x1410DD0FC -> 0x1410CA720 to assemble the mounted graph, and then "
        "continues higher-level finalize/setup work through +0x1DF0, +0x1A8, "
        "+0x1B10, +0x3D98, and +0x3C20. STATIC appearance ordering: reads "
        "payload=ProcessSpawnInfo+28 at 0x1410DC703 and index=payload+24 at "
        "0x1410DC7B7; publishes own A8/BC tuple into retained table "
        "0x143EEBE80+index*16 at 0x1410DC868. This precedes base Init call "
        "0x1410DCAFF -> 0x140CBFF50 -> 0x141E994C0 -> 0x141BB4280 and its "
        "appearance callback 0x141BB4AB2. Normal slot0 initialization has "
        "already retained this Player; do not remove the identity gate on "
        "a claim that only outer post-return publishers populate the table.",
    ),
    0x1410DE520: (
        "InitializeOwnerLocalRuntimeObject3BD0_Size190",
        "In-place initializer for the owner-local runtime/control object "
        "stored into outer_player+0x3BD0 during the top-level Player init "
        "pass. Seeds vtable 0x142A4BFD8 and two repeated state bands with "
        "0x1000-sized capacity fields, making +0x3BD0 one of the strongest "
        "upper promotion/rebind anchors above the mounted graph. Its local "
        "+0x108 field belongs to this sibling owner-local object, not to the "
        "top-level Player descriptor/id pair later consumed by 0x140CC1450.",
    ),
    0x1410DE740: (
        "Player_AllocateAnimData",
        "Exact Player vtable +0x6F0 implementation. Its source assertion is "
        "Player::AllocateAnimData(const ArtItemSkeleton*); it delegates the "
        "Player-specific allocation/setup to 0x140CC1450 and returns the "
        "FgAnimData consumed by NdGameObject_Init at Player+0x1A8. The "
        "separate concrete slot from Npc_AllocateAnimDataForDrawable proves "
        "the retained Player1 and native NadineBuddy own independent Fg data.",
    ),
    0x1413C04A0: (
        "TaskManager_SpawnPrimaryPlayerFromPersistentState",
        "STATIC source TaskManager::SpawnPlayer, task-manager.cpp:320; base "
        "and MainMenu virtual+248, reached from1413C2383 when primary weak "
        "slot is invalid. Copies fresh checkpoint CharacterPersistentState "
        "into caller-local storage via1413C0675, then explicitly EDX=0 at "
        "1413C0763 and calls1410F1910 at1413C076A. Old Secondary name was wrong. "
        "At call R14=live manager, R13=Continue/R8; arg6 points to local value. "
        "Checks returned record/id at1413C076F..0790, then virtual+240 "
        "slot0 state/flashlight publication. Borrowed local arg6 dies on "
        "outer return; deferred spawning needs its own value copy, not replay.",
    ),
    0x1413BF7B0: (
        "TaskManager_PublishRetainedFlashlightRuntimeRecords_Slot240",
        "TaskManager / TaskManagerPlayMovie virtual +0x240 implementation. "
        "Consumes an incoming packet field at +0x14, validates live actor "
        "identity through the global retained slot table 0x143EEBE80, hashes "
        "`spawn-flashlight` and later `turn-on-flashlight`, then publishes "
        "structured records into live actors through virtual +0xD0. This is "
        "a real upper retained/runtime TaskManager path, but it is flashlight/"
        "state publication, not the direct Player promotion hook for Nadine.",
    ),
    0x1413C3310: (
        "TaskManager_BootstrapMenuAndMainMenuRuntimePublishOnce_Slot238",
        "TaskManager / TaskManagerPlayMovie virtual +0x238 implementation. "
        "One-time gated initializer that hashes `menu` and `main-menu`, "
        "resolves/creates manager-side records through the 0x141CBA1B0 / "
        "0x141CBAC10 / 0x141CBACC0 / 0x141CBAE10 family, and publishes a "
        "compact packet through 0x141CB95B0. This looks like a TaskManager "
        "menu/main-menu bootstrap/publish stage, not a Player ownership "
        "binder.",
    ),
    0x1413BCAE0: (
        "TaskManagerPlayMovie_ActivateRetainedCameraOverlayState_Slot258",
        "TaskManagerPlayMovie override for virtual +0x258. Touches global "
        "state around 0x143FE10D8, drives 0x1417D5F50, calls 0x1410F3DD0, "
        "and sets this+0x1F0=1, which makes it look like a specialized "
        "play-movie / retained-camera / overlay activation helper rather than "
        "a general Player promotion hook.",
    ),
    0x1410DE430: (
        "InitializeOwnerLocalFixedEntryTable1E_Size7A0",
        "Sibling upper-layer owner-local object initializer beside "
        "0x1410DE520. Zeros repeated pointer fields every 0x40 bytes, seeds "
        "object+0x10 = self+0x20, and sets object+0x18 = 0x1E, which looks "
        "like a fixed-entry table/list owned by the same top-level Player "
        "band above the mounted graph.",
    ),
    0x1410DE660: (
        "StampUpperPlayerVirtualFamilyVftableBFC0",
        "Tiny vptr stamper for the upper Player virtual-family object whose "
        "family block begins at 0x142A4BFC0. The surrounding assembly pass "
        "allocates this sibling upper-layer object and stores it at "
        "outer_owner+0x2018, which ties 0x1410DC160 and 0x1410DE740 to the "
        "same upper class family as the +0x3BD0 registry object. The raw "
        "table should be treated as a mixed upper-family block rather than a "
        "clean function-only vtable read.",
    ),
    0x1410C6BA0: (
        "GetUpperPlayerVirtualFamilyObject2018",
        "Trivial getter that returns outer_owner+0x2018. This pins +0x2018 as "
        "a first-class upper-layer runtime object in the same Player band as "
        "+0x3BD0 rather than a transient setup pointer.",
    ),
    0x140CBFB30: (
        "InitializeSharedBaseCtorBeforePlayerVftable",
        "Shared base ctor reached first from 0x1410C8FD0 before the real "
        "Player vtable is stamped. Zero-initializes the common action/"
        "gameplay band, including +0x15B4/+0x15C8/+0x1680/+0x1748/+0x1808/"
        "+0x1B10, and initializes the embedded object at +0x1A20. It does "
        "not visibly seed the later descriptor/id pair at player+0x108/"
        "+0x10C.",
    ),
    0x140CBFF50: (
        "InitializeSharedPlayerPolicyStateFromUpperInitPacket",
        "Shared upper/base init stage beneath the visible Player-only phases. "
        "Consumes a compact incoming packet through arg3+0x28, copies packet "
        "fields into player+0x15A4/+0x15A8/+0x15AC/+0x15B0, merges packet "
        "bit0 into player+0x161A, allocates/builds player+0x1B10, and then "
        "continues through 0x141E994C0 while seeding broader shared state "
        "around +0x16A0/+0x1680/+0x16AC/+0x16D0/+0x1A8. A direct pass also "
        "shows a copy from shared source 0x143FE1088 into the upper Player "
        "band +0x18B0..+0x1910, including source+0x108 -> player+0x18E0; "
        "this still does not directly seed player+0x108/+0x10C.",
    ),
    0x1410C9F20: (
        "InitializeEmbeddedPlayerAuthoredDescriptorState100",
        "Small in-place initializer for the embedded Player subobject at "
        "player+0x100. Writes vtable 0x142AB08C8, clears late fields around "
        "+0x110/+0x122/+0x128, but does not itself seed the authored key later "
        "consumed at player+0x108/+0x10C.",
    ),
    0x1410C9F50: (
        "ZeroEmbeddedPlayerAuthoredDescriptorKey_8_10",
        "Tiny helper immediately adjacent to the embedded +0x100 initializer. "
        "Clears the key fields at object+0x8 and object+0x10 and returns the "
        "same pointer. No direct rel32 callers have surfaced yet, but the "
        "shape matches the hidden authored pair inside player+0x100.",
    ),
    0x1416860A0: (
        "BoundTransform_SetWorldTransformRelativeToParent",
        "STATIC descriptive name; shared BoundTransform helper, not Player-"
        "specific. Resolves bound+8 key/+C generation through table14386FD00 "
        "and entry+50; only a valid match refreshes cached parent+10/+20, "
        "otherwise it retains the cache. Calls1401990F0 to compose inverse "
        "parent with requested world transform, then stores local+30/+40. "
        "Actor bound+100 therefore writes owner+130/+140; used by physics "
        "import141E59C36 and NPC/Player paths. No owner callback, joint-cache "
        "invalidation or Havok queue is performed by this helper. A null key "
        "does not establish an identity cached parent.",
    ),
    0x1416858E0: (
        "BuildCompactDescriptorCarrierFromRuntimeObject50_54",
        "Builds a small carrier record from a runtime object that exposes "
        "dword +0x50 and word +0x54. Seeds carrier header words with "
        "0x7FCCCCCC, copies +0x50/+0x54 into the compact descriptor key at "
        "+0x8/+0xC when present, and clears the local transform/quaternion "
        "bands. In Player::Init this feeds 0x1416862B0 and refreshes the "
        "embedded authored descriptor state under player+0x100, not the "
        "canonical Player live pair at +0xA8/+0xBC.",
    ),
    0x141685B40: (
        "InitializeEmptyExtendedDescriptorCarrier",
        "Initializes the larger empty descriptor carrier used by the wide "
        "0x50-byte descriptor banks under Player bringup and sibling gameplay "
        "paths. Seeds the same 0x7FCCCCCC headers, zeroes local state, and "
        "copies the default vector bands from 0x1428805D0.",
    ),
    0x1416862B0: (
        "RefreshEmbeddedPlayerAuthoredDescriptorFromCompactCarrier",
        "Consumes the compact descriptor carrier built by 0x1416858E0 and "
        "refreshes the embedded Player authored-descriptor object rooted at "
        "player+0x100. Validates the keyed entry against 0x14386FD00, copies "
        "resolved records into +0x10/+0x20, rebuilds derived basis/vector "
        "state at +0x30/+0x40, and leaves the canonical Player live pair at "
        "+0xA8/+0xBC untouched.",
    ),
    0x141E7C600: (
        "CompareEmbeddedDescriptorCarrierOrderingAndWarn_141E7C600",
        "Small comparison/logging helper immediately after the ret stub at "
        "0x141E7C5F0. Compares two carrier objects through their virtual +0x28 "
        "results, logs a warning when ordering is unexpected, and returns a "
        "boolean-like permit byte. The real Player::Init call still targets "
        "the stub at 0x141E7C5F0, so this body is a sibling implementation, "
        "not the active early bringup step.",
    ),
    0x1413BF090: (
        "TaskManager_ResolveOrSpawnCompanionFromLevelEntity",
        "STATIC exact3args: descriptor DWORD, class-hash QWORD (unused in "
        "current body), name pointer. Native NULL contract for id0/disable "
        "byte143D441A4/missing entity/readiness. Resolves existing NPC or "
        "level entity, builds override, tail-calls EntitySpawner_Spawn at "
        "1413BF304 with arg4TRUE. This sets spawner+51bit08 and bypasses "
        "spawner-own weak-pair publication/level association; TaskManager "
        "publishes its own companion pair. Nadine-only caller1413C26C5.",
    ),
    0x140A90E50: (
        "GetNetGameManagerInlineSlotBand43D0",
        "Returns the inline 0x43D0 per-slot band at manager+0x3C0+idx*0x43D0. "
        "Runtime readback from slot 1 shows session/map/mode string state "
        "such as `NET_MAP_REMNANTS_FOREST_VIP_TDM`, so this is not the "
        "materialized tracker object used by the spawn path.",
    ),
    0x140A90E70: (
        "GetNetGameManagerTrackerPointerArrayBase",
        "Small getter for the pointer-family rooted at manager+0x3A0. Treat "
        "this as an upper pointer-array helper, not as the real inline slot "
        "object seam.",
    ),
    0x140A90E80: (
        "GetMeNetPlayerTrackerByScreenIndex",
        "Screen-indexed accessor in the NetGameManager family. The assert "
        "strings explicitly mention `NetGameManager::GetMeNetPlayerTracker`, "
        "but runtime probing showed that treating this as a direct safe "
        "tracker getter can yield code-like pointers instead of a live slot "
        "object. Keep it demoted below the explicit inline-slot helper "
        "0x140A90E50 until a clean tracker-object contract is proven.",
    ),
    0x140A90F40: (
        "FindNetPlayerTrackerByRuntimePlayerObjectAndPlayerHash",
        "Scans the two-slot NetGameManager tracker family for a tracker entry "
        "that matches the hashed `Player` token and the supplied runtime "
        "player object. This is a broader generic tracker lookup than "
        "GetMeNetPlayerTrackerByScreenIndex and appears all over gameplay code.",
    ),
    0x140A38FA0: (
        "DriveDualSlotNetPlayerBringupFromCtxA_WithCtxBAvailabilityGates",
        "Upper dual-slot bringup caller above 0x140A90550. It does inspect "
        "ctxB=0x143E0EF00 through 0x14194D5F0 and count/selection gates, but "
        "the actual AddOrUpdate calls at 0x140A392EC/0x140A3936A still pass "
        "rdx=&0x143E08C80 and source entries resolved from ctxA. This makes "
        "ctxB a gating/availability input here, not the direct source context "
        "for manager-side slot creation.",
    ),
    0x140A9AAD0: (
        "NetGameManager_RespawnPlayer",
        "Manager-side respawn path identified by the `NetGameManager::RespawnPlayer` "
        "string xref. Works on a live tracker object, stages fresh state into "
        "tracker+0x500..+0x540 and tracker+0x840/+0x844, then either hands "
        "control to NetPlayerTracker_StartOrQueueSpawnPlayer with edx="
        "tracker+0x4F8 or queues a deferred respawn through tracker+0xA00/"
        "+0xA04 with timing in tracker+0x4C0/+0x4C8.",
    ),
    0x140A90550: (
        "NetGameManager_AddOrUpdateNetPlayerTracker",
        "Heavy manager-side tracker bringup/update helper from the "
        "`NetGameManager::AddOrUpdateNetPlayer` string family. Scans the "
        "0x43D0 tracker array, allocates or selects a tracker object, seeds "
        "tracker state from incoming spawn/loadout data, writes payload into "
        "tracker+0x7C0..+0x7DF/+0x7E0/+0x7B8/+0x1DC/+0x7BC, writes the "
        "effective screen/player index into tracker+0x1E0, derives "
        "tracker+0x1E4 from the `NetCharacter-` token family, and stores the "
        "tracker in manager+0x3A0/+0x3B0 under the matching local-player "
        "conditions. Real callers now pin the low call shape as: rcx=global "
        "manager 0x143DD70C0, rdx=shared context 0x143E08C80, r8=source "
        "object, r9d=slot, stack arg5=flag. Also note that direct callers "
        "still apply additional post-call tracker initialization, so this is "
        "the low create/update worker rather than the whole stable bringup "
        "sequence by itself. PIVOT SEAM: resolves the source via ctxA "
        "(lea rcx,[0x143E08C80]; call 0x14194E510) at 0x140A9075E, NOT ctxB. "
        "Arg5 is an is-local-player flag: when set, slot+0x1DC is taken from a "
        "local-id counter byte at 0x14304172A (then incremented) and slot+0x1E8 "
        "records local-vs-remote. is_local varies per caller (0x140A58287->0, "
        "0x140A392EC->r15b, 0x140A3A02C/06E->r14b); the caller passing 1 is the "
        "ready local-second-player construction to mirror for campaign co-op.",
    ),
    0x140ACBE10: (
        "ConstructNetSharedContextBase_0x800Entries",
        "Base constructor used by the global shared-context objects at "
        "0x143E08C80 / 0x143E0EF00. Seeds a large 0x800-backed entry family, "
        "zeroes the embedded owner pointer at +0x8, and initializes the tail "
        "helper block through 0x140AC62D0.",
    ),
    0x140072A50: (
        "InitializeCtxAObjectAndInstallVtable_1429E61F0",
        "Early startup builder for ctxA at 0x143E08C80. Calls the shared "
        "base constructor 0x140ACBE10 and then installs the ctxA-specific "
        "vtable 0x1429E61F0.",
    ),
    0x14007C100: (
        "InitializeCtxBObjectAndInstallVtable_1429EF890",
        "Early startup builder for ctxB at 0x143E0EF00. Calls the shared base "
        "constructor 0x140ACBE10, zeroes the embedded owner field at +0x8, "
        "installs the ctxB-specific vtable 0x1429EF890, and seeds "
        "0x143E10E88.",
    ),
    0x140B1E050: (
        "ResetCtxBObjectAndClearPendingSelectionGlobals",
        "Global ctxB reset helper. Calls 0x14194AF00 on 0x143E0EF00 and then "
        "clears the staged pending-selection globals around "
        "0x143E10E80/0x143E10E90/0x143E10EC4..+0x10. It does not consume the "
        "incoming rcx and is not itself the owner-teardown path.",
    ),
    0x140B1E100: (
        "PublishCtxBPendingSelectionVector",
        "Small ctxB-side publisher for pending selection state. Marks the "
        "family dirty, stores the candidate pointer into 0x143E10E90, and "
        "optionally mirrors the vector block into 0x143E0F618..+0x38 before "
        "clearing 0x143E10EC8.",
    ),
    0x140B1E400: (
        "ProcessCtxBCandidateSyncAndStagePendingSelection",
        "Major ctxB lifecycle/synchronization pass. Scans candidate objects, "
        "compares them against the current-selected pointer at ctxB+0x18 "
        "(global 0x143E0EF18), and on mismatch stages a pending selection into "
        "0x143E10E90 plus 0x143E0F618..+0x38 before calling 0x140BF1FF0(1,0). "
        "This is a pending-selection publisher, not the final writer of "
        "ctxB+0x18.",
    ),
    0x140B1E890: (
        "RunCtxBSelectionStateMachineAndDispatchPendingCommit",
        "Large ctxB-side state machine keyed by 0x143E10E84. Walks ctxB/ctxA "
        "selection bands, publishes intermediate records through "
        "0x140A59540/0x140141840, and reaches the first confirmed pending "
        "commit handoff at 0x140B1ECBC.",
    ),
    0x140B1DF30: (
        "ReturnFixedCtxBSelectionToken_0xA",
        "ctxB vtable slot +0x28 from the concrete vtable installed by "
        "0x140B1DF80. Returns fixed token 0xA. This corrected the earlier "
        "misread that mapped ctxB->virtual +0x28 to 0x140B204E0.",
    ),
    0x140B1ECBC: (
        "DispatchCtxBPendingSelectionToGlobalCommitService",
        "First confirmed pending->commit handoff for ctxB. Loads the staged "
        "candidate from 0x143E10E90, calls *(0x143880318)->virtual +0x48 with "
        "(&ctxB=0x143E0EF00, pending=r8), then clears 0x143E10E90 and emits a "
        "follow-up packet through 0x142204E50. The remaining unknown is the "
        "service-side implementation of the +0x48 commit.",
    ),
    0x140B1EE70: (
        "DispatchFixedCtxBSelectionTokenThroughGlobalService28",
        "Active campaign request path for ctxB. First calls ctxB->virtual "
        "+0x28, which is the fixed-token slot 0x140B1DF30 returning 0xA, then "
        "forwards that token into *(0x143880318)->virtual "
        "+0x28(service, &ctxB, token, 1). This is the live request surface "
        "above the service-side commit family.",
    ),
    0x140B204E0: (
        "BuildHeavySelectionPayloadFromGlobalObjectList",
        "Neighboring heavy request/payload builder in the local ctx/service "
        "family. Computes a CRC-like token from a fixed authored string, folds "
        "in optional world/runtime data, iterates the global object list "
        "rooted at 0x143FE1128+0x3D0, and packages the result through "
        "0x141C9ED50. Current vtable decode no longer ties this worker to "
        "ctxB->virtual +0x28.",
    ),
    0x140B1E220: (
        "EmitCtxBSelectionOrOwnerSourceEvents_0x104",
        "ctxB-side notification/emission helper. With a direct source pointer "
        "it emits one 0x104 packet through 0x140A59540; without one, if "
        "ctxB+0x8 is bound, it first rebuilds the owner/fallback source list "
        "through 0x14194D5F0 and emits 0x104 for each non-current source. "
        "This is downstream of owner bind, not the bind itself.",
    ),
    0x140B21DE0: (
        "ResolveCtxBSecondPlayerSelectionForSplitRefresh",
        "Helper used by the split/local-player refresh family. Checks the "
        "ctxB source-count path through 0x14194CFB0, falls back through the "
        "sub-sign-in descriptor block at 0x143071008 when needed, and "
        "otherwise returns the active/pending ctxB selection. This keeps the "
        "stock split player-2 source tied to ctxB rather than a campaign-"
        "native Nadine path.",
    ),
    0x140B21E70: (
        "HasCtxBResolvedSourceWithFlag42",
        "Post-bind consumer gate for ctxB. Requires both ctxB+0x18 and "
        "ctxB+0x8, builds a source list through 0x14194D5F0(&ctxB, out, 0), "
        "resolves each entry through 0x14194E510, and returns success once a "
        "resolved object has byte +0x42 set.",
    ),
    0x140B21F10: (
        "IsCtxBSourceListUnavailableOrEmpty",
        "Companion post-bind consumer gate for ctxB. If ctxB+0x18 or ctxB+0x8 "
        "is missing it returns true/unavailable; otherwise it calls "
        "0x14194D5F0(&ctxB, out, 0) and returns false once at least one "
        "source entry is available.",
    ),
    0x140AEAAFC: (
        "NotifyCtxBOwner58ThenRebuildSources_0x114",
        "Live owner-consumer path for ctxB. If ctxB+0x8 is bound, calls "
        "owner->virtual +0x58(&ctxB, 1), then immediately rebuilds the ctxB "
        "source list through 0x14194D5F0. This is a strong downstream anchor "
        "for the real ctxB owner contract.",
    ),
    0x140835A70: (
        "DispatchCtxBOwner70ForIndexedSource",
        "Loads an indexed source entry, then if ctxB+0x8 is bound calls "
        "owner->virtual +0x70(&ctxB, source_id_from_entry+0xE8). This pins a "
        "second live method on the concrete ctxB owner interface.",
    ),
    0x14083E580: (
        "DispatchCtxBOwner70ForResolvedSource",
        "Sibling owner-consumer path that resolves a source entry and then "
        "calls owner->virtual +0x70(&ctxB, source_id). Another live user of "
        "the same bound ctxB owner interface.",
    ),
    0x1409F9B20: (
        "RunNetStartupCallbackThatBindsGlobalSelectionService",
        "Outer startup callback used by the net/front-end lifecycle. The "
        "restart path in 0x1409FBA60 takes this address and passes it through "
        "0x1415B43B0 -> 0x1415B4870 -> 0x1415B47C0 as a callback record. Its "
        "inner bringup branch at 0x1409FA94E installs the mode-dependent "
        "global selection service and performs the shared-context binds.",
    ),
    0x1409FA94E: (
        "InitializeModeDependentGlobalSelectionService_143880318",
        "Inner bringup block inside 0x1409F9B20. Installs a mode-dependent "
        "global selection service into 0x143880318 (and sibling slots "
        "0x143880300/0x143880320), binds it through 0x1409EB500 with an "
        "upper context packet, then runs the service virtual +0x8 bringup "
        "path. 0x1430641DC is the Net Init Error Testing selector. If init "
        "succeeds and 0x1430641DC!=0x14, startup keeps variant A; only "
        "failing init or 0x1430641DC==0x14 constructs variant B and "
        "overwrites 0x143880318. The later startup bind calls "
        "service->virtual +0x20 exactly three times: for 0x143E08C80, for "
        "0x143E0AC40, and for ctxB=0x143E0EF00 via rsi set at 0x1409FA6DE. "
        "This supersedes the earlier misread that startup skipped ctxB.",
    ),
    0x1409EB500: (
        "CopyUpperPacketHeaderIntoGlobalSelectionService",
        "Tiny startup helper used by the mode-dependent global selection "
        "service. Copies the first 0x20 bytes plus trailing dword from the "
        "upper packet in r8 into the live service object at 0x143880318 "
        "before the service virtual +0x8 bringup stage runs. Those exact "
        "owner+0x8..+0x28 bytes are what 0x14194D5F0 later clones into "
        "ctxB+0x1D00 for the single-entry fallback source path.",
    ),
    0x1409EE7D0: (
        "HandleFrontEndStageTokenForNetShutdown",
        "Upper token handler in the net/front-end lifecycle. Hash-matches the "
        "incoming stage token against strings including 'added-to-stage' and "
        "'popped'; on the matched transition it can drive the broad "
        "0x1409FD360 shutdown path.",
    ),
    0x1409EE9B0: (
        "RunMultiplayerFrontEndShutdownFlow",
        "Large multiplayer/front-end state machine above the global net "
        "shutdown path. References tokens such as 'ss-fmenu-multi-main', "
        "'PAGE_MULTIPLAYER_CONNECTING', and 'part-module/part-multiplayer', "
        "and calls 0x1409FD360 from that upper lifecycle band.",
    ),
    0x1409F78B0: (
        "HandleNetUpdateTokenAndMaybeRestart",
        "Upper event/token handler keyed by the hash of 'update'. Works "
        "through the 0x14383E560 interface (virtual +0x30/+0x38), and on the "
        "restart branch calls 0x1409FD360 before continuing into the paired "
        "net re-entry worker at 0x1409FBA60.",
    ),
    0x1409F7C60: (
        "HandleNetUpdateOrPoppedTokenAndMaybeRestart",
        "Sibling upper event/token handler above the same global shutdown "
        "family. Checks hashes for 'update' and 'popped', drives "
        "0x14383E560->virtual +0x40 on one branch, and can call 0x1409FD360 "
        "plus 0x1409FBA60 from that front-end/net lifecycle.",
    ),
    0x1409FBA60: (
        "NdiNetInit",
        "CONFIRMED by decoded strings: NdiNetInit from game/net/flow.cpp "
        "(!g_ndiStartNetInit, 'Trying to initialize net while it's active', "
        "PAGE_MULTIPLAYER_CONNECTING, popup_mp_connecting, pMainPageStack). "
        "This is the multiplayer net-init flow. Around 0x1409FD068 it builds an "
        "0x80-byte ndjob record with callback field 0 = 0x1409F9B20 (priority "
        "3), then schedules it via 0x1415B43B0/0x1415B4870/0x1415B47C0. The "
        "ctxB owner rebind is therefore gated behind multiplayer net-init; "
        "single-player campaign never runs this, which explains live "
        "ctxB+0x8 == 0.",
    ),
    0x1415B43B0: (
        "NdjobAllocWorkBatch",
        "ndjob (corelib/job/job.cpp) work-batch allocator from scheduler global "
        "0x1437E9700. Generic job system, not net-specific. Used by NdiNetInit "
        "before staging the 0x1409F9B20 record.",
    ),
    0x1415B4870: (
        "NdjobCopyJobRecords",
        "Copies one or more 0x80-byte ndjob job records into the batch. In the "
        "NdiNetInit path this stages the record whose callback field 0 is "
        "0x1409F9B20. Animation SpawnerJob calls at 0x1415C5350; four "
        "32-byte copies preserve callback+0/data+8/counter+0x10 before "
        "queue count/cursor advancement at 0x1415B48ED..0x1415B494E.",
    ),
    0x1415D0810: (
        "Ndjob_TryAcquireRunnableWork",
        "Generic ndjob ready-work selection, called from 0x1415D16AA. "
        "Ordinary queued-record branch at 0x1415D0C8F..0x1415D0D18 "
        "decrements count, advances/wraps cursor under scheduler lock, "
        "then copies the 0x80-byte job into worker storage; surrounding "
        "paired continuation/work branch at 0x1415D0B08..0x1415D0C6D "
        "is distinct from that ordinary dequeue. At-most-once removal is "
        "not unconditional execution or a no-reentrant-dispatch proof.",
    ),
    0x1415D0F20: (
        "Ndjob_ExecuteWorkerRecordAndFinalize",
        "Generic worker execution from 0x1415D1AD4/0x1415D1AF2. At "
        "0x1415D11B3 loads RCX=record+8 and at 0x1415D11B7 calls "
        "record+0 directly; result ignored. Completion handling reads "
        "record+0x10 at 0x1415D135F when continuation field +0xA8 is zero. "
        "Animation third-category records "
        "carry AsyncObjectUpdateJointBlendJob and its 0x38-byte data. "
        "Do not detour this global dispatcher just to scope one job type.",
    ),
    0x1400F6090: (
        "Ndjob_RegisterJointBlendJobName",
        "Static initializer for registration row 0x143714A10. Hashes name "
        "AsyncObjectUpdate_JointBlend_Job, links prior head 0x1437E96D8 "
        "into row+0x10 and publishes the new head at 0x1400F60F4. Row+8 "
        "is callback 0x141A92A00, but this initializer does not call it.",
    ),
    0x1415D15B0: (
        "Ndjob_WorkerFiberLoop",
        "Generic worker fiber loop: acquires runnable work at 0x1415D16AA "
        "through 0x1415D0810 and dispatches ordinary/resumed worker state "
        "through 0x1415D0F20 at 0x1415D1AD4/0x1415D1AF2. Scheduling is "
        "fiber-based, not one indivisible job per OS thread.",
    ),
    0x1415D1BE0: (
        "Ndjob_CooperativelyWaitCounterOnWorkerFiber",
        "Worker branch of Ndjob_WaitForCounter at 0x1415B5419. Copies "
        "continuation state, schedules waiting/resume records, obtains a "
        "fiber through 0x1415D0330 and calls Platform_SwitchToStoredFiber "
        "at 0x1415D1E43. FLS profiling state is restored at 0x1415D1F29. "
        "C++ thread_local overlay scopes are not fiber-local: any reachable "
        "wait may expose suspended state to another job on the OS thread.",
    ),
    0x142205850: (
        "Platform_SwitchToStoredFiber",
        "Thin platform wrapper: SwitchToFiber(*arg1) at 0x142205853. "
        "Called by ndjob cooperative worker wait at 0x1415D1E43; preserves "
        "OS thread while changing execution stack/fiber-local storage.",
    ),
    0x142204DB0: (
        "Platform_CreateFiber",
        "STATIC: ndsys::CreateFiber in thread-win.cpp. Calls CreateFiberEx "
        "at 0x142204E2B with 0x2000 commit, caller reserve/entry/parameter. "
        "Ndjob init supplies entry 0x1415B1DF0 at 0x1415B2C42/0x1415B2CBE "
        "and calls here at 0x1415B2C50/0x1415B2CCC. Actual Windows fibers, "
        "not merely an engine name for OS threads.",
    ),
    0x1422054C0: (
        "Platform_StartFiberExecution",
        "STATIC: source-identified ndsys::StartFiberExecution. Converts "
        "worker thread with ConvertThreadToFiber at 0x142205585, retains "
        "return fiber in TLS+0x80, switches at 0x1422055E6 and calls "
        "ConvertFiberToThread after return. Ndjob worker calls at 0x1415B1DBD.",
    ),
    0x1415B1D30: (
        "Ndjob_WorkerThreadEntry",
        "STATIC: OS worker thread entry initializes COM/platform thread, "
        "acquires fiber via 0x1415D0330 at 0x1415B1DB5, enters it through "
        "Platform_StartFiberExecution at 0x1415B1DBD, then cleans up and "
        "ends the OS thread. Distinct from worker fiber entry 0x1415B1DF0.",
    ),
    0x1415B1DF0: (
        "Ndjob_WorkerFiberEntryInitializeJobLocalStorage",
        "STATIC: source string InitializeJobLocalStorageAtFiberStart, "
        "job-system.cpp. Allocates 0x260 at 0x1415B1E3B and sets engine "
        "FLS at 0x1415B1E96 before WorkerFiberLoop at 0x1415B1F44. "
        "Installed as CreateFiberEx callback by two init callsites. Mod "
        "overlay scope must use its own FLS indices, never engine FLS.",
    ),
    0x1415B3E00: (
        "Ndjob_ShutdownWorkersFibersAndJobLocalStorage",
        "STATIC: marks stop at 0x1415B3E1D, signals/joins worker threads "
        "before ReleaseFiber calls 0x1415B3E9A/0x1415B3ECA, clears "
        "scheduler global, then FlsFree at 0x1415B3F9B. Does not establish "
        "arbitrary live-fiber deletion safety or DLL unload permission.",
    ),
    0x1422051E0: (
        "Platform_ReleaseFiber",
        "STATIC: source-identified ndsys::ReleaseFiber. Checks *arg1, "
        "calls DeleteFiber at 0x142205237 then clears handle. Ndjob "
        "shutdown calls only after its worker join phase. Borrowed "
        "stack overlay FLS pointers must not have a cleanup callback.",
    ),
    0x1415B47C0: (
        "NdjobFinalizeAndSubmitBatch",
        "Finalizes the ndjob batch and tails into enqueue 0x1415B4560. Callback "
        "is not invoked inline.",
    ),
    0x1415B4560: (
        "NdjobEnqueueToLane",
        "ndjob per-lane enqueue. Global 0x1437E9700 holds a pointer to ndjob "
        "scheduler state; lane array at state+0x4FC0, stride 0x2200, depth cap "
        "0x400. Priority-inserts the task-group pointer at lane+0xC8 (count at "
        "lane+0); lane id = group+0x4C, affinity mask = group+0x38; wakes "
        "workers via import 0x1425D6668. Drain side is the generic ndjob worker "
        "pool, which always runs.",
    ),
    0x1409FD360: (
        "ShutdownNetFrontEndAndTeardownGlobalSelectionService",
        "Broad net/front-end shutdown path, not just a local ctxB helper. "
        "Toggles the category-0x16 subsystem gates, resets ctxB/ctxA active "
        "entries, clears the bound owner globals at 0x143E0EF08/0x143E08C88, "
        "tears down 0x143880318 through virtual +0x18/+0x0, and clears the "
        "service aliases. Direct callers now sit in upper front-end/net "
        "transition handlers rather than the local ctxB object family.",
    ),
    0x1409FDD70: (
        "TeardownGlobalSelectionService_143880318",
        "Inner teardown block inside 0x1409FD360. Resets the concrete shared "
        "contexts, calls service virtual +0x18 and then virtual +0x0 when "
        "the service is live, and finally clears 0x143880318 plus the "
        "sibling alias at 0x143880300.",
    ),
    0x140072A54: (
        "InitializeGlobalNetSharedContextA_143E08C80",
        "Early startup init for the concrete global shared-context object at "
        "0x143E08C80. Calls ConstructNetSharedContextBase_0x800Entries and "
        "then swaps in a context-specific vtable.",
    ),
    0x14007C104: (
        "InitializeGlobalNetSharedContextB_143E0EF00",
        "Early startup init for the sibling global shared-context object at "
        "0x143E0EF00. Reinitializes the object, explicitly zeroes the "
        "embedded owner field at +0x8 (global 0x143E0EF08), and installs its "
        "context-specific vtable.",
    ),
    0x14194CD20: (
        "ResetNetSharedContextActiveEntries_12x0x1C0",
        "Reset/teardown helper for the shared-context objects. Iterates the "
        "12-entry 0x1C0-stride family rooted at +0x800, clears active "
        "entries, and resets their +0xE0/+0xF0 state.",
    ),
    0x14194AF00: (
        "HardResetSharedContextEntryBandsAndOwnerState",
        "Hard reset for the ctxA/ctxB-style shared-context objects. Clears the "
        "12 active 0x1C0-stride entry bands, resets current-selection state "
        "at +0x18/+0x200/+0x208/+0x20C/+0x1ED0..+0x1ED4, zeroes the embedded "
        "owner field at +0x8, re-seeds default bytes around +0xA1/+0xB0..+0xF0 "
        "and +0x7C0/+0x748, and resets the pending-token/output fields near "
        "+0x1F54.",
    ),
    0x14194CFB0: (
        "CountCtxBActiveSourcesOrFallbackOne",
        "Counts active source entries for the second shared-context family. If "
        "ctxB+0x18 is null, falls back to the single-entry path; otherwise it "
        "counts active 0x1C0-stride entries rooted at ctxB+0x800.",
    ),
    0x14194D5F0: (
        "BuildCtxBSourceListFromOwnerOrFallback",
        "Central source-list builder for ctxB. If ctxB+0x18 is null, it "
        "materializes a single fallback entry at ctxB+0x1D00 by copying "
        "owner+0x8..+0x27 plus dword owner+0x28 from the pointer in ctxB+0x8. "
        "This exact layout matches the startup header copy into "
        "0x143880318 via 0x1409EB500, which strengthens the reading that the "
        "fallback owner can be the global selection service object itself. "
        "Otherwise it enumerates the active "
        "0x800/0x1C0-stride entry family and builds the output list.",
    ),
    0x14194E3B0: (
        "PublishKind70SourceSummaryIntoCtxOwnerAndRefreshPayload",
        "Pushes a selected source-summary pointer block through ctx+0x8 "
        "owner->virtual +0x80(ctx, summary_ptr, 0x70). Then refreshes the "
        "kind-0x70 payload behind either the fallback entry at ctx+0x1D00+0xFC "
        "or the currently selected active entry's +0xFC payload. Used by the "
        "split/session refresh path after it chooses a stock player-2 source, "
        "so this is source-summary publication, not a direct camera-manager "
        "handoff.",
    ),
    0x14194E510: (
        "ResolveKind70PayloadFromCtxSourceEntryOrFallback",
        "Given a ctx object and one source-entry pointer, resolves only the "
        "kind-0x70 payload. If ctx+0x18 is null, it falls back to ctx+0x1ED4 "
        "when ctx+0x1ED0 == 0x70. Otherwise it scans the 12-entry 0x1C0-stride "
        "family at ctx+0x800, matches the incoming source entry by string "
        "identity, and returns entry+0xFC when that entry's kind dword at "
        "+0xF8 is 0x70. This is the concrete record layer consumed by post-bind "
        "ctxB users and by the stock split player-2 summary path.",
    ),
    0x14194C870: (
        "GetActiveSharedContextEntryByOrdinal",
        "Returns the active 0x1C0-stride entry at ctx+0x800 + index*0x1C0 "
        "when that slot's active byte +0xE0 is set, otherwise null. Live "
        "callers use this as a stock entry-selection helper before comparing "
        "against current/fallback ctx state.",
    ),
    0x14194C980: (
        "GetCtxCurrentSelectedOrFallbackEntry",
        "Returns the current-selected 0x1C0 entry for a shared-context object. "
        "If ctx+0x18 is null it falls back to ctx+0x1D00; otherwise it scans "
        "the 12-entry family at +0x800 and returns the entry matching the "
        "current id dword at ctx+0x1EC4.",
    ),
    0x14194DB30: (
        "DispatchCtxBOwner58OrReturn2",
        "Small ctxB wrapper. If ctxB+0x8 is bound, tail-calls "
        "owner->virtual +0x58(ctxB, bool_flag); otherwise returns 2.",
    ),
    0x14194DB70: (
        "DispatchCtxBOwner60OrReturn2",
        "Small ctxB wrapper. If ctxB+0x8 is bound, tail-calls "
        "owner->virtual +0x60(ctxB, bool_flag); otherwise returns 2.",
    ),
    0x14194DB90: (
        "DispatchCtxBOwner98",
        "Thin ctxB wrapper for owner->virtual +0x98(ctxB).",
    ),
    0x14194DBB0: (
        "DispatchCtxBOwnerA0",
        "Thin ctxB wrapper for owner->virtual +0xA0(ctxB).",
    ),
    0x14194DBD0: (
        "DispatchCtxBOwnerA8",
        "Thin ctxB wrapper for owner->virtual +0xA8(ctxB, token).",
    ),
    0x14194DBF0: (
        "StoreCtxBTokenAt200ThenDispatchOwner78",
        "Writes the incoming token to ctxB+0x200, then if ctxB+0x8 is bound "
        "tail-calls owner->virtual +0x78(ctxB, token). This confirms another "
        "live owner slot above the previously anchored +0x50/+0x58/+0x70/+0x78 "
        "band.",
    ),
    0x1419739E0: (
        "BindSharedContextIntoVariantASelectionServiceAndWriteOwner8",
        "Variant-A global selection-service bind worker (vtable +0x20). Finds "
        "or allocates a free 0x8D68-stride service slot at service+0x50, "
        "stores the incoming context pointer into that slot, and then writes "
        "context+0x8 = service. Combined with 0x1409EB500 + 0x14194D5F0, this "
        "now looks like a real bind of ctxB to the service object itself, not "
        "to a separate service-entry pointer. This is the first confirmed "
        "non-null writer for the embedded shared-context owner field.",
    ),
    0x141973A90: (
        "RebuildVariantASelectionServiceRequestTable",
        "Variant-A service worker reached from the global request family "
        "(vtable +0x28). Rebuilds the request/payload store at service+0x24600 "
        "from authored/time-derived state and writes compact records into that "
        "service-local table.",
    ),
    0x141974E10: (
        "DispatchDerivedSelectionTokenThroughVariantAService28",
        "Exact variant-A implementation of the global service request slot "
        "(vtable +0x28). Accepts (service, ctx, derived_token, bool_flag), "
        "resets the ctx entry, stores the token in ctx+0x208, seeds "
        "ctx+0x1ECC=0x100, and emits a larger 0x128 request payload into the "
        "variant-A service-local table at service+0x24600.",
    ),
    0x141975870: (
        "CommitCtxSelectionPendingCandidateThroughVariantAService48",
        "Variant-A implementation of the global service commit slot "
        "(vtable +0x48). Finds the bound ctx entry, clears ctx+0x18, resets "
        "ctx active-entry state, packages ctx+0x1ED0/+0x1ED4 plus the pending "
        "candidate pointer, publishes a 0x98-byte payload into service+0x24600, "
        "and updates the service-local queue/global slot family. This is still "
        "packet/queue-heavy and does not end in a plain ctx+0x18 assignment.",
    ),
    0x141975C00: (
        "ToggleCtxSelectionStateAndEmitVariantAEntryPayload",
        "Variant-A service-side helper over a bound ctx entry. Toggles the "
        "byte at ctx+0x1ECC, and when ctx+0x18 is live emits a compact 0x13E "
        "payload into the variant-A service-local table at service+0x24600. "
        "This looks like a bool/toggle request path, not the token-carrying "
        "campaign service->+0x28 call shape.",
    ),
    0x141979A60: (
        "BindSharedContextIntoVariantBSelectionServiceAndWriteOwner8",
        "Variant-B global selection-service bind worker (vtable +0x20). Finds "
        "or allocates a free 0x9570-stride service slot at service+0x4B8, "
        "stores the incoming context pointer into that slot, and then writes "
        "context+0x8 = service. This is the sibling non-null writer for the "
        "embedded shared-context owner field.",
    ),
    0x14197AE50: (
        "CommitCtxSelectionPendingCandidateThroughVariantBService48",
        "Variant-B implementation of the global service commit slot "
        "(vtable +0x48). Finds the bound ctx entry, resets entry-local state, "
        "calls service->virtual +0x10, packages ctx+0x1ED0/+0x1ED4 plus the "
        "pending candidate pointer, writes ctx+0x18 = pending, then waits "
        "through repeated service->virtual +0x10 calls until ctx+0xB8 flips. "
        "If unresolved, stores -1 in ctx+0xBC and finally sets ctx+0xB8=1.",
    ),
    0x14197ABC0: (
        "DispatchDerivedSelectionTokenThroughVariantBService28",
        "Exact variant-B implementation of the global service request slot "
        "(vtable +0x28). Accepts (service, ctx, derived_token, bool_flag), "
        "resets the ctx entry, stores the token in ctx+0x208, clears the "
        "entry-local request state at +0x8D68, seeds the 0x2332 request lane "
        "through 0x141920B00, and drives local follow-up through ctx->virtual "
        "+0x48 plus 0x14194CA10.",
    ),
    0x14197B0C0: (
        "EmitCtxSelectionTokenThroughVariantBEntryPayloadTable",
        "Variant-B service-side helper over a bound ctx entry. If ctx+0x18 is "
        "live, packages the caller-supplied token in r8d together with the "
        "current selected object pointer into a compact 0xD1 payload at "
        "entry+0x8D28.",
    ),
    0x14194E0D0: (
        "DispatchCtxBStateWithWildcardMinus1",
        "Tiny ctxB dispatcher that re-enters the main ctxB virtual +0x10 "
        "path with r8d=-1, i.e. a wildcard/any-state request.",
    ),
    0x14194E0E0: (
        "DispatchCtxBCurrentDesiredStateThroughVirtual8",
        "CtxB helper that scans the 12-entry table rooted at ctxB+0x8E8, "
        "matches the current desired id from ctxB+0x1EC8, and then dispatches "
        "that id through the ctxB virtual +0x08 path.",
    ),
    0x14194DE00: (
        "DispatchCtxBRequestedStateAcrossEntryTable",
        "Main ctxB dispatch path for a requested state id. Walks the 12-entry "
        "0x1C0-stride table at ctxB+0x8E8, checks current-selection and entry "
        "ids, and either forwards through 0x143FE1080->virtual +0xB0 or "
        "re-enters ctxB virtual +0x10 for matching entries.",
    ),
    0x14194DEE0: (
        "RebuildCtxBEntrySelectionSetAndDispatch",
        "Heavier ctxB rebuild/select path over the same 12-entry entry table. "
        "Uses the 0x1436866C0 service plus 0x141982510 to rebuild per-entry "
        "selection state before dispatching follow-up work. Still no plain "
        "direct writer to ctxB+0x18 is visible here.",
    ),
    0x14194C550: (
        "EmitCtxBSelectionPresentEventThroughPacketBus",
        "CtxB publish path reached through ctxB virtual +0x60. When "
        "ctxB+0x718 is live, emits a packet/event through 0x142204E50. "
        "Consumes current selection state but does not write ctxB+0x18.",
    ),
    0x14194C5F0: (
        "EmitCtxBCurrentSelectionPresentEvent",
        "Sibling ctxB publish helper. Checks whether current-selection state "
        "exists and emits another packet/event through the same upper packet "
        "bus family. No direct current-selection assignment here either.",
    ),
    0x14194C690: (
        "EmitCtxBSelectionPresentEventWhenCurrentBound",
        "Another small ctxB packet publisher. If ctx+0x18 is non-null, emits "
        "a packet/event through 0x142204E50. This consumes live selection but "
        "does not assign it.",
    ),
    0x14194C730: (
        "RefreshCtxBThroughOwner50ThenSelf60",
        "Bridge between the ctxB object and its bound owner. If ctxB+0x8 is "
        "live, calls owner->virtual +0x50(ctxB), then re-enters ctxB virtual "
        "+0x60. This is downstream of the confirmed startup bind, not the "
        "first owner-writer.",
    ),
    0x1407DF190: (
        "GetCtxBCurrentSelectionIfLive",
        "Concrete ctxB vtable slot +0x20 from 0x1429EF890. Returns ctxB+0x18 "
        "only when byte +0xC8 is set, otherwise returns null. This cuts off "
        "the false bind hypothesis around upper callers that only invoke "
        "ctxB->virtual +0x20.",
    ),
    0x140B204A0: (
        "SetCtxBTokenBitAndDispatchOwner78",
        "Concrete ctxB vtable slot +0x38 from 0x1429EF890. Toggles bit 0 in "
        "ctxB+0x200 from dl, loads the bound owner from ctxB+0x8, and "
        "tail-calls owner->virtual +0x78(ctxB). This proves the ctxB object "
        "consumes an already-bound owner and does not perform the bind here.",
    ),
    0x140B204D0: (
        "IsCtxBTokenBitClear",
        "Concrete ctxB vtable slot +0x40 from 0x1429EF890. Returns the "
        "inverted low-bit state of ctxB+0x200.",
    ),
    0x140B20240: (
        "MarkCtxBSelectionPresentAndGlobalDirty",
        "Concrete ctxB vtable slot +0x80 from 0x1429EF890. If ctxB+0x18 is "
        "live, sets ctxB+0x1F80=1; always sets global dirty byte "
        "0x1437E7513=1.",
    ),
    0x140B20260: (
        "PublishCtxBModePacketAndSetActiveBit",
        "Concrete ctxB vtable slot +0x88 from 0x1429EF890. Sets ctxB+0x10=1, "
        "packages a large token-keyed payload from a retained/runtime/object-"
        "list band, and publishes it through 0x141C9ED50.",
    ),
    0x140B20760: (
        "MarkCtxBGlobalDirtyFlag7513",
        "Tiny ctxB slot that only marks the global dirty byte "
        "0x1437E7513=1.",
    ),
    0x140B20710: (
        "ClearCtxBPendingLatchAndOptionallyDispatchOwner78",
        "Concrete ctxB vtable slot +0x48 from 0x1429EF890. Clears "
        "ctxB+0x1FD0, and when the global gate at 0x1437E6F39 is set writes "
        "ctxB+0x200=1 and tail-calls owner->virtual +0x78(ctxB,1); then "
        "clears ctxB+0x10.",
    ),
    0x140B20770: (
        "ResetCtxBRuntimeFlagsAndTailRefresh",
        "CtxB runtime reset slot. Clears a small set of globals plus local "
        "ctx fields around +0x10/+0x1FD0, then tails into the broader "
        "refresh/reset helper at 0x140ACC140.",
    ),
    0x140B222B0: (
        "InitNetPlayerDataStartingValues",
        "Seeds a split/net-side NetPlayerData block from starting-value script "
        "queries such as *net-starting-gold*, *net-starting-xp*, and the "
        "*net-starting-mission-0/1/2* family. Reached from split login/logout "
        "and wider session-state refresh helpers.",
    ),
    0x140B22C00: (
        "CopySplitSubPlayerDescriptorBlock",
        "Copies the 0x24-byte split sub-player descriptor block. Uses the "
        "live global descriptor at 0x143071008..+0x28 when split state is "
        "active, otherwise falls back to a default descriptor payload. This is "
        "the stock sub-player descriptor feed used by split login and by the "
        "ctxB fallback selector; it is not itself a live local-player actor "
        "pointer path.",
    ),
    0x140B22D00: (
        "SetNetPlayerDataSlaveFlags",
        "Split-side NetPlayerData helper anchored by the assert "
        "'m_isSlave == isSlave' in NetPlayerData::Init. Validates the "
        "incoming slave-state byte at +0x9604 and stores the companion flag "
        "byte at +0x9605.",
    ),
    0x140B9DE90: (
        "HandleNetLogoutSplitScreenCommit",
        "Split logout commit/reset helper. Reinitializes the split-side "
        "NetPlayerData blocks through InitNetPlayerDataStartingValues, clears "
        "the split availability/login state, and conditionally re-runs the "
        "large split refresh at 0x140ACC140.",
    ),
    0x140ACC140: (
        "RefreshSplitScreenSummaryFromSessionAndCtxB",
        "Large split/local-player refresh helper reached from split login, "
        "split logout, and broader session-state transitions. Rebuilds summary "
        "state under 0x1437E7740, consults ctxB through 0x14194CFB0 and "
        "0x140B21DE0, then publishes kind-0x70 source summaries back through "
        "0x14194E3B0 for both ctxA and ctxB. This keeps the stock player-2 "
        "source tied to the ctxB selection family, but it still operates on "
        "descriptor/summary state rather than directly producing a live "
        "local-player-1 actor pointer or camera-manager owner.",
    ),
    0x140C021C0: (
        "QueryNetIsSecondPlayerAvailable",
        "Script command handler for net-is-second-player-available?. Reads the "
        "runtime split-availability state rather than booting split mode.",
    ),
    0x140C02220: (
        "HandleNetLoginSplitScreen",
        "Script command handler for net-login-split-screen. Gated by "
        "*net-allow-sub-sign-in*, the runtime availability byte, and the "
        "split login state; copies the split descriptor block, enables split "
        "render state, seeds NetPlayerData, and refreshes the stock "
        "player-2/session summary path. Runtime probing shows this boots split "
        "state and CameraManager(1) in campaign, but stock local-player-1 "
        "resolution can still remain null until separately patched.",
    ),
    0x140C02420: (
        "HandleNetLogoutSplitScreen",
        "Script command handler for net-logout-split-screen. Hands off to the "
        "split logout/reset helper 0x140B9DE90.",
    ),
    0x140B1FAF0: (
        "ConsumeCtxBCurrentSelectionAndEntryCounters",
        "CtxB-side refresh/consume path that checks ctx+0x18 plus counters at "
        "+0x1EC4/+0x1EC8 and 0x1436866C0-backed state before proceeding. It "
        "uses current selection but does not directly assign it.",
    ),
    0x140B1E1A0: (
        "CheckCtxSelectionPermitAgainstCtxACtxBCounts",
        "Upper permit gate that consults both shared-context families through "
        "0x14194CFB0 and returns a bool-like result for whether selection "
        "work should proceed.",
    ),
    0x140A90A00: (
        "NetGameManager_AddOrUpdateNetPlayerCinema",
        "Cinema-flavored wrapper above NetGameManager_AddOrUpdateNetPlayerTracker. "
        "The function carries the `NetGameManager::AddOrUpdateNetPlayerCinema` "
        "string, finds or creates a tracker, seeds spawn/loadout payload, "
        "stores tracker+0x1DC=incoming_r8d and tracker+0x1E0=0 for the "
        "local-player/cinema path, mirrors the incoming 0x40 payload into "
        "tracker-local bands around +0x159/+0xD9/+0x119, hashes the fixed "
        "`NetCharacter-` token to derive tracker+0x1E4, and returns the "
        "tracker pointer. Useful as a payload template, but not as a slot-1 "
        "seam because it hardcodes local slot 0.",
    ),
    0x140A58310: (
        "NetGameManager_UpdateOrCreateLateJoinerTrackerFromExternalRecord",
        "Higher late-joiner bridge that consumes an external player-like "
        "record. Reuses an existing tracker by matching record+0x18/+0x1C "
        "against tracker+0x1DC/+0x1E0, otherwise builds through the "
        "`late-joiner-%d` token family and falls back to "
        "NetGameManager_AddOrUpdateNetPlayerCinema or "
        "NetGameManager_AddOrUpdateNetPlayerTracker, then copies the wider "
        "state block record+0x20..0x3C into tracker fields +0x86C..+0x884. "
        "Retail disassembly now pins additional external-record fields: a "
        "19-entry WORD array at +0x40, paired signed-byte array at +0x66, "
        "and later state at +0x7C/+0x80/+0x84/+0x88/+0x89/+0x8C/+0x90/+0x91/"
        "+0xB4/+0xC0/+0xC8.",
    ),
    0x140A58190: (
        "NetGameManager_CreateOrReuseLateJoinerTrackerFromCompactRecord",
        "Compact late-joiner bridge above the tracker layer. Builds the local "
        "packet through `late-joiner-%d`, then either routes into "
        "NetGameManager_AddOrUpdateNetPlayerCinema or resolves a source object "
        "and falls back to NetGameManager_AddOrUpdateNetPlayerTracker.",
    ),
    0x140A4DDA0: (
        "NetGameManager_CreateCinemaTrackerFromSingleIdRecord",
        "Thin cinema-only wrapper above NetGameManager_AddOrUpdateNetPlayerCinema. "
        "Requires the same global gate, only consumes record+0x18 as the id, "
        "builds the local 0x40 packet, calls the cinema wrapper, and then "
        "stamps the usual tracker flags.",
    ),
    0x140A4E7DC: (
        "NetGameManager_CreateCinemaTrackersFromIdArray10",
        "Bulk thin wrapper above NetGameManager_AddOrUpdateNetPlayerCinema. "
        "Iterates a compact 10-entry id array, builds the same local 0x40 "
        "packet through the `cinema-player-%d` token family for each entry, "
        "calls the cinema wrapper, and stamps the same tracker flags.",
    ),
    0x140B346B0: (
        "NetPlayerTracker_StartOrQueueSpawnPlayer",
        "Thin tracker-side gate above NetPlayerTracker_SpawnPlayer. Clears "
        "pending bits at +0xA7C/+0x814, checks tracker readiness through "
        "virtual +0x18 and current live-entity state, and then either "
        "tail-calls NetPlayerTracker_SpawnPlayer or queues deferred spawn "
        "state in the tracker fields around +0xA00/+0xA04/+0x4C0/+0x4C8.",
    ),
    0x140B31F60: (
        "NetPlayerTracker_SpawnPlayer",
        "Heavy net-player spawn routine. Resolves the look id, derives online "
        "starting-weapon records, checks startup placement modes such as "
        "`start-on-edge` / `start-in-cover` / `start-in-swim`, and then calls "
        "PromoteRetainedPlayerSlot to materialize the real Player object for "
        "the requested tracker slot. The retained slot comes from tracker+0x1E0, "
        "later upper authored/runtime production consumes tracker+0x1E4, and "
        "the body snapshots tracker+0x840/+0x844 into +0x848/+0x84C before "
        "continuing. The returned Player object is then used directly through "
        "fields such as +0x1D88, which makes this one of the strongest built-in "
        "second-player bringup paths found so far.",
    ),
    0x140B44EC0: (
        "BuildNetPlayerTrackerRespawnTransformBlock",
        "Builds the staged respawn transform block later stored into "
        "tracker+0x500..+0x540. Prefers a valid live actor pair from "
        "tracker+0x8/+0x10 and copies live_actor+0x100/+0x120/+0x140; "
        "otherwise falls back through tracker+0x20, which on this path is "
        "just the 16-byte reference-position carrier later consumed by "
        "0x141685C30 rather than a compact actor-pair record. Static recovery "
        "now also shows this fallback still funnels into the same crashing "
        "builder contract, so it does not bypass the remaining lifecycle gap.",
    ),
    0x140B45030: (
        "InitializeCompactActorPairRecord",
        "Zero-initializes the small actor-pair record used by the upper "
        "respawn and actor-selection helpers. Layout is dword at +0x0, "
        "qword at +0x8, dword at +0x10.",
    ),
    0x140B45040: (
        "BuildCompactActorPairRecordFromCurrentContext",
        "Builds the compact actor-pair record from current context. Either "
        "publishes a resolved actor key through the manager lookup path or "
        "falls back to a direct live object pair (object+0xA8 / object+0xBC).",
    ),
    0x140B45100: (
        "BuildCompactActorPairRecordFromSourcePair",
        "Normalizes an incoming compact actor-pair/source pair into the same "
        "record shape used by respawn and actor-selection helpers. Prefers "
        "the manager lookup/key path and falls back to direct object+0xA8 / "
        "object+0xBC storage when needed.",
    ),
    0x140B451B0: (
        "AreCompactActorPairRecordsEquivalent",
        "Compares two compact actor-pair records. Accepts either matching "
        "dword keys or matching live object pair identity.",
    ),
    0x140B411F0: (
        "ResolveNetPlayerPostSpawnSelectorFromPlayer854",
        "Returns the integer selector used after promotion from player+0x854, "
        "falling back to a hashed default if the field is zero.",
    ),
    0x140B412D0: (
        "ResolveNetPlayerPostSpawnSelectorFromPlayer858",
        "Returns the integer selector used after promotion from player+0x858, "
        "falling back to a hashed default if the field is zero.",
    ),
    0x140B347C0: (
        "NetPlayerTracker_NetRespawn",
        "Tracker-side respawn entry identified by the `NetPlayerTracker::NetRespawn` "
        "string xref. With r8b=1 it copies the caller-supplied 0x50 respawn "
        "transform block from r9 straight into tracker+0x500..+0x540 and "
        "bypasses BuildNetPlayerTrackerRespawnTransformBlock. With r8b=0 it "
        "falls back to the older tracker+0x8/+0x10 or tracker+0x20 source "
        "paths. This makes it a cleaner first local patch seam than the "
        "manager wrapper above it.",
    ),
    0x141685BC0: (
        "BuildRespawnTransformBlockFromSeedRecord",
        "Builds the 0x50 transform/orientation block from a compact seed "
        "record and then resolves authored transform overrides through "
        "0x1416860A0.",
    ),
    0x141685C30: (
        "BuildRespawnTransformBlockFromReferenceTransformAndSeedRecord",
        "Builds the 0x50 respawn transform block from a reference transform "
        "plus a compact seed record. This is the helper used by live callers "
        "that later pass r8b=1 into NetPlayerTracker_NetRespawn. The live "
        "tracker-side caller uses tracker+0x20 as the reference transform "
        "carrier and a stack seed shaped as: dword(+0), qword(+4), dword(+0xC), "
        "vec4(+0x10), vec4(+0x20). Raw disassembly shows the helper only "
        "consumes the first 16 bytes at rdx as reference position. Runtime "
        "testing has already ruled out three naive sources (`actor+0x100`, "
        "inline slot+0x20, and global source0+0x20`), so the remaining "
        "unknown is the caller-side lifecycle that prepares a valid source "
        "band before this helper becomes safe.",
    ),
    0x140BDB2E0: (
        "NetEventRespawnPlayer_Fill",
        "Builds the concrete respawn event payload from a tracker plus optional "
        "BoundFrame and boolean flag. Serializes tracker identity into "
        "event+0x18, has-transform into event+0x1C, optional transform data "
        "into event+0x20..+0x60, and the final byte flag into event+0xC8.",
    ),
    0x140BDB440: (
        "NetEventRespawnPlayer_HostFill",
        "Host-side fill/validation pass for NetEventRespawnPlayer. Derives "
        "tracker-side spawn mode/timing fields into event offsets "
        "+0xB0/+0xB8/+0xBC/+0xC0/+0xC4.",
    ),
    0x140BDB620: (
        "SendNetEventRespawnPlayer",
        "Send/broadcast helper for NetEventRespawnPlayer. Allocates the event, "
        "calls the fill helpers, and packages the message for the net-event "
        "transport layer.",
    ),
    0x140BDBF70: (
        "SendNetEventSignalRespawnPlayer",
        "Sends the lighter respawn-signal event with tracker pointer, boolean, "
        "and timeframe payload, using the same net-event transport family as "
        "SendNetEventRespawnPlayer.",
    ),
    0x140B6B970: (
        "HandleNetEventRespawnPlayer_ResolveTrackerAndCallRespawn",
        "Upper net-event style handler that resolves tracker/event ids, "
        "walks the NetGameManager tracker array, and is the only direct "
        ".text caller of NetGameManager_RespawnPlayer. Useful as a conceptual "
        "bridge above the tracker layer, but still less direct than the "
        "manager-side AddOrUpdate/Respawn entrypoints for a local runtime patch.",
    ),
    0x140BBFD70: (
        "NetEventCharacterMove_Execute",
        "Virtual +0x18 of NetEventCharacterMove. Resolves the target tracker "
        "and Player, converts event animation bytes into a NetSnapshot, then "
        "publishes that snapshot to the Player. The earlier tracker lifecycle "
        "label was incorrect.",
    ),
    0x141E994C0: (
        "Character_Init",
        "STATIC exact Character::Init source character.cpp:261/267/273. Shared "
        "Character initialization, not Player-only upper handoff. Invokes "
        "NdGameObject_Init141BB4280 at141E9959F, owner virtual+788 at141E995DA "
        "(empty141E9C0D0 on exact Pirate), initializes health owner+4A8 and "
        "other shared state. Player base140CBFF50 is only one caller. Does not "
        "establish optional body+698 creation/readiness.",
    ),
    0x1410C6F00: (
        "ConstructPlayer_Size49E0OrInitializeExistingStorage",
        "Alloc-ctor wrapper for the RTTI-backed Player class. If rdx is non-null, "
        "jumps directly into 0x1410C8FD0; otherwise allocates 0x49E0 bytes and "
        "then calls 0x1410C8FD0.",
    ),
    0x1410C8FD0: (
        "InitializePlayerCtorAndStampPlayerVftable",
        "Real in-place Player constructor. Calls base ctor at 0x140CBFB30, "
        "stamps the RTTI-backed Player vtable 0x142A4BFE8, and zero-initializes "
        "the broad Player layout including the upper owner bands and mounted "
        "subsystem slots.",
    ),
    0x1410C6F60: (
        "ConstructSiblingPlayerHelperForGlobalSlotBF90",
        "Allocates a 0x20-byte helper object, stamps vtable 0x142A0A678, and "
        "stores the incoming owner/context pointer at +0x10. This helper is "
        "registered by 0x1400AA940 into the global slot family at "
        "0x143EEBF90/0x143EEBFA0.",
    ),
    0x1410C6FC0: (
        "AttachGlobalPlayerHelperBF90ToOwnerIfSupported",
        "Checks the owner-side object at rcx+0xC0, asks it about the global "
        "slot family rooted at 0x143EEBF90 through virtual +0x198, and on "
        "success stores that global slot pointer back into the owner's +0xC0 "
        "subobject while setting local flags at +0x3D/+0x3E. This makes "
        "player+0xC0 look like a live Player-side helper/state registry, not "
        "dead ctor-only storage.",
    ),
    0x1410CF370: (
        "Player_InitializeDrawControl_FromProcessSpawnInfoAndResolvedLook",
        "Upper Player init stage identified by the embedded "
        "Player::InitializeDrawControl(const ProcessSpawnInfo &, const "
        "ResolvedLook &) string. Real Player vtable slot +0x718. Logs through "
        "outer_player+0x3C20, binds the incoming extended wrapper packet "
        "(0x1417C35A0 / vtable 0x1428D5D60) plus the draw-control context "
        "through 0x141BBACC0, wires player+0x1B10 to that same context, "
        "applies companion flag masks 0x200 and 0x800, resets local "
        "draw-control bands at +0x3B0/+0x3B8/+0x3C0, and returns the incoming "
        "ProcessSpawnInfo-like state pointer. This is now the clearest upper "
        "Player-side consumer of the wrapper family, but it behaves as a "
        "downstream draw-control/behavior bind stage rather than the place "
        "where the real Player authored identity is created.",
    ),
    0x1410CF470: (
        "Player_InitializeCompositeBody",
        "Upper Player init stage identified by the embedded "
        "Player::InitializeCompositeBody(void) string. Called directly from "
        "0x1410DC160, builds additional mounted runtime owners, and continues "
        "the upper Player bringup after the early draw/control path.",
    ),
    0x141E584D0: (
        "CompositeBody_SetCollisionFilter",
        "Iterates the manager's 0x110-byte collider entries at manager+0x48, "
        "writes the requested collision-filter value to entry+0xC8, and "
        "publishes the low six filter bits to the attached rigid body or queues "
        "a physics refresh through 0x141CF6ED0. Player_InitializeCompositeBody "
        "calls this with value 4 after tagging every entry as player. The nearby "
        "entry+0xC4 byte is not this filter field.",
    ),
    0x140A243C0: (
        "NetPlayer_InitializeCompositeBodyWithCollisionSettings",
        "Resolves net-player-collision-settings or its ally variant, builds the "
        "Player composite body, tags each 0x110-byte collider entry at +0x78 as "
        "netcharacter, sets collision layer 5 through "
        "CompositeBody_SetCollisionFilter, then switches the body state to 2.",
    ),
    0x141EC95E0: (
        "Physics_InitializeCollisionFilters",
        "Constructs the 64-layer HavokGamelibFilter matrix and wraps it in "
        "HavokPairCollisionFilter before installing it on the physics world. "
        "Its layer-mask calls explicitly disable character pairs 4<->4, "
        "4<->5, and 5<->5.",
    ),
    0x1421F86B0: (
        "HavokGroupFilter_DisableLayerPairs",
        "Symmetrically clears collision permissions for every layer selected "
        "by mask A against every layer selected by mask B in the 64-row matrix "
        "at filter+0x48.",
    ),
    0x140CE7ED0: (
        "PlayerBumpController_Update",
        "Campaign Player-bump update. Builds a Crowd::ProbeResult through "
        "PlayerBumpController_BuildCrowdProbeResult, tries to start an authored "
        "bump action through PlayerBumpController_TryStartBumpAction, and falls "
        "back to PlayerBumpController_UpdateCrowdAdjustment when no action starts.",
    ),
    0x140CE1E20: (
        "PlayerBumpController_BuildCrowdProbeResult",
        "Builds the Player-facing Crowd::ProbeResult from Player transform and "
        "movement through 0x1406DD350, then updates the bump probe state. This "
        "path is separate from Havok rigid-body contact filtering.",
    ),
    0x140CE9530: (
        "PlayerBumpController_UpdateCrowdAdjustment",
        "Fallback Crowd::ProbeResult consumer used when no authored bump action "
        "starts. It adjusts crowd movement and handles no-adjust-to-player; the "
        "hero-bump action names are built at 0x140CE2520, not here.",
    ),
    0x1406DD350: (
        "Crowd_UpdateProbe",
        "Crowd::UpdateProbe from crowd-util.cpp. Maintains a 64-byte ProbeResult, "
        "accepts an existing non-self candidate by actor capability or SimpleNpc "
        "selector, and otherwise performs the spatial collision query.",
    ),
    0x140CE2520: (
        "PlayerBumpController_BuildBumpActionCandidate",
        "Builds the 128-byte Player bump-action candidate, including directional "
        "hero-bump-%s%s%s-a/b/c hashes and movement/contact eligibility flags. "
        "SimpleNpc is optional target-specific data, not a requirement for Player.",
    ),
    0x140CE8E10: (
        "PlayerBumpController_TryStartBumpAction",
        "Consumes Crowd::ProbeResult, calls the bump-action candidate builder, and "
        "starts PlayerBumpAction through 0x140CE39E0 when the candidate flags allow it. "
        "Returns false to permit crowd movement adjustment.",
    ),
    0x140CDCC70: (
        "PlayerBumpController_IsContactEligible",
        "Checks ProbeResult distance, contact geometry/relative movement through "
        "0x140CDC900, bump cooldown, and optional SimpleNpc velocity before allowing "
        "an authored bump action.",
    ),
    0x141E36910: (
        "SimpleGridHashManager_RegisterObject",
        "Registers an NdGameObject in the shared simple-grid hash, stores its live "
        "handle, grid cell, velocity and radii, and returns a SimpleGridHashId.",
    ),
    0x141E37080: (
        "SimpleGridHashManager_UpdateGridHash",
        "Validated by its embedded source signature. Verifies that a supplied "
        "SimpleGridHashId still belongs to the same NdGameObject and updates its "
        "grid cell, transform, velocity and radii.",
    ),
    0x140CC1450: (
        "ApplyProcessSpawnInfoStateIntoTopLevelPlayerOwner",
        "Top-level Player apply/finalize path reached from 0x1410DE740. "
        "Validates a lookup record against owner ids at +0x108/+0x10C, using "
        "player+0x164 as the runtime-state slot kind and optionally forcing "
        "kind 0x80 when global 0x143E07473 is set and player+0x161A bit0 is "
        "set. After copying accepted state back into outer_player+0x110 it "
        "builds a transform block from +0x110/+0x120/+0x130/+0x140 and "
        "commits through 0x1418C4840. That callee immediately consumes "
        "external_state+0x40/+0x48/+0x58 and seeds them into the runtime "
        "slot, which makes the packet shape concrete. The path then "
        "conditionally refreshes a `proto`-tagged upper state into "
        "player+0x1A0 through 0x1417CD200/0x1417CD3D0 and finally sets "
        "player+0x2D1=1. The same +0x108/+0x10C pair is later re-resolved by "
        "multiple upper Player helper families through the shared table "
        "0x14386FD00, so this is a stable upper authored-record key rather "
        "than a one-shot apply detail. The consumed external_state is now "
        "best read as a real ProcessSpawnInfo-derived packet family seeded "
        "earlier by 0x1410F14C0; the closest currently pinned shape match is "
        "the extended wrapper family at 0x1417C35A0 / vtable 0x1428D5D60, "
        "not the returned materialized object from 0x1417C3790.",
    ),
    0x140CC18D0: (
        "SeedPlayer15B4FromIncomingContext58E0",
        "Real Player vtable slot +0x738. If the incoming context pointer is "
        "non-null, copies context+0x58E0 into player+0x15B4; otherwise clears "
        "player+0x15B4. This is a narrow upper-context seed helper used from "
        "0x1410FE840 after the typed context block is built, not a broad apply "
        "or promotion hook.",
    ),
    0x1410DE890: (
        "GetPlayerCollisionSettingsRecord",
        "Player vtable slot +0x850. Lazily resolves the authored "
        "`player-collision-settings*` record into global 0x1437423D0 and "
        "returns it. The earlier upper-rebuild/player+0x3828 interpretation "
        "was incorrect and must not be used as a lifecycle call.",
    ),
    0x141BBA490: (
        "BuildUpperPlayerStateOwnerAt1A0ThenRunPlayerSlot710",
        "Real Player vtable slot +0x700. Allocates a 0x2B0 upper state owner "
        "through 0x1415A0E90 -> 0x141728690, stores it into player+0x1A0, "
        "links player+0x1A8->+0x10 back to that owner, optionally queries "
        "Player slot +0x708 for a small classifier/count, then feeds "
        "player+0x1A0, player+0x1A8, player+0x160, and the +0x708 result into "
        "0x141728720 before immediately calling Player slot +0x710. This "
        "makes +0x700 the entry point of a distinct upper state-build/publish "
        "band over player+0x1A0/+0x1A8. It is now clearly neighboring the "
        "upper +0x6F0/+0x718 lifecycle, but not yet proven to sit inside the "
        "same +0x850 rebuild ordering.",
    ),
    0x141BBA2A0: (
        "ApplyUpperActiveStatePayloadIntoPlayer1A8AndSetSelectorGateFlag",
        "Real Player vtable slot +0x6F8. Consumes an incoming upper payload "
        "record, copies vector/scalar fields into the active upper state at "
        "player+0x1A8 (+0x150..+0x194), derives a delta vector, and checks a "
        "selector/capability through player+0x30 via 0x1417C4800. On success "
        "it raises the byte flag at the caller-provided helper offset "
        "+0x2D0. This makes +0x6F8 a concrete upper active-state payload "
        "applicator sitting after the +0x718 wrapper bind in the sibling "
        "upper lifecycle block.",
    ),
    0x1410CE260: (
        "PublishUpperPlayerOverlayReadJointAndReferenceState",
        "Real Player vtable slot +0x710. Consumes the upper state owner at "
        "player+0x1A0 together with the active upper state at player+0x1A8 "
        "and publishes multiple authored upper Player families: "
        "`anim-net-player`, `*player-overlay-priorities*`, "
        "`*player-default-overlays-count*`, `*player-default-overlays*`, "
        "`apReference`, `align`, `run-chloe`, and `base-read-joints-layer`. "
        "Early in the path it resolves knowledge/state ids through "
        "0x1417651D0/0x141765030 and publishes them through the wrapper at "
        "[upper_owner+0x60] via 0x1417F9150. The dereferenced master overlay "
        "state is then installed at +0x18 in each constructed AnimCmdGenerator, "
        "AnimSnapshot and AnimPose layer. Later it builds the owner at "
        "player+0x4260, seeds typed subrecords through 0x141728D90/0x14172A720/"
        "0x14172ACD0, and only after those upper overlay/reference layers are "
        "prepared does control continue into the animation-snapshot stage at "
        "player+0x3FD8. This is therefore a real upper Player state-build/"
        "publish phase, not a side helper. Direct vcall scans currently show "
        "it only being entered from Player slot +0x700.",
    ),
    0x141308680: (
        "UpperPlayerBlock_Drive700And708ThenEnterMounted1E98VerbPath",
        "Upper block that invokes Player slot +0x700, immediately invokes "
        "Player slot +0x708, then continues into the mounted Player subsystem "
        "at player+0x1E98 through virtual +0x190. This makes the upper "
        "+0x700/+0x708 band a live feeder into a concrete mounted Player verb "
        "owner rather than an isolated helper path.",
    ),
    0x141728690: (
        "InitializeUpperPlayerStateOwner2B0",
        "In-place initializer for the 0x2B0 upper state owner allocated by "
        "Player slot +0x700. Zeroes the main bands, allocates a 0x200 scratch/"
        "entry area at +0x78, and clears the late fields through +0x2AC. The "
        "result is the owner later stored into player+0x1A0.",
    ),
    0x141728720: (
        "PopulateUpperPlayerStateOwnerFromActiveStateAndClassifier",
        "Populates the 0x2B0 upper state owner built by 0x141728690. Mirrors "
        "an optional retained/live pair into +0x38, stores the active upper "
        "state object at +0x20, copies classifier/count fields from the "
        "incoming active-state descriptor into +0x214/+0x218, synchronizes a "
        "live runtime object through 0x141780990, forwards a compact state "
        "summary through 0x1418BBEB0, and allocates the per-entry table at "
        "+0x30 using the classifier/count passed from Player slot +0x708. "
        "This is the concrete build step that prepares player+0x1A0 before "
        "Player slot +0x710 publishes overlay/read-joint/reference state.",
    ),
    0x141BB4AA0: (
        "UpperPlayerLifecycleBlock_738_6F0_700_718",
        "Sibling upper block that explicitly chains Player slots "
        "+0x738, +0x6F0, +0x700, and +0x718 in one continuous path. It caches "
        "the compact state from +0x738 into player+0x170 (and may seed "
        "player+0x190), resolves an authored record through 0x1417CD080, "
        "stores the +0x6F0 result into player+0x1A8, then runs the upper "
        "state-owner band through +0x700 before continuing into +0x718. This "
        "is currently the strongest concrete upper lifecycle anchor above the "
        "mounted Player graph that includes both the direct apply seam and the "
        "new +0x700 state-owner band.",
    ),
    0x141BB3330: (
        "NdGameObject_Construct",
        "Exact NdGameObject base constructor. Installs the NdGameObject "
        "vtable and initializes DrawControl pointer object+0x198 and "
        "FgAnimData pointer object+0x1A8 to null before the derived class "
        "constructor and NdGameObject_Init establish their independent "
        "render ownership chains.",
    ),
    0x141BB4280: (
        "NdGameObject_Init",
        "Exact NdGameObject::Init source path shared by drawable game objects. "
        "Calls the concrete object's vtable slot +0x6F0 to allocate FgAnimData, "
        "stores it at object+0x1A8, constructs IDrawControl at object+0x198, "
        "and then runs the derived initialization/finalization slots. Player "
        "uses the extended upper-state and ambient-occluder branches in this "
        "generic lifecycle. STATIC look selection: call 0x141BB4AB2 through "
        "slot+2E0 returns 0x141BB4AB9, the exact legacy-Nadine override gate. "
        "Only afterward 0x141BB4ACC/4ACF tests ProcessSpawnInfo+98. A null "
        "prebuilt look uses the selected hash through slot+238 at "
        "0x141BB4AE3 and publishes object+170 at 0x141BB4B03; a prebuilt "
        "look bypasses that hash-fed construction, not the getter callback. "
        "Installing a getter hook after Init cannot retroactively rebuild it. "
        "STATIC identity: full SpawnInfo name hash141BB48A4, explicit+64 or "
        "spawner desired id ->object+D8 via1416A6B20 at141BB4A04; SpawnInfo+68 "
        "->object+DC at141BB4A0E. Native network path140A96B00 may replace+D8.",
    ),
    0x141BCD340: (
        "ResolvePlayerAmbientOccluderSelectionAndRefreshDrawControlMeshes",
        "Real Player vtable slot +0x740. Resolves authored ambient-occluder "
        "ids around `character-ambient-occluder-array`, `ambient-occluders`, "
        "and `*characters-ambient-occluders-map*`, writes the selected id to "
        "player+0x180, optionally refreshes the draw-control object at "
        "player+0x198 via 0x1417567C0, then scans draw-control meshes and "
        "updates ambient-occluder usage through 0x141756570/0x141755B10. "
        "This pins +0x740 as a draw-control/ambient-classification stage, "
        "not an ownership or verb-dispatch seam.",
    ),
    0x140CC4BF0: (
        "PlayerBase_SpawnHatWithIdFromMap",
        "Exact source assertion names PlayerBase::SpawnHatWithIdFromMap. It "
        "resolves the selected authored look, creates tracked prop processes "
        "in Player+0x1758 (16-byte entries), adds resolved meshes to the "
        "Player+0x198 DrawControl, records their ids in Player+0x1858, and "
        "materializes optional look VFX. This is the per-hat worker called by "
        "0x140CC5890.",
    ),
    0x140CC5890: (
        "PlayerBase_RebuildHatAttachmentsFromIds",
        "Rebuilds Player hat/hair attachments from Player+0x15AC/+0x15B0. "
        "It kills up to 16 old tracked processes at Player+0x1758, removes "
        "their mesh ids at Player+0x1858 from the parent Player+0x198 "
        "DrawControl, clears both tables, resolves both authored hat ids, and "
        "calls PlayerBase_SpawnHatWithIdFromMap for the selected entries. "
        "Callers include Player initialization, ChangeLookId, and direct "
        "hat-id setters.",
    ),
    0x1410FE840: (
        "Player_ChangeLookId",
        "Exact source assertion names Player::ChangeLookId(StringId). It tears "
        "down and reconstructs the Player DrawControl/composite body, builds a "
        "ProcessSpawnInfo, then reinitializes the owner's prop/process control. "
        "The stock PreFrame loop at 0x14015CE1C consumes Player+0x3D10 to call "
        "this function outside object Update.",
    ),
    0x141E61010: (
        "NdPropControl_Init",
        "Exact source assertion names NdPropControl::Init. It resolves look2 and "
        "prop records from ProcessSpawnInfo/owner state and builds the pending "
        "native prop-process list through NdPropControl_BuildPropSpawnList.",
    ),
    0x141E61860: (
        "NdPropControl_BuildPropSpawnList",
        "Exact source assertion names NdPropControl::BuildPropSpawnList. It maps "
        "resolved prop ids to authored definitions and appends pending spawn "
        "records consumed by the follow-up materialization pass.",
    ),
    0x141E63D70: (
        "NdPropControl_SpawnPropFromDefinition",
        "Exact source assertion names NdPropControl::SpawnPropFromDefinition. It "
        "builds native spawn state from an authored prop definition, invokes the "
        "owner's virtual process creator, and attaches the spawned process.",
    ),
    0x1401186C0: (
        "RegisterProcessCharClothFactory",
        "Registers the exact ProcessCharCloth type string with the generic "
        "process registry and creator ProcessCharCloth_Factory at 0x141F41470.",
    ),
    0x141F3DEE0: (
        "ProcessCharCloth_Constructor",
        "RTTI-backed constructor: installs ??_7ProcessCharCloth@@6B@ and "
        "initializes the class-specific 0x880..0x960 tail.",
    ),
    0x141F3E060: (
        "ProcessCharCloth_Init",
        "Exact source assertions name ProcessCharCloth::Init. It consumes the "
        "spawn descriptor, resolves the attach joint and parent handle at "
        "+0x4E0/+0x4E8, builds cloth render/physics state, and marks +0x870 "
        "initialized.",
    ),
    0x141F3E6E0: (
        "ProcessCharCloth_UpdateRoot",
        "Exact source assertions name ProcessCharCloth::UpdateRoot. It resolves "
        "the parent Player handle and updates cloth root transforms/wind data.",
    ),
    0x141F3F7A0: (
        "ProcessCharCloth_EventHandler",
        "Exact source assertions name ProcessCharCloth::EventHandler; it applies "
        "cloth simulation and visibility/event parameters to the owned bodies.",
    ),
    0x141F41470: (
        "ProcessCharCloth_Factory",
        "Factory registered for ProcessCharCloth: allocates 0x980 bytes and "
        "runs ProcessCharCloth_Constructor, or constructs into supplied storage.",
    ),
    0x141308220: (
        "UpperPlayerVerbStage_EdgeAimReload_And_IdleReach0",
        "Containing upper Player verb stage around the block at 0x141308680. "
        "Hashes authored tokens such as `edge-aim-reload`, "
        "`idle-reach0-stamina`, and `idle-reach0`, gates through the Player "
        "qualification owner at +0x1D88, reuses the owner-local registry at "
        "+0x3BD0, then drives Player slots +0x700/+0x708 and continues into "
        "the mounted Player subsystem at +0x1E98. This is strong evidence "
        "that the +0x700 state-owner band is reused by concrete upper verbs, "
        "but the stage itself is still downstream of the real promotion/apply "
        "surface.",
    ),
    0x140CC17A0: (
        "ResolveEffectivePlayerStateKindIntoOutDword",
        "Real Player vtable slot +0x4A8. Writes an effective upper state-kind "
        "dword to the caller-provided output. In the normal case it returns "
        "player+0x174; under the global gate at 0x143E07473 it instead hashes "
        "the `Player` token and writes that override. This slot is used in the "
        "same sibling upper lifecycle block after the +0x6F8 payload "
        "application path.",
    ),
    0x1401D48D0: (
        "ReturnTrue_PlayerUpperPermit768",
        "Real Player vtable slot +0x768. Trivial `return true` permit gate "
        "used after the +0x6F8 path in the sibling upper lifecycle block.",
    ),
    0x1401D48E0: (
        "ReturnTrue_PlayerUpperPermit760",
        "Real Player vtable slot +0x760. Trivial `return true` permit gate "
        "queried by Player slot +0x700 before the +0x708 classifier stage.",
    ),
    0x14102F6B0: (
        "LookupHashedEntryInPlayerOwner3BD0Registry",
        "Consumer of the owner-local object at outer_player+0x3BD0. Uses "
        "object+0xB0 as bucket count, object+0x20 as bucket table, and linked "
        "entries with hashed ids at entry+0x10, which pins +0x3BD0 as a real "
        "hashed owner-local runtime registry rather than a passive state blob.",
    ),
    0x141728AF0: (
        "AnimControl_GetJointCache_Assert178",
        "STATIC exact anim-control.cpp:178 AnimControl::GetJointCache "
        "assertion. Requires control+20 FgAnimData and returns that pointer "
        "+30 at141728B4D. Independent consumer tying owner+1A0 AnimControl "
        "to the Fg+30 cache; caller140624591 is LegModifier's lazy read path. "
        "Do not call this asserting getter merely to observe memory.",
    ),
    0x14177F660: (
        "AnimMgr_InitializeFgPoolAndDefaultPlugins",
        "STATIC descriptive name: allocates1536*0x380 FgAnimData bytes, "
        "initializes slots and publishes pool1466D0200. Installs default "
        "Nd phase/postpose callbacks at1466D0C50/58 (14177F76E/77C), clears "
        "validation slot1466D0C60. A later native initialization can replace "
        "an observer callback; no automatic reinstall is admitted.",
    ),
    0x14177F7D0: (
        "AnimMgr_SetGamePostPosePluginCallback",
        "STATIC tiny setter stores Game_ProcessAnimPostPosePluginFunc "
        "1406CD220 into1466D0C58 at14177F7D7, returning that pointer. "
        "No current caller xrefs; do not invent an active reset frequency. "
        "Native game initialization independently writes the same slot at "
        "140157E84. Existing jobs copy the callback, so restoring this "
        "global alone cannot prove queued-wrapper references drained.",
    ),
    0x14177F7F0: (
        "AnimMgr_AllocateAnimData",
        "Exact AnimMgr::AllocateAnimData(unsigned int, Process*) source "
        "identity. Claims a 0x380-byte FgAnimData slot from the global pool "
        "at 0x1466D0200, copies Process+0xA8/+0xBC into the retained pair at "
        "+0x1A8/+0x1B0, and initializes the driven-instance capacity/count/"
        "index array at +0x208/+0x20C/+0x210. Pair loads 0x14177F94E/95A "
        "and store 0x14177F975 match the slot-table spawn writers bit-for-bit; "
        "this permits reject-only Fg-versus-slot identity comparison without "
        "resolving a possibly unrelated retained cell.",
    ),
    0x14177FA70: (
        "AnimMgr_FreeAnimData",
        "AnimMgr::FreeAnimData pool counterpart immediately following the "
        "exact allocator. Derives the 0x380-byte slot index, clears every "
        "AnimMgr category/allocated bit and returns the slot to the free mask; "
        "it does not inspect or release FgAnimData+0x210 driven instances.",
    ),
    0x14177FEB0: (
        "AnimMgr_QueueResidualJointBlendGroups",
        "Special multi-group tail reached from PerformAnimBlend141A927D9 "
        "and JointBlend141A9336B. Builds the96-byte per-Fg deferred-stage "
        "record. STATIC correction: +0x24 stores bits NOT selected in the "
        "incoming group mask over Art+0x30 groups (14177FFA5/FFA7), not the "
        "incoming residual mask itself. Sets state2 at+0x58 and publishes "
        "the Fg slot through AnimMgr_PublishResidualJointBlendJobReady; "
        "this function does not execute those groups.",
    ),
    0x141780100: (
        "AnimMgr_PublishResidualJointBlendJobReady",
        "Derives the FgAnimData pool index and, under lock1466D0C30, ORs its "
        "bit at141780194 into the ready bitset obtained from the current "
        "runtime context (14160C120 result+0x10828, then structure+0x8). "
        "STATIC correction: this is not a direct store into the normal "
        "global AnimMgr category-ready masks. The exact downstream consumer "
        "has not been re-established in this bounded audit; do not infer "
        "JointBlend completion from this publication boundary.",
    ),
    0x141782B30: (
        "AnimMgr_DispatchAsyncObjectUpdateJointBlendJob",
        "SyncUpdate callback thunk for the residual joint-blend category. "
        "Advances the 56-byte work-item cursor and tail-jumps to "
        "AnimManager_AsyncObjectUpdateJointBlendJob 0x141A92A00. Exact "
        "tail JMP at 0x141782B3A is E9 C1 FE 30 00; changing its target "
        "preserves cursor advancement and dispatcher return address. "
        "Independent ingress from queued callback LEA 0x1417815B7. AFE source-only "
        "observer redirects these two producers without installing the three "
        "pose-influencing Process/cmd16 hooks; writer flags remainfalse.",
    ),
    0x141783380: (
        "AnimMgr_VisitUnblockedReadyObjects",
        "Iterates set bits from arg2's 64-qword ready mask, skips blocked "
        "bits in 0x1466CE600, and limits pool indices to below 0x600. "
        "At 0x14178348A invokes callback arg3 with RCX=pool+index*0x380 "
        "and RDX=arg4. SyncUpdate resets its 0x38-job cursor and dispatches "
        "residual JointBlend thunk 0x141782B30 through 0x14178327D.",
    ),
    0x141782B40: (
        "AnimMgr_SyncUpdate",
        "Exact AnimMgr::SyncUpdate(unsigned int,bool) source identity. Consumes "
        "the category ready masks and at 0x14178327D dispatches the residual "
        "joint-blend callback 0x141782B30. That thunk tail-calls141A92A00 "
        "SYNCHRONOUSLY here, not a separate job submission. Its independently "
        "named joint-blend callback contains Process callsites141A92E78/141A932F8. "
        "AsyncUpdate waits normal spawner counters separately at1417829B9/C6/D4.",
    ),
    0x14181A4B0: (
        "FgDrawMgr_AttachInstanceToAnimData",
        "Exact FgDrawMgr::AttachInstanceToAnimData source assertion. Appends "
        "an existing global FgInstance pool index to FgAnimData+0x210 and "
        "increments +0x20C without allocating or changing instance state.",
    ),
    0x1418C4840: (
        "FgAnimData_Init",
        "Exact FgAnimData::Init(ArtItemSkeleton*, Transform&, "
        "JointCache::ConfigType) source assertion. Initializes a 0x380-byte "
        "AnimMgr pool slot, commits the initial world transform at "
        "+0x1C0..+0x1F0 and seeds the late animation state. It does not populate "
        "the optional scalar-bank pointers at +0x118/+0x120.",
    ),
    0x1418C47B0: (
        "FgAnimData_ConstructDefaults",
        "Initializes the fixed FgAnimData fields and delegates to "
        "FgAnimData_Reset. It explicitly zeros the optional pointer band "
        "+0xF8..+0x120, including decimal-command-17's +0x120 scalar-bank pointer.",
    ),
    0x1418C4AD0: (
        "FgAnimData_Reset",
        "Resets a reusable 0x380-byte FgAnimData pool slot. It clears every qword "
        "in the optional pointer band +0xF8..+0x120 before later Init and is called "
        "both by pool setup and AnimMgr_AllocateAnimData. STATIC writes flags "
        "DWORD=78h at1418C4AEF, then ORs200000h at1418C4BDE; allocator calls "
        "Reset at14177F944. Phase40 is therefore part of default reset flags, "
        "not an owner-root-change detection signal.",
    ),
    0x1418C4E00: (
        "FgAnimData_RelocateInternalPointers",
        "Relocates existing internal FgAnimData pointers that fall inside a moved "
        "memory range. The +0x120 store at 0x1418C4E6E only rebases a prior non-null "
        "pointer; it does not create or populate the scalar bank.",
    ),
    0x1417CD200: (
        "SkelTable_FindArtItemSkeletonByPackageNameHash",
        "STATIC AGY correction: returns ArtItemSkeleton*, NOT upper Player "
        "state. Registry143862600 stride24/bitmap1466EC480 is populated "
        "by LoginArtItemSkeleton: ptr+0 at1417CD681, package-name ptr+8 "
        "atCD684, hash+0x10 atCD6AE. Lookup compares requested package hash "
        "atCD244 and returns entry+0 atCD2DD. proto/young-drake-base/ "
        "chloe-base callers subsequently read skeleton database+0x24.",
    ),
    0x1417CD3D0: (
        "FinalizeOrActivateUpperPlayerStateRecord",
        "Follow-up helper over an upper Player state record. Uses the record "
        "field at +0x24 and internal registry state to retain/finalize the "
        "record, and is called after 0x140CC1450 stores a freshly resolved "
        "state into player+0x1A0.",
    ),
    0x1410DB040: (
        "ValidateTopLevelPlayerActionContextAgainstProcessSpawnInfoState",
        "Broad top-layer Player preflight validator over ProcessSpawnInfo-style "
        "input. Now clearly reads as a gesture/verb permit gate: it checks the "
        "current outer-owner state across +0x1A0, +0x1D88, +0x1D50, +0x1D48, "
        "+0x1E58, +0x1FF0, and +0x3D98 and emits structured failure reasons "
        "such as `cinematic is playing`, `player has two-handed weapon in "
        "hands`, `player is aiming`, `player is in an IGC`, `player is "
        "crouched`, `player has flashlight/torch/flare`, `character is in "
        "melee`, and other locomotion/gesture restrictions.",
    ),
    0x1410DBF10: (
        "AdaptTopLevelPlayerValidationResultIntoIncomingRecord",
        "Small wrapper around 0x1410DB040. The incoming record now reads as: "
        "record+0x0 = live actor, record+0xC = minimum accepted result "
        "threshold or -1 wildcard, record+0x10 = optional out pointer for the "
        "chosen failure token. Feeds that record into the top-layer Player "
        "preflight validator and copies the produced reason/result pointer "
        "back through record+0x10 when the threshold rejects the action.",
    ),
    0x1410DC120: (
        "ClassifyUpperPlayerStateTagIntoSmallEnum",
        "Reads the authored/state tag at incoming_record+0x4 and maps four "
        "known hashes into a compact result enum 0..3, else returns -1. Sits "
        "directly above the top-level Player init family around 0x1410DC160.",
    ),
    0x1410DBFA0: (
        "InitializeUpperPlayerOwnerBridgeWrapperBF50",
        "Seeds a tiny wrapper object with vtable 0x142A4BF50 and stores the "
        "real outer Player owner in object+0x8. This wrapper is then used to "
        "expose upper Player surfaces without passing the raw owner directly.",
    ),
    0x1410DBF80: (
        "AcceptWildcardIncomingUpperStateOrResolveBridgeIndexedSubstate",
        "If incoming_record+0xC is -1, returns success immediately. Otherwise "
        "tail-calls the bridge-wrapper virtual at +0x10, which resolves one of "
        "the compact indexed upper Player substates under owner+0x1A0.",
    ),
    0x1401B8420: (
        "BuildFixedTwoEntryUpperPlayerDescriptorFromBridgeWrapper",
        "Shared bridge-wrapper virtual that jumps into 0x141E52590. Builds a "
        "compact descriptor object with count=2 and a secondary authored-id "
        "array, which is then consumed by the upper Player manager at +0x3FD8 "
        "during registration.",
    ),
    0x1401B8430: (
        "ReturnFalseFromBridgeWrapperVirtual",
        "Trivial bridge-wrapper virtual that returns false. Sits beside the "
        "shared descriptor-builder slot in the 0x142A4BF50 wrapper family.",
    ),
    0x1410DBFC0: (
        "GetUpperPlayerActionPolicy2018FromBridgeWrapper",
        "Returns wrapper->owner->+0x2018 from the tiny upper Player bridge "
        "object at 0x142A4BF50.",
    ),
    0x1410DBFE0: (
        "GetIndexedUpperPlayerSubstateFromOwner1A0ViaBridgeWrapper",
        "For indices 0..1 resolves an indexed substate entry through "
        "wrapper->owner->+0x1A0 -> +0x58 -> +0x318 + 0x30*index. This exposes "
        "compact upper Player substate via the same bridge wrapper family.",
    ),
    0x1410DC020: (
        "GetUpperPlayerFinalizeLogBand3C20FromBridgeWrapper",
        "Returns wrapper->owner + 0x3C20 from the tiny upper Player bridge "
        "object at 0x142A4BF50.",
    ),
    0x1410DC030: (
        "RelocateOuterPlayerPointerInsideBridgeWrapperIfInRange",
        "Bridge-wrapper relocate helper. If wrapper->owner falls inside the "
        "supplied [old_base, old_end) image/window, rewrites object+0x8 to "
        "new_base + (owner-old_base).",
    ),
    0x1410DC050: (
        "DestroyBridgeWrapperAndOptionallyFree",
        "Tiny freeing dtor for the upper Player bridge wrapper. Stamps the "
        "wrapper-local vtable 0x1428DF6B0 and optionally releases the 0x10-byte "
        "object when dl&1 is set.",
    ),
    0x1410C6B90: (
        "Player_GetGestureController",
        "Trivial Player getter for Player+0x3FD8. RTTI vtable 0x142B91B60 "
        "identifies the object exactly as GestureController.",
    ),
    0x141E65C60: (
        "GestureController_Create",
        "Allocator wrapper for the GestureController stored at Player+0x3FD8. "
        "Allocates size 0x5E70 and calls GestureController_Initialize.",
    ),
    0x141E66090: (
        "GestureController_Initialize",
        "In-place GestureController initializer. Stamps exact RTTI vtable "
        "0x142B91B60, stores owner in +0x10 and initializes layer handlers/links. "
        "Shared, not Player-only: GameCharacter initializer1403D0947/0951 also "
        "constructs this object and stores it at owner+1CA0.",
    ),
    0x141E668C0: (
        "GestureController_BindOwnerAndBuildLayerHandlers",
        "Binds the Player bridge into GestureController and builds authored "
        "layer handlers. Tag 0x675650B0 creates a 0x290 GestureLayer; live base "
        "tag 0x7339D08C creates the compact BaseLayerGestureLayer. This exact "
        "tag split determines whether pre-align context+0x10 can carry a custom "
        "TargetPoint.",
    ),
    0x141E52590: (
        "BuildFixedTwoEntryUpperPlayerAuthoredDescriptor",
        "Shared upper bridge/helper that allocates a compact descriptor object "
        "with count=2, allocates a two-entry dword array, and fills it with "
        "two fixed authored ids derived from short baked byte sequences before "
        "returning the descriptor to manager-side registration paths like "
        "0x141E668C0.",
    ),
    0x141E65BE0: (
        "GetRegisteredUpperPlayerBridgeEntryCountFromManager3FD8",
        "Reads manager+0x840 and returns DWORD [that_object+0x0]. Used by the "
        "manager registration path as the entry count for upper bridge-derived "
        "handler arrays.",
    ),
    0x141E66D50: (
        "BindUpperPlayerAnimSnapshotManager3FD8FromInitPacketAndOwner4260State",
        "Top-level upper-manager bind path called directly from Player init. "
        "Consumes a compact typed packet/builder plus owner-local state at "
        "outer_player+0x4260, derives authored hash families, stores a "
        "compact object into manager+0x838, and dispatches handler->virtual "
        "+0x10 across all registered upper manager entries. The typed packet "
        "entries wired into this path now resolve to AnimSnapshotLayer setup "
        "families, so +0x3FD8 should be treated as an upper animation/"
        "snapshot manager rather than the final Player ownership surface.",
    ),
    0x141E69AA0: (
        "ResetUpperPlayerAnimSnapshotManager3FD8PerEntryStateAndInvalidateHandlers",
        "Upper Player manager phase used from the high-level Player update path. "
        "Clears repeated per-entry counters/pointers across the manager body "
        "and then iterates registered handlers in +0xD8 via handler->virtual "
        "+0xD8, passing owner/context state from +0x10, +0x170, +0x838, and "
        "+0x850. This is now best read as the invalidate/reset phase of the "
        "upper Player AnimSnapshotLayer manager.",
    ),
    0x141E69BD0: (
        "GestureController_PostAlignUpdate",
        "Exact gesture-controller.cpp:1201 GestureController::PostAlignUpdate. "
        "Shared Player/GameCharacter controller phase, also NPC+1CA0 virtual+68. "
        "Reinitializes the shared "
        "state band at +0x170, builds a temporary 8-slot aggregate from "
        "owner-derived state, and then pushes that aggregate back into all "
        "registered handlers through handler->virtual +0xE0. Current typed "
        "handlers and nearby strings tie this phase to upper AnimSnapshotLayer "
        "publication rather than to direct Player ownership rebinding.",
    ),
    0x141A77430: (
        "RelocateAnimSnapshotLayerSetupEntry04PointersIfInRange",
        "Relocation helper for the typed `0x04` upper bind packet entry. "
        "Rebases embedded pointers at +0x10/+0x18/+0x40/+0x48 when they fall "
        "inside the moving range, matching the entry vtable at 0x142B35140.",
    ),
    0x141A774B0: (
        "CheckAnimSnapshotLayerSetupEntry04HasAttachedSources",
        "Readiness/helper check for the typed `0x04` upper bind packet entry. "
        "Returns true when either +0x40 or +0x48 is non-null.",
    ),
    0x141A774E0: (
        "AssertThenDispatchAnimSnapshotLayerSetupEntry04ViaVirtual58",
        "Diagnostic/assert wrapper for the typed `0x04` AnimSnapshotLayer "
        "setup entry. Validates the variant, then tail-dispatches the real "
        "operation through entry virtual +0x58.",
    ),
    0x141A77550: (
        "SeedAnimSnapshotLayerSetupEntry04PrimarySource",
        "Compact initializer for the typed `0x04` AnimSnapshotLayer setup "
        "entry. Seeds the owner/state fields and stores the primary source "
        "pointer in +0x40.",
    ),
    0x141A77580: (
        "SeedAnimSnapshotLayerSetupEntry04SecondarySource",
        "Compact initializer for the typed `0x04` AnimSnapshotLayer setup "
        "entry. Seeds the owner/state fields and stores the secondary source "
        "pointer in +0x48.",
    ),
    0x141A77A20: (
        "RelocateAnimSnapshotLayerSetupEntry08PointersIfInRange",
        "Relocation helper for the typed `0x08` upper bind packet entry. "
        "Rebases embedded pointers at +0x10/+0x18/+0x40 when they fall "
        "inside the moving range, matching the entry vtable at 0x142B352E0.",
    ),
    0x141A77A80: (
        "CheckAnimSnapshotLayerSetupEntry08HasAttachedSource",
        "Readiness/helper check for the typed `0x08` upper bind packet entry. "
        "Returns true when +0x40 is non-null.",
    ),
    0x141A77AA0: (
        "AssertThenDispatchAnimSnapshotLayerSetupEntry08ViaVirtual58",
        "Diagnostic/assert wrapper for the typed `0x08` AnimSnapshotLayer "
        "setup entry. Validates the variant, then tail-dispatches the real "
        "operation through entry virtual +0x58.",
    ),
    0x141A77B50: (
        "SeedAnimSnapshotLayerSetupEntry08PrimarySource",
        "Compact initializer for the typed `0x08` AnimSnapshotLayer setup "
        "entry. Seeds the owner/state fields and stores the primary source "
        "pointer in +0x40.",
    ),
    0x141A77B90: (
        "AppendResolvedAnimSnapshotLayerRecordIntoUpperPacketWriter",
        "Takes a resolved authored record from the entry source object, packs "
        "it into the destination packet writer as a `0x0A000C`-shaped chunk, "
        "and advances the writer cursor by 10 dwords.",
    ),
    0x140CB5050: (
        "WriteKeyedByteFlagIntoPlayerOwner3BD0Registry",
        "Resolves/creates a keyed entry inside outer_owner+0x3BD0 and writes "
        "a one-byte payload into it. Used by upper Player action-policy paths "
        "to publish compact verb/state flags like ReloadWeapon, RechamberWeapon, "
        "kRopeClimb, and piton-stab-fx.",
    ),
    0x141099520: (
        "BuildUpperPlayerGesturePermitContextFromMountedGraph",
        "Upper Player verb/gesture policy builder. Gathers state from mounted "
        "siblings +0x1E50 and +0x1E58, owner-local runtime state at +0x4320, "
        "and higher Player surfaces +0x1DF0, +0x1D78+0x120, and +0x3BD0 to "
        "produce a local gesture/action context block.",
    ),
    0x141099175: (
        "QueryUpperPlayerGesturePermitFamily2018WithBuiltContext",
        "Calls 0x141099520 to build a local gesture/action context, then feeds "
        "that context into the upper Player family object at outer_owner+0x2018. "
        "This is a live upper PlayerGesturePermit/action-policy path above the "
        "mounted graph, not just a lower weapon-action helper.",
    ),
    0x14109BE19: (
        "ConsumeUpperGesturePermitResultAndPublishKeyedPolicyFlag",
        "Consumes the compact result returned by the upper +0x2018 family, "
        "stores the returned scalar/state-id into local verb state, and then "
        "publishes a keyed one-byte policy/action flag back into outer_owner+0x3BD0. "
        "This ties the upper gesture/action-permit family directly to the "
        "owner-local registry surface.",
    ),
    0x140CB5050: (
        "WriteKeyedByteFlagIntoPlayerOwner3BD0Registry",
        "Takes rcx = outer_owner+0x3BD0, edx = hashed key, r8 = pointer to a "
        "one-byte payload, resolves/allocates storage through the internal "
        "tables at object+0xB8, copies the byte, and then dispatches a "
        "follow-up registry update. This makes +0x3BD0 an owner-local keyed "
        "byte/flag registry, not just a passive lookup cache.",
    ),
    0x1411A65C0: (
        "ConstructMountedPlayerSubsystem1DE0_Size6090",
        "Constructor for the mounted outer_owner+0x1DE0 Player subsystem. "
        "Seeds several large internal tables/subregions and calls the mounted-"
        "player action-family hash checker 0x1410F75C0 during setup. This is "
        "a substantial member of the real Player action-owner graph.",
    ),
    0x140F187B0: (
        "ConstructMountedPlayerSubsystem1D48_Size690",
        "Constructor for the mounted outer_owner+0x1D48 Player subsystem. "
        "Lives in the same early Player::Init band as the later qualification/"
        "policy owners and should be treated as part of the same promotion map.",
    ),
    0x14100D5A0: (
        "PlayerStrafeController_Create",
        "Allocates and constructs the concrete PlayerStrafeController stored "
        "at Player+0x1D50. Installs RTTI vtable 0x142A3FA88, initializes its "
        "JointChain and JacobianMap storage, and returns the 0x320-byte object.",
    ),
    0x141301710: (
        "ConstructMountedPlayerSubsystem1DA0_Size52C0",
        "Heavy mounted Player subsystem constructed into outer_owner+0x1DA0. "
        "This sits in the same investigate/read-mode/presentation region as "
        "other real Player owners, not in a helper-only bucket.",
    ),
    0x140FF5560: (
        "BuildMountedPlayerSubsystem1DE8",
        "Direct builder for outer_owner+0x1DE8 in the main Player::Init "
        "assembly pass. This is another formal mounted Player subsystem.",
    ),
    0x140F61490: (
        "DispatchMountedPlayerCallbackIntoOuter1FB0ReadyPath",
        "Small callback helper that resolves an owner-side object, queries "
        "outer_owner+0x1FB0 via virtual +0x78, and if present tail-calls the "
        "same dispatch object via virtual +0x48. This is a direct ingress into "
        "the outer Player verb-dispatch hub.",
    ),
    0x140F614E0: (
        "RouteAuthoredPlayerEventIdIntoOuter1FB0Dispatch",
        "Callback-family dispatcher under vtable 0x142A35090. Reads an "
        "authored event id from the incoming record and routes directly into "
        "outer_owner+0x1FB0 via virtual +0x100 or +0x128, with a few compact "
        "event ids only toggling local callback state bytes.",
    ),
    0x140F615B0: (
        "ConstructMountedPlayerEventCallback_40",
        "Allocating constructor for the compact authored-event callback object "
        "with vtable 0x142A35090. Stores the outer Player owner pointer and "
        "feeds the callback family that routes events into +0x1FB0.",
    ),
    0x140F61610: (
        "InitializeMountedPlayerEventCallbackInPlace",
        "In-place initializer for the compact authored-event callback family "
        "with vtable 0x142A35090.",
    ),
    0x140F5F920: (
        "ConstructMountedPlayerDispatchHub1FB0_Size3E0",
        "Allocating constructor for the real outer_owner+0x1FB0 dispatch hub. "
        "Allocates 0x3E0 bytes, clears the block, and tail-calls the in-place "
        "initializer at 0x140F5F980.",
    ),
    0x140F5F980: (
        "InitializeMountedPlayerDispatchHub1FB0",
        "In-place initializer for the real outer_owner+0x1FB0 dispatch hub "
        "with vtable 0x142A34E10. Seeds the state bands later consumed by the "
        "outer verb orchestrator and authored-event callback family.",
    ),
    0x140F60680: (
        "ProcessOuterPlayerMountedVerbOrchestrator",
        "Large outer Player-side verb orchestrator. Chooses among mounted "
        "subsystems (+0x1DE0/+0x1E98/+0x1E50/+0x1EB0/+0x1DB8), uses the "
        "+0x1FB0 dispatch object heavily, and commits through owner virtuals "
        "+0xBD0/+0xC78/+0xC98/+0xCB8.",
    ),
    # --- Input pipeline (2026-06-16 capstone trace; toward redirecting control) ---
    0x141A99F40: (
        "XInputGetStateWrapper",
        "Thin per-pad wrapper: reads pad-object index at +0x0, calls "
        "XInputGetCapabilities(idx) then XInputGetState(idx, &pad+0x20); sets "
        "connected byte at pad+0x4. Pad object layout: index +0x0, connected "
        "+0x4, caps +0xC, XINPUT_STATE +0x20 (sThumbLX +0x28).",
    ),
    0x141A9A790: (
        "InputManager_PollPads_SingleActivePad",
        "Pad poll loop of the single global input manager (vtable base "
        "0x142B37E50, ctor 0x14179C280, size 0x70). Iterates 4 pad slots at "
        "mgr+8..+0x20; mgr+0x28 = active pad; mgr+0x58 = round-robin scan "
        "counter. GetCapabilities gates GetState per index. Picks the FIRST "
        "connected pad with input as the active pad -> drives THE one player "
        "(campaign = single-player input, no pad-index->player-index routing).",
    ),
    0x141A99FD0: (
        "InputManager_NormalizeStick",
        "Normalizes a pad's XINPUT_STATE into the shared input-state: reads "
        "pad+0x28/+0x2A (int16 LX/LY), /32768, clamps, writes normalized LX/LY "
        "to dst+0x10/+0x14 (RX/RY follow). dst (rdx) = the input-state GLOBAL at "
        "module RVA 0x66E4220.",
    ),
    0x14179C280: (
        "InputManager_Init",
        "Creates the single global input manager (heap, 0x70) + outer 0xf0 "
        "object; registers a Steam callback. Called once from the engine init "
        "0x140155760 (no per-player loop) => single-player input architecture.",
    ),
    0x14179C170: (
        "Input_AnyAxisNonzeroPredicate",
        "Returns whether any of several float axes (rbx+0/4/8/0xc, "
        "+0x4E8..+0x504) are non-zero (deadzone/has-activity test). Used by the "
        "focus dispatch to decide if a consumer has live input.",
    ),
    0x14179E310: (
        "Input_FocusDispatch",
        "Distributes the input-state (global 0x66E4220) to the consumer array "
        "0x146B2C620 (count 0x146B1B55C). For each consumer calls "
        "*(consumer)+0x30(input-state); FIRST that returns true claims focus "
        "(stored at 0x146B2C618) and the loop breaks. This is the UI/overlay "
        "input-focus stack (null in plain gameplay); gameplay player-control is "
        "the downstream default path.",
    ),
    0x14183B410: (
        "Input_ScaleCopyVirtual",
        "Converts one source input block into a destination Joypad. The direct "
        "analog copy is gated by g_InputWindowFocused and GameInfo keyboard mode; "
        "keyboard replacements exist only for destination indexes 0 and 2. "
        "Destination Joypad 3 therefore needs the narrow background analog route.",
    ),
    0x141836640: (
        "JoypadState_Construct",
        "Constructs the 0xBE9-byte per-slot Joypad state used at "
        "FrameState+0xDB0+slot*0xBF0, then calls JoypadState_Reset.",
    ),
    0x1418369C0: (
        "JoypadState_Initialize",
        "Initializes a per-slot Joypad state, including its callback array and "
        "command tables, then calls JoypadState_Reset.",
    ),
    0x141836BB0: (
        "JoypadState_Reset",
        "Resets the per-slot Joypad state and installs the joypad-default command.",
    ),
    0x14179EA90: (
        "Input_ScaleCopyThunk",
        "Thunk: forwards to Input_ScaleCopyVirtual(a4, a3, a5); then optionally "
        "sub_141836BB0(a4) if a1+0x10 set.",
    ),
    0x140289150: (
        "FrameState_PerSlotInputApply",
        "Per-slot input/camera apply = FrameState vtable+0x20 (FrameState = "
        "*(g_LocalPlayerInputManager_Ptr 0x143FE1088), RTTI FrameState, vtable "
        "RVA 0x28FB968). Args (mgr, vtbl, inputblock(rbx), slotstruct(a4), "
        "slot(a5)). Processes per-slot camera heading (cos/sin) into the input "
        "block and calls Input_ScaleCopyVirtual. PER-SLOT ENABLE GATES: byte at "
        "FrameState+0x70B8+slot and struct at FrameState+0x6FC8+0x28*slot; both "
        "set for slot 0 (active), 0 for slots 1-5 (idle). Activating slot 1 "
        "(gates + player binding + feeding its input block 0x1466E4220[166*1]) "
        "is the native per-viewport/per-player path to a real player 2.",
    ),
    0x140289A90: (
        "Input_BlendAxisByThresholds",
        "Pure arithmetic helper used by the circular scripted-input branch. "
        "Selects or linearly blends bounded axis constants around a threshold; "
        "it has no state writes and does not activate a player slot.",
    ),
    0x1402893D0: (
        "FrameState_PerSlotInputApply_Branch",
        "Branch of the per-slot apply taken when FrameState+0x6FC8+0x28*slot is "
        "set (slot has an active per-slot input struct).",
    ),
    0x14179E6C0: (
        "Input_PerPlayerDispatch_MgrArg",
        "Sibling of Input_FocusDispatch taking the manager as arg a1. Same body: "
        "UI focus loop over consumer array, then a 6-SLOT loop applying each "
        "slot's input-state block &qword_1466E4220[166*idx] via "
        "(*mgr+0x20)(mgr,*mgr,inputblock,slotstruct,slot). This is the engine's "
        "native PER-PLAYER input distribution (6 slots; slot 0 active in "
        "campaign, slots 1-5 idle). Manager = *(0x143FE1088); slot structs at "
        "mgr+0xDB0 + slot*0xBF0. The loop cursor is mgr+0xE88 and the input-block "
        "selector is cursor+0x18 = Joypad+0xF0. Per-slot apply = manager vtable+0x20.",
    ),
    # --- Player fire and native network-animation path (2026-06-29) ---
    0x140E2C730: (
        "PlayerFireController_Fire",
        "Confirmed live campaign fire core (game/player/player-fire.cpp). "
        "controller+0x108 is the owning Player; R8 is the active weapon and "
        "RDX points to FireArgs. Runtime hooks fired for both local roles.",
    ),
    0x140A46D70: (
        "SendNetEventEmitProjectileBullet",
        "Multiplayer-gated wrapper for NetEventEmitProjectileBullet. It is not "
        "the campaign fire source and produced no events in campaign runtime.",
    ),
    0x140A45520: ("NetEventEmitProjectileBullet_Ctor", "Constructor for the projectile-bullet network event."),
    0x140A45B40: ("NetEventEmitProjectileBullet_Read", "Deserializes a projectile-bullet network event."),
    0x140A45C90: ("NetEventEmitProjectileBullet_Write", "Serializes a projectile-bullet network event."),
    0x140A45E80: ("NetEventEmitProjectileBullet_Execute", "Executes a received projectile-bullet network event."),
    0x1418BB6D0: (
        "BuildAnimationEffectEntryList",
        "Builds 32-byte animation-effect entries containing an id, pointers, "
        "and flags. The output is not a joint-transform array and must never "
        "be copied between processes.",
    ),
    0x140BC4E60: (
        "NetEventCharacterMove_FillAnimCmdBitStream",
        "Builds native animation commands from Player+0x1A0 AnimControl via "
        "AnimControl_CreateAnimCommands, then serializes them into a 4096-bit "
        "stream. Its fourth argument is the multiplayer-only pArtGroups lookup "
        "table (Player+0x3FC8), not the animation-command source.",
    ),
    0x140BC2970: (
        "NetEventCharacterMove_FillPlayer",
        "Fills a CharacterMove event for a Player and invokes "
        "FillAnimCmdBitStream with Player+0x3FC8 as pArtGroups. Campaign leaves "
        "that multiplayer-only table null; do not force this network serializer "
        "as a replacement for the live AnimManager command path.",
    ),
    0x140BC3760: ("NetEventCharacterMove_FillNpc", "NPC CharacterMove event fill path; also invokes FillAnimCmdBitStream."),
    0x140BBD730: (
        "NetEventCharacterMove_Ctor",
        "In-place constructor for the 0x610-byte NetEventCharacterMove object; "
        "initializes its 512-byte animation bit buffer and event fields.",
    ),
    0x140B87B00: (
        "NetSnapshot_Ctor",
        "In-place constructor for the native NetSnapshot wrapper. Initializes "
        "fields through +0x710; allocate at least 0x720 bytes.",
    ),
    0x140BC11A0: (
        "NetEventCharacterMove_ToNetSnapshot",
        "Exact event-to-snapshot converter. Sets snapshot+0x688 to event+0x1C, "
        "copies the animation byte count/time, flags, weapons and movement "
        "metadata. This closes the local animation replication chain.",
    ),
    0x140BC2240: (
        "NetEventCharacterMove_FillWeaponState",
        "Reads eight live WeaponController slots from Player+0x1D88, records "
        "their compact art ids and states at event+0x3DC/+0x3FC, and fills "
        "selected weapon, throwable and detailed per-weapon state fields.",
    ),
    0x140BC1660: (
        "NetEventCharacterMove_PublishSnapshotToCharacter",
        "Builds a NetSnapshot from a CharacterMove event and publishes it to "
        "the supplied character through the native state-record path.",
    ),
    0x140B87820: (
        "NetSnapshot_GetAnimArtGroup",
        "Resolves one of 32 configured player/NPC animation art groups from "
        "*net-player-anim-art-groups* or *net-npc-anim-art-groups*.",
    ),
    0x1410D6530: (
        "Player_InitializeNetAnimArtGroups",
        "Clears Player+0x3FC8 and, when the multiplayer mode byte is set, "
        "allocates a 32-pointer table and fills it through "
        "NetSnapshot_GetAnimArtGroup. Campaign leaves this field null.",
    ),
    0x140A00BB0: (
        "NetAnimCmdGenerator_Initialize",
        "Initializes NetAnimCmdGenerator ownership and its own 32-entry "
        "animation art-group table before resetting snapshot storage.",
    ),
    0x140A01970: (
        "NetAnimCmdGenerator_RefreshAnimArtGroups",
        "Rebuilds the generator's 32-entry animation art-group table.",
    ),
    0x140BBD9D0: ("NetEventCharacterMove_Dtor", "Destructor for the 0x610-byte NetEventCharacterMove object."),
    0x140BBE5C0: ("NetEventCharacterMove_Read", "Deserializes NetEventCharacterMove."),
    0x140BBF000: ("NetEventCharacterMove_Write", "Serializes NetEventCharacterMove."),
    0x1403D01B0: (
        "NetAnimCmdGenerator_Ctor",
        "Initializes the NetAnimCmdGenerator embedded in each Player at +0x1B30.",
    ),
    0x140A090A0: ("NetAnimCmdGenerator_Dtor", "Destroys the Player-embedded NetAnimCmdGenerator."),
    0x140A01E50: (
        "NetAnimCmdGenerator_AddSnapshot",
        "Copies a received animation snapshot (up to 0x200 bytes) into the "
        "Player-embedded generator queue. Snapshot time is at +8 and payload "
        "pointer at +0x688 in the source wrapper.",
    ),
    0x140A02890: ("NetAnimCmdGenerator_ProcessSnapshot", "Consumes one queued native animation snapshot."),
    0x140A02820: ("NetAnimCmdGenerator_ProcessPendingSnapshots", "Pumps pending snapshots through ProcessSnapshot."),
    0x140A038B0: ("NetAnimCmdGenerator_GetOldestIceSnapshot", "Returns the oldest queued ICE animation snapshot."),
    0x140A09340: (
        "NetAnimCmdGenerator_DualSnapshotNodePool_Allocate",
        "Allocates a dual-snapshot node used by the native animation generator.",
    ),
    0x140A07160: (
        "NetAnimCmdGenerator_UpdateFlagsAndWeapons",
        "Applies snapshot flags and weapon state while reconstructing remote "
        "animation. This may solve visible weapons together with animation.",
    ),
    0x141BB7960: (
        "Process_PublishStructuredEntry",
        "Common Process virtual +0xD0 event sink present in hundreds of RTTI "
        "vtables. Resolves the concrete process event controller through virtual "
        "slot 24 and forwards the structured entry; defers while unavailable.",
    ),
    0x141E72540: (
        "ProcessEventController_DispatchStructuredEntry",
        "Dispatches a structured Process event through immediate script, "
        "replacement or deferred controller paths. PlayerWeaponAnimControl uses "
        "this generic path for authored named weapon events.",
    ),
    0x1401D4EE0: (
        "Process_GetEventController",
        "Common Process virtual slot 24 implementation. Returns the event/script "
        "controller stored at Process+0x218; Process_PublishStructuredEntry calls "
        "this slot before dispatching a structured event.",
    ),
    0x1410EDFB0: (
        "Player_KickPlayerDepenetrateWeapons",
        "Resolves targRWrist/targLWrist/rightHand/leftHand skeleton joints and "
        "writes wrist weapon-attachment transforms through the embedded object at "
        "Player+0x3530 into Player+0x3600/+0x3608.",
    ),
    0x141095BD0: (
        "PlayerWeaponHolsterAction_ResolveAndBindAttachPoint",
        "Resolves the live ProcessWeapon from the action handle, reads its authored "
        "art id at +0x860, selects mirrored/normal weapon-base-layer state and the "
        "per-art weapon-holster-point record, configures PlayerWeaponAnimControl "
        "through Player+0x1DF0, then stores the weapon handle through vtable +0xB8.",
    ),
    0x14108F4D0: (
        "PlayerWeaponAnimControl_Create",
        "Allocates the 0xE0-byte RTTI-backed PlayerWeaponAnimControl and installs "
        "vtable 0x142A46778. Player initialization stores it at Player+0x1DF0.",
    ),
    0x14108DAB0: (
        "PlayerWeaponAnimControl_SetWeaponHandle",
        "PlayerWeaponAnimControl vtable +0xB8. Copies the supplied 16-byte game "
        "object handle into control+0x30; it does not itself bind an attach point.",
    ),
    0x14108DE30: (
        "PlayerWeaponAnimControl_HandleNamedWeaponEvent",
        "PlayerWeaponAnimControl vtable +0xE8. Reads the stored weapon handle at "
        "+0x30 and publishes a named authored event to the resolved weapon process.",
    ),
    0x14108E700: (
        "PlayerWeaponAnimControl_InitializeHolsterJoints",
        "PlayerWeaponAnimControl vtable +0x130. Resolves r_leg_gun_holster, "
        "r_upper_leg, pelvis_holster and pelvis in the owning Player skeleton into "
        "signed indices at +0x78/+0x7C/+0x80/+0x84. Runtime legacy Nadine "
        "resolves -1,5,-1,3 because both Chloe marker names are absent; her "
        "actual pistol marker is joint 99 under a different name id.",
    ),
    0x14108EBF0: (
        "PlayerWeaponAnimControl_UpdateHolsterJointTransforms",
        "PlayerWeaponAnimControl vtable +0x138. Builds the two relative holster "
        "transforms at +0x90/+0xB0 only when each stored joint-index pair is valid.",
    ),
    0x14108ECE0: (
        "PlayerWeaponAnimControl_UpdateHolsterIk",
        "PlayerWeaponAnimControl vtable +0x140, called near the end of "
        "Player_PostAnimBlending_Async after vtable +0x138 refreshes both bases. "
        "Reduces active WeaponPartial 0xE2B990B9 snapshots to one "
        "WeaponHolsterIkData, selects the leg or pelvis pair from its reference "
        "id, composes the cached marker-relative basis and drives the arm IK.",
    ),
    0x1414FE7E0: (
        "WeaponHolster_SelectCharacterAttachPoint",
        "Selects gun_holster_pelvis, gun_holster_leg or generic gun_holster by "
        "scanning the target character's authored attachment-point records at "
        "+0x1E0.",
    ),
    0x1414FEB30: (
        "WeaponHolster_ResolveAttachPoint",
        "Reads active WeaponArtDef+0xD8 -> descriptor+8 -> primary binding "
        "attachment StringId at binding+0 (0x1414FEB8E). Descriptor+0 is not "
        "a primary-binding count. A non-generic id such as targSpineProp "
        "0x00AD047A or targChest 0x4F62CD9D bypasses the character scan; "
        "generic gun_holster 0x555EFB8D delegates to "
        "WeaponHolster_SelectCharacterAttachPoint. The current selected id "
        "alone does not establish an art's authored holster endpoint.",
    ),
    0x1414FE540: (
        "WeaponHolster_BuildBinding",
        "Builds a Player holster binding: selected attachment StringId at +0, "
        "primary authored transform at +0x10/+0x20, secondary transform at "
        "+0x30/+0x40 and scalar at +0x50. ArtDef+0xD8 points to a descriptor "
        "whose +8 is the primary binding and +0x10 is an optional alternative "
        "endpoint array. Primary+0 is the id, +0x10/+0x20 its transform, "
        "+0x30/+0x38 optional secondary T/Q pointers and +0x40 the scalar; "
        "secondary transform pointers are not alternative endpoint records. "
        "At 0x1414FE621/62A each secondary component defaults to the primary "
        "T/Q; 0x1414FE63C/643 replaces Q only for nonnull primary+0x38, "
        "and 0x1414FE647/64E replaces T only for nonnull primary+0x30. "
        "Thus absent optional T/Q is valid component-wise primary fallback "
        "(including both-null M30/Ruger), not an absent secondary output. "
        "Non-generic targSpineProp (Mazur and other long guns, not P99) "
        "bypasses the character gun_holster* scan. "
        "The controller copies the primary transform to weapon+0x4F0/+0x500. "
        "A later +0x7A8 selected-id redirect does not rebuild this art-specific "
        "primary transform for the replacement endpoint. The secondary output "
        "at +0x30/+0x40 is consumed by 0x1410A9490 as a +0x600/+0x610 local "
        "blend target when both long-gun slots are occupied; it is distinct "
        "from ApplyLocalAttachmentCorrection's +0x510/+0x520 depenetration.",
    ),
    0x1410A9490: (
        "PlayerWeaponController_BlendSlotToSecondaryHolsterBinding",
        "STATIC: resolves the incoming inventory slot through exact "
        "PlayerWeaponController vslot +0xB0 (0x1410A1F90), builds its holster "
        "binding and requires the actual selected attachment id to match. "
        "At 0x1410A9533 it calls ProcessWeapon vslot +0x7A8 (0x141ED0580) "
        "with that same id, copies the builder's secondary output +0x30/+0x40 "
        "to weapon+0x600/+0x610, snapshots primary +0x4F0/+0x500 into "
        "+0x620/+0x630 and seeds duration/progress 0.2/0 at 0x1410A9557. "
        "The sole calling function RebindWeaponToHolster invokes it at "
        "0x1410A9A85 and 0x1410A9A92 for occupied slots 17 and 1. This is "
        "the dual-long-gun local endpoint blend, not runtime world-pivot "
        "depenetration at +0x510/+0x520/+0x5F0.",
    ),
    0x1410CDF90: (
        "Player_SetupAttachSystemFromLookData",
        "Exact Player::SetupAttachSystem source assertion. Selects player, "
        "player-net or player-young-drake from runtime mode/look state and calls "
        "AttachSystem_InitFromDcData on Player+0x1E0 with reserve count zero. "
        "NdGameObject_Init dispatches this through Player vtable +0x448 at "
        "0x141BB5043; it returns before PlayerWeaponController construction and "
        "the GiveWeapon holster path, making its post-return the exact early "
        "compatibility-table boundary.",
    ),
    0x1418B61B0: (
        "AttachSystem_InitFromDcData",
        "Exact AttachSystem::InitFromDcData source assertion. Resolves the "
        "authored *attach-points* knowledge data, allocates capacity*48 bytes, "
        "resolves each authored joint against the active rig and materializes "
        "valid 48-byte attachment records into the AttachSystem container.",
    ),
    0x1418B6D40: (
        "AttachSystem_CopyConstruct",
        "Deep-copies AttachSystem capacity*48 bytes and live count into a new "
        "engine allocation while intentionally clearing the destination rig "
        "context pointer at +0x10.",
    ),
    0x1418B6E20: (
        "AttachSystem_Clear",
        "Clears only the AttachSystem live record count at +0x4; capacity and "
        "the existing 48-byte record allocation remain intact.",
    ),
    0x1418B6E50: (
        "AttachSystem_AppendRecord",
        "Appends one 48-byte attachment record when live count is below capacity "
        "and its signed joint index is -1 or valid for the active rig. Copies "
        "transform, StringId, joint, flag bit0 and runtime dword, then advances "
        "the live count.",
    ),
    0x1401990F0: (
        "Transform_MakeRelative",
        "Computes inverse(parent) * child for the compact 32-byte translation/"
        "quaternion transform representation and writes the result argument.",
    ),
    0x1401998F0: (
        "Transform_Compose",
        "Composes parent * relative for the compact 32-byte translation/"
        "quaternion transform representation and writes the result argument.",
    ),
    0x14019A270: (
        "Matrix4x4_FromTranslationQuaternion",
        "Builds a rigid 4x4 matrix from the compact Locator translation at "
        "+0 and quaternion at +0x10. WeaponIK_Apply uses it for the selected "
        "and opposite wrist/hand-prop Locators before constructing the final "
        "off-hand target.",
    ),
    0x14023ABB0: (
        "Matrix4x4_InvertAffine",
        "Inverts a general nonsingular affine4x4 transform using cofactors, "
        "determinant and vrcpps Newton refinement, NOT a rigid transpose. "
        "The historical InvertRigid name was too narrow. WeaponIK_Apply applies it to the "
        "opposite hand-prop matrix before transferring the opposite wrist-to-"
        "prop relation onto the selected hand-prop. SkelTable retarget "
        "construction also uses it on sampled transforms and source/target "
        "inverse-global Mat34 operands converted to4x4 columns.",
    ),
    0x140EE7500: (
        "Matrix4x4_NormalizeBasisRows",
        "Normalizes the first three basis rows of a 4x4 transform in place. "
        "WeaponIK_Apply uses it immediately before converting the transferred "
        "off-hand target matrix back to a quaternion and translation. STATIC "
        "reach trace: writes basis rows+0/+10/+20 ONLY, not translation+30; "
        "it cannot secretly clamp or blend the pending left target position.",
    ),
    0x1403421B0: (
        "Script_ReattachPlayerWeapon",
        "Native reattach-player-weapon script command. Validates a "
        "ProcessWeaponBase selector, builds the holster binding, resolves the "
        "owner attachment point and reaches WeaponAttachment_AttachToOwnerPoint.",
    ),
    0x140342EE0: (
        "Script_AttachPlayerWeaponBackToHolster",
        "Native attach-player-weapon-back-to-holster script command. Builds the "
        "holster binding and physically attaches the weapon before publishing "
        "the set-attached structured event.",
    ),
    0x140340C40: (
        "Script_AttachObjectOffset",
        "Registered state-script native attach-offset. Parses an attachable "
        "object, owner and authored point/offset, calls "
        "WeaponAttachment_AttachToOwnerPoint, then publishes set-attached.",
    ),
    0x140341430: (
        "Script_AttachObject",
        "Registered state-script native attach. Parses an attachable object, "
        "owner and authored attach point, calls "
        "WeaponAttachment_AttachToOwnerPoint, then publishes set-attached.",
    ),
    0x14003EA60: (
        "Script_RegisterAttachOffset",
        "Static native-registration stub pairing the literal attach-offset "
        "with Script_AttachObjectOffset in the global script-native registry.",
    ),
    0x14003EAC0: (
        "Script_RegisterAttach",
        "Static native-registration stub pairing the literal attach with "
        "Script_AttachObject in the global script-native registry.",
    ),
    0x14003EB80: (
        "Script_RegisterReattachPlayerWeapon",
        "Static native-registration stub pairing reattach-player-weapon with "
        "Script_ReattachPlayerWeapon in the global script-native registry.",
    ),
    0x14003EC40: (
        "Script_RegisterAttachPlayerWeaponBackToHolster",
        "Static native-registration stub pairing "
        "attach-player-weapon-back-to-holster with its native implementation.",
    ),
    0x141ED0370: (
        "WeaponAttachment_AttachToOwnerPoint",
        "Concrete attachment mutation used by the two player-weapon script "
        "commands. Replaces weapon+0x4E0 parent handle, selects the owner's "
        "+0x1E0 attachment record by StringId, updates weapon+0x688/+0x691 and "
        "runs the attachment refresh virtuals.",
    ),
    0x14150F2E0: (
        "ProcessWeapon_Ctor",
        "Constructs ProcessWeapon and installs vtable 0x142A9BF48. Runtime census "
        "identified firearm children through this RTTI-backed class. Installed "
        "sidbase.bin and weapon-art.bin resolve 0x8075397D as "
        "pistol-silenced-dlc (child 0x255BEBA0 p99-silencer-dlc), "
        "0x33E29A1B as sniper-rifle-bolt and 0x0B8BD6D6 as the grenade.",
    ),
    0x141510990: (
        "ProcessWeapon_Dtor",
        "Destroys ProcessWeapon-owned handles and child state, restores the "
        "ProcessWeaponBase vtable, then delegates to the base destructor.",
    ),
    0x1401D0A90: (
        "ProcessWeapon_ParseAllowTracerAndDispatch",
        "Parses the authored allow-tracer key from the emission parameter map. "
        "Missing or nonzero values dispatch true through virtual slot +0x800; "
        "an explicit zero dispatches false and later clears launch-record+0x88 "
        "bit 5, preventing stock Tracer materialization.",
    ),
    0x1415068E0: (
        "WeaponUpgrade_ClampLevelIndex",
        "Validates the selected per-property upgrade level against the authored "
        "level count, emits the embedded out-of-range diagnostic, and clamps to "
        "the highest defined level.",
    ),
    0x141506AB0: (
        "WeaponUpgradeTable_GetValueByIndex",
        "Reads the upgrade-level byte at upgrade_levels[index], validates that "
        "level against the weapon-art id, and returns the selected 32-bit table "
        "value or 0xFFFFFFFF. ProjectileBullet tracer materialization queries "
        "index 0x0D at 0x1415224AE.",
    ),
    0x14151C030: (
        "ProcessWeapon_CreateMuzzleSfxActor",
        "Builds the authored muzzle SFX selector packet, materializes the SFX "
        "actor, publishes set-register/set-pitch state and returns its retained "
        "handle. This is the source of ProcessWeapon+0x998/+0x9A8 handles, not "
        "a secondary rifle drawable.",
    ),
    0x14150E860: (
        "ProcessWeapon_StopMuzzleSfxActors",
        "Stops the retained authored muzzle SFX actors held at ProcessWeapon "
        "+0x998/+0x9A8 and clears their lifecycle state. This is the teardown "
        "boundary paired with ProcessWeapon_CreateMuzzleSfxActor.",
    ),
    0x14155BE10: (
        "Tracer_InitFromProjectileLaunchRecord",
        "Exact Tracer::Init source boundary and vtable entry. ABI arguments are "
        "(Tracer, result, spawn_info); spawn_info+0x28 selects the copied "
        "projectile launch record. It normalizes direction at record+0x30 and "
        "materializes persistent visual transforms from origin record+0x70.",
    ),
    0x1410F8660: (
        "StringId_FormatDashedPairAndHash",
        "Formats the supplied category/value pair as '-%s-%s' and returns its "
        "engine CRC-style StringId. Used for weapon-holster-point, animation "
        "layers and many other hierarchical authored keys.",
    ),
    0x14023AEA0: (
        "InputObject_MatchesActiveLocalJoypad",
        "Returns true when object+0x44 has bit 0x80000 and object+0x50 matches the "
        "active joypad index from g_LocalPlayerInputManager (+0x48/+0x50). Gates "
        "weapon-attachment refresh at Player+0x3530.",
    ),
    0x140A80E50: (
        "WeaponArtId_ToNetworkIndex",
        "Maps Weapon+0x860 authored art id to the compact network inventory "
        "index. Used independently by projectile events and all eight entries "
        "in NetEventCharacterMove_FillWeaponState. Its non-standard-art fallback "
        "depends on the dormant online definition registry, so do not use it as "
        "a campaign raw-art serializer.",
    ),
    0x140A81050: (
        "NetGameManager_GetWeaponIdByStatIndex",
        "Inverse compact-index mapping. Embedded source assertions identify "
        "NetGameManager::GetWeaponIdByStatIndex at net-game-manager.cpp:534-535; "
        "indices >=19 resolve through the cached online weapon definition map.",
    ),
    0x1410A4D40: (
        "PlayerWeaponController_GiveWeapon",
        "Exact embedded signature: PlayerWeaponController::GiveWeapon(WeaponInfo*, "
        "bool, bool, BuffSource const*), controller vtable +0x158. Reads initial "
        "total ammo from WeaponInfo+0x84; -1 selects authored/random derivation "
        "before passing the result to CreateAndAddWeapon vtable +0x130. The "
        "replacement slot is NOT WeaponInfo+8: it resolves the incoming art via "
        "CachedWeaponArtDefs, reads the definition record DWORD at +8, maps "
        "campaign aliases 16->0 and 17/22->1, then retains the exact old "
        "ProcessWeapon from vtable +0xB0. It detaches that object at 0x1410A68AC, "
        "publishes weapon-dropped at 0x1410A693C, then sends it only when native "
        "net mode is enabled. WeaponInfo fields include art +0x80, ammo +0x84, "
        "flags +0x96 and net drop state +0x9C..+0xB3. Runtime iq proved the "
        "separate COPG return-RVA admission omits whitelist art 0x33E29A1B; COPW "
        "v3 therefore captures this original 0xC0 argument at the safe +9 detour "
        "only inside the exact outer WeaponInteractControl transaction.",
    ),
    0x1410A1F90: (
        "PlayerWeaponController_GetWeaponBySlot",
        "Controller vtable slot +0xB0. Bounds slot to 0..22, indexes the 0x10-byte "
        "weak records at controller+0x148, reads handle pointer +0 and generation "
        "+8, resolves object from handle+8, and requires object+0xBC to match. "
        "On success it also writes object+0x72=1 at 0x1410A1FCB, so read-only "
        "inventory admission must reproduce the weak checks instead of calling "
        "this vslot. ProcessWeapon authored id is at +0x860.",
    ),
    0x1410A77E0: (
        "PlayerWeaponController_DetachWeaponBySlot",
        "Controller vtable +0x188. Resolves the live weapon by slot, updates "
        "controller state, clears the manager weak record and returns the "
        "detached object without killing it. Native removal wrappers call this "
        "and then ProcessKillTree; never overwrite an occupied live slot.",
    ),
    0x1410A1C10: (
        "PlayerWeaponController_SwitchToWeapon",
        "Controller vtable slot +0x40. Validates the requested inventory slot, "
        "runs the concrete switch implementation at vtable +0x48, records the "
        "requested slot at controller+0x130 when the third argument is set, and "
        "returns success independently of later visual attachment.",
    ),
    0x1410A28A0: (
        "PlayerWeaponController_ApplyWeaponSwitch",
        "Controller vtable slot +0x48 and the concrete weapon switch state "
        "machine. Resolves the requested and current weapon objects, applies "
        "holster/unholster transitions, and rejects invalid controller states.",
    ),
    0x1410A4130: (
        "PlayerWeaponController_StartCrossSlotSwitch",
        "Exact state-2 different-slot transition used by ApplyWeaponSwitch. "
        "Removes the prior channel-1 transition, enqueues PlayerPutGunAwayAction "
        "for the current weapon together with PlayerGetWeaponInHandAction for "
        "the target weapon, then enters controller state 5. This paired path is "
        "distinct from a cold same-slot Draw followed by Put.",
    ),
    0x1410A4740: (
        "PlayerWeaponController_GetEffectiveCurrentWeaponIndex",
        "Controller vtable +0x68. Normally returns authoritative current index "
        "+0x120; while current is transient -2, returns staged +0x124 only when "
        "that index is valid and resolves to a current ProcessWeapon. "
        "Player_ChangeLookId saves this result before rebuilding the look.",
    ),
    0x1410A6E20: (
        "PlayerWeaponController_ReapplyWeaponIndex",
        "Controller vtable +0x60. Resolves the current ProcessWeapon through "
        "vtable +0xB0 and, when present, delegates the supplied index and flags "
        "to PlayerWeaponController_ApplyImmediateWeaponIndex. ChangeLookId calls "
        "this after rebuilding the Player body and attachment system.",
    ),
    0x1410A6E70: (
        "PlayerWeaponController_ApplyImmediateWeaponIndex",
        "Removes conflicting weapon classes, updates controller selection fields, "
        "calls PlayerWeaponController_EquipWeaponIndex and refreshes the weapon "
        "overlay/knowledge state. Used by GiveWeapon and vtable +0x60 reapply.",
    ),
    0x1410A49E0: (
        "PlayerWeaponController_GetHandAttachedWeaponSlot",
        "Implementation behind controller vtable slot +0x78. Scans inventory "
        "slots and returns the slot whose attachment resolves to rightHand or "
        "leftHand and whose attachment owner is this Player; returns -1/20 fallback.",
    ),
    0x1410A4CE0: (
        "PlayerWeaponController_GetRequestedInHandsSlot",
        "Controller vtable slot +0x90. Returns controller+0x130, which records "
        "the requested in-hands slot but does not prove visual attachment.",
    ),
    0x1410A1FE0: (
        "PlayerWeaponController_GetCurrentWeapon",
        "Controller vtable slot +0xB8. Resolves slot 19 when controller+0x120 "
        "equals 19; otherwise resolves the current slot byte at +0x12C.",
    ),
    0x141805020: (
        "ResolvedLook_ResolveMeshes2",
        "Source-identified ResolvedLook::ResolveMeshes2 path. Recursively walks "
        "the Look2 mesh-reference graph, resolves only look2-mesh leaves into "
        "the ResolvedLook mesh table and copies the authored +0x48..+0x4C flag "
        "bytes into the parallel mesh bitsets. Exact nadine-rural traversal "
        "reaches head, shirt, body, nadine-holster-game and nadine-holster and "
        "contains no pistol geometry leaf.",
    ),
    0x141805C60: (
        "ResolvedLook_ResolveProps2",
        "Resolves the Look2 prop list into ResolvedLook+0x5848. Stores at most "
        "16 prop ids at +0x584C with parallel flags at +0x588C; legacy "
        "nadine-rural resolves only its authored hair-cloth prop.",
    ),
    0x141E61A40: (
        "NdPropControl_MaterializePendingProps",
        "Materializes the pending Look2 prop spawn entries built by "
        "NdPropControl_BuildPropSpawnList and stores their retained actor weak "
        "pairs in the live NdPropControl table.",
    ),
    0x141E63000: (
        "NdPropControl_ResolveLivePropAtIndex",
        "Validates an NdPropControl live-entry index and generation, then returns "
        "the retained prop actor. Player main-view visibility uses this resolver "
        "before publishing visibility events to Look2 props.",
    ),
    0x1410D67B0: (
        "Player_GetActiveWeapon",
        "Player vtable slot +0x8C0. Resolves the special slot-19 weapon or the "
        "PlayerWeaponController current weapon, with a screen-route fallback.",
    ),
    # --- 2026-07-18: live ProcessWeapon ammo ownership ---
    0x141103830: (
        "ProcessWeapon_GetMagazineAmmo",
        "ProcessWeapon vtable +0x848. Returns signed int16 live magazine ammo "
        "from weapon+0x900. BuildFireArgsAndFire exits before Fire when this is <=0.",
    ),
    0x141103840: (
        "ProcessWeapon_GetReserveAmmo",
        "ProcessWeapon vtable +0x858. Returns signed int16 reserve ammo from "
        "weapon+0x902.",
    ),
    0x141103850: (
        "ProcessWeapon_GetTotalAmmo",
        "ProcessWeapon vtable +0x840. Returns magazine+reserve from signed "
        "int16 fields weapon+0x900/+0x902.",
    ),
    0x141103870: (
        "ProcessWeapon_IsMagazineEmpty",
        "ProcessWeapon vtable +0x860. Returns weapon+0x900 <= 0.",
    ),
    0x141103880: (
        "ProcessWeapon_IsMagazineUnloaded",
        "ProcessWeapon vtable +0x868. Returns weapon+0x908 == 0; the native "
        "ammo setters keep this loaded-state byte coherent with magazine ammo.",
    ),
    0x141517C90: (
        "ProcessWeapon_InitializeAmmoFromTotal",
        "Initializes live magazine/reserve from a total count using vtable "
        "+0x850 capacity, then updates weapon+0x908/+0x909 state bytes.",
    ),
    0x141517CF0: (
        "ProcessWeapon_SetMagazineAmmo",
        "ProcessWeapon vtable +0x820. Clamps magazine to capacity, rebalances "
        "reserve, writes +0x900, clears +0x909 and sets +0x908=(magazine>0).",
    ),
    0x141517D80: (
        "ProcessWeapon_SetReserveAmmo",
        "ProcessWeapon vtable +0x828. Writes the live signed int16 reserve "
        "field at weapon+0x902 and updates ammo state.",
    ),
    0x1415180C0: (
        "ProcessWeapon_AddAmmo",
        "ProcessWeapon vtable +0x818. Adds ammo to the live magazine or reserve "
        "according to capacity and reload state.",
    ),
    0x14151E0B0: (
        "ProcessWeapon_ReloadMagazine",
        "Moves live reserve weapon+0x902 into magazine weapon+0x900 up to "
        "capacity and marks weapon+0x908 loaded.",
    ),
    0x14151E2F0: (
        "ProcessWeapon_ReloadOneRound",
        "Single-round reload path: decrements reserve+0x902 and increments "
        "magazine+0x900 while capacity permits.",
    ),
    0x14151E740: (
        "ProcessWeapon_ConsumeShotAmmo",
        "Called only from ProcessWeapon_PrepareFireFromArgs after projectile "
        "dispatch. Decrements live magazine+0x900 at 0x14151EB13 unless a "
        "native cheat/turret/special condition suppresses consumption.",
    ),
    0x140E28A30: (
        "PlayerFireController_UpdateInputAndFire",
        "Per-frame fire controller update. Reads fire-hold/fire-press/fire-release "
        "from the owner's Joypad and runs the weapon-use, input and shot-readiness gates.",
    ),
    0x140E2ACA0: (
        "PlayerFireController_CheckFireInput",
        "Resolves Player vtable +0x8C0 active weapon, validates weapon state, "
        "then consumes controller+0x1E8 hold or +0x1E9 press before action exclusions.",
    ),
    0x140E2ADF0: (
        "PlayerFireController_CheckActionExclusions",
        "Rejects firing during authored Grenade, Grenade-Add, Grapple, "
        "Grapple-Add and gun-up-layer action states.",
    ),
    0x140E2B220: (
        "PlayerFireController_CheckShotReadiness",
        "Final active-weapon readiness check: rejects Drive and incompatible "
        "player states, then checks weapon class and shot timing/state. The "
        "decisive magazine gate is in BuildFireArgsAndFire, not this helper.",
    ),
    0x1410B15A0: (
        "PlayerWeaponController_CanUseWeapon",
        "Controller vtable slot +0x260. Broad player/action/camera/traversal gate "
        "called by PlayerFireController_UpdateInputAndFire before a shot.",
    ),
    0x1401B7600: (
        "WeaponInfo_InitializeDefaults",
        "Initializes the 0xC0-byte WeaponInfo used by the developer give-weapon "
        "path; writes initial ammo DWORD -1 at +0x84 (0x1401B766B), so GiveWeapon "
        "derives an authored/random total unless the caller replaces it. Authored "
        "weapon id is subsequently written at +0x80.",
    ),
    0x140CA1630: (
        "PlayerActiveAsyncJob_PublishHudState",
        "Callback registered by PlayerActiveAsyncJob descriptor 0x143071B70. "
        "Runs for each active Player and calls PlayerHud_ScreenStatePublishUpdate "
        "at 0x140CA1655, providing the retained screen-1 puppet HUD route.",
    ),
    # --- Input block-selector bus + HUD is-aiming publish chain (2026-07-04) ---
    0x14179D780: (
        "FrameState_ConstructJoypadSlots",
        "FrameState/input-manager constructor part that initializes the six "
        "per-slot Joypad states via JoypadState_Initialize(slot, selector, "
        "mode): selectors are {0,1,0,1,0,1} and mode is 3 for slots 0-1 (real "
        "local-player lanes) and 0 for slots 2-5. The selector byte lands at "
        "Joypad+0xF0 and picks WHICH g_InputStateBlocks entry the slot "
        "consumes in Input_PerPlayerDispatch: even slots read block 0, odd "
        "slots read block 1. Slot 3 (co-op puppet joypad) therefore shares "
        "input block 1 with slot 1 = the engine's local-player-1 lane; "
        "anything left in block 1 outside slot 3's conversion is read as "
        "local-player-1 input (co-op crosshair/aim HUD leak root cause).",
    ),
    0x140C91970: (
        "PlayerHud_ScreenStatePublishUpdate",
        "Per-frame player/HUD state publish driven from the global player "
        "handle at 0x143EEBE80 (entry stub 0x1401656B0). Reads the screen "
        "index at a1+0x4010, then runs "
        "UpdatePlayerHudAimingAndWeaponAccuracyPublishes on the per-screen "
        "HUD state array 0x143EF1E30 (0x100 stride). Also binds "
        "g_LocalPlayerInputManager_Ptr+0xE0 into type-512 HUD records.",
    ),
    # --- Per-screen HUD reticle publisher family (2026-07-04) ---
    0x14135A650: (
        "PlayerHudScreen_ReticleProcessPublisher",
        "Per-screen HUD reticle publisher over g_PerScreenPlayerHudState "
        "(screen/slot index at state+0xF8): spawns/kills the ss-hud-reticle "
        "process and publishes reticle-show-time / update-reticle-aspect. "
        "Reads GetRetainedPlayerEntityBySlot(*(state+0xF8)) and the slot's "
        "Joypad at g_LocalPlayerInputManager_Ptr+0xBF0*(slot+2). A screen-1 "
        "(co-op puppet) publish spawns the crosshair onto the single shared "
        "viewport — gated by the co-op mod when *(state+0xF8)!=0.",
    ),
    0x14135AF20: (
        "PlayerHudScreen_AmmoReticlePublisher",
        "Sibling per-screen HUD publisher (same state+0xF8 screen index): "
        "publishes total-ammo and also spawns ss-hud-reticle / "
        "reticle-show-time records. Called from "
        "UpdatePlayerHudAimingAndWeaponAccuracyPublishes. Gated by the co-op "
        "mod for screen!=0.",
    ),
    0x14135C050: (
        "PlayerHudScreen0_BlindfireReticlePublisher",
        "Blindfire reticle publisher hardwired to HUD screen 0 through the "
        "fixed aliases qword_143EF1F20 (= screen-0 state+0xF0) and "
        "qword_143EF1F28 (= screen-0 state+0xF8): show-blindfire-reticle, "
        "ss-hud-blindfire-reticle, blindfire-weapon-accuracy publishes.",
    ),
    0x14135D2B0: (
        "PlayerHudScreen_WeaponAmmoPublisher",
        "Per-screen weapon HUD publisher. Reads screen index at state+0xF8, "
        "selects g_PlayerHandleForHudPublish[2*screen], reads the active "
        "ProcessWeapon through vtable +0x840 total and +0x848 magazine, publishes "
        "selected-weapon-id/total-ammo/loaded-ammo/show-weapon-hud?, and spawns "
        "the process-global ss-hud-weapon. Screen 1 must be gated in single-viewport co-op.",
    ),
    0x1413B42E0: (
        "PlayerHudScreen_ReticlePublishCaller",
        "Fills the per-screen HUD record (flags from the player handle pair "
        "at g_PlayerHandleForHudPublish[2*idx]) and tail-calls "
        "PlayerHudScreen_ReticleProcessPublisher with "
        "&g_PerScreenPlayerHudState[32*idx].",
    ),
    0x1401656B0: (
        "PlayerHud_GlobalHandleUpdateEntry",
        "Frame entry that validates the global player handle "
        "(ptr+id pair at 0x143EEBE80, id checked against object+0xBC), sets "
        "object+0x72=1 and calls PlayerHud_ScreenStatePublishUpdate. A "
        "sibling handle at 0x146A8C838 gets vtable+0x1E0 instead.",
    ),
    # --- Sniper scope reticle (is-scope-visible) path (2026-07-05) ---
    0x140C177F0: (
        "SniperReticleManager_QueryScopeVisible",
        "Resolves the PROCESS-GLOBAL ss-hud-sniper-reticle-manager (string "
        "0x142A03110) via the property registry dword_146466FE0, then queries "
        "its 'is-scope-visible?' property (mgr+168 owner, id mgr+188; property "
        "vfunc +0x208). Returns bool = full-screen sniper scope reticle should "
        "draw. Prologue 48 89 5C 24 08 ... 55. ONLY caller = "
        "ReticleManager_Update. The property is global: it goes true when EITHER "
        "the local player OR the co-op Second player enters scoped-rifle ADS, so "
        "on the single physical viewport a Second-player scope leaks here. The "
        "co-op mod detours this (kScopeVisibleQueryRva 0xC177F0) and forces 0 "
        "when the LOCAL player is not scoped (Player+0x1548==0).",
    ),
    0x140C193A0: (
        "ReticleManager_Update",
        "Per-frame reticle/HUD manager update in the main HUD chain "
        "(HudUpdate_Main 0x140C149A0). Operates on the primary screen: reads "
        "g_PlayerHandleForHudPublish (local player) and ResolveScreenRouteEntity"
        "ByIndex(0). Sole caller of SniperReticleManager_QueryScopeVisible "
        "(0x140c19abc) — the scope reticle draw is gated on that query result.",
    ),
    0x140C149A0: (
        "HudUpdate_Main",
        "Main per-frame HUD update: evaluates Joypad commands "
        "(Joypad_IsCommandActive), resolves the strafe/aim camera "
        "(CameraControlStrafe vtable off_143054560), and calls "
        "ReticleManager_Update. Reads g_LocalPlayerInputManager_Ptr HUD state.",
    ),
    # --- Co-op aim/shot direction source (2026-07-05) ---
    0x140E2C730: (
        "PlayerFireController_Fire",
        "Confirmed live campaign fire core (game/player/player-fire.cpp). Owner "
        "controller at arg+0x108; weapon passed separately. AIM/SHOT DIRECTION "
        "is resolved per the FIRING PLAYER's screen index: "
        "ResolveScreenRouteEntityByIndex(*(controllerOwner+0x4010)) then "
        "SelectPreferredCameraChildFromRouteEntity(...,0), checking the child "
        "descriptor at +0x1A8 against off_143054560 (aim/strafe camera kind) "
        "(e.g. line ~999 unzoomed-sniper-shots branch, and the main direction "
        "path). So a co-op Second player (screen index 1 at +0x4010) fires along "
        "its OWN screen-1 aim camera, which the mod does not drive -> wrong "
        "direction. The transform is read via CameraManager_GetBlendedCamera"
        "Transform from g_PerScreenCameraTransformCache[0x400*idx] / a blended "
        "child transform (NOT CameraManager+0x1B0). The mod only blends peer "
        "camera yaw into the body quat (Player+0x140); the real fix must drive the "
        "screen-1 aim camera CHILD (CameraControlStrafe) orientation.",
    ),
    0x141C06650: (
        "CameraManager_GetBlendedCameraTransform",
        "Copies a screen's complete camera state into the out buffer for "
        "consumers (e.g. PlayerFireController_Fire aim direction). Picks the "
        "active camera child via SelectPreferredCameraChildFromRouteEntity; if a "
        "single camera is active copies g_PerScreenCameraTransformCache "
        "(0x1465FDAA0, stride 0x400 per screen index *(mgr+292)), else builds a "
        "blend from mgr+0x1A0, then CameraState_CopyTransformAndParams -> out. "
        "CameraState_CopyPrefix350 first copies +0x000..+0x34F, then the raw "
        "4x4 transform at +0x350 and params at +0x390 are copied. Screen "
        "unprojection consumes the derived basis prefix +0x00/+0x10/+0x20; "
        "overwriting only raw transform +0x350 after this copy does not rebuild "
        "or alter that already-emitted prefix.",
    ),
    0x141C0B410: (
        "CameraManager_RunCameras",
        "Exact camera-manager.cpp CameraManager::RunCameras source assertions. "
        "After resolving/cross-fading the finite final camera Locator, writes "
        "its position/quaternion and parameters at manager+0x1A0 and calls "
        "CameraFinalInfo_Set to derive complete camera state.",
    ),
    0x141727D50: (
        "CameraFinalInfo_Set",
        "Exact camera-final.cpp CameraFinalInfo::Set source assertion. Copies "
        "and normalizes the incoming Locator position/quaternion, derives the "
        "complete camera basis/projection fields and validates finalCamera.",
    ),
    0x141C06240: (
        "CameraManager_RefreshSnapshot",
        "Exact camera-manager.cpp CameraManager::RefreshSnapshot source "
        "assertion. Refreshes the snapshot object, then copies the 32-byte "
        "manager+0x1A0 final Locator into snapshot+0x60.",
    ),
    0x14014BF00: (
        "CameraState_CopyPrefix350",
        "Leaf copy of exactly 0x350 bytes from source camera state to "
        "destination (+0x000 through +0x34F). Called first by "
        "CameraState_CopyTransformAndParams before the separate raw transform "
        "and parameter tail copies.",
    ),
    0x14014BDA0: (
        "CameraState_CopyTransformAndParams",
        "Copies a complete camera state. First calls CameraState_CopyPrefix350 "
        "for +0x000..+0x34F, then copies the raw 4x4 transform "
        "(+0x350..+0x38F), scalar params (+0x390..), vectors +0x3C0/+0x3D0, "
        "flags and the tail through +0x3F4. Used by "
        "CameraManager_GetBlendedCameraTransform.",
    ),
    # --- Fire ray source chain (2026-07-05) ---
    0x140E2BCD0: (
        "PlayerFireController_BuildFireArgsAndFire",
        "Sole caller of PlayerFireController_Fire. First resolves the active "
        "ProcessWeapon, calls vtable +0x848 GetMagazineAmmo and +0x868 "
        "IsMagazineUnloaded, and returns before Fire when magazine<=0 or unloaded. "
        "Then builds FireArgs: seeds the aim "
        "ray via ResolveScreenRoutePoseSeedOrDefault(&seed, *(owner+0x4010)) "
        "(the firing player's screen index), cooks it through sub_140CADD60 into "
        "the FireArgs buffer, applies cover/strafe context (sub_1410E2950(idx)), "
        "then calls Fire. THE aim-direction entry point for the co-op Second "
        "player (screen index 1).",
    ),
    0x140E16DF0: (
        "PlayerGrenadeController_Construct",
        "Player grenade thrower init: resolves *grenade-default-arc-art* and "
        "*grenade-l1-r1-release-window* settings; owner stored at this+0x108 "
        "(a1[33]), owner link +0x3BD0. Sibling of the fire controller cluster.",
    ),
    0x140E17AC0: (
        "PlayerGrenadeController_ResolveContextSettingsAndPose",
        "Resolves the grenade-aim settings id (*aim-grenade* / *aim-grenade-move* "
        "/ *aim-grenade-cassie* / *zipline-grenade* / cover variants) and the "
        "GrenadeMove move-set for the current movement context, fetching the "
        "camera pose seed TEN times via ResolveScreenRoutePoseSeedOrDefault"
        "(&seed, *(owner+0x4010)) — the SAME per-screen fire-ray source as "
        "PlayerFireController_BuildFireArgsAndFire. Called by the trajectory "
        "solver 0x140E1B000 ('Grenade Settings: %s' debug print). This function "
        "alone is insufficient: UpdateArc and Release re-resolve the same seed "
        "through 0x140E1B550/0x140E229C0/0x140E22B80, so the co-op override "
        "must cover the exact full grenade callsite whitelist.",
    ),
    0x1414115B0: (
        "VehicleCtrl_NotifyPlayerEntering",
        "VehicleCtrl::NotifyPlayerEntering(const DC::VehicleEntryDef*): hashes "
        "'vehicle-entry-def' (0x142926668), resolves controller sub-objects via "
        "0x14142B890(...,2/3), packages 0x150 records, copies the VehicleEntryDef "
        "payload (+0xA8/+0xBC), serializes via 0x1417F8B00, and pushes the entry "
        "record into the live rider actor via actor->virtual+0xD0. The bridge for "
        "co-op vehicle entry (driver/passenger seat).",
    ),
    0x140B54F30: (
        "ScriptCmd_NetTaskComplete_MpOnly",
        "'net-task-complete!' handler — MP-ONLY: gated on the net-mode flag "
        "HIBYTE(dword_143E07470); its 'local' helper sub_140A5D3F0 is actually "
        "SendNetEventStartNetTask. Does NOTHING in campaign. This is the MP "
        "objective/valve layer, NOT the campaign progression path (see "
        "GameTaskGraphMgr_Update). Do not use for co-op campaign task sync.",
    ),
    0x141CB9820: (
        "GameTaskGraphMgr_Update",
        "GameTaskGraphMgr::Update (per-frame): drains and applies the campaign "
        "task-graph COMMAND list at g_GameTaskGraphCommandList (0x146A514A8, count "
        "0x146A51568, stride 0x28), advancing active subnodes (objectives). This "
        "IS the campaign progression backbone (script 'task-complete' issues "
        "commands via AddCommand). Co-op Option 1 follower-apply target: replay "
        "the driver's relayed task-graph commands here. STATIC owns recursive "
        "global146A51570/depth146A51580, same singleton+200 lock as Reset; "
        "token1415C5E50 follows native execution/FLS identity. This lock alone "
        "does not establish TaskManager active ProcessTask quiescence.",
    ),
    0x141CB95B0: (
        "GameTaskGraphMgr_AddCommand",
        "GameTaskGraphMgr::AddCommand (find-or-add). Searches g_GameTaskGraph"
        "CommandList (base 0x146A514A0, count g_GameTaskGraphCommandCount, stride "
        "0x28) for a matching 40-byte command; if absent appends it (assert "
        "'m_numQueuedTaskCommands < kMaxQueuedTaskCommands', kMax=5) copying "
        "record fields +0..+36. Record schema: +0 TYPE (1/2/3 = kinds; built by "
        "wrappers 0x141CB9700/9760/97C0), +4 subnode id (*(subnode+8)), +8 graph "
        "id (*(graph+8)), +12 param, +16 (-1 for types 2/3), +24 qword, +32 byte, "
        "+36 int. Co-op Option 1 follower-apply: build the driver's relayed "
        "command record and call this; GameTaskGraphMgr_Update then applies it.",
    ),
    0x141CB9700: (
        "GameTaskGraphMgr_AddCommand_Type1",
        "Typed AddCommand wrapper: builds a TYPE-1 task-graph command record from "
        "(graph a2, subnode) with param a4, then calls GameTaskGraphMgr_AddCommand.",
    ),
    0x141C8E680: (
        "TaskManager_UpdateAndQuery",
        "Large task/objective manager (0xF29) over the global task state at "
        "dword_146DAE7F0 (task-play-time / task-complete records dword_146DAE7F8"
        "..+0x14). Campaign progression backbone: scripted scenes and SsInstances "
        "gate on task state, so syncing tasks advances co-op narrative at the "
        "right level.",
    ),
    0x1408B1350: (
        "ScriptQuery_InteractableHasBeenInspected",
        "'interactable-has-been-inspected?' query core. Resolves the interactable "
        "by StringId, calls vtbl+0x248 to get its state object, returns "
        "state+0xD0 bit 1 = inspected flag. Co-op (story-authority Option 1): "
        "inspect/interact sync = relay {interactable StringId, kind} and set this "
        "flag on the follower's matching object (StringId is deterministic across "
        "copies) — no net session / NetEventInteractable needed.",
    ),
    0x140A45E80: (
        "NetEventEmitProjectileBullet_Execute",
        "Applies a received emit-projectile-bullet net event: reads campaign-valid "
        "globals (g_LocalPlayerInputManager_Ptr, g_PlayerHandleForHudPublish, "
        "g_PerScreenPlayerHudState), maps the weapon via "
        "NetGameManager_GetWeaponIdByStatIndex (static cached table, not a session "
        "singleton), spawns the real ProjectileBulletInstant + net-fire-effect. "
        "Not inherently session-locked except shooter resolution; documents the "
        "native bullet-replication primitive.",
    ),
    0x140A81050: (
        "NetGameManager_GetWeaponIdByStatIndex",
        "net-game-manager.cpp:534 inverse compact stat-index -> weapon-id map. "
        "Pure static lookup: GetCachedWeaponDryFireEventRegistry + "
        "g_StandardWeaponArtIds prefix + cached online weapon def map for "
        "index>=19. No session singleton deref (campaign-available).",
    ),
    0x14073E9C0: (
        "Camera_ResolveRemapByModeAndId",
        "Resolves a camera-remap entry by StringId from an authored registry "
        "table, selecting the table by mode: *net-camera-remaps* when net-mode "
        "(HIBYTE(g_NetModeFlags)), else default / *dlc-camera-remaps* / "
        "*devmenu-camera-remaps*. Confirms camera remaps are StringId-deterministic "
        "and natively networked in MP. Basis for co-op puzzle-camera spectate: the "
        "same remap resolves object-anchored on both copies.",
    ),
    0x140A162C0: (
        "NetCharacter_EventHandler",
        "NetCharacter::EventHandler — dispatches NetCharacterSsAnimateEventContext "
        "and calls NetAnimCmdGenerator_AddSnapshot. Confirms the native animation "
        "snapshot CAPTURE is bound to NetCharacter (a net-session object): in "
        "campaign the local Player is not a NetCharacter, so no snapshots are "
        "generated natively — full NetSnapshot animation sync needs a standalone "
        "capture against the Player's AnimControl. The APPLY half "
        "(ProcessSnapshot/UpdateFlagsAndWeapons) runs on a plain NdGameObject.",
    ),
    0x1408AC710: (
        "ScriptState_SuppressInteractableSystem",
        "'suppress-interactable-system' get/set (fact-backed, byte_1437E5A82). "
        "Gates the whole interactable system. Co-op: set on the FOLLOWER so its "
        "use/interact button never starts inspections/puzzles/pickups/conversation-"
        "pieces/treasure — the narrative-trigger disable for the non-driver "
        "(pairs with the joypad per-command disable for other action triggers).",
    ),
    0x14006D4D0: (
        "ScriptCmd_InteractableCameraRemapEnable",
        "'interactable-camera-remap-enable' handler (sibling ...-disable): turns "
        "on the interaction/puzzle camera remap (framed *remap-camera-int-* shots) "
        "for the interactable. The scripted 'puzzle camera'; in co-op the "
        "spectating follower shows it via the synced scene (object-anchored remap) "
        "or a screen-0 camera-transform relay during the spectate window.",
    ),
    0x1406B14D0: (
        "Npc_TeleportIntoVehicleSkill",
        "Npc TeleportIntoVehicle skill ('TeleportIntoVehicle: Teleporting %s into "
        "the car'; checks '%s is sitting in the car'). Teleports an Npc straight "
        "into a vehicle seat (no pathing) and runs the native enter (seat AP bind "
        "+ enter anim + role). RECOMMENDED path for a co-op passenger button since "
        "the puppet is an Npc — resolves the seat AP and does bind/anim/role for "
        "us; may need the puppet skill/behavior system momentarily active.",
    ),
    0x1403946D0: (
        "VehicleCtrl_BuildAddPassengerEvent",
        "Builds the 'add-passenger' event (vehicle-controller family "
        "0x140394B20..0x140395560): assigns the passenger role to a rider on the "
        "vehicle controller. set-driver is the driver counterpart; passenger-"
        "entered fires on completion.",
    ),
    0x1400BAB30: (
        "ScriptQuery_CarGetRiderByRole",
        "'car-get-rider-by-role' handler: returns the vehicle's rider actor for a "
        "role (driver/passenger). Confirms the vehicle owns its riders by role; "
        "riders bind to the seat via apReference + bindSpawner so a seated "
        "passenger follows the vehicle transform natively (co-op: only the vehicle "
        "object needs syncing).",
    ),
    0x14059D740: (
        "VehicleCtrl_BuildPassengerEntryState",
        "Passenger-seat entry state builder: hashes 'hero-passenger-a-enter-front' "
        "(0x142965D18) into controller state +0xA0, writes role at +0xB0 and an "
        "entry-state flag bundle at +0x12C, and commits target-actor entry "
        "transforms around +0x50/+0x70/+0x80. Best hook for seating the co-op "
        "Second player as passenger (with add-passenger/passenger-entered "
        "0x140394B20..0x140395560 and set-driver for the driver).",
    ),
    0x140D9C700: (
        "Vehicle_PlayerDrivingStateUpdate",
        "Publishes/updates the 'player-driving' state (car anim overlay / IGC "
        "driving flag) for the driving player. Part of driveable-car-controller / "
        "driveable-vehicle-controller. In two-copy co-op the driver is local on "
        "its own copy, so this runs natively there; the passenger copy only "
        "relays the vehicle object transform.",
    ),
    0x140BC9730: (
        "NetEventInteractable_ClusterAnchor",
        "One of the game/net/net-event/net-event-interactable.cpp functions "
        "(cluster ~0x140BC97xx). The engine's native replicated INTERACTABLE "
        "net-event type (sibling files: net-event-melee/npc/player/weapon/"
        "character-move/connection). Candidate transport for co-op inspection/"
        "interaction sync (story-authority model). Needs a standalone-apply check "
        "(works without a live net session?) before reuse.",
    ),
    0x141213C40: (
        "ScriptQuery_GetPlayerMeleeTarget",
        "'get-player-melee-target' handler. Reads the LOCAL player "
        "(g_PlayerHandleForHudPublish) -> player+0x1F60 (combat/melee "
        "controller) -> virtual +0x1C8 -> result+0xD8 = current melee target "
        "entity id. Target is chosen locally by melee-attack-rater (sub_1411F7370) "
        "from nearby enemies, not networked. Co-op: the Second player melees via "
        "its own controller over its local enemy set, so correct melee needs "
        "enemy position sync, not a melee event channel.",
    ),
    0x1411F7370: (
        "MeleeAttackRater_RateTargets",
        "melee-attack-rater.cpp core (0x1861). Rates candidate melee targets by "
        "geometry/angle/distance; shared by player and NPC melee behaviors "
        "(many callers). Produces the locally-selected melee target stored on the "
        "combat controller (player+0x1F60 -> +0xD8).",
    ),
    0x141279F10: (
        "MeleeCommandQueue_QueryMeleeSquare",
        "PlayerMeleeController vtable +0x210. Resolves melee-square (or the MP "
        "hold/release variants) and forwards the hash to queue validator +0x1F8. "
        "PlayerMoveStick uses a true result to enter the free melee animation path.",
    ),
    0x141275910: (
        "PlayerMeleeController_TryAttack",
        "Source literal PlayerMeleeController::TryAttack. Builds a target-rating "
        "request, calls MeleeAttackRater_RateTargets, and commits the selected "
        "attack through controller virtuals +0x208/+0x1D8 only on success.",
    ),
    0x1410F5740: (
        "Player_TryConsumeQueuedMeleeActions",
        "Shared Player action dispatcher. Reads Player+0x1F60, queries square, "
        "triangle, R1, and circle through vtable +0x210/+0x218/+0x220/+0x228, "
        "then routes accepted R1 commands to PlayerMeleeController_TryAttack.",
    ),
    0x1410FC9F0: (
        "Player_TryStartFreeMeleeAnimation",
        "Sole code caller is PlayerMoveStick_MainUpdate at 0x140F07A0A after "
        "MeleeCommandQueue_QueryMeleeSquare succeeds. Applies explicit feature, "
        "network-mode, action-state, and Player+0x1DE0 controller gates before "
        "dispatching Player animation decision/publish virtuals.",
    ),
    0x1402E6E10: (
        "ProcessExplosion_Init",
        "ProcessExplosion::Init (game/process-explosion.cpp): initializes the "
        "explosion process spawned by a detonating projectile/grenade (siblings "
        "Active::Enter/Update, ApplyDamageToObject, CalculateDamageToApply, "
        "spawn-explosion-effects-locator). Spawned via the process system (no "
        "direct callers). In co-op, the Second player's natively thrown grenade "
        "detonates per copy -> native explosion visuals; no relay layer needed. "
        "Debug: 'Control Grenade Explosions Manually', 'Spawn Test Explosions'.",
    ),
    0x140E1B000: (
        "PlayerGrenadeController_SolveTrajectory",
        "Grenade arc/trajectory solver: resolves settings via 0x140E17AC0, "
        "broadcasts gravity/speed scalars from settings +0x18/+0x1C/+0x20 into "
        "SIMD and integrates the arc; cached solver state at this+0x3D0 "
        "(a1[122]). Six callers = arc preview draw + toss commit paths "
        "(0x140E19600, 0x140E1F440, 0x140E1F5B0, 0x140E1FB50, 0x140E23920, "
        "0x140E24A00).",
    ),
    0x141C077B0: (
        "ResolveScreenRoutePoseSeedOrDefault",
        "CameraManager::GetHandle(idx) semantics (assert string, table "
        "0x14697AD50). blk = *(mgr+0xB0); requires gate bytes blk+85 && blk+86 "
        "and a dword_14697AE34 bit inside the bitset at *(blk+8)+32; then copies "
        "the 32-byte POSE SEED at blk+0x60 = [position float4][orientation quat "
        "float4] to out. Gate failure -> default pos(0,0,0) + identity quat "
        "(xmmword_1428805D0). This seed is the FIRE RAY origin+direction consumed "
        "by PlayerFireController_BuildFireArgsAndFire — the co-op Second player's "
        "screen-1 seed is never driven by the mod (wrong-direction shots). Co-op "
        "fix: hook here and override quaternion-only for idx==1 from the peer's "
        "relayed screen-0 seed. Grenade scope is the exact 17 return-RVA "
        "whitelist spanning ResolveContext, SolveTrajectory, UpdateArc and "
        "Release; firearm sites E268EA/E2BFFF/E2C450 remain separate.",
    ),
    0x140E8F990: (
        "GunMoveController_ApplyCameraYawToMoveDirection",
        "GunMoveController vtable +0x38. Resolves the owner's screen-route camera, "
        "rotates the supplied direction by camera yaw, normalizes it, and writes "
        "the world-space result to GunMoveController+0x150.",
    ),
    0x140E86980: (
        "PlayerGunMove_GetConfiguredMoveSpeed",
        "Player::GunMoveShield vtable slot +0xD0, called by "
        "PlayerGunMoveShield_UpdateAndDispatch before publishing speed-gunmove. "
        "Reads the spawn-initialized Player+0x3328 nominal speed, scales it by "
        "the active GunMove configuration and reduces it by shield stoppage. "
        "Player+0x3328 is not a live gait-speed output.",
    ),
    0x140E8D140: (
        "GunMoveController_BuildActionState",
        "Builds the authored GunMove child action for selector states 0..4/103. "
        "The strafe-family branches load the owner Player from controller+0x108 "
        "and snapshot Player+0x100..+0x14F, including local-root quaternion "
        "Player+0x140, into player-ap-strafe-* action arguments.",
    ),
    0x140E8E830: (
        "GunMoveController_InitializeActionState",
        "GunMoveController vtable +0x28. Initializes the child action by calling "
        "GunMoveController_BuildActionState with state 0 (or 103 under the global "
        "mode byte), then clears movement magnitude/state fields. This is the "
        "initial builder path used when GunMove is entered.",
    ),
    0x140E8EE90: (
        "GunMoveController_UpdateAimMovementDirection",
        "Normalizes the supplied GunMove movement vector, stores its adjusted "
        "form at controller+0x130, normalized direction at +0x140 and magnitude "
        "at +0x160. The later +0x38 camera-yaw method writes +0x150. Non-zero "
        "movement selects moving child states 0/1 from +0x140/+0x150; stationary "
        "turn states 3/4 are a separate yaw-rate-gated branch.",
    ),
    0x140E8F720: (
        "GunMoveController_SelectAimIdleTurnState",
        "Consumes GunMoveController+0x140/+0x150/+0x160 and current child state. "
        "Non-zero movement returns states 0/1. At zero movement, authored move "
        "settings+0xF4 and active anim-state yaw-rate +0xD4 > 0.1 gate turn "
        "continuation states 3/4; otherwise it returns aim-idle state 2.",
    ),
    0x140E8FD00: (
        "GunMoveController_UpdateTurnYawFromDirection",
        "Reads GunMoveController+0x150, computes and smooths target yaw against the "
        "owner Player runtime world basis, writes the yaw quaternion through the "
        "Player+0x100 transform helper, commits via runtime vtable +0x208, and "
        "publishes absolute normalized yaw rate to active anim-state+0xD4. "
        "That state is [[Player+0x1A0]+0x58]+0x10, not Player+0x1A8.",
    ),
    0x140E90590: (
        "GunMoveController_UpdateActionState",
        "GunMoveController vtable +0x50. Calls the turn-yaw writer for active turn "
        "states except 2/103, selects the next state via 0x140E8F720, and builds "
        "the authored child action via GunMoveController_BuildActionState. The "
        "writer-before-selector order makes active-state+0xD4 turn hysteresis, not "
        "a proven cold aim-idle initiator.",
    ),
    0x140EF7890: (
        "PlayerMovementSettings_ResolveCurrentMoveEntry",
        "Hashes the move channel, resolves the owner Player's current movement "
        "setting id, and returns its 440-byte entry from the normal or Chloe "
        "player-movement-anim-list. GunMove stationary selection reads authored "
        "turn-enable byte +0xF4 from this entry.",
    ),
    0x1415182E0: (
        "ProcessWeapon_PrepareFireFromArgs",
        "ProcessWeapon vtable +0x870. Copies the 0xA0-byte FireArgs payload to "
        "ProcessWeapon+0xB00. It does not own final projectile direction; "
        "downstream emission overwrites FireArgs+0x10 and applies spread.",
    ),
    0x1414FE050: (
        "ProcessWeapon_ResolveMuzzleTransform",
        "ProcessWeapon vtable +0x8C8. Resolves the authored muzzle or "
        "silenced-muzzle attachment from ProcessWeapon+0x1E0 through "
        "0x1418B7060; falls back through 0x141686030 to the weapon bound at "
        "+0x100. This is the native producer of the launch-origin transform.",
    ),
    0x141515860: (
        "ProcessWeapon_UpdateMuzzleAndDispatchFire",
        "ProcessWeapon vtable +0x410. Calls muzzle-transform slot +0x8C8 at "
        "0x141515A17, stores the 32-byte result to ProcessWeapon+0x970/+0x980 "
        "at 0x141515A22, then reaches ProcessWeapon_BuildAndDispatchProjectile.",
    ),
    0x141518FC0: (
        "ProcessWeapon_BuildAndDispatchProjectile",
        "Consumes ProcessWeapon's FireArgs copy, reads the native muzzle origin "
        "from ProcessWeapon+0x970 at 0x141519124, may overwrite direction at "
        "FireArgs+0x10, computes the final origin vector, and dispatches projectile "
        "emission through ProcessWeapon vtable +0x920 at 0x141519773.",
    ),
    0x141519D30: (
        "ProcessWeapon_ProjectileEmission",
        "ProcessWeapon vtable +0x920. Performs the late retained-player direction "
        "rewrite, normalization and spread. For the default branch, record "
        "0x14151B72B is stored at SpawnInfo+0x28; only weapon+0x906 > 1 replaces "
        "that pointer with the later 0x14151BC3E record at 0x14151BE33.",
    ),
    0x141519B50: (
        "ProcessWeapon_SpawnProjectile",
        "Exact embedded signature ProcessWeapon::SpawnProjectile(const SpawnInfo "
        "&, Process **). SpawnInfo+0x28 is the authoritative selected launch-record "
        "pointer. Job-policy bit 0 selects direct materialization at 0x141519C26; "
        "otherwise it submits ProcessWeapon_MaterializeProjectileJob and blocks in "
        "Ndjob_WaitForCounter before returning.",
    ),
    0x141519B20: (
        "ProcessWeapon_MaterializeProjectileJob",
        "Worker callback installed by ProcessWeapon_SpawnProjectile at "
        "0x141519C44. Materializes the selector packet and writes the optional "
        "Process output pointer; nested projectile and Tracer Init execute on this "
        "worker thread.",
    ),
    0x1415B5300: (
        "Ndjob_WaitForCounter",
        "Blocking job-counter wait identified by embedded ndjob::WaitForCounter "
        "assertions. ProcessWeapon_SpawnProjectile waits here after submitting its "
        "materialization callback. Worker branch 0x1415B5419 invokes "
        "Ndjob_CooperativelyWaitCounterOnWorkerFiber, which SwitchToFiber's "
        "at 0x1415D1E43; non-worker branch sleeps/polls. Thread-local mod "
        "state alone is insufficient isolation across these job waits.",
    ),
    0x1415B4200: (
        "Ndjob_ReleaseCounter",
        "Releases a completed job counter back to its pool after clearing the "
        "counter record. Called immediately after ProcessWeapon_SpawnProjectile's "
        "blocking wait.",
    ),
    0x141520750: (
        "BuildProjectileLaunchRecord",
        "Builds the common projectile launch record: source owner handle at +0x0, "
        "world origin R8 duplicated at +0x20/+0x70, and caller-selected direction "
        "R9 duplicated at +0x30/+0x40. Call 0x14151B72B passes post-spread "
        "var_EB8 and remains the "
        "selected single-projectile record; 0x14151BC3E passes normalized "
        "pre-spread var_EA8 and is selected only when weapon+0x906 > 1.",
    ),
    0x141521F00: (
        "ProjectileBullet_InitBaseFromLaunchRecord",
        "Base projectile launch-record setup used by ProjectileBulletInstant::Init. "
        "Copies record+0x20 into the projectile start transform, constructs the "
        "start bound at projectile+0x130, and independently constructs the base "
        "projectile/tracer bound at +0x3A0 from duplicate origin record+0x70. "
        "Tracer materialization is then gated by authored no-tracers state and "
        "WeaponUpgradeTable_GetValueByIndex(..., 0x0D) <= 0 before selecting "
        "the Tracer process at 0x141522541..0x141522592.",
    ),
    0x14152BF20: (
        "ProjectileBulletInstant_Init",
        "Typed by embedded projectile-bullet-instant.cpp assertions. Reads the "
        "launch-record pointer from SpawnInfo+0x28, sets m_initialPosBound from "
        "record+0x20, and sets its endpoint world translation to start_ws + "
        "record+0x30. ProbeKick "
        "later derives its ray from endpoint_ws - start_ws.",
    ),
    0x14014E240: (
        "BoundTransform_GetTranslationWs_A",
        "Conditionally refreshes cached parent +0x10 when the authored table "
        "entry generation matches; null/mismatch retains the cache. It then rotates "
        "TranslationPs by the cached parent quaternion, adds cached parent "
        "TranslationWs, and returns TranslationWs.",
    ),
    0x140186600: (
        "BoundTransform_GetTranslationWs_B",
        "Second identical BoundTransform TranslationWs getter used while building "
        "the ProjectileBulletInstant endpoint.",
    ),
    0x141686120: (
        "BoundTransform_SetTranslationWs",
        "Converts a requested world translation through the inverse cached parent "
        "rotation and stores the resulting TranslationPs at bound+0x30. "
        "Null/mismatched parent key does not establish identity cached parent; "
        "do not substitute world coordinates directly for the local result.",
    ),
    0x1416861E0: (
        "BoundTransform_SetRotationWs",
        "Converts a requested world quaternion through the inverse cached parent "
        "rotation and stores the resulting RotationPs at bound+0x40. "
        "Null/mismatched parent key does not establish identity cached parent; "
        "do not substitute world rotation directly for the local result.",
    ),
    0x14152C9B0: (
        "ProjectileBulletInstant_ProbeKick",
        "Typed by the embedded ProjectileBulletInstant::ProbeKick signature. "
        "Continues collision/probe setup from the instant projectile initialized "
        "with the local spawn launch record. Entry 0x14152C9B0 keeps RAX=entry RSP "
        "live through 0x14152C9E4, so a short absolute entry trampoline is unsafe; "
        "instrument the direct Init call at 0x14152C8A6 instead.",
    ),
    0x1416A5790: (
        "Player_IsCurrentActionStateHash",
        "Reads Player+0xC0, resolves the active state record through two pointer "
        "levels, and compares the authored state hash at [record+0x10]+0x18. "
        "GunMoveShield and its action-set builder use it for exact state names.",
    ),
    0x1410B2480: (
        "PlayerWeaponController_IsShieldDrawn",
        "PlayerWeaponController vtable +0xE0. In network mode returns the cached "
        "controller+0x13C byte; in campaign requires selected slot 1 and a live "
        "weapon whose class hash is ProcessShield.",
    ),
    0x140EFB750: (
        "ScreenPlayer_IsShieldDrawn",
        "Resolves the Player from g_PlayerHandleForHudPublish[screen] and calls "
        "PlayerWeaponController vtable +0xE0. This is a shield-specific query, "
        "not a generic screen-active or weapon-ready predicate.",
    ),
    0x142053820: (
        "PlayerMoveSequencer_EnqueueControllerAction",
        "Unconditionally links the supplied action into the requested "
        "PlayerMoveSequencer channel, copies controller+0x100 into "
        "action+0x1C when needed, stores the controller at action+0x28, and "
        "clears controller+0x100. This is queue insertion, not an admission "
        "predicate.",
    ),
    0x142051EC0: (
        "PlayerMoveSequencer_UpdatePhase",
        "Ticks every enabled action channel for phase 0..3. An action at "
        "+0x18 state 0 is changed to 1 before vtable+0x10 OnStart; later it "
        "dispatches vtable+0x28/+0x30/+0x38/+0x40 and removes state 2/3 "
        "actions through PlayerMoveSequencer cleanup.",
    ),
    0x142051D00: (
        "PlayerMoveSequencer_RemovePlayerMoveAction",
        "Source-identified RemovePlayerMoveAction. Changes lifecycle state 1 "
        "to 2, invokes vtable +0x18 followed by +0x00, unlinks the intrusive "
        "node and calls the deleting destructor at +0x08.",
    ),
    0x141096150: (
        "PlayerGetWeaponInHandAction_OnStart",
        "PlayerGetWeaponInHandAction vtable +0x10. Resolves the retained weapon "
        "descriptor and requests the authored s_weapon-grab 28B0F93B / "
        "s_weapon-draw 1313FB4E path through PlayerWeaponAnimControl vtable "
        "+0x10; this is the multi-frame Draw producer, not the separate 0.1 s "
        "ProcessWeapon attachment clock.",
    ),
    0x141096730: (
        "PlayerGetWeaponInHandAction_PostAnimUpdate",
        "PlayerGetWeaponInHandAction vtable +0x38, dispatched in sequencer "
        "phase 2. Compares PlayerWeaponAnimControl current state with the "
        "expected holster/no-mirror variants and sets action+0x18 to 3 on a "
        "mismatch or completed auto state.",
    ),
    0x141096BB0: (
        "PlayerPutGunAwayAction_OnStart",
        "PlayerPutGunAwayAction vtable +0x10. Resolves and binds the weapon "
        "holster descriptor and normally requests s_weapon-holster DF5B68FC. "
        "When the current state is s_weapon-grab it selects phase 3 and requests "
        "AD73C917 s_weapon-holster^no-weapon, the seven-frame reverse-grab tail, "
        "through PlayerWeaponAnimControl vtable +0x10.",
    ),
    0x141096F00: (
        "PlayerPutGunAwayAction_PreAnimUpdate",
        "PlayerPutGunAwayAction vtable +0x28, dispatched in sequencer phase 0. "
        "Compares the current animation state against s_weapon-holster and "
        "s_weapon-holster^no-weapon variants and sets action+0x18 to 3 on "
        "mismatch.",
    ),
    0x14108C2D0: (
        "PlayerWeaponAnimControl_RequestWeaponState",
        "PlayerWeaponAnimControl vtable +0x10. Requests a weapon state on the "
        "exact 0xE2B990B9 animation layer through a selector fast path or the "
        "direct-fade fallback, and records the requested state at control+0x18.",
    ),
    0x14108D080: (
        "PlayerWeaponAnimControl_GetCurrentWeaponState",
        "PlayerWeaponAnimControl vtable +0x98. Reads the active state id from "
        "animation layer 0xE2B990B9 and returns zero when that layer/state is "
        "not currently available.",
    ),
    0x14108B720: (
        "PlayerWeaponAnimControl_RelocatePointers",
        "PlayerWeaponAnimControl vtable +0xD0 relocation callback. Adjusts "
        "owned pointers that fall inside a relocated memory interval; it does "
        "not consume weapon state or perform attachment mutation.",
    ),
    0x141511470: (
        "ProcessWeapon_RelocateOwnedPointers",
        "ProcessWeapon vtable +0x08 relocation callback. Calls the base "
        "relocation path and rebases owned pointers including +0xAD0 and the "
        "embedded state at +0xBE0 when they fall inside the moved interval. "
        "It does not write the selected attachment index at +0x688.",
    ),
    0x14108D450: (
        "PlayerWeaponAnimControl_UpdateOverlaysChanged",
        "PlayerWeaponAnimControl vtable +0xD8. Consumes the authored "
        "overlays-changed marker and refreshes animation overlay state without "
        "reading the weapon attachment descriptor.",
    ),
    0x14108D780: (
        "PlayerWeaponAnimControl_PublishPropPlayAnimation",
        "Builds and publishes a play-animation event to a resolved weapon/prop "
        "process; this animates the prop but does not reparent it.",
    ),
    0x14108DAC0: (
        "PlayerWeaponAnimControl_UpdatePropAnimations",
        "PlayerWeaponAnimControl vtable +0xE0. Tracks authored per-state prop "
        "animation data and delegates play-animation events for primary and "
        "optional secondary props; it never calls the attachment helper.",
    ),
    0x14108D3B0: (
        "PlayerWeaponAnimControl_RequestStateIfPresent",
        "PlayerWeaponAnimControl vtable +0xC8. Tests a requested state in the "
        "weapon layer and invokes the generic state-transition/fade helper when "
        "present; it has no physical attachment write.",
    ),
    0x14108D120: (
        "PlayerWeaponAnimControl_IsCurrentWeaponStateAutoOrUnavailable",
        "PlayerWeaponAnimControl vtable +0xA0. Queries the authored 'auto' "
        "root-candidate marker on the active WeaponPartial layer; returns "
        "true when the layer/current instance is unavailable. Its real "
        "callers are draw and hand-switch action completion monitors.",
    ),
    0x14108E470: (
        "PlayerWeaponAnimControl_IsCurrentWeaponStateAuto",
        "PlayerWeaponAnimControl vtable +0xC0. Queries the authored auto flag "
        "for the current state in WeaponPartial 0xE2B990B9 and returns false "
        "when the layer is unavailable. Its real callers are reload-action "
        "completion monitors; it does not select an idle pose.",
    ),
    0x14189C970: (
        "AnimStateLayer_CanResolveRootCandidateStateId",
        "Checks whether the current layer state graph can resolve an authored "
        "root-candidate key, then validates the candidate against the current "
        "selector record. Boolean query only; no state or pose write.",
    ),
    0x14189CA20: (
        "AnimStateLayer_HasResolvableCurrentRootCandidate",
        "Queries an authored key such as 'auto' through current state data and "
        "ResolveActorRootCandidateEntryByStateId. Used as an action-completion "
        "policy predicate; it does not request the candidate state.",
    ),
    0x142057380: (
        "AnimActionMgr_Init",
        "Initializes the owner-local animation StateAction manager, its bounded "
        "action storage and factory pointer. Player installs this at +0x1B8.",
    ),
    0x142057BA0: (
        "AnimActionMgr_AnimStateCreate",
        "Creates authored StateActions for a new animation state by calling the "
        "manager factory with action StringId, owner, manager id, layer id and "
        "AnimInstance id, then starts the returned action through vtable +0x10.",
    ),
    0x142057FB0: (
        "AnimActionMgr_OnStateTransition",
        "Finds the active authored action for a state transition and dispatches "
        "StateAction vtable +0x18. Player weapon-holster reaches its derived "
        "one-shot wrapper through this path.",
    ),
    0x142058040: (
        "AnimActionMgr_OnStateEnd",
        "Dispatches StateAction vtable +0x20 at animation-state end. The base "
        "wrapper marks a missed weapon-holster action finished without forcing "
        "its one-shot executor.",
    ),
    0x142058130: (
        "AnimActionMgr_Update",
        "Per-frame authored action update. Calls StateAction vtable +0x28; the "
        "Player weapon-holster wrapper executes after normalized time 0.9.",
    ),
    0x1410FD120: (
        "Player_CreateStateAction",
        "Player AnimActionMgr factory. Case 0xFA06C342 allocates the exact "
        "PlayerHolsterWeaponAnimAction for weapon-holster and records Player, "
        "manager, animation layer and AnimInstance identity in the action.",
    ),
    0x141089C70: (
        "PlayerStateAction_ResolveCurrentWeapon",
        "Resolves the action's current animation layer/state and returns its "
        "ProcessWeapon only when the expected process type bit is present.",
    ),
    0x14108A5D0: (
        "PlayerHolsterWeaponAnimAction_Update",
        "PlayerHolsterWeaponAnimAction derived update wrapper. Executes the "
        "one-shot holster path after current animation time exceeds 0.9.",
    ),
    0x14108A610: (
        "PlayerHolsterWeaponAnimAction_OnTransition",
        "PlayerHolsterWeaponAnimAction derived transition wrapper. A nonzero "
        "transition marker executes the one-shot holster path.",
    ),
    0x14108A640: (
        "PlayerHolsterWeaponAnimAction_Execute",
        "One-shot weapon-holster executor. Resolves the current weapon, calls "
        "PlayerWeaponController vtable +0x3D0, requires exact targSpineProp at "
        "0x14108A796 (a different selected id exits at 0x14108A798), selects "
        "targSpinePropAnim, calls the rebind again, then writes the computed "
        "record-handoff transform to weapon+0x4F0/+0x500 at 0x14108AD4F and "
        "seeds the authored attachment blend before setting weapon+0x8F4 at "
        "0x14108ADC8.",
    ),
    0x1410A9670: (
        "PlayerWeaponController_RebindWeaponToHolster",
        "PlayerWeaponController vtable +0x3D0. The ordinary path builds the "
        "weapon's authored holster binding and passes its attach id to weapon "
        "vtable +0x7A8 while retaining the same Player owner, then copies the "
        "binding's primary transform (+0x10) to weapon+0x4F0/+0x500 at "
        "0x1410A9A65/0x1410A9A99. It does not compare the selected attachment "
        "id after vtable +0x7A8, so a separately scoped endpoint rebind has no "
        "outer targSpineProp postcondition. It never calls ProcessWeapon vtable "
        "+0x7B8 and never writes the current local transform at +0x130/+0x140; "
        "the new selected record is therefore not synchronously recomputed. It "
        "then publishes player-holstered to the ProcessWeapon; the weapon event "
        "handler forwards that event to a valid ProcessWeaponStrap child.",
    ),
    0x1410A9D10: (
        "PlayerWeaponController_RebindCurrentWeaponForHolster",
        "Resolves the controller's current weapon through vtable +0xB0, reads "
        "WeaponArtDefsOnline+0x30, and either calls the exact holster rebind at "
        "vtable +0x3D0 or clears the supplied fallback slot through +0x1A0. "
        "Called by weapon-switch finalization, GiveWeapon, immediate-index, and "
        "the Player-death inventory reset helpers.",
    ),
    0x1410AEBB0: (
        "PlayerWeaponController_ResetInventoryForPlayerDeath",
        "PlayerWeaponController vtable +0x350. The sole exact Player+0x1D88 "
        "dispatch is 0x140D76290 in the Player death reset path. Removes move "
        "actions, preserves the active slot, iterates all 19 inactive inventory "
        "slots, and calls vtable +0x3D0 for every holsterable weapon before "
        "applying the players-drop-weapons rule.",
    ),
    0x140D761B0: (
        "Player_ResetStateForDeath",
        "Player death-state reset called from PlayerDeathAction_OnStart. Stops "
        "active control/action state and invokes PlayerWeaponController vtable "
        "+0x350 at 0x140D76290 before processing death-revenge/loadout rules.",
    ),
    0x140D77880: (
        "PlayerDeathAction_OnStart",
        "Death action entry: marks the action active, resets the owning Player "
        "through Player_ResetStateForDeath, emits all-death or falling-death "
        "VOX, and completes the remaining death-chain bookkeeping.",
    ),
    0x141ED0580: (
        "WeaponAttachment_SelectExistingOwnerPoint",
        "Common weapon vtable +0x7A8 implementation. Searches the current "
        "owner's AttachSystem by StringId and writes the selected byte index to "
        "weapon+0x688 without changing the owner handle.",
    ),
    # --- 2026-07-26: ordinary wall-hang -> climb-up command/action path ---
    0x14130E670: (
        "PlayerEdgeController_IsEdgeJumpAActive",
        "PlayerEdgeController RTTI vtable slot 19. Hashes the authored command "
        "edge-jump-a and calls Joypad_IsCommandActive with edge=0 for the owning "
        "Player joypad. This is distinct from generic jump/jump-hold.",
    ),
    0x14130ED40: (
        "PlayerEdgeController_CanEdgeJumpAWithFacing",
        "PlayerEdgeController RTTI vtable slot 18. Requires authored "
        "edge-jump-a through Joypad_IsCommandActive(edge=0), then checks the "
        "current edge/facing geometry before accepting the upward transition.",
    ),
    0x14130E890: (
        "PlayerEdgeController_PassesDropHoldGeometryGate",
        "PlayerEdgeController RTTI vtable slot 21. Validates the current edge "
        "pair, separation and height/probe conditions before the auxiliary "
        "drop-hold gate may suppress competing edge transitions.",
    ),
    0x14130EBF0: (
        "PlayerEdgeController_IsDropHoldActive",
        "PlayerEdgeController RTTI vtable slot 23. Hashes the authored command "
        "drop-hold and calls Joypad_IsCommandActive with edge=0 for the owning "
        "Player joypad; it is distinct from generic crouch and evade.",
    ),
    0x141315B00: (
        "PlayerEdgeController_StartClimbUp",
        "PlayerEdgeController RTTI vtable slot 57. Removes the current move "
        "actions, selects ledge-climb-up/ledge-climb-up-2 and climb-up variants, "
        "enqueues a PlayerCommonAction through PlayerMoveSequencer, then publishes "
        "player-climb-up. This is the ordinary wall-hang climb-up transition.",
    ),
    0x1412C9000: (
        "PlayerEdgeClimb2AnimAction_Update",
        "PlayerEdgeClimb2AnimAction RTTI vtable slot 7. Advances the climb action "
        "and independently queries authored edge-jump-a with edge=0 before "
        "committing its continuation flag.",
    ),
    0x14132D340: (
        "PlayerEdgeState_RefreshTraversalProbes",
        "Called from PlayerEdgeState_Update before transition selection. Clears "
        "Edge+0x48/+0x49, selects the local-bound or fallback geometry-probe path "
        "from the owning input record, and rewrites those two traversal latches.",
    ),
    0x14132F990: (
        "PlayerEdgeState_Update",
        "Player::Edge RTTI vtable slot 4 and player-edge-state.cpp owner. Selects "
        "ledge variants and calls the PlayerEdgeController climb-up/fall helpers; "
        "it is the outer ordinary edge-state update rather than DragEdge, Perch "
        "or Vault.",
    ),
    0x141334B80: (
        "PlayerEdgeState_TryTransitions",
        "Ordinary Player::Edge transition selector. Reads Edge+0x48/+0x49, "
        "queries PlayerEdgeController slots 18/19 for edge-jump-a, and can call "
        "slots 57/59 to start the corresponding climb transition.",
    ),
    0x14133F7D0: (
        "PlayerEdgeState_IsEdgeJumpAActive",
        "Player::Edge RTTI vtable slot 11. Independently hashes edge-jump-a and "
        "calls Joypad_IsCommandActive with edge=0 for the owning Player.",
    ),
    0x141324450: (
        "PlayerEdgeController_PublishReleaseEdge",
        "PlayerEdgeController RTTI virtual entry referenced at 0x142A74258. "
        "Hashes player-release-edge, marks the resolved current edge process at "
        "+0x72 and dispatches that authored event through its vtable +0xD0.",
    ),
    0x1413330C0: (
        "PlayerEdgeState_HandleDropCommand",
        "Ordinary Player::Edge wall-hang release handler called from Update at "
        "0x141331429. Independently queries authored drop with edge=0 at "
        "0x141333301, 0x141333463 and 0x14133405C, publishes the release state "
        "block, consumes drop, and returns handled.",
    ),
    0x14131F560: (
        "PlayerEdgeController_ApplyReleaseOrGrabTransition",
        "PlayerEdgeController RTTI vtable slot 166. Builds climb-release-edge "
        "or climb-grab-edge actor-state descriptors, dispatches them into the "
        "runtime slot and publishes player-left-edge after releasing.",
    ),
    0x141341A10: (
        "PlayerEdgeController_PublishPlayerLeftEdge",
        "Publishes the authored player-left-edge marker for the resolved edge "
        "actor after the controller's release transition.",
    ),
    0x14133D800: (
        "PlayerEdgeState_StartFallFromEdge",
        "Direct ordinary-edge fall transition called by HandleDropCommand at "
        "0x14133493D. Removes current move actions, constructs a "
        "PlayerFallMoveUpdate, wraps it in PlayerCommonAction, enqueues it at "
        "0x14133DA28 and exits the current Player state.",
    ),
    0x141319E80: (
        "PlayerEdgeController_StartLedgeGrabFall",
        "PlayerEdgeController RTTI vtable slot 120. Removes current actions, "
        "builds the fall/ledge-grab-fall action pair and enqueues it at "
        "0x14131A25E; geometry/break variants occupy adjacent slots.",
    ),
    # --- 2026-08-11: local Nadine weapon-grip static closure ---
    0x141090650: (
        "Player_GetMainAnimStateCurrentPhase",
        "Resolves the owning Player main AnimControl and returns the current "
        "active-state phase at AnimStateInstance+0x168. PlayerWeaponIKAction "
        "uses it for lifecycle and WeaponIKFeather weighting.",
    ),
    0x1418A4CC0: (
        "AnimStateInstance_GetPreviousPhase",
        "Returns AnimStateInstance+0x16C. PhaseUpdate copies current phase "
        "+0x168 into this field before advancing the current phase; command "
        "generation reads +0x168 instead, so this is previous-phase/event "
        "bookkeeping rather than a rendered-pose writer.",
    ),
    0x141090AC0: (
        "Player_UpdateWeaponPartialBlendAndIkInputs",
        "Per-Player weapon-partial/IK prepass. Integrates the per-screen target "
        "and transition duration into the main active-state phase and applies "
        "blind-fire gesture attenuation before PlayerWeaponIKAction_Update. "
        "STATIC: main instance comes from [[Player+0x1A0]+0x58]+0x10; stores "
        "at141091946/1979/1983 target its+0x168. WeaponPartial instance is "
        "looked up separately by E2B990B9; its call1410915D5 queries node-tree "
        "ValidBits, not resnapshot. No direct Hands/WeaponPartial node+0x60, "
        "instance+0x158/+0x164 or layer+0x28 write was found in this function. "
        "Do not use main-phase mutation as proof of those fields changing.",
    ),
    0x140FB1790: (
        "Settings_RegisterHackDisableWeaponPartialsForScreen",
        "Registers the exact per-screen debug setting "
        "hack-disable-weapon-partials. The backing byte defaults to zero and is "
        "indexed by Player screen with a 0xC0 stride; screen 1 is not enabled "
        "automatically.",
    ),
    0x140E3E660: (
        "PlayerFlashlightController_Create",
        "Allocates 0x270 bytes and constructs the PlayerFlashlightController "
        "later stored at Player+0x1FF0.",
    ),
    0x140E3E6A0: (
        "PlayerFlashlightController_Construct",
        "Installs the PlayerFlashlightController vtable and initializes its two "
        "flashlight process tokens, pose state, ArmIkChain and retained effect "
        "handle. This proves Player+0x1FF0 is not a Skill.",
    ),
    0x140E35620: (
        "PlayerFlashlightController_HasActiveHandProcesses",
        "PlayerFlashlightController vtable +0x18. Compares either stored "
        "flashlight process token at +0x1C/+0x24 with the owning Player "
        "GestureController current token and reports whether flashlight hand "
        "presentation is active.",
    ),
    0x140E35AD0: (
        "PlayerFlashlightController_GetAndMarkActiveEffectProcess",
        "PlayerFlashlightController vtable +0xD8. Resolves the retained effect "
        "process at +0x1B8/+0x1C0, validates its generation and marks object+0x72.",
    ),
    0x140CCF6E0: (
        "PlayerBlindFireAimGestureState_Construct",
        "Constructs the 0x30-byte Player-owned blind-fire/aim gesture state "
        "stored by pointer at Player+0x4320; initializes its GestureLayer token "
        "to {-1,-1}, latch to zero and timers to zero.",
    ),
    0x140CCE350: (
        "PlayerBlindFireAimGestureState_SelectGestureId",
        "Selects the authored blind-fire/cover/ledge/rope/slope aim gesture id "
        "for the PlayerBlindFireAimGestureState update.",
    ),
    0x140CCECD0: (
        "PlayerBlindFireAimGestureState_IsActiveOrLatched",
        "Returns true when the stored GestureLayer index/generation token is "
        "still current or when the transient continuation latch at +0x8 is set.",
    ),
    0x140CCED10: (
        "PlayerBlindFireAimGestureState_Update",
        "Starts, advances or releases the selected blind-fire/aim gesture and "
        "stores its actor-local GestureLayer token in the state at +0x0C.",
    ),
    0x140CCFB70: (
        "PlayerBlindFireAimGestureState_AdjustWeaponIKBlend",
        "Adjusts the desired weapon-IK scalar and ramp for active blind-fire "
        "gestures. It does not construct or replace either hand target.",
    ),
    0x140CD0070: (
        "PlayerBlindFireAimGestureState_GetActiveGestureId",
        "Returns the currently active authored blind-fire/aim gesture id from "
        "the Player-owned gesture state.",
    ),
    0x140CD0100: (
        "PlayerBlindFireAimGestureState_IsActiveOrRecentlyActive",
        "Extends the exact active-token predicate with the state+0x28 recent-"
        "activity grace interval.",
    ),
    0x141ECE250: (
        "GestureController_IsLayerTokenCurrent",
        "Validates a stored {GestureLayer index,generation} pair against the "
        "current token returned by GestureController vtable +0x90 and the "
        "selected GestureLayer vtable +0xC0.",
    ),
    0x141ECE2C0: (
        "GestureController_StopGestureByCurrentLayerToken",
        "Validates a GestureLayer token, then dispatches the normal release "
        "request through GestureController +0x78 / GestureLayer +0x28.",
    ),
    0x1420F9020: (
        "GestureLayer_GetCurrentToken",
        "GestureLayer vtable +0xC0. Returns {layer+0x10 index,layer+0x1B0 "
        "generation} while an authored gesture is active, else {-1,-1}.",
    ),
    0x141820BF0: (
        "JointTree_BuildFromRootToNamedLeaves_Varargs",
        "Varargs wrapper that collects named leaf joints and forwards them to "
        "JointTree_BuildFromRootToNamedLeaves.",
    ),
    0x141820E80: (
        "JointTree_BuildFromRootToNamedLeaves",
        "Builds a concrete joint tree from one root to named leaf joints in the "
        "supplied FgAnimData skeleton. WeaponIKFeatherInfo uses the Player "
        "primary skeleton, not the render-look retarget skeleton.",
    ),
}

# Data/global names (set via ida_name on data addresses).
DATA_NAMES = {
    0x146A51570: (
        "g_GameTaskGraphRecursiveLockOwnerToken",
        "STATIC QWORD singleton146A51370+200; GameTaskGraphMgr_Reset and "
        "GameTaskGraphMgr_Update recursively acquire it with execution token "
        "1415C5E50, releasing to0 when depth reaches0. Not an active TaskManager "
        "update counter, resource lease or proof all workers have drained.",
    ),
    0x146A51580: (
        "g_GameTaskGraphRecursiveLockDepth",
        "STATIC DWORD singleton146A51370+210; recursive depth paired with "
        "owner token146A51570 in task-graph Update/Reset. Depth0 observed alone "
        "does not atomically authorize a spawn against concurrent teardown.",
    ),
    0x143D464C0: (
        "g_NadineBuddyTypeFactoryRecord",
        "STATIC registered by14004D930 through TypeFactory_Register1417C4650: "
        "+68 creator1404A0F40, +70 literalNadineBuddy, +10 type hash. A type "
        "registration is not the maintained actor's EntitySpawner owner.",
    ),
    0x1437AC384: (
        "g_HavokThreadTlsIndex",
        "STATIC DWORD loaded by addReference142227B74 beforeTlsGetValue. "
        "Instruction bytes8B0D0A485801 resolve this address. Adjacent lock "
        "1437AC388 is a DIFFERENT QWORD. Live-world retirement must require valid TLS.",
    ),
    0x146467008: (
        "g_ProcessKillAllowed",
        "STATIC byte ProcessMgr+28 checked byKillTree1416A5C43/81 assertion. "
        "Never force it to authorize an unsafe phase; refuse native retirement if0.",
    ),
    0x1464670D0: (
        "g_ProcessTreeRoot",
        "STATIC LOW QWORD is loaded Process* root for141623050 at1413B9FEE. "
        "Pass its VALUE, not address/global weak record; high QWORD type not inferred.",
    ),
    0x1437AC388: (
        "g_HavokReferencedObjectLock",
        "STATIC hkReferencedObjectLock singleton, node142FEA2F0 output slot. "
        "addReference142227B60 loads atB7F; B14900 RBX=0 atB8C proves null "
        "load although this global page is missing from minidump. Shutdown "
        "142232807 can zero it; that writer's execution timing is unproved.",
    ),
    0x142FEA2F0: (
        "g_HavokReferencedObjectLockSingletonRegistration",
        "STATIC singleton node: +0 literal hkReferencedObjectLock142805F78, "
        "+8 factory142228150, +10 next, +18 output-slot pointer1437AC388. "
        "Shutdown142232740 reverse-walks registrations and zeros output slots.",
    ),
    0x1437AD338: (
        "g_HavokSingletonRegistrationList",
        "STATIC Havok singleton registration list traversed by142232740; "
        "includes node142FEA2F0 whose output isg_HavokReferencedObjectLock.",
    ),
    0x146B4541E: (
        "g_HavokClosePending",
        "STATIC byte consumed by141B3F290 at141B3F29F:HavokClose(0), then "
        "cleared at141B3F2A4. GameFramePostRenderUpdate calls pending service "
        "at140166893, allowing singleton shutdown before process exit.",
    ),
    0x146917C14: (
        "g_HavokFinalShutdown",
        "STATIC source-identifiedg_havok.m_finalShutdown inHavokClose141B3FEE0. "
        "FinalGameClose passes1; deferred/world close passes0. Not a general "
        "collision/job-ready or retained-Player lease flag.",
    ),
    0x146917C20: (
        "g_HavokWorld",
        "STATIC primary Havok world pointer. Probe creation140D0AE8C checks "
        "nonnull, but unchanged cached-shape reuse140D0AE21 bypasses this "
        "test and enters addReference via141ED7400. Do not equate shape "
        "existence with initialized world/TLS/singleton lifetime.",
    ),
    0x1437DFD5E: (
        "g_QuitToMainMenuRequested",
        "STATIC byte set by Script_QuitToMainMenu_Request1403480D0. Only other "
        "direct IDB xrefs: read140166811 and clear14016681A in "
        "GameFramePostRenderUpdate. Cleared BEFORE menu task dispatch: not a "
        "world-loaded flag or a teardown/lifetime exclusion lease.",
    ),
    0x1466D0C50: (
        "g_AnimMgrPhasePluginCallback",
        "STATIC first qword of three-callback registry; copied into job+18, "
        "then Process argument5. Distinct from postpose+8 and validation+10.",
    ),
    0x1466D0C58: (
        "g_AnimMgrPostPosePluginCallback",
        "STATIC aligned qword. Default Nd141937D20 writer14177F77C; Game "
        "1406CD220 writers140157E84/14177F7D7. Async141781EE3/EEA and "
        "sync141783173/17A copy it into job+20, then Process argument6. "
        "Cmd24 invokes this copy at141ABAC66. A compare/exchange expecting "
        "the exact Game pointer can publish a reader wrapper; old jobs may "
        "bypass it and queued new jobs may retain it after global restore. "
        "This is not an unload/drain fence; never free the wrapper from "
        "global-pointer restoration alone.",
    ),
    0x1466D0C60: (
        "g_AnimMgrValidationCallback",
        "STATIC third registry qword, initialized null at14177F783, copied "
        "into job+28 then Process argument7. NOT the cmd24 postpose slot.",
    ),
    0x146B49FA8: (
        "g_HavokDestructionProbeJobCounter",
        "STATIC created by KickDestructionProbes141B416A0 /141D7F580 with callback "
        "141B41000. Waited by141B41870 and HavokStep. NOT animation completion.",
    ),
    0x146A5C380: (
        "g_RigidBodyFrameSyncEndWorkBits",
        "STATIC128qwords/8192bits.141CF36B0 marks registered RigidBody+54 index; "
        "141CF7B70 scans work and141F80F80 conditionally clears it. Moving bodies "
        "may retain bits, so not exclusively pending transforms.14010E390 only "
        "memset-initializes it; writers synchronize through146A5C780.",
    ),
    0x143684E64: (
        "g_ArmIkSolveBypass",
        "Nonzero byte makes SolveArmIk return AL=1 without solving or writing "
        "request+20 (1418F0B8C). A global debug/bypass condition, not a "
        "per-owner successful-solve flag; live value must be observed.",
    ),
    0x146904C60: (
        "g_SkinningShaderByFamilyAndMode",
        "6x8 shader-pointer table filled by141876A60 from IngameShaderMgr. "
        "Five ordinary Cs_Skinning families have8 modes; rope onlymode0. "
        "141967ED2..EDD selects8*family+mode using commandbyte0.",
    ),
    0x14386FC79: (
        "g_SkinningPassThroughEnabled",
        "DMENU Pass-through Skinning (T-pose mode) at141B96BA7/BD4. "
        "Adds shader modebit1 in1418830D9; not a dual-quaternion switch.",
    ),
    0x14300B138: (
        "g_SkinningJointCacheThreshold",
        "DMENU Skinning Joint Threshold at141B96C72/CF4, capped310 "
        "at141B96D2C. Compared with bonecount at1418830CB for modebit2.",
    ),
    0x146C71CE0: (
        "g_AnimPackedQuaternionPermutationRows",
        "Four16-byte vpermilps selector rows, materialized by140128020 "
        "from separate32-byte blocks142BF0700/0740. Used by opcode24 "
        "worker142185CF0 at142185EE5 and related packed-quaternion workers. "
        "Rows(3,1,2,0),(0,3,2,1),(0,1,3,2),(0,1,2,3) act on(C,B,A,D).",
    ),
    0x142BF0700: (
        "k_AnimPackedQuaternionPermutationRows01",
        "First32-byte initializer block for146C71CE0: selector0=(3,1,2,0), "
        "selector1=(0,3,2,1). Next32 bytes are unrelated; secondhalf at142BF0740.",
    ),
    0x142BF0740: (
        "k_AnimPackedQuaternionPermutationRows23",
        "Second32-byte initializer block copied to146C71D00: "
        "selector2=(0,1,3,2),selector3=(0,1,2,3).",
    ),
    0x143714A10: (
        "g_NdjobJointBlendJobRegistration",
        "0x20-byte registration record: +0 name AsyncObjectUpdate_JointBlend_Job "
        "at 0x142B37488; +8 callback 0x141A92A00; +0x10 next; +0x18 hash. "
        "Initializer 0x1400F6090 computes name CRC and links into "
        "0x1437E96D8. Distinct from the copied executable job records and "
        "the PE runtime-function row 0x146E9D7E0.",
    ),
    0x143682F4A: (
        "g_AnimCpuPaletteConstantOverrideEnabled",
        "STATIC: nonzero selects Async141A9357F..5E0 constant CPU-palette "
        "overwrite when Fg+230 exists and !(Fg.flags&0x200); Art+34 rows, "
        "48B each, after normal groups and owner callbacks. Also read at "
        "141C01621/141D3C0AD. Broader user-facing option/default not inferred; "
        "not an actual-frame admission and not enabled by this analysis.",
    ),
    0x143682F4B: (
        "g_AnimManagerForceCachedSkeletonPrepass",
        "Nonzero forces PerformAnimBlend's cached-skeleton prepass even when "
        "FgAnimData+0x1A0 equals the current target skeleton at +0x18. The "
        "prepass has its own Process callsite 0x141A921E2 and can consume "
        "groups before the normal inline callsite 0x141A92759.",
    ),
    0x143682FF5: (
        "g_AnimManagerResidualGroupSplitFlags",
        "PerformAnimBlend reads byte2 of this DWORD at 0x141A9238C. Together "
        "with FgAnimData+0x2D1, the underlying skeleton +0x1E word, group "
        "count, Fg+7 bit3 and Fg flags 0x20000 it selects the special one-inline-"
        "group path whose residual mask is queued at 0x141A927D9.",
    ),
    0x1438401BC: (
        "g_AnimMasterTableArtGroupRegistryEpoch",
        "Animation art-group registry change counter, incremented by "
        "LoginArtGroup at 0x1417C9185 and LogoutArtGroup at 0x1417C94A0. "
        "Both writers mutate the table/listeners without an internal lock or "
        "CMPXCHG. The counter can reject a completed registry change, but it is "
        "not a residency pin and cannot authorize a live borrowed-clip read.",
    ),
    0x1438401CC: (
        "g_SkelTableRegistryEpoch",
        "Independent skeleton-registry lifecycle counter incremented by "
        "SkelTable_LoginArtItemSkeleton and SkelTable_LogoutArtItemSkeleton. "
        "It is distinct from g_AnimMasterTableArtGroupRegistryEpoch; freshness "
        "checks need this epoch and borrowed dereferences still require the lock.",
    ),
    0x1438401D8: (
        "g_SkelTableRetargetEntryCount",
        "Live number of 0x40-byte runtime skeleton-retarget entries in "
        "g_SkelTableRetargetEntries. SkelTable_FindRetargetEntry reads this "
        "bound; each entry's resolved record count is a separate u16 at +0x20.",
    ),
    0x143864A00: (
        "g_SkelTableRetargetEntries",
        "Live fixed-capacity skeleton-retarget entry array, stride 0x40. "
        "STATIC ADF:256 entries ending143868A00, where a distinct draw array "
        "starts.4096 was a generic source-span limit, not legal table capacity. "
        "BuildRetargetEntries populates it from skel-retargets.bin; exact P99 "
        "Chloe-order evaluation uses rows 20 (Player to Chloe) and 157 "
        "(Chloe to legacy Nadine) only after FindRetargetEntry returns the same "
        "array element. Borrowed record/reference snapshots are copied while "
        "g_SkelTableRegistryLock is held and are rejected if either registry epoch "
        "or the three resolver identities changes. STATIC AHC: inline image "
        "storage allows table-only page observations across Find/Resolve within "
        "one existing locked invocation; reload count and request its full "
        "extent every time. Fresh scope after unlock. Package callbacks can "
        "still rebuild numerical payloads in place; epoch equality is NOT a "
        "value/lifetime certificate. Exact owned-value admission remains. "
        "ISOLATED AHD cost caveat: the offline whole-image RX MEM_PRIVATE "
        "mapping reports ~48.8 MB query regions here; query-only separator "
        "page control changes ~132us to ~2us then restores it. This is a "
        "fixture-layout effect, NOT FindRetargetEntry CPU or live1ms evidence. "
        "Do not weaken checks or alter production protections based on it. "
        "ISOLATED ADE: nonexecuting file-backed MEM_IMAGE control gives "
        "2.7..2.8us median despite RegionSize58601472 (PAGE_READONLY). "
        "Backing/protection/geometry differ: not backing-only causality or "
        "a measured complete/live preparation budget.",
    ),
    0x1466EC3E0: (
        "g_SkelTableRegistryBuckets",
        "Pointer to the 384-bucket skeleton-registry hash table. The initialized "
        "shape includes an additional sentinel slot at buckets[384].",
    ),
    0x1466EC440: (
        "g_SkelTableRegistrySentinel",
        "Skeleton-registry sentinel node pointer; initialized shape requires "
        "g_SkelTableRegistryBuckets[384] to equal this value.",
    ),
    0x1466EC470: (
        "g_SkelTableRegistryBucketCount",
        "Skeleton-registry bucket count. The pinned retail build initializes it "
        "to 384 before publishing the initialized byte.",
    ),
    0x1466EC474: (
        "g_SkelTableRegistryInitialized",
        "One-byte initialized flag for the skeleton registry. Borrowed lookup "
        "snapshots fail closed unless it is exactly one under the registry lock.",
    ),
    0x1466EC4B0: (
        "g_SkelTableRegistryLock",
        "64-bit registry lock with free value 0 and owned value -1. Login/Logout "
        "spin using LOCK CMPXCHG and release with atomic XCHG; runtime probes use "
        "one nonblocking InterlockedCompareExchange64 attempt and never spin. "
        "STATIC AHA: protects skeleton logout invalidation, NOT separate "
        "AnimMaster group lookup lifetime or package-observer stateC retarget "
        "rebuild1406FBB40->1417CDDC0. That path can rewrite existing record "
        "payloads without taking this lock or advancing registry epochs.",
    ),
    0x14386FD00: (
        "g_AuthoredTransformPointerTable",
        "Shared inline qword pointer table indexed by an owner's authored key at "
        "+0x108. When the selected entry's generation DWORD at +0x50 matches owner "
        "+0x10C, its first 0x20 bytes refresh cached parent +0x110. Null/mismatch "
        "retains that cache, which stock paths still compose with local +0x130. "
        "Also indexes registered RigidBody entries for FrameSyncEnd141CF7B70; "
        "the table is not exclusively authored objects or enemy transforms.",
    ),
    0x146905C58: (
        "g_ArmIkJointNameIds",
        "Ten runtime-initialized StringIds ordered as left "
        "{root,shoulder,elbow,wrist,hand_prop_attachment} then the matching "
        "right-side five. WeaponIKFeather resolves them inside the current "
        "Player skeleton and never treats the array as fixed skeleton ordinals.",
    ),
    0x142A35B38: (
        "PlayerRunNGunController_vftable",
        "Exact PlayerRunNGunController vtable for the special run-and-gun/"
        "blind-fire locomotion controller stored at Player+0x1E40.",
    ),
    0x142A23EA8: (
        "PlayerFlashlightController_vftable",
        "Exact RTTI PlayerFlashlightController vtable installed by "
        "PlayerFlashlightController_Construct; +0x18 checks active flashlight "
        "hand processes and +0xD8 resolves the retained effect process.",
    ),
    0x143073319: (
        "g_PerScreenHackDisableWeaponPartials",
        "Screen-0 byte of the 0xC0-stride per-screen "
        "hack-disable-weapon-partials setting. The registered default is zero; "
        "screen 1 is at +0xC0.",
    ),
    0x142A9A630: (
        "ProcessWeaponBase_vftable",
        "Exact MSVC RTTI ProcessWeaponBase vtable. COL 0x142CD85D8 names "
        "ProcessWeaponBase; slot +0x70 is ProcessWeaponBase_EventHandler.",
    ),
    0x142A9BF48: (
        "ProcessWeapon_vftable",
        "Exact MSVC RTTI ProcessWeapon vtable installed by ProcessWeapon_Ctor; "
        "slot +0x70 is ProcessWeapon_EventHandler.",
    ),
    0x142A465C8: (
        "PlayerDrawWeaponAnimAction_vftable",
        "RTTI-backed PlayerDrawWeaponAnimAction vtable; +0x48 resolves and "
        "commits the current weapon through PlayerWeaponController +0x248.",
    ),
    0x142A46630: (
        "PlayerGrabWeaponAnimAction_vftable",
        "RTTI-backed PlayerGrabWeaponAnimAction vtable. Its ProcessWeapon+0x8F4 "
        "toggle is conditional on the spine-prop attachment and is not the "
        "cold long-gun Draw commit.",
    ),
    0x142A46748: (
        "WeaponHolsterIkBlender_vftable",
        "Exact MSVC RTTI WeaponHolsterIkBlender vtable. Slot +0x10 is "
        "WeaponHolsterIkBlender_ExtractData 0x14108B120 and slot +0x18 is "
        "WeaponHolsterIkBlender_BlendData 0x14108B580.",
    ),
    0x142B14D68: (
        "AnimSnapshotNodeAnimation_vftable",
        "Exact MSVC RTTI AnimSnapshotNodeAnimation vtable; slot +0xC8 is "
        "AnimSnapshotNodeAnimation_SampleNamedReference 0x141918820.",
    ),
    0x142B15218: (
        "AnimSnapshotNodeBlend_vftable",
        "Exact MSVC RTTI AnimSnapshotNodeBlend vtable; slot +0xC8 is "
        "AnimSnapshotNodeBlend_SampleNamedReference 0x14191F220.",
    ),
    0x142BBECC0: (
        "GestureApplyJointReaderFunc_vftable",
        "Exact MSVC RTTI Gesture::ApplyJointReaderFunc vtable installed by "
        "Gesture_GetComputeArgs. Slot +0x8 is "
        "Gesture_GetComputeArgs_BuildStepState 0x141FD3680 and is invoked by "
        "AnimNodeGesture_StepAngles during live post-align processing. The "
        "closure stores Player at +0x28 and its reference-local target vector "
        "at +0x30; +0x30 is not a world-space homogeneous point.",
    ),
    0x142980958: (
        "GestureTargetLookAt_vftable",
        "Unique MSVC RTTI Gesture::TargetLookAt vtable. Slot 0 is "
        "GestureTargetLookAt_GetTarget 0x1406F2360.",
    ),
    0x142AA3238: (
        "Tracer_InitFromProjectileLaunchRecord_VtableSlot",
        "Exact Tracer RTTI vtable slot containing "
        "Tracer_InitFromProjectileLaunchRecord 0x14155BE10.",
    ),
    0x143F041F0: (
        "g_ScreenRouteHudStatePtrs",
        "Per-screen HUD/reticle state pointer table, indexed with stride 0x368. "
        "The routed reticle point uses float pairs +0x30/+0x34, +0x38/+0x3C "
        "and optional +0x40/+0x44; PlayerAimController_ApplyWorldAimBasis writes "
        "the dynamic reticle-offset pair at +0x38/+0x3C.",
    ),
    0x142B09B58: (
        "AnimSimpleLayer_vftable",
        "Exact MSVC RTTI AnimSimpleLayer vtable. CreateAnimCmds is reached through "
        "the type-1 AnimControl dispatcher and step through vtable +0x40.",
    ),
    0x143ECD8E4: (
        "g_FlinchPartialLayerId",
        "StringId initialized from literal FlinchPartial and used by Player setup "
        "for its one named persistent AnimSimpleLayer.",
    ),
    0x142A4BEF8: (
        "PlayerLookAtTracker_vftable",
        "Exact MSVC RTTI PlayerLookAtTracker vtable used by both embedded "
        "Player records at +0x21E0/+0x2260. Slot 0 tests +0x70 active, +0x8 "
        "publishes a world target and +0x10 clears it.",
    ),
    0x142B91B60: (
        "GestureController_vftable",
        "Exact MSVC RTTI GestureController vtable for Player+0x3FD8.",
    ),
    0x142BD3B30: (
        "BaseLayerGestureLayer_vftable",
        "Exact BaseLayerGestureLayer RTTI vtable. Slot +0xD8 is the pre-align "
        "update that dispatches a null custom TargetPoint.",
    ),
    0x142BD4068: (
        "GestureLayer_vftable",
        "Exact GestureLayer RTTI vtable. Slot +0xD8 may dispatch the embedded "
        "object+0x1C0 custom TargetPoint when enabled.",
    ),
    0x142BC16B0: (
        "AnimNodeGesture_vftable",
        "Exact AnimNodeGesture RTTI vtable installed by its constructor.",
    ),
    0x142B9CCC8: (
        "GestureTargetScriptLambda_vftable",
        "Exact Gesture::TargetScriptLambda RTTI vtable. Slot 0 executes the "
        "authored owner-scoped target callback.",
    ),
    0x146C71A10: (
        "g_GestureConfig_Ptr",
        "Process-global Gesture::Config pointer returned by GestureConfig_Get. "
        "The stock game installs g_DefaultGestureConfigInstance; this pointer is "
        "configuration metadata, not a per-Player camera object.",
    ),
    0x143052930: (
        "g_DefaultGestureConfigInstance",
        "Stock Gesture::Config singleton object. Its first qword is "
        "GestureConfig_Base_vftable and it is installed into "
        "g_GestureConfig_Ptr by the game initialization paths.",
    ),
    0x1429809E8: (
        "GestureConfig_Base_vftable",
        "Base Gesture::Config vtable. Slot +0x18 is the optional AimAt weapon "
        "muzzle-reference query and slot +0x20 creates LookAt, AimAt or script "
        "target providers from authored gesture settings.",
    ),
    0x143026D20: (
        "g_AnimJointProgramBuiltinOperatorTable",
        "Built-in skeleton joint-program dispatch table. The resolved non-null "
        "entries include encoded scalar/matrix operators 54 through 59 used by "
        "AnimCmdEvaluator_ExecuteSkeletonJointPrograms.",
    ),
    0x143026CB0: (
        "g_AnimJointProgramCustomOperatorTable",
        "Six-entry custom skeleton joint-program operator table for authored "
        "opcodes 128 through 133. Every handler is closed over its operand stream "
        "and the four evaluator buffers; none reads camera/gameplay globals. "
        "ProcessAnimCmdListSegment stores this exact table at SegmentContext+0x10 "
        "in 0x141AB9B44 and ExecuteSkeletonJointPrograms dispatches slot opcode-128 "
        "through it at 0x14217B724.",
    ),
    0x14300C938: (
        "g_AnimJointProgramCustomOperatorCount",
        "Custom skeleton joint-program operator count. The retail value is six, "
        "covering authored opcodes 128 through 133 in the table at 0x143026CB0.",
    ),
    0x1429B6878: (
        "PickupInteractControl_Vtable",
        "MSVC RTTI .?AVPickupInteractControl@@ vtable. Slot +0xE0 aliases the "
        "shared PickupInteractControl_PublishHudPrompt entry 0x1408AEA00.",
    ),
    0x1429BF4A8: (
        "WeaponInteractControl_Vtable",
        "MSVC RTTI .?AVWeaponInteractControl@@ vtable. Slots identify ordinary "
        "ground ProcessWeapon availability, request, OnInteract, and shared HUD "
        "publication; distinct from WeaponPickupInteractControl.",
    ),
    0x1429BF4B8: (
        "WeaponInteractControl_OnInteract_VtableSlot",
        "WeaponInteractControl vtable +0x10 and sole dispatch reference to "
        "WeaponInteractControl_OnInteract 0x141505BB0. Patching this data slot "
        "preserves the function's original prologue and unwind metadata.",
    ),
    0x142AA6E90: (
        "WeaponPickupInteractControl_Vtable",
        "MSVC RTTI .?AVWeaponPickupInteractControl@@ vtable. Slot +0x10 is "
        "OnInteract, +0x28 RequestInteract and +0xE0 the continuous HUD prompt "
        "publisher.",
    ),
    0x143EF1F20: (
        "FHUD_PhysicalInteractStore_Ptr",
        "Physical FHUD/HudFlash store used by UpdateInteractScriptsAndExpire and "
        "DestroyInteractScript. Four interact records begin at +0x830 with "
        "stride 0x710; refresh bytes begin at +0x820.",
    ),
    0x1429FCF90: (
        "NetEventSyncWeaponPickups_Vtable",
        "Native late-join pickup-consumption event vtable; Apply is "
        "NetEventSyncWeaponPickups_Apply 0x140BCE440.",
    ),
    0x1429FCFD8: (
        "NetEventAddDroppedPickup_Vtable",
        "Native replacement-weapon world-spawn event vtable; Apply is "
        "NetEventAddDroppedPickup_Apply 0x140BCD0F0.",
    ),
    0x142A1DBB8: (
        "PlayerEvadeController_Vtable",
        "MSVC RTTI .?AUPlayerEvadeController@@ vtable. Ordinary run-roll uses "
        "slot 10 evade-start, slot 14 eligibility, slot 15 VehicleCheck and "
        "slot 16 accepted action start; slot 13 evade-roll is a separate command.",
    ),
    0x142A73B90: (
        "PlayerEdgeController_Vtable",
        "MSVC RTTI .?AUPlayerEdgeController@@ vtable. Slots 18/19 query "
        "edge-jump-a; slot 57 starts the ordinary ledge climb-up action.",
    ),
    0x142A75380: (
        "PlayerEdgeState_Vtable",
        "MSVC RTTI .?AVEdge@Player@@ vtable owned by player-edge-state.cpp; "
        "slot 4 is the outer ordinary Player edge-state update.",
    ),
    0x142A707F0: (
        "PlayerEdgeClimb2AnimAction_Vtable",
        "MSVC RTTI .?AUPlayerEdgeClimb2AnimAction@@ vtable; slot 7 updates the "
        "climb animation and queries edge-jump-a with held semantics.",
    ),
    0x142A46560: (
        "PlayerHolsterWeaponAnimAction_Vtable",
        "MSVC RTTI vtable for PlayerHolsterWeaponAnimAction; created by Player "
        "StateAction factory case weapon-holster 0xFA06C342.",
    ),
    0x142BE8DFC: (
        "kAimAlternateLayerReleaseDuration",
        "Float 0.2 used as the fade duration by "
        "AnimParamRegistry_RequestLayerFadeOutAndRelease for Aim0/Aim2/Aim4/Aim6 "
        "when PlayerAimController's resolved branch-control byte is nonzero.",
    ),
    0x142A27658: (
        "PlayerGunMoveShield_Vtable",
        "MSVC RTTI .?AVGunMoveShield@Player@@; UpdateAndDispatch is slot +0x148 "
        "at 0x140E84440.",
    ),
    0x143742720: (
        "g_CachedOnlineWeaponDefinitionMap",
        "Cached handle for *weapon-gameplay-defs-online*. First qword resolves "
        "to a map with count at +0 and authored weapon-id array at +8.",
    ),
    0x143E04A00: (
        "g_StandardWeaponArtIds",
        "Fixed 19-entry authored weapon-id prefix used by both directions of "
        "the NetGameManager weapon stat-index mapping.",
    ),
    0x1437EA62C: (
        "g_InputWindowFocused",
        "Global input-focus byte maintained by Input_UpdateWindowFocusFlag and "
        "WM_SETFOCUS/WM_KILLFOCUS. Both per-player dispatch and "
        "Input_ScaleCopyVirtual require it before native conversion.",
    ),
    0x146A514A0: (
        "g_GameTaskGraphCommandList",
        "Campaign task-graph command list, base address (count "
        "g_GameTaskGraphCommandCount 0x146A51568, kMax 5, stride 0x28 = 40-byte "
        "records: +0 type(1/2/3), +4 subnode id, +8 graph id, +12 param, +16, +24 "
        "qword, +32 byte, +36 int). Appended by GameTaskGraphMgr_AddCommand, "
        "drained by GameTaskGraphMgr_Update to advance objectives. Co-op Option 1 "
        "relays these command records.",
    ),
    0x146A51568: (
        "g_GameTaskGraphCommandCount",
        "Entry count for g_GameTaskGraphCommandList (0x146A514A8).",
    ),
    0x143E07470: (
        "g_NetModeFlags",
        "Net/game mode flags. HIBYTE gates MP-only paths (e.g. "
        "ScriptCmd_NetTaskComplete_MpOnly, net-task verbs) — false in campaign. "
        "Useful to distinguish campaign vs MP session in mod code.",
    ),
    0x1465FDAA0: (
        "g_PerScreenCameraTransformCache",
        "Per-screen camera transform cache, stride 0x400 (64 xmmword) per screen "
        "index. CameraManager_GetBlendedCameraTransform copies entry "
        "[0x400*idx] as the screen's current camera transform when a single "
        "camera child is active. This (not CameraManager+0x1B0) is what "
        "PlayerFireController_Fire reads for aim/shot direction, so the co-op "
        "Second player's screen-1 entry governs where it shoots.",
    ),
    0x143ECEAA4: (
        "g_DebugCheatFlags_AmmoGrenades",
        "Built-in debug cheat flags: byte +1 is Infinite Ammo and byte +3 is "
        "Infinite Grenades. The circular scripted-input branch sets both as "
        "unrelated side effects, not as input activation.",
    ),
    0x143FE1088: (
        "g_LocalPlayerInputManager_Ptr",
        "Pointer to the native local-player/input manager. Per-player input "
        "distribution iterates 6 slots here (Joypad objects at *+0xDB0 + i*0xBF0; "
        "the internal loop cursor is Joypad+0xD8); "
        "per-slot input apply = its vtable+0x20. Slot 0 = active player; slots "
        "1-5 idle in campaign = the dormant native multiplayer input path.",
    ),
    0x1466E4220: (
        "g_InputStateBlocks",
        "Array of 6 per-player input-state blocks (166 qwords / 0x530 bytes "
        "each). Stick normalizer 0x141A99FD0 writes the active pad's normalized "
        "stick to block+0x10/+0x14. Read by the per-player dispatch "
        "0x14179E6C0/0x14179E310.",
    ),
    0x146B2C620: (
        "g_InputFocusConsumerArray",
        "Array of UI/overlay input-focus consumers (count g_InputFocusConsumerCount "
        "0x146B1B55C). First whose vtable+0x30 returns true claims focus "
        "(g_InputFocusCurrent 0x146B2C618). Null in plain gameplay.",
    ),
    0x146B1B55C: ("g_InputFocusConsumerCount", "Count for g_InputFocusConsumerArray."),
    0x146B2C618: ("g_InputFocusCurrent", "Currently-focused UI input consumer (null in gameplay)."),
    0x1437E0250: (
        "g_EngineClock",
        "Pointer to the engine clock. The current 64-bit tick at +0x310, "
        "multiplied by g_EngineClockToSeconds, is the timestamp used by "
        "NetEventCharacterMove and NetAnimCmdGenerator.",
    ),
    0x142BE8AE8: (
        "g_EngineClockToSeconds",
        "Float scale converting g_EngineClock+0x310 ticks to the native "
        "animation/network timestamp.",
    ),
    0x143EEBE80: (
        "g_PlayerHandleForHudPublish",
        "Primary slot of six 16-byte retained player entries: handle64+0, "
        "generation32+8, padding+0xC. Slot0 also serves the legacy HUD global "
        "handle name. Spawn writers copy Process+A8/BC, identical to the Fg "
        "owner pair at +1A8/1B0; ContainsRetainedIdentity compares exactly "
        "12 identity bytes, not the padding. A raw tuple is NOT a lifetime lease. "
        "ISOLATED AHD: owned P99 preparation queries this slot five times, "
        "including final ticket evidence AFTER the seal registry unlock. "
        "That last read must remain fresh. Offline RX MEM_PRIVATE query "
        "latency here is dominated by its broad mapped region; it is not "
        "proof of live owner-read cost or permission to cache across unlock. "
        "ISOLATED ADE: SEC_IMAGE_NO_EXECUTE query-only control yields "
        "2.7..2.8us median with MEM_IMAGE/RO/RegionSize51757056; no native "
        "function or full preparation runs in that view. Different geometry "
        "and protection prevent a backing-only causal claim; live1ms unknown.",
    ),
    0x143EF1E30: (
        "g_PerScreenPlayerHudState",
        "Per-screen player/HUD state array (0x100 stride), indexed by the "
        "screen index at player+0x4010. Target object of "
        "UpdatePlayerHudAimingAndWeaponAccuracyPublishes: is-aiming, "
        "weapon-accuracy, reticle-show-time records are committed into "
        "[screen]+0xF0 block offsets ~+0x3B18/+0x3B30.",
    ),
}


INSTRUCTION_COMMENTS = {
    0x1413C076A: (
        "STATIC E8A111D3FF ->1410F1910, primary routeEDX=0. RCX=local16B "
        "output; R8=Continue held inR13; R14=live TaskManager; sixth argument "
        "is fresh local CharacterPersistentState. Return076F still lies inside "
        "that value's lifetime, unlike replay after the outer function returns."
    ),
    0x1413C076F: (
        "STATIC native primary-spawn return: RAX points to output weak pair; "
        "record/id/record+8/actor+BC checked through0790 before owner+240. "
        "A wrapper must preserve PRIMARY RAX and output, using separate slot1 "
        "scratch. This marker is not a global reset/physics exclusion fence."
    ),
    0x140F73C34: (
        "STATIC E8D7DC1700 ->1410F1910. ScriptSpawnPlayer supplies script route "
        "inESI/EDX, R8=Continue held inR14, RCX=local16B output and sixth input "
        "local CharacterPersistentState built by140F73C90. Return140F73C39 "
        "precedes caller cleanup; output RCX is not the copied descriptor."
    ),
    0x1410F17E9: (
        "STATIC native16B publication of resolved Player+A8/+BC weak pair to "
        "143EEBE80+16*route. Output record/id follow at17FD/1806; nil materialized "
        "Player publishes an empty pair. Never replace this with raw slot pokes."
    ),
    0x14158980F: (
        "STATIC ReadCheckpoint directly memcpy reads1C60-byte checkpoint from "
        "SaveBuffer after tag/bounds checks, then advances cursor; no pointer "
        "rebinding/refcount pass. Inline persistent states can be owned by "
        "value, but captured IDs/locators still require current scene binding."
    ),
    0x1413C26C5: (
        "STATIC exact Nadine maintainer CALL1413BF090, bytesE8C6C9FFFF. "
        "ECX=task-default+A8 descriptor, RDX=hashNadineBuddy C785414E, "
        "R8=nadine-buddy; RDI=live complete TaskManager. Native callee has3 "
        "args/no stack args; private relay may pass RDI in volatile R9 ONLY."
    ),
    0x1413C26CA: (
        "STATIC TEST RAX,RAX then JE26F6: NULL leaves manager+168/+170 "
        "unchanged, skips Nadine changed flag and pair publication only, "
        "then continues Cutter/other companion logic. Not a forged success."
    ),
    0x1413C26EE: (
        "STATIC stores resolved actor+A8/+BC into TaskManager+168/+170. "
        "This maintained weak pair, not actor+150, is the resurrection owner."
    ),
    0x141D22796: (
        "STATIC Spawn arg4 TRUE sets spawner+51 bit08 before materialization; "
        "Object_InitSpawnParentAndSpawner then deliberately omits actor+150. "
        "1413BF090 supplies TRUE for the TaskManager-owned companion route."
    ),
    0x140166884: (
        "STATIC exactE8 37C14B01 ->ProcessMgr_DestroyDeadProcesses1416229C0; "
        "RCX=&ProcessMgr146466FE0 from14016687D, preserveRAX. Unconditional "
        "ordinary postrender seam AFTER jobs/nav/physics waits, BEFORE free/"
        "compaction/Havokclose. Liveworld effects require WORLD->Mgr lock order."
    ),
    0x140166833: (
        "STATIC exactE8 48628500 ->1409BCA80 char(a1,a2,int). RCX=143DC9980, "
        "RDX=1413C80D0 result,R8D=actualR15D. Preserve3args/AL. Explicit menu "
        "leave queues only AFTER original successful nonzero result, not timeout."
    ),
    0x1401667D0: (
        "STATIC wait/release NPC drain callback counter[RBP+20] submitted "
        "140166243:14015C7C0->1403E4AF0->1403E71B0->140420F60. Before166884; "
        "still recheck target-specific navigation counter slots before live retirement."
    ),
    0x1415FDF1F: (
        "STATIC rebind stable allocation record+8 to moved object AFTER memmove. "
        "For Process, +A8 record/+BC generation are copied unchanged. Ownership "
        "must follow record/gen and validateCURRENT native argument, not pin "
        "the captured spawn address (failed B20628 mod matcher)."
    ),
    0x1413C6B1F: (
        "STATIC exact E8 FC2CFFFF ->ActorReset_ResetActors1413B9820, "
        "ECX=reason/EDX=bool, return1413C6B24; AFTER explicit primary kill. "
        "Owned replica repair scopes ONLY this call/reason2, not all direct resets."
    ),
    0x1413B9E8C: (
        "STATIC current-process safety predicate+110, return1413B9E93. "
        "Never force this admission: ResetActors asserts if it would kill caller."
    ),
    0x14029B82C: (
        "STATIC main-menu ProcessTask setsHavokClosePending=1. Consumed at "
        "postrender140166893 after task reset/update and dead-process cleanup."
    ),
    0x1401667C1: (
        "STATIC wait/release queuedHavokStep callback140164F10 counter before "
        "quit/reset140166833 and HavokClose140166893 on ordinary menu path."
    ),
    0x142227B7F: (
        "STATIC movRBX,[g_HavokReferencedObjectLock1437AC388]. B14900 fault "
        "RBX=0 therefore identifies null global lock, NOT a null shape pointer."
    ),
    0x142227B8C: (
        "CONFIRMED2026-10-24 B14900 terminal AV atcmpDWORD[RBX+10],1; RBX=0 "
        "read0x10. Immediately followsTlsGetValue atB86, RAX=0 too. "
        "RVA2227B8C; not ffxGetResourceDX12 despite nearest export in cdb."
    ),
    0x142232807: (
        "STATIC zero *singleton output-slot. Node142FEA2F0 maps this writer "
        "to lock1437AC388. Real shutdown writer, not proof it ran before crash."
    ),
    0x141ED74F9: (
        "STATIC HavokProbeShape retains nonnull supplied shape via142227B60; "
        "return141ED74FE is exact caller in B14900 terminal dump."
    ),
    0x140166893: (
        "STATIC service pending Havok open/close141B3F290 afterProcessMgr "
        "cleanup; close(0) reaches singleton/TLS shutdown without process exit."
    ),
    0x1413B9529: (
        "STATIC reset-kill visitor calls virtual+110 with forwarded reasonRDX. "
        "Player resolves1416A5B50; reason2 rejects signed+3D<0 with+3C&02 clear."
    ),
    0x1413B97BC: (
        "STATIC reset visitor calls virtual+110 with forwarded reasonRDX; "
        "Player1416A5B50 gates admission before composite-body pass2 teardown. "
        "Persistent secondary "
        "need not follow explicit primary kill1410F3DD0."
    ),
    0x1417C3E31: (
        "STATIC ProcessSpawnInfo+48 bit0 copies to Process+3D bit80 afterclear. "
        "CommitResolvedPlayerSpawn initializer1417C32E5 zeros packet+48; no "
        "proof descriptor replay itself supplied B14900's persistent flag."
    ),
    0x1416A5E2A: (
        "STATIC Process+40 m_killFlags.m_enteredOnKill (mask04). Set BEFORE "
        "virtual+190 cleanup1416A5E33; Player vtable/id can still match here. "
        "Mod retained lookup must reject this phase; read is not lifetime lease."
    ),
    0x1416A6044: (
        "STATIC invalidate Process runtime id+BC AFTER OnKill callback and "
        "token-index removal, BEFORE dead-list publication/destructor. "
        "Retained slot pairs need not themselves be cleared to become invalid."
    ),
    0x1417C3D0E: (
        "STATIC correction: mov [rbx+B8],r14d writes descriptor delta, NOT local "
        "runtime id+BC. Base constructor1416A3393 assigns+BC via1416A38A0."
    ),
    0x1417C3E1D: (
        "STATIC32-byte shortened debug name into object+8; long input becomes "
        "14-byte prefix + ellipsis +14-byte suffix. Not a full authored actor key."
    ),
    0x141E9C442: (
        "STATIC owner+698 RigidBody transform queue. Exact Pirate reaches here "
        "via+408/141DE9910. Final publication is FrameSyncEnd under HavokStep, "
        "not this call or phase40 Fg matrix publication alone."
    ),
    0x141D8049A: (
        "STATIC marked-body FrameSyncEnd after146B49FA8 wait/release and before "
        "HavokStepInternal141D80588; dt/InputManager/world gates apply. Counter "
        "is destruction probes, NOT an animation fence. Job dependency unresolved."
    ),
    0x141DEDA20: (
        "STATIC UpdateRootLocator tail:141DEDA11 copies local root, this invokes "
        "upright cleanup141DEB500 with owner+1A0 AnimControl.141DEDA46 stores "
        "normalized resulting local rotation, not a rollback. Base node+108 bit400 "
        "can suppress cleanup. This chain is not physics publication."
    ),
    0x141DE9608: (
        "STATIC local-root +130 store in FALSE virtual+5E8 alternate net-animation "
        "branch, not ordinary campaign Pirate native/controller branch. Do not "
        "use as a network writer without final physics/lifetime/phase proof."
    ),
    0x141DE9610: (
        "STATIC local-root +140 quaternion store paired with141DE9608, alternate "
        "net-animation branch. World+240 and local+130 are not interchangeable."
    ),
    0x141A8FCCC: (
        "STATIC phase8 retained-owner virtual+400; exact Pirate resolves14042FB40. "
        "Phase40 follows at141A913F9. Post-return is an observation candidate only, "
        "not proven safe for transform/physics writes."
    ),
    0x1401D4E10: (
        "STATIC exact RTTI-confirmed Pirate vtable+208 is this empty callback. "
        "Calling it after a root write cannot publish/refresh Pirate physics."
    ),
    0x1418F1DEE: (
        "STATIC left post-solve/pre-commit seam: wrist rotation finished at "
        "1418F1DE3, DL=1 at1DE8, RCX=left set at1DEA. Forward BOTH arguments "
        "and original return exactly once. Work buffers still live BEFORE "
        "14181ECD0, freed by its reset. Right commit1DF9 must stay untouched. "
        "Endpoint-preserving roll candidate: native world D on shoulder, "
        "inverse D on wrist, verify solved locators or restore two local Qs "
        "and invalidate world prefix. REJECTED visible repair2026-10-09 "
        "PID21244: four numeric APPLIED rows but nearly straight S/E/W and "
        "essentially unchanged E; user reports not fixed. Keep writer OFF."
    ),
    0x1418F107F: (
        "STATIC compares d to0.99*(upper+lower), constant142BE91BC=3F7D70A4. "
        "This is unreachable/debug/refusal routing, NOT the actual reach clamp. "
        "Terminal clears request+34 at1418F1BC5, so out-of-reach may continue."
    ),
    0x1418F113E: (
        "STATIC actual dClamped=min(requested |T-S|,upper+lower), no bend margin. "
        "1418F116A passes(dClamped,lower,upper) to triangle-angle helper. "
        "At d=upper+lower the elbow circle radius is zero; endpoint-preserving "
        "roll cannot create a visible bend. PID21244 logs lack raw T, so its "
        "near-straight solved geometry alone does not prove T was unreachable."
    ),
    0x1418F116F: (
        "STATIC arm bend plane: normalize((E-S) cross (W-E)) from incoming "
        "joint positions. Degenerate fallback at1418F1202 is negative "
        "elbow-world Y. No authored pole or torso collision test here."
    ),
    0x1418F1C43: (
        "Terminal opposite/left SolveArmIk for selector1. Exact ABI has ONE "
        "argument RCX=request (stack+60), AL return; entry RDX/R8 are not "
        "parameters on this path. Request+0=set,+8=armIndex -1,+C=0,+10=rawT "
        "float4,+20=zero-initialized output float4,+30=smoothstep(weight), "
        "+34=failUnreachable0 from1BC5. Caller preserves AL at1C48, ignores "
        "output+20, and proceeds with wristQ and both commits regardless AL. "
        "Post-solve positions precede final wrist orientation1DE3. Read-only "
        "capture must pair nativeT/pre/post same-call; do not mutate request."
    ),
    0x1418F0B8C: (
        "Global byte143684E64 bypass returns AL=1 without solving or updating "
        "request+20. AL=1 alone is NOT successful wrist observation. A paired "
        "reach capture must record bypass flag and actual pre/post positions."
    ),
    0x1418F1515: (
        "Solved path stores current wrist float4 at request+20 after getter "
        "1418F150C. ArmIkChain world prefix is exclusive (slot11 stores "
        "ordinal+1); copy S/E/W from existing table[1] only if prefix>wrist. "
        "No extra world-refresh calls are needed for this read-only snapshot."
    ),
    0x1418F18BD: (
        "STATIC 2026-10-08: first dereference after four stock getter calls "
        "1872(right wrist),1896(left wrist),18A6(right prop),18B5(left prop). "
        "Earlier results are pointers retained at stack+28/r14/rdi/rbx, not "
        "copied values. Any paired input relay must leave initialized stock "
        "left-wrist bytes intact until the final getter validates BOTH "
        "substitutions; early one-sided override is not fail-stock. No new "
        "runtime relay is admitted by this ordering observation."
    ),
    0x141AB97CC: (
        "STATIC native Process epoch: seven pushes put RSP=entryReturnSlot-38; "
        "this LEA yields RBP=entryReturnSlot-15A8. After sub rsp,1670, "
        "cmd16 Segment=RBP-80 and status=RBP+58 imply offsets1628/1550. "
        "Compare with an independently published original-call return slot; "
        "do not invent epoch authority from Segment+08 (reserved zero)."
    ),
    0x141AB9B3D: (
        "STATIC P99 correction: LEA custom-operator table143026CB0, NOT cmd16 "
        "CALL. Actual call141ABA973; return141ABA978. Do not hook this LEA "
        "as a post-program source boundary."
    ),
    0x141ABA973: (
        "STATIC actual cmd16 CALL14217B630; RDX=stack Segment from141ABA96B. "
        "Return141ABA978 is the candidate source window only for admitted "
        "normal Async g0 from Process141A92E78, not arbitrary C39 contexts. "
        "Two-arg ABI; original RCX/RDX must be forwarded once. Caller reloads "
        "R10/R11 at978/97D and parser reloads EDX/R8/R9; no inherited volatile "
        "argument is a source of authority. Stock faults must propagate, not retry. "
        "Independently captured native Process R must equal Segment+1628 and "
        "status+1550; Async frame identity alone cannot exclude nested Process."
    ),
    0x141ABA978: (
        "STATIC pinned g0 post16: untouched JP24/26/49/51 and newly computed "
        "W22 coexist. JP+480/4E0/930/990 each48B; workspace+5C0 64B. Copy "
        "before next command/cleanup;256B alone has no owner/epoch/source "
        "provenance and is NOT a final rendered palette. No hook admitted."
    ),
}


FUNCTION_TYPES = {
    0x1418F0B60: (
        "unsigned __int8 __fastcall SolveArmIk(void *request);"
    ),
    0x14217B630: (
        "unsigned int *__fastcall AnimCmdEvaluator_ExecuteSkeletonJointPrograms("
        "unsigned int *status, const void *segment);"
    ),
    0x141ED1380: (
        "unsigned __int8 __fastcall "
        "NdAttachableObject_UpdateSelectedAttachmentTransform(void *object);"
    ),
}


def ensure_function(ea):
    function = ida_funcs.get_func(ea)
    if function is None:
        ida_funcs.add_func(ea)
        function = ida_funcs.get_func(ea)
    return function


def apply_labels():
    # A previous melee pass accidentally put the dispatcher function name on an
    # interior instruction, forcing IDA to suffix the real entry with `_0`.
    ida_name.set_name(0x141273B20, "", ida_name.SN_NOWARN)
    # Auto-analysis coalesces the adjacent SkelTable bucket-count DWORD and
    # initialized byte into one wider item. Split them so both confirmed
    # lifecycle fields can carry independent names and comments.
    ida_bytes.del_items(0x1466EC470, ida_bytes.DELIT_SIMPLE, 5)
    if not ida_bytes.create_dword(0x1466EC470, 4):
        raise RuntimeError("failed to create SkelTable bucket-count DWORD")
    if not ida_bytes.create_byte(0x1466EC474, 1):
        raise RuntimeError("failed to create SkelTable initialized byte")
    for ea, (name, comment) in FUNCTIONS.items():
        ensure_function(ea)
        ida_name.set_name(
            ea,
            name,
            ida_name.SN_FORCE | ida_name.SN_NOCHECK,
        )
        ida_funcs.set_func_cmt(ea, comment, False)
        ida_bytes.set_cmt(ea, comment, False)
    for ea, (name, comment) in DATA_NAMES.items():
        ida_name.set_name(
            ea,
            name,
            ida_name.SN_FORCE | ida_name.SN_NOCHECK,
        )
        ida_bytes.set_cmt(ea, comment, False)
    for ea, comment in INSTRUCTION_COMMENTS.items():
        if not ida_bytes.set_cmt(ea, comment, False):
            raise RuntimeError(f"failed to apply instruction comment at 0x{ea:X}")
    for ea, declaration in FUNCTION_TYPES.items():
        if not idc.SetType(ea, declaration):
            raise RuntimeError(
                f"failed to apply function type at 0x{ea:X}: {declaration}"
            )


def main():
    import os
    import traceback

    log_path = os.environ.get(
        "TLL_APPLY_LOG",
        r"C:\users\vladimir\desktop\TLLCoopResearch\reports\apply_status.txt",
    )
    log = open(log_path, "w", encoding="utf-8")
    rc = 0
    try:
        n_fn = len(FUNCTIONS)
        n_data = len(DATA_NAMES)
        apply_labels()
        log.write(
            f"apply_labels OK: {n_fn} functions, {n_data} data names, "
            f"{len(FUNCTION_TYPES)} function types\n"
        )
        # Save the open database (IDA 9.x). flags=0 keeps the DB; this repacks
        # the .i64. (Old DBFL_KILL via _ida_loader was unreliable here.)
        path = idc.get_idb_path()
        log.write(f"idb_path={path}\n")
        ok = ida_loader.save_database(path, 0)
        log.write(f"save_database returned {ok}\n")
    except Exception as e:  # noqa: BLE001
        rc = 1
        log.write("ERROR: " + repr(e) + "\n" + traceback.format_exc())
    finally:
        log.flush()
        log.close()
    ida_pro.qexit(rc)


main()
