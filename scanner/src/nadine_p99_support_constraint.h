#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace coop::p99_support_constraint {

using Locator = std::array<float, 8>;

inline constexpr std::size_t kGetterCount = 4;
inline constexpr std::size_t kRightWrist = 0;
inline constexpr std::size_t kLeftWrist = 1;
inline constexpr std::size_t kRightHandProp = 2;
inline constexpr std::size_t kLeftHandProp = 3;

// actor77 anim-elena-combat-dlc.pak pistol-down clip 5E67B1FA, authored on
// elena-base D0B04A60/8592680F and evaluated through exact Elena-to-Nadine
// retarget row 149 into nadine-dlc-base C67A06F1/C39CA709. This is the authored
// left-wrist goal relative to the live right-hand prop, not a world-space pose.
inline constexpr Locator kNativeDownSupportConstraint{
    std::bit_cast<float>(0x3D90ED53u),
    std::bit_cast<float>(0xBC09CC1Cu),
    std::bit_cast<float>(0xBC974151u), 0.0F,
    std::bit_cast<float>(0x3EE07B1Au),
    std::bit_cast<float>(0x3D882E76u),
    std::bit_cast<float>(0xBF49A24Fu),
    std::bit_cast<float>(0x3EDB042Du)};
inline constexpr char kNativeDownConstraintSha256[] =
    "19e6944679d6e6f89d91020a083393806655a9d6291cf38b90a5223a64942a21";

[[nodiscard]] inline bool locator_valid(const Locator& locator) noexcept {
    for (const float value : locator) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    const float norm_sq = locator[4] * locator[4] +
        locator[5] * locator[5] + locator[6] * locator[6] +
        locator[7] * locator[7];
    return std::isfinite(norm_sq) && std::abs(norm_sq - 1.0F) <= 0.001F;
}

[[nodiscard]] inline Locator compose_rigid(
    const Locator& parent, const Locator& relative) noexcept {
    Locator output{};
    if (!locator_valid(parent) || !locator_valid(relative)) {
        return output;
    }
    const float qx = parent[4];
    const float qy = parent[5];
    const float qz = parent[6];
    const float qw = parent[7];
    const float vx = relative[0];
    const float vy = relative[1];
    const float vz = relative[2];
    const float tx = 2.0F * (qy * vz - qz * vy);
    const float ty = 2.0F * (qz * vx - qx * vz);
    const float tz = 2.0F * (qx * vy - qy * vx);
    output[0] = parent[0] + vx + qw * tx + (qy * tz - qz * ty);
    output[1] = parent[1] + vy + qw * ty + (qz * tx - qx * tz);
    output[2] = parent[2] + vz + qw * tz + (qx * ty - qy * tx);
    output[3] = 0.0F;

    const float rx = relative[4];
    const float ry = relative[5];
    const float rz = relative[6];
    const float rw = relative[7];
    output[4] = qw * rx + qx * rw + qy * rz - qz * ry;
    output[5] = qw * ry - qx * rz + qy * rw + qz * rx;
    output[6] = qw * rz + qx * ry - qy * rx + qz * rw;
    output[7] = qw * rw - qx * rx - qy * ry - qz * rz;
    const float norm_sq = output[4] * output[4] + output[5] * output[5] +
        output[6] * output[6] + output[7] * output[7];
    if (std::isfinite(norm_sq) && norm_sq > 0.000001F) {
        const float inverse_norm = 1.0F / std::sqrt(norm_sq);
        for (std::size_t index = 4; index < output.size(); ++index) {
            output[index] *= inverse_norm;
        }
    }
    return output;
}

