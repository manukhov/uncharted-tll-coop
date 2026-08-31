#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Pure, build-pinned admission contract for the P99 state-2 transfer:
//
//   Player D350 --row20--> Chloe --blend B8 mode2/weight1-->
//   Chloe composite --row157--> legacy Nadine
//
// The runtime remains responsible for proving the same Perform job/lifetime and
// for doing all native pose evaluation.  This policy only validates immutable
// scope, entry/record metadata, the 21-joint semantic join, and normalized
// ValidBits evidence.  It grants no authority for state 4, another weapon, a
// raw ordinal copy, a partial group, or a cached donor pointer.
namespace coop::p99_chloe_order {

inline constexpr std::uint32_t kP99ArtId = 0x8075397Du;
inline constexpr std::uint32_t kWeaponOutState = 2u;
inline constexpr std::uint32_t kPlayerBaseClipId = 0xD350A452u;
inline constexpr std::uint32_t kChloeAdditiveClipId = 0xB8FC6B25u;
inline constexpr std::uint8_t kRequiredBlendMode = 2u;
inline constexpr std::uint32_t kUnitWeightBits = 0x3F800000u;

inline constexpr std::uint32_t kSkelTableObjectId = 0xFE014FD2u;
inline constexpr std::uint32_t kSkelTableTypeId = 0x8E53B36Eu;

inline constexpr std::uint32_t kProtoBaseId = 0x1003D654u;
inline constexpr std::uint32_t kChloeBaseId = 0xB70B7EAFu;
inline constexpr std::uint32_t kNadineDlcBaseId = 0xC67A06F1u;

inline constexpr std::uint32_t kPlayerDatabaseId = 0x27E57F43u;
inline constexpr std::uint32_t kPlayerSkeletonId = 0xFD6DBE03u;
inline constexpr std::uint32_t kChloeDatabaseId = 0x6C2B2396u;
inline constexpr std::uint32_t kChloeSkeletonId = 0x4B0F5DE3u;
inline constexpr std::uint32_t kNadineDatabaseId = 0xED25C708u;
inline constexpr std::uint32_t kNadineSkeletonId = 0xC39CA709u;

inline constexpr std::uint16_t kRuntimeRetargetRecordStride = 0x2Eu;
inline constexpr std::size_t kRequiredJointCount = 21;
inline constexpr std::size_t kRequiredFingerCount = 19;
inline constexpr std::size_t kGlobalMaskWordCount = 11; // 704 normalized bits.

struct TerminalCommandPlanEvidence {
    std::span<const std::uint32_t> words{};
    std::uint32_t processing_group = 0xFFFFFFFFu;
    std::uint32_t base_word = 0xFFFFFFFFu;
    std::uint32_t additive_word = 0xFFFFFFFFu;
    std::uint32_t finalizer_word = 0xFFFFFFFFu;
};

// The two selected leaves must be the complete terminal blend subtree. No
// opaque callback or other logical-lane writer may run between either leaf,
// their blend and cmd3. The only accepted post-cmd3 difference is the
// statically resolved group-zero joint-conversion callback; cmd27 then exports
// the finalized JointParams for both retained groups.
inline constexpr bool exact_terminal_command_plan(
    const TerminalCommandPlanEvidence& evidence) {
    constexpr std::uint32_t kAnimationWords = 6;
    constexpr std::uint32_t kBlendWords = 6;
    constexpr std::uint32_t kMaxLanes = 64;
    constexpr std::uint32_t kPostPoseJointConversionId = 0x248B1AAEu;
    const auto& words = evidence.words;
    if (evidence.processing_group > 1 ||
        evidence.base_word >= words.size() ||
        evidence.additive_word >= words.size() ||
        evidence.finalizer_word >= words.size() ||
        evidence.base_word + kAnimationWords != evidence.additive_word ||
        evidence.additive_word + kAnimationWords + kBlendWords !=
            evidence.finalizer_word ||
        words.size() - evidence.base_word <
            2 * kAnimationWords + kBlendWords) {
        return false;
    }
    const std::uint32_t blend_word =
        evidence.additive_word + kAnimationWords;
    if (words[evidence.base_word] !=
            ((kAnimationWords << 16) | 6u) ||
        words[evidence.additive_word] !=
            ((kAnimationWords << 16) | 6u) ||
        words[blend_word] != ((kBlendWords << 16) | 7u)) {
        return false;
    }
    const std::uint32_t base_lane = words[evidence.base_word + 4];
    const std::uint32_t additive_lane = words[evidence.additive_word + 4];
    if (base_lane != 0 || additive_lane == base_lane ||
        additive_lane >= kMaxLanes ||
        words[evidence.base_word + 5] != 0 ||
        words[evidence.additive_word + 5] != 0 ||
        words[blend_word + 1] != base_lane ||
        words[blend_word + 2] != additive_lane ||
        words[blend_word + 3] != 0 ||
        words[blend_word + 4] != kRequiredBlendMode ||
        words[blend_word + 5] != kUnitWeightBits) {
        return false;
    }

    std::size_t cursor = evidence.finalizer_word;
    const auto consume = [&](std::uint32_t opcode,
                             std::uint32_t command_words) constexpr {
        if (cursor >= words.size() ||
            command_words > words.size() - cursor ||
            words[cursor] != ((command_words << 16) | opcode)) {
            return false;
        }
        cursor += command_words;
        return true;
    };
    if (!consume(3u, 2u) || !consume(4u, 2u) ||
        !consume(15u, 4u) || !consume(16u, 2u) ||
        !consume(17u, 4u)) {
        return false;
    }
    if (evidence.processing_group == 0) {
        const std::size_t conversion_word = cursor;
        if (!consume(24u, 6u) ||
            words[conversion_word + 1] != kPostPoseJointConversionId) {
            return false;
        }
    }
    return consume(27u, 6u) && consume(5u, 2u) && cursor == words.size();
}

// Read-only classification for one admitted Perform that failed to seal.  The
// runtime fills this from TLS-local scalar masks and from the same owner/table
// and skeleton comparisons already required by seal; it grants no pose-write
// authority and cannot relax any admission predicate.
enum class SealIncompleteStage : std::uint8_t {
    kNone = 0,
    kNoParserCandidate,
    kOneGroupCaptured,
    kOwnerTableIdentity,
    kSkeletonIdentity,
    kOtherIncomplete,
};

// Scalar-only snapshot of the branches that decide whether PerformAnimBlend
// reaches its normal inline Process loop.  These fields describe control flow;
// they never authorize command parsing or a pose write.
struct PerformBranchEvidence {
    bool snapshot_valid = false;
    bool job_object_present = false;
    std::uint8_t job_flags14 = 0;
    std::uint32_t fg_flags0 = 0;
    std::uint8_t fg_enabled5 = 0;
    std::uint8_t fg_state6 = 0;
    std::uint8_t fg_flags7 = 0;
    bool custom_callback_present = false;
    bool cached_skeleton_exact = false;
    bool global_prepass_enabled = false;
    bool global_split_enabled = false;
    std::uint8_t fg_split_ready2d1 = 0;
    std::uint32_t target_group_count = 0;
    std::uint16_t target_auxiliary_word = 0;
};

struct PerformBranchDecision {
    bool can_enter = false;
    bool main_loop = false;
    bool prepass = false;
    bool split_residual = false;
    bool force_one = false;
    std::uint32_t inline_group_limit = 0;
};

[[nodiscard]] inline constexpr PerformBranchDecision derive_perform_branch(
    const PerformBranchEvidence& evidence) noexcept {
    PerformBranchDecision decision{};
    if (!evidence.snapshot_valid) {
        return decision;
    }
    const bool group_flag = (evidence.fg_flags7 & 0x08u) != 0;
    const bool no_deferred_groups =
        (evidence.fg_flags0 & 0x00020000u) != 0;
    decision.can_enter = evidence.fg_enabled5 != 0 &&
        (evidence.job_flags14 == 0 || group_flag);
    decision.main_loop = decision.can_enter &&
        evidence.job_object_present && !evidence.custom_callback_present;
    if (!decision.main_loop) {
        return decision;
    }
    decision.prepass = !evidence.cached_skeleton_exact ||
        evidence.global_prepass_enabled;
    decision.force_one = !group_flag && !no_deferred_groups;
    decision.split_residual = evidence.global_split_enabled &&
        (evidence.fg_split_ready2d1 != 0 ||
         evidence.target_auxiliary_word == 0) &&
        evidence.target_group_count > 1 && group_flag &&
        !no_deferred_groups;
    decision.inline_group_limit =
        decision.force_one || decision.split_residual
        ? 1u
        : evidence.target_group_count;
    return decision;
}

struct SealDiagnosticEvidence {
    PerformBranchEvidence perform_branch{};
    std::uint8_t process_group_mask = 0;
    std::uint8_t parser_candidate_group_mask = 0;
    std::uint8_t captured_group_mask = 0;
    bool entries_captured = false;
    bool group0_valid = false;
    bool group1_valid = false;
    bool owner_checked = false;
    bool owner_exact = false;
    bool skeleton_checked = false;
    bool skeleton_exact = false;
    bool seal_succeeded = false;
};

[[nodiscard]] inline constexpr SealIncompleteStage classify_seal_diagnostic(
    const SealDiagnosticEvidence& evidence) noexcept {
    constexpr std::uint8_t kRequiredGroups = 0x03u;
    const std::uint8_t valid_group_mask =
        static_cast<std::uint8_t>((evidence.group0_valid ? 0x01u : 0u) |
                                  (evidence.group1_valid ? 0x02u : 0u));
    const bool both_groups_ready =
        evidence.captured_group_mask == kRequiredGroups &&
        evidence.entries_captured && valid_group_mask == kRequiredGroups;
    if ((evidence.process_group_mask & ~kRequiredGroups) != 0 ||
        (evidence.parser_candidate_group_mask & ~kRequiredGroups) != 0 ||
        (evidence.captured_group_mask & ~kRequiredGroups) != 0 ||
        (evidence.parser_candidate_group_mask &
             ~evidence.process_group_mask) != 0 ||
        evidence.parser_candidate_group_mask !=
            evidence.captured_group_mask ||
        valid_group_mask != evidence.captured_group_mask ||
        evidence.entries_captured !=
            (evidence.captured_group_mask != 0) ||
        evidence.owner_checked != both_groups_ready ||
        (evidence.owner_exact && !evidence.owner_checked) ||
        evidence.skeleton_checked !=
            (evidence.owner_checked && evidence.owner_exact) ||
        (evidence.skeleton_exact && !evidence.skeleton_checked)) {
        return SealIncompleteStage::kOtherIncomplete;
    }
    if (evidence.seal_succeeded) {
        return evidence.process_group_mask == kRequiredGroups &&
                evidence.parser_candidate_group_mask == kRequiredGroups &&
                evidence.captured_group_mask == kRequiredGroups &&
                evidence.entries_captured && evidence.group0_valid &&
                evidence.group1_valid && evidence.owner_checked &&
                evidence.owner_exact && evidence.skeleton_checked &&
                evidence.skeleton_exact
            ? SealIncompleteStage::kNone
            : SealIncompleteStage::kOtherIncomplete;
    }
    if (evidence.parser_candidate_group_mask == 0 &&
        evidence.captured_group_mask == 0 && !evidence.entries_captured &&
        !evidence.group0_valid && !evidence.group1_valid) {
        return SealIncompleteStage::kNoParserCandidate;
    }
    if (evidence.captured_group_mask == 0x01u ||
        evidence.captured_group_mask == 0x02u) {
        return SealIncompleteStage::kOneGroupCaptured;
    }
    if (both_groups_ready && evidence.owner_checked &&
        !evidence.owner_exact) {
        return SealIncompleteStage::kOwnerTableIdentity;
    }
    if (both_groups_ready && evidence.owner_checked && evidence.owner_exact &&
        evidence.skeleton_checked && !evidence.skeleton_exact) {
        return SealIncompleteStage::kSkeletonIdentity;
    }
    return SealIncompleteStage::kOtherIncomplete;
}

struct ScopeEvidence {
    bool is_role_b = false;
    bool is_retained_local_owner = false;
    bool is_legacy_nadine = false;
    std::uint8_t owner_index = 0xFF;
    std::uint32_t art_id = 0;
    std::uint32_t weapon_state = 0;
    std::uint32_t base_clip_id = 0;
    std::uint32_t additive_clip_id = 0;
    std::uint8_t blend_mode = 0;
    std::uint32_t blend_weight_bits = 0;
    bool base_is_additive = true;
    bool right_is_additive = false;
    // Row157 must consume the completed Chloe pose as an absolute pose.  It
    // must not inherit B8's additive flag.
    bool composite_is_additive = true;
};

// Exact identity/layout evidence for one runtime retarget entry.  The DC table
// identity, row index, native stride and raw file offsets are build-pinned
// provenance (entry_index may also be recomputed by scanning the live table);
// they are not claimed to reside in the entry object.  Remaining values are
// compared to live entry/authored-resource/skeleton/clip observations as noted.
struct RetargetEntryMetadata {
    // Build-pinned DC provenance.
    std::uint32_t table_object_id = 0;
    std::uint32_t table_type_id = 0;
    std::uint16_t entry_index = 0;
    // Build-pinned native layout; the live entry stores the pointer/count but
    // not the 0x2e stride.
    std::uint16_t record_stride = 0;
    std::uint32_t authored_row_offset = 0;
    std::uint32_t authored_map_offset = 0;
    // Raw authored map count, live-readable at +0x20 of the authored resource
    // reached through entry +0x00.  BuildRetargetEntry omits unresolved pairs,
    // so this is deliberately distinct from the runtime entry count.
    std::uint32_t authored_record_count = 0;
    // Resolved/compressed record count stored directly at live entry +0x20.
    std::uint16_t runtime_record_count = 0;
    // Authored raw-row +0x0c/+0x10 provenance, reached only after the exact
    // table row/resource has been identified.  These are not entry +0x08/+0x0c.
    std::uint32_t authored_source_base_id = 0;
    std::uint32_t authored_target_base_id = 0;
    // Direct live entry fields: database/object IDs at +0x08/+0x0c and
    // underlying skeleton IDs at +0x10/+0x14.
    std::uint32_t source_database_id = 0; // entry +0x08
    std::uint32_t source_skeleton_id = 0; // entry +0x10
    std::uint32_t target_database_id = 0; // entry +0x0c
    std::uint32_t target_skeleton_id = 0; // entry +0x14
    // Live underlying skeleton headers/group descriptors.
    std::uint16_t source_joint_count = 0;
    std::uint16_t target_joint_count = 0;
    std::uint8_t source_group_count = 0;
    std::uint8_t target_group_count = 0;
    // Live authored resource reached through entry +0x00: profile +0x24,
    // scale +0x28, flags/arm/leg bytes +0x2c/+0x2d.
    std::uint32_t profile_id = 0;
    std::uint32_t scale_bits = 0;
    std::uint32_t flags = 0;
    std::uint8_t arm_ik = 0;
    std::uint8_t leg_ik = 0;
};

// Value-only projection of runtime record bytes +0x24..+0x2c.  BuildRetargetEntry
// stores skeleton-global joint indices at +0x24/+0x28 and cumulative flattened
// parameter/reference-pose indices at +0x26/+0x2a.  The latter are not local to
// the current processing group: ApplyJointRetargets uses +0x2a directly against
// the complete target reference pose.  Group numbers are captured beside the
// record and checked against the build-pinned skeleton ranges.
struct RetargetRecordMetadata {
    std::uint16_t source_global = 0;
    std::uint16_t source_parameter_index = 0;
    std::uint16_t target_global = 0;
    std::uint16_t target_reference_index = 0;
    std::uint8_t source_group = 0xFF;
    std::uint8_t target_group = 0xFF;
    std::uint8_t mode = 0xFF;
};

struct RetargetEntryView {
    RetargetEntryMetadata metadata{};
    std::span<const RetargetRecordMetadata> records{};
};

// Native group-local ValidBits must be normalized to skeleton-global LSB-first
// words before admission.  `groups` is the exact set of captured groups; it is
// kept separately so duplicate or missing group callbacks fail closed even
// when their OR-ed masks would look identical.
struct PoseValidEvidence {
    std::uint16_t skeleton_joint_count = 0;
    std::span<const std::uint8_t> groups{};
    std::array<std::uint64_t, kGlobalMaskWordCount> global_lsb_bits{};
};

enum class SourcePoseKind : std::uint8_t {
    kPlayerBase = 1,
    kChloeAdditive = 2,
};

// Exact live SegmentContext layout at the retarget-plugin Add boundary.  The
// group descriptor is selected from the source skeleton's 0x20-byte group
// table; its +0x02 word selects one 10-byte processing descriptor.  Byte +0x04
// is the logical lane count, not the 48-byte JointParams row size.  Byte +0x05
// and word +0x08 are retained as raw build-pinned fields because no semantic
// interpretation is needed to admit this transfer.
struct SourcePoseLayoutEvidence {
    SourcePoseKind kind = SourcePoseKind::kPlayerBase;
    std::uint8_t target_group = 0xFF;
    std::uint8_t source_group = 0xFF;
    bool group_pointer_exact = false;
    bool processing_pointer_exact = false;
    bool stable = false;
    std::uint32_t group_table_offset = 0;
    std::uint32_t processing_table_offset = 0;
    std::uint16_t group_first = 0;
    std::uint16_t processing_descriptor_index = 0;
    std::uint16_t workspace_count = 0;
    std::uint16_t parameter_count = 0;
    std::uint16_t processing_joint_count = 0;
    std::uint16_t processing_scalar_count = 0;
    std::uint8_t processing_lane_count = 0;
    std::uint8_t processing_byte5 = 0xFF;
    std::uint16_t processing_first_parameter = 0;
    std::uint16_t processing_word8 = 0xFFFF;
};

struct TransferEvidence {
    ScopeEvidence scope{};
    RetargetEntryView row20{};
    RetargetEntryView row157{};
    std::span<const SourcePoseLayoutEvidence> player_source_layouts{};
    std::span<const SourcePoseLayoutEvidence> chloe_source_layouts{};
    PoseValidEvidence player_base{};
    PoseValidEvidence chloe_additive{};
    PoseValidEvidence chloe_composite{};
    PoseValidEvidence nadine_output{};
};

enum class RejectReason : std::uint8_t {
    kNone,
    kScope,
    kSourceLayout,
    kRow20Identity,
    kRow157Identity,
    kEntryJoin,
    kRecordCount,
    kRecordLayout,
    kDuplicateRecord,
    kRequiredMapping,
    kSemanticJoin,
    kGroupCoverage,
    kValidMask,
};

struct Decision {
    RejectReason reason = RejectReason::kNone;

