#include "nadine_p99_source_observation.h"
#include <array>
#include <barrier>
#include <cstring>
#include <iostream>
#include <thread>

namespace o = coop::p99_source_observation;
namespace {
unsigned checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}

void waiting_and_limits() {
    o::Policy policy;
    check(policy.begin_probe(0) == o::Probe::kStopped, "unarmed cannot probe");
    check(policy.arm(0, 10000000) && !policy.arm(0, 10000000), "arm exactly once");
    for (std::uint32_t index = 0; index < o::kMaximumProbes; ++index) {
        const auto now = index * o::kProbeIntervalMs;
        check(policy.begin_probe(now) == o::Probe::kGranted, "rate-limited probe admitted");
        check(policy.begin_probe(now) == o::Probe::kBusy, "same-thread reentry never resamples");
        check(!policy.finish_probe(now, false), "non-P99 selection consumes no preparation");
        check(policy.probes() == index + 1 && policy.attempts() == 0, "exact probe accounting");
        if (index + 1 != o::kMaximumProbes) {
            check(policy.begin_probe(now + 99) == o::Probe::kThrottled, "99ms cannot query again");
            check(policy.probes() == index + 1, "throttle does not consume probe quota");
        }
    }
    check(policy.reason() == o::Reason::kProbeLimit, "600th nonselected probe permanently stops");
    check(policy.begin_probe(60000) == o::Probe::kStopped, "no 601st probe");
    check(!policy.arm(300000, 10000000) && policy.reason() == o::Reason::kProbeLimit,
          "late signal cannot restart a probe-limit terminal policy");
    policy.abandon();
    check(policy.reason() == o::Reason::kProbeLimit, "finally preserves original terminal reason");

    o::Policy last_selected;
    check(last_selected.arm(0, 10000000), "last-probe arm");
    for (std::uint32_t index = 0; index < o::kMaximumProbes; ++index) {
        const auto now = index * o::kProbeIntervalMs;
        check(last_selected.begin_probe(now) == o::Probe::kGranted, "last-probe waiting");
        check(last_selected.finish_probe(now, index + 1 == o::kMaximumProbes) ==
            (index + 1 == o::kMaximumProbes), "600th selected probe may reserve sole attempt");
    }
    check(last_selected.finish_preparation(0, 1, true, true) == o::Reason::kSuccess &&
        last_selected.attempts() == 1, "last probe success remains one-shot");
}

// Models only scalar policy publication after an external wait. No named event,
// wall-clock wait, process, hook, native callback or game memory is involved.
void late_arm_wait_does_not_consume_lifetime() {
    constexpr std::uint64_t installed_at = 1000;
    constexpr std::uint64_t armed_at = installed_at + 300000;
    o::Policy policy;
    for (const auto waiting_now : {installed_at, installed_at + o::kLifetimeMs,
            armed_at, installed_at + 1}) {
        check(policy.begin_probe(waiting_now) == o::Probe::kStopped,
              "unarmed calls stay stock-only even beyond the observation lifetime");
        check(policy.reason() == o::Reason::kNone && policy.probes() == 0 && policy.attempts() == 0,
              "unarmed stopped result is not a terminal transition or consumed quota");
    }
    check(policy.arm(armed_at, 10000000), "first explicit arm succeeds after the full wait");
    check(!policy.arm(armed_at + 1000000, 1000000000),
          "second signal cannot replace the arm timestamp or frequency");
    check(policy.begin_probe(armed_at) == o::Probe::kGranted &&
          !policy.finish_probe(armed_at, false),
          "first late-armed probe is immediately available with no inherited throttle");
    check(policy.probes() == 1 && policy.attempts() == 0 && policy.reason() == o::Reason::kNone,
          "waiting consumed neither probe nor preparation budget");
    check(policy.begin_probe(armed_at + o::kLifetimeMs - 1) == o::Probe::kGranted &&
          !policy.finish_probe(armed_at + o::kLifetimeMs - 1, false),
          "full observation lifetime is measured from successful arm, not installation");
    check(policy.begin_probe(armed_at + o::kLifetimeMs) == o::Probe::kStopped &&
          policy.reason() == o::Reason::kExpired && policy.probes() == 2 && policy.attempts() == 0,
          "exact arm-relative deadline stops before a third probe");
    check(!policy.arm(armed_at + o::kLifetimeMs + 1, 10000000) &&
          policy.reason() == o::Reason::kExpired,
          "late signal cannot restart an expired observation");

    o::Policy immutable_budget;
    check(immutable_budget.arm(armed_at, 10000000) &&
          !immutable_budget.arm(armed_at + 1, 1000000000),
          "failed repeated arm cannot enlarge the original one-millisecond budget");
    check(immutable_budget.begin_probe(armed_at) == o::Probe::kGranted &&
          immutable_budget.finish_probe(armed_at, true), "late arm reserves only one preparation");
    check(immutable_budget.finish_preparation(0, 10001, true, true) == o::Reason::kBudget &&
          immutable_budget.attempts() == 1,
          "late arm and rejected duplicate signal preserve the original 10000-tick limit");
}

