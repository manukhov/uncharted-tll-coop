#pragma once

// Value-only policy for ONE normal JointBlend invocation, not a memory reader,
// hook installer, lifetime lease, physics agreement test or write admission.
// Callers supply fresh copied fields and independently verified native/FLS scope.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace coop::enemy_jointblend_observation {
inline constexpr std::size_t kMaxSamples = 6, kMaxBodies = 16, kMaxSelectedJoints = 16;
inline constexpr std::uintptr_t kPirateRva = 0x294B9D0;
inline constexpr std::uint32_t kNormalSkeleton = 0xBD7EEE2F, kMediumSkeleton = 0xFC65A17A;
inline constexpr std::uint32_t kNormalDatabase = 0x2628A5DC, kMediumDatabase = 0xF181B564;
inline constexpr std::uint32_t kExportId = 0x248B1AAE;
inline constexpr std::uintptr_t kBodyStoreRva = 0x1CF3790;
inline constexpr std::uintptr_t kBodyStoreCallRva = 0x1CF0F36, kBodyStoreReturnRva = 0x1CF0F3B;
inline constexpr std::uintptr_t kRefreshAncestorReturnRva = 0x1CF5872;
inline constexpr bool kNumericalConsistencyProven = false, kWriteAdmission = false;
using Fingerprint = std::array<std::uint8_t, 32>;
constexpr Fingerprint fingerprint(const char (&hex)[65]) {
    Fingerprint out{};
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<std::uint8_t>(16 * nibble(hex[2 * i]) + nibble(hex[2 * i + 1]));
    return out;
}
inline constexpr auto kNormalProgramSha256 = fingerprint(
    "eefeb3067ed1e47ba98c0fe2f629e27f6193037ca20b636ceb4e6a7b4afc6710");
inline constexpr auto kMediumProgramSha256 = fingerprint(
    "84082e471fa7a253c10888b38a9ec421b935e04f16077ccca7808d4dd58c429a");

enum class Reason : std::uint8_t {
    none, reused_scope, sample_limit, bad_nonce, bad_clock, budget, selection_changed, session_stopped,
    owner_links, cache_links, group_range, unknown_skeleton, program_identity, pointer_range,
    excluded_branch, body_limit, joint_limit, body_identity, joint_range,
    reentry, invocation_mismatch, identity_changed, stage_order, root_copy,
    export_mismatch, body_store_mismatch, locator_mismatch, incomplete
};
enum class Stage : std::uint8_t {
    empty, normal_entry, reset_returned, process_entered, export_entered,
    export_returned, process_returned, primary_entered, primary_returned, closed
};
// Authored-byte identity only, never evidence of cmd16/operator execution.
enum class ProgramMatch : std::uint8_t { identity_unverified, normal_candidate, medium_candidate };
enum class Comparison : std::uint8_t { not_performed, reported_equal, reported_different };
enum class CapturePoint : std::uint8_t { none, pre_consumer, post_store };
enum class LocatorClass : std::uint8_t {
    not_applicable, pre_consumer_invalid_not_consumed, post_store_invalid_unverified, valid_unverified,
    valid_reported_equal, valid_reported_different
};

