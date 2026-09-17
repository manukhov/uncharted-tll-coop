#include "nadine_p99_group_entry.h"
#include <array>
#include <iostream>

int main() {
    namespace entry = coop::p99_group_entry;
    unsigned checks = 0, failures = 0;
    const auto check = [&](bool value) { ++checks; if (!value) ++failures; };
    constexpr entry::Identity local{0x123456780000ull, 0xA1234567};
    const std::array<entry::Identity, 8> identities{{local, {},
        {0, local.generation}, {local.handle, 0},
        {local.handle + 16, local.generation}, {local.handle, local.generation + 1},
        {local.handle, local.generation ^ 0x80000000u}, {local.handle ^ 0x100000000ull, local.generation}}};
    for (const auto before : identities)
        for (const auto fg : identities)
            for (const auto after : identities)
                for (const bool eligible : {false, true})
                    for (const bool read_ok : {false, true})
                        for (const bool full_ok : {false, true}) {
                            unsigned reads = 0, validations = 0;
                            const bool expected_pair = before.handle && before.generation &&
                                before.handle == fg.handle && before.generation == fg.generation &&
                                before.handle == after.handle && before.generation == after.generation;
                            const bool result = entry::admit(eligible,
                                [&](entry::Observations& out) { ++reads; out = {before, fg, after}; return read_ok; },
                                [&] { ++validations; return full_ok; });
                            check(reads == (eligible ? 1u : 0u));
                            check(validations == (eligible && read_ok && expected_pair ? 1u : 0u));
                            check(result == (eligible && read_ok && expected_pair && full_ok));
                        }
    // Scene fanout cannot multiply page queries, source locks, or materialization
    // for foreign identities. The validation callback represents all such work.
    unsigned expensive_calls = 0;
    for (std::uintptr_t foreign = 1; foreign <= 100000; ++foreign) {
        check(!entry::admit(true,
            [&](entry::Observations& out) { out = {local, {foreign, local.generation}, local}; return true; },
            [&] { ++expensive_calls; return true; }));
    }
    check(expensive_calls == 0);
    check(entry::admit(true,
        [&](entry::Observations& out) { out = {local, local, local}; return true; },
        [&] { ++expensive_calls; return true; }));
    check(expensive_calls == 1);
    entry::Active parent{};
    entry::mark_parent(nullptr);
    check(!parent.reentered);
    entry::mark_parent(&parent);
    check(parent.reentered); // Includes source preparation, before job TLS exists.
    entry::Active following{};
    check(!following.reentered); // No cross-job poison.
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
