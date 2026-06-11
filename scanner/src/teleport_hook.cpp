#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "coop_protocol.h"

namespace {

constexpr std::uintptr_t kNadineBuddyVtableRva = 0x2949638;
constexpr std::uintptr_t kDisableBuddyTeleportRva = 0x3D441AD;
constexpr std::uintptr_t kBuildNavigationFrameRva = 0x1DF2D60;
constexpr std::uintptr_t kMoveToRva = 0x3F6890;
constexpr std::uintptr_t kType4CommandRva = 0x3F69B0;
constexpr std::uintptr_t kRequestEnableAiRva = 0x3F5C80;
constexpr std::uintptr_t kRequestDisableAiRva = 0x3F5D30;
constexpr std::uintptr_t kGetCameraManagerRva = 0x1C06420;
constexpr std::uintptr_t kGetCurrentCameraRva = 0x1C0D4B0;
constexpr std::uintptr_t kNetGameManagerGlobalRva = 0x3DD70C0;
constexpr std::uintptr_t kNetPlayerContextGlobalRva = 0x3E08C80;
constexpr std::uintptr_t kNetPlayerSourceArrayARva = 0x3DD7480;
constexpr std::uintptr_t kNetPlayerSourceArrayBRva = 0x3E09568;
constexpr std::uintptr_t kAddOrUpdateNetPlayerTrackerRva = 0xA90550;
constexpr std::uintptr_t kGetNetPlayerTrackerByScreenIndexRva = 0xA90E80;
constexpr std::uintptr_t kNetPlayerTrackerNetRespawnRva = 0xB347C0;
constexpr std::uintptr_t kBuildRespawnTransformBlockRva = 0x1685C30;
constexpr std::uintptr_t kRespawnSeedDefaultVec4Rva = 0x28805D0;
constexpr std::size_t kNetPlayerSourceEntrySize = 0x1C0;
constexpr std::size_t kUpdateSlot = 87;
constexpr std::size_t kWorldPositionOffset = 0x240;
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
constexpr bool kEnableBuiltInSlot1NetPlayerScaffold = true;
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
using AddOrUpdateNetPlayerTrackerFunction =
    void*(__fastcall*)(void*, void*, const void*, std::uint32_t, std::uint8_t);
using GetNetPlayerTrackerByScreenIndexFunction =
    void*(__fastcall*)(void*, std::uint32_t);
using BuildRespawnTransformBlockFunction =
    void*(__fastcall*)(void*, const void*, const void*);
using NetPlayerTrackerNetRespawnFunction =
    void(__fastcall*)(void*, std::int32_t, std::uint8_t, const void*);

UpdateFunction g_original_update = nullptr;
std::uintptr_t* g_nadine_vtable = nullptr;
std::uintptr_t g_module = 0;
bool g_control_enabled = false;
enum class CommandSource : std::uint32_t {
    LocalKeyboard = 0,
    RemoteClient = 1,
};

enum class MotionBackend : std::uint32_t {
    Type2MoveTo = 0,
    Type4Command = 1,
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

void log_line(const char* line) {
    std::ofstream output(
        R"(C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\locomotion_hook.log)",
        std::ios::app
    );
    output << line << '\n';
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
        issue_move_command(object, 0.0F, 0.0F, goal_distance, true);
        control_state.last_command_time = now;
    }

    control_state.was_moving = is_moving;
    control_state.last_input_mask = command.input_mask;
}

void __fastcall hooked_update(void* object) {
    if (object != nullptr &&
        *reinterpret_cast<std::uintptr_t*>(object) ==
            reinterpret_cast<std::uintptr_t>(g_nadine_vtable) &&
        std::string_view(
            reinterpret_cast<const char*>(object) + sizeof(void*)
        ) == "nadine-buddy") {
        if (!g_slot1_scaffold_attempted) {
            g_slot1_scaffold_attempted = true;
            try_slot1_netplayer_respawn_scaffold(object);
        }
        auto& control_state = g_object_states[object];
        if ((GetAsyncKeyState(VK_F9) & 1) != 0) {
            log_camera_transform();
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
            g_motion_backend =
                g_motion_backend == MotionBackend::Type2MoveTo
                    ? MotionBackend::Type4Command
                    : MotionBackend::Type2MoveTo;
            for (auto& [ignored, state] : g_object_states) {
                state.was_moving = false;
                state.last_input_mask = 0;
                state.last_command_time = 0;
            }
            log_line(
                g_motion_backend == MotionBackend::Type2MoveTo
                    ? "Motion backend: Type2MoveTo"
                    : "Motion backend: Type4Command"
            );
        }
        if (object != g_controlled_object) {
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

        const InputCommand command = get_active_command();
        apply_control_command(object, control_state, command);
    }

    g_original_update(object);
}

DWORD WINAPI install_hook(void*) {
    g_module = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(L"tll.exe")
    );
    if (g_module == 0) {
        log_line("tll.exe module not found");
        return 1;
    }

    g_nadine_vtable =
        reinterpret_cast<std::uintptr_t*>(
            g_module + kNadineBuddyVtableRva
        );
    *reinterpret_cast<std::uint8_t*>(
        g_module + kDisableBuddyTeleportRva
    ) = 1;
    g_original_update =
        reinterpret_cast<UpdateFunction>(g_nadine_vtable[kUpdateSlot]);

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
    submit_remote_input_packet(coop::InputPacket{});
    start_udp_receiver();
    log_line("Press F8 to toggle manual control");
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
