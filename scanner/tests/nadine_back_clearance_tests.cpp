#include "nadine_back_clearance.h"
#include "nadine_back_clearance_profiles.h"
#include "nadine_holster_binding.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>

namespace {

using coop::nadine_back::ClearanceResult;
using coop::nadine_back::apply_body_back_clearance;

// Do not use assert: these checks must execute in the Release/NDEBUG build.
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n";   \
            return false;                                                       \
        }                                                                       \
    } while (false)

constexpr std::array<float, 4> kBodyIdentity{0.0F, 0.0F, 0.0F, 1.0F};

std::array<float, 8> stock_frame() {
    return {1.0F, 2.0F, 3.0F, -0.0F, -0.0F, 0.0F, -0.0F, 1.0F};
}

ClearanceResult sentinel_result() {
    return {{41.0F, -0.0F, 43.0F, 44.0F, 45.0F, 46.0F, 47.0F,
             std::bit_cast<float>(0x7FC055AAu)},
            {71.0F, -0.0F, 73.0F}};
}

template <std::size_t N>
bool same_bits(const std::array<float, N>& left,
               const std::array<float, N>& right) {
    for (std::size_t i = 0; i < N; ++i) {
        if (std::bit_cast<std::uint32_t>(left[i]) !=
            std::bit_cast<std::uint32_t>(right[i])) {
            return false;
        }
    }
    return true;
}

bool same_result_bits(const ClearanceResult& left, const ClearanceResult& right) {
    return same_bits(left.output, right.output) &&
        same_bits(left.body_back_delta, right.body_back_delta);
}

bool near(float left, float right, float tolerance = 1.0e-6F) {
    return std::fabs(left - right) <= tolerance;
}

bool unchanged_channels(const std::array<float, 8>& stock,
                        const ClearanceResult& result) {
    constexpr std::array<std::size_t, 6> preserved{1, 3, 4, 5, 6, 7};
    for (const std::size_t i : preserved) {
        if (std::bit_cast<std::uint32_t>(stock[i]) !=
            std::bit_cast<std::uint32_t>(result.output[i])) {
            return false;
        }
    }
    return std::bit_cast<std::uint32_t>(result.body_back_delta[1]) == 0u;
}

bool rejects_without_writing(const std::array<float, 8>& stock,
                            const std::array<float, 4>& body,
                            float depth, float weight) {
    ClearanceResult result = sentinel_result();
    const ClearanceResult before = result;
    CHECK(!apply_body_back_clearance(stock, body, depth, weight, result));
    CHECK(same_result_bits(before, result));
    return true;
}

bool cardinal_yaws_move_behind_the_body() {
    const float half_root = std::sqrt(0.5F);
    const std::array<std::array<float, 4>, 4> bodies{{
        kBodyIdentity,
        {0.0F, half_root, 0.0F, half_root},
        {0.0F, 1.0F, 0.0F, 0.0F},
        {0.0F, -half_root, 0.0F, half_root},
    }};
    constexpr std::array<std::array<float, 3>, 4> deltas{{
        {0.0F, 0.0F, -0.25F},
        {-0.25F, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.25F},
        {0.25F, 0.0F, 0.0F},
    }};
    const auto stock = stock_frame();
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        ClearanceResult result{};
        CHECK(apply_body_back_clearance(stock, bodies[i], 0.25F, 1.0F, result));
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(near(result.body_back_delta[axis], deltas[i][axis]));
            CHECK(near(result.output[axis], stock[axis] + deltas[i][axis]));
        }
        CHECK(unchanged_channels(stock, result));
    }
    return true;
}

bool accepted_nonunit_body_scales_are_normalized_locally() {
    const auto stock = stock_frame();
    const std::array<float, 4> body{0.0F, 0.38268343F, 0.0F, 0.92387950F};
    ClearanceResult reference{};
    CHECK(apply_body_back_clearance(stock, body, 0.123F, 0.37F, reference));
    for (const float scale : {0.99F, 1.0F, 1.01F, -0.99F, -1.0F, -1.01F}) {
        auto scaled = body;
        for (float& component : scaled) component *= scale;
        const auto before = scaled;
        ClearanceResult result{};
        CHECK(apply_body_back_clearance(stock, scaled, 0.123F, 0.37F, result));
        CHECK(same_bits(scaled, before));
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(near(result.body_back_delta[axis], reference.body_back_delta[axis]));
            CHECK(near(result.output[axis], reference.output[axis]));
        }
        CHECK(unchanged_channels(stock, result));
    }
    return true;
}

