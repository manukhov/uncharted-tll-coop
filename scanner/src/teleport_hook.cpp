#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace {

constexpr std::uintptr_t kNadineBuddyVtableRva = 0x2949638;
constexpr std::uintptr_t kDisableBuddyTeleportRva = 0x3D441AD;
constexpr std::uintptr_t kBuildNavigationFrameRva = 0x1DF2D60;
constexpr std::uintptr_t kMoveToRva = 0x3F6890;
constexpr std::size_t kUpdateSlot = 87;
constexpr std::size_t kWorldPositionOffset = 0x240;
constexpr std::size_t kOrientationOffset = 0x140;
constexpr std::size_t kLocomotionControllerOffset = 0x15B8;
constexpr std::size_t kNavigationRadiusOffset = 0x1C8;
constexpr std::size_t kFacingOffset = 0x24A0;
constexpr std::size_t kMoveParametersOffset = 0x24B0;
constexpr ULONGLONG kCommandIntervalMs = 50;
constexpr float kGoalDistance = 1.25F;

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

UpdateFunction g_original_update = nullptr;
std::uintptr_t* g_nadine_vtable = nullptr;
std::uintptr_t g_module = 0;
bool g_control_enabled = false;
struct ObjectControlState {
    bool was_moving = false;
    ULONGLONG last_command_time = 0;
    std::uint8_t last_input_mask = 0;
};
std::unordered_map<void*, ObjectControlState> g_object_states;

void log_line(const char* line) {
    std::ofstream output(
        R"(C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\locomotion_hook.log)",
        std::ios::app
    );
    output << line << '\n';
}

void issue_move_command(
    void* object,
    float direction_x,
    float direction_y,
    bool log_target
) {
    alignas(32) std::byte navigation_frame[0x60]{};
    alignas(16) float target[4]{};
    alignas(16) std::byte facing[0x10]{};
    alignas(16) std::byte move_parameters[0x30]{};
    const auto* world_position = reinterpret_cast<const float*>(
        static_cast<const std::byte*>(object) + kWorldPositionOffset
    );
    target[0] = world_position[0] + direction_x * kGoalDistance;
    target[1] = world_position[1] + direction_y * kGoalDistance;
    target[2] = world_position[2];
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

void __fastcall hooked_update(void* object) {
    if (object != nullptr &&
        *reinterpret_cast<std::uintptr_t*>(object) ==
            reinterpret_cast<std::uintptr_t>(g_nadine_vtable) &&
        std::string_view(
            reinterpret_cast<const char*>(object) + sizeof(void*)
        ) == "nadine-buddy") {
        auto& control_state = g_object_states[object];
        if ((GetAsyncKeyState(VK_F8) & 1) != 0) {
            g_control_enabled = !g_control_enabled;
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

        float local_forward = 0.0F;
        float local_right = 0.0F;
        std::uint8_t input_mask = 0;
        if (g_control_enabled) {
            if ((GetAsyncKeyState('W') & 0x8000) != 0) {
                local_forward += 1.0F;
                input_mask |= 1;
            }
            if ((GetAsyncKeyState('S') & 0x8000) != 0) {
                local_forward -= 1.0F;
                input_mask |= 2;
            }
            if ((GetAsyncKeyState('A') & 0x8000) != 0) {
                local_right -= 1.0F;
                input_mask |= 4;
            }
            if ((GetAsyncKeyState('D') & 0x8000) != 0) {
                local_right += 1.0F;
                input_mask |= 8;
            }
        }

        const auto* orientation = reinterpret_cast<const float*>(
            static_cast<const std::byte*>(object) + kOrientationOffset
        );
        const float qx = orientation[0];
        const float qy = orientation[1];
        const float qz = orientation[2];
        const float qw = orientation[3];
        const float forward_x =
            1.0F - 2.0F * (qy * qy + qz * qz);
        const float forward_y = 2.0F * (qx * qy + qw * qz);
        const float right_x = 2.0F * (qx * qy - qw * qz);
        const float right_y =
            1.0F - 2.0F * (qx * qx + qz * qz);
        float direction_x =
            forward_x * local_forward + right_x * local_right;
        float direction_y =
            forward_y * local_forward + right_y * local_right;
        const float length = std::sqrt(
            direction_x * direction_x + direction_y * direction_y
        );
        const bool is_moving = length > 0.0F;
        const ULONGLONG now = GetTickCount64();
        const bool input_changed =
            input_mask != control_state.last_input_mask;
        if (is_moving &&
            (input_changed ||
             now - control_state.last_command_time >=
                 kCommandIntervalMs)) {
            issue_move_command(
                object,
                direction_x / length,
                direction_y / length,
                input_changed
            );
            control_state.last_command_time = now;
        } else if (!is_moving && control_state.was_moving) {
            issue_move_command(object, 0.0F, 0.0F, true);
            control_state.last_command_time = now;
        }
        control_state.was_moving = is_moving;
        control_state.last_input_mask = input_mask;
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
    log_line("NadineBuddy WASD hook installed; press F8 to toggle");
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
