#pragma once

#include "nadine_p99_chloe_order_policy.h"
#include "nadine_p99_chloe_source_rows.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Shared production value loop, not a native-math reimplementation. Runtime
// supplies the checked SEH/native callbacks; offline fixtures supply explicit
// scalar operations. Passing the latter proves routing/order/masks, NOT bitwise
// equivalence to RetargetPlugin or BlendTransformsByValidBits in tll.exe.
namespace coop::p99_source_materialize {
namespace order = p99_chloe_order;
namespace embedded = p99_chloe_source_rows;
using Row = std::array<std::byte, order::kRawJointCacheRowBytes>;
using Record = std::array<std::byte, order::kRuntimeRetargetRecordStride>;
using Mask = std::array<std::uint64_t, order::kGlobalMaskWordCount>;
inline constexpr std::size_t kBlendBlobBytes = 112;
inline constexpr std::uint16_t kBlendEncoding = 0x0108;
inline constexpr std::uint64_t kOneRowValid = 0x8000000000000000ull;
static_assert(embedded::kRows.size() == order::kRequiredJointCount &&
              order::kExpectedTransfers.size() == order::kRequiredJointCount);

struct SourceRows {
    bool valid = false;
    std::uint8_t source_group = 0xFF;
    std::uint8_t target_group = 0xFF;
    std::array<Row, order::kRequiredJointCount> rows{};
    std::array<std::uint8_t, order::kRequiredJointCount> present{};
    Mask valid_mask{};
    friend bool operator==(const SourceRows&, const SourceRows&) = default;
};
struct GroupResult {
    bool valid = false;
    std::uint8_t target_group = 0xFF;
    SourceRows player_base{};
    SourceRows chloe_additive{};
    std::array<Row, order::kRequiredJointCount> rows{};
    std::array<std::uint8_t, order::kRequiredJointCount> present{};
    Mask chloe_composite_mask{};
    Mask nadine_output_mask{};
    friend bool operator==(const GroupResult&, const GroupResult&) = default;
};

template<class T>
[[nodiscard]] inline T load(const std::byte* bytes) noexcept {
    T result{};
    std::memcpy(&result, bytes, sizeof(result));
    return result;
}
template<class T>
inline void store(std::byte* bytes, T value) noexcept {
    std::memcpy(bytes, &value, sizeof(value));
}

// Inputs are completed/caller-owned values. No borrowed source address, page
// certificate, owner pointer or lock is retained across a native callback.
// The caller still owns the native operand assertion preconditions and SEH.
template<class RowValid, class RecordValid, class DoJoint, class Blend>
[[nodiscard]] bool evaluate_joint(
    const Record& row20_record, const Record& row157_record,
    const order::ExpectedJointTransfer& transfer,
    const Row& player_base, const Row& chloe_additive,
    const Row& chloe_reference, const Row& nadine_reference,
    Row* output, RowValid row_valid, RecordValid record_valid, DoJoint do_joint, Blend blend) {
    if (output == nullptr) return false;
    *output = {};
    alignas(16) Row player_source = player_base;
    alignas(16) Row additive_source = chloe_additive;
    alignas(16) Row chloe_target_reference = chloe_reference;
    alignas(16) Row nadine_target_reference = nadine_reference;
    if (!row_valid(player_source.data()) || !row_valid(additive_source.data()) ||
        !row_valid(chloe_target_reference.data()) ||
        !row_valid(nadine_target_reference.data()) || !record_valid(row20_record) ||
        !record_valid(row157_record)) return false;

    const auto* row20 = row20_record.data();
    const auto* row157 = row157_record.data();
    const auto row20_mode = order::runtime_retarget_record_mode(row20);
    const auto row157_mode = order::runtime_retarget_record_mode(row157);
    const int player_parameter = order::parameter_index_for_global(order::kPlayerGroups, transfer.player_global);
    const int chloe_parameter = order::parameter_index_for_global(order::kChloeGroups, transfer.chloe_global);
    const int nadine_parameter = order::parameter_index_for_global(order::kNadineGroups, transfer.nadine_global);
    std::array<std::byte, 10> row20_fields{}, row157_fields{};
    std::memcpy(row20_fields.data(), row20 + 0x24, row20_fields.size());
    std::memcpy(row157_fields.data(), row157 + 0x24, row157_fields.size());
    if (player_parameter < 0 || chloe_parameter < 0 || nadine_parameter < 0 ||
        load<std::uint16_t>(row20 + 0x24) != transfer.player_global ||
        load<std::uint16_t>(row20 + 0x26) != player_parameter ||
        load<std::uint16_t>(row20 + 0x28) != transfer.chloe_global ||
        load<std::uint16_t>(row20 + 0x2A) != chloe_parameter || row20_mode != transfer.mode ||
        load<std::uint16_t>(row157 + 0x24) != transfer.chloe_global ||
        load<std::uint16_t>(row157 + 0x26) != chloe_parameter ||
        load<std::uint16_t>(row157 + 0x28) != transfer.nadine_global ||
        load<std::uint16_t>(row157 + 0x2A) != nadine_parameter || row157_mode != transfer.mode ||
        row20_mode > 3 || row157_mode > 3) return false;

    alignas(16) std::array<std::byte, kBlendBlobBytes> left{}, right{};
    alignas(16) Row chloe_base{}, nadine_output{};
    if (!do_joint(row20, player_source.data(), chloe_target_reference.data(), false, chloe_base.data()) ||
        !row_valid(chloe_base.data())) return false;
    std::memcpy(left.data(), chloe_base.data(), chloe_base.size());
    std::memcpy(right.data(), additive_source.data(), additive_source.size());
    store(left.data() + 96, kOneRowValid);
    store(right.data() + 96, kOneRowValid);
    blend(kBlendEncoding, left.data(), right.data(), 1.0F, std::uint16_t{0}, nullptr);
    if (load<std::uint64_t>(left.data() + 96) != kOneRowValid ||
        load<std::uint64_t>(left.data() + 104) != 0 || !row_valid(left.data()) ||
        !do_joint(row157, left.data(), nadine_target_reference.data(), false, nadine_output.data()) ||
        !row_valid(nadine_output.data()) ||
        std::memcmp(row20_fields.data(), row20 + 0x24, row20_fields.size()) != 0 ||
        std::memcmp(row157_fields.data(), row157 + 0x24, row157_fields.size()) != 0) return false;
    *output = nadine_output;
    return true;
}

template<class RowValid>
[[nodiscard]] bool build_source_rows(bool player_source, std::uint32_t target_group,
                                     SourceRows* output, RowValid row_valid) {
    if (output == nullptr) return false;
    *output = {};
    if (target_group > 1) return false;
    SourceRows result{};
    result.source_group = player_source ? embedded::kPlayerGroupPrograms[target_group].source_group
                                       : embedded::kChloeGroupPrograms[target_group].source_group;
    result.target_group = static_cast<std::uint8_t>(target_group);
    std::size_t selected = 0;
    for (std::size_t index = 0; index < embedded::kRows.size(); ++index) {
        if (order::kExpectedTransfers[index].nadine_group != target_group) continue;
        const auto& source = embedded::kRows[index];
        const auto source_group = player_source ? source.player_group : source.chloe_group;
        const auto global = player_source ? source.player_global : source.chloe_global;
        const auto& words = player_source ? source.player_words : source.chloe_additive_words;
        if (source_group != result.source_group) return false;
        std::memcpy(result.rows[index].data(), words.data(), result.rows[index].size());
        if (!row_valid(result.rows[index].data())) return false;
        result.present[index] = 1;
        result.valid_mask[global / 64u] |= std::uint64_t{1} << (global % 64u);
        ++selected;
    }
    const auto expected = target_group == 0 ? order::kExpectedNadineGroup0Targets.size()
                                           : order::kExpectedNadineRightFingerTargets.size();
    if (selected != expected) return false;
    result.valid = true;
    *output = result;
    return true;
}

// This is the production 21-row semantic loop. Operand metadata admission is
// done by the locked adapter; this loop only accepts its completed value result.
// EvaluateJoint has the same argument sequence as evaluate_joint without the
// four injected validation/math callbacks (normally the existing SEH wrapper).
template<class Operands, class EvaluateJoint>
[[nodiscard]] bool evaluate_group(const Operands& operands,
    const SourceRows& player_base, const SourceRows& chloe_additive,
    std::uint32_t target_group, GroupResult* output, EvaluateJoint evaluate) {
    if (output == nullptr) return false;
    *output = {};
    if (target_group > 1 || !operands.valid || !operands.row20.valid || !operands.row157.valid ||
        !player_base.valid || !chloe_additive.valid || player_base.target_group != target_group ||
        chloe_additive.target_group != target_group) return false;
    GroupResult result{};
    result.target_group = static_cast<std::uint8_t>(target_group);
    result.player_base = player_base;
    result.chloe_additive = chloe_additive;
    std::size_t selected = 0;
    for (std::size_t index = 0; index < order::kExpectedTransfers.size(); ++index) {
        const auto& transfer = order::kExpectedTransfers[index];
        if (transfer.nadine_group != target_group) continue;
        if (player_base.present[index] != 1 || chloe_additive.present[index] != 1 ||
            !evaluate(operands.row20_records[index], operands.row157_records[index], transfer,
                player_base.rows[index], chloe_additive.rows[index], operands.chloe_references[index],
                operands.nadine_references[index], &result.rows[index])) return false;
        result.present[index] = 1;
        result.chloe_composite_mask[transfer.chloe_global / 64u] |= std::uint64_t{1} << (transfer.chloe_global % 64u);
        result.nadine_output_mask[transfer.nadine_global / 64u] |= std::uint64_t{1} << (transfer.nadine_global % 64u);
        ++selected;
    }
    const auto expected = target_group == 0 ? order::kExpectedNadineGroup0Targets.size()
                                           : order::kExpectedNadineRightFingerTargets.size();
    if (selected != expected) return false;
    result.valid = true;
    *output = result;
    return true;
}
} // namespace coop::p99_source_materialize