void preparation_budget_and_one_shot() {
    for (const bool prepared : {false, true}) {
        for (const auto elapsed : {0ull, 9999ull, 10000ull, 10001ull}) {
            o::Policy policy;
            check(policy.arm(123, 10000000), "valid QPC frequency");
            check(policy.begin_probe(123) == o::Probe::kGranted, "first probe immediate");
            check(policy.finish_probe(123, true), "one full preparation reserved");
            check(policy.attempts() == 1, "attempt consumed before native work");
            check(policy.begin_probe(223) == o::Probe::kBusy, "in-flight preparation serializes other probes");
            const auto expected = elapsed > 10000 ? o::Reason::kBudget :
                prepared ? o::Reason::kSuccess : o::Reason::kRefused;
            check(policy.finish_preparation(77, 77 + elapsed, true, prepared) == expected,
                  "immutable combined 1ms budget, including exact boundary");
            check(policy.begin_probe(323) == o::Probe::kStopped, "success and refusal are both one-shot");
            check(!policy.arm(323, 10000000), "terminal policy cannot be rearmed");
            check(policy.probes() == 1 && policy.attempts() == 1, "no hidden retry");
        }
    }
}

void deadline_clock_and_abort() {
    for (const auto install : {0ull, 999ull, UINT64_MAX - o::kLifetimeMs}) {
        o::Policy policy;
        check(policy.arm(install, 10000000), "deadline arm including high uint64");
        check(policy.begin_probe(install + o::kLifetimeMs - 1) == o::Probe::kGranted,
              "probe before deadline allowed");
        check(!policy.finish_probe(install + o::kLifetimeMs, true), "expiry blocks selected preparation");
        check(policy.reason() == o::Reason::kExpired && policy.attempts() == 0,
              "no overflow-prone deadline addition");
        check(!policy.arm(install + o::kLifetimeMs, 10000000),
              "expired selected probe cannot be armed by a later signal");
    }
    for (const auto bad_frequency : {0ull, 999ull}) {
        o::Policy policy;
        check(!policy.arm(100, bad_frequency) && policy.reason() == o::Reason::kClock,
              "invalid immutable budget cannot arm");
        check(!policy.arm(300000, 10000000) && policy.reason() == o::Reason::kClock,
              "failed first arm is terminal even when a later frequency is valid");
    }
    o::Policy backwards;
    check(backwards.arm(100, 10000000), "clock arm");
    check(backwards.begin_probe(99) == o::Probe::kStopped && backwards.reason() == o::Reason::kClock,
          "backward install clock rejects before probe");
    o::Policy later_backwards;
    check(later_backwards.arm(0, 10000000) && later_backwards.begin_probe(100) == o::Probe::kGranted,
          "later clock arm");
    check(!later_backwards.finish_probe(200, false), "observe finishing monotonic time");
    check(later_backwards.begin_probe(199) == o::Probe::kStopped &&
        later_backwards.reason() == o::Reason::kClock, "clock cannot regress behind prior finish");
    for (const bool valid : {false, true}) {
        o::Policy policy;
        check(policy.arm(0, 10000000), "QPC failure arm");
        check(policy.begin_probe(0) == o::Probe::kGranted && policy.finish_probe(0, true), "QPC failure selected");
        check(policy.finish_preparation(2, 1, valid, true) == o::Reason::kClock,
              "invalid/reversed QPC stops without retry");
    }
    for (const auto reason : {o::Reason::kBudget, o::Reason::kClock, o::Reason::kException}) {
        o::Policy policy;
        check(policy.arm(0, 10000000), "abort arm");
        check(policy.begin_probe(0) == o::Probe::kGranted, "abort owns probe");
        check(policy.abort(reason) == reason && policy.attempts() == 0,
              "failed/expensive preselect stops before materialization");
        check(policy.begin_probe(100) == o::Probe::kStopped, "aborted probe stays closed");
        check(!policy.arm(300000, 10000000) && policy.reason() == reason &&
              policy.probes() == 1 && policy.attempts() == 0,
              "late signal cannot rearm or overwrite an aborted preselection");
        check(std::strcmp(o::reason_name(reason), "unknown") != 0, "static human reason available");
    }
    o::Policy misuse;
    check(misuse.arm(0, 10000000) && !misuse.finish_probe(0, true) &&
        misuse.reason() == o::Reason::kProtocol, "cannot prepare without granted probe");
}

