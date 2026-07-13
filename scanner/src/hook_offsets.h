// hook_offsets.h — TLL co-op hook: module-base-relative addresses (RVAs), struct
// field offsets, and timing/tunable constants for tll.exe (1.4.21058.0).
//
// SINGLE SOURCE OF TRUTH for the reverse-engineered addresses used by the injected
// hook. Every value is documented at its declaration; keep it in sync with the IDA
// database (ida/tll.i64) and reports/initial-findings.md. `constexpr` at namespace
// scope has internal linkage, so this header is included by teleport_hook.cpp only
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
constexpr std::size_t kPlayerQuatOffset = 0x140;              // orientation quaternion
constexpr std::size_t kPlayerLocalPosOffset = 0x130;          // local position vec4
constexpr std::size_t kPlayerLocalRootMirrorOffset = 0x250;   // local/world mirror
constexpr std::size_t kPlayerCollisionRootOffset = 0x280;     // capsule / collision root
// NdGameObject vtable +0x2B8: derives +0x240/+0x250/+0x280 from +0x130/+0x140.
constexpr std::uintptr_t kNdGameObjectRefreshWorldTransformRva = 0x1BB8320;
// Player animation runtime. Player+0x1A8 is FgAnimData*; async phase bits in the
// first DWORD select owner vtable +0x400/+0x408/+0x410 for bits 8/0x10/0x20.
constexpr std::size_t kPlayerFgAnimDataOffset = 0x1A8;
constexpr std::size_t kPlayerUpperStateOwnerOffset = 0x1A0;
constexpr std::size_t kFgAnimDataFlagsOffset = 0;
constexpr std::size_t kFgAnimDataStateOffset = 4;
constexpr std::size_t kFgAnimDataUpperStateOwnerOffset = 0x10;
constexpr std::size_t kFgAnimDataRetainedHandleOffset = 0x1A8;
constexpr std::size_t kFgAnimDataRetainedIdOffset = 0x1B0;
constexpr std::size_t kFgAnimDataRuntimePayloadOffset = 0x230;
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
// PlayerFireController::FireArgs pose-seed block written by BuildFireArgs (+0x100).
constexpr std::size_t kFireArgsPoseSeedOffset = 0x100;
// PLAYER-FIRE runtime dumps: aim orientation quat at +0x6C (indices 27..30).
constexpr std::size_t kFireArgsAimQuatOffset = 0x6C;
// CameraManager_GetBlendedCameraTransform out buffer: 4x4 matrix at +0x350.
constexpr std::size_t kCameraOutMatrixOffset = 0x350;
// Route-entity screen index field read inside GetBlendedCameraTransform.
constexpr std::size_t kRouteEntityScreenIndexOffset = 0x124;
constexpr std::size_t kPlayerAdsActiveOffset = 0x1548;
// PlayerFireController_Fire writes FireArgs-derived aim vectors here during fire;
// also populated during ADS (initial-findings field diff +0x1570..+0x1588).
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
constexpr std::size_t kWeaponCtrlCurrentSlotOffset = 0x120;  // int32; -1 = none
// Draw state machine: 0 = holstered/idle, 2 = drawn (no ADS), 4 = ADS,
// 1/5 = draw/holster transitions.
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
// Clean remote-Player feasibility test: capture only the normal slot-0 spawn
// arguments. This installs one execute breakpoint that disarms itself after the
// capture; it does not start the old diagnostic thread or input/camera probes.
constexpr bool kEnableSpawnCaptureDiagnostic = true;
constexpr std::uintptr_t kAddJoypadDisabledCommandsRva = 0x1839FB0;
constexpr std::uintptr_t kInputScaleCopyRva = 0x183B410;
constexpr std::uintptr_t kGameInfoVtableRva = 0x29040B8;
constexpr std::uintptr_t kPlayerFireControllerFireRva = 0x0E2C730;
// Sole caller of PlayerFireController_Fire; seeds aim ray via pose-seed resolver.
constexpr std::uintptr_t kBuildFireArgsAndFireRva = 0x0E2BCD0;
constexpr std::uintptr_t kPlayerFireControllerTryStartFireRva = 0x0E2ACA0;
constexpr std::uintptr_t kPlayerFireControllerCheckShotReadinessRva = 0x0E2B220;
constexpr std::uintptr_t kPlayerTryStartBumpActionRva = 0x0CE8E10;
// Weapon pickup animation relay. RequestInteract is the pickup-action start;
// OnInteract is retained only as a safety gate for replay completion.
constexpr std::uintptr_t kPickupRequestInteractRva = 0x08C8A40;
constexpr std::uintptr_t kWeaponPickupOnInteractRva = 0x157AA70;
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
// ProcessKillTree 0x1416A5C20: destroys a weapon process object. Used with
// GetWeaponBySlot (controller vtable +0xB0) to free a manager slot before a
// relayed pickup give — otherwise the new long gun spills into the next slot and
// collides with the pistol (confirmed fountain root 2026-07-07).
constexpr std::uintptr_t kProcessKillTreeRva = 0x16A5C20;
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
// Ground GunMoveShield call to Player_CheckAimActionEligibility. A callsite
// relay can conditionally admit only the slot-1 puppet while preserving the
// original function and all downstream weapon-specific aim arguments.
constexpr std::uintptr_t kPlayerAimEligibilityCallsiteRva = 0x0E8573C;
constexpr std::uintptr_t kPlayerAimEligibilityRva = 0x10E3670;
// PlayerAimController_ComputeDirectionalAimWeights calls the current-state
// additive permission query immediately before setting controller+0x8A0.
constexpr std::uintptr_t kPlayerAimAdditivePermissionCallsiteRva = 0x0CB348A;
constexpr std::uintptr_t kPlayerAimAdditivePermissionRva = 0x0CADF40;
// Exact inverse used by script command set-instance-flag-draw-in-main-view:
// clears a mask from instance+0x18 for every DrawControl mesh child.
constexpr std::uintptr_t kDrawControlClearChildFlagsByMaskRva = 0x1754FF0;
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
constexpr std::uint32_t kSlot1ScreenIndex = 1;
constexpr float kGoalDistance = 3.0F;
constexpr float kRemoteGoalDistance = 1.25F;

#endif  // TLL_HOOK_OFFSETS_H
