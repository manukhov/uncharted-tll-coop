#include "nadine_p99_group_fiber.h"
#include <array>
#include <atomic>
#include <iostream>
#include <source_location>

namespace fiber = coop::p99_group_fiber;
struct Frame : fiber::Binding { int job = 0, process = 0; bool nested = false; };
std::atomic<unsigned> checks{0}, failures{0};
void check(bool value, std::source_location at = std::source_location::current()) {
    ++checks;
    if (!value) { ++failures; std::cerr << "failed line " << at.line() << '\n'; }
}

struct Fake {
    inline static std::array<void*, 2> values{};
    inline static unsigned context = 0, reads = 0, writes = 0, fail_write = 0;
    inline static bool fail_read = false, fail_allocate = false;
    static void reset() { values = {}; context = reads = writes = fail_write = 0; fail_read = fail_allocate = false; }
    static DWORD allocate() { return fail_allocate ? FLS_OUT_OF_INDEXES : 7; }
    static fiber::Identity identity() { return {reinterpret_cast<void*>(std::uintptr_t{0x1000} + context), 0}; }
    static bool read(DWORD, void*& value) { ++reads; value = values[context]; return !fail_read; }
    static bool write(DWORD, void* value) {
        if (++writes == fail_write) return false;
        values[context] = value; return true;
    }
};

void fake_tests() {
    using Slot = fiber::Slot<Frame, Fake>;
    Fake::reset();
    Slot slot; Frame a{}, b{}, child{}; Frame* previous = nullptr;
    check(!slot.current()); check(slot.initialize());
    check(slot.enter(a, previous) && previous == nullptr); a.job = 11;
    Fake::context = 1;
    check(!slot.current()); check(slot.enter(b, previous) && previous == nullptr); b.job = 22;
    for (unsigned i = 0; i < 1000; ++i) {
        Fake::context = i % 2;
        check(slot.current() == (Fake::context == 0 ? &a : &b));
        check(slot.current()->job == (Fake::context == 0 ? 11 : 22));
    }
    Fake::context = 0;
    check(slot.enter(child, previous) && previous == &a);
    check(slot.current() == &child && child.job == 0);
    check(slot.leave(child, previous)); check(slot.current() == &a);
    check(slot.leave(a, nullptr)); Fake::context = 1;
    check(slot.current() == &b); check(slot.leave(b, nullptr)); check(!slot.current());

    Fake::reset(); Fake::fail_allocate = true;
    Slot no_index; check(!no_index.initialize()); check(no_index.failed());
    check(!no_index.enter(a, previous)); check(Fake::writes == 0);

    Fake::reset(); Slot publish_fail; check(publish_fail.initialize());
    check(publish_fail.enter(a, previous));
    Fake::fail_write = 2;
    check(!publish_fail.enter(child, previous)); check(publish_fail.failed());
    check(!publish_fail.current()); // Still-live parent must NOT leak into fallback.
    check(publish_fail.leave(a, nullptr));

    Fake::reset(); Slot restore_fail; check(restore_fail.initialize());
    check(restore_fail.enter(a, previous)); Fake::fail_write = 2;
    check(!restore_fail.leave(a, nullptr)); check(restore_fail.failed());
    Fake::values[0] = reinterpret_cast<void*>(1); // Simulate dead stack pointee.
    const auto reads = Fake::reads;
    check(!restore_fail.current()); check(Fake::reads == reads);
    check(!restore_fail.enter(child, previous)); check(Fake::reads == reads);

    Fake::reset(); Slot read_fail; check(read_fail.initialize()); Fake::fail_read = true;
    check(!read_fail.enter(a, previous)); check(read_fail.failed());

    Fake::reset(); Slot wrong_fiber; check(wrong_fiber.initialize());
    check(wrong_fiber.enter(a, previous)); Fake::context = 1; Fake::values[1] = &a;
    check(!wrong_fiber.current()); check(wrong_fiber.failed());
    Fake::context = 0; check(wrong_fiber.leave(a, nullptr));

    Fake::reset(); Slot restore_mismatch; check(restore_mismatch.initialize());
    check(restore_mismatch.enter(a, previous)); Fake::values[0] = nullptr;
    check(!restore_mismatch.leave(a, nullptr)); check(restore_mismatch.failed());

    for (DWORD initial_error : {ERROR_SUCCESS, ERROR_INVALID_PARAMETER, ERROR_NOT_ENOUGH_MEMORY})
        for (DWORD retried_error : {ERROR_SUCCESS, ERROR_INVALID_PARAMETER, ERROR_NOT_ENOUGH_MEMORY})
            for (bool boot_ok : {false, true})
                for (bool existing_frame : {false, true}) {
                    unsigned read_count=0, bootstrap_count=0;
                    void* value=nullptr;
                    const bool ok=fiber::read_bootstrapped([&]() {
                        return ++read_count==1 ? fiber::ReadResult{nullptr,initial_error} :
                            fiber::ReadResult{existing_frame?&a:nullptr,retried_error};
                    },[&]() {++bootstrap_count;return boot_ok;},value);
                    const bool retry=initial_error==ERROR_INVALID_PARAMETER&&boot_ok;
                    check(read_count==(retry?2u:1u));
                    check(bootstrap_count==(initial_error==ERROR_INVALID_PARAMETER?1u:0u));
                    check(value==(retry&&existing_frame?&a:nullptr));
                    check(ok==(retry?(existing_frame||retried_error==ERROR_SUCCESS):initial_error==ERROR_SUCCESS));
                }
}

