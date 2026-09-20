#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace coop::p99_preselection_trace {

enum class Stage : std::uint8_t {
    Setup, NormalJob, SelectGate, Retained, FgKind, Owner, Target, Registry, Identity, Count
};

[[nodiscard]] constexpr const char* stage_name(Stage stage) noexcept {
    switch (stage) {
    case Stage::Setup: return "setup";
    case Stage::NormalJob: return "normal-job";
    case Stage::SelectGate: return "select-gate";
    case Stage::Retained: return "retained";
    case Stage::FgKind: return "fg-kind";
    case Stage::Owner: return "owner";
    case Stage::Target: return "target";
    case Stage::Registry: return "registry";
    case Stage::Identity: return "identity";
    case Stage::Count: break;
    }
    return "unknown";
}

struct Stamp {
    std::uint64_t ticks = 0;
    bool valid = false;
};

// Local diagnostics, never admission or borrowed-memory authority. Callers
// provide timestamps and evidence already read by the unchanged checks. The
// entered mask identifies reached trace boundaries, not successful predicates.
// Instrumentation and debugger/scheduling overhead remain in elapsed wall time;
// these bins cannot establish uninstrumented production performance.
struct Trace {
    std::array<std::uint64_t, 9> elapsed{};
    std::uint32_t entered_mask = 0;
    Stage active = Stage::Setup;
    const char* detail = "not-started";
    bool clock_valid = true;
    bool finished = false;
    bool selected = false;
    std::uint64_t queries = 0;
    std::uint32_t skeleton_id = 0;
    std::uint16_t parameter_count = 0;
    bool normal_passed = false;
    Stamp last{};
    bool started = false;

    void start(Stamp begin) noexcept {
        if (finished) return;
        if (started) { clock_valid = false; return; }
        started = true;
        active = Stage::Setup;
        detail = "setup";
        entered_mask |= std::uint32_t{1};
        last = begin;
        clock_valid = clock_valid && begin.valid;
    }

    void enter(Stage next, Stamp now, const char* reason) noexcept {
        if (finished) return;
        const auto index = static_cast<std::uint8_t>(next);
        if (!started || index >= static_cast<std::uint8_t>(Stage::Count)) {
            clock_valid = false;
            return;
        }
        charge(now);
        active = next;
        entered_mask |= std::uint32_t{1} << index;
        detail = reason != nullptr ? reason : stage_name(next);
    }

    void finish(Stamp end) noexcept {
        if (finished) return;
        charge(end);
        finished = true;
    }

private:
    void charge(Stamp now) noexcept {
        const auto index = static_cast<std::uint8_t>(active);
        if (!started || !clock_valid || !last.valid || !now.valid ||
            now.ticks < last.ticks || index >= static_cast<std::uint8_t>(Stage::Count)) {
            clock_valid = false;
            return;
        }
        const auto delta = now.ticks - last.ticks;
        if (delta > std::numeric_limits<std::uint64_t>::max() - elapsed[index]) {
            clock_valid = false;
            return;
        }
        elapsed[index] += delta;
        last = now;
    }
};

static_assert(static_cast<std::uint8_t>(Stage::Count) == 9);
static_assert(std::is_standard_layout_v<Trace>);
static_assert(std::is_trivially_copyable_v<Trace>);
static_assert(std::is_trivially_destructible_v<Trace>);

} // namespace coop::p99_preselection_trace
