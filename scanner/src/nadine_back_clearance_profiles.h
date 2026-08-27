#pragma once

#include <array>
#include <cstdint>

namespace coop::nadine_back {

enum class ClearanceProfileId : std::uint8_t {
    kNone,
    kType95Primary,
    kM30RugerPrimary,
};

struct AuthoredClearanceProfile {
    ClearanceProfileId id = ClearanceProfileId::kNone;
    float depth_meters = 0.0F;
};

// Offline weapon-art.bin Type 95/QBZ-95 primary P3. The exact art AND all
// authored primary words must match; a different online binding is not this
// measured profile. Never generalize a distance by primary-profile equality
// alone: e.g. 3019CBEE is Ruger, not the separately measured FAL model.
inline constexpr std::array<std::uint32_t, 8> kType95PrimaryBindingBits{
    0x3DC6A7F0u, 0xBD449BA6u, 0xBDA1CAC1u, 0x00000000u,
    0xBD4DB3EAu, 0xBDCBE230u, 0x3E0A9AE4u, 0x3F7C0861u,
};

inline constexpr std::array<std::uint32_t, 8> kM30RugerPrimaryBindingBits{
    0x3D9BA5E3u, 0xBCA3D70Au, 0xBD4CCCCDu, 0x00000000u,
    0xBD6477B3u, 0xBDCD7209u, 0x3E0A06FBu, 0x3F7BF4C4u,
};

[[nodiscard]] inline constexpr AuthoredClearanceProfile
select_authored_clearance_profile(
    std::uint32_t art, std::uint32_t model,
    const std::array<std::uint32_t, 8>& primary) noexcept {
    if (art == 0x27617D0Du && model == 0xA99E5C9Fu &&
        primary == kType95PrimaryBindingBits) {
        // HYPOTHESIS pending visual validation: rounded-up maximum of the
        // two root-only, anatomical-torso-overlap differential candidates
        // 12.764830 / 13.516403 cm, calibrated against accepted Mazur +5 cm.
        // The mixed-skin torso proxy is not a proof of final pixel clearance.
        // See the 2026-08-27 AGC geometry finding and reproducible audit.
        return {ClearanceProfileId::kType95Primary, 0.136F};
    }
    if (art == 0x3019CBEEu && model == 0x3D6F8C72u &&
        primary == kM30RugerPrimaryBindingBits) {
        // Same bounded differential method, but the actual Ruger mesh:
        // 6.272113 / 4.864946 cm. This is a separately scoped candidate,
        // not the FAL result and not a guarantee for every animated pose.
        return {ClearanceProfileId::kM30RugerPrimary, 0.063F};
    }
    return {};
}

} // namespace coop::nadine_back
