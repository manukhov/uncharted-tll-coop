#include "p99_preparation_audit.h"
#include <array>
#include <iostream>

extern "C" __declspec(dllimport) int P99PrepareAudit(std::uintptr_t, void*, std::uint32_t,
    std::byte*, std::uint32_t, coop::p99_preparation_audit::Stats*);
extern "C" __declspec(dllimport) int P99PrepareAuditGuardAtStage(std::uintptr_t, void*,
    std::byte*, std::uint32_t, coop::p99_preparation_audit::Stats*,
    coop::p99_preparation_audit::GuardMutation*);

int main() {
    namespace audit = coop::p99_preparation_audit;
    unsigned checks = 0, failures = 0;
    const auto check = [&](bool okay, const char* reason) {
        ++checks;
        if (!okay) { ++failures; std::cerr << reason << '\n'; }
    };
    std::array<std::byte, audit::kOutputBytes> output{};
    audit::Stats stats{};
    check(P99PrepareAudit(0, nullptr, 1, output.data(), audit::kOutputBytes, nullptr) == -1, "null stats");
    stats.size = 0;
    check(P99PrepareAudit(0, nullptr, 1, output.data(), audit::kOutputBytes, &stats) == -1, "bad stats size");
    stats = {}; stats.version = 0;
    check(P99PrepareAudit(0, nullptr, 1, output.data(), audit::kOutputBytes, &stats) == -1, "bad stats version");
    stats = {};
    check(P99PrepareAudit(0, nullptr, 1, output.data(), audit::kOutputBytes, &stats) == -2, "null image/job");
    for (const auto iterations : {0u, audit::kMaximumIterations + 1}) {
        stats = {};
        check(P99PrepareAudit(1, output.data(), iterations, output.data(), audit::kOutputBytes, &stats) == -2,
              "iteration bounds refuse before image access");
    }
    stats = {};
    check(P99PrepareAudit(1, output.data(), 1, output.data(), audit::kOutputBytes - 1, &stats) == -2, "output size");
    stats = {};
    check(P99PrepareAudit(1, output.data(), 1, output.data(), audit::kOutputBytes, &stats) == -4, "invalid image");
    check(stats.completed_iterations == 0 && stats.resolve_calls == 0 && stats.find_calls == 0 &&
          stats.retarget_calls == 0 && stats.blend_calls == 0, "no native call on admission failure");
    stats = {};
    check(P99PrepareAuditGuardAtStage(1, output.data(), output.data(), audit::kOutputBytes,
        &stats, nullptr) == -2, "null guard request");
    audit::GuardMutation guard{};
    guard.size = 0;
    check(P99PrepareAuditGuardAtStage(1, output.data(), output.data(), audit::kOutputBytes,
        &stats, &guard) == -2, "guard size");
    for (const auto stage : {0u, 1u, 5u, 8u, 0xFFFFFFFFu}) {
        guard = {}; guard.stage = stage;
        check(P99PrepareAuditGuardAtStage(1, output.data(), output.data(), audit::kOutputBytes,
            &stats, &guard) == -2, "only fresh lock boundaries accept guard requests");
    }
    for (const auto stage : {6u, 7u}) {
        guard = {}; guard.stage = stage;
        check(P99PrepareAuditGuardAtStage(1, output.data(), output.data(), audit::kOutputBytes,
            &stats, &guard) == -4 && guard.applied == 0,
            "valid guard stage still refuses an invalid image without changing protection");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
