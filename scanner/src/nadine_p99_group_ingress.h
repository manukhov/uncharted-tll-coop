#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace coop::p99_group_ingress {

// Build 1.4.21058.0 only. Redirect the callback value, not the callee's
// unwind-described prologue. Both instructions retain their original opcode.
struct Site {
    std::uintptr_t rva;
    std::size_t size;
    std::size_t displacement_offset;
    std::array<std::uint8_t, 7> stock;
};
inline constexpr std::uintptr_t kNativeJobRva = 0x1A92A00;
inline constexpr std::array<Site, 2> kSites{{
    {0x17815B7, 7, 3, {0x48, 0x8D, 0x05, 0x42, 0x14, 0x31, 0x00}}, // lea rax, job
    {0x1782B3A, 5, 1, {0xE9, 0xC1, 0xFE, 0x30, 0x00, 0x00, 0x00}}, // jmp job
}};
inline constexpr std::size_t kRelayBytes = 14;

// A failed/partial attempt can leave callback relays reachable for the rest
// of the process. Never clear their original target or retry installation.
class InstallationGate {
    std::atomic<bool> started_{false};
public:
    bool begin() { return !started_.exchange(true, std::memory_order_acq_rel); }
};

inline std::optional<std::uintptr_t> add(std::uintptr_t base, std::uintptr_t size) {
    if (size > std::numeric_limits<std::uintptr_t>::max() - base) return {};
    return base + size;
}
inline std::optional<std::int32_t> relative(std::uintptr_t next, std::uintptr_t target) {
    if (target >= next) {
        const auto distance = target - next;
        if (distance > static_cast<std::uintptr_t>(INT32_MAX)) return {};
        return static_cast<std::int32_t>(distance);
    }
    const auto distance = next - target;
    if (distance > std::uintptr_t{0x80000000}) return {};
    return static_cast<std::int32_t>(-static_cast<std::int64_t>(distance));
}

struct Patch {
    std::array<std::uint8_t, 7> instruction{};
    std::array<std::uint8_t, kRelayBytes> relay{};
    std::size_t size = 0;
};

// Value-only planner. A caller must still suspend/check thread RIPs, compare
// ALL sites again and publish/rollback as one transaction. No queued native
// callback is rewritten: jobs already holding the old address bypass the TLS
// wrapper. Empty-scope dispatch stays stock; ndjob switches Windows fibers,
// so thread_local alone cannot isolate suspended jobs. Fiber-bound scope is
// a separate mandatory blocker. This does not claim exactly-once scheduling.
inline std::optional<Patch> plan(std::size_t index, std::uintptr_t module,
    std::span<const std::uint8_t> observed, std::uintptr_t relay, std::uintptr_t hook) {
    static_assert(sizeof(std::uintptr_t) == 8);
    if (index >= kSites.size() || module == 0 || relay == 0 || hook == 0) return {};
    const auto& site = kSites[index];
    if (observed.size() != site.size ||
        !std::equal(observed.begin(), observed.end(), site.stock.begin())) return {};
    const auto address = add(module, site.rva);
    const auto native = add(module, kNativeJobRva);
    if (!address || !native) return {};
    const auto next = add(*address, site.size);
    if (!next || !add(relay, kRelayBytes)) return {};
    const auto expected = relative(*next, *native);
    std::int32_t old = 0;
    std::memcpy(&old, observed.data() + site.displacement_offset, sizeof(old));
    if (!expected || old != *expected) return {};
    const auto displacement = relative(*next, relay);
    if (!displacement) return {};
    Patch result{};
    result.size = site.size;
    std::copy(observed.begin(), observed.end(), result.instruction.begin());
    std::memcpy(result.instruction.data() + site.displacement_offset, &*displacement, 4);
    // FF 25 [rip+0]; dq hook. No register, stack, argument or return-address
    // changes. Leaf relay only; never put relocated instructions here.
    result.relay[0] = 0xFF;
    result.relay[1] = 0x25;
    std::memcpy(result.relay.data() + 6, &hook, sizeof(hook));
    return result;
}

} // namespace coop::p99_group_ingress
