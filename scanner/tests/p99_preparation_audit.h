#pragma once
#include <cstddef>
#include <cstdint>

namespace coop::p99_preparation_audit {
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kMaximumIterations = 1000;
inline constexpr std::uint32_t kOutputBytes = 21 * 48;
enum class Stage : std::uint32_t {
    kNone = 0, kNormalJob = 1, kSelect = 2, kOwner = 3, kCapture = 4,
    kMath = 5, kPostcheck = 6, kSeal = 7, kOperandIdentity = 8, kClock = 9
};
// Phase measurements overlap: owner includes snapshots inside select/seal.
// Total includes instrumentation overhead, not Python/fixture construction.
struct Stats {
    std::uint32_t size = sizeof(Stats), version = kVersion;
    std::uint32_t completed_iterations = 0, failed_stage = 0;
    std::uint64_t qpc_frequency = 0, total_ticks = 0, select_ticks = 0;
    std::uint64_t owner_ticks = 0, capture_ticks = 0, math_ticks = 0;
    std::uint64_t postcheck_ticks = 0, seal_ticks = 0;
    std::uint64_t virtual_queries = 0, resolve_calls = 0, find_calls = 0;
    std::uint64_t retarget_calls = 0, blend_calls = 0, rejected_calls = 0;
    std::uint64_t virtual_query_ticks = 0;
};
static_assert(sizeof(Stats) == 136);
// Offline negative test only: guard the fixed table's first page at a fresh
// postcheck/seal boundary, observe the guard, then restore its protection.
struct GuardMutation {
    std::uint32_t size = sizeof(GuardMutation), stage = 0, applied = 0;
    std::uint32_t old_protection = 0, observed_protection = 0, error = 0;
};
static_assert(sizeof(GuardMutation) == 24);
// Returns0 success; -1 invalid stats; -2 arguments/bounds; -3 busy;
// -4 image/owned-buffer admission; -5 preparation refusal; -6 clock failure;
// -7 unexpected structured exception. A refused iteration is never retried.
using Prepare = int(__cdecl*)(std::uintptr_t, void*, std::uint32_t,
    std::byte*, std::uint32_t, Stats*);
} // namespace coop::p99_preparation_audit
