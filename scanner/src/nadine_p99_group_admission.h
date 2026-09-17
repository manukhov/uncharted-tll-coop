#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// STATIC C39/P99 storage and command-envelope checks, not a runtime lease.
// All addresses below are integer identities. Spans must refer to caller-owned
// copies; this module NEVER dereferences a game address or invokes its allocator.
// A successful result proves only the stated value contract for that snapshot.
namespace coop::p99_group_admission {

using Address = std::uint64_t;
inline constexpr std::size_t kCapacity = 64;
inline constexpr std::uint64_t kArenaBytes = 0x20000;
inline constexpr std::uint64_t kMetadataBytes = 0x1000;
inline constexpr std::uint64_t kRecordBytes = 56;
inline constexpr std::uint64_t kTableOffset = 0xE00;
inline constexpr std::size_t kMaxCommandWords = 4096; // Conservative local bound.

struct Range { Address start = 0; std::uint64_t bytes = 0; };

[[nodiscard]] constexpr bool valid_range(Range range) noexcept {
    return range.start != 0 && range.bytes != 0 &&
        range.bytes <= std::numeric_limits<Address>::max() - range.start;
}

// Callers must first validate both ranges. Uses half-open intervals.
[[nodiscard]] constexpr bool overlaps(Range a, Range b) noexcept {
    return a.start < b.start + b.bytes && b.start < a.start + a.bytes;
}

enum class Reject : std::uint8_t {
    kNone, kSnapshotExtent, kAddressRange, kShape, kCounts, kCursors,
    kRecordPointer, kDuplicateRecord, kRecordExtent, kRecordOrder,
    kFreeList, kMissingAllocation, kAllocationTag, kSegmentState, kAlias,
    kCommandExtent, kCommandLength, kCommandGrammar, kCommandPayload,
    kCallback, kUnknownPrefix,
};

struct Decision {
    Reject reason = Reject::kShape;
    [[nodiscard]] constexpr bool accepted() const noexcept { return reason == Reject::kNone; }
};

// Native offsets: +00,+08,+10,+18,+20,+28,+30,+38,+48,+4C,+50.
// +40 is deliberately not interpreted. This is a value DTO, not a native overlay.
struct WorkBuffer {
    Address address = 0;
    Address arena = 0;
    std::uint64_t arena_bytes = 0;
    Address metadata = 0;
    std::uint64_t metadata_bytes = 0;
    Address free_head = 0;
    Address table = 0;
    std::uint64_t front_cursor = 0;
    std::uint64_t back_cursor = 0;
    std::uint32_t front_count = 0;
    std::uint32_t back_count = 0;
    std::uint32_t capacity = 0;
};

struct Allocation {
    std::uint64_t offset = 0;
    std::uint64_t bytes = 0;
    std::uint64_t first = 0;
    std::uint64_t count = 0;
    std::uint64_t flags = 0;
    std::uint64_t type = 0;
    Address next_free = 0;
};
static_assert(sizeof(Allocation) == kRecordBytes);
static_assert(kRecordBytes * kCapacity == kTableOffset);
static_assert(kTableOffset + sizeof(Address) * kCapacity == kMetadataBytes);

struct Metadata {
    WorkBuffer work{};
    // records[i] is the copy from metadata+56*i; table is the full pointer table.
    std::span<const Allocation> records{};
    std::span<const Address> table{};
};

[[nodiscard]] constexpr std::size_t record_index(Address pointer, Address base) noexcept {
    if (pointer < base || pointer - base >= kTableOffset ||
        (pointer - base) % kRecordBytes != 0) {
        return kCapacity;
    }
    return static_cast<std::size_t>((pointer - base) / kRecordBytes);
}

[[nodiscard]] inline Decision validate_work_buffer(const Metadata& snapshot) noexcept {
    const auto& w = snapshot.work;
    if (snapshot.records.size() != kCapacity || snapshot.table.size() != kCapacity) {
        return {Reject::kSnapshotExtent};
    }
    if (w.capacity != kCapacity || w.arena_bytes != kArenaBytes ||
        w.metadata_bytes != kMetadataBytes) {
        return {Reject::kShape};
    }
    const std::array<Range, 3> ranges{{{w.address, 88},
        {w.arena, w.arena_bytes}, {w.metadata, w.metadata_bytes}}};
    for (const auto range : ranges) {
        if (!valid_range(range)) return {Reject::kAddressRange};
    }
    if (w.address % 8 != 0 || w.arena % 16 != 0 || w.metadata % 16 != 0 ||
        w.table != w.metadata + kTableOffset) {
        return {Reject::kAddressRange};
    }
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        for (std::size_t j = i + 1; j < ranges.size(); ++j) {
            if (overlaps(ranges[i], ranges[j])) return {Reject::kAlias};
        }
    }
    if (w.front_count > kCapacity || w.back_count > kCapacity - w.front_count) {
        return {Reject::kCounts};
    }
    if (w.front_cursor > w.back_cursor || w.back_cursor > w.arena_bytes ||
        w.front_cursor % 16 != 0 || w.back_cursor % 16 != 0) {
        return {Reject::kCursors};
    }
    std::array<bool, kCapacity> seen{};
    std::uint64_t previous_end = 0;
    std::uint64_t back_first = w.arena_bytes;
    // Both active portions are ascending by offset in table order. The native
    // exact-start lookup (14217AA60) searches the back portion in reverse.
    for (std::size_t slot = 0; slot < kCapacity; ++slot) {
        const bool front = slot < w.front_count;
        const bool back = slot >= kCapacity - w.back_count;
        if (!front && !back) continue; // Inactive table words carry no authority.
        const auto index = record_index(snapshot.table[slot], w.metadata);
        if (index == kCapacity) return {Reject::kRecordPointer};
        if (seen[index]) return {Reject::kDuplicateRecord};
        seen[index] = true;
        const auto& record = snapshot.records[index];
        if (record.bytes == 0 || record.offset % 16 != 0 || record.bytes % 16 != 0 ||
            record.offset > w.arena_bytes || record.bytes > w.arena_bytes - record.offset ||
            record.count > std::numeric_limits<std::uint64_t>::max() - record.first ||
            record.next_free != 0) {
            return {Reject::kRecordExtent};
        }
        const auto end = record.offset + record.bytes;
        if (record.offset < previous_end) return {Reject::kRecordOrder};
        if (front && end > w.front_cursor) return {Reject::kCursors};
        if (back && record.offset < w.back_cursor) return {Reject::kCursors};
        if (slot == kCapacity - w.back_count) back_first = record.offset;
        previous_end = end;
        if (slot + 1 == w.front_count && end != w.front_cursor) return {Reject::kCursors};
    }
    if ((w.front_count == 0 && w.front_cursor != 0) || back_first != w.back_cursor) {
        return {Reject::kCursors};
    }
    // Read-only free-list walk over the OWNED array. Bound by 64 even for cycles;
    // every record must belong exactly once to the active set or the free list.
    Address free = w.free_head;
    std::size_t free_count = 0;
    while (free != 0) {
        const auto index = record_index(free, w.metadata);
        if (index == kCapacity || seen[index] || free_count == kCapacity) {
            return {Reject::kFreeList};
        }
        seen[index] = true;
        ++free_count;
        free = snapshot.records[index].next_free;
    }
    if (free_count != kCapacity - w.front_count - w.back_count) return {Reject::kFreeList};
    return {Reject::kNone};
}

