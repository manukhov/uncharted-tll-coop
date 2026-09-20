#include "local_appearance_observation.h"
#include <array>
#include <barrier>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
using coop::appearance::InitLogBudget;
using coop::appearance::InitOutcome;
constexpr std::array outcomes{InitOutcome::NoOutput, InitOutcome::RoleDisabled,
    InitOutcome::LocalMismatch, InitOutcome::HashSelected};
unsigned checks = 0, failures = 0;

void check(bool value, const char* description) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << description << '\n'; }
}

void serial_claims() {
    InitLogBudget budget;
    constexpr std::array names{"no-output", "role-disabled", "local-mismatch", "hash-selected"};
    for (const auto invalid : {4u, 31u, 32u, UINT32_MAX}) {
        const auto outcome = static_cast<InitOutcome>(invalid);
        check(!budget.claim(outcome), "invalid outcome rejects before a shift");
        check(std::strcmp(coop::appearance::outcome_name(outcome), "unknown") == 0,
              "invalid outcome has a bounded diagnostic name");
    }
    for (std::size_t index = 0; index < outcomes.size(); ++index) {
        check(std::strcmp(coop::appearance::outcome_name(outcomes[index]), names[index]) == 0,
              "exact init outcome name");
        check(budget.claim(outcomes[index]), "first claim independently admits each outcome");
        for (unsigned repeat = 0; repeat < 1000; ++repeat)
            check(!budget.claim(outcomes[index]), "duplicates cannot consume another log line");
    }
    for (const auto outcome : outcomes)
        check(!budget.claim(outcome), "all four outcomes remain consumed");
}

void concurrent_claims() {
    InitLogBudget budget;
    std::barrier start(8);
    std::array<std::atomic<unsigned>, outcomes.size()> claimed{};
    std::array<std::thread, 8> threads;
    for (std::size_t thread = 0; thread < threads.size(); ++thread) {
        threads[thread] = std::thread([&, thread] {
            start.arrive_and_wait();
            for (unsigned repeat = 0; repeat < 1000; ++repeat) {
                for (std::size_t offset = 0; offset < outcomes.size(); ++offset) {
                    const auto index = (thread + repeat + offset) % outcomes.size();
                    if (budget.claim(outcomes[index]))
                        claimed[index].fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();
    unsigned total = 0;
    for (std::size_t index = 0; index < outcomes.size(); ++index) {
        const auto count = claimed[index].load(std::memory_order_relaxed);
        check(count == 1, "eight concurrent callers admit exactly one line per outcome");
        check(!budget.claim(outcomes[index]), "concurrent terminal mask never rearms");
        total += count;
    }
    check(total == outcomes.size(), "maximum of four diagnostic lines across all callers");
}
} // namespace

int main() {
    serial_claims();
    concurrent_claims();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
