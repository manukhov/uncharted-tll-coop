#pragma once

#include "nadine_p99_group_ingress.h"

namespace coop::p99_consumed_frame {

// Typed alias of the existing native CALL bridge, at the same code address.
// The bridge leaves RAX intact while clearing the published return slot.
using Native = std::uintptr_t(__fastcall*)(void*);
extern "C" std::uintptr_t p99_consumed_invoke_native(void* job, Native native,
    std::uintptr_t* return_slot);

// Retail 1.4.21058.0, normal Perform -> owner +0x408 job only. These are
// address equalities, not a stack scan or permission to dereference old frames.
inline constexpr std::uintptr_t kOuterRva = 0x1A928F0;
inline constexpr std::uintptr_t kPerformRva = 0x1A92260;
inline constexpr std::uintptr_t kPerformCallsiteRva = 0x1A92905;
inline constexpr std::uintptr_t kProcessContextLeaRva = 0x1A92745;
inline constexpr std::uintptr_t kProcessCallsiteRva = 0x1A92759;
inline constexpr std::uintptr_t kPostCallsiteRva = 0x1A929A1;
inline constexpr std::uintptr_t kPerformReturnRva = 0x1A9290A;
inline constexpr std::uintptr_t kOuterStackBytes = 0x28;
inline constexpr std::uintptr_t kPerformStackBytes = 0x1B8;
inline constexpr std::uintptr_t kProcessContextOffset = 0x140;
inline constexpr std::uintptr_t kContextToOuterReturnSlot = 0xA8;
inline constexpr std::uintptr_t kPostToOuterReturnSlot = 0x30;
static_assert(kOuterStackBytes + sizeof(std::uintptr_t) + kPerformStackBytes -
    kProcessContextOffset == kContextToOuterReturnSlot);
static_assert(kOuterStackBytes + sizeof(std::uintptr_t) == kPostToOuterReturnSlot);

inline constexpr std::array<std::uint8_t, 21> kOuterPrologue{
    0x48,0x83,0xEC,0x28,0x48,0x89,0x5C,0x24,0x30,0x48,0x8B,
    0x59,0x08,0x48,0x89,0x74,0x24,0x38,0x48,0x8B,0xF1};
inline constexpr std::array<std::uint8_t, 43> kPerformPrologue{
    0x48,0x8B,0xC4,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x81,0xEC,0x90,0x01,0,0,0x48,0xC7,0x80,0x68,0xFF,
    0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0x48,0x89,0x58,0x10,0x48,0x89,
    0x70,0x18,0xC5,0xF8,0x29,0x70,0xC8};
inline constexpr std::array<std::uint8_t, 8> kProcessContextLea{
    0x4C,0x8D,0x84,0x24,0x40,0x01,0,0};
inline constexpr std::array<std::uint8_t, 5> kPerformCallsiteStock{
    0xE8,0x56,0xF9,0xFF,0xFF};
inline constexpr std::array<std::uint8_t, 5> kProcessCallsiteStock{
    0xE8,0x62,0x70,0x02,0x00};
inline constexpr std::array<std::uint8_t, 6> kPostCallsiteStock{
    0xFF,0x90,0x08,0x04,0,0};

[[nodiscard]] inline bool layout_matches(std::span<const std::uint8_t> outer,
    std::span<const std::uint8_t> perform,
    std::span<const std::uint8_t> context_lea) noexcept {
    return std::ranges::equal(outer, kOuterPrologue) &&
        std::ranges::equal(perform, kPerformPrologue) &&
        std::ranges::equal(context_lea, kProcessContextLea);
}

[[nodiscard]] inline constexpr bool owns_process(std::uintptr_t context,
    std::uintptr_t outer_return_slot) noexcept {
    return context != 0 && (context & 15u) == 0 &&
        outer_return_slot != 0 && (outer_return_slot & 15u) == 8 &&
        context <= std::numeric_limits<std::uintptr_t>::max() - kContextToOuterReturnSlot &&
        context + kContextToOuterReturnSlot == outer_return_slot;
}

// return_slot is _AddressOfReturnAddress() in a noinline wrapper reached by
// CALL -> stack-neutral JMP relay, not the saved return-PC value. A 6-byte
// native call replaced by E8+NOP returns to +5; this does not change its slot.
[[nodiscard]] inline constexpr bool owns_post(std::uintptr_t return_slot,
    std::uintptr_t outer_return_slot) noexcept {
    return return_slot != 0 && (return_slot & 15u) == 8 &&
        outer_return_slot != 0 && (outer_return_slot & 15u) == 8 &&
        return_slot <= std::numeric_limits<std::uintptr_t>::max() - kPostToOuterReturnSlot &&
        return_slot + kPostToOuterReturnSlot == outer_return_slot;
}

// New callback-value ingress sites, NOT the old AsyncJointBlend sites. The
// sync thunk has already advanced its job cursor and put job in RCX here.
inline constexpr std::array<p99_group_ingress::Site, 2> kIngressSites{{
    {0x1781560, 7, 3, {0x48,0x8D,0x05,0x89,0x13,0x31,0x00}},
    {0x1782B2A, 5, 1, {0xE9,0xC1,0xFD,0x30,0x00,0,0}},
}};

[[nodiscard]] inline std::optional<p99_group_ingress::Patch> plan_ingress(
    std::size_t index, std::uintptr_t module,
    std::span<const std::uint8_t> observed, std::uintptr_t relay,
    std::uintptr_t hook) noexcept {
    namespace ingress = p99_group_ingress;
    if (index >= kIngressSites.size() || !module || !relay || !hook) return {};
    const auto& site = kIngressSites[index];
    if (observed.size() != site.size ||
        !std::equal(observed.begin(), observed.end(), site.stock.begin())) return {};
    const auto address = ingress::add(module, site.rva);
    const auto native = ingress::add(module, kOuterRva);
    if (!address || !native) return {};
    const auto next = ingress::add(*address, site.size);
    if (!next || !ingress::add(relay, ingress::kRelayBytes)) return {};
    const auto expected = ingress::relative(*next, *native);
    const auto replacement = ingress::relative(*next, relay);
    std::int32_t old = 0;
    std::memcpy(&old, observed.data() + site.displacement_offset, sizeof(old));
    if (!expected || old != *expected || !replacement) return {};
    ingress::Patch patch{};
    patch.size = site.size;
    std::copy(observed.begin(), observed.end(), patch.instruction.begin());
    std::memcpy(patch.instruction.data() + site.displacement_offset, &*replacement, 4);
    patch.relay[0] = 0xFF;
    patch.relay[1] = 0x25;
    std::memcpy(patch.relay.data() + 6, &hook, sizeof(hook));
    return patch;
}

} // namespace coop::p99_consumed_frame
