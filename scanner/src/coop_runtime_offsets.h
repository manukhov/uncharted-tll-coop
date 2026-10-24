// coop_runtime_offsets.h — TLL co-op runtime: module-base-relative addresses
// (RVAs), struct field offsets, and timing/tunable constants for tll.exe
// (1.4.21058.0).
//
// SINGLE SOURCE OF TRUTH for the reverse-engineered addresses used by the injected
// runtime. Every value is documented at its declaration; keep it in sync with the
// IDA database (ida/tll.i64) and COOP_STATUS.md. `constexpr` at namespace
// scope has internal linkage, so this header is included by coop_runtime.cpp only
// (behaviour is identical to the previous inline block; no linkage change).
//
// Naming: k<Subsystem><What>Rva   = add to g_module for the absolute address.
//         k<Struct><Field>Offset  = byte offset inside a runtime object.
#ifndef TLL_HOOK_OFFSETS_H
#define TLL_HOOK_OFFSETS_H

#include <array>
#include <cstddef>
#include <cstdint>

#include <Windows.h>  // ULONGLONG

constexpr std::uintptr_t kNadineBuddyVtableRva = 0x2949638;
constexpr std::uintptr_t kDisableBuddyTeleportRva = 0x3D441AD;
constexpr std::uintptr_t kBuildNavigationFrameRva = 0x1DF2D60;
constexpr std::uintptr_t kMoveToRva = 0x3F6890;
constexpr std::uintptr_t kBeginNpcCommandRva = 0x3F5B60;
constexpr std::uintptr_t kType4CommandRva = 0x3F69B0;
constexpr std::uintptr_t kRequestEnableAiRva = 0x3F5C80;
constexpr std::uintptr_t kRequestDisableAiRva = 0x3F5D30;
constexpr std::uintptr_t kGetCameraManagerRva = 0x1C06420;
constexpr std::uintptr_t kGetBlendedCameraTransformRva = 0x1C06650;
constexpr std::uintptr_t kGetCurrentCameraRva = 0x1C0D4B0;
constexpr std::uintptr_t kPerScreenCameraTransformCacheRva = 0x65FDAA0;
constexpr std::size_t kPerScreenCameraCacheStride = 0x400;
constexpr std::uintptr_t kSplitSecondPlayerAvailableRva = 0x3071038;
constexpr std::uintptr_t kSplitLoginStateRva = 0x307103C;
constexpr std::uintptr_t kRenderSplitModeRva = 0x3FE0260;
constexpr std::uintptr_t kRenderSplitAuxModeRva = 0x3FE033C;
constexpr std::uintptr_t kMaxLocalPlayersRva = 0x37CF310;
constexpr std::uintptr_t kLocalPlayerResolverGlobalRva = 0x3FE1070;
// NpcManager global (dword_143D3C0D0). Its leading list is consumed by the NPC
// spot/path-search tick; it is not the Player update scheduler's active-player list.
constexpr std::uintptr_t kNpcManagerRva = 0x3D3C0D0;
constexpr std::size_t kNpcManagerSpotActorCountOffset = 0;
constexpr std::size_t kNpcManagerSpotActorListOffset = 0x10; // 16-byte handle pairs
constexpr std::size_t kNpcManagerSpotMaskOffset = 0x5D08;
// Local-player resolver owner sub-object active count (mouse/cursor gate path).
constexpr std::size_t kLocalPlayerResolverActiveCountOffset = 0x495C; // +18780
constexpr std::uintptr_t kCharacterManagerAddCharacterRva = 0x1D63420;
constexpr std::uintptr_t kCharacterManagerRemoveCharacterRva = 0x1D63640;
constexpr std::uintptr_t kNetIsSecondPlayerAvailableRva = 0xC021C0;
constexpr std::uintptr_t kNetLoginSplitScreenRva = 0xC02220;
constexpr std::uintptr_t kNetLogoutSplitScreenRva = 0xC02420;
constexpr std::uintptr_t kSpawnFromDescriptorRva = 0x10F1910;
constexpr std::uintptr_t kRetainedSlotTableRva = 0x3EEBE80;   // g_RetainedPlayerSlotTable {ptr,id}[]
constexpr std::uintptr_t kPlayerVtableRva = 0x2A4BFE8;        // Player class vtable
// Observer+8 calls ResetActors AFTER killing the primary. Only this exact
// reason2 scope may admit our owned replica to native body/kill passes.
constexpr std::uintptr_t kTaskObserverActorResetCallsiteRva = 0x13C6B1F;
constexpr std::uintptr_t kActorResetRva = 0x13B9820;
constexpr std::uintptr_t kPlayerShouldResetVtableSlotRva = kPlayerVtableRva + 0x110;
constexpr std::uintptr_t kProcessShouldResetForReasonRva = 0x16A5B50;
constexpr std::uintptr_t kThreadContextGetCurrentProcessRva = 0x16A2E80;
constexpr std::uintptr_t kActorResetCurrentPredicateReturnRva = 0x13B9E93;
constexpr std::uintptr_t kActorResetCollectPredicateReturnRva = 0x13B975B;
constexpr std::uintptr_t kActorResetShutdownPredicateReturnRva = 0x13B97C2;
constexpr std::uintptr_t kActorResetKillPredicateReturnRva = 0x13B952F;
// Player vtable +0x448. NdGameObject::Init calls this after creating
// Player+0x1E0 and before PlayerWeaponController/GiveWeapon construction.
constexpr std::uintptr_t kPlayerSetupAttachSystemVtableSlotRva =
    kPlayerVtableRva + 0x448;
constexpr std::uintptr_t kPlayerSetupAttachSystemRva = 0x10CDF90;
constexpr std::size_t kPlayerQuatOffset = 0x140;              // orientation quaternion
constexpr std::size_t kPlayerLocalPosOffset = 0x130;          // local position vec4
constexpr std::size_t kPlayerLocalRootMirrorOffset = 0x250;   // local/world mirror
constexpr std::size_t kPlayerCollisionRootOffset = 0x280;     // capsule / collision root
// NdGameObject vtable +0x2B8: derives +0x240/+0x250/+0x280 from +0x130/+0x140.
constexpr std::uintptr_t kNdGameObjectRefreshWorldTransformRva = 0x1BB8320;
// Player animation runtime. Player+0x1A8 is FgAnimData*; async phase bits in the
// first DWORD select owner vtable +0x400/+0x408/+0x410 for bits 8/0x10/0x20.
constexpr std::size_t kPlayerFgAnimDataOffset = 0x1A8;
// Main Player AnimControl/upper-state owner. The ordinary weapon-IK path reads
// its active WeaponPartial layer from this object at 0x141095924.
constexpr std::size_t kPlayerUpperStateOwnerOffset = 0x1A0;
// The upper owner owns a wrapper at +0x60; dereferencing the wrapper yields the
// Player master AnimOverlayState installed at AnimStateLayer+0x18. The layer
// step copies that master into a track-local state before snapshot resolution.
constexpr std::size_t kPlayerUpperOwnerOverlayWrapperOffset = 0x60;
// Animation-id/clip lookup pipeline used by the scoped Nadine probes.
constexpr std::uintptr_t kAnimOverlayStateCopyRva = 0x17F9DD0;
constexpr std::uintptr_t kAnimSetRemapResolveAnimationIdRva = 0x17CA2E0;
constexpr std::uintptr_t kAnimationDatabaseResolveClipRva = 0x17C8E20;
constexpr std::uintptr_t kAnimOverlayStateResolveAnimationIdRva = 0x17F9F00;
// PlayerAimController_PublishDirectionalAimWeights 0x140CB41F0 writes directional
// fields to [[Player+0x1A0]+0x58]+0x10 at +0x188..+0x194 (D/U/L/R layout).
constexpr std::size_t kPlayerAnimHubChildOffset = 0x58;
constexpr std::size_t kPlayerAnimActiveStateOffset = 0x10;
constexpr std::size_t kAnimStateAimAddLeftOffset = 0x190;
constexpr std::size_t kAnimStateAimAddRightOffset = 0x194;
constexpr std::size_t kAnimStateAimAddUpOffset = 0x188;
constexpr std::size_t kAnimStateAimAddDownOffset = 0x18C;
constexpr std::size_t kAnimStateTurnYawRateOffset = 0xD4;
constexpr std::uintptr_t kPlayerAimPublishWeightsCallsiteRva = 0xCB4176;
constexpr std::uintptr_t kPlayerAimPublishWeightsRva = 0xCB41F0;
constexpr std::uintptr_t kPlayerAimSmoothAndPublishWeightsCallsiteRva = 0xCB416F;
constexpr std::uintptr_t kPlayerAimSmoothAndPublishWeightsRva = 0xCB47A0;
constexpr std::uintptr_t kPlayerAimResolveBaseStateRva = 0x10D5D40;
constexpr std::uintptr_t kAnimParamRegistryRequestLayerFadeOutAndReleaseRva =
    0x172C8E0;
