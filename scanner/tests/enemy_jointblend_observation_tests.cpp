#include "enemy_jointblend_observation.h"

#include <functional>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace {
namespace obs = coop::enemy_jointblend_observation;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; return false; } } while (false)

obs::Metadata metadata() {
    obs::Metadata m{};
    auto& i = m.identity;
    i.module = 0x140000000ULL; i.job = 0x10000; i.native_return_slot = 0x110008;
    i.owner = 0x20000; i.vtable = i.module + obs::kPirateRva;
    i.record = 0x21000; i.record_owner = i.owner; i.owner_record = i.record;
    i.fg = 0x30000; i.owner_fg = i.fg; i.fg_record = i.record;
    i.id = i.owner_id = i.fg_id = 37;
    i.art_database_id = obs::kNormalDatabase; i.raw_skeleton_id = obs::kNormalSkeleton;
    i.job_control = i.fg_control = i.owner_control = 0x32000; i.control_fg = i.fg;
    i.art = i.active_art18 = i.active_art1a0 = i.cache_art = 0x40000;
    i.raw = i.art_raw = i.cache_raw = 0x50000; i.cache = i.fg + 0x30;
    i.matrix_bank = 0x70000; i.locator_bank = 0x80000;
    i.group_table_relative = 0x40; i.group_descriptor = i.raw + i.group_table_relative; i.program_directory = i.raw + 0x140;
    i.program_relative = 0x5340; i.program = i.raw + i.program_relative;
    i.program_bytes = 0x40B0; i.workspace_count = 90; i.parameter_count = 55; i.program_count = 1;
    i.composite = 0x60000; i.composite_owner = i.owner; i.body_array = 0x61000;
    m.fg_flags = 0x20; m.fg_byte8 = 1; m.raw_mode = 2; m.local_pose = 0x33000;
    m.fingerprint_present = true; m.program_sha256 = obs::kNormalProgramSha256;
    m.body_count = 3; m.selected_count = 2; m.selected_joints[0] = 3; m.selected_joints[1] = 34;
    for (std::size_t b = 0; b < m.body_count; ++b) {
        auto& body = m.bodies[b];
        body.body = i.body_array + b * 0x110; body.native = 0xA0000 + b * 0x200;
        body.owner = i.owner; body.handle_id = static_cast<std::uint32_t>(100 + b);
        body.handle_index = static_cast<std::uint16_t>(10 + b);
        body.source = b == 0 ? 1 : 3; body.joint = b == 1 ? 3 : 34;
        body.desired_mode = 2; body.native_motion = 4;
    }
    return m;
}
struct Fixture {
    obs::Metadata m = metadata();
    obs::Scope s{};
    obs::Session session{};
    obs::Witness w{m.identity, 1, true};
    obs::Export e{m.identity.group_descriptor, 0xB0000, m.identity.matrix_bank, obs::kExportId, 0, 90};
    obs::RootCopy roots{};
    Fixture() {
        roots.complete = true;
        for (std::size_t j = 0; j < roots.fg_root.size(); ++j)
            roots.fg_root[j] = roots.cache_root[j] = static_cast<std::uint32_t>(0x3f000000 + j);
    }
};
bool enter_process(Fixture& f) {
    CHECK(obs::begin(f.s, f.session, f.m, 1, 10000000));
    CHECK(obs::reset_returned(f.s, f.w, f.roots));
    CHECK(obs::process_entered(f.s, f.w, 0, true));
    return true;
}
bool enter_primary(Fixture& f) {
    CHECK(enter_process(f));
    CHECK(obs::cmd24_entered(f.s, f.w, f.e));
    CHECK(obs::cmd24_returned(f.s, f.w, f.e));
    CHECK(obs::process_returned(f.s, f.w));
    CHECK(obs::primary_entered(f.s, f.w, f.m.identity.composite));
    return true;
}
obs::StoreWitness store_witness() {
    return {obs::kBodyStoreRva, obs::kBodyStoreCallRva, obs::kBodyStoreReturnRva,
            obs::kRefreshAncestorReturnRva, 0, true, true};
}
bool body(Fixture& f, std::size_t index, obs::Locator locator = {obs::CapturePoint::post_store, false, obs::Comparison::not_performed}) {
    if (index == 0) locator = {};
    return obs::body_stored(f.s, f.w, index, f.m.bodies[index], store_witness(), locator);
}

