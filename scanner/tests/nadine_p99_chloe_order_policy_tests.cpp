#include "nadine_p99_chloe_order_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {

namespace policy = coop::p99_chloe_order;
using policy::Decision;
using policy::PoseValidEvidence;
using policy::RejectReason;
using policy::RetargetEntryMetadata;
using policy::RetargetRecordMetadata;
using policy::SourcePoseKind;
using policy::SourcePoseLayoutEvidence;

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

struct Group {
    std::uint16_t first;
    std::uint16_t count;
};

// Independent fixture transcription from the three skeleton objects.  Do not
// build the positive fixture from the policy's own tables: that would let an
// omitted or mistyped group validate itself.
constexpr std::array<Group, 9> player_groups{{
    {0, 168}, {168, 63}, {231, 76}, {307, 83}, {390, 107},
    {497, 52}, {549, 90}, {639, 38}, {677, 25},
}};
constexpr std::array<Group, 8> chloe_groups{{
    {0, 205}, {205, 62}, {267, 38}, {305, 72},
    {377, 83}, {460, 107}, {567, 59}, {626, 25},
}};
constexpr std::array<Group, 8> nadine_groups{{
    {0, 104}, {104, 38}, {142, 119}, {261, 83},
    {344, 107}, {451, 52}, {503, 52}, {555, 25},
}};

struct FixtureTransfer {
    std::uint32_t semantic_id;
    std::uint16_t player;
    std::uint16_t chloe;
    std::uint16_t nadine;
    std::uint8_t player_group;
    std::uint8_t chloe_group;
    std::uint8_t nadine_group;
    std::uint8_t mode;
};

constexpr std::array<FixtureTransfer, 21> fixture_transfers{{
    {0xB42DF599u, 30, 39, 28, 0, 0, 0, 1},
    {0x05FC3888u, 32, 41, 30, 0, 0, 0, 3},
    {0xD38C382Du, 640, 268, 105, 7, 2, 1, 1},
    {0xDECF1EF4u, 642, 270, 107, 7, 2, 1, 1},
    {0xDA0E0343u, 644, 272, 109, 7, 2, 1, 1},
    {0xC4495346u, 646, 274, 111, 7, 2, 1, 1},
    {0x74890107u, 648, 276, 137, 7, 2, 1, 1},
    {0x79CA27DEu, 650, 278, 139, 7, 2, 1, 1},
    {0x7D0B3A69u, 652, 280, 141, 7, 2, 1, 1},
    {0xAAE09A7Du, 654, 282, 113, 7, 2, 1, 1},
    {0x69506AC1u, 656, 284, 115, 7, 2, 1, 1},
    {0x64134C18u, 658, 286, 117, 7, 2, 1, 1},
    {0x60D251AFu, 660, 288, 119, 7, 2, 1, 1},
    {0x715AB839u, 662, 290, 121, 7, 2, 1, 1},
    {0xB38B9BE7u, 664, 292, 123, 7, 2, 1, 1},
    {0xBEC8BD3Eu, 666, 294, 125, 7, 2, 1, 1},
    {0xBA09A089u, 668, 296, 127, 7, 2, 1, 1},
    {0xFB0C60CDu, 670, 298, 129, 7, 2, 1, 1},
    {0xC1B94193u, 672, 300, 131, 7, 2, 1, 1},
    {0xCCFA674Au, 674, 302, 133, 7, 2, 1, 1},
    {0xC83B7AFDu, 676, 304, 135, 7, 2, 1, 1},
}};

constexpr std::array<std::uint16_t, 2> fixture_g0_targets{28, 30};
constexpr std::array<std::uint16_t, 19> fixture_finger_targets{
    105, 107, 109, 111, 113, 115, 117, 119, 121, 123,
    125, 127, 129, 131, 133, 135, 137, 139, 141,
};

// Independent transcription of the live source-evaluation descriptors.  The
// larger workspace counts are intentionally different from the processing
// row counts for both group-zero clips.
constexpr std::array<SourcePoseLayoutEvidence, 2> fixture_player_layouts{{
    {SourcePoseKind::kPlayerBase, 0, 0, true, true, true,
     0x40, 0x160, 0, 0, 168, 71, 71, 27, 2, 0, 0, 0},
    {SourcePoseKind::kPlayerBase, 1, 7, true, true, true,
     0x40, 0x160, 639, 7, 38, 38, 38, 0, 1, 0, 435, 0},
}};
constexpr std::array<SourcePoseLayoutEvidence, 2> fixture_chloe_layouts{{
    {SourcePoseKind::kChloeAdditive, 0, 0, true, true, true,
     0x40, 0x140, 0, 0, 205, 89, 89, 27, 2, 0, 0, 0},
    {SourcePoseKind::kChloeAdditive, 1, 2, true, true, true,
     0x40, 0x140, 267, 2, 38, 38, 38, 0, 1, 0, 90, 0},
}};

