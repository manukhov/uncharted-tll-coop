#include "enemy_jointblend_hooks.h"

#include "nadine_p99_group_fiber.h"
#include "nadine_p99_group_native_frame.h"
#include "nadine_p99_process_native_frame.h"

#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <limits>

namespace coop::enemy_jointblend_hooks {
namespace {

#if defined(TLL_ENEMY_HOOK_AUDIT)
std::atomic<std::uint64_t> g_reads{}, g_writes{}, g_bootstraps{};
#endif

// Reuse only the reviewed generic slot mechanism. These two FLS allocations
// and their bootstrap marker are enemy-private, never P99 observer state.
struct EnemyWindowsApi {
    inline static std::atomic<DWORD> bootstrap_index{FLS_OUT_OF_INDEXES};
    inline static const unsigned char bootstrap_marker = 0;
    static DWORD allocate() noexcept {
        const DWORD error = GetLastError();
        const DWORD index = FlsAlloc(nullptr);
        if (index != FLS_OUT_OF_INDEXES) {
            const DWORD bootstrap = FlsAlloc(nullptr);
            if (bootstrap == FLS_OUT_OF_INDEXES) {
                FlsFree(index); // Still unpublished; no wrapper can use it.
                SetLastError(error);
                return FLS_OUT_OF_INDEXES;
            }
            bootstrap_index.store(bootstrap, std::memory_order_release);
        }
        SetLastError(error);
        return index;
    }
    static p99_group_fiber::Identity identity() noexcept {
        const DWORD error = GetLastError();
        const auto id = IsThreadAFiber() ? p99_group_fiber::Identity{GetCurrentFiber(), 0}
            : p99_group_fiber::Identity{nullptr, GetCurrentThreadId()};
        SetLastError(error);
        return id;
    }
    static bool read(DWORD index, void*& value) noexcept {
#if defined(TLL_ENEMY_HOOK_AUDIT)
        g_reads.fetch_add(1, std::memory_order_relaxed);
#endif
        const DWORD error = GetLastError();
        const bool ok = p99_group_fiber::read_bootstrapped([&]() {
            SetLastError(ERROR_SUCCESS);
            void* result = FlsGetValue(index);
            return p99_group_fiber::ReadResult{result, GetLastError()};
        }, []() {
#if defined(TLL_ENEMY_HOOK_AUDIT)
            g_bootstraps.fetch_add(1, std::memory_order_relaxed);
#endif
            const DWORD bootstrap = bootstrap_index.load(std::memory_order_acquire);
            return bootstrap != FLS_OUT_OF_INDEXES &&
                FlsSetValue(bootstrap, const_cast<unsigned char*>(&bootstrap_marker)) != FALSE;
        }, value);
        SetLastError(error);
        return ok;
    }
    static bool write(DWORD index, void* value) noexcept {
#if defined(TLL_ENEMY_HOOK_AUDIT)
        g_writes.fetch_add(1, std::memory_order_relaxed);
#endif
        const DWORD error = GetLastError();
        const bool ok = FlsSetValue(index, value) != FALSE;
        SetLastError(error);
        return ok;
    }
};

struct ProcessFrame {
    std::uintptr_t native_return_slot = 0;
    ProcessSite site = ProcessSite::none;
};
struct Frame : p99_group_fiber::Binding {
    std::uint64_t nonce = 0;
    std::uintptr_t job = 0;
    std::uintptr_t native_return_slot = 0;
    ProcessFrame* process = nullptr;
    bool observed = false;
    bool invalid = false;
    bool emitting = false;
    bool pending_reentry = false;
    bool exception_reported = false;
};

p99_group_fiber::Slot<Frame, EnemyWindowsApi> g_frames;
std::atomic_flag g_configure_attempt = ATOMIC_FLAG_INIT;
std::atomic<bool> g_ready{false};
std::atomic<std::uint64_t> g_nonce{0};
// Optimization hint only, NOT admission, collector publication, retention or
// unload proof. Registration races stop(); all configuration remains permanent.
// Forced DeleteFiber may omit finally and leave this nonzero forever. Never
// repair it from another thread or use zero as permission to free anything.
std::atomic<std::uint64_t> g_entered_wrappers{0};
NativeCalls g_native{};
Sink g_sink{};

bool register_wrapper() noexcept {
    auto count = g_entered_wrappers.load(std::memory_order_acquire);
    // Bounded contention failure disables observation rather than spinning or
    // wrapping to a false zero. Physical scopes cannot approach uint64 max.
    for (unsigned attempt = 0; attempt != 16; ++attempt) {
        if (count == std::numeric_limits<std::uint64_t>::max()) return false;
        if (g_entered_wrappers.compare_exchange_weak(count, count + 1,
            std::memory_order_acq_rel, std::memory_order_acquire)) return true;
    }
    return false;
}

Event event_for(const Frame& frame, Kind kind, ProcessSite site,
    std::uintptr_t return_pc, std::uintptr_t return_slot,
    const std::array<std::uintptr_t, 7>& arguments,
    std::uintptr_t result = 0, bool normal_return = false) noexcept {
    Event event{};
    event.kind = kind;
    event.site = site;
    event.invocation_nonce = frame.nonce;
    event.job = frame.job;
    event.original_jointblend_return_slot = frame.native_return_slot;
    event.original_process_return_slot = frame.process ? frame.process->native_return_slot : 0;
    event.wrapper_entry_return_pc = return_pc;
    event.wrapper_entry_return_slot = return_slot;
    event.arguments = arguments;
    event.result = result;
    event.normal_return = normal_return;
    return event;
}

void emit(Frame& frame, const Event& event) noexcept {
    if (!frame.observed || frame.emitting) return;
    const DWORD error = GetLastError();
    frame.emitting = true;
    g_sink.emit(g_sink.context, event);
    frame.emitting = false;
    // Even a sink-induced nested wrapper cannot make its events join this
    // frame. Defer this one rejection rather than recursively invoking sink.
    if (frame.pending_reentry) {
        frame.pending_reentry = false;
        Event rejection = event;
        rejection.kind = Kind::Reentry;
        rejection.result = 0;
        rejection.normal_return = false;
        frame.emitting = true;
        g_sink.emit(g_sink.context, rejection);
        frame.emitting = false;
        frame.pending_reentry = false; // Already invalid; no repeated emission.
    }
    SetLastError(error);
}

void invalidate(Frame& frame, std::uintptr_t pc, std::uintptr_t slot,
    const std::array<std::uintptr_t, 7>& args) noexcept {
    if (!frame.observed || frame.invalid) return;
    frame.invalid = true;
    if (frame.emitting) frame.pending_reentry = true;
    else emit(frame, event_for(frame, Kind::Reentry, ProcessSite::none, pc, slot, args));
}

void exception(Frame& frame, ProcessSite site, std::uintptr_t pc,
    std::uintptr_t slot, const std::array<std::uintptr_t, 7>& args) noexcept {
    frame.invalid = true;
    if (!frame.observed || frame.exception_reported) return;
    frame.exception_reported = true;
    emit(frame, event_for(frame, Kind::Exception, site, pc, slot, args));
}

Frame* observable() noexcept {
    // A same-fiber ancestor registers before any FLS access and unregisters only
    // after cleanup. A foreign active fiber merely causes conservative lookup.
    if (g_entered_wrappers.load(std::memory_order_acquire) == 0) return nullptr;
    Frame* frame = g_frames.current();
    return frame && frame->observed && !frame->invalid && !frame->emitting ? frame : nullptr;
}

ProcessSite current_site(const Frame& frame) noexcept {
    return frame.process ? frame.process->site : ProcessSite::none;
}

bool requested() noexcept {
    const DWORD error = GetLastError();
    const bool result = g_sink.requested(g_sink.context);
    SetLastError(error);
    return result;
}

void jointblend_observed(void* job, std::uintptr_t pc, std::uintptr_t slot, DWORD entry_error, bool wanted) {
    Frame* outer = g_frames.current();
    if (!outer && (!wanted || g_frames.failed())) {
        SetLastError(entry_error);
        g_native.jointblend(job);
        return;
    }
    const std::array<std::uintptr_t, 7> args{reinterpret_cast<std::uintptr_t>(job)};
    if (outer) invalidate(*outer, pc, slot, args);
    Frame frame{};
    Frame* previous = nullptr;
    frame.job = reinterpret_cast<std::uintptr_t>(job);
    frame.observed = outer == nullptr;
    if (frame.observed) {
        const auto previous_nonce = g_nonce.fetch_add(1, std::memory_order_relaxed);
        if (previous_nonce == std::numeric_limits<std::uint64_t>::max()) {
            g_frames.poison(); // No nonce reuse even after uint64 overflow.
            frame.observed = false;
        } else frame.nonce = previous_nonce + 1;
    }
    if (!g_frames.enter(frame, previous)) {
        SetLastError(entry_error);
        g_native.jointblend(job);
        return;
    }
    bool normal = false;
    __try {
        emit(frame, event_for(frame, Kind::Entry, ProcessSite::none, pc, slot, args));
        SetLastError(entry_error);
        p99_group_native_frame::p99_group_invoke_native(job, g_native.jointblend,
            &frame.native_return_slot);
        normal = true;
    } __finally {
        const DWORD native_error = GetLastError();
        if (!normal) exception(frame, ProcessSite::none, pc, slot, args);
        // Assembly normal return clears this; abnormal unwind needs us too.
        frame.native_return_slot = 0;
        frame.process = nullptr;
        emit(frame, event_for(frame, Kind::Exit, ProcessSite::none, pc, slot,
            args, 0, normal));
        g_frames.leave(frame, previous);
        SetLastError(native_error);
    }
}

__declspec(noinline) void __fastcall jointblend(void* job) {
    const auto pc = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto slot = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    const DWORD entry_error = GetLastError();
    const bool wanted = requested();
    // Do not touch FLS on the dormant root path. An existing scope still needs
    // nested-invocation rejection even after the request gate closes.
    if (!wanted && g_entered_wrappers.load(std::memory_order_acquire) == 0) {
        SetLastError(entry_error);
        g_native.jointblend(job);
        return;
    }
    if (!register_wrapper()) {
        // Do not let this unregistered subtree silently join an existing scope.
        if (auto* outer = g_frames.current())
            invalidate(*outer, pc, slot, {reinterpret_cast<std::uintptr_t>(job)});
        g_frames.poison();
        SetLastError(entry_error);
        g_native.jointblend(job);
        return;
    }
    __try {
        jointblend_observed(job, pc, slot, entry_error, wanted);
    } __finally {
        const DWORD error = GetLastError();
        g_entered_wrappers.fetch_sub(1, std::memory_order_acq_rel);
        SetLastError(error);
    }
}

__declspec(noinline) void* __fastcall reset(void* cache, const void* root) {
    const auto pc = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto slot = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    const DWORD entry_error = GetLastError();
    Frame* frame = observable();
    if (!frame) { SetLastError(entry_error); return g_native.reset(cache, root); }
    const auto site = current_site(*frame);
    const std::array<std::uintptr_t, 7> args{reinterpret_cast<std::uintptr_t>(cache),
        reinterpret_cast<std::uintptr_t>(root)};
    void* result = nullptr;
    bool normal = false;
    __try {
        emit(*frame, event_for(*frame, Kind::ResetEnter, site, pc, slot, args));
        SetLastError(entry_error);
        result = g_native.reset(cache, root);
        normal = true;
    } __finally {
        const DWORD native_error = GetLastError();
        if (!normal) exception(*frame, site, pc, slot, args);
        else if (!frame->invalid) emit(*frame, event_for(*frame, Kind::ResetReturn,
            site, pc, slot, args, reinterpret_cast<std::uintptr_t>(result), true));
        SetLastError(native_error);
    }
    return result;
}

std::uint32_t process_impl(ProcessSite site, std::uintptr_t pc, std::uintptr_t slot,
    const std::uint32_t* commands, std::uint32_t words, const void* context,
    std::uint32_t group, const void* a5, const void* a6, const void* a7) {
    const DWORD entry_error = GetLastError();
    Frame* frame = observable();
    if (!frame) {
        SetLastError(entry_error);
        return g_native.process(commands, words, context, group, a5, a6, a7);
    }
    const std::array<std::uintptr_t, 7> args{reinterpret_cast<std::uintptr_t>(commands),
        words, reinterpret_cast<std::uintptr_t>(context), group,
        reinterpret_cast<std::uintptr_t>(a5), reinterpret_cast<std::uintptr_t>(a6),
        reinterpret_cast<std::uintptr_t>(a7)};
    ProcessFrame process{};
    process.site = site;
    ProcessFrame* previous = frame->process;
    if (previous) invalidate(*frame, pc, slot, args);
    frame->process = &process;
    std::uint32_t result = 0;
    bool normal = false;
    __try {
        if (!frame->invalid) emit(*frame, event_for(*frame, Kind::ProcessEnter, site, pc, slot, args));
        SetLastError(entry_error);
        result = p99_process_native_frame::p99_process_invoke_native(commands,
            words, context, group, a5, a6, a7, g_native.process, &process.native_return_slot);
        normal = true;
    } __finally {
        const DWORD native_error = GetLastError();
        if (!normal) exception(*frame, site, pc, slot, args);
        process.native_return_slot = 0;
        if (normal && !frame->invalid) emit(*frame, event_for(*frame,
            Kind::ProcessReturn, site, pc, slot, args, result, true));
        frame->process = previous;
        SetLastError(native_error);
    }
    return result;
}

// Capture these addresses in the actual patched-call entry, NOT process_impl's
// helper frame. Seven native arguments remain seven arguments at the hook ABI.
__declspec(noinline) std::uint32_t __fastcall process_first(const std::uint32_t* commands,
    std::uint32_t words, const void* context, std::uint32_t group,
    const void* a5, const void* a6, const void* a7) {
    return process_impl(ProcessSite::first, reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
        reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()),
        commands, words, context, group, a5, a6, a7);
}
__declspec(noinline) std::uint32_t __fastcall process_later(const std::uint32_t* commands,
    std::uint32_t words, const void* context, std::uint32_t group,
    const void* a5, const void* a6, const void* a7) {
    return process_impl(ProcessSite::later, reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
        reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()),
        commands, words, context, group, a5, a6, a7);
}

