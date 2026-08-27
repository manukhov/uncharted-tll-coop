#include "nadine_pistol_grip_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <utility>

namespace {

namespace policy = coop::pistol_grip;
using policy::GripRowKind;
using policy::RowEvidence;

std::size_t checks = 0;

// These checks execute in Release/NDEBUG too; do not replace them with assert.
#define CHECK(condition)                                                        \
    do {                                                                        \
        ++checks;                                                               \
        if (!(condition)) {                                                     \
            std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n";   \
            return false;                                                       \
        }                                                                       \
    } while (false)

constexpr RowEvidence chloe_row(std::uint32_t art, std::uint32_t source,
                               std::uint32_t target) {
    RowEvidence result{};
    result.art_id = art;
    result.set_id = 0x4C9FB65Cu;
    result.selector = 0x3F334972u;
    result.source = source;
    result.target = target;
    result.type = 1;
    return result;
}

constexpr RowEvidence burst_row(std::uint32_t source, std::uint32_t target) {
    RowEvidence result = chloe_row(0xB6B0DD17u, source, target);
    result.set_id = 0x926FDDA0u;
    result.selector = 0xB6B0DD17u;
    return result;
}

constexpr std::array<RowEvidence, 6> accepted_rows{
    chloe_row(0x8075397Du, 0xD350A452u, 0xB8FC6B25u),
    chloe_row(0xB6B0DD17u, 0xD350A452u, 0xB8FC6B25u),
    chloe_row(0x8075397Du, 0x9DCD31BCu, 0x68238D0Bu),
    chloe_row(0xB6B0DD17u, 0x9DCD31BCu, 0x68238D0Bu),
    burst_row(0x9A64704Fu, 0xB8FC6B25u),
    burst_row(0x522E6370u, 0x1CB03721u),
};

bool exact_supported_arts_and_sets() {
    CHECK(policy::kP99ArtId == 0x8075397Du);
    CHECK(policy::kBurstPistolArtId == 0xB6B0DD17u);
    CHECK(policy::supported_art(0x8075397Du));
    CHECK(policy::supported_art(0xB6B0DD17u));
    CHECK(policy::expected_weapon_set(0x8075397Du) == 0x43DC560Cu);
    CHECK(policy::expected_weapon_set(0xB6B0DD17u) == 0x926FDDA0u);
    for (const std::uint32_t art : {0u, 0xFFFFFFFFu, 0x33E29A1Bu,
                                  0xC938AAA1u, 0x27617D0Du}) {
        CHECK(!policy::supported_art(art));
        CHECK(policy::expected_weapon_set(art) == 0);
    }
    return true;
}

bool exact_right_and_burst_rows() {
    constexpr std::array<GripRowKind, 6> expected{
        GripRowKind::kPistolRightHand, GripRowKind::kPistolRightHand,
        GripRowKind::kPistolAimHands, GripRowKind::kPistolAimHands,
        GripRowKind::kBurstRightHand, GripRowKind::kBurstAimHands,
    };
    for (std::size_t i = 0; i < accepted_rows.size(); ++i) {
        CHECK(policy::classify_row(accepted_rows[i]) == expected[i]);
    }
    return true;
}

bool all_72_asset_aim_rows_for_both_arts() {
    // Independent expected fixture transcribed from the asset audit, not a
    // loop over the policy's own list (which could hide an omitted source).
    constexpr std::array<std::uint32_t, 72> expected{
        0x9DCD31BCu, 0x6B59254Eu, 0x418147E1u, 0x311E3C9Bu,
        0x1E4917C2u, 0x5DF463F2u, 0x6ED66CB8u, 0x9EEDC65Au,
        0x2E7D04E7u, 0x781DFA12u, 0x08828168u, 0x27D5AA31u,
        0x1D147D10u, 0x574AD14Bu, 0xDE0DD8B8u, 0x79A1F334u,
        0x18E4B240u, 0xAA3393F5u, 0x3B7C43EFu, 0xC5AECB10u,
        0x7779EAA5u, 0x31561847u, 0x391F7D3Bu, 0x8BC85C8Eu,
        0xE549AED2u, 0x26E403E0u, 0x94332255u, 0x95D6D5A8u,
        0xE8F5D295u, 0x5A22F320u, 0x64686F76u, 0xC49B9633u,
        0x764CB786u, 0xAE4A7F9Cu, 0x75D2BA1Cu, 0xC7059BA9u,
        0xE47CF3F1u, 0xEAA9CFF3u, 0x587EEE46u, 0x94E3888Bu,
        0x24B81E86u, 0x966F3F33u, 0xB658CFD2u, 0x97D7BF83u,
        0x25009E36u, 0x7CC7C621u, 0x4C9A4FECu, 0xFE4D6E59u,
        0xA9FC650Eu, 0xD6EA3A24u, 0x643D1B91u, 0x4C94252Du,
        0x8D13BF50u, 0x3FC49EE5u, 0xF60D5FE6u, 0x841C9342u,
        0x36CBB2F7u, 0x8692249Cu, 0x4A0D4237u, 0xF8DA6382u,
        0x772C9E42u, 0x66630691u, 0xD4B42724u, 0x1A46BDF7u,
        0x481DDA1Cu, 0xFACAFBA9u, 0x46846353u, 0xC03624B0u,
        0x72E10505u, 0x361B1829u, 0x0E27F5C5u, 0xBCF0D470u,
    };
    CHECK(expected == policy::kPistolAimSources);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        for (std::size_t j = i + 1; j < expected.size(); ++j) {
            CHECK(expected[i] != expected[j]);
        }
        for (const std::uint32_t art : {0x8075397Du, 0xB6B0DD17u}) {
            CHECK(policy::classify_row(chloe_row(art, expected[i], 0x68238D0Bu)) ==
                  GripRowKind::kPistolAimHands);
        }
    }
    return true;
}

bool reject_other_arts_including_mazur() {
    for (const RowEvidence& original : accepted_rows) {
        for (const std::uint32_t art : {0u, 0xFFFFFFFFu, 0x33E29A1Bu,
                                      0xC938AAA1u, 0x27617D0Du}) {
            RowEvidence row = original;
            row.art_id = art;
            CHECK(policy::classify_row(row) == GripRowKind::kNone);
        }
    }
    return true;
}

bool reject_wrong_set_selector_source_or_target() {
    for (const RowEvidence& original : accepted_rows) {
        for (const std::uint32_t value : {0u, 0xFFFFFFFFu}) {
            RowEvidence row = original;
            row.set_id = value;
            CHECK(policy::classify_row(row) == GripRowKind::kNone);
            row = original;
            row.selector = value;
            CHECK(policy::classify_row(row) == GripRowKind::kNone);
            row = original;
            row.source = value;
            CHECK(policy::classify_row(row) == GripRowKind::kNone);
            row = original;
            row.target = value;
            CHECK(policy::classify_row(row) == GripRowKind::kNone);
        }
        RowEvidence row = original;
        row.set_id ^= 1u;
        CHECK(policy::classify_row(row) == GripRowKind::kNone);
        row = original;
        row.selector ^= 1u;
        CHECK(policy::classify_row(row) == GripRowKind::kNone);
        row = original;
        row.source ^= 1u;
        CHECK(policy::classify_row(row) == GripRowKind::kNone);
        row = original;
        row.target ^= 1u;
        CHECK(policy::classify_row(row) == GripRowKind::kNone);
    }
    return true;
}

bool reject_every_nonzero_payload_bit() {
    for (const RowEvidence& original : accepted_rows) {
        for (std::size_t word = 0; word < original.payload.size(); ++word) {
            for (unsigned bit = 0; bit < 32; ++bit) {
                RowEvidence row = original;
                row.payload[word] = std::uint32_t{1} << bit;
                CHECK(policy::classify_row(row) == GripRowKind::kNone);
            }
        }
    }
    return true;
}

bool reject_every_other_type_and_nonzero_byte25() {
    for (const RowEvidence& original : accepted_rows) {
        for (unsigned value = 0; value <= 255; ++value) {
            RowEvidence row = original;
            row.type = static_cast<std::uint8_t>(value);
            if (value != 1) {
                CHECK(policy::classify_row(row) == GripRowKind::kNone);
            }
            row = original;
            row.byte25 = static_cast<std::uint8_t>(value);
            if (value != 0) {
                CHECK(policy::classify_row(row) == GripRowKind::kNone);
            }
        }
    }
    return true;
}

bool reject_every_nonzero_flag_bit() {
    for (const RowEvidence& original : accepted_rows) {
        for (unsigned bit = 0; bit < 16; ++bit) {
            RowEvidence row = original;
            row.flags = static_cast<std::uint16_t>(1u << bit);
            CHECK(policy::classify_row(row) == GripRowKind::kNone);
        }
    }
    return true;
}

bool reject_non_pose_cursor_and_cursor_flags() {
    for (const RowEvidence& original : accepted_rows) {
        for (unsigned value = 1; value <= 255; ++value) {
            RowEvidence row = original;
            row.cursor_kind = static_cast<std::uint8_t>(value);
            CHECK(policy::classify_row(row) == GripRowKind::kNone);
            row = original;
            row.cursor_flags = static_cast<std::uint8_t>(value);
            CHECK(policy::classify_row(row) == GripRowKind::kNone);
        }
    }
    return true;
}

bool preserve_authored_type0_and_transform_state_additives() {
    RowEvidence row = burst_row(0xD350A452u, 0x9A64704Fu);
    row.type = 0; // Exact type-0 row 35EB8: keep Colt's authored finger pose.
    CHECK(policy::classify_row(row) == GripRowKind::kNone);
    for (const std::uint32_t source : {0x3C347132u, 0xA1B17699u}) {
        row = burst_row(source, 0x522E6370u);
        row.flags = 2; // Exact rows 35F58/36138: TransformState, not this seam.
        row.cursor_kind = 1;
        CHECK(policy::classify_row(row) == GripRowKind::kNone);
    }
    return true;
}

bool never_confuse_art_specific_and_mesh_sets() {
    for (const RowEvidence& original : {accepted_rows[4], accepted_rows[5]}) {
        RowEvidence row = original;
        row.art_id = 0x8075397Du;
        CHECK(policy::classify_row(row) == GripRowKind::kNone);
        row = original;
        row.set_id = 0x4C9FB65Cu;
        row.selector = 0x3F334972u;
        CHECK(policy::classify_row(row) == GripRowKind::kNone);
    }
    for (const std::uint32_t art : {0x8075397Du, 0xB6B0DD17u}) {
        RowEvidence row = chloe_row(art, 0xD350A452u, 0xB8FC6B25u);
        row.set_id = policy::expected_weapon_set(art);
        row.selector = art;
        CHECK(policy::classify_row(row) == GripRowKind::kNone);
        row = chloe_row(art, 0x9DCD31BCu, 0x68238D0Bu);
        row.set_id = 0x6567DEA7u; // Actual vill-Nadine set, not its selector.
        row.selector = 0x41666B5Au;
        CHECK(policy::classify_row(row) == GripRowKind::kNone);
    }
    return true;
}

bool shared_targets_do_not_grant_provenance() {
    // These sources reach shared Chloe grips in other weapon families. A final
    // B8FC6B25/68238D0B pointer must never be used as sufficient evidence.
    for (const std::uint32_t art : {0x8075397Du, 0xB6B0DD17u}) {
        for (const std::uint32_t source : {0x0A2F8976u, 0xEBEFDD92u,
                                         0x0166117Bu}) {
            CHECK(policy::classify_row(chloe_row(art, source, 0xB8FC6B25u)) ==
                  GripRowKind::kNone);
            CHECK(policy::classify_row(chloe_row(art, source, 0x68238D0Bu)) ==
                  GripRowKind::kNone);
        }
    }
    return true;
}

bool constexpr_contract_and_diagnostic_names() {
    constexpr auto right = policy::classify_row(accepted_rows[0]);
    constexpr auto burst = policy::classify_row(accepted_rows[4]);
    constexpr auto rejected = policy::classify_row(RowEvidence{});
    CHECK(right == GripRowKind::kPistolRightHand);
    CHECK(burst == GripRowKind::kBurstRightHand);
    CHECK(rejected == GripRowKind::kNone);
    CHECK(std::strcmp(policy::row_kind_name(right), "pistol-right-hand") == 0);
    CHECK(std::strcmp(policy::row_kind_name(burst), "burst-right-hand") == 0);
    CHECK(std::strcmp(policy::row_kind_name(GripRowKind::kBurstAimHands),
                      "burst-aim-hands") == 0);
    CHECK(std::strcmp(policy::row_kind_name(GripRowKind::kPistolAimHands),
                      "pistol-aim-hands") == 0);
    CHECK(std::strcmp(policy::row_kind_name(GripRowKind::kNone), "none") == 0);
    CHECK(std::strcmp(policy::row_kind_name(static_cast<GripRowKind>(255)),
                      "none") == 0);
    return true;
}

} // namespace

int main() {
    const std::array<std::pair<const char*, bool (*)()>, 13> tests{{
        {"exact arts and weapon sets", exact_supported_arts_and_sets},
        {"right-hand and Colt rows", exact_right_and_burst_rows},
        {"all 72 aim rows for both arts", all_72_asset_aim_rows_for_both_arts},
        {"other arts and Mazur", reject_other_arts_including_mazur},
        {"set/selector/source/target guards", reject_wrong_set_selector_source_or_target},
        {"every payload bit", reject_every_nonzero_payload_bit},
        {"all other types and byte25", reject_every_other_type_and_nonzero_byte25},
        {"every flags bit", reject_every_nonzero_flag_bit},
        {"pose cursor and flags", reject_non_pose_cursor_and_cursor_flags},
        {"keep type0 and TransformState rows", preserve_authored_type0_and_transform_state_additives},
        {"keep set and art provenance", never_confuse_art_specific_and_mesh_sets},
        {"shared target is insufficient", shared_targets_do_not_grant_provenance},
        {"constexpr contract and names", constexpr_contract_and_diagnostic_names},
    }};
    std::size_t passed = 0;
    for (const auto& [name, run] : tests) {
        if (!run()) {
            std::cerr << "FAIL: " << name << '\n';
            return 1;
        }
        ++passed;
    }
    std::cout << passed << '/' << tests.size() << " tests passed; " << checks
              << " checks (Release-safe)\n";
    return 0;
}
