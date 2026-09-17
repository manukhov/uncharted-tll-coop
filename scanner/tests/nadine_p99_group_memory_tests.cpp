#include "nadine_p99_group_memory.h"
#include <iostream>

namespace {
struct Allocation {
    void* pointer = nullptr;
    ~Allocation() { if (pointer) VirtualFree(pointer, 0, MEM_RELEASE); }
};
}

int main() {
    namespace memory = coop::p99_group_memory;
    unsigned checks = 0, failures = 0;
    const auto check = [&](bool value) { ++checks; if (!value) ++failures; };
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto page = static_cast<std::size_t>(system.dwPageSize);
    if (page == 0 || page > 0x20000) return 2;
    // Test-process memory ONLY. Never attach to or access the game.
    const auto extent = std::size_t{0x20000} + page;
    Allocation arena{VirtualAlloc(nullptr, extent, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)};
    if (!arena.pointer) return 2;
    const auto base = reinterpret_cast<std::uintptr_t>(arena.pointer);
    check(memory::readable(base, 0x20000));
    check(memory::readable(base + 127, 0x20000));
    check(!memory::readable(base, 0x20001));
    check(!memory::readable(0, 1));
    check(!memory::readable(base, 0));
    check(!memory::readable(UINTPTR_MAX - 3, 8));

    {
        coop::p99_group_readability::Batch stage;
        check(stage.readable(base, 0x20000, memory::query_region));
        check(stage.readable(base + 127, 1024, memory::query_region));
        check(stage.query_count() == 1);
    } // End stage BEFORE changing page protection.

    DWORD previous = 0;
    auto* middle = reinterpret_cast<void*>(base + page);
    for (const DWORD protection : {PAGE_READONLY, PAGE_READWRITE,
                                  PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE}) {
        if (!VirtualProtect(middle, page, protection, &previous)) return 2;
        check(memory::readable(base, 0x20000));
    }
    if (!VirtualProtect(middle, page, PAGE_READWRITE | PAGE_GUARD, &previous)) return 2;
    {
        coop::p99_group_readability::Batch next;
        check(!next.readable(base, 0x20000, memory::query_region));
        check(next.query_count() == 2); // RW prefix followed by guard, fresh queries.
        MEMORY_BASIC_INFORMATION info{};
        check(VirtualQuery(middle, &info, sizeof(info)) == sizeof(info));
        check((info.Protect & PAGE_GUARD) != 0);
        check(!next.readable(base, 1, memory::query_region));
        check(next.query_count() == 2); // Failed batch never retries/probes payload.
    }
    for (unsigned repetition = 0; repetition < 16; ++repetition) {
        check(!memory::readable(base, 0x20000));
        check(!memory::readable(base + page, 1));
        MEMORY_BASIC_INFORMATION info{};
        check(VirtualQuery(middle, &info, sizeof(info)) == sizeof(info));
        check((info.Protect & PAGE_GUARD) != 0); // Alarm was NOT consumed.
        check(info.State == MEM_COMMIT);
    }
    if (!VirtualProtect(middle, page, PAGE_NOACCESS, &previous)) return 2;
    check(!memory::readable(base, 0x20000));
    check(memory::readable(base, page));
    if (!VirtualFree(middle, page, MEM_DECOMMIT)) return 2;
    check(!memory::readable(base, 0x20000));
    if (!VirtualAlloc(middle, page, MEM_COMMIT, PAGE_READWRITE)) return 2;
    check(memory::readable(base, 0x20000)); // Fresh query, no cached denial.
    Allocation reserved{VirtualAlloc(nullptr, page, MEM_RESERVE, PAGE_NOACCESS)};
    if (!reserved.pointer) return 2;
    check(!memory::readable(reinterpret_cast<std::uintptr_t>(reserved.pointer), 1));

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
