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
extern "C" __declspec(dllimport) int P99AuditFullChain(std::uintptr_t, const std::byte*,
    unsigned, std::byte*, unsigned, AuditStats*);
using Input = std::array<std::byte, 21 * 94>;
using FullInput = std::array<std::byte, 21 * 188>;
constexpr unsigned kFullOutputBytes = 21 * 48 + 4 * static_cast<unsigned>(sizeof(m::Mask));
static_assert(kFullOutputBytes == 1360);
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

FullInput valid_full_input() {
    FullInput input{};
    const auto first = valid_input();
    for (unsigned index = 0; index < 21; ++index) {
        auto* row = input.data() + index * 188;
        std::memcpy(row, first.data() + index * 94, 46);
        std::memcpy(row + 46, row, 46);
        const auto& transfer = o::kExpectedTransfers[index];
        m::store<std::uint16_t>(row + 46 + 0x24, transfer.chloe_global);
        m::store<std::uint16_t>(row + 46 + 0x26, static_cast<std::uint16_t>(
            o::parameter_index_for_global(o::kChloeGroups, transfer.chloe_global)));
        m::store<std::uint16_t>(row + 46 + 0x28, transfer.nadine_global);
        m::store<std::uint16_t>(row + 46 + 0x2A, static_cast<std::uint16_t>(
            o::parameter_index_for_global(o::kNadineGroups, transfer.nadine_global)));
        std::memcpy(row + 92, first.data() + index * 94 + 46, 48);
        std::memcpy(row + 140, first.data() + index * 94 + 46, 48);
    }
    return input;
}

void full_refused(const FullInput& input, unsigned bad) {
    std::array<std::byte, kFullOutputBytes + 64> storage;
    storage.fill(std::byte{0xA7});
    const auto before = input;
    AuditStats stats{sizeof(AuditStats), 99, 99, 99};
    check(P99AuditFullChain(1, input.data(), static_cast<unsigned>(input.size()),
        storage.data() + 32, kFullOutputBytes, &stats) == -3);
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

void full_chain_admission() {
    const auto pristine = valid_full_input();
    // Mutating even the last row157 record/reference must refuse before row0
    // reaches native math; image1 would otherwise fault immediately.
    for (unsigned index = 0; index < 21; ++index) {
        for (unsigned record_offset : {0u, 46u}) {
            for (unsigned offset = 0; offset < 0x24; offset += 2) {
                for (std::uint16_t bits : std::array<std::uint16_t, 3>{0x7C00, 0xFC00, 0x7E00}) {
                    auto input = pristine;
                    m::store(input.data() + index * 188 + record_offset + offset, bits);
                    full_refused(input, index);
                }
            }
            for (unsigned offset : {0x06u, 0x14u}) {
                auto input = pristine;
                for (unsigned lane = 0; lane < 4; ++lane)
                    m::store<std::uint16_t>(input.data() + index * 188 + record_offset + offset + lane * 2, 0x8000);
                full_refused(input, index);
            }
            for (unsigned offset = 0x24; offset <= 0x2C; ++offset) {
                auto input = pristine;
                input[index * 188 + record_offset + offset] ^= std::byte{0x40};
                full_refused(input, index);
            }
        }
        for (unsigned reference_offset : {92u, 140u}) {
            for (unsigned lane = 0; lane < 12; ++lane) {
                if (lane == 3 || lane == 11) continue;
                for (float value : {std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::infinity(),
                                    -std::numeric_limits<float>::infinity()}) {
                    auto input = pristine;
                    m::store(input.data() + index * 188 + reference_offset + lane * 4, value);
                    full_refused(input, index);
                }
            }
            for (float value : {0.0F, 0.9F, 1.02F}) {
                auto input = pristine;
                m::store(input.data() + index * 188 + reference_offset + 28, value);
                full_refused(input, index);
            }
        }
    }
    auto padding = pristine;
    for (unsigned index = 0; index < 21; ++index) {
        for (unsigned record_offset : {0u, 46u})
            padding[index * 188 + record_offset + 45] = std::byte{0xFF};
        for (unsigned reference_offset : {92u, 140u}) {
            m::store(padding.data() + index * 188 + reference_offset + 12,
                     std::numeric_limits<float>::quiet_NaN());
            m::store(padding.data() + index * 188 + reference_offset + 44,
                     std::numeric_limits<float>::quiet_NaN());
        }
    }
    padding[20 * 188 + 46 + 0x2C] = std::byte{0};
    full_refused(padding, 20); // Unowned W/padding did not hide the final bad mode.

    std::array<std::byte, kFullOutputBytes> output{};
    for (unsigned input_size : {0u, 3947u, 3949u, ~0u}) {
        AuditStats stats{sizeof(AuditStats), 99, 99, 99};
        check(P99AuditFullChain(1, reinterpret_cast<const std::byte*>(1), input_size,
              output.data(), kFullOutputBytes, &stats) == -2);
        check(stats.retarget_calls == 0 && stats.blend_calls == 0);
    }
    for (unsigned output_size : {0u, 1359u, 1361u, ~0u}) {
        AuditStats stats{sizeof(AuditStats), 99, 99, 99};
        check(P99AuditFullChain(1, reinterpret_cast<const std::byte*>(1), 3948,
              output.data(), output_size, &stats) == -2);
        check(stats.retarget_calls == 0 && stats.blend_calls == 0);
    }
    check(P99AuditFullChain(1, nullptr, 3948, output.data(), kFullOutputBytes, nullptr) == -1);
    AuditStats stats{sizeof(AuditStats)};
    check(P99AuditFullChain(1, nullptr, 3948, output.data(), kFullOutputBytes, &stats) == -2);
    check(P99AuditFullChain(1, pristine.data(), 3948, nullptr, kFullOutputBytes, &stats) == -2);
    check(P99AuditFullChain(0, pristine.data(), 3948, output.data(), kFullOutputBytes, &stats) == -2);
    stats.size = 0;
    check(P99AuditFullChain(1, reinterpret_cast<const std::byte*>(1), 3948,
          output.data(), kFullOutputBytes, &stats) == -1);
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
    full_chain_admission();
    std::printf("P99 first-stage/full-chain admission: %u checks, %u failures; no native calls\n", checks, failures);
    return failures ? 1 : 0;
}
