#pragma once

#include <cstdint>

namespace coop::p99_wrist_invoke {

using Native = std::uint32_t*(__fastcall*)(std::uint32_t*, const void*);
using Capture = bool(__fastcall*)(void* cookie, const void* original_context);

enum class Phase : std::uint8_t {
    ready, native, callback, captured, rejected, native_fault, callback_fault,
    reentered,
};

// Caller-owned, single-execution/fiber state, NOT a concurrent owner/epoch gate.
// Never reset/reuse an active or terminal state. No game pointer is retained.
struct State {
    Phase phase = Phase::ready;
    bool active = false;
};

// Ordinary compiled Windows x64 call wrapper, not a detour or retail adapter.
// DO NOT patch the game's two-argument CALL directly to this six-argument
// helper. observation_runtime.inl supplies the distinct two-argument adapter;
// that route remains uninstalled until its complete live admission is audited.
// Native must be a valid nonnull function and is called exactly once, including
// terminal/reentrant paths, with the original two arguments. Its exceptions
// propagate; there is no retry. A nested call using this State cancels capture.
// Capture runs only after normal native return; its SEH faults are contained.
// It may fill ONLY an unpublished owned buffer: publish/use it only if the final
// phase is captured. Callback reentry or a fault can leave partial bytes there.
// No native addresses, FLS, allocation, pose changes, owner admission or I/O.
// The original native result and post-native Windows last-error are preserved.
#if defined(TLL_P99_WRIST_ABI_ARTIFACT)
extern "C" // Versioned .def alias names this exact body, without an extra call.
#endif
__declspec(noinline) std::uint32_t* invoke(State& state, Native native,
    std::uint32_t* status, const void* original_context,
    Capture capture, void* cookie);

} // namespace coop::p99_wrist_invoke
