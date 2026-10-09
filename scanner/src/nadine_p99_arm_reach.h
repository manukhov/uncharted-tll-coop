#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace coop::p99_arm_reach {
using Point = std::array<float, 3>;
using Chain = std::array<Point, 3>; // shoulder, elbow, wrist
using Vector = std::array<double, 3>;

struct Metrics {
    double upper_length = 0;
    double lower_length = 0;
    double requested_distance = 0;
    double pre_reach = 0;
    double post_reach = 0;
    double target_excess = 0;
    double target_error = 0;
    double clamped_target_error = 0;
    double pre_elbow_radius = 0;
    double post_elbow_radius = 0;
    double shoulder_shift = 0;
    double segment_length_error = 0;
};

inline Vector delta(const Point& a, const Point& b) noexcept {
    return {static_cast<double>(a[0])-b[0], static_cast<double>(a[1])-b[1],
            static_cast<double>(a[2])-b[2]};
}
inline double dot(const Vector& a, const Vector& b) noexcept {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
inline double length(const Vector& value) noexcept { return std::sqrt(dot(value,value)); }
inline double radius(const Chain& chain) noexcept {
    const auto axis=delta(chain[2],chain[0]), upper=delta(chain[1],chain[0]);
    const double squared=dot(axis,axis);
    if (squared<=1e-12) return 0;
    const double projection=dot(upper,axis)/squared;
    Vector perpendicular{};
    for (std::size_t i=0;i<3;++i) perpendicular[i]=upper[i]-projection*axis[i];
    return length(perpendicular);
}

// Read-only geometry, no pose correction and no automatic diagnosis. In
// particular a straight OUTPUT does not establish that INPUT T was unreachable.
// Only paired same-call native target and pre/post chain snapshots can test it.
inline bool analyze(const Chain& before, const Chain& after, const Point& target,
                    Metrics* output) noexcept {
    if (output==nullptr) return false;
    *output={};
    for(const auto* chain:{&before,&after})
        for(const auto& point:*chain)
            for(float value:point) if(!std::isfinite(value)) return false;
    for(float value:target) if(!std::isfinite(value)) return false;
    Metrics m{};
    m.upper_length=length(delta(before[1],before[0]));
    m.lower_length=length(delta(before[2],before[1]));
    const auto requested=delta(target,before[0]);
    m.requested_distance=length(requested);
    if(m.upper_length<=1e-6 || m.lower_length<=1e-6 || m.requested_distance<=1e-6)
        return false;
    m.pre_reach=length(delta(before[2],before[0]));
    m.post_reach=length(delta(after[2],after[0]));
    const double maximum=m.upper_length+m.lower_length;
    m.target_excess=m.requested_distance-maximum;
    m.target_error=length(delta(after[2],target));
    const double factor=std::min(m.requested_distance,maximum)/m.requested_distance;
    Vector clamped_error{};
    for(std::size_t i=0;i<3;++i)
        clamped_error[i]=static_cast<double>(after[2][i])-before[0][i]-factor*requested[i];
    m.clamped_target_error=length(clamped_error);
    m.pre_elbow_radius=radius(before);
    m.post_elbow_radius=radius(after);
    m.shoulder_shift=length(delta(after[0],before[0]));
    m.segment_length_error=std::max(
        std::abs(length(delta(after[1],after[0]))-m.upper_length),
        std::abs(length(delta(after[2],after[1]))-m.lower_length));
    *output=m;
    return true;
}
} // namespace coop::p99_arm_reach