struct Segment {
    std::uint16_t group = 0xFFFF;
    std::uint16_t flags = 0; // Native +4A, NOT builder flags9/8.
    Address joint_params = 0; // +50
    Address matrices = 0;     // +58
    Address scalars = 0;      // +60, must be null after cmd15.
    Address auxiliary = 0;    // +68
};

[[nodiscard]] inline Decision validate_group_allocations(
    const Metadata& snapshot, const Segment& segment) noexcept {
    const auto metadata = validate_work_buffer(snapshot);
    if (!metadata.accepted()) return metadata;
    if (segment.group > 1 || segment.scalars != 0 || (segment.flags & 3) != 0 ||
        (segment.group == 1 && segment.auxiliary != 0)) {
        return {Reject::kSegmentState};
    }
    const bool g0 = segment.group == 0;
    const std::uint64_t first = g0 ? 0 : 104;
    const std::array<Address, 3> pointers{segment.joint_params, segment.matrices, segment.auxiliary};
    const std::array<Allocation, 3> tags{{
        {0, g0 ? 4992u : 1824u, first, g0 ? 104u : 38u, 0, 0, 0},
        {0, g0 ? 6720u : 2496u, first, g0 ? 105u : 39u, 0, 3, 0},
        {0, 1072, 0, 266, 0, 5, 0},
    }};
    for (std::size_t target = 0; target < (g0 ? 3u : 2u); ++target) {
        const Allocation* found = nullptr; // Points only into the owned snapshot.
        for (std::size_t slot = 0; slot < kCapacity; ++slot) {
            if (slot >= snapshot.work.front_count && slot < kCapacity - snapshot.work.back_count) continue;
            const auto index = record_index(snapshot.table[slot], snapshot.work.metadata);
            const auto& record = snapshot.records[index]; // Validated in the first pass.
            if (snapshot.work.arena + record.offset == pointers[target]) found = &record;
        }
        if (!found) return {Reject::kMissingAllocation}; // Interior pointers also reject.
        const auto& tag = tags[target];
        if (found->bytes != tag.bytes || found->first != tag.first || found->count != tag.count ||
            found->type != tag.type || found->flags != tag.flags) return {Reject::kAllocationTag};
    }
    return {Reject::kNone};
}

