#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace coop::p99_group_readability {
// Page accessibility only. This never establishes allocator membership, live
// ownership, or synchronization; the independent WorkBuffer audit is mandatory.
struct Region {
    std::uintptr_t base = 0;
    std::size_t bytes = 0;
    bool committed = false;
    bool readable = false;
    bool guarded = false;
};
inline constexpr std::size_t kMaximumSpan = 0x20000;
inline constexpr std::size_t kMaximumRegions = 64;

template<class Query>
[[nodiscard]] bool readable(std::uintptr_t address, std::size_t bytes, Query query) noexcept {
    if (address == 0 || bytes == 0 || bytes > kMaximumSpan ||
        bytes > std::numeric_limits<std::uintptr_t>::max()-address) return false;
    const auto end = address+bytes;
    auto cursor = address;
    for (std::size_t count=0;cursor<end && count<kMaximumRegions;++count) {
        Region region{};
        if (!query(cursor,region) || !region.committed || !region.readable || region.guarded ||
            region.bytes==0 || region.base>cursor ||
            region.bytes>std::numeric_limits<std::uintptr_t>::max()-region.base) return false;
        const auto region_end=region.base+region.bytes;
        if (region_end<=cursor) return false;
        cursor=region_end<end?region_end:end;
    }
    return cursor==end;
}
} // namespace coop::p99_group_readability
