#pragma once

#include <array>
#include <cstdint>

namespace coop::enemy_jointblend_hooks {

using JointBlend = void(__fastcall*)(void* job);
using Reset = void*(__fastcall*)(void* cache, const void* root);
using Process = std::uint32_t(__fastcall*)(const std::uint32_t* commands,
    std::uint32_t words, const void* context, std::uint32_t group,
    const void* phase_callback, const void* postpose_callback,
    const void* validation_callback);
using Postpose = std::uintptr_t(__fastcall*)(void* work_buffer, void* segment,
    const void* context, std::uint32_t id, const void* payload,
    const void* joint_set);
using Primary = void(__fastcall*)(void* composite);
using Store = std::int32_t(__fastcall*)(void* body, const void* converted_pose);

struct NativeCalls {
    JointBlend jointblend = nullptr;
    Reset reset = nullptr;
    Process process = nullptr;
    Postpose postpose = nullptr;
    Primary primary = nullptr;
    Store store = nullptr;
};

enum class Kind : std::uint8_t {
    Entry, Exit, ResetEnter, ResetReturn, ProcessEnter, ProcessReturn,
    PostposeEnter, PostposeReturn, PrimaryEnter, PrimaryReturn,
    StoreEnter, StoreReturn, Reentry, Exception,
};
enum class ProcessSite : std::uint8_t { none, first, later };

// Value-only, synchronous observations. These addresses are NOT retained-object
// leases or permission to dereference them after emit returns. No game objects
// are read by this package. In particular, Store ancestry belongs to the guarded
// synchronous collector, not to these ABI wrappers.
struct Event {
    Kind kind = Kind::Entry;
    ProcessSite site = ProcessSite::none;
    std::uint64_t invocation_nonce = 0;
    std::uintptr_t job = 0;
    // ORIGINAL native entry slots published by the two assembly bridges, not
    // wrapper slots. Zero before bridge entry and after normal bridge return.
    std::uintptr_t original_jointblend_return_slot = 0;
    std::uintptr_t original_process_return_slot = 0;
    std::uintptr_t wrapper_entry_return_pc = 0;
    std::uintptr_t wrapper_entry_return_slot = 0;
    std::array<std::uintptr_t, 7> arguments{};
    // Process is zero-extended uint32, Store is its unchanged low32 bit pattern,
    // Postpose is opaque full RAX. Void calls report zero.
    std::uintptr_t result = 0;
    // Exit is emitted once for normal return or SEH unwind of an observed outer
    // JointBlend. Forced fiber deletion need not unwind and can omit Exit: a
    // worker must stop/timeout without publishing or reusing an active sample.
    // Only Exit ends that scope; inner Exception merely invalidates it.
    bool normal_return = false;
};

struct Sink {
    void* context = nullptr;
    // Fast atomic gate only: no game reads, allocation, logging or blocking.
    bool (*requested)(void*) noexcept = nullptr;
    // Bounded synchronous observer. Must not throw or retain borrowed pointers.
    void (*emit)(void*, const Event&) noexcept = nullptr;
};

struct Targets {
    // JointBlend callback, JointBlend tail, Reset, first Process, later Process,
    // Primary, Store. Both JointBlend entries share the same wrapper.
    std::array<std::uintptr_t, 7> code{};
    std::uintptr_t postpose = 0;
};

// One attempt, before publishing any hook. Own enemy FLS slots; no P99 slot or
// observer state is reused. Never free slots/unload this package while patched,
// queued, running or suspended native callbacks can still reach the wrappers.
// configure/targets preserve LastError. No patch is installed here.
[[nodiscard]] bool configure(const NativeCalls& native, Sink sink) noexcept;
[[nodiscard]] Targets targets() noexcept; // All zero until configure succeeds.

#if defined(TLL_ENEMY_HOOK_AUDIT)
// Separate deterministic fixture only. Not compiled into the real core or
// performance fixture; no reset/reconfiguration surface in production.
struct AuditSnapshot {
    std::uint64_t reads{}, writes{}, bootstraps{}, entered_wrappers{};
};
[[nodiscard]] AuditSnapshot audit_snapshot() noexcept;
void audit_exhaust_counter() noexcept; // Terminal fixture case only.
#endif

} // namespace coop::enemy_jointblend_hooks
