#pragma once

#include "nadine_p99_group_admission.h"
#include "nadine_p99_group_program_manifest.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// Read-only BUILDING BLOCK, not a deployed observer. The caller must establish
// retained owner/P99, exact normal Async g0 grammar and native invocation, then
// obtain fresh post-cmd16 context/allocator copies BEFORE invoking this helper.
// Synthetic phase/identity values cannot establish any of those live facts.
// Never retain a Reader/page approval across stock execution. No game pointer
// is dereferenced directly here; the supplied Reader owns bounded reads.
namespace coop::p99_wrist_snapshot {
namespace storage = p99_group_admission;
namespace program = p99_group_program_manifest;
using Address = storage::Address;
using Context = std::array<std::byte, 0x80>;
inline constexpr std::array<std::size_t, 4> kJointOffsets{0x480, 0x4E0, 0x930, 0x990};
inline constexpr std::size_t kWorkspaceOffset = 0x5C0;
inline constexpr std::size_t kPayloadBytes = 256;

struct Payload {
    // Raw JP24/26/49/51 (48B each), then W22 (64B). Preserve sign bits,
    // S.w/T.w and workspace padding; interpreting/validating numbers is later.
    // No borrowed pointer or self-reference is stored in the published value.
    std::array<std::byte, kPayloadBytes> bytes{};
};
static_assert(sizeof(Payload) == 256);

enum class Phase : std::uint8_t { BeforeProgram, AfterProgram };
enum class Reject : std::uint8_t {
    None, Repeated, Phase, Bindings, ContextExtent, ContextIdentity,
    SegmentState, Allocation, Read,
};
struct Result {
    Reject reason = Reject::Phase;
    storage::Reject allocation_reason = storage::Reject::kNone;
    Payload payload{};
    [[nodiscard]] constexpr bool copied() const noexcept { return reason == Reject::None; }
    // A successful value-contract check never manufactures runtime authority.
    [[nodiscard]] static constexpr bool source_admitted() noexcept { return false; }
    [[nodiscard]] static constexpr bool final_render_admitted() noexcept { return false; }
};

struct Bindings {
    Address module = 0, raw = 0, scratch = 0;
    // Integer identities for independently validated target/command bindings.
    // This structure does NOT acquire a lifetime lease or verify the program.
};

template<class T>
[[nodiscard]] inline T load(const Context& context, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, context.data()+offset, sizeof(value));
    return value;
}

class Attempt {
public:
    Attempt() = default;
    Attempt(const Attempt&) = delete;
    Attempt& operator=(const Attempt&) = delete;
    [[nodiscard]] bool used() const noexcept { return used_; }

    // Exactly one terminal attempt per object, including a failed attempt. Not
    // thread-safe: one admitted fiber/invocation owns it. A future live adapter
    // must independently enforce global arming/expiry and overhead budget.
    template<class Reader>
    [[nodiscard]] Result capture(Phase phase, std::span<const std::byte> context_bytes,
        const Bindings& bindings, const storage::Metadata& metadata, Reader& reader) noexcept {
        static_assert(noexcept(reader.bytes(Address{}, std::size_t{},
            static_cast<void*>(nullptr), std::size_t{})), "Reader bytes must contain read faults");
        if (used_) return {Reject::Repeated};
        used_ = true;
        if (phase != Phase::AfterProgram) return {Reject::Phase};
        if (!storage::valid_range({bindings.module, program::kCustomTableRva+1}) ||
            !storage::valid_range({bindings.raw, program::kRequiredRawExtent}) ||
            !storage::valid_range({bindings.scratch, 6480}) ||
            (bindings.module & 0xFFF) != 0 ||
            (bindings.scratch & 15) != 0) return {Reject::Bindings};
        if (context_bytes.size() != sizeof(Context)) return {Reject::ContextExtent};
        Context context{};
        std::memcpy(context.data(), context_bytes.data(), context.size());
        // Segment+08 is reserved ZERO, not the parent AnimCmdContext pointer.
        // The old dormant writer used the rejected parent-pointer comparison.
        if (load<Address>(context,0) != metadata.work.address ||
            load<Address>(context,8) != 0 || load<Address>(context,0x20) != 0 ||
            load<Address>(context,0x10) != bindings.module+program::kCustomTableRva ||
            load<std::uint16_t>(context,0x18) != program::kCustomCount ||
            load<Address>(context,0x28) != bindings.raw ||
            load<Address>(context,0x30) != bindings.scratch ||
            load<Address>(context,0x38) != bindings.raw+0x40 ||
            load<Address>(context,0x40) != bindings.raw+0x140) return {Reject::ContextIdentity};
        const storage::Segment segment{load<std::uint16_t>(context,0x48),
            load<std::uint16_t>(context,0x4A), load<Address>(context,0x50),
            load<Address>(context,0x58), load<Address>(context,0x60), load<Address>(context,0x68)};
        if (segment.group != 0 || segment.flags != 0 || segment.scalars != 0)
            return {Reject::SegmentState};
        const auto allocation = storage::validate_group_allocations(metadata, segment);
        if (!allocation.accepted()) return {Reject::Allocation, allocation.reason};

        // Stage privately. A partial/faulting read never leaks a mixed/partial
        // operand set, and there is no caller-provided destination to alias the
        // game's input or output. Only these five source spans are requested.
        Payload staged{};
        for (std::size_t i=0; i<kJointOffsets.size(); ++i) {
            if (!reader.bytes(segment.joint_params, kJointOffsets[i],
                    staged.bytes.data()+48*i, 48)) return {Reject::Read};
        }
        if (!reader.bytes(segment.matrices, kWorkspaceOffset,
                staged.bytes.data()+192, 64)) return {Reject::Read};
        Result result{Reject::None};
        result.payload = staged;
        return result;
    }
private:
    bool used_ = false;
};
} // namespace coop::p99_wrist_snapshot