constexpr std::size_t kFgAnimDataFlagsOffset = 0;
constexpr std::size_t kFgAnimDataStateOffset = 4;
constexpr std::size_t kFgAnimDataUpperStateOwnerOffset = 0x10;
// JointCache local-pose array used by CharacterLegIkController_ResetIkBuffers;
// one row is 48 bytes per skeleton joint.
constexpr std::size_t kFgAnimDataJointCacheLocalPoseOffset = 0x100;
constexpr std::size_t kFgAnimDataRetainedHandleOffset = 0x1A8;
constexpr std::size_t kFgAnimDataRetainedIdOffset = 0x1B0;
// Uniform scalar applied before publishing an owner's BASE world matrix.
constexpr std::size_t kFgAnimDataWorldScaleOffset = 0x140;
constexpr std::size_t kFgAnimDataRenderWorldMatrixOffset = 0x1C0;
// Optional render-only matrix applied by FgAnimData_GetRenderWorldMatrix when
// FgAnimData flags contain 0x40000.
constexpr std::size_t kFgAnimDataAuxRenderMatrixOffset = 0x2E0;
// AnimMgr::AllocateAnimData claims one 0x380-byte slot from this fixed-capacity
// pool and records every driven global FgInstance index at +0x210. The first
// 0x600 allocation bits correspond one-to-one with the pool slots.
constexpr std::uintptr_t kFgAnimDataPoolAllocatedBitsRva = 0x66CE200;
constexpr std::uintptr_t kFgAnimDataPoolBasePointerRva = 0x66D0200;
constexpr std::uint32_t kFgAnimDataPoolCapacity = 0x600;
constexpr std::size_t kFgAnimDataPoolStride = 0x380;
constexpr std::size_t kFgAnimDataMaxDrivenInstancesOffset = 0x208;
constexpr std::size_t kFgAnimDataDrivenInstanceCountOffset = 0x20C;
constexpr std::size_t kFgAnimDataDrivenInstanceIndicesOffset = 0x210;
// FgDrawMgr::AllocateInstance claims one 0x280-byte slot from this global pool
// and appends its index to exactly one owning FgAnimData driven list.
// FreeInstance immediately clears both used masks, sets the pending-delete mask
// and FgInstance+0x254 bit 1, then FrameBegin resets the slot after three GPU
// frames and clears the pending-delete mask.
constexpr std::uintptr_t kFgInstancePrimaryUsedBitsRva = 0x67C3080;
constexpr std::uintptr_t kFgInstanceSecondaryUsedBitsRva = 0x67C3600;
constexpr std::uintptr_t kFgInstancePendingDeleteBitsRva = 0x67C3A00;
constexpr std::uintptr_t kFgInstancePoolBasePointerRva = 0x67C3480;
constexpr std::uintptr_t kFgInstancePoolCapacityRva = 0x67C3488;
constexpr std::size_t kFgInstancePoolStride = 0x280;
constexpr std::size_t kFgInstanceStateFlagsOffset = 0x18;
constexpr std::size_t kFgInstanceRetireFlagsOffset = 0x254;
constexpr std::size_t kFgAnimDataRuntimePayloadOffset = 0x230;
constexpr std::size_t kFgAnimDataPreviousPaletteOffset = 0x238;
// Hex-Rays prints structure offsets in decimal: its +560/+568 are +0x230/+0x238.
// These are the current/previous pNextFrameSkinningBoneMats buffers (48 B/joint).
constexpr std::size_t kFgAnimDataCurrentSkinningMatsOffset = 0x230;
constexpr std::size_t kFgAnimDataPreviousSkinningMatsOffset = 0x238;
constexpr std::size_t kFgAnimDataCurrentUploadSkinningMatsOffset = 0x240;
constexpr std::size_t kFgAnimDataPreviousUploadSkinningMatsOffset = 0x248;
constexpr std::size_t kFgAnimDataSkinningPaletteLockOffset = 0x220;
constexpr std::uintptr_t kFgAnimDataPublishWorldTransformRva = 0x18C55D0;
constexpr std::uintptr_t kAnimManagerPublishWorldTransformCallsiteRva = 0x1A8FC00;
// Player_PostAnimBlending_Async calls CharacterLegIkController vtable +0x18
// after ResetIkBuffers and the intervening preparation calls. Its entry is the
// narrow boundary after both LegIkChains have been seeded from JointCache and
// before the stock leg solver mutates their rotations.
constexpr std::uintptr_t kCharacterLegIkControllerUpdateRva = 0x1F9E6F0;
constexpr std::uintptr_t kCharacterLegIkControllerUpdateVtableSlotRva =
    0x2BBB478;
// Player vtable +0x408. AnimManager calls this after PerformAnimBlend and before
// the separately queued AsyncObjectUpdate_JointBlend job builds the final
// skinning palette.  The stock method runs all Player phase-3/late IK first.
constexpr std::uintptr_t kPlayerPostAnimBlendingAsyncRva = 0x10EB6A0;
constexpr std::uintptr_t kPlayerPostAnimBlendingAsyncVtableSlotRva =
    0x2A4C3F0;
// Recompute one JointCache world locator from its local-pose row and the
// already-current parent locator. RCX is FgAnimData+0x30, EDX is joint index.
constexpr std::uintptr_t kJointCacheRecomputeWorldLocatorRva = 0x1688320;
// FgDrawManager worker callsite. The worker passes one FgAnimData and the
// global FgInstance pool to PrepareInstanceForRendering; after the call every
// driven 0x280-byte instance owns the current render-world matrix at +0x140.
constexpr std::uintptr_t kFgDrawManagerPrepareInstanceRva = 0x181A9B0;
constexpr std::uintptr_t kFgDrawManagerPrepareInstanceCallsiteRva = 0x181B925;
// All three direct calls to FgAnimData_GetRenderWorldMatrix inside
// PrepareInstanceForRendering. The first two feed temporal/motion data and the
// last one writes each driven FgInstance's visible +0x140 matrix.
constexpr std::uintptr_t kFgAnimDataGetRenderWorldMatrixRva = 0x03014F0;
constexpr std::array<std::uintptr_t, 3>
    kFgDrawManagerRenderWorldMatrixCallsiteRvas{
        0x181AD57, 0x181ADFE, 0x181B3F4};
constexpr std::uintptr_t kMatrix4x4FromTranslationQuaternionRva = 0x019A270;
constexpr std::uintptr_t kMatrix4x4MultiplyRva = 0x0301310;
// Shared authored-transform pointer table used by the stock retained-owner
// phase-0x40 parent refresh. ProcessWeapon+0x108 is the table index and +0x10C
// is matched against the selected entry's generation DWORD at +0x50. A match
// refreshes cached parent +0x110; null/mismatch keeps and composes that cache.
constexpr std::uintptr_t kAuthoredTransformPointerTableRva = 0x386FD00;
// PrepareInstanceForRendering holds Fg+0x220 and calls the image's memcpy thunk
// with upload/current/48*jointCount in RCX/RDX/R8. R14 still contains the exact
// FgAnimData. A three-byte relay surfaces R14 as the fourth Win64 argument.
constexpr std::uintptr_t kFgDrawManagerSkinningPaletteMemcpyCallsiteRva =
    0x181AB33;
constexpr std::uintptr_t kMemCpyImportThunkRva = 0x24ECF8C;
// A synchronous animation-playback path also publishes directly into an
// owner's FgAnimData and invokes PrepareInstanceForRendering outside the normal
// PostAnimProcessing worker batch.
constexpr std::uintptr_t kFgDrawManagerSynchronousPrepareInstanceCallsiteRva =
    0x197DC54;
// RenderMgr::MainDrawCore passes the completed per-frame FgInstance pointer
// array, matching FgLodData array and instance count to CullFgLods::Spawn.
// This is the first consumer after FgDrawManager's queue publication and is a
// read-only discriminator for stale or alternate rifle submissions.
constexpr std::uintptr_t kRenderMgrCullFgLodsSpawnRva = 0x1A2C440;
constexpr std::uintptr_t kRenderMgrMainDrawCullFgLodsCallsiteRva = 0x1661A57;
// RenderMgr::MainDrawCore next schedules CullFgSceneJob for its concrete
// SceneWindow, waits through MeshCull::WaitForCullFgScene and finalizes the
// resulting per-view draw lists. Each emitted 9-byte submesh row retains the
// originating FgScene index as a uint16 at +7, which makes this post-wait
// callsite an exact read-only provenance boundary for local-view pixels.
constexpr std::uintptr_t kMeshCullWaitForCullFgSceneRva = 0x1A35BE0;
constexpr std::uintptr_t kRenderMgrMainDrawWaitForCullFgSceneCallsiteRva =
    0x1661BE4;
constexpr std::size_t kFgAnimDataSkeletonOffset = 0x18;
constexpr std::size_t kSkeletonJointCountOffset = 0x34;
// Animation command evaluator and the fixed-stride SkelTable retarget registry.
// Its 0x28-byte AnimCmdContext is {target skeleton, FgAnimData+0x2C0 value,
// &FgAnimData+0x1C0, optional scratch, flags}; the scratch pointer may be null.
// ProcessAnimCmdListSegment case 6 selects direct clip evaluation when the
// source/target underlying skeleton ids match, otherwise it resolves this table.
constexpr std::uintptr_t kProcessAnimCmdListSegmentRva = 0x1AB97C0;
constexpr std::uintptr_t kAnimCmdFinalizeGroupDefaultsCallsiteRva = 0x1AB9D18;
constexpr std::uintptr_t kAnimCmdFinalizeGroupDefaultsRva = 0x1AB78F0;
constexpr std::uintptr_t kAnimCmdCopyGroupPoseCallsiteRva = 0x1ABA7F5;
constexpr std::uintptr_t kAnimCmdCopyGroupPoseRva = 0x217B310;
constexpr std::uintptr_t kAnimCmdExpandJointParamsCallsiteRva = 0x1ABA89A;
constexpr std::uintptr_t kAnimCmdExpandJointParamsRva = 0x217BF30;
// Normal campaign command-evaluator boundaries. Opcode 16 is the only broad
// producer of WorkBuffer+0x58; opcode 26 consumes the same 64-byte rows.
constexpr std::uintptr_t kAnimCmdExecuteJointProgramsCallsiteRva = 0x1ABA973;
constexpr std::uintptr_t kAnimCmdExecuteJointProgramsRva = 0x217B630;
constexpr std::uintptr_t kAnimJointProgramBuiltinOperatorTableRva = 0x3026D20;
constexpr std::uintptr_t kAnimJointProgramOp40Rva = 0x2194470;
constexpr std::uintptr_t kAnimJointProgramOp41Rva = 0x2194680;
constexpr std::uintptr_t kAnimJointProgramOp44Rva = 0x21946D0;
constexpr std::uintptr_t kAnimJointProgramOp47Rva = 0x2194720;
constexpr std::uintptr_t kAnimJointProgramOp51Rva = 0x21949E0;
constexpr std::uintptr_t kAnimJointProgramCustomOperatorTableRva = 0x3026CB0;
constexpr std::uintptr_t kAnimJointProgramCustomOp128Rva = 0x1A97060;
constexpr std::uintptr_t kAnimJointProgramCustomOp129Rva = 0x1A951F0;
constexpr std::uintptr_t kAnimJointProgramCustomOp131Rva = 0x1A94D80;
constexpr std::uintptr_t kAnimCmdWriteSkinningPaletteCallsiteRva = 0x1ABAE8C;
constexpr std::uintptr_t kAnimCmdWriteSkinningPaletteRva = 0x218B170;
// Embedded PlayerInputCommandState (sub_14024D7E0 returns Player+0x39A0).
constexpr std::size_t kPlayerInputCommandStateOffset = 0x39A0;
constexpr std::uintptr_t kPlayerInputCommandStateIsCommandActiveRva = 0x1E39D50;
// Exact reload decision queries inside Player_BuildAcceptedActionInputState.
// Both are direct E8 calls to PlayerInputCommandState_IsCommandActive with
// edge=0; the first hashes `reload`, the second `reload-release`.
constexpr std::uintptr_t kPlayerReloadIsCommandActiveCallsiteRva = 0xC9E611;
constexpr std::uintptr_t kPlayerReloadReleaseIsCommandActiveCallsiteRva =
    0xC9E632;
constexpr std::uintptr_t kPlayerReloadClipActionVtableRva = 0x2A48948;
constexpr std::uintptr_t kPlayerReloadLoopActionVtableRva = 0x2A48AF8;
constexpr std::size_t kPlayerReloadActionStartVtableSlot = 2;
constexpr std::uintptr_t kPlayerReloadClipActionStartRva = 0x1098430;
constexpr std::uintptr_t kPlayerReloadLoopActionStartRva = 0x109A620;
// Third exact `drop`, edge=0 consumer inside PlayerEdgeState_HandleDropCommand.
// The first two consumers query Joypad directly; this one bypasses that hook and
// therefore needs its own retained-puppet callsite relay.
constexpr std::uintptr_t kPlayerEdgeDropInputIsCommandActiveCallsiteRva =
    0x133405C;
