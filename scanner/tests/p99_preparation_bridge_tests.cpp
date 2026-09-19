#include "p99_preparation_audit.h"
#include <array>
#include <iostream>

extern "C" __declspec(dllimport) int P99PrepareAudit(std::uintptr_t, void*, std::uint32_t,
    std::byte*, std::uint32_t, coop::p99_preparation_audit::Stats*);
extern "C" __declspec(dllimport) int P99PrepareAuditGuardAtStage(std::uintptr_t, void*,
    std::byte*, std::uint32_t, coop::p99_preparation_audit::Stats*,
    coop::p99_preparation_audit::GuardMutation*);
extern "C" __declspec(dllimport) int P99PrepareAuditTrace(std::uintptr_t, void*,
    std::byte*, std::uint32_t, coop::p99_preparation_audit::Stats*,
    coop::p99_preparation_audit::QueryTrace*);

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
    check(sizeof(audit::Stats) == 136 && sizeof(audit::GuardMutation) == 24,
        "existing audit ABI unchanged");
    check(sizeof(audit::QuerySample) == 40 && offsetof(audit::QuerySample, ticks) == 24 &&
        offsetof(audit::QuerySample, last_stage) == 32 && offsetof(audit::QuerySample, flags) == 36,
        "raw query sample ABI");
    check(sizeof(audit::QueryTrace) == 5144 && offsetof(audit::QueryTrace, entries) == 24,
        "bounded query trace ABI");
    check(audit::kQueryResult == 1 && audit::kQueryCommitted == 2 &&
        audit::kQueryReadable == 4 && audit::kQueryGuarded == 8, "raw query flags ABI");
    audit::QueryTrace trace{};
    check(trace.size == 5144 && trace.version == 1 && trace.capacity == 128 &&
        trace.count == 0 && trace.overflow == 0 && trace.reserved == 0, "trace defaults");
    stats = {};
    check(P99PrepareAuditTrace(1, output.data(), output.data(), audit::kOutputBytes,
        &stats, nullptr) == -2, "null trace request");
    check(P99PrepareAuditTrace(1, output.data(), output.data(), audit::kOutputBytes,
        &stats, reinterpret_cast<audit::QueryTrace*>(std::uintptr_t{1})) == -2,
        "unreadable trace request refuses before image access");
    check(P99PrepareAuditTrace(1, output.data(), output.data(), audit::kOutputBytes,
        nullptr, &trace) == -1, "trace export retains stats admission");
    const auto refused_trace = [&]() {
        stats = {};
        const int result = P99PrepareAuditTrace(1, output.data(), output.data(), audit::kOutputBytes,
            &stats, &trace);
        return result == -2 && stats.completed_iterations == 0 && stats.virtual_queries == 0 &&
            stats.resolve_calls == 0 && stats.find_calls == 0 && stats.retarget_calls == 0 &&
            stats.blend_calls == 0;
    };
    trace.size = 0;
    check(refused_trace(), "bad trace size refuses before image access");
    trace = {}; trace.version = 0;
    check(refused_trace(), "bad trace version refuses before image access");
    for (const auto capacity : {0u, audit::kQueryTraceCapacity + 1, 0xFFFFFFFFu}) {
        trace = {}; trace.capacity = capacity;
        check(refused_trace(), "trace capacity bounds refuse before image access");
    }
    trace = {}; trace.reserved = 1;
    check(refused_trace(), "nonzero trace reserved field refuses before image access");
    for (const auto capacity : {1u, audit::kQueryTraceCapacity}) {
        trace = {}; trace.capacity = capacity;
        trace.count = trace.overflow = 0xFFFFFFFFu;
        for (auto& sample : trace.entries) sample = {1, 2, 3, 4, 5, 6};
        stats = {};
        output.fill(std::byte{0xA5});
        check(P99PrepareAuditTrace(1, output.data(), output.data(), audit::kOutputBytes,
            &stats, &trace) == -4, "valid trace still refuses invalid image");
        bool entries_zero = true;
        for (const auto& sample : trace.entries)
            entries_zero = entries_zero && sample.address == 0 && sample.region_base == 0 &&
                sample.region_bytes == 0 && sample.ticks == 0 && sample.last_stage == 0 && sample.flags == 0;
        bool output_zero = true;
        for (const auto byte : output) output_zero = output_zero && byte == std::byte{0};
        check(trace.size == sizeof(trace) && trace.version == audit::kVersion &&
            trace.capacity == capacity && trace.count == 0 && trace.overflow == 0 &&
            trace.reserved == 0 && entries_zero && output_zero,
            "trace/output reset precedes preparation and preserves trace configuration");
        check(stats.completed_iterations == 0 && stats.virtual_queries == 0 && stats.resolve_calls == 0 &&
            stats.find_calls == 0 && stats.retarget_calls == 0 && stats.blend_calls == 0,
            "trace admission failure never invokes a retail native");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