template <std::size_t N>
int group_for(const std::array<Group, N>& groups, std::uint16_t global) {
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const std::uint32_t end =
            static_cast<std::uint32_t>(groups[i].first) + groups[i].count;
        if (global >= groups[i].first && global < end) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

template <std::size_t N>
std::uint16_t flattened_index_for(
    const std::array<Group, N>& groups, std::uint16_t global) {
    std::uint32_t prefix = 0;
    for (const Group& group : groups) {
        if (global >= group.first &&
            global < static_cast<std::uint32_t>(group.first) + group.count) {
            return static_cast<std::uint16_t>(
                prefix + static_cast<std::uint32_t>(global - group.first));
        }
        prefix += group.count;
    }
    return 0xFFFFu;
}

template <std::size_t SourceN, std::size_t TargetN>
RetargetRecordMetadata record(
    std::uint16_t source,
    std::uint16_t target,
    std::uint8_t mode,
    const std::array<Group, SourceN>& sources,
    const std::array<Group, TargetN>& targets) {
    const int source_group = group_for(sources, source);
    const int target_group = group_for(targets, target);
    RetargetRecordMetadata result{};
    result.source_global = source;
    result.source_group = static_cast<std::uint8_t>(source_group);
    result.source_parameter_index = flattened_index_for(sources, source);
    result.target_global = target;
    result.target_group = static_cast<std::uint8_t>(target_group);
    result.target_reference_index = flattened_index_for(targets, target);
    result.mode = mode;
    return result;
}

bool fixture_player_endpoint(std::uint16_t value) {
    for (const FixtureTransfer& item : fixture_transfers) {
        if (item.player == value) {
            return true;
        }
    }
    return false;
}

bool fixture_chloe_endpoint(std::uint16_t value) {
    for (const FixtureTransfer& item : fixture_transfers) {
        if (item.chloe == value) {
            return true;
        }
    }
    return false;
}

bool fixture_nadine_endpoint(std::uint16_t value) {
    for (const FixtureTransfer& item : fixture_transfers) {
        if (item.nadine == value) {
            return true;
        }
    }
    return false;
}

void set_bit(
    std::array<std::uint64_t, policy::kGlobalMaskWordCount>& words,
    std::uint16_t global) {
    words[global / 64] |= std::uint64_t{1} << (global % 64);
}

struct Fixture {
    std::array<RetargetRecordMetadata, 93> row20_records{};
    std::array<RetargetRecordMetadata, 71> row157_records{};
    std::array<std::uint8_t, 2> player_mask_groups{0, 7};
    std::array<std::uint8_t, 2> additive_mask_groups{0, 2};
    std::array<std::uint8_t, 2> composite_mask_groups{0, 2};
    std::array<std::uint8_t, 2> nadine_mask_groups{0, 1};
    std::array<SourcePoseLayoutEvidence, 2> player_layouts{
        fixture_player_layouts};
    std::array<SourcePoseLayoutEvidence, 2> chloe_layouts{
        fixture_chloe_layouts};
    policy::TransferEvidence evidence{};

    Fixture() {
        for (std::size_t i = 0; i < fixture_transfers.size(); ++i) {
            const FixtureTransfer& item = fixture_transfers[i];
            row20_records[i] = record(
                item.player, item.chloe, item.mode, player_groups, chloe_groups);
            row157_records[i] = record(
                item.chloe, item.nadine, item.mode, chloe_groups, nadine_groups);
        }

        // Structurally valid, unique non-grip filler makes both spans equal the
        // exact 93/71 resolved runtime counts without deriving acceptance from
        // policy
        // constants.  Filler never aliases a required source/middle/target.
        std::size_t next = fixture_transfers.size();
        for (std::uint16_t candidate = 0; next < row20_records.size();
             ++candidate) {
            if (fixture_player_endpoint(candidate) ||
                fixture_chloe_endpoint(candidate)) {
                continue;
            }
            row20_records[next++] = record(
                candidate, candidate, 0, player_groups, chloe_groups);
        }
        next = fixture_transfers.size();
        for (std::uint16_t candidate = 0; next < row157_records.size();
             ++candidate) {
            if (fixture_chloe_endpoint(candidate) ||
                fixture_nadine_endpoint(candidate)) {
                continue;
            }
            row157_records[next++] = record(
                candidate, candidate, 0, chloe_groups, nadine_groups);
        }

        evidence.scope = {
            true, true, true, 0, 0x8075397Du, 2u,
            0xD350A452u, 0xB8FC6B25u, 2u, 0x3F800000u,
            false, true, false,
        };
        evidence.row20.metadata = {
            0xFE014FD2u, 0x8E53B36Eu, 20, 0x2E, 0x410u, 0x2124u, 96, 93,
            0x1003D654u, 0xB70B7EAFu,
            0x27E57F43u, 0xFD6DBE03u, 0x6C2B2396u, 0x4B0F5DE3u,
            702, 651, 9, 8, 0u, 0x3F6BE76Du, 0u, 0, 0,
        };
        evidence.row157.metadata = {
            0xFE014FD2u, 0x8E53B36Eu, 157, 0x2E, 0x1DC0u, 0x2720u, 74, 71,
            0xB70B7EAFu, 0xC67A06F1u,
            0x6C2B2396u, 0x4B0F5DE3u, 0xED25C708u, 0xC39CA709u,
            651, 580, 8, 8, 0xFC34096Cu, 0x3F770A3Du, 0u, 0, 0,
        };
        evidence.row20.records = row20_records;
        evidence.row157.records = row157_records;
        evidence.player_source_layouts = player_layouts;
        evidence.chloe_source_layouts = chloe_layouts;

        evidence.player_base.skeleton_joint_count = 702;
        evidence.player_base.groups = player_mask_groups;
        evidence.chloe_additive.skeleton_joint_count = 651;
        evidence.chloe_additive.groups = additive_mask_groups;
        evidence.chloe_composite.skeleton_joint_count = 651;
        evidence.chloe_composite.groups = composite_mask_groups;
        evidence.nadine_output.skeleton_joint_count = 580;
        evidence.nadine_output.groups = nadine_mask_groups;
        for (const FixtureTransfer& item : fixture_transfers) {
            set_bit(evidence.player_base.global_lsb_bits, item.player);
            set_bit(evidence.chloe_additive.global_lsb_bits, item.chloe);
            set_bit(evidence.chloe_composite.global_lsb_bits, item.chloe);
            set_bit(evidence.nadine_output.global_lsb_bits, item.nadine);
        }
    }
};

Decision decide(const Fixture& fixture) {
    return policy::evaluate(fixture.evidence);
}

template <std::size_t N>
bool fixture_pairs_are_unique(
    const std::array<RetargetRecordMetadata, N>& records) {
    for (std::size_t i = 0; i < records.size(); ++i) {
        for (std::size_t j = i + 1; j < records.size(); ++j) {
            if (records[i].source_global == records[j].source_global &&
                records[i].target_global == records[j].target_global) {
                return false;
            }
        }
    }
    return true;
}

bool exact_asset_constants_and_target_inventory() {
    CHECK(policy::kExpectedTransfers.size() == fixture_transfers.size());
    CHECK(policy::kExpectedNadineGroup0Targets == fixture_g0_targets);
    CHECK(policy::kExpectedNadineRightFingerTargets == fixture_finger_targets);
    CHECK(policy::kExpectedNadineRightFingerTargets.size() == 19);
    CHECK(policy::kExpectedRow20.entry_index == 20);
    CHECK(policy::kExpectedRow20.authored_row_offset == 0x410u);
    CHECK(policy::kExpectedRow20.authored_map_offset == 0x2124u);
    CHECK(policy::kExpectedRow20.authored_record_count == 96);
    CHECK(policy::kExpectedRow20.runtime_record_count == 93);
    CHECK(policy::kExpectedRow20.authored_record_count -
              policy::kExpectedRow20.runtime_record_count ==
          3);
    CHECK(policy::kExpectedRow20.authored_source_base_id == 0x1003D654u);
    CHECK(policy::kExpectedRow20.authored_target_base_id == 0xB70B7EAFu);
    CHECK(policy::kExpectedRow20.source_database_id == 0x27E57F43u);
    CHECK(policy::kExpectedRow20.target_database_id == 0x6C2B2396u);
    CHECK(policy::kExpectedRow20.source_skeleton_id == 0xFD6DBE03u);
    CHECK(policy::kExpectedRow20.target_skeleton_id == 0x4B0F5DE3u);
    CHECK(policy::kExpectedRow20.scale_bits == 0x3F6BE76Du);
    CHECK(policy::kExpectedRow157.entry_index == 157);
    CHECK(policy::kExpectedRow157.authored_row_offset == 0x1DC0u);
    CHECK(policy::kExpectedRow157.authored_map_offset == 0x2720u);
    CHECK(policy::kExpectedRow157.authored_record_count == 74);
    CHECK(policy::kExpectedRow157.runtime_record_count == 71);
    CHECK(policy::kExpectedRow157.authored_record_count -
              policy::kExpectedRow157.runtime_record_count ==
          3);
    CHECK(policy::kExpectedRow157.authored_source_base_id == 0xB70B7EAFu);
    CHECK(policy::kExpectedRow157.authored_target_base_id == 0xC67A06F1u);
    CHECK(policy::kExpectedRow157.source_database_id == 0x6C2B2396u);
    CHECK(policy::kExpectedRow157.target_database_id == 0xED25C708u);
    CHECK(policy::kExpectedRow157.source_skeleton_id == 0x4B0F5DE3u);
    CHECK(policy::kExpectedRow157.target_skeleton_id == 0xC39CA709u);
    CHECK(policy::kExpectedRow157.profile_id == 0xFC34096Cu);
    CHECK(policy::kExpectedRow157.scale_bits == 0x3F770A3Du);

    const auto check_layout = [](const SourcePoseLayoutEvidence& actual,
                                 const SourcePoseLayoutEvidence& expected) {
        return actual.kind == expected.kind &&
            actual.target_group == expected.target_group &&
            actual.source_group == expected.source_group &&
            actual.group_pointer_exact == expected.group_pointer_exact &&
            actual.processing_pointer_exact ==
                expected.processing_pointer_exact &&
            actual.stable == expected.stable &&
            actual.group_table_offset == expected.group_table_offset &&
            actual.processing_table_offset ==
                expected.processing_table_offset &&
            actual.group_first == expected.group_first &&
            actual.processing_descriptor_index ==
                expected.processing_descriptor_index &&
            actual.workspace_count == expected.workspace_count &&
            actual.parameter_count == expected.parameter_count &&
            actual.processing_joint_count ==
                expected.processing_joint_count &&
            actual.processing_scalar_count ==
                expected.processing_scalar_count &&
            actual.processing_lane_count == expected.processing_lane_count &&
            actual.processing_byte5 == expected.processing_byte5 &&
            actual.processing_first_parameter ==
                expected.processing_first_parameter &&
            actual.processing_word8 == expected.processing_word8;
    };
    for (std::size_t i = 0; i < fixture_player_layouts.size(); ++i) {
        CHECK(check_layout(
            policy::kExpectedPlayerSourceLayouts[i],
            fixture_player_layouts[i]));
        CHECK(check_layout(
            policy::kExpectedChloeSourceLayouts[i],
            fixture_chloe_layouts[i]));
    }

    for (std::size_t i = 0; i < fixture_transfers.size(); ++i) {
        const auto& actual = policy::kExpectedTransfers[i];
        const auto& expected = fixture_transfers[i];
        CHECK(actual.semantic_id == expected.semantic_id);
        CHECK(actual.player_global == expected.player);
        CHECK(actual.chloe_global == expected.chloe);
        CHECK(actual.nadine_global == expected.nadine);
        CHECK(actual.player_group == expected.player_group);
        CHECK(actual.chloe_group == expected.chloe_group);
        CHECK(actual.nadine_group == expected.nadine_group);
        CHECK(actual.mode == expected.mode);
        for (std::size_t j = i + 1; j < fixture_transfers.size(); ++j) {
            CHECK(actual.semantic_id != policy::kExpectedTransfers[j].semantic_id);
            CHECK(actual.player_global != policy::kExpectedTransfers[j].player_global);
            CHECK(actual.chloe_global != policy::kExpectedTransfers[j].chloe_global);
            CHECK(actual.nadine_global != policy::kExpectedTransfers[j].nadine_global);
        }
    }
    return true;
}

bool source_pose_layout_is_exact_and_fail_closed() {
    {
        Fixture fixture;
        std::swap(fixture.player_layouts[0], fixture.player_layouts[1]);
        std::swap(fixture.chloe_layouts[0], fixture.chloe_layouts[1]);
        CHECK(decide(fixture).admitted());
    }

    const auto rejects_player = [](std::size_t index, auto mutate) {
        Fixture fixture;
        mutate(fixture.player_layouts[index]);
        return decide(fixture).reason == RejectReason::kSourceLayout;
    };
    const auto rejects_chloe = [](std::size_t index, auto mutate) {
        Fixture fixture;
        mutate(fixture.chloe_layouts[index]);
        return decide(fixture).reason == RejectReason::kSourceLayout;
    };
    for (std::size_t index = 0; index < 2; ++index) {
#define CHECK_LAYOUT_FIELD(field, mutation)                                      \
        CHECK(rejects_player(index, [](auto& value) { value.field mutation; })); \
        CHECK(rejects_chloe(index, [](auto& value) { value.field mutation; }))
        CHECK_LAYOUT_FIELD(kind, = value.kind == SourcePoseKind::kPlayerBase
                                      ? SourcePoseKind::kChloeAdditive
                                      : SourcePoseKind::kPlayerBase);
        CHECK_LAYOUT_FIELD(target_group, ^= 1u);
        CHECK_LAYOUT_FIELD(source_group, ^= 1u);
        CHECK_LAYOUT_FIELD(group_pointer_exact, = false);
        CHECK_LAYOUT_FIELD(processing_pointer_exact, = false);
        CHECK_LAYOUT_FIELD(stable, = false);
        CHECK_LAYOUT_FIELD(group_table_offset, ^= 0x20u);
        CHECK_LAYOUT_FIELD(processing_table_offset, ^= 0x20u);
        CHECK_LAYOUT_FIELD(group_first, ^= 1u);
        CHECK_LAYOUT_FIELD(processing_descriptor_index, ^= 1u);
        CHECK_LAYOUT_FIELD(workspace_count, ^= 1u);
        CHECK_LAYOUT_FIELD(parameter_count, ^= 1u);
        CHECK_LAYOUT_FIELD(processing_joint_count, ^= 1u);
        CHECK_LAYOUT_FIELD(processing_scalar_count, ^= 1u);
        CHECK_LAYOUT_FIELD(processing_lane_count, ^= 1u);
        CHECK_LAYOUT_FIELD(processing_byte5, ^= 1u);
        CHECK_LAYOUT_FIELD(processing_first_parameter, ^= 1u);
        CHECK_LAYOUT_FIELD(processing_word8, ^= 1u);
#undef CHECK_LAYOUT_FIELD
    }
    {
        Fixture fixture;
        fixture.evidence.player_source_layouts =
            fixture.evidence.player_source_layouts.first(1);
        CHECK(decide(fixture).reason == RejectReason::kSourceLayout);
    }
    {
        Fixture fixture;
        fixture.evidence.chloe_source_layouts =
            fixture.evidence.chloe_source_layouts.first(1);
        CHECK(decide(fixture).reason == RejectReason::kSourceLayout);
    }
    {
        Fixture fixture;
        fixture.player_layouts[1] = fixture.player_layouts[0];
        CHECK(decide(fixture).reason == RejectReason::kSourceLayout);
    }
    {
        Fixture fixture;
        fixture.chloe_layouts[1] = fixture.chloe_layouts[0];
        CHECK(decide(fixture).reason == RejectReason::kSourceLayout);
    }
    {
        Fixture fixture;
        const std::array<SourcePoseLayoutEvidence, 3> extra{
            fixture.player_layouts[0], fixture.player_layouts[1],
            fixture.player_layouts[0]};
        fixture.evidence.player_source_layouts = extra;
        CHECK(decide(fixture).reason == RejectReason::kSourceLayout);
    }
    return true;
}

bool exact_fixture_is_admitted() {
    Fixture fixture;
    CHECK(fixture.row20_records.size() == 93);
    CHECK(fixture.row157_records.size() == 71);
    CHECK(fixture_pairs_are_unique(fixture.row20_records));
    CHECK(fixture_pairs_are_unique(fixture.row157_records));
    // These three shipped skeletons have dense, consecutive parameter groups.
    // BuildRetargetEntry therefore stores cumulative +0x26/+0x2a indices equal
    // to the skeleton-global joints, not group-local ordinals.
    for (std::size_t i = 0; i < fixture_transfers.size(); ++i) {
        CHECK(fixture.row20_records[i].source_parameter_index ==
              fixture_transfers[i].player);
        CHECK(fixture.row20_records[i].target_reference_index ==
              fixture_transfers[i].chloe);
        CHECK(fixture.row157_records[i].source_parameter_index ==
              fixture_transfers[i].chloe);
        CHECK(fixture.row157_records[i].target_reference_index ==
              fixture_transfers[i].nadine);
    }
    CHECK(decide(fixture).admitted());

    // Group callback order is not semantic; the exact unique set is.
    std::swap(fixture.player_mask_groups[0], fixture.player_mask_groups[1]);
    std::swap(fixture.additive_mask_groups[0], fixture.additive_mask_groups[1]);
    std::swap(fixture.composite_mask_groups[0], fixture.composite_mask_groups[1]);
    std::swap(fixture.nadine_mask_groups[0], fixture.nadine_mask_groups[1]);
    CHECK(decide(fixture).admitted());
    return true;
}

bool scope_is_exact_and_fail_closed() {
    const auto rejects = [](auto mutate) {
        Fixture fixture;
        mutate(fixture.evidence.scope);
        return policy::evaluate(fixture.evidence).reason == RejectReason::kScope;
    };
    CHECK(rejects([](auto& value) { value.is_role_b = false; }));
    CHECK(rejects([](auto& value) { value.is_retained_local_owner = false; }));
    CHECK(rejects([](auto& value) { value.is_legacy_nadine = false; }));
    CHECK(rejects([](auto& value) { value.owner_index = 1; }));
    CHECK(rejects([](auto& value) { value.art_id ^= 1u; }));
    CHECK(rejects([](auto& value) { value.art_id = 0x33E29A1Bu; })); // Mazur
    CHECK(rejects([](auto& value) { value.weapon_state = 4; })); // ADS
    CHECK(rejects([](auto& value) { value.base_clip_id ^= 1u; }));
    CHECK(rejects([](auto& value) { value.additive_clip_id ^= 1u; }));
    CHECK(rejects([](auto& value) { value.blend_mode = 1; }));
    CHECK(rejects([](auto& value) { value.blend_weight_bits ^= 1u; }));
    CHECK(rejects([](auto& value) { value.base_is_additive = true; }));
    CHECK(rejects([](auto& value) { value.right_is_additive = false; }));
    CHECK(rejects([](auto& value) { value.composite_is_additive = true; }));
    return true;
}

bool exact_entry_identity_and_entry_join() {
    const auto rejects_row20 = [](auto mutate) {
        Fixture fixture;
        mutate(fixture.evidence.row20.metadata);
        return !policy::evaluate(fixture.evidence).admitted();
    };
    const auto rejects_row157 = [](auto mutate) {
        Fixture fixture;
        mutate(fixture.evidence.row157.metadata);
        return !policy::evaluate(fixture.evidence).admitted();
    };

    CHECK(rejects_row20([](auto& v) { v.table_object_id ^= 1u; }));
    CHECK(rejects_row20([](auto& v) { v.table_type_id ^= 1u; }));
    CHECK(rejects_row20([](auto& v) { ++v.entry_index; }));
    CHECK(rejects_row20([](auto& v) { ++v.record_stride; }));
    CHECK(rejects_row20([](auto& v) { v.authored_row_offset ^= 0x10u; }));
    CHECK(rejects_row20([](auto& v) { v.authored_map_offset ^= 0x0Cu; }));
    CHECK(rejects_row20([](auto& v) { --v.authored_record_count; }));
    CHECK(rejects_row20([](auto& v) { --v.runtime_record_count; }));
    CHECK(rejects_row20([](auto& v) { v.authored_source_base_id ^= 1u; }));
    CHECK(rejects_row20([](auto& v) { v.authored_target_base_id ^= 1u; }));
    CHECK(rejects_row20([](auto& v) { v.source_database_id ^= 1u; }));
    CHECK(rejects_row20([](auto& v) { v.source_skeleton_id ^= 1u; }));
    CHECK(rejects_row20([](auto& v) { v.target_database_id ^= 1u; }));
    CHECK(rejects_row20([](auto& v) { v.target_skeleton_id ^= 1u; }));
    CHECK(rejects_row20([](auto& v) { --v.source_joint_count; }));
    CHECK(rejects_row20([](auto& v) { --v.target_joint_count; }));
    CHECK(rejects_row20([](auto& v) { --v.source_group_count; }));
    CHECK(rejects_row20([](auto& v) { --v.target_group_count; }));
    CHECK(rejects_row20([](auto& v) { v.profile_id = 1; }));
    CHECK(rejects_row20([](auto& v) { v.scale_bits ^= 1u; }));
    CHECK(rejects_row20([](auto& v) { v.flags = 1; }));
    CHECK(rejects_row20([](auto& v) { v.arm_ik = 1; }));
    CHECK(rejects_row20([](auto& v) { v.leg_ik = 1; }));

    CHECK(rejects_row157([](auto& v) { v.table_object_id ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { v.table_type_id ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { ++v.entry_index; }));
    CHECK(rejects_row157([](auto& v) { ++v.record_stride; }));
    CHECK(rejects_row157([](auto& v) { v.authored_row_offset ^= 0x10u; }));
    CHECK(rejects_row157([](auto& v) { v.authored_map_offset ^= 0x0Cu; }));
    CHECK(rejects_row157([](auto& v) { --v.authored_record_count; }));
    CHECK(rejects_row157([](auto& v) { --v.runtime_record_count; }));
    CHECK(rejects_row157([](auto& v) { v.authored_source_base_id ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { v.authored_target_base_id ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { v.source_database_id ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { v.source_skeleton_id ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { v.target_database_id ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { v.target_skeleton_id ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { --v.source_joint_count; }));
    CHECK(rejects_row157([](auto& v) { --v.target_joint_count; }));
    CHECK(rejects_row157([](auto& v) { --v.source_group_count; }));
    CHECK(rejects_row157([](auto& v) { --v.target_group_count; }));
    CHECK(rejects_row157([](auto& v) { v.profile_id ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { v.scale_bits ^= 1u; }));
    CHECK(rejects_row157([](auto& v) { v.flags = 1; }));
    CHECK(rejects_row157([](auto& v) { v.arm_ik = 1; }));
    CHECK(rejects_row157([](auto& v) { v.leg_ik = 1; }));

    for (int side = 0; side < 6; ++side) {
        Fixture fixture;
        switch (side) {
        case 0: fixture.evidence.row20.metadata.authored_target_base_id ^= 1u; break;
        case 1: fixture.evidence.row20.metadata.target_database_id ^= 1u; break;
        case 2: fixture.evidence.row20.metadata.target_skeleton_id ^= 1u; break;
        case 3: fixture.evidence.row157.metadata.authored_source_base_id ^= 1u; break;
        case 4: fixture.evidence.row157.metadata.source_database_id ^= 1u; break;
        default: fixture.evidence.row157.metadata.source_skeleton_id ^= 1u; break;
        }
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kEntryJoin);
    }
    return true;
}

bool record_counts_and_duplicates_fail_closed() {
    {
        Fixture fixture;
        fixture.evidence.row20.records =
            std::span<const RetargetRecordMetadata>(fixture.row20_records).first(92);
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordCount);
    }
    {
        Fixture fixture;
        fixture.evidence.row157.records =
            std::span<const RetargetRecordMetadata>(fixture.row157_records).first(70);
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordCount);
    }
    {
        Fixture fixture;
        fixture.row20_records.back() = fixture.row20_records.front();
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kDuplicateRecord);
    }
    {
        Fixture fixture;
        fixture.row157_records.back() = fixture.row157_records.front();
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kDuplicateRecord);
    }
    {
        Fixture fixture;
        // Same required source, different target: no identical pair, but the
        // endpoint is ambiguous and must not be selected by first match.
        fixture.row20_records.back() = fixture.row20_records[0];
        fixture.row20_records.back().target_global = 40;
        fixture.row20_records.back().target_reference_index = 40;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRequiredMapping);
    }
    {
        Fixture fixture;
        // Same required C39 destination from a different Chloe source.
        fixture.row157_records.back() = fixture.row157_records[0];
        fixture.row157_records.back().source_global = 40;
        fixture.row157_records.back().source_parameter_index = 40;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRequiredMapping);
    }
    return true;
}

bool every_required_record_field_is_checked() {
    for (std::size_t i = 0; i < fixture_transfers.size(); ++i) {
        {
            Fixture fixture;
            ++fixture.row20_records[i].source_global;
            ++fixture.row20_records[i].source_parameter_index;
            CHECK(!policy::evaluate(fixture.evidence).admitted());
        }
        {
            Fixture fixture;
            ++fixture.row20_records[i].target_global;
            ++fixture.row20_records[i].target_reference_index;
            CHECK(!policy::evaluate(fixture.evidence).admitted());
        }
        {
            Fixture fixture;
            fixture.row20_records[i].source_parameter_index ^= 1u;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
        }
        {
            Fixture fixture;
            fixture.row20_records[i].target_reference_index ^= 1u;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
        }
        {
            Fixture fixture;
            fixture.row20_records[i].source_group ^= 1u;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
        }
        {
            Fixture fixture;
            fixture.row20_records[i].target_group ^= 1u;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
        }
        {
            Fixture fixture;
            fixture.row20_records[i].mode =
                fixture.row20_records[i].mode == 1 ? 2 : 1;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRequiredMapping);
        }
        {
            Fixture fixture;
            ++fixture.row157_records[i].source_global;
            ++fixture.row157_records[i].source_parameter_index;
            CHECK(!policy::evaluate(fixture.evidence).admitted());
        }
        {
            Fixture fixture;
            ++fixture.row157_records[i].target_global;
            ++fixture.row157_records[i].target_reference_index;
            CHECK(!policy::evaluate(fixture.evidence).admitted());
        }
        {
            Fixture fixture;
            fixture.row157_records[i].source_parameter_index ^= 1u;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
        }
        {
            Fixture fixture;
            fixture.row157_records[i].target_reference_index ^= 1u;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
        }
        {
            Fixture fixture;
            fixture.row157_records[i].source_group ^= 1u;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
        }
        {
            Fixture fixture;
            fixture.row157_records[i].target_group ^= 1u;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
        }
        {
            Fixture fixture;
            fixture.row157_records[i].mode =
                fixture.row157_records[i].mode == 1 ? 2 : 1;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRequiredMapping);
        }
    }

    // Regression for the rejected interpretation of +0x26/+0x2a as local to
    // processing groups 7/2/1.  Every one of these values is structurally
    // plausible but disagrees with the native cumulative parameter layout.
    {
        Fixture fixture;
        fixture.row20_records[2].source_parameter_index = 1;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
    }
    {
        Fixture fixture;
        fixture.row20_records[2].target_reference_index = 1;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
    }
    {
        Fixture fixture;
        fixture.row157_records[2].source_parameter_index = 1;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
    }
    {
        Fixture fixture;
        fixture.row157_records[2].target_reference_index = 1;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
    }

    {
        Fixture fixture;
        fixture.row20_records.back().source_global = 702;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
    }
    {
        Fixture fixture;
        fixture.row20_records.back().target_global = 651;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
    }
    {
        Fixture fixture;
        fixture.row157_records.back().source_global = 651;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
    }
    {
        Fixture fixture;
        fixture.row157_records.back().target_global = 580;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
    }
    {
        Fixture fixture;
        fixture.row20_records.back().mode = 4;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRecordLayout);
    }
    return true;
}

