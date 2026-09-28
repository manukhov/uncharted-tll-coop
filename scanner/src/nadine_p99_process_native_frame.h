#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace coop::p99_process_native_frame {

inline constexpr std::uintptr_t kProcessRva = 0x1AB97C0;
inline constexpr std::uintptr_t kSegmentLeaRva = 0x1ABA96B;
inline constexpr std::uintptr_t kStatusLeaRva = 0x1ABA96F;
inline constexpr std::uintptr_t kSegmentToReturnSlot = 0x1628;
inline constexpr std::uintptr_t kStatusToReturnSlot = 0x1550;
// Retail1.4.21058.0: seven pushes=38, RBP=R-15A8, sub RSP,1670.
// Pin through the complete SUB, not an instruction-splitting32-byte prefix.
inline constexpr std::array<std::uint8_t,33> kNativePrologue{
    0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x8D,0xAC,0x24,0x90,0xEA,0xFF,0xFF,0xB8,0x70,0x16,0,0,
    0xE8,0x32,0x27,0xA3,0,0x48,0x2B,0xE0};
inline constexpr std::array<std::uint8_t,4> kSegmentLea{0x48,0x8D,0x55,0x80};
inline constexpr std::array<std::uint8_t,4> kStatusLea{0x48,0x8D,0x4D,0x58};

[[nodiscard]] inline bool layout_matches(std::span<const std::uint8_t> prologue,
    std::span<const std::uint8_t> segment_lea,
    std::span<const std::uint8_t> status_lea) noexcept {
    return std::ranges::equal(prologue,kNativePrologue) &&
        std::ranges::equal(segment_lea,kSegmentLea) &&
        std::ranges::equal(status_lea,kStatusLea);
}

// Reject-only integer comparison. The expected address MUST have been captured
// independently by the bridge for the current live Process call. Do not derive
// it from status/Segment, read the return PC, scan the stack, or treat this as
// owner/group/allocator admission. Segment+08 is reserved zero, NOT its parent.
[[nodiscard]] inline constexpr bool owns(std::uintptr_t status,
    std::uintptr_t segment, std::uintptr_t expected_return_slot) noexcept {
    return status != 0 && segment != 0 && expected_return_slot != 0 &&
        (status & 15u) == 8 && (segment & 15u) == 0 &&
        (expected_return_slot & 15u) == 8 &&
        status <= std::numeric_limits<std::uintptr_t>::max()-kStatusToReturnSlot &&
        segment <= std::numeric_limits<std::uintptr_t>::max()-kSegmentToReturnSlot &&
        status+kStatusToReturnSlot == expected_return_slot &&
        segment+kSegmentToReturnSlot == expected_return_slot;
}

using Native = std::uint32_t(__fastcall*)(const std::uint32_t*, std::uint32_t,
    const void*, std::uint32_t, const void*, const void*, const void*);

// Nine-argument helper, NOT a replacement for the native seven-argument entry.
// Calls a valid nonnull Native once with unchanged original7; forwards EAX.
// Publishes its imminent native CALL return-slot address, clears on normal
// return. Caller MUST clear its own slot under __finally on abnormal unwind.
// Slot must be live private storage for THIS invocation, not shared with an
// outer bridge during recursive entry. No game pointers or state retained here.
extern "C" std::uint32_t p99_process_invoke_native(const std::uint32_t* commands,
    std::uint32_t words, const void* context, std::uint32_t group,
    const void* a5, const void* a6, const void* a7, Native native,
    std::uintptr_t* return_slot);

} // namespace coop::p99_process_native_frame