void split_observation_completion() {
    // Stock execution is deliberately absent from the measured intervals.
    // The independent wall deadline still applies after that execution.
    for (const bool captured : {false, true}) {
        for (const auto ticks : {0ull, 9999ull, 10000ull, 10001ull}) {
            o::Policy policy;
            check(policy.arm(100, 10000000) &&
                policy.begin_probe(100) == o::Probe::kGranted &&
                policy.finish_probe(101, true), "split observation selected once");
            coop::p99_group_budget::Budget cost{10000};
            check(cost.charge(11, 11), "empty overhead interval allowed");
            (void)cost.charge(1000000, 1000000 + ticks); // Not elapsed stock time.
            const auto expected = ticks > 10000 ? o::Reason::kBudget :
                captured ? o::Reason::kSuccess : o::Reason::kRefused;
            check(policy.finish_observation(119999, cost, captured) == expected,
                "sum overhead not wall interval, exact one-millisecond bound");
            check(policy.begin_probe(120000) == o::Probe::kStopped && policy.attempts() == 1,
                "split success and refusal terminal without retry");
        }
    }
    for (const auto now : {100ull, 120100ull}) {
        o::Policy policy;
        check(policy.arm(100, 10000000) &&
            policy.begin_probe(101) == o::Probe::kGranted && policy.finish_probe(102, true),
            "split deadline fixture selected");
        coop::p99_group_budget::Budget cost{10000};
        check(policy.finish_observation(now, cost, true) ==
            (now == 100 ? o::Reason::kClock : o::Reason::kExpired),
            "backward clock or expired native return cannot publish");
    }
    for (const auto limit : {0ull, 9999ull, 10001ull, UINT64_MAX}) {
        o::Policy policy;
        check(policy.arm(0, 10000000) && policy.begin_probe(0) == o::Probe::kGranted &&
            policy.finish_probe(0, true), "split immutable limit fixture selected");
        check(policy.finish_observation(0, {limit}, true) == o::Reason::kProtocol,
            "caller cannot replace arm-time overhead limit");
    }
    o::Policy protocol;
    check(protocol.arm(0, 10000000) &&
        protocol.finish_observation(0, {10000}, true) == o::Reason::kProtocol,
        "no split completion without a selected lease");
    o::Policy aborted;
    check(aborted.arm(0, 10000000) && aborted.begin_probe(0) == o::Probe::kGranted &&
        aborted.finish_probe(0, true), "aborted split fixture selected");
    check(aborted.abort(o::Reason::kException) == o::Reason::kException &&
        aborted.finish_observation(0, {10000}, true) == o::Reason::kException,
        "late split completion cannot overwrite a stock fault");
}

void concurrent_first_arm_after_unarmed_calls() {
    o::Policy policy;
    constexpr std::uint64_t armed_at = 300000;
    std::barrier all_unarmed(8);
    std::array<o::Probe, 8> before{};
    std::array<bool, 8> armed{};
    std::array<std::thread, 8> threads;
    for (std::size_t index = 0; index < threads.size(); ++index) {
        threads[index] = std::thread([&, index] {
            before[index] = policy.begin_probe(armed_at);
            all_unarmed.arrive_and_wait();
            armed[index] = policy.arm(armed_at, 10000000);
        });
    }
    for (auto& thread : threads) thread.join();
    unsigned winners = 0;
    for (std::size_t index = 0; index < threads.size(); ++index) {
        check(before[index] == o::Probe::kStopped,
              "all concurrent pre-signal candidates are stock-only");
        winners += armed[index] ? 1u : 0u;
    }
    check(winners == 1 && policy.reason() == o::Reason::kNone &&
          policy.probes() == 0 && policy.attempts() == 0,
          "competing signals publish exactly one first arm without probing");
    check(policy.begin_probe(armed_at) == o::Probe::kGranted &&
          !policy.finish_probe(armed_at, false) && policy.reason() == o::Reason::kNone,
          "winning first arm publishes a usable policy after concurrent unarmed calls");
}

void concurrent_nonspinning_admission() {
    o::Policy policy;
    check(policy.arm(0, 10000000), "concurrency arm");
    std::barrier start(8), all_begun(8);
    std::array<o::Probe, 8> results{};
    std::array<std::thread, 8> threads;
    for (std::size_t index = 0; index < threads.size(); ++index) {
        threads[index] = std::thread([&, index] {
            start.arrive_and_wait();
            results[index] = policy.begin_probe(0);
            all_begun.arrive_and_wait();
            if (results[index] == o::Probe::kGranted) (void)policy.finish_probe(0, false);
        });
    }
    for (auto& thread : threads) thread.join();
    unsigned granted = 0, busy = 0;
    for (const auto result : results) {
        granted += result == o::Probe::kGranted;
        busy += result == o::Probe::kBusy;
    }
    check(granted == 1 && busy == 7 && policy.probes() == 1 && policy.attempts() == 0,
          "eight simultaneous candidates perform exactly one preselect");
}

