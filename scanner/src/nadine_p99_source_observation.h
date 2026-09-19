#pragma once
#include "nadine_p99_group_budget.h"
#include <atomic>
#include <cstdint>

namespace coop::p99_source_observation {
inline constexpr std::uint32_t kMaximumProbes = 600;
inline constexpr std::uint32_t kMaximumPreparations = 1;
inline constexpr std::uint64_t kProbeIntervalMs = 100;
inline constexpr std::uint64_t kLifetimeMs = 120000;

enum class Probe { kGranted, kBusy, kThrottled, kStopped };
enum class Reason {
    kNone, kSuccess, kRefused, kBudget, kClock, kExpired, kProbeLimit,
    kException, kProtocol
};
inline constexpr const char* reason_name(Reason reason) noexcept {
    switch (reason) {
    case Reason::kNone: return "none";
    case Reason::kSuccess: return "success";
    case Reason::kRefused: return "refused";
    case Reason::kBudget: return "budget";
    case Reason::kClock: return "clock";
    case Reason::kExpired: return "expired";
    case Reason::kProbeLimit: return "probe-limit";
    case Reason::kException: return "exception";
    case Reason::kProtocol: return "protocol";
    }
    return "unknown";
}

// Value-only one-shot admission, not lifetime/owner authority. No game pointers,
// waits, callbacks, allocations or spin loop. Only a caller receiving kGranted
// owns the lease and may finish/abort it; use its stack flag in __finally.
// begin_probe is after the cheap retained-identity gate, before owner preselect.
// A selected probe keeps its lease through preparation and consumes the sole
// attempt BEFORE calling it. Nonselected probes must also be timed by the caller;
// abort(kBudget/kClock) instead of finish_probe on invalid/over-budget timing.
// finish_preparation receives the combined probe+preparation interval, never
// stock execution. Deadline/budget containment is post-call, not preemption.
class Policy {
    enum class Phase { kUnarmed, kArming, kIdle, kProbe, kPreparation, kStopped };
    static_assert(std::atomic<Phase>::is_always_lock_free);
public:
    bool arm(std::uint64_t install_ms, std::uint64_t qpc_frequency) noexcept {
        auto expected = Phase::kUnarmed;
        if (!phase_.compare_exchange_strong(expected, Phase::kArming)) return false;
        install_ms_ = install_ms;
        last_observed_ms_ = install_ms;
        limit_ticks_ = p99_group_budget::one_millisecond(qpc_frequency);
        if (limit_ticks_ == 0) { stop(Reason::kClock); return false; }
        phase_.store(Phase::kIdle, std::memory_order_release);
        return true;
    }

    [[nodiscard]] Probe begin_probe(std::uint64_t now_ms) noexcept {
        auto expected = Phase::kIdle;
        if (!phase_.compare_exchange_strong(expected, Phase::kProbe,
                std::memory_order_acq_rel)) {
            return expected == Phase::kProbe || expected == Phase::kPreparation ||
                expected == Phase::kArming ? Probe::kBusy : Probe::kStopped;
        }
        if (!within_lifetime(now_ms)) return Probe::kStopped;
        const auto probes = probes_.load(std::memory_order_relaxed);
        if (probes == kMaximumProbes) { stop(Reason::kProbeLimit); return Probe::kStopped; }
        if (probes != 0 && now_ms - last_probe_ms_ < kProbeIntervalMs) {
            phase_.store(Phase::kIdle, std::memory_order_release);
            return Probe::kThrottled;
        }
        last_probe_ms_ = now_ms;
        probes_.store(probes + 1, std::memory_order_relaxed);
        return Probe::kGranted;
    }

    [[nodiscard]] bool finish_probe(std::uint64_t now_ms, bool selected) noexcept {
        if (phase_.load(std::memory_order_acquire) != Phase::kProbe) {
            stop(Reason::kProtocol); return false;
        }
        if (!within_lifetime(now_ms)) return false;
        if (selected) {
            if (attempts_.load(std::memory_order_relaxed) != 0) {
                stop(Reason::kProtocol); return false;
            }
            attempts_.store(kMaximumPreparations, std::memory_order_relaxed);
            phase_.store(Phase::kPreparation, std::memory_order_release);
            return true;
        }
        if (probes_.load(std::memory_order_relaxed) == kMaximumProbes)
            stop(Reason::kProbeLimit);
        else phase_.store(Phase::kIdle, std::memory_order_release);
        return false;
    }

    Reason finish_preparation(std::uint64_t begin_ticks, std::uint64_t end_ticks,
        bool clock_valid, bool prepared) noexcept {
        if (phase_.load(std::memory_order_acquire) != Phase::kPreparation)
            return stop(Reason::kProtocol);
        if (!clock_valid || end_ticks < begin_ticks) return stop(Reason::kClock);
        p99_group_budget::Budget budget{limit_ticks_};
        if (!budget.charge(begin_ticks, end_ticks)) return stop(Reason::kBudget);
        return stop(prepared ? Reason::kSuccess : Reason::kRefused);
    }

    // Lease-owner only. A stopped policy cannot be restarted or have its first
    // terminal reason replaced by a later finally/exception/clock observation.
    Reason abort(Reason reason = Reason::kException) noexcept {
        if (reason == Reason::kNone || reason == Reason::kSuccess)
            reason = Reason::kProtocol;
        return stop(reason);
    }
    void abandon() noexcept { (void)abort(); }
    [[nodiscard]] Reason reason() const noexcept { return reason_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint32_t probes() const noexcept { return probes_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t attempts() const noexcept { return attempts_.load(std::memory_order_relaxed); }

private:
    bool within_lifetime(std::uint64_t now_ms) noexcept {
        if (now_ms < last_observed_ms_) {
            stop(Reason::kClock); return false;
        }
        last_observed_ms_ = now_ms;
        if (now_ms - install_ms_ >= kLifetimeMs) { stop(Reason::kExpired); return false; }
        return true;
    }
    Reason stop(Reason reason) noexcept {
        auto expected = Reason::kNone;
        reason_.compare_exchange_strong(expected, reason, std::memory_order_acq_rel);
        phase_.store(Phase::kStopped, std::memory_order_release);
        return reason_.load(std::memory_order_acquire);
    }
    std::atomic<Phase> phase_{Phase::kUnarmed};
    std::atomic<Reason> reason_{Reason::kNone};
    std::atomic<std::uint32_t> probes_{0}, attempts_{0};
    // Written before publication by arm, or exclusively by the granted lease.
    std::uint64_t install_ms_ = 0, last_probe_ms_ = 0, last_observed_ms_ = 0, limit_ticks_ = 0;
};
} // namespace coop::p99_source_observation
