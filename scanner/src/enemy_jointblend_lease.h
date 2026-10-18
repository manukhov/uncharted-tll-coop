#pragma once
#include <atomic>

namespace coop::enemy_jointblend_collector {
// One-shot admission and output ownership share ONE modification order. Two
// independent armed/active atomics permit the stop/admit store-buffering race.
// Release is owner-only and must follow every mutable output write. This is a
// collector-data lease, never authority to unload wrapper/relay/FLS resources.
class Lease final {
    static constexpr unsigned kArmed=1, kStopped=2, kBusy=4;
    std::atomic<unsigned> state_{0};
public:
    bool arm() noexcept {
        unsigned expected=0;
        return state_.compare_exchange_strong(expected,kArmed,std::memory_order_acq_rel);
    }
    bool acquire() noexcept {
        unsigned expected=kArmed;
        return state_.compare_exchange_strong(expected,kArmed|kBusy,std::memory_order_acq_rel);
    }
    void stop() noexcept { state_.fetch_or(kStopped,std::memory_order_acq_rel); }
    bool requested() const noexcept {
        return (state_.load(std::memory_order_acquire)&(kArmed|kStopped))==kArmed;
    }
    bool quiescent() const noexcept {
        return (state_.load(std::memory_order_acquire)&(kStopped|kBusy))==kStopped;
    }
    void release() noexcept { state_.fetch_and(~kBusy,std::memory_order_release); }
};
} // namespace coop::enemy_jointblend_collector
