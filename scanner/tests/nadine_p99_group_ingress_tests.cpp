#include "nadine_p99_group_ingress.h"
#include <iostream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace {
// Isolated ABI smoke test, NOT an engine or production-TLS test. Execute the
// real planner's two instruction kinds and leaf relay against compiled code;
// check one native call and compiler-generated finally unwind on an exception.
struct Fixture { unsigned calls = 0, finished = 0; bool raise = false; };
__declspec(noinline) void native_fixture(void* opaque) {
    auto& state = *static_cast<Fixture*>(opaque);
    ++state.calls;
    if (state.raise) RaiseException(0xE0429901, 0, 0, nullptr);
}
__declspec(noinline) void wrapper_fixture(void* opaque) {
    __try { native_fixture(opaque); }
    __finally { ++static_cast<Fixture*>(opaque)->finished; }
}
bool invoke_fixture(void* entry, Fixture* state) {
    __try { reinterpret_cast<void(*)(void*)>(entry)(state); return true; }
    __except (GetExceptionCode() == 0xE0429901 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}
bool executable_smoke(std::size_t index) {
    namespace ingress = coop::p99_group_ingress;
    auto* memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000,
        MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE));
    if (!memory) return false;
    const auto& site = ingress::kSites[index];
    const auto address = reinterpret_cast<std::uintptr_t>(memory);
    const auto patch = ingress::plan(index, address - site.rva,
        std::span{site.stock}.first(site.size), address + 64,
        reinterpret_cast<std::uintptr_t>(&wrapper_fixture));
    bool success = false;
    if (address > site.rva && patch) {
        std::memcpy(memory, patch->instruction.data(), patch->size);
        memory[patch->size] = 0xC3; // LEA accessor returns RAX; JMP never reaches it.
        std::memcpy(memory + 64, patch->relay.data(), patch->relay.size());
        DWORD old = 0;
        if (VirtualProtect(memory, 0x1000, PAGE_EXECUTE_READ, &old) &&
            FlushInstructionCache(GetCurrentProcess(), memory, 0x1000)) {
            DWORD64 image_base = 0;
            const bool wrapper_has_unwind = RtlLookupFunctionEntry(
                reinterpret_cast<DWORD64>(&wrapper_fixture), &image_base, nullptr) != nullptr;
            const bool relay_is_leaf = RtlLookupFunctionEntry(address + 64, &image_base, nullptr) == nullptr;
            void* entry = memory;
            if (index == 0) entry = reinterpret_cast<void*(*)()>(memory)();
            Fixture normal{}, exceptional{};
            exceptional.raise = true;
            const bool normal_return = invoke_fixture(entry, &normal);
            const bool exceptional_return = invoke_fixture(entry, &exceptional);
            success = (index != 0 || entry == memory + 64) && wrapper_has_unwind && relay_is_leaf &&
                normal_return && normal.calls == 1 && normal.finished == 1 &&
                !exceptional_return && exceptional.calls == 1 && exceptional.finished == 1;
            if (!success) std::cerr << "smoke " << index << " unwind=" << wrapper_has_unwind
                << " leaf=" << relay_is_leaf << " normal=" << normal_return << '/' << normal.calls << '/' << normal.finished
                << " exceptional=" << exceptional_return << '/' << exceptional.calls << '/' << exceptional.finished << '\n';
        } else std::cerr << "smoke protect/flush failed " << GetLastError() << '\n';
    } else {
        std::cerr << "smoke plan failed " << index << " address=" << std::hex << address << std::dec << '\n';
    }
    return VirtualFree(memory, 0, MEM_RELEASE) != FALSE && success;
}
}
#endif

int main() {
    namespace ingress = coop::p99_group_ingress;
    unsigned checks = 0, failures = 0;
    const auto check = [&](bool value) { ++checks; if (!value) ++failures; };
    constexpr std::uintptr_t hook = 0x7FFABCDE1230;
    for (std::size_t i = 0; i < ingress::kSites.size(); ++i) {
        const auto& site = ingress::kSites[i];
        const auto stock = std::span{site.stock}.first(site.size);
        for (std::uintptr_t base : {0x140000000ULL, 0x7FF600000000ULL, 0x10000ULL}) {
            const auto next = base + site.rva + site.size;
            for (std::int64_t delta : {-0x80000000LL, -0x10000LL, -1LL, 0LL, 1LL, 0x10000LL, 0x7FFFFFFFLL}) {
                if (delta < 0 && next < static_cast<std::uintptr_t>(-delta)) continue;
                const auto relay = delta < 0 ? next - static_cast<std::uintptr_t>(-delta) : next + delta;
                const auto patch = ingress::plan(i, base, stock, relay, hook);
                check(patch.has_value());
                if (!patch) continue;
                check(patch->size == site.size);
                for (std::size_t b = 0; b < site.displacement_offset; ++b)
                    check(patch->instruction[b] == stock[b]);
                std::int32_t encoded = 0;
                std::memcpy(&encoded, patch->instruction.data() + site.displacement_offset, 4);
                check(encoded == delta);
                check(patch->relay[0] == 0xFF && patch->relay[1] == 0x25);
                for (std::size_t b = 2; b < 6; ++b) check(patch->relay[b] == 0);
                std::uintptr_t destination = 0;
                std::memcpy(&destination, patch->relay.data() + 6, 8);
                check(destination == hook);
            }
            check(!ingress::plan(i, base, stock, next + 0x80000000ULL, hook));
            if (next > 0x80000001ULL)
                check(!ingress::plan(i, base, stock, next - 0x80000001ULL, hook));
            for (std::size_t b = 0; b < site.size; ++b) {
                for (unsigned bit = 0; bit < 8; ++bit) {
                    auto mutated = site.stock;
                    mutated[b] ^= static_cast<std::uint8_t>(1u << bit);
                    check(!ingress::plan(i, base, std::span{mutated}.first(site.size), next, hook));
                }
            }
            for (std::size_t size = 0; size < site.size; ++size)
                check(!ingress::plan(i, base, stock.first(size), next, hook));
            check(!ingress::plan(i, base, stock, 0, hook));
            check(!ingress::plan(i, base, stock, next, 0));
            check(!ingress::plan(i, 0, stock, next, hook));
            check(!ingress::plan(i, UINTPTR_MAX - site.rva, stock, next, hook));
            check(!ingress::plan(i, base, stock, UINTPTR_MAX - 5, hook));
        }
    }
    check(!ingress::plan(2, 0x140000000, {}, 0x140010000, hook));
    check(ingress::relative(0x80000000, 0) == INT32_MIN);
    check(!ingress::relative(0x80000001, 0));
    check(ingress::relative(0, 0x7FFFFFFF) == INT32_MAX);
    check(!ingress::relative(0, 0x80000000));
    check(!ingress::add(UINTPTR_MAX, 1));
    check(ingress::add(UINTPTR_MAX, 0) == UINTPTR_MAX);
    ingress::InstallationGate attempt;
    check(attempt.begin());
    // Neither a simulated success, preflight failure nor uncertain rollback
    // resets the one-way gate. A repeat cannot clear the published target.
    for (unsigned retry = 0; retry < 100; ++retry) check(!attempt.begin());
#if defined(_WIN32)
    check(executable_smoke(0));
    check(executable_smoke(1));
#endif
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