// Record the index for test teardown only, after all test fibers are deleted.
// Production deliberately retains its index for process lifetime.
struct TestWindows : fiber::WindowsApi {
    inline static DWORD index = FLS_OUT_OF_INDEXES;
    static DWORD allocate() { return index = fiber::WindowsApi::allocate(); }
};
using RealSlot = fiber::Slot<Frame, TestWindows>;
struct Fixture {
    RealSlot* slot;
    void* main = nullptr;
    void* a = nullptr;
    void* b = nullptr;
    void* migration_return = nullptr;
    DWORD original_thread = 0;
    unsigned cleanup = 0;
};

void nested_exception(Fixture* f) {
    Frame child{}; Frame* previous = nullptr;
    check(f->slot->enter(child, previous));
    check(previous != nullptr && previous->job == 11);
    if (previous) previous->nested = true;
    __try { RaiseException(0xE0429902, 0, 0, nullptr); }
    __finally { check(f->slot->leave(child, previous)); ++f->cleanup; }
}
void catch_nested(Fixture* f) {
    __try { nested_exception(f); check(false); }
    __except (GetExceptionCode() == 0xE0429902 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
}
void WINAPI fiber_a(void* opaque) {
    auto* f = static_cast<Fixture*>(opaque);
    Frame a{}; Frame* previous = nullptr;
    check(f->slot->enter(a, previous) && previous == nullptr); a.job = 11; a.process = 111;
    SwitchToFiber(f->b);
    check(f->slot->current() == &a && a.process == 111 && !a.nested);
    catch_nested(f);
    check(f->slot->current() == &a && a.nested && f->cleanup == 1);
    SwitchToFiber(f->b);
    check(f->slot->current() == &a && a.job == 11);
    check(f->slot->leave(a, nullptr)); check(!f->slot->current());
    SwitchToFiber(f->main);
}
void WINAPI fiber_b(void* opaque) {
    auto* f = static_cast<Fixture*>(opaque);
    // Old queued callback, no wrapper: it must see no suspended A state.
    check(!f->slot->current());
    Frame b{}; Frame* previous = nullptr;
    check(f->slot->enter(b, previous) && previous == nullptr); b.job = 22; b.process = 222;
    SwitchToFiber(f->a);
    check(f->slot->current() == &b && b.process == 222 && !b.nested);
    check(f->slot->leave(b, nullptr)); check(!f->slot->current());
    SwitchToFiber(f->a);
}
void WINAPI deleted_fiber(void* opaque) {
    auto* f = static_cast<Fixture*>(opaque);
    Frame abandoned{}; Frame* previous = nullptr;
    check(f->slot->enter(abandoned, previous));
    SwitchToFiber(f->main); // DeleteFiber releases this stack and its FLS.
}
void WINAPI fresh_fiber(void* opaque) {
    auto* f = static_cast<Fixture*>(opaque);
    check(!f->slot->current());
    SwitchToFiber(f->main);
}
void WINAPI migrated_fiber(void* opaque) {
    auto* f = static_cast<Fixture*>(opaque);
    Frame frame{}; Frame* previous = nullptr;
    check(f->slot->enter(frame, previous)); frame.job = 33;
    f->original_thread = GetCurrentThreadId();
    SwitchToFiber(f->main);
    check(GetCurrentThreadId() != f->original_thread);
    check(f->slot->current() == &frame && frame.job == 33);
    check(f->slot->leave(frame, previous));
    SwitchToFiber(f->migration_return);
}
DWORD WINAPI migration_thread(void* opaque) {
    auto* f = static_cast<Fixture*>(opaque);
    f->migration_return = ConvertThreadToFiber(nullptr);
    check(f->migration_return != nullptr);
    if (!f->migration_return) return 1;
    check(!f->slot->current());
    SwitchToFiber(f->a);
    check(!f->slot->current());
    check(ConvertFiberToThread() != FALSE);
    return 0;
}
void real_tests() {
    RealSlot slot; check(slot.initialize());
    // Sync callback on an ordinary thread is also isolated and restores null.
    Frame normal{}; Frame* previous = nullptr;
    SetLastError(12345);
    check(slot.enter(normal, previous)); check(slot.current() == &normal);
    check(slot.leave(normal, previous)); check(GetLastError() == 12345);

    Fixture f{&slot}; f.main = ConvertThreadToFiber(nullptr);
    check(f.main != nullptr); if (!f.main) return;
    f.a = CreateFiber(0, fiber_a, &f); f.b = CreateFiber(0, fiber_b, &f);
    check(f.a && f.b);
    if (f.a && f.b) SwitchToFiber(f.a);
    check(!slot.current() && !slot.failed());
    if (f.a) DeleteFiber(f.a); if (f.b) DeleteFiber(f.b);
    void* doomed = CreateFiber(0, deleted_fiber, &f); check(doomed != nullptr);
    if (doomed) { SwitchToFiber(doomed); DeleteFiber(doomed); }
    void* fresh = CreateFiber(0, fresh_fiber, &f); check(fresh != nullptr);
    if (fresh) { SwitchToFiber(fresh); DeleteFiber(fresh); }

    f.a = CreateFiber(0, migrated_fiber, &f); check(f.a != nullptr);
    if (f.a) {
        SwitchToFiber(f.a);
        HANDLE thread = CreateThread(nullptr, 0, migration_thread, &f, 0, nullptr);
        check(thread != nullptr);
        if (thread) {
            const DWORD wait=WaitForSingleObject(thread, 10000);
            check(wait == WAIT_OBJECT_0);
            if(wait!=WAIT_OBJECT_0) ExitProcess(2); // Never delete a worker's active stack.
            CloseHandle(thread);
        }
        DeleteFiber(f.a);
    }
    check(!slot.current() && !slot.failed());
    check(ConvertFiberToThread() != FALSE);
    check(FlsFree(TestWindows::index) != FALSE);
    check(FlsFree(fiber::WindowsApi::bootstrap_index.load()) != FALSE);
}

int main() {
    fake_tests(); real_tests();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
