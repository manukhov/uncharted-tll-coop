#pragma once
#include "nadine_p99_native_operand_fixture.h"
#include "nadine_p99_source_materialize.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace coop::p99_operand_identity {
enum class Difference : std::uint8_t { kNone, kInvalid, kRow20, kRow157, kChloeReference, kNadineReference };
struct Decision {
    Difference difference = Difference::kInvalid;
    std::size_t row = 0;
    [[nodiscard]] bool admitted() const noexcept { return difference == Difference::kNone; }
};
inline constexpr std::size_t kComparedRecordBytes = 0x2D;
inline constexpr std::size_t kComparedBytes = 21 * (2 * kComparedRecordBytes + 2 * 48);
static_assert(kComparedBytes == 3906);
static_assert(p99_native_operand_fixture::kRows.size() == p99_chloe_order::kRequiredJointCount);

// Caller passes COMPLETED OWNED values. No native calls, memory probing, live
// profile lookup, pointer retention, epoch cache, or source substitution here.
// Equality proves only equivalence of this invocation's numerical inputs to
// the reviewed conditional oracle, NOT the historical/live profile branch.
// The builder leaves record+2D unwritten; compare all other record bytes and
// ALL48 bytes of both references, including lanes the row validator ignores.
template<class Operands>
[[nodiscard]] Decision compare(const Operands& value) noexcept {
    if (!value.valid || !value.row20.valid || !value.row157.valid) return {};
    for (std::size_t index = 0; index < p99_native_operand_fixture::kRows.size(); ++index) {
        const auto& row = p99_native_operand_fixture::kRows[index];
        if (std::memcmp(value.row20_records[index].data(), row.data(), kComparedRecordBytes) != 0)
            return {Difference::kRow20, index};
        if (std::memcmp(value.row157_records[index].data(), row.data() + 46, kComparedRecordBytes) != 0)
            return {Difference::kRow157, index};
        if (std::memcmp(value.chloe_references[index].data(), row.data() + 92, 48) != 0)
            return {Difference::kChloeReference, index};
        if (std::memcmp(value.nadine_references[index].data(), row.data() + 140, 48) != 0)
            return {Difference::kNadineReference, index};
    }
    return {Difference::kNone, 0};
}
} // namespace coop::p99_operand_identity
