#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <TlHelp32.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
// player => full mechanics, weapons/aim work (no 2-player gating).
volatile bool g_local_make_nadine = true;
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
    coop::StatePacket peer_state{};        // latest received peer state
    std::uint32_t send_seq = 0;
    bool enabled = false;                  // toggled on once peer + puppet ready
};
StateNetState g_state_net{};
// Position offset added to the peer state before applying to the puppet. 0 for
// real two-instance/two-PC play (puppet sits at the peer's actual position).
float g_state_loopback_offset_x = 0.0F;

std::size_t readable_bytes(const void* p);

void log_line(const char* line) {
    std::ofstream output(
        R"(C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\locomotion_hook.log)",
        std::ios::app
    );
    output << line << '\n';
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
std::uint32_t* __fastcall hooked_appearance(void* a1, std::uint32_t* a2) {
    if (a2 != nullptr &&
        (g_local_make_nadine ||
         (g_appearance_swap_active && g_nadine_appearance_hash != 0))) {
        *a2 = 0xd27c074fu;  // hash("nadine")
        return a2;
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
    if (chloe0 == nullptr || clone1 == nullptr || nadine == nullptr) {
        log_line("BODY-DIFF aborted: need all three objects live");
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
    alignas(32) std::byte navigation_frame[0x60]{};
    alignas(16) float target[4]{};
    alignas(16) std::byte facing[0x10]{};
    alignas(16) std::byte move_parameters[0x30]{};
    const auto* world_position = reinterpret_cast<const float*>(
        static_cast<const std::byte*>(object) + kWorldPositionOffset
    );
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
    if (controller == 0) {
        log_line("NadineBuddy locomotion controller is null");
        return;
    }

    const float navigation_radius = *reinterpret_cast<float*>(
        controller + kNavigationRadiusOffset
    );
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
void drive_nadine_toward_peer(void* object, ObjectControlState& control_state) {
    if (!g_state_net.enabled || !g_state_net.has_peer_state.load()) {
        return;
    }
    // Time-delayed path replay: record the incoming peer position each frame and
    // steer Nadine toward where the peer was ~kReplayDelayMs ago. On a single PC
    // (loopback, peer == our own Chloe) this makes Nadine visibly RETRACE the
    // player's route with a lag — unambiguous proof the send->recv->drive path
    // works (distinct from her stock AI follow). For real two-PC play the peer
    // is the other player and the small delay is just normal interpolation lag.
    constexpr ULONGLONG kReplayDelayMs = 1200;
    constexpr int kRingSize = 256;
    struct Sample { float x, z; ULONGLONG t; };
    static Sample ring[kRingSize]{};
    static int ring_head = 0;
    static bool ring_primed = false;

    const ULONGLONG now = GetTickCount64();
    const coop::StatePacket live = g_state_net.peer_state;
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
    const float dx = tgt_x - cur[0];
    const float dz = tgt_z - cur[2];
    const float dist = std::sqrt(dx * dx + dz * dz);
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
    constexpr ULONGLONG kDriveCmdIntervalMs = 140;
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
    while (g_state_net.running.load()) {
        coop::StatePacket packet{};
        const int received = recvfrom(g_state_net.recv_socket,
                                      reinterpret_cast<char*>(&packet),
                                      sizeof(packet), 0, nullptr, nullptr);
        if (received == sizeof(coop::StatePacket) &&
            packet.magic == coop::kStatePacketMagic &&
            packet.version == coop::kStatePacketVersion) {
            g_state_net.peer_state = packet;
            g_state_net.has_peer_state.store(true);
        } else if (received == SOCKET_ERROR) {
            if (!g_state_net.running.load()) {
                break;
            }
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
    const auto* vel = reinterpret_cast<const float*>(static_cast<std::byte*>(local_obj) + kPlayerVelocityOffset);
    coop::StatePacket pkt{};
    pkt.sequence = ++g_state_net.send_seq;
    pkt.timestamp_ms = static_cast<std::uint32_t>(GetTickCount64());
    pkt.flags = 1;
    std::memcpy(pkt.pos, pos, sizeof(pkt.pos));
    std::memcpy(pkt.quat, quat, sizeof(pkt.quat));
    std::memcpy(pkt.velocity, vel, sizeof(pkt.velocity));
    // Replicate the local player's resolved action buttons (slot-0 input block
    // +1304 low 32 bits = jump/melee/dodge/aim) via anim_state so the peer can
    // play the same actions on our puppet.
    auto* blk0 = reinterpret_cast<std::byte*>(g_module + 0x66E4220);
    if (readable_bytes(blk0) >= 0x528) {
        pkt.anim_state = static_cast<std::uint32_t>(
            *reinterpret_cast<const std::uint64_t*>(blk0 + 1304) & 0xFFFFFFFFu);
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
volatile bool g_drive_screen1_cam = false;  // NUMPAD6: drive screen-1 cam + fwd input test
// g_play_as_clone defined earlier (before hooked_resolve_local_player)
// Single-instance test: when there is no network peer, mirror the LOCAL Chloe
// onto the puppet (+2m) so actions/transform can be verified on one PC.
bool g_puppet_local_mirror = true;

bool latest_peer_state(coop::StatePacket& out, void*& puppet) {
    if (!g_puppet_net_apply || g_module == 0) {
        return false;
    }
    puppet = retained_player_object(1);
    if (puppet == nullptr) {
        return false;
    }
    if (g_state_net.enabled && g_state_net.has_peer_state.load()) {
        out = g_state_net.peer_state;
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
    out.anim_state = (readable_bytes(blk0) >= 0x528)
                         ? static_cast<std::uint32_t>(
                               *reinterpret_cast<const std::uint64_t*>(blk0 + 1304) & 0xFFFFFFFFu)
                         : 0;
    out.pos[0] += 2.0F;  // visible beside Chloe
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
        return;
    }
    // Persistence diagnostic: read +0x260 BEFORE we overwrite it. If last frame's
    // written velocity survived (≈ peer speed), the locomotion is NOT overwriting it
    // (good - anim should see it). If it's ~0, the locomotion recomputes it after
    // our write -> the decoupled-anim approach can't work via +0x260.
    {
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - last > 400) {
            last = now;
            auto* vr = reinterpret_cast<const float*>(
                static_cast<std::byte*>(puppet) + kPlayerVelocityOffset);
            const float vmag = std::sqrt(vr[0] * vr[0] + vr[2] * vr[2]);
            const float pmag = std::sqrt(pkt.velocity[0] * pkt.velocity[0] +
                                         pkt.velocity[2] * pkt.velocity[2]);
            std::ostringstream l;
            l << "ANIMTEST +0x260 before=(" << vr[0] << "," << vr[2] << ") mag=" << vmag
              << " peerMag=" << pmag
              << (vmag > 0.5F && pmag > 0.5F ? " PERSISTED" : " (reset/idle)");
            log_line(l.str().c_str());
        }
    }
    auto* world = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kWorldPositionOffset);
    auto* localp = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kPlayerLocalPosOffset);
    auto* quat = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kPlayerQuatOffset);
    // STABLE puppet: hard-set transform to the peer's + zero velocity so the
    // (collisionless) integration can't drift it. Clean position+facing tracking,
    // no flicker. (No run animation in this mode - that is the documented
    // next-session integration; it fought the position tracking.)
    // STABLE network avatar (working MVP): hard-set position+facing + zero velocity.
    std::memcpy(world, pkt.pos, sizeof(pkt.pos));
    std::memcpy(localp, pkt.pos, sizeof(pkt.pos));
    std::memcpy(quat, pkt.quat, sizeof(pkt.quat));
    auto* v0 = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kPlayerVelocityOffset);
    auto* v1 = reinterpret_cast<float*>(static_cast<std::byte*>(puppet) + kPlayerVelocityMirrorOffset);
    v0[0] = v0[1] = v0[2] = 0.0F;
    v1[0] = v1[1] = v1[2] = 0.0F;
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

void net_tick() {
    // hooked_update now fires for every Player each frame; rate-limit the send.
    static ULONGLONG last_send = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last_send >= 16) {  // ~60 Hz
        last_send = now;
        send_local_player_state();
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
        // Drive the remote clone by its OWN locomotion (input) when it exists and
        // we're not locally controlling it (play-as-clone). The old transform-drag
        // (teleport-mirror) is REMOVED: it killed animation + collision. Locomotion-
        // driven = animates + collides + visible from player 1's view. The clone's
        // input is slot-1 (replicated remote input in real co-op).
        if (retained_player_object(1) != nullptr && !g_play_as_clone) {
            g_puppet_net_apply = false;   // no transform drag
            g_drive_slot1 = false;        // clone idles (no input leak); WASD drives
                                          // the main player. Control the clone by
                                          // switching the camera to it (NUMPAD3).
            g_drive_slot_index = 1;
            g_drive_screen1_cam = true;   // keep anim-cam owner alive each frame
        }
        const bool ins = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (ins && !prev_insert) {
            g_velprobe_enabled = !g_velprobe_enabled;
            log_line(g_velprobe_enabled
                         ? "Velocity-field probe ENABLED (run; reads slot0 + controller)"
                         : "Velocity-field probe disabled");
        }
        prev_insert = ins;
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
    if (g_want_nadine_body) {
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
    spawn(g_spawn_desc, 1, g_spawn_ctx, g_spawn_a4, g_spawn_flag, g_spawn_out);
    if (appear_swap) {
        g_appearance_swap_active = false;
        log_line("APPEARSWAP done (clone spawn returned)");
    }
    g_slot1_visual_override_active = false;
    log_line("reissue spawn idx=1 returned");
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

void __fastcall hooked_update(void* object) {
    // DIAGNOSTIC: when inert, the DLL does NOTHING but pass the update through (no
    // clone spawn, no drives, no per-frame logic) - used to isolate whether our
    // active behavior breaks the main player's aiming/firing. Toggle with VK_END.
    if (g_dll_inert) {
        g_original_update(object);
        return;
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
        // VARIANT B: do NOT auto-spawn a 2nd Player clone (it breaks the local
        // player's weapons/aim via 2-player gating). The local player itself is made
        // Nadine via the appearance hook (g_local_make_nadine). Manual F12 still
        // spawns a clone if needed for legacy experiments.
        if (!g_local_make_nadine && g_spawn_captured &&
            retained_player_object(1) == nullptr &&
            !g_slot1_auto_reissue_done &&
            (!g_want_nadine_body || g_nadine_visual_identity_valid)) {
            g_slot1_auto_reissue_done = true;
            reissue_spawn_for_slot1();
            log_line("slot1 auto-spawn armed (no forced movement)");
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
            g_puppet_net_apply = false;
            log_line("clone spawned passive (drive OFF; use NUMPAD3 to control it)");
        }
        g_last_nadine_object = object;
        force_split_render_mode();
        auto& control_state = g_object_states[object];
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
        if ((GetAsyncKeyState(VK_SCROLL) & 1) != 0) {
            fix_clone_collision_filter();  // copy Chloe's havok filter to clone colliders
        }
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
        if ((GetAsyncKeyState(VK_END) & 1) != 0) {
            g_want_nadine_body = !g_want_nadine_body;
            log_line(g_want_nadine_body
                         ? "BODY-SWAP toggle ON: next F12 reissue builds clone with "
                           "Nadine record/skeleton (press F12 to respawn)"
                         : "BODY-SWAP toggle OFF: next F12 reissue = plain Chloe body");
        }
        if ((GetAsyncKeyState(VK_NUMPAD2) & 1) != 0) {
            g_manual_slot1_apply = !g_manual_slot1_apply;
            log_line(g_manual_slot1_apply
                         ? "manual slot1 apply ON"
                         : "manual slot1 apply OFF");
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
            drive_slot1_tick();
            apply_slot1_perslot_on_game_thread();
            g_original_update(object);
            return;
        }

        if (control_state.ai_disabled != g_control_enabled) {
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

    drive_slot1_tick();
    apply_slot1_perslot_on_game_thread();
    g_original_update(object);
    // Puppet (slot-1 Player): let its own update run above (so the anim graph
    // samples the velocity we wrote in net_tick), then assert the networked
    // transform last so the engine can't fight it into a flicker.
    apply_puppet_transform_post(object);
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
    }
    submit_remote_input_packet(coop::InputPacket{});
    start_udp_receiver();
    log_line("local-player-1 -> Nadine bind hook ENABLED");
    log_line("one-shot split render activation ENABLED after login_state==3");
    start_state_net();
    install_xinput_hook();
    g_velprobe_enabled = false;
    CreateThread(nullptr, 0, &aux_input_thread, nullptr, 0, nullptr);
    g_spawn_veh = AddVectoredExceptionHandler(1, &spawn_capture_veh);
    g_velwriter_veh = AddVectoredExceptionHandler(0, &velwriter_capture_veh);
    g_perslot_veh = AddVectoredExceptionHandler(0, &perslot_capture_veh);
    g_inputstate_veh = AddVectoredExceptionHandler(0, &inputstate_capture_veh);
    g_stickread_veh = AddVectoredExceptionHandler(0, &stickread_capture_veh);
    g_inputwrite_veh = AddVectoredExceptionHandler(0, &inputwrite_capture_veh);
    g_inputwrite_stick_veh = AddVectoredExceptionHandler(0, &inputwrite_stick_capture_veh);
    g_visual_seed_veh = AddVectoredExceptionHandler(0, &visual_seed_capture_veh);
    g_packet_look_veh = AddVectoredExceptionHandler(1, &packet_look_veh);
    g_record_swap_veh = AddVectoredExceptionHandler(1, &record_swap_veh);
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
    arm_spawn_breakpoint_all_threads();
    // DIAGNOSTIC input-capture HW breakpoints (perslot / inputstate / inputwrite /
    // stick) are NO LONGER auto-armed: they hook stick/input processing and BREAK
    // AIMING (right stick) for the main player on inject. They were only needed for
    // the original input RE; the clone drive writes FrameState directly without them.
    // Re-arm manually via the Numpad/PageUp hotkeys if input RE is needed again.
    log_line("Spawn-arg capture armed (trigger a checkpoint/respawn to capture)");
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