// The two earlier exact `drop`, edge=0 calls inside the same handler query
// Joypad directly. Their return addresses scope sender event capture to the
// native Player::Edge consumer rather than the generic physical binding.
constexpr std::uintptr_t kPlayerEdgeDropJoypadIsCommandActiveCallsiteARva =
    0x1333301;
constexpr std::uintptr_t kPlayerEdgeDropJoypadIsCommandActiveCallsiteBRva =
    0x1333463;
// Exact grenade command consumers. The scheduler entry and its state-1/state-2/
// state-4 hold/release updates all call PlayerInputCommandState_IsCommandActive
// directly. Relaying only these E8 sites avoids FastGrenadeController +0xA0,
// whose independent Joypad query marks the action inhibited at controller+0x1A0.
constexpr std::uintptr_t kGrenadeSchedulerIsCommandActiveCallsiteRva =
    0x10E78E6;
constexpr std::uintptr_t kGrenadeState1IsCommandActiveCallsiteRva = 0xE218DD;
constexpr std::uintptr_t kGrenadeState2IsCommandActiveCallsiteRva = 0xE21A8F;
constexpr std::uintptr_t kGrenadeState4IsCommandActiveCallsiteRva = 0xE2227D;
constexpr std::size_t kPlayerFastGrenadeControllerOffset = 0x1E50;
constexpr std::uintptr_t kFastGrenadeControllerVtableRva = 0x2A227A0;
constexpr std::size_t kFastGrenadeControllerStartVtableOffset = 0x40;
constexpr std::size_t kFastGrenadeControllerResetVtableOffset = 0x60;
constexpr std::size_t kFastGrenadeControllerCanStartVtableOffset = 0xA8;
constexpr std::size_t kFastGrenadeControllerReleaseVtableOffset = 0x140;
constexpr std::uintptr_t kFastGrenadeControllerStartRva = 0xE19600;
constexpr std::uintptr_t kFastGrenadeControllerResetRva = 0xE1D850;
constexpr std::uintptr_t kFastGrenadeControllerCanStartRva = 0xE25540;
constexpr std::uintptr_t kFastGrenadeControllerReleaseRva = 0xE1B900;
constexpr std::uintptr_t kGrenadeReleasePostArcHoldRva = 0xE1FF90;
constexpr std::uintptr_t kGrenadeResetReturnCoverAbortRva = 0xE201EB;
constexpr std::uintptr_t kGrenadeResetReturnNetGateAbortRva = 0xE2034B;
// Shared ResetToIdle call in UpdateStateAndCommitThrow. State 7 reaches it on
// deadline expiry; state 8 reaches the same call when no live authored action
// state is available.
constexpr std::uintptr_t kGrenadeResetReturnState7Or8AbortRva = 0xE22333;
// Return address of the scheduler's FastGrenadeController::CanStart call. The
// stock loadout binds its ProcessWeapon before StartArc; provision the retained
// puppet only after this eligibility check succeeds, never from StartArc's
// internal CanStart recheck (return RVA 0xE19612).
constexpr std::uintptr_t kGrenadeSchedulerCanStartReturnRva = 0x10E7C83;
// Exact press-query callsites inside the naturally scheduled melee dispatcher.
// Both are E8 calls to PlayerInputCommandState_IsCommandActive. They capture
// local PC/pad melee; remote execution is injected at the Joypad square query.
constexpr std::uintptr_t kMeleeR1IsCommandActiveCallsiteRva = 0x1273AE2;
constexpr std::uintptr_t kMeleeLmousePressIsCommandActiveCallsiteRva = 0x1273D3B;
// MoveStick air-melee anim gate (sub_1410FC9F0); net-mode early-out blocks puppet.
constexpr std::uintptr_t kPlayerMeleeTryAnimRva = 0x10FC9F0;
constexpr std::uintptr_t kMeleeTryAnimEnabledGlobalRva = 0x3ECF31A;
constexpr std::uintptr_t kNetModeFlagsGlobalRva = 0x3E07470;
// Large mounted action controller used by TryAnim (not the +0x1F60 queue owner).
constexpr std::size_t kPlayerMeleeActionControllerOffset = 0x1DE0;
constexpr std::size_t kPlayerMeleeSiblingControllerOffset = 0x1D88;
constexpr std::size_t kPlayerMeleeActionGetObjectVtableOffset = 0x280;
constexpr std::size_t kPlayerMeleeActionValidateMoveVtableOffset = 0x30;
constexpr std::size_t kPlayerMeleeActionBranchCheckVtableOffset = 0x1C8;
constexpr std::size_t kPlayerMeleeActionAltCheckVtableOffset = 0x158;
constexpr std::size_t kPlayerMeleeSiblingDispatchVtableOffset = 0x200;
constexpr std::size_t kPlayerMeleePrimaryAnimVtableOffset = 0xDE8;
constexpr std::size_t kPlayerMeleeActionGateByteOffset = 0x8C1;
// Live melee button dispatcher: probes Joypad_IsCommandActive then pushes the
// 16-slot queue via PushOrRotateMeleeCommandQueueEntry. Called from
// PlayerMeleeController vtable tick with (controller+0x150, player).
constexpr std::uintptr_t kDispatchPlayerMeleeCommandsRva = 0x272A50;
constexpr std::uintptr_t kPushOrRotateMeleeCommandQueueEntryRva = 0x273FA0;
constexpr std::uintptr_t kPlayerMeleeControllerTickDispatchRva = 0x279DF0;
constexpr std::uintptr_t kPlayerMeleeControllerVtableRva = 0x2A6C2E8;
constexpr std::size_t kMeleeCommandQueueRegionSize = 0x400;
// Zeroes/init melee command descriptor before PlayerMeleeController vtable+504.
constexpr std::uintptr_t kInitMeleeCommandDescRva = 0xE416A0;
// Player vtable offsets used by the TryAnim success path in MoveStick LABEL_491.
constexpr std::size_t kPlayerMeleeAnimDecisionVtableOffset = 0xC78;
constexpr std::size_t kPlayerMeleePublishVtableOffset = 0xC80;
// Combat/melee command-queue owner mounted at Player init (ConstructMeleeCommandQueueOwner).
constexpr std::size_t kPlayerCombatControllerOffset = 0x1F60;
constexpr std::size_t kPlayerMeleeControllerPlayerBackrefOffset = 0x108;
constexpr std::size_t kPlayerMeleeControllerQueueBaseOffset = 0x150;
// vtable+0x210 (0x141279F10) resolves string→hash then calls validate at +0x1F8;
// it is NOT a validate-with-128-byte-buffer entry point.
constexpr std::size_t kPlayerMeleeControllerValidateCommandVtableOffset = 0x210;
constexpr std::size_t kPlayerMeleeControllerQueueValidateVtableOffset = 0x1F8;
constexpr std::size_t kMeleeTargetResultEntityIdOffset = 0xD8;
constexpr std::size_t kPlayerHealthSystemOffset = 0x4A8;
constexpr std::size_t kPlayerCurrentHealthOffset = 0x38;
constexpr std::size_t kPlayerMaxHealthOffset = 0x44;
constexpr std::uintptr_t kSetControlActionStateIdAndForward168Rva = 0x1DE5A10;
constexpr std::uintptr_t kAttachCameraRetainedBindingRva = 0x1C074E0;
constexpr std::uintptr_t kToggleCameraRetainedBindingRva = 0x1C07730;
constexpr std::uintptr_t kNetGameManagerGlobalRva = 0x3DD70C0;
constexpr std::uintptr_t kNetPlayerContextGlobalRva = 0x3E08C80;
constexpr std::uintptr_t kNetPlayerSourceArrayARva = 0x3DD7480;
constexpr std::uintptr_t kNetPlayerSourceArrayBRva = 0x3E09568;
constexpr std::uintptr_t kAddOrUpdateNetPlayerTrackerRva = 0xA90550;
constexpr std::uintptr_t kGetNetPlayerTrackerByScreenIndexRva = 0xA90E80;
constexpr std::uintptr_t kNetPlayerTrackerNetRespawnRva = 0xB347C0;
constexpr std::uintptr_t kBuildRespawnTransformBlockRva = 0x1685C30;
constexpr std::uintptr_t kRespawnSeedDefaultVec4Rva = 0x28805D0;
constexpr std::uintptr_t kStringHashRva = 0x14D910;
constexpr std::uintptr_t kResolveUpperActorRecordRva = 0x17CD080;
constexpr std::uintptr_t kScopeVisibleQueryRva = 0x0C177F0;
constexpr std::uintptr_t kPoseSeedResolverRva = 0x1C077B0;  // ResolveScreenRoutePoseSeedOrDefault (fire/aim ray source)
constexpr std::uintptr_t kReticleHudPublisherRva = 0x135A650;
constexpr std::uintptr_t kReticleAimHudPublisherRva = 0x135AF20;
constexpr std::uintptr_t kBlindfireReticleHudPublisherRva = 0x135C050;
// Per-screen selected-weapon/ammo publisher. Reads state+0xF8, publishes
// selected-weapon-id/total-ammo/loaded-ammo, and spawns ss-hud-weapon.
constexpr std::uintptr_t kWeaponHudPublisherRva = 0x135D2B0;
// Per-screen HUD state: the screen/slot index lives at state+0xF8 (see
// GetRetainedPlayerEntityBySlot(*(state+248)) inside the reticle publishers
// and the fixed screen-0 global 0x143EF1F28 = g_PerScreenPlayerHudState+0xF8).
constexpr std::size_t kPerScreenHudStateScreenIndexOffset = 0xF8;
constexpr std::uintptr_t kPlayerVisualLookSeedRva = 0x1BB4B01;
constexpr std::uintptr_t kPlayerVisualStateSeedRva = 0x1BB4B14;
constexpr std::size_t kNetPlayerSourceEntrySize = 0x1C0;
constexpr std::size_t kUpdateSlot = 87;
constexpr std::size_t kWorldPositionOffset = 0x240;
// Player velocity vec3 (units/sec); the locomotion anim graph blends run/walk
// from this. Found by runtime direction-matching probe (2026-06-15); mirrored
// at +0x270. Writing it on the puppet makes the engine play motion animation.
constexpr std::size_t kPlayerVelocityOffset = 0x260;
constexpr std::size_t kPlayerVelocityMirrorOffset = 0x270;
constexpr std::size_t kReferenceTransformOffset = 0x100;
constexpr std::size_t kCameraRotationOffset = 0x1B0;
// BuildFireArgs stack dataflow: FireArgs starts at var_520 and the 32-byte
// resolver seed is copied to var_4C0, therefore seed=+0x60 and quat=+0x70.
constexpr std::size_t kFireArgsPoseSeedOffset = 0x60;
constexpr std::size_t kFireArgsAimQuatOffset = 0x70;
constexpr std::size_t kFireArgsTargetVectorOffset = 0x00;
constexpr std::size_t kFireArgsDirectionVectorOffset = 0x10;
constexpr std::uintptr_t kProcessWeaponSpawnProjectileRva = 0x1519B50;
// ProcessWeapon::ProjectileEmission first stores the 0x151B72B record pointer at
// SpawnInfo+0x28. Only projectile-count > 1 replaces it with the later 0x151BC3E
// record at 0x151BE33. Observe/mutate the selected pointer at SpawnProjectile
// instead of guessing which BuildProjectileLaunchRecord call owns this shot.
constexpr std::size_t kSpawnInfoLaunchRecordPointerOffset = 0x28;
constexpr std::size_t kProjectileLaunchRecordOwnerHandleOffset = 0x00;
// BuildProjectileLaunchRecord copies its R8 world-position vector here. Instant
// bullet Init passes this field to BoundTransform_SetTranslationWs for the ray
// start, then computes the endpoint as start + direction.
constexpr std::size_t kProjectileLaunchRecordOriginOffset = 0x20;
constexpr std::size_t kProjectileLaunchRecordDirectionOffset = 0x30;
constexpr std::size_t kProjectileLaunchRecordDirectionMirrorOffset = 0x40;
// The builder duplicates the same R8 origin for the base projectile/tracer path.
constexpr std::size_t kProjectileLaunchRecordOriginMirrorOffset = 0x70;
// Exact Tracer RTTI vtable slot containing Tracer::Init. Its third argument is
// spawn_info and spawn_info+0x28 selects the copied launch record.
constexpr std::uintptr_t kTracerInitVtableSlotRva = 0x2AA3238;
constexpr std::uintptr_t kTracerInitRva = 0x155BE10;
constexpr std::size_t kRuntimeObjectHandlePointerOffset = 0xA8;
constexpr std::size_t kRuntimeObjectHandleIdOffset = 0xBC;
constexpr std::uintptr_t kProjectileBulletInstantProbeKickRva = 0x152C9B0;
constexpr std::uintptr_t kProjectileBulletInstantProbeKickCallsiteRva =
    0x152C8A6;