void detailed_transition_ownership() {
    o::Policy unarmed;
    check(unarmed.begin_probe_detailed(0)==o::ProbeDetailed::kStopped &&
        unarmed.poll_expiry(UINT64_MAX)==o::ExpiryPoll::kStopped &&
        unarmed.reason()==o::Reason::kNone,"unarmed stopped does not own terminal reporting");
    for(bool clock:{false,true}) {
        o::Policy p; check(p.arm(100,10000000),"detailed terminal arm");
        check(p.begin_probe_detailed(clock?99:100+o::kLifetimeMs)==o::ProbeDetailed::kTerminal,
            "successful CAS distinguishes its own expiry/clock terminal");
        check(p.reason()==(clock?o::Reason::kClock:o::Reason::kExpired) && p.probes()==0 && p.attempts()==0,
            "terminal before selection consumes no quota");
        check(p.begin_probe_detailed(200000)==o::ProbeDetailed::kStopped &&
            p.poll_expiry(200000)==o::ExpiryPoll::kStopped &&
            p.begin_probe(200000)==o::Probe::kStopped,"later observers cannot claim somebody else's terminal");
    }
    o::Policy p; check(p.arm(0,10000000),"detailed released arm");
    check(p.begin_probe_detailed(0)==o::ProbeDetailed::kGranted &&
        p.begin_probe_detailed(0)==o::ProbeDetailed::kBusy,"detailed begin owns exactly one lease");
    check(p.finish_probe_detailed(0,false)==o::ProbeFinish::kReleased,"ordinary nonselection explicitly releases");
    check(p.begin_probe_detailed(99)==o::ProbeDetailed::kThrottled,"detailed throttle unchanged");
    check(p.begin_probe_detailed(100)==o::ProbeDetailed::kGranted &&
        p.finish_probe_detailed(100,true)==o::ProbeFinish::kSelected && p.attempts()==1,
        "selected detailed finish retains sole preparation lease");
    check(p.poll_expiry(o::kLifetimeMs)==o::ExpiryPoll::kBusy && p.reason()==o::Reason::kNone,
        "worker cannot expire another preparation even at deadline");
    check(p.finish_observation(o::kLifetimeMs,{10000},true)==o::Reason::kExpired,
        "actual preparation owner handles expiry");

    for(bool selected:{false,true}) {
        o::Policy ended; check(ended.arm(100,10000000),"detailed finish expiry arm");
        check(ended.begin_probe_detailed(100)==o::ProbeDetailed::kGranted &&
            ended.finish_probe_detailed(100+o::kLifetimeMs,selected)==o::ProbeFinish::kTerminal,
            "finish owns expiry before release or preparation");
        check(ended.attempts()==0 && ended.reason()==o::Reason::kExpired,"expired finish consumes no preparation");
        o::Policy backwards; check(backwards.arm(100,10000000),"detailed finish clock arm");
        check(backwards.begin_probe_detailed(101)==o::ProbeDetailed::kGranted &&
            backwards.finish_probe_detailed(100,selected)==o::ProbeFinish::kTerminal &&
            backwards.reason()==o::Reason::kClock,"finish clock error remains own terminal");
    }
    o::Policy limited; check(limited.arm(0,10000000),"detailed quota arm");
    for(std::uint32_t index=0;index<o::kMaximumProbes;++index) {
        const auto now=index*o::kProbeIntervalMs;
        check(limited.begin_probe_detailed(now)==o::ProbeDetailed::kGranted,"detailed quota begin");
        check(limited.finish_probe_detailed(now,false)==(index+1==o::kMaximumProbes?
            o::ProbeFinish::kTerminal:o::ProbeFinish::kReleased),"600th finish owns quota terminal");
    }
    check(limited.reason()==o::Reason::kProbeLimit&&limited.poll_expiry(60000)==o::ExpiryPoll::kStopped,
        "worker cannot claim the last probe's limit result");
    o::Policy misuse; check(misuse.arm(0,10000000),"detailed misuse arm");
    check(misuse.finish_probe_detailed(0,true)==o::ProbeFinish::kTerminal &&
        misuse.reason()==o::Reason::kProtocol,"invalid finish keeps legacy protocol-stop (not observer authority)");
}

void idle_expiry_without_callbacks() {
    for(const auto origin:{0ull,999ull,UINT64_MAX-o::kLifetimeMs}) {
        o::Policy p; check(p.arm(origin,10000000),"poll deadline arm");
        check(p.poll_expiry(origin)==o::ExpiryPoll::kIdle &&
            p.poll_expiry(origin+o::kLifetimeMs-1)==o::ExpiryPoll::kIdle,
            "valid worker poll returns lease to idle without callback");
        check(p.probes()==0&&p.attempts()==0&&p.reason()==o::Reason::kNone,"poll not counted as probe/preparation");
        check(p.poll_expiry(origin+o::kLifetimeMs)==o::ExpiryPoll::kTerminal &&
            p.reason()==o::Reason::kExpired,"worker owns exact idle expiry including high uint64");
        check(p.poll_expiry(origin+o::kLifetimeMs)==o::ExpiryPoll::kStopped &&
            p.probes()==0&&p.attempts()==0,"repeated worker cannot duplicate owned report");
    }
    o::Policy clock; check(clock.arm(100,10000000),"poll clock arm");
    check(clock.poll_expiry(200)==o::ExpiryPoll::kIdle && clock.poll_expiry(199)==o::ExpiryPoll::kTerminal &&
        clock.reason()==o::Reason::kClock,"worker checks monotonic lifetime under own lease");
    o::Policy shared_clock; check(shared_clock.arm(100,10000000),"shared monotonic clock arm");
    check(shared_clock.poll_expiry(200)==o::ExpiryPoll::kIdle &&
        shared_clock.begin_probe_detailed(199)==o::ProbeDetailed::kTerminal &&
        shared_clock.reason()==o::Reason::kClock,"later probe respects time already observed by worker");
    o::Policy throttle; check(throttle.arm(0,10000000),"poll throttle arm");
    check(throttle.poll_expiry(10)==o::ExpiryPoll::kIdle &&
        throttle.begin_probe_detailed(10)==o::ProbeDetailed::kGranted &&
        throttle.finish_probe_detailed(10,false)==o::ProbeFinish::kReleased,"poll does not delay first actual probe");
    for(std::uint64_t now=11;now<110;++now)
        check(throttle.poll_expiry(now)==o::ExpiryPoll::kIdle,"poll independent of probe interval");
    check(throttle.begin_probe_detailed(109)==o::ProbeDetailed::kThrottled &&
        throttle.begin_probe_detailed(110)==o::ProbeDetailed::kGranted &&
        throttle.probes()==2 && throttle.attempts()==0,"many polls neither consume quota nor reset throttle");
    check(throttle.poll_expiry(0)==o::ExpiryPoll::kBusy &&
        throttle.poll_expiry(UINT64_MAX)==o::ExpiryPoll::kBusy &&
        throttle.reason()==o::Reason::kNone,"busy poll cannot stop foreign probe on clock or expiry");
    check(throttle.finish_probe_detailed(110,false)==o::ProbeFinish::kReleased,"busy polls leave foreign clock intact");
}

