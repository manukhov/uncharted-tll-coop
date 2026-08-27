#pragma once

#include <array>
#include <cstdint>

namespace coop::nadine_back {

// Match WeaponHolster_BuildBinding's independent T/Q fallback: only a null
// optional pointer keeps the corresponding primary words. A failed read of
// a present component rejects the whole secondary without publishing a prefix.
// The caller owns memory safety and transform validation; this only copies bits.
template <class ReadComponent>
[[nodiscard]] bool resolve_secondary_binding(
    const std::array<std::uint32_t, 8>& primary,
    const void* optional_translation,
    const void* optional_rotation,
    ReadComponent read_component,
    std::array<std::uint32_t, 8>& output) {
    auto candidate = primary;
    if (optional_translation != nullptr &&
        !read_component(optional_translation, candidate.data())) {
        return false;
    }
    if (optional_rotation != nullptr &&
        !read_component(optional_rotation, candidate.data() + 4)) {
        return false;
    }
    output = candidate;
    return true;
}

} // namespace coop::nadine_back