bool weighted_delta_has_the_requested_horizontal_norm() {
    const auto stock = stock_frame();
    const std::array<float, 4> body{0.2F, 0.3F, 0.1F, std::sqrt(0.86F)};
    for (const float weight : {0.0F, 0.1F, 0.25F, 0.5F, 1.0F}) {
        ClearanceResult result{};
        CHECK(apply_body_back_clearance(stock, body, 0.125F, weight, result));
        CHECK(near(std::hypot(result.body_back_delta[0], result.body_back_delta[2]),
                   0.125F * weight));
        CHECK(unchanged_channels(stock, result));
    }
    return true;
}

bool translation_preserves_y_w_and_stock_quaternion_bits() {
    auto stock = stock_frame();
    stock[1] = -0.0F;
    stock[3] = std::bit_cast<float>(0x80000000u);
    stock[5] = 0.1F;
    stock[7] = 1.1F; // Valid stock norm drift must not be normalized away.
    const auto before = stock;
    const std::array<float, 4> body{0.0F, 0.38268343F, 0.0F, 0.92387950F};
    ClearanceResult result{};
    CHECK(apply_body_back_clearance(stock, body, 0.125F, 0.5F, result));
    CHECK(same_bits(stock, before));
    CHECK(unchanged_channels(stock, result));
    CHECK(std::bit_cast<std::uint32_t>(result.output[1]) == 0x80000000u);
    CHECK(std::bit_cast<std::uint32_t>(result.output[3]) == 0x80000000u);
    CHECK(std::bit_cast<std::uint32_t>(result.output[4]) == 0x80000000u);
    CHECK(std::bit_cast<std::uint32_t>(result.output[6]) == 0x80000000u);
    CHECK(result.output[7] == 1.1F);
    return true;
}

bool repeated_fresh_stock_calls_do_not_accumulate() {
    const auto stock = stock_frame();
    ClearanceResult result{};
    CHECK(apply_body_back_clearance(stock, kBodyIdentity, 0.25F, 1.0F, result));
    const ClearanceResult first = result;
    for (unsigned i = 0; i < 32; ++i) {
        CHECK(apply_body_back_clearance(stock, kBodyIdentity, 0.25F, 1.0F, result));
        CHECK(same_result_bits(result, first));
    }
    return true;
}

bool output_may_alias_the_stock_input() {
    const auto stock = stock_frame();
    ClearanceResult expected{};
    CHECK(apply_body_back_clearance(stock, kBodyIdentity, 0.25F, 1.0F, expected));
    ClearanceResult aliased{};
    std::memcpy(aliased.output.data(), stock.data(), sizeof(stock));
    CHECK(apply_body_back_clearance(
        aliased.output, kBodyIdentity, 0.25F, 1.0F, aliased));
    CHECK(same_result_bits(aliased, expected));
    return true;
}

bool zero_weight_is_an_exact_stock_endpoint() {
    auto stock = stock_frame();
    stock[0] = -0.0F;
    stock[1] = -0.0F;
    stock[2] = -0.0F;
    const std::array<float, 3> zero_delta{};
    for (const float weight : {0.0F, -0.0F}) {
        ClearanceResult result = sentinel_result();
        CHECK(apply_body_back_clearance(
            stock, kBodyIdentity, std::numeric_limits<float>::max(), weight, result));
        CHECK(same_bits(result.output, stock));
        CHECK(same_bits(result.body_back_delta, zero_delta));
    }
    return true;
}

bool zero_depth_is_also_an_exact_stock_endpoint() {
    auto stock = stock_frame();
    stock[0] = -0.0F;
    stock[2] = -0.0F;
    const std::array<float, 3> zero_delta{};
    for (const float depth : {0.0F, -0.0F}) {
        ClearanceResult result = sentinel_result();
        CHECK(apply_body_back_clearance(stock, kBodyIdentity, depth, 1.0F, result));
        CHECK(same_bits(result.output, stock));
        CHECK(same_bits(result.body_back_delta, zero_delta));
    }
    return true;
}