bool semantic_middle_join_and_order_are_mandatory() {
    {
        Fixture fixture;
        // Each endpoint and each middle still occurs exactly once, but the two
        // row20 middle joints are crossed relative to row157.
        std::swap(fixture.row20_records[2].target_global,
                  fixture.row20_records[3].target_global);
        std::swap(fixture.row20_records[2].target_reference_index,
                  fixture.row20_records[3].target_reference_index);
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kSemanticJoin);
    }
    {
        Fixture fixture;
        // Make the crossed middle internally join on both legs.  It still is
        // not the exact StringId transfer for either finger.
        std::swap(fixture.row20_records[2].target_global,
                  fixture.row20_records[3].target_global);
        std::swap(fixture.row20_records[2].target_reference_index,
                  fixture.row20_records[3].target_reference_index);
        std::swap(fixture.row157_records[2].source_global,
                  fixture.row157_records[3].source_global);
        std::swap(fixture.row157_records[2].source_parameter_index,
                  fixture.row157_records[3].source_parameter_index);
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kRequiredMapping);
    }
    return true;
}

PoseValidEvidence& mask_stage(Fixture& fixture, int stage) {
    switch (stage) {
    case 0: return fixture.evidence.player_base;
    case 1: return fixture.evidence.chloe_additive;
    case 2: return fixture.evidence.chloe_composite;
    default: return fixture.evidence.nadine_output;
    }
}

