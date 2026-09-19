#include "dll_injection_completion.h"
#include <iostream>

namespace {
unsigned checks = 0, failures = 0;
void check(bool value, const char* description) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << description << '\n'; }
}
void run_case(DWORD wait_result, bool query_ok, DWORD exit_code,
              coop::injection::Completion expected, bool completed) {
    unsigned queries = 0, releases = 0;
    bool ordered = true;
    const auto result = coop::injection::finish_load_thread(wait_result,
        [&](DWORD& value) {
            ++queries;
            ordered = ordered && releases == 0;
            value = exit_code;
            return query_ok;
        },
        [&] {
            ordered = ordered && queries == 1;
            ++releases;
        });
    check(result == expected, "exact completion classification");
    check(queries == (completed ? 1u : 0u), "query only after a completed wait");
    check(releases == (completed ? 1u : 0u), "retain path unless thread termination is proven");
    check(ordered, "completed cleanup follows exit query exactly once");
}
} // namespace

int main() {
    using C = coop::injection::Completion;
    run_case(WAIT_TIMEOUT, true, 1, C::kTimedOut, false);
    run_case(WAIT_FAILED, true, 1, C::kWaitFailed, false);
    run_case(WAIT_ABANDONED_0, true, 1, C::kUnexpectedWait, false);
    run_case(WAIT_OBJECT_0 + 1, true, 1, C::kUnexpectedWait, false);
    run_case(WAIT_OBJECT_0, false, 1, C::kExitQueryFailed, true);
    run_case(WAIT_OBJECT_0, true, 0, C::kLoadFailed, true);
    run_case(WAIT_OBJECT_0, true, STILL_ACTIVE, C::kStillActive, true);
    run_case(WAIT_OBJECT_0, true, 1, C::kLoaded, true);
    run_case(WAIT_OBJECT_0, true, 0x80000000u, C::kLoaded, true);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