struct Identity {
    // ORIGINAL JointBlend entry return slot published by the group MASM bridge.
    // Not the Process bridge's separate original slot or a C++ wrapper/game PC.
    std::uintptr_t module{}, job{}, native_return_slot{}, owner{}, vtable{};
    std::uintptr_t record{}, record_owner{}, owner_record{}, fg{}, owner_fg{}, fg_record{};
    // Art+24 is a database key; raw+4 is a different skeleton identity.
    std::uint32_t id{}, owner_id{}, fg_id{}, art_database_id{}, raw_skeleton_id{};
    std::uintptr_t job_control{}, fg_control{}, owner_control{}, control_fg{};
    std::uintptr_t art{}, active_art18{}, active_art1a0{}, cache_art{};
    std::uintptr_t raw{}, art_raw{}, cache_raw{}, cache{}, locator_bank{}, matrix_bank{};
    std::uintptr_t group_descriptor{}, program_directory{}, program{}, composite{}, composite_owner{}, body_array{};
    std::uint32_t group_table_relative{}, program_relative{}, program_bytes{};
    std::uint16_t first{}, workspace_count{}, parameter_count{};
    // Group+6 is the first index in the GLOBAL program directory; group+F is
    // this group's program count, not the global directory's capacity.
    std::uint16_t program_index{}, program_count{};
    bool operator==(const Identity&) const = default;
};
struct BodyDescriptor {
    std::uintptr_t body{}, owner{}, native{};
    std::uint32_t handle_id{};
    std::uint16_t handle_index{};
    std::int16_t joint{};
    std::uint8_t source{}, desired_mode{}, native_motion{};
    bool operator==(const BodyDescriptor&) const = default;
};
struct Metadata {
    Identity identity{};
    std::uint32_t fg_flags{};
    std::uintptr_t local_pose{};
    std::uint8_t job_flag14{}, raw_mode{}, fg_byte8{};
    bool force_mode2{}, custom_callback{}, initially_cached_prepass{}, fingerprint_present{};
    Fingerprint program_sha256{};
    std::uint8_t body_count{}, selected_count{};
    std::array<BodyDescriptor, kMaxBodies> bodies{};
    std::array<std::int16_t, kMaxSelectedJoints> selected_joints{};
};
struct Session {
    Identity selected{}; // Inert selection bytes; NEVER dereference on reuse.
    std::array<BodyDescriptor, kMaxBodies> selected_bodies{};
    std::array<std::int16_t, kMaxSelectedJoints> selected_joints{};
    std::uint64_t last_nonce{};
    std::uint32_t attempted{};
    std::uint8_t selected_body_count{}, selected_count{};
    bool selected_once{}, stopped{};
};
struct Witness {
    Identity identity{};
    std::uint64_t nonce{};
    // Must come from exact native-return-slot + live fiber ownership, NOT
    // equal job/Fg pointers, an OS thread id, timestamps or a packet token.
    bool native_scope_verified{};
};
struct RootCopy {
    std::array<std::uint32_t, 16> fg_root{}, cache_root{};
    bool complete{};
};
struct Export {
    std::uintptr_t descriptor{}, workspace{}, destination{};
    std::uint32_t id{};
    std::uint16_t first{}, count{};
    bool operator==(const Export&) const = default;
};
struct Locator {
    CapturePoint point{};
    bool valid{};
    Comparison comparison{}; // Supplied result; this policy does no pose math.
};
struct StoreWitness {
    std::uintptr_t target_rva{}, call_rva{}, return_rva{}, refresh_ancestor_return_rva{};
    std::uint32_t native_eax{}; // Recorded verbatim, not normalized or required to be1.
    bool native_returned{}, ancestry_verified{};
};
struct Scope {
    Metadata metadata{};
    Export export_call{};
    std::uint64_t nonce{}, budget_limit{}, observer_ticks{};
    std::uint32_t stored_mask{};
    Stage stage{}, failure_stage{};
    Reason reason{};
    ProgramMatch program_match{};
    std::array<LocatorClass, kMaxBodies> locators{};
    std::array<std::uint32_t, kMaxBodies> store_return_eax{};
};
static_assert(std::is_trivially_copyable_v<Scope> && std::is_standard_layout_v<Scope>);
static_assert(sizeof(Scope) < 2048);