constexpr std::uintptr_t kBoundTransformGetTranslationWsRva = 0x14E240;
constexpr std::size_t kProjectileBulletInstantOwnerHandleOffset = 0x100;
constexpr std::size_t kProjectileBulletInstantStartBoundOffset = 0x130;
constexpr std::size_t kProjectileBulletInstantEndpointBoundOffset = 0x570;
// CameraManager_GetBlendedCameraTransform out buffer: 4x4 matrix at +0x350.
constexpr std::size_t kCameraOutMatrixOffset = 0x350;
// Route-entity screen index field read inside GetBlendedCameraTransform.
constexpr std::size_t kRouteEntityScreenIndexOffset = 0x124;
constexpr std::size_t kPlayerAdsActiveOffset = 0x1548;
// PlayerFireController_Fire writes FireArgs-derived aim vectors here during fire;
// also populated during ADS (FINDINGS_ARCHIVE.md field diff +0x1570..+0x1588).
constexpr std::size_t kPlayerAimTargetOffset = 0x1570;
// AIM POSE on the live Player (found 2026-07-07 by up/down live diff): +0x1720 =
// aim origin pos (vec4), +0x1730 = aim orientation UNIT QUATERNION (x,z flip with
// pitch). Candidate input to the visible aim pose (upper-body point).
constexpr std::size_t kPlayerAimPosePosOffset = 0x1720;
constexpr std::size_t kPlayerAimPoseQuatOffset = 0x1730;
// Duplicated world aim-at point (biggest movers in the 2026-07-07 up/down live
// diff, delta ~14.5 on pure pitch). Candidate IK aim-at input for the upper-body
// pitch bend (build af: yaw follows via the +0x1730 writer, pitch does not).
constexpr std::size_t kPlayerAimAtPointOffset = 0x21F0;
constexpr std::size_t kPlayerAimAtPointAuxOffset = 0x2240;
// PlayerLookAtIkController heap object; Player+0x1E38 is the POINTER (AssembleGraph
// 0x1410CBBC4 stores alloc result at a1+7736). Do NOT treat +0x1E38 as embedded.
constexpr std::size_t kPlayerLookAtIkPtrOffset = 0x1E38;
constexpr std::size_t kPlayerLookAtIkOffset = 0x1E38;  // alias: pointer slot
constexpr std::size_t kPlayerLookAtIkEnabledByteOffset = 0x420;   // from IK this
constexpr std::size_t kPlayerLookAtIkModeOffset = 0x3E0;          // 1=point @+0x3F0
constexpr std::size_t kPlayerLookAtIkWorldPointOffset = 0x3F0;
constexpr std::size_t kPlayerLookAtIkSlotWeightOffset = 0x330;
constexpr std::size_t kPlayerLookAtIkSlot0ModeFlagOffset = 0x340; // (slot+13)<<6, slot0
constexpr std::size_t kPlayerLookAtIkSlot0DataOffset = 0x350;     // SetLookPoint slot0
constexpr std::size_t kPlayerLookAtIkSlotTableOffset = 0x390;     // UpdateIk slot0 copy src
constexpr std::uintptr_t kPlayerLookAtSetPointRva = 0xEDA5F0;     // vtable+0x28
constexpr std::uintptr_t kPlayerLookAtUpdateIkRva = 0xEDA680;     // vtable+0x38
// Per-screen camera handle table (2 entries, {ptr,id} 16B each). The resolver
// ResolveScreenRoutePoseSeedOrDefault reads screen idx's pose seed from
// *(*(table[2*idx]+8)+176)+0x60. Writing the peer seed to that exact source for
// idx=1 drives the puppet's aim natively (found 2026-07-07).
// Pickup gesture (2026-07-10): InputScreenBlock_ActivateCommand activates an
// interact command id in the per-screen block's +0x590 list, bypassing the +0x518
// button routing that never reaches the puppet. Per-screen block =
// *(g_module+0x3FE1088) + 0xDB0 + 0xBF0*joypadIdx (joypadIdx = player+0x39B8).
// Exp 5b: vtable slot (in .rdata) holding PlayerInteractController update
// sub_140EC6C10. Swap it to probe whether the interact update runs for the puppet.
// ProjectileGrenade::Init 0x141533560 - fires on every grenade throw/spawn. Push-
// based prologue (trampoline-safe): 40 53 56 57 41 54 41 55 41 56 41 57 (12 bytes).
constexpr std::uintptr_t kProjectileGrenadeInitRva = 0x1533560;
constexpr std::uintptr_t kEmitGrenadeProjectileRva = 0x154B3B0;
constexpr std::uintptr_t kEmitGrenadeProjectileCallsiteRva = 0x40D852;
// NetEventEmitGrenade is the native receiver-side spawn recipe. Its Execute
// builds ProcessSpawnInfo and drives the descriptor/process materializer.
constexpr std::uintptr_t kNetEventEmitGrenadeDefaultCtorRva = 0xBE5C80;
constexpr std::uintptr_t kNetEventEmitGrenadeBuildFromProjectileRva = 0xBE5D30;
constexpr std::uintptr_t kNetEventEmitGrenadeExecuteRva = 0xBE63A0;
constexpr std::uintptr_t kProjectileGrenadeResolveOwnerRva = 0x1544CD0;
constexpr std::uintptr_t kProjectileGrenadeLaunchSetupRva = 0x1532010;
constexpr std::uintptr_t kSendNetEventGrenadeStartFuseRva = 0xBE9380;
// SimpleBreakable / destructible env NetEvent relay (net-event.cpp).
constexpr std::uintptr_t kNetGameManagerSendNetEventMaskRva = 0xA962F0;
constexpr std::uintptr_t kSendNetEventBreakableAttackExplosionWithResultsRva =
    0xA56CF0;
constexpr std::uintptr_t kNetEventBreakableAttackExplosionWithResultsExecuteRva =
    0xA56910;
constexpr std::uintptr_t kNetEventBreakableAttackProjectileWithResultsExecuteRva =
    0xA55580;
