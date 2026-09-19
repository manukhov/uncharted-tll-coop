#pragma once
#include <Windows.h>

namespace coop::injection {
enum class Completion {
    kLoaded,
    kTimedOut,
    kWaitFailed,
    kUnexpectedWait,
    kExitQueryFailed,
    kLoadFailed,
    kStillActive,
};

// A signaled thread handle is the only completion proof. If waiting is
// inconclusive, the remote DLL-path allocation may still be read by LoadLibrary
// and must survive this injector process. Never terminate the remote thread.
template<class QueryExit, class ReleasePath>
[[nodiscard]] Completion finish_load_thread(
    DWORD wait_result, QueryExit query_exit, ReleasePath release_path) noexcept {
    if (wait_result == WAIT_TIMEOUT) return Completion::kTimedOut;
    if (wait_result == WAIT_FAILED) return Completion::kWaitFailed;
    if (wait_result != WAIT_OBJECT_0) return Completion::kUnexpectedWait;

    DWORD exit_code = 0;
    const bool queried = query_exit(exit_code);
    // The wait proved termination even if querying the exit code failed.
    release_path();
    if (!queried) return Completion::kExitQueryFailed;
    if (exit_code == STILL_ACTIVE) return Completion::kStillActive;
    if (exit_code == 0) return Completion::kLoadFailed;
    return Completion::kLoaded;
}
} // namespace coop::injection
