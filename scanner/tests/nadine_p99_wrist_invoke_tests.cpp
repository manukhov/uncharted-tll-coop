#include "nadine_p99_wrist_invoke.h"
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <source_location>

namespace policy = coop::p99_wrist_invoke;
unsigned checks = 0, failures = 0;
void check(bool value, std::source_location where = std::source_location::current()) {
    ++checks;
    if (!value) { ++failures; std::cerr << "failed line " << where.line() << '\n'; }
}

constexpr DWORD kNativeFault = 0xE0420195;
constexpr DWORD kReadFault = 0xE0420295;
constexpr DWORD kNativeError = 0x579;
enum class Action { normal, reject, no_callback, native_fault, callback_fault,
    nested_native, nested_callback, nested_native_fault };
struct Case {
    policy::State state{};
    Action action = Action::normal;
    unsigned calls = 0, captures = 0, depth = 0, caught = 0;
    unsigned unwind_checks = 0;
    std::uint32_t status = 0xDEAD, result = 0xBEEF;
    std::uint32_t* returned = nullptr;
    DWORD last_error = 0;
    std::array<std::byte, 256> source{};
    struct {
        std::uint64_t before = 0x123456789ABCDEF0;
        std::array<std::byte, 256> bytes{};
        std::uint64_t after = 0x0FEDCBA987654321;
    } owned;
};
Case* current = nullptr; // Isolated single-thread fixture, not runtime storage.

extern "C" std::uint32_t* p99_wrist_native_entry_probe(std::uint32_t*, const void*);
extern "C" unsigned p99_wrist_invoke_abi_probe(void(__fastcall*)());
bool __fastcall capture(void*, const void*);
__declspec(noinline) void __fastcall abi_thunk();

__declspec(noinline) void verify_active_unwind(Case& test) {
    CONTEXT c{};
    RtlCaptureContext(&c);
    DWORD64 invoke_base = 0, thunk_base = 0;
    auto* invoke_entry = RtlLookupFunctionEntry(
        reinterpret_cast<DWORD64>(&policy::invoke), &invoke_base, nullptr);
    auto* thunk_entry = RtlLookupFunctionEntry(
        reinterpret_cast<DWORD64>(&abi_thunk), &thunk_base, nullptr);
    check(invoke_entry != nullptr && thunk_entry != nullptr);
    if (!invoke_entry || !thunk_entry) return;
    bool found_invoke = false, found_probe = false;
    for (unsigned depth = 0; depth < 20 && c.Rip; ++depth) {
        DWORD64 image_base = 0, establisher = 0;
        void* handler = nullptr;
        auto* entry = RtlLookupFunctionEntry(c.Rip, &image_base, nullptr);
        check(entry != nullptr);
        if (!entry) break;
        const auto begin = image_base + entry->BeginAddress;
        if (begin == reinterpret_cast<DWORD64>(&p99_wrist_invoke_abi_probe)) {
            found_probe = true;
            break;
        }
        const bool at_invoke = begin == invoke_base + invoke_entry->BeginAddress;
        const auto old_sp = c.Rsp;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, c.Rip, entry,
            &c, &handler, &establisher, nullptr);
        if (at_invoke) {
            found_invoke = true;
            check(c.Rsp > old_sp && (c.Rsp & 15u) == 0);
            check(c.Rip >= thunk_base + thunk_entry->BeginAddress &&
                  c.Rip < thunk_base + thunk_entry->EndAddress);
        }
    }
    check(found_invoke && found_probe);
    ++test.unwind_checks;
}

extern "C" std::uint32_t* p99_wrist_native_body(
    std::uint32_t* status, const void* context, std::uintptr_t entry_sp) {
    auto& test = *static_cast<Case*>(const_cast<void*>(context));
    ++test.calls;
    check(status == &test.status && context == &test);
    check((entry_sp & 15u) == 8); // Native entry, after its CALL return push.
    *status = 0x51515151; // Intentional stock fixture write, NOT wrapper output.
    if (test.action == Action::native_fault ||
        (test.action == Action::nested_native_fault && test.depth != 0))
        RaiseException(kNativeFault, 0, 0, nullptr);
    if ((test.action == Action::nested_native || test.action == Action::nested_native_fault) &&
        test.depth == 0) {
        ++test.depth;
        const auto nested = policy::invoke(test.state, &p99_wrist_native_entry_probe,
            status, context, &capture, &test);
        --test.depth;
        check(nested == &test.result);
        check(test.state.phase == policy::Phase::reentered && test.state.active);
    }
    SetLastError(kNativeError);
    return &test.result; // Deliberately distinct from the status pointer.
}

