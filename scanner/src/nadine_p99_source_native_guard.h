#pragma once

#include "nadine_p99_chloe_order_policy.h"
#include <cstddef>
#include <cstdint>
#include <span>

// Conservative value-only admission for the mode-1/mode-3 DoJointRetarget
// calls made by the owned P99 source. This is NOT an engine-math replacement,
// a borrowed-memory validator, or authorization to enable the package.
namespace coop::p99_source_native_guard {

[[nodiscard]] inline constexpr bool half_finite(std::uint16_t bits) noexcept {
    return (bits & 0x7C00u) != 0x7C00u;
}

[[nodiscard]] inline constexpr std::uint16_t load_half_word(
    std::span<const std::byte> record, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(record[offset]) |
        (static_cast<std::uint16_t>(
             std::to_integer<std::uint8_t>(record[offset + 1])) << 8));
}

// DecodePackedReferenceLocator 0x141AE1F10 reads quaternion halves at
// locator+6/+8/+A/+C, then unconditionally normalizes with rsqrt plus Newton
// refinement (0x141AE1F7C..1FF2). A finite nonzero half quaternion has norm^2
// in [2^-48, 4*65504^2], so its float norm is normal, positive and finite.
// All signed-zero combinations must be refused, not just four +0 words.
[[nodiscard]] inline constexpr bool packed_quaternion_nonzero(
    std::span<const std::byte> record, std::size_t first_component) noexcept {
    if (first_component > record.size() ||
        record.size() - first_component < 8) return false;
    std::uint16_t magnitude_bits = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        magnitude_bits |= load_half_word(record, first_component + i * 2) & 0x7FFFu;
    }
    return magnitude_bits != 0;
}

[[nodiscard]] inline constexpr bool record_valid(
    std::span<const std::byte> record) noexcept {
    if (record.size() != p99_chloe_order::kRuntimeRetargetRecordStride) return false;
    const auto mode = p99_chloe_order::runtime_retarget_record_mode(record.data());
    if (mode != 1 && mode != 3) return false;

    // Two packed 14-byte locators, three additional vector halves, one scalar.
    // This deliberately admits a narrower domain than native mode 1/3 needs:
    // some numerical fields do not contribute to those modes' final output.
    // A rejected unused field is a policy refusal, not proof of corruption.
    for (std::size_t offset = 0; offset < 0x24; offset += 2) {
        if (!half_finite(load_half_word(record, offset))) return false;
    }
    // DoJoint decodes BOTH locators even for mode 3. For mode 1, malformed
    // reference rotations can reach the retargetedRot assertion callback at
    // 0x141AE29BD before any caller-side output validity check can run.
    // Keep exact transfer metadata checks separate; tail padding+2D is ignored.
    return packed_quaternion_nonzero(record, 0x06) &&
        packed_quaternion_nonzero(record, 0x14);
}

} // namespace coop::p99_source_native_guard