constexpr std::uintptr_t kNetEventBreakableAttackExplosionExecuteRva = 0xA56290;
constexpr std::uintptr_t kNetEventBreakableAttackProjectileExecuteRva = 0xA54F90;
constexpr std::uintptr_t kNetEventSyncBreakableExecuteRva = 0xA65160;
constexpr std::uintptr_t kSendNetEventSyncBreakableRva = 0xA655C0;
constexpr std::uintptr_t kBreakableLocalApplyPostRva = 0x88E044;
constexpr std::uintptr_t kBreakableLocalApplyJnzTargetRva = 0x88E1AE;
constexpr std::uintptr_t kBreakableLocalApplyResumeRva = 0x88E056;
constexpr std::uintptr_t kBreakableLocalApplyAltPostRva = 0x88E32E;
constexpr std::uintptr_t kBreakableLocalApplyAltResumeRva = 0x88E32F;
// SimpleBreakable::EventHandler explosion-damage event (0x32330453) -> sub_14088E6F0.
constexpr std::uintptr_t kBreakableExplosionDamagePostRva = 0x88DC83;
constexpr std::uintptr_t kBreakableExplosionDamageResumeRva = 0x88D9DE;
constexpr std::uintptr_t kSimpleBreakableExplosionDamageFnRva = 0x88E6F0;
constexpr std::uintptr_t kSendNetEventSyncBreakableMulticastCallRva = 0xA65771;
constexpr std::uintptr_t kNetGameManagerMulticastNetEventRva = 0xA96830;
constexpr std::uintptr_t kSimpleBreakableEventHandlerRva = 0x88D960;
constexpr std::uintptr_t kNetObjectResolverTableRva = 0x6466FE0;
constexpr std::uintptr_t kNetObjectResolveByIdRva = 0x1622BC0;
constexpr std::uintptr_t kNetObjectTableEntryToObjectRva = 0x14DCE0;
constexpr std::uintptr_t kInteractControllerUpdateVtableSlotRva = 0x2A2CB70;
constexpr std::uintptr_t kInteractControllerUpdateRva = 0xEC6C10;
constexpr std::uintptr_t kInputScreenBlockActivateCommandRva = 0x183A650;
constexpr std::uintptr_t kFrameStateGlobalRva = 0x3FE1088;
constexpr std::size_t kFrameStateScreenBlockBaseOffset = 0xDB0;
constexpr std::size_t kFrameStateScreenBlockStride = 0xBF0;
constexpr std::size_t kPlayerJoypadIndexOffset = 0x39B8;
constexpr std::uintptr_t kScreenCameraHandleTableRva = 0x697AD50;
constexpr std::size_t kCameraHandleInnerOffset = 0x8;    // handle -> +8
constexpr std::size_t kCameraBlockOffset = 0xB0;         // (+8) -> +176 = blk
constexpr std::size_t kCameraPoseSeedOffset = 0x60;      // blk -> +0x60 (32B seed)
constexpr std::size_t kCameraGateByteAOffset = 85;
constexpr std::size_t kCameraGateByteBOffset = 86;
constexpr std::size_t kPlayerAimTargetAuxOffset = 0x1580;
constexpr std::size_t kLocomotionControllerOffset = 0x15B8;
// PlayerWeaponController (player+0x1D88) fields. Runtime-pinned 2026-07-06 by a
// 500ms live watch across holster/draw/ADS cycles (+ SwitchToWeapon 0x1410A1C10
// decompile: compares the requested slot against +0x120, owner Player at +0x108).
constexpr std::size_t kPlayerWeaponControllerOffset = 0x1D88;
constexpr std::size_t kWeaponCtrlCurrentSlotOffset = 0x120;  // int32; -1 = none
// Draw state machine: 0 inactive, 1 Draw, 2 WeaponOut, 3 Reloading, 4 Aiming,
// 5 Holster, 6/7 hand switch, 8 Rechamber, 9/10 WeaponIK, 11 FinishShield,
// 12 UnholsterDisabled, 13/14 Drop.  Exact names are in ApplyState 0x1410AAFB0.
constexpr std::size_t kWeaponCtrlDrawStateOffset = 0x134;
// Normalizes the peer's horizontal ground speed (units/s) into a 0..1 left-stick
// magnitude fed to the clone's slot-1 input so its own locomotion blends walk/run.
// Tunable at runtime; ~full-run speed.
constexpr float kCloneRunSpeedNorm = 4.0F;
constexpr std::size_t kNavigationRadiusOffset = 0x1C8;
constexpr std::size_t kFacingOffset = 0x24A0;
constexpr std::size_t kMoveParametersOffset = 0x24B0;
constexpr std::size_t kType2508Offset = 0x2508;
constexpr std::size_t kType2514Offset = 0x2514;
constexpr std::size_t kType2560Offset = 0x2560;
constexpr std::size_t kType2561Offset = 0x2561;
constexpr ULONGLONG kRemoteCommandTimeoutMs = 500;
constexpr ULONGLONG kRemoteCommandIntervalMs = 16;
constexpr ULONGLONG kCommandIntervalMs = 250;
constexpr ULONGLONG kType4CommandIntervalMs = 16;
constexpr bool kEnableBuiltInSlot1NetPlayerScaffold = false;
// Old reverse-engineering helpers suspend every game thread, install hardware
// breakpoints, and poll game objects from a separate thread. They are incompatible
// with a stable two-instance runtime and are never needed by the current co-op path.
constexpr bool kEnableLegacyRuntimeDiagnostics = false;
constexpr std::uintptr_t kAddJoypadDisabledCommandsRva = 0x1839FB0;
constexpr std::uintptr_t kInputScaleCopyRva = 0x183B410;
constexpr std::uintptr_t kGameInfoVtableRva = 0x29040B8;
constexpr std::uintptr_t kPlayerFireControllerFireRva = 0x0E2C730;
// Sole caller of PlayerFireController_Fire; seeds aim ray via pose-seed resolver.
constexpr std::uintptr_t kBuildFireArgsAndFireRva = 0x0E2BCD0;
constexpr std::uintptr_t kPlayerFireControllerTryStartFireRva = 0x0E2ACA0;
constexpr std::uintptr_t kPlayerFireControllerCheckShotReadinessRva = 0x0E2B220;
// Live ProcessWeapon signed 16-bit ammo fields. BuildFireArgsAndFire reads the
// magazine through vtable+0x848 and exits before Fire when it is <= 0.
constexpr std::size_t kProcessWeaponMagazineAmmoOffset = 0x900;
constexpr std::size_t kProcessWeaponReserveAmmoOffset = 0x902;
// Native SetClip/Reload keep these bytes coherent with a non-empty magazine.
// vtable+0x868 reports the unloaded state when +0x908 is zero.
constexpr std::size_t kProcessWeaponMagazineLoadedStateOffset = 0x908;
constexpr std::size_t kProcessWeaponAmmoDirtyStateOffset = 0x909;
constexpr std::uintptr_t kPlayerTryStartBumpActionRva = 0x0CE8E10;
// Weapon pickup animation relay. RequestInteract is the pickup-action start;
// OnInteract is retained only as a safety gate for replay completion.
constexpr std::uintptr_t kPickupRequestInteractRva = 0x08C8A40;
constexpr std::uintptr_t kWeaponPickupOnInteractRva = 0x157AA70;
constexpr std::uintptr_t kWeaponInteractControlVftableRva = 0x29BF4A8;
// Shared PickupInteractControl-family vtable +0xE0 HUD publisher. Its explicit
// third argument is the Player for whom the prompt is being built; screen-state
// routing alone is insufficient because the retained Player can map back onto
// the single visible campaign HUD.
constexpr std::uintptr_t kPickupInteractControlPublishHudPromptRva = 0x08AEA00;
// The script native hud-draw-interactable-on-object (0x140328C20) builds a
// fallback-keyed record without Player/object weak ownership and submits it at
// this exact callsite. Its first argument is the explicit per-screen HUD state.
constexpr std::uintptr_t kHudDrawInteractableRecordCallsiteRva = 0x03290FD;
constexpr std::uintptr_t kFhudPublishInteractRecordRva = 0x1359470;
// Kept for the read-only/exact-slot diagnostic implementation below. Runtime
// mutation at this owner boundary is rejected and no installer invokes it.
constexpr std::uintptr_t kFhudUpdateInteractScriptsCallsiteRva = 0x1356BF1;
constexpr std::uintptr_t kFhudUpdateInteractScriptsRva = 0x1357D60;
constexpr std::uintptr_t kFhudDestroyInteractScriptRva = 0x1357CA0;
constexpr std::uintptr_t kFhudPhysicalInteractStorePtrRva = 0x3EF1F20;
constexpr std::uintptr_t kFhudSecondaryInteractStorePtrRva = 0x3EF2020;
// Runtime-confirmed sole source of the authored M30 prompt. The source record
// carries the interactable's stock weak handle/generation at +0x18/+0x20; keep
// this exact callsite as a bounded receiver-local ProcessWeapon candidate index.
constexpr std::uintptr_t kPickupControlHudRecordCallsiteRva = 0x08AF179;
constexpr std::uintptr_t kPlayerRequestedInteractRva = 0x0C87D70;
// PlayerPickupController vtable +0x38 -> Start/Queue 0x140F49CA0. This is the
// proven producer of dra-pickup-* gestures from the interact-with-me script event.
constexpr std::uintptr_t kPlayerPickupStartVtableSlotRva = 0x2A34190;
constexpr std::uintptr_t kPlayerPickupStartRva = 0x0F49CA0;
// Builds the 0x15C-byte command/pose block consumed by Player_InteractDispatcher.
// The builder resolves the indexed Player and snapshots the per-screen +0x590
// command list; invoking the dispatcher without this block is not equivalent to
// a stock per-player interact tick.
constexpr std::uintptr_t kPlayerBuildInteractDispatchStateRva = 0x0C805A0;
constexpr std::uintptr_t kPlayerInteractDispatcherRva = 0x0C85D20;
constexpr std::array<std::uintptr_t, 4> kPlayerInteractDispatcherCallsiteRvas{
    0x0C8829B, 0x0CD4526, 0x0D74893, 0x0EC7CBA,
};
// Availability wrapper: calls NdInteractControl vtable+0x8. It does not start the
// interaction; the accepted path must still call control vtable+0x28 RequestInteract.
constexpr std::uintptr_t kInteractControlGetAvailabilityRva = 0x1C9A360;
// Player_ResolveWeaponPickupInteractAnim: resolves weapon-pickup-swap / when-no-weapon
// command id from the pickup art def (or the player's active weapon when pickup=null).
constexpr std::uintptr_t kPlayerResolveWeaponPickupInteractAnimRva = 0x0C7FF40;
// NdInteractControl::IsWithinInteractionRadius — radius/angle prep before interact start.
constexpr std::uintptr_t kInteractWithinRadiusRva = 0x1C937D0;
// Player-side dispatcher command gate: checks pressed/hold byte in the dispatch-state
// command table before RequestInteract / publish (NOT Player_PublishRequestedInteract).
constexpr std::uintptr_t kInteractDispatcherCommandGateRva = 0x0C88130;
// Player vtable +0x248 returns the per-player interact-control object used by the
// pickup dispatcher branch (sub_141C937D0 / InteractControl_GetAvailability).
constexpr std::size_t kPlayerGetInteractControlVtableOffset = 0x248;
constexpr std::uintptr_t kResolveGameObjectHandleByIdRva = 0x0A989C0;
constexpr std::uintptr_t kPlayerInteractDispatcherBaseRva = 0x3EC4EE0;
constexpr std::size_t kPlayerInteractDispatcherStride = 13760;
// Context-replay pickup animation (2026-07-12). Publisher writes the player-
// requested-interact descriptor into the TLS-ContextProcess actor's runtime state
// slot; the actor's anim graph reads it. Drive the puppet by switching context.
constexpr std::uintptr_t kPublishRequestedInteractRva = 0xC87D70;
constexpr std::uintptr_t kThreadContextGetCurrentRva = 0x16A2E80;  // no args -> proc
constexpr std::uintptr_t kThreadContextSetCurrentRva = 0x16A2ED0;  // (proc)
// PlayerWeaponController::GiveWeapon 0x1410A4D40 (vt+0x158) + 9: diagnostic hook
// point past the entry `test rdx,rdx; jz rel32` (which is not relocatable).
constexpr std::uintptr_t kGiveWeaponTailRva = 0x10A4D49;
// Return address (RVA) inside Player script-event handler sub_140DCDEA0 right
// after its `call [vt+0x158]` GiveWeapon call = the ground-pickup / scripted
// give-weapon event path (hash 0xFB1091A3 branch). Runtime-confirmed 2026-07-06:
// a real ground pickup logs exactly this ret_rva.
constexpr std::uintptr_t kPickupGiveRetRva = 0xDD4208;
// Native dropped-pickup payload constructor. Campaign GiveWeapon executes the
// local drop but skips SendNetEventAddDroppedPickup behind HIBYTE(g_NetModeFlags).
// Native Apply/ID-generation RVAs are deliberately not exposed to runtime code:
// calling them from retained Player update enters the unsafe global registry.
constexpr std::uintptr_t kNetEventAddDroppedPickupCtorFromProcessWeaponRva =
    0x0BCC9D0;
