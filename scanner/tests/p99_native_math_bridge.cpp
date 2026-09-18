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

namespace {
constexpr unsigned kFullInputStride = 46 + 46 + 48 + 48;
constexpr unsigned kFullInputBytes = 21 * kFullInputStride;
constexpr unsigned kMaskBytes = static_cast<unsigned>(sizeof(m::Mask));
constexpr unsigned kFullOutputBytes = 21 * 48 + 4 * kMaskBytes;
static_assert(kFullInputBytes == 3948 && kMaskBytes == 88 && kFullOutputBytes == 1360);

struct FullOperands {
    bool valid = true;
    struct Entry { bool valid = true; } row20{}, row157{};
    std::array<m::Record, 21> row20_records{}, row157_records{};
    std::array<m::Row, 21> chloe_references{}, nadine_references{};
};

bool metadata_valid(const m::Record& record, const o::ExpectedJointTransfer& transfer,
                    bool second) {
    const auto source = second ? transfer.chloe_global : transfer.player_global;
    const auto target = second ? transfer.nadine_global : transfer.chloe_global;
    const int source_parameter = second
        ? o::parameter_index_for_global(o::kChloeGroups, source)
        : o::parameter_index_for_global(o::kPlayerGroups, source);
    const int target_parameter = second
        ? o::parameter_index_for_global(o::kNadineGroups, target)
        : o::parameter_index_for_global(o::kChloeGroups, target);
    return source_parameter >= 0 && target_parameter >= 0 &&
        m::load<std::uint16_t>(record.data() + 0x24) == source &&
        m::load<std::uint16_t>(record.data() + 0x26) == source_parameter &&
        m::load<std::uint16_t>(record.data() + 0x28) == target &&
        m::load<std::uint16_t>(record.data() + 0x2A) == target_parameter &&
        o::runtime_retarget_record_mode(record.data()) == transfer.mode;
}

// Native functions see ONLY these inner guarded copies, never shared-loop or
// caller buffers. Successful owned values are copied back to the real shared
// materializer. Failure poisons only its local blend masks to stop the loop.
struct FullMath {
    using DoJoint = bool(__fastcall*)(const void*, const void*, const void*, bool, void*);
    using Blend = void(__fastcall*)(std::uint16_t, void*, void*, float, std::uint16_t, void*);
    DoJoint retarget;
    Blend blend;
    AuditStats* stats;
    unsigned index = ~0u;
    int error = 0;

    bool joint(const void* record_bytes, const void* source, const void* reference,
               bool flag, void* destination) {
        if (error) return false;
        if (flag) { error = -4; return false; }
        Guarded<46> record;
        Guarded<48> src, ref, output;
        std::memcpy(record.value.data(), record_bytes, record.value.size());
        std::memcpy(src.value.data(), source, src.value.size());
        std::memcpy(ref.value.data(), reference, ref.value.size());
        const auto record_before = record.value;
        const auto src_before = src.value, ref_before = ref.value;
        ++stats->retarget_calls;
        const bool okay = retarget(record.value.data(), src.value.data(), ref.value.data(),
                                  false, output.value.data());
        if (!record.intact() || !src.intact() || !ref.intact() || !output.intact() ||
            record.value != record_before || src.value != src_before || ref.value != ref_before) {
            error = -6;
            return false;
        }
        if (!okay || !row_valid(output.value.data())) { error = -4; return false; }
        std::memcpy(destination, output.value.data(), output.value.size());
        return true;
    }

    void additive(std::uint16_t encoding, void* lhs, void* rhs, float weight,
                  std::uint16_t field, void* scratch) {
        auto* shared_left = static_cast<std::byte*>(lhs);
        const auto stop = [&] { m::store(shared_left + 104, std::uint64_t{1}); };
        if (error) { stop(); return; }
        Guarded<m::kBlendBlobBytes> left, right;
        std::memcpy(left.value.data(), lhs, left.value.size());
        std::memcpy(right.value.data(), rhs, right.value.size());
        const auto masks_valid = [](const auto& blob) {
            return m::load<std::uint64_t>(blob.data() + 96) == m::kOneRowValid &&
                   m::load<std::uint64_t>(blob.data() + 104) == 0;
        };
        if (encoding != m::kBlendEncoding || weight != 1.0F || field != 0 || scratch ||
            !row_valid(left.value.data()) || !row_valid(right.value.data()) ||
            !masks_valid(left.value) || !masks_valid(right.value)) {
            error = -5;
            stop();
            return;
        }
        const auto right_before = right.value;
        ++stats->blend_calls;
        blend(encoding, left.value.data(), right.value.data(), weight, field, nullptr);
        if (!left.intact() || !right.intact() || right.value != right_before) {
            error = -6;
            stop();
            return;
        }
        if (!row_valid(left.value.data()) || !masks_valid(left.value)) {
            error = -5;
            stop();
            return;
        }
        // Native SIMD padding writes left[48:96]. This discarded sentinel is
        // deliberately copied, not required to stay zero or finite. Right112
        // remains read-only, and both mask words must remain {MSB,0}.
        std::memcpy(lhs, left.value.data(), left.value.size());
    }

