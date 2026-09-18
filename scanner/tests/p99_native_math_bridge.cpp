// Offline helper DLL only. Never injected; no game process handles or hooks.
#include "nadine_p99_source_materialize.h"
#include "nadine_p99_source_native_guard.h"
#include <cmath>
#include <intrin.h>
#include <immintrin.h>
#include <algorithm>

namespace m = coop::p99_source_materialize;
namespace o = coop::p99_chloe_order;
namespace guard = coop::p99_source_native_guard;

extern "C" __declspec(dllexport) unsigned P99AuditCpuReady() {
    int values[4]{};
    __cpuidex(values, 1, 0);
    constexpr unsigned required = (1u << 27) | (1u << 28) | (1u << 29);
    if ((static_cast<unsigned>(values[2]) & required) != required) return 0;
    if ((_xgetbv(0) & 6) != 6) return 0;
    __cpuidex(values, 7, 0);
    return (values[1] & (1 << 5)) != 0; // Conservative AVX2 envelope.
}

extern "C" __declspec(dllexport) unsigned P99AuditMxcsr(unsigned value) {
    const unsigned previous = _mm_getcsr();
    _mm_setcsr(value);
    return previous;
}

bool row_valid(const std::byte* row) {
    std::array<float, 12> values{};
    std::memcpy(values.data(), row, sizeof(values));
    float norm = 0;
    for (unsigned index = 0; index < values.size(); ++index) {
        if (index == 3 || index == 11) continue;
        if (!std::isfinite(values[index])) return false;
        if (index >= 4 && index <= 7) {
            if (std::abs(values[index]) > 1.01F) return false;
            norm += values[index] * values[index];
        }
    }
    return norm > 0.90F && norm < 1.10F;
}

struct AuditStats {
    unsigned size, retarget_calls, blend_calls, rejected_index;
};

template<std::size_t N> struct alignas(16) Guarded {
    std::array<std::byte, 32> head{};
    std::array<std::byte, N> value{};
    std::array<std::byte, 32 + (16 - N % 16) % 16> tail{};
    Guarded() { head.fill(std::byte{0xCD}); tail.fill(std::byte{0xCD}); }
    bool intact() const {
        const auto unchanged = [](std::byte x) { return x == std::byte{0xCD}; };
        return std::all_of(head.begin(), head.end(), unchanged) &&
            std::all_of(tail.begin(), tail.end(), unchanged);
    }
};

// Input: 21 * (record46 + Chloe reference48). Output: 21 * (base48 + additive48).
// This tests the first numerical stage, not the complete production21-row
// materializer, live table/registry selection, or any rendered Nadine result.
extern "C" __declspec(dllexport) int P99AuditRow20(std::uintptr_t image,
    const std::byte* input, unsigned input_bytes, std::byte* output,
    unsigned output_bytes, AuditStats* stats) {
    constexpr unsigned input_size = 21 * 94, output_size = 21 * 96;
    if (!stats || stats->size != sizeof(AuditStats)) return -1;
    *stats = {sizeof(AuditStats), 0, 0, ~0u};
    if (!input || !output || input_bytes != input_size || output_bytes != output_size || !image) return -2;
    std::memset(output, 0, output_size);
    std::array<m::Record, 21> records{};
    std::array<m::Row, 21> references{}, sources{}, additives{};
    for (std::size_t index = 0; index < 21; ++index) {
        std::memcpy(records[index].data(), input + index * 94, 46);
        std::memcpy(references[index].data(), input + index * 94 + 46, 48);
        std::memcpy(sources[index].data(), coop::p99_chloe_source_rows::kRows[index].player_words.data(), 48);
        std::memcpy(additives[index].data(), coop::p99_chloe_source_rows::kRows[index].chloe_additive_words.data(), 48);
        const auto& transfer = o::kExpectedTransfers[index];
        const auto* rec = records[index].data();
        if (!guard::record_valid(records[index]) || !row_valid(references[index].data()) ||
            !row_valid(sources[index].data()) || !row_valid(additives[index].data()) ||
            m::load<std::uint16_t>(rec + 0x24) != transfer.player_global ||
            m::load<std::uint16_t>(rec + 0x26) != o::parameter_index_for_global(o::kPlayerGroups, transfer.player_global) ||
            m::load<std::uint16_t>(rec + 0x28) != transfer.chloe_global ||
            m::load<std::uint16_t>(rec + 0x2A) != o::parameter_index_for_global(o::kChloeGroups, transfer.chloe_global) ||
            o::runtime_retarget_record_mode(rec) != transfer.mode) {
            stats->rejected_index = static_cast<unsigned>(index);
            return -3; // ALL rows preflight before the first native call.
        }
    }
    using DoJoint = bool(__fastcall*)(const void*, const void*, const void*, bool, void*);
    using Blend = void(__fastcall*)(std::uint16_t, void*, void*, float, std::uint16_t, void*);
    const auto retarget = reinterpret_cast<DoJoint>(image + 0x1AE2100);
    const auto blend = reinterpret_cast<Blend>(image + 0x2188A30);
    std::array<std::byte, output_size> completed{};
    for (std::size_t index = 0; index < 21; ++index) {
        Guarded<46> record;
        Guarded<48> src, ref, base;
        Guarded<m::kBlendBlobBytes> left, right;
        record.value = records[index];
        src.value = sources[index];
        ref.value = references[index];
        ++stats->retarget_calls;
        if (!retarget(record.value.data(), src.value.data(), ref.value.data(), false, base.value.data()) || !row_valid(base.value.data())) {
            stats->rejected_index = static_cast<unsigned>(index);
            return -4;
        }
        if (!record.intact() || !src.intact() || !ref.intact() || !base.intact() ||
            record.value != records[index] || src.value != sources[index] || ref.value != references[index]) {
            stats->rejected_index = static_cast<unsigned>(index);
            return -6;
        }
        std::memcpy(left.value.data(), base.value.data(), 48);
        std::memcpy(right.value.data(), additives[index].data(), 48);
        m::store(left.value.data() + 96, m::kOneRowValid);
        m::store(right.value.data() + 96, m::kOneRowValid);
        const auto right_before = right.value;
        ++stats->blend_calls;
        blend(m::kBlendEncoding, left.value.data(), right.value.data(), 1.0F, 0, nullptr);
        if (!row_valid(left.value.data()) || m::load<std::uint64_t>(left.value.data() + 96) != m::kOneRowValid ||
            m::load<std::uint64_t>(left.value.data() + 104) != 0) {
            stats->rejected_index = static_cast<unsigned>(index);
            return -5;
        }
        // Dispatcher D1D..D24 pads the four SIMD indices to {0,48,48,48}.
        // left[48:96] is a writable/discarded sentinel, NOT untouched padding.
        // The helper writes both rows within112; right stays read-only.
        if (!left.intact() || !right.intact() || right.value != right_before) {
            stats->rejected_index = static_cast<unsigned>(index);
            return -6;
        }
        std::memcpy(completed.data() + index * 96, base.value.data(), 48);
        std::memcpy(completed.data() + index * 96 + 48, left.value.data(), 48);
    }
    std::memcpy(output, completed.data(), output_size);
    return 0;
}