bool every_valid_mask_bit_is_fail_closed() {
    // Any missing required bit or any extra bit, including padding beyond the
    // skeleton joint count, changes the exact normalized mask and is rejected.
    for (int stage = 0; stage < 4; ++stage) {
        for (std::size_t bit = 0;
             bit < policy::kGlobalMaskWordCount * 64;
             ++bit) {
            Fixture fixture;
            PoseValidEvidence& pose = mask_stage(fixture, stage);
            pose.global_lsb_bits[bit / 64] ^= std::uint64_t{1} << (bit % 64);
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kValidMask);
        }
    }
    for (int stage = 0; stage < 4; ++stage) {
        Fixture fixture;
        --mask_stage(fixture, stage).skeleton_joint_count;
        CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kValidMask);
    }
    return true;
}

bool mask_group_coverage_is_exact_and_unique() {
    for (int stage = 0; stage < 4; ++stage) {
        {
            Fixture fixture;
            PoseValidEvidence& pose = mask_stage(fixture, stage);
            pose.groups = pose.groups.first(1);
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kGroupCoverage);
        }
        {
            Fixture fixture;
            PoseValidEvidence& pose = mask_stage(fixture, stage);
            auto* groups = const_cast<std::uint8_t*>(pose.groups.data());
            groups[1] = groups[0];
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kGroupCoverage);
        }
        {
            Fixture fixture;
            PoseValidEvidence& pose = mask_stage(fixture, stage);
            auto* groups = const_cast<std::uint8_t*>(pose.groups.data());
            groups[1] = 6;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kGroupCoverage);
        }
        {
            Fixture fixture;
            PoseValidEvidence& pose = mask_stage(fixture, stage);
            const std::array<std::uint8_t, 3> groups{
                pose.groups[0], pose.groups[1], 6,
            };
            pose.groups = groups;
            CHECK(policy::evaluate(fixture.evidence).reason == RejectReason::kGroupCoverage);
        }
    }
    return true;
}

