#pragma once

#include "nadine_p99_support_constraint.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace coop::p99_arm_roll {
using Locator = p99_support_constraint::Locator;
using Vector = std::array<float, 3>;
using Quaternion = std::array<float, 4>;

// Native DOWN5E67B1FA, row149, Nadine19..30. Unit incoming LEFT bend normal
// expressed in the SAME authored clip's right-prop30 frame, not a world pose.
// Independently tied to the disk reconstruction by Python regression tests.
inline constexpr Vector kAuthoredDownNormal{
    std::bit_cast<float>(0xBEF97DA3u), std::bit_cast<float>(0xBEFDA560u),
    std::bit_cast<float>(0xBF381801u)};

inline float dot(const Vector& a, const Vector& b) noexcept {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
inline Vector cross(const Vector& a, const Vector& b) noexcept {
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
inline Vector subtract(const Vector& a, const Vector& b) noexcept {
    return {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
}
inline Vector position(const Locator& value) noexcept {
    return {value[0], value[1], value[2]};
}
inline bool normalize(Vector& value, float minimum_length = 1e-5F) noexcept {
    const float length = std::sqrt(dot(value, value));
    if (!std::isfinite(length) || length < minimum_length) return false;
    for (auto& component : value) component /= length;
    return true;
}
inline Vector rotate(const Quaternion& q, const Vector& value) noexcept {
    const Vector xyz{q[0], q[1], q[2]};
    const Vector first = cross(xyz, value), second = cross(xyz, first);
    return {value[0]+2*(q[3]*first[0]+second[0]),
            value[1]+2*(q[3]*first[1]+second[1]),
            value[2]+2*(q[3]*first[2]+second[2])};
}
inline Quaternion quaternion(const Locator& value) noexcept {
    return {value[4], value[5], value[6], value[7]};
}

struct Plan {
    Quaternion world_delta{};
    Vector expected_elbow{};
    Vector desired_normal{};
    float angle_degrees = 0;
};

// Pure post-solve operation. Roll about the CURRENT shoulder->wrist axis.
// Shoulder uses D_world; wrist must then use conjugate(D_world) through the
// native world-delta helper. This leaves wrist position/orientation and all
// distal frames invariant, unlike changing the terminal support constraint.
// No reach solve, live-state authority or torso clearance is inferred here.
[[nodiscard]] inline bool plan(
    const Locator& shoulder, const Locator& elbow, const Locator& wrist,
    const Locator& right_prop, Plan* output) noexcept {
    if (output == nullptr) return false;
    *output = {};
    for (const auto* item : {&shoulder, &elbow, &wrist, &right_prop}) {
        if (!p99_support_constraint::locator_valid(*item)) return false;
    }
    const Vector s = position(shoulder), e = position(elbow), w = position(wrist);
    Vector axis = subtract(w, s), upper = subtract(e, s), lower = subtract(w, e);
    if (!normalize(axis) || !normalize(upper) || !normalize(lower)) return false;
    Vector normal = cross(upper, lower);
    if (!normalize(normal, 1e-4F)) return false;
    Quaternion prop_rotation = quaternion(right_prop);
    float prop_norm_sq = 0;
    for (const float component : prop_rotation) prop_norm_sq += component*component;
    const float inverse_prop_norm = 1.F/std::sqrt(prop_norm_sq);
    for (float& component : prop_rotation) component *= inverse_prop_norm;
    Vector desired = rotate(prop_rotation, kAuthoredDownNormal);
    const float along_axis = dot(desired, axis);
    for (std::size_t i = 0; i < 3; ++i) desired[i] -= along_axis*axis[i];
    if (!normalize(desired, 1e-4F)) return false;
    const float sine = dot(axis, cross(normal, desired));
    const float cosine = std::clamp(dot(normal, desired), -1.F, 1.F);
    const float angle = std::atan2(sine, cosine);
    const float half_sine = std::sin(angle*.5F);
    Plan candidate{};
    candidate.world_delta = {axis[0]*half_sine, axis[1]*half_sine,
                             axis[2]*half_sine, std::cos(angle*.5F)};
    const Vector rotated = rotate(candidate.world_delta, subtract(e, s));
    for (std::size_t i = 0; i < 3; ++i) candidate.expected_elbow[i] = s[i]+rotated[i];
    candidate.desired_normal = desired;
    candidate.angle_degrees = angle*57.29577951308232F;
    if (!std::isfinite(candidate.angle_degrees)) return false;
    for (const float component : candidate.expected_elbow) {
        if (!std::isfinite(component)) return false;
    }
    *output = candidate;
    return true;
}
} // namespace coop::p99_arm_roll
