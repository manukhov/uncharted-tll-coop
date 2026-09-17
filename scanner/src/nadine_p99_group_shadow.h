#pragma once

#include "nadine_p99_chloe_order_policy.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

// Value-only implementation of the STATIC C39 cmd16 storage contract. This
// header does not install a hook or confer owner/program/allocation authority.
// The caller must supply an owned snapshot of all21 completed retarget values
// and a proven live, nonaliasing cmd15-expanded group buffer. No owner, weapon,
// registry, or source-clip pointer is retained here.
namespace coop::p99_group_shadow {

namespace order = coop::p99_chloe_order;
inline constexpr std::size_t kRowBytes = 48;
inline constexpr std::size_t kContextBytes = 0x80;
inline constexpr std::size_t kMaxExpandedRows = 104;
inline constexpr std::uint32_t kCompleteMask = (1u << order::kRequiredJointCount) - 1u;
using Row = std::array<std::byte, kRowBytes>;
using SourceRows = std::array<Row, order::kRequiredJointCount>;
using Context = std::array<std::byte, kContextBytes>;

struct GroupShape {
    std::uint16_t index = 0xFFFF;
    std::uint16_t first_global = 0;
    std::uint16_t parameter_count = 0;
    std::uint16_t expanded_count = 0;
};

enum class Reject : std::uint8_t {
    kNone, kGroupShape, kOriginalExtent, kIncompleteSources, kSourceValue,
};

struct Shadow {
    // Stable, caller-owned storage. No self-referential pointers: safe to move
    // the result before creating the local context which points into it.
    alignas(16) std::array<std::byte, kMaxExpandedRows * kRowBytes> bytes{};
    std::uint16_t byte_count = 0;
    std::uint8_t group = 0xFF;
    bool valid = false;
};

struct Result {
    Reject reason = Reject::kGroupShape;
    Shadow shadow{};
};

[[nodiscard]] inline constexpr bool exact_shape(const GroupShape& shape) noexcept {
    if (shape.index > 1) {
        return false;
    }
    const auto& expected = order::kNadineGroups[shape.index];
    return shape.first_global == expected.first &&
        shape.parameter_count == expected.parameter_count &&
        shape.expanded_count == expected.count;
}

[[nodiscard]] inline bool numeric_row_valid(const Row& row) noexcept {
    std::array<float, 12> values{};
    std::memcpy(values.data(), row.data(), kRowBytes);
    for (const auto component : {0u, 1u, 2u, 8u, 9u, 10u}) {
        if (!std::isfinite(values[component])) {
            return false;
        }
    }
    float norm_squared = 0;
    for (std::size_t component = 4; component < 8; ++component) {
        if (!std::isfinite(values[component]) || std::abs(values[component]) > 1.01F) {
            return false;
        }
        norm_squared += values[component] * values[component];
    }
    // Same value contract as the existing native-retarget source producer.
    // Scale/translation W lanes are not asserted to be finite.
    return norm_squared > 0.90F && norm_squared < 1.10F;
}

[[nodiscard]] inline Result build(
    const GroupShape& shape, std::span<const std::byte> native_expanded,
    const SourceRows& sources, std::uint32_t source_mask) noexcept {
    Result result{};
    if (!exact_shape(shape)) {
        return result;
    }
    const std::size_t bytes = static_cast<std::size_t>(shape.expanded_count) * kRowBytes;
    if (native_expanded.size() != bytes) {
        result.reason = Reject::kOriginalExtent;
        return result;
    }
    if (source_mask != kCompleteMask) {
        result.reason = Reject::kIncompleteSources;
        return result;
    }
    // Validate BOTH groups before producing either shadow. This is not an
    // atomic publication promise: native group0 still executes before group1.
    for (const Row& source : sources) {
        if (!numeric_row_valid(source)) {
            result.reason = Reject::kSourceValue;
            return result;
        }
    }
    std::memcpy(result.shadow.bytes.data(), native_expanded.data(), bytes);
    for (std::size_t i = 0; i < order::kExpectedTransfers.size(); ++i) {
        const auto& transfer = order::kExpectedTransfers[i];
        if (transfer.nadine_group != shape.index) {
            continue;
        }
        const std::size_t local = transfer.nadine_global - shape.first_global;
        std::memcpy(result.shadow.bytes.data() + local * kRowBytes,
                    sources[i].data(), kRowBytes);
    }
    result.shadow.byte_count = static_cast<std::uint16_t>(bytes);
    result.shadow.group = static_cast<std::uint8_t>(shape.index);
    result.shadow.valid = true;
    result.reason = Reject::kNone;
    return result;
}

// Native cmd5 must continue to see its own original+0x50 allocation. Only the
// copied context is modified; the caller supplies stable aligned shadow storage
// and keeps it alive across exactly one synchronous native cmd16 invocation.
[[nodiscard]] inline std::optional<Context> copied_context(
    const Context& original, Shadow& stable_shadow) noexcept {
    if (!stable_shadow.valid || stable_shadow.group > 1 ||
        stable_shadow.byte_count !=
            order::kNadineGroups[stable_shadow.group].count * kRowBytes) {
        return std::nullopt;
    }
    Context copy = original;
    const std::uintptr_t shadow_address =
        reinterpret_cast<std::uintptr_t>(stable_shadow.bytes.data());
    std::memcpy(copy.data() + 0x50, &shadow_address, sizeof(shadow_address));
    return copy;
}

consteval bool mapped_rows_fit() {
    std::array<bool, 2 * kMaxExpandedRows> seen{};
    std::array<std::size_t, 2> counts{};
    for (const auto& transfer : order::kExpectedTransfers) {
        if (transfer.nadine_group > 1) {
            return false;
        }
        const auto& group = order::kNadineGroups[transfer.nadine_group];
        if (transfer.nadine_global < group.first ||
            transfer.nadine_global - group.first >= group.parameter_count ||
            transfer.nadine_global - group.first >= group.count) {
            return false;
        }
        const auto slot = transfer.nadine_group * kMaxExpandedRows +
            transfer.nadine_global - group.first;
        if (seen[slot]) {
            return false;
        }
        seen[slot] = true;
        ++counts[transfer.nadine_group];
    }
    return counts[0] == 2 && counts[1] == 19;
}
static_assert(mapped_rows_fit());
static_assert(sizeof(std::uintptr_t) == 8);
static_assert(alignof(Shadow) >= 16);

} // namespace coop::p99_group_shadow
