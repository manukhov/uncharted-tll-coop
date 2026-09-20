#pragma once
#include <atomic>
#include <cstdint>

namespace coop::appearance {

enum class InitOutcome : std::uint32_t {
    NoOutput, RoleDisabled, LocalMismatch, HashSelected
};

[[nodiscard]] constexpr const char* outcome_name(InitOutcome outcome) noexcept {
    switch (outcome) {
    case InitOutcome::NoOutput: return "no-output";
    case InitOutcome::RoleDisabled: return "role-disabled";
    case InitOutcome::LocalMismatch: return "local-mismatch";
    case InitOutcome::HashSelected: return "hash-selected";
    }
    return "unknown";
}

// Diagnostic admission only: one process-wide instance permits at most four
// init-result log lines. It neither selects an appearance nor reads game data.
class InitLogBudget {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
public:
    [[nodiscard]] bool claim(InitOutcome outcome) noexcept {
        const auto index = static_cast<std::uint32_t>(outcome);
        if (index >= 4) return false;
        const auto bit = std::uint32_t{1} << index;
        return (seen_.fetch_or(bit, std::memory_order_relaxed) & bit) == 0;
    }

private:
    std::atomic<std::uint32_t> seen_{0};
};

} // namespace coop::appearance
