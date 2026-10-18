#include "enemy_jointblend_read_windows.h"
#include "nadine_p99_group_memory.h"
#include <Windows.h>
#include <cstring>
#include <limits>

namespace coop::enemy_jointblend_read {
namespace {
int read_exception(unsigned code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
        code == EXCEPTION_GUARD_PAGE ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
// Separate leaf: no destructible locals across SEH. Catch only memory faults,
// not arbitrary application exceptions. A copy is accepted only in full.
bool copy_bytes(std::uintptr_t address, void* output, std::size_t bytes) noexcept {
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), bytes); return true; }
    __except (read_exception(GetExceptionCode())) { return false; }
}
}
bool Reader::refuse(Failure why) noexcept {
    if (failure_ == Failure::none) failure_ = why;
    ++totals_.refusals;
    return false;
}
bool Reader::within_budget() noexcept {
    if (failure_ != Failure::none) return false;
    const auto now = tick_ ? tick_(context_) : 0;
    if (!start_ || !now || now < start_) return refuse(Failure::clock);
    return now - start_ <= allowance_ || refuse(Failure::budget);
}
bool Reader::begin_event(std::uint64_t start, std::uint64_t allowance, Tick tick, void* context) noexcept {
    if (pages_) return refuse(Failure::scope); // Never reset another event's cache.
    failure_ = Failure::none;
    start_ = start; allowance_ = allowance; tick_ = tick; context_ = context;
    if (!within_budget()) return false;
    pages_.emplace(); ++totals_.events;
    return true;
}
void Reader::end_event() noexcept {
    pages_.reset(); tick_ = nullptr; context_ = nullptr;
}
bool Reader::query(std::uintptr_t address, p99_group_readability::Region& out) noexcept {
    if (!within_budget()) return false;
    const DWORD saved = GetLastError();
    ++totals_.queries;
    const bool ok = p99_group_memory::query_region(address, out);
    SetLastError(saved);
    if (!within_budget()) return false;
    return ok;
}
bool Reader::copy(std::uintptr_t address, void* output, std::size_t bytes) noexcept {
    ++totals_.attempts;
    if (!pages_) return refuse(Failure::scope);
    if (!within_budget()) return false;
    const auto dst = reinterpret_cast<std::uintptr_t>(output);
    constexpr auto max = std::numeric_limits<std::uintptr_t>::max();
    if (!address || !dst || !bytes || bytes > 0x40B0 || bytes > max-address || bytes > max-dst ||
        (address < dst+bytes && dst < address+bytes)) return refuse(Failure::span);
    if (!pages_->readable(address, bytes, [this](auto at, auto& region) { return query(at, region); }))
        return refuse(Failure::pages);
    // Query work can consume the remaining allowance. Do not start a copy then.
    if (!within_budget()) return false;
    const DWORD saved = GetLastError();
    const bool copied = copy_bytes(address, output, bytes);
    SetLastError(saved);
    if (!copied) { ++totals_.faults; return refuse(Failure::exception); }
    ++totals_.copies; totals_.bytes += bytes;
    return within_budget(); // A slow/faulting page is not preemptible; reject afterwards too.
}
}
