#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

// Pure post-stock world translation. This owns no art-specific distances,
// identity/phase authority, game memory, orientation writes or persistent state.
// The caller must supply the freshly computed stock frame on every callback.
namespace coop::nadine_back {

struct ClearanceResult {
    std::array<float, 8> output{}; // Txyzw, Qxyzw
    std::array<float, 3> body_back_delta{};
};

namespace clearance_detail {

inline bool valid_stock_transform(const std::array<float, 8>& stock) noexcept {
    for (const float value : stock) {
        if (!std::isfinite(value)) return false;
    }
    double norm_squared = 0.0;
    for (std::size_t i = 4; i < stock.size(); ++i) {
        const double component = stock[i];
        norm_squared += component * component;
    }
    // Match raw_attach_transform_valid's admitted quaternion range. Do not
    // normalize the stock quaternion: only its X/Z translation may change.
    return norm_squared > 0.5 && norm_squared < 1.5;
}

inline bool normalized_body_quaternion(
    const std::array<float, 4>& body, std::array<double, 4>& normalized) noexcept {
    double norm_squared = 0.0;
    for (const float value : body) {
        if (!std::isfinite(value) || std::fabs(value) > 1.01F) return false;
        const double component = value;
        norm_squared += component * component;
    }
    // Keep valid_network_orientation's existing admission bounds rather than
    // granting an arbitrary finite/nonzero corrupted pose a new orientation.
    // Normalize only this local copy: mild accepted norm drift must not change
    // the heading obtained from q * (0,0,+1) * inverse(q).
    if (!(norm_squared > static_cast<double>(0.90F) &&
          norm_squared < static_cast<double>(1.10F))) {
        return false;
    }
    const double inverse_norm = 1.0 / std::sqrt(norm_squared);
    for (std::size_t i = 0; i < body.size(); ++i) {
        normalized[i] = static_cast<double>(body[i]) * inverse_norm;
    }
    return true;
}

inline bool finite_float_range(double value) noexcept {
    constexpr double limit = (std::numeric_limits<float>::max)();
    return std::isfinite(value) && value >= -limit && value <= limit;
}

} // namespace clearance_detail

// Failure leaves result byte-for-byte unchanged, including a result whose
// output aliases stock. Y, position-W and all four stock quaternion channels
// are copied as bytes. A zero amount preserves the complete stock frame.
// Even zero weight/depth must pass all validation, including a usable heading.
[[nodiscard]] inline bool apply_body_back_clearance(
    const std::array<float, 8>& stock,
    const std::array<float, 4>& body_world_quaternion,
    float depth_meters, float weight, ClearanceResult& result) noexcept {
    if (!clearance_detail::valid_stock_transform(stock) ||
        !std::isfinite(depth_meters) || depth_meters < 0.0F ||
        !std::isfinite(weight) || weight < 0.0F || weight > 1.0F) {
        return false;
    }

    std::array<double, 4> body{};
    if (!clearance_detail::normalized_body_quaternion(
            body_world_quaternion, body)) {
        return false;
    }
    const double forward_x = 2.0 * (body[0] * body[2] + body[3] * body[1]);
    const double forward_z =
        1.0 - 2.0 * (body[0] * body[0] + body[1] * body[1]);
    const double horizontal_length =
        std::sqrt(forward_x * forward_x + forward_z * forward_z);
    // Same horizontal-heading floor as the accepted item-side depth path.
    if (!std::isfinite(horizontal_length) || horizontal_length < 1.0e-4) {
        return false;
    }

    ClearanceResult candidate{};
    std::memcpy(candidate.output.data(), stock.data(), sizeof(candidate.output));
    if (depth_meters != 0.0F && weight != 0.0F) {
        const double amount =
            static_cast<double>(depth_meters) * static_cast<double>(weight);
        const double delta_x = -forward_x / horizontal_length * amount;
        const double delta_z = -forward_z / horizontal_length * amount;
        const double output_x = static_cast<double>(stock[0]) + delta_x;
        const double output_z = static_cast<double>(stock[2]) + delta_z;
        // Check before narrowing, including additions to large finite world
        // positions. Never publish a partially translated/overflowed frame.
        if (!clearance_detail::finite_float_range(delta_x) ||
            !clearance_detail::finite_float_range(delta_z) ||
            !clearance_detail::finite_float_range(output_x) ||
            !clearance_detail::finite_float_range(output_z)) {
            return false;
        }
        candidate.body_back_delta[0] = static_cast<float>(delta_x);
        candidate.body_back_delta[2] = static_cast<float>(delta_z);
        candidate.output[0] = static_cast<float>(output_x);
        candidate.output[2] = static_cast<float>(output_z);
    }

    if (!clearance_detail::valid_stock_transform(candidate.output)) return false;
    std::memcpy(result.output.data(), candidate.output.data(), sizeof(result.output));
    std::memcpy(result.body_back_delta.data(), candidate.body_back_delta.data(),
                sizeof(result.body_back_delta));
    return true;
}

} // namespace coop::nadine_back