bool complete_witness_is_not_numerical_or_write_admission() {
    Fixture f;
    CHECK(enter_primary(f));
    CHECK(obs::charge(f.s, 10, 4000));
    for (std::size_t i = 0; i < f.m.body_count; ++i) CHECK(body(f, i));
    CHECK(obs::primary_returned(f.s, f.w));
    CHECK(obs::close(f.s, f.w));
    CHECK(obs::completed_witness(f.s));
    CHECK(f.s.program_match == obs::ProgramMatch::normal_candidate);
    CHECK(!obs::kNumericalConsistencyProven && !obs::kWriteAdmission);
    CHECK(f.s.locators[0] == obs::LocatorClass::not_applicable);
    CHECK(f.s.locators[1] == obs::LocatorClass::post_store_invalid_unverified);
    CHECK(!obs::selected_locator_comparisons_reported_equal(f.s));
    return true;
}
bool exact_owner_and_cache_links() {
    const std::vector<std::function<void(obs::Identity&)>> changes{
        [](auto& x) { x.module = 0; }, [](auto& x) { ++x.vtable; },
        [](auto& x) { x.job = 0; }, [](auto& x) { x.native_return_slot = 0; },
        [](auto& x) { ++x.native_return_slot; }, [](auto& x) { x.owner = 0; },
        [](auto& x) { x.record = 0; }, [](auto& x) { ++x.record_owner; },
        [](auto& x) { ++x.owner_record; }, [](auto& x) { ++x.owner_id; },
        [](auto& x) { x.fg = 0; }, [](auto& x) { ++x.owner_fg; },
        [](auto& x) { ++x.fg_record; }, [](auto& x) { ++x.fg_id; },
        [](auto& x) { x.art = 0; }, [](auto& x) { ++x.active_art18; },
        [](auto& x) { ++x.active_art1a0; }, [](auto& x) { ++x.cache_art; },
        [](auto& x) { x.raw = 0; }, [](auto& x) { ++x.art_raw; },
        [](auto& x) { ++x.cache_raw; }, [](auto& x) { ++x.cache; },
        [](auto& x) { x.matrix_bank = 0; }, [](auto& x) { x.locator_bank = 0; },
        [](auto& x) { x.id = x.owner_id = x.fg_id = 0; }
    };
    for (const auto& change : changes) { auto m = metadata(); change(m.identity); CHECK(obs::admit(m) != obs::Reason::none); }
    CHECK(!obs::points_at(UINTPTR_MAX - 1, 2, 0));
    return true;
}
bool group_range_and_candidate_identity() {
    for (const auto& change : std::vector<std::function<void(obs::Identity&)>>{
        [](auto& x) { x.first = 1; }, [](auto& x) { x.workspace_count = 55; },
        [](auto& x) { x.parameter_count = 90; }, [](auto& x) { ++x.group_descriptor; },
        [](auto& x) { x.group_table_relative = 0; },
        [](auto& x) { x.art_database_id = x.raw_skeleton_id = 0xC39CA709; },
        [](auto& x) { x.art_database_id = obs::kNormalSkeleton; },
        [](auto& x) { x.art_database_id = obs::kMediumDatabase; },
        [](auto& x) { x.raw_skeleton_id = obs::kNormalDatabase; },
        [](auto& x) { ++x.raw_skeleton_id; }, [](auto& x) { x.program_directory = 0; },
        [](auto& x) { x.program_count = 0; }, [](auto& x) { x.program_count = 2; },
        [](auto& x) { x.program_index = 1; x.program_count = 2; }, [](auto& x) { ++x.program; },
        [](auto& x) { x.program_bytes = 0; }, [](auto& x) { x.program_bytes = 0x100001; }
    }) { auto m = metadata(); change(m.identity); CHECK(obs::admit(m) != obs::Reason::none); }
    auto m = metadata(); m.identity.art_database_id = obs::kMediumDatabase; m.identity.raw_skeleton_id = obs::kMediumSkeleton;
    CHECK(obs::admit(m) == obs::Reason::none);
    CHECK(obs::classify_program(m) == obs::ProgramMatch::identity_unverified);
    m.program_sha256 = obs::kMediumProgramSha256;
    CHECK(obs::classify_program(m) == obs::ProgramMatch::medium_candidate);
    return true;
}
bool unknown_program_can_be_observed_but_not_joined_to_asset_proof() {
    for (int mutation = 0; mutation < 5; ++mutation) {
        auto m = metadata();
        if (mutation == 0) m.fingerprint_present = false;
        if (mutation == 1) ++m.program_sha256[0];
        if (mutation == 2) { m.identity.program_relative += 16; m.identity.program += 16; }
        if (mutation == 3) ++m.identity.program_bytes;
        if (mutation == 4) { m.identity.group_table_relative += 32; m.identity.group_descriptor += 32; }
        CHECK(obs::admit(m) == obs::Reason::none);
        CHECK(obs::classify_program(m) == obs::ProgramMatch::identity_unverified);
    }
    return true;
}
bool excluded_native_branches() {
    for (const auto& change : std::vector<std::function<void(obs::Metadata&)>>{
        [](auto& x) { x.job_flag14 = 1; }, [](auto& x) { x.raw_mode = 0; },
        [](auto& x) { x.raw_mode = 1; }, [](auto& x) { x.fg_flags &= ~0x20u; },
        [](auto& x) { x.identity.job_control = 0; }, [](auto& x) { ++x.identity.fg_control; },
        [](auto& x) { ++x.identity.owner_control; }, [](auto& x) { ++x.identity.control_fg; },
        [](auto& x) { x.fg_byte8 = 0; },
        [](auto& x) { x.custom_callback = true; }, [](auto& x) { x.initially_cached_prepass = true; }
    }) { auto m = metadata(); change(m); CHECK(obs::admit(m) == obs::Reason::excluded_branch); }
    auto m = metadata(); m.raw_mode = 1; m.force_mode2 = true;
    CHECK(obs::admit(m) == obs::Reason::none);
    m.local_pose = 0; CHECK(obs::admit(m) == obs::Reason::excluded_branch);
    m.local_pose = 1; m.force_mode2 = false; m.fg_flags |= 2;
    CHECK(obs::admit(m) == obs::Reason::none);
    m.raw_mode = 0; CHECK(obs::admit(m) == obs::Reason::excluded_branch);
    return true;
}
bool bounded_body_and_actual_joint_coverage() {
    for (const auto& change : std::vector<std::function<void(obs::Metadata&)>>{
        [](auto& x) { x.body_count = 0; }, [](auto& x) { x.body_count = 17; },
        [](auto& x) { x.selected_count = 0; }, [](auto& x) { x.selected_count = 17; },
        [](auto& x) { x.identity.composite = 0; }, [](auto& x) { x.identity.body_array = 0; },
        [](auto& x) { ++x.identity.composite_owner; }, [](auto& x) { ++x.bodies[1].body; },
        [](auto& x) { ++x.bodies[1].owner; }, [](auto& x) { x.bodies[1].native = 0; },
        [](auto& x) { x.bodies[1].body = x.bodies[0].body; },
        [](auto& x) { x.bodies[1].native = x.bodies[0].native; },
        [](auto& x) { x.bodies[1].desired_mode = 3; }, [](auto& x) { x.bodies[1].native_motion = 2; },
        [](auto& x) { x.bodies[1].source = 4; }, [](auto& x) { x.bodies[1].joint = -1; },
        [](auto& x) { x.bodies[1].joint = 90; }, [](auto& x) { x.selected_joints[1] = 3; },
        [](auto& x) { x.selected_joints[1] = 90; }, [](auto& x) { x.selected_joints[1] = -1; },
        [](auto& x) { x.selected_joints[1] = 33; }, [](auto& x) { x.bodies[1].source = 1; }
    }) { auto m = metadata(); change(m); CHECK(obs::admit(m) != obs::Reason::none); }
    auto m = metadata(); m.bodies[1].joint = m.selected_joints[0] = 89;
    CHECK(obs::admit(m) == obs::Reason::none); // Not limited to historical ten indices or parameter prefix55.
    m.body_count = 4; m.bodies[3] = m.bodies[2];
    m.bodies[3].body += 0x110; m.bodies[3].native += 0x200;
    CHECK(obs::admit(m) == obs::Reason::none); // Multiple bodies may use joint34.
    return true;
}
bool pointer_span_ends_never_wrap() {
    constexpr auto top = std::numeric_limits<std::uintptr_t>::max();
    CHECK(obs::fits_span(top - 16, 16)); CHECK(!obs::fits_span(top - 15, 16));
    CHECK(!obs::fits_span(0, 1) && !obs::fits_span(1, 0));
    for (const auto& change : std::vector<std::function<void(obs::Metadata&)>>{
        [](auto& x) { x.identity.job = UINTPTR_MAX - 8; },
        [](auto& x) { x.identity.native_return_slot = UINTPTR_MAX - 7; },
        [](auto& x) { x.identity.matrix_bank = UINTPTR_MAX - 64; },
        [](auto& x) { x.identity.locator_bank = UINTPTR_MAX - 32; },
        [](auto& x) { x.identity.program_directory = UINTPTR_MAX - 4; },
        [](auto& x) { x.identity.body_array = UINTPTR_MAX - 0x100; },
        [](auto& x) { x.identity.composite = UINTPTR_MAX - 0x80; },
        [](auto& x) { x.bodies[2].native = UINTPTR_MAX - 0x100; },
        [](auto& x) { x.identity.job_control = x.identity.fg_control = x.identity.owner_control = UINTPTR_MAX - 8; },
        [](auto& x) { x.identity.vtable = UINTPTR_MAX - 0x100; x.identity.module = x.identity.vtable - obs::kPirateRva; },
        [](auto& x) { x.identity.record = x.identity.owner_record = x.identity.fg_record = UINTPTR_MAX - 8; },
        [](auto& x) { x.identity.fg = x.identity.owner_fg = x.identity.control_fg = UINTPTR_MAX - 0x100; x.identity.cache = x.identity.fg + 0x30; },
        [](auto& x) { x.identity.art = x.identity.active_art18 = x.identity.active_art1a0 = x.identity.cache_art = UINTPTR_MAX - 0x20; },
        [](auto& x) {
            auto& i = x.identity; i.owner = i.record_owner = i.composite_owner = UINTPTR_MAX - 0x100;
            for (std::size_t b = 0; b < x.body_count; ++b) x.bodies[b].owner = i.owner;
        },
        [](auto& x) {
            auto& i = x.identity; i.raw = i.art_raw = i.cache_raw = UINTPTR_MAX - 0x80;
            i.group_descriptor = i.raw + 0x40; i.program_relative = 0x50; i.program = i.raw + 0x50;
        },
        [](auto& x) {
            auto& i = x.identity; i.raw = i.art_raw = i.cache_raw = UINTPTR_MAX - 0x80;
            i.group_table_relative = 0x70; i.group_descriptor = i.raw + 0x70;
            i.program_relative = 0; i.program = i.raw; i.program_bytes = 1;
        }
    }) {
        auto m = metadata(); change(m); CHECK(obs::admit(m) == obs::Reason::pointer_range);
    }
    auto m = metadata(); m.identity.program_index = m.identity.program_count;
    CHECK(obs::admit(m) == obs::Reason::program_identity);
    Fixture f; CHECK(enter_process(f)); f.e.workspace = top - 64;
    CHECK(!obs::cmd24_entered(f.s, f.w, f.e)); CHECK(f.s.reason == obs::Reason::export_mismatch);
    return true;
}
bool quota_and_nonce_are_monotonic_even_for_rejected_attempts() {
    obs::Session session{}; auto m = metadata(); m.custom_callback = true;
    for (std::uint64_t n = 1; n <= obs::kMaxSamples; ++n) {
        obs::Scope s{}; CHECK(!obs::begin(s, session, m, n, 10000000));
        CHECK(s.reason == obs::Reason::excluded_branch);
    }
    obs::Scope extra{}; CHECK(!obs::begin(extra, session, metadata(), 7, 10000000));
    CHECK(extra.reason == obs::Reason::sample_limit && session.attempted == 6);
    session = {}; obs::Scope a{}, b{};
    CHECK(obs::begin(a, session, metadata(), 4, 10000000));
    CHECK(!obs::begin(b, session, metadata(), 4, 10000000));
    CHECK(b.reason == obs::Reason::bad_nonce);
    CHECK(!obs::begin(a, session, metadata(), 5, 10000000));
    CHECK(a.reason == obs::Reason::reused_scope);
    return true;
}
bool session_pins_one_freshly_read_actor_and_collection_without_retention() {
    for (const auto& change : std::vector<std::function<void(obs::Metadata&)>>{
        [](auto& x) { ++x.identity.id; ++x.identity.owner_id; ++x.identity.fg_id; },
        [](auto& x) { ++x.identity.record; ++x.identity.owner_record; ++x.identity.fg_record; },
        [](auto& x) { x.identity.fg += 16; x.identity.owner_fg += 16; x.identity.control_fg += 16; x.identity.cache += 16; },
        [](auto& x) { ++x.identity.art; ++x.identity.active_art18; ++x.identity.active_art1a0; ++x.identity.cache_art; },
        [](auto& x) { ++x.identity.matrix_bank; },
        [](auto& x) { ++x.bodies[1].handle_id; }, [](auto& x) { ++x.bodies[1].native; },
        [](auto& x) { std::swap(x.selected_joints[0], x.selected_joints[1]); }
    }) {
        obs::Session session{}; obs::Scope first{}, changed{}, after_stop{};
        auto m = metadata(); CHECK(obs::begin(first, session, m, 1, 10000000));
        change(m); CHECK(obs::admit(m) == obs::Reason::none);
        CHECK(!obs::begin(changed, session, m, 2, 10000000));
        CHECK(changed.reason == obs::Reason::selection_changed && session.stopped);
        CHECK(!obs::begin(after_stop, session, metadata(), 3, 10000000));
        CHECK(after_stop.reason == obs::Reason::session_stopped);
    }
    obs::Session session{}; obs::Scope first{}, second{}, unreadmitted{};
    auto m = metadata(); CHECK(obs::begin(first, session, m, 1, 10000000));
    m.identity.job += 64; m.identity.native_return_slot += 16;
    CHECK(obs::begin(second, session, m, 2, 10000000)); // New lexical invocation, same selected object bytes.
    ++m.identity.record_owner;
    CHECK(!obs::begin(unreadmitted, session, m, 3, 10000000));
    CHECK(unreadmitted.reason == obs::Reason::owner_links); // Pin is never reused as fresh admission.
    return true;
}
bool observer_budget_is_cumulative_overflow_safe_and_excludes_unmeasured_native_time() {
    Fixture f; CHECK(obs::begin(f.s, f.session, f.m, 1, 10000000));
    CHECK(obs::charge(f.s, 100, 4100));
    CHECK(obs::charge(f.s, 100000000, 100006000)); // Long native interval between charges is not counted.
    CHECK(f.s.observer_ticks == 10000);
    CHECK(!obs::charge(f.s, 200, 201)); CHECK(f.s.reason == obs::Reason::budget);
    CHECK(!obs::charge(f.s, 0, 0));
    Fixture huge; CHECK(obs::begin(huge.s, huge.session, huge.m, 1, UINT64_MAX));
    CHECK(!obs::charge(huge.s, 0, UINT64_MAX)); CHECK(huge.s.reason == obs::Reason::budget);
    Fixture bad; CHECK(obs::begin(bad.s, bad.session, bad.m, 1, 10000000));
    CHECK(!obs::charge(bad.s, 9, 8)); CHECK(bad.s.reason == obs::Reason::bad_clock);
    Fixture no_clock; CHECK(!obs::begin(no_clock.s, no_clock.session, no_clock.m, 1, 999));
    CHECK(no_clock.s.reason == obs::Reason::bad_clock);
    Fixture false_clock; CHECK(obs::begin(false_clock.s, false_clock.session, false_clock.m, 1, 10000000));
    CHECK(!obs::charge(false_clock.s, 0, 0, false));
    return true;
}
bool scope_identity_and_reentry_fail_closed() {
    for (const auto& change : std::vector<std::function<void(obs::Witness&)>>{
        [](auto& x) { x.native_scope_verified = false; }, [](auto& x) { ++x.nonce; },
        [](auto& x) { ++x.identity.job; }, [](auto& x) { x.identity.native_return_slot += 16; },
        [](auto& x) { ++x.identity.owner; }, [](auto& x) { ++x.identity.id; },
        [](auto& x) { ++x.identity.fg; }, [](auto& x) { ++x.identity.art; },
        [](auto& x) { ++x.identity.raw; }, [](auto& x) { ++x.identity.matrix_bank; },
        [](auto& x) { ++x.identity.locator_bank; }, [](auto& x) { ++x.identity.program; },
        [](auto& x) { ++x.identity.composite; }, [](auto& x) { ++x.identity.body_array; }
    }) {
        Fixture f; CHECK(obs::begin(f.s, f.session, f.m, 1, 10000000)); change(f.w);
        CHECK(!obs::reset_returned(f.s, f.w, f.roots)); CHECK(!obs::completed_witness(f.s));
    }
    Fixture f; CHECK(enter_process(f)); CHECK(!obs::reentered(f.s));
    CHECK(f.s.reason == obs::Reason::reentry);
    CHECK(!obs::cmd24_entered(f.s, f.w, f.e)); CHECK(f.s.reason == obs::Reason::reentry);
    return true;
}
bool root_copy_requires_complete_matching_bytes() {
    for (bool incomplete : {false, true}) {
        Fixture f; CHECK(obs::begin(f.s, f.session, f.m, 1, 10000000));
        if (incomplete) f.roots.complete = false; else ++f.roots.cache_root[15];
        CHECK(!obs::reset_returned(f.s, f.w, f.roots)); CHECK(f.s.reason == obs::Reason::root_copy);
    }
    return true;
}
bool missing_or_duplicate_stage_never_completes() {
    Fixture skip; CHECK(obs::begin(skip.s, skip.session, skip.m, 1, 10000000));
    CHECK(!obs::primary_entered(skip.s, skip.w, skip.m.identity.composite));
    Fixture reset; CHECK(obs::begin(reset.s, reset.session, reset.m, 1, 10000000));
    CHECK(obs::reset_returned(reset.s, reset.w, reset.roots));
    CHECK(!obs::reset_returned(reset.s, reset.w, reset.roots));
    Fixture no_export; CHECK(enter_process(no_export));
    CHECK(!obs::process_returned(no_export.s, no_export.w));
    Fixture early_return; CHECK(enter_process(early_return));
    CHECK(obs::cmd24_entered(early_return.s, early_return.w, early_return.e));
    CHECK(!obs::process_returned(early_return.s, early_return.w));
    Fixture twice; CHECK(enter_process(twice)); CHECK(obs::cmd24_entered(twice.s, twice.w, twice.e));
    CHECK(obs::cmd24_returned(twice.s, twice.w, twice.e));
    CHECK(!obs::cmd24_returned(twice.s, twice.w, twice.e));
    Fixture no_bodies; CHECK(enter_primary(no_bodies));
    CHECK(obs::primary_returned(no_bodies.s, no_bodies.w));
    CHECK(!obs::close(no_bodies.s, no_bodies.w)); CHECK(no_bodies.s.reason == obs::Reason::incomplete);
    return true;
}
bool exact_group0_process_site_and_export_contract() {
    for (const auto& pair : {std::pair{1u, true}, std::pair{0u, false}}) {
        Fixture f; CHECK(obs::begin(f.s, f.session, f.m, 1, 10000000));
        CHECK(obs::reset_returned(f.s, f.w, f.roots));
        CHECK(!obs::process_entered(f.s, f.w, pair.first, pair.second));
    }
    for (const auto& change : std::vector<std::function<void(obs::Export&)>>{
        [](auto& x) { ++x.id; }, [](auto& x) { ++x.descriptor; }, [](auto& x) { x.workspace = 0; },
        [](auto& x) { ++x.destination; }, [](auto& x) { ++x.first; }, [](auto& x) { x.count = 55; }
    }) {
        Fixture f; CHECK(enter_process(f)); change(f.e);
        CHECK(!obs::cmd24_entered(f.s, f.w, f.e)); CHECK(f.s.reason == obs::Reason::export_mismatch);
    }
    Fixture changed; CHECK(enter_process(changed)); CHECK(obs::cmd24_entered(changed.s, changed.w, changed.e));
    changed.e.workspace += 64; CHECK(!obs::cmd24_returned(changed.s, changed.w, changed.e));
    return true;
}
bool concrete_body_store_and_all_rows_required() {
    Fixture f; CHECK(enter_primary(f)); CHECK(body(f, 0)); CHECK(!body(f, 0));
    CHECK(f.s.reason == obs::Reason::body_store_mismatch);
    Fixture wrong; CHECK(enter_primary(wrong));
    auto store = store_witness(); store.target_rva = 0x1CF0EE0;
    CHECK(!obs::body_stored(wrong.s, wrong.w, 0, wrong.m.bodies[0], store, {}));
    Fixture stale; CHECK(enter_primary(stale)); auto changed = stale.m.bodies[1]; ++changed.handle_id;
    CHECK(!obs::body_stored(stale.s, stale.w, 1, changed, store_witness(), {obs::CapturePoint::post_store, false, {}}));
    Fixture over; CHECK(enter_primary(over));
    CHECK(!obs::body_stored(over.s, over.w, 16, over.m.bodies[0], store_witness(), {}));
    Fixture partial; CHECK(enter_primary(partial)); CHECK(body(partial, 0)); CHECK(body(partial, 1));
    CHECK(obs::primary_returned(partial.s, partial.w)); CHECK(!obs::close(partial.s, partial.w));
    CHECK(partial.s.reason == obs::Reason::incomplete);
    return true;
}
bool store_ancestry_requires_actual_normal_return_and_preserves_eax() {
    for (const auto& change : std::vector<std::function<void(obs::StoreWitness&)>>{
        [](auto& x) { ++x.target_rva; }, [](auto& x) { ++x.call_rva; },
        [](auto& x) { ++x.return_rva; }, [](auto& x) { ++x.refresh_ancestor_return_rva; },
        [](auto& x) { x.native_returned = false; }, [](auto& x) { x.ancestry_verified = false; }
    }) {
        Fixture f; CHECK(enter_primary(f)); auto store = store_witness(); change(store);
        CHECK(!obs::body_stored(f.s, f.w, 0, f.m.bodies[0], store, {}));
        CHECK(f.s.reason == obs::Reason::body_store_mismatch);
    }
    for (std::uint32_t eax : {0u, 1u, 0xFFFFFFFFu}) {
        Fixture f; CHECK(enter_primary(f)); auto store = store_witness(); store.native_eax = eax;
        CHECK(obs::body_stored(f.s, f.w, 0, f.m.bodies[0], store, {}));
        CHECK(f.s.store_return_eax[0] == eax);
    }
    return true;
}
bool valid_locators_require_separate_comparison_and_never_imply_policy_math() {
    CHECK(obs::classify_locator({obs::CapturePoint::pre_consumer, false, obs::Comparison::reported_different}) == obs::LocatorClass::pre_consumer_invalid_not_consumed);
    CHECK(obs::classify_locator({obs::CapturePoint::post_store, false, obs::Comparison::not_performed}) == obs::LocatorClass::post_store_invalid_unverified);
    Fixture stale; CHECK(enter_primary(stale));
    CHECK(!body(stale, 1, {obs::CapturePoint::post_store, true, obs::Comparison::reported_different}));
    CHECK(stale.s.reason == obs::Reason::locator_mismatch);
    Fixture unknown; CHECK(enter_primary(unknown)); CHECK(body(unknown, 0));
    CHECK(body(unknown, 1, {obs::CapturePoint::post_store, true, obs::Comparison::not_performed})); CHECK(body(unknown, 2));
    CHECK(obs::primary_returned(unknown.s, unknown.w)); CHECK(obs::close(unknown.s, unknown.w));
    CHECK(obs::completed_witness(unknown.s)); CHECK(!obs::selected_locator_comparisons_reported_equal(unknown.s));
    Fixture match; CHECK(enter_primary(match)); CHECK(body(match, 0));
    CHECK(body(match, 1, {obs::CapturePoint::post_store, true, obs::Comparison::reported_equal}));
    CHECK(body(match, 2, {obs::CapturePoint::post_store, true, obs::Comparison::reported_equal}));
    CHECK(obs::primary_returned(match.s, match.w)); CHECK(obs::close(match.s, match.w));
    CHECK(match.s.locators[1] == obs::LocatorClass::valid_reported_equal);
    CHECK(obs::selected_locator_comparisons_reported_equal(match.s));
    CHECK(!obs::kNumericalConsistencyProven);
    Fixture absent; CHECK(enter_primary(absent)); CHECK(!body(absent, 1, {}));
    CHECK(absent.s.reason == obs::Reason::locator_mismatch);
    Fixture pre; CHECK(enter_primary(pre));
    CHECK(!body(pre, 1, {obs::CapturePoint::pre_consumer, false, obs::Comparison::not_performed}));
    return true;
}
} // namespace

