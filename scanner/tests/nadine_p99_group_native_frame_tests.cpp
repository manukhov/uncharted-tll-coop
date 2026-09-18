#include "nadine_p99_group_fiber.h"
#include "nadine_p99_group_native_frame.h"
#include <array>
#include <iostream>
#include <source_location>

namespace frame_policy = coop::p99_group_native_frame;
namespace fiber = coop::p99_group_fiber;
unsigned checks = 0, failures = 0;
void check(bool value, std::source_location where = std::source_location::current()) {
    ++checks;
    if (!value) { ++failures; std::cerr << "failed line " << where.line() << '\n'; }
}

void value_tests() {
    constexpr std::uintptr_t native_sp = 0x100000;
    constexpr auto return_slot = native_sp + frame_policy::kNativeStackBytes;
    check(frame_policy::owns(true, native_sp + 0x110, return_slot));
    check(frame_policy::owns(false, native_sp + 0x160, return_slot));
    check(!frame_policy::owns(false, native_sp + 0x110, return_slot));
    check(!frame_policy::owns(true, native_sp + 0x160, return_slot));
    check(!frame_policy::owns(true, 0, return_slot));
    check(!frame_policy::owns(true, native_sp + 0x110, 0));
    check(!frame_policy::owns(true, UINTPTR_MAX & ~std::uintptr_t{15}, 8));
    for (const bool first_site : {false, true}) {
        const auto context = native_sp + (first_site ? 0x110 : 0x160);
        for (const auto group : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, UINT32_MAX}) {
            const auto native = frame_policy::admit(first_site, context, return_slot, group);
            check(native.same_invocation);
            check(native.may_consider_overlay == (group <= 1));
            const auto old = frame_policy::admit(first_site, context - 16, return_slot, group);
            check(!old.same_invocation && !old.may_consider_overlay);
        }
    }
    for (std::uintptr_t offset = 1; offset <= 1024; ++offset) {
        check(!frame_policy::owns(true, native_sp + 0x110 + offset, return_slot));
        check(!frame_policy::owns(false, native_sp + 0x160 + offset, return_slot));
    }
    auto prologue = frame_policy::kNativePrologue;
    auto first = frame_policy::kContextLeas[0], second = frame_policy::kContextLeas[1];
    check(frame_policy::layout_matches(prologue, first, second));
    for (std::size_t i = 0; i < prologue.size(); ++i) {
        prologue[i] ^= 1;
        check(!frame_policy::layout_matches(prologue, first, second));
        prologue[i] ^= 1;
    }
    for (std::size_t i = 0; i < first.size(); ++i) {
        first[i] ^= 1; check(!frame_policy::layout_matches(prologue, first, second)); first[i] ^= 1;
        second[i] ^= 1; check(!frame_policy::layout_matches(prologue, first, second)); second[i] ^= 1;
    }
    check(!frame_policy::layout_matches(prologue, second, first));
    check(!frame_policy::layout_matches(std::span{prologue}.first(42), first, second));
    check(!frame_policy::layout_matches(prologue, std::span{first}.first(7), second));
    check(!frame_policy::layout_matches(prologue, first, std::span{second}.first(7)));
}

struct Frame : fiber::Binding {
    std::uintptr_t native_return_slot = 0;
    void* process = nullptr;
    bool refused = false;
    bool candidate = true;
};
fiber::Slot<Frame> scope;
enum class Action { normal, later_groups, old_recursion, wrapped_recursion, exception, nested_exception, switch_fiber };
struct Job {
    Action action = Action::normal;
    unsigned depth = 0;
    unsigned native_calls = 0, admitted = 0, rejected = 0, finalizers = 0, normal_clears = 0;
    unsigned caught = 0;
    unsigned stock_process_calls = 0;
    void* root_fiber = nullptr;
    void* other_fiber = nullptr;
};
extern "C" void p99_native_frame_fixture(void*);

// Mirrors production ownership: initialize a fresh FLS frame for each wrapped
// entry; only top-level entries may publish; clear the bridge identity before
// restoring the previous scope, also during SEH unwinding.
__declspec(noinline) void invoke_wrapped(Job& job) {
    Frame frame{};
    Frame* previous = nullptr;
    check(scope.enter(frame, previous));
    frame.candidate = previous == nullptr;
    if (previous) previous->refused = true;
    __try {
        frame_policy::p99_group_invoke_native(&job, &p99_native_frame_fixture,
            &frame.native_return_slot);
        check(frame.native_return_slot == 0);
        ++job.normal_clears;
    } __finally {
        frame.native_return_slot = 0;
        frame.process = nullptr;
        ++job.finalizers;
        check(scope.leave(frame, previous));
    }
}