    bool evaluate(const m::Record& first, const m::Record& second,
        const o::ExpectedJointTransfer& transfer, const m::Row& player,
        const m::Row& additive_row, const m::Row& chloe, const m::Row& nadine,
        m::Row* output) {
        index = static_cast<unsigned>(&transfer - o::kExpectedTransfers.data());
        const bool okay = m::evaluate_joint(first, second, transfer, player, additive_row,
            chloe, nadine, output, row_valid, guard::record_valid,
            [&](auto... args) { return joint(args...); },
            [&](auto... args) { additive(args...); });
        if (!okay) {
            stats->rejected_index = index;
            if (!error) error = -4;
        }
        return okay;
    }
};
} // namespace

// Input per semantic row: row20 record46, row157 record46, Chloe reference48,
// Nadine reference48. Output:21 final Nadine rows48, followed by four masks88:
// group0 Chloe, group0 Nadine, group1 Chloe, group1 Nadine. Unlike Row20, this
// invokes the ACTUAL shared group/joint materializer, not an audit-local loop.
// Operand provenance/conditional row157 branch admission remains caller-owned.
extern "C" __declspec(dllexport) int P99AuditFullChain(std::uintptr_t image,
    const std::byte* input, unsigned input_bytes, std::byte* output,
    unsigned output_bytes, AuditStats* stats) {
    if (!stats || stats->size != sizeof(AuditStats)) return -1;
    *stats = {sizeof(AuditStats), 0, 0, ~0u};
    if (!input || !output || input_bytes != kFullInputBytes ||
        output_bytes != kFullOutputBytes || !image) return -2;
    std::memset(output, 0, kFullOutputBytes);
    FullOperands operands;
    // ALL42 records,42 references and42 embedded source rows (both groups)
    // pass value/metadata checks before ANY native function can be called.
    for (std::size_t index = 0; index < 21; ++index) {
        const auto* bytes = input + index * kFullInputStride;
        std::memcpy(operands.row20_records[index].data(), bytes, 46);
        std::memcpy(operands.row157_records[index].data(), bytes + 46, 46);
        std::memcpy(operands.chloe_references[index].data(), bytes + 92, 48);
        std::memcpy(operands.nadine_references[index].data(), bytes + 140, 48);
        m::Row player{}, additive{};
        std::memcpy(player.data(), coop::p99_chloe_source_rows::kRows[index].player_words.data(), 48);
        std::memcpy(additive.data(), coop::p99_chloe_source_rows::kRows[index].chloe_additive_words.data(), 48);
        const auto& transfer = o::kExpectedTransfers[index];
        if (!guard::record_valid(operands.row20_records[index]) ||
            !guard::record_valid(operands.row157_records[index]) ||
            !metadata_valid(operands.row20_records[index], transfer, false) ||
            !metadata_valid(operands.row157_records[index], transfer, true) ||
            !row_valid(operands.chloe_references[index].data()) ||
            !row_valid(operands.nadine_references[index].data()) ||
            !row_valid(player.data()) || !row_valid(additive.data())) {
            stats->rejected_index = static_cast<unsigned>(index);
            return -3;
        }
    }
    std::array<m::SourceRows, 2> players{}, additives{};
    for (unsigned group = 0; group < 2; ++group) {
        if (!m::build_source_rows(true, group, &players[group], row_valid) ||
            !m::build_source_rows(false, group, &additives[group], row_valid)) return -3;
    }
    FullMath math{reinterpret_cast<FullMath::DoJoint>(image + 0x1AE2100),
                  reinterpret_cast<FullMath::Blend>(image + 0x2188A30), stats};
    std::array<m::GroupResult, 2> groups{};
    for (unsigned group = 0; group < 2; ++group) {
        if (!m::evaluate_group(operands, players[group], additives[group], group, &groups[group],
            [&](auto&&... args) { return math.evaluate(args...); })) return math.error ? math.error : -4;
    }
    std::array<std::byte, kFullOutputBytes> completed{};
    for (std::size_t index = 0; index < 21; ++index) {
        const auto group = o::kExpectedTransfers[index].nadine_group;
        if (group > 1 || groups[group].present[index] != 1 ||
            groups[1 - group].present[index] != 0 || !row_valid(groups[group].rows[index].data())) {
            stats->rejected_index = static_cast<unsigned>(index);
            return -4;
        }
        std::memcpy(completed.data() + index * 48, groups[group].rows[index].data(), 48);
    }
    for (unsigned group = 0; group < 2; ++group) {
        auto* masks = completed.data() + 21 * 48 + group * 2 * kMaskBytes;
        std::memcpy(masks, groups[group].chloe_composite_mask.data(), kMaskBytes);
        std::memcpy(masks + kMaskBytes, groups[group].nadine_output_mask.data(), kMaskBytes);
    }
    if (stats->retarget_calls != 42 || stats->blend_calls != 21) return -4;
    std::memcpy(output, completed.data(), completed.size());
    return 0;
}
