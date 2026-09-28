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
// Detailed transition results carry reporting ownership. kTerminal means THIS
// invocation acquired Idle->Probe and stopped it; kStopped only observed an
// unarmed/already-stopped policy and must not report somebody else's outcome.
enum class ProbeDetailed { kGranted, kBusy, kThrottled, kStopped, kTerminal };
enum class ProbeFinish { kReleased, kSelected, kTerminal };
enum class ExpiryPoll { kIdle, kBusy, kStopped, kTerminal };
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
// waits, allocations or spin loop. Clock-entry APIs invoke only one cheap
// noexcept scalar clock callback under the acquired lease, not arbitrary work.
// Only a caller receiving kGranted
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

    // Sample only AFTER acquiring the lease. A timestamp captured before CAS
    // can become stale while preempted behind a later callback, producing a
    // false clock-regression terminal even though the system clock is sound.
    template<class Now>
    [[nodiscard]] ProbeDetailed begin_probe_clock(Now&& now) noexcept {
        static_assert(noexcept(now()), "The lease clock must be a cheap noexcept scalar callback");
        auto expected = Phase::kIdle;
        if (!phase_.compare_exchange_strong(expected, Phase::kProbe,
                std::memory_order_acq_rel)) {
            return expected == Phase::kProbe || expected == Phase::kPreparation ||
                expected == Phase::kArming ? ProbeDetailed::kBusy : ProbeDetailed::kStopped;
        }
        const std::uint64_t now_ms = now();
        if (!within_lifetime(now_ms)) return ProbeDetailed::kTerminal;
        const auto probes = probes_.load(std::memory_order_relaxed);
        if (probes == kMaximumProbes) { stop(Reason::kProbeLimit); return ProbeDetailed::kTerminal; }
        if (probes != 0 && now_ms - last_probe_ms_ < kProbeIntervalMs) {
            phase_.store(Phase::kIdle, std::memory_order_release);
            return ProbeDetailed::kThrottled;
        }
        last_probe_ms_ = now_ms;
        probes_.store(probes + 1, std::memory_order_relaxed);
        return ProbeDetailed::kGranted;
    }

    // Value-only compatibility/test API: supplied timestamps retain their
    // original semantics. Concurrent production callers use *_clock instead.
    [[nodiscard]] ProbeDetailed begin_probe_detailed(std::uint64_t now_ms) noexcept {
        return begin_probe_clock([now_ms]() noexcept { return now_ms; });
    }

    [[nodiscard]] Probe begin_probe(std::uint64_t now_ms) noexcept {
        switch (begin_probe_detailed(now_ms)) {
        case ProbeDetailed::kGranted: return Probe::kGranted;
        case ProbeDetailed::kBusy: return Probe::kBusy;
        case ProbeDetailed::kThrottled: return Probe::kThrottled;
        default: return Probe::kStopped; // Historical terminal/unarmed mapping.
        }
    }

    // Granted lease-owner only, exactly like the legacy finish_probe. Decide
    // the outcome while still owning the lease: after publishing Idle another
    // invocation may stop the policy before this caller even returns. Reading
    // reason() then cannot tell whose transition it was. Invalid invocations
    // retain the historical protocol-stop behavior; they are not a poll API.
    [[nodiscard]] ProbeFinish finish_probe_detailed(std::uint64_t now_ms, bool selected) noexcept {
        if (phase_.load(std::memory_order_acquire) != Phase::kProbe) {
            stop(Reason::kProtocol); return ProbeFinish::kTerminal;
        }
        if (!within_lifetime(now_ms)) return ProbeFinish::kTerminal;
        if (selected) {
            if (attempts_.load(std::memory_order_relaxed) != 0) {
                stop(Reason::kProtocol); return ProbeFinish::kTerminal;
            }
            attempts_.store(kMaximumPreparations, std::memory_order_relaxed);
            phase_.store(Phase::kPreparation, std::memory_order_release);
            return ProbeFinish::kSelected;
        }
        if (probes_.load(std::memory_order_relaxed) == kMaximumProbes) {
            stop(Reason::kProbeLimit);
            return ProbeFinish::kTerminal;
        }
        const auto result = ProbeFinish::kReleased;
        phase_.store(Phase::kIdle, std::memory_order_release);
        return result;
    }

    [[nodiscard]] bool finish_probe(std::uint64_t now_ms, bool selected) noexcept {
        return finish_probe_detailed(now_ms, selected) == ProbeFinish::kSelected;
    }

    // A worker can expire an idle policy when no game callback arrives. The
    // exact same CAS lease protects all non-atomic clocks. Never abort a Probe,
    // Preparation or Arming owned by another invocation, even past expiry.
    // This is not a probe: it changes neither counters nor the probe throttle.
    template<class Now>
    [[nodiscard]] ExpiryPoll poll_expiry_clock(Now&& now) noexcept {
        static_assert(noexcept(now()), "The lease clock must be a cheap noexcept scalar callback");
        auto expected = Phase::kIdle;
        if (!phase_.compare_exchange_strong(expected, Phase::kProbe,
                std::memory_order_acq_rel)) {
            return expected == Phase::kProbe || expected == Phase::kPreparation ||
                expected == Phase::kArming ? ExpiryPoll::kBusy : ExpiryPoll::kStopped;
        }
        const std::uint64_t now_ms = now();
        if (!within_lifetime(now_ms)) return ExpiryPoll::kTerminal;
        const auto result = ExpiryPoll::kIdle;
        phase_.store(Phase::kIdle, std::memory_order_release);
        return result;
    }

    [[nodiscard]] ExpiryPoll poll_expiry(std::uint64_t now_ms) noexcept {
        return poll_expiry_clock([now_ms]() noexcept { return now_ms; });
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

    // Dedicated post-native observer: the caller charges ONLY mod work into
    // separate intervals. Never present a stock-inclusive wall interval as
    // overhead or fabricate timestamps from accumulated ticks. This method
    // also checks expiry after the native call and before owned publication.
    // The budget limit must be the original immutable arm-time limit.
    Reason finish_observation(std::uint64_t now_ms,
        const p99_group_budget::Budget& overhead, bool captured) noexcept {
        if (phase_.load(std::memory_order_acquire) != Phase::kPreparation)
            return stop(Reason::kProtocol);
        if (!within_lifetime(now_ms)) return reason();
        if (overhead.limit != limit_ticks_) return stop(Reason::kProtocol);
        if (overhead.failed || overhead.spent > limit_ticks_)
            return stop(Reason::kBudget);
        return stop(captured ? Reason::kSuccess : Reason::kRefused);
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
    // Written before publication by arm, or exclusively under the shared CAS
    // lease (a granted probe or one lexical poll_expiry invocation).
    std::uint64_t install_ms_ = 0, last_probe_ms_ = 0, last_observed_ms_ = 0, limit_ticks_ = 0;
};
} // namespace coop::p99_source_observation