void worker_probe_and_release_races() {
    // All contenders are at the deadline. Exactly ONE successful Idle->Probe
    // CAS owns the terminal result; losers return Busy or Stopped, never infer
    // ownership by observing the winner's shared terminal reason.
    for(unsigned run=0;run<32;++run) {
        o::Policy p; check(p.arm(0,10000000),"worker/callback terminal race arm");
        std::barrier start(8); std::array<std::thread,8> threads;
        std::array<o::ProbeDetailed,4> probes{}; std::array<o::ExpiryPoll,4> polls{};
        for(std::size_t i=0;i<threads.size();++i) threads[i]=std::thread([&,i] {
            start.arrive_and_wait();
            if(i<4) probes[i]=p.begin_probe_detailed(o::kLifetimeMs);
            else polls[i-4]=p.poll_expiry(o::kLifetimeMs);
        });
        for(auto&thread:threads) thread.join();
        unsigned owners=0;
        for(auto result:probes) {
            owners+=result==o::ProbeDetailed::kTerminal;
            check(result==o::ProbeDetailed::kTerminal||result==o::ProbeDetailed::kBusy||
                result==o::ProbeDetailed::kStopped,"callback race result distinguishes owned terminal");
        }
        for(auto result:polls) {
            owners+=result==o::ExpiryPoll::kTerminal;
            check(result==o::ExpiryPoll::kTerminal||result==o::ExpiryPoll::kBusy||
                result==o::ExpiryPoll::kStopped,"worker race result distinguishes owned terminal");
        }
        check(owners==1&&p.reason()==o::Reason::kExpired&&p.probes()==0&&p.attempts()==0,
            "exactly one reporting owner and no selection at deadline");
    }
    // The worker can win immediately AFTER the callback releases. The callback
    // must still return Released, even if the global reason is terminal before
    // the calling code resumes. No reason() read can reconstruct this outcome.
    for(unsigned run=0;run<64;++run) {
        o::Policy p; check(p.arm(0,10000000)&&p.begin_probe(0)==o::Probe::kGranted,"finish race owns original probe");
        std::barrier start(2); o::ProbeFinish finished{}; o::ExpiryPoll polled{};
        std::thread callback([&]{start.arrive_and_wait();finished=p.finish_probe_detailed(0,false);});
        std::thread worker([&]{start.arrive_and_wait();polled=p.poll_expiry(o::kLifetimeMs);});
        callback.join(); worker.join();
        check(finished==o::ProbeFinish::kReleased &&
            (polled==o::ExpiryPoll::kBusy||polled==o::ExpiryPoll::kTerminal),"finish outcome fixed before worker can acquire released lease");
        if(polled==o::ExpiryPoll::kBusy) polled=p.poll_expiry(o::kLifetimeMs);
        check(polled==o::ExpiryPoll::kTerminal&&p.reason()==o::Reason::kExpired&&
            finished==o::ProbeFinish::kReleased&&p.probes()==1&&p.attempts()==0,
            "later worker-owned terminal cannot be attributed to earlier released probe");
    }
    for(unsigned run=0;run<32;++run) {
        o::Policy p; check(p.arm(0,10000000)&&p.begin_probe(0)==o::Probe::kGranted,"selection race owns probe");
        std::barrier start(2); o::ProbeFinish finished{}; o::ExpiryPoll polled{};
        std::thread callback([&]{start.arrive_and_wait();finished=p.finish_probe_detailed(0,true);});
        std::thread worker([&]{start.arrive_and_wait();polled=p.poll_expiry(UINT64_MAX);});
        callback.join(); worker.join();
        check(finished==o::ProbeFinish::kSelected&&polled==o::ExpiryPoll::kBusy&&p.attempts()==1&&
            p.reason()==o::Reason::kNone,"Probe->Preparation never exposes an idle gap to worker");
        check(p.finish_preparation(0,0,true,true)==o::Reason::kSuccess,"selected owner still completes after racing expired worker");
    }
    for(unsigned run=0;run<32;++run) {
        o::Policy p; std::barrier start(2); bool armed=false; o::ExpiryPoll polled{};
        std::thread armer([&]{start.arrive_and_wait();armed=p.arm(100,10000000);});
        std::thread worker([&]{start.arrive_and_wait();polled=p.poll_expiry(100);});
        armer.join(); worker.join();
        check(armed&&p.reason()==o::Reason::kNone&&p.probes()==0&&p.attempts()==0,
            "worker racing arm never mutates another arming lease");
        check(polled==o::ExpiryPoll::kStopped||polled==o::ExpiryPoll::kBusy||polled==o::ExpiryPoll::kIdle,
            "arming race has no worker-owned terminal");
        check(p.begin_probe_detailed(100)==o::ProbeDetailed::kGranted&&
            p.finish_probe_detailed(100,false)==o::ProbeFinish::kReleased,"concurrent arm publishes intact usable state");
    }
}

