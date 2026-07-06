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
// Weapon-pickup relay (2026-07-06, see initial-findings). OnInteract named by its
// own assert in weapon-pickup.cpp; the Apply drives net-pick-up-other-client.
constexpr std::uintptr_t kWeaponPickupOnInteractRva = 0x157AA70;
// PlayerWeaponController::GiveWeapon 0x1410A4D40 (vt+0x158) + 9: diagnostic hook
// point past the entry `test rdx,rdx; jz rel32` (which is not relocatable).
constexpr std::uintptr_t kGiveWeaponTailRva = 0x10A4D49;
// Return address (RVA) inside Player script-event handler sub_140DCDEA0 right
// after its `call [vt+0x158]` GiveWeapon call = the ground-pickup / scripted
// give-weapon event path (hash 0xFB1091A3 branch). Runtime-confirmed 2026-07-06:
// a real ground pickup logs exactly this ret_rva.
constexpr std::uintptr_t kPickupGiveRetRva = 0xDD4208;
constexpr std::uintptr_t kNetEventSyncWeaponPickupsApplyRva = 0xBCE440;
constexpr std::uintptr_t kNetEventSyncWeaponPickupsVftableRva = 0x29FCF90;
constexpr std::uintptr_t kWeaponPickupVftableRva = 0x2AA66C0;
// WeaponPickupInteractControl::OnInteract resolves the pickup at ctrl+0x10
// (sub_141C977A0); the +0x80 block is the lazily-filled interactable cache.
constexpr std::size_t kWeaponPickupInteractPickupPtrOffset = 0x10;
// Global manager table used by NetEventSyncWeaponPickups_Apply: managers indexed
// by type; slot 22 = the game-object handle registry consumer. Pre-check non-null
// before calling Apply (single-player init state unknown).
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
