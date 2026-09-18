// Guard-only regression: image address 1 deliberately cannot execute native math.
#include "nadine_p99_source_materialize.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace m = coop::p99_source_materialize;
namespace o = coop::p99_chloe_order;
struct AuditStats { unsigned size, retarget_calls, blend_calls, rejected_index; };
extern "C" __declspec(dllimport) int P99AuditRow20(std::uintptr_t, const std::byte*,
    unsigned, std::byte*, unsigned, AuditStats*);
using Input = std::array<std::byte, 21 * 94>;
unsigned checks = 0, failures = 0;
void check(bool okay) { ++checks; if (!okay) ++failures; }

Input valid_input() {
    Input input{};
    for (unsigned index = 0; index < 21; ++index) {
        auto* row = input.data() + index * 94;
        const auto& transfer = o::kExpectedTransfers[index];
        m::store<std::uint16_t>(row + 0x0C, 0x3C00); // nonzero half quaternions
        m::store<std::uint16_t>(row + 0x1A, 0x3C00);
        m::store<std::uint16_t>(row + 0x24, transfer.player_global);
        m::store<std::uint16_t>(row + 0x26, static_cast<std::uint16_t>(
            o::parameter_index_for_global(o::kPlayerGroups, transfer.player_global)));
        m::store<std::uint16_t>(row + 0x28, transfer.chloe_global);
        m::store<std::uint16_t>(row + 0x2A, static_cast<std::uint16_t>(
            o::parameter_index_for_global(o::kChloeGroups, transfer.chloe_global)));
        row[0x2C] = static_cast<std::byte>(transfer.mode);
        for (unsigned lane : {0u, 1u, 2u, 7u}) m::store(row + 46 + lane * 4, 1.0F);
    }
    return input;
}

void refused(const Input& input, unsigned bad) {
    std::array<std::byte, 21 * 96 + 64> storage;
    storage.fill(std::byte{0xA7});
    AuditStats stats{sizeof(AuditStats), 99, 99, 99};
    const auto before = input;
    check(P99AuditRow20(1, input.data(), static_cast<unsigned>(input.size()),
        storage.data() + 32, 21 * 96, &stats) == -3);
    check(stats.retarget_calls == 0 && stats.blend_calls == 0);
    check(stats.rejected_index == bad);
    check(input == before);
    check(std::all_of(storage.begin(), storage.begin() + 32,
        [](std::byte x) { return x == std::byte{0xA7}; }));
    check(std::all_of(storage.end() - 32, storage.end(),
        [](std::byte x) { return x == std::byte{0xA7}; }));
    check(std::all_of(storage.begin() + 32, storage.end() - 32,
        [](std::byte x) { return x == std::byte{0}; }));
}

int main() {
    const auto pristine = valid_input();
    for (unsigned index = 0; index < 21; ++index) {
        for (unsigned offset = 0; offset < 0x24; offset += 2) {
            for (std::uint16_t bits : std::array<std::uint16_t, 3>{0x7C00, 0xFC00, 0x7E00}) {
                auto input = pristine;
                m::store(input.data() + index * 94 + offset, bits);
                refused(input, index);
            }
        }
        for (unsigned offset : {0x06u, 0x14u}) {
            auto input = pristine;
            for (unsigned q = 0; q < 4; ++q)
                m::store<std::uint16_t>(input.data() + index * 94 + offset + q * 2, 0x8000);
            refused(input, index);
        }
        for (unsigned offset : {0x24u, 0x26u, 0x28u, 0x2Au, 0x2Cu}) {
            auto input = pristine;
            input[index * 94 + offset] ^= std::byte{0x40};
            refused(input, index);
        }
        for (unsigned lane = 0; lane < 12; ++lane) {
            if (lane == 3 || lane == 11) continue;
            auto input = pristine;
            m::store(input.data() + index * 94 + 46 + lane * 4,
                     std::numeric_limits<float>::quiet_NaN());
            refused(input, index);
        }
        auto input = pristine;
        m::store(input.data() + index * 94 + 46 + 28, 0.0F);
        refused(input, index);
    }
    // W lanes and record padding remain unowned; a later bad row is still reached.
    auto padding = pristine;
    for (unsigned index = 0; index < 20; ++index) {
        padding[index * 94 + 45] = std::byte{0xFF};
        m::store(padding.data() + index * 94 + 46 + 12, std::numeric_limits<float>::quiet_NaN());
        m::store(padding.data() + index * 94 + 46 + 44, std::numeric_limits<float>::quiet_NaN());
    }
    padding[20 * 94 + 0x2C] = std::byte{0};
    refused(padding, 20);
    // Invalid lengths/null pointers must be rejected without reading the input.
    std::array<std::byte, 2016> output{};
    for (unsigned input_size : {0u, 1973u, 1975u, ~0u}) {
        AuditStats stats{sizeof(AuditStats)};
        check(P99AuditRow20(1, reinterpret_cast<const std::byte*>(1), input_size,
              output.data(), 2016, &stats) == -2);
        check(stats.retarget_calls == 0 && stats.blend_calls == 0);
    }
    check(P99AuditRow20(1, nullptr, 1974, output.data(), 2016, nullptr) == -1);
    AuditStats stats{sizeof(AuditStats)};
    check(P99AuditRow20(1, nullptr, 1974, output.data(), 2016, &stats) == -2);
    check(P99AuditRow20(1, pristine.data(), 1974, nullptr, 2016, &stats) == -2);
    check(P99AuditRow20(0, pristine.data(), 1974, output.data(), 2016, &stats) == -2);
    std::printf("P99 first-stage admission: %u checks, %u failures; no native calls\n", checks, failures);
    return failures ? 1 : 0;
}
