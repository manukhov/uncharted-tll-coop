#pragma once

// Bounded byte-copy adapter for the observation-only JointBlend policy.
// Reader::copy(address, destination, bytes) must return true ONLY after a full
// copy, without allocation or game calls. The Windows caller supplies page
// preflight and SEH within one callback-free read phase, never across native work.
// Read consistency is reject-only: neither equal snapshots nor SEH retain an
// object, defeat ABA, establish an atomic view, or admit game-memory writes.
#include "enemy_jointblend_observation.h"
#include <cstring>

namespace coop::enemy_jointblend_capture {
namespace observation = enemy_jointblend_observation;
using Address = std::uintptr_t;
inline constexpr std::size_t kGroup0Rows = 90, kCandidateProgramBytes = 0x40B0;
inline constexpr Address kForceMode2Rva = 0x3682F49, kForceCachedPrepassRva = 0x3682F4B;

enum class Reason : std::uint8_t {
    none, unreadable, pointer_range, non_pirate, changed, policy_rejected,
    candidate_extent, invalid_timing, already_attempted
};
struct Status {
    Reason reason{};
    observation::Reason policy_reason{};
    constexpr bool ok() const { return reason == Reason::none; }
};
struct Filter {
    Address fg{}, record{}, owner{}, vtable{};
};
enum class CaptureTiming : std::uint8_t { none, reset_returned, export_returned, pre_consumer, post_store };
using Matrix = std::array<std::uint32_t, 16>;
struct MatrixCapture {
    std::array<Matrix, kGroup0Rows> rows{};
    CaptureTiming timing{};
    bool complete{};
};
struct LocatorCapture {
    std::array<std::array<std::uint32_t, 8>, observation::kMaxSelectedJoints> rows{};
    // Two native uint64 words cover admitted indices0..89. Words are copied
    // before AND after the row copies; equality is not an atomicity proof.
    std::array<std::uint64_t, 2> valid_before{}, valid_after{};
    std::array<std::int16_t, observation::kMaxSelectedJoints> joints{};
    std::uint8_t count{};
    CaptureTiming timing{};
    bool complete{};
};
struct ProgramCapture {
    // Candidate authored bytes only. Even a later hash match does not prove
    // cmd16 executed this program; Process's generated commands are separate.
    std::array<std::byte, kCandidateProgramBytes> bytes{};
    observation::Identity identity{};
    bool attempted{}, complete{};
};

namespace detail {
template <class T, std::size_t N>
T field(const std::array<std::byte, N>& bytes, std::size_t offset) {
    T out{};
    if (offset <= N && sizeof(T) <= N - offset) std::memcpy(&out, bytes.data() + offset, sizeof(T));
    return out;
}
constexpr bool add(Address base, Address offset, Address& out) {
    if (!base || offset > std::numeric_limits<Address>::max() - base) return false;
    out = base + offset;
    return true;
}
template <class Reader>
Status copy(Reader& r, Address base, Address offset, void* dst, std::size_t bytes) {
    Address at{};
    if (!add(base, offset, at) || !observation::fits_span(at, bytes)) return {Reason::pointer_range};
    return r.copy(at, dst, bytes) ? Status{} : Status{Reason::unreadable};
}
template <class Reader, class T>
Status read(Reader& r, Address base, Address offset, T& out) {
    return copy(r, base, offset, &out, sizeof(out));
}
inline Status policy(observation::Reason why) {
    return why == observation::Reason::none ? Status{} : Status{Reason::policy_rejected, why};
}
} // namespace detail

// Four scalar reads only. In particular a non-Pirate never reads Art, controls,
// CompositeBody or body arrays. Filter output grants no ownership/admission.
template <class Reader>
Status fast_filter(Reader& r, Address module, Address job, Filter& out) {
    out = {};
    Address expected{};
    if (!detail::add(module, observation::kPirateRva, expected)) return {Reason::pointer_range};
    Filter f{};
    if (const auto s = detail::read(r, job, 8, f.fg); !s.ok()) return s;
    if (const auto s = detail::read(r, f.fg, 0x1A8, f.record); !s.ok()) return s;
    if (const auto s = detail::read(r, f.record, 8, f.owner); !s.ok()) return s;
    if (const auto s = detail::read(r, f.owner, 0, f.vtable); !s.ok()) return s;
    if (f.vtable != expected) return {Reason::non_pirate};
    out = f;
    return {};
}

namespace detail {
template <class Reader>
Status metadata_pass(Reader& r, Address module, Address job, Address original_slot, observation::Metadata& m) {
    m = {};
    if (!original_slot || (original_slot & 15u) != 8 || !observation::fits_span(original_slot, 8))
        return policy(observation::Reason::owner_links);
    Filter f{};
    if (const auto s = fast_filter(r, module, job, f); !s.ok()) return s;
    std::array<std::byte, 0x18> job_bytes{};
    std::array<std::byte, 0x2A8> fg{};
    std::array<std::byte, 0x240> owner{};
    std::array<std::byte, 16> record{};
    if (const auto s = read(r, job, 0, job_bytes); !s.ok()) return s;
    if (const auto s = read(r, f.fg, 0, fg); !s.ok()) return s;
    if (const auto s = read(r, f.owner, 0, owner); !s.ok()) return s;
    if (const auto s = read(r, f.record, 0, record); !s.ok()) return s;
    auto& i = m.identity;
    i.module = module; i.job = job; i.native_return_slot = original_slot;
    i.owner = f.owner; i.vtable = field<Address>(owner, 0); i.record = f.record;
    i.record_owner = field<Address>(record, 8); i.owner_record = field<Address>(owner, 0xA8);
    i.fg = f.fg; i.owner_fg = field<Address>(owner, 0x1A8); i.fg_record = field<Address>(fg, 0x1A8);
    i.id = i.fg_id = field<std::uint32_t>(fg, 0x1B0); i.owner_id = field<std::uint32_t>(owner, 0xBC);
    if (field<Address>(job_bytes, 8) != f.fg || i.vtable != f.vtable || i.record_owner != f.owner ||
        i.owner_record != f.record || i.fg_record != f.record || i.owner_fg != f.fg || !i.id || i.owner_id != i.id)
        return {Reason::changed};
    i.job_control = field<Address>(job_bytes, 0); i.fg_control = field<Address>(fg, 0x10);
    i.owner_control = field<Address>(owner, 0x1A0);
    if (!i.job_control || i.job_control != i.fg_control || i.owner_control != i.job_control)
        return policy(observation::Reason::excluded_branch);
    if (const auto s = read(r, i.job_control, 0x20, i.control_fg); !s.ok()) return s;
    if (i.control_fg != i.fg) return policy(observation::Reason::excluded_branch);
    i.art = i.active_art18 = field<Address>(fg, 0x18); i.active_art1a0 = field<Address>(fg, 0x1A0);
    i.cache_art = field<Address>(fg, 0xF0); i.cache_raw = field<Address>(fg, 0xF8);
    if (!add(i.fg, 0x30, i.cache)) return {Reason::pointer_range};
    i.locator_bank = field<Address>(fg, 0x108); i.matrix_bank = field<Address>(fg, 0x110);
    if (!i.art || i.art != i.active_art1a0 || i.art != i.cache_art || !i.cache_raw || !i.locator_bank || !i.matrix_bank)
        return policy(observation::Reason::cache_links);
    m.fg_flags = field<std::uint32_t>(fg, 0); m.raw_mode = field<std::uint8_t>(fg, 6);
    m.fg_byte8 = field<std::uint8_t>(fg, 8); m.local_pose = field<Address>(fg, 0x100);
    m.job_flag14 = field<std::uint8_t>(job_bytes, 0x14); m.custom_callback = field<Address>(fg, 0x2A0) != 0;
    std::uint8_t force{}, prepass{};
    if (const auto s = read(r, module, kForceMode2Rva, force); !s.ok()) return s;
    if (const auto s = read(r, module, kForceCachedPrepassRva, prepass); !s.ok()) return s;
    m.force_mode2 = force != 0; m.initially_cached_prepass = prepass != 0;
    const auto mode = ((m.force_mode2 || (m.fg_flags & 2u)) && m.local_pose) ? 2 : m.raw_mode;
    if (m.job_flag14 || !m.raw_mode || mode != 2 || !(m.fg_flags & 0x20u) || !(m.fg_byte8 & 1u) ||
        m.custom_callback || m.initially_cached_prepass) return policy(observation::Reason::excluded_branch);
    std::array<std::byte, 0x48> art{};
    std::array<std::byte, 0x40> raw{};
    if (const auto s = read(r, i.art, 0, art); !s.ok()) return s;
    i.art_database_id = field<std::uint32_t>(art, 0x24); i.raw = i.art_raw = field<Address>(art, 0x40);
    if (!i.raw || i.raw != i.cache_raw) return policy(observation::Reason::cache_links);
    if (i.art_database_id != observation::kNormalDatabase && i.art_database_id != observation::kMediumDatabase)
        return policy(observation::Reason::unknown_skeleton);
    if (const auto s = read(r, i.raw, 0, raw); !s.ok()) return s;
    i.raw_skeleton_id = field<std::uint32_t>(raw, 4);
    if (!observation::candidate_skeleton_pair(i)) return policy(observation::Reason::unknown_skeleton);
    // Equal nonzero counts are pinned for these two authored assets, not a
    // general native theorem relating Art records to raw descriptor counts.
    if (!field<std::uint8_t>(raw, 0x16) || field<std::uint32_t>(art, 0x30) != field<std::uint8_t>(raw, 0x16))
        return policy(observation::Reason::group_range);
    i.group_table_relative = field<std::uint32_t>(raw, 0x2C);
    const auto directory_relative = field<std::uint32_t>(raw, 0x34);
    if (!i.group_table_relative || !directory_relative || !add(i.raw, i.group_table_relative, i.group_descriptor) ||
        !add(i.raw, directory_relative, i.program_directory)) return {Reason::pointer_range};
    std::array<std::byte, 32> group{};
    if (const auto s = read(r, i.group_descriptor, 0, group); !s.ok()) return s;
    i.first = field<std::uint16_t>(group, 0); i.program_index = field<std::uint16_t>(group, 6);
    i.program_count = field<std::uint8_t>(group, 0xF);
    i.workspace_count = field<std::uint16_t>(group, 0x10); i.parameter_count = field<std::uint16_t>(group, 0x12);
    if (i.first || i.workspace_count != kGroup0Rows || i.parameter_count != 55)
        return policy(observation::Reason::group_range);
    if (i.program_index || i.program_count != 1) return policy(observation::Reason::program_identity);
    std::array<std::byte, 8> directory{};
    if (const auto s = read(r, i.program_directory, 0, directory); !s.ok()) return s;
    i.program_bytes = field<std::uint16_t>(directory, 2); i.program_relative = field<std::uint32_t>(directory, 4);
    if (!i.program_relative || !add(i.raw, i.program_relative, i.program)) return {Reason::pointer_range};
    i.composite = field<Address>(owner, 0x238);
    std::array<std::byte, 0x128> composite{};
    if (const auto s = read(r, i.composite, 0, composite); !s.ok()) return s;
    i.composite_owner = field<Address>(composite, 8); i.body_array = field<Address>(composite, 0x48);
    const auto body_count = field<std::uint32_t>(composite, 0x10);
    if (i.composite_owner != i.owner) return policy(observation::Reason::body_identity);
    if (!body_count || body_count > observation::kMaxBodies) return policy(observation::Reason::body_limit);
    m.body_count = static_cast<std::uint8_t>(body_count);
    std::array<std::byte, 0x110 * observation::kMaxBodies> bodies{};
    if (const auto s = copy(r, i.body_array, 0, bodies.data(), 0x110u * body_count); !s.ok()) return s;
    for (std::size_t b = 0; b < body_count; ++b) {
        auto& body = m.bodies[b];
        const auto at = b * 0x110;
        if (!add(i.body_array, at, body.body)) return {Reason::pointer_range};
        body.owner = field<Address>(bodies, at + 0x68); body.native = field<Address>(bodies, at + 0x40);
        body.handle_id = field<std::uint32_t>(bodies, at + 0x50); body.handle_index = field<std::uint16_t>(bodies, at + 0x54);
        body.joint = field<std::int16_t>(bodies, at + 0xC4); body.source = field<std::uint8_t>(bodies, at + 0xC6);
        body.desired_mode = static_cast<std::uint8_t>((field<std::uint32_t>(bodies, at + 0xA0) >> 13) & 3u);
        if (body.owner != i.owner || !body.native || body.desired_mode != 2 || (body.source != 1 && body.source != 3))
            return policy(observation::Reason::body_identity);
        if (const auto s = read(r, body.native, 0x160, body.native_motion); !s.ok()) return s;
        if (body.source == 3) {
            if (body.joint < 0 || body.joint >= i.workspace_count) return policy(observation::Reason::joint_range);
            bool found = false;
            for (std::size_t j = 0; j < m.selected_count; ++j) found |= m.selected_joints[j] == body.joint;
            if (!found) {
                if (m.selected_count == observation::kMaxSelectedJoints) return policy(observation::Reason::joint_limit);
                m.selected_joints[m.selected_count++] = body.joint;
            }
        }
    }
    // Metadata never hashes or copies the program on the game path.
    return policy(observation::admit(m));
}
inline bool same_metadata(const observation::Metadata& a, const observation::Metadata& b) {
    if (!(a.identity == b.identity) || a.fg_flags != b.fg_flags || a.local_pose != b.local_pose ||
        a.job_flag14 != b.job_flag14 || a.raw_mode != b.raw_mode || a.fg_byte8 != b.fg_byte8 ||
        a.force_mode2 != b.force_mode2 || a.custom_callback != b.custom_callback ||
        a.initially_cached_prepass != b.initially_cached_prepass || a.body_count != b.body_count ||
        a.selected_count != b.selected_count) return false;
    for (std::size_t bidx = 0; bidx < a.body_count; ++bidx) if (!(a.bodies[bidx] == b.bodies[bidx])) return false;
    for (std::size_t j = 0; j < a.selected_count; ++j) if (a.selected_joints[j] != b.selected_joints[j]) return false;
    return true;
}
} // namespace detail

enum class MetadataRead : std::uint8_t { single_pass, paired_pass };
template <class Reader>
Status read_metadata(Reader& r, Address module, Address job, Address originalJointBlendSlot, observation::Metadata& out,
                     MetadataRead mode = MetadataRead::single_pass) {
    out = {};
    observation::Metadata first{}, second{};
    if (const auto s = detail::metadata_pass(r, module, job, originalJointBlendSlot, first); !s.ok()) return s;
    if (mode == MetadataRead::paired_pass) {
        if (const auto s = detail::metadata_pass(r, module, job, originalJointBlendSlot, second); !s.ok()) return s;
        if (!detail::same_metadata(first, second)) return {Reason::changed};
    }
    // Compact final owner-pair/collection recheck even on the single-pass event
    // path. A paired full pass is available for initial binding, not forced at
    // every event. Neither mode is a resource lease or a coherent snapshot.
    Filter after{};
    if (const auto s = fast_filter(r, module, job, after); !s.ok()) return s;
    const auto& i = first.identity;
    if (after.fg != i.fg || after.record != i.record || after.owner != i.owner || after.vtable != i.vtable)
        return {Reason::changed};
    Address record{}, fg{}, composite{}, array{};
    std::uint32_t owner_id{}, fg_id{}, count{};
    if (const auto s = detail::read(r, i.owner, 0xA8, record); !s.ok()) return s;
    if (const auto s = detail::read(r, i.owner, 0xBC, owner_id); !s.ok()) return s;
    if (const auto s = detail::read(r, i.owner, 0x1A8, fg); !s.ok()) return s;
    if (const auto s = detail::read(r, i.fg, 0x1B0, fg_id); !s.ok()) return s;
    if (const auto s = detail::read(r, i.owner, 0x238, composite); !s.ok()) return s;
    if (record != i.record || owner_id != i.id || fg != i.fg || fg_id != i.id || composite != i.composite)
        return {Reason::changed};
    if (const auto s = detail::read(r, composite, 0x10, count); !s.ok()) return s;
    if (const auto s = detail::read(r, composite, 0x48, array); !s.ok()) return s;
    if (count != first.body_count || array != i.body_array) return {Reason::changed};
    out = first;
    return {};
}

// Snapshot helpers require freshly admitted metadata and the caller's current
// native/FLS scope. Bracket them with fresh metadata checks at the selected hook;
// these helpers do not recursively redo metadata or manufacture phase evidence.
template <class Reader>
Status read_roots(Reader& r, const observation::Metadata& m, observation::RootCopy& out) {
    out = {};
    if (const auto why = observation::admit(m); why != observation::Reason::none) return detail::policy(why);
    if (const auto s = detail::read(r, m.identity.fg, 0x1C0, out.fg_root); !s.ok()) { out = {}; return s; }
    if (const auto s = detail::read(r, m.identity.cache, 0, out.cache_root); !s.ok()) { out = {}; return s; }
    out.complete = true;
    return {};
}
template <class Reader>
Status read_group0_matrices(Reader& r, const observation::Metadata& m, CaptureTiming timing, MatrixCapture& out) {
    out = {};
    if (timing != CaptureTiming::export_returned) return {Reason::invalid_timing};
    if (const auto why = observation::admit(m); why != observation::Reason::none) return detail::policy(why);
    if (const auto s = detail::read(r, m.identity.matrix_bank, 0, out.rows); !s.ok()) { out = {}; return s; }
    out.timing = timing; out.complete = true;
    return {};
}
template <class Reader>
Status read_selected_locators(Reader& r, const observation::Metadata& m, CaptureTiming timing, LocatorCapture& out) {
    out = {};
    if (timing != CaptureTiming::export_returned && timing != CaptureTiming::pre_consumer && timing != CaptureTiming::post_store)
        return {Reason::invalid_timing};
    if (const auto why = observation::admit(m); why != observation::Reason::none) return detail::policy(why);
    if (const auto s = detail::read(r, m.identity.cache, 0x40, out.valid_before); !s.ok()) { out = {}; return s; }
    for (std::size_t j = 0; j < m.selected_count; ++j) {
        const auto offset = static_cast<Address>(m.selected_joints[j]) * 32;
        if (const auto s = detail::read(r, m.identity.locator_bank, offset, out.rows[j]); !s.ok()) { out = {}; return s; }
        out.joints[j] = m.selected_joints[j];
    }
    if (const auto s = detail::read(r, m.identity.cache, 0x40, out.valid_after); !s.ok()) { out = {}; return s; }
    if (out.valid_before != out.valid_after) { out = {}; return {Reason::changed}; }
    out.count = m.selected_count; out.timing = timing; out.complete = true;
    return {};
}
template <class Reader>
Status read_program_once(Reader& r, const observation::Metadata& m, ProgramCapture& out) {
    if (out.attempted) return {Reason::already_attempted};
    out = {}; out.attempted = true;
    if (const auto why = observation::admit(m); why != observation::Reason::none) return detail::policy(why);
    const auto& i = m.identity;
    if (i.group_table_relative != 0x40 || i.program_relative != 0x5340 || i.program_bytes != kCandidateProgramBytes)
        return {Reason::candidate_extent};
    if (const auto s = detail::read(r, i.program, 0, out.bytes); !s.ok()) { out = {}; out.attempted = true; return s; }
    out.identity = i; out.complete = true;
    return {}; // Later owned-byte hashing establishes identity, not execution.
}
static_assert(std::is_trivially_copyable_v<MatrixCapture> && std::is_trivially_copyable_v<LocatorCapture> &&
              std::is_trivially_copyable_v<ProgramCapture>);
} // namespace coop::enemy_jointblend_capture