std::vector<std::uint32_t> exact_terminal_commands(
    std::uint32_t processing_group) {
    std::vector<std::uint32_t> words;
    const auto append = [&](std::uint32_t opcode, std::uint32_t count) {
        words.push_back((count << 16) | opcode);
        words.resize(words.size() + count - 1, 0);
    };
    append(0, 2);
    append(1, 2);
    append(2, 4);
    append(6, 6);
    words[words.size() - 2] = 0;
    append(6, 6);
    words[words.size() - 2] = 1;
    append(7, 6);
    words[words.size() - 5] = 0;
    words[words.size() - 4] = 1;
    words[words.size() - 3] = 0;
    words[words.size() - 2] = policy::kRequiredBlendMode;
    words[words.size() - 1] = policy::kUnitWeightBits;
    append(3, 2);
    append(4, 2);
    append(15, 4);
    append(16, 2);
    append(17, 4);
    if (processing_group == 0) {
        append(24, 6);
        words[words.size() - 5] = 0x248B1AAEu;
    }
    append(27, 6);
    append(5, 2);
    return words;
}

bool terminal_command_plan_is_exact_and_fail_closed() {
    constexpr std::uint32_t base_word = 8;
    constexpr std::uint32_t additive_word = 14;
    constexpr std::uint32_t finalizer_word = 26;
    for (std::uint32_t group = 0; group < 2; ++group) {
        const auto evaluate = [group](
                                  const std::vector<std::uint32_t>& candidate,
                                  std::uint32_t candidate_group,
                                  std::uint32_t base,
                                  std::uint32_t additive,
                                  std::uint32_t finalizer) {
            return policy::exact_terminal_command_plan({
                std::span<const std::uint32_t>(candidate.data(),
                                               candidate.size()),
                candidate_group, base, additive, finalizer});
        };
        auto words = exact_terminal_commands(group);
        CHECK(evaluate(words, group, base_word, additive_word,
                       finalizer_word));
        CHECK(!evaluate(words, group ^ 1u, base_word, additive_word,
                        finalizer_word));
        CHECK(!evaluate(words, group, base_word + 1, additive_word,
                        finalizer_word));
        CHECK(!evaluate(words, group, base_word, additive_word + 1,
                        finalizer_word));
        CHECK(!evaluate(words, group, base_word, additive_word,
                        finalizer_word + 1));

        const std::array<std::size_t, 13> structural_words{
            base_word, base_word + 4, base_word + 5,
            additive_word, additive_word + 4, additive_word + 5,
            20, 21, 22, 23, 24, 25, finalizer_word,
        };
        for (const std::size_t word : structural_words) {
            auto mutated = words;
            mutated[word] ^= 1u;
            CHECK(!evaluate(mutated, group, base_word, additive_word,
                            finalizer_word));
        }

        const std::array<std::size_t, 7> common_tail_headers{
            26, 28, 30, 34, 36,
            group == 0 ? 46u : 40u,
            group == 0 ? 52u : 46u,
        };
        for (const std::size_t word : common_tail_headers) {
            auto mutated = words;
            mutated[word] ^= 1u;
            CHECK(!evaluate(mutated, group, base_word, additive_word,
                            finalizer_word));
        }
        if (group == 0) {
            auto bad_opcode = words;
            bad_opcode[40] ^= 1u;
            CHECK(!evaluate(bad_opcode, group, base_word, additive_word,
                            finalizer_word));
            auto bad_id = words;
            bad_id[41] ^= 1u;
            CHECK(!evaluate(bad_id, group, base_word, additive_word,
                            finalizer_word));
        }
        auto extended = words;
        extended.push_back(0);
        CHECK(!evaluate(extended, group, base_word, additive_word,
                        finalizer_word));
    }
    return true;
}

} // namespace

int main() {
    const std::array<std::pair<const char*, bool (*)()>, 12> tests{{
        {"asset constants and exact C39 targets", exact_asset_constants_and_target_inventory},
        {"source-pose layout", source_pose_layout_is_exact_and_fail_closed},
        {"exact fixture", exact_fixture_is_admitted},
        {"scope", scope_is_exact_and_fail_closed},
        {"entry identity and join", exact_entry_identity_and_entry_join},
        {"record counts and duplicates", record_counts_and_duplicates_fail_closed},
        {"every required record field", every_required_record_field_is_checked},
        {"semantic middle join", semantic_middle_join_and_order_are_mandatory},
        {"every valid-mask bit", every_valid_mask_bit_is_fail_closed},
        {"mask group coverage", mask_group_coverage_is_exact_and_unique},
        {"terminal command plan", terminal_command_plan_is_exact_and_fail_closed},
        {"repeat exact fixture after negatives", exact_fixture_is_admitted},
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
