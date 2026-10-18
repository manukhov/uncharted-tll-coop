#include "enemy_jointblend_hooks.h"
#include "nadine_p99_group_native_frame.h"
#include "nadine_p99_process_native_frame.h"

#include <Windows.h>
#include <intrin.h>
#include <array>
#include <cstdio>

namespace hooks = coop::enemy_jointblend_hooks;

namespace {
unsigned checks = 0, failures = 0;
void check(bool condition, const char* name) noexcept {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", name); }
}
constexpr DWORD kException = 0xE0426117;
constexpr DWORD kEntryError = 0x12345678;
constexpr DWORD kNativeError = 0x31415926;
constexpr std::uintptr_t kPostposeResult = 0xFEDCBA9876543210;
constexpr std::uint32_t kProcessResult = 0xFEDC1234;
constexpr std::int32_t kStoreResult = -1234567;
constexpr std::uintptr_t kRoot = 0x1020304050607080;
constexpr std::uintptr_t kA5 = 0x1122334455667788;
constexpr std::uintptr_t kA6 = 0x8877665544332211;
constexpr std::uintptr_t kA7 = 0x1122334488776655;
enum class ThrowAt { none, jointblend, reset, process, postpose, primary, store };

struct State {
    std::array<hooks::Event, 512> events{};
    std::size_t event_count = 0;
    std::array<unsigned, 6> calls{};
    bool gate = true;
    bool sink_reenter = false;
    std::uint64_t last_entry_nonce = 0;
} state;
struct Job {
    ThrowAt throw_at = ThrowAt::none;
    bool reenter_jointblend = false;
    bool reenter_process = false;
    unsigned jointblend_depth = 0;
    unsigned process_depth = 0;
    std::uintptr_t native_jointblend_slot = 0;
    std::uintptr_t native_process_slot = 0;
    void* yield_to = nullptr;
    void* return_to = nullptr;
    DWORD entered_thread = 0;
    bool expect_migration = false;
};
hooks::Targets targets{};
hooks::JointBlend jointblend_hook = nullptr;
hooks::Reset reset_hook = nullptr;
hooks::Process first_hook = nullptr, later_hook = nullptr;
hooks::Postpose postpose_hook = nullptr;
hooks::Primary primary_hook = nullptr;
hooks::Store store_hook = nullptr;

void maybe_throw(const Job& job, ThrowAt at) {
    if (job.throw_at == at) RaiseException(kException, 0, 0, nullptr);
}
bool requested(void* context) noexcept {
    check(context == &state, "requested context");
    SetLastError(0xBAD10001); // Observer changes must never leak to native.
    return state.gate;
}
void emit(void* context, const hooks::Event& event) noexcept {
    check(context == &state, "emit context");
    check(event.invocation_nonce != 0, "nonzero invocation nonce");
    check(event.wrapper_entry_return_pc != 0 && event.wrapper_entry_return_slot != 0,
        "real wrapper entry PC and slot supplied");
    check((event.wrapper_entry_return_slot & 15u) == 8, "wrapper entry stack alignment");
    check(*reinterpret_cast<const std::uintptr_t*>(event.wrapper_entry_return_slot) ==
        event.wrapper_entry_return_pc, "wrapper slot really contains supplied PC");
    auto* job = reinterpret_cast<Job*>(event.job);
    if (event.kind == hooks::Kind::Entry) {
        check(event.invocation_nonce > state.last_entry_nonce, "monotonic scope nonce");
        state.last_entry_nonce = event.invocation_nonce;
        check(event.original_jointblend_return_slot == 0 &&
            event.original_process_return_slot == 0, "entry precedes both native bridges");
    } else if (event.kind == hooks::Kind::Exit) {
        check(event.original_jointblend_return_slot == 0 &&
            event.original_process_return_slot == 0, "exit expires both native slots");
    } else if (event.kind != hooks::Kind::Exception && event.kind != hooks::Kind::Reentry) {
        check(event.original_jointblend_return_slot == job->native_jointblend_slot,
            "bridge publishes actual original JointBlend entry slot");
        if (event.kind == hooks::Kind::PostposeEnter || event.kind == hooks::Kind::PostposeReturn)
            check(event.original_process_return_slot == job->native_process_slot,
                "bridge publishes actual original Process entry slot");
        if (event.kind == hooks::Kind::ProcessEnter || event.kind == hooks::Kind::ProcessReturn)
            check(event.original_process_return_slot == 0, "Process entry/return outside native bridge lifetime");
    }
    if (state.event_count < state.events.size()) state.events[state.event_count++] = event;
    else check(false, "event fixture capacity");
    if (state.sink_reenter && event.kind == hooks::Kind::ResetEnter) {
        state.sink_reenter = false;
        jointblend_hook(job);
    }
    SetLastError(0xBAD10002);
}

__declspec(noinline) void* __fastcall native_reset(void* cache, const void* root) {
    ++state.calls[1];
    check(GetLastError() == kEntryError, "Reset original LastError");
    check(reinterpret_cast<std::uintptr_t>(root) == kRoot, "Reset second argument");
    auto& job = *static_cast<Job*>(cache);
    maybe_throw(job, ThrowAt::reset);
    SetLastError(kNativeError);
    return reinterpret_cast<void*>(kRoot + 0x80);
}
__declspec(noinline) std::uintptr_t __fastcall native_postpose(void* work, void* segment,
    const void* context, std::uint32_t id, const void* payload, const void* joints) {
    ++state.calls[3];
    check(GetLastError() == kEntryError, "Postpose original LastError");
    auto& job = *static_cast<Job*>(work);
    check(context == work && id == 0x248B1AAE &&
        reinterpret_cast<std::uintptr_t>(payload) == kA5 &&
        reinterpret_cast<std::uintptr_t>(joints) == kA6,
        "Postpose all six arguments including two stack arguments");
    check(reinterpret_cast<std::uintptr_t>(segment) + 0x1628 == job.native_process_slot,
        "Postpose Segment is second argument");
    maybe_throw(job, ThrowAt::postpose);
    SetLastError(kNativeError);
    return kPostposeResult;
}
__declspec(noinline) std::uint32_t __fastcall native_process(const std::uint32_t* commands,
    std::uint32_t words, const void* context, std::uint32_t group,
    const void* a5, const void* a6, const void* a7) {
    ++state.calls[2];
    check(GetLastError() == kEntryError, "Process original LastError");
    check(words == 0xF1234567 && (group == 0 || group == 3) &&
        reinterpret_cast<std::uintptr_t>(a5) == kA5 &&
        reinterpret_cast<std::uintptr_t>(a6) == kA6 &&
        reinterpret_cast<std::uintptr_t>(a7) == kA7,
        "Process seven arguments including full-width stack values");
    check(static_cast<const void*>(commands) == context, "Process first and third arguments");
    auto& job = *const_cast<Job*>(static_cast<const Job*>(context));
    const auto previous_slot = job.native_process_slot;
    job.native_process_slot = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    if (job.reenter_process && job.process_depth == 0) {
        ++job.process_depth;
        SetLastError(kEntryError);
        check(first_hook(commands, words, context, group, a5, a6, a7) == kProcessResult,
            "nested Process native result");
        --job.process_depth;
    }
    maybe_throw(job, ThrowAt::process);
    SetLastError(kEntryError);
    const auto result = postpose_hook(&job,
        reinterpret_cast<void*>(job.native_process_slot - 0x1628), &job, 0x248B1AAE,
        reinterpret_cast<const void*>(kA5), reinterpret_cast<const void*>(kA6));
    check(result == kPostposeResult, "opaque full64 Postpose RAX forwarded");
    check(GetLastError() == kNativeError, "Postpose native LastError survives return event");
    job.native_process_slot = previous_slot;
    SetLastError(kNativeError);
    return kProcessResult;
}
__declspec(noinline) std::int32_t __fastcall native_store(void* body, const void* pose) {
    ++state.calls[5];
    check(GetLastError() == kEntryError, "Store original LastError");
    check(reinterpret_cast<std::uintptr_t>(pose) == kA7, "Store converted-pose argument");
    maybe_throw(*static_cast<Job*>(body), ThrowAt::store);
    SetLastError(kNativeError);
    return kStoreResult;
}
__declspec(noinline) void __fastcall native_primary(void* composite) {
    ++state.calls[4];
    check(GetLastError() == kEntryError, "Primary original LastError");
    maybe_throw(*static_cast<Job*>(composite), ThrowAt::primary);
    SetLastError(kEntryError);
    check(store_hook(composite, reinterpret_cast<const void*>(kA7)) == kStoreResult,
        "signed Store EAX forwarded unchanged");
    check(GetLastError() == kNativeError, "Store native LastError survives return event");
    SetLastError(kNativeError);
}
__declspec(noinline) void __fastcall native_jointblend(void* pointer) {
    ++state.calls[0];
    check(GetLastError() == kEntryError, "JointBlend original LastError");
    auto& job = *static_cast<Job*>(pointer);
    const auto previous_slot = job.native_jointblend_slot;
    job.native_jointblend_slot = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    job.entered_thread = GetCurrentThreadId();
    if (job.yield_to) {
        void* target = job.yield_to;
        job.yield_to = nullptr;
        SwitchToFiber(target);
        if (job.expect_migration) check(GetCurrentThreadId() != job.entered_thread,
            "native JointBlend resumed on other OS thread");
    }
    maybe_throw(job, ThrowAt::jointblend);
    if (job.reenter_jointblend && job.jointblend_depth == 0) {
        ++job.jointblend_depth;
        SetLastError(kEntryError);
        jointblend_hook(&job);
        --job.jointblend_depth;
    }
    SetLastError(kEntryError);
    check(reset_hook(&job, reinterpret_cast<const void*>(kRoot)) ==
        reinterpret_cast<void*>(kRoot + 0x80), "Reset full64 pointer result");
    check(GetLastError() == kNativeError, "Reset native LastError survives return event");
    for (unsigned site = 0; site != 2; ++site) {
        SetLastError(kEntryError);
        const auto process = site == 0 ? first_hook : later_hook;
        check(process(reinterpret_cast<const std::uint32_t*>(&job), 0xF1234567, &job,
            site == 0 ? 0 : 3, reinterpret_cast<const void*>(kA5),
            reinterpret_cast<const void*>(kA6), reinterpret_cast<const void*>(kA7)) ==
            kProcessResult, "Process uint32 high-bit result forwarded");
        check(GetLastError() == kNativeError, "Process native LastError survives return event");
    }
    SetLastError(kEntryError);
    primary_hook(&job);
    check(GetLastError() == kNativeError, "Primary native LastError survives return event");
    job.native_jointblend_slot = previous_slot;
    SetLastError(kNativeError);
}

bool run_catching(Job& job) {
    __try {
        SetLastError(kEntryError);
        jointblend_hook(&job);
    } __except (GetExceptionCode() == kException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}
void clear() noexcept {
    state.event_count = 0;
    state.calls = {};
    state.gate = true;
    state.sink_reenter = false;
}
unsigned count(hooks::Kind kind) noexcept {
    unsigned n = 0;
    for (std::size_t i = 0; i < state.event_count; ++i) if (state.events[i].kind == kind) ++n;
    return n;
}
void check_complete_trace(Job& job) noexcept {
    check(state.event_count == 16 && count(hooks::Kind::Entry) == 1 &&
        count(hooks::Kind::Exit) == 1 && count(hooks::Kind::Exception) == 0,
        "complete event trace exactly once");
    check(state.calls == std::array<unsigned, 6>{1, 1, 2, 2, 1, 1},
        "every original called exactly once per site");
    const auto nonce = state.events[0].invocation_nonce;
    unsigned first = 0, later = 0;
    for (std::size_t i = 0; i < state.event_count; ++i) {
        const auto& event = state.events[i];
        check(event.invocation_nonce == nonce && event.job == reinterpret_cast<std::uintptr_t>(&job),
            "all events belong to same exact outer invocation");
        if (event.kind == hooks::Kind::ProcessEnter) {
            if (event.site == hooks::ProcessSite::first) ++first;
            if (event.site == hooks::ProcessSite::later) ++later;
            check(event.arguments[1] == 0xF1234567 && event.arguments[4] == kA5 &&
                event.arguments[5] == kA6 && event.arguments[6] == kA7,
                "Process value-only event preserves seven arguments");
        }
        if (event.kind == hooks::Kind::StoreReturn)
            check(event.result == static_cast<std::uint32_t>(kStoreResult) && event.normal_return,
                "Store event preserves low32 without boolean interpretation");
        if (event.kind == hooks::Kind::PostposeReturn)
            check(event.result == kPostposeResult && event.normal_return, "Postpose event opaque64 result");
    }
    check(first == 1 && later == 1, "two Process sites distinguished");
    check(state.events[state.event_count - 1].normal_return, "normal outer Exit");
}
void normal_and_gate_tests() {
    clear();
    Job job{};
    check(!run_catching(job), "normal native invocation returns");
    check(GetLastError() == kNativeError, "outer LastError preserved across FLS cleanup");
    check_complete_trace(job);
    clear();
    state.gate = false;
    check(!run_catching(job), "gate false native invocation returns");
    check(state.event_count == 0 && state.calls == std::array<unsigned, 6>{1, 1, 2, 2, 1, 1},
        "gate false forwards entire stock path exactly once with no events");
    clear();
    SetLastError(kEntryError);
    check(reset_hook(&job, reinterpret_cast<const void*>(kRoot)) == reinterpret_cast<void*>(kRoot + 0x80),
        "unscoped Reset forwards");
    SetLastError(kEntryError);
    check(first_hook(reinterpret_cast<const std::uint32_t*>(&job), 0xF1234567, &job, 0,
        reinterpret_cast<const void*>(kA5), reinterpret_cast<const void*>(kA6),
        reinterpret_cast<const void*>(kA7)) == kProcessResult, "unscoped Process forwards");
    SetLastError(kEntryError);
    primary_hook(&job);
    check(state.event_count == 0 && state.calls == std::array<unsigned, 6>{0, 1, 1, 1, 1, 1},
        "unscoped calls and descendants never create an enemy frame");
}
void reentry_tests() {
    clear();
    Job job{};
    job.reenter_jointblend = true;
    check(!run_catching(job), "nested JointBlend returns");
    check(state.calls == std::array<unsigned, 6>{2, 2, 4, 4, 2, 2},
        "nested JointBlend originals still called exactly once");
    check(state.event_count == 3 && count(hooks::Kind::Entry) == 1 &&
        count(hooks::Kind::Reentry) == 1 && count(hooks::Kind::Exit) == 1,
        "nested JointBlend invalidates parent and suppresses child descendants");
    clear();
    job = {};
    job.reenter_process = true;
    check(!run_catching(job), "nested Process returns");
    check(state.calls[2] == 4 && state.calls[3] == 4 && count(hooks::Kind::Reentry) == 1 &&
        count(hooks::Kind::ProcessEnter) == 1 && count(hooks::Kind::PostposeEnter) == 0 &&
        count(hooks::Kind::Exit) == 1, "nested Process invalidates parent without child event joining");
    clear();
    job = {};
    state.sink_reenter = true;
    check(!run_catching(job), "sink-induced nested JointBlend returns");
    check(count(hooks::Kind::Reentry) == 1 && count(hooks::Kind::Entry) == 1 &&
        count(hooks::Kind::Exit) == 1 && state.calls[0] == 2,
        "sink-induced reentry rejected without recursive emit");
    clear();
    job = {};
    check(!run_catching(job), "fresh scope after reentry");
    check_complete_trace(job);
}
void exception_tests() {
    for (auto stage : {ThrowAt::jointblend, ThrowAt::reset, ThrowAt::process,
        ThrowAt::postpose, ThrowAt::primary, ThrowAt::store}) {
        clear();
        Job job{};
        job.throw_at = stage;
        check(run_catching(job), "real SEH propagates to caller unchanged");
        check(count(hooks::Kind::Exception) == 1 && count(hooks::Kind::Exit) == 1 &&
            !state.events[state.event_count - 1].normal_return,
            "unwind emits reject and exactly one abnormal outer Exit");
        const auto before = state.event_count;
        job.throw_at = ThrowAt::none;
        SetLastError(kEntryError);
        reset_hook(&job, reinterpret_cast<const void*>(kRoot));
        check(state.event_count == before, "SEH finally removes active frame");
        clear();
        job = {};
        check(!run_catching(job), "fresh scope after SEH");
        check_complete_trace(job);
    }
}

void WINAPI fiber_entry(void* opaque) {
    auto& job = *static_cast<Job*>(opaque);
    check(!run_catching(job), "fiber native invocation completes");
    SwitchToFiber(job.return_to);
}
struct Migration { void* fiber = nullptr; Job* job = nullptr; };
DWORD WINAPI migration_thread(void* opaque) {
    auto& migration = *static_cast<Migration*>(opaque);
    void* caller = ConvertThreadToFiber(nullptr);
    check(caller != nullptr, "migration thread becomes fiber");
    if (!caller) return 1;
    migration.job->return_to = caller;
    SwitchToFiber(migration.fiber);
    check(ConvertFiberToThread() != FALSE, "migration thread exits fiber mode");
    return 0;
}
void fiber_tests() {
    clear();
    void* main_fiber = ConvertThreadToFiber(nullptr);
    check(main_fiber != nullptr, "test thread becomes fiber");
    if (!main_fiber) return;
    Job a{}, b{};
    a.yield_to = b.yield_to = main_fiber;
    a.return_to = b.return_to = main_fiber;
    void* fiber_a = CreateFiber(0, fiber_entry, &a);
    void* fiber_b = CreateFiber(0, fiber_entry, &b);
    check(fiber_a && fiber_b, "create two independent fibers");
    if (fiber_a && fiber_b) {
        SwitchToFiber(fiber_a); // A suspended within its original JointBlend.
        SwitchToFiber(fiber_b); // B must create a fresh scope, not reject A.
        const auto before = state.event_count;
        Job unscoped{};
        SetLastError(kEntryError);
        reset_hook(&unscoped, reinterpret_cast<const void*>(kRoot));
        check(state.event_count == before, "main fiber cannot observe suspended A/B scopes");
        SwitchToFiber(fiber_a);
        SwitchToFiber(fiber_b);
        check(count(hooks::Kind::Entry) == 2 && count(hooks::Kind::Exit) == 2 &&
            count(hooks::Kind::Reentry) == 0, "interleaved fibers keep independent scope lifetimes");
        std::uint64_t nonce_a = 0, nonce_b = 0;
        for (std::size_t i = 0; i < state.event_count; ++i) {
            const auto& event = state.events[i];
            auto& expected = event.job == reinterpret_cast<std::uintptr_t>(&a) ? nonce_a : nonce_b;
            if (!expected) expected = event.invocation_nonce;
            check(expected == event.invocation_nonce, "fiber events never switch nonce");
        }
        check(nonce_a != 0 && nonce_b != 0 && nonce_a != nonce_b, "fibers have distinct invocation nonces");
    }
    if (fiber_a) DeleteFiber(fiber_a);
    if (fiber_b) DeleteFiber(fiber_b);

    // Delete a suspended fiber: its FLS borrowed stack entry must not leak to a
    // newly allocated fiber even if Windows recycles an address.
    clear();
    Job abandoned{};
    abandoned.yield_to = abandoned.return_to = main_fiber;
    void* doomed = CreateFiber(0, fiber_entry, &abandoned);
    check(doomed != nullptr, "create disposable suspended fiber");
    if (doomed) { SwitchToFiber(doomed); DeleteFiber(doomed); }
    clear();
    Job fresh{};
    fresh.return_to = main_fiber;
    void* replacement = CreateFiber(0, fiber_entry, &fresh);
    check(replacement != nullptr, "create replacement fiber");
    if (replacement) {
        SwitchToFiber(replacement);
        check_complete_trace(fresh);
        DeleteFiber(replacement);
    }

    clear();
    Job migrating{};
    migrating.yield_to = main_fiber;
    migrating.expect_migration = true;
    void* moving = CreateFiber(0, fiber_entry, &migrating);
    check(moving != nullptr, "create migration fiber");
    if (moving) {
        SwitchToFiber(moving);
        Migration migration{moving, &migrating};
        HANDLE thread = CreateThread(nullptr, 0, migration_thread, &migration, 0, nullptr);
        check(thread != nullptr, "create destination OS thread");
        if (thread) {
            const DWORD wait = WaitForSingleObject(thread, 10000);
            check(wait == WAIT_OBJECT_0, "migration thread completes");
            if (wait != WAIT_OBJECT_0) ExitProcess(2); // Never free an active stack.
            CloseHandle(thread);
            check_complete_trace(migrating);
        }
        DeleteFiber(moving);
    }
    check(ConvertFiberToThread() != FALSE, "test returns to ordinary thread");
    clear();
    Job after{};
    check(!run_catching(after), "ordinary thread works after fiber conversion");
    check_complete_trace(after);
}
} // namespace

int main() {
    check(hooks::targets().code[0] == 0 && hooks::targets().postpose == 0,
        "targets unavailable before one-shot configuration");
    SetLastError(kEntryError);
    const hooks::NativeCalls native{native_jointblend, native_reset, native_process,
        native_postpose, native_primary, native_store};
    check(hooks::configure(native, {&state, requested, emit}), "isolated FLS configuration");
    check(GetLastError() == kEntryError, "configuration preserves LastError");
    check(!hooks::configure(native, {&state, requested, emit}), "second configuration rejected");
    targets = hooks::targets();
    check(targets.code[0] != 0 && targets.code[0] == targets.code[1] &&
        targets.code[3] != targets.code[4] && targets.postpose != 0, "exact target ordering");
    jointblend_hook = reinterpret_cast<hooks::JointBlend>(targets.code[0]);
    reset_hook = reinterpret_cast<hooks::Reset>(targets.code[2]);
    first_hook = reinterpret_cast<hooks::Process>(targets.code[3]);
    later_hook = reinterpret_cast<hooks::Process>(targets.code[4]);
    primary_hook = reinterpret_cast<hooks::Primary>(targets.code[5]);
    store_hook = reinterpret_cast<hooks::Store>(targets.code[6]);
    postpose_hook = reinterpret_cast<hooks::Postpose>(targets.postpose);
    DWORD64 image = 0;
    check(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(
        &coop::p99_group_native_frame::p99_group_invoke_native), &image, nullptr) != nullptr,
        "original JointBlend bridge has Windows unwind metadata");
    check(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(
        &coop::p99_process_native_frame::p99_process_invoke_native), &image, nullptr) != nullptr,
        "original Process bridge has Windows unwind metadata");
    normal_and_gate_tests();
    reentry_tests();
    exception_tests();
    fiber_tests();
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