// ProcessKillTree 0x1416A5C20: destroys a weapon process object. Used with
// GetWeaponBySlot (controller vtable +0xB0) to free a manager slot before a
// relayed pickup give — otherwise the new long gun spills into the next slot and
// collides with the pistol (confirmed fountain root 2026-07-07).
constexpr std::uintptr_t kProcessKillTreeRva = 0x16A5C20;
// Ordinary campaign ground weapons use WeaponInteractControl, not
// WeaponPickupInteractControl. The accepted interaction consumes the exact
// source ProcessWeapon, while GiveWeapon detaches the replaced ProcessWeapon.
constexpr std::uintptr_t kWeaponInteractOnInteractRva = 0x1505BB0;
constexpr std::uintptr_t kWeaponInteractOnInteractVtableSlotRva = 0x29BF4B8;
constexpr std::uintptr_t kWeaponInteractSourceKillCallsiteRva = 0x1506839;
constexpr std::uintptr_t kProcessWeaponDetachRva = 0x1ED3630;
constexpr std::uintptr_t kPickupReplacementDetachCallsiteRva = 0x10A68AC;
// PlayerAimController::GetTargetPointFromReticle 0x140CB1720 (self-named by assert
// player-aim.cpp). Confirmed the dominant per-frame idx==1 caller of the pose-seed
// resolver during puppet ADS (122/frame). It reads the screen pose seed and returns
// the aim TARGET POINT that drives the visible upper-body aim pose. Arming the
// resolver's quat-only override around this call (owner==puppet) should make the
// puppet's visible aim follow the peer crosshair (same recipe as the fire caller).
constexpr std::uintptr_t kAimTargetFromReticleRva = 0xCB1720;
// Exact return site of the ResolveScreenRoutePoseSeedOrDefault call INSIDE
// GetTargetPointFromReticle (from the idx==1 caller histogram). Used to scope the
// resolver's pose-seed override to just the aim-target derivation - no trampoline,
// no sustained camera override.
constexpr std::uintptr_t kAimTargetReticleCallSiteRva = 0xCB17D6;
// Return site of ResolveScreenRoutePoseSeedOrDefault inside sub_1410E9210
// (Player per-frame update): native code vmovups resolver result into Player+0x1720
// and +0x1730 (aim pose). Top POSESEED-CALLERS hit ~150/frame during puppet ADS.
constexpr std::uintptr_t kPlayerAimPoseSeedCallSiteRva = 0x10E934B;
// PlayerAimController vtable +0x58 -> ApplyWorldAimBasis 0x140CAF110. This is
// the narrow native input to directional aim-additive weights: target point,
// body-forward, mode bytes, and four authored angular limits.
constexpr std::uintptr_t kPlayerAimWorldBasisVtableSlotRva = 0x2A0B250;
constexpr std::uintptr_t kPlayerAimWorldBasisRva = 0x0CAF110;
// PlayerAimController vtable +0x80 — skeleton aim origin (targHead/targPelvis).
constexpr std::uintptr_t kPlayerAimOriginPointRva = 0x0CAFD90;
constexpr std::uintptr_t kPlayerAimBlendTickVtableSlotRva = 0x2A0B228;
constexpr std::uintptr_t kPlayerAimBlendTickRva = 0x0CAFA20;
constexpr std::uintptr_t kPlayerAimBlendTargetCallsiteRva = 0x0CAFB7C;
constexpr std::uintptr_t kPlayerAimBlendTargetRva = 0x0CAE480;
// AnimNodeGesture::StepAngles is the once-per-frame-epoch consumer of the
// cached Gesture::ApplyJointReaderFunc closure. The closure owns the
// reference-local target vector derived from TargetLookAt at +0x30 and its
// Player owner at +0x28.
constexpr std::uintptr_t kAnimNodeGestureStepAnglesRva = 0x1FFEDB0;
constexpr std::uintptr_t kGestureApplyJointReaderFuncVtableRva = 0x2BBECC0;
// PlayerStrafeController (Player+0x1D50) concrete vtable +0x60. The Player
// post-animation-blend phase supplies the final routed-camera/reticle Locator;
// UpdateStrafeIk consumes its positive-Z forward.y for head/spine pitch.
constexpr std::uintptr_t kPlayerStrafeControllerVtableRva = 0x2A3FA88;
constexpr std::size_t
    kPlayerStrafeControllerUpdatePostAnimBlendSlotOffset = 0x60;
constexpr std::uintptr_t
    kPlayerStrafeControllerUpdatePostAnimBlendRva = 0x100C400;
constexpr std::size_t kPlayerStrafeControllerOwnerOffset = 0x108;
// Exact native pair used by Player_BuildStrafeIkReferenceLocator to turn
// forward/world-up into a Locator quaternion.
constexpr std::uintptr_t
    kBuildOrthonormalBasisFromTwoVectorsAndPositionRva = 0x15D23B0;
constexpr std::uintptr_t kQuaternionFromOrthonormalBasisRva = 0x2177E50;
// GunMoveController (Player+0x1DA8) vtable +0x38. Stock code resolves the
// owner's route camera and writes its camera-relative world direction to
// controller+0x150 before aim-idle turn-state selection.
constexpr std::uintptr_t kGunMoveControllerVtableRva = 0x2A280A8;
constexpr std::size_t kGunMoveControllerApplyCameraYawSlotOffset = 0x38;
constexpr std::uintptr_t kGunMoveControllerApplyCameraYawRva = 0x0E8F990;
constexpr std::uintptr_t kGunMoveControllerBuildActionStateRva = 0x0E8D140;
constexpr std::uintptr_t kGunMoveControllerInitialBuildActionStateCallsiteRva =
    0x0E8E8C1;
constexpr std::uintptr_t kGunMoveControllerUpdateBuildActionStateCallsiteRva =
    0x0E9063B;
constexpr std::size_t kGunMoveControllerOwnerOffset = 0x108;
constexpr std::size_t kGunMoveControllerTurnDirectionOffset = 0x150;
constexpr std::size_t kGunMoveControllerMoveMagnitudeOffset = 0x160;
constexpr std::uintptr_t kGunMoveUpdateTurnYawCallsiteRva = 0xE90605;
constexpr std::uintptr_t kGunMoveSelectAimIdleTurnCallsiteRva = 0xE9060F;
constexpr std::uintptr_t kGunMoveUpdateTurnYawRva = 0xE8FD00;
constexpr std::uintptr_t kGunMoveSelectAimIdleTurnRva = 0xE8F720;
// Ground GunMoveShield call to Player_CheckAimActionEligibility. A callsite
// relay can conditionally admit only the slot-1 puppet while preserving the
// original function and all downstream weapon-specific aim arguments.
constexpr std::uintptr_t kPlayerAimEligibilityCallsiteRva = 0x0E8573C;
constexpr std::uintptr_t kPlayerAimEligibilityRva = 0x10E3670;
// First call in PlayerGunMoveShield_UpdateAndDispatch. The callsite invokes the
// stock aim-balance refresh and is a safe boundary for read-only dispatch logging.
constexpr std::uintptr_t kPlayerGunMoveShieldEntryCallsiteRva = 0x0E84483;
constexpr std::uintptr_t kPlayerGunMoveShieldAimBalanceRefreshRva = 0x0E91C00;
// Player::GunMoveShield vtable +0x20 update wrapper. It calls the common
// dispatch only when the per-screen shield-drawn predicate succeeds.
constexpr std::uintptr_t kPlayerGunMoveShieldWrapperRva = 0x0E86930;
constexpr std::uintptr_t kScreenPlayerShieldDrawnPredicateRva = 0x0EFB750;
// PlayerAimController_ComputeDirectionalAimWeights calls the current-state
// additive permission query immediately before setting controller+0x8A0.
constexpr std::uintptr_t kPlayerAimAdditivePermissionCallsiteRva = 0x0CB348A;
constexpr std::uintptr_t kPlayerAimAdditivePermissionRva = 0x0CADF40;
// Player::Active main-view visibility transition. Player+0x1618 is the current
// visible request and +0x1619 is the previous request. A false request publishes
// DisableMainView to body attachments and all weapons, which is correct for the
// local scoped camera but not for the remote retained Player.
constexpr std::uintptr_t kPlayerMainViewVisibilityUpdateRva = 0x0C990A0;
// Exact inverse of IDrawControl_HideMesh: writes mesh entry +0x243 to 0xFF.
constexpr std::uintptr_t kDrawControlHideMeshRva = 0x1754510;
constexpr std::uintptr_t kDrawControlShowMeshRva = 0x1754A30;
// False branch of Script_SetInstanceFlagDrawInMainView. RDI still carries the
// explicit target object while RCX is target+0x198 DrawControl.
constexpr std::uintptr_t kDrawControlSetChildFlagsCallsiteRva = 0x03343D3;
constexpr std::uintptr_t kDrawControlSetChildFlagsByMaskRva = 0x1754EF0;
// Exact inverse used by script command set-instance-flag-draw-in-main-view:
// clears a mask from instance+0x18 for every DrawControl mesh child.
constexpr std::uintptr_t kDrawControlClearChildFlagsByMaskRva = 0x1754FF0;
// NdAttachableObject_PostJointUpdate_Async child/parent DrawControl visibility
// inheritance. The callsite passes child DrawControl in RCX and its resolved
// attachment-parent DrawControl in RDX.
constexpr std::uintptr_t kNdAttachableSyncParentVisibilityCallsiteRva =
    0x1ED63F7;
constexpr std::uintptr_t kDrawControlSyncParentVisibilityRva = 0x1754DA0;
// PlayerWeaponHolsterAction resolves the action's ProcessWeapon handle, looks up
// its authored weapon-holster-point record, then binds the copied handle through
// Player+0x1DF0 vtable +0xB8. The direct lookup callsite is instrumented together
// with the outer function so a missing record is distinguishable from a failed
// final bind without changing attachment state.
constexpr std::uintptr_t kPlayerWeaponHolsterResolveBindRva = 0x1095BD0;
constexpr std::uintptr_t kPlayerWeaponHolsterKnowledgeLookupCallsiteRva =
    0x1095E49;
constexpr std::uintptr_t kResolveKnowledgeRegistryEntryByIdRva = 0x1765030;
// PlayerPlayWeaponOverlayAction reaches PlayerWeaponController vtable +0x3C0.
// That lookup first calls the Chloe-base-only animation-id map, then searches
// the mapped record before falling back to the original id. The exact callsite
// relay and outer lookup hook form a bounded read-only selection discriminator.
constexpr std::uintptr_t kPlayerWeaponControllerMapChloeOverlayIdRva =
    0x109D450;
constexpr std::uintptr_t kPlayerWeaponControllerFindOverlayRecordRva =
    0x109D740;
constexpr std::uintptr_t kPlayerWeaponControllerMapChloeOverlayCallsiteRva =
    0x109D757;
// PlayerWeaponAnimControl is stored at Player+0x1DF0. Its vtable +0x28
// receives the final WeaponPartial animation id after the controller lookup
// and all caller-side fallbacks have completed.
constexpr std::uintptr_t kPlayerWeaponAnimControlVtableRva = 0x2A46778;
constexpr std::uintptr_t kPlayerWeaponAnimControlRequestWeaponPartRva =
    0x108C0C0;
constexpr std::uintptr_t kPlayerWeaponAnimControlRequestWeaponPartVtableOffset =
    0x08;
constexpr std::uintptr_t kPlayerWeaponAnimControlRequestCustomPartRva =
    0x108B940;
constexpr std::uintptr_t kPlayerWeaponAnimControlRequestCustomPartVtableOffset =
    0x28;
// AnimControl's type-2 phase loop has one direct call to
// AnimStateLayer_PhaseUpdateInstances. After the call, every live instance has
// either materialized or reused its SnapshotNodeHeap and the command generator
// has not run yet. At the callsite RBX is this AnimControl and RSI is the exact
// registry index, so a relay can prove layer ownership without a global scan.
constexpr std::uintptr_t kAnimControlPhaseUpdateType2LayerCallsiteRva =
    0x172CB32;
