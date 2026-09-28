#include "nadine_p99_wrist_invoke.h"
#include <Windows.h>

namespace coop::p99_wrist_invoke {

std::uint32_t* invoke(State& state, Native native,
    std::uint32_t* status, const void* original_context,
    Capture capture, void* cookie) {
    // The outer call owns cleanup. Never let a nested call revive its grant.
    if (state.active) state.phase = Phase::reentered;
    if (state.phase != Phase::ready)
        return native(status, original_context);

    state.phase = Phase::native;
    state.active = true;
    std::uint32_t* result = nullptr;
    bool returned = false;
    __try {
        // Keep this OUTSIDE the capture exception handler. In particular, a
        // native fault must not be swallowed and must never trigger a retry.
        result = native(status, original_context);
        returned = true;
        const DWORD native_error = GetLastError();
        if (state.phase == Phase::native) {
            state.phase = Phase::callback;
            bool accepted = false;
            bool callback_returned = false;
            __try {
                accepted = capture != nullptr && capture(cookie, original_context);
                callback_returned = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                state.phase = Phase::callback_fault;
            }
            if (callback_returned && state.phase == Phase::callback)
                state.phase = accepted ? Phase::captured : Phase::rejected;
        }
        SetLastError(native_error);
    } __finally {
        state.active = false;
        if (!returned) state.phase = Phase::native_fault;
    }
    return result;
}

} // namespace coop::p99_wrist_invoke