struct Isolation {
    Range external_scratch{};
    Range segment_context{};
    Range copied_context{};
    Range shadow{};
    Range owned_sources{};
};

[[nodiscard]] inline Decision validate_isolation(const WorkBuffer& work,
    const Segment& segment, const Isolation& isolation) noexcept {
    if (segment.group > 1 || isolation.external_scratch.bytes != 6480 ||
        isolation.segment_context.bytes != 0x80 || isolation.copied_context.bytes != 0x80 ||
        isolation.shadow.bytes < (segment.group == 0 ? 4992u : 1824u) ||
        isolation.owned_sources.bytes != 21 * 48) return {Reject::kShape};
    const std::array<Range, 8> ranges{{{work.address, 88}, {work.arena, work.arena_bytes},
        {work.metadata, work.metadata_bytes}, isolation.external_scratch, isolation.segment_context,
        isolation.copied_context, isolation.shadow, isolation.owned_sources}};
    for (const auto range : ranges) {
        if (!valid_range(range)) return {Reject::kAddressRange};
    }
    if (isolation.external_scratch.start % 16 != 0 || isolation.shadow.start % 16 != 0 ||
        isolation.copied_context.start % 16 != 0) return {Reject::kAddressRange};
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        for (std::size_t j = i + 1; j < ranges.size(); ++j) {
            if (overlaps(ranges[i], ranges[j])) return {Reject::kAlias};
        }
    }
    return {Reject::kNone};
}

// Values must come from independently validated native bindings, not be filled
// by reading the command payload itself. Equality does not prove pointee extent.
struct CommandBindings {
    Address image_base = 0;
    Address postpose_callback = 0;
    Address pose_source = 0;
    Address scalar_source = 0;
    Address cmd15_source = 0; // Build a2[5] = *(Fg+118), not the root matrix.
    Address conversion_destination = 0;
    std::array<Address, 3> sphere_arguments{};
    std::array<Address, 3> palette_arguments{};
};

struct CommandResult {
    Reject reason = Reject::kCommandGrammar;
    std::size_t cmd16_word = 0;
    std::size_t suffix_word = 0;
    std::size_t prefix_words = 0;
    bool known_native_prefix = false;
    [[nodiscard]] constexpr bool accepted() const noexcept { return reason == Reject::kNone; }
    [[nodiscard]] constexpr bool envelope_valid() const noexcept {
        return accepted();
    }
};

[[nodiscard]] constexpr std::uint32_t header(std::uint16_t opcode, std::uint16_t words) noexcept {
    return static_cast<std::uint32_t>(opcode) | (static_cast<std::uint32_t>(words) << 16);
}

// Index validity is established by the command's exact checked length first.
[[nodiscard]] inline Address qword(std::span<const std::uint32_t> command, std::size_t word) noexcept {
    return command[word] | (static_cast<Address>(command[word + 1]) << 32);
}

// Direct lane/control cases of ProcessAnimCmdListSegment 141AB97C0. Exact
// lengths cover all directly read payload fields. No plugin/virtual command,
// context reset, final-pose evaluator or publication/export command is admitted.
// This whitelist does NOT validate the native animation/pointer operands.
[[nodiscard]] constexpr std::uint16_t native_prefix_length(std::uint16_t opcode) noexcept {
    switch (opcode) {
    case 2: case 13: case 35: case 36: return 4;
    case 3: case 8: case 9: case 11: return 2;
    case 6: case 7: case 18: case 20: return 6;
    case 10: case 12: case 34: return 10;
    default: return 0;
    }
}

