#include "enemy_jointblend_capture.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace {
namespace cap = coop::enemy_jointblend_capture;
namespace obs = coop::enemy_jointblend_observation;
using A = std::uintptr_t;
#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << ": " #x "\n"; return false; } } while (false)

struct FakeReader {
    std::map<A, std::vector<std::byte>> memory;
    std::vector<std::pair<A, std::size_t>> reads;
    std::function<void(FakeReader&, A, std::size_t)> before;
    std::size_t fail_call{}, partial_bytes{};
    void block(A at, std::size_t bytes) { memory[at].resize(bytes); }
    template <class T> void put(A at, T value) {
        auto p = memory.upper_bound(at);
        if (p == memory.begin()) throw "missing fixture block";
        --p;
        const auto offset = at - p->first;
        if (offset > p->second.size() || sizeof(T) > p->second.size() - offset) throw "fixture overflow";
        std::memcpy(p->second.data() + offset, &value, sizeof(T));
    }
    bool copy(A at, void* dst, std::size_t bytes) {
        reads.emplace_back(at, bytes);
        if (before) before(*this, at, bytes);
        if (reads.size() == fail_call) {
            std::memset(dst, 0xA5, std::min(bytes, partial_bytes));
            return false;
        }
        auto p = memory.upper_bound(at);
        if (p == memory.begin()) return false;
        --p;
        const auto offset = at - p->first;
        if (offset > p->second.size() || bytes > p->second.size() - offset) return false;
        std::memcpy(dst, p->second.data() + offset, bytes);
        return true;
    }
};
struct Fixture {
    static constexpr A module = 0x140000000ULL, job = 0x10000, owner = 0x20000, record = 0x21000;
    static constexpr A fg = 0x30000, control = 0x32000, art = 0x40000, raw = 0x50000;
    static constexpr A composite = 0x60000, bodies = 0x61000, matrices = 0x70000, locators = 0x80000;
    static constexpr A native = 0xA0000, slot = 0x110008;
    FakeReader r;
    Fixture() {
        r.block(module + cap::kForceMode2Rva, 3); r.block(job, 0x18); r.block(owner, 0x240);
        r.block(record, 16); r.block(fg, 0x2A8); r.block(control, 0x28); r.block(art, 0x48);
        r.block(raw, 0x9400); r.block(composite, 0x128); r.block(bodies, 0x110 * obs::kMaxBodies);
        r.block(matrices, 64 * 90); r.block(locators, 32 * 90);
        r.put(job, control); r.put(job + 8, fg); r.put(owner, module + obs::kPirateRva);
        r.put(record + 8, owner); r.put(owner + 0xA8, record); r.put(owner + 0xBC, std::uint32_t{37});
        r.put(owner + 0x1A0, control); r.put(owner + 0x1A8, fg); r.put(owner + 0x238, composite);
        r.put(control + 0x20, fg); r.put(fg, std::uint32_t{0x20}); r.put(fg + 6, std::uint8_t{2});
        r.put(fg + 8, std::uint8_t{1}); r.put(fg + 0x10, control); r.put(fg + 0x18, art);
        r.put(fg + 0x1A0, art); r.put(fg + 0x1A8, record); r.put(fg + 0x1B0, std::uint32_t{37});
        r.put(fg + 0xF0, art); r.put(fg + 0xF8, raw); r.put(fg + 0x100, A{0x33000});
        r.put(fg + 0x108, locators); r.put(fg + 0x110, matrices);
        r.put(art + 0x24, obs::kNormalDatabase); r.put(art + 0x30, std::uint32_t{6}); r.put(art + 0x40, raw);
        r.put(raw + 4, obs::kNormalSkeleton); r.put(raw + 0x16, std::uint8_t{6});
        r.put(raw + 0x2C, std::uint32_t{0x40}); r.put(raw + 0x34, std::uint32_t{0x140});
        r.put(raw + 0x4F, std::uint8_t{1}); r.put(raw + 0x50, std::uint16_t{90}); r.put(raw + 0x52, std::uint16_t{55});
        r.put(raw + 0x142, std::uint16_t{0x40B0}); r.put(raw + 0x144, std::uint32_t{0x5340});
        r.put(composite + 8, owner); r.put(composite + 0x10, std::uint32_t{3}); r.put(composite + 0x48, bodies);
        for (std::size_t b = 0; b < obs::kMaxBodies; ++b) {
            const A body = bodies + 0x110 * b, n = native + 0x200 * b;
            r.block(n, 0x161); r.put(n + 0x160, std::uint8_t{4});
            r.put(body + 0x40, n); r.put(body + 0x68, owner); r.put(body + 0x50, static_cast<std::uint32_t>(100 + b));
            r.put(body + 0x54, static_cast<std::uint16_t>(10 + b)); r.put(body + 0xA0, std::uint32_t{2u << 13});
            r.put(body + 0xC4, static_cast<std::int16_t>(b + 2)); r.put(body + 0xC6, static_cast<std::uint8_t>(b ? 3u : 1u));
        }
        cap::Matrix root{};
        for (std::size_t n = 0; n < root.size(); ++n) root[n] = static_cast<std::uint32_t>(0x3F800000 + n);
        r.put(fg + 0x1C0, root); r.put(fg + 0x30, root);
    }
    cap::Status metadata(obs::Metadata& m, cap::MetadataRead mode = cap::MetadataRead::single_pass) {
        return cap::read_metadata(r, module, job, slot, m, mode);
    }
};
bool metadata_success_and_no_hash() {
    Fixture f; obs::Metadata m{};
    CHECK(f.metadata(m).ok()); CHECK(obs::admit(m) == obs::Reason::none);
    CHECK(m.identity.art_database_id == obs::kNormalDatabase && m.identity.raw_skeleton_id == obs::kNormalSkeleton);
    CHECK(m.identity.program_bytes == 0x40B0 && m.identity.program == f.raw + 0x5340);
    CHECK(m.selected_count == 2 && m.selected_joints[0] == 3 && m.selected_joints[1] == 4);
    CHECK(!m.fingerprint_present && obs::classify_program(m) == obs::ProgramMatch::identity_unverified);
    CHECK(std::none_of(f.r.reads.begin(), f.r.reads.end(), [&](auto x) { return x.first == m.identity.program; }));
    f.r.put(f.art + 0x24, obs::kMediumDatabase); f.r.put(f.raw + 4, obs::kMediumSkeleton);
    CHECK(f.metadata(m, cap::MetadataRead::paired_pass).ok());
    return true;
}
bool fast_filter_is_small_and_nonpirate_is_early() {
    Fixture f; cap::Filter filter{};
    f.r.put(f.owner, f.module + 1);
    CHECK(cap::fast_filter(f.r, f.module, f.job, filter).reason == cap::Reason::non_pirate);
    CHECK(f.r.reads.size() == 4 && filter.owner == 0);
    f.r.reads.clear(); obs::Metadata m{};
    CHECK(f.metadata(m).reason == cap::Reason::non_pirate);
    CHECK(f.r.reads.size() == 4 && m.identity.owner == 0);
    return true;
}
bool all_read_failures_clear_output() {
    Fixture baseline; obs::Metadata m{}; CHECK(baseline.metadata(m).ok());
    for (std::size_t call = 1; call <= baseline.r.reads.size(); ++call) {
        Fixture f; f.r.fail_call = call; f.r.partial_bytes = 5; m.identity.owner = 123;
        CHECK(f.metadata(m).reason == cap::Reason::unreadable);
        CHECK(!m.identity.owner && !m.body_count && !m.fingerprint_present);
    }
    return true;
}
bool mutations_fail_closed() {
    const std::vector<std::function<void(Fixture&)>> changes{
        [](auto& f) { f.r.put(f.owner + 0xBC, std::uint32_t{38}); },
        [](auto& f) { f.r.put(f.owner + 0x1A8, A{1}); },
        [](auto& f) { f.r.put(f.control + 0x20, A{1}); },
        [](auto& f) { f.r.put(f.fg + 0xF0, A{1}); },
        [](auto& f) { f.r.put(f.raw + 4, std::uint32_t{1}); },
        [](auto& f) { f.r.put(f.art + 0x24, obs::kNormalSkeleton); }, // Old equality bug.
        [](auto& f) { f.r.put(f.art + 0x24, obs::kMediumDatabase); }, // Crossed pair.
        [](auto& f) { f.r.put(f.raw + 4, obs::kNormalDatabase); },
        [](auto& f) { f.r.put(f.raw + 0x16, std::uint8_t{0}); },
        [](auto& f) { f.r.put(f.art + 0x30, std::uint32_t{7}); },
        [](auto& f) { f.r.put(f.raw + 0x50, std::uint16_t{91}); },
        [](auto& f) { f.r.put(f.raw + 0x52, std::uint16_t{56}); },
        [](auto& f) { f.r.put(f.raw + 0x46, std::uint16_t{1}); },
        [](auto& f) { f.r.put(f.raw + 0x4F, std::uint8_t{2}); },
        [](auto& f) { f.r.put(f.raw + 0x142, std::uint16_t{0}); },
        [](auto& f) { f.r.put(f.job + 0x14, std::uint8_t{1}); },
        [](auto& f) { f.r.put(f.fg + 0x2A0, A{1}); },
        [](auto& f) { f.r.put(f.module + cap::kForceCachedPrepassRva, std::uint8_t{1}); },
        [](auto& f) { f.r.put(f.fg + 8, std::uint8_t{0}); },
        [](auto& f) { f.r.put(f.composite + 8, A{1}); },
        [](auto& f) { f.r.put(f.composite + 0x10, std::uint32_t{17}); },
        [](auto& f) { f.r.put(f.composite + 0x10, std::uint32_t{0x10003}); },
        [](auto& f) { f.r.put(f.bodies + 0x110 + 0xC4, std::int16_t{90}); },
        [](auto& f) { f.r.put(f.bodies + 0x110 + 0xC6, std::uint8_t{4}); },
        [](auto& f) { f.r.put(f.bodies + 0x110 + 0x40, f.native); },
        [](auto& f) { f.r.put(f.native + 0x160, std::uint8_t{1}); },
        [](auto& f) { f.r.put(f.bodies + 0xA0, std::uint32_t{0}); }
    };
    for (const auto& change : changes) { Fixture f; obs::Metadata m{}; change(f); CHECK(!f.metadata(m).ok()); CHECK(!m.identity.owner); }
    return true;
}
bool effective_mode_and_caps() {
    Fixture f; obs::Metadata m{}; f.r.put(f.fg + 6, std::uint8_t{1});
    CHECK(!f.metadata(m).ok()); f.r.put(f.module + cap::kForceMode2Rva, std::uint8_t{1}); CHECK(f.metadata(m).ok());
    f.r.put(f.fg + 0x100, A{0}); CHECK(!f.metadata(m).ok());
    f.r.put(f.fg + 6, std::uint8_t{2}); f.r.put(f.composite + 0x10, std::uint32_t{16});
    CHECK(f.metadata(m).ok() && m.body_count == 16 && m.selected_count == 15);
    f.r.put(f.bodies + 0x110 * 2 + 0xC4, std::int16_t{3});
    CHECK(f.metadata(m).ok() && m.selected_count == 14); // repeated body joints are valid
    return true;
}
bool identity_changes_during_pass_reject() {
    Fixture f; obs::Metadata m{};
    f.r.before = [&](FakeReader& r, A at, std::size_t) { if (at == f.native + 0x160) r.put(f.owner + 0xBC, std::uint32_t{38}); };
    CHECK(!f.metadata(m).ok() && !m.identity.owner);
    Fixture g; unsigned full_jobs = 0;
    g.r.before = [&](FakeReader& r, A at, std::size_t bytes) {
        if (at == g.job && bytes == 0x18 && ++full_jobs == 2) r.put(g.bodies + 0x50, std::uint32_t{999});
    };
    CHECK(g.metadata(m, cap::MetadataRead::paired_pass).reason == cap::Reason::changed);
    CHECK(!m.identity.owner);
    return true;
}
bool address_overflow_never_reaches_reader() {
    Fixture f; obs::Metadata m{}; const A max = std::numeric_limits<A>::max();
    CHECK(cap::read_metadata(f.r, max, f.job, f.slot, m).reason == cap::Reason::pointer_range);
    CHECK(f.r.reads.empty());
    CHECK(cap::read_metadata(f.r, f.module, max - 4, f.slot, m).reason == cap::Reason::pointer_range);
    CHECK(f.r.reads.empty());
    CHECK(cap::read_metadata(f.r, f.module, f.job, max - 7, m).policy_reason == obs::Reason::owner_links);
    CHECK(f.r.reads.empty());
    f.r.put(f.composite + 0x48, max - 64); CHECK(f.metadata(m).reason == cap::Reason::pointer_range);
    CHECK(std::none_of(f.r.reads.begin(), f.r.reads.end(), [&](auto x) { return x.first >= max - 64; }));
    return true;
}
bool roots_and_matrix_capture_are_owned_and_timed() {
    Fixture f; obs::Metadata m{}; CHECK(f.metadata(m).ok());
    obs::RootCopy roots{}; CHECK(cap::read_roots(f.r, m, roots).ok() && roots.complete && roots.fg_root == roots.cache_root);
    cap::MatrixCapture matrices{}; f.r.put(f.matrices + 64 * 89 + 60, std::uint32_t{123});
    CHECK(cap::read_group0_matrices(f.r, m, cap::CaptureTiming::post_store, matrices).reason == cap::Reason::invalid_timing);
    CHECK(cap::read_group0_matrices(f.r, m, cap::CaptureTiming::export_returned, matrices).ok());
    CHECK(matrices.complete && matrices.rows[89][15] == 123);
    f.r.put(f.matrices + 64 * 89 + 60, std::uint32_t{0}); CHECK(matrices.rows[89][15] == 123);
    f.r.fail_call = f.r.reads.size() + 1; f.r.partial_bytes = 20;
    CHECK(!cap::read_group0_matrices(f.r, m, cap::CaptureTiming::export_returned, matrices).ok());
    CHECK(!matrices.complete && matrices.rows[0][0] == 0);
    return true;
}
bool locators_copy_selected_rows_and_bits_only() {
    Fixture f; obs::Metadata m{}; CHECK(f.metadata(m).ok());
    f.r.put(f.fg + 0x70, std::uint64_t{1u << 3}); f.r.put(f.locators + 3 * 32, std::uint32_t{99});
    cap::LocatorCapture loc{}; const auto before = f.r.reads.size();
    CHECK(cap::read_selected_locators(f.r, m, cap::CaptureTiming::pre_consumer, loc).ok());
    CHECK(loc.complete && loc.count == 2 && loc.rows[0][0] == 99 && loc.valid_before[0] == (1u << 3));
    CHECK(f.r.reads.size() - before == 4);
    CHECK(cap::read_selected_locators(f.r, m, cap::CaptureTiming::post_store, loc).ok());
    CHECK(loc.timing == cap::CaptureTiming::post_store); // does not assert pre-consumer validity
    unsigned bit_reads = 0;
    f.r.before = [&](FakeReader& r, A at, std::size_t) {
        if (at == f.fg + 0x70 && ++bit_reads == 2) r.put(at, std::uint64_t{0});
    };
    CHECK(cap::read_selected_locators(f.r, m, cap::CaptureTiming::export_returned, loc).reason == cap::Reason::changed);
    CHECK(!loc.complete && loc.count == 0);
    return true;
}
bool program_exact_extent_once_no_hash() {
    Fixture f; obs::Metadata m{}; CHECK(f.metadata(m).ok()); cap::ProgramCapture p{};
    f.r.put(f.raw + 0x5340, std::uint32_t{123}); const auto before = f.r.reads.size();
    CHECK(cap::read_program_once(f.r, m, p).ok() && p.complete && p.attempted);
    CHECK(f.r.reads.size() == before + 1 && f.r.reads.back().second == 0x40B0 && !m.fingerprint_present);
    CHECK(p.bytes[0] == std::byte{123});
    CHECK(cap::read_program_once(f.r, m, p).reason == cap::Reason::already_attempted);
    CHECK(f.r.reads.size() == before + 1 && p.complete);
    cap::ProgramCapture wrong{}; ++m.identity.program_bytes;
    CHECK(cap::read_program_once(f.r, m, wrong).reason == cap::Reason::candidate_extent && wrong.attempted && !wrong.complete);
    return true;
}
bool program_partial_failure_consumes_attempt_and_clears() {
    Fixture f; obs::Metadata m{}; CHECK(f.metadata(m).ok()); cap::ProgramCapture p{};
    f.r.fail_call = f.r.reads.size() + 1; f.r.partial_bytes = 100;
    CHECK(cap::read_program_once(f.r, m, p).reason == cap::Reason::unreadable);
    CHECK(p.attempted && !p.complete && !p.identity.owner && p.bytes[0] == std::byte{});
    CHECK(cap::read_program_once(f.r, m, p).reason == cap::Reason::already_attempted);
    return true;
}
bool snapshot_policy_refusal_does_not_read() {
    Fixture f; obs::Metadata m{}; CHECK(f.metadata(m).ok()); m.identity.workspace_count = 91;
    const auto before = f.r.reads.size(); obs::RootCopy root{}; cap::MatrixCapture matrix{}; cap::LocatorCapture loc{};
    CHECK(!cap::read_roots(f.r, m, root).ok());
    CHECK(!cap::read_group0_matrices(f.r, m, cap::CaptureTiming::export_returned, matrix).ok());
    CHECK(!cap::read_selected_locators(f.r, m, cap::CaptureTiming::pre_consumer, loc).ok());
    CHECK(f.r.reads.size() == before && !root.complete && !matrix.complete && !loc.complete);
    return true;
}
} // namespace
int main() {
    const std::pair<const char*, bool (*)()> tests[]{
        {"metadata", metadata_success_and_no_hash}, {"filter", fast_filter_is_small_and_nonpirate_is_early},
        {"partial reads", all_read_failures_clear_output}, {"mutations", mutations_fail_closed},
        {"mode/caps", effective_mode_and_caps}, {"changed", identity_changes_during_pass_reject},
        {"overflow", address_overflow_never_reaches_reader}, {"root/matrices", roots_and_matrix_capture_are_owned_and_timed},
        {"locators", locators_copy_selected_rows_and_bits_only}, {"program", program_exact_extent_once_no_hash},
        {"program partial", program_partial_failure_consumes_attempt_and_clears}, {"snapshot refusal", snapshot_policy_refusal_does_not_read}
    };
    std::size_t passed = 0;
    for (const auto& [name, run] : tests) { if (!run()) { std::cerr << "FAIL " << name << '\n'; return 1; } ++passed; }
    std::cout << passed << '/' << std::size(tests) << " passed\n";
    return 0;
}
