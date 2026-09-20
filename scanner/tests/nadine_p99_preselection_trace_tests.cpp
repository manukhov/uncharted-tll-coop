#include "nadine_p99_preselection_trace.h"
#include <cstring>
#include <iostream>
#include <numeric>
#include <utility>

namespace {
namespace trace = coop::p99_preselection_trace;
using trace::Stage;
using trace::Trace;
unsigned checks = 0, failures = 0;

void check(bool value, const char* description) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << description << '\n'; }
}

std::uint64_t total(const Trace& value) {
    return std::accumulate(value.elapsed.begin(), value.elapsed.end(), std::uint64_t{0});
}

void exact_partition_and_names() {
    Trace value;
    constexpr std::array<const char*, 9> names{
        "setup", "normal-job", "select-gate", "retained", "fg-kind", "owner", "target", "registry", "identity"};
    constexpr std::array<std::uint64_t, 10> times{100, 103, 105, 110, 110, 120, 135, 155, 160, 180};
    value.start({times[0], true});
    check(value.started && value.active == Stage::Setup && value.entered_mask == 1 &&
        std::strcmp(value.detail, "setup") == 0, "start initializes setup boundary");
    for (std::uint8_t index = 1; index < names.size(); ++index)
        value.enter(static_cast<Stage>(index), {times[index], true}, names[index]);
    value.skeleton_id = 0xC39CA709;
    value.parameter_count = 6480;
    value.normal_passed = true;
    value.queries = 13;
    value.selected = true;
    value.finish({times.back(), true});
    for (std::uint8_t index = 0; index < names.size(); ++index) {
        check(std::strcmp(trace::stage_name(static_cast<Stage>(index)), names[index]) == 0,
              "exact stage name");
        check(value.elapsed[index] == times[index + 1] - times[index], "each interval charged to prior stage");
    }
    check(value.clock_valid && value.finished && value.entered_mask == 0x1FF && total(value) == 80,
          "nine bins partition complete elapsed interval exactly");
    check(value.skeleton_id == 0xC39CA709 && value.parameter_count == 6480 &&
        value.normal_passed && value.queries == 13 && value.selected,
        "finish preserves caller-provided scalar evidence");
}

void early_refusals_and_boundaries() {
    for (std::uint8_t terminal = 0; terminal < static_cast<std::uint8_t>(Stage::Count); ++terminal) {
        Trace value;
        value.start({7, true});
        for (std::uint8_t index = 1; index <= terminal; ++index)
            value.enter(static_cast<Stage>(index), {std::uint64_t{7} + index, true}, "refused-check");
        value.finish({std::uint64_t{8} + terminal, true});
        check(value.clock_valid && value.finished && !value.selected && !value.normal_passed,
              "refusal never becomes admission evidence");
        check(value.entered_mask == (std::uint32_t{1} << (terminal + 1)) - 1 &&
            total(value) == std::uint64_t{terminal} + 1, "only reached stages have intervals");
        for (std::uint8_t index = terminal + 1; index < static_cast<std::uint8_t>(Stage::Count); ++index)
            check(value.elapsed[index] == 0, "unentered refusal tail stays zero");
    }
    Trace zero;
    zero.start({0, true});
    zero.enter(Stage::SelectGate, {0, true}, nullptr);
    zero.finish({0, true});
    check(zero.clock_valid && total(zero) == 0 && std::strcmp(zero.detail, "select-gate") == 0,
          "equal timestamps and null diagnostic reason are supported");
    Trace repeated;
    repeated.start({2, true});
    repeated.enter(Stage::Setup, {5, true}, "same-stage");
    repeated.finish({8, true});
    check(repeated.elapsed[0] == 6 && repeated.entered_mask == 1 && repeated.clock_valid,
          "repeated stage accumulates without adding mask bits");
}

