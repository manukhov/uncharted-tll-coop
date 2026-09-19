#pragma once
#include "nadine_p99_source_read.h"

namespace coop::p99_retarget_table_admission {
inline constexpr std::uintptr_t kTableRva = 0x3864A00;
inline constexpr std::size_t kEntryBytes = 0x40;
inline constexpr std::uint32_t kMaximumEntries = 4096;
static_assert(kMaximumEntries * kEntryBytes == p99_source_read::kMaximumSourceSpan);

// The sole source Reader exception across native execution: fixed image storage
// only, within ONE existing SkelTable-locked capture/identity invocation. The
// complete allowlist is Find1417D0BD0 and Resolve1417CD100 -> 1417D1170: no calls
// out, callbacks, logging, allocation or global writes (the walker writes only
// its caller-owned iterator). Destroy before unlock, native math, or other calls.
//
// This object retains only page observations, NEVER count/payload/entry/resource
// pointers. Every request validates the current image/count and its complete
// span; growth queries any uncovered regions. The lock does not exclude all
// in-place builders or concurrent protection changes. Existing fresh payload
// identity checks, normal-job lifetime contract and SEH remain mandatory.
template<class Query, class Copy>
class Admission {
public:
    Admission(std::uintptr_t image, Query query, Copy copy)
        : image_(image), reader_(query, copy) {}
    Admission(const Admission&) = delete;
    Admission& operator=(const Admission&) = delete;
    Admission(Admission&&) = delete;
    Admission& operator=(Admission&&) = delete;

    [[nodiscard]] bool range(std::uintptr_t current_image, std::uint32_t count) noexcept {
        if (failed_) return false;
        std::uintptr_t table = 0;
        if (current_image != image_ || count == 0 || count > kMaximumEntries ||
            !p99_source_read::add(image_, kTableRva, table) ||
            !reader_.range(table, static_cast<std::size_t>(count) * kEntryBytes)) {
            failed_ = true;
            return false;
        }
        return true;
    }
    [[nodiscard]] std::size_t query_count() const noexcept { return reader_.query_count(); }
private:
    const std::uintptr_t image_;
    p99_source_read::Reader<Query, Copy> reader_;
    bool failed_ = false;
};
} // namespace coop::p99_retarget_table_admission
