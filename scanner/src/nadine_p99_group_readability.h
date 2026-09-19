#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
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

// One uninterrupted read-only admission batch, never a job/frame cache.
// Do not reuse across native execution, allocator/source work or callbacks.
// Only p99_retarget_table_admission::Admission has a narrower, explicitly
// audited exception for the fixed-image table across read-only native lookups;
// no general borrowed-source batch inherits that exception.
// VirtualQuery reports a snapshot, NOT a lease against concurrent protection
// changes. The caller retains the same normal-job lifetime contract and SEH.
class Batch {
public:
    Batch() = default;
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    Batch(Batch&&) = delete;
    Batch& operator=(Batch&&) = delete;

    [[nodiscard]] std::size_t query_count() const noexcept { return queries_; }

    template<class Query>
    [[nodiscard]] bool readable(std::uintptr_t address, std::size_t bytes, Query query) noexcept {
        if (failed_) return false;
        if (address == 0 || bytes == 0 || bytes > kMaximumSpan ||
            bytes > std::numeric_limits<std::uintptr_t>::max() - address) {
            failed_ = true;
            return false;
        }
        const auto end = address + bytes;
        auto cursor = address;
        for (std::size_t steps = 0; cursor < end && steps < kMaximumRegions; ++steps) {
            const Region* covering = nullptr;
            for (std::size_t i = 0; i < count_; ++i) {
                const auto& r = regions_[i];
                if (cursor >= r.base && cursor - r.base < r.bytes) { covering = &r; break; }
            }
            if (!covering) {
                if (count_ == regions_.size()) { failed_ = true; return false; }
                Region region{};
                ++queries_;
                if (!query(cursor, region) || !region.committed || !region.readable || region.guarded ||
                    region.bytes == 0 || region.base > cursor ||
                    region.bytes > std::numeric_limits<std::uintptr_t>::max() - region.base ||
                    region.base + region.bytes <= cursor) {
                    failed_ = true;
                    return false;
                }
                regions_[count_] = region;
                covering = &regions_[count_++];
            }
            const auto region_end = covering->base + covering->bytes;
            cursor = region_end < end ? region_end : end;
        }
        if (cursor != end) failed_ = true;
        return cursor == end;
    }

private:
    std::array<Region, kMaximumRegions> regions_{};
    std::size_t count_ = 0;
    std::size_t queries_ = 0;
    bool failed_ = false;
};
} // namespace coop::p99_group_readability