bool __fastcall capture(void* cookie, const void* context) {
    auto& test = *static_cast<Case*>(cookie);
    ++test.captures;
    check(cookie == context && context == &test);
    check(test.state.active && test.state.phase == policy::Phase::callback);
    check(test.calls == 1 && test.status == 0x51515151);
    verify_active_unwind(test);
    // Only fixture-owned storage is read/copied. Partial output remains private.
    if (test.action == Action::callback_fault) {
        std::memcpy(test.owned.bytes.data(), test.source.data(), 19);
        RaiseException(kReadFault, 0, 0, nullptr);
    }
    if (test.action == Action::nested_callback) {
        ++test.depth;
        check(policy::invoke(test.state, &p99_wrist_native_entry_probe,
            &test.status, context, &capture, cookie) == &test.result);
        --test.depth;
    }
    std::memcpy(test.owned.bytes.data(), test.source.data(), test.source.size());
    SetLastError(0xBAD); // Must not replace the native function's last-error.
    return test.action != Action::reject;
}

__declspec(noinline) void __fastcall abi_thunk() {
    auto& test = *current;
    __try {
        test.returned = policy::invoke(test.state, &p99_wrist_native_entry_probe,
            &test.status, &test, test.action == Action::no_callback ? nullptr : &capture, &test);
        test.last_error = GetLastError();
    } __except (GetExceptionCode() == kNativeFault ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        ++test.caught;
        check(!test.state.active && test.state.phase == policy::Phase::native_fault);
    }
}

void run_case(Action action, policy::Phase expected, unsigned calls, unsigned captures) {
    Case test{};
    test.action = action;
    for (std::size_t i = 0; i < test.source.size(); ++i)
        test.source[i] = static_cast<std::byte>((i * 29 + 7) & 255);
    const auto original = test.source;
    current = &test;
    check(p99_wrist_invoke_abi_probe(&abi_thunk) == 0);
    check(!test.state.active && test.state.phase == expected);
    check(test.calls == calls && test.captures == captures);
    check(test.source == original);
    check(test.owned.before == 0x123456789ABCDEF0 && test.owned.after == 0x0FEDCBA987654321);
    if (expected == policy::Phase::native_fault) {
        check(test.caught == 1 && test.returned == nullptr);
    } else {
        check(test.caught == 0 && test.returned == &test.result);
        check(test.last_error == kNativeError);
    }
    check(test.unwind_checks == captures);
    if (expected == policy::Phase::captured) check(test.owned.bytes == original);
    if (expected == policy::Phase::callback_fault) {
        check(test.owned.bytes[0] == original[0] && test.owned.bytes[19] == std::byte{});
        check(test.caught == 0); // Callback SEH stayed inside wrapper, stock ran once.
    }
    // Terminal state cannot recapture, but later stock invocation still occurs.
    const auto captures_before = test.captures;
    const auto calls_before = test.calls;
    test.action = Action::normal;
    test.caught = 0;
    test.returned = nullptr;
    check(p99_wrist_invoke_abi_probe(&abi_thunk) == 0);
    check(test.state.phase == expected && !test.state.active);
    check(test.calls == calls_before + 1 && test.captures == captures_before);
    check(test.returned == &test.result && test.last_error == kNativeError && test.caught == 0);
    current = nullptr;
}

int main() {
    DWORD64 base = 0;
    for (auto address : {reinterpret_cast<DWORD64>(&policy::invoke),
                        reinterpret_cast<DWORD64>(&p99_wrist_native_entry_probe),
                        reinterpret_cast<DWORD64>(&p99_wrist_invoke_abi_probe)})
        check(RtlLookupFunctionEntry(address, &base, nullptr) != nullptr);
    run_case(Action::normal, policy::Phase::captured, 1, 1);
    run_case(Action::reject, policy::Phase::rejected, 1, 1);
    run_case(Action::no_callback, policy::Phase::rejected, 1, 0);
    run_case(Action::native_fault, policy::Phase::native_fault, 1, 0);
    run_case(Action::callback_fault, policy::Phase::callback_fault, 1, 1);
    run_case(Action::nested_native, policy::Phase::reentered, 2, 0);
    run_case(Action::nested_callback, policy::Phase::reentered, 2, 1);
    run_case(Action::nested_native_fault, policy::Phase::native_fault, 2, 0);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