// Deliberately calls the SAME function with the SAME Job address, bypassing the
// ingress wrapper. This is the case that Fg/job-address equality cannot reject.
__declspec(noinline) void invoke_old(Job& job) { p99_native_frame_fixture(&job); }

void process_fixture(Job& job, Frame& frame, bool first, void* context, std::uint32_t group) {
    void* previous = frame.process;
    __try {
        frame.process = nullptr;
        const auto admission = frame_policy::admit(first,
            reinterpret_cast<std::uintptr_t>(context), frame.native_return_slot, group);
        if (!admission.same_invocation) { frame.refused = true; ++job.rejected; }
        if (admission.may_consider_overlay && frame.candidate && !frame.refused) {
            frame.process = context;
            ++job.admitted;
        }
        // A stock cmd16 in a refused nested Process must see NO parent scope.
        check(frame.process == ((admission.may_consider_overlay && frame.candidate && !frame.refused) ? context : nullptr));
        ++job.stock_process_calls;
    } __finally { frame.process = previous; }
    check(frame.process == previous);
}

constexpr DWORD kTestException = 0xE0420095;
__declspec(noinline) void invoke_nested_exception(Job& job) {
    __try { invoke_wrapped(job); }
    __except (GetExceptionCode() == kTestException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        ++job.caught;
    }
}

__declspec(noinline) void verify_active_unwind(std::uintptr_t native_slot,
    std::uintptr_t bound_slot, bool direct_native) {
    CONTEXT context{};
    RtlCaptureContext(&context);
    const auto bridge = reinterpret_cast<DWORD64>(&frame_policy::p99_group_invoke_native);
    bool found_bridge = false, found_fixture = false;
    for (unsigned depth = 0; depth < 16 && context.Rip; ++depth) {
        DWORD64 image_base = 0, establisher = 0;
        void* handler_data = nullptr;
        auto* runtime = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
        check(runtime != nullptr);
        if (!runtime) break;
        const auto begin = image_base + runtime->BeginAddress;
        if (begin == reinterpret_cast<DWORD64>(&p99_native_frame_fixture)) found_fixture = true;
        if (begin == bridge) {
            found_bridge = true;
            const auto bridge_sp = context.Rsp;
            check(bridge_sp == bound_slot + sizeof(void*));
            check((bridge_sp == native_slot + sizeof(void*)) == direct_native);
            const auto return_pc = *reinterpret_cast<const DWORD64*>(bridge_sp + 0x38);
            // Metadata-only unwind does not execute the assembly clear. Verify
            // both restored stack and return PC against this live caller frame.
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, runtime,
                &context, &handler_data, &establisher, nullptr);
            check(context.Rsp == bridge_sp + 0x40 && context.Rip == return_pc);
            break;
        }
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, runtime,
            &context, &handler_data, &establisher, nullptr);
    }
    check(found_fixture && found_bridge);
}

extern "C" void p99_native_frame_fixture_body(void* opaque, void* first_context,
    void* second_context, void* actual_return_slot) {
    auto& job = *static_cast<Job*>(opaque);
    ++job.native_calls;
    auto* frame = scope.current();
    if (frame == nullptr) {
        check(job.action == Action::switch_fiber);
        return; // Other fiber's old callback must never borrow suspended state.
    }
    const auto token = frame->native_return_slot;
    const bool old = job.action == Action::old_recursion && job.depth == 1;
    check((token == reinterpret_cast<std::uintptr_t>(actual_return_slot)) == !old);
    check(frame_policy::owns(true, reinterpret_cast<std::uintptr_t>(first_context), token) == !old);
    check(frame_policy::owns(false, reinterpret_cast<std::uintptr_t>(second_context), token) == !old);
    verify_active_unwind(reinterpret_cast<std::uintptr_t>(actual_return_slot), token, !old);
    check(frame->native_return_slot == token);

    if (job.action == Action::exception ||
        (job.action == Action::nested_exception && job.depth == 1)) {
        RaiseException(kTestException, 0, 0, nullptr);
        check(false);
        return;
    }
    process_fixture(job, *frame, true, first_context, 0);
    if (job.depth == 0 && job.action != Action::normal && job.action != Action::later_groups) {
        // Keep a nonnull parent Process marker during old/new recursive work.
        frame->process = first_context;
        ++job.depth;
        switch (job.action) {
        case Action::old_recursion: invoke_old(job); break;
        case Action::wrapped_recursion: invoke_wrapped(job); break;
        case Action::nested_exception: invoke_nested_exception(job); break;
        case Action::switch_fiber: SwitchToFiber(job.other_fiber); break;
        default: break;
        }
        --job.depth;
        check(scope.current() == frame);
        check(frame->native_return_slot == token);
        check(frame->process == first_context);
        frame->process = nullptr;
    }
    process_fixture(job, *frame, false, second_context, 1);
    if (job.action == Action::later_groups) {
        for (std::uint32_t group = 2; group < 8; ++group) {
            process_fixture(job, *frame, false, second_context, group);
            check(!frame->refused);
        }
    }
}