void clocks_sample_only_under_owned_lease() {
    o::Policy p; unsigned samples=0;
    const auto clock=[&]() noexcept {++samples;return std::uint64_t{1000};};
    check(p.begin_probe_clock(clock)==o::ProbeDetailed::kStopped&&
        p.poll_expiry_clock(clock)==o::ExpiryPoll::kStopped&&samples==0,
        "unarmed clock APIs do not sample time or claim a terminal");
    check(p.arm(1000,10000000)&&p.begin_probe_clock(clock)==o::ProbeDetailed::kGranted&&samples==1,
        "successful probe samples clock exactly once");
    check(p.begin_probe_clock(clock)==o::ProbeDetailed::kBusy&&
        p.poll_expiry_clock(clock)==o::ExpiryPoll::kBusy&&samples==1,
        "foreign probe lease suppresses both clock callbacks");
    check(p.finish_probe_detailed(1000,true)==o::ProbeFinish::kSelected,
        "clock-based probe can retain selected lease");
    check(p.begin_probe_clock(clock)==o::ProbeDetailed::kBusy&&
        p.poll_expiry_clock(clock)==o::ExpiryPoll::kBusy&&samples==1,
        "foreign preparation suppresses both clock callbacks");
    check(p.abort(o::Reason::kRefused)==o::Reason::kRefused,"actual owner terminates selected lease");
    check(p.begin_probe_clock(clock)==o::ProbeDetailed::kStopped&&
        p.poll_expiry_clock(clock)==o::ExpiryPoll::kStopped&&samples==1,
        "already stopped clock APIs never resample or own another report");

    o::Policy throttled; check(throttled.arm(0,10000000)&&
        throttled.begin_probe(0)==o::Probe::kGranted&&!throttled.finish_probe(0,false),"clock throttle setup");
    samples=0;
    const auto soon=[&]() noexcept {++samples;return std::uint64_t{99};};
    check(throttled.begin_probe_clock(soon)==o::ProbeDetailed::kThrottled&&samples==1,
        "owned throttle decision still samples only once");
    check(throttled.poll_expiry_clock(soon)==o::ExpiryPoll::kIdle&&samples==2&&
        throttled.probes()==1&&throttled.attempts()==0,"worker clock sample does not consume probe quota");
}

void preempted_clock_caller_resamples_after_acquiring() {
    for(bool delayed_probe:{false,true}) {
        // Reproduce the dangerous schedule without sleeping: a would-be worker
        // or probe has observed1001, then a callback fully advances the policy
        // through1002..1003. Only after that release may the delayed caller try
        // its CAS. Its clock callback must observe1004 under the NEW lease.
        o::Policy p; check(p.arm(1000,10000000),"delayed clock caller arm");
        std::atomic<std::uint64_t> clock{1001};
        std::barrier preempted(2), prior_callback_finished(2);
        std::uint64_t stale=0,sampled=0; unsigned samples=0;
        o::ProbeDetailed probe{},nested{}; o::ExpiryPoll poll{};
        std::thread delayed([&] {
            stale=clock.load(); // Explicitly NOT passed to the new API.
            preempted.arrive_and_wait();
            prior_callback_finished.arrive_and_wait();
            const auto read_clock=[&]() noexcept {
                ++samples;
                // Test-only scalar reentry proves the outer CAS already owns
                // Probe when this callback is invoked. It must be harmless Busy.
                nested=p.begin_probe_detailed(1004);
                sampled=clock.load(); return sampled;
            };
            if(delayed_probe) probe=p.begin_probe_clock(read_clock);
            else poll=p.poll_expiry_clock(read_clock);
        });
        preempted.arrive_and_wait();
        clock.store(1002);
        check(p.begin_probe_clock([&]() noexcept {return clock.load();})==o::ProbeDetailed::kGranted,
            "intervening callback acquires and samples1002");
        clock.store(1003);
        check(p.finish_probe_detailed(clock.load(),false)==o::ProbeFinish::kReleased,
            "intervening callback releases at1003");
        clock.store(1004); prior_callback_finished.arrive_and_wait(); delayed.join();
        check(stale==1001&&sampled==1004&&samples==1&&nested==o::ProbeDetailed::kBusy,
            "delayed caller samples fresh time once, only after successful lease CAS");
        check((delayed_probe?probe==o::ProbeDetailed::kThrottled:poll==o::ExpiryPoll::kIdle)&&
            p.reason()==o::Reason::kNone&&p.probes()==1&&p.attempts()==0,
            "scheduler delay is not a false clock error or a consumed probe");
        clock.store(1102);
        check(p.begin_probe_clock([&]() noexcept {return clock.load();})==o::ProbeDetailed::kGranted&&
            p.finish_probe_detailed(1102,false)==o::ProbeFinish::kReleased,
            "subsequent actual probe remains usable with original throttle");

        // Compatibility methods intentionally still treat a supplied stale
        // number as regression. Production must call the new sampling API.
        o::Policy numeric; check(numeric.arm(1000,10000000)&&
            numeric.begin_probe(1002)==o::Probe::kGranted&&!numeric.finish_probe(1003,false),
            "numeric compatibility schedule setup");
        check((delayed_probe?numeric.begin_probe_detailed(stale)==o::ProbeDetailed::kTerminal:
            numeric.poll_expiry(stale)==o::ExpiryPoll::kTerminal)&&numeric.reason()==o::Reason::kClock,
            "numeric compatibility clock semantics unchanged");
    }
}