constexpr std::uintptr_t kAnimStateLayerPhaseUpdateInstancesRva = 0x189D720;
constexpr std::uintptr_t kAnimControlFindActiveType2LayerByIdRva = 0x172B3E0;
constexpr std::uintptr_t kAnimSnapshotNodeAnimationResolveClipRva = 0x1918070;
// AnimStateLayer::TakeTransitions consumes the four-entry request queue at
// +0x4D4. Its normal transition path either creates/fades a new state instance
// or loops the existing current instance when the requested state already
// matches. The read-only pre/post census distinguishes those exact outcomes.
constexpr std::uintptr_t kAnimStateLayerTakeTransitionsRva = 0x18A1350;
// AnimStateLayer::UpdateBlendWeights is the sole per-step consumer of the two
// fade timer/duration pairs at AnimStateInstance +0x150/+0x154 and
// +0x15C/+0x160. It publishes the corresponding weights at +0x158/+0x164
// before PhaseUpdate and the second completed-instance pruning pass.
constexpr std::uintptr_t kAnimStateLayerUpdateBlendWeightsRva = 0x18A3820;
// Final visible-pose generation boundary used by the scoped legacy-Nadine
// Mazur repair. AsyncObjectUpdateJointBlendJob rebuilds and publishes the
// palette consumed by rendering. CreateAnimCmdsForObject returns only after it
// has appended the full per-group command suffix, so the repair can validate
// capacity and every late command before inserting a partial pose at cmd3.
constexpr std::uintptr_t kAnimManagerCreateAnimCmdsForObjectRva = 0x1A8FCF0;
// Exact normal-builder CALL inside BuildCommandSegmentsForObjectMask.  The
// callsite is used instead of detouring the 16-byte CreateAnimCmds prologue so
// the repair cannot race animation workers across a partially written entry
// jump.  Its stock bytes are E8 EC E6 FF FF.
constexpr std::uintptr_t kAnimManagerCreateAnimCmdsNormalCallsiteRva =
    0x1A915FF;
// Source-identified PerformAnimBlend(AsyncUpdateJobData*). This is the normal
// command-DAG producer that writes the persistent FgAnimData+0x100 local pose
// before Player post-blend and the later Async joint-palette rebuild.
constexpr std::uintptr_t kAnimManagerPerformAnimBlendRva = 0x1A92260;
// Sole normal Perform call in AnimManager's post-blend worker.  The wrapper
// scopes the nested CreateAnimCmds call above without touching either function
// entry.  Its stock bytes are E8 56 F9 FF FF.
constexpr std::uintptr_t kAnimManagerPerformAnimBlendCallsiteRva = 0x1A92905;
constexpr std::array<std::uint8_t, 5>
    kAnimManagerPerformAnimBlendCallsiteStockBytes{
        0xE8, 0x56, 0xF9, 0xFF, 0xFF};
// Normal inline ProcessAnimCmdListSegment call. The offline-generated-row P99
// transfer
// no longer hooks this site; its exact retail E8 bytes are a negative installer
// witness that the rejected command-parser path remains stock.
constexpr std::uintptr_t kAnimManagerProcessAnimCmdListSegmentCallsiteRva =
    0x1A92759;
constexpr std::array<std::uint8_t, 5>
    kAnimManagerProcessAnimCmdListSegmentCallsiteStockBytes{
        0xE8, 0x62, 0x70, 0x02, 0x00};
// Exact six-byte indirect Player vtable+0x408 call in the same worker, after
// the Perform call above and before AsyncObjectUpdateJointBlendJob.  A scoped
// callsite relay can run stock post-blend first and then restore the exact
// Perform-produced C39 arm rows without modifying the shared Player vtable.
// Stock bytes: FF 90 08 04 00 00.
constexpr std::uintptr_t kAnimManagerPlayerPostBlendCallsiteRva = 0x1A929A1;
constexpr std::array<std::uint8_t, 6>
    kAnimManagerPlayerPostBlendCallsiteStockBytes{
        0xFF, 0x90, 0x08, 0x04, 0x00, 0x00};
constexpr std::uintptr_t kAnimManagerAsyncObjectUpdateJointBlendJobRva =
    0x1A92A00;
// Cross-skeleton animation command case 6 resolves the exact source and
// target skeletons, then calls RetargetAnimUsingBatchMgr.  Its nested
// retarget post-pose plugin calls AddJointRetargetCommands while
// SegmentContext+0x50 still points at the source JointParams.  The two exact
// E8 callsites let a read-only census bind a live ArtItemAnim to those source
// rows without detouring either hot function entry.
constexpr std::uintptr_t kProcessAnimCmdRetargetCallsiteRva = 0x1AB9FC4;
constexpr std::array<std::uint8_t, 5>
    kProcessAnimCmdRetargetCallsiteStockBytes{
        0xE8, 0xF7, 0x12, 0xE8, 0xFF};
constexpr std::uintptr_t kRetargetAnimUsingBatchMgrRva = 0x193B2C0;
constexpr std::uintptr_t kRetargetPostPoseAddCallsiteRva = 0x1937DBA;
constexpr std::array<std::uint8_t, 5>
    kRetargetPostPoseAddCallsiteStockBytes{
        0xE8, 0xD1, 0xE6, 0xFF, 0xFF};
constexpr std::uintptr_t kRetargetPluginAddJointRetargetCommandsRva =
    0x1936490;
// Native value-only helpers used by the state-2 P99 Chloe-space transfer.
// ResolveArtItemSkeleton and FindRetargetEntry are read-only registry lookups.
// DoJointRetarget evaluates one already-built 0x2e record without invoking
// BatchMgr, while the blend helper applies the engine's exact ValidBits/mode-2
// transform math to local buffers.
constexpr std::uintptr_t kSkelTableResolveArtItemSkeletonRva = 0x17CD100;
constexpr std::uintptr_t kSkelTableFindRetargetEntryRva = 0x17D0BD0;
// ResolveArtItemSkeleton returns a borrowed pointer.  The SkelTable registry
// owns a separate epoch and a 64-bit 0/-1 spin lock; P99 snapshots use a
// single non-blocking CAS and copy every borrowed operand before releasing it.
constexpr std::uintptr_t kSkelTableRegistryEpochRva = 0x38401CC;
constexpr std::uintptr_t kSkelTableRegistryBucketsPtrRva = 0x66EC3E0;
constexpr std::uintptr_t kSkelTableRegistrySentinelPtrRva = 0x66EC440;
constexpr std::uintptr_t kSkelTableRegistryBucketCountRva = 0x66EC470;
constexpr std::uintptr_t kSkelTableRegistryInitializedRva = 0x66EC474;
constexpr std::uintptr_t kSkelTableRegistryLockRva = 0x66EC4B0;
constexpr std::uint32_t kSkelTableRegistryExpectedBucketCount = 384;
constexpr std::uintptr_t kSkelTableRetargetEntryCountRva = 0x38401D8;
constexpr std::uintptr_t kSkelTableRetargetEntriesRva = 0x3864A00;
constexpr std::uintptr_t kRetargetPluginDoJointRetargetRva = 0x1AE2100;
constexpr std::uintptr_t kAnimJointParamsBlendTransformsByValidBitsRva =
    0x2188A30;
constexpr std::uintptr_t kAnimMasterTableArtGroupRegistryEpochRva =
    0x38401BC;
// Read-only final holster discriminator. The anim-control event hook exposes
// authored named events, the binding builder exposes the selected gun_holster*
// id and offsets, and the final mutation hook proves whether the state-script
// path reaches the concrete owner attachment operation.
constexpr std::uintptr_t kPlayerWeaponAnimNamedEventRva = 0x108DE30;
// Player holster IK is a separate skeleton/arm path from the ProcessWeapon
// attachment. The outer update scopes the exact PlayerWeaponAnimControl; its
// blender extracts the authored hand/leg/back reference record used to choose
// the leg or pelvis target basis.
constexpr std::uintptr_t kPlayerWeaponAnimControlUpdateHolsterIkRva =
    0x108ECE0;
// The generic reducer returns the final custom-data record after all live
// snapshots and transitions have been blended. Hooking this boundary avoids
// mistaking one intermediate ExtractData source for the IK result actually
// consumed by PlayerWeaponAnimControl_UpdateHolsterIk.
constexpr std::uintptr_t kAnimStateLayerExtractAndBlendCustomDataRva =
    0x108F800;
// Shared Player move-action sequencer. Its phase update owns the first
// PlayerGetWeaponInHandAction / PlayerPutGunAwayAction vtable dispatch and the
// later phase-specific checks, so a before/after detour distinguishes a queued
// action from one that actually reached OnStart without invoking engine code.
constexpr std::uintptr_t kPlayerMoveSequencerUpdateRva = 0x2051EC0;
// Exact Player phase-zero call: mov rcx,[Player+0x3BD0], xor edx,edx,
// call PlayerMoveSequencer_UpdatePhase.
constexpr std::uintptr_t kPlayerMoveSequencerPhase0CallsiteRva = 0x0CA4B85;
// PlayerCommonAction vtable slot +0x10. PlayerMoveSequencer sets lifecycle
// +0x18 from 0 to 1 immediately before dispatching this exact OnStart slot.
constexpr std::uintptr_t kPlayerCommonActionOnStartVtableSlotRva = 0x2A076A0;
constexpr std::uintptr_t kPlayerCommonActionOnStartRva = 0x0C59180;
constexpr std::uintptr_t kWeaponHolsterBuildBindingRva = 0x14FE540;
constexpr std::uintptr_t kWeaponAttachmentAttachToOwnerPointRva = 0x1ED0370;
// Stock Player weapon-marker path. AnimActionMgr calls the Player factory for
// weapon-holster, the one-shot action executes the transition, and weapon
// vtable +0x7A8 selects a record in the existing Player owner's AttachSystem.
constexpr std::uintptr_t kPlayerHolsterWeaponAnimActionExecuteRva = 0x108A640;
constexpr std::uintptr_t kPlayerHolsterWeaponAnimActionVtableRva = 0x2A46560;
constexpr std::uintptr_t kPlayerHolsterWeaponAnimActionUpdateTailCallsiteRva =
    0x108A5F9;
constexpr std::uintptr_t kPlayerHolsterWeaponAnimActionTransitionCallsiteRva =
    0x108A62B;
constexpr std::uintptr_t kWeaponAttachmentSelectExistingOwnerPointRva =
    0x1ED0580;
// ProcessWeapon vtable +0x410 (0x141515860) directly calls the shared
// NdAttachableObject base update here. Relaying this one E8 observes only the
// ProcessWeapon final-transform boundary and leaves the global base function
// untouched.
constexpr std::uintptr_t kProcessWeaponPostJointBaseCallsiteRva = 0x1515A30;
constexpr std::uintptr_t kNdAttachableObjectPostJointUpdateRva = 0x1ED28E0;
// NdAttachableObject_ResolveSelectedAttachmentTransform has already resolved
// the selected AttachSystem record into a 32-byte world transform when it
// reaches this direct E8. The callee either copies that transform unchanged
// (object+0x68C == -1) or applies the object's mesh-local/depenetration
// correction, so a pass-through relay separates record placement from the
// last object-local adjustment without touching attachment state.
constexpr std::uintptr_t kNdAttachableObjectLocalTransformCallsiteRva =
    0x1ED19BB;
