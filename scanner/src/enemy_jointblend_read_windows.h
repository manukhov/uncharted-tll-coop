#pragma once
#include "nadine_p99_group_readability.h"
#include <optional>

namespace coop::enemy_jointblend_read {
enum class Failure : std::uint8_t { none, scope, span, clock, budget, pages, exception };
using Tick = std::uint64_t(*)(void*) noexcept;
// Tick has the collector's bounded, nonthrowing, nonreentrant clock contract;
// it must not run game code or change source ownership/protection.
struct Totals {
    std::uint64_t events{}, queries{}, attempts{}, copies{}, bytes{}, faults{}, refusals{};
};
// One collector owner, not thread-local. Its lease must exclude all other
// invocations, including fibers. Only event-local page observations are cached.
// Output must be private writable caller-owned storage, disjoint from sources.
// A failed copy may leave partial output in rejected diagnostics; never treat
// it as valid evidence. Consumers must require complete(sample), not published.
// Accessibility/SEH are NOT object retention or an atomic snapshot. A concurrent
// protection change can race VirtualQuery, including consuming a new guard bit.
class Reader final {
public:
    Reader() = default;
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    bool begin_event(std::uint64_t start, std::uint64_t allowance, Tick tick, void* context) noexcept;
    void end_event() noexcept;
    bool copy(std::uintptr_t address, void* output, std::size_t bytes) noexcept;
    Failure failure() const noexcept { return failure_; }
    const Totals& totals() const noexcept { return totals_; } // Owner or stopped/quiescent worker only.
    bool active() const noexcept { return pages_.has_value(); }
private:
    std::optional<p99_group_readability::Batch> pages_{};
    Tick tick_{};
    void* context_{};
    std::uint64_t start_{}, allowance_{};
    Failure failure_{};
    Totals totals_{};
    bool refuse(Failure why) noexcept;
    bool within_budget() noexcept;
    bool query(std::uintptr_t address, p99_group_readability::Region& out) noexcept;
};
}
