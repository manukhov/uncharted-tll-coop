#pragma once
#include "nadine_p99_group_readability.h"
#include <Windows.h>

namespace coop::p99_group_memory {
// No payload access: PAGE_GUARD must be rejected without consuming its alarm.
// This adapter is shared by runtime and the isolated Windows allocation test.
// Keep it behind local-owner admission, never on every animation job.
[[nodiscard]] inline bool query_region(
    std::uintptr_t cursor, p99_group_readability::Region& region) noexcept {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    const auto protection = mbi.Protect & 0xFF;
    const bool accessible = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_WRITECOPY;
    region = {reinterpret_cast<std::uintptr_t>(mbi.BaseAddress), mbi.RegionSize,
        mbi.State == MEM_COMMIT, accessible, (mbi.Protect & PAGE_GUARD) != 0};
    return true;
}

[[nodiscard]] inline bool readable(std::uintptr_t address, std::size_t bytes) noexcept {
    return p99_group_readability::readable(address, bytes, query_region);
}
} // namespace coop::p99_group_memory