__declspec(noinline) std::uintptr_t __fastcall postpose(void* work, void* segment,
    const void* context, std::uint32_t id, const void* payload, const void* joints) {
    const auto pc = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto slot = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    const DWORD entry_error = GetLastError();
    Frame* frame = observable();
    if (!frame) { SetLastError(entry_error); return g_native.postpose(work, segment, context, id, payload, joints); }
    const auto site = current_site(*frame);
    const std::array<std::uintptr_t, 7> args{reinterpret_cast<std::uintptr_t>(work),
        reinterpret_cast<std::uintptr_t>(segment), reinterpret_cast<std::uintptr_t>(context),
        id, reinterpret_cast<std::uintptr_t>(payload), reinterpret_cast<std::uintptr_t>(joints)};
    std::uintptr_t result = 0;
    bool normal = false;
    __try {
        emit(*frame, event_for(*frame, Kind::PostposeEnter, site, pc, slot, args));
        SetLastError(entry_error);
        result = g_native.postpose(work, segment, context, id, payload, joints);
        normal = true;
    } __finally {
        const DWORD native_error = GetLastError();
        if (!normal) exception(*frame, site, pc, slot, args);
        else if (!frame->invalid) emit(*frame, event_for(*frame, Kind::PostposeReturn,
            site, pc, slot, args, result, true));
        SetLastError(native_error);
    }
    return result;
}

