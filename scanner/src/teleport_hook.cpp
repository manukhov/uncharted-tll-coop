#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <TlHelp32.h>
#include <intrin.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "coop_protocol.h"

namespace {

constexpr std::uintptr_t kNadineBuddyVtableRva = 0x2949638;
constexpr std::uintptr_t kDisableBuddyTeleportRva = 0x3D441AD;
constexpr std::uintptr_t kBuildNavigationFrameRva = 0x1DF2D60;
constexpr std::uintptr_t kMoveToRva = 0x3F6890;
constexpr std::uintptr_t kBeginNpcCommandRva = 0x3F5B60;
constexpr std::uintptr_t kType4CommandRva = 0x3F69B0;
constexpr std::uintptr_t kRequestEnableAiRva = 0x3F5C80;
constexpr std::uintptr_t kRequestDisableAiRva = 0x3F5D30;
constexpr std::uintptr_t kGetCameraManagerRva = 0x1C06420;
constexpr std::uintptr_t kGetCurrentCameraRva = 0x1C0D4B0;
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
constexpr std::size_t kLocomotionControllerOffset = 0x15B8;
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

using UpdateFunction = void(__fastcall*)(void*);
using BuildNavigationFrameFunction =
    void(__fastcall*)(void*, const void*, std::uint32_t, float);
using MoveToFunction = void(__fastcall*)(
    void*,
    const void*,
    const void*,
    const void*,
    std::uint32_t,
    bool,
    bool,
    void*,
    std::int32_t,
    bool
);
using BeginNpcCommandFunction = void(__fastcall*)(void*, std::uint32_t, void*, void*, std::uint8_t);
using Type4CommandFunction = void(__fastcall*)(
    void*,
    std::int32_t,
    const void*,
    std::int32_t,
    std::uint8_t,
    std::uint8_t,
    void*,
    std::int32_t,
    bool
);
using RequestEnableAiFunction = void(__fastcall*)(void*, bool);
using RequestDisableAiFunction = void(__fastcall*)(void*, bool, bool);
using GetCameraManagerFunction = void*(__fastcall*)(std::uint32_t);
using GetCurrentCameraFunction = void*(__fastcall*)(void*, bool);
using GetCameraTransformFunction = void*(__fastcall*)(void*, void*);
using DirectAttachCameraRetainedBindingFunction = void(__fastcall*)(void*);
using ToggleCameraRetainedBindingFunction = bool(__fastcall*)(void*);
using ResolveLocalPlayerFunction = void*(__fastcall*)(void*, std::uint32_t);
using SetControlActionStateFunction = void(__fastcall*)(void*, std::uint32_t);
using ScriptQueryFunction = void*(__fastcall*)(void*);
using ScriptCommandFunction = void*(__fastcall*)(void*);
using AddOrUpdateNetPlayerTrackerFunction =
    void*(__fastcall*)(void*, void*, const void*, std::uint32_t, std::uint8_t);
using GetNetPlayerTrackerByScreenIndexFunction =
    void*(__fastcall*)(void*, std::uint32_t);
using BuildRespawnTransformBlockFunction =
    void*(__fastcall*)(void*, const void*, const void*);
using NetPlayerTrackerNetRespawnFunction =
    void(__fastcall*)(void*, std::int32_t, std::uint8_t, const void*);
using StringHashFunction = std::uint32_t(__fastcall*)(const char*, std::uint64_t, std::uint64_t);
using ResolveUpperActorRecordFunction = void*(__fastcall*)(std::uint32_t);

UpdateFunction g_original_update = nullptr;
using PlayerInputStateUpdateFunction = char(__fastcall*)(void*);
PlayerInputStateUpdateFunction g_original_player_input_state_update = nullptr;
using AddJoypadDisabledCommandsFunction = std::int64_t(__fastcall*)(
    void*, std::int64_t, int, int, const std::uint32_t*, int);
AddJoypadDisabledCommandsFunction g_original_add_joypad_disabled_commands = nullptr;
using InputScaleCopyFunction = std::int64_t(__fastcall*)(void*, void*, std::uint32_t);
using GameInfoKeyboardModeFunction = bool(__fastcall*)(void*);
using EvaluateKeyboardButtonBindingFunction = char(__fastcall*)(
    void*, void*, std::uint32_t);
InputScaleCopyFunction g_original_input_scale_copy = nullptr;
GameInfoKeyboardModeFunction g_original_gameinfo_keyboard_mode = nullptr;
EvaluateKeyboardButtonBindingFunction g_original_evaluate_keyboard_button_binding =
    nullptr;
std::atomic<DWORD> g_force_joypad3_analog_thread{0};
std::atomic<std::uint64_t> g_force_joypad3_analog_hits{0};
SRWLOCK g_joypad3_analog_lock = SRWLOCK_INIT;
SRWLOCK g_joypad_disabled_commands_lock = SRWLOCK_INIT;
// DIAGNOSTIC toggle (VK_HOME): INERT = DLL no-op pass-through. Default ACTIVE.
volatile bool g_dll_inert = false;
// PROVEN: nulling the clone's retained-slot pointer to hide it from single-player
// player-count gating CRASHES (the engine holds/derefs the slot). Default OFF.
volatile bool g_hide_clone_from_slot = false;
void* g_clone_cached = nullptr;

// Player vtable[92] (sub_140A1DCC0) = the APPEARANCE-hash getter (returns
// object+0x15A4 into *a2), fed to the look builder -> appearance-record -> mesh.
// We hook it so the clone's look build resolves NADINE's appearance (0xd27c074f =
// hash("nadine")) instead of Chloe's (0x3f334972). Gated to the clone spawn window
// so Chloe's own look is untouched.
constexpr std::size_t kAppearanceSlot = 92;
using AppearanceFunction = std::uint32_t* (__fastcall*)(void*, std::uint32_t*);
AppearanceFunction g_original_appearance = nullptr;
volatile bool g_appearance_swap_active = false;
std::uint32_t g_nadine_appearance_hash = 0;
// VARIANT B step 1: make the LOCAL player render as Nadine (no clone). Single local
// player => full mechanics, weapons/aim work (no 2-player gating). Defaults true;
// in two-instance play start_state_net() sets it per ROLE (player-2 machine = Nadine
// local; player-1 machine = Chloe local).
volatile bool g_local_make_nadine = true;
// Two-instance role: false = role A (recv :31338, "player 1", local Chloe), true =
// role B (recv :31339, "player 2", local Nadine). Set in start_state_net().
volatile bool g_net_role_b = false;
// One-PC two-instance validation only: both processes otherwise poll the same
// physical XInput pad and rotate both local cameras together. The background
// process receives a neutral pad state. Real two-PC play leaves input untouched.
volatile bool g_loopback_two_instance = false;
// EXPERIMENTAL (DISPROVEN): tried rendering the role-B remote avatar (NadineBuddy)
// as Chloe via the appearance-hash swap. Result: NO visual change — the NPC's mesh
// is hard-bound (Nadine is streamed for the NadineBuddy), and the hash swap that
// works for the Player does NOT recolor the NPC's model. So the avatar stays Nadine.
// Left OFF; a distinct-Chloe avatar would need a Chloe-based actor, not a hash swap.
volatile bool g_avatar_show_chloe = false;
// Legacy single-process auto-spawn remains off. The network path creates slot 1 only
// after a fresh peer packet arrives, so a lone campaign instance is unchanged.
volatile bool g_enable_legacy_clone_autospawn = false;
volatile bool g_enable_remote_player_autospawn = true;
// VARIANT B avatar mirror-test: copy the local player's joint cache (pose) into NPC
// NadineBuddy each frame so the NPC mirrors the player's pose (validates joint-driving).
volatile bool g_mirror_pose = false;
bool g_mirror_nadine_ai_disabled = false;
ResolveLocalPlayerFunction g_original_resolve_local_player = nullptr;
std::uintptr_t* g_nadine_vtable = nullptr;
std::uintptr_t g_module = 0;
bool g_control_enabled = false;
// When set, NadineBuddy is driven toward the replicated peer position (remote
// player avatar) via animated NPC move-intent instead of local WASD.
bool g_net_drive_nadine = false;
bool g_local_player1_bind_enabled = false;
bool g_force_split_render_enabled = false;  // OFF: split-render mode broke main-player aiming
bool g_split_render_applied = false;
bool g_split_login_done = false;
void* g_last_nadine_object = nullptr;

// --- Spawn-arg capture via hardware breakpoint (no code modification) ---
volatile bool g_spawn_captured = false;
std::uint8_t g_spawn_desc[0x200]{};
void* g_spawn_ctx = nullptr;
void* g_spawn_a4 = nullptr;
std::uint64_t g_spawn_idx = 0;
std::uint64_t g_spawn_flag = 0;
void* g_spawn_out = nullptr;
PVOID g_spawn_veh = nullptr;

struct LookProbeHashes {
    std::uint32_t chloe_base = 0;
    std::uint32_t coop_chloe = 0;
    std::uint32_t nadine_base = 0;
    std::uint32_t chloe = 0;
    std::uint32_t nadine = 0;
    std::uint32_t player = 0;
};

// --- Input-state capture (find the control consumer) via exec breakpoint DR2 ---
// Breakpoint the stick normalizer 0x141A99FD0(pad, dst); dst (rdx) is the shared
// input-state the control system reads. Capture it so we can watch who reads it.
constexpr std::uintptr_t kStickNormalizerRva = 0x1A99FD0;
constexpr std::uintptr_t kPerSlotApplyRva = 0x289150;   // sub_140289150
constexpr std::uintptr_t kRealInputFnRva = 0x2893D0;  // sub_1402893D0 (per-slot real-input apply)
volatile bool g_inputstate_capturing = false;
volatile std::uintptr_t g_inputstate_addr = 0;
PVOID g_inputstate_veh = nullptr;
volatile bool g_perslot_capturing = false;
int g_perslot_hits[6]{};
PVOID g_perslot_veh = nullptr;
volatile std::uintptr_t g_perslot_fs = 0;
volatile int g_perslot_a2 = 0;
volatile std::uintptr_t g_perslot_a4 = 0;

// --- XInput IAT hook (virtual controller for player 2) ---
// The game polls XInputGetState(userIndex, ...) per local player. Index 1 (the
// 2nd controller) is absent in campaign, so local-player-1 gets a zeroed stick
// and cannot move. We IAT-hook XInputGetState to (a) log which indices are
// polled, and (b) optionally synthesize a connected pad on index 1 with an
// injected stick, so the engine feeds player-2 native movement input.
struct XInputGamepadLite {
    std::uint16_t wButtons;
    std::uint8_t  bLeftTrigger;
    std::uint8_t  bRightTrigger;
    std::int16_t  sThumbLX;
    std::int16_t  sThumbLY;
    std::int16_t  sThumbRX;
    std::int16_t  sThumbRY;
};
struct XInputStateLite {
    std::uint32_t dwPacketNumber;
    XInputGamepadLite Gamepad;
};
struct XInputVibrationLite {
    std::uint16_t wLeftMotorSpeed;
    std::uint16_t wRightMotorSpeed;
};
struct XInputCapabilitiesLite {
    std::uint8_t Type;
    std::uint8_t SubType;
    std::uint16_t Flags;
    XInputGamepadLite Gamepad;
    XInputVibrationLite Vibration;
};
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XInputStateLite*);
using XInputGetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XInputCapabilitiesLite*);
using NormalizeStickFn = void(__fastcall*)(void*, void*);
using PerSlotApplyFn = std::int64_t(__fastcall*)(void*, int, void*, void*, std::uint32_t);
XInputGetStateFn g_real_xinput_get_state = nullptr;
XInputGetCapabilitiesFn g_real_xinput_get_caps = nullptr;
using PlayerFireControllerFireFunction = void(__fastcall*)(
    void*, void*, void*, double);
PlayerFireControllerFireFunction g_original_player_fire = nullptr;
void __fastcall hooked_player_fire(
    void* controller, void* fire_args, void* weapon, double scalar);
using InPlaceCtorFunction = void*(__fastcall*)(void*);
using FillAnimCmdBitStreamFunction = std::uint32_t(__fastcall*)(
    void*, void*, void*, void*, std::uint32_t, std::uint32_t);
using CharacterMoveToSnapshotFunction = void(__fastcall*)(void*, void*);
using NetAnimAddSnapshotFunction = void(__fastcall*)(void*, void*);
using InitializePlayerNetAnimArtGroupsFunction = void(__fastcall*)(void*);
constexpr std::uintptr_t kXInputGetStateIatRva = 0x25D6D60;
constexpr std::uintptr_t kXInputGetCapsIatRva = 0x25D6D68;
volatile bool g_xinput_log = false;
volatile bool g_xinput_inject_p0 = false;   // temporary synth pad on index 0 to force controller path
volatile bool g_xinput_inject_p2 = false;   // synthesize pad on index 1
volatile bool g_request_cam0_nadine = false; // aux thread -> retarget main cam (game thread)
volatile int  g_inject_lx = 0;               // injected left stick X (-32768..32767)
volatile int  g_inject_ly = 0;               // injected left stick Y
ULONGLONG g_xinput_last_log[4] = {0, 0, 0, 0};
std::uint32_t g_xinput_inject_packet = 0;
ULONGLONG g_xinput_p0_inject_until = 0;
bool g_xinput_p0_armed_in_scene = false;
bool g_slot1_auto_reissue_done = false;
ULONGLONG g_slot1_autodrive_until = 0;
bool g_slot1_branch_struct_logged = false;
bool g_local_animation_mirror_test = false;

// --- Velocity-writer capture via hardware write breakpoint (DR1) ---
// Arms a 4-byte WRITE breakpoint on slot-0 Player +0x260 (velocity). The
// trapping RIP is the instruction that writes velocity; that function reads the
// player's move-input, so it leads us to the input application path.
volatile bool g_velwriter_capturing = false;
std::uintptr_t g_velwriter_addr = 0;
std::uintptr_t g_velwriter_rips[8]{};
volatile int g_velwriter_count = 0;
PVOID g_velwriter_veh = nullptr;

// --- Read-breakpoint on the input-state stick (find gameplay control reader) ---
constexpr std::uintptr_t kInputStickRva = 0x66E4230;  // input-state global +0x10 (LX)
volatile bool g_stickread_capturing = false;
std::uintptr_t g_stickread_rips[16]{};
volatile int g_stickread_count = 0;
PVOID g_stickread_veh = nullptr;

// --- Write-breakpoint on block[0]+0x0/+0x4 (find Chloe's real heading writer) ---
// One 8-byte watch on the start of input block 0 catches both floats at +0x0
// and +0x4, which is the earliest shared heading pair written before later
// copies into +0x10/+0x14.
constexpr std::uintptr_t kInputBlock0HeadingRva = 0x66E4220;
volatile bool g_inputwrite_capturing = false;
std::uintptr_t g_inputwrite_addr = 0;
std::uintptr_t g_inputwrite_rips[16]{};
volatile int g_inputwrite_count = 0;
PVOID g_inputwrite_veh = nullptr;

// --- Write-breakpoint on block[0]+0x10/+0x14 (keyboard/gameplay stick pair) ---
// If the first pair at +0x0/+0x4 stays silent, the live keyboard path may write
// directly into the later normalized stick pair at +0x10/+0x14.
volatile bool g_inputwrite_stick_capturing = false;
std::uintptr_t g_inputwrite_stick_addr = 0;
std::uintptr_t g_inputwrite_stick_rips[16]{};
volatile int g_inputwrite_stick_count = 0;
PVOID g_inputwrite_stick_veh = nullptr;
PVOID g_visual_seed_veh = nullptr;
volatile bool g_slot1_visual_override_armed = false;
volatile bool g_slot1_visual_override_active = false;
std::uint32_t g_nadine_visual_look_id = 0;
std::uint32_t g_nadine_visual_state_id = 0;
std::uint32_t g_nadine_visual_resolved_hash = 0;
bool g_nadine_visual_identity_logged = false;
bool g_nadine_visual_identity_valid = false;

void arm_inputstate_breakpoint_current_thread();
void arm_slot1_visual_seed_breakpoint_current_thread();

// LOOK-INJECTION (body swap test): during the clone reissue spawn, intercept
// InitializeSelectorBackedProcessSpawnPacket (0x1417C32A0) at entry and override
// arg4 (r9 -> packet+0x20 "base"/look, read by Player::Init) with a target look
// hash, so the spawned Player streams that character's BODY.
constexpr std::uintptr_t kPacketInitRva = 0x17C32A0;
PVOID g_packet_look_veh = nullptr;
volatile bool g_inject_look = false;
std::uint32_t g_inject_look_hash = 0;   // target (e.g. coop-drake)
std::uint32_t g_player_sel_hash = 0;    // hash("Player") - gate the right packet
void arm_packet_look_breakpoint_current_thread();
void disarm_packet_look_breakpoint_current_thread();

// BODY-SWAP via the render LOOK system (SAMP-style SetPlayerSkin). The look builder
// feeds the character's look2 hash (Chloe=0xcde4b742, Nadine=0x0f6d80f3) into the
// look-apply fn sub_141804170 (ndlib/render/look.cpp GetLook2) in RDX. During the
// clone's spawn we break there (DR3) and swap Chloe's look2 -> Nadine's so the
// clone's render look (art-groups/mesh) resolves as Nadine.
// (Earlier record-resolve redirect at 0x17CD080 only broke body streaming = DEAD.)
constexpr std::uintptr_t kLookApplyRva = 0x1804170;
// COLLISION diag: Player::InitializeCompositeBody (builds the physics/collision
// body). DR3-probe its entry during the clone reissue spawn to confirm whether the
// clone gets a composite body at all (vs a ghost spawn missing physics promotion).
constexpr std::uintptr_t kInitCompositeBodyRva = 0x10CF470;
volatile bool g_diag_compbody = false;
int g_compbody_hits = 0;
PVOID g_record_swap_veh = nullptr;
volatile bool g_bodyswap_arm = false;       // gate: only during clone spawn
// NADINE LOOK SWAP via appearance-hash (vtable[92]/object+0x15A4): ON by default so
// the clone auto-spawn resolves Nadine's appearance. (look2-rdx swap was the wrong
// arg; appearance-hash is the look builder's primary key into the mesh record.)
volatile bool g_want_nadine_body = true;
std::uint32_t g_bodyswap_match_id = 0;       // Chloe look2 hash (rdx to match)
std::uint32_t g_bodyswap_to_id = 0;          // Nadine look2 hash (rdx replacement)
int g_bodyswap_hits = 0;
void arm_record_swap_breakpoint_current_thread();
void disarm_record_swap_breakpoint_current_thread();
void arm_compbody_diag_breakpoint_current_thread();

enum class CommandSource : std::uint32_t {
    LocalKeyboard = 0,
    RemoteClient = 1,
};

enum class MotionBackend : std::uint32_t {
    Type2MoveTo = 0,
    Type4Command = 1,
    Type7Steering = 2,
};

struct InputCommand {
    float forward = 0.0F;
    float right = 0.0F;
    std::uint8_t input_mask = 0;
};

struct RemoteCommandState {
    coop::InputPacket last_packet{};
    InputCommand command{};
    ULONGLONG received_at_ms = 0;
    bool has_packet = false;
};

struct UdpReceiverState {
    SOCKET socket = INVALID_SOCKET;
    HANDLE thread = nullptr;
    std::atomic<bool> running = false;
    bool winsock_ready = false;
};

struct ObjectControlState {
    bool was_moving = false;
    bool ai_disabled = false;
    ULONGLONG last_command_time = 0;
    std::uint8_t last_input_mask = 0;
};

struct RespawnSeedRecord {
    std::uint32_t scalar0 = 0x7FCCCCCC;
    std::uint32_t scalar1 = 0x7FCCCCCC;
    std::uint32_t scalar2 = 0x7FCCCCCC;
    std::int32_t respawn_mode = 0;
    float zero_vec4[4]{};
    float default_vec4[4]{};
};

struct RespawnTransformBlock {
    std::byte bytes[0x50]{};
};

std::unordered_map<void*, ObjectControlState> g_object_states;
void* g_controlled_object = nullptr;
CommandSource g_command_source = CommandSource::LocalKeyboard;
MotionBackend g_motion_backend = MotionBackend::Type2MoveTo;
RemoteCommandState g_remote_command_state{};
UdpReceiverState g_udp_receiver{};
bool g_slot1_scaffold_attempted = false;

// --- Variant A player-state replication (Phase 1 MVP) ---
struct StateNetState {
    SOCKET recv_socket = INVALID_SOCKET;   // bound to kStatePort, receives peer state
    SOCKET send_socket = INVALID_SOCKET;   // sends local state to peer
    sockaddr_in peer_addr{};
    bool peer_set = false;
    HANDLE thread = nullptr;
    std::atomic<bool> running = false;
    bool winsock_ready = false;
    std::atomic<bool> has_peer_state = false;
    std::atomic<ULONGLONG> peer_state_received_ms = 0;
    SRWLOCK peer_state_lock = SRWLOCK_INIT;
    coop::StatePacket peer_state{};        // latest received peer state
    std::atomic<bool> has_peer_enemies = false;
    std::atomic<ULONGLONG> peer_enemies_received_ms = 0;
    SRWLOCK peer_enemies_lock = SRWLOCK_INIT;
    coop::EnemyPacket peer_enemies{};      // latest received host enemy snapshot
    std::uint32_t send_seq = 0;
    std::uint32_t enemy_send_seq = 0;
    bool enabled = false;                  // toggled on once peer + puppet ready
};
StateNetState g_state_net{};

void publish_peer_state_snapshot(const coop::StatePacket& packet) {
    AcquireSRWLockExclusive(&g_state_net.peer_state_lock);
    g_state_net.peer_state = packet;
    ReleaseSRWLockExclusive(&g_state_net.peer_state_lock);
    g_state_net.peer_state_received_ms.store(GetTickCount64(), std::memory_order_release);
    g_state_net.has_peer_state.store(true, std::memory_order_release);
}

bool copy_peer_state_snapshot(coop::StatePacket& out) {
    if (!g_state_net.has_peer_state.load(std::memory_order_acquire)) {
        return false;
    }
    const ULONGLONG received =
        g_state_net.peer_state_received_ms.load(std::memory_order_acquire);
    if (received == 0 || GetTickCount64() - received > 1500) {
        return false;
    }
    AcquireSRWLockShared(&g_state_net.peer_state_lock);
    out = g_state_net.peer_state;
    ReleaseSRWLockShared(&g_state_net.peer_state_lock);
    return true;
}

// Input and transform require a fresh packet. The last confirmed health remains
// authoritative while an alt-tabbed instance is suspended and stops sending.
bool copy_last_peer_state_snapshot(coop::StatePacket& out) {
    if (!g_state_net.has_peer_state.load(std::memory_order_acquire)) {
        return false;
    }
    AcquireSRWLockShared(&g_state_net.peer_state_lock);
    out = g_state_net.peer_state;
    ReleaseSRWLockShared(&g_state_net.peer_state_lock);
    return true;
}

void publish_peer_enemies_snapshot(const coop::EnemyPacket& packet) {
    AcquireSRWLockExclusive(&g_state_net.peer_enemies_lock);
    g_state_net.peer_enemies = packet;
    ReleaseSRWLockExclusive(&g_state_net.peer_enemies_lock);
    g_state_net.peer_enemies_received_ms.store(GetTickCount64(), std::memory_order_release);
    g_state_net.has_peer_enemies.store(true, std::memory_order_release);
}

bool copy_peer_enemies_snapshot(coop::EnemyPacket& out) {
    if (!g_state_net.has_peer_enemies.load(std::memory_order_acquire)) {
        return false;
    }
    const ULONGLONG received =
        g_state_net.peer_enemies_received_ms.load(std::memory_order_acquire);
    if (received == 0 || GetTickCount64() - received > 1500) {
        return false;
    }
    AcquireSRWLockShared(&g_state_net.peer_enemies_lock);
    out = g_state_net.peer_enemies;
    ReleaseSRWLockShared(&g_state_net.peer_enemies_lock);
    return true;
}

bool process_owns_foreground_window() {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(foreground, &process_id);
    return process_id == GetCurrentProcessId();
}
// SPSC ring for client->host damage events (recv thread = producer, game thread =
// consumer on the host). Lock-free single-producer/single-consumer.
struct ClientDmgEvent { std::uint32_t name_hash; float damage; };
ClientDmgEvent g_client_dmg_ring[64]{};
std::atomic<std::uint32_t> g_client_dmg_head{0};
std::uint32_t g_client_dmg_tail = 0;
// Deferred until player-to-player synchronization is complete. Keep all recovered
// enemy paths available, but do not let them mutate either world during player work.
volatile bool g_enemy_sync = false;
// Client->host damage relay master toggle.
volatile bool g_client_damage_relay = false;
// Pausing client enemy AI makes transforms deterministic, but also removes every
// local reaction/attack because those states are not yet replicated. Keep local AI
// active until the host action-state stream is recovered.
volatile bool g_pause_client_enemy_ai = false;
// RequestDisableAi remains disabled for NadineBuddy. Even after validating its
// pointer chain, live runs have ended through the engine's fatal path after pausing
// the buddy. Client enemy replicas use their own guarded one-shot call below.
volatile bool g_allow_runtime_ai_toggle = false;
// Client-side mirrored death through the campaign npc-die sequence. A lethal base
// AttackInfo is forbidden; it lacks metadata required by the kill branch.
volatile bool g_enable_client_death_publish = false;
// Prevent the client-side mirrored death from being relayed back to the host.
volatile bool g_applying_peer_enemy_death = false;
// Single-instance validation of the enemy client-apply code path (enumerate +
// RequestDisableAi + transform write) without needing two instances: freeze all
// enemies in place with AI off. Toggle NUMPAD5. Default off.
volatile bool g_enemy_freeze_selftest = false;
// Phase 3 recon: periodic CharacterManager dump (off).
volatile bool g_charmgr_dump_periodic = false;
// Position offset added to the peer state before applying to the puppet. 0 for
// real two-instance/two-PC play (puppet sits at the peer's actual position).
float g_state_loopback_offset_x = 0.0F;

std::size_t readable_bytes(const void* p);
bool world_stable();
bool valid_network_position(const float* position);

void log_line(const char* line) {
    std::ofstream output(
        R"(C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\locomotion_hook.log)",
        std::ios::app
    );
    output << "[pid=" << GetCurrentProcessId() << "] " << line << '\n';
}

LONG CALLBACK crash_observer(EXCEPTION_POINTERS* exception) {
    if (exception == nullptr || exception->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_FLT_DIVIDE_BY_ZERO) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static std::atomic<unsigned> event_count{0};
    if (event_count.fetch_add(1, std::memory_order_relaxed) >= 8) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    char line[1536]{};
    const auto address = reinterpret_cast<std::uintptr_t>(
        exception->ExceptionRecord->ExceptionAddress);
    const auto rip = exception->ContextRecord != nullptr
        ? static_cast<std::uintptr_t>(exception->ContextRecord->Rip)
        : 0;
    const auto rsp = exception->ContextRecord != nullptr
        ? static_cast<std::uintptr_t>(exception->ContextRecord->Rsp)
        : 0;
    const CONTEXT* const context = exception->ContextRecord;
    const auto fault_kind = exception->ExceptionRecord->NumberParameters >= 1
        ? exception->ExceptionRecord->ExceptionInformation[0]
        : 0;
    const auto fault_address = exception->ExceptionRecord->NumberParameters >= 2
        ? exception->ExceptionRecord->ExceptionInformation[1]
        : 0;
    std::uintptr_t stack[12]{};
    SIZE_T stack_read = 0;
    if (rsp != 0) {
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(rsp),
                          stack, sizeof(stack), &stack_read);
    }
    int length = std::snprintf(
        line, sizeof(line),
        "[pid=%lu] EXCEPTION code=0x%08lX address=0x%llX rva=0x%llX "
        "access=%llu fault=0x%llX rip=0x%llX rsp=0x%llX "
        "rcx=0x%llX rdx=0x%llX r8=0x%llX r9=0x%llX "
        "rbx=0x%llX rbp=0x%llX rsi=0x%llX rdi=0x%llX r14=0x%llX r15=0x%llX",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(code),
        static_cast<unsigned long long>(address),
        static_cast<unsigned long long>(address > g_module ? address - g_module : 0),
        static_cast<unsigned long long>(fault_kind),
        static_cast<unsigned long long>(fault_address),
        static_cast<unsigned long long>(rip),
        static_cast<unsigned long long>(rsp),
        static_cast<unsigned long long>(context ? context->Rcx : 0),
        static_cast<unsigned long long>(context ? context->Rdx : 0),
        static_cast<unsigned long long>(context ? context->R8 : 0),
        static_cast<unsigned long long>(context ? context->R9 : 0),
        static_cast<unsigned long long>(context ? context->Rbx : 0),
        static_cast<unsigned long long>(context ? context->Rbp : 0),
        static_cast<unsigned long long>(context ? context->Rsi : 0),
        static_cast<unsigned long long>(context ? context->Rdi : 0),
        static_cast<unsigned long long>(context ? context->R14 : 0),
        static_cast<unsigned long long>(context ? context->R15 : 0));
    if (length > 0 && static_cast<std::size_t>(length) < sizeof(line)) {
        length += std::snprintf(line + length, sizeof(line) - static_cast<std::size_t>(length),
                                " stack=");
        const std::size_t count = stack_read / sizeof(stack[0]);
        for (std::size_t i = 0; i < count && length > 0 &&
             static_cast<std::size_t>(length) < sizeof(line); ++i) {
            const auto value = stack[i];
            const auto value_rva = value >= g_module && value < g_module + 0x30000000
                ? value - g_module
                : 0;
            length += std::snprintf(
                line + length, sizeof(line) - static_cast<std::size_t>(length),
                " 0x%llX(rva=0x%llX)",
                static_cast<unsigned long long>(value),
                static_cast<unsigned long long>(value_rva));
        }
        if (length > 0 && static_cast<std::size_t>(length) + 2 < sizeof(line)) {
            line[length++] = '\r';
            line[length++] = '\n';
            line[length] = '\0';
        }
    }
    const HANDLE file = CreateFileA(
        R"(C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\locomotion_hook.log)",
        FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE && length > 0) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

std::int64_t __fastcall hooked_add_joypad_disabled_commands(
    void* manager, std::int64_t joypad_index, int owner_id, int from_code,
    const std::uint32_t* commands, int command_count) {
    AcquireSRWLockExclusive(&g_joypad_disabled_commands_lock);
    const std::int64_t result = g_original_add_joypad_disabled_commands(
        manager, joypad_index, owner_id, from_code, commands, command_count);
    ReleaseSRWLockExclusive(&g_joypad_disabled_commands_lock);
    return result;
}

bool __fastcall hooked_gameinfo_keyboard_mode(void* game_info) {
    if (g_force_joypad3_analog_thread.load(std::memory_order_acquire) ==
        GetCurrentThreadId()) {
        g_force_joypad3_analog_hits.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return g_original_gameinfo_keyboard_mode(game_info);
}

char __fastcall hooked_evaluate_keyboard_button_binding(
    void* joypad, void* descriptor, std::uint32_t command_hash) {
    // Keyboard masks are global to the process, so without this filter a local
    // jump/crouch/etc. is accepted for both Player Joypads. Joypad 3 already
    // receives its peer's normalized per-Joypad masks through source+0x518.
    if (g_state_net.enabled && joypad != nullptr) {
        void* const frame_state =
            *reinterpret_cast<void**>(g_module + 0x3FE1088);
        if (frame_state != nullptr) {
            void* const remote_joypad = static_cast<std::byte*>(frame_state) +
                                        0xDB0 + 0xBF0 * 3;
            if (joypad == remote_joypad) {
                return 0;
            }
        }
    }
    return g_original_evaluate_keyboard_button_binding(
        joypad, descriptor, command_hash);
}

bool install_joypad3_keyboard_isolation() {
    auto* const target = reinterpret_cast<std::uint8_t*>(
        g_module + 0x1838740);
    constexpr std::array<std::uint8_t, 15> expected{
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x55,
        0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57,
    };
    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        log_line("Joypad3 keyboard isolation skipped: unexpected prologue");
        return false;
    }

    constexpr std::size_t kPatchSize = expected.size();
    constexpr std::size_t kEntryJumpSize = 12;
    constexpr std::size_t kReturnJumpSize = 14;
    auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kPatchSize + kReturnJumpSize, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        log_line("Joypad3 keyboard isolation skipped: trampoline allocation failed");
        return false;
    }
    std::memcpy(trampoline, target, kPatchSize);
    auto* const jump_back = trampoline + kPatchSize;
    // The copied prologue sets RAX=entry RSP and the original body still uses it
    // at 0x14183875D. A mov-rax/jmp-rax return corrupts that live value. Use a
    // RIP-indirect absolute jump, which preserves every register.
    jump_back[0] = 0xFF;
    jump_back[1] = 0x25;
    *reinterpret_cast<std::uint32_t*>(jump_back + 2) = 0;
    *reinterpret_cast<std::uintptr_t*>(jump_back + 6) =
        reinterpret_cast<std::uintptr_t>(target + kPatchSize);
    FlushInstructionCache(
        GetCurrentProcess(), trampoline, kPatchSize + kReturnJumpSize);
    g_original_evaluate_keyboard_button_binding =
        reinterpret_cast<EvaluateKeyboardButtonBindingFunction>(trampoline);

    DWORD old_protection = 0;
    if (!VirtualProtect(target, kPatchSize, PAGE_EXECUTE_READWRITE,
                        &old_protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_original_evaluate_keyboard_button_binding = nullptr;
        log_line("Joypad3 keyboard isolation skipped: VirtualProtect failed");
        return false;
    }
    target[0] = 0x48;
    target[1] = 0xB8;
    *reinterpret_cast<std::uintptr_t*>(target + 2) =
        reinterpret_cast<std::uintptr_t>(&hooked_evaluate_keyboard_button_binding);
    target[10] = 0xFF;
    target[11] = 0xE0;
    std::memset(
        target + kEntryJumpSize, 0x90, kPatchSize - kEntryJumpSize);
    FlushInstructionCache(GetCurrentProcess(), target, kPatchSize);
    DWORD ignored = 0;
    VirtualProtect(target, kPatchSize, old_protection, &ignored);
    log_line("Joypad3 isolated from process-global keyboard command masks");
    return true;
}

std::int64_t __fastcall hooked_input_scale_copy(
    void* joypad, void* source, std::uint32_t destination_index) {
    // Window focus is a global input gate. Serialize every conversion so only
    // remote Joypad 3 can temporarily see it enabled in a background process.
    AcquireSRWLockExclusive(&g_joypad3_analog_lock);
    auto* const input_focus_gate = reinterpret_cast<std::uint8_t*>(
        g_module + 0x37EA62C);
    const std::uint8_t saved_input_focus = *input_focus_gate;
    if (destination_index == 3) {
        *input_focus_gate = 1;
    }
    const DWORD previous = destination_index == 3
        ? g_force_joypad3_analog_thread.exchange(
              GetCurrentThreadId(), std::memory_order_acq_rel)
        : 0;

    // Feed the remote Player through the same normalized button boundary used by
    // a native local Player. Input_ScaleCopyVirtual derives held/pressed/released
    // Joypad masks and hold timestamps from source+0x518, so command bindings
    // (jump, crouch, melee, cover, traversal, etc.) remain engine-owned.
    if (destination_index == 3 && readable_bytes(source) >= 0x520) {
        coop::StatePacket peer{};
        if (copy_peer_state_snapshot(peer) && (peer.flags & 8) != 0) {
            auto* const source_bytes = static_cast<std::byte*>(source);
            const std::uint64_t synthetic_move_gate =
                *reinterpret_cast<const std::uint64_t*>(source_bytes + 0x518) &
                0x500u;
            *reinterpret_cast<std::uint64_t*>(source_bytes + 0x518) =
                peer.input_buttons | synthetic_move_gate;
        }
    }
    const std::int64_t result =
        g_original_input_scale_copy(joypad, source, destination_index);
    if (destination_index == 3) {
        g_force_joypad3_analog_thread.store(previous, std::memory_order_release);
        *input_focus_gate = saved_input_focus;
    }
    ReleaseSRWLockExclusive(&g_joypad3_analog_lock);

    if (destination_index == 3) {
        static thread_local ULONGLONG last_log = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - last_log >= 500 && readable_bytes(joypad) >= 0xE8 &&
            readable_bytes(source) >= 0x520) {
            last_log = now;
            const auto* src = static_cast<const float*>(source);
            const auto* dst = static_cast<const float*>(joypad);
            std::ostringstream line;
            line << "JOYPAD3-ANALOG source=(" << src[0] << "," << src[1]
                 << ") processed=(" << dst[0x48 / 4] << ","
                 << dst[0x4C / 4] << ") right=(" << dst[0x50 / 4] << ","
                 << dst[0x54 / 4] << ") modeOverrides="
                 << g_force_joypad3_analog_hits.load(std::memory_order_relaxed)
                 << " windowFocused=" << static_cast<unsigned>(saved_input_focus)
                 << " buttons src=0x" << std::hex
                 << *reinterpret_cast<const std::uint64_t*>(
                        static_cast<const std::byte*>(source) + 0x518)
                 << " pressed=0x"
                 << *reinterpret_cast<const std::uint64_t*>(
                        static_cast<const std::byte*>(joypad) + 0xD0)
                 << " released=0x"
                 << *reinterpret_cast<const std::uint64_t*>(
                        static_cast<const std::byte*>(joypad) + 0xD8)
                 << " current=0x"
                 << *reinterpret_cast<const std::uint64_t*>(
                        static_cast<const std::byte*>(joypad) + 0xE0)
                 << std::dec;
            log_line(line.str().c_str());
        }
    }
    return result;
}

bool install_joypad3_analog_route() {
    auto* const game_info_vtable = reinterpret_cast<std::uintptr_t*>(
        g_module + kGameInfoVtableRva);
    auto* const mode_slot = &game_info_vtable[0x90 / sizeof(void*)];
    if (*mode_slot != g_module + 0x29AB50) {
        log_line("Joypad3 analog route skipped: unexpected GameInfo vfunc+0x90");
        return false;
    }
    g_original_gameinfo_keyboard_mode =
        reinterpret_cast<GameInfoKeyboardModeFunction>(*mode_slot);
    DWORD old_mode_protection = 0;
    if (!VirtualProtect(mode_slot, sizeof(*mode_slot), PAGE_READWRITE,
                        &old_mode_protection)) {
        log_line("Joypad3 analog route skipped: GameInfo vtable protect failed");
        return false;
    }
    *mode_slot = reinterpret_cast<std::uintptr_t>(&hooked_gameinfo_keyboard_mode);
    FlushInstructionCache(GetCurrentProcess(), mode_slot, sizeof(*mode_slot));
    DWORD ignored_mode = 0;
    VirtualProtect(mode_slot, sizeof(*mode_slot), old_mode_protection, &ignored_mode);

    auto* const target = reinterpret_cast<std::uint8_t*>(
        g_module + kInputScaleCopyRva);
    constexpr std::array<std::uint8_t, 15> expected{
        0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48,
        0x8D, 0xAC, 0x24, 0xB8, 0xCB, 0xFF, 0xFF,
    };
    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        log_line("Joypad3 analog route skipped: unexpected Input_ScaleCopy prologue");
        return false;
    }

    constexpr std::size_t kPatchSize = expected.size();
    constexpr std::size_t kJumpSize = 12;
    auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kPatchSize + kJumpSize, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        log_line("Joypad3 analog route skipped: trampoline allocation failed");
        return false;
    }
    std::memcpy(trampoline, target, kPatchSize);
    auto* const jump_back = trampoline + kPatchSize;
    jump_back[0] = 0x48;
    jump_back[1] = 0xB8;
    *reinterpret_cast<std::uintptr_t*>(jump_back + 2) =
        reinterpret_cast<std::uintptr_t>(target + kPatchSize);
    jump_back[10] = 0xFF;
    jump_back[11] = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), trampoline, kPatchSize + kJumpSize);
    g_original_input_scale_copy =
        reinterpret_cast<InputScaleCopyFunction>(trampoline);

    DWORD old_target_protection = 0;
    if (!VirtualProtect(target, kPatchSize, PAGE_EXECUTE_READWRITE,
                        &old_target_protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_original_input_scale_copy = nullptr;
        log_line("Joypad3 analog route skipped: function protect failed");
        return false;
    }
    target[0] = 0x48;
    target[1] = 0xB8;
    *reinterpret_cast<std::uintptr_t*>(target + 2) =
        reinterpret_cast<std::uintptr_t>(&hooked_input_scale_copy);
    target[10] = 0xFF;
    target[11] = 0xE0;
    std::memset(target + kJumpSize, 0x90, kPatchSize - kJumpSize);
    FlushInstructionCache(GetCurrentProcess(), target, kPatchSize);
    DWORD ignored_target = 0;
    VirtualProtect(target, kPatchSize, old_target_protection, &ignored_target);
    log_line("Joypad3 uses native analog source while keyboard mode remains global");
    return true;
}

bool install_joypad_disabled_commands_lock() {
    auto* const target = reinterpret_cast<std::uint8_t*>(
        g_module + kAddJoypadDisabledCommandsRva);
    constexpr std::array<std::uint8_t, 14> expected{
        0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48,
        0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,
    };
    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        log_line("joypad command lock hook skipped: unexpected function prologue");
        return false;
    }

    constexpr std::size_t kPatchSize = expected.size();
    constexpr std::size_t kJumpSize = 12;
    auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kPatchSize + kJumpSize, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        log_line("joypad command lock hook skipped: trampoline allocation failed");
        return false;
    }
    std::memcpy(trampoline, target, kPatchSize);
    std::uint8_t* jump_back = trampoline + kPatchSize;
    jump_back[0] = 0x48;
    jump_back[1] = 0xB8;  // mov rax, imm64
    *reinterpret_cast<std::uintptr_t*>(jump_back + 2) =
        reinterpret_cast<std::uintptr_t>(target + kPatchSize);
    jump_back[10] = 0xFF;
    jump_back[11] = 0xE0;  // jmp rax
    FlushInstructionCache(GetCurrentProcess(), trampoline, kPatchSize + kJumpSize);

    g_original_add_joypad_disabled_commands =
        reinterpret_cast<AddJoypadDisabledCommandsFunction>(trampoline);

    DWORD old_protection = 0;
    if (!VirtualProtect(target, kPatchSize, PAGE_EXECUTE_READWRITE,
                        &old_protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_original_add_joypad_disabled_commands = nullptr;
        log_line("joypad command lock hook skipped: VirtualProtect failed");
        return false;
    }
    target[0] = 0x48;
    target[1] = 0xB8;  // mov rax, imm64
    *reinterpret_cast<std::uintptr_t*>(target + 2) =
        reinterpret_cast<std::uintptr_t>(&hooked_add_joypad_disabled_commands);
    target[10] = 0xFF;
    target[11] = 0xE0;  // jmp rax
    target[12] = 0x90;
    target[13] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, kPatchSize);
    DWORD ignored = 0;
    VirtualProtect(target, kPatchSize, old_protection, &ignored);

    log_line("joypad disabled-command mutation serialized for multiple Players");
    return true;
}

bool install_player_fire_capture() {
    auto* const target = reinterpret_cast<std::uint8_t*>(
        g_module + kPlayerFireControllerFireRva);
    constexpr std::array<std::uint8_t, 20> expected{
        0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
        0x57, 0x48, 0x8D, 0xAC, 0x24, 0xC0, 0xBD, 0xFF, 0xFF,
    };
    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        log_line("player-fire capture skipped: unexpected function prologue");
        return false;
    }

    constexpr std::size_t kPatchSize = expected.size();
    constexpr std::size_t kJumpSize = 12;
    auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kPatchSize + kJumpSize, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        log_line("player-fire capture skipped: trampoline allocation failed");
        return false;
    }
    std::memcpy(trampoline, target, kPatchSize);
    auto* const jump_back = trampoline + kPatchSize;
    jump_back[0] = 0x48;
    jump_back[1] = 0xB8;
    *reinterpret_cast<std::uintptr_t*>(jump_back + 2) =
        reinterpret_cast<std::uintptr_t>(target + kPatchSize);
    jump_back[10] = 0xFF;
    jump_back[11] = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), trampoline, kPatchSize + kJumpSize);
    g_original_player_fire =
        reinterpret_cast<PlayerFireControllerFireFunction>(trampoline);

    DWORD old_protection = 0;
    if (!VirtualProtect(target, kPatchSize, PAGE_EXECUTE_READWRITE,
                        &old_protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_original_player_fire = nullptr;
        log_line("player-fire capture skipped: VirtualProtect failed");
        return false;
    }
    target[0] = 0x48;
    target[1] = 0xB8;
    *reinterpret_cast<std::uintptr_t*>(target + 2) =
        reinterpret_cast<std::uintptr_t>(&hooked_player_fire);
    target[10] = 0xFF;
    target[11] = 0xE0;
    std::memset(target + kJumpSize, 0x90, kPatchSize - kJumpSize);
    FlushInstructionCache(GetCurrentProcess(), target, kPatchSize);
    DWORD ignored = 0;
    VirtualProtect(target, kPatchSize, old_protection, &ignored);
    log_line("PlayerFireController::Fire capture installed");
    return true;
}

std::uint32_t resolve_upper_actor_record_hash(std::uint32_t id) {
    if (g_module == 0 || id == 0) {
        return 0;
    }
    const auto resolve =
        reinterpret_cast<ResolveUpperActorRecordFunction>(
            g_module + kResolveUpperActorRecordRva
        );
    void* const wrapper = resolve(id);
    if (wrapper == nullptr || readable_bytes(wrapper) < sizeof(void*)) {
        return 0;
    }
    void* const record = *reinterpret_cast<void**>(wrapper);
    if (record == nullptr || readable_bytes(record) < 0x1C) {
        return 0;
    }
    return *reinterpret_cast<std::uint32_t*>(
        static_cast<std::byte*>(record) + 0x18
    );
}

void observe_nadine_visual_identity(void* object) {
    if (object == nullptr || readable_bytes(object) < 0x194) {
        return;
    }

    const std::uint32_t look_id =
        *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(object) + 0x170);
    const std::uint32_t state_id =
        *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(object) + 0x190);
    if (state_id != 0 && !g_nadine_visual_identity_valid) {
        const std::uint32_t resolved_hash = resolve_upper_actor_record_hash(state_id);
        if (resolved_hash != 0) {
            g_nadine_visual_look_id = look_id;
            g_nadine_visual_state_id = state_id;
            g_nadine_visual_resolved_hash = resolved_hash;
            g_nadine_visual_identity_valid = true;
        }
    }

    if (!g_nadine_visual_identity_logged) {
        g_nadine_visual_identity_logged = true;
        std::ostringstream line;
        line << "Nadine visual identity candidate: object=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(object)
             << " +170=0x" << look_id
             << " +190=0x" << state_id
             << " resolved(+190)->0x" << resolve_upper_actor_record_hash(state_id)
             << " valid=" << std::dec << (g_nadine_visual_identity_valid ? 1 : 0);
        log_line(line.str().c_str());
    }
}

// BODY-SWAP RECON: diff the sub-object pointer graph of the real Chloe (slot 0),
// the spawned clone-Player (slot 1, also Chloe art) and the NPC NadineBuddy.
// For each 8-byte offset that holds a pointer into readable memory on all three,
// read the pointed-to object's first qword (its vtable). The art-group / look /
// draw-control sub-object we want is the offset where BOTH Chloes share the same
// sub-object vtable but Nadine differs (Chloe-type vs Nadine-type) -> that is the
// field to graft Nadine's pointer onto the clone. Logs candidates with their
// vtable RVAs so we can identify the draw-control/look class statically.
void* retained_player_object(int index);  // defined below

// Hooked Player vtable[92] (appearance-hash getter). VARIANT B step 1: when
// g_local_make_nadine is on (and we are NOT spawning a 2nd Player clone), force the
// appearance hash to Nadine for the local Player so the LOCAL player renders as
// Nadine with full mechanics (single local player => no 2-player gating, weapons/aim
// work). Also still supports the legacy clone-spawn-window swap.
// Diagnostic: collect distinct caller RVAs of the appearance getter so we can tell
// the LOOK/RENDER callers (mesh) apart from gameplay/ability gating callers (lasso).
// Set true to gather; logged once per distinct caller. Default ON for one run.
volatile bool g_log_appearance_callers = false;
// RVA of the LOOK BUILDER's call to vtable[92] (PlayerUpperDrawControlLifecycle...
// @0x1bb4280; the `call [r8+0x2e0]` at 0x1bb4ab2 returns here). This is the ONLY
// caller that drives the rendered MESH. All other callers of the appearance getter
// are gameplay/identity checks (e.g. lasso/rope ability gating) and MUST see the
// real character (Chloe) so abilities stay intact. => caller-gated swap.
constexpr std::uintptr_t kLookBuilderAppearCallRva = 0x1bb4ab9;
std::uint32_t* __fastcall hooked_appearance(void* a1, std::uint32_t* a2) {
    std::uintptr_t ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    std::uintptr_t rva = (g_module != 0 && ret > g_module) ? (ret - g_module) : ret;
    if (g_log_appearance_callers && g_module != 0) {
        static std::uintptr_t seen[64]{};
        static int seen_n = 0;
        bool known = false;
        for (int i = 0; i < seen_n; ++i) if (seen[i] == rva) { known = true; break; }
        if (!known && seen_n < 64) {
            seen[seen_n++] = rva;
            std::ostringstream l;
            l << "APPEAR-CALLER rva=0x" << std::hex << rva
              << " (caller #" << std::dec << seen_n << ")";
            log_line(l.str().c_str());
        }
    }
    // Swap to Nadine ONLY for the look builder (mesh); everyone else -> real Chloe so
    // lasso/rope and other ability gating keyed on the character hash keep working.
    bool from_lookbuilder = (rva == kLookBuilderAppearCallRva);
    // Role B (player-2 machine): the REMOTE avatar represents player 1 = Chloe, so
    // render the NadineBuddy avatar as Chloe. (Hook-domain hashes: chloe 0x3f334972,
    // nadine 0xd27c074f.) The NPC renders through a DIFFERENT look path than the
    // player look builder (0x1bb4ab9), so this swap is gated by OBJECT only (the
    // buddy), for ANY caller — the buddy is AI-disabled + net-driven, so we don't
    // care that its gameplay/identity reads also see Chloe. EXPERIMENTAL.
    if (a2 != nullptr && g_avatar_show_chloe && g_net_role_b &&
        g_last_nadine_object != nullptr && a1 == g_last_nadine_object) {
        *a2 = 0x3f334972u;  // hash("chloe")
        return a2;
    }
    if (a2 != nullptr && from_lookbuilder) {
        // Local-player Nadine swap (role B / player-2 machine): ONLY the local player
        // object, so the shared look builder doesn't recolor the buddy or other NPCs.
        if (g_local_make_nadine && a1 == retained_player_object(0)) {
            *a2 = 0xd27c074fu;  // hash("nadine")
            return a2;
        }
        // Legacy clone-spawn-window swap.
        if (g_appearance_swap_active && g_nadine_appearance_hash != 0) {
            *a2 = 0xd27c074fu;  // hash("nadine")
            return a2;
        }
    }
    if (g_original_appearance != nullptr) {
        return g_original_appearance(a1, a2);
    }
    return a2;
}

// SEH-isolated call of vtable[94] (off 752) = the character's "get look id" method.
// Kept in its own function (no C++ unwinding objects) so __try/__except compiles.
std::uint32_t safe_get_look_id(void* obj) {
    if (obj == nullptr || readable_bytes(obj) < sizeof(void*)) return 0;
    std::uintptr_t vt = *reinterpret_cast<std::uintptr_t*>(obj);
    if (vt <= g_module || readable_bytes(reinterpret_cast<void*>(vt + 752)) < 8) return 0;
    using GetLookFn = std::uint32_t*(__fastcall*)(void*, void*);
    auto fn = *reinterpret_cast<GetLookFn*>(vt + 752);
    std::uint8_t scratch[64]{};
    __try {
        std::uint32_t* r = fn(obj, scratch);
        if (r != nullptr && readable_bytes(r) >= 4) return *r;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 0;
}

// SEH-isolated call of an object's vtable[slot] that follows the (this, &out_dword)
// "getter" convention (e.g. vtable[92]/off 736 = the APPEARANCE hash used by the
// look builder). Returns the written dword, or 0 on fault.
std::uint32_t safe_get_dword_vslot(void* obj, std::size_t vt_off) {
    if (obj == nullptr || readable_bytes(obj) < sizeof(void*)) return 0;
    std::uintptr_t vt = *reinterpret_cast<std::uintptr_t*>(obj);
    if (vt <= g_module || readable_bytes(reinterpret_cast<void*>(vt + vt_off)) < 8) return 0;
    using GetFn = std::uint32_t*(__fastcall*)(void*, void*);
    auto fn = *reinterpret_cast<GetFn*>(vt + vt_off);
    std::uint8_t scratch[64]{};
    __try {
        std::uint32_t* r = fn(obj, scratch);
        if (r != nullptr && readable_bytes(r) >= 4) return *r;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 0;
}

void dump_body_subobject_diff() {
    void* chloe0 = retained_player_object(0);
    void* clone1 = retained_player_object(1);
    void* nadine = g_last_nadine_object;
    std::ostringstream hdr;
    hdr << "BODY-DIFF chloe0=0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(chloe0) << " clone1=0x"
        << reinterpret_cast<std::uintptr_t>(clone1) << " nadine=0x"
        << reinterpret_cast<std::uintptr_t>(nadine)
        << " module=0x" << g_module;
    log_line(hdr.str().c_str());
    // clone1 may be null (Variant B has no clone); the clone-comparison probes
    // null-check it internally. Only the local player + Nadine NPC are required.
    if (chloe0 == nullptr || nadine == nullptr) {
        log_line("BODY-DIFF aborted: need local player + Nadine NPC live");
        return;
    }
    // Resolve the MSVC-x64 RTTI class name of a polymorphic object (vtable[-1] ->
    // CompleteObjectLocator -> +0xC TypeDescriptor RVA -> +0x10 mangled name).
    auto rtti_name = [](void* obj, char* buf, std::size_t buflen) -> bool {
        buf[0] = 0;
        if (obj == nullptr || readable_bytes(obj) < sizeof(void*)) return false;
        std::uintptr_t vt = *reinterpret_cast<std::uintptr_t*>(obj);
        if (vt <= g_module || readable_bytes(reinterpret_cast<void*>(vt - 8)) < 8)
            return false;
        std::uintptr_t col = *reinterpret_cast<std::uintptr_t*>(vt - 8);
        if (col <= g_module || readable_bytes(reinterpret_cast<void*>(col)) < 16)
            return false;
        std::uint32_t td_rva = *reinterpret_cast<std::uint32_t*>(col + 12);
        std::uintptr_t td = g_module + td_rva;
        const char* name = reinterpret_cast<const char*>(td + 0x10);
        if (readable_bytes(name) < 4) return false;
        std::size_t i = 0;
        for (; i < buflen - 1; ++i) {
            if (readable_bytes(name + i) < 1) break;
            char c = name[i];
            if (c == 0) break;
            buf[i] = c;
        }
        buf[i] = 0;
        return i > 0;
    };
    // 2-level component map of the NadineBuddy object: scan the top object for
    // polymorphic sub-objects, then scan each found sub-object for its own
    // polymorphic children. The body/art component (DrawControl/AnimData/
    // ArtGroup/Skeleton) will appear by RTTI NAME somewhere in this tree.
    char nname[96];
    char cname[96];
    static void* visited[256];
    int visited_n = 0;
    auto seen = [&](void* p) -> bool {
        for (int i = 0; i < visited_n; ++i) if (visited[i] == p) return true;
        if (visited_n < 256) visited[visited_n++] = p;
        return false;
    };
    int mapped = 0;
    // scan_obj logs every offset of `obj` that holds a polymorphic object.
    // depth 0 = the NadineBuddy itself (compare clone at same offset); collects
    // child pointers into `children` for the second pass.
    auto scan_obj = [&](void* obj, void* clone_obj, std::size_t span,
                        const char* tag, void** children, int& nchild, int maxchild) {
        for (std::size_t off = sizeof(void*); off + sizeof(void*) <= span; off += sizeof(void*)) {
            if (readable_bytes(obj) < off + 8) break;
            void* pn = *reinterpret_cast<void**>(static_cast<std::byte*>(obj) + off);
            if (!rtti_name(pn, nname, sizeof(nname))) continue;
            if (seen(pn)) continue;
            if (children != nullptr && nchild < maxchild) children[nchild++] = pn;
            cname[0] = 0;
            if (clone_obj != nullptr && readable_bytes(clone_obj) >= off + 8) {
                void* pc = *reinterpret_cast<void**>(static_cast<std::byte*>(clone_obj) + off);
                rtti_name(pc, cname, sizeof(cname));
            }
            ++mapped;
            std::ostringstream l;
            l << "BODY-MAP " << tag << "+0x" << std::hex << off
              << " nadine=" << nname;
            if (clone_obj != nullptr) l << " | clone=" << (cname[0] ? cname : "(none)");
            l << " (ptr=0x" << reinterpret_cast<std::uintptr_t>(pn) << ")";
            log_line(l.str().c_str());
        }
    };
    void* lvl1[128];
    int n1 = 0;
    seen(nadine);
    scan_obj(nadine, clone1, 0x4000, "L0", lvl1, n1, 128);
    // Second level: scan each direct sub-object's first 0x600 bytes.
    for (int i = 0; i < n1; ++i) {
        char childtag[32];
        std::snprintf(childtag, sizeof(childtag), "L1[0x%llX] ",
                      static_cast<unsigned long long>(
                          reinterpret_cast<std::uintptr_t>(lvl1[i])));
        int dummy = 0;
        scan_obj(lvl1[i], nullptr, 0x600, childtag, nullptr, dummy, 0);
    }
    std::ostringstream done;
    done << "BODY-MAP done: " << std::dec << mapped << " polymorphic sub-objects (2 levels)";
    log_line(done.str().c_str());

    // LOOK-IDS probe (ndlib/render/look.cpp path): the look builder reads each
    // character's look id via vtable[94] (off 752) and variant ids at +0x178/+0x17C.
    // Capture these for Chloe / clone / Nadine -> they are the values to inject as a
    // look-override (spawn-ctx +0x90) to apply Nadine's render look SAMP-style.
    auto probe_look = [&](const char* tag, void* obj) {
        if (obj == nullptr || readable_bytes(obj) < 0x194) return;
        std::uint32_t look_id = safe_get_look_id(obj);
        std::uint32_t v178 = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(obj) + 0x178);
        std::uint32_t v17c = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(obj) + 0x17C);
        std::ostringstream l;
        l << "LOOK-IDS " << tag << " vtable[94]->look=0x" << std::hex << look_id
          << " +0x178=0x" << v178 << " +0x17C=0x" << v17c
          << " (+0x170=0x" << *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(obj) + 0x170)
          << " +0x190=0x" << *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(obj) + 0x190)
          << ")" << std::dec;
        log_line(l.str().c_str());
    };
    probe_look("chloe0", chloe0);
    probe_look("clone1", clone1);
    probe_look("nadine", nadine);

    // COLLISION probe: player+0x238 = the collision-body manager built by
    // InitializeCompositeBody (+0x10=collider count, +0x48=array of 272-byte
    // colliders tagged with hash("player")). Compare clone vs Chloe to see if the
    // clone's colliders are missing/empty (ghost) or built-but-not-physicalized.
    auto probe_collbody = [&](const char* tag, void* obj) {
        if (obj == nullptr || readable_bytes(obj) < 0x240) return;
        void* mgr = *reinterpret_cast<void**>(static_cast<std::byte*>(obj) + 0x238);
        std::ostringstream l;
        l << "COLLBODY " << tag << " mgr=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(mgr);
        if (mgr != nullptr && readable_bytes(mgr) >= 0x80) {
            auto* m = static_cast<std::byte*>(mgr);
            // dump manager qwords 0x00..0x78
            l << " mgr[";
            for (std::size_t o = 0; o <= 0x78; o += 8)
                l << "+0x" << o << "=0x" << *reinterpret_cast<std::uint64_t*>(m + o) << " ";
            l << "]";
            void* arr = *reinterpret_cast<void**>(m + 0x48);
            if (arr != nullptr && readable_bytes(arr) >= 0x110) {
                auto* e = static_cast<std::byte*>(arr);
                l << " e0[";
                for (std::size_t o = 0; o <= 0x108; o += 8)
                    l << "+0x" << o << "=0x" << *reinterpret_cast<std::uint64_t*>(e + o) << " ";
                l << "]";
            }
        }
        l << std::dec;
        log_line(l.str().c_str());
    };
    probe_collbody("chloe0", chloe0);
    probe_collbody("clone1", clone1);

    // APPEARANCE probe: Player appearance hash = object+0x15A4 (returned by
    // vtable[92]=sub_140A1DCC0, fed to the look builder -> appearance-record
    // lookups -> mesh). Read it for both Chloe-players, and Nadine's appearance
    // hash via her vtable[92]. The swap = make the clone's look build use Nadine's.
    auto probe_appearance = [&](const char* tag, void* obj) {
        if (obj == nullptr || readable_bytes(obj) < 0x15A8) return;
        std::uint32_t f15a4 = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(obj) + 0x15A4);
        std::uint32_t v92 = safe_get_dword_vslot(obj, 736);  // vtable[92]
        std::ostringstream l;
        l << "APPEAR " << tag << " +0x15A4=0x" << std::hex << f15a4
          << " vtable[92]=0x" << v92 << std::dec;
        log_line(l.str().c_str());
    };
    probe_appearance("chloe0", chloe0);
    probe_appearance("clone1", clone1);
    probe_appearance("nadine", nadine);

    // JOINT-CACHE probe (Variant B avatar): locate a character's draw-control +
    // FgAnimData + joint cache so we can drive its pose from the net. From the look
    // builder: object+0x198 = IDrawControl, +0x1A8 = FgAnimData, +0x164 = jointCount.
    // IDrawControl layout: [0]=FgAnimData, +8=joint array (592 B/joint = pose), +16=count.
    auto probe_joints = [&](const char* tag, void* obj) {
        if (obj == nullptr || readable_bytes(obj) < 0x1B0) return;
        auto* o = static_cast<std::byte*>(obj);
        void* dc  = *reinterpret_cast<void**>(o + 0x198);   // IDrawControl
        void* fg  = *reinterpret_cast<void**>(o + 0x1A8);   // FgAnimData
        std::uint32_t jc = *reinterpret_cast<std::uint32_t*>(o + 0x164);
        std::ostringstream l;
        l << "JOINTS " << tag << " drawctrl(+0x198)=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(dc)
          << " fganim(+0x1A8)=0x" << reinterpret_cast<std::uintptr_t>(fg)
          << " jointCount(+0x164)=" << std::dec << jc;
        if (dc != nullptr && readable_bytes(dc) >= 0x18) {
            auto* d = static_cast<std::byte*>(dc);
            void* fgInDc = *reinterpret_cast<void**>(d + 0x00);
            void* jarr   = *reinterpret_cast<void**>(d + 0x08);
            std::uint32_t dcCount = *reinterpret_cast<std::uint32_t*>(d + 0x10);
            l << std::hex << " | dc[0]=fg?0x" << reinterpret_cast<std::uintptr_t>(fgInDc)
              << " dc[+8]=jointArr=0x" << reinterpret_cast<std::uintptr_t>(jarr)
              << " dc[+0x10]=count=" << std::dec << dcCount;
        }
        // Dump the FgAnimData head (qwords 0..0x78) to find the FULL joint/pose cache.
        void* skel = nullptr;
        void* pose = nullptr;
        if (fg != nullptr && readable_bytes(fg) >= 0x80) {
            auto* f = static_cast<std::byte*>(fg);
            pose = *reinterpret_cast<void**>(f + 0x10);  // per-instance joint cache (pose)
            skel = *reinterpret_cast<void**>(f + 0x18);  // shared ArtItemSkeleton
        }
        log_line(l.str().c_str());
        // Skeleton head (find joint count) + joint-cache head (find matrix size).
        if (skel != nullptr && readable_bytes(skel) >= 0x40) {
            auto* s = static_cast<std::byte*>(skel);
            std::ostringstream sl;
            sl << "JOINTS " << tag << " SKEL=0x" << std::hex
               << reinterpret_cast<std::uintptr_t>(skel) << " head[";
            for (std::size_t o = 0; o <= 0x38; o += 4)
                sl << "+0x" << o << "=0x" << *reinterpret_cast<std::uint32_t*>(s + o) << " ";
            sl << "]" << std::dec;
            log_line(sl.str().c_str());
        }
        if (pose != nullptr && readable_bytes(pose) >= 0x60) {
            auto* p = static_cast<std::byte*>(pose);
            std::uint64_t xforms = *reinterpret_cast<std::uint64_t*>(p + 0x0);  // transforms array
            std::uint32_t jc2 = *reinterpret_cast<std::uint32_t*>(p + 0x8);     // joint count
            std::ostringstream pl;
            pl << "JOINTS " << tag << " JointCache=0x" << std::hex
               << reinterpret_cast<std::uintptr_t>(pose)
               << " xforms=0x" << xforms << " count=" << std::dec << jc2;
            log_line(pl.str().c_str());
            // dump first 2 joints' transform as floats to find the per-joint stride/format
            if (xforms != 0 && readable_bytes(reinterpret_cast<void*>(xforms)) >= 0x60) {
                auto* x = reinterpret_cast<float*>(xforms);
                std::ostringstream xl;
                xl << "JOINTS " << tag << " xform[0..23]=[";
                for (int i = 0; i < 24; ++i) xl << x[i] << " ";
                xl << "]";
                log_line(xl.str().c_str());
            }
        }
    };
    probe_joints("chloe0", chloe0);
    probe_joints("nadine", nadine);

    // DESC-SCAN: hunt Chloe's per-instance identity values inside the captured
    // spawn descriptor/ctx/a4 (these are PER-SPAWN, so a hit = a field we can flip
    // to Nadine for the clone ONLY, leaving main Chloe untouched). Scan flat + one
    // pointer level. Chloe: state_id=0x6c2b2396, look2=0xcde4b742, look_id=0x3f334972.
    {
        const std::uint32_t chloe_vals[] = {0x6c2b2396u, 0xcde4b742u, 0x3f334972u};
        const char* val_name[] = {"state_id", "look2", "look_id"};
        auto scan_flat = [&](const char* tag, std::byte* mem, std::size_t span) {
            if (mem == nullptr) return;
            std::size_t avail = readable_bytes(mem);
            if (avail < 4) return;
            if (span > avail) span = avail;
            for (std::size_t off = 0; off + 4 <= span; off += 4) {
                std::uint32_t v;
                std::memcpy(&v, mem + off, 4);
                for (int k = 0; k < 3; ++k) {
                    if (v == chloe_vals[k]) {
                        std::ostringstream l;
                        l << "DESC-SCAN " << tag << "+0x" << std::hex << off
                          << " = chloe " << val_name[k] << " 0x" << v << std::dec;
                        log_line(l.str().c_str());
                    }
                }
            }
        };
        scan_flat("desc", reinterpret_cast<std::byte*>(g_spawn_desc), sizeof(g_spawn_desc));
        scan_flat("ctx", reinterpret_cast<std::byte*>(g_spawn_ctx), 0x800);
        scan_flat("a4", reinterpret_cast<std::byte*>(g_spawn_a4), 0x800);
        // one pointer level out of the descriptor
        for (std::size_t off = 0; off + 8 <= sizeof(g_spawn_desc); off += 8) {
            void* p;
            std::memcpy(&p, g_spawn_desc + off, 8);
            if (p == nullptr || reinterpret_cast<std::uintptr_t>(p) < 0x10000) continue;
            if (readable_bytes(p) < 8) continue;
            char tag[40];
            std::snprintf(tag, sizeof(tag), "desc[+0x%llX]->",
                          static_cast<unsigned long long>(off));
            scan_flat(tag, static_cast<std::byte*>(p), 0x200);
        }
        log_line("DESC-SCAN done");
    }
}

void log_camera_transform() {
    const auto get_camera_manager =
        reinterpret_cast<GetCameraManagerFunction>(
            g_module + kGetCameraManagerRva
        );
    const auto get_current_camera =
        reinterpret_cast<GetCurrentCameraFunction>(
            g_module + kGetCurrentCameraRva
        );
    void* const manager = get_camera_manager(0);
    void* const camera =
        manager != nullptr ? get_current_camera(manager, false) : nullptr;

    std::ostringstream line;
    line << "Camera snapshot manager=0x" << std::hex
         << reinterpret_cast<std::uintptr_t>(manager) << " camera=0x"
         << reinterpret_cast<std::uintptr_t>(camera) << std::dec;
    log_line(line.str().c_str());
    if (camera == nullptr) {
        return;
    }

    const auto vtable = *reinterpret_cast<std::uintptr_t**>(camera);
    const auto function_address = vtable[0x2F0 / sizeof(void*)];
    const auto get_camera_transform =
        reinterpret_cast<GetCameraTransformFunction>(function_address);
    alignas(32) float transform[16]{};
    void* const result = get_camera_transform(camera, transform);

    std::ostringstream method_line;
    method_line << " camera transform vfunc rva=0x" << std::hex
                << (function_address - g_module) << " result=0x"
                << reinterpret_cast<std::uintptr_t>(result);
    log_line(method_line.str().c_str());
    for (std::size_t index = 0; index < 12; index += 4) {
        std::ostringstream values_line;
        values_line << " transform+0x" << std::hex
                    << index * sizeof(float) << std::dec
                    << " = " << transform[index] << ", "
                    << transform[index + 1] << ", "
                    << transform[index + 2] << ", "
                    << transform[index + 3];
        log_line(values_line.str().c_str());
    }

    constexpr std::size_t manager_offsets[] = {
        0x1A0, 0x1B0, 0x1C0, 0x1D0, 0x1F0,
        0x200, 0x210, 0x220, 0x240, 0x250,
    };
    for (const std::size_t offset : manager_offsets) {
        const auto* values = reinterpret_cast<const float*>(
            static_cast<const std::byte*>(manager) + offset
        );
        std::ostringstream values_line;
        values_line << " manager+0x" << std::hex << offset << std::dec
                    << " = " << values[0] << ", " << values[1] << ", "
                    << values[2] << ", " << values[3];
        log_line(values_line.str().c_str());
    }
}

void* get_net_game_manager() {
    return reinterpret_cast<void*>(g_module + kNetGameManagerGlobalRva);
}

void* get_net_player_context() {
    return reinterpret_cast<void*>(g_module + kNetPlayerContextGlobalRva);
}

void* get_net_player_source_array_entry(
    std::uintptr_t array_rva,
    std::uint32_t index
) {
    return reinterpret_cast<void*>(
        g_module + array_rva + index * kNetPlayerSourceEntrySize
    );
}

void* get_slot1_tracker() {
    const auto get_tracker =
        reinterpret_cast<GetNetPlayerTrackerByScreenIndexFunction>(
            g_module + kGetNetPlayerTrackerByScreenIndexRva
        );
    return get_tracker(get_net_game_manager(), kSlot1ScreenIndex);
}

void* try_create_slot1_tracker_from_source_object(const void* source_object) {
    if (source_object == nullptr) {
        log_line("slot1 tracker creation skipped: source object is null");
        return nullptr;
    }

    {
        std::ostringstream line;
        line << "slot1 tracker create source=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(source_object);
        log_line(line.str().c_str());
    }

    const auto add_or_update_tracker =
        reinterpret_cast<AddOrUpdateNetPlayerTrackerFunction>(
            g_module + kAddOrUpdateNetPlayerTrackerRva
        );
    void* const tracker = add_or_update_tracker(
        get_net_game_manager(),
        get_net_player_context(),
        source_object,
        kSlot1ScreenIndex,
        0
    );
    if (tracker == nullptr) {
        log_line("slot1 tracker creation failed via AddOrUpdateNetPlayerTracker");
        return nullptr;
    }

    {
        std::ostringstream line;
        line << "slot1 tracker created via AddOrUpdateNetPlayerTracker scaffold"
             << " tracker=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(tracker);
        log_line(line.str().c_str());
    }
    return tracker;
}

RespawnSeedRecord build_default_respawn_seed(std::int32_t respawn_mode) {
    RespawnSeedRecord seed{};
    seed.respawn_mode = respawn_mode;
    std::memcpy(
        seed.default_vec4,
        reinterpret_cast<const void*>(g_module + kRespawnSeedDefaultVec4Rva),
        sizeof(seed.default_vec4)
    );
    return seed;
}

bool try_slot1_netplayer_respawn_scaffold(void* nadine_object) {
    if constexpr (!kEnableBuiltInSlot1NetPlayerScaffold) {
        return false;
    }

    if (nadine_object == nullptr) {
        log_line("slot1 scaffold aborted: Nadine object is null");
        return false;
    }

    void* tracker = get_slot1_tracker();
    if (tracker == nullptr) {
        const void* source_object =
            get_net_player_source_array_entry(kNetPlayerSourceArrayARva, 0);
        if (source_object == nullptr) {
            log_line("slot1 scaffold sourceA[0] is null; falling back to live NadineBuddy");
            source_object = nadine_object;
        }
        tracker = try_create_slot1_tracker_from_source_object(source_object);
    }
    if (tracker == nullptr) {
        log_line("slot1 scaffold aborted: slot1 tracker is null");
        return false;
    }

    const auto build_block =
        reinterpret_cast<BuildRespawnTransformBlockFunction>(
            g_module + kBuildRespawnTransformBlockRva
        );
    const auto net_respawn =
        reinterpret_cast<NetPlayerTrackerNetRespawnFunction>(
            g_module + kNetPlayerTrackerNetRespawnRva
        );

    const RespawnSeedRecord seed = build_default_respawn_seed(0);
    RespawnTransformBlock block{};
    build_block(
        &block,
        static_cast<const std::byte*>(nadine_object) + kReferenceTransformOffset,
        &seed
    );
    net_respawn(tracker, seed.respawn_mode, 1, &block);
    {
        std::ostringstream line;
        line << "slot1 scaffold issued direct NetPlayerTracker::NetRespawn"
             << " tracker=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(tracker)
             << " nadine=0x"
             << reinterpret_cast<std::uintptr_t>(nadine_object);
        log_line(line.str().c_str());
    }
    return true;
}

bool get_camera_axes_idx(
    int idx,
    float& forward_x,
    float& forward_z,
    float& right_x,
    float& right_z
) {
    const auto get_camera_manager =
        reinterpret_cast<GetCameraManagerFunction>(
            g_module + kGetCameraManagerRva
        );
    void* const manager = get_camera_manager(idx);
    if (manager == nullptr || readable_bytes(
            static_cast<std::byte*>(manager) + kCameraRotationOffset + 16) < 4) {
        return false;
    }
    const auto* rotation = reinterpret_cast<const float*>(
        static_cast<const std::byte*>(manager) + kCameraRotationOffset);
    const float x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];
    forward_x = 2.0F * (x * z + w * y);
    forward_z = 1.0F - 2.0F * (x * x + y * y);
    right_x = 1.0F - 2.0F * (y * y + z * z);
    right_z = 2.0F * (x * z - w * y);
    const float fl = std::sqrt(forward_x * forward_x + forward_z * forward_z);
    const float rl = std::sqrt(right_x * right_x + right_z * right_z);
    if (fl < 0.001F || rl < 0.001F) {
        return false;
    }
    forward_x /= fl; forward_z /= fl; right_x /= rl; right_z /= rl;
    return true;
}

bool get_camera_axes(
    float& forward_x,
    float& forward_z,
    float& right_x,
    float& right_z
) {
    const auto get_camera_manager =
        reinterpret_cast<GetCameraManagerFunction>(
            g_module + kGetCameraManagerRva
        );
    void* const manager = get_camera_manager(0);
    if (manager == nullptr) {
        return false;
    }

    const auto* rotation = reinterpret_cast<const float*>(
        static_cast<const std::byte*>(manager) + kCameraRotationOffset
    );
    const float x = rotation[0];
    const float y = rotation[1];
    const float z = rotation[2];
    const float w = rotation[3];

    forward_x = 2.0F * (x * z + w * y);
    forward_z = 1.0F - 2.0F * (x * x + y * y);
    right_x = 1.0F - 2.0F * (y * y + z * z);
    right_z = 2.0F * (x * z - w * y);

    const float forward_length = std::sqrt(
        forward_x * forward_x + forward_z * forward_z
    );
    const float right_length = std::sqrt(
        right_x * right_x + right_z * right_z
    );
    if (forward_length < 0.001F || right_length < 0.001F) {
        return false;
    }

    forward_x /= forward_length;
    forward_z /= forward_length;
    right_x /= right_length;
    right_z /= right_length;
    return true;
}

InputCommand read_local_keyboard_command() {
    InputCommand command{};
    if (!g_control_enabled) {
        return command;
    }

    if ((GetAsyncKeyState('W') & 0x8000) != 0) {
        command.forward += 1.0F;
        command.input_mask |= 1;
    }
    if ((GetAsyncKeyState('S') & 0x8000) != 0) {
        command.forward -= 1.0F;
        command.input_mask |= 2;
    }
    if ((GetAsyncKeyState('A') & 0x8000) != 0) {
        command.right -= 1.0F;
        command.input_mask |= 4;
    }
    if ((GetAsyncKeyState('D') & 0x8000) != 0) {
        command.right += 1.0F;
        command.input_mask |= 8;
    }
    return command;
}

InputCommand decode_input_packet(const coop::InputPacket& packet) {
    InputCommand command{};
    command.forward = packet.move_forward;
    command.right = -packet.move_right;
    command.input_mask = static_cast<std::uint8_t>(packet.buttons & 0xFF);
    return command;
}

bool submit_remote_input_packet(const coop::InputPacket& packet) {
    if (packet.magic != coop::kInputPacketMagic ||
        packet.version != coop::kInputPacketVersion) {
        return false;
    }

    g_remote_command_state.last_packet = packet;
    g_remote_command_state.command = decode_input_packet(packet);
    g_remote_command_state.received_at_ms = GetTickCount64();
    g_remote_command_state.has_packet = true;
    return true;
}

InputCommand get_active_command() {
    if (!g_control_enabled) {
        return {};
    }

    if (g_command_source == CommandSource::RemoteClient) {
        if (!g_remote_command_state.has_packet) {
            return {};
        }
        const ULONGLONG age_ms =
            GetTickCount64() - g_remote_command_state.received_at_ms;
        if (age_ms <= kRemoteCommandTimeoutMs) {
            return g_remote_command_state.command;
        }
        return {};
    }

    return read_local_keyboard_command();
}

DWORD WINAPI udp_receiver_thread(void*) {
    sockaddr_in source_address{};
    int source_address_length = sizeof(source_address);
    while (g_udp_receiver.running.load()) {
        coop::InputPacket packet{};
        const int received = recvfrom(
            g_udp_receiver.socket,
            reinterpret_cast<char*>(&packet),
            sizeof(packet),
            0,
            reinterpret_cast<sockaddr*>(&source_address),
            &source_address_length
        );
        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                continue;
            }
            std::ostringstream line;
            line << "UDP recvfrom failed: " << error;
            log_line(line.str().c_str());
            break;
        }
        if (received != sizeof(coop::InputPacket)) {
            continue;
        }
        if (!submit_remote_input_packet(packet)) {
            log_line("Rejected remote input packet");
        }
    }
    return 0;
}

bool start_udp_receiver() {
    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        log_line("WSAStartup failed");
        return false;
    }
    g_udp_receiver.winsock_ready = true;

    g_udp_receiver.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp_receiver.socket == INVALID_SOCKET) {
        log_line("UDP socket creation failed");
        WSACleanup();
        g_udp_receiver.winsock_ready = false;
        return false;
    }

    DWORD timeout_ms = 100;
    setsockopt(
        g_udp_receiver.socket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout_ms),
        sizeof(timeout_ms)
    );

    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_address.sin_port = htons(coop::kHostInputPort);
    if (bind(
            g_udp_receiver.socket,
            reinterpret_cast<const sockaddr*>(&bind_address),
            sizeof(bind_address)
        ) == SOCKET_ERROR) {
        std::ostringstream line;
        line << "UDP bind failed: " << WSAGetLastError();
        log_line(line.str().c_str());
        closesocket(g_udp_receiver.socket);
        g_udp_receiver.socket = INVALID_SOCKET;
        WSACleanup();
        g_udp_receiver.winsock_ready = false;
        return false;
    }

    g_udp_receiver.running.store(true);
    g_udp_receiver.thread = CreateThread(
        nullptr,
        0,
        udp_receiver_thread,
        nullptr,
        0,
        nullptr
    );
    if (g_udp_receiver.thread == nullptr) {
        log_line("Failed to create UDP receiver thread");
        g_udp_receiver.running.store(false);
        closesocket(g_udp_receiver.socket);
        g_udp_receiver.socket = INVALID_SOCKET;
        WSACleanup();
        g_udp_receiver.winsock_ready = false;
        return false;
    }

    std::ostringstream line;
    line << "UDP input receiver listening on port " << coop::kHostInputPort;
    log_line(line.str().c_str());
    return true;
}

void issue_move_command(
    void* object,
    float direction_x,
    float direction_z,
    float goal_distance,
    bool log_target
) {
    if (object == nullptr || readable_bytes(object) < 0x27D1 ||
        *reinterpret_cast<std::uintptr_t*>(object) !=
            g_module + kNadineBuddyVtableRva) {
        return;
    }
    void* const clock = *reinterpret_cast<void**>(
        static_cast<std::byte*>(object) + 0xE0);
    if (clock == nullptr || readable_bytes(clock) < 0x18) {
        return;
    }
    alignas(32) std::byte navigation_frame[0x60]{};
    alignas(16) float target[4]{};
    alignas(16) std::byte facing[0x10]{};
    alignas(16) std::byte move_parameters[0x30]{};
    const auto* world_position = reinterpret_cast<const float*>(
        static_cast<const std::byte*>(object) + kWorldPositionOffset
    );
    if (!valid_network_position(world_position) || !std::isfinite(direction_x) ||
        !std::isfinite(direction_z) || !std::isfinite(goal_distance) ||
        goal_distance < 0.0F || goal_distance > 10.0F) {
        return;
    }
    target[0] = world_position[0] + direction_x * goal_distance;
    target[1] = world_position[1];
    target[2] = world_position[2] + direction_z * goal_distance;
    target[3] = world_position[3];
    std::memcpy(
        facing,
        static_cast<const std::byte*>(object) + kFacingOffset,
        sizeof(facing)
    );
    std::memcpy(
        move_parameters,
        static_cast<const std::byte*>(object) + kMoveParametersOffset,
        sizeof(move_parameters)
    );

    const auto controller = *reinterpret_cast<std::uintptr_t*>(
        static_cast<std::byte*>(object) + kLocomotionControllerOffset
    );
    if (controller == 0 ||
        readable_bytes(reinterpret_cast<void*>(controller)) <
            kNavigationRadiusOffset + sizeof(float)) {
        return;
    }

    const float navigation_radius = *reinterpret_cast<float*>(
        controller + kNavigationRadiusOffset
    );
    if (!std::isfinite(navigation_radius) || navigation_radius <= 0.0F ||
        navigation_radius > 100.0F) {
        return;
    }
    const auto build_navigation_frame =
        reinterpret_cast<BuildNavigationFrameFunction>(
            g_module + kBuildNavigationFrameRva
        );
    const auto move_to = reinterpret_cast<MoveToFunction>(
        g_module + kMoveToRva
    );

    build_navigation_frame(
        navigation_frame,
        target,
        1,
        navigation_radius
    );
    // The campaign script wrapper always validates the projected frame before it
    // writes command type 2. Calling MoveTo without this check can enqueue an
    // off-navmesh/invalid command and fail later inside the navigation update.
    using ValidateNavigationFrameFunction = bool(__fastcall*)(void*, const void*);
    const auto validate_navigation_frame =
        reinterpret_cast<ValidateNavigationFrameFunction>(g_module + 0x1DEF6C0);
    if (!validate_navigation_frame(nullptr, navigation_frame)) {
        return;
    }
    move_to(
        object,
        navigation_frame,
        facing,
        move_parameters,
        1,
        true,
        false,
        nullptr,
        0,
        false
    );

    if (log_target) {
        std::ostringstream line;
        line << "MoveTo object=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(object) << std::dec
             << " target=" << target[0] << ", " << target[1] << ", "
             << target[2];
        log_line(line.str().c_str());
    }
}

void issue_type4_command(void* object, bool log_command) {
    const auto* move_parameters = reinterpret_cast<const std::byte*>(
        static_cast<const std::byte*>(object) + kMoveParametersOffset
    );
    const std::int32_t type_2508 = *reinterpret_cast<const std::int32_t*>(
        static_cast<const std::byte*>(object) + kType2508Offset
    );
    const std::int32_t type_2514 = *reinterpret_cast<const std::int32_t*>(
        static_cast<const std::byte*>(object) + kType2514Offset
    );
    const std::uint8_t flags0 = *reinterpret_cast<const std::uint8_t*>(
        static_cast<const std::byte*>(object) + kType2560Offset
    );
    const std::uint8_t flags1 = *reinterpret_cast<const std::uint8_t*>(
        static_cast<const std::byte*>(object) + kType2561Offset
    );
    const auto type4_command = reinterpret_cast<Type4CommandFunction>(
        g_module + kType4CommandRva
    );

    type4_command(
        object,
        type_2508,
        move_parameters,
        type_2514,
        static_cast<std::uint8_t>((flags0 >> 3) & 0x1),
        static_cast<std::uint8_t>(flags1 & 0x1),
        nullptr,
        0,
        false
    );

    if (log_command) {
        std::ostringstream line;
        line << "Type4Command object=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(object) << std::dec
             << " 2508=" << type_2508 << " 2514=" << type_2514
             << " flag0=" << ((flags0 >> 3) & 0x1)
             << " flag1=" << (flags1 & 0x1);
        log_line(line.str().c_str());
    }
}

void issue_type7_steering(
    void* object,
    float forward_axis,
    float right_axis,
    bool log_command
) {
    const auto begin_command = reinterpret_cast<BeginNpcCommandFunction>(
        g_module + kBeginNpcCommandRva
    );
    begin_command(object, 7, nullptr, nullptr, 0);

    const auto steer_forward = static_cast<std::int32_t>(forward_axis * 1000.0f);
    const auto steer_right = static_cast<std::int32_t>(right_axis * 1000.0f);
    *reinterpret_cast<std::int32_t*>(static_cast<std::byte*>(object) + 0x2558) = steer_forward;
    *reinterpret_cast<std::int32_t*>(static_cast<std::byte*>(object) + 0x255C) = steer_right;
    auto* flags0 = reinterpret_cast<std::uint8_t*>(static_cast<std::byte*>(object) + kType2560Offset);
    *flags0 &= 0xFE;
    if (std::abs(steer_forward) > 0 || std::abs(steer_right) > 0) {
        *flags0 |= 1;
    }
    auto* state = reinterpret_cast<std::uint8_t*>(static_cast<std::byte*>(object) + 0x1D57);
    *state &= static_cast<std::uint8_t>(~0x4);

    if (log_command) {
        std::ostringstream line;
        line << "Type7Steering object=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(object) << std::dec
             << " forward=" << steer_forward << " right=" << steer_right
             << " flags0=" << static_cast<int>(*flags0);
        log_line(line.str().c_str());
    }
}

// Drive the remote-player avatar (NadineBuddy) toward the replicated peer
// position using native NPC move-intent, so she RUNS with full animation
// instead of being teleported. This is the proven buddy locomotion path.
// Single-PC validation: when there is no network peer, feed the LOCAL player's own
// position as the "peer" target. Combined with the 1200ms replay ring below, the
// NadineBuddy avatar then RETRACES the player's path with full run animation — the
// unambiguous one-PC proof of the animated-avatar drive. Harmless on two real PCs
// (has_peer_state is true there, so this fallback never runs).
bool g_avatar_self_mirror = true;
bool valid_network_position(const float* position) {
    constexpr float kCoordinateLimit = 1000000.0F;
    return position != nullptr &&
           std::isfinite(position[0]) && std::isfinite(position[1]) &&
           std::isfinite(position[2]) &&
           std::abs(position[0]) < kCoordinateLimit &&
           std::abs(position[1]) < kCoordinateLimit &&
           std::abs(position[2]) < kCoordinateLimit;
}

bool valid_network_movement(const coop::StatePacket& packet) {
    if ((packet.flags & 4) == 0 || !std::isfinite(packet.move_magnitude) ||
        packet.move_magnitude < 0.0F || packet.move_magnitude > 1.01F) {
        return false;
    }
    for (float axis : packet.move_stick) {
        if (!std::isfinite(axis) || std::abs(axis) > 1.01F) return false;
    }
    for (float component : packet.move_world) {
        if (!std::isfinite(component) || std::abs(component) > 1.01F) return false;
    }
    return true;
}

bool valid_network_orientation(const float* quaternion) {
    if (quaternion == nullptr) return false;
    float norm_sq = 0.0F;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(quaternion[i]) || std::abs(quaternion[i]) > 1.01F) {
            return false;
        }
        norm_sq += quaternion[i] * quaternion[i];
    }
    return norm_sq > 0.90F && norm_sq < 1.10F;
}

bool compute_remote_position_correction(
    const coop::StatePacket& peer, const void* remote_player,
    float& correction_x, float& correction_z) {
    correction_x = 0.0F;
    correction_z = 0.0F;
    // A correction-only move is internally inconsistent: the remote Player then
    // has zero authored input but a non-zero world vector, so its graph stays in
    // idle while physics drags the body. Reconcile ordinary drift only while the
    // peer is genuinely moving; the existing >4 m snap still handles teleports.
    if (!valid_network_movement(peer) || peer.move_magnitude <= 0.01F) {
        return false;
    }
    if (remote_player == nullptr ||
        readable_bytes(static_cast<const std::byte*>(remote_player) +
                       kWorldPositionOffset + 3 * sizeof(float)) < sizeof(float)) {
        return false;
    }
    const auto* position = reinterpret_cast<const float*>(
        static_cast<const std::byte*>(remote_player) + kWorldPositionOffset);
    const float dx = peer.pos[0] - position[0];
    const float dz = peer.pos[2] - position[2];
    const float distance_sq = dx * dx + dz * dz;
    if (!std::isfinite(distance_sq)) return false;

    constexpr float kDeadband = 0.20F;
    constexpr float kFullCorrectionDistance = 2.0F;
    constexpr float kMaxCorrection = 0.40F;
    if (distance_sq <= kDeadband * kDeadband) return false;

    const float distance = std::sqrt(distance_sq);
    float strength = (distance - kDeadband) /
                     (kFullCorrectionDistance - kDeadband) * kMaxCorrection;
    if (strength > kMaxCorrection) strength = kMaxCorrection;
    constexpr float kMinCorrection = 0.04F;
    if (strength < kMinCorrection) return false;
    correction_x = dx / distance * strength;
    correction_z = dz / distance * strength;
    return true;
}

void drive_nadine_toward_peer(void* object, ObjectControlState& control_state) {
    // This runs synchronously from the NPC's game-thread update. Keeping it behind
    // foreground-window ownership made one-PC two-instance synchronization stop as
    // soon as the user switched windows.
    if (!world_stable()) {
        return;
    }
    coop::StatePacket live{};
    bool have_live = false;
    bool from_network = false;
    if (g_state_net.enabled && g_state_net.has_peer_state.load(std::memory_order_acquire)) {
        if (!copy_peer_state_snapshot(live)) {
            return;
        }
        have_live = true;
        from_network = true;
    } else if (g_avatar_self_mirror) {
        void* chloe = retained_player_object(0);
        if (chloe != nullptr &&
            readable_bytes(static_cast<std::byte*>(chloe) + kWorldPositionOffset) >= 16) {
            const auto* p = reinterpret_cast<const float*>(
                static_cast<std::byte*>(chloe) + kWorldPositionOffset);
            live.pos[0] = p[0];
            live.pos[1] = p[1];
            live.pos[2] = p[2];
            have_live = true;
        }
    }
    if (!have_live) {
        return;
    }
    if (!valid_network_position(live.pos)) {
        return;
    }
    // Time-delayed path replay: record the incoming peer position each frame and
    // steer Nadine toward where the peer was ~kReplayDelayMs ago. On a single PC
    // (loopback, peer == our own Chloe) this makes Nadine visibly RETRACE the
    // player's route with a lag — unambiguous proof the send->recv->drive path
    // works (distinct from her stock AI follow). For REAL peer data (two instances)
    // use a small interpolation lag instead, so the avatar tracks the other player
    // responsively rather than 1.2s behind.
    const ULONGLONG kReplayDelayMs = from_network ? 120 : 1200;
    constexpr int kRingSize = 256;
    struct Sample { float x, z; ULONGLONG t; };
    static Sample ring[kRingSize]{};
    static int ring_head = 0;
    static bool ring_primed = false;

    const ULONGLONG now = GetTickCount64();
    ring[ring_head] = {live.pos[0], live.pos[2], now};
    ring_head = (ring_head + 1) % kRingSize;
    if (ring_head == 0) {
        ring_primed = true;
    }

    // Pick the most recent sample at least kReplayDelayMs old.
    float tgt_x = live.pos[0];
    float tgt_z = live.pos[2];
    bool have_target = false;
    ULONGLONG best_age = 0;
    for (int i = 0; i < kRingSize; ++i) {
        const Sample& s = ring[i];
        if (s.t == 0) {
            continue;
        }
        const ULONGLONG age = now - s.t;
        if (age >= kReplayDelayMs && (!have_target || age < best_age)) {
            tgt_x = s.x;
            tgt_z = s.z;
            best_age = age;
            have_target = true;
        }
    }
    if (!have_target) {
        return;  // not enough history yet
    }

    const auto* cur = reinterpret_cast<const float*>(
        static_cast<std::byte*>(object) + kWorldPositionOffset
    );
    if (!valid_network_position(cur)) {
        return;
    }
    const float dx = tgt_x - cur[0];
    const float dz = tgt_z - cur[2];
    const float dist = std::sqrt(dx * dx + dz * dz);
    if (!std::isfinite(dist)) {
        return;
    }
    static ULONGLONG last_diag = 0;
    if (now - last_diag > 500) {
        last_diag = now;
        std::ostringstream d;
        d << "nadine-drive: peer_seq=" << live.sequence
          << " nadine=(" << cur[0] << "," << cur[2] << ")"
          << " tgt=(" << tgt_x << "," << tgt_z << ")"
          << " dist=" << dist << " age=" << best_age;
        log_line(d.str().c_str());
    }
    constexpr float kArrive = 0.35F;
    // Re-issue the move only periodically (not every frame) so the navmesh path
    // can run smoothly; re-issuing every 16ms toward a shifting short-lookahead
    // target makes the run stutter.
    constexpr ULONGLONG kDriveCmdIntervalMs = 250;
    if (dist > kArrive) {
        if (now - control_state.last_command_time >= kDriveCmdIntervalMs) {
            // Use a SHORT fixed lookahead as goal distance (the proven-safe
            // manual-control pattern); passing the full far distance makes the
            // navmesh pathfind to an off-mesh point and crashes.
            const float goal = (dist < kGoalDistance) ? dist : kGoalDistance;
            issue_move_command(object, dx / dist, dz / dist, goal, false);
            control_state.last_command_time = now;
        }
        control_state.was_moving = true;
    } else if (control_state.was_moving) {
        issue_move_command(object, 0.0F, 0.0F, 0.0F, false);
        control_state.was_moving = false;
        control_state.last_command_time = now;
    }
}

void apply_control_command(
    void* object,
    ObjectControlState& control_state,
    const InputCommand& command
) {
    float forward_x = 0.0F;
    float forward_z = 1.0F;
    float right_x = 1.0F;
    float right_z = 0.0F;
    get_camera_axes(forward_x, forward_z, right_x, right_z);

    const float direction_x =
        forward_x * command.forward + right_x * command.right;
    const float direction_z =
        forward_z * command.forward + right_z * command.right;
    const float length = std::sqrt(
        direction_x * direction_x + direction_z * direction_z
    );
    const bool is_moving = length > 0.0F;
    const ULONGLONG now = GetTickCount64();
    const bool input_changed =
        command.input_mask != control_state.last_input_mask;
    const ULONGLONG command_interval =
        g_motion_backend == MotionBackend::Type4Command
            ? kType4CommandIntervalMs
            : (g_command_source == CommandSource::RemoteClient
                   ? kRemoteCommandIntervalMs
                   : kCommandIntervalMs);
    const float goal_distance =
        g_command_source == CommandSource::RemoteClient
            ? kRemoteGoalDistance
            : kGoalDistance;

    if (is_moving &&
        (input_changed ||
         now - control_state.last_command_time >= command_interval)) {
        const float normalized_x = direction_x / length;
        const float normalized_z = direction_z / length;
        if (g_motion_backend == MotionBackend::Type4Command) {
            issue_move_command(
                object,
                normalized_x,
                normalized_z,
                goal_distance,
                false
            );
            issue_type4_command(object, input_changed);
        } else if (g_motion_backend == MotionBackend::Type7Steering) {
            issue_type7_steering(object, command.forward, command.right, input_changed);
        } else {
            issue_move_command(
                object,
                normalized_x,
                normalized_z,
                goal_distance,
                input_changed
            );
        }
        control_state.last_command_time = now;
    } else if (!is_moving && control_state.was_moving) {
        if (g_motion_backend == MotionBackend::Type7Steering) {
            issue_type7_steering(object, 0.0f, 0.0f, true);
        } else {
            issue_move_command(object, 0.0F, 0.0F, goal_distance, true);
        }
        control_state.last_command_time = now;
    }

    control_state.was_moving = is_moving;
    control_state.last_input_mask = command.input_mask;
}

// Activate native 2-local-player mode and STAY logged in (no logout), so a
// second controller maps to player 2. Combine with the XInput idx=1 inject.
void split_login_persistent() {
    auto* second_available =
        reinterpret_cast<std::uint8_t*>(g_module + kSplitSecondPlayerAvailableRva);
    auto* split_login_state =
        reinterpret_cast<std::uint32_t*>(g_module + kSplitLoginStateRva);
    auto* max_local_players =
        reinterpret_cast<std::uint32_t*>(g_module + kMaxLocalPlayersRva);
    const auto net_login_split_screen =
        reinterpret_cast<ScriptCommandFunction>(g_module + kNetLoginSplitScreenRva);
    *max_local_players = 2;
    *second_available = 1;
    g_split_render_applied = false;
    std::uint64_t r = 0;
    net_login_split_screen(&r);
    std::ostringstream line;
    line << "split login PERSISTENT: max_local_players=" << *max_local_players
         << " avail=" << static_cast<int>(*second_available)
         << " login_state=" << *split_login_state;
    log_line(line.str().c_str());
}

void diagnose_split_screen_login() {
    auto* second_available =
        reinterpret_cast<std::uint8_t*>(g_module + kSplitSecondPlayerAvailableRva);
    auto* split_login_state =
        reinterpret_cast<std::uint32_t*>(g_module + kSplitLoginStateRva);
    auto* max_local_players =
        reinterpret_cast<std::uint32_t*>(g_module + kMaxLocalPlayersRva);
    const auto net_is_second_player_available =
        reinterpret_cast<ScriptQueryFunction>(g_module + kNetIsSecondPlayerAvailableRva);
    const auto net_login_split_screen =
        reinterpret_cast<ScriptCommandFunction>(g_module + kNetLoginSplitScreenRva);
    const auto net_logout_split_screen =
        reinterpret_cast<ScriptCommandFunction>(g_module + kNetLogoutSplitScreenRva);

    std::uint64_t query_result = 0;
    net_is_second_player_available(&query_result);

    std::ostringstream before;
    before << "split diag before: avail_byte=" << static_cast<int>(*second_available)
           << " login_state=" << *split_login_state
           << " max_local_players=" << *max_local_players
           << " query=" << query_result;
    log_line(before.str().c_str());

    *max_local_players = 2;
    *second_available = 1;
    g_split_render_applied = false;
    net_login_split_screen(&query_result);

    std::uint64_t post_login_query = 0;
    net_is_second_player_available(&post_login_query);
    const auto get_camera_manager =
        reinterpret_cast<GetCameraManagerFunction>(g_module + kGetCameraManagerRva);
    void* const cam0 = get_camera_manager(0);
    void* const cam1 = get_camera_manager(1);

    std::ostringstream after;
    after << "split diag after login: avail_byte=" << static_cast<int>(*second_available)
          << " login_state=" << *split_login_state
          << " max_local_players=" << *max_local_players
          << " query=" << post_login_query
          << " cam0=0x" << std::hex << reinterpret_cast<std::uintptr_t>(cam0)
          << " cam1=0x" << reinterpret_cast<std::uintptr_t>(cam1) << std::dec;
    log_line(after.str().c_str());

    net_logout_split_screen(&query_result);
    std::uint64_t post_logout_query = 0;
    net_is_second_player_available(&post_logout_query);
    std::ostringstream logout;
    logout << "split diag after logout: avail_byte=" << static_cast<int>(*second_available)
           << " login_state=" << *split_login_state
           << " query=" << post_logout_query;
    log_line(logout.str().c_str());
}

void log_local_player_targets(void* nadine_object) {
    auto* resolver_owner = reinterpret_cast<void*>(g_module + kLocalPlayerResolverGlobalRva);
    auto resolve = [&](std::uint32_t index) -> void* {
        auto* owner = reinterpret_cast<void**>(resolver_owner);
        if (owner == nullptr || *owner == nullptr) {
            return nullptr;
        }
        auto* vtbl = *reinterpret_cast<void***>(*owner);
        if (vtbl == nullptr) {
            return nullptr;
        }
        const auto fn = reinterpret_cast<ResolveLocalPlayerFunction>(vtbl[0xA8 / sizeof(void*)]);
        return fn(*owner, index);
    };

    auto log_object = [&](const char* label, void* object) {
        std::ostringstream line;
        line << label << "=0x" << std::hex << reinterpret_cast<std::uintptr_t>(object);
        if (object != nullptr) {
            const auto vtable = *reinterpret_cast<std::uintptr_t*>(object);
            const auto name = reinterpret_cast<const char*>(object) + sizeof(void*);
            line << " vtbl_rva=0x" << (vtable - g_module)
                 << " name='" << std::string_view(name) << "'";
        }
        line << std::dec;
        log_line(line.str().c_str());
    };

    void* const player0 = resolve(0);
    void* const player1 = resolve(1);
    log_line("local-player target snapshot:");
    log_object("  local-player-0", player0);
    log_object("  local-player-1", player1);
    log_object("  nadine-buddy", nadine_object);
}

volatile bool g_play_as_clone = false;  // NUMPAD3: route player-0 (input+camera) to the clone
void* __fastcall hooked_resolve_local_player(void* owner, std::uint32_t index) {
    void* result =
        g_original_resolve_local_player != nullptr
            ? g_original_resolve_local_player(owner, index)
            : nullptr;

    if (g_local_player1_bind_enabled && index == 1 && g_last_nadine_object != nullptr) {
        std::ostringstream line;
        line << "resolve_local_player(1): original=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(result)
             << " patched=0x" << reinterpret_cast<std::uintptr_t>(g_last_nadine_object)
             << std::dec;
        log_line(line.str().c_str());
        return g_last_nadine_object;
    }
    // (resolve_local_player(0)->clone redirect REMOVED: input is not bound via this
    // resolver, so it just froze the clone. Control = scripted slot-1 + pin Chloe.)
    return result;
}

void force_split_render_mode() {
    if (!g_force_split_render_enabled || g_split_render_applied) {
        return;
    }
    auto* login_state = reinterpret_cast<std::uint32_t*>(g_module + kSplitLoginStateRva);
    if (*login_state != 3) {
        return;
    }
    auto* render_mode = reinterpret_cast<std::uint32_t*>(g_module + kRenderSplitModeRva);
    auto* render_aux = reinterpret_cast<std::uint32_t*>(g_module + kRenderSplitAuxModeRva);
    *render_mode = 2;
    *render_aux = 2;
    g_split_render_applied = true;
    std::ostringstream line;
    line << "applied one-shot split render mode: mode=" << *render_mode
         << " aux=" << *render_aux
         << " login_state=" << *login_state;
    log_line(line.str().c_str());
}

void set_nadine_control_state(void* object, std::uint32_t state_id) {
    const auto set_state = reinterpret_cast<SetControlActionStateFunction>(
        g_module + kSetControlActionStateIdAndForward168Rva
    );
    set_state(object, state_id);
    std::ostringstream line;
    line << "SetControlActionStateIdAndForward_168 state=" << state_id
         << " object=0x" << std::hex << reinterpret_cast<std::uintptr_t>(object)
         << std::dec;
    log_line(line.str().c_str());
}

void register_nadine_as_character() {
    if (g_last_nadine_object == nullptr) {
        log_line("AddCharacter skipped: Nadine object is null");
        return;
    }
    auto* manager = *reinterpret_cast<void**>(g_module + kLocalPlayerResolverGlobalRva);
    if (manager == nullptr) {
        log_line("AddCharacter skipped: CharacterManager is null");
        return;
    }
    const auto count_before = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(manager) + 0xC);
    const auto capacity = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(manager) + 0x8);
    using AddCharacterFunction = void(__fastcall*)(void*, void*);
    const auto add_character = reinterpret_cast<AddCharacterFunction>(g_module + kCharacterManagerAddCharacterRva);
    {
        std::ostringstream line;
        line << "AddCharacter pre: manager=0x" << std::hex << reinterpret_cast<std::uintptr_t>(manager)
             << " nadine=0x" << reinterpret_cast<std::uintptr_t>(g_last_nadine_object)
             << std::dec << " count=" << count_before << " capacity=" << capacity;
        log_line(line.str().c_str());
    }
    if (count_before >= capacity) {
        log_line("AddCharacter aborted: count >= capacity");
        return;
    }
    add_character(manager, g_last_nadine_object);
    const auto count_after = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(manager) + 0xC);
    std::ostringstream line;
    line << "AddCharacter post: count=" << count_after;
    log_line(line.str().c_str());
}

// Resolve the live Player object for retained slot `index` from
// g_RetainedPlayerSlotTable: entry {ptr,id} at table+index*0x10; player object =
// *(entry.ptr + 8). Returns nullptr if the slot is empty.
extern volatile int g_drive_slot_index;

void* retained_player_object(int index) {
    if (g_module == 0) {
        return nullptr;
    }
    auto* table = reinterpret_cast<std::uint8_t*>(g_module + kRetainedSlotTableRva);
    void* entry_ptr = *reinterpret_cast<void**>(table + static_cast<std::size_t>(index) * 0x10);
    if (entry_ptr == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<void**>(static_cast<std::byte*>(entry_ptr) + 8);
}

// Swap retained-player slots 0 and 1 (each a 16-byte {ptr,id,pad} entry). After
// this, local-player-0 resolves to what was slot 1 (our spawned Player), so the
// camera + input follow it; the original Chloe (now slot 1) stays alive/intact.
// Idempotent (swapping again restores). Requires slot 1 to be populated (F12).
void swap_local_player_slots() {
    if (g_module == 0) {
        return;
    }
    auto* table = reinterpret_cast<std::uint8_t*>(g_module + kRetainedSlotTableRva);
    void* s0 = *reinterpret_cast<void**>(table);
    void* s1 = *reinterpret_cast<void**>(table + 0x10);
    if (s0 == nullptr || s1 == nullptr) {
        log_line("slot swap skipped: slot0 or slot1 empty (spawn slot1 with F12 first)");
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(table, 0x20, PAGE_READWRITE, &old)) {
        log_line("slot swap: VirtualProtect failed");
        return;
    }
    std::uint8_t tmp[0x10];
    std::memcpy(tmp, table, 0x10);
    std::memcpy(table, table + 0x10, 0x10);
    std::memcpy(table + 0x10, tmp, 0x10);
    DWORD ignored = 0;
    VirtualProtect(table, 0x20, old, &ignored);
    std::ostringstream line;
    line << "local-player slots SWAPPED: new slot0 obj=0x" << std::hex
         << reinterpret_cast<std::uintptr_t>(retained_player_object(0))
         << " slot1 obj=0x" << reinterpret_cast<std::uintptr_t>(retained_player_object(1))
         << std::dec;
    log_line(line.str().c_str());
}

void toggle_main_camera_focus() {
    // Disabled for now: both attempted camera paths broke control semantics:
    // 1) retained-slot swap moves the camera, but changes which actor slot 0/1
    //    means and kills the working clone-control path;
    // 2) direct CameraManager(0) attach leaves game control on Chloe and makes
    //    the camera/control relationship inconsistent.
    // Keep the proven state intact: clone stays slot 1 and receives input.
    g_drive_slot_index = 1;
    log_line("camera focus toggle disabled: preserving working clone control");
    log_line("drive target slot=1");
}

// Scan the game heap (a window around the player objects) for 8-byte pointers
// equal to the two retained-player object addresses, logging absolute slot
// addresses. The movement system keeps its own cached pointer to the controlled
// Player (separate from the slot table the camera uses); this finds candidate
// holders so we can repoint movement to the clone. Walks committed readable
// regions via VirtualQuery so it never faults.
void scan_for_player_pointers() {
    void* p0 = retained_player_object(0);
    void* p1 = retained_player_object(1);
    if (p0 == nullptr || p1 == nullptr) {
        log_line("ptr scan: need both slots populated");
        return;
    }
    const auto a0 = reinterpret_cast<std::uintptr_t>(p0);
    const auto a1 = reinterpret_cast<std::uintptr_t>(p1);
    const std::uintptr_t lo = (a0 < a1 ? a0 : a1) - 0x4000000;
    const std::uintptr_t hi = (a0 > a1 ? a0 : a1) + 0x4000000;
    std::ostringstream line;
    line << "ptr scan p0=0x" << std::hex << a0 << " p1=0x" << a1 << " holders:";
    int hits = 0;
    std::uintptr_t addr = lo;
    while (addr < hi && hits < 80) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0) {
            break;
        }
        const auto region_base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t region_end = region_base + mbi.RegionSize;
        const DWORD prot = mbi.Protect & 0xFF;
        const bool readable =
            mbi.State == MEM_COMMIT &&
            (prot == PAGE_READWRITE || prot == PAGE_READONLY ||
             prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
             prot == PAGE_WRITECOPY) &&
            (mbi.Protect & PAGE_GUARD) == 0;
        if (readable) {
            std::uintptr_t q = region_base < lo ? lo : region_base;
            q &= ~static_cast<std::uintptr_t>(7);
            for (; q + 8 <= region_end && q < hi && hits < 80; q += 8) {
                const void* v = *reinterpret_cast<void**>(q);
                if (v == p0 || v == p1) {
                    line << " 0x" << std::hex << q << (v == p0 ? "=p0" : "=p1");
                    ++hits;
                }
            }
        }
        addr = region_end;
    }
    line << std::dec << " (" << hits << ")";
    log_line(line.str().c_str());
}

std::size_t readable_bytes(const void* p);  // defined below

const char* look_probe_hash_name(std::uint32_t value, const LookProbeHashes& h) {
    if (value == h.chloe_base) return "chloe-base";
    if (value == h.coop_chloe) return "coop-chloe";
    if (value == h.nadine_base) return "nadine-base";
    if (value == h.chloe) return "chloe";
    if (value == h.nadine) return "nadine";
    if (value == h.player) return "player";
    return nullptr;
}

bool is_probable_user_pointer(std::uintptr_t p) {
    return p >= 0x10000 && p < 0x0000800000000000ULL;
}

void scan_spawn_text_region(const char* label, std::uintptr_t base,
                            std::size_t requested_size, int& hits) {
    if (hits >= 48 || base == 0) {
        return;
    }
    const std::size_t avail = readable_bytes(reinterpret_cast<void*>(base));
    if (avail == 0) {
        return;
    }
    const std::size_t size = avail < requested_size ? avail : requested_size;
    const char* names[] = {"chloe-base", "coop-chloe", "nadine-base", "chloe", "nadine"};
    const auto* bytes = reinterpret_cast<const char*>(base);
    for (const char* name : names) {
        const std::size_t len = std::strlen(name);
        if (len == 0 || size < len) {
            continue;
        }
        for (std::size_t off = 0; off + len <= size && hits < 48; ++off) {
            if (std::memcmp(bytes + off, name, len) != 0) {
                continue;
            }
            std::ostringstream line;
            line << "SPAWN look-source text " << label
                 << "+0x" << std::hex << off
                 << " = " << name << std::dec;
            log_line(line.str().c_str());
            ++hits;
        }
    }
}

void scan_spawn_hash_region(const char* label, std::uintptr_t base,
                            std::size_t requested_size, const LookProbeHashes& h,
                            int& hits) {
    if (hits >= 48 || base == 0) {
        return;
    }
    const std::size_t avail = readable_bytes(reinterpret_cast<void*>(base));
    if (avail < sizeof(std::uint32_t)) {
        return;
    }
    const std::size_t size = avail < requested_size ? avail : requested_size;
    for (std::size_t off = 0; off + sizeof(std::uint32_t) <= size && hits < 48; off += 4) {
        const auto* p = reinterpret_cast<const std::uint32_t*>(base + off);
        const std::uint32_t value = *p;
        const char* name = look_probe_hash_name(value, h);
        if (name == nullptr) {
            continue;
        }
        std::ostringstream line;
        line << "SPAWN look-source hash " << label
             << "+0x" << std::hex << off
             << " = " << name << "(0x" << value << ")" << std::dec;
        log_line(line.str().c_str());
        ++hits;
    }
    scan_spawn_text_region(label, base, size, hits);
}

void scan_spawn_pointer_region(const char* owner, std::uintptr_t base,
                               std::size_t requested_size, const LookProbeHashes& h,
                               int& hits) {
    if (hits >= 48 || base == 0) {
        return;
    }
    const std::size_t avail = readable_bytes(reinterpret_cast<void*>(base));
    if (avail < sizeof(std::uintptr_t)) {
        return;
    }
    const std::size_t size = avail < requested_size ? avail : requested_size;
    int scanned_targets = 0;
    for (std::size_t off = 0; off + sizeof(std::uintptr_t) <= size &&
                            hits < 48 && scanned_targets < 96; off += 8) {
        const std::uintptr_t target = *reinterpret_cast<const std::uintptr_t*>(base + off);
        if (!is_probable_user_pointer(target) ||
            readable_bytes(reinterpret_cast<void*>(target)) < sizeof(std::uint32_t)) {
            continue;
        }
        ++scanned_targets;
        const int before = hits;
        std::ostringstream target_label;
        target_label << owner << ".ptr+0x" << std::hex << off
                     << "->0x" << target << std::dec;
        const std::string label = target_label.str();
        scan_spawn_hash_region(label.c_str(), target, 0x400, h, hits);
        if (hits != before) {
            std::ostringstream line;
            line << "SPAWN look-source ptr-owner " << owner
                 << "+0x" << std::hex << off
                 << " -> 0x" << target << std::dec;
            log_line(line.str().c_str());
        }
    }
}

void log_spawn_look_source_probe(std::uintptr_t desc, std::uintptr_t ctx,
                                 std::uintptr_t a4, std::uintptr_t out,
                                 std::uintptr_t rsp) {
    if (g_module == 0) {
        return;
    }
    const auto string_hash =
        reinterpret_cast<StringHashFunction>(g_module + kStringHashRva);
    LookProbeHashes h{};
    h.chloe_base = string_hash("chloe-base", 10, 0);
    h.coop_chloe = string_hash("coop-chloe", 10, 0);
    h.nadine_base = string_hash("nadine-base", 11, 0);
    h.chloe = string_hash("chloe", 5, 0);
    h.nadine = string_hash("nadine", 6, 0);
    h.player = string_hash("player", 6, 0);

    {
        std::ostringstream line;
        line << "SPAWN look-source probe hashes: chloe-base=0x" << std::hex << h.chloe_base
             << " coop-chloe=0x" << h.coop_chloe
             << " nadine-base=0x" << h.nadine_base
             << " chloe=0x" << h.chloe
             << " nadine=0x" << h.nadine
             << " player=0x" << h.player << std::dec;
        log_line(line.str().c_str());
    }

    int hits = 0;
    scan_spawn_hash_region("captured_desc_copy", reinterpret_cast<std::uintptr_t>(g_spawn_desc),
                           sizeof(g_spawn_desc), h, hits);
    scan_spawn_hash_region("live_desc", desc, 0x800, h, hits);
    scan_spawn_hash_region("ctx", ctx, 0x1000, h, hits);
    scan_spawn_hash_region("a4", a4, 0x800, h, hits);
    scan_spawn_hash_region("out", out, 0x800, h, hits);
    scan_spawn_hash_region("stack", rsp, 0x100, h, hits);

    scan_spawn_pointer_region("live_desc", desc, 0x400, h, hits);
    scan_spawn_pointer_region("ctx", ctx, 0x800, h, hits);
    scan_spawn_pointer_region("a4", a4, 0x400, h, hits);
    scan_spawn_pointer_region("out", out, 0x400, h, hits);
    scan_spawn_pointer_region("stack", rsp, 0x100, h, hits);

    std::ostringstream done;
    done << "SPAWN look-source probe done: hits=" << hits;
    log_line(done.str().c_str());
}

// The currently-focused input consumer (the object that handles input each
// frame; during gameplay it drives the controlled player). Stable module global.
constexpr std::uintptr_t kFocusConsumerGlobalRva = 0x6B2C618;

// Dump the focused input consumer: its class vtable RVA (RTTI offline) and any
// field holding the controlled-player pointer (the control ref to repoint).
void dump_focus_consumer() {
    // Per-player input manager vtable + per-slot apply (vtable+0x20) RVA — logged
    // first so it runs even when the focus consumer is null (gameplay).
    void* mgr = *reinterpret_cast<void**>(g_module + 0x3FE1088);
    if (mgr != nullptr && readable_bytes(mgr) >= 8) {
        const auto vt = *reinterpret_cast<std::uintptr_t*>(mgr);
        std::ostringstream m;
        m << "inputmgr=0x" << std::hex << reinterpret_cast<std::uintptr_t>(mgr)
          << " vtbl_rva=0x" << (vt > g_module ? vt - g_module : 0);
        if (vt > g_module && readable_bytes(reinterpret_cast<void*>(vt)) >= 0x28) {
            const auto apply = *reinterpret_cast<std::uintptr_t*>(vt + 0x20);
            m << " perslot_apply_rva=0x" << (apply > g_module ? apply - g_module : 0);
        }
        m << std::dec;
        log_line(m.str().c_str());
        // Scan per-slot structs (FrameState + 0xDB0 + 0xBF0*slot) for several
        // candidate Chloe references to find the slot->player binding: the
        // player object, its retained handle pair (chloe+0xA8 ptr / +0xBC id),
        // and the retained-table slot0 handle.
        void* chloe = retained_player_object(0);
        void* chloe_handle = nullptr;
        std::uint32_t chloe_id = 0;
        if (chloe != nullptr && readable_bytes(chloe) >= 0xC0) {
            chloe_handle = *reinterpret_cast<void**>(static_cast<std::byte*>(chloe) + 0xA8);
            chloe_id = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(chloe) + 0xBC);
        }
        void* table_h0 = *reinterpret_cast<void**>(g_module + kRetainedSlotTableRva);
        {
            std::ostringstream c;
            c << "scan candidates: chloe=0x" << std::hex << reinterpret_cast<std::uintptr_t>(chloe)
              << " handle=0x" << reinterpret_cast<std::uintptr_t>(chloe_handle)
              << " id=0x" << chloe_id
              << " tableH0=0x" << reinterpret_cast<std::uintptr_t>(table_h0) << std::dec;
            log_line(c.str().c_str());
        }
        for (int s = 0; s < 2; ++s) {
            auto* slotstruct = static_cast<std::byte*>(mgr) + 0xDB0 + 0xBF0 * s;
            if (readable_bytes(slotstruct) < 0xBF0) {
                continue;
            }
            std::ostringstream sl;
            sl << "frameslot[" << s << "]=0x" << std::hex
               << reinterpret_cast<std::uintptr_t>(slotstruct) << " hits:";
            for (std::size_t off = 0; off < 0xBF0; off += 4) {
                if (off + 8 <= 0xBF0) {
                    void* v = *reinterpret_cast<void**>(slotstruct + off);
                    if (v == chloe) sl << " +0x" << off << "=obj";
                    else if (chloe_handle && v == chloe_handle) sl << " +0x" << off << "=handle";
                    else if (table_h0 && v == table_h0) sl << " +0x" << off << "=tblH0";
                }
                if (chloe_id && *reinterpret_cast<std::uint32_t*>(slotstruct + off) == chloe_id) {
                    sl << " +0x" << off << "=id";
                }
            }
            sl << std::dec;
            log_line(sl.str().c_str());
        }
    } else {
        log_line("inputmgr null/unreadable");
    }

    // Raw float dump of input block[0] (active) and block[1] (clone/slot1) to
    // find the move fields and what drives the slot-1 clone in a circle.
    for (int b = 0; b < 2; ++b) {
        auto* blk = reinterpret_cast<float*>(g_module + 0x66E4220 + 0x530 * b);
        if (readable_bytes(blk) < 0x100) {
            continue;
        }
        std::ostringstream bl;
        bl << "block[" << b << "]:";
        for (int i = 0; i < 24; ++i) {
            const float v = blk[i];
            if (v != 0.0F) {
                bl << " +0x" << std::hex << (i * 4) << std::dec << "=" << v;
            }
        }
        log_line(bl.str().c_str());
    }

    auto* slot = reinterpret_cast<void**>(g_module + kFocusConsumerGlobalRva);
    void* consumer = *slot;
    if (consumer == nullptr || readable_bytes(consumer) < 8) {
        log_line("focus consumer: null/unreadable");
        return;
    }
    const auto vt = *reinterpret_cast<std::uintptr_t*>(consumer);
    void* ctrl = retained_player_object(0);
    void* ctrl1 = retained_player_object(1);
    std::ostringstream line;
    line << "focus consumer=0x" << std::hex << reinterpret_cast<std::uintptr_t>(consumer)
         << " vtbl_rva=0x" << (vt > g_module ? vt - g_module : 0)
         << " ctrl0=0x" << reinterpret_cast<std::uintptr_t>(ctrl)
         << " ctrl1=0x" << reinterpret_cast<std::uintptr_t>(ctrl1) << std::dec
         << " player-ref hits:";
    for (std::size_t off = 0x8; off <= 0x600; off += 8) {
        void* v = *reinterpret_cast<void**>(static_cast<std::byte*>(consumer) + off);
        if (v == ctrl) line << " +0x" << std::hex << off << "=p0" << std::dec;
        else if (v == ctrl1) line << " +0x" << std::hex << off << "=p1" << std::dec;
    }
    log_line(line.str().c_str());
}

// Dump aligned pointer fields of a Player slot and the Nadine object, side by
// side. Differing pairs are candidate per-character references (model /
// skeleton / character-def) we may swap to make a Player look like Nadine.
void dump_character_fields_for_slot(int slot, const char* label) {
    void* chloe = retained_player_object(slot);
    void* nadine = g_last_nadine_object;
    if (chloe == nullptr || nadine == nullptr) {
        std::ostringstream line;
        line << "char dump: need " << label << " + Nadine";
        log_line(line.str().c_str());
        return;
    }
    auto* cb = static_cast<std::byte*>(chloe);
    auto* nb = static_cast<std::byte*>(nadine);
    std::ostringstream hdr;
    hdr << "char dump " << label << "=0x" << std::hex << reinterpret_cast<std::uintptr_t>(chloe)
        << " nadine=0x" << reinterpret_cast<std::uintptr_t>(nadine) << std::dec;
    log_line(hdr.str().c_str());
    auto modvt = [&](void* obj) -> std::uintptr_t {
        if (obj == nullptr || readable_bytes(obj) < 8) {
            return 0;
        }
        const auto vt = *reinterpret_cast<std::uintptr_t*>(obj);
        if (vt > g_module && vt < g_module + 0x8000000 && readable_bytes(reinterpret_cast<void*>(vt)) >= 8) {
            return vt - g_module;  // module-relative vtable RVA
        }
        return 0;
    };
    // Enumerate ALL sub-component pointer fields (those pointing to an object
    // with a module vtable) for both Chloe and Nadine, logging the class vtable
    // RVAs so every component can be RTTI-resolved offline to find the visual /
    // skeleton / animation subsystem to swap.
    int logged = 0;
    for (std::size_t off = 0x8; off <= 0x5000 && logged < 260; off += 8) {
        void* cv = *reinterpret_cast<void**>(cb + off);
        void* nv = *reinterpret_cast<void**>(nb + off);
        const std::uintptr_t crva = modvt(cv);
        const std::uintptr_t nrva = modvt(nv);
        if (crva == 0 && nrva == 0) {
            continue;
        }
        std::ostringstream line;
        line << "  +0x" << std::hex << off
             << " c=0x" << reinterpret_cast<std::uintptr_t>(cv)
             << " n=0x" << reinterpret_cast<std::uintptr_t>(nv)
             << " c_rva=0x" << crva << " n_rva=0x" << nrva
             << (crva != 0 && crva == nrva ? " SAME" : "") << std::dec;
        log_line(line.str().c_str());
        ++logged;
    }
    std::ostringstream done;
    done << "char dump " << label << " done (" << logged << " component fields)";
    log_line(done.str().c_str());
}

void dump_character_fields() {
    dump_character_fields_for_slot(0, "slot0");
    dump_character_fields_for_slot(1, "slot1");
}

// Guarded dump of the captured input-state neighborhood: scan dst +/-0x800 for
// the controlled-player pointer (so we can find the control's player reference).
// Runs on the background thread with readable_bytes guards (never in the VEH).
void dump_inputstate_neighborhood() {
    const std::uintptr_t dst = g_inputstate_addr;
    if (dst == 0) {
        return;
    }
    void* ctrl = retained_player_object(0);
    std::ostringstream line;
    line << "input-state dump dst=0x" << std::hex << dst
         << " ctrl_player=0x" << reinterpret_cast<std::uintptr_t>(ctrl)
         << std::dec << " hits:";
    const std::uintptr_t lo = dst - 0x800;
    const std::uintptr_t hi = dst + 0x800;
    for (std::uintptr_t q = lo; q <= hi; q += 8) {
        if (readable_bytes(reinterpret_cast<void*>(q)) < 8) {
            continue;
        }
        void* v = *reinterpret_cast<void**>(q);
        if (v == ctrl) {
            line << " dst" << (q >= dst ? "+0x" : "-0x") << std::hex
                 << (q >= dst ? q - dst : dst - q) << std::dec;
        }
    }
    log_line(line.str().c_str());
}

// Swap characters 0 and 1 in the real CharacterManager (*(0x143898630)).
// Layout (from GetCharacter 0x141D63C50): array = *(mgr+0); count = *(mgr+8);
// 16-byte {ptr,id} entries; character = *(entry.ptr+8), valid if char+0xBC==id.
// Chloe=char0, Nadine=char1. If gameplay control resolves the player via
// GetCharacter(0), this redirects control. Idempotent (swap again to restore).
constexpr std::uintptr_t kCharMgrRealGlobalRva = 0x3898630;

void swap_character_manager_0_1() {
    if (g_module == 0) {
        return;
    }
    void* mgr = *reinterpret_cast<void**>(g_module + kCharMgrRealGlobalRva);
    if (mgr == nullptr || readable_bytes(mgr) < 0x10) {
        log_line("charmgr swap: manager null/unreadable");
        return;
    }
    const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(mgr) + 8);
    auto* arr = *reinterpret_cast<std::uint8_t**>(mgr);
    if (count < 2 || arr == nullptr || readable_bytes(arr) < 0x20) {
        std::ostringstream line;
        line << "charmgr swap aborted: count=" << count
             << " arr=0x" << std::hex << reinterpret_cast<std::uintptr_t>(arr) << std::dec;
        log_line(line.str().c_str());
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(arr, 0x20, PAGE_READWRITE, &old)) {
        log_line("charmgr swap: VirtualProtect failed");
        return;
    }
    std::uint8_t tmp[0x10];
    std::memcpy(tmp, arr, 0x10);
    std::memcpy(arr, arr + 0x10, 0x10);
    std::memcpy(arr + 0x10, tmp, 0x10);
    DWORD ignored = 0;
    VirtualProtect(arr, 0x20, old, &ignored);
    std::ostringstream line;
    line << "charmgr swap done: mgr=0x" << std::hex << reinterpret_cast<std::uintptr_t>(mgr)
         << " arr=0x" << reinterpret_cast<std::uintptr_t>(arr) << std::dec
         << " count=" << count;
    log_line(line.str().c_str());
}

DWORD WINAPI state_recv_thread(void*) {
    // One buffer big enough for the largest packet (EnemyPacket); dispatch by magic.
    alignas(8) char buf[sizeof(coop::EnemyPacket) + 64];
    while (g_state_net.running.load()) {
        const int received = recvfrom(g_state_net.recv_socket, buf, sizeof(buf), 0,
                                      nullptr, nullptr);
        if (received == SOCKET_ERROR) {
            if (!g_state_net.running.load()) break;
            continue;
        }
        if (received < static_cast<int>(sizeof(std::uint32_t))) continue;
        std::uint32_t magic;
        std::memcpy(&magic, buf, sizeof(magic));
        if (magic == coop::kStatePacketMagic &&
            received == static_cast<int>(sizeof(coop::StatePacket))) {
            coop::StatePacket packet{};
            std::memcpy(&packet, buf, sizeof(packet));
            if (packet.version == coop::kStatePacketVersion &&
                valid_network_position(packet.pos)) {
                publish_peer_state_snapshot(packet);
            }
        } else if (magic == coop::kEnemyPacketMagic &&
                   received >= static_cast<int>(offsetof(coop::EnemyPacket, enemies)) &&
                   received <= static_cast<int>(sizeof(coop::EnemyPacket))) {
            coop::EnemyPacket ep{};
            std::memcpy(&ep, buf, static_cast<std::size_t>(received));
            if (ep.version == coop::kEnemyPacketVersion &&
                ep.count <= coop::kMaxEnemies) {
                publish_peer_enemies_snapshot(ep);
            }
        } else if (magic == coop::kClientDamageMagic &&
                   received == static_cast<int>(sizeof(coop::ClientDamagePacket))) {
            coop::ClientDamagePacket dp{};
            std::memcpy(&dp, buf, sizeof(dp));
            std::uint32_t head = g_client_dmg_head.load(std::memory_order_relaxed);
            g_client_dmg_ring[head % 64] = {dp.name_hash, dp.damage};
            g_client_dmg_head.store(head + 1, std::memory_order_release);
        }
    }
    return 0;
}

std::string read_peer_ip() {
    std::ifstream in(R"(C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\coop_peer.txt)");
    std::string ip;
    if (in && std::getline(in, ip)) {
        while (!ip.empty() && (ip.back() == '\r' || ip.back() == '\n' || ip.back() == ' ')) {
            ip.pop_back();
        }
    }
    if (ip.empty()) {
        ip = "127.0.0.1";
    }
    return ip;
}

bool start_state_net() {
    const std::string peer_ip = read_peer_ip();
    g_state_net.recv_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    g_state_net.send_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_state_net.recv_socket == INVALID_SOCKET || g_state_net.send_socket == INVALID_SOCKET) {
        log_line("state net: socket creation failed");
        return false;
    }
    // Auto-role for TWO INSTANCES ON ONE PC: first instance binds kStatePort
    // (31338) and sends to kStatePort+1; the second finds 31338 taken, binds
    // 31338+1 and sends to 31338. On TWO separate PCs the peer IP is not
    // loopback, both bind 31338 with no collision and talk port-to-port.
    const bool loopback = (peer_ip == "127.0.0.1" || peer_ip == "localhost");
    g_loopback_two_instance = loopback;
    std::uint16_t my_port = coop::kStatePort;
    std::uint16_t peer_port = coop::kStatePort;
    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(my_port);
    if (bind(g_state_net.recv_socket, reinterpret_cast<sockaddr*>(&bind_addr),
             sizeof(bind_addr)) == SOCKET_ERROR) {
        if (!loopback) {
            std::ostringstream line;
            line << "state net: bind failed " << WSAGetLastError();
            log_line(line.str().c_str());
            return false;
        }
        // Loopback second instance: 31338 taken -> become the "B" peer.
        my_port = coop::kStatePort + 1;
        peer_port = coop::kStatePort;
        bind_addr.sin_port = htons(my_port);
        if (bind(g_state_net.recv_socket, reinterpret_cast<sockaddr*>(&bind_addr),
                 sizeof(bind_addr)) == SOCKET_ERROR) {
            std::ostringstream line;
            line << "state net: bind failed (both ports) " << WSAGetLastError();
            log_line(line.str().c_str());
            return false;
        }
    } else if (loopback) {
        // Loopback first instance ("A"): send to the B port.
        peer_port = coop::kStatePort + 1;
    }
    // ROLE assignment (cosmetic): role B = "player 2" machine (local Nadine), role A
    // = "player 1" (local Chloe). Authoritative source = reports/coop_role.txt ("1"
    // or "2"), which works for REAL two-PC play (both bind 31338 there, so port can't
    // distinguish). Fallback for loopback two-instance = the bound port (B = 31339).
    {
        std::ifstream rin(R"(C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\coop_role.txt)");
        std::string role;
        if (rin && std::getline(rin, role)) {
            while (!role.empty() && (role.back() == '\r' || role.back() == '\n' ||
                                     role.back() == ' ')) {
                role.pop_back();
            }
        }
        if (role == "2") {
            g_net_role_b = true;
        } else if (role == "1") {
            g_net_role_b = false;
        } else {
            g_net_role_b = (my_port == coop::kStatePort + 1);  // loopback B instance
        }
        g_local_make_nadine = g_net_role_b;  // player-2 machine renders local as Nadine
        std::ostringstream rl;
        rl << "co-op role = " << (g_net_role_b ? "B (player 2, local Nadine)"
                                               : "A (player 1, local Chloe)");
        log_line(rl.str().c_str());
    }
    g_state_net.peer_addr.sin_family = AF_INET;
    g_state_net.peer_addr.sin_port = htons(peer_port);
    if (InetPtonA(AF_INET, peer_ip.c_str(), &g_state_net.peer_addr.sin_addr) == 1) {
        g_state_net.peer_set = true;
    }
    g_state_net.running.store(true);
    g_state_net.thread = CreateThread(nullptr, 0, state_recv_thread, nullptr, 0, nullptr);
    g_state_net.enabled = true;
    std::ostringstream line;
    line << "state net started: recv :" << my_port << " -> peer " << peer_ip
         << ":" << peer_port << (g_state_net.peer_set ? "" : " (invalid peer ip)");
    log_line(line.str().c_str());
    return true;
}

void send_local_player_state() {
    if (!g_state_net.enabled || !g_state_net.peer_set) {
        return;
    }
    void* local_obj = retained_player_object(0);
    if (local_obj == nullptr) {
        return;
    }
    const auto* pos = reinterpret_cast<const float*>(static_cast<std::byte*>(local_obj) + kWorldPositionOffset);
    const auto* quat = reinterpret_cast<const float*>(static_cast<std::byte*>(local_obj) + kPlayerQuatOffset);
    if (!valid_network_position(pos)) {
        return;
    }
    coop::StatePacket pkt{};
    pkt.sequence = ++g_state_net.send_seq;
    pkt.timestamp_ms = static_cast<std::uint32_t>(GetTickCount64());
    pkt.flags = 1;
    std::memcpy(pkt.pos, pos, sizeof(pkt.pos));
    std::memcpy(pkt.quat, quat, sizeof(pkt.quat));
    // Player+0x260 reads 0 through the retained proxy (it is an anim OUTPUT, not a
    // live field on this object), so derive world velocity from the POSITION DELTA.
    // This is the signal the remote clone's locomotion uses to blend walk/run.
    {
        static float pvx = 0, pvy = 0, pvz = 0;
        static float cvx = 0, cvy = 0, cvz = 0;
        static ULONGLONG pvt = 0;
        const ULONGLONG vnow = GetTickCount64();
        if (pvt == 0) {
            pvx = pos[0]; pvy = pos[1]; pvz = pos[2]; pvt = vnow;
        } else if (vnow - pvt >= 16) {
            const float dt = static_cast<float>(vnow - pvt) / 1000.0F;
            if (dt > 0.0F && dt < 0.5F) {
                cvx = (pos[0] - pvx) / dt;
                cvy = (pos[1] - pvy) / dt;
                cvz = (pos[2] - pvz) / dt;
            }
            pvx = pos[0]; pvy = pos[1]; pvz = pos[2]; pvt = vnow;
        }
        pkt.velocity[0] = cvx; pkt.velocity[1] = cvy; pkt.velocity[2] = cvz;
    }
    if (readable_bytes(static_cast<std::byte*>(local_obj) +
                       kPlayerHealthSystemOffset + sizeof(void*)) >= sizeof(void*)) {
        auto* health = *reinterpret_cast<std::byte**>(
            static_cast<std::byte*>(local_obj) + kPlayerHealthSystemOffset);
        if (health != nullptr &&
            readable_bytes(health + kPlayerMaxHealthOffset + sizeof(std::int32_t)) >=
                sizeof(std::int32_t)) {
            const auto hp = *reinterpret_cast<const std::int32_t*>(
                health + kPlayerCurrentHealthOffset);
            const auto max_hp = *reinterpret_cast<const std::int32_t*>(
                health + kPlayerMaxHealthOffset);
            if (hp >= 0 && max_hp > 0 && hp <= max_hp && max_hp <= 100000) {
                pkt.health = hp;
                pkt.max_health = max_hp;
                pkt.flags |= 2;
            }
        }
    }
    // Player slot 16 publishes the stable movement result after resolving the
    // Joypad and camera basis. Send both the raw pair and the resulting world
    // vector so the receiver does not reinterpret movement through screen 1.
    if (readable_bytes(static_cast<std::byte*>(local_obj) + 0x3A40) >= 1) {
        const auto* base = static_cast<const std::byte*>(local_obj);
        std::memcpy(pkt.move_stick, base + 0x39C8, sizeof(pkt.move_stick));
        std::memcpy(pkt.move_world, base + 0x3A30, sizeof(pkt.move_world));
        pkt.move_magnitude = *reinterpret_cast<const float*>(base + 0x3A1C);
        pkt.flags |= 4;
        if (!valid_network_movement(pkt)) {
            pkt.flags &= static_cast<std::uint16_t>(~4u);
            std::memset(pkt.move_stick, 0, sizeof(pkt.move_stick));
            std::memset(pkt.move_world, 0, sizeof(pkt.move_world));
            pkt.move_magnitude = 0.0F;
        }
    }
    // Input_ScaleCopyVirtual normalizes keyboard/gamepad input into the local
    // Player's Joypad masks at +0xD0/+0xD8/+0xE0. Send that native boundary; the
    // receiver feeds the held mask back through its own Input_ScaleCopyVirtual so
    // command edges and hold durations are derived by the engine.
    if (readable_bytes(static_cast<std::byte*>(local_obj) + 0x39BC) >= 1) {
        const auto* const base = static_cast<const std::byte*>(local_obj);
        const int joypad_index =
            *reinterpret_cast<const std::int32_t*>(base + 0x39B8);
        void* const frame_state =
            *reinterpret_cast<void**>(g_module + 0x3FE1088);
        if (frame_state != nullptr && joypad_index >= 0 && joypad_index < 6) {
            const auto* const joypad = static_cast<const std::byte*>(frame_state) +
                                       0xDB0 + 0xBF0 * joypad_index;
            if (readable_bytes(joypad) >= 0xE8) {
                pkt.buttons_pressed =
                    *reinterpret_cast<const std::uint64_t*>(joypad + 0xD0);
                pkt.buttons_released =
                    *reinterpret_cast<const std::uint64_t*>(joypad + 0xD8);
                pkt.input_buttons =
                    *reinterpret_cast<const std::uint64_t*>(joypad + 0xE0);
                pkt.flags |= 8;
            }
        }
    }
    sendto(g_state_net.send_socket, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
           reinterpret_cast<const sockaddr*>(&g_state_net.peer_addr), sizeof(g_state_net.peer_addr));
}

// Returns the latest peer state with the loopback offset applied, or false if
// there is nothing to apply / no puppet yet.
// Master switch for forcing networked transform/velocity onto the puppet.
// When OFF, the puppet is left as a pure second local player (no transform/
// velocity override) so we can observe its native input-driven locomotion.
bool g_puppet_net_apply = false;
// DIAGNOSTIC (toggle 'P'): when true, apply_puppet_transform_post does NOT write the
// clone's position/facing each frame — the clone runs FREE from its slot-1 input so we
// can test whether locomotion animation needs actual translation (the soft-lerp pin may
// be cancelling it). Only a large-divergence snap remains so it can't drift forever.
volatile bool g_clone_freerun = true;
volatile bool g_drive_screen1_cam = false;  // NUMPAD6: drive screen-1 cam + fwd input test
// g_play_as_clone defined earlier (before hooked_resolve_local_player)
// Single-instance test: when there is no network peer, mirror the LOCAL Chloe
// onto the puppet (+2m) so actions/transform can be verified on one PC.
bool g_puppet_local_mirror = false;

bool latest_peer_state(coop::StatePacket& out, void*& puppet) {
    // Both copies must keep applying their peer while the other window has focus.
    // Foreground ownership is relevant only to local diagnostic hotkeys.
    if (!g_puppet_net_apply || g_module == 0) {
        return false;
    }
    puppet = retained_player_object(1);
    if (puppet == nullptr) {
        return false;
    }
    if (g_state_net.enabled && g_state_net.has_peer_state.load(std::memory_order_acquire)) {
        if (!copy_peer_state_snapshot(out)) {
            return false;
        }
        out.pos[0] += g_state_loopback_offset_x;  // 0 for real two-instance play
        return true;
    }
    // No peer: single-instance self-mirror for testing (Chloe +2m + her buttons).
    if (!g_puppet_local_mirror) {
        return false;
    }
    void* chloe = retained_player_object(0);
    if (chloe == nullptr || readable_bytes(static_cast<std::byte*>(chloe) + 0x260) < 4) {
        return false;
    }
    auto* base = static_cast<std::byte*>(chloe);
    std::memcpy(out.pos, base + kWorldPositionOffset, sizeof(out.pos));
    std::memcpy(out.quat, base + kPlayerQuatOffset, sizeof(out.quat));
    // Velocity from POSITION DELTA (the +0x260 field reads 0 through the retained
    // proxy). Reliable world-space velocity of the peer (local Chloe here).
    {
        const auto* p = reinterpret_cast<const float*>(base + kWorldPositionOffset);
        static float prevx = 0, prevy = 0, prevz = 0;
        static float cvx = 0, cvy = 0, cvz = 0;  // cached velocity
        static ULONGLONG prevt = 0;
        const ULONGLONG now = GetTickCount64();
        if (prevt == 0) { prevx = p[0]; prevy = p[1]; prevz = p[2]; prevt = now; }
        else if (now - prevt >= 16) {  // recompute ~60Hz (called many times/frame)
            const float dt = (now - prevt) / 1000.0F;
            if (dt < 0.5F) {
                cvx = (p[0] - prevx) / dt;
                cvy = (p[1] - prevy) / dt;
                cvz = (p[2] - prevz) / dt;
            }
            prevx = p[0]; prevy = p[1]; prevz = p[2]; prevt = now;
        }
        out.velocity[0] = cvx; out.velocity[1] = cvy; out.velocity[2] = cvz;
    }
    auto* blk0 = reinterpret_cast<std::byte*>(g_module + 0x66E4220);
    if (readable_bytes(blk0) >= 0x528) {
        const auto* axes = reinterpret_cast<const float*>(blk0);
        out.move_stick[0] = axes[0];
        out.move_stick[1] = axes[1];
        out.look_stick[0] = axes[2];
        out.look_stick[1] = axes[3];
        out.input_buttons = *reinterpret_cast<const std::uint64_t*>(blk0 + 1304);
    }
    if (readable_bytes(base + 0x3A40) >= 1) {
        std::memcpy(out.move_stick, base + 0x39C8, sizeof(out.move_stick));
        std::memcpy(out.move_world, base + 0x3A30, sizeof(out.move_world));
        out.move_magnitude = *reinterpret_cast<const float*>(base + 0x3A1C);
        out.flags |= 4;
    }
    out.pos[0] += 2.0F;  // visible beside Chloe
    return true;
}

bool apply_peer_health_to_slot1(const coop::StatePacket& peer, bool log_once) {
    if ((peer.flags & 2) == 0 || peer.health < 0 || peer.max_health <= 0 ||
        peer.health > peer.max_health || peer.max_health > 100000) {
        return false;
    }
    void* puppet = retained_player_object(1);
    if (puppet == nullptr ||
        readable_bytes(static_cast<std::byte*>(puppet) +
                       kPlayerHealthSystemOffset + sizeof(void*)) < sizeof(void*)) {
        return false;
    }
    auto* health = *reinterpret_cast<std::byte**>(
        static_cast<std::byte*>(puppet) + kPlayerHealthSystemOffset);
    if (health == nullptr ||
        readable_bytes(health + kPlayerMaxHealthOffset + sizeof(std::int32_t)) <
            sizeof(std::int32_t)) {
        return false;
    }
    *reinterpret_cast<std::int32_t*>(health + kPlayerCurrentHealthOffset) = peer.health;
    *reinterpret_cast<std::int32_t*>(health + kPlayerMaxHealthOffset) = peer.max_health;
    if (log_once) {
        std::ostringstream line;
        line << "remote Player health seeded " << peer.health << "/" << peer.max_health
             << " from peer seq=" << peer.sequence;
        log_line(line.str().c_str());
    }
    return true;
}

// PRE (before the puppet's own update runs): feed the replicated velocity into
// the puppet's locomotion field so the engine's anim graph samples it this frame
// and plays run/walk. Mirrored at +0x270.
void apply_puppet_velocity_pre() {
    coop::StatePacket pkt{};
    void* puppet = nullptr;
    if (!latest_peer_state(pkt, puppet)) {
        return;
    }
    auto* vel = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kPlayerVelocityOffset);
    auto* vel_mirror = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kPlayerVelocityMirrorOffset);
    std::memcpy(vel, pkt.velocity, sizeof(pkt.velocity));
    std::memcpy(vel_mirror, pkt.velocity, sizeof(pkt.velocity));
}

// POST (after the puppet's own update runs): assert the networked transform last
// so the engine's own integration can't fight it (was causing flicker).
void apply_puppet_transform_post(void* puppet) {
    coop::StatePacket pkt{};
    void* resolved = nullptr;
    // Diagnostic (rate-limited): why isn't the puppet being driven?
    if (false) {
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - last > 1000) {
            last = now;
            void* p1 = retained_player_object(1);
            std::ostringstream d;
            d << "PUPPETDBG obj=0x" << std::hex
              << reinterpret_cast<std::uintptr_t>(puppet) << " slot1=0x"
              << reinterpret_cast<std::uintptr_t>(p1) << std::dec
              << " apply=" << (g_puppet_net_apply ? 1 : 0)
              << " netEn=" << (g_state_net.enabled ? 1 : 0)
              << " hasPeer=" << (g_state_net.has_peer_state.load() ? 1 : 0);
            log_line(d.str().c_str());
        }
    }
    if (!latest_peer_state(pkt, resolved) || resolved != puppet) {
        // A background instance can stop ticking for several seconds. Freeze its
        // avatar at the last confirmed transform instead of releasing it to local
        // physics, which can produce a false fall/death transition.
        if (puppet != retained_player_object(1) ||
            !copy_last_peer_state_snapshot(pkt) || (pkt.flags & 1) == 0) {
            return;
        }
        resolved = puppet;
    }
    apply_peer_health_to_slot1(pkt, false);
    auto* world = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kWorldPositionOffset);
    auto* localp = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kPlayerLocalPosOffset);
    auto* quat = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kPlayerQuatOffset);
    // SOFT position authority (2026-06-30): the clone is driven by the peer's
    // slot-1 input (drive_remote_slot1_actions_tick) so its OWN locomotion graph
    // animates walk/run. We must NOT hard-pin + zero velocity here (that froze the
    // animation — the old "no run animation in this mode"). Instead lerp the world
    // transform toward the authoritative peer position so input-driven motion stays
    // smooth and convergent; HARD-SNAP on large deltas (teleports / scripted moves /
    // first apply) since those aren't reproduced by locomotion. Facing (quat) stays
    // authoritative. Velocity is left to the clone's own locomotion (it is an anim
    // OUTPUT; zeroing it killed the run blend).
    constexpr float kPosLerp = 0.35F;
    constexpr float kSnapDistSq = 4.0F * 4.0F;  // >4m apart => snap, don't lerp
    const float dx = pkt.pos[0] - world[0];
    const float dy = pkt.pos[1] - world[1];
    const float dz = pkt.pos[2] - world[2];
    const bool too_far = dx * dx + dy * dy + dz * dz > kSnapDistSq;
    // Facing is independent from position reconciliation. The old free-run branch
    // returned before this copy, leaving the remote Player free to spin from tiny
    // local correction vectors while its peer stood still.
    if (valid_network_orientation(pkt.quat)) {
        std::memcpy(quat, pkt.quat, sizeof(pkt.quat));
    }
    if (g_clone_freerun) {
        // Let native locomotion own position. A direction-only correction cannot
        // close longitudinal lag while both players already run at magnitude 1,
        // because the corrected vector is normalized back to 1. Add a tiny capped
        // post-step only while the peer genuinely moves. This remains far below
        // the rejected 35%-of-error lerp and is disabled at idle, so it cannot
        // recreate the idle body drag.
        if (too_far) {
            std::memcpy(world, pkt.pos, sizeof(pkt.pos));
            std::memcpy(localp, pkt.pos, sizeof(pkt.pos));
        } else if (valid_network_movement(pkt) && pkt.move_magnitude > 0.01F) {
            constexpr float kNudgeDeadband = 0.12F;
            constexpr float kMaxNudgePerUpdate = 0.015F;
            const float horizontal_sq = dx * dx + dz * dz;
            if (horizontal_sq > kNudgeDeadband * kNudgeDeadband) {
                const float horizontal = std::sqrt(horizontal_sq);
                const float step = horizontal < kMaxNudgePerUpdate
                    ? horizontal
                    : kMaxNudgePerUpdate;
                const float nx = dx / horizontal * step;
                const float nz = dz / horizontal * step;
                world[0] += nx;
                world[2] += nz;
                localp[0] += nx;
                localp[2] += nz;
            }
        }
        static ULONGLONG last_position_log = 0;
        const ULONGLONG position_now = GetTickCount64();
        if (position_now - last_position_log >= 500) {
            last_position_log = position_now;
            std::ostringstream line;
            line << "PUPPET-POS horizontal=" << std::sqrt(dx * dx + dz * dz)
                 << " vertical=" << std::abs(dy)
                 << " peerMove=" << pkt.move_magnitude
                 << " hardSnap=" << (too_far ? 1 : 0);
            log_line(line.str().c_str());
        }
        return;
    }
    if (too_far) {
        std::memcpy(world, pkt.pos, sizeof(pkt.pos));
        std::memcpy(localp, pkt.pos, sizeof(pkt.pos));
    } else {
        for (int i = 0; i < 3; ++i) {
            world[i] += (pkt.pos[i] - world[i]) * kPosLerp;
            localp[i] = world[i];
        }
        world[3] = pkt.pos[3];
        localp[3] = pkt.pos[3];
    }
}

using PlayerDeathEntryFunction = std::int64_t(__fastcall*)(void*);
PlayerDeathEntryFunction g_original_player_death_entries[4]{};

std::int64_t guard_remote_player_death(void* player, int entry) {
    coop::StatePacket peer{};
    const bool remote_clone = player != nullptr && player == retained_player_object(1);
    const bool peer_alive = remote_clone && copy_last_peer_state_snapshot(peer) &&
                            (peer.flags & 2) != 0 && peer.health > 0;
    if (peer_alive) {
        apply_peer_health_to_slot1(peer, false);
        const ULONGLONG received =
            g_state_net.peer_state_received_ms.load(std::memory_order_acquire);
        static std::atomic<ULONGLONG> last_log{0};
        const ULONGLONG now = GetTickCount64();
        ULONGLONG previous = last_log.load(std::memory_order_relaxed);
        if (now - previous >= 1000 &&
            last_log.compare_exchange_strong(
                previous, now, std::memory_order_relaxed)) {
            std::ostringstream line;
            line << "REMOTE-DEATH blocked entry=+0x" << std::hex
                 << (0xBE8 + entry * 8) << std::dec << " peer_hp=" << peer.health
                 << " packet_age_ms=" << (received != 0 ? now - received : 0);
            log_line(line.str().c_str());
        }
        return 0;
    }
    auto original = g_original_player_death_entries[entry];
    return original != nullptr ? original(player) : 0;
}

std::int64_t __fastcall hooked_player_death_be8(void* player) {
    return guard_remote_player_death(player, 0);
}
std::int64_t __fastcall hooked_player_death_bf0(void* player) {
    return guard_remote_player_death(player, 1);
}
std::int64_t __fastcall hooked_player_death_bf8(void* player) {
    return guard_remote_player_death(player, 2);
}
std::int64_t __fastcall hooked_player_death_c00(void* player) {
    return guard_remote_player_death(player, 3);
}

// Diagnostic: find the Player's velocity field. While slot-0 Chloe runs, her
// world position (+0x240) moves each frame; that per-frame delta gives the
// ground-truth horizontal movement direction. Scan the object for a float
// triplet whose direction matches the movement and whose magnitude is in a
// plausible run-speed band, and log the offset(s). Run straight for ~1s and the
// real velocity offset will be the one that recurs across the log lines.
bool g_velprobe_enabled = false;

// How many bytes from `p` are safely readable (committed, non-guard, readable
// protection) in its containing region. 0 if `p` is not readable. Used to clamp
// scans so the background probe never reads off the end of an object into
// unmapped memory (which would crash the process).
std::size_t readable_bytes(const void* p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT) {
        return 0;
    }
    const DWORD prot = mbi.Protect & 0xFF;
    const bool readable =
        prot == PAGE_READONLY || prot == PAGE_READWRITE ||
        prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
        prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_WRITECOPY;
    if (!readable || (mbi.Protect & PAGE_GUARD) != 0) {
        return 0;
    }
    const auto end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return end - reinterpret_cast<std::uintptr_t>(p);
}

void __fastcall hooked_player_fire(
    void* controller,
    void* fire_args,
    void* weapon,
    double scalar
) {
    static std::atomic<unsigned> captured{0};
    const unsigned index = captured.fetch_add(1, std::memory_order_relaxed);
    if (index < 16 && controller != nullptr && fire_args != nullptr &&
        weapon != nullptr && readable_bytes(controller) >= 0x110 &&
        readable_bytes(fire_args) >= 0x100 && readable_bytes(weapon) >= 0x220) {
        void* const player = *reinterpret_cast<void**>(
            static_cast<std::byte*>(controller) + 0x108);
        const std::uint32_t weapon_word_218 = *reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::byte*>(weapon) + 0x218);
        std::uint64_t scalar_bits = 0;
        std::memcpy(&scalar_bits, &scalar, sizeof(scalar_bits));
        const auto* words = reinterpret_cast<const std::uint32_t*>(fire_args);
        std::ostringstream line;
        line << "PLAYER-FIRE #" << index << std::hex
             << " controller=0x" << reinterpret_cast<std::uintptr_t>(controller)
             << " player=0x" << reinterpret_cast<std::uintptr_t>(player)
             << " weapon=0x" << reinterpret_cast<std::uintptr_t>(weapon)
             << " weaponWord218=0x" << weapon_word_218
             << " args=0x" << reinterpret_cast<std::uintptr_t>(fire_args)
             << " scalarBits=0x" << scalar_bits
             << " raw=";
        for (std::size_t i = 0; i < 0x100 / sizeof(std::uint32_t); ++i) {
            line << words[i];
            if (i + 1 != 0x100 / sizeof(std::uint32_t)) line << ',';
        }
        log_line(line.str().c_str());
    }
    g_original_player_fire(controller, fire_args, weapon, scalar);
}

void probe_velocity_field() {
    static float prev[3]{};
    static bool have_prev = false;
    static ULONGLONG last_log = 0;
    static std::array<float, 32> prev_block0{};
    static bool have_prev_block0 = false;
    void* p = retained_player_object(0);
    if (p == nullptr || readable_bytes(p) < 0xC10) {
        return;
    }
    auto* base = static_cast<std::byte*>(p);
    const auto* pos = reinterpret_cast<const float*>(base + kWorldPositionOffset);
    const float cur[3]{pos[0], pos[1], pos[2]};
    if (!have_prev) {
        std::memcpy(prev, cur, sizeof(prev));
        have_prev = true;
        return;
    }
    const float dx = cur[0] - prev[0];
    const float dz = cur[2] - prev[2];
    std::memcpy(prev, cur, sizeof(prev));
    const float dlen = std::sqrt(dx * dx + dz * dz);
    if (dlen < 0.01F) {
        return;  // not moving this frame
    }
    const ULONGLONG now = GetTickCount64();
    if (now - last_log < 150) {
        return;
    }
    last_log = now;
    const float ndx = dx / dlen;
    const float ndz = dz / dlen;

    // Expected camera-relative stick for this world movement direction:
    // dir = forward*sf + right*sr  =>  sf = dir.forward, sr = dir.right.
    float fx = 0, fz = 1, rx = 1, rz = 0;
    const bool have_cam = get_camera_axes(fx, fz, rx, rz);
    const float sf = ndx * fx + ndz * fz;
    const float sr = ndx * rx + ndz * rz;

    std::ostringstream line;
    line << "velprobe wdir=" << ndx << "," << ndz;
    if (have_cam) {
        line << " stick(sr,sf)=" << sr << "," << sf;
    }
    // Scan a memory object [obj, obj+span) for: W = world-direction vectors
    // matching movement dir (mag 0.3..25), and S = camera-relative stick pairs
    // matching (sr,sf). Labels the section.
    auto scan = [&](const char* label, std::byte* obj, std::size_t span) {
        if (obj == nullptr) {
            return;
        }
        // Clamp the scan to what is actually readable so we never overrun the
        // object into unmapped memory (the background thread has no game-side
        // lifetime guarantees).
        const std::size_t avail = readable_bytes(obj);
        if (avail < 0x20) {
            return;
        }
        if (span > avail - 0x10) {
            span = avail - 0x10;
        }
        line << " " << label << "/W:";
        for (std::size_t off = 0x10; off <= span; off += 4) {
            const auto* v = reinterpret_cast<const float*>(obj + off);
            const float vh = std::sqrt(v[0] * v[0] + v[2] * v[2]);
            if (vh < 0.3F || vh > 25.0F) {
                continue;
            }
            const float dot = (v[0] * ndx + v[2] * ndz) / vh;
            if (dot > 0.97F) {
                line << " +0x" << std::hex << off << std::dec
                     << "(" << v[0] << "," << v[1] << "," << v[2] << ",m" << vh << ")";
            }
        }
        if (have_cam) {
            line << " " << label << "/S:";
            for (std::size_t off = 0x10; off <= span; off += 4) {
                const auto* v = reinterpret_cast<const float*>(obj + off);
                const float a = v[0];
                const float b = v[1];
                const float m = std::sqrt(a * a + b * b);
                if (m < 0.3F || m > 1.3F) {
                    continue;
                }
                const float tol = 0.10F;
                const bool match =
                    (std::abs(a - sr) < tol && std::abs(b - sf) < tol) ||
                    (std::abs(a - sf) < tol && std::abs(b - sr) < tol) ||
                    (std::abs(a + sr) < tol && std::abs(b + sf) < tol) ||
                    (std::abs(a + sf) < tol && std::abs(b + sr) < tol);
                if (match) {
                    line << " +0x" << std::hex << off << std::dec
                         << "(" << a << "," << b << ")";
                }
            }
        }
    };

    scan("P", base, 0xC00);
    // Locomotion controller object pointed to by player+0x15B8 — the likely
    // home of the per-player move intent / desired-move we can inject.
    void* controller = *reinterpret_cast<void**>(base + kLocomotionControllerOffset);
    scan("C", static_cast<std::byte*>(controller), 0x1200);
    // PlayerSnapshot (player+0xb0) — the player's movement/control state object;
    // the control writes the move intent here from input. Scan for the intent.
    void* psnap = *reinterpret_cast<void**>(base + 0xB0);
    scan("PS", static_cast<std::byte*>(psnap), 0x800);
    // Slot-0 input block (0x1466E4220) — find the move-stick field to feed slot 1.
    scan("B0", reinterpret_cast<std::byte*>(g_module + 0x66E4220), 0x300);

    // Also diff the first 0x80 bytes of block[0] across real player movement.
    // The breakpoints on +0/+4 and +0x10/+0x14 stayed silent, so this tells us
    // which offsets actually change on the live keyboard/controller path.
    auto* block0 = reinterpret_cast<float*>(g_module + 0x66E4220);
    if (readable_bytes(block0) >= prev_block0.size() * sizeof(float)) {
        if (!have_prev_block0) {
            std::memcpy(prev_block0.data(), block0, prev_block0.size() * sizeof(float));
            have_prev_block0 = true;
        } else {
            line << " B0diff:";
            int shown = 0;
            for (std::size_t i = 0; i < prev_block0.size() && shown < 16; ++i) {
                const float curv = block0[i];
                const float oldv = prev_block0[i];
                if (std::abs(curv - oldv) > 0.02f) {
                    line << " +0x" << std::hex << (i * sizeof(float)) << std::dec
                         << "(" << oldv << "->" << curv << ")";
                    ++shown;
                }
            }
            std::memcpy(prev_block0.data(), block0, prev_block0.size() * sizeof(float));
        }
    }
    log_line(line.str().c_str());
}

void log_player_slot_positions() {
    static ULONGLONG last_log = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last_log < 1000) {
        return;
    }
    void* p0 = retained_player_object(0);
    void* p1 = retained_player_object(1);
    if (p0 == nullptr && p1 == nullptr) {
        return;
    }
    last_log = now;
    std::ostringstream line;
    line << "player slots";
    auto add_slot = [&](int slot, void* obj) {
        line << " s" << slot << "=0x" << std::hex << reinterpret_cast<std::uintptr_t>(obj);
        if (obj == nullptr || readable_bytes(obj) < kPlayerVelocityMirrorOffset + 0x10) {
            line << std::dec;
            return;
        }
        auto* base = static_cast<std::byte*>(obj);
        const auto vtable = *reinterpret_cast<std::uintptr_t*>(obj);
        const auto* pos = reinterpret_cast<const float*>(base + kWorldPositionOffset);
        const auto* vel = reinterpret_cast<const float*>(base + kPlayerVelocityOffset);
        line << " vt=0x" << (vtable > g_module ? vtable - g_module : 0)
             << std::dec
             << " pos=(" << pos[0] << "," << pos[1] << "," << pos[2] << ")"
             << " vel=(" << vel[0] << "," << vel[1] << "," << vel[2] << ")";
    };
    add_slot(0, p0);
    add_slot(1, p1);
    log_line(line.str().c_str());
}

// Experimental slot-1 drive path. Runtime showed that clean block[1] feed and
// the FrameState+0x6FC8 branch structs are not cleanly isolated from Chloe, so
// this stays diagnostic only. block[i] = 0x1466E4220 + i*0x530.
volatile bool g_drive_slot1 = false;
volatile bool g_manual_slot1_apply = false;

// Which retained slot the drive feeds (1 = freshly spawned clone; after a
// camera slot-swap the clone becomes slot 0, so we drive slot 0 to test whether
// being "local-player-0" gives it the main-camera movement basis = straight).
volatile int g_drive_slot_index = 1;
// The clone object captured before any swap, so we log ITS position across swaps.
void* g_autotest_clone_obj = nullptr;

// Live-tunable move mapping: rotate the (fx,-fy) move vector by g_move_rot and
// optionally swap axes, so the user can dial in the clone's camera convention in
// game (NUMPAD7/9 = -/+15deg, NUMPAD8 = axis swap) without rebuilds.
volatile float g_move_rot = -1.5707963F;  // radians; measured slot-1 basis correction.
                                        // NUMPAD7/9 retunes live if needed.
volatile bool g_move_swap = false;
volatile float g_desired_world_x = 0.0F;
volatile float g_desired_world_z = 0.0F;
volatile bool g_have_desired_world = false;

void sync_screen1_camera_rotation_to_screen0() {
    const auto get_camera_manager =
        reinterpret_cast<GetCameraManagerFunction>(
            g_module + kGetCameraManagerRva
        );
    void* const cam0 = get_camera_manager(0);
    void* const cam1 = get_camera_manager(1);
    if (cam0 == nullptr || cam1 == nullptr) {
        return;
    }
    auto* c0 = static_cast<std::byte*>(cam0);
    auto* c1 = static_cast<std::byte*>(cam1);
    if (readable_bytes(c0 + kCameraRotationOffset) < 0x60 ||
        readable_bytes(c1 + kCameraRotationOffset) < 0x60) {
        return;
    }
    std::memcpy(c1 + kCameraRotationOffset, c0 + kCameraRotationOffset, 16);
    std::memcpy(c1 + 0x200, c0 + 0x200, 16);
}

// Auto-test: drive the slot-1 clone forward for a window with no manual keys.
volatile bool g_auto_stick_active = false;
volatile float g_auto_stick_x = 0.0F;
volatile float g_auto_stick_y = 0.0F;
ULONGLONG g_autotest_t0 = 0;
bool g_autotest_done = false;
bool g_autotest_drive_on = false;

struct SyntheticPadLite {
    std::uint32_t index = 0;
    std::uint8_t connected = 1;
    std::uint8_t pad_05[7]{};
    XInputCapabilitiesLite caps{};
    XInputStateLite state{};
};

// Feed the peer's resolved action state through the engine's native scripted-input
// structure for retained slot 1. Transform replication alone can move the remote
// Player but cannot make its weapon graph aim/fire. Keep movement axes zero here:
// position/facing still come from the authoritative snapshot, while this first
// stage isolates action delivery from the unresolved slot-1 camera basis.
void drive_remote_slot1_actions_tick() {
    void* const remote_player = retained_player_object(1);
    if (!g_puppet_net_apply || g_module == 0 || remote_player == nullptr ||
        readable_bytes(static_cast<std::byte*>(remote_player) + 0x39BC) < 1) {
        return;
    }
    const int joypad_index = *reinterpret_cast<const std::int32_t*>(
        static_cast<const std::byte*>(remote_player) + 0x39B8);
    if (joypad_index < 0 || joypad_index >= 6) {
        return;
    }
    void* fs = *reinterpret_cast<void**>(g_module + 0x3FE1088);
    if (fs == nullptr || readable_bytes(fs) < 0x7100) {
        return;
    }
    // DIAGNOSTIC: compare the real per-slot Joypad objects. The dispatch cursor is
    // FrameState+0xE88, but it passes cursor-0xD8 to Input_ScaleCopyVirtual, so the
    // actual objects begin at FrameState+0xDB0 with a 0xBF0 stride.
    {
        static ULONGLONG last_ss = 0;
        static ULONGLONG mode_since = 0;
        static bool previous_f9 = false;
        static bool have_branch = false;
        static bool have_spin = false;
        static bool diff_logged = false;
        static std::array<std::byte, 0xBF0> branch_snapshot{};
        static std::array<std::byte, 0xBF0> spin_snapshot{};
        const ULONGLONG ssn = GetTickCount64();
        const bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (f9 != previous_f9) {
            previous_f9 = f9;
            mode_since = ssn;
        }
        if (mode_since == 0) mode_since = ssn;
        if (ssn - last_ss > 700) {
            last_ss = ssn;
            void* const local_player = retained_player_object(0);
            const int local_joypad_index =
                local_player != nullptr &&
                        readable_bytes(static_cast<std::byte*>(local_player) + 0x39BC) >= 1
                    ? *reinterpret_cast<const std::int32_t*>(
                          static_cast<const std::byte*>(local_player) + 0x39B8)
                    : -1;
            auto* ss0 = local_joypad_index >= 0 && local_joypad_index < 6
                ? static_cast<std::byte*>(fs) + 0xDB0 + 0xBF0 * local_joypad_index
                : nullptr;
            auto* ss1 = static_cast<std::byte*>(fs) + 0xDB0 + 0xBF0 * joypad_index;
            if (ss0 != nullptr && readable_bytes(ss0) >= 0xBF0 &&
                readable_bytes(ss1) >= 0xBF0) {
                auto* d0 = reinterpret_cast<const std::uint32_t*>(ss0);
                auto* d1 = reinterpret_cast<const std::uint32_t*>(ss1);
                std::ostringstream s;
                s << "JOYPAD-SLOT f9=" << (f9 ? 1 : 0)
                  << " remote=" << joypad_index << "[";
                for (int i = 0; i < 32; ++i) s << std::hex << d1[i] << " ";
                s << "] local=" << std::dec << local_joypad_index << "[" << std::hex;
                for (int i = 0; i < 32; ++i) s << std::hex << d0[i] << " ";
                s << "]" << std::dec;
                log_line(s.str().c_str());

                // Reliable capture of slot-1's resolved block (0x66E4220+0x530): the 8
                // stick floats + buttons. Compare f9=1 (byte-gate, clone MOVES) vs f9=0
                // (struct, clone stands) to see exactly which source value differs.
                auto* blk1 = reinterpret_cast<const std::byte*>(
                    g_module + 0x66E4220 + 0x530);
                if (readable_bytes(blk1) >= 0x528) {
                    auto* bf = reinterpret_cast<const float*>(blk1);
                    std::ostringstream b;
                    b << "BLOCK1 f9=" << (f9 ? 1 : 0) << " axes[";
                    for (int i = 0; i < 8; ++i) b << bf[i] << " ";
                    b << "] p24=" << bf[9] << " btn=0x" << std::hex
                      << *reinterpret_cast<const std::uint64_t*>(blk1 + 0x518)
                      << std::dec;
                    log_line(b.str().c_str());
                }

                // Let each mode run before sampling so the previous frame has
                // completed Input_ScaleCopyVirtual and its consumer callbacks.
                if (ssn - mode_since > 500) {
                    auto& destination = f9 ? spin_snapshot : branch_snapshot;
                    std::memcpy(destination.data(), ss1, destination.size());
                    if (f9) have_spin = true; else have_branch = true;
                }
                if (have_branch && have_spin && !diff_logged) {
                    diff_logged = true;
                    int differences = 0;
                    for (std::size_t off = 0; off < 0xBF0; off += 4) {
                        std::uint32_t branch_value = 0;
                        std::uint32_t spin_value = 0;
                        std::memcpy(&branch_value, branch_snapshot.data() + off, 4);
                        std::memcpy(&spin_value, spin_snapshot.data() + off, 4);
                        if (branch_value == spin_value) continue;
                        if (differences < 160) {
                            std::ostringstream diff;
                            diff << "JOYPAD-DIFF +0x" << std::hex << off
                                 << " branch=0x" << branch_value
                                 << " spin=0x" << spin_value << std::dec;
                            log_line(diff.str().c_str());
                        }
                        ++differences;
                    }
                    std::ostringstream summary;
                    summary << "JOYPAD-DIFF total=" << differences
                            << " (first 160 logged)";
                    log_line(summary.str().c_str());
                }
            }
        }
    }
    auto* branch1 = static_cast<std::byte*>(fs) + 0x6FC8 + 0x28 * joypad_index;
    auto* spin_gate = static_cast<std::uint8_t*>(fs) + 0x70B8 + joypad_index;
    coop::StatePacket peer{};
    if (!copy_peer_state_snapshot(peer)) {
        coop::StatePacket last_peer{};
        if (copy_last_peer_state_snapshot(last_peer)) {
            apply_peer_health_to_slot1(last_peer, false);
        }
        // Never leave held fire/aim behind after a stale or disconnected peer.
        std::memset(branch1, 0, 0x28);
        *spin_gate = 0;
        return;
    }

    apply_peer_health_to_slot1(peer, false);

    // The dispatch destination is Player+0x39B8 (Joypad 3 for retained slot 1 in
    // campaign), not the retained-table index. Keep source block 1 isolated from
    // Chloe's block 0.
    auto* slot1_index = static_cast<std::uint8_t*>(fs) + 0xE88 +
                        0xBF0 * joypad_index + 0x18;
    if (readable_bytes(slot1_index) >= 1) {
        *slot1_index = 1;
    }
    // DIAGNOSTIC (hold F9): force the byte-gate DEMO SPIN for slot 1 with the struct
    // branch OFF, to test whether the clone consumes slot-1 input AT ALL. If the clone
    // spins => binding OK (our struct values are the issue); if it does NOT spin =>
    // the clone is not bound to slot-1 input (the buried control->player binding).
    if ((GetAsyncKeyState(VK_F9) & 0x8000) != 0) {
        std::memset(branch1, 0, 0x28);  // struct branch disabled (enable byte 0)
        *spin_gate = 1;                 // re-set every frame (the demo gate self-clears)
        static ULONGLONG spin_log = 0;
        const ULONGLONG spin_now = GetTickCount64();
        if (spin_now - spin_log > 500) {
            spin_log = spin_now;
            std::ostringstream spin;
            spin << "REMOTE-DRIVE F9 spin-test: destination=" << joypad_index
                 << " source=1 byte gate forced, struct OFF";
            auto* blk1 = reinterpret_cast<const std::byte*>(
                g_module + 0x66E4220 + 0x530);
            if (readable_bytes(blk1) >= 0x528) {
                const auto* values = reinterpret_cast<const float*>(blk1);
                spin << " previous-block axes=[" << values[0] << "," << values[1]
                     << "," << values[2] << "," << values[3] << "; "
                     << values[4] << "," << values[5] << "," << values[6]
                     << "," << values[7] << "] buttons=0x" << std::hex
                     << *reinterpret_cast<const std::uint64_t*>(blk1 + 0x518)
                     << std::dec;
            }
            log_line(spin.str().c_str());
        }
        return;
    }
    std::memset(branch1, 0, 0x28);
    branch1[0] = std::byte{1};
    // Keep the synthetic circular-motion branch off. Its only unique global writes
    // enable the built-in Infinite Ammo / Infinite Grenades cheats; they are not
    // locomotion activation. Running both branches also applies the Joypad state
    // twice and contaminates this comparison.
    *spin_gate = 0;
    // struct+0x0C = duration; the per-slot apply accumulates dt at +0x24 and times
    // out the input when it exceeds +0x0C. The PROVEN drive_slot1_tick keeps this
    // LARGE so the input stays active every frame (0.1 timed out -> clone stood).
    *reinterpret_cast<float*>(branch1 + 0x0C) = 1.0e9F;

    // Feed the peer's native raw move pair. The post-slot-16 hook below replaces
    // the camera-derived result with the peer's native world vector, so screen 1
    // cannot rotate the intended direction. Position reconciliation is separate.
    const bool movement_valid = valid_network_movement(peer);
    float correction_x = 0.0F;
    float correction_z = 0.0F;
    const bool correcting = compute_remote_position_correction(
        peer, remote_player, correction_x, correction_z);
    float sx = movement_valid ? peer.move_stick[0] : 0.0F;
    float sy = movement_valid ? peer.move_stick[1] : 0.0F;
    const float magnitude = movement_valid ? peer.move_magnitude : 0.0F;
    const bool moving = magnitude > 0.01F || correcting;
    *reinterpret_cast<float*>(branch1 + 0x14) = sx;   // L-stick X
    *reinterpret_cast<float*>(branch1 + 0x18) = sy;   // L-stick Y
    // DISPROVEN (2026-06-30): replicating the byte-gate's block right stick
    // (+0x08=0.5, +0x0C=-0.72) did not move the remote clone. The later route read
    // proved CameraManager(1) exists; this test had targeted Joypad 1 while the clone
    // consumes Joypad 3. Keep right stick zero so this correction isolates the real
    // destination index.
    *reinterpret_cast<float*>(branch1 + 0x1C) = 0.0F;
    *reinterpret_cast<float*>(branch1 + 0x20) = 0.0F;

    // struct+0x10 (flags) is written to the block buttons VERBATIM by the real
    // branch: leaving it 0 makes locomotion IGNORE a valid stick (clone STANDS). So
    // set the native "input active" bits 0x500 whenever a stick is fed, plus the
    // peer's resolved buttons (fire/aim/jump/etc).
    std::uint32_t actions = static_cast<std::uint32_t>(peer.input_buttons);
    if (moving || actions != 0 || peer.buttons_pressed != 0 ||
        peer.buttons_released != 0) {
        actions |= 0x500u;
    }
    *reinterpret_cast<std::uint32_t*>(branch1 + 0x10) = actions;
    static std::uint32_t previous_actions = 0;
    if (actions != previous_actions) {
        std::ostringstream line;
        line << "REMOTE-ACTIONS seq=" << peer.sequence
             << " held=0x" << std::hex << peer.input_buttons
             << " pressed=0x" << peer.buttons_pressed
             << " released=0x" << peer.buttons_released
             << " branch=0x" << actions << std::dec;
        log_line(line.str().c_str());
        previous_actions = actions;
    }
    // Rate-limited drive diagnostic: confirm the feed runs, the magnitude, and that
    // the engine's per-slot apply CONSUMED our struct into slot-1's block[1].
    static ULONGLONG last_drive_log = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last_drive_log > 500) {
        last_drive_log = now;
        std::ostringstream d;
        d << "REMOTE-DRIVE destination=" << joypad_index << " source=1 magnitude="
          << magnitude
          << " correction=(" << correction_x << "," << correction_z << ")"
          << " sx=" << sx << " sy=" << sy << " flags=0x" << std::hex << actions
          << std::dec;
        auto* blk1 = reinterpret_cast<std::byte*>(g_module + 0x66E4220 + 0x530);
        if (readable_bytes(blk1) >= 0x520) {
            auto* bf = reinterpret_cast<const float*>(blk1);
            d << " | block1 sx=" << bf[0] << " sy=" << bf[1] << " rawx=" << bf[4]
              << " rawy=" << bf[5] << " btn=0x" << std::hex
              << *reinterpret_cast<const std::uint64_t*>(blk1 + 1304) << std::dec;
        }
        log_line(d.str().c_str());
    }
}

void drive_slot1_tick() {
    if (!g_drive_slot1 || g_module == 0) {
        return;
    }
    if (retained_player_object(1) == nullptr) {
        return;
    }
    void* fs = *reinterpret_cast<void**>(g_module + 0x3FE1088);
    if (fs == nullptr || readable_bytes(fs) < 0x7100) {
        return;
    }
    const int idx = g_drive_slot_index;
    // Slot-N "block active" index byte. Only force it for the spawned slot (>0);
    // slot 0 is already an active local player.
    if (idx > 0) {
        auto* slotN_index =
            static_cast<std::uint8_t*>(fs) + 0xE88 + 0xBF0 * idx + 0x18;
        if (readable_bytes(slotN_index) >= 1) {
            *slotN_index = 1;
        }
    }
    auto* spin_gate = reinterpret_cast<std::uint8_t*>(
        static_cast<std::byte*>(fs) + 0x70B8 + idx);
    auto* branch1 = static_cast<std::byte*>(fs) + 0x6FC8 + 0x28 * idx;
    sync_screen1_camera_rotation_to_screen0();
    // Do NOT mirror branch0 (= slot-0 / Chloe's live control struct): that drags
    // her button/action state into slot 1 (clone ran/jumped/opened map). Build
    // branch1 CLEAN: zero it, set the gate byte, and fill only heading/mag below.
    std::memset(branch1, 0, 0x28);
    branch1[0] = std::byte{1};
    // The +0x70B8 path is the proven "run in circles" demo path. Keep it OFF
    // once the real branch struct is mirrored so slot 1 can only take the
    // per-slot real-input branch.
    *spin_gate = 0;
    if (!g_slot1_branch_struct_logged) {
        g_slot1_branch_struct_logged = true;
        auto* q1 = reinterpret_cast<const std::uint64_t*>(branch1);
        std::ostringstream line;
        line << "drive branch-struct diagnostic slot=" << idx
             << " dst=[" << std::hex << q1[0] << " " << q1[1] << " "
             << q1[2] << " " << q1[3] << " " << q1[4] << "]"
             << " gate_byte=" << static_cast<unsigned>(std::to_integer<unsigned char>(branch1[0]))
             << " spin_gate=" << std::dec << static_cast<int>(*spin_gate);
        log_line(line.str().c_str());
    }

    int lx = 0;
    int ly = 0;
    if ((GetAsyncKeyState('A') & 0x8000) != 0) lx -= 22000;
    if ((GetAsyncKeyState('D') & 0x8000) != 0) lx += 22000;
    if ((GetAsyncKeyState('S') & 0x8000) != 0) ly -= 22000;
    if ((GetAsyncKeyState('W') & 0x8000) != 0) ly += 22000;
    if (g_auto_stick_active && lx == 0 && ly == 0) {
        lx = static_cast<int>(g_auto_stick_x * 22000.0F);
        ly = static_cast<int>(g_auto_stick_y * 22000.0F);
    }

    // Gamepad (controller 0): LEFT stick steers the clone. The right stick is not
    // forwarded into slot 1 (it mutates slot 1's hidden camera/yaw basis and makes
    // movement directions drift).
    if (g_real_xinput_get_state != nullptr) {
        XInputStateLite pad{};
        if (g_real_xinput_get_state(0, &pad) == 0) {
            const int dz = 7000;  // stick deadzone
            const auto& g = pad.Gamepad;
            const bool lxa = g.sThumbLX > dz || g.sThumbLX < -dz;
            const bool lya = g.sThumbLY > dz || g.sThumbLY < -dz;
            if (lxa || lya) {
                lx = g.sThumbLX;
                ly = g.sThumbLY;
            }
        }
    }

    const ULONGLONG now = GetTickCount64();

    // sub_1402893D0 (the per-slot real-input branch) maps this struct ->
    // slot-1's input block (0x1466E4220+0x530):
    //   struct+0x14 -> block stick X (block+0x00 / +0x10)
    //   struct+0x18 -> block stick Y (block+0x04 / +0x14)
    //   struct+0x1C -> block+0x08 / +0x18
    //   struct+0x20 -> block+0x0C / +0x1C
    //   struct+0x10 (u32 flags) -> block buttons qword (block+1304/+1288/+1296)
    // The proven SPIN path forces block-buttons |= 0x500 ("input active" bits);
    // the real branch writes block-buttons = struct+0x10 verbatim, so leaving
    // struct+0x10 = 0 makes locomotion ignore a valid stick (clone STANDS).
    // => set 0x500 whenever we feed a stick. struct+0x0C = duration (kept large),
    // struct+0x24 = elapsed (0).
    {
        float fx = static_cast<float>(lx) / 22000.0F;
        float fy = static_cast<float>(ly) / 22000.0F;
        bool used_block0_input = false;
        auto* blk0b = reinterpret_cast<std::byte*>(g_module + 0x66E4220);
        if (readable_bytes(blk0b) >= 0x528) {
            auto* b0f = reinterpret_cast<const float*>(blk0b);
            const float b0x = b0f[0];
            const float b0y = b0f[1];
            const float b0rx = b0f[2];
            const float b0ry = b0f[3];
            if (std::fabs(fx) < 0.001F && std::fabs(fy) < 0.001F &&
                (std::fabs(b0x) > 0.001F || std::fabs(b0y) > 0.001F)) {
                // Fall back to Chloe's already-normalized game input. This covers
                // keyboard/Steam Input paths that do not show up in raw XInput.
                // block0 is already in the game's processed stick convention,
                // but applying it 1:1 to slot 1 is reversed in-game. Keep the
                // arrow-key path untouched; only flip the block0 fallback.
                fx = -b0x;
                fy = b0y;
                lx = static_cast<int>(fx * 22000.0F);
                ly = static_cast<int>(fy * 22000.0F);
                used_block0_input = true;
            }
            (void)b0rx;
            (void)b0ry;
        }
        float flen = std::sqrt(fx * fx + fy * fy);
        if (flen > 1.0F) { fx /= flen; fy /= flen; flen = 1.0F; }  // clamp, keep analog
        *reinterpret_cast<float*>(branch1 + 0x0C) = 1.0e9F;  // duration
        // struct+0x14/+0x18 -> block LEFT stick (camera-relative move).
        // struct+0x1C/+0x20 -> RIGHT stick/camera; keep it zero for now. Forwarding
        // Chloe's camera input made slot 1's movement basis unstable.
        const bool moving = flen > 0.0F;
        const bool turning = false;
        if (moving) {
            float fwx = 0.0F, fwz = 1.0F, rx = 1.0F, rz = 0.0F;
            if (get_camera_axes(fwx, fwz, rx, rz)) {
                float dwx = fx * rx + fy * fwx;
                float dwz = fx * rz + fy * fwz;
                const float dl = std::sqrt(dwx * dwx + dwz * dwz);
                if (dl > 0.001F) {
                    g_desired_world_x = dwx / dl;
                    g_desired_world_z = dwz / dl;
                    g_have_desired_world = true;
                }
            }
        } else {
            g_have_desired_world = false;
        }
        float mx = fx;
        float my = -fy;
        float c0fx = 0.0F, c0fz = 1.0F, c0rx = 1.0F, c0rz = 0.0F;
        const bool have_c0 = get_camera_axes_idx(0, c0fx, c0fz, c0rx, c0rz);
        if (moving && have_c0) {
            const float world_x = fx * c0rx + fy * c0fx;
            const float world_z = fx * c0rz + fy * c0fz;
            const float world_len = std::sqrt(world_x * world_x + world_z * world_z);
            if (world_len > 0.001F) {
                const float wx = world_x / world_len;
                const float wz = world_z / world_len;
                // Measured from runtime slot-1 movement:
                //   feed ( 0,-1) -> world +X
                //   feed ( 1, 0) -> world +Z
                // So invert that basis directly. CameraManager(1) may match cam0,
                // but slot-1 locomotion still uses this lower movement basis.
                mx = wz;
                my = -wx;
            }
        }
        if (g_move_swap) { const float t = mx; mx = my; my = t; }
        const float cr = std::cos(g_move_rot);
        const float sr = std::sin(g_move_rot);
        const float rmx = mx * cr - my * sr;
        const float rmy = mx * sr + my * cr;
        *reinterpret_cast<float*>(branch1 + 0x14) = moving ? rmx : 0.0F;
        *reinterpret_cast<float*>(branch1 + 0x18) = moving ? rmy : 0.0F;
        *reinterpret_cast<float*>(branch1 + 0x1C) = 0.0F;
        *reinterpret_cast<float*>(branch1 + 0x20) = 0.0F;
        // Buttons: mirror Chloe's resolved action bits (block0+1304 low 32) so the
        // clone gets jump/melee/dodge/interact/aim/fire = "everything Chloe has".
        std::uint32_t flags = (moving || turning) ? 0x500u : 0u;
        if (readable_bytes(blk0b) >= 0x528) {
            const std::uint64_t cb = *reinterpret_cast<const std::uint64_t*>(blk0b + 1304);
            flags |= static_cast<std::uint32_t>(cb & 0xFFFFFFFFu);
        }
        *reinterpret_cast<std::uint32_t*>(branch1 + 0x10) = flags;
        static ULONGLONG last_src_log = 0;
        if (used_block0_input && now - last_src_log > 500) {
            last_src_log = now;
            std::ostringstream src;
            src << "slot1 input source: block0 fx=" << fx << " fy=" << fy
                << " cam=(disabled)";
            log_line(src.str().c_str());
        }
    }

    // No direct block[1] feed: let the engine's own per-slot apply
    // (sub_1402893D0) read branch1 and write block[1]. We only own branch1
    // (clean heading/mag), so the clone gets ONLY our move, no Chloe buttons.
    // Camera-vs-motion capture: when the stick is held (no right stick), log the
    // block stick we feed, the clone's world velocity, and the rendering camera's
    // forward vector. This reveals the yaw offset between the clone's locomotion
    // camera and the camera the user sees, so we can compensate.
    {
        static ULONGLONG clast = 0;
        static float prevx = 0.0F, prevz = 0.0F;
        void* clone = g_autotest_clone_obj ? g_autotest_clone_obj
                                           : retained_player_object(idx);
        if (clone != nullptr && now - clast > 300 &&
            readable_bytes(static_cast<std::byte*>(clone) + 0x140) >= 4) {
            auto* p = reinterpret_cast<const float*>(
                static_cast<std::byte*>(clone) + kWorldPositionOffset);
            const float dx = p[0] - prevx;
            const float dz = p[2] - prevz;
            prevx = p[0]; prevz = p[2];
            if ((lx != 0 || ly != 0) && (dx * dx + dz * dz) > 1e-4F) {
                clast = now;
                // rendering camera forward
                float c0fx = 0, c0fz = 0, c0rx = 0, c0rz = 0;
                float c1fx = 0, c1fz = 0, c1rx = 0, c1rz = 0;
                const bool have0 = get_camera_axes_idx(0, c0fx, c0fz, c0rx, c0rz);
                const bool have1 = get_camera_axes_idx(idx, c1fx, c1fz, c1rx, c1rz);
                std::ostringstream cl;
                cl << "CAMCAP in=(" << lx << "," << ly << ") fed=("
                   << *reinterpret_cast<float*>(branch1 + 0x14) << ","
                   << *reinterpret_cast<float*>(branch1 + 0x18) << ") vel=("
                   << dx << "," << dz << ") desired=("
                   << g_desired_world_x << "," << g_desired_world_z
                   << ") move_rot=" << g_move_rot
                   << " cam0=" << (have0 ? 1 : 0) << "F(" << c0fx << "," << c0fz << ")"
                   << " cam1=" << (have1 ? 1 : 0) << "F(" << c1fx << "," << c1fz << ")";
                log_line(cl.str().c_str());
            }
        }
    }
    // Button-bit capture: log Chloe's slot-0 input block button qword (block+1304)
    // and the current gamepad button mask whenever Chloe's buttons are nonzero, so
    // we learn the game's jump/melee/dodge/interact bits (to feed them to the clone).
    {
        auto* blk0 = reinterpret_cast<std::byte*>(g_module + 0x66E4220);
        if (readable_bytes(blk0) >= 0x528) {
            const std::uint64_t b0 = *reinterpret_cast<const std::uint64_t*>(blk0 + 1304);
            static std::uint64_t prev_b0 = 0;
            if (b0 != prev_b0 && b0 != 0) {
                prev_b0 = b0;
                std::uint16_t pad_btn = 0;
                if (g_real_xinput_get_state != nullptr) {
                    XInputStateLite ps{};
                    if (g_real_xinput_get_state(0, &ps) == 0) pad_btn = ps.Gamepad.wButtons;
                }
                std::ostringstream bl;
                bl << "BTNCAP chloe block0 buttons=0x" << std::hex << b0
                   << " padButtons=0x" << pad_btn << std::dec;
                log_line(bl.str().c_str());
            } else if (b0 != prev_b0) {
                prev_b0 = b0;
            }
        }
    }
    static ULONGLONG last_log = 0;
    if (now - last_log > 500) {
        last_log = now;
        auto* bf = reinterpret_cast<const float*>(branch1);
        std::ostringstream line;
        line << "drive slot=" << idx << " lx=" << lx << " ly=" << ly
             << " hx=" << bf[5] << " hy=" << bf[6] << " mag=" << bf[7]
             << " flags=0x" << std::hex
             << *reinterpret_cast<const std::uint32_t*>(branch1 + 0x10) << std::dec;
        // Read back what the engine's per-slot apply wrote into slot-N's input
        // block (0x1466E4220+0x530*idx). If consumed, block stickX/Y mirror ours.
        auto* blk1 = reinterpret_cast<std::byte*>(
            g_module + 0x66E4220 + 0x530 * idx);
        if (readable_bytes(blk1) >= 0x520) {
            auto* bfx = reinterpret_cast<const float*>(blk1);
            line << " | block1 sx=" << bfx[0] << " sy=" << bfx[1]
                 << " btn=0x" << std::hex
                 << *reinterpret_cast<const std::uint64_t*>(blk1 + 1304) << std::dec;
        }
        log_line(line.str().c_str());
    }
}

void apply_slot1_perslot_on_game_thread() {
    if (!g_manual_slot1_apply) {
        return;
    }
    if (!g_drive_slot1 || g_module == 0) {
        return;
    }
    if (retained_player_object(1) == nullptr) {
        return;
    }
    void* fs = reinterpret_cast<void*>(g_perslot_fs);
    void* a4 = reinterpret_cast<void*>(g_perslot_a4);
    if (fs == nullptr || a4 == nullptr) {
        return;
    }
    auto* block1 = reinterpret_cast<void*>(g_module + 0x66E4220 + 0x530);
    const auto apply = reinterpret_cast<PerSlotApplyFn>(g_module + kPerSlotApplyRva);
    static ULONGLONG last_log = 0;
    const ULONGLONG now = GetTickCount64();
    const bool should_log = now - last_log > 500;
    if (should_log) {
        last_log = now;
        std::ostringstream line;
        line << "manual slot1 apply fs=0x" << std::hex << g_perslot_fs
             << " a2=" << std::dec << g_perslot_a2
             << " a4=0x" << std::hex << g_perslot_a4 << std::dec;
        log_line(line.str().c_str());
    }
    arm_inputstate_breakpoint_current_thread();
    const auto* before = reinterpret_cast<const float*>(block1);
    const float b0 = before[0];
    const float b1 = before[1];
    const float b4 = before[4];
    const float b5 = before[5];
    apply(fs, g_perslot_a2, block1, a4, 1);
    if (should_log) {
        const auto* after = reinterpret_cast<const float*>(block1);
        std::ostringstream line;
        line << "manual slot1 apply result"
             << " before=[" << b0 << "," << b1 << "," << b4 << "," << b5 << "]"
             << " after=[" << after[0] << "," << after[1] << "," << after[4] << "," << after[5] << "]";
        log_line(line.str().c_str());
        const std::uintptr_t st = g_inputstate_addr;
        if (st != 0 && readable_bytes(reinterpret_cast<void*>(st)) >= 0x28) {
            auto* f = reinterpret_cast<float*>(st);
            std::ostringstream stline;
            stline << "manual slot1 struct after"
                   << " st=0x" << std::hex << st
                   << " +0x10=0x" << *reinterpret_cast<std::uint32_t*>(st + 0x10)
                   << std::dec
                   << " +0x14=" << f[5]
                   << " +0x18=" << f[6]
                   << " +0x1C=" << f[7]
                   << " +0x20=" << f[8]
                   << " +0x24=" << f[9];
            log_line(stline.str().c_str());
        }
    }
}

void update_inject_stick() {
    if (!g_xinput_inject_p2) {
        return;
    }
    // Manual test: drive the injected player-2 stick from arrow keys so we can
    // verify a second player appears and moves. (Net-driven stick comes later.)
    int lx = 0;
    int ly = 0;
    if ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0) lx -= 28000;
    if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) lx += 28000;
    if ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) ly -= 28000;
    if ((GetAsyncKeyState(VK_UP) & 0x8000) != 0) ly += 28000;
    g_inject_lx = lx;
    g_inject_ly = ly;
}

// ===== Phase 3: host-authoritative ENEMY replication =====
// Enemy NPC vtable RVA (from the CharacterManager dump; grunt class shared by all
// npc-forest1-* enemies). Other enemy archetypes may add more vtables later.
constexpr std::uintptr_t kEnemyVtableRva = 0x294b9d0;     // .?AVPirate@@ (live enemy)
constexpr std::uintptr_t kRagdollVtableRva = 0x29137a0;  // .?AVProcessRagdoll@@ (dead)
constexpr std::uintptr_t kHealthSystemVtableRva = 0x2bbe128;
constexpr std::size_t kHealthSystemOffset = 0x4a8;
constexpr std::size_t kCurrentHpOffset = 0x3c;
constexpr std::size_t kMaxHpOffset = 0x44;

// FNV-1a over the actor name (bounded). Both instances hash the same name string,
// so this is a stable cross-instance key (does not need to match the game's hash).
std::uint32_t coop_name_hash(const char* s, std::size_t maxlen) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < maxlen && s[i] != '\0'; ++i) {
        h ^= static_cast<unsigned char>(s[i]);
        h *= 16777619u;
    }
    return h;
}

// Iterate valid Character objects in the CharacterManager (global *(base+0x3898630)),
// invoking fn(obj, name). Heavily readable_bytes-guarded.
template <class F>
void for_each_character(F&& fn) {
    if (g_module == 0) return;
    void* mgr = *reinterpret_cast<void**>(g_module + 0x3898630);
    if (mgr == nullptr || readable_bytes(mgr) < 0x10) return;
    void* ents = *reinterpret_cast<void**>(mgr);
    std::uint32_t count =
        *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(mgr) + 0xc);
    if (ents == nullptr || count == 0 || count > 4096) return;
    if (readable_bytes(ents) < static_cast<std::size_t>(count) * 16) return;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto* e = static_cast<std::byte*>(ents) + static_cast<std::size_t>(i) * 16;
        void* rec = *reinterpret_cast<void**>(e);
        std::uint32_t id = *reinterpret_cast<std::uint32_t*>(e + 8);
        if (rec == nullptr || readable_bytes(rec) < 0x10) continue;
        void* obj = *reinterpret_cast<void**>(static_cast<std::byte*>(rec) + 8);
        if (obj == nullptr || readable_bytes(obj) < 0x300) continue;
        if (*reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(obj) + 0xbc) != id)
            continue;
        const char* name = reinterpret_cast<const char*>(obj) + sizeof(void*);
        fn(obj, name);
    }
}

bool is_enemy_object(void* obj) {
    std::uintptr_t vt = *reinterpret_cast<std::uintptr_t*>(obj);
    // Track both the live enemy (Pirate) and its dead form (ProcessRagdoll) so the
    // Pirate->Ragdoll death transition stays matched by name.
    return vt == g_module + kEnemyVtableRva || vt == g_module + kRagdollVtableRva;
}
bool is_dead_ragdoll(void* obj) {
    return *reinterpret_cast<std::uintptr_t*>(obj) == g_module + kRagdollVtableRva;
}

// RequestDisableAi starts by dereferencing npc+0x1FC0 and later walks the binding at
// npc+0x1F88 without checking either pointer. A partially constructed NPC therefore
// crashes at RequestDisableAi+0x17. Mirror its complete access chain here and call it
// only once all nodes are readable.
bool npc_ai_control_ready(void* obj) {
    if (obj == nullptr || readable_bytes(obj) < 0x27D1) return false;

    auto* const skill = *reinterpret_cast<std::byte**>(
        static_cast<std::byte*>(obj) + 0x1FC0);
    if (skill == nullptr || readable_bytes(skill + 0x368) < 1) return false;

    auto* const binding = *reinterpret_cast<std::byte**>(
        static_cast<std::byte*>(obj) + 0x1F88);
    if (binding == nullptr || readable_bytes(binding) < 0x10) return false;
    auto* const holder = *reinterpret_cast<std::byte**>(binding + 0x08);
    if (holder == nullptr || readable_bytes(holder) < sizeof(void*)) return false;
    auto* const state = *reinterpret_cast<std::byte**>(holder);
    if (state == nullptr) return true;
    if (readable_bytes(state) < 0x81) return false;

    auto* const owner = *reinterpret_cast<std::byte**>(state + 0x68);
    if (owner == nullptr || readable_bytes(owner + 0x2388) < sizeof(void*)) return false;
    auto* const registry = *reinterpret_cast<std::byte**>(owner + 0x2388);
    if (registry == nullptr || readable_bytes(registry + 0x28) < sizeof(void*)) return false;
    auto* const entries = *reinterpret_cast<std::byte**>(registry + 0x28);
    if (entries == nullptr || readable_bytes(entries) < 0x0C) return false;
    const std::uint32_t count = *reinterpret_cast<std::uint32_t*>(entries + 0x08);
    if (count == 0) return true;
    if (count > 4096) return false;
    auto* const array = *reinterpret_cast<std::byte**>(entries);
    return array != nullptr &&
           readable_bytes(array) >= static_cast<std::size_t>(count) * 24;
}
bool plausible_ptr(std::uintptr_t v);
bool is_npc_enemy_name(const char* name) {
    return name != nullptr && readable_bytes(name) >= 4 && name[0] == 'n' &&
           name[1] == 'p' && name[2] == 'c' && name[3] == '-';
}
bool is_enemy_character(void* obj, const char* name) {
    return is_enemy_object(obj) || is_npc_enemy_name(name);
}

struct EnemyHealthTarget {
    void* enemy = nullptr;
    void* health = nullptr;
    const char* name = "?";
    std::int32_t hp = 0;
    std::int32_t max_hp = 0;
};

bool read_enemy_health(void* obj, EnemyHealthTarget& out) {
    if (obj == nullptr || readable_bytes(static_cast<std::byte*>(obj) + kHealthSystemOffset + 8) < 8) {
        return false;
    }
    void* const health = *reinterpret_cast<void**>(
        static_cast<std::byte*>(obj) + kHealthSystemOffset);
    if (health == nullptr ||
        readable_bytes(static_cast<std::byte*>(health) + kMaxHpOffset + sizeof(std::int32_t)) <
            sizeof(std::int32_t)) {
        return false;
    }
    if (*reinterpret_cast<std::uintptr_t*>(health) != g_module + kHealthSystemVtableRva) {
        return false;
    }
    out.enemy = obj;
    out.health = health;
    out.hp = *reinterpret_cast<std::int32_t*>(
        static_cast<std::byte*>(health) + kCurrentHpOffset);
    out.max_hp = *reinterpret_cast<std::int32_t*>(
        static_cast<std::byte*>(health) + kMaxHpOffset);
    return true;
}

bool find_health_system_in_object(void* obj, EnemyHealthTarget& out) {
    if (read_enemy_health(obj, out)) {
        return true;
    }
    if (obj == nullptr) {
        return false;
    }
    const std::uintptr_t hsvt = g_module + kHealthSystemVtableRva;
    const std::size_t avail = readable_bytes(obj);
    const std::size_t hi = avail < 0x4000 ? avail : 0x4000;
    auto* base = static_cast<std::byte*>(obj);
    for (std::size_t o1 = 0x10; o1 + 8 <= hi; o1 += 8) {
        std::uintptr_t v1 = *reinterpret_cast<std::uintptr_t*>(base + o1);
        if (!plausible_ptr(v1)) continue;
        auto* p1 = reinterpret_cast<std::byte*>(v1);
        if (readable_bytes(p1) < 0xe0) continue;
        if (*reinterpret_cast<std::uintptr_t*>(p1) == hsvt) {
            out.enemy = obj;
            out.health = p1;
            out.hp = *reinterpret_cast<std::int32_t*>(p1 + kCurrentHpOffset);
            out.max_hp = *reinterpret_cast<std::int32_t*>(p1 + kMaxHpOffset);
            return true;
        }
        const std::size_t a2 = readable_bytes(p1);
        const std::size_t hi2 = a2 < 0x300 ? a2 : 0x300;
        for (std::size_t o2 = 0; o2 + 8 <= hi2; o2 += 8) {
            std::uintptr_t v2 = *reinterpret_cast<std::uintptr_t*>(p1 + o2);
            if (!plausible_ptr(v2)) continue;
            auto* p2 = reinterpret_cast<std::byte*>(v2);
            if (readable_bytes(p2) < 0xe0) continue;
            if (*reinterpret_cast<std::uintptr_t*>(p2) == hsvt) {
                out.enemy = obj;
                out.health = p2;
                out.hp = *reinterpret_cast<std::int32_t*>(p2 + kCurrentHpOffset);
                out.max_hp = *reinterpret_cast<std::int32_t*>(p2 + kMaxHpOffset);
                return true;
            }
        }
    }
    return false;
}

bool find_first_live_enemy_health(EnemyHealthTarget& out) {
    for_each_character([&](void* obj, const char* name) {
        if (out.enemy != nullptr || !is_enemy_character(obj, name) || is_dead_ragdoll(obj)) {
            return;
        }
        EnemyHealthTarget candidate{};
        if (!find_health_system_in_object(obj, candidate) || candidate.hp <= 0) {
            return;
        }
        candidate.name = name;
        out = candidate;
    });
    return out.enemy != nullptr;
}

void dump_enemy_health_candidates() {
    int logged = 0;
    int total = 0;
    for_each_character([&](void* obj, const char* name) {
        if (!is_enemy_character(obj, name) || logged >= 32) {
            return;
        }
        ++total;
        std::uintptr_t vt = readable_bytes(obj) >= 8
            ? *reinterpret_cast<std::uintptr_t*>(obj)
            : 0;
        EnemyHealthTarget target{};
        const bool has_health = find_health_system_in_object(obj, target);
        std::ostringstream line;
        line << "HP-CAND #" << logged
             << " name='" << (name != nullptr ? name : "?") << "'"
             << " obj=0x" << std::hex << reinterpret_cast<std::uintptr_t>(obj)
             << " vt_rva=0x" << (vt > g_module ? vt - g_module : 0)
             << std::dec << " dead=" << (is_dead_ragdoll(obj) ? 1 : 0);
        if (has_health) {
            line << " hs=0x" << std::hex << reinterpret_cast<std::uintptr_t>(target.health)
                 << std::dec << " hp=" << target.hp << " max=" << target.max_hp;
        } else {
            line << " no HealthSystem";
        }
        log_line(line.str().c_str());
        ++logged;
    });
    std::ostringstream line;
    line << "HP-CAND total logged=" << logged << " total_seen=" << total;
    log_line(line.str().c_str());
}


void* g_hp_kill_watch_enemy = nullptr;
void* g_hp_kill_watch_health = nullptr;
ULONGLONG g_hp_kill_watch_until = 0;
ULONGLONG g_hp_kill_last_log = 0;

void hp_kill_selftest() {
    EnemyHealthTarget target{};
    if (!find_first_live_enemy_health(target)) {
        log_line("HP-KILL: no live enemy with HealthSystem found");
        dump_enemy_health_candidates();
        return;
    }

    *reinterpret_cast<std::int32_t*>(
        static_cast<std::byte*>(target.health) + kCurrentHpOffset) = 0;
    g_hp_kill_watch_enemy = target.enemy;
    g_hp_kill_watch_health = target.health;
    g_hp_kill_watch_until = GetTickCount64() + 3000;
    g_hp_kill_last_log = 0;

    std::ostringstream line;
    line << "HP-KILL: enemy='" << target.name
         << "' obj=0x" << std::hex << reinterpret_cast<std::uintptr_t>(target.enemy)
         << " hs=0x" << reinterpret_cast<std::uintptr_t>(target.health)
         << std::dec << " HP " << target.hp << "->0"
         << " max=" << target.max_hp;
    log_line(line.str().c_str());
}

void monitor_hp_kill_selftest(ULONGLONG now) {
    if (g_hp_kill_watch_enemy == nullptr || now > g_hp_kill_watch_until ||
        now - g_hp_kill_last_log < 250) {
        return;
    }
    g_hp_kill_last_log = now;

    const bool enemy_readable = readable_bytes(g_hp_kill_watch_enemy) >= 8;
    const bool health_readable =
        g_hp_kill_watch_health != nullptr &&
        readable_bytes(static_cast<std::byte*>(g_hp_kill_watch_health) + kMaxHpOffset + sizeof(std::int32_t)) >=
            sizeof(std::int32_t);
    std::uintptr_t vt = 0;
    bool dead = false;
    if (enemy_readable) {
        vt = *reinterpret_cast<std::uintptr_t*>(g_hp_kill_watch_enemy);
        dead = vt == g_module + kRagdollVtableRva;
    }
    std::int32_t hp = -999999;
    if (health_readable) {
        hp = *reinterpret_cast<std::int32_t*>(
            static_cast<std::byte*>(g_hp_kill_watch_health) + kCurrentHpOffset);
    }

    std::ostringstream line;
    line << "HP-KILL watch: enemy=" << (enemy_readable ? "readable" : "gone")
         << " vt_rva=0x" << std::hex << (vt > g_module ? vt - g_module : 0)
         << std::dec << " dead=" << (dead ? 1 : 0)
         << " hp=" << hp;
    log_line(line.str().c_str());
}

// ===== NPC damage-handler capture: sub_140409F70(Npc* a1, AttackInfo* a2, a3) =====
// The proper kill goes through this upper attack/damage handler (knockdown/death logic).
// It is virtual slot +0x1170 in the character vtables. Static reverse showed that
// AttackInfo is polymorphic and much larger than 0x100 bytes (AttackInfo=0x860,
// CloseCombatAttackInfo=0x880, ProjectileAttackInfo=0x8c0), so raw partial copies are
// invalid. Keep this hook disabled until capture uses AttackInfo virtual clone (+0x8)
// and replay goes through the native event publisher at module+0x27a270.
using NpcDamageFn = std::uint64_t(__fastcall*)(void*, void*, void*);
using AttackInfoCloneFn = void*(__fastcall*)(void*);
using AttackInfoDeleteFn = void*(__fastcall*)(void*, unsigned char);
using AttackInfoConstructorFn = void*(__fastcall*)(void*, int);
using PublishAttackFn = std::uint64_t(__fastcall*)(void*, void*);
std::uint64_t g_orig_npc_damage = 0;
volatile bool g_install_npc_damage_hook = false;  // enemies deferred until player sync is complete
volatile bool g_capture_attackinfo_clone = false;  // OFF: clone_attackinfo(vtable[1]) crashed combat
volatile bool g_npc_damage_capture_log = false;
volatile bool g_enable_attackinfo_replay_test = false;
volatile bool g_enable_fresh_attackinfo_test = false;
void* g_attackinfo_template = nullptr;
void* g_attackinfo_original_target = nullptr;
ULONGLONG g_attackinfo_capture_time = 0;

bool is_supported_attackinfo(void* attack_info) {
    if (attack_info == nullptr || readable_bytes(attack_info) < sizeof(std::uintptr_t)) {
        return false;
    }
    const std::uintptr_t vt = *reinterpret_cast<std::uintptr_t*>(attack_info);
    return vt == g_module + 0x28f9198 ||  // AttackInfo
           vt == g_module + 0x2935c50 ||  // ProjectileAttackInfo
           vt == g_module + 0x29c4970;    // CloseCombatAttackInfo
}

void destroy_attackinfo_clone(void*& attack_info) {
    if (!is_supported_attackinfo(attack_info)) {
        attack_info = nullptr;
        return;
    }
    auto** const vt = *reinterpret_cast<void***>(attack_info);
    const auto destroy = reinterpret_cast<AttackInfoDeleteFn>(vt[0]);
    void* const old = attack_info;
    attack_info = nullptr;
    destroy(old, 1);
}

void* clone_attackinfo(void* attack_info) {
    if (!is_supported_attackinfo(attack_info)) {
        return nullptr;
    }
    auto** const vt = *reinterpret_cast<void***>(attack_info);
    const auto clone = reinterpret_cast<AttackInfoCloneFn>(vt[1]);
    return clone(attack_info);
}

void destroy_attackinfo_in_place(void* attack_info) {
    if (!is_supported_attackinfo(attack_info)) {
        return;
    }
    auto** const vt = *reinterpret_cast<void***>(attack_info);
    const auto destroy = reinterpret_cast<AttackInfoDeleteFn>(vt[0]);
    destroy(attack_info, 0);
}

bool apply_scripted_npc_death(void* target);

// Apply non-lethal damage through a fresh base AttackInfo. Static reverse and live
// testing showed that the base class lacks metadata consumed by the kill branch, so
// potentially lethal damage is routed through campaign `npc-die` instead.
bool publish_attack_on(void* source, void* target, float damage) {
    if (source == nullptr || target == nullptr) return false;
    if (readable_bytes(static_cast<std::byte*>(source) + 0xBC) < sizeof(std::uint32_t))
        return false;
    if (!is_enemy_object(target) || is_dead_ragdoll(target)) return false;
    EnemyHealthTarget health{};
    if (read_enemy_health(target, health) && health.hp > 0 &&
        damage >= static_cast<float>(health.hp)) {
        return apply_scripted_npc_death(target);
    }

    alignas(16) std::array<std::byte, 0x860> storage{};
    void* const attack_info =
        reinterpret_cast<AttackInfoConstructorFn>(g_module + 0x27A160)(storage.data(), 0);
    if (!is_supported_attackinfo(attack_info)) return false;

    auto* const bytes = static_cast<std::byte*>(attack_info);
    *reinterpret_cast<void**>(bytes + 0x08) =
        *reinterpret_cast<void**>(static_cast<std::byte*>(source) + 0xA8);
    *reinterpret_cast<std::uint32_t*>(bytes + 0x10) =
        *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(source) + 0xBC);
    *reinterpret_cast<float*>(bytes + 0x14) = damage;
    *reinterpret_cast<float*>(bytes + 0x20) = damage;   // total damage
    *reinterpret_cast<std::uint32_t*>(bytes + 0x24) = 1;  // one damage point
    *reinterpret_cast<float*>(bytes + 0x70) = damage;
    if (readable_bytes(static_cast<std::byte*>(target) + kWorldPositionOffset) >=
        sizeof(float) * 4) {
        std::memcpy(bytes + 0x30,
                    static_cast<std::byte*>(target) + kWorldPositionOffset,
                    sizeof(float) * 4);
    }
    reinterpret_cast<PublishAttackFn>(g_module + 0x27A270)(attack_info, target);
    destroy_attackinfo_in_place(attack_info);
    return true;
}

// Mirror the campaign script command `npc-die` without going through its script
// argument decoder. Unlike a lethal base AttackInfo, this is the engine's native
// forced-NPC-death sequence: remove the full health pool, then give Die() a fresh
// pointer-free HitDescription.
bool apply_scripted_npc_death(void* target) {
    if (target == nullptr || !is_enemy_object(target) || is_dead_ragdoll(target) ||
        readable_bytes(target) < 0x15D0) {
        return false;
    }

    auto** const npc_vtable = *reinterpret_cast<void***>(target);
    if (npc_vtable[0x860 / 8] != reinterpret_cast<void*>(g_module + 0x2F8B40) ||
        npc_vtable[0xD00 / 8] != reinterpret_cast<void*>(g_module + 0x1DEE1D0)) {
        return false;
    }

    void* const health = *reinterpret_cast<void**>(
        static_cast<std::byte*>(target) + kHealthSystemOffset);
    if (health == nullptr || readable_bytes(health) < 0xD0 ||
        *reinterpret_cast<std::uintptr_t*>(health) !=
            g_module + kHealthSystemVtableRva) {
        return false;
    }

    void* const controller_owner = *reinterpret_cast<void**>(
        static_cast<std::byte*>(target) + 0x15C8);
    if (controller_owner == nullptr || readable_bytes(controller_owner) < 0x80) {
        return false;
    }
    auto** const owner_vtable = *reinterpret_cast<void***>(controller_owner);
    if (owner_vtable == nullptr || readable_bytes(owner_vtable) < 0x80 ||
        owner_vtable[0x78 / 8] == nullptr) {
        return false;
    }
    using GetHitControllerFunction = void*(__fastcall*)(void*);
    void* const hit_controller = reinterpret_cast<GetHitControllerFunction>(
        owner_vtable[0x78 / 8])(controller_owner);
    constexpr std::uintptr_t kAiHitControllerVtableRva = 0x29593C0;
    if (hit_controller == nullptr || readable_bytes(hit_controller) < 0xC0 ||
        *reinterpret_cast<std::uintptr_t*>(hit_controller) !=
            g_module + kAiHitControllerVtableRva) {
        return false;
    }
    auto** const hit_vtable = *reinterpret_cast<void***>(hit_controller);
    if (hit_vtable[0xB8 / 8] != reinterpret_cast<void*>(g_module + 0x500C20)) {
        return false;
    }

    using IsDeadFunction = bool(__fastcall*)(void*);
    if (reinterpret_cast<IsDeadFunction>(npc_vtable[0x860 / 8])(target)) {
        return true;
    }
    using GetMaxHealthFunction = std::int32_t(__fastcall*)(void*);
    auto** const health_vtable = *reinterpret_cast<void***>(health);
    const std::int32_t max_health = reinterpret_cast<GetMaxHealthFunction>(
        health_vtable[0xC0 / 8])(health);
    if (max_health <= 0 || max_health > 1000000) {
        return false;
    }

    using ApplyFullHealthDamageFunction = std::int64_t(__fastcall*)(
        void*, std::int32_t, std::uint8_t, std::int32_t, std::uint8_t);
    reinterpret_cast<ApplyFullHealthDamageFunction>(npc_vtable[0xD00 / 8])(
        target, max_health, 0, 0, 0);

    alignas(16) std::array<std::byte, 0x70> hit_description{};
    const float direction[4] = {0.0F, 0.0F, 1.0F, 0.0F};
    std::memcpy(hit_description.data() + 0x10, direction, sizeof(direction));
    *reinterpret_cast<float*>(hit_description.data() + 0x48) = -1.0F;
    *reinterpret_cast<std::int32_t*>(hit_description.data() + 0x64) = max_health;
    using DieFunction = void(__fastcall*)(void*, const void*, bool);
    reinterpret_cast<DieFunction>(hit_vtable[0xB8 / 8])(
        hit_controller, hit_description.data(), true);
    return true;
}

bool publish_fresh_nadine_attackinfo() {
    void* const source = g_last_nadine_object;
    if (source == nullptr) {
        log_line("FRESH-ATTACKINFO: Nadine source is unavailable");
        return false;
    }
    if (readable_bytes(static_cast<std::byte*>(source) + kWorldPositionOffset) <
        sizeof(float) * 4) {
        log_line("FRESH-ATTACKINFO: Nadine position is unavailable");
        return false;
    }

    const auto* const source_pos = reinterpret_cast<const float*>(
        static_cast<std::byte*>(source) + kWorldPositionOffset);
    EnemyHealthTarget target{};
    float nearest_distance_sq = INFINITY;
    for_each_character([&](void* obj, const char* name) {
        if (obj == source || !is_enemy_object(obj) || is_dead_ragdoll(obj) ||
            readable_bytes(static_cast<std::byte*>(obj) + kWorldPositionOffset) <
                sizeof(float) * 4) {
            return;
        }
        EnemyHealthTarget candidate{};
        if (!find_health_system_in_object(obj, candidate) || candidate.hp <= 0) {
            return;
        }
        const auto* const pos = reinterpret_cast<const float*>(
            static_cast<std::byte*>(obj) + kWorldPositionOffset);
        const float dx = pos[0] - source_pos[0];
        const float dy = pos[1] - source_pos[1];
        const float dz = pos[2] - source_pos[2];
        const float distance_sq = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distance_sq) || distance_sq >= nearest_distance_sq) {
            return;
        }
        candidate.name = name;
        target = candidate;
        nearest_distance_sq = distance_sq;
    });
    if (target.enemy == nullptr) {
        log_line("FRESH-ATTACKINFO: no live enemy found");
        return false;
    }

    std::ostringstream before;
    before << "FRESH-ATTACKINFO: target='" << target.name
           << "' obj=0x" << std::hex << reinterpret_cast<std::uintptr_t>(target.enemy)
           << std::dec << " distance=" << std::sqrt(nearest_distance_sq)
           << " hp=" << target.hp << "/" << target.max_hp
           << "; publishing 25 damage from Nadine";
    log_line(before.str().c_str());

    const bool ok = publish_attack_on(source, target.enemy, 25.0F);
    EnemyHealthTarget after{};
    const bool health_readable = find_health_system_in_object(target.enemy, after);
    std::ostringstream result;
    result << "FRESH-ATTACKINFO: " << (ok ? "published" : "publish failed");
    if (health_readable) {
        result << "; hp " << target.hp << "->" << after.hp;
    }
    log_line(result.str().c_str());
    return ok;
}

std::uint64_t __fastcall hooked_npc_damage(void* a1, void* a2, void* a3) {
    if (a2 != nullptr) {
        // CLIENT->HOST damage relay: on the client (role B), the local player's attack
        // on an enemy lands here. Forward {enemy name-hash, damage} to the host, which
        // re-applies it authoritatively. Keep the native client damage path running as
        // well, so both copies perform the same hit/death transition.
        if (g_client_damage_relay && !g_applying_peer_enemy_death &&
            g_net_role_b && g_state_net.enabled &&
            g_state_net.peer_set && a1 != nullptr &&
            readable_bytes(a1) >= 0x20 && is_enemy_object(a1)) {
            // Client hit an enemy: relay {name-hash, damage} to the host. The original
            // handler below still applies the hit locally; dead ragdolls are excluded
            // from snapshot transform writes in apply_peer_enemies().
            if (is_supported_attackinfo(a2)) {
                float dmg = *reinterpret_cast<float*>(static_cast<std::byte*>(a2) + 0x20);
                const char* nm = reinterpret_cast<const char*>(a1) + sizeof(void*);
                if (dmg > 0.0f && dmg < 1.0e5f && readable_bytes(nm) >= 4) {
                    coop::ClientDamagePacket dp{};
                    dp.name_hash = coop_name_hash(nm, 64);
                    dp.damage = dmg;
                    sendto(g_state_net.send_socket, reinterpret_cast<const char*>(&dp),
                           sizeof(dp), 0,
                           reinterpret_cast<const sockaddr*>(&g_state_net.peer_addr),
                           sizeof(g_state_net.peer_addr));
                }
            }
        }
        std::size_t a2sz = readable_bytes(a2);
        // AttackInfo clone-capture DISABLED: clone_attackinfo (vtable[1]) crashed both
        // instances during early combat. The relay above doesn't need it (it reads the
        // damage scalar + name directly). Kept behind a default-off flag.
        if (g_capture_attackinfo_clone && g_attackinfo_template == nullptr &&
            is_supported_attackinfo(a2)) {
            g_attackinfo_template = clone_attackinfo(a2);
            if (g_attackinfo_template != nullptr) {
                g_attackinfo_original_target = a1;
                g_attackinfo_capture_time = GetTickCount64();
            }
        }
        if (g_npc_damage_capture_log) {
            static int n = 0;
            if (n < 6) { ++n;
                std::uintptr_t vt = (a1 && readable_bytes(a1) >= 8)
                    ? *reinterpret_cast<std::uintptr_t*>(a1) : 0;
                std::ostringstream l; l << "NPCDMG a1=0x" << std::hex
                    << reinterpret_cast<std::uintptr_t>(a1) << " a1vtRVA=0x"
                    << (vt > g_module ? vt - g_module : vt) << " a2=0x"
                    << reinterpret_cast<std::uintptr_t>(a2) << " a2sz=0x" << a2sz
                    << " a3=0x" << reinterpret_cast<std::uintptr_t>(a3);
                log_line(l.str().c_str());
                if (a2sz >= 0x40) {
                    const auto* b = static_cast<const std::byte*>(a2);
                    std::ostringstream d; d << "  AI bytes:";
                    for (std::size_t i = 0; i < 0x40; i += 8)
                        d << " +0x" << std::hex << i << "=0x"
                          << *reinterpret_cast<const std::uint64_t*>(b + i);
                    log_line(d.str().c_str());
                }
            }
        }
    }
    return reinterpret_cast<NpcDamageFn>(g_orig_npc_damage)(a1, a2, a3);
}
void install_npc_damage_hook_fn() {
    if (g_module == 0) return;
    const std::uintptr_t target = g_module + 0x409F70;
    g_orig_npc_damage = target;
    int patched = 0;
    for (std::uintptr_t a = g_module + 0x2930000; a < g_module + 0x2960000; a += 8) {
        auto* slot = reinterpret_cast<std::uintptr_t*>(a);
        if (readable_bytes(slot) < 8) continue;
        if (*slot != target) continue;
        DWORD oldp = 0;
        if (VirtualProtect(slot, 8, PAGE_READWRITE, &oldp)) {
            *slot = reinterpret_cast<std::uintptr_t>(&hooked_npc_damage);
            FlushInstructionCache(GetCurrentProcess(), slot, 8);
            DWORD ign = 0; VirtualProtect(slot, 8, oldp, &ign);
            ++patched;
        }
    }
    std::ostringstream l; l << "NPC-damage hook installed: patched " << patched
        << " vtable slots (sub_140409F70)";
    log_line(l.str().c_str());
}

// DIAGNOSTIC hook of AiHitController::TakeHit (vtable[17] @0x5001e0): captures real
// HitDescription bytes + the controller->owner(enemy) mapping when an enemy is hit, so
// we learn the HitDescription size and how to reach an enemy's controller for the
// client->host damage relay (Path 1). Logs the first few hits, then calls the original.
using TakeHitFn = std::uint64_t(__fastcall*)(void*, const void*);
std::uint64_t g_orig_takehit = 0;
// Install the TakeHit vtable hook? Default OFF (it fires per-frame-per-enemy and tanked
// FPS 120->20). Use the NPC-damage-handler capture below instead.
volatile bool g_install_takehit_hook = false;
// OFF: the per-hit owner/controller scan (0x8000 + VirtualQuery per qword) + dumps caused
// severe in-combat lag. Recon done (ownerOff=0x8, HitDescription layout, template). The
// cheap template/last-controller capture below still runs.
volatile bool g_takehit_probe = false;
// Captured real HitDescription (TEMPLATE with valid local pointers) + the most recent
// enemy controller, for the single-instance host-apply self-test (VK_DECIMAL).
std::array<std::byte, 0x100> g_hd_template{};
volatile bool g_hd_template_valid = false;
void* g_last_enemy_controller = nullptr;
void* g_last_enemy_owner = nullptr;
std::uint64_t __fastcall hooked_takehit(void* self, const void* hitDesc) {
    // MINIMAL hot-path capture (NO VirtualQuery — that per-call readable_bytes was the FPS
    // killer): just remember the last controller. Template/HP read happen low-rate elsewhere.
    if (self != nullptr) {
        g_last_enemy_controller = self;
        if (readable_bytes(self) >= 0x10) {
            void* owner = *reinterpret_cast<void**>(static_cast<std::byte*>(self) + 0x8);
            if (owner != nullptr && readable_bytes(owner) >= 0x20) {
                const char* name = reinterpret_cast<const char*>(owner) + 8;
                if (is_npc_enemy_name(name)) {
                    g_last_enemy_owner = owner;
                }
            }
        }
    }
    if (hitDesc != nullptr && readable_bytes(hitDesc) >= g_hd_template.size()) {
        const bool first = !g_hd_template_valid;
        std::memcpy(g_hd_template.data(), hitDesc, g_hd_template.size());
        g_hd_template_valid = true;
        if (first) {
            std::ostringstream l;
            l << "TAKEHIT template captured controller=0x" << std::hex
              << reinterpret_cast<std::uintptr_t>(self)
              << " owner=0x" << reinterpret_cast<std::uintptr_t>(g_last_enemy_owner);
            log_line(l.str().c_str());
        }
    }
    if (g_takehit_probe && self != nullptr && hitDesc != nullptr) {
        static int n = 0;
        if (n < 24) {
            ++n;
            // Find the owner enemy: a pointer in the controller whose +8 = "npc-" name.
            const char* ename = "?";
            std::size_t owner_off = 0xffff;
            if (readable_bytes(self) >= 0x90) {
                for (std::size_t off = 0; off <= 0x88; off += 8) {
                    void* p = *reinterpret_cast<void* const*>(
                        static_cast<const std::byte*>(self) + off);
                    if (p != nullptr && readable_bytes(p) >= 0x20) {
                        const char* nm = reinterpret_cast<const char*>(p) + 8;
                        if (readable_bytes(nm) >= 4 && nm[0] == 'n' && nm[1] == 'p' &&
                            nm[2] == 'c' && nm[3] == '-') {
                            ename = nm; owner_off = off; break;
                        }
                    }
                }
            }
            std::ostringstream l;
            l << "TAKEHIT self=0x" << std::hex << reinterpret_cast<std::uintptr_t>(self)
              << " hd=0x" << reinterpret_cast<std::uintptr_t>(hitDesc) << std::dec
              << " ownerOff=0x" << std::hex << owner_off
              << " enemy='" << ename << "'";
            log_line(l.str().c_str());
            // Reverse mapping enemy->controller: scan the owner enemy object for a qword
            // == self (the controller). Per-qword readable check, small range, AFTER the
            // header so the header always lands even if the scan finds nothing.
            if (owner_off != 0xffff) {
                void* owner = *reinterpret_cast<void* const*>(
                    static_cast<const std::byte*>(self) + owner_off);
                std::size_t found = 0xffffffff;
                for (std::size_t off = 0; off < 0x8000; off += 8) {
                    auto* slotp = static_cast<std::byte*>(owner) + off;
                    if (readable_bytes(slotp) < 8) break;
                    if (*reinterpret_cast<void* const*>(slotp) == self) { found = off; break; }
                }
                std::ostringstream c;
                c << "  enemyCtrlOff=0x" << std::hex << found;
                log_line(c.str().c_str());
            }
            // Dump HitDescription as qwords + float pairs (first 0xC0 bytes).
            if (readable_bytes(hitDesc) >= 0xC0) {
                const auto* b = static_cast<const std::byte*>(hitDesc);
                for (std::size_t off = 0; off < 0xC0; off += 8) {
                    std::uint64_t q = *reinterpret_cast<const std::uint64_t*>(b + off);
                    float f0, f1;
                    std::memcpy(&f0, b + off, 4);
                    std::memcpy(&f1, b + off + 4, 4);
                    std::ostringstream d;
                    d << "  HD+0x" << std::hex << off << " = 0x" << q << std::dec
                      << "  (f " << f0 << ", " << f1 << ")";
                    log_line(d.str().c_str());
                }
            }
        }
    }
    return reinterpret_cast<TakeHitFn>(g_orig_takehit)(self, hitDesc);
}

void prepare_lethal_hitdesc(std::byte* clone, std::size_t size) {
    if (size < 0x80) {
        return;
    }
    // Known live-hit captures put a damage-like float at +0x48. Touch a few
    // neighboring scalar fields too; the packet is a copied real HitDescription,
    // so its pointers/shape remain native.
    for (std::size_t off : {0x40ull, 0x44ull, 0x48ull, 0x4Cull, 0x50ull}) {
        *reinterpret_cast<float*>(clone + off) = 9999.0f;
    }
}

void log_last_enemy_after_call(const char* tag) {
    std::uintptr_t owner_vt = 0;
    bool owner_dead = false;
    std::int32_t hp = -999999;
    std::int32_t max_hp = -999999;
    if (g_last_enemy_owner != nullptr && readable_bytes(g_last_enemy_owner) >= 8) {
        owner_vt = *reinterpret_cast<std::uintptr_t*>(g_last_enemy_owner);
        owner_dead = owner_vt == g_module + kRagdollVtableRva;
        EnemyHealthTarget health{};
        if (find_health_system_in_object(g_last_enemy_owner, health)) {
            hp = health.hp;
            max_hp = health.max_hp;
        }
    }
    std::ostringstream line;
    line << tag << ": owner_vt_rva=0x" << std::hex
         << (owner_vt > g_module ? owner_vt - g_module : 0)
         << std::dec << " dead=" << (owner_dead ? 1 : 0)
         << " hp=" << hp << " max=" << max_hp;
    log_line(line.str().c_str());
}

void takehit_selftest_from_last_hit() {
    log_line("TAKEHIT-SELFTEST disabled: repeat TakeHit crashed in live test");
}

void die_selftest_from_last_hit(bool force) {
    log_line(force ? "DIE-SELFTEST disabled: Die(true) crashed in live test"
                   : "DIE-SELFTEST disabled: Die(false) only knocks down, no kill");
}

void combo_kill_selftest_from_last_hit() {
    log_line("COMBO-SELFTEST disabled: repeat TakeHit crashed in live test");
}

// HOST (role A): enumerate enemies and send their transforms to the client.
void send_enemy_states() {
    if (!g_enemy_sync || !g_state_net.enabled || !g_state_net.peer_set) return;
    if (!g_state_net.has_peer_state.load()) return;  // no real peer -> skip enumeration
    if (g_net_role_b) return;  // only the authoritative host sends enemies
    coop::EnemyPacket pkt{};
    int n = 0;
    for_each_character([&](void* obj, const char* name) {
        if (n >= coop::kMaxEnemies || !is_enemy_object(obj)) return;
        const auto* const position = reinterpret_cast<const float*>(
            static_cast<std::byte*>(obj) + kWorldPositionOffset);
        if (!valid_network_position(position)) return;
        auto& es = pkt.enemies[n];
        es.name_hash = coop_name_hash(name, 64);
        std::memcpy(es.pos,
                    static_cast<std::byte*>(obj) + kWorldPositionOffset, sizeof(es.pos));
        std::memcpy(es.quat,
                    static_cast<std::byte*>(obj) + kPlayerQuatOffset, sizeof(es.quat));
        es.flags = is_dead_ragdoll(obj) ? coop::kEnemyFlagDead : 0;
        EnemyHealthTarget eh{};
        es.hp = read_enemy_health(obj, eh) ? eh.hp : -1;  // host current HP
        ++n;
    });
    pkt.count = static_cast<std::uint16_t>(n);
    pkt.sequence = ++g_state_net.enemy_send_seq;
    pkt.timestamp_ms = static_cast<std::uint32_t>(GetTickCount64());
    const int bytes = static_cast<int>(offsetof(coop::EnemyPacket, enemies) +
                                       static_cast<std::size_t>(n) * sizeof(coop::EnemyState));
    sendto(g_state_net.send_socket, reinterpret_cast<const char*>(&pkt), bytes, 0,
           reinterpret_cast<const sockaddr*>(&g_state_net.peer_addr),
           sizeof(g_state_net.peer_addr));
}

std::unordered_map<void*, bool> g_enemy_ai_disabled;  // disable each enemy's AI once
std::unordered_map<void*, bool> g_enemy_death_applied;  // consume each host death once

// CLIENT (role B): match local enemies to host snapshot by name-hash and apply the
// host transform. Pause local AI only after all unguarded RequestDisableAi pointers
// are ready; otherwise leave this actor untouched until a later frame.
void apply_peer_enemies() {
    if (!g_enemy_sync || !g_net_role_b) return;
    if (!g_state_net.enabled) return;
    coop::EnemyPacket ep{};
    if (!copy_peer_enemies_snapshot(ep)) return;
    int applied = 0;
    for_each_character([&](void* obj, const char* name) {
        // A locally completed native death owns its ragdoll. Moving or overwriting it
        // from a host snapshot destabilizes the physics/death pipeline.
        if (!is_enemy_object(obj) || is_dead_ragdoll(obj)) return;
        if (g_pause_client_enemy_ai && !g_enemy_ai_disabled[obj]) {
            if (!npc_ai_control_ready(obj)) return;
            reinterpret_cast<RequestDisableAiFunction>(
                g_module + kRequestDisableAiRva)(obj, false, true);
            g_enemy_ai_disabled[obj] = true;
            std::ostringstream line;
            line << "ENEMY-SYNC client AI paused for '" << name << "'";
            log_line(line.str().c_str());
        }
        const std::uint32_t h = coop_name_hash(name, 64);
        for (int i = 0; i < ep.count && i < coop::kMaxEnemies; ++i) {
            if (ep.enemies[i].name_hash != h) continue;
            if (!valid_network_position(ep.enemies[i].pos)) continue;
            auto* world = reinterpret_cast<float*>(
                static_cast<std::byte*>(obj) + kWorldPositionOffset);
            auto* localp = reinterpret_cast<float*>(
                static_cast<std::byte*>(obj) + kPlayerLocalPosOffset);
            auto* quat = reinterpret_cast<float*>(
                static_cast<std::byte*>(obj) + kPlayerQuatOffset);
            const bool dead = (ep.enemies[i].flags & coop::kEnemyFlagDead) != 0;
            if (dead) {
                // Consume the host's death once through the same HealthSystem +
                // AiHitController sequence used by the campaign `npc-die` command.
                if (g_enable_client_death_publish && !g_enemy_death_applied[obj]) {
                    g_applying_peer_enemy_death = true;
                    const bool killed = apply_scripted_npc_death(obj);
                    g_applying_peer_enemy_death = false;
                    if (killed) g_enemy_death_applied[obj] = true;
                    std::ostringstream line;
                    line << "ENEMY-SYNC client scripted death "
                         << (killed ? "applied" : "not ready")
                         << " obj=0x" << std::hex
                         << reinterpret_cast<std::uintptr_t>(obj);
                    log_line(line.str().c_str());
                }
            } else {
                g_enemy_death_applied.erase(obj);
                std::memcpy(world, ep.enemies[i].pos, sizeof(ep.enemies[i].pos));
                std::memcpy(localp, ep.enemies[i].pos, sizeof(ep.enemies[i].pos));
                std::memcpy(quat, ep.enemies[i].quat, sizeof(ep.enemies[i].quat));
                // Pin this puppet's HP to the host's authoritative value so the client
                // shows correct HP and never kills the puppet locally (host is authority).
                if (ep.enemies[i].hp >= 0) {
                    EnemyHealthTarget eh{};
                    if (read_enemy_health(obj, eh)) {
                        *reinterpret_cast<std::int32_t*>(
                            static_cast<std::byte*>(eh.health) + kCurrentHpOffset) =
                            ep.enemies[i].hp;
                    }
                }
            }
            ++applied;
            break;
        }
    });
    static ULONGLONG last_diag = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last_diag > 1000) {
        last_diag = now;
        std::ostringstream l;
        l << "ENEMY-SYNC client applied " << applied << "/" << ep.count
          << " (seq " << ep.sequence << ")";
        log_line(l.str().c_str());
    }
}

// Single-instance self-test: exercise the enemy client-apply path (enumerate +
// RequestDisableAi + transform write) by freezing every enemy at its own current
// position. If enemies cleanly stop with AI off and no crash, the apply path is
// validated; the two-instance test then only adds the (low-risk) network transport.
std::unordered_map<void*, std::array<float, 8>> g_enemy_freeze_pose;  // pos[4]+quat[4]
void enemy_freeze_selftest() {
    int frozen = 0;
    for_each_character([&](void* obj, const char* /*name*/) {
        if (!is_enemy_object(obj)) return;
        if (!g_enemy_ai_disabled[obj]) {
            const auto disable = reinterpret_cast<RequestDisableAiFunction>(
                g_module + kRequestDisableAiRva);
            disable(obj, false, true);
            g_enemy_ai_disabled[obj] = true;
        }
        // Capture each enemy's pose ONCE, then overwrite to that FIXED pose every
        // frame so the write actually fights the engine (real apply-path test).
        auto it = g_enemy_freeze_pose.find(obj);
        if (it == g_enemy_freeze_pose.end()) {
            std::array<float, 8> pose{};
            std::memcpy(pose.data(), static_cast<std::byte*>(obj) + kWorldPositionOffset, 16);
            std::memcpy(pose.data() + 4, static_cast<std::byte*>(obj) + kPlayerQuatOffset, 16);
            it = g_enemy_freeze_pose.emplace(obj, pose).first;
        }
        const float* pose = it->second.data();
        std::memcpy(static_cast<std::byte*>(obj) + kWorldPositionOffset, pose, 16);
        std::memcpy(static_cast<std::byte*>(obj) + kPlayerLocalPosOffset, pose, 16);
        std::memcpy(static_cast<std::byte*>(obj) + kPlayerQuatOffset, pose + 4, 16);
        ++frozen;
    });
    static ULONGLONG last_diag = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last_diag > 1000) {
        last_diag = now;
        std::ostringstream l;
        l << "ENEMY-FREEZE selftest: froze " << frozen << " enemies (AI off + pinned)";
        log_line(l.str().c_str());
    }
}

// HEALTH PROBE: watch enemy fields for any 4-byte value that DECREASES (health drops
// on a hit). OFF now — the inline brute scan was too noisy and did NOT reveal HP
// (health lives in a damageable sub-component, not inline). Use static RE of the
// damage/death function instead.
volatile bool g_enemy_health_probe = false;
constexpr std::size_t kHpLo = 0x80, kHpHi = 0x1200;
std::unordered_map<void*, std::array<std::uint32_t, (kHpHi - kHpLo) / 4>> g_hp_snap;
void enemy_health_probe() {
    for_each_character([&](void* obj, const char* /*name*/) {
        if (!is_enemy_object(obj)) return;
        auto* base = static_cast<std::byte*>(obj);
        // Per-object readable extent (objects may be shorter than kHpHi).
        const std::size_t avail = readable_bytes(base);
        const std::size_t hi = (avail < kHpHi) ? (avail & ~std::size_t{3}) : kHpHi;
        if (hi <= kHpLo) return;
        auto it = g_hp_snap.find(obj);
        if (it == g_hp_snap.end()) {  // baseline this enemy
            std::array<std::uint32_t, (kHpHi - kHpLo) / 4> s{};
            for (std::size_t off = kHpLo; off < hi; off += 4)
                s[(off - kHpLo) / 4] = *reinterpret_cast<std::uint32_t*>(base + off);
            g_hp_snap.emplace(obj, s);
            return;
        }
        auto& snap = it->second;
        for (std::size_t off = kHpLo; off < hi; off += 4) {
            if (off >= 0x120 && off < 0x2b0) continue;  // skip transform/velocity churn
            std::uint32_t& prev = snap[(off - kHpLo) / 4];
            std::uint32_t cur = *reinterpret_cast<std::uint32_t*>(base + off);
            if (cur == prev) continue;
            float pf, cf; std::memcpy(&pf, &prev, 4); std::memcpy(&cf, &cur, 4);
            // Death catch: any nonzero field that becomes exactly 0 (or float 0).
            const bool zeroed = prev != 0 && (cur == 0 || cf == 0.0f) &&
                                (pf > 1.0f || (prev > 0 && prev <= 1000000u));
            const bool floatdrop = pf >= 2.0f && pf <= 1.0e6f && cf < pf - 1.0f && cf >= 0.0f;
            const bool intdrop = prev > 0 && prev <= 1000000u && cur < prev && (prev - cur) <= 100u;
            if (zeroed || floatdrop || intdrop) {
                std::ostringstream l;
                l << "HP-PROBE enemy=0x" << std::hex << reinterpret_cast<std::uintptr_t>(obj)
                  << " +0x" << off << std::dec
                  << " : " << prev << "->" << cur << " (f " << pf << "->" << cf << ")";
                log_line(l.str().c_str());
            }
            prev = cur;
        }
    });
}

// Low-rate HealthSystem probe: for the first live enemy, read *(enemy+0x16a0) = its
// HealthSystem (0xe0 bytes) and log any plausible float field that CHANGES (the current
// HP drops when the player shoots). Low rate => no FPS hit. Default ON for this build.
volatile bool g_hp_sys_probe = false;  // HP located (enemy+0x4a8 -> HS, HP int@+0x3c); heavy scan off
bool plausible_ptr(std::uintptr_t v) {
    return v >= 0x10000000ull && v < 0x800000000000ull && (v & 7) == 0;
}
void enemy_hp_probe() {
    void* enemy = nullptr;
    for_each_character([&](void* obj, const char* /*nm*/) {
        if (enemy == nullptr && is_enemy_object(obj) && !is_dead_ragdoll(obj)) enemy = obj;
    });
    if (enemy == nullptr) return;
    const std::uintptr_t hsvt = g_module + 0x2bbe128;
    // 2-level scan from the enemy for a HealthSystem (vtable hsvt). HP = int at HS+0x3c.
    static void* found_hs = nullptr;
    static std::int32_t snap_hp = -999;
    if (found_hs == nullptr) {
        const std::size_t avail = readable_bytes(enemy);
        const std::size_t hi = avail < 0x4000 ? avail : 0x4000;
        auto* base = static_cast<std::byte*>(enemy);
        for (std::size_t o1 = 0x10; o1 + 8 <= hi && found_hs == nullptr; o1 += 8) {
            std::uintptr_t v1 = *reinterpret_cast<std::uintptr_t*>(base + o1);
            if (!plausible_ptr(v1)) continue;
            auto* p1 = reinterpret_cast<std::byte*>(v1);
            if (readable_bytes(p1) < 0xe0) continue;
            if (*reinterpret_cast<std::uintptr_t*>(p1) == hsvt) {
                std::int32_t curhp = *reinterpret_cast<std::int32_t*>(p1 + 0x3c);
                if (curhp <= 0) continue;  // skip dead/downed; lock onto a LIVE enemy
                found_hs = p1;
                std::ostringstream l; l << "HPC HS direct enemy+0x" << std::hex << o1
                    << " hs=0x" << v1 << " HP@+0x3c=" << std::dec << curhp
                    << " max@+0x44=" << *reinterpret_cast<std::int32_t*>(p1 + 0x44);
                log_line(l.str().c_str()); break;
            }
            // level 2: scan p1's first 0x300 for a HealthSystem ptr.
            const std::size_t a2 = readable_bytes(p1);
            const std::size_t hi2 = a2 < 0x300 ? a2 : 0x300;
            for (std::size_t o2 = 0; o2 + 8 <= hi2; o2 += 8) {
                std::uintptr_t v2 = *reinterpret_cast<std::uintptr_t*>(p1 + o2);
                if (!plausible_ptr(v2)) continue;
                auto* p2 = reinterpret_cast<std::byte*>(v2);
                if (readable_bytes(p2) < 0xe0) continue;
                if (*reinterpret_cast<std::uintptr_t*>(p2) == hsvt) {
                    found_hs = p2;
                    std::ostringstream l; l << "HPC HS 2lvl enemy+0x" << std::hex << o1
                        << "->+0x" << o2 << " hs=0x" << v2 << " HP@+0x3c=" << std::dec
                        << *reinterpret_cast<std::int32_t*>(p2 + 0x3c)
                        << " max@+0x44=" << *reinterpret_cast<std::int32_t*>(p2 + 0x44);
                    log_line(l.str().c_str()); break;
                }
            }
        }
        if (found_hs == nullptr) {
            static bool warned = false; if (!warned) { warned = true;
                log_line("HPC: HealthSystem not found in enemy (2-level scan)"); }
        }
        return;
    }
    // watch HP int at HS+0x3c
    if (readable_bytes(static_cast<std::byte*>(found_hs) + 0x48) < 8) { found_hs = nullptr; return; }
    std::int32_t hp = *reinterpret_cast<std::int32_t*>(static_cast<std::byte*>(found_hs) + 0x3c);
    if (hp != snap_hp) {
        std::ostringstream l; l << "HPC HP " << snap_hp << " -> " << hp; log_line(l.str().c_str());
        snap_hp = hp;
    }
}

// True only when the local player is a fully-constructed Player. During a
// checkpoint/area reload (which the user can trigger by walking into a transition,
// or that fires on alt-tab/minimize in some scenes) the engine FREES and recreates
// the Player object; the retained-slot table transiently points at a half-torn-down
// object whose vtable is null, or at a brand-new object. Dereferencing game state in
// that window (enemy sync, state send, native publish) reads/walks freed memory and
// crashes. Gate every per-tick world action on this.
bool world_stable() {
    void* p = retained_player_object(0);
    if (p == nullptr || readable_bytes(p) < 0x300) return false;
    return *reinterpret_cast<std::uintptr_t*>(p) == g_module + kPlayerVtableRva;
}

bool mirror_local_animation_snapshot_to_clone() {
    void* const source = retained_player_object(0);
    void* const clone = retained_player_object(1);
    if (source == nullptr || clone == nullptr ||
        readable_bytes(source) < 0x3FD0 || readable_bytes(clone) < 0x1B38) {
        return false;
    }

    void* anim_source = *reinterpret_cast<void**>(
        static_cast<std::byte*>(source) + 0x3FC8);
    if (anim_source == nullptr) {
        // Player::Init normally calls this builder, but it allocates the cache
        // only when the multiplayer-mode byte is set. Raise that one byte only
        // for the synchronous builder call, then restore campaign state.
        auto* const multiplayer_gate = reinterpret_cast<std::uint8_t*>(
            g_module + 0x3E07473);
        const std::uint8_t saved_gate = *multiplayer_gate;
        *multiplayer_gate = 1;
        reinterpret_cast<InitializePlayerNetAnimArtGroupsFunction>(
            g_module + kInitializePlayerNetAnimArtGroupsRva)(source);
        *multiplayer_gate = saved_gate;
        anim_source = *reinterpret_cast<void**>(
            static_cast<std::byte*>(source) + 0x3FC8);

        unsigned populated = 0;
        if (anim_source != nullptr && readable_bytes(anim_source) >= 0x100) {
            const auto* groups = static_cast<void* const*>(anim_source);
            for (unsigned index = 0; index < 32; ++index) {
                populated += groups[index] != nullptr ? 1u : 0u;
            }
        }
        std::ostringstream line;
        line << "ANIM-MIRROR art groups built: ptr=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(anim_source) << std::dec
             << " populated=" << populated << "/32";
        log_line(line.str().c_str());
    }
    void* const engine_clock = *reinterpret_cast<void**>(
        g_module + kEngineClockPointerRva);
    // FillAnimCmdBitStream requires the multiplayer-populated pArtGroups cache.
    // Campaign Players leave +0x3FC8 null; calling the serializer in that state
    // crashes in SelectSpawnerOrGroupEntryByValue.
    if (anim_source == nullptr || readable_bytes(anim_source) < 0x100 ||
        engine_clock == nullptr ||
        readable_bytes(engine_clock) < 0x318) {
        return false;
    }

    // Campaign data wall: FillAnimCmdBitStream -> SelectSpawnerOrGroupEntryByValue
    // derefs *(*(entry+0x48)+0x10) and then *v14 for every NON-null art-group
    // entry. The MP net-anim art-defs resolve by id in single-player but come back
    // structurally incomplete (entry+0x48 sub-table absent). Validate the exact
    // chain the serializer walks; if any populated entry is malformed, bail with a
    // diagnostic instead of crashing the game.
    {
        const auto* const groups = static_cast<void* const*>(anim_source);
        unsigned populated = 0;
        unsigned malformed = 0;
        for (unsigned index = 0; index < 32; ++index) {
            void* const entry = groups[index];
            if (entry == nullptr) continue;
            ++populated;
            if (readable_bytes(entry) < 0x50) { ++malformed; continue; }
            void* const sub = *reinterpret_cast<void* const*>(
                static_cast<const std::byte*>(entry) + 0x48);
            if (sub == nullptr || readable_bytes(sub) < 0x18) {
                ++malformed;
                continue;
            }
            void* const arr = *reinterpret_cast<void* const*>(
                static_cast<const std::byte*>(sub) + 0x10);
            if (arr == nullptr || readable_bytes(arr) < 0x4) ++malformed;
        }
        if (populated == 0 || malformed != 0) {
            static bool wall_logged = false;
            if (!wall_logged) {
                wall_logged = true;
                std::ostringstream line;
                line << "ANIM-MIRROR campaign art-group wall: populated="
                     << populated << "/32 malformed=" << malformed
                     << " (entry+0x48 sub-table missing in single-player) -> "
                        "FillAnimCmdBitStream skipped, no crash";
                log_line(line.str().c_str());
            }
            return false;
        }
    }

    alignas(32) std::array<std::byte, 0x610> move_event{};
    reinterpret_cast<InPlaceCtorFunction>(
        g_module + kNetEventCharacterMoveCtorRva)(move_event.data());

    const auto ticks = *reinterpret_cast<const std::uint64_t*>(
        static_cast<const std::byte*>(engine_clock) + 0x310);
    const float scale = *reinterpret_cast<const float*>(
        g_module + kEngineClockScaleRva);
    const float engine_time = static_cast<float>(ticks) * scale;
    *reinterpret_cast<float*>(move_event.data() + 0x18) = engine_time;
    *reinterpret_cast<std::uint32_t*>(move_event.data() + 0x220) |= 1u;

    struct BitStream {
        void* data;
        std::uint32_t bit_position;
        std::uint32_t bit_capacity;
        std::uint32_t status;
        std::uint32_t reserved;
    } bit_stream{move_event.data() + 0x1C, 0, 4096, 0, 0};

    const std::uint32_t bit_count =
        reinterpret_cast<FillAnimCmdBitStreamFunction>(
            g_module + kFillAnimCmdBitStreamRva)(
                move_event.data(), &bit_stream, source, anim_source, 1, 7);
    if (bit_count == 0 || bit_count > 4096) {
        static bool invalid_logged = false;
        if (!invalid_logged) {
            invalid_logged = true;
            std::ostringstream line;
            line << "ANIM-MIRROR rejected bit count=" << bit_count;
            log_line(line.str().c_str());
        }
        return false;
    }
    *reinterpret_cast<std::uint32_t*>(move_event.data() + 0x21C) = bit_count;

    alignas(32) std::array<std::byte, 0x720> snapshot{};
    reinterpret_cast<InPlaceCtorFunction>(
        g_module + kNetSnapshotCtorRva)(snapshot.data());
    reinterpret_cast<CharacterMoveToSnapshotFunction>(
        g_module + kCharacterMoveToSnapshotRva)(
            move_event.data(), snapshot.data());
    reinterpret_cast<NetAnimAddSnapshotFunction>(
        g_module + kNetAnimAddSnapshotRva)(
            static_cast<std::byte*>(clone) + 0x1B30, snapshot.data());

    static ULONGLONG last_log = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last_log >= 1000) {
        last_log = now;
        std::ostringstream line;
        line << "ANIM-MIRROR native snapshot queued: bits=" << bit_count
             << " bytes=" << ((bit_count + 7) >> 3)
             << " time=" << engine_time;
        log_line(line.str().c_str());
    }
    return true;
}

void net_tick() {
    // Reload guard: do nothing while the local player is being recreated.
    if (!world_stable()) return;
    const bool foreground = process_owns_foreground_window();
    // hooked_update fires for every Player + the buddy each frame; rate-limit by clock.
    static ULONGLONG last_send = 0, last_enemy_send = 0, last_enemy_apply = 0;
    static ULONGLONG last_freeze = 0, last_hp_probe = 0, last_hpsys = 0;
    static ULONGLONG last_animation_mirror = 0;
    const ULONGLONG now = GetTickCount64();
    if (foreground && (GetAsyncKeyState(VK_SCROLL) & 1) != 0) {
        g_local_animation_mirror_test = !g_local_animation_mirror_test;
        log_line(g_local_animation_mirror_test
                     ? "ANIM-MIRROR ON: local Player -> slot-1 clone"
                     : "ANIM-MIRROR OFF");
    }
    if (g_local_animation_mirror_test && now - last_animation_mirror >= 33) {
        last_animation_mirror = now;
        mirror_local_animation_snapshot_to_clone();
    }
    if (foreground) monitor_hp_kill_selftest(now);
    if (foreground && g_hp_sys_probe && now - last_hpsys >= 150) {
        last_hpsys = now;
        enemy_hp_probe();
    }
    if (foreground && g_enemy_health_probe && now - last_hp_probe >= 100) {
        last_hp_probe = now;
        enemy_health_probe();
    }
    if (foreground && g_enemy_freeze_selftest && now - last_freeze >= 16) {
        last_freeze = now;
        enemy_freeze_selftest();
    }
    if (now - last_send >= 16) {  // ~60 Hz local player state
        last_send = now;
        send_local_player_state();
    }
    if (!g_net_role_b) {
        if (now - last_enemy_send >= 33) {  // ~30 Hz host enemy snapshot
            last_enemy_send = now;
            send_enemy_states();
        }
        // HOST: drain client->host damage events and apply them authoritatively.
        if (g_client_damage_relay) {
            std::uint32_t head = g_client_dmg_head.load(std::memory_order_acquire);
            int applied = 0;
            while (g_client_dmg_tail != head && applied < 64) {
                ClientDmgEvent ev = g_client_dmg_ring[g_client_dmg_tail % 64];
                ++g_client_dmg_tail; ++applied;
                void* tgt = nullptr;
                for_each_character([&](void* obj, const char* name) {
                    if (tgt == nullptr && is_enemy_object(obj) && !is_dead_ragdoll(obj) &&
                        coop_name_hash(name, 64) == ev.name_hash) {
                        tgt = obj;
                    }
                });
                if (tgt != nullptr && g_last_nadine_object != nullptr) {
                    publish_attack_on(g_last_nadine_object, tgt, ev.damage);
                }
            }
        }
    } else {
        if (now - last_enemy_apply >= 16) {
            last_enemy_apply = now;
            apply_peer_enemies();
        }
    }
}

// Background thread for diagnostics that must work regardless of whether a
// NadineBuddy is in the scene (the per-frame hook only fires for that object).
// Handles the velocity-field probe, the injected-stick update, and the INSERT
// toggle independently of the game thread (read-only on game memory).
// Drive the puppet's locomotion INPUT with a forward run magnitude proportional
// to the peer's speed, so the engine plays the run/walk animation (replicating
// the velocity OUTPUT field +0x260/+0x270 does NOT animate it). The transform
// apply still forces the correct world position on top.
void feed_puppet_run_anim(const coop::StatePacket& pkt) {
    if (g_module == 0) return;
    void* fs = *reinterpret_cast<void**>(g_module + 0x3FE1088);
    if (fs == nullptr || readable_bytes(fs) < 0x7100) return;
    // slot-1 index activation + clean real-input branch.
    auto* slotN_index = static_cast<std::uint8_t*>(fs) + 0xE88 + 0xBF0 + 0x18;
    if (readable_bytes(slotN_index) >= 1) *slotN_index = 1;
    auto* spin = reinterpret_cast<std::uint8_t*>(static_cast<std::byte*>(fs) + 0x70B8 + 1);
    *spin = 0;
    auto* b1 = static_cast<std::byte*>(fs) + 0x6FC8 + 0x28;
    std::memset(b1, 0, 0x28);
    b1[0] = std::byte{1};
    *reinterpret_cast<float*>(b1 + 0x0C) = 1.0e9F;  // duration
    const float speed = std::sqrt(pkt.velocity[0] * pkt.velocity[0] +
                                  pkt.velocity[2] * pkt.velocity[2]);
    const float mag = speed > 6.0F ? 1.0F : speed / 6.0F;  // ~6 m/s = full run
    // Forward in the block convention (block+0x04 = -1 = forward). Magnitude
    // drives the locomotion speed -> run/walk anim blend.
    *reinterpret_cast<float*>(b1 + 0x14) = 0.0F;
    *reinterpret_cast<float*>(b1 + 0x18) = -mag;
    *reinterpret_cast<std::uint32_t*>(b1 + 0x10) = (mag > 0.05F) ? 0x500u : 0u;
}

// State-field probe: log Chloe's (slot 0) DWORDs that CHANGE, so the locomotion/
// anim STATE id can be spotted (discrete change at idle<->run<->jump transitions,
// vs floats that churn every frame). Scans player + PlayerSnapshot(+0xB0).
volatile bool g_state_probe_enabled = false;
void state_probe() {
    static std::array<std::uint32_t, 0x300 / 4> prev_p{};
    static std::array<std::uint32_t, 0x300 / 4> prev_s{};
    static bool have = false;
    static ULONGLONG last = 0;
    void* p = retained_player_object(0);
    if (p == nullptr || readable_bytes(p) < 0x320) return;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 120) return;
    last = now;
    auto* pp = reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(p) + 0x100);
    void* snap = (readable_bytes(static_cast<std::byte*>(p) + 0xB8) >= 8)
                     ? *reinterpret_cast<void**>(static_cast<std::byte*>(p) + 0xB0)
                     : nullptr;
    auto* sp = (snap && readable_bytes(snap) >= 0x320)
                   ? reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(snap) + 0x100)
                   : nullptr;
    if (!have) {
        std::memcpy(prev_p.data(), pp, prev_p.size() * 4);
        if (sp) std::memcpy(prev_s.data(), sp, prev_s.size() * 4);
        have = true;
        return;
    }
    std::ostringstream line;
    line << "stateprobe P:";
    int shown = 0;
    for (std::size_t i = 0; i < prev_p.size() && shown < 24; ++i) {
        if (pp[i] != prev_p[i]) {
            // Skip values that look like live floats (small exponent churn): show
            // ints/hashes (high bits set) and small enums; cheap heuristic = show
            // all, prefixed hex; the discrete ones stand out across the capture.
            line << " +0x" << std::hex << (0x100 + i * 4) << "(" << prev_p[i]
                 << "->" << pp[i] << ")" << std::dec;
            ++shown;
        }
        prev_p[i] = pp[i];
    }
    if (sp) {
        line << " | S:";
        shown = 0;
        for (std::size_t i = 0; i < prev_s.size() && shown < 24; ++i) {
            if (sp[i] != prev_s[i]) {
                line << " +0x" << std::hex << (0x100 + i * 4) << "(" << prev_s[i]
                     << "->" << sp[i] << ")" << std::dec;
                ++shown;
            }
            prev_s[i] = sp[i];
        }
    }
    log_line(line.str().c_str());
}

// Diagnostic: dump the 2-entry screen camera table (0x14697AD50) and the result
// of CameraManager::Get(0/1), to confirm screen 1's camera is null (no 2nd-player
// camera basis) before attempting to activate it.
void log_screen_camera_state() {
    // Read the table directly (each entry is 16 bytes: ptr @+0, id @+8). Avoid
    // calling CameraManager::Get(1) which asserts/__debugbreaks on a null entry.
    auto* table = reinterpret_cast<std::uint64_t*>(g_module + 0x697AD50);
    std::ostringstream l;
    l << "SCREENCAM e0.ptr=0x" << std::hex << table[0] << " e0.id=" << table[1]
      << " | e1.ptr=0x" << table[2] << " e1.id=" << table[3] << std::dec;
    log_line(l.str().c_str());
    // Dump the route-entity child-list area (+0xF0..+0x118) for both screens so
    // we can find where the active camera child pointer lives (screen 0 has one,
    // screen 1 is empty).
    for (int s = 0; s < 2; ++s) {
        const std::uint64_t re = table[s * 2];
        if (re == 0 || readable_bytes(reinterpret_cast<void*>(re)) < 0x120) continue;
        auto* q = reinterpret_cast<std::uint64_t*>(re);
        std::ostringstream d;
        d << "SCREENCAM route" << s << " @0x" << std::hex << re << ":";
        for (int off = 0xE0; off <= 0x118; off += 8) {
            d << " +0x" << off << "=0x" << q[off / 8];
        }
        log_line(d.str().c_str());
    }
    // Per-player camera-control owner: player+0x36C0; its screen index = owner+0x84.
    for (int slot = 0; slot < 2; ++slot) {
        void* p = retained_player_object(slot);
        std::ostringstream d;
        d << "CAMOWNER slot" << slot << " player=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(p) << std::dec;
        if (p != nullptr && readable_bytes(static_cast<std::byte*>(p) + 0x36C8) >= 8) {
            void* owner = *reinterpret_cast<void**>(static_cast<std::byte*>(p) + 0x36C0);
            d << " +0x36C0(owner)=0x" << std::hex
              << reinterpret_cast<std::uintptr_t>(owner) << std::dec;
            if (owner != nullptr && readable_bytes(static_cast<std::byte*>(owner) + 0x88) >= 4) {
                d << " owner+0x84(screenIdx)="
                  << *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(owner) + 0x84);
            }
        }
        log_line(d.str().c_str());
    }
}

// Experiment: the slot-1 puppet already HAS a camera-control owner (player+0x36C0)
// with screen index 1, but screen 1's gameplay camera is never materialized
// (the update isn't called for the puppet). Call
// UpdatePlayerAnimatedCameraControlOwnerAndRouteState(owner) manually to
// materialize screen 1's camera -> give the puppet's locomotion a real yaw.
void materialize_screen1_camera_once() {
    void* puppet = retained_player_object(1);
    if (puppet == nullptr) { log_line("screen1cam: no puppet"); return; }
    if (readable_bytes(static_cast<std::byte*>(puppet) + 0x36C8) < 8) return;
    void* owner = *reinterpret_cast<void**>(static_cast<std::byte*>(puppet) + 0x36C0);
    if (owner == nullptr) { log_line("screen1cam: no owner"); return; }
    using Fn = char(__fastcall*)(void*);
    const auto fn = reinterpret_cast<Fn>(g_module + 0x238800);
    const char r = fn(owner);
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last > 1000) {  // rate-limit when driven continuously
        last = now;
        std::ostringstream l;
        l << "screen1cam: UpdatePlayerAnimCamCtrl(owner=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(owner) << ") ret=" << std::dec
          << static_cast<int>(r);
        log_line(l.str().c_str());
    }
}

DWORD WINAPI aux_input_thread(void*) {
    bool prev_insert = false;
    bool prev_swap = false;
    bool prev_scan = false;
    bool prev_dump = false;
    bool prev_cam = false;
    std::uintptr_t dumped_inputstate = 0;
    while (true) {
        if (g_inputstate_addr != 0 && g_inputstate_addr != dumped_inputstate) {
            dumped_inputstate = g_inputstate_addr;
            dump_inputstate_neighborhood();
        }
        // Net co-op drive (reversed 2026-06-30): the clone animates from its OWN
        // locomotion graph fed by the peer's per-slot input (slot 1), while
        // apply_puppet_transform_post holds the authoritative position via a SOFT
        // correction (not a hard pin) so the input-driven motion isn't frozen.
        // g_puppet_net_apply gates BOTH the slot-1 net feed
        // (drive_remote_slot1_actions_tick) and the position application. The run
        // animation comes from the fed stick MAGNITUDE; facing comes from the
        // replicated quaternion. We do NOT drive the clone's camera owner
        // (sub_140238800) — that materializes a heavy second gameplay camera.
        if (retained_player_object(1) != nullptr && !g_play_as_clone) {
            g_puppet_net_apply = true;    // soft position + slot-1 input feed
            g_drive_slot1 = false;        // no manual WASD leak; the net feed drives it
            g_drive_slot_index = 1;
        }
        const bool ins = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (ins && !prev_insert) {
            g_velprobe_enabled = !g_velprobe_enabled;
            log_line(g_velprobe_enabled
                         ? "Velocity-field probe ENABLED (run; reads slot0 + controller)"
                         : "Velocity-field probe disabled");
        }
        prev_insert = ins;
        const bool pin_key = (GetAsyncKeyState('P') & 0x8000) != 0;
        static bool prev_pin_key = false;
        if (pin_key && !prev_pin_key) {
            g_clone_freerun = !g_clone_freerun;
            log_line(g_clone_freerun
                         ? "CLONE FREE-RUN ON (no position pin; input-driven only)"
                         : "CLONE FREE-RUN OFF (soft-lerp position authority)");
        }
        prev_pin_key = pin_key;
        const bool sw = (GetAsyncKeyState(VK_MULTIPLY) & 0x8000) != 0;
        if (sw && !prev_swap) {
            swap_local_player_slots();
        }
        prev_swap = sw;
        const bool cam = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        if (cam && !prev_cam) {
            toggle_main_camera_focus();
        }
        prev_cam = cam;
        const bool scn = (GetAsyncKeyState(VK_DIVIDE) & 0x8000) != 0;
        if (scn && !prev_scan) {
            scan_for_player_pointers();
        }
        prev_scan = scn;
        const bool dmp = (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0;
        if (dmp && !prev_dump) {
            dump_focus_consumer();
            dump_character_fields();
        }
        prev_dump = dmp;
        const bool camk = (GetAsyncKeyState(VK_ADD) & 1) != 0;
        if (camk) {
            g_drive_slot1 = !g_drive_slot1;
            log_line(g_drive_slot1 ? "drive slot1 ON (arrows feed block[1], gate set)"
                                   : "drive slot1 OFF");
        }
        drive_slot1_tick();
        // (Puppet transform/velocity apply is now SYNCHRONOUS inside hooked_update
        // via the Player-vtable hook - no async aux apply needed.)
        log_player_slot_positions();
        if (g_velprobe_enabled) {
            probe_velocity_field();
        }
        static bool prev_sp = false;
        const bool spk = (GetAsyncKeyState(VK_NUMPAD4) & 0x8000) != 0;
        if (spk && !prev_sp) {
            g_state_probe_enabled = !g_state_probe_enabled;
            log_line(g_state_probe_enabled ? "state probe ON (idle/run/jump to find state field)"
                                           : "state probe OFF");
        }
        prev_sp = spk;
        if (g_state_probe_enabled) {
            state_probe();
        }
        static bool prev_scam = false;
        const bool scam = (GetAsyncKeyState(VK_NUMPAD5) & 0x8000) != 0;
        if (scam && !prev_scam) {
            log_screen_camera_state();
        }
        prev_scam = scam;
        static bool prev_s1c = false;
        const bool s1c = (GetAsyncKeyState(VK_NUMPAD6) & 0x8000) != 0;
        if (s1c && !prev_s1c) {
            g_drive_screen1_cam = !g_drive_screen1_cam;  // toggle; game thread drives it
            log_line(g_drive_screen1_cam ? "screen1 camera drive ON"
                                         : "screen1 camera drive OFF");
        }
        prev_s1c = s1c;
        // DIAGNOSTIC: VK_HOME toggles the DLL between INERT (no-op pass-through) and
        // ACTIVE (clone + drives). Lets us isolate whether our active behavior breaks
        // the main player's aiming. (Runs in the aux thread so it works while inert.)
        static bool prev_inert_key = false;
        const bool ik = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
        if (ik && !prev_inert_key) {
            g_dll_inert = !g_dll_inert;
            log_line(g_dll_inert ? "DLL INERT (pass-through; no clone/drives)"
                                 : "DLL ACTIVE (clone + drives enabled)");
        }
        prev_inert_key = ik;
        static bool prev_pac = false;
        const bool pac = (GetAsyncKeyState(VK_NUMPAD3) & 0x8000) != 0;
        if (pac && !prev_pac) {
            g_play_as_clone = !g_play_as_clone;
            if (g_play_as_clone) {
                g_puppet_net_apply = false;       // stop puppet tracking
                swap_local_player_slots();        // camera -> clone
                g_drive_slot1 = true;             // scripted WASD drives the clone
                g_drive_slot_index = 1;
                // NOTE: with one local input set, WASD also drives Chloe (the game
                // routes all local input to the active player). Real co-op uses
                // separate per-player input, so this conflict does not occur there.
                log_line("PLAY-AS-CLONE ON: WASD drives clone (camera on clone)");
            } else {
                g_drive_slot1 = false;
                swap_local_player_slots();        // swap back (camera -> Chloe)
                log_line("PLAY-AS-CLONE OFF: control back to Chloe");
            }
        }
        prev_pac = pac;
        update_inject_stick();
        Sleep(15);
    }
}

// COLLISION FIX TEST: the clone's collider entries differ from Chloe's only at
// byte +0xc4 (havok collision filter info: Chloe 0x2c vs clone 0x22) -> the clone's
// body is filtered out of world collisions = ghost. Copy Chloe's filter byte onto
// all of the clone's collider entries (manager+0x238, +0x10=count, +0x48=array of
// 272-byte entries).
void fix_clone_collision_filter() {
    void* chloe0 = retained_player_object(0);
    void* clone1 = retained_player_object(1);
    if (chloe0 == nullptr || clone1 == nullptr) {
        log_line("COLLFIX: need both chloe0 and clone1");
        return;
    }
    auto get_mgr = [](void* o) -> std::byte* {
        if (o == nullptr || readable_bytes(o) < 0x240) return nullptr;
        return *reinterpret_cast<std::byte**>(static_cast<std::byte*>(o) + 0x238);
    };
    std::byte* cmgr = get_mgr(chloe0);
    std::byte* kmgr = get_mgr(clone1);
    if (cmgr == nullptr || kmgr == nullptr ||
        readable_bytes(cmgr) < 0x50 || readable_bytes(kmgr) < 0x50) {
        log_line("COLLFIX: collision manager unreadable");
        return;
    }
    std::byte* carr = *reinterpret_cast<std::byte**>(cmgr + 0x48);
    std::byte* karr = *reinterpret_cast<std::byte**>(kmgr + 0x48);
    std::uint32_t kcount = *reinterpret_cast<std::uint32_t*>(kmgr + 0x10);
    if (carr == nullptr || karr == nullptr || readable_bytes(carr) < 0xc5) {
        log_line("COLLFIX: collider arrays unreadable");
        return;
    }
    std::uint8_t cfilter = *reinterpret_cast<std::uint8_t*>(carr + 0xc4);  // Chloe ref
    int n = static_cast<int>(kcount);
    if (n > 64) n = 64;
    int changed = 0;
    for (int i = 0; i < n; ++i) {
        std::byte* e = karr + static_cast<std::size_t>(i) * 0x110;
        if (readable_bytes(e) < 0xc5) break;
        std::uint8_t* f = reinterpret_cast<std::uint8_t*>(e + 0xc4);
        if (*f != cfilter) { *f = cfilter; ++changed; }
    }
    std::ostringstream l;
    l << "COLLFIX: clone collider +0xc4 set to chloe filter 0x" << std::hex
      << (int)cfilter << " on " << std::dec << changed << "/" << n << " entries";
    log_line(l.str().c_str());
}

void log_joypad_command_slot_state(int slot, const char* phase) {
    if (slot < 0 || slot > 5 || g_module == 0) return;
    auto* const root_ptr = reinterpret_cast<std::byte**>(g_module + 0x3FE1088);
    if (readable_bytes(root_ptr) < sizeof(*root_ptr) || *root_ptr == nullptr) return;
    auto* const manager = *root_ptr + 3504 + 3056 * slot;
    if (readable_bytes(manager) < 0x5A0) return;
    const auto free_count = *reinterpret_cast<const std::uint32_t*>(manager + 0x538);
    auto* const active = *reinterpret_cast<std::byte* const*>(manager + 0x598);
    std::uint32_t active_count = 0xFFFFFFFFu;
    if (active != nullptr && readable_bytes(active + 0x24) >= 4) {
        active_count = *reinterpret_cast<const std::uint32_t*>(active + 0x20);
    }
    std::ostringstream line;
    line << "JOYPAD-SLOT " << phase << " slot=" << slot
         << " mgr=0x" << std::hex << reinterpret_cast<std::uintptr_t>(manager)
         << " free_count=" << std::dec << free_count
         << " active=0x" << std::hex << reinterpret_cast<std::uintptr_t>(active)
         << " active_count=" << std::dec << active_count << " free_ptrs=";
    for (int i = 0; i < 9; ++i) {
        line << " 0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(
                    *reinterpret_cast<std::byte* const*>(manager + 0x4F0 + 8 * i));
    }
    log_line(line.str().c_str());
}

void reissue_spawn_for_slot1() {
    if (!g_spawn_captured) {
        log_line("reissue spawn skipped: no captured spawn args yet");
        return;
    }
    // Idempotency guard: never spawn a second clone into slot 1 while one already
    // exists. A double-spawn into the same retained slot corrupts the slot and
    // crashes (this was the F12-after-auto-spawn crash).
    if (retained_player_object(1) != nullptr) {
        log_line("reissue spawn skipped: slot 1 already occupied (idempotent)");
        return;
    }
    const auto string_hash =
        reinterpret_cast<StringHashFunction>(g_module + kStringHashRva);
    // Candidate "Chloe look" hashes to find in the spawn args, and the target
    // look to replace them with (DRAKE - a known co-op full-body skin, to prove
    // the body swaps via the spawn look).
    const std::uint32_t chloe_base    = string_hash("chloe-base", 10, 0);
    const std::uint32_t coop_chloe    = string_hash("coop-chloe", 10, 0);
    const std::uint32_t chloe_hash    = string_hash("chloe", 5, 0);
    const std::uint32_t chloe_default = string_hash("chloe-default", 13, 0);
    const std::uint32_t target        = string_hash("coop-drake", 10, 0);
    const std::uint32_t from_set[] = {chloe_base, coop_chloe, chloe_hash, chloe_default};
    {
        std::ostringstream h;
        h << "LOOKSWAP target(coop-drake)=0x" << std::hex << target
          << " from chloe-base=0x" << chloe_base << " coop-chloe=0x" << coop_chloe
          << " chloe=0x" << chloe_hash << " chloe-default=0x" << chloe_default << std::dec;
        log_line(h.str().c_str());
    }
    int patched = 0;
    auto scan_region = [&](const char* label, std::byte* mem, std::size_t span) {
        if (mem == nullptr) return;
        const std::size_t avail = readable_bytes(mem);
        if (avail < 4) return;
        if (span > avail) span = avail;
        for (std::size_t off = 0; off + 4 <= span; off += 4) {
            std::uint32_t v = 0;
            std::memcpy(&v, mem + off, 4);
            bool hit = false;
            for (std::uint32_t f : from_set) if (v == f) { hit = true; break; }
            if (!hit) continue;
            std::memcpy(mem + off, &target, 4);
            ++patched;
            std::ostringstream l;
            l << "LOOKSWAP " << label << "+0x" << std::hex << off
              << ": 0x" << v << " -> 0x" << target << std::dec;
            log_line(l.str().c_str());
        }
    };
    scan_region("desc", reinterpret_cast<std::byte*>(g_spawn_desc), sizeof(g_spawn_desc));
    scan_region("ctx", reinterpret_cast<std::byte*>(g_spawn_ctx), 0x800);
    scan_region("a4", reinterpret_cast<std::byte*>(g_spawn_a4), 0x800);
    {
        std::ostringstream summary;
        summary << "LOOKSWAP summary: patched=" << patched
                << " (if 0, the look is NOT a hash in the spawn args)";
        log_line(summary.str().c_str());
    }
    using SpawnFn = void(__fastcall*)(void*, std::uint64_t, void*, void*, std::uint64_t, void*);
    const auto spawn = reinterpret_cast<SpawnFn>(g_module + kSpawnFromDescriptorRva);
    std::ostringstream line;
    line << "reissue spawn idx=1 desc=0x" << std::hex
         << reinterpret_cast<std::uintptr_t>(g_spawn_desc)
         << " ctx=0x" << reinterpret_cast<std::uintptr_t>(g_spawn_ctx) << std::dec;
    log_line(line.str().c_str());
    // Visual override (Nadine hair) DISABLED for the control test: it left the
    // body un-streamed (only the hair rendered, "floating"). Spawn plain Chloe so
    // the clone's movement is the only variable. Re-enable once control is solid.
    // LOOK INJECTION (body-swap test): override the spawn packet's base/look with
    // coop-drake during this spawn, on this (game) thread.
    g_inject_look = false;  // coop-drake packet override = DEAD (no body effect)
    // NADINE LOOK SWAP (the right lever): force the clone's appearance-hash
    // (Player vtable[92] -> object+0x15A4, fed to the look builder -> appearance-
    // record -> mesh) from Chloe's (0x3f334972=hash("chloe")) to Nadine's
    // (0xd27c074f=hash("nadine")) DURING this synchronous spawn, so the clone's
    // look build resolves Nadine's mesh. This is the look/mesh layer, NOT the
    // skeleton record (which broke the body when redirected).
    bool appear_swap = false;
    // Role A sees remote Nadine. Role B already renders its local Player as Nadine,
    // so its remote slot-1 Player must retain Chloe's appearance.
    if (g_want_nadine_body && !g_net_role_b) {
        // FIXED Nadine appearance hash (hash("nadine")). Do NOT read it from
        // g_last_nadine_object: after a location reload that pointer is stale (the
        // old freed Nadine) -> garbage hash -> "bald Chloe". The constant is robust.
        g_nadine_appearance_hash = 0xd27c074fu;
        g_appearance_swap_active = true;
        appear_swap = true;
        std::ostringstream l;
        l << "APPEARSWAP armed: clone look build -> Nadine appearance 0x"
          << std::hex << g_nadine_appearance_hash << std::dec;
        log_line(l.str().c_str());
    }
    log_joypad_command_slot_state(0, "before-spawn");
    log_joypad_command_slot_state(1, "before-spawn");
    spawn(g_spawn_desc, 1, g_spawn_ctx, g_spawn_a4, g_spawn_flag, g_spawn_out);
    if (appear_swap) {
        g_appearance_swap_active = false;
        log_line("APPEARSWAP done (clone spawn returned)");
    }
    g_slot1_visual_override_active = false;
    coop::StatePacket peer_after_spawn{};
    if (copy_last_peer_state_snapshot(peer_after_spawn)) {
        apply_peer_health_to_slot1(peer_after_spawn, true);
    }
    log_line("reissue spawn idx=1 returned");
    log_joypad_command_slot_state(0, "after-spawn");
    log_joypad_command_slot_state(1, "after-spawn");
    // Read-only falsification probe for the independent-input binding. PlayerMoveStick
    // selects Joypad as FrameState+0xDB0+0xBF0*(Player+0x39B8), independently of
    // Player+0x4010 (screen route). A reissued idx=1 spawn is expected to set both to 1;
    // if only +0x4010 changes, the clone still reads player 0's Joypad.
    for (int slot = 0; slot < 2; ++slot) {
        auto* player = static_cast<std::byte*>(retained_player_object(slot));
        if (player == nullptr || readable_bytes(player) < 0x4014) continue;
        std::ostringstream binding;
        binding << "PLAYER-INPUT-BIND slot=" << slot
                << " player=0x" << std::hex
                << reinterpret_cast<std::uintptr_t>(player)
                << " joypad_index=" << std::dec
                << *reinterpret_cast<const std::int32_t*>(player + 0x39B8)
                << " screen_index="
                << *reinterpret_cast<const std::int32_t*>(player + 0x4010);
        log_line(binding.str().c_str());
    }
    // GATING PROBE: did spawning the clone bump any "co-op / num-players" globals?
    // If yes -> a simple "hold them at 1" fix; if no -> the gate is the retained-slot
    // occupancy itself (deeper). Read after the spawn.
    {
        auto rd32 = [&](std::uintptr_t rva) -> std::uint32_t {
            auto* p = reinterpret_cast<std::uint32_t*>(g_module + rva);
            return readable_bytes(p) >= 4 ? *p : 0xFFFFFFFFu;
        };
        auto rd8 = [&](std::uintptr_t rva) -> std::uint32_t {
            auto* p = reinterpret_cast<std::uint8_t*>(g_module + rva);
            return readable_bytes(p) >= 1 ? *p : 0xFFu;
        };
        std::ostringstream l;
        l << "GATING globals after clone spawn: max_local_players="
          << std::dec << rd32(kMaxLocalPlayersRva)
          << " second_available=" << rd8(kSplitSecondPlayerAvailableRva)
          << " split_login_state=" << rd32(kSplitLoginStateRva)
          << " slot0=" << (retained_player_object(0) ? 1 : 0)
          << " slot1=" << (retained_player_object(1) ? 1 : 0);
        log_line(l.str().c_str());
    }
    // TEST: hide the clone from the retained-slot table (so single-player weapon/aim
    // systems count only player 0) while keeping the clone OBJECT alive (it still
    // renders/animates via the engine update list + our vtable hook). If the main
    // player regains aiming AND Nadine stays visible, this is the "separate players"
    // fix for the local/active-player case.
    if (g_hide_clone_from_slot) {
        auto* table = reinterpret_cast<std::uint8_t*>(g_module + kRetainedSlotTableRva);
        auto* slot1ptr = reinterpret_cast<void**>(table + 0x10);
        g_clone_cached = *slot1ptr;
        DWORD oldp = 0;
        if (VirtualProtect(slot1ptr, sizeof(void*), PAGE_READWRITE, &oldp)) {
            *slot1ptr = nullptr;
            DWORD ign = 0;
            VirtualProtect(slot1ptr, sizeof(void*), oldp, &ign);
            std::ostringstream l;
            l << "CLONE hidden from retained slot 1 (cached obj=0x" << std::hex
              << reinterpret_cast<std::uintptr_t>(g_clone_cached)
              << ") - main player should regain weapons; Nadine should stay visible";
            log_line(l.str().c_str());
        }
    }
    // Auto-drive removed: it fed slot-1 input forward and destabilized the
    // active player (Chloe carried forward). Drive is now manual (Numpad+) only.
}

// Retarget the MAIN game camera (CameraManager 0) to follow Nadine, so the
// player view is on her (the real "play as Nadine" camera, not the split-screen
// CameraManager(1) used by F5).
void retarget_main_camera_to_nadine() {
    if (g_last_nadine_object == nullptr) {
        log_line("camera0 retarget skipped: Nadine object is null");
        return;
    }
    const auto get_camera_manager =
        reinterpret_cast<GetCameraManagerFunction>(g_module + kGetCameraManagerRva);
    const auto attach_binding =
        reinterpret_cast<DirectAttachCameraRetainedBindingFunction>(g_module + kAttachCameraRetainedBindingRva);
    void* const manager0 = get_camera_manager(0);
    if (manager0 == nullptr) {
        log_line("camera0 retarget skipped: CameraManager(0) is null");
        return;
    }
    const auto retained_ptr = *reinterpret_cast<void**>(static_cast<std::byte*>(g_last_nadine_object) + 0xA8);
    const auto retained_id = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(g_last_nadine_object) + 0xBC);
    *reinterpret_cast<void**>(static_cast<std::byte*>(manager0) + 0x2A8) = retained_ptr;
    *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(manager0) + 0x2B0) = retained_id;
    attach_binding(manager0);
    std::ostringstream line;
    line << "camera0 (main) direct attach to Nadine: manager0=0x" << std::hex
         << reinterpret_cast<std::uintptr_t>(manager0)
         << " pair=0x" << reinterpret_cast<std::uintptr_t>(retained_ptr)
         << "/0x" << retained_id << std::dec;
    log_line(line.str().c_str());
}

void retarget_camera_manager1_to_nadine() {
    if (g_last_nadine_object == nullptr) {
        log_line("camera1 retarget skipped: Nadine object is null");
        return;
    }
    const auto get_camera_manager =
        reinterpret_cast<GetCameraManagerFunction>(g_module + kGetCameraManagerRva);
    const auto attach_binding =
        reinterpret_cast<DirectAttachCameraRetainedBindingFunction>(g_module + kAttachCameraRetainedBindingRva);
    void* const manager1 = get_camera_manager(1);
    if (manager1 == nullptr) {
        log_line("camera1 retarget skipped: CameraManager(1) is null");
        return;
    }
    const auto retained_ptr = *reinterpret_cast<void**>(static_cast<std::byte*>(g_last_nadine_object) + 0xA8);
    const auto retained_id = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(g_last_nadine_object) + 0xBC);
    *reinterpret_cast<void**>(static_cast<std::byte*>(manager1) + 0x2A8) = retained_ptr;
    *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(manager1) + 0x2B0) = retained_id;
    attach_binding(manager1);
    std::ostringstream line;
    line << "camera1 direct attach: manager1=0x" << std::hex
         << reinterpret_cast<std::uintptr_t>(manager1)
         << " pair=0x" << reinterpret_cast<std::uintptr_t>(retained_ptr)
         << "/0x" << retained_id << std::dec;
    log_line(line.str().c_str());
}

// PLAY-AS-CLONE: attach the MAIN render camera (CameraManager 0) to the slot-1
// clone via its retained binding (+0xA8/+0xBC), so the player SEES the clone. No
// slot swap (clone stays slot 1 = still receives our input). Combined with the
// clone drive this puts camera AND control on the same actor.
void attach_main_camera_to_clone() {
    void* clone = retained_player_object(1);
    if (clone == nullptr) return;
    if (readable_bytes(static_cast<std::byte*>(clone) + 0xC0) < 4) return;
    const auto get_camera_manager =
        reinterpret_cast<GetCameraManagerFunction>(g_module + kGetCameraManagerRva);
    const auto attach_binding =
        reinterpret_cast<DirectAttachCameraRetainedBindingFunction>(
            g_module + kAttachCameraRetainedBindingRva);
    void* const manager0 = get_camera_manager(0);
    if (manager0 == nullptr) return;
    const auto rptr = *reinterpret_cast<void**>(static_cast<std::byte*>(clone) + 0xA8);
    const auto rid = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(clone) + 0xBC);
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    const bool lognow = now - last > 1000;
    if (lognow) {
        last = now;
        // What's currently in manager0's follow-target slot (does the game keep
        // resetting it to Chloe each frame?).
        const auto cur = *reinterpret_cast<void**>(static_cast<std::byte*>(manager0) + 0x2A8);
        std::ostringstream l;
        l << "ATTACHDBG clone=0x" << std::hex << reinterpret_cast<std::uintptr_t>(clone)
          << " clone.bind=0x" << reinterpret_cast<std::uintptr_t>(rptr) << "/0x" << rid
          << " mgr0=0x" << reinterpret_cast<std::uintptr_t>(manager0)
          << " mgr0.target(before)=0x" << reinterpret_cast<std::uintptr_t>(cur) << std::dec;
        log_line(l.str().c_str());
    }
    if (rptr == nullptr) return;
    *reinterpret_cast<void**>(static_cast<std::byte*>(manager0) + 0x2A8) = rptr;
    *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(manager0) + 0x2B0) = rid;
    attach_binding(manager0);
}

void arm_velwriter_breakpoint_all_threads();  // defined below
void arm_inputstate_breakpoint_all_threads();  // defined below
void arm_stickread_breakpoint_all_threads();  // defined below
void arm_inputwrite_breakpoint_all_threads();  // defined below

void maybe_disable_p0_xinput_injection() {
    if (!g_xinput_inject_p0) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_xinput_p0_inject_until != 0 && now >= g_xinput_p0_inject_until) {
        g_xinput_inject_p0 = false;
        g_xinput_p0_inject_until = 0;
        log_line("XInput P0 auto-inject OFF");
    }
}

DWORD WINAPI hooked_xinput_get_state(DWORD user_index, XInputStateLite* state) {
    maybe_disable_p0_xinput_injection();
    if (g_loopback_two_instance && user_index == 0 && state != nullptr &&
        !process_owns_foreground_window()) {
        const DWORD result = g_real_xinput_get_state
                                 ? g_real_xinput_get_state(user_index, state)
                                 : 1167;
        if (result == 0) {
            // Keep the controller connected, but suppress controls belonging to
            // the inactive copy so its local camera remains independent.
            std::memset(&state->Gamepad, 0, sizeof(state->Gamepad));
        }
        return result;
    }
    if (g_xinput_inject_p0 && user_index == 0 && state != nullptr) {
        std::memset(state, 0, sizeof(*state));
        state->dwPacketNumber = ++g_xinput_inject_packet;
        state->Gamepad.sThumbLX = 0;
        state->Gamepad.sThumbLY = 22000;
        if (g_xinput_log) {
            static ULONGLONG last_p0 = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - last_p0 > 500) {
                last_p0 = now;
                log_line("XInputGetState idx=0 INJECT lx=0 ly=22000");
            }
        }
        return 0;
    }
    if (g_xinput_inject_p2 && user_index == 1 && state != nullptr) {
        std::memset(state, 0, sizeof(*state));
        state->dwPacketNumber = ++g_xinput_inject_packet;
        state->Gamepad.sThumbLX = static_cast<std::int16_t>(g_inject_lx);
        state->Gamepad.sThumbLY = static_cast<std::int16_t>(g_inject_ly);
        if (g_xinput_log) {
            static ULONGLONG last = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - last > 1000) {
                last = now;
                std::ostringstream line;
                line << "XInputGetState idx=1 INJECT lx=" << g_inject_lx
                     << " ly=" << g_inject_ly;
                log_line(line.str().c_str());
            }
        }
        return 0;  // ERROR_SUCCESS: report a connected pad with our stick
    }
    const DWORD result = g_real_xinput_get_state
                             ? g_real_xinput_get_state(user_index, state)
                             : 1167;  // ERROR_DEVICE_NOT_CONNECTED
    if (g_xinput_log && user_index < 4) {
        const ULONGLONG now = GetTickCount64();
        if (now - g_xinput_last_log[user_index] > 1500) {
            g_xinput_last_log[user_index] = now;
            std::ostringstream line;
            line << "XInputGetState idx=" << user_index << " result=" << result
                 << (result == 0 ? " (connected)" : " (absent)");
            log_line(line.str().c_str());
        }
    }
    return result;
}

DWORD WINAPI hooked_xinput_get_caps(DWORD user_index, DWORD flags,
                                    XInputCapabilitiesLite* caps) {
    maybe_disable_p0_xinput_injection();
    if (g_xinput_inject_p0 && user_index == 0 && caps != nullptr) {
        std::memset(caps, 0, sizeof(*caps));
        caps->Type = 1;
        caps->SubType = 1;
        caps->Gamepad.wButtons = 0xF3FF;
        caps->Gamepad.sThumbLX = 0x7FFF;
        caps->Gamepad.sThumbLY = 0x7FFF;
        caps->Gamepad.sThumbRX = 0x7FFF;
        caps->Gamepad.sThumbRY = 0x7FFF;
        return 0;
    }
    if (g_xinput_inject_p2 && user_index == 1 && caps != nullptr) {
        // Report a connected standard gamepad so the poll loop proceeds to call
        // XInputGetState(1) (where we inject the stick) and the connection scan
        // sees controller 1 as present.
        std::memset(caps, 0, sizeof(*caps));
        caps->Type = 1;     // XINPUT_DEVTYPE_GAMEPAD
        caps->SubType = 1;  // XINPUT_DEVSUBTYPE_GAMEPAD
        caps->Gamepad.wButtons = 0xF3FF;
        caps->Gamepad.sThumbLX = 0x7FFF;
        caps->Gamepad.sThumbLY = 0x7FFF;
        caps->Gamepad.sThumbRX = 0x7FFF;
        caps->Gamepad.sThumbRY = 0x7FFF;
        return 0;  // ERROR_SUCCESS
    }
    const DWORD r = g_real_xinput_get_caps
                        ? g_real_xinput_get_caps(user_index, flags, caps)
                        : 1167;
    if (g_xinput_log && user_index < 4) {
        static ULONGLONG last[4] = {0, 0, 0, 0};
        const ULONGLONG now = GetTickCount64();
        if (now - last[user_index] > 1500) {
            last[user_index] = now;
            std::ostringstream line;
            line << "XInputGetCapabilities idx=" << user_index << " result=" << r;
            log_line(line.str().c_str());
        }
    }
    return r;
}

void patch_iat_slot(std::uintptr_t rva, void* hook, void** out_real, const char* name) {
    auto* iat = reinterpret_cast<void**>(g_module + rva);
    *out_real = *iat;
    DWORD old = 0;
    if (VirtualProtect(iat, sizeof(*iat), PAGE_READWRITE, &old)) {
        *iat = hook;
        DWORD ignored = 0;
        VirtualProtect(iat, sizeof(*iat), old, &ignored);
        std::ostringstream line;
        line << name << " IAT hook installed (real=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(*out_real) << std::dec << ")";
        log_line(line.str().c_str());
    } else {
        log_line("IAT hook: VirtualProtect failed");
    }
}

void install_xinput_hook() {
    patch_iat_slot(kXInputGetStateIatRva, reinterpret_cast<void*>(&hooked_xinput_get_state),
                   reinterpret_cast<void**>(&g_real_xinput_get_state), "XInputGetState");
    patch_iat_slot(kXInputGetCapsIatRva, reinterpret_cast<void*>(&hooked_xinput_get_caps),
                   reinterpret_cast<void**>(&g_real_xinput_get_caps), "XInputGetCapabilities");
}

// Copy the local player's joint cache (pose) into the NPC NadineBuddy so it mirrors
// the player. JointCache = FgAnimData(+0x1A8)+0x10; cache+0x0 = transforms array,
// cache+0x8 = joint count; ~32 bytes/joint. Validated pointers; same skeleton => same
// layout/count so a raw copy reproduces the pose.
void mirror_pose_to_nadine(void* nadine) {
    void* chloe = retained_player_object(0);
    if (chloe == nullptr || nadine == nullptr) return;
    auto get_xforms = [](void* obj, void*& xf, std::uint32_t& cnt) -> bool {
        xf = nullptr; cnt = 0;
        if (readable_bytes(obj) < 0x1B0) return false;
        void* fg = *reinterpret_cast<void**>(static_cast<std::byte*>(obj) + 0x1A8);
        if (fg == nullptr || readable_bytes(fg) < 0x20) return false;
        void* jc = *reinterpret_cast<void**>(static_cast<std::byte*>(fg) + 0x10);
        if (jc == nullptr || readable_bytes(jc) < 0x10) return false;
        xf = *reinterpret_cast<void**>(static_cast<std::byte*>(jc) + 0x0);
        cnt = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(jc) + 0x8);
        return xf != nullptr;
    };
    void* xc; void* xn; std::uint32_t cc; std::uint32_t cn;
    if (!get_xforms(chloe, xc, cc) || !get_xforms(nadine, xn, cn)) return;
    if (cc == 0 || cc != cn || cc > 256) return;          // sanity: same count
    std::size_t bytes = static_cast<std::size_t>(cc) * 32;  // 32 bytes/joint
    if (readable_bytes(xc) < bytes || readable_bytes(xn) < bytes) return;
    std::memcpy(xn, xc, bytes);
}

// PHASE 3 RECON: enumerate the CharacterManager (global *(module+0x3898630)) to learn
// what actors it holds (players + NPCs + enemies). Layout from sub_141D63420: [+0]=
// entries array {recPtr, id}[16B]; [+8]=capacity; [+0xc]=count. Per entry: obj =
// *(recPtr+8), valid when *(u32*)(obj+0xbc)==id. Logs vtable RVA + name + world pos
// for each. Heavily readable_bytes-guarded; safe to call once.
void dump_character_manager() {
    if (g_module == 0) return;
    void* mgr = *reinterpret_cast<void**>(g_module + 0x3898630);
    if (mgr == nullptr || readable_bytes(mgr) < 0x10) {
        log_line("CHARMGR: null/unreadable manager");
        return;
    }
    void* ents = *reinterpret_cast<void**>(static_cast<std::byte*>(mgr) + 0x0);
    std::uint32_t cap = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(mgr) + 0x8);
    std::uint32_t count = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(mgr) + 0xc);
    {
        std::ostringstream h;
        h << "CHARMGR: mgr=0x" << std::hex << reinterpret_cast<std::uintptr_t>(mgr)
          << " ents=0x" << reinterpret_cast<std::uintptr_t>(ents)
          << std::dec << " cap=" << cap << " count=" << count;
        log_line(h.str().c_str());
    }
    if (ents == nullptr || count == 0 || count > 4096) return;
    if (readable_bytes(ents) < static_cast<std::size_t>(count) * 16) {
        log_line("CHARMGR: entries array too small to read");
        return;
    }
    int logged = 0;
    for (std::uint32_t i = 0; i < count && logged < 128; ++i) {
        auto* e = static_cast<std::byte*>(ents) + static_cast<std::size_t>(i) * 16;
        void* rec = *reinterpret_cast<void**>(e + 0);
        std::uint32_t id = *reinterpret_cast<std::uint32_t*>(e + 8);
        if (rec == nullptr || readable_bytes(rec) < 0x10) continue;
        void* obj = *reinterpret_cast<void**>(static_cast<std::byte*>(rec) + 8);
        if (obj == nullptr || readable_bytes(obj) < 0x300) continue;
        std::uint32_t objid = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(obj) + 0xbc);
        if (objid != id) continue;  // not a live/valid character slot
        std::uintptr_t vt = *reinterpret_cast<std::uintptr_t*>(obj);
        std::uintptr_t vtrva = (vt > g_module) ? (vt - g_module) : vt;
        // name string lives at obj+sizeof(void*) for these character objects
        const char* name = "?";
        const char* cand = reinterpret_cast<const char*>(obj) + sizeof(void*);
        if (readable_bytes(cand) >= 1) {
            bool printable = true;
            for (int k = 0; k < 24; ++k) {
                char c = cand[k];
                if (c == 0) break;
                if (c < 0x20 || c > 0x7e) { printable = false; break; }
            }
            if (printable) name = cand;
        }
        const auto* pos = reinterpret_cast<const float*>(
            static_cast<std::byte*>(obj) + kWorldPositionOffset);
        std::ostringstream l;
        l << "CHARMGR[" << std::dec << i << "] obj=0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(obj)
          << " vtRVA=0x" << vtrva << std::dec
          << " name='" << name << "'"
          << " pos=(" << pos[0] << "," << pos[1] << "," << pos[2] << ")";
        log_line(l.str().c_str());
        ++logged;
    }
    std::ostringstream d; d << "CHARMGR: logged " << logged << " valid characters";
    log_line(d.str().c_str());
}

void __fastcall hooked_update(void* object) {
    // DIAGNOSTIC: when inert, the DLL does NOTHING but pass the update through (no
    // clone spawn, no drives, no per-frame logic) - used to isolate whether our
    // active behavior breaks the main player's aiming/firing. Toggle with VK_END.
    if (g_dll_inert) {
        g_original_update(object);
        return;
    }
    // PHASE 3 RECON: periodic CharacterManager dump (every ~5s). Done its job
    // (confirmed enemies are enumerable); gated off now to keep the log clean.
    if (g_charmgr_dump_periodic) {
        static ULONGLONG last_dump_ms = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - last_dump_ms > 5000) {
            last_dump_ms = now;
            dump_character_manager();
        }
    }
    net_tick();
    // (PLAY-AS-CLONE camera comes from the retained-slot SWAP done on toggle, not
    // a per-frame attach.)
    // ANIM FROM PLAYER-1 VIEW: the engine only auto-updates the ACTIVE player's
    // animated-camera-control owner, so the remote clone freezes when the main
    // camera is on player 1. Keep the clone's owner (puppet+0x36C0) driven every
    // frame via 0x238800 so it ANIMATES in player 1's view. NUMPAD6 toggles.
    if (g_drive_screen1_cam && !g_play_as_clone) {
        static ULONGLONG last_anim_ms = 0;
        const ULONGLONG now = GetTickCount64();
        if (now != last_anim_ms) {
            last_anim_ms = now;
            materialize_screen1_camera_once();
        }
    }
    if (object != nullptr &&
        *reinterpret_cast<std::uintptr_t*>(object) ==
            reinterpret_cast<std::uintptr_t>(g_nadine_vtable) &&
            std::string_view(
            reinterpret_cast<const char*>(object) + sizeof(void*)
        ) == "nadine-buddy") {
        observe_nadine_visual_identity(object);
        g_last_nadine_object = object;  // set EARLY so the clone auto-spawn (below)
                                        // can read Nadine's look2 for the body swap.
        // Deep body/joint inspection remains manual (Insert) only. Running that
        // large pointer walk automatically in both instances raced scene teardown.
        // (Removed: a leftover diagnostic auto-injected ly=22000 on controller 0
        // for 2s on scene load = the "carried forward right after spawn" bug.)
        // DISABLED: auto split-screen login (max_local_players=2 / net_login_split_
        // screen) put the game into a half-activated 2-player mode that BREAKS the
        // main player's weapon AIM/FIRE controls on inject. Not needed for the clone
        // (it spawns + drives independently). Re-enable manually only if pursuing the
        // engine's split-screen path.
        if (!g_split_login_done) {
            g_split_login_done = true;
            // split_login_persistent();  // <- breaks aiming; left off
        }
        if (!g_slot1_scaffold_attempted) {
            g_slot1_scaffold_attempted = true;
            try_slot1_netplayer_respawn_scaffold(object);
        }
        coop::StatePacket fresh_peer{};
        const bool peer_is_fresh = copy_peer_state_snapshot(fresh_peer);
        // A remote Player is created only for a live peer. This keeps ordinary
        // single-player sessions untouched and gives both replicas the full Player
        // graph needed for later action/event synchronization.
        if ((g_enable_legacy_clone_autospawn ||
             (g_enable_remote_player_autospawn && peer_is_fresh)) &&
            g_spawn_captured &&
            retained_player_object(1) == nullptr &&
            !g_slot1_auto_reissue_done &&
            (!g_want_nadine_body || g_nadine_visual_identity_valid)) {
            g_slot1_auto_reissue_done = true;
            reissue_spawn_for_slot1();
            log_line(peer_is_fresh
                         ? "remote Player auto-spawned for live peer"
                         : "legacy slot1 auto-spawned");
        }
        // SEPARATION: do NOT auto-enable the slot-1 drive. Marking slot 1 as an
        // active input slot every frame (drive_slot1_tick) makes the engine treat the
        // session as 2-player and GATES the main player's aiming/firing. The clone
        // now stays a passive 2nd actor (Nadine body + collision) until you explicitly
        // control it with NUMPAD3 (play-as-clone). Real co-op will drive it from the
        // network, not from an always-on local marker.
        if (retained_player_object(1) != nullptr && !g_autotest_done) {
            g_autotest_done = true;
            g_drive_slot1 = false;
            g_auto_stick_active = false;
            g_puppet_net_apply = peer_is_fresh && g_enable_remote_player_autospawn;
            if (g_puppet_net_apply) {
                std::ostringstream line;
                line << "remote Player synchronization enabled, first peer seq="
                     << fresh_peer.sequence;
                log_line(line.str().c_str());
            } else {
                log_line("clone spawned passive (drive OFF; use NUMPAD3 to control it)");
            }
        }
        g_last_nadine_object = object;
        force_split_render_mode();
        auto& control_state = g_object_states[object];
        // TWO-INSTANCE AUTO-ENABLE: when a real network peer is present, turn the
        // animated-avatar drive on automatically (no need to press END in each
        // window). Set g_controlled_object BEFORE the (object!=controlled) guard
        // below so this buddy reaches the drive path. Fires once.
        if (!g_enable_remote_player_autospawn && !g_net_drive_nadine &&
            g_state_net.enabled &&
            g_state_net.has_peer_state.load(std::memory_order_acquire) &&
            world_stable()) {
            g_net_drive_nadine = true;
            g_control_enabled = true;
            g_controlled_object = object;
            for (auto& [ignored, state] : g_object_states) {
                state.was_moving = false;
                state.last_input_mask = 0;
                state.last_command_time = 0;
            }
            log_line("Net-drive Nadine AUTO-enabled (network peer detected)");
        }
        if ((GetAsyncKeyState(VK_F9) & 1) != 0) {
            log_camera_transform();
        }
        if ((GetAsyncKeyState(VK_F1) & 1) != 0) {
            set_nadine_control_state(object, 0);
        }
        if ((GetAsyncKeyState(VK_F2) & 1) != 0) {
            set_nadine_control_state(object, 1);
        }
        if ((GetAsyncKeyState(VK_F3) & 1) != 0) {
            set_nadine_control_state(object, 2);
        }
        if ((GetAsyncKeyState(VK_F5) & 1) != 0) {
            retarget_camera_manager1_to_nadine();
        }
        if (g_request_cam0_nadine) {
            g_request_cam0_nadine = false;
            // Disabled: poking CameraManager(0)'s retained-target fields breaks
            // the camera<->control coupling (movement freezes, camera doesn't
            // follow). The main camera follows local-player-0 via the retained
            // slot table instead (proven by the slot swap moving the camera).
            log_line("camera0 retarget DISABLED (breaks control); use slot path");
        }
        if ((GetAsyncKeyState(VK_F4) & 1) != 0) {
            register_nadine_as_character();
        }
        if ((GetAsyncKeyState(VK_F12) & 1) != 0) {
            reissue_spawn_for_slot1();
        }
        if ((GetAsyncKeyState(VK_DELETE) & 1) != 0) {
            g_puppet_net_apply = !g_puppet_net_apply;
            log_line(g_puppet_net_apply
                         ? "Puppet net-apply ON (force networked transform/velocity)"
                         : "Puppet net-apply OFF (puppet = pure local-input player)");
        }
        if ((GetAsyncKeyState(VK_PRIOR) & 1) != 0) {
            arm_velwriter_breakpoint_all_threads();
        }
        if ((GetAsyncKeyState(VK_NUMPAD0) & 1) != 0) {
            arm_inputstate_breakpoint_all_threads();  // capture sub_1402893D0 rcx (real input struct)
        }
        if ((GetAsyncKeyState(VK_NUMPAD1) & 1) != 0) {
            arm_inputwrite_breakpoint_all_threads();  // capture the first writer of block[0]+0/+4
        }
        if ((GetAsyncKeyState(VK_INSERT) & 1) != 0) {
            static ULONGLONG last_dump = 0;  // debounce: heavy scan, avoid freeze
            const ULONGLONG now = GetTickCount64();
            if (now - last_dump >= 3000) {
                last_dump = now;
                dump_body_subobject_diff();  // RTTI map + look-ids + desc-scan
            }
        }
        // Collision-filter mutation is intentionally not automatic. It confounded
        // the clean remote-Player lifetime test and is unrelated to input ownership.
        if ((GetAsyncKeyState(VK_PAUSE) & 1) != 0) {
            // Toggle the global CO-OP MODE gate: HIBYTE(dword_143E07470) +
            // byte_143E03568. This is the engine switch that wakes per-slot
            // character selection (different base body per player). Reversible.
            auto* gate_dword = reinterpret_cast<std::uint8_t*>(g_module + 0x3E07473);
            auto* gate_byte = reinterpret_cast<std::uint8_t*>(g_module + 0x3E03568);
            std::uint8_t newv = (*gate_dword != 0) ? 0 : 1;
            *gate_dword = newv;
            *gate_byte = newv;
            std::ostringstream l;
            l << "COOP-GATE toggled -> HIBYTE(143E07470)=" << std::dec << (int)*gate_dword
              << " byte(143E03568)=" << (int)*gate_byte
              << " (watch for per-slot character behavior / instability)";
            log_line(l.str().c_str());
        }
        // (Removed duplicate VK_END handler that toggled g_want_nadine_body — it
        // consumed the END key-edge before the net-drive toggle below. The legacy
        // clone body-swap is unused in Variant B. END now cleanly toggles net-drive.)
        if ((GetAsyncKeyState(VK_NUMPAD2) & 1) != 0) {
            g_manual_slot1_apply = !g_manual_slot1_apply;
            log_line(g_manual_slot1_apply
                         ? "manual slot1 apply ON"
                         : "manual slot1 apply OFF");
        }
        if ((GetAsyncKeyState(VK_NUMPAD5) & 1) != 0) {
            g_enemy_freeze_selftest = !g_enemy_freeze_selftest;
            if (!g_enemy_freeze_selftest) { g_enemy_ai_disabled.clear(); g_enemy_freeze_pose.clear(); }
            log_line(g_enemy_freeze_selftest
                         ? "ENEMY-FREEZE selftest ON (enemies AI-off + pinned)"
                         : "ENEMY-FREEZE selftest OFF");
        }
        // Use an explicit up->down edge. GetAsyncKeyState's low bit is affected by
        // keyboard auto-repeat and caused one physical press to publish many attacks.
        static bool decimal_was_down = false;
        const bool decimal_is_down = (GetAsyncKeyState(VK_DECIMAL) & 0x8000) != 0;
        const bool decimal_pressed = decimal_is_down && !decimal_was_down;
        decimal_was_down = decimal_is_down;
        // Fresh AttackInfo is the preferred feasibility probe because it avoids stale
        // handles from a captured attack. Both tests stay disabled until an explicit
        // controlled live run.
        if (decimal_pressed) {
            if (g_enable_fresh_attackinfo_test) {
                publish_fresh_nadine_attackinfo();
            } else if (!g_enable_attackinfo_replay_test) {
                log_line("ATTACKINFO-REPLAY disabled: safe publisher test not enabled");
            } else if (g_attackinfo_template == nullptr) {
                log_line("ATTACKINFO-REPLAY: no full AttackInfo clone captured");
            } else if (GetTickCount64() - g_attackinfo_capture_time > 15000) {
                destroy_attackinfo_clone(g_attackinfo_template);
                g_attackinfo_original_target = nullptr;
                log_line("ATTACKINFO-REPLAY: captured template expired");
            } else {
                void* target = nullptr;
                for_each_character([&](void* obj, const char* /*name*/) {
                    if (target == nullptr && obj != g_attackinfo_original_target &&
                        is_enemy_object(obj) && !is_dead_ragdoll(obj)) {
                        target = obj;
                    }
                });
                void* replay = clone_attackinfo(g_attackinfo_template);
                if (target == nullptr) {
                    destroy_attackinfo_clone(replay);
                    log_line("ATTACKINFO-REPLAY: no second live enemy found");
                } else if (replay == nullptr) {
                    log_line("ATTACKINFO-REPLAY: full clone failed");
                } else {
                    log_line("ATTACKINFO-REPLAY: publishing native Attack event");
                    reinterpret_cast<PublishAttackFn>(g_module + 0x27a270)(replay, target);
                    destroy_attackinfo_clone(replay);
                    log_line("ATTACKINFO-REPLAY: publisher returned; clone destroyed");
                }
            }
        }
        if ((GetAsyncKeyState('K') & 1) != 0) {
            log_line("DIE-SELFTEST disabled: Die(true) crashed in live test");
        }
        if ((GetAsyncKeyState('L') & 1) != 0) {
            log_line("COMBO-SELFTEST disabled: repeat TakeHit crashed in live test");
        }
        if ((GetAsyncKeyState('J') & 1) != 0) {
            log_line("TAKEHIT-SELFTEST disabled: repeat TakeHit crashed in live test");
        }
        // Live move-mapping calibration for the clone.
        if ((GetAsyncKeyState(VK_NUMPAD7) & 1) != 0) {
            g_move_rot -= 0.2617994F;  // -15 deg
            std::ostringstream l; l << "move_rot = " << g_move_rot << " rad";
            log_line(l.str().c_str());
        }
        if ((GetAsyncKeyState(VK_NUMPAD9) & 1) != 0) {
            g_move_rot += 0.2617994F;  // +15 deg
            std::ostringstream l; l << "move_rot = " << g_move_rot << " rad";
            log_line(l.str().c_str());
        }
        if ((GetAsyncKeyState(VK_NUMPAD8) & 1) != 0) {
            g_move_swap = !g_move_swap;
            log_line(g_move_swap ? "move axis swap ON" : "move axis swap OFF");
        }
        if ((GetAsyncKeyState(VK_HOME) & 1) != 0) {
            g_xinput_inject_p2 = !g_xinput_inject_p2;
            log_line(g_xinput_inject_p2
                         ? "XInput P2 inject ON (arrow keys = player-2 stick on idx 1)"
                         : "XInput P2 inject OFF");
        }
        if ((GetAsyncKeyState(VK_NEXT) & 1) != 0) {
            split_login_persistent();
        }
        if ((GetAsyncKeyState(VK_END) & 1) != 0) {
            g_net_drive_nadine = !g_net_drive_nadine;
            // Reuse the manual-control AI-disable machinery so Nadine holds the
            // driven motion: enabling control disables her AI on the next tick.
            g_control_enabled = g_net_drive_nadine;
            g_controlled_object = g_net_drive_nadine ? object : g_controlled_object;
            for (auto& [ignored, state] : g_object_states) {
                state.was_moving = false;
                state.last_input_mask = 0;
                state.last_command_time = 0;
            }
            log_line(g_net_drive_nadine
                         ? "Net-drive Nadine as remote avatar ENABLED (runs to peer pos)"
                         : "Net-drive Nadine disabled");
        }
        if ((GetAsyncKeyState(VK_F6) & 1) != 0) {
            log_local_player_targets(object);
        }
        if ((GetAsyncKeyState(VK_F7) & 1) != 0) {
            diagnose_split_screen_login();
        }
        if ((GetAsyncKeyState(VK_OEM_3) & 1) != 0) {  // `~`/ё : toggle client death-publish
            g_enable_client_death_publish = !g_enable_client_death_publish;
            log_line(g_enable_client_death_publish
                         ? "CLIENT-DEATH-PUBLISH ON (mirror host kills on puppet)"
                         : "CLIENT-DEATH-PUBLISH OFF (host-dead enemy left frozen)");
        }
        if ((GetAsyncKeyState(VK_F8) & 1) != 0) {
            g_control_enabled = !g_control_enabled;
            if (g_control_enabled) {
                g_controlled_object = object;
            }
            for (auto& [ignored, state] : g_object_states) {
                state.was_moving = false;
                state.last_input_mask = 0;
            }
            log_line(
                g_control_enabled
                    ? "WASD control enabled"
                    : "WASD control disabled"
            );
        }
        if ((GetAsyncKeyState(VK_F10) & 1) != 0) {
            g_command_source =
                g_command_source == CommandSource::LocalKeyboard
                    ? CommandSource::RemoteClient
                    : CommandSource::LocalKeyboard;
            log_line(
                g_command_source == CommandSource::LocalKeyboard
                    ? "Command source: local keyboard"
                    : "Command source: remote client"
            );
        }
        if ((GetAsyncKeyState(VK_F11) & 1) != 0) {
            if (g_motion_backend == MotionBackend::Type2MoveTo) {
                g_motion_backend = MotionBackend::Type4Command;
            } else if (g_motion_backend == MotionBackend::Type4Command) {
                g_motion_backend = MotionBackend::Type7Steering;
            } else {
                g_motion_backend = MotionBackend::Type2MoveTo;
            }
            for (auto& [ignored, state] : g_object_states) {
                state.was_moving = false;
                state.last_input_mask = 0;
                state.last_command_time = 0;
            }
            log_line(
                g_motion_backend == MotionBackend::Type2MoveTo
                    ? "Motion backend: Type2MoveTo"
                    : (g_motion_backend == MotionBackend::Type4Command
                           ? "Motion backend: Type4Command"
                           : "Motion backend: Type7Steering")
            );
        }
        if (object != g_controlled_object) {
            drive_remote_slot1_actions_tick();
            drive_slot1_tick();
            apply_slot1_perslot_on_game_thread();
            g_original_update(object);
            return;
        }

        if (g_allow_runtime_ai_toggle &&
            control_state.ai_disabled != g_control_enabled &&
            npc_ai_control_ready(object)) {
            if (g_control_enabled) {
                const auto request_disable_ai =
                    reinterpret_cast<RequestDisableAiFunction>(
                        g_module + kRequestDisableAiRva
                    );
                request_disable_ai(object, false, true);
                control_state.ai_disabled = true;
                log_line("NadineBuddy AI disabled for manual control");
            } else {
                const auto request_enable_ai =
                    reinterpret_cast<RequestEnableAiFunction>(
                        g_module + kRequestEnableAiRva
                    );
                request_enable_ai(object, false);
                control_state.ai_disabled = false;
                g_controlled_object = nullptr;
                log_line("NadineBuddy AI restored");
            }
        }

        if (g_net_drive_nadine) {
            drive_nadine_toward_peer(object, control_state);
        } else {
            const InputCommand command = get_active_command();
            apply_control_command(object, control_state, command);
        }
    }

    drive_remote_slot1_actions_tick();
    drive_slot1_tick();
    apply_slot1_perslot_on_game_thread();
    if (object == retained_player_object(1)) {
        apply_puppet_velocity_pre();
    }
    g_original_update(object);
    // VARIANT B avatar mirror-test: AFTER the NPC's own update wrote its joint cache,
    // overwrite it with the local player's pose so the NPC mirrors the player (proves
    // joint-driving). Only for the Nadine NPC.
    if (g_mirror_pose && object == g_last_nadine_object) {
        mirror_pose_to_nadine(object);
    }
    // Puppet (slot-1 Player): let its own update run above (so the anim graph
    // samples the velocity we wrote in net_tick), then assert the networked
    // transform last so the engine can't fight it into a flicker.
    apply_puppet_transform_post(object);
}

char __fastcall hooked_player_input_state_update(void* player) {
    const bool is_local = player != nullptr && player == retained_player_object(0);
    const bool is_remote = player != nullptr && player == retained_player_object(1);
    const int role = is_local ? 0 : (is_remote ? 1 : -1);
    float before_magnitude = 0.0F;
    float before_vector[4]{};
    if (role >= 0 && readable_bytes(static_cast<std::byte*>(player) + 0x3A40) >= 1) {
        before_magnitude = *reinterpret_cast<const float*>(
            static_cast<const std::byte*>(player) + 0x3A1C);
        std::memcpy(before_vector, static_cast<const std::byte*>(player) + 0x3A30,
                    sizeof(before_vector));
    }

    const char result = g_original_player_input_state_update(player);
    if (role < 0 || readable_bytes(static_cast<std::byte*>(player) + 0x3A40) < 1) {
        return result;
    }

    bool peer_movement_applied = false;
    float correction_x = 0.0F;
    float correction_z = 0.0F;
    if (is_remote) {
        coop::StatePacket peer{};
        if (copy_peer_state_snapshot(peer) && valid_network_movement(peer)) {
            auto* base = static_cast<std::byte*>(player);
            compute_remote_position_correction(
                peer, player, correction_x, correction_z);
            float corrected_world[4]{};
            std::memcpy(corrected_world, peer.move_world, sizeof(corrected_world));
            corrected_world[0] += correction_x;
            corrected_world[2] += correction_z;
            const float corrected_magnitude = std::sqrt(
                corrected_world[0] * corrected_world[0] +
                corrected_world[2] * corrected_world[2]);
            if (corrected_magnitude > 1.0F) {
                corrected_world[0] /= corrected_magnitude;
                corrected_world[2] /= corrected_magnitude;
            }
            std::memcpy(base + 0x39C8, peer.move_stick, sizeof(peer.move_stick));
            *reinterpret_cast<float*>(base + 0x3A1C) =
                corrected_magnitude > 1.0F ? 1.0F : corrected_magnitude;
            std::memcpy(base + 0x3A30, corrected_world, sizeof(corrected_world));
            peer_movement_applied = true;
        }
    }

    static ULONGLONG last_log[2]{};
    const ULONGLONG now = GetTickCount64();
    if (now - last_log[role] < 500) {
        return result;
    }
    last_log[role] = now;
    const auto* base = static_cast<const std::byte*>(player);
    const int joypad_index = *reinterpret_cast<const std::int32_t*>(base + 0x39B8);
    const float after_magnitude = *reinterpret_cast<const float*>(base + 0x3A1C);
    const auto* after_vector = reinterpret_cast<const float*>(base + 0x3A30);
    const auto* raw_stick = reinterpret_cast<const float*>(base + 0x39C8);
    std::ostringstream line;
    line << "PLAYER-INPUT-UPDATE role=" << (role == 0 ? "local" : "remote")
         << " joypad=" << joypad_index << " raw=(" << raw_stick[0] << ","
         << raw_stick[1] << ") beforeMag=" << before_magnitude << " afterMag="
         << after_magnitude << " beforeVec=(" << before_vector[0] << ","
         << before_vector[1] << "," << before_vector[2] << ") afterVec=("
         << after_vector[0] << "," << after_vector[1] << "," << after_vector[2]
         << ") peerMove=" << (peer_movement_applied ? 1 : 0)
         << " correction=(" << correction_x << "," << correction_z << ")";
    void* const fs = *reinterpret_cast<void**>(g_module + 0x3FE1088);
    if (fs != nullptr && joypad_index >= 0 && joypad_index < 6) {
        auto* const joypad = static_cast<std::byte*>(fs) + 0xDB0 +
                             0xBF0 * joypad_index;
        if (readable_bytes(joypad) >= 0xBE9) {
            const auto hash = reinterpret_cast<StringHashFunction>(
                g_module + kStringHashRva);
            const std::uint32_t move_hash = hash("move", 4, 0);
            using IsCommandDisabledFunction = bool(__fastcall*)(void*, std::uint32_t);
            using GetCommandFunction = void*(__fastcall*)(void*, std::uint32_t, int);
            const auto is_disabled = reinterpret_cast<IsCommandDisabledFunction>(
                g_module + 0x183A3D0);
            const auto get_command = reinterpret_cast<GetCommandFunction>(
                g_module + 0x183ABF0);
            void* const command = get_command(joypad, move_hash, 0);
            const int binding = command != nullptr && readable_bytes(command) >= 28
                ? *reinterpret_cast<const std::int32_t*>(
                      static_cast<const std::byte*>(command) + 24)
                : -1;
            const auto* processed0 = reinterpret_cast<const float*>(joypad + 0x48);
            const auto* processed1 = reinterpret_cast<const float*>(joypad + 0x50);
            line << " joypadReset=" << static_cast<int>(
                        *reinterpret_cast<const std::uint8_t*>(joypad + 0xBE0))
                 << " moveDisabled=" << (is_disabled(joypad, move_hash) ? 1 : 0)
                 << " moveCommand=0x" << std::hex
                 << reinterpret_cast<std::uintptr_t>(command) << std::dec
                 << " binding=" << binding << " processed0=(" << processed0[0]
                 << "," << processed0[1] << ") processed1=(" << processed1[0]
                 << "," << processed1[1] << ")";
        }
    }
    log_line(line.str().c_str());
    return result;
}

LONG CALLBACK spawn_capture_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress) ==
            g_module + kSpawnFromDescriptorRva) {
        CONTEXT* c = ep->ContextRecord;
        if (!g_spawn_captured && c->Rcx != 0 && c->Rdx == 0) {
            std::memcpy(g_spawn_desc, reinterpret_cast<void*>(c->Rcx), sizeof(g_spawn_desc));
            g_spawn_idx = c->Rdx;
            g_spawn_ctx = reinterpret_cast<void*>(c->R8);
            g_spawn_a4 = reinterpret_cast<void*>(c->R9);
            g_spawn_flag = *reinterpret_cast<std::uint64_t*>(c->Rsp + 0x28);
            g_spawn_out = *reinterpret_cast<void**>(c->Rsp + 0x30);
            g_spawn_captured = true;
            std::ostringstream line;
            line << "SPAWN captured: desc=0x" << std::hex << c->Rcx
                 << " idx=" << std::dec << g_spawn_idx
                 << " ctx=0x" << std::hex << c->R8
                 << " a4=0x" << c->R9
                 << " flag=0x" << g_spawn_flag
                 << " out=0x" << reinterpret_cast<std::uintptr_t>(g_spawn_out)
                 << std::dec;
            log_line(line.str().c_str());
            log_spawn_look_source_probe(c->Rcx, c->R8, c->R9,
                                        reinterpret_cast<std::uintptr_t>(g_spawn_out),
                                        c->Rsp);
        }
        // Disarm on this thread now that we captured, and set Resume Flag so the
        // instruction is not re-trapped in an infinite loop.
        c->Dr0 = 0;
        c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0x1);
        c->Dr6 = 0;
        c->EFlags |= 0x10000; // RF
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG CALLBACK packet_look_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress) ==
            g_module + kPacketInitRva) {
        CONTEXT* c = ep->ContextRecord;
        // Only the Player packet (selector in rdx == hash("Player")), and only while
        // our clone spawn is in flight. Override r9 (arg4 -> packet+0x20 base/look).
        if (g_inject_look && g_inject_look_hash != 0 &&
            static_cast<std::uint32_t>(c->Rdx) == g_player_sel_hash) {
            std::ostringstream l;
            l << "LOOKINJECT packet init: rdx(sel)=0x" << std::hex
              << static_cast<std::uint32_t>(c->Rdx) << " r9(base) 0x" << c->R9
              << " -> 0x" << g_inject_look_hash << std::dec;
            log_line(l.str().c_str());
            c->R9 = g_inject_look_hash;
            // Got the Player packet - disarm so we don't touch later packets.
            c->Dr2 = 0;
            c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0x10 | (0xFULL << 24));
        }
        c->Dr6 = 0;
        c->EFlags |= 0x10000;  // RF: skip the bp for one instruction
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void arm_packet_look_breakpoint_current_thread() {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE th = GetCurrentThread();
    if (!GetThreadContext(th, &ctx)) return;
    ctx.Dr2 = g_module + kPacketInitRva;
    ctx.Dr7 = (ctx.Dr7 & ~static_cast<DWORD64>(0xFULL << 24)) | 0x10;  // L2, exec, len1
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    SetThreadContext(th, &ctx);
}
void disarm_packet_look_breakpoint_current_thread() {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE th = GetCurrentThread();
    if (!GetThreadContext(th, &ctx)) return;
    ctx.Dr2 = 0;
    ctx.Dr7 = ctx.Dr7 & ~static_cast<DWORD64>(0x10 | (0xFULL << 24));
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    SetThreadContext(th, &ctx);
}

LONG CALLBACK record_swap_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress) ==
            g_module + kInitCompositeBodyRva) {
        CONTEXT* c = ep->ContextRecord;
        if (g_diag_compbody) {
            ++g_compbody_hits;
            std::ostringstream l;
            l << "COMPBODY InitializeCompositeBody fired: player(rcx)=0x" << std::hex
              << c->Rcx << " (hit #" << std::dec << g_compbody_hits << ")";
            log_line(l.str().c_str());
        }
        c->Dr6 = 0;
        c->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress) ==
            g_module + kLookApplyRva) {
        CONTEXT* c = ep->ContextRecord;
        // Only while the clone spawn is in flight, and only the look-apply whose
        // look2 hash (rdx) matches Chloe's -> swap to Nadine's render look.
        if (g_bodyswap_arm && g_bodyswap_to_id != 0 &&
            static_cast<std::uint32_t>(c->Rdx) == g_bodyswap_match_id) {
            std::ostringstream l;
            l << "BODYSWAP look2: rdx 0x" << std::hex
              << static_cast<std::uint32_t>(c->Rdx) << " -> 0x" << g_bodyswap_to_id
              << std::dec << " (hit #" << (g_bodyswap_hits + 1) << ")";
            log_line(l.str().c_str());
            c->Rdx = g_bodyswap_to_id;
            ++g_bodyswap_hits;
        }
        c->Dr6 = 0;
        c->EFlags |= 0x10000;  // RF: step over the bp instruction once
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void arm_compbody_diag_breakpoint_current_thread() {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE th = GetCurrentThread();
    if (!GetThreadContext(th, &ctx)) return;
    ctx.Dr3 = g_module + kInitCompositeBodyRva;
    ctx.Dr7 = (ctx.Dr7 & ~static_cast<DWORD64>(0xFULL << 28)) | 0x40;  // L3, exec
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    SetThreadContext(th, &ctx);
}

void arm_record_swap_breakpoint_current_thread() {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE th = GetCurrentThread();
    if (!GetThreadContext(th, &ctx)) return;
    ctx.Dr3 = g_module + kLookApplyRva;
    // L3 = bit 6 (0x40); DR3 RW/LEN nibble = bits 28..31 -> 0 (execute, len 1).
    ctx.Dr7 = (ctx.Dr7 & ~static_cast<DWORD64>(0xFULL << 28)) | 0x40;
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    SetThreadContext(th, &ctx);
}
void disarm_record_swap_breakpoint_current_thread() {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE th = GetCurrentThread();
    if (!GetThreadContext(th, &ctx)) return;
    ctx.Dr3 = 0;
    ctx.Dr7 = ctx.Dr7 & ~static_cast<DWORD64>(0x40 | (0xFULL << 28));
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    SetThreadContext(th, &ctx);
}

LONG CALLBACK visual_seed_capture_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        const std::uintptr_t hit =
            reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
        const std::uintptr_t look_addr = g_module + kPlayerVisualLookSeedRva;
        const std::uintptr_t state_addr = g_module + kPlayerVisualStateSeedRva;
        if (hit != look_addr && hit != state_addr) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        CONTEXT* c = ep->ContextRecord;
        const std::uint32_t source_look =
            (c->Rbx != 0 && readable_bytes(reinterpret_cast<void*>(c->Rbx)) >= 8)
                ? *reinterpret_cast<std::uint32_t*>(c->Rbx)
                : 0;
        const std::uint32_t source_state =
            (c->Rbx != 0 && readable_bytes(reinterpret_cast<void*>(c->Rbx)) >= 8)
                ? *reinterpret_cast<std::uint32_t*>(c->Rbx + 4)
                : 0;

        std::ostringstream line;
        line << "VISUAL " << (hit == look_addr ? "look" : "state")
             << " seed hit player=0x" << std::hex << c->R15
             << " pair=0x" << c->Rbx
             << " src(+0/+4)=0x" << source_look << "/0x" << source_state
             << " nadine=0x" << g_nadine_visual_look_id << "/0x"
             << g_nadine_visual_state_id
             << " active=" << std::dec << (g_slot1_visual_override_active ? 1 : 0)
             << " valid=" << (g_nadine_visual_identity_valid ? 1 : 0);

        // NOTE: +0x170 = resolved LOOK id (not the "nadine" selector — selector
        // produced plain Chloe). Codex's resolved-id (NadineBuddy +0x170) gives
        // Nadine HAIR only; the BODY is the streamed character mesh from the
        // (Chloe) spawn descriptor and is NOT swappable here. Kept as best-known.
        if (g_slot1_visual_override_active && g_nadine_visual_identity_valid) {
            if (hit == look_addr) {
                c->Rax = g_nadine_visual_look_id;
                c->Rip = look_addr + 2;
            } else {
                c->Rax = g_nadine_visual_state_id;
                c->Rip = state_addr + 3;
            }
            line << " PATCHED";
        } else {
            line << " observed";
        }
        log_line(line.str().c_str());

        if (hit == look_addr) {
            c->Dr0 = 0;
            c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0x3 | (0xFULL << 16));
        } else {
            c->Dr1 = 0;
            c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0xC | (0xFULL << 20));
            g_slot1_visual_override_armed = false;
        }
        c->Dr6 = 0;
        c->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG CALLBACK velwriter_capture_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        g_velwriter_capturing) {
        CONTEXT* c = ep->ContextRecord;
        if ((c->Dr6 & 0x2) != 0) {  // DR1 triggered
            const std::uintptr_t rip = c->Rip;
            bool known = false;
            for (int i = 0; i < g_velwriter_count; ++i) {
                if (g_velwriter_rips[i] == rip) {
                    known = true;
                    break;
                }
            }
            if (!known && g_velwriter_count < 8) {
                g_velwriter_rips[g_velwriter_count++] = rip;
                std::ostringstream line;
                line << "VEL writer RIP=0x" << std::hex << rip
                     << " (rva=0x" << (rip - g_module) << ")" << std::dec;
                log_line(line.str().c_str());
                // Walk the stack: log return addresses in .text (the call chain
                // up to the per-player update / control that drives this player).
                const std::uintptr_t code_lo = g_module + 0x1000;
                const std::uintptr_t code_hi = g_module + 0x25D526C;
                std::ostringstream st;
                st << "  STACK rva:";
                auto* sp = reinterpret_cast<std::uintptr_t*>(c->Rsp);
                int shown = 0;
                for (int i = 0; i < 0x200 && shown < 16; ++i) {
                    const std::uintptr_t val = sp[i];
                    if (val >= code_lo && val < code_hi) {
                        st << " 0x" << std::hex << (val - g_module) << std::dec;
                        ++shown;
                    }
                }
                log_line(st.str().c_str());
            }
            c->Dr6 = 0;
            if (g_velwriter_count >= 8) {
                // Captured enough distinct writers; disarm DR1 on this thread.
                c->Dr1 = 0;
                c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0x4);
                g_velwriter_capturing = false;
                log_line("VEL writer capture complete (disarming)");
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG CALLBACK inputstate_capture_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        g_inputstate_capturing &&
        reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress) ==
            g_module + kRealInputFnRva) {
        CONTEXT* c = ep->ContextRecord;
        // sub_1402893D0(rcx=per-slot input struct, rdx, r8=block, r9=?, slot).
        // Log rcx (the REAL struct address we couldn't compute) + its key fields:
        // +0x10 flags, +0x14/+0x18 heading, +0x1C magnitude, +0x0C, +0x20, +0x24.
        const std::uintptr_t st = c->Rcx;
        std::ostringstream line;
        line << "REALINPUT rcx=0x" << std::hex << st
             << " r8=0x" << c->R8 << " r9=0x" << c->R9 << std::dec;
        if (st != 0 && readable_bytes(reinterpret_cast<void*>(st)) >= 0x28) {
            auto* f = reinterpret_cast<float*>(st);
            line << " +0x0C=" << f[3] << " +0x10=0x" << std::hex
                 << *reinterpret_cast<std::uint32_t*>(st + 0x10) << std::dec
                 << " +0x14=" << f[5] << " +0x18=" << f[6]
                 << " +0x1C=" << f[7] << " +0x20=" << f[8] << " +0x24=" << f[9];
        }
        g_inputstate_addr = st;
        log_line(line.str().c_str());
        c->Dr2 = 0;
        c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0x10);
        c->Dr6 = 0;
        c->EFlags |= 0x10000;
        g_inputstate_capturing = false;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG CALLBACK perslot_capture_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        g_perslot_capturing &&
        reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress) ==
            g_module + kPerSlotApplyRva) {
        CONTEXT* c = ep->ContextRecord;
        const std::uint64_t slot64 = *reinterpret_cast<std::uint64_t*>(c->Rsp + 0x28);
        const int slot = static_cast<int>(slot64 & 0xFFFFFFFFu);
        if (slot == 0) {
            g_perslot_fs = c->Rcx;
            g_perslot_a2 = static_cast<int>(c->Rdx & 0xFFFFFFFFu);
            g_perslot_a4 = c->R9;
        }
        if (slot >= 0 && slot < 6 && g_perslot_hits[slot] < 8) {
            ++g_perslot_hits[slot];
            auto* fs = reinterpret_cast<std::byte*>(c->Rcx);
            auto* block = reinterpret_cast<void*>(c->R8);
            std::ostringstream line;
            line << "PERSLOT entry slot=" << slot
                 << " hit=" << g_perslot_hits[slot]
                 << " fs=0x" << std::hex << c->Rcx
                 << " block=0x" << c->R8;
            if (fs != nullptr && readable_bytes(fs) >= 0x7100) {
                auto* spin = reinterpret_cast<std::uint8_t*>(fs + 0x70B8 + slot);
                auto* branch = fs + 0x6FC8 + (0x28 * slot);
                auto* branch_q = reinterpret_cast<const std::uint64_t*>(branch);
                line << " spin=" << std::dec << static_cast<unsigned>(*spin)
                     << " gate0=" << static_cast<unsigned>(std::to_integer<unsigned char>(branch[0]))
                     << " q0=0x" << std::hex << branch_q[0]
                     << " q1=0x" << branch_q[1];
            }
            if (block != nullptr && readable_bytes(block) >= 0x18) {
                auto* f = reinterpret_cast<const float*>(block);
                line << std::dec
                     << " b[0]=" << f[0]
                     << " b[1]=" << f[1]
                     << " b[4]=" << f[4]
                     << " b[5]=" << f[5];
            }
            log_line(line.str().c_str());
        }
        c->Dr1 = 0;
        c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0x4);
        c->Dr6 = 0;
        c->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void arm_inputstate_breakpoint_all_threads() {
    const std::uintptr_t addr = g_module + kRealInputFnRva;
    g_inputstate_capturing = true;
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int armed = 0;
    // DR2 exec: L2=bit4 (0x10); R/W2=00 (exec) bits24-25; LEN2=00 bits26-27.
    const DWORD64 dr7_bits = static_cast<DWORD64>(0x10);
    const DWORD64 dr7_mask = ~((static_cast<DWORD64>(0x10)) |
                              (static_cast<DWORD64>(0xF) << 24));
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                   FALSE, te.th32ThreadID);
            if (!th) continue;
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            SuspendThread(th);
            if (GetThreadContext(th, &ctx)) {
                ctx.Dr2 = addr;
                ctx.Dr7 = (ctx.Dr7 & dr7_mask) | dr7_bits;
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) ++armed;
            }
            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    std::ostringstream line;
    line << "input-state exec-bp armed on " << armed << " threads @0x"
         << std::hex << addr << std::dec << " (move to trigger)";
    log_line(line.str().c_str());
}

void arm_inputstate_breakpoint_current_thread() {
    const std::uintptr_t addr = g_module + kRealInputFnRva;
    g_inputstate_capturing = true;
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE th = GetCurrentThread();
    if (!GetThreadContext(th, &ctx)) {
        return;
    }
    // DR2 exec.
    ctx.Dr2 = addr;
    ctx.Dr7 = (ctx.Dr7 & ~((static_cast<DWORD64>(0x10)) |
                           (static_cast<DWORD64>(0xF) << 24))) |
              static_cast<DWORD64>(0x10);
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (SetThreadContext(th, &ctx)) {
        std::ostringstream line;
        line << "input-state exec-bp armed on CURRENT thread @0x"
             << std::hex << addr << std::dec;
        log_line(line.str().c_str());
    }
}

void arm_perslot_breakpoint_all_threads() {
    const std::uintptr_t addr = g_module + kPerSlotApplyRva;
    std::memset(g_perslot_hits, 0, sizeof(g_perslot_hits));
    g_perslot_capturing = true;
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int armed = 0;
    // DR1 exec.
    const DWORD64 dr7_bits = static_cast<DWORD64>(0x4);
    const DWORD64 dr7_mask = ~((static_cast<DWORD64>(0x4)) |
                               (static_cast<DWORD64>(0xF) << 20));
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                   FALSE, te.th32ThreadID);
            if (!th) continue;
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            SuspendThread(th);
            if (GetThreadContext(th, &ctx)) {
                ctx.Dr1 = addr;
                ctx.Dr7 = (ctx.Dr7 & dr7_mask) | dr7_bits;
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) ++armed;
            }
            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    std::ostringstream line;
    line << "per-slot exec-bp armed on " << armed << " threads @0x"
         << std::hex << addr << std::dec;
    log_line(line.str().c_str());
}

LONG CALLBACK stickread_capture_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        g_stickread_capturing) {
        CONTEXT* c = ep->ContextRecord;
        if ((c->Dr6 & 0x8) != 0) {  // DR3 triggered
            const std::uintptr_t rip = c->Rip;
            bool known = false;
            for (int i = 0; i < g_stickread_count; ++i) {
                if (g_stickread_rips[i] == rip) { known = true; break; }
            }
            if (!known && g_stickread_count < 16) {
                g_stickread_rips[g_stickread_count++] = rip;
                std::ostringstream line;
                line << "STICK reader RIP=0x" << std::hex << rip
                     << " (rva=0x" << (rip - g_module) << ")" << std::dec;
                log_line(line.str().c_str());
            }
            c->Dr6 = 0;
            if (g_stickread_count >= 16) {
                c->Dr3 = 0;
                c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0x40);
                g_stickread_capturing = false;
                log_line("STICK reader capture complete");
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG CALLBACK inputwrite_capture_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        g_inputwrite_capturing) {
        CONTEXT* c = ep->ContextRecord;
        if ((c->Dr6 & 0x8) != 0) {  // DR3 triggered
            const std::uintptr_t rip = c->Rip;
            bool known = false;
            for (int i = 0; i < g_inputwrite_count; ++i) {
                if (g_inputwrite_rips[i] == rip) {
                    known = true;
                    break;
                }
            }
            if (!known && g_inputwrite_count < 16) {
                g_inputwrite_rips[g_inputwrite_count++] = rip;
                std::ostringstream line;
                line << "INPUT block0 writer RIP=0x" << std::hex << rip
                     << " (rva=0x" << (rip - g_module) << ")"
                     << " rcx=0x" << c->Rcx
                     << " rdx=0x" << c->Rdx
                     << " r8=0x" << c->R8
                     << " r9=0x" << c->R9;
                if (g_inputwrite_addr != 0 &&
                    readable_bytes(reinterpret_cast<void*>(g_inputwrite_addr)) >= 8) {
                    const auto* words =
                        reinterpret_cast<const std::uint32_t*>(g_inputwrite_addr);
                    const auto* vals =
                        reinterpret_cast<const float*>(g_inputwrite_addr);
                    line << " block0[+0]=0x" << words[0]
                         << "/" << std::dec << vals[0]
                         << " block0[+4]=0x" << std::hex << words[1]
                         << "/" << std::dec << vals[1];
                } else {
                    line << std::dec;
                }
                log_line(line.str().c_str());

                const std::uintptr_t code_lo = g_module + 0x1000;
                const std::uintptr_t code_hi = g_module + 0x25D526C;
                std::ostringstream st;
                st << "  STACK rva:";
                auto* sp = reinterpret_cast<std::uintptr_t*>(c->Rsp);
                int shown = 0;
                for (int i = 0; i < 0x200 && shown < 16; ++i) {
                    const std::uintptr_t val = sp[i];
                    if (val >= code_lo && val < code_hi) {
                        st << " 0x" << std::hex << (val - g_module) << std::dec;
                        ++shown;
                    }
                }
                log_line(st.str().c_str());
            }
            c->Dr6 = 0;
            if (g_inputwrite_count >= 16) {
                c->Dr3 = 0;
                c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0x40);
                g_inputwrite_capturing = false;
                log_line("INPUT block0 writer capture complete");
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG CALLBACK inputwrite_stick_capture_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
        g_inputwrite_stick_capturing) {
        CONTEXT* c = ep->ContextRecord;
        if ((c->Dr6 & 0x4) != 0) {  // DR2 triggered
            const std::uintptr_t rip = c->Rip;
            bool known = false;
            for (int i = 0; i < g_inputwrite_stick_count; ++i) {
                if (g_inputwrite_stick_rips[i] == rip) {
                    known = true;
                    break;
                }
            }
            if (!known && g_inputwrite_stick_count < 16) {
                g_inputwrite_stick_rips[g_inputwrite_stick_count++] = rip;
                std::ostringstream line;
                line << "INPUT block0 stick writer RIP=0x" << std::hex << rip
                     << " (rva=0x" << (rip - g_module) << ")"
                     << " rcx=0x" << c->Rcx
                     << " rdx=0x" << c->Rdx
                     << " r8=0x" << c->R8
                     << " r9=0x" << c->R9;
                if (g_inputwrite_stick_addr != 0 &&
                    readable_bytes(reinterpret_cast<void*>(g_inputwrite_stick_addr)) >= 8) {
                    const auto* words =
                        reinterpret_cast<const std::uint32_t*>(g_inputwrite_stick_addr);
                    const auto* vals =
                        reinterpret_cast<const float*>(g_inputwrite_stick_addr);
                    line << " block0[+10]=0x" << words[0]
                         << "/" << std::dec << vals[0]
                         << " block0[+14]=0x" << std::hex << words[1]
                         << "/" << std::dec << vals[1];
                } else {
                    line << std::dec;
                }
                log_line(line.str().c_str());

                const std::uintptr_t code_lo = g_module + 0x1000;
                const std::uintptr_t code_hi = g_module + 0x25D526C;
                std::ostringstream st;
                st << "  STACK rva:";
                auto* sp = reinterpret_cast<std::uintptr_t*>(c->Rsp);
                int shown = 0;
                for (int i = 0; i < 0x200 && shown < 16; ++i) {
                    const std::uintptr_t val = sp[i];
                    if (val >= code_lo && val < code_hi) {
                        st << " 0x" << std::hex << (val - g_module) << std::dec;
                        ++shown;
                    }
                }
                log_line(st.str().c_str());
            }
            c->Dr6 = 0;
            if (g_inputwrite_stick_count >= 16) {
                c->Dr2 = 0;
                c->Dr7 = c->Dr7 & ~static_cast<DWORD64>(0x10);
                g_inputwrite_stick_capturing = false;
                log_line("INPUT block0 stick writer capture complete");
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void arm_stickread_breakpoint_all_threads() {
    const std::uintptr_t addr = g_module + kInputStickRva;
    g_stickread_count = 0;
    g_stickread_capturing = true;
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    int armed = 0;
    // DR3 read/write 4-byte: L3=bit6(0x40); R/W3=11 at bits28-29; LEN3=11 at 30-31.
    const DWORD64 dr7_bits = static_cast<DWORD64>(0x40) |
                             (static_cast<DWORD64>(0x3) << 28) |
                             (static_cast<DWORD64>(0x3) << 30);
    const DWORD64 dr7_mask = ~((static_cast<DWORD64>(0x40)) |
                              (static_cast<DWORD64>(0xF) << 28));
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!th) continue;
            CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            SuspendThread(th);
            if (GetThreadContext(th, &ctx)) {
                ctx.Dr3 = addr;
                ctx.Dr7 = (ctx.Dr7 & dr7_mask) | dr7_bits;
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) ++armed;
            }
            ResumeThread(th); CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    std::ostringstream line;
    line << "STICK read-bp armed on " << armed << " threads @0x" << std::hex << addr
         << std::dec << " (move gamepad to capture readers)";
    log_line(line.str().c_str());
}

void arm_inputwrite_stick_breakpoint_all_threads() {
    const std::uintptr_t addr = g_module + kInputStickRva;
    g_inputwrite_stick_addr = addr;
    g_inputwrite_stick_count = 0;
    g_inputwrite_stick_capturing = true;
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        log_line("arm input-stick-write bp: snapshot failed");
        return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int armed = 0;
    // DR2 write 8-byte watch on block[0]+0x10. This catches the normalized
    // stick pair (+0x10/+0x14), which may be the live keyboard path.
    const DWORD64 dr7_bits = static_cast<DWORD64>(0x10) |
                             (static_cast<DWORD64>(0x1) << 24) |
                             (static_cast<DWORD64>(0x2) << 26);
    const DWORD64 dr7_mask = ~((static_cast<DWORD64>(0x10)) |
                               (static_cast<DWORD64>(0x3) << 24) |
                               (static_cast<DWORD64>(0x3) << 26));
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) {
                continue;
            }
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                   FALSE, te.th32ThreadID);
            if (!th) {
                continue;
            }
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            SuspendThread(th);
            if (GetThreadContext(th, &ctx)) {
                ctx.Dr2 = addr;
                ctx.Dr7 = (ctx.Dr7 & dr7_mask) | dr7_bits;
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) {
                    ++armed;
                }
            }
            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    std::ostringstream line;
    line << "INPUT block0 stick write-bp armed on " << armed << " threads @0x"
         << std::hex << addr << std::dec
         << " (move Chloe to capture +0x10/+0x14 writers)";
    log_line(line.str().c_str());
}

void arm_inputwrite_breakpoint_all_threads() {
    const std::uintptr_t addr = g_module + kInputBlock0HeadingRva;
    g_inputwrite_addr = addr;
    g_inputwrite_count = 0;
    g_inputwrite_capturing = true;
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        log_line("arm input-write bp: snapshot failed");
        return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int armed = 0;
    // DR3 write 8-byte watch on block[0]+0. This catches both heading floats at
    // +0x0 and +0x4 before later copies.
    const DWORD64 dr7_bits = static_cast<DWORD64>(0x40) |
                             (static_cast<DWORD64>(0x1) << 28) |
                             (static_cast<DWORD64>(0x2) << 30);
    const DWORD64 dr7_mask = ~((static_cast<DWORD64>(0x40)) |
                               (static_cast<DWORD64>(0x3) << 28) |
                               (static_cast<DWORD64>(0x3) << 30));
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) {
                continue;
            }
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                   FALSE, te.th32ThreadID);
            if (!th) {
                continue;
            }
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            SuspendThread(th);
            if (GetThreadContext(th, &ctx)) {
                ctx.Dr3 = addr;
                ctx.Dr7 = (ctx.Dr7 & dr7_mask) | dr7_bits;
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) {
                    ++armed;
                }
            }
            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    std::ostringstream line;
    line << "INPUT block0 write-bp armed on " << armed << " threads @0x"
         << std::hex << addr << std::dec
         << " (move Chloe to capture first heading writers)";
    log_line(line.str().c_str());
}

void arm_velwriter_breakpoint_all_threads() {
    void* slot0 = retained_player_object(0);
    if (slot0 == nullptr) {
        log_line("arm vel-writer bp: slot0 player null");
        return;
    }
    const std::uintptr_t addr =
        reinterpret_cast<std::uintptr_t>(slot0) + kPlayerVelocityOffset;
    g_velwriter_addr = addr;
    g_velwriter_count = 0;
    g_velwriter_capturing = true;
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        log_line("arm vel-writer bp: snapshot failed");
        return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int armed = 0;
    // DR1 write 4-byte: L1=bit2; R/W1=01 (write) at bits 20-21; LEN1=11 (4) at 22-23.
    const DWORD64 dr7_bits =
        static_cast<DWORD64>(0x4) |
        (static_cast<DWORD64>(0x1) << 20) |
        (static_cast<DWORD64>(0x3) << 22);
    const DWORD64 dr7_mask =
        ~((static_cast<DWORD64>(0x4)) |
          (static_cast<DWORD64>(0x3) << 20) |
          (static_cast<DWORD64>(0x3) << 22));
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) {
                continue;
            }
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                   FALSE, te.th32ThreadID);
            if (!th) {
                continue;
            }
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            SuspendThread(th);
            if (GetThreadContext(th, &ctx)) {
                ctx.Dr1 = addr;
                ctx.Dr7 = (ctx.Dr7 & dr7_mask) | dr7_bits;
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) {
                    ++armed;
                }
            }
            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    std::ostringstream line;
    line << "vel-writer write-bp armed on " << armed << " threads @0x"
         << std::hex << addr << std::dec;
    log_line(line.str().c_str());
}

void arm_spawn_breakpoint_all_threads() {
    const std::uintptr_t addr = g_module + kSpawnFromDescriptorRva;
    const DWORD pid = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        log_line("arm spawn bp: snapshot failed");
        return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    int armed = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) {
                continue;
            }
            HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                                   FALSE, te.th32ThreadID);
            if (!th) {
                continue;
            }
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            SuspendThread(th);
            if (GetThreadContext(th, &ctx)) {
                ctx.Dr0 = addr;
                ctx.Dr7 = (ctx.Dr7 & ~static_cast<DWORD64>(0xF)) | 0x1; // L0 enable, exec, len 1
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (SetThreadContext(th, &ctx)) {
                    ++armed;
                }
            }
            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    std::ostringstream line;
    line << "spawn HW breakpoint armed on " << armed << " threads @0x" << std::hex << addr;
    log_line(line.str().c_str());
}

void arm_slot1_visual_seed_breakpoint_current_thread() {
    const std::uintptr_t look_addr = g_module + kPlayerVisualLookSeedRva;
    const std::uintptr_t state_addr = g_module + kPlayerVisualStateSeedRva;
    g_slot1_visual_override_armed = true;
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE th = GetCurrentThread();
    if (!GetThreadContext(th, &ctx)) {
        log_line("visual seed bp: GetThreadContext failed");
        return;
    }
    ctx.Dr0 = look_addr;
    ctx.Dr1 = state_addr;
    ctx.Dr7 = (ctx.Dr7 & ~static_cast<DWORD64>(0xF | (0xFULL << 16) | (0xFULL << 20))) |
              0x5; // L0 + L1, execute breakpoints, len 1
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (SetThreadContext(th, &ctx)) {
        std::ostringstream line;
        line << "visual seed bp armed on CURRENT thread look=0x" << std::hex << look_addr
             << " state=0x" << state_addr
             << " nadine_look=0x" << g_nadine_visual_look_id
             << " nadine_state=0x" << g_nadine_visual_state_id
             << " valid=" << std::dec << (g_nadine_visual_identity_valid ? 1 : 0);
        log_line(line.str().c_str());
    } else {
        log_line("visual seed bp: SetThreadContext failed");
    }
}

DWORD WINAPI install_hook(void*) {
    g_module = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(L"tll.exe")
    );
    if (g_module == 0) {
        log_line("tll.exe module not found");
        return 1;
    }
    AddVectoredExceptionHandler(0, &crash_observer);
    log_line("crash observer installed (no breakpoints)");
    install_joypad_disabled_commands_lock();
    install_joypad3_analog_route();
    install_joypad3_keyboard_isolation();
    install_player_fire_capture();

    // VARIANT A (co-op mode gate at init) PROVEN DEAD: setting HIBYTE(dword_143E07470)
    // + byte_143E03568 before save load CRASHES the campaign load — campaign levels
    // have no co-op data/spawners, so enabling co-op mode dereferences uninitialized
    // co-op session state. The runtime co-op-character path is conclusively non-viable.
    // (Gate-set removed; the PAUSE hotkey toggle still exists for further probing.)

    g_nadine_vtable =
        reinterpret_cast<std::uintptr_t*>(
            g_module + kNadineBuddyVtableRva
        );
    *reinterpret_cast<std::uint8_t*>(
        g_module + kDisableBuddyTeleportRva
    ) = 1;
    g_original_update =
        reinterpret_cast<UpdateFunction>(g_nadine_vtable[kUpdateSlot]);

    auto* resolver_owner = reinterpret_cast<void**>(g_module + kLocalPlayerResolverGlobalRva);
    if (resolver_owner != nullptr && *resolver_owner != nullptr) {
        auto** resolver_vtable = reinterpret_cast<std::uintptr_t**>(*resolver_owner);
        if (resolver_vtable != nullptr && *resolver_vtable != nullptr) {
            auto* resolver_slot = &(*resolver_vtable)[0xA8 / sizeof(void*)];
            g_original_resolve_local_player = reinterpret_cast<ResolveLocalPlayerFunction>(*resolver_slot);
            DWORD resolver_old = 0;
            if (VirtualProtect(resolver_slot, sizeof(*resolver_slot), PAGE_READWRITE, &resolver_old)) {
                *resolver_slot = reinterpret_cast<std::uintptr_t>(&hooked_resolve_local_player);
                FlushInstructionCache(GetCurrentProcess(), resolver_slot, sizeof(*resolver_slot));
                DWORD ignored_resolver = 0;
                VirtualProtect(resolver_slot, sizeof(*resolver_slot), resolver_old, &ignored_resolver);
                log_line("local-player resolver hook installed at vfunc+0xA8");
            } else {
                log_line("VirtualProtect failed for local-player resolver hook");
            }
        }
    }

    if (reinterpret_cast<std::uintptr_t>(g_original_update) !=
        g_module + 0x1BB8320) {
        log_line("Unexpected NadineBuddy update function; hook refused");
        return 2;
    }

    DWORD old_protection = 0;
    auto* slot = &g_nadine_vtable[kUpdateSlot];
    if (!VirtualProtect(
            slot,
            sizeof(*slot),
            PAGE_READWRITE,
            &old_protection
        )) {
        log_line("VirtualProtect failed");
        return 3;
    }

    *slot = reinterpret_cast<std::uintptr_t>(&hooked_update);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), old_protection, &ignored);
    log_line(
        "NadineBuddy control hook installed; local keyboard feeds command path"
    );

    // ALSO hook the Player vtable's update slot (same shared function 0x141BB8320)
    // so hooked_update fires for the slot-1 clone too -> we can apply the puppet
    // transform SYNCHRONOUSLY post-update (kills flicker) and manipulate its
    // anim/locomotion fields each frame. Filtered inside by object == puppet.
    {
        auto* pvt = reinterpret_cast<std::uintptr_t*>(g_module + kPlayerVtableRva);
        // Read-only scheduling probe. Player vtable[16] is the sole caller of
        // PlayerInputCommandState_Update (0x141E39FB0); the pass-through hook tells
        // us whether retained slot 1 reaches that phase and what it publishes.
        auto* input_update_slot = &pvt[16];
        if (*input_update_slot == g_module + 0xC9F8B0) {
            g_original_player_input_state_update =
                reinterpret_cast<PlayerInputStateUpdateFunction>(*input_update_slot);
            DWORD old_input_update = 0;
            if (VirtualProtect(input_update_slot, sizeof(*input_update_slot),
                               PAGE_READWRITE, &old_input_update)) {
                *input_update_slot =
                    reinterpret_cast<std::uintptr_t>(&hooked_player_input_state_update);
                FlushInstructionCache(GetCurrentProcess(), input_update_slot,
                                      sizeof(*input_update_slot));
                DWORD ignored_input_update = 0;
                VirtualProtect(input_update_slot, sizeof(*input_update_slot),
                               old_input_update, &ignored_input_update);
                log_line("Player vtable[16] input-state scheduling probe installed");
            }
        } else {
            log_line("Player vtable[16] unexpected; input-state probe skipped");
        }
        auto* pslot = &pvt[kUpdateSlot];
        if (*pslot == g_module + 0x1BB8320) {
            DWORD oldp = 0;
            if (VirtualProtect(pslot, sizeof(*pslot), PAGE_READWRITE, &oldp)) {
                *pslot = reinterpret_cast<std::uintptr_t>(&hooked_update);
                FlushInstructionCache(GetCurrentProcess(), pslot, sizeof(*pslot));
                DWORD ign = 0;
                VirtualProtect(pslot, sizeof(*pslot), oldp, &ign);
                log_line("Player vtable update hook installed (puppet sync apply)");
            }
        } else {
            log_line("Player vtable[87] unexpected; player hook skipped");
        }
        // Hook Player vtable[92] = appearance-hash getter (sub_140A1DCC0) for the
        // Nadine look swap on the clone (gated by g_appearance_swap_active).
        auto* aslot = &pvt[kAppearanceSlot];
        if (*aslot == g_module + 0x0A1DCC0) {
            g_original_appearance = reinterpret_cast<AppearanceFunction>(*aslot);
            DWORD oldp = 0;
            if (VirtualProtect(aslot, sizeof(*aslot), PAGE_READWRITE, &oldp)) {
                *aslot = reinterpret_cast<std::uintptr_t>(&hooked_appearance);
                FlushInstructionCache(GetCurrentProcess(), aslot, sizeof(*aslot));
                DWORD ign = 0;
                VirtualProtect(aslot, sizeof(*aslot), oldp, &ign);
                log_line("Player vtable[92] appearance hook installed (Nadine look swap)");
            }
        } else {
            log_line("Player vtable[92] unexpected; appearance hook skipped");
        }

        // Guard the four native Player death-entry variants. A remote replica must
        // never author its own fall/death while the real peer still reports HP>0.
        constexpr std::size_t first_death_slot = 0xBE8 / sizeof(void*);
        const std::uintptr_t expected[4] = {
            g_module + 0x0D79C00, g_module + 0x0D79950,
            g_module + 0x0D78EC0, g_module + 0x0D792D0,
        };
        const std::uintptr_t replacements[4] = {
            reinterpret_cast<std::uintptr_t>(&hooked_player_death_be8),
            reinterpret_cast<std::uintptr_t>(&hooked_player_death_bf0),
            reinterpret_cast<std::uintptr_t>(&hooked_player_death_bf8),
            reinterpret_cast<std::uintptr_t>(&hooked_player_death_c00),
        };
        bool expected_all = true;
        for (int i = 0; i < 4; ++i) {
            expected_all = expected_all && pvt[first_death_slot + i] == expected[i];
        }
        if (expected_all) {
            DWORD oldp = 0;
            auto* first = &pvt[first_death_slot];
            if (VirtualProtect(first, 4 * sizeof(*first), PAGE_READWRITE, &oldp)) {
                for (int i = 0; i < 4; ++i) {
                    g_original_player_death_entries[i] =
                        reinterpret_cast<PlayerDeathEntryFunction>(first[i]);
                    first[i] = replacements[i];
                }
                FlushInstructionCache(GetCurrentProcess(), first, 4 * sizeof(*first));
                DWORD ign = 0;
                VirtualProtect(first, 4 * sizeof(*first), oldp, &ign);
                log_line("Player remote-death guards installed at +0xBE8..+0xC00");
            }
        } else {
            log_line("Player death-entry vtable mismatch; guards skipped");
        }
    }
    // Hook AiHitController::TakeHit = vtable[17] @ module+0x2959448 (func 0x5001e0).
    // Captures real HitDescription bytes + controller->owner mapping when enemies are
    // hit (Path 1 client->host damage recon). Single shared vtable -> hooks all enemies.
    // Default OFF: the TakeHit hook is only needed for the damage-relay recon/build.
    // Leaving it installed adds per-hit overhead in combat; skip unless explicitly on.
    if (g_install_takehit_hook) {
        auto* tslot = reinterpret_cast<std::uintptr_t*>(g_module + 0x2959448);
        if (*tslot == g_module + 0x5001e0) {
            g_orig_takehit = *tslot;
            DWORD oldp = 0;
            if (VirtualProtect(tslot, sizeof(*tslot), PAGE_READWRITE, &oldp)) {
                *tslot = reinterpret_cast<std::uintptr_t>(&hooked_takehit);
                FlushInstructionCache(GetCurrentProcess(), tslot, sizeof(*tslot));
                DWORD ign = 0;
                VirtualProtect(tslot, sizeof(*tslot), oldp, &ign);
                log_line("AiHitController::TakeHit hook installed (HitDescription probe)");
            }
        } else {
            std::ostringstream l;
            l << "TakeHit vtable[17] unexpected 0x" << std::hex << *tslot
              << " (expected 0x" << (g_module + 0x5001e0) << "); hook skipped";
            log_line(l.str().c_str());
        }
    }
    submit_remote_input_packet(coop::InputPacket{});
    start_udp_receiver();
    log_line("local-player-1 -> Nadine bind hook ENABLED");
    log_line("one-shot split render activation ENABLED after login_state==3");
    start_state_net();
    // Role A applies its own attacks normally and receives relayed damage in the
    // network tick. Only role B needs to observe local hits and forward them.
    if (g_install_npc_damage_hook && g_net_role_b) {
        install_npc_damage_hook_fn();
    } else {
        log_line("NPC-damage hook skipped on role A");
    }
    install_xinput_hook();
    g_velprobe_enabled = false;
    if constexpr (kEnableSpawnCaptureDiagnostic) {
        g_spawn_veh = AddVectoredExceptionHandler(1, &spawn_capture_veh);
    }
    if constexpr (kEnableLegacyRuntimeDiagnostics) {
        CreateThread(nullptr, 0, &aux_input_thread, nullptr, 0, nullptr);
        g_velwriter_veh = AddVectoredExceptionHandler(0, &velwriter_capture_veh);
        g_perslot_veh = AddVectoredExceptionHandler(0, &perslot_capture_veh);
        g_inputstate_veh = AddVectoredExceptionHandler(0, &inputstate_capture_veh);
        g_stickread_veh = AddVectoredExceptionHandler(0, &stickread_capture_veh);
        g_inputwrite_veh = AddVectoredExceptionHandler(0, &inputwrite_capture_veh);
        g_inputwrite_stick_veh =
            AddVectoredExceptionHandler(0, &inputwrite_stick_capture_veh);
        g_visual_seed_veh = AddVectoredExceptionHandler(0, &visual_seed_capture_veh);
        g_packet_look_veh = AddVectoredExceptionHandler(1, &packet_look_veh);
        g_record_swap_veh = AddVectoredExceptionHandler(1, &record_swap_veh);
    } else {
        log_line("legacy diagnostic thread/VEH/hardware breakpoints disabled");
    }
    {
        const auto string_hash =
            reinterpret_cast<StringHashFunction>(g_module + kStringHashRva);
        g_player_sel_hash = string_hash("Player", 6, 0);
        g_inject_look_hash = string_hash("coop-drake", 10, 0);  // target body
        std::ostringstream l;
        l << "LOOKINJECT ready: player-sel=0x" << std::hex << g_player_sel_hash
          << " target(coop-drake)=0x" << g_inject_look_hash << std::dec;
        log_line(l.str().c_str());
    }
    if constexpr (kEnableSpawnCaptureDiagnostic) {
        arm_spawn_breakpoint_all_threads();
    }
    // DIAGNOSTIC input-capture HW breakpoints (perslot / inputstate / inputwrite /
    // stick) are NO LONGER auto-armed: they hook stick/input processing and BREAK
    // AIMING (right stick) for the main player on inject. They were only needed for
    // the original input RE; the clone drive writes FrameState directly without them.
    // Re-arm manually via the Numpad/PageUp hotkeys if input RE is needed again.
    log_line(kEnableSpawnCaptureDiagnostic
                 ? "Spawn-arg capture armed (trigger a checkpoint/respawn to capture)"
                 : "Spawn-arg capture disabled in stable co-op build");
    log_line("input-capture breakpoints NOT auto-armed (they broke aiming); spawn only");
    log_line("velprobe disabled on load (press Insert to enable)");
    log_line("Press PageUp to capture the velocity-writer RIP (run, then read log)");
    log_line("Press Numpad1 to capture block[0]+0/+4 writers (move Chloe after arming)");
    log_line("Press F12 to reissue spawn for slot 1 (after capture)");
    log_line("Press F4 to AddCharacter(manager, Nadine) into CharacterManager");
    log_line("Press F1/F2/F3 to set Nadine control state 0/1/2 via owner+0x15C8 -> +0x168");
    log_line("Press F5 to retarget CameraManager(1) to Nadine via cached pair + toggle");
    log_line("Press F6 to log local-player-0/1 targets");
    log_line("Press F7 to test split-screen login chain");
    log_line("Press F8 to toggle manual control");
    log_line("Press F10 to toggle main camera between Chloe and clone");
    log_line("Press F11 to toggle motion backend");
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        const HANDLE thread = CreateThread(
            nullptr,
            0,
            install_hook,
            nullptr,
            0,
            nullptr
        );
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