    [[nodiscard]] constexpr bool admitted() const noexcept {
        return reason == RejectReason::kNone;
    }
};

struct SkeletonGroup {
    std::uint16_t first = 0;
    std::uint16_t count = 0;
};

// actor77 proto/chloe-base/nadine-dlc-base skeleton program descriptors.
inline constexpr std::array<SkeletonGroup, 9> kPlayerGroups{{
    {0, 168}, {168, 63}, {231, 76}, {307, 83}, {390, 107},
    {497, 52}, {549, 90}, {639, 38}, {677, 25},
}};
inline constexpr std::array<SkeletonGroup, 8> kChloeGroups{{
    {0, 205}, {205, 62}, {267, 38}, {305, 72},
    {377, 83}, {460, 107}, {567, 59}, {626, 25},
}};
inline constexpr std::array<SkeletonGroup, 8> kNadineGroups{{
    {0, 104}, {104, 38}, {142, 119}, {261, 83},
    {344, 107}, {451, 52}, {503, 52}, {555, 25},
}};

// actor77.psarc exact source skeleton-program tables.  Player's processing
// table begins at raw+0x160 while Chloe's begins at raw+0x140.  Group zero is
// deliberately not conflated with its larger workspace: only 71/89 authored
// JointParams rows precede the native 128-bit ValidBits block.
inline constexpr std::array<SourcePoseLayoutEvidence, 2>
    kExpectedPlayerSourceLayouts{{
        {SourcePoseKind::kPlayerBase, 0, 0, true, true, true,
         0x40, 0x160, 0, 0, 168, 71, 71, 27, 2, 0, 0, 0},
        {SourcePoseKind::kPlayerBase, 1, 7, true, true, true,
         0x40, 0x160, 639, 7, 38, 38, 38, 0, 1, 0, 435, 0},
    }};

inline constexpr std::array<SourcePoseLayoutEvidence, 2>
    kExpectedChloeSourceLayouts{{
        {SourcePoseKind::kChloeAdditive, 0, 0, true, true, true,
         0x40, 0x140, 0, 0, 205, 89, 89, 27, 2, 0, 0, 0},
        {SourcePoseKind::kChloeAdditive, 1, 2, true, true, true,
         0x40, 0x140, 267, 2, 38, 38, 38, 0, 1, 0, 90, 0},
    }};

struct ExpectedJointTransfer {
    std::uint32_t semantic_id = 0;
    std::uint16_t player_global = 0;
    std::uint16_t chloe_global = 0;
    std::uint16_t nadine_global = 0;
    std::uint8_t player_group = 0;
    std::uint8_t chloe_group = 0;
    std::uint8_t nadine_group = 0;
    std::uint8_t mode = 0;
};

// The finger ordering changes across skeletons.  In particular, Player/Chloe
// place the thumb before ring/middle/index while C39 stores the thumb last.
// These are semantic StringId joins, not an ordinal range copy.
inline constexpr std::array<ExpectedJointTransfer, kRequiredJointCount>
    kExpectedTransfers{{
        // r_palm, r_hand_prop_attachment
        {0xB42DF599u, 30, 39, 28, 0, 0, 0, 1},
        {0x05FC3888u, 32, 41, 30, 0, 0, 0, 3},
        // r_pinky a..d
        {0xD38C382Du, 640, 268, 105, 7, 2, 1, 1},
        {0xDECF1EF4u, 642, 270, 107, 7, 2, 1, 1},
        {0xDA0E0343u, 644, 272, 109, 7, 2, 1, 1},
        {0xC4495346u, 646, 274, 111, 7, 2, 1, 1},
        // r_thumb a..c
        {0x74890107u, 648, 276, 137, 7, 2, 1, 1},
        {0x79CA27DEu, 650, 278, 139, 7, 2, 1, 1},
        {0x7D0B3A69u, 652, 280, 141, 7, 2, 1, 1},
        // r_ring metacarpal, a..c
        {0xAAE09A7Du, 654, 282, 113, 7, 2, 1, 1},
        {0x69506AC1u, 656, 284, 115, 7, 2, 1, 1},
        {0x64134C18u, 658, 286, 117, 7, 2, 1, 1},
        {0x60D251AFu, 660, 288, 119, 7, 2, 1, 1},
        // r_middle metacarpal, a..c
        {0x715AB839u, 662, 290, 121, 7, 2, 1, 1},
        {0xB38B9BE7u, 664, 292, 123, 7, 2, 1, 1},
        {0xBEC8BD3Eu, 666, 294, 125, 7, 2, 1, 1},
        {0xBA09A089u, 668, 296, 127, 7, 2, 1, 1},
        // r_index metacarpal, a..c
        {0xFB0C60CDu, 670, 298, 129, 7, 2, 1, 1},
        {0xC1B94193u, 672, 300, 131, 7, 2, 1, 1},
        {0xCCFA674Au, 674, 302, 133, 7, 2, 1, 1},
        {0xC83B7AFDu, 676, 304, 135, 7, 2, 1, 1},
    }};

inline constexpr std::array<std::uint16_t, 2> kExpectedNadineGroup0Targets{
    28, 30,
};
inline constexpr std::array<std::uint16_t, kRequiredFingerCount>
    kExpectedNadineRightFingerTargets{
        105, 107, 109, 111, 113, 115, 117, 119, 121, 123,
        125, 127, 129, 131, 133, 135, 137, 139, 141,
    };

inline constexpr RetargetEntryMetadata kExpectedRow20{
    kSkelTableObjectId,
    kSkelTableTypeId,
    20,
    kRuntimeRetargetRecordStride,
    0x410u,
    0x2124u,
    96,
    93,
    kProtoBaseId,
    kChloeBaseId,
    kPlayerDatabaseId,
    kPlayerSkeletonId,
    kChloeDatabaseId,
    kChloeSkeletonId,
    702,
    651,
    9,
    8,
    0x00000000u,
    0x3F6BE76Du,
    0x00000000u,
    0,
    0,
};

inline constexpr RetargetEntryMetadata kExpectedRow157{
    kSkelTableObjectId,
    kSkelTableTypeId,
    157,
    kRuntimeRetargetRecordStride,
    0x1DC0u,
    0x2720u,
    74,
    71,
    kChloeBaseId,
    kNadineDlcBaseId,
    kChloeDatabaseId,
    kChloeSkeletonId,
    kNadineDatabaseId,
    kNadineSkeletonId,
    651,
    580,
    8,
    8,
    0xFC34096Cu,
    0x3F770A3Du,
    0x00000000u,
    0,
    0,
};

namespace detail {

template <std::size_t N>
[[nodiscard]] inline constexpr int group_for_global(
    const std::array<SkeletonGroup, N>& groups,
    std::uint16_t global) noexcept {
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
[[nodiscard]] inline constexpr int flattened_index_for_global(
    const std::array<SkeletonGroup, N>& groups,
    std::uint16_t global) noexcept {
    std::uint32_t prefix = 0;
    for (const SkeletonGroup& group : groups) {
        const std::uint32_t end =
            static_cast<std::uint32_t>(group.first) + group.count;
        if (global >= group.first && global < end) {
            const std::uint32_t flattened =
                prefix + static_cast<std::uint32_t>(global - group.first);
            return flattened <= 0xFFFFu
                ? static_cast<int>(flattened)
                : -1;
        }
        prefix += group.count;
    }
    return -1;
}

[[nodiscard]] inline constexpr bool same_entry_metadata(
    const RetargetEntryMetadata& left,
    const RetargetEntryMetadata& right) noexcept {
    return left.table_object_id == right.table_object_id &&
        left.table_type_id == right.table_type_id &&
        left.entry_index == right.entry_index &&
        left.record_stride == right.record_stride &&
        left.authored_row_offset == right.authored_row_offset &&
        left.authored_map_offset == right.authored_map_offset &&
        left.authored_record_count == right.authored_record_count &&
        left.runtime_record_count == right.runtime_record_count &&
        left.authored_source_base_id == right.authored_source_base_id &&
        left.authored_target_base_id == right.authored_target_base_id &&
        left.source_database_id == right.source_database_id &&
        left.source_skeleton_id == right.source_skeleton_id &&
        left.target_database_id == right.target_database_id &&
        left.target_skeleton_id == right.target_skeleton_id &&
        left.source_joint_count == right.source_joint_count &&
        left.target_joint_count == right.target_joint_count &&
        left.source_group_count == right.source_group_count &&
        left.target_group_count == right.target_group_count &&
        left.profile_id == right.profile_id &&
        left.scale_bits == right.scale_bits &&
        left.flags == right.flags &&
        left.arm_ik == right.arm_ik && left.leg_ik == right.leg_ik;
}

template <std::size_t SourceN, std::size_t TargetN>
[[nodiscard]] inline constexpr bool record_layout_valid(
    const RetargetRecordMetadata& record,
    const std::array<SkeletonGroup, SourceN>& source_groups,
    const std::array<SkeletonGroup, TargetN>& target_groups) noexcept {
    const int source_group = group_for_global(source_groups, record.source_global);
    const int target_group = group_for_global(target_groups, record.target_global);
    const int source_parameter =
        flattened_index_for_global(source_groups, record.source_global);
    const int target_reference =
        flattened_index_for_global(target_groups, record.target_global);
    if (source_group < 0 || target_group < 0 || source_parameter < 0 ||
        target_reference < 0 || record.mode > 3u ||
        record.source_group != static_cast<std::uint8_t>(source_group) ||
        record.target_group != static_cast<std::uint8_t>(target_group)) {
        return false;
    }
    return record.source_parameter_index ==
            static_cast<std::uint16_t>(source_parameter) &&
        record.target_reference_index ==
            static_cast<std::uint16_t>(target_reference);
}

[[nodiscard]] inline constexpr bool same_pair(
    const RetargetRecordMetadata& left,
    const RetargetRecordMetadata& right) noexcept {
    return left.source_global == right.source_global &&
        left.target_global == right.target_global;
}

template <std::size_t N>
consteval std::array<std::uint64_t, kGlobalMaskWordCount> make_mask(
    const std::array<std::uint16_t, N>& globals) {
    std::array<std::uint64_t, kGlobalMaskWordCount> result{};
    for (const std::uint16_t joint : globals) {
        result[joint / 64] |= std::uint64_t{1} << (joint % 64);
    }
    return result;
}

inline constexpr std::array<std::uint16_t, kRequiredJointCount>
    kPlayerValidGlobals{
        30, 32, 640, 642, 644, 646, 648, 650, 652, 654, 656,
        658, 660, 662, 664, 666, 668, 670, 672, 674, 676,
    };
inline constexpr std::array<std::uint16_t, kRequiredJointCount>
    kChloeValidGlobals{
        39, 41, 268, 270, 272, 274, 276, 278, 280, 282, 284,
        286, 288, 290, 292, 294, 296, 298, 300, 302, 304,
    };
inline constexpr std::array<std::uint16_t, kRequiredJointCount>
    kNadineValidGlobals{
        28, 30, 105, 107, 109, 111, 113, 115, 117, 119, 121,
        123, 125, 127, 129, 131, 133, 135, 137, 139, 141,
    };

inline constexpr auto kPlayerValidMask = make_mask(kPlayerValidGlobals);
inline constexpr auto kChloeValidMask = make_mask(kChloeValidGlobals);
inline constexpr auto kNadineValidMask = make_mask(kNadineValidGlobals);

template <std::size_t N>
[[nodiscard]] inline constexpr bool exact_groups(
    std::span<const std::uint8_t> actual,
    const std::array<std::uint8_t, N>& expected) noexcept {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        for (std::size_t j = i + 1; j < actual.size(); ++j) {
            if (actual[i] == actual[j]) {
                return false;
            }
        }
    }
    for (const std::uint8_t wanted : expected) {
        std::size_t matches = 0;
        for (const std::uint8_t group : actual) {
            matches += group == wanted ? 1u : 0u;
        }
        if (matches != 1) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline constexpr bool same_source_layout(
    const SourcePoseLayoutEvidence& left,
    const SourcePoseLayoutEvidence& right) noexcept {
    return left.kind == right.kind &&
        left.target_group == right.target_group &&
        left.source_group == right.source_group &&
        left.group_pointer_exact == right.group_pointer_exact &&
        left.processing_pointer_exact == right.processing_pointer_exact &&
        left.stable == right.stable &&
        left.group_table_offset == right.group_table_offset &&
        left.processing_table_offset == right.processing_table_offset &&
        left.group_first == right.group_first &&
        left.processing_descriptor_index ==
            right.processing_descriptor_index &&
        left.workspace_count == right.workspace_count &&
        left.parameter_count == right.parameter_count &&
        left.processing_joint_count == right.processing_joint_count &&
        left.processing_scalar_count == right.processing_scalar_count &&
        left.processing_lane_count == right.processing_lane_count &&
        left.processing_byte5 == right.processing_byte5 &&
        left.processing_first_parameter ==
            right.processing_first_parameter &&
        left.processing_word8 == right.processing_word8;
}

template <std::size_t N>
[[nodiscard]] inline constexpr bool exact_source_layouts(
    std::span<const SourcePoseLayoutEvidence> actual,
    const std::array<SourcePoseLayoutEvidence, N>& expected) noexcept {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        for (std::size_t j = i + 1; j < actual.size(); ++j) {
            if (actual[i].kind == actual[j].kind &&
                actual[i].target_group == actual[j].target_group) {
                return false;
            }
        }
    }
    for (const SourcePoseLayoutEvidence& wanted : expected) {
        std::size_t matches = 0;
        for (const SourcePoseLayoutEvidence& candidate : actual) {
            matches += same_source_layout(candidate, wanted) ? 1u : 0u;
        }
        if (matches != 1) {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
[[nodiscard]] inline constexpr RejectReason validate_pose(
    const PoseValidEvidence& pose,
    std::uint16_t expected_joint_count,
    const std::array<std::uint8_t, N>& expected_groups,
    const std::array<std::uint64_t, kGlobalMaskWordCount>& expected_mask) noexcept {
    if (!exact_groups(pose.groups, expected_groups)) {
        return RejectReason::kGroupCoverage;
    }
    if (pose.skeleton_joint_count != expected_joint_count ||
        pose.global_lsb_bits != expected_mask) {
        return RejectReason::kValidMask;
    }
    return RejectReason::kNone;
}

} // namespace detail

[[nodiscard]] inline constexpr Decision evaluate(
    const TransferEvidence& evidence) noexcept {
    const ScopeEvidence& scope = evidence.scope;
    if (!scope.is_role_b || !scope.is_retained_local_owner ||
        !scope.is_legacy_nadine || scope.owner_index != 0 ||
        scope.art_id != kP99ArtId || scope.weapon_state != kWeaponOutState ||
        scope.base_clip_id != kPlayerBaseClipId ||
        scope.additive_clip_id != kChloeAdditiveClipId ||
        scope.blend_mode != kRequiredBlendMode ||
        scope.blend_weight_bits != kUnitWeightBits || scope.base_is_additive ||
        !scope.right_is_additive || scope.composite_is_additive) {
        return {RejectReason::kScope};
    }

    if (!detail::exact_source_layouts(
            evidence.player_source_layouts,
            kExpectedPlayerSourceLayouts) ||
        !detail::exact_source_layouts(
            evidence.chloe_source_layouts,
            kExpectedChloeSourceLayouts)) {
        return {RejectReason::kSourceLayout};
    }

    if (evidence.row20.metadata.authored_target_base_id !=
            evidence.row157.metadata.authored_source_base_id ||
        evidence.row20.metadata.target_database_id !=
            evidence.row157.metadata.source_database_id ||
        evidence.row20.metadata.target_skeleton_id !=
            evidence.row157.metadata.source_skeleton_id) {
        return {RejectReason::kEntryJoin};
    }
    if (!detail::same_entry_metadata(evidence.row20.metadata, kExpectedRow20)) {
        return {RejectReason::kRow20Identity};
    }
    if (!detail::same_entry_metadata(evidence.row157.metadata, kExpectedRow157)) {
        return {RejectReason::kRow157Identity};
    }
    if (evidence.row20.records.size() !=
            kExpectedRow20.runtime_record_count ||
        evidence.row157.records.size() !=
            kExpectedRow157.runtime_record_count) {
        return {RejectReason::kRecordCount};
    }

    for (const RetargetRecordMetadata& record : evidence.row20.records) {
        if (!detail::record_layout_valid(record, kPlayerGroups, kChloeGroups)) {
            return {RejectReason::kRecordLayout};
        }
    }
    for (const RetargetRecordMetadata& record : evidence.row157.records) {
        if (!detail::record_layout_valid(record, kChloeGroups, kNadineGroups)) {
            return {RejectReason::kRecordLayout};
        }
    }
    for (std::size_t i = 0; i < evidence.row20.records.size(); ++i) {
        for (std::size_t j = i + 1; j < evidence.row20.records.size(); ++j) {
            if (detail::same_pair(evidence.row20.records[i],
                                  evidence.row20.records[j])) {
                return {RejectReason::kDuplicateRecord};
            }
        }
    }
    for (std::size_t i = 0; i < evidence.row157.records.size(); ++i) {
        for (std::size_t j = i + 1; j < evidence.row157.records.size(); ++j) {
            if (detail::same_pair(evidence.row157.records[i],
                                  evidence.row157.records[j])) {
                return {RejectReason::kDuplicateRecord};
            }
        }
    }

    for (const ExpectedJointTransfer& expected : kExpectedTransfers) {
        const RetargetRecordMetadata* row20_record = nullptr;
        const RetargetRecordMetadata* row157_record = nullptr;
        std::size_t row20_source_matches = 0;
        std::size_t row20_middle_matches = 0;
        std::size_t row157_middle_matches = 0;
        std::size_t row157_target_matches = 0;

        for (const RetargetRecordMetadata& record : evidence.row20.records) {
            if (record.source_global == expected.player_global) {
                row20_record = &record;
                ++row20_source_matches;
            }
            row20_middle_matches +=
                record.target_global == expected.chloe_global ? 1u : 0u;
        }
        for (const RetargetRecordMetadata& record : evidence.row157.records) {
            if (record.target_global == expected.nadine_global) {
                row157_record = &record;
                ++row157_target_matches;
            }
            row157_middle_matches +=
                record.source_global == expected.chloe_global ? 1u : 0u;
        }
        if (row20_source_matches != 1 || row20_middle_matches != 1 ||
            row157_middle_matches != 1 || row157_target_matches != 1 ||
            row20_record == nullptr || row157_record == nullptr) {
            return {RejectReason::kRequiredMapping};
        }
        if (row20_record->target_global != row157_record->source_global) {
            return {RejectReason::kSemanticJoin};
        }
        if (row20_record->target_global != expected.chloe_global ||
            row20_record->source_group != expected.player_group ||
            row20_record->target_group != expected.chloe_group ||
            row157_record->source_group != expected.chloe_group ||
            row157_record->target_group != expected.nadine_group ||
            row20_record->mode != expected.mode ||
            row157_record->mode != expected.mode) {
            return {RejectReason::kRequiredMapping};
        }
    }

    constexpr std::array<std::uint8_t, 2> player_groups{0, 7};
    constexpr std::array<std::uint8_t, 2> chloe_groups{0, 2};
    constexpr std::array<std::uint8_t, 2> nadine_groups{0, 1};

    RejectReason mask_result = detail::validate_pose(
        evidence.player_base, 702, player_groups, detail::kPlayerValidMask);
    if (mask_result != RejectReason::kNone) {
        return {mask_result};
    }
    mask_result = detail::validate_pose(
        evidence.chloe_additive, 651, chloe_groups, detail::kChloeValidMask);
    if (mask_result != RejectReason::kNone) {
        return {mask_result};
    }
    mask_result = detail::validate_pose(
        evidence.chloe_composite, 651, chloe_groups, detail::kChloeValidMask);
    if (mask_result != RejectReason::kNone) {
        return {mask_result};
    }
    mask_result = detail::validate_pose(
        evidence.nadine_output, 580, nadine_groups, detail::kNadineValidMask);
    if (mask_result != RejectReason::kNone) {
        return {mask_result};
    }
    return {};
}

} // namespace coop::p99_chloe_order