[[nodiscard]] inline CommandResult validate_commands(std::uint16_t group,
    std::span<const std::uint32_t> words, const CommandBindings& binding) noexcept {
    CommandResult result{};
    if (group > 1 || words.empty() || words.size() > kMaxCommandWords) {
        result.reason = Reject::kCommandExtent;
        return result;
    }
    if (binding.image_base == 0 || binding.image_base > std::numeric_limits<Address>::max() - 0x6CD220 ||
        binding.postpose_callback != binding.image_base + 0x6CD220) {
        result.reason = Reject::kCallback;
        return result;
    }
    // First pass establishes ALL command boundaries before any payload read.
    std::array<std::size_t, kMaxCommandWords / 2> starts{};
    std::size_t command_count = 0;
    std::size_t pose_count = 0;
    for (std::size_t cursor = 0; cursor < words.size();) {
        const std::size_t length = words[cursor] >> 16;
        const auto opcode = words[cursor] & 0xFFFFu;
        if (length < 2 || length % 2 != 0 || length > words.size() - cursor) {
            result.reason = Reject::kCommandLength;
            return result;
        }
        starts[command_count++] = cursor;
        if (opcode == 16) { ++pose_count; result.cmd16_word = cursor; }
        if (opcode == 27 || (opcode == 5 && (length != 2 || cursor + length != words.size()))) return result;
        cursor += length;
    }
    if (pose_count != 1 || words[starts[command_count - 1]] != header(5, 2)) return result;
    const auto command_at = [&](std::size_t index) noexcept {
        const auto offset = starts[index];
        return words.subspan(offset, words[offset] >> 16);
    };
    const auto matches = [&](std::size_t index, std::uint16_t op, std::uint16_t length) noexcept {
        return index < command_count && words[starts[index]] == header(op, length);
    };
    std::size_t close = 0;
    if (group == 0) {
        constexpr std::array<std::uint32_t, 11> fixed{
            header(0, 2), header(1, 2), header(2, 4), header(9, 2), header(3, 2),
            header(4, 2), header(14, 6), header(15, 4), header(16, 2),
            header(17, 4), header(24, 6)};
        if ((words.size() != 48 || command_count != 13) &&
            (words.size() != 58 || command_count != 14)) return result;
        for (std::size_t i = 0; i < fixed.size(); ++i) {
            if (words[starts[i]] != fixed[i]) return result;
        }
        if (command_at(2)[1] != 0 || command_at(2)[2] != 1 || command_at(3)[1] != 0 ||
            qword(command_at(6), 2) != binding.pose_source ||
            qword(command_at(6), 4) != binding.scalar_source) {
            result.reason = Reject::kCommandPayload;
            return result;
        }
        close = 5;
        const auto conversion = command_at(10);
        // Cmd24's +8 qword is the native type-0 marker, not padding.
        if (conversion[1] != 0x248B1AAE || qword(conversion, 2) != 0 ||
            qword(conversion, 4) != binding.conversion_destination) {
            result.reason = Reject::kCommandPayload;
            return result;
        }
        if (command_count == 14) {
            if (!matches(11, 24, 10)) return result;
            const auto sphere = command_at(11);
            if (sphere[1] != 0x514C0761 || qword(sphere, 2) != 0) {
                result.reason = Reject::kCommandPayload;
                return result;
            }
            for (std::size_t i = 0; i < 3; ++i) {
                if (qword(sphere, 4 + 2 * i) != binding.sphere_arguments[i]) {
                    result.reason = Reject::kCommandPayload;
                    return result;
                }
            }
        }
        if (!matches(command_count - 2, 26, 10)) return result;
    } else {
        // The stock prefix runs BEFORE the overlay is created. The admitted
        // direct cases receive only native context/lanes, never the FUTURE local
        // copied cmd16 context or shadow. The caller must not expose those
        // addresses early. Exact known opcodes suffice for this overlay envelope;
        // this is not a proof of arbitrary native clip/retarget operand semantics.
        if (command_count < 8 || !matches(0, 0, 2) || !matches(1, 1, 2)) return result;
        close = command_count - 6;
        constexpr std::array<std::uint32_t, 6> suffix{
            header(4, 2), header(15, 4), header(16, 2), header(17, 4), header(26, 10), header(5, 2)};
        for (std::size_t i = 0; i < suffix.size(); ++i) {
            if (words[starts[close + i]] != suffix[i]) return result;
        }
        bool lane_open = false;
        for (std::size_t i = 2; i < close; ++i) {
            const auto opcode = static_cast<std::uint16_t>(words[starts[i]] & 0xFFFFu);
            const auto length = native_prefix_length(opcode);
            if (length == 0 || !matches(i, opcode, length)) {
                result.reason = Reject::kUnknownPrefix;
                return result;
            }
            if (opcode == 2) {
                if (lane_open || command_at(i)[2] == 0 || command_at(i)[2] > 0x7FFF) return result;
                lane_open = true;
            } else {
                if (!lane_open) return result;
                if (opcode == 3) lane_open = false;
            }
        }
        if (lane_open) return result;
        result.prefix_words = starts[close];
        result.known_native_prefix = true;
    }
    result.suffix_word = starts[close];
    const auto root = command_at(group == 0 ? 7 : close + 1);
    const auto scalar = command_at(group == 0 ? 9 : close + 3);
    const auto palette = command_at(command_count - 2);
    if (qword(root, 2) != binding.cmd15_source || qword(scalar, 2) != binding.scalar_source ||
        palette[8] != (group == 0 ? 0u : 104u) || palette[9] != (group == 0 ? 104u : 38u)) {
        result.reason = Reject::kCommandPayload;
        return result;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (qword(palette, 2 + 2 * i) != binding.palette_arguments[i]) {
            result.reason = Reject::kCommandPayload;
            return result;
        }
    }
    result.reason = Reject::kNone;
    return result;
}

} // namespace coop::p99_group_admission
