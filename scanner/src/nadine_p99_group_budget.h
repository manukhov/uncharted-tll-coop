#pragma once
#include <cstdint>

namespace coop::p99_group_budget {
// Cumulative overhead measured only around mod validation/source work, never
// around stock Process/cmd16. This is post-call containment, NOT preemption of
// a blocked OS/native call or a hard frame-time guarantee.
struct Budget {
    std::uint64_t limit = 0;
    std::uint64_t spent = 0;
    bool failed = false;

    [[nodiscard]] bool charge(std::uint64_t begin, std::uint64_t end, bool clock_ok = true) noexcept {
        if (failed) return false;
        if (!clock_ok || limit == 0 || end < begin || spent > limit || end - begin > limit - spent) {
            failed = true;
            return false;
        }
        spent += end - begin;
        return true;
    }
};
// Windows QPC frequency is read once at installation. 1ms cumulative budget.
[[nodiscard]] constexpr std::uint64_t one_millisecond(std::uint64_t frequency) noexcept {
    return frequency / 1000;
}
} // namespace coop::p99_group_budget
