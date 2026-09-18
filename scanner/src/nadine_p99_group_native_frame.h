#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace coop::p99_group_native_frame {

// Retail 1.4.21058.0: five pushes (0x28) plus sub rsp,0x5D0.
// Process sites form their argument3 from this exact native stack frame.
inline constexpr std::uintptr_t kNativeStackBytes = 0x5F8;
inline constexpr std::array<std::uintptr_t, 2> kContextOffsets{0x110, 0x160};
inline constexpr std::array<std::uintptr_t, 2> kContextLeaRvas{0x1A92E64, 0x1A932E4};
inline constexpr std::array<std::uint8_t, 43> kNativePrologue{
    0x48,0x8B,0xC4,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x81,0xEC,0xD0,0x05,0,0,0x48,0xC7,0x80,0x48,0xFB,0xFF,
    0xFF,0xFE,0xFF,0xFF,0xFF,0x48,0x89,0x58,0x10,0x48,0x89,0x70,
    0x18,0xC5,0xF8,0x29,0x70,0xC8};
inline constexpr std::array<std::array<std::uint8_t, 8>, 2> kContextLeas{{
    {0x4C,0x8D,0x84,0x24,0x10,0x01,0x00,0x00},
    {0x4C,0x8D,0x84,0x24,0x60,0x01,0x00,0x00},
}};

[[nodiscard]] inline bool layout_matches(std::span<const std::uint8_t> prologue,
    std::span<const std::uint8_t> first_lea, std::span<const std::uint8_t> second_lea) noexcept {
    return std::ranges::equal(prologue, kNativePrologue) &&
        std::ranges::equal(first_lea, kContextLeas[0]) &&
        std::ranges::equal(second_lea, kContextLeas[1]);
}

// Pure reject-only address comparison: no stack scanning, return-address read,
// borrowed-object access or assumption that equal Fg/job pointers mean equal
// invocations. A live recursive native invocation has a different return slot.
[[nodiscard]] inline constexpr bool owns(bool first_site, std::uintptr_t context,
    std::uintptr_t expected_return_slot) noexcept {
    if (context == 0 || (context & 15u) != 0 ||
        expected_return_slot == 0 || (expected_return_slot & 15u) != 8) return false;
    const auto distance = kNativeStackBytes - kContextOffsets[first_site ? 0 : 1];
    return context <= std::numeric_limits<std::uintptr_t>::max() - distance &&
        context + distance == expected_return_slot;
}

struct Admission {
    bool same_invocation = false;
    bool may_consider_overlay = false;
};

// Nadine has eight native groups. Groups2..7 remain stock, but they are not
// foreign/reentrant calls when their exact native frame still matches.
[[nodiscard]] inline constexpr Admission admit(bool first_site,
    std::uintptr_t context, std::uintptr_t expected_return_slot,
    std::uint32_t group) noexcept {
    const bool same = owns(first_site, context, expected_return_slot);
    return {same, same && group <= 1};
}

using Native = void(__fastcall*)(void*);

// Windows x64 MASM implementation has real .pdata/.xdata and calls Native once.
// It sets *return_slot immediately before CALL and clears on normal return.
// The C++ caller MUST clear under __finally too: an exception can unwind past
// the assembly clear. Slot storage is caller-owned and must outlive that call.
extern "C" void p99_group_invoke_native(void* job, Native native,
    std::uintptr_t* return_slot);

} // namespace coop::p99_group_native_frame