__declspec(noinline) void __fastcall primary(void* composite) {
    const auto pc = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto slot = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    const DWORD entry_error = GetLastError();
    Frame* frame = observable();
    if (!frame) { SetLastError(entry_error); g_native.primary(composite); return; }
    const auto site = current_site(*frame);
    const std::array<std::uintptr_t, 7> args{reinterpret_cast<std::uintptr_t>(composite)};
    bool normal = false;
    __try {
        emit(*frame, event_for(*frame, Kind::PrimaryEnter, site, pc, slot, args));
        SetLastError(entry_error);
        g_native.primary(composite);
        normal = true;
    } __finally {
        const DWORD native_error = GetLastError();
        if (!normal) exception(*frame, site, pc, slot, args);
        else if (!frame->invalid) emit(*frame, event_for(*frame, Kind::PrimaryReturn,
            site, pc, slot, args, 0, true));
        SetLastError(native_error);
    }
}

__declspec(noinline) std::int32_t __fastcall store(void* body, const void* pose) {
    const auto pc = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto slot = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    const DWORD entry_error = GetLastError();
    Frame* frame = observable();
    if (!frame) { SetLastError(entry_error); return g_native.store(body, pose); }
    const auto site = current_site(*frame);
    const std::array<std::uintptr_t, 7> args{reinterpret_cast<std::uintptr_t>(body),
        reinterpret_cast<std::uintptr_t>(pose)};
    std::int32_t result = 0;
    bool normal = false;
    __try {
        emit(*frame, event_for(*frame, Kind::StoreEnter, site, pc, slot, args));
        SetLastError(entry_error);
        result = g_native.store(body, pose);
        normal = true;
    } __finally {
        const DWORD native_error = GetLastError();
        if (!normal) exception(*frame, site, pc, slot, args);
        else if (!frame->invalid) emit(*frame, event_for(*frame, Kind::StoreReturn,
            site, pc, slot, args, static_cast<std::uint32_t>(result), true));
        SetLastError(native_error);
    }
    return result;
}

} // namespace