void true_clock_fault_and_terminal_reporting() {
    for(bool probe:{false,true}) {
        for(bool regression:{false,true}) {
            o::Policy p; check(p.arm(1000,10000000)&&
                p.poll_expiry_clock([]() noexcept {return std::uint64_t{1003};})==o::ExpiryPoll::kIdle,
                "clock-based terminal fixture observes valid time");
            unsigned samples=0;
            const auto clock=[&]() noexcept {++samples;return regression?std::uint64_t{1002}:1000+o::kLifetimeMs;};
            check(probe?p.begin_probe_clock(clock)==o::ProbeDetailed::kTerminal:
                p.poll_expiry_clock(clock)==o::ExpiryPoll::kTerminal,"clock-sampling CAS owner alone gets terminal result");
            check(samples==1&&p.reason()==(regression?o::Reason::kClock:o::Reason::kExpired)&&
                p.probes()==0&&p.attempts()==0,"true regression/expiry still stops before counters change");
            check(p.begin_probe_clock(clock)==o::ProbeDetailed::kStopped&&
                p.poll_expiry_clock(clock)==o::ExpiryPoll::kStopped&&samples==1,
                "later terminal observers neither sample clock nor acquire reporting ownership");
        }
    }
    for(unsigned run=0;run<32;++run) {
        o::Policy p; check(p.arm(0,10000000),"clock terminal race arm");
        std::atomic<unsigned> samples{0}; std::barrier start(2);
        o::ProbeDetailed probe{}; o::ExpiryPoll poll{};
        const auto clock=[&]() noexcept {samples.fetch_add(1);return o::kLifetimeMs;};
        std::thread callback([&]{start.arrive_and_wait();probe=p.begin_probe_clock(clock);});
        std::thread worker([&]{start.arrive_and_wait();poll=p.poll_expiry_clock(clock);});
        callback.join(); worker.join();
        const auto owners=static_cast<unsigned>(probe==o::ProbeDetailed::kTerminal)+
            static_cast<unsigned>(poll==o::ExpiryPoll::kTerminal);
        check(owners==1&&samples.load()==1&&p.reason()==o::Reason::kExpired,
            "racing worker/callback have one clock sample and one terminal reporting owner");
        check((probe==o::ProbeDetailed::kTerminal||probe==o::ProbeDetailed::kBusy||probe==o::ProbeDetailed::kStopped)&&
            (poll==o::ExpiryPoll::kTerminal||poll==o::ExpiryPoll::kBusy||poll==o::ExpiryPoll::kStopped)&&
            p.probes()==0&&p.attempts()==0,"losing clock entry never runs clock or quota logic");
    }
}

// Value-only composition model of source_observation_runtime.inl. It invokes
// the REAL policy with injected selection/preparation/stock callbacks. C++
// scope cleanup models the wrapper's __finally control flow, not Windows SEH
// or the assembly/native bridge (those have independent fixture tests).
template<class Finish> struct Finally {
    Finish finish;
    ~Finally() { finish(); }
};
struct Composition {
    o::Policy policy;
    bool ready = true, fiber_enter = true, eligible = true, scope_valid = true;
    bool clock_valid = true;
    std::uint64_t now_ms = 0, probe_ticks = 1, total_ticks = 2, native_slot = 0;
    unsigned selects = 0, preparations = 0, stocks = 0, frames_left = 0, abandoned = 0;
    int job = 7;
};
template<class Select, class Prepare, class Stock>
void compose(Composition& state, Select select, Prepare prepare, Stock stock) {
    const auto invoke_stock = [&] { ++state.stocks; stock(&state.job); };
    if (!state.ready || !state.fiber_enter) {
        state.ready = false;
        invoke_stock();
        return;
    }
    Finally frame_cleanup{[&] { state.native_slot = 0; ++state.frames_left; }};
    if (state.eligible && state.policy.begin_probe(state.now_ms) == o::Probe::kGranted) {
        bool leased = true;
        Finally lease_cleanup{[&] {
            if (leased) { state.policy.abandon(); state.ready = false; ++state.abandoned; }
        }};
        bool selected = false;
        if (state.clock_valid) { ++state.selects; selected = select(); }
        coop::p99_group_budget::Budget budget{10000};
        if (!state.clock_valid || !budget.charge(0, state.probe_ticks)) {
            (void)state.policy.abort(state.clock_valid ? o::Reason::kBudget : o::Reason::kClock);
            leased = false;
        } else if (!state.scope_valid) {
            (void)state.policy.abort(o::Reason::kProtocol);
            leased = false;
        } else if (!state.policy.finish_probe(state.now_ms, selected)) {
            leased = false;
        } else {
            ++state.preparations;
            const bool prepared = prepare();
            state.ready = false;
            (void)state.policy.finish_preparation(0, state.total_ticks,
                state.clock_valid, prepared && state.scope_valid);
            leased = false;
        }
    }
    if (state.policy.reason() != o::Reason::kNone) state.ready = false;
    // No catch around either the candidate or stock, and no retry in cleanup.
    state.native_slot = 0x1238;
    invoke_stock();
    state.native_slot = 0;
}