bool invalid_stock_channels_are_transactional_failures() {
    const std::array invalid{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    for (std::size_t i = 0; i < 8; ++i) {
        for (const float value : invalid) {
            auto stock = stock_frame();
            stock[i] = value;
            CHECK(rejects_without_writing(stock, kBodyIdentity, 0.25F, 1.0F));
            CHECK(rejects_without_writing(stock, kBodyIdentity, 0.25F, 0.0F));
        }
    }
    return true;
}

bool invalid_stock_quaternions_are_not_repaired() {
    for (const float scalar : {0.0F, 0.5F, 2.0F}) {
        auto stock = stock_frame();
        stock[7] = scalar;
        CHECK(rejects_without_writing(stock, kBodyIdentity, 0.25F, 1.0F));
    }
    return true;
}

bool invalid_body_components_are_transactional_failures() {
    const auto stock = stock_frame();
    const std::array invalid{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    for (std::size_t i = 0; i < 4; ++i) {
        for (const float value : invalid) {
            auto body = kBodyIdentity;
            body[i] = value;
            CHECK(rejects_without_writing(stock, body, 0.25F, 1.0F));
            CHECK(rejects_without_writing(stock, body, 0.25F, 0.0F));
        }
    }
    return true;
}

bool body_quaternion_admission_is_not_widened_by_normalization() {
    const auto stock = stock_frame();
    const std::array<std::array<float, 4>, 6> invalid{{
        {0.0F, 0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 0.0F, 0.5F},
        {0.0F, 0.0F, 0.0F, 2.0F},
        {0.0F, 0.0F, 0.0F, 1.04F}, // Norm admitted, component bound rejects.
        {0.0F, 0.66F, 0.0F, 0.66F}, // Norm below the lower bound.
        {0.0F, 0.75F, 0.0F, 0.75F}, // Norm above the upper bound.
    }};
    for (const auto& body : invalid) {
        CHECK(rejects_without_writing(stock, body, 0.25F, 1.0F));
        CHECK(rejects_without_writing(stock, body, 0.0F, 0.0F));
    }
    return true;
}

bool degenerate_horizontal_headings_fail_even_at_zero_weight() {
    const auto stock = stock_frame();
    const float half_root = std::sqrt(0.5F);
    const std::array<std::array<float, 4>, 2> vertical{{
        {half_root, 0.0F, 0.0F, half_root},
        {-half_root, 0.0F, 0.0F, half_root},
    }};
    for (const auto& body : vertical) {
        CHECK(rejects_without_writing(stock, body, 0.25F, 1.0F));
        CHECK(rejects_without_writing(stock, body, 0.25F, 0.0F));
        CHECK(rejects_without_writing(stock, body, 0.0F, 1.0F));
    }
    constexpr double pi = 3.14159265358979323846;
    const double near_vertical_half_angle = 89.999 * pi / 360.0;
    const std::array<float, 4> near_vertical{
        static_cast<float>(std::sin(near_vertical_half_angle)), 0.0F, 0.0F,
        static_cast<float>(std::cos(near_vertical_half_angle)),
    };
    CHECK(rejects_without_writing(stock, near_vertical, 0.25F, 1.0F));
    const double usable_half_angle = 89.9 * pi / 360.0;
    const std::array<float, 4> steep_but_usable{
        static_cast<float>(std::sin(usable_half_angle)), 0.0F, 0.0F,
        static_cast<float>(std::cos(usable_half_angle)),
    };
    ClearanceResult result{};
    CHECK(apply_body_back_clearance(stock, steep_but_usable, 0.25F, 1.0F, result));
    CHECK(near(result.body_back_delta[2], -0.25F));
    CHECK(unchanged_channels(stock, result));
    return true;
}

bool invalid_depths_and_weights_never_write() {
    const auto stock = stock_frame();
    const std::array invalid_depth{
        -0.01F, -1.0F,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    for (const float depth : invalid_depth) {
        CHECK(rejects_without_writing(stock, kBodyIdentity, depth, 1.0F));
        CHECK(rejects_without_writing(stock, kBodyIdentity, depth, 0.0F));
    }
    const std::array invalid_weight{
        -0.01F, 1.000001F, 2.0F,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    for (const float weight : invalid_weight) {
        CHECK(rejects_without_writing(stock, kBodyIdentity, 0.25F, weight));
        CHECK(rejects_without_writing(stock, kBodyIdentity, 0.0F, weight));
    }
    return true;
}

bool translated_float_overflow_is_transactional() {
    auto stock = stock_frame();
    stock[2] = -std::numeric_limits<float>::max();
    CHECK(rejects_without_writing(
        stock, kBodyIdentity, std::numeric_limits<float>::max(), 1.0F));
    stock[2] = std::numeric_limits<float>::max();
    const std::array<float, 4> reversed{0.0F, 1.0F, 0.0F, 0.0F};
    CHECK(rejects_without_writing(
        stock, reversed, std::numeric_limits<float>::max(), 1.0F));
    const float half_root = std::sqrt(0.5F);
    const std::array<float, 4> right{0.0F, half_root, 0.0F, half_root};
    stock = stock_frame();
    stock[0] = -std::numeric_limits<float>::max();
    CHECK(rejects_without_writing(
        stock, right, std::numeric_limits<float>::max(), 1.0F));
    return true;
}

bool alias_failure_leaves_the_complete_result_unchanged() {
    ClearanceResult result = sentinel_result();
    const auto stock = stock_frame();
    std::memcpy(result.output.data(), stock.data(), sizeof(stock));
    result.output[2] = -std::numeric_limits<float>::max();
    const ClearanceResult before = result;
    CHECK(!apply_body_back_clearance(
        result.output, kBodyIdentity, std::numeric_limits<float>::max(), 1.0F, result));
    CHECK(same_result_bits(result, before));
    return true;
}

bool only_the_exact_type95_authored_profile_selects_a_distance() {
    using namespace coop::nadine_back;
    const auto profile = select_authored_clearance_profile(
        0x27617D0Du, 0xA99E5C9Fu, kType95PrimaryBindingBits);
    CHECK(profile.id == ClearanceProfileId::kType95Primary);
    CHECK(profile.depth_meters == 0.136F);
    for (const std::uint32_t art : {
             0u, 0x33E29A1Bu, 0x8075397Du, 0x3019CBEEu,
             0xC3181EBDu, 0xC14CB0B7u, 0x354646B8u}) {
        const auto other = select_authored_clearance_profile(
            art, 0xA99E5C9Fu, kType95PrimaryBindingBits);
        CHECK(other.id == ClearanceProfileId::kNone);
        CHECK(other.depth_meters == 0.0F);
    }
    for (std::size_t i = 0; i < kType95PrimaryBindingBits.size(); ++i) {
        auto changed = kType95PrimaryBindingBits;
        changed[i] ^= 1u;
        const auto other = select_authored_clearance_profile(
            0x27617D0Du, 0xA99E5C9Fu, changed);
        CHECK(other.id == ClearanceProfileId::kNone);
        CHECK(other.depth_meters == 0.0F);
    }
    for (const std::uint32_t model : {0u, 0xA99E5C9Eu, 0x3D6F8C72u}) {
        const auto other = select_authored_clearance_profile(
            0x27617D0Du, model, kType95PrimaryBindingBits);
        CHECK(other.id == ClearanceProfileId::kNone);
        CHECK(other.depth_meters == 0.0F);
    }
    return true;
}

bool a_different_type95_primary_does_not_inherit_the_candidate() {
    using namespace coop::nadine_back;
    constexpr std::array<std::uint32_t, 8> p1{
        0x3D9BA5E3u, 0xBCA3D70Au, 0xBD4CCCCDu, 0u,
        0xBD6477B3u, 0xBDCD7209u, 0x3E0A06FBu, 0x3F7BF4C4u};
    const auto profile = select_authored_clearance_profile(
        0x27617D0Du, 0xA99E5C9Fu, p1);
    CHECK(profile.id == ClearanceProfileId::kNone);
    CHECK(profile.depth_meters == 0.0F);
    return true;
}

bool m30_requires_its_own_ruger_model_and_primary() {
    using namespace coop::nadine_back;
    const auto profile = select_authored_clearance_profile(
        0x3019CBEEu, 0x3D6F8C72u, kM30RugerPrimaryBindingBits);
    CHECK(profile.id == ClearanceProfileId::kM30RugerPrimary);
    CHECK(profile.depth_meters == 0.063F);
    for (const std::uint32_t model : {0u, 0xCA03E178u, 0xA99E5C9Fu}) {
        CHECK(select_authored_clearance_profile(
                  0x3019CBEEu, model, kM30RugerPrimaryBindingBits).id ==
              ClearanceProfileId::kNone);
    }
    for (const std::uint32_t art : {0u, 0x33E29A1Bu, 0x8075397Du, 0xC14CB0B7u}) {
        CHECK(select_authored_clearance_profile(
                  art, 0x3D6F8C72u, kM30RugerPrimaryBindingBits).id ==
              ClearanceProfileId::kNone);
    }
    for (std::size_t i = 0; i < kM30RugerPrimaryBindingBits.size(); ++i) {
        auto changed = kM30RugerPrimaryBindingBits;
        changed[i] ^= 1u;
        CHECK(select_authored_clearance_profile(0x3019CBEEu, 0x3D6F8C72u, changed).id ==
              ClearanceProfileId::kNone);
    }
    ClearanceResult result{};
    CHECK(apply_body_back_clearance(
        stock_frame(), kBodyIdentity, profile.depth_meters, 0.5F, result));
    CHECK(near(result.body_back_delta[2], -0.0315F));
    return true;
}

bool secondary_components_fall_back_independently_with_exact_target_bits() {
    using coop::nadine_back::resolve_secondary_binding;
    constexpr auto primary = coop::nadine_back::kM30RugerPrimaryBindingBits;
    constexpr std::array<std::uint32_t, 4> translation{
        0x3E000001u, 0xBD800000u, 0x3DCCCCCDu, 0x80000000u};
    constexpr std::array<std::uint32_t, 4> rotation{
        0x80000000u, 0x3F3504F3u, 0x80000000u, 0x3F3504F3u};
    // Keep one explicit pointer identity for both the table and callback;
    // do not reevaluate a local constexpr array's data() inside a capture.
    const void* const translation_source = translation.data();
    const void* const rotation_source = rotation.data();
    struct Case {
        const void* translation;
        const void* rotation;
        std::array<std::uint32_t, 8> target;
        std::size_t reads;
    };
    const std::array<Case, 4> cases{{
        {nullptr, nullptr, primary, 0},
        {translation_source, nullptr,
         {translation[0], translation[1], translation[2], translation[3],
          primary[4], primary[5], primary[6], primary[7]}, 1},
        {nullptr, rotation_source,
         {primary[0], primary[1], primary[2], primary[3],
          rotation[0], rotation[1], rotation[2], rotation[3]}, 1},
        {translation_source, rotation_source,
         {translation[0], translation[1], translation[2], translation[3],
          rotation[0], rotation[1], rotation[2], rotation[3]}, 2},
    }};
    for (const auto& item : cases) {
        std::array<const void*, 2> sources{};
        std::size_t reads = 0;
        const auto read_component = [&, translation_source, rotation_source](
                                        const void* source, std::uint32_t* words) {
            if (reads >= sources.size()) return false;
            sources[reads++] = source;
            if (source != translation_source && source != rotation_source) {
                std::cerr << "secondary fixture callback: source=" << source
                          << " translation=" << translation_source
                          << " rotation=" << rotation_source
                          << " reads=" << reads << '\n';
                return false;
            }
            std::memcpy(words, source, 4 * sizeof(std::uint32_t));
            return true;
        };
        std::array<std::uint32_t, 8> output{};
        output.fill(0xDEADC0DEu);
        const bool resolved = resolve_secondary_binding(
            primary, item.translation, item.rotation, read_component, output);
        if (!resolved) {
            std::cerr << "secondary fixture case=" << (&item - cases.data())
                      << " tableT=" << item.translation << " tableQ=" << item.rotation
                      << " reads=" << reads << " expectedReads=" << item.reads << '\n';
        }
        CHECK(resolved);
        CHECK(output == item.target);
        CHECK(reads == item.reads);
        if (item.translation != nullptr) CHECK(sources[0] == item.translation);
        if (item.rotation != nullptr) CHECK(sources[reads - 1] == item.rotation);
        // The native +600 target comparison is word-exact, including signed zero.
        auto different_target = item.target;
        different_target[3] ^= 0x80000000u;
        CHECK(output != different_target);
    }
    return true;
}

bool unreadable_present_secondary_never_falls_back_or_publishes_a_prefix() {
    using coop::nadine_back::resolve_secondary_binding;
    const auto primary = coop::nadine_back::kM30RugerPrimaryBindingBits;
    const auto primary_before = primary;
    constexpr std::array<std::uint32_t, 4> translation{11u, 12u, 13u, 14u};
    constexpr std::array<std::uint32_t, 4> rotation{21u, 22u, 23u, 24u};
    const void* const translation_source = translation.data();
    const void* const rotation_source = rotation.data();
    const unsigned unreadable_tag = 0;
    const void* const unreadable = &unreadable_tag;
    struct Case {
        const void* translation;
        const void* rotation;
        std::size_t reads;
    };
    const std::array<Case, 4> cases{{
        {unreadable, nullptr, 1},
        {nullptr, unreadable, 1},
        {translation_source, unreadable, 2},
        {unreadable, rotation_source, 1},
    }};
    for (const auto& item : cases) {
        std::size_t reads = 0;
        std::size_t failed_reads = 0;
        const auto read_component = [&, translation_source, rotation_source, unreadable](
                                        const void* source, std::uint32_t* words) {
            ++reads;
            if (source == unreadable) {
                ++failed_reads;
                // Even a callback that touched its private destination before
                // reporting failure must not leak those words into output.
                for (std::size_t i = 0; i < 4; ++i) words[i] = 0xBAD0F00Du;
                return false;
            }
            if (source != translation_source && source != rotation_source) {
                std::cerr << "failure fixture callback: source=" << source
                          << " translation=" << translation_source
                          << " rotation=" << rotation_source
                          << " unreadable=" << unreadable << '\n';
                return false;
            }
            std::memcpy(words, source, 4 * sizeof(std::uint32_t));
            return true;
        };
        std::array<std::uint32_t, 8> output{
            0x80000000u, 0x7FC055AAu, 0xDEAD0002u, 0xDEAD0003u,
            0xDEAD0004u, 0xDEAD0005u, 0xDEAD0006u, 0xDEAD0007u};
        const auto before = output;
        CHECK(!resolve_secondary_binding(
            primary, item.translation, item.rotation, read_component, output));
        CHECK(output == before);
        CHECK(primary == primary_before);
        if (reads != item.reads || failed_reads != 1) {
            std::cerr << "failure fixture case=" << (&item - cases.data())
                      << " tableT=" << item.translation << " tableQ=" << item.rotation
                      << " reads=" << reads << " expectedReads=" << item.reads
                      << " failedReads=" << failed_reads << '\n';
        }
        CHECK(reads == item.reads && failed_reads == 1);
    }
    return true;
}

} // namespace

int main() {
    constexpr std::array tests{
        cardinal_yaws_move_behind_the_body,
        accepted_nonunit_body_scales_are_normalized_locally,
        weighted_delta_has_the_requested_horizontal_norm,
        translation_preserves_y_w_and_stock_quaternion_bits,
        repeated_fresh_stock_calls_do_not_accumulate,
        output_may_alias_the_stock_input,
        zero_weight_is_an_exact_stock_endpoint,
        zero_depth_is_also_an_exact_stock_endpoint,
        invalid_stock_channels_are_transactional_failures,
        invalid_stock_quaternions_are_not_repaired,
        invalid_body_components_are_transactional_failures,
        body_quaternion_admission_is_not_widened_by_normalization,
        degenerate_horizontal_headings_fail_even_at_zero_weight,
        invalid_depths_and_weights_never_write,
        translated_float_overflow_is_transactional,
        alias_failure_leaves_the_complete_result_unchanged,
        only_the_exact_type95_authored_profile_selects_a_distance,
        a_different_type95_primary_does_not_inherit_the_candidate,
        m30_requires_its_own_ruger_model_and_primary,
        secondary_components_fall_back_independently_with_exact_target_bits,
        unreadable_present_secondary_never_falls_back_or_publishes_a_prefix,
    };
    unsigned failures = 0;
    for (const auto test : tests) {
        if (!test()) ++failures;
    }
    std::cout << "nadine_back_clearance: " << tests.size() - failures << '/'
              << tests.size() << " cases passed\n";
    return failures == 0 ? 0 : 1;
}