int main() {
    using Test = std::pair<const char*, bool (*)()>;
    const Test tests[]{
        {"complete witness scope", complete_witness_is_not_numerical_or_write_admission},
        {"owner and cache links", exact_owner_and_cache_links},
        {"group range and skeleton", group_range_and_candidate_identity},
        {"unknown program classification", unknown_program_can_be_observed_but_not_joined_to_asset_proof},
        {"excluded native branches", excluded_native_branches},
        {"actual body coverage", bounded_body_and_actual_joint_coverage},
        {"pointer span overflow", pointer_span_ends_never_wrap},
        {"quota and nonce", quota_and_nonce_are_monotonic_even_for_rejected_attempts},
        {"one actor per session", session_pins_one_freshly_read_actor_and_collection_without_retention},
        {"cumulative observer budget", observer_budget_is_cumulative_overflow_safe_and_excludes_unmeasured_native_time},
        {"invocation and reentry", scope_identity_and_reentry_fail_closed},
        {"root copy", root_copy_requires_complete_matching_bytes},
        {"stage sequence", missing_or_duplicate_stage_never_completes},
        {"group0 export", exact_group0_process_site_and_export_contract},
        {"concrete body store", concrete_body_store_and_all_rows_required},
        {"store ancestry and EAX", store_ancestry_requires_actual_normal_return_and_preserves_eax},
        {"stale locator classification", valid_locators_require_separate_comparison_and_never_imply_policy_math},
    };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        if (!test()) { std::cerr << "FAIL: " << name << '\n'; return 1; }
        ++passed;
    }
    std::cout << passed << " enemy JointBlend observation groups passed\n";
    return 0;
}
