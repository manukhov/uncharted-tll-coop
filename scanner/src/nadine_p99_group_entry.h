#pragma once
#include <cstdint>

namespace coop::p99_group_entry {
// Reject-only identity, NOT a retained reference or permission to write a pose.
// Native slot/Fg records compare handle64 + generation32, never 16-byte padding.
struct Identity {
    std::uintptr_t handle = 0;
    std::uint32_t generation = 0;
};
[[nodiscard]] constexpr bool same(Identity a, Identity b) noexcept {
    return a.handle != 0 && a.generation != 0 &&
        a.handle == b.handle && a.generation == b.generation;
}
struct Observations {
    Identity slot_before{};
    Identity fg{};
    Identity slot_after{};
};
struct Active {
    bool reentered = false;
};
// Track entry itself, not just the later publication of an admitted job. Source
// preparation is inside this boundary too. The runtime restores the FLS pointer
// under __finally, including foreign/native exception exits.
inline void mark_parent(Active* parent) noexcept {
    if (parent) parent->reentered = true;
}
[[nodiscard]] constexpr bool candidate(const Observations& seen) noexcept {
    // A changed pair fails closed. A matching reread is NOT an atomic snapshot;
    // the existing full owner/Fg/generation selector remains authoritative.
    return same(seen.slot_before, seen.fg) && same(seen.slot_before, seen.slot_after);
}
template<class Read, class Validate>
[[nodiscard]] bool admit(bool eligible, Read read, Validate validate) {
    if (!eligible) return false;
    Observations seen{};
    if (!read(seen) || !candidate(seen)) return false;
    return validate(); // Expensive work cannot run for a rejected identity.
}
} // namespace coop::p99_group_entry