void invalid_clocks_never_recover() {
    for (unsigned fault = 0; fault < 5; ++fault) {
        Trace value;
        value.start({100, fault != 0});
        if (fault == 1) value.enter(Stage::SelectGate, {120, false}, "bad-stamp");
        else if (fault == 2) value.enter(Stage::SelectGate, {99, true}, "backward");
        else value.enter(Stage::SelectGate, {110, true}, "gate");
        if (fault == 3) value.finish({120, false});
        else if (fault == 4) value.finish({109, true});
        const auto valid_prefix = total(value);
        value.enter(Stage::Owner, {150, true}, "later-valid");
        value.finish({160, true});
        check(!value.clock_valid && value.finished && total(value) == valid_prefix,
              "invalid/backward clocks never fabricate later deltas or recover");
    }
    Trace prefix;
    prefix.start({10, true});
    prefix.enter(Stage::SelectGate, {15, true}, "gate");
    prefix.enter(Stage::Retained, {20, false}, "retained");
    prefix.enter(Stage::Owner, {25, true}, "owner");
    prefix.finish({30, true});
    check(prefix.elapsed[0] == 5 && total(prefix) == 5 && !prefix.clock_valid,
          "clock fault preserves only the measured valid prefix");
    check(prefix.active == Stage::Owner && std::strcmp(prefix.detail, "owner") == 0,
          "diagnostic stage can progress while elapsed charging stays invalid");
}

void invalid_stages_overflow_and_terminal_immutability() {
    for (const auto invalid : {Stage::Count, static_cast<Stage>(255)}) {
        Trace value;
        value.start({0, true});
        value.enter(invalid, {10, true}, "invalid");
        check(!value.clock_valid && value.entered_mask == 1 && value.active == Stage::Setup &&
            total(value) == 0, "invalid next enum fails before indexing or shifting");
        check(std::strcmp(trace::stage_name(invalid), "unknown") == 0, "invalid stage name bounded");
        value.finish({20, true});
        check(!value.clock_valid && total(value) == 0, "invalid enum cannot recover with a valid stamp");
        Trace corrupted;
        corrupted.start({0, true});
        corrupted.active = invalid;
        corrupted.finish({10, true});
        check(!corrupted.clock_valid && corrupted.finished && total(corrupted) == 0,
              "invalid active enum cannot index elapsed storage");
    }
    for (const auto delta : {2ull, 3ull}) {
        Trace value;
        value.start({10, true});
        value.elapsed[0] = UINT64_MAX - 2;
        value.finish({10 + delta, true});
        check(value.clock_valid == (delta == 2), "exact uint64 boundary allowed, overflow rejected");
        check(value.elapsed[0] == (delta == 2 ? UINT64_MAX : UINT64_MAX - 2),
              "overflow never wraps or partially charges the interval");
    }
    Trace completed;
    completed.start({UINT64_MAX - 2, true});
    completed.finish({UINT64_MAX, true});
    completed.finish({0, false});
    completed.enter(Stage::Owner, {0, false}, "late");
    completed.start({0, false});
    check(completed.clock_valid && completed.finished && total(completed) == 2 &&
        completed.entered_mask == 1 && completed.active == Stage::Setup,
        "double finish and post-finish updates are inert");
    Trace unstarted;
    unstarted.enter(Stage::Owner, {10, true}, "unstarted");
    unstarted.start({20, true});
    unstarted.finish({30, true});
    check(!unstarted.clock_valid && total(unstarted) == 0, "use before start cannot recover clock validity");
    Trace restart;
    restart.start({10, true});
    restart.enter(Stage::Owner, {12, true}, "owner");
    restart.start({20, true});
    restart.finish({30, true});
    check(!restart.clock_valid && restart.active == Stage::Owner && total(restart) == 2,
          "second start cannot erase diagnostic history or reset elapsed accounting");
    Trace no_start;
    no_start.finish({0, true});
    check(no_start.finished && !no_start.clock_valid && no_start.entered_mask == 0,
          "finish without start fails closed");
}
} // namespace

static_assert(std::is_trivially_destructible_v<Trace>);
static_assert(std::is_trivially_copyable_v<Trace>);
static_assert(std::is_standard_layout_v<Trace>);
static_assert(noexcept(std::declval<Trace&>().start({})));
static_assert(noexcept(std::declval<Trace&>().enter(Stage::Owner, {}, nullptr)));
static_assert(noexcept(std::declval<Trace&>().finish({})));

int main() {
    exact_partition_and_names();
    early_refusals_and_boundaries();
    invalid_clocks_never_recover();
    invalid_stages_overflow_and_terminal_immutability();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