void composed_normal_paths() {
    enum class Path { success, refused, nonselected, busy, throttled, expired,
        unready, fiber_failed, foreign, probe_budget, probe_clock, scope_lost };
    for (const auto path : {Path::success, Path::refused, Path::nonselected,
            Path::busy, Path::throttled, Path::expired, Path::unready, Path::fiber_failed,
            Path::foreign, Path::probe_budget, Path::probe_clock, Path::scope_lost}) {
        Composition state;
        check(state.policy.arm(0, 10000000), "composition arm");
        if (path == Path::busy || path == Path::throttled) {
            check(state.policy.begin_probe(0) == o::Probe::kGranted, "outer lease fixture");
            if (path == Path::throttled) {
                check(!state.policy.finish_probe(0, false), "previous waiting probe");
                state.now_ms = 99;
            }
        }
        if (path == Path::expired) state.now_ms = o::kLifetimeMs;
        if (path == Path::unready) state.ready = false;
        if (path == Path::fiber_failed) state.fiber_enter = false;
        if (path == Path::foreign) state.eligible = false;
        if (path == Path::probe_budget) state.probe_ticks = 10001;
        if (path == Path::probe_clock) state.clock_valid = false;
        if (path == Path::scope_lost) state.scope_valid = false;
        compose(state, [&] { return path != Path::nonselected; },
            [&] { return path != Path::refused; },
            [&](int* job) { check(job == &state.job && *job == 7, "stock gets unchanged original job"); });
        const bool attempted = path == Path::success || path == Path::refused;
        check(state.stocks == 1 && state.preparations == (attempted ? 1u : 0u),
              "every ordinary outcome calls stock exactly once");
        check(state.frames_left == ((path == Path::unready || path == Path::fiber_failed) ? 0u : 1u) &&
            state.native_slot == 0 && state.abandoned == 0, "normal frame and lease cleanup");
        if (attempted) {
            check(state.policy.attempts() == 1 && state.policy.reason() ==
                (path == Path::success ? o::Reason::kSuccess : o::Reason::kRefused), "producer result terminates policy");
        }
        if (path == Path::busy) {
            check(state.selects == 0 && state.policy.reason() == o::Reason::kNone,
                  "busy invocation cannot abandon someone else's lease");
            check(!state.policy.finish_probe(100, false) && state.policy.reason() == o::Reason::kNone,
                  "actual outer lease owner still finishes normally");
        }
        if (path == Path::throttled || path == Path::expired || path == Path::foreign)
            check(state.selects == 0, "no preselect on rejected cheap admission");
        if (path == Path::probe_clock)
            check(state.selects == 0 && state.policy.reason() == o::Reason::kClock,
                  "missing initial timer never starts borrowed preselection");
        if (path == Path::probe_budget)
            check(state.policy.reason() == o::Reason::kBudget, "preselection budget stops before producer");
    }
}

void composed_exception_paths() {
    enum class Fault { selection, preparation, stock };
    for (const auto fault : {Fault::selection, Fault::preparation, Fault::stock}) {
        Composition state;
        check(state.policy.arm(0, 10000000), "exception composition arm");
        unsigned caught = 0;
        try {
            compose(state,
                [&] { if (fault == Fault::selection) throw 71; return true; },
                [&] { if (fault == Fault::preparation) throw 72; return true; },
                [&](int* job) {
                    check(job == &state.job, "exceptional stock receives original job");
                    if (fault == Fault::stock) throw 73;
                });
        } catch (int) { ++caught; }
        check(caught == 1 && state.frames_left == 1 && state.native_slot == 0,
              "exception propagates once and expires outer frame/token");
        check(state.stocks == (fault == Fault::stock ? 1u : 0u),
              "no stock retry or compensating stock call during exception unwind");
        check(state.abandoned == (fault == Fault::stock ? 0u : 1u),
              "only outstanding candidate lease is abandoned");
        check(state.policy.reason() == (fault == Fault::stock ? o::Reason::kSuccess : o::Reason::kException),
              "stock exception cannot overwrite already-completed observation reason");
        check(state.policy.begin_probe(100) == o::Probe::kStopped && !state.ready,
              "exceptional composition cannot resample source");
        check(state.policy.attempts() == (fault == Fault::selection ? 0u : 1u),
              "preparation exception still consumed its sole attempt");
    }
}
} // namespace

int main() {
    waiting_and_limits();
    late_arm_wait_does_not_consume_lifetime();
    preparation_budget_and_one_shot();
    split_observation_completion();
    deadline_clock_and_abort();
    concurrent_nonspinning_admission();
    concurrent_first_arm_after_unarmed_calls();
    detailed_transition_ownership();
    idle_expiry_without_callbacks();
    worker_probe_and_release_races();
    clocks_sample_only_under_owned_lease();
    preempted_clock_caller_resamples_after_acquiring();
    true_clock_fault_and_terminal_reporting();
    composed_normal_paths();
    composed_exception_paths();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