constexpr bool points_at(std::uintptr_t base, std::uintptr_t offset, std::uintptr_t value) {
    return base && offset <= std::numeric_limits<std::uintptr_t>::max() - base && base + offset == value;
}
constexpr bool fits_span(std::uintptr_t base, std::uintptr_t bytes) {
    // Require a representable exclusive end as well as every addressed byte.
    return base && bytes && bytes <= std::numeric_limits<std::uintptr_t>::max() - base;
}
inline bool candidate_skeleton_pair(const Identity& i) {
    return (i.art_database_id == kNormalDatabase && i.raw_skeleton_id == kNormalSkeleton) ||
        (i.art_database_id == kMediumDatabase && i.raw_skeleton_id == kMediumSkeleton);
}
inline ProgramMatch classify_program(const Metadata& m) {
    const auto& i = m.identity;
    if (!candidate_skeleton_pair(i) || !m.fingerprint_present || i.group_table_relative != 0x40 || i.first || i.workspace_count != 90 || i.parameter_count != 55 ||
        i.program_count != 1 || i.program_index || i.program_relative != 0x5340 || i.program_bytes != 0x40B0)
        return ProgramMatch::identity_unverified;
    if (i.raw_skeleton_id == kNormalSkeleton && m.program_sha256 == kNormalProgramSha256)
        return ProgramMatch::normal_candidate;
    if (i.raw_skeleton_id == kMediumSkeleton && m.program_sha256 == kMediumProgramSha256)
        return ProgramMatch::medium_candidate;
    return ProgramMatch::identity_unverified;
}
inline Reason admit(const Metadata& m) {
    const auto& i = m.identity;
    if (!i.job || !i.native_return_slot || (i.native_return_slot & 15u) != 8 || !i.owner || !i.record ||
        !i.id || !points_at(i.module, kPirateRva, i.vtable) || i.record_owner != i.owner ||
        i.owner_record != i.record || i.owner_id != i.id || !i.fg || i.owner_fg != i.fg ||
        i.fg_record != i.record || i.fg_id != i.id) return Reason::owner_links;
    if (!i.art || i.active_art18 != i.art || i.active_art1a0 != i.art || i.cache_art != i.art ||
        !i.raw || i.art_raw != i.raw || i.cache_raw != i.raw || !points_at(i.fg, 0x30, i.cache) ||
        !i.matrix_bank || !i.locator_bank) return Reason::cache_links;
    if (!candidate_skeleton_pair(i)) return Reason::unknown_skeleton;
    if (i.first || i.workspace_count != 90 || i.parameter_count != 55 ||
        !i.group_table_relative || !points_at(i.raw, i.group_table_relative, i.group_descriptor)) return Reason::group_range;
    // This observer deliberately admits only the two audited group0 layouts
    // (first program0, one program). Do not generalize index<count: the index
    // is global, whereas count belongs to this one group.
    if (!i.program_directory || i.program_index != 0 || i.program_count != 1 || !i.program_bytes ||
        i.program_bytes > 0x100000 || !points_at(i.raw, i.program_relative, i.program))
        return Reason::program_identity;
    const auto effective_mode = ((m.force_mode2 || (m.fg_flags & 2u)) && m.local_pose) ? 2 : m.raw_mode;
    if (m.job_flag14 || !m.raw_mode || effective_mode != 2 || !(m.fg_flags & 0x20u) || !(m.fg_byte8 & 1u) ||
        !i.job_control || i.job_control != i.fg_control || i.owner_control != i.job_control ||
        i.control_fg != i.fg || m.custom_callback || m.initially_cached_prepass)
        return Reason::excluded_branch;
    if (!m.body_count || m.body_count > kMaxBodies || !i.composite || !i.body_array) return Reason::body_limit;
    if (i.composite_owner != i.owner) return Reason::body_identity;
    if (!m.selected_count || m.selected_count > kMaxSelectedJoints) return Reason::joint_limit;
    if (!fits_span(i.job, 0x18) || !fits_span(i.native_return_slot, 8) || !fits_span(i.owner, 0x240) ||
        !fits_span(i.vtable, 0x418) || !fits_span(i.record, 16) || !fits_span(i.fg, 0x2A8) ||
        !fits_span(i.job_control, 0x28) || !fits_span(i.art, 0x48) || !fits_span(i.raw, 0x40) ||
        !fits_span(i.group_descriptor, 32) || !fits_span(i.program_directory, 8u * i.program_count) ||
        !fits_span(i.program, i.program_bytes) || !fits_span(i.composite, 0x128) ||
        !fits_span(i.body_array, 0x110u * m.body_count) ||
        !fits_span(i.matrix_bank, 64u * i.workspace_count) || !fits_span(i.locator_bank, 32u * i.workspace_count))
        return Reason::pointer_range;
    for (std::size_t j = 0; j < m.selected_count; ++j) {
        if (m.selected_joints[j] < 0 || m.selected_joints[j] >= i.workspace_count) return Reason::joint_range;
        for (std::size_t k = 0; k < j; ++k)
            if (m.selected_joints[j] == m.selected_joints[k]) return Reason::joint_range;
    }
    unsigned roots = 0, joints = 0;
    for (std::size_t b = 0; b < m.body_count; ++b) {
        const auto& body = m.bodies[b];
        if (!points_at(i.body_array, 0x110 * b, body.body) || !body.native || body.owner != i.owner || body.desired_mode != 2 ||
            body.native_motion != 4 || (body.source != 1 && body.source != 3)) return Reason::body_identity;
        if (!fits_span(body.native, 0x161)) return Reason::pointer_range;
        for (std::size_t k = 0; k < b; ++k)
            if (body.body == m.bodies[k].body || body.native == m.bodies[k].native) return Reason::body_identity;
        if (body.source == 1) { ++roots; continue; }
        ++joints;
        bool selected = false;
        for (std::size_t j = 0; j < m.selected_count; ++j) selected |= body.joint == m.selected_joints[j];
        if (!selected || body.joint < 0 || body.joint >= i.workspace_count) return Reason::joint_range;
    }
    return roots == 1 && joints ? Reason::none : Reason::body_identity;
}
inline bool reject(Scope& s, Reason why) {
    if (s.reason == Reason::none) { s.reason = why; s.failure_stage = s.stage; }
    return false;
}
inline bool begin(Scope& s, Session& session, const Metadata& m, std::uint64_t nonce, std::uint64_t frequency) {
    if (s.stage != Stage::empty || s.reason != Reason::none) return reject(s, Reason::reused_scope);
    if (session.stopped) return reject(s, Reason::session_stopped);
    if (session.attempted >= kMaxSamples) return reject(s, Reason::sample_limit);
    ++session.attempted; // Rejected and incomplete attempts also consume quota.
    if (!nonce || nonce <= session.last_nonce) return reject(s, Reason::bad_nonce);
    session.last_nonce = nonce;
    s.nonce = nonce; s.metadata = m; s.budget_limit = frequency / 1000;
    if (!s.budget_limit) return reject(s, Reason::bad_clock);
    if (const auto why = admit(m); why != Reason::none) return reject(s, why);
    auto selection = m.identity;
    selection.job = selection.native_return_slot = 0; // Different invocation, same selected actor/resources.
    if (!session.selected_once) {
        session.selected = selection; session.selected_body_count = m.body_count;
        session.selected_count = m.selected_count; session.selected_joints = m.selected_joints;
        session.selected_bodies = m.bodies; session.selected_once = true;
    } else {
        bool same = session.selected == selection && session.selected_body_count == m.body_count && session.selected_count == m.selected_count;
        for (std::size_t b = 0; same && b < m.body_count; ++b) same = session.selected_bodies[b] == m.bodies[b];
        for (std::size_t j = 0; same && j < m.selected_count; ++j) same = session.selected_joints[j] == m.selected_joints[j];
        if (!same) { session.stopped = true; return reject(s, Reason::selection_changed); }
    }
    s.program_match = classify_program(m);
    s.stage = Stage::normal_entry;
    return true;
}
// Charge ONLY observer work, including admission/copying. Never surround stock
// calls with this interval. Post-call containment is not preemption or a hard
// real-time bound; the integrator must also use bounded copies/no allocation.
inline bool charge(Scope& s, std::uint64_t start, std::uint64_t end, bool clock_ok = true) {
    if (s.reason != Reason::none) return false;
    if (!clock_ok || !s.budget_limit || end < start) return reject(s, Reason::bad_clock);
    if (s.observer_ticks > s.budget_limit || end - start > s.budget_limit - s.observer_ticks)
        return reject(s, Reason::budget);
    s.observer_ticks += end - start;
    return true;
}
inline bool witness(Scope& s, const Witness& w, Stage expected) {
    if (s.reason != Reason::none) return false;
    if (!w.native_scope_verified || !w.nonce || w.nonce != s.nonce) return reject(s, Reason::invocation_mismatch);
    if (!(w.identity == s.metadata.identity)) return reject(s, Reason::identity_changed);
    return s.stage == expected || reject(s, Reason::stage_order);
}
inline bool reentered(Scope& s) { return reject(s, Reason::reentry); }
inline bool reset_returned(Scope& s, const Witness& w, const RootCopy& roots) {
    if (!witness(s, w, Stage::normal_entry)) return false;
    if (!roots.complete || roots.fg_root != roots.cache_root) return reject(s, Reason::root_copy);
    s.stage = Stage::reset_returned; return true;
}
inline bool process_entered(Scope& s, const Witness& w, std::uint32_t group, bool first_site) {
    if (!witness(s, w, Stage::reset_returned)) return false;
    if (group || !first_site) return reject(s, Reason::stage_order); // Mode2's first inline site only.
    s.stage = Stage::process_entered; return true;
}
inline bool cmd24_entered(Scope& s, const Witness& w, const Export& e) {
    if (!witness(s, w, Stage::process_entered)) return false;
    const auto& i = s.metadata.identity;
    if (e.id != kExportId || !fits_span(e.workspace, 64u * (i.workspace_count + 1u)) || e.descriptor != i.group_descriptor ||
        e.destination != i.matrix_bank || e.first != i.first || e.count != i.workspace_count)
        return reject(s, Reason::export_mismatch);
    s.export_call = e; s.stage = Stage::export_entered; return true;
}
inline bool cmd24_returned(Scope& s, const Witness& w, const Export& e) {
    if (!witness(s, w, Stage::export_entered)) return false;
    if (!(e == s.export_call)) return reject(s, Reason::export_mismatch);
    s.stage = Stage::export_returned; return true;
}
inline bool process_returned(Scope& s, const Witness& w) {
    if (!witness(s, w, Stage::export_returned)) return false;
    s.stage = Stage::process_returned; return true;
}
inline bool primary_entered(Scope& s, const Witness& w, std::uintptr_t composite) {
    if (!witness(s, w, Stage::process_returned)) return false;
    if (composite != s.metadata.identity.composite) return reject(s, Reason::body_identity);
    s.stage = Stage::primary_entered; return true;
}
constexpr LocatorClass classify_locator(const Locator& l) {
    if (l.point == CapturePoint::none) return LocatorClass::not_applicable;
    if (!l.valid) return l.point == CapturePoint::pre_consumer ? LocatorClass::pre_consumer_invalid_not_consumed
                                                           : LocatorClass::post_store_invalid_unverified;
    if (l.comparison == Comparison::reported_equal) return LocatorClass::valid_reported_equal;
    if (l.comparison == Comparison::reported_different) return LocatorClass::valid_reported_different;
    return LocatorClass::valid_unverified;
}
inline bool body_stored(Scope& s, const Witness& w, std::size_t index,
                       const BodyDescriptor& body, const StoreWitness& store, const Locator& locator) {
    if (!witness(s, w, Stage::primary_entered)) return false;
    if (index >= s.metadata.body_count || store.target_rva != kBodyStoreRva ||
        store.call_rva != kBodyStoreCallRva || store.return_rva != kBodyStoreReturnRva ||
        store.refresh_ancestor_return_rva != kRefreshAncestorReturnRva ||
        !store.native_returned || !store.ancestry_verified ||
        !(body == s.metadata.bodies[index]) || (s.stored_mask & (1u << index)))
        return reject(s, Reason::body_store_mismatch);
    const auto classification = classify_locator(locator);
    if (locator.point != (body.source == 3 ? CapturePoint::post_store : CapturePoint::none))
        return reject(s, Reason::locator_mismatch);
    s.locators[index] = classification;
    if (classification == LocatorClass::valid_reported_different) return reject(s, Reason::locator_mismatch);
    s.store_return_eax[index] = store.native_eax;
    s.stored_mask |= 1u << index;
    return true; // Actual selected store return, NOT retention/refresh completion/Havok queue/numerical agreement.
}
inline bool primary_returned(Scope& s, const Witness& w) {
    if (!witness(s, w, Stage::primary_entered)) return false;
    s.stage = Stage::primary_returned; return true;
}
inline bool close(Scope& s, const Witness& w) {
    if (!witness(s, w, Stage::primary_returned)) return false;
    if (s.stored_mask != ((1u << s.metadata.body_count) - 1u)) return reject(s, Reason::incomplete);
    s.stage = Stage::closed; return true;
}
// This is ONLY complete event-chain evidence. Missing locator comparisons and
// unknown program identity remain explicitly unverified in this result.
inline bool completed_witness(const Scope& s) { return s.stage == Stage::closed && s.reason == Reason::none; }
inline bool selected_locator_comparisons_reported_equal(const Scope& s) {
    if (!completed_witness(s)) return false;
    for (std::size_t b = 0; b < s.metadata.body_count; ++b)
        if (s.metadata.bodies[b].source == 3 && s.locators[b] != LocatorClass::valid_reported_equal) return false;
    return true; // Reported comparisons only; no pose math is performed here.
}
// Even a completed witness and pinned producer identity do not prove numerical
// root/E0/D8/body consistency, input freshness, residency or cross-copy identity.
} // namespace coop::enemy_jointblend_observation