constexpr std::uintptr_t kNdAttachableObjectApplyLocalTransformRva =
    0x1ED0A70;
constexpr std::uintptr_t kAttachSystemResolveRecordTransformRva = 0x18B7060;
// Final compose inside the source-identified "Animating" update. Its R8 is
// exactly object+0x130, after the selected-attachment update and any later
// animation-layer overwrite, immediately before FgAnimData publication.
constexpr std::uintptr_t kNdAttachableObjectFinalLocalComposeCallsiteRva =
    0x1ED287B;
// Read-only active-grip discriminator. GunMoveController vtable +0xB8 reaches
// 0x140E909C0, which calls ArmBlockProbes::Kick(Player*, bool). The probe logs
// its completed output beside the active rightHand attach record so authored
// attachment placement can be separated from arm-pose/retarget output.
constexpr std::uintptr_t kArmBlockProbesKickRva = 0x0C2BCB0;
// AiWeaponController vtable +0x170. Native NPC firearm attachment state applies
// here (rightHand versus pistolHolster/longgunHolster).
constexpr std::uintptr_t kAiWeaponControllerEnforceWeaponAttachmentRva =
    0x05AE580;
constexpr std::uintptr_t kAiWeaponControllerVtableRva = 0x29673C8;
constexpr std::size_t kAiWeaponControllerEnforceWeaponAttachmentVtableOffset =
    0x170;
// Cached WeaponArtDef registry and generic id lookup used by
// WeaponHolster_ResolveAttachPoint. The runtime probe reads the art record's
// +0xD8 holster-id list; neither helper mutates the weapon or Player.
constexpr std::uintptr_t kGetCachedWeaponArtDefsHashRegistryRva = 0x14FA380;
constexpr std::uintptr_t kLookupRegistryRecordPointerByIdRva = 0x1614E40;
// Stock AttachSystem repair path for legacy Nadine's filtered-out firearm
// attachment records. The two transform helpers convert a Chloe marker-local
// record to an equivalent transform relative to a joint shared by both rigs;
// AppendRecord then uses the existing engine-owned spare capacity.
constexpr std::uintptr_t kAttachSystemAppendRecordRva = 0x18B6E50;
constexpr std::uintptr_t kRigGetJointTransformRva = 0x1687C90;
constexpr std::uintptr_t kTransformMakeRelativeRva = 0x01990F0;
constexpr std::uintptr_t kTransformComposeRva = 0x01998F0;
// Stock 32-byte transform interpolation used by the weapon attachment handoff:
// linear translation plus shortest-path quaternion slerp.
constexpr std::uintptr_t kTransformInterpolateTranslationAndRotationRva =
    0x0221550;
// RTTI-backed ProcessWeapon vtable. Runtime exact-parent census identifies
// pistol/rifle/grenade children by this class and authored art at +0x860.
constexpr std::uintptr_t kProcessWeaponVtableRva = 0x2A9BF48;
// PlayerWeaponController exact RTTI vtable and the two stock grenade-loadout
// boundaries used by FastGrenadeController. CreateAndAddWeapon is vtable +0x130;
// BindGrenadeWeaponSlot is FastGrenadeController vtable +0xF0.
constexpr std::uintptr_t kPlayerWeaponControllerVtableRva = 0x2A47F90;
constexpr std::uintptr_t kPlayerWeaponControllerCreateAndAddWeaponRva =
    0x10A8920;
constexpr std::uintptr_t kPlayerGrenadeControllerBindWeaponSlotRva = 0x0E17720;
constexpr std::uintptr_t kProcessWeaponGetTotalAmmoRva = 0x1103850;
constexpr std::uintptr_t kProcessWeaponInitializeAmmoFromTotalRva = 0x1517C90;
// ProcessWeapon_Initialize 0x14150F640 calls ProcessWeaponBase::Init exactly
// once at 0x14150F6AF. This cold boundary still owns the original attach-init
// descriptor (context+0x28) before later draw/put actions can rewrite it.
constexpr std::uintptr_t kProcessWeaponBaseInitRva = 0x14FAF40;
constexpr std::uintptr_t kProcessWeaponBaseInitCallsiteRva = 0x150F6AF;
// ProcessWeapon's TypeFactor/factory vtable +0x58 callback at 0x141103B70
// calls the complete ProcessWeapon_Initialize exactly once here.  Returning
// from this E8 means the strap child has already been materialized, while the
// generic materializer has not yet called NdDrawableObject::AllocateSnapshot
// through factory vtable +0x1A8.
constexpr std::uintptr_t kProcessWeaponInitializeRva = 0x150F640;
constexpr std::uintptr_t kProcessWeaponFactoryInitializeCallsiteRva =
    0x1103B7E;
// PlayerWeaponController vtable +0x3D0. The stock Player holster AnimAction
// calls this with its current ProcessWeapon; it builds the authored holster
// binding, selects the existing owner point and copies the binding-local
// transform into weapon+0x4F0/+0x500.
constexpr std::uintptr_t kPlayerWeaponControllerRebindWeaponToHolsterRva =
    0x10A9670;
// PlayerWeaponController vtable +0x48. This is the complete stock switch
// request used by both Draw and Put; its Draw branch enqueues the Get and IK
// actions and enters kStateDraw before the per-frame controller can reach
// kStateWeaponOut.
constexpr std::uintptr_t kPlayerWeaponControllerApplyWeaponSwitchRva =
    0x10A28A0;
constexpr std::uintptr_t kProcessWeaponSelectExistingOwnerPointVtableSlotRva =
    kProcessWeaponVtableRva + 0x7A8;
constexpr std::uintptr_t kProcessWeaponUpdateSelectedAttachmentVtableSlotRva =
    kProcessWeaponVtableRva + 0x7B8;
// Calls the selected resolver/0x141ED19BB correction, then applies the optional
// LongGunJiggle and writes the final post-jiggle local transform to +0x130/+0x140.
constexpr std::uintptr_t kNdAttachableObjectUpdateSelectedAttachmentRva =
    0x1ED1380;
// ProcessWeaponBase stores an RTTI LongGunJiggle object at weapon+0x838.
// Its vslot +0x28 is called by ProcessWeaponBase_Update 0x141500880 before
// the stock local/selected attachment update. Passing false disables physics
// and resets the jiggle displacement/angular state to its authored defaults.
constexpr std::uintptr_t kLongGunJiggleVtableRva = 0x2A9AF48;
constexpr std::uintptr_t kLongGunJiggleSetEnabledAndResetRva = 0x1F7EA80;
constexpr std::size_t kLongGunJiggleSetEnabledAndResetVtableOffset = 0x28;
constexpr std::size_t kLongGunJiggleSize = 0xE0;
// RTTI-backed ProcessCharCloth vtable. At the callsite above RBX still owns the
// NdAttachableObject, so the relay can distinguish cloth from other attachments.
constexpr std::uintptr_t kProcessCharClothVtableRva = 0x2BAF780;
constexpr std::size_t kWeaponControllerGetWeaponBySlotVtOffset = 0xB0;
constexpr std::uintptr_t kWeaponPickupVftableRva = 0x2AA66C0;
constexpr std::size_t kWeaponPickupGetInteractControlVtableOffset = 0x248;
// WeaponPickupInteractControl::OnInteract resolves the pickup at ctrl+0x10
// (sub_141C977A0); the +0x80 block is the lazily-filled interactable cache.
constexpr std::size_t kWeaponPickupInteractPickupPtrOffset = 0x10;
// Global manager table slot 22 owns the game-object handle registry used by
// ResolveGameObjectHandleById.
constexpr std::uintptr_t kGlobalManagerTableRva = 0x6B17D20;
constexpr std::size_t kGameObjectRuntimeIdOffset = 0xBC;
constexpr std::uintptr_t kPlayerBumpControllerUpdateVtableSlotRva = 0x2A0EF08;
constexpr std::uintptr_t kNetEventCharacterMoveCtorRva = 0x0BBD730;
constexpr std::uintptr_t kFillAnimCmdBitStreamRva = 0x0BC4E60;
constexpr std::uintptr_t kNetSnapshotCtorRva = 0x0B87B00;
constexpr std::uintptr_t kCharacterMoveToSnapshotRva = 0x0BC11A0;
constexpr std::uintptr_t kNetAnimAddSnapshotRva = 0x0A01E50;
constexpr std::uintptr_t kInitializePlayerNetAnimArtGroupsRva = 0x10D6530;
constexpr std::uintptr_t kEngineClockPointerRva = 0x37E0250;
constexpr std::uintptr_t kEngineClockScaleRva = 0x2BE8AE8;
// REJECTED 2026-07-15: this entire path is AudioManager (gamelib/audio/arm.cpp),
// not a character skeleton renderer. Constants remain only for disabled historical
// probes and must not be used as an aiming/skinning surface.
constexpr std::uintptr_t kSkelTableGlobalRva = 0x6919C40;
constexpr std::uintptr_t kCharacterSkeletonRenderJobEntryRva = 0x1B5CE90;
constexpr std::uintptr_t kCharacterSkeletonRenderUpdateRva = 0x1B61530;
// E8 call inside AudioManager_RenderJobEntry immediately before AudioManager_Render.
constexpr std::uintptr_t kCharacterSkeletonRenderUpdateCallsiteRva = 0x1B5CEA6;
constexpr std::size_t kSkelTableSkipRenderFlagOffset = 0x81;
// These offsets belong to AudioManager/ARM state, not FgAnimData matrices.
constexpr std::size_t kSkelTablePalettePtrOffset = 0x230;
constexpr std::size_t kSkelTableSkinningPtrOffset = 0x560;
constexpr std::size_t kSkelTableJointWorkspaceBaseOffset = 0x14B8;
constexpr std::size_t kSkelTableJointWorkspaceStride = 0x6C0;
// Historical audio-entry probe offsets, retained while the rejected code is parked.
constexpr std::size_t kSkelJointWorkspaceJointIdOffset = 0x0;
constexpr std::size_t kSkelJointWorkspaceInstancePtrOffset = 0xB8;
constexpr std::size_t kSkelJointWorkspaceMatrixBaseOffset = 0x558;
constexpr std::uintptr_t kCharacterSkeletonEvalPerJointRva = 0x1B757F0;
constexpr std::uintptr_t kCharacterSkeletonEvalPerJointCallsiteRva = 0x1B63CE6;
// PlayerUpper body draw path (look builder): object+0x198 = IDrawControl.
constexpr std::size_t kPlayerDrawControlOffset = 0x198;
constexpr std::uint32_t kSlot1ScreenIndex = 1;
constexpr float kGoalDistance = 3.0F;
constexpr float kRemoteGoalDistance = 1.25F;

#endif  // TLL_HOOK_OFFSETS_H