bool configure(const NativeCalls& native, Sink sink) noexcept {
    const DWORD error = GetLastError();
    bool success = false;
    if (!g_configure_attempt.test_and_set(std::memory_order_acq_rel) &&
        native.jointblend && native.reset && native.process && native.postpose &&
        native.primary && native.store && sink.requested && sink.emit && g_frames.initialize()) {
        g_native = native;
        g_sink = sink;
        g_ready.store(true, std::memory_order_release);
        success = true;
    }
    SetLastError(error);
    return success;
}

Targets targets() noexcept {
    if (!g_ready.load(std::memory_order_acquire)) return {};
    return {{reinterpret_cast<std::uintptr_t>(&jointblend),
        reinterpret_cast<std::uintptr_t>(&jointblend), reinterpret_cast<std::uintptr_t>(&reset),
        reinterpret_cast<std::uintptr_t>(&process_first), reinterpret_cast<std::uintptr_t>(&process_later),
        reinterpret_cast<std::uintptr_t>(&primary), reinterpret_cast<std::uintptr_t>(&store)},
        reinterpret_cast<std::uintptr_t>(&postpose)};
}

#if defined(TLL_ENEMY_HOOK_AUDIT)
AuditSnapshot audit_snapshot() noexcept {
    return {g_reads.load(), g_writes.load(), g_bootstraps.load(), g_entered_wrappers.load()};
}
void audit_exhaust_counter() noexcept {
    g_entered_wrappers.store(std::numeric_limits<std::uint64_t>::max());
}
#endif

} // namespace coop::enemy_jointblend_hooks