[[nodiscard]] inline Locator make_relative_rigid(
    const Locator& parent, const Locator& child) noexcept {
    if (!locator_valid(parent) || !locator_valid(child)) {
        return {};
    }
    Locator inverse{};
    inverse[4] = -parent[4];
    inverse[5] = -parent[5];
    inverse[6] = -parent[6];
    inverse[7] = parent[7];
    const float qnorm_sq = parent[4] * parent[4] + parent[5] * parent[5] +
        parent[6] * parent[6] + parent[7] * parent[7];
    if (!std::isfinite(qnorm_sq) || std::abs(qnorm_sq - 1.0F) > 0.001F) {
        return {};
    }
    Locator delta = child;
    delta[0] -= parent[0];
    delta[1] -= parent[1];
    delta[2] -= parent[2];
    return compose_rigid(inverse, delta);
}

[[nodiscard]] inline Locator terminal_left_target(
    const Locator& right_hand_prop, const Locator& left_hand_prop,
    const Locator& left_wrist) noexcept {
    return compose_rigid(
        right_hand_prop, make_relative_rigid(left_hand_prop, left_wrist));
}

enum class ReturnChoice : std::uint8_t {
    kStock,
    kStagedLeftWrist,
    kCapturedRightHandProp,
};

enum class RejectReason : std::uint8_t {
    kNone,
    kInactive,
    kOrder,
    kMetadata,
    kLocator,
    kDesired,
};

struct StepDecision {
    ReturnChoice choice = ReturnChoice::kStock;
    RejectReason reason = RejectReason::kNone;
};

// One synchronous WeaponIK_Apply transaction. Getter 1 exposes a TLS-owned
// byte-identical copy of stock leftWrist, but does not modify it. Only getter 3
// may replace that already-returned copy and leftProp together, after all four
// exact calls have been observed. Therefore every refusal preserves stock.
struct Transaction {
    bool active = false;
    bool failed = false;
    bool committed = false;
    std::uint8_t next_getter = 0;
    RejectReason reject_reason = RejectReason::kNone;
    std::array<bool, kGetterCount> captured{};
    std::array<Locator, kGetterCount> stock{};
    Locator staged_left_wrist{};

    void begin(bool admitted) noexcept {
        *this = {};
        active = admitted;
        if (!admitted) {
            reject_reason = RejectReason::kInactive;
        }
    }

    [[nodiscard]] StepDecision observe(
        std::size_t getter_index, const Locator& locator,
        bool metadata_matches, const Locator* desired_left_wrist) noexcept {
        if (!active || failed || committed) {
            return {ReturnChoice::kStock,
                    failed ? reject_reason : RejectReason::kInactive};
        }
        if (getter_index >= kGetterCount || getter_index != next_getter) {
            return reject(RejectReason::kOrder);
        }
        if (!metadata_matches) {
            return reject(RejectReason::kMetadata);
        }
        if (!locator_valid(locator)) {
            return reject(RejectReason::kLocator);
        }
        stock[getter_index] = locator;
        captured[getter_index] = true;
        ++next_getter;

        if (getter_index == kLeftWrist) {
            staged_left_wrist = locator;
            return {ReturnChoice::kStagedLeftWrist, RejectReason::kNone};
        }
        if (getter_index != kLeftHandProp) {
            return {ReturnChoice::kStock, RejectReason::kNone};
        }
        if (desired_left_wrist == nullptr ||
            !locator_valid(*desired_left_wrist)) {
            return reject(RejectReason::kDesired);
        }
        for (const bool present : captured) {
            if (!present) {
                return reject(RejectReason::kOrder);
            }
        }
        staged_left_wrist = *desired_left_wrist;
        committed = true;
        return {ReturnChoice::kCapturedRightHandProp,
                RejectReason::kNone};
    }

    void fail(RejectReason reason) noexcept {
        if (active && !committed) {
            failed = true;
            reject_reason = reason;
        }
    }

private:
    [[nodiscard]] StepDecision reject(RejectReason reason) noexcept {
        failed = true;
        reject_reason = reason;
        return {ReturnChoice::kStock, reason};
    }
};

}  // namespace coop::p99_support_constraint
