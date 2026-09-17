#pragma once
#include <cstdint>

namespace coop::p99_group_scope {
// Per-native-job values only. The Windows wrapper owns TLS lifetime, prevents
// nested exposure and expires this state under __finally. No identity here is
// a lease for another job; partial publication is never presented as rollback.
struct State {
    std::uint8_t entered = 0;
    std::uint8_t completed = 0;
    bool refused = false;

    [[nodiscard]] constexpr bool can_start(std::uint32_t group, bool first_site) const noexcept {
        return !refused && group <= 1 && first_site == (group == 0) &&
            entered == completed && completed == (group == 0 ? 0 : 1);
    }
    [[nodiscard]] constexpr bool enter(std::uint32_t group) noexcept {
        if (!can_start(group,group==0)) return false;
        entered |= static_cast<std::uint8_t>(1u<<group);
        return true;
    }
    constexpr void finish(std::uint32_t group) noexcept {
        if (group > 1 || entered != (group==0?1:3) || completed != (group==0?0:1)) {
            refused=true;
            return;
        }
        completed=entered;
    }
    [[nodiscard]] constexpr bool incomplete() const noexcept {
        return entered != 0 && completed != 3;
    }
};
} // namespace coop::p99_group_scope