void check_unwind() {
    DWORD64 image_base = 0;
    const auto address = reinterpret_cast<DWORD64>(&frame_policy::p99_group_invoke_native);
    auto* runtime = RtlLookupFunctionEntry(address, &image_base, nullptr);
    check(runtime != nullptr);
    if (runtime) {
        check(image_base + runtime->BeginAddress == address);
        const auto* info = reinterpret_cast<const unsigned char*>(image_base + runtime->UnwindData);
        check((info[0] & 7) == 1 && (info[0] >> 3) == 0);
        check(info[2] == 1); // one UWOP_ALLOC_SMALL, seven*8 bytes
        check((info[5] & 15) == 2 && (info[5] >> 4) == 6); // (6+1)*8 = 0x38
    }
    check(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&p99_native_frame_fixture),
        &image_base, nullptr) != nullptr);
}

void simple_cases() {
    Job normal{};
    invoke_wrapped(normal);
    check(normal.native_calls == 1 && normal.admitted == 2 && normal.rejected == 0);
    check(normal.normal_clears == 1 && normal.finalizers == 1 && !scope.current());

    Job later{Action::later_groups};
    invoke_wrapped(later);
    check(later.native_calls == 1 && later.stock_process_calls == 8);
    check(later.admitted == 2 && later.rejected == 0);
    check(later.normal_clears == 1 && later.finalizers == 1 && !scope.current());

    Job old{Action::old_recursion};
    invoke_wrapped(old);
    check(old.native_calls == 2 && old.admitted == 1 && old.rejected == 2);
    check(old.normal_clears == 1 && old.finalizers == 1 && !scope.current());

    Job nested{Action::wrapped_recursion};
    invoke_wrapped(nested);
    check(nested.native_calls == 2 && nested.admitted == 1 && nested.rejected == 0);
    check(nested.normal_clears == 2 && nested.finalizers == 2 && !scope.current());

    Job exception{Action::exception};
    invoke_nested_exception(exception);
    check(exception.native_calls == 1 && exception.admitted == 0 && exception.caught == 1);
    check(exception.normal_clears == 0 && exception.finalizers == 1 && !scope.current());

    Job nested_exception{Action::nested_exception};
    invoke_wrapped(nested_exception);
    check(nested_exception.native_calls == 2 && nested_exception.admitted == 1 && nested_exception.caught == 1);
    check(nested_exception.normal_clears == 1 && nested_exception.finalizers == 2 && !scope.current());
}

void WINAPI other_fiber(void* opaque) {
    auto& job = *static_cast<Job*>(opaque);
    check(scope.current() == nullptr);
    invoke_old(job);
    check(scope.current() == nullptr);
    SwitchToFiber(job.root_fiber);
}

int main() {
    value_tests();
    check(scope.initialize());
    check_unwind();
    simple_cases(); // ordinary non-fiber thread, including actual SEH unwinding
    void* root = ConvertThreadToFiber(nullptr);
    check(root != nullptr);
    if (root) {
        simple_cases();
        Job switching{Action::switch_fiber};
        switching.root_fiber = root;
        switching.other_fiber = CreateFiber(0, other_fiber, &switching);
        check(switching.other_fiber != nullptr);
        if (switching.other_fiber) {
            invoke_wrapped(switching);
            check(switching.native_calls == 2 && switching.admitted == 2 && switching.rejected == 0);
            check(switching.finalizers == 1 && !scope.current());
            DeleteFiber(switching.other_fiber);
        }
        check(ConvertFiberToThread() != FALSE);
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
