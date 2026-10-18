#include "enemy_jointblend_collector.h"
#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
#include <cstdio>
#include <thread>

namespace {
namespace col = coop::enemy_jointblend_collector;
namespace cap = coop::enemy_jointblend_capture;
namespace obs = coop::enemy_jointblend_observation;
namespace hook = coop::enemy_jointblend_hooks;
using A = std::uintptr_t;
#define CHECK(x) do { if (!(x)) { std::cerr << __func__ << ':' << __LINE__ << ": " #x "\n"; return false; } } while (false)

struct Reader {
    std::map<A, std::vector<std::byte>> memory;
    std::size_t calls{}, forbidden_calls{}, copied_bytes{};
    bool forbidden{};
    void block(A at, std::size_t bytes) { memory[at].resize(bytes); }
    template<class T> void put(A at, T value) {
        auto p = memory.upper_bound(at);
        if (p == memory.begin()) throw "missing fixture block";
        --p;
        const auto offset = at - p->first;
        if (offset > p->second.size() || sizeof(T) > p->second.size() - offset) throw "fixture overflow";
        std::memcpy(p->second.data() + offset, &value, sizeof(value));
    }
    bool copy(A at, void* destination, std::size_t bytes) noexcept {
        ++calls;
        if (forbidden) { ++forbidden_calls; return false; }
        auto p = memory.upper_bound(at);
        if (p == memory.begin()) return false;
        --p;
        const auto offset = at - p->first;
        if (offset > p->second.size() || bytes > p->second.size() - offset) return false;
        std::memcpy(destination, p->second.data() + offset, bytes);
        copied_bytes += bytes;
        return true;
    }
};
struct Clock {
    std::uint64_t time = 1000000, step = 1;
    static std::uint64_t now(void* p) noexcept {
        auto& clock = *static_cast<Clock*>(p);
        const auto value = clock.time;
        clock.time += clock.step;
        return value;
    }
};
struct Fixture {
    static constexpr A module = 0x140000000ULL, job = 0x10000, owner = 0x20000, record = 0x21000;
    static constexpr A fg = 0x30000, control = 0x32000, art = 0x40000, raw = 0x50000;
    static constexpr A composite = 0x60000, bodies = 0x61000, matrices = 0x70000, locators = 0x80000;
    static constexpr A workspace = 0x90000, payload = 0x94000, native = 0xA0000, input = 0xB0000;
    static constexpr A work_buffer = 0x98000;
    static constexpr A commands = 0xC0000;
    static constexpr A jointblend_slot = 0x110008, process_context = jointblend_slot - (0x5F8 - 0x110);
    static constexpr A segment = 0x120000, process_slot = segment + 0x1628, store_slot = 0x130008;
    Reader reader;
    Clock clock;
    col::Collector<Reader> collector{reader, {module, 0x7000000}, {&clock, Clock::now, 1000000}};
    std::uint64_t nonce = 1;
    unsigned body_count = 3;
    explicit Fixture(unsigned count = 3, col::Clock real_clock = {})
        : collector(reader, {module, 0x7000000}, real_clock.now ? real_clock : col::Clock{&clock, Clock::now, 1000000}),
          body_count(count) {
        reader.block(module + cap::kForceMode2Rva, 3);
        reader.block(job, 0x18); reader.block(owner, 0x240); reader.block(record, 16);
        reader.block(fg, 0x2A8); reader.block(control, 0x28); reader.block(art, 0x48);
        reader.block(raw, 0x9400); reader.block(composite, 0x128); reader.block(bodies, 0x110 * body_count);
        reader.block(matrices, 64 * 90); reader.block(locators, 32 * 90); reader.block(payload, 8);
        reader.block(segment, 0x60); reader.block(process_context, 8);
        reader.block(input, 32); reader.block(store_slot + 0xB0, 8);
        reader.put(process_context, art); reader.put(segment + 0x28, raw);
        reader.put(segment, work_buffer); reader.put(segment + 8, process_context);
        reader.put(job, control); reader.put(job + 8, fg); reader.put(owner, module + obs::kPirateRva);
        reader.put(record + 8, owner); reader.put(owner + 0xA8, record); reader.put(owner + 0xBC, std::uint32_t{37});
        reader.put(owner + 0x1A0, control); reader.put(owner + 0x1A8, fg); reader.put(owner + 0x238, composite);
        reader.put(control + 0x20, fg); reader.put(fg, std::uint32_t{0x20}); reader.put(fg + 6, std::uint8_t{2});
        reader.put(fg + 8, std::uint8_t{1}); reader.put(fg + 0x10, control); reader.put(fg + 0x18, art);
        reader.put(fg + 0x1A0, art); reader.put(fg + 0x1A8, record); reader.put(fg + 0x1B0, std::uint32_t{37});
        reader.put(fg + 0xF0, art); reader.put(fg + 0xF8, raw); reader.put(fg + 0x100, A{0x33000});
        reader.put(fg + 0x108, locators); reader.put(fg + 0x110, matrices);
        reader.put(art + 0x24, obs::kNormalDatabase); reader.put(art + 0x30, std::uint32_t{6}); reader.put(art + 0x40, raw);
        reader.put(raw + 4, obs::kNormalSkeleton); reader.put(raw + 0x16, std::uint8_t{6});
        reader.put(raw + 0x2C, std::uint32_t{0x40}); reader.put(raw + 0x34, std::uint32_t{0x140});
        reader.put(raw + 0x4F, std::uint8_t{1}); reader.put(raw + 0x50, std::uint16_t{90}); reader.put(raw + 0x52, std::uint16_t{55});
        reader.put(raw + 0x142, std::uint16_t{0x40B0}); reader.put(raw + 0x144, std::uint32_t{0x5340});
        reader.put(composite + 8, owner); reader.put(composite + 0x10, static_cast<std::uint32_t>(body_count)); reader.put(composite + 0x48, bodies);
        for (std::size_t b = 0; b < body_count; ++b) {
            const A body = bodies + 0x110 * b, n = native + 0x200 * b;
            reader.block(n, 0x161); reader.put(n + 0x160, std::uint8_t{4});
            reader.put(body + 0x40, n); reader.put(body + 0x68, owner);
            reader.put(body + 0x50, static_cast<std::uint32_t>(100 + b));
            reader.put(body + 0x54, static_cast<std::uint16_t>(10 + b));
            reader.put(body + 0xA0, std::uint32_t{2u << 13});
            reader.put(body + 0xC4, static_cast<std::int16_t>(b + 2));
            reader.put(body + 0xC6, static_cast<std::uint8_t>(b ? 3u : 1u));
        }
        cap::Matrix root{};
        for (std::size_t i = 0; i < root.size(); ++i) root[i] = static_cast<std::uint32_t>(0x3F800000 + i);
        reader.put(fg + 0x1C0, root); reader.put(fg + 0x30, root);
        reader.put(fg + 0x70, std::uint64_t{(1u << 3) | (1u << 4)});
        reader.put(segment + 0x38, raw + 0x40); reader.put(segment + 0x58, workspace);
        reader.put(payload, matrices); reader.put(store_slot + 0xB0, module + 0x1CF5872);
        reader.put(input, col::Words8{1, 2, 3, 4, 5, 6, 7, 8});
    }
    hook::Event event(hook::Kind kind, unsigned body = 0) const {
        hook::Event e{};
        e.kind = kind; e.invocation_nonce = nonce; e.job = job;
        e.original_jointblend_return_slot = jointblend_slot;
        e.wrapper_entry_return_slot = 0x140008;
        switch (kind) {
        case hook::Kind::Entry:
        case hook::Kind::Exit:
            e.original_jointblend_return_slot = 0;
            e.wrapper_entry_return_pc = module + 0x17815BE;
            e.arguments[0] = job;
            break;
        case hook::Kind::ResetEnter:
        case hook::Kind::ResetReturn:
            e.wrapper_entry_return_pc = module + 0x1A92BAA;
            e.arguments[0] = fg + 0x30; e.arguments[1] = fg + 0x1C0; e.result = matrices;
            break;
        case hook::Kind::ProcessEnter:
        case hook::Kind::ProcessReturn:
            e.site = hook::ProcessSite::first;
            e.wrapper_entry_return_pc = module + 0x1A92E7D;
            e.arguments = {commands, 4, process_context, 0, 0, 0, 0};
            break;
        case hook::Kind::PostposeEnter:
        case hook::Kind::PostposeReturn:
            e.site = hook::ProcessSite::first;
            e.original_process_return_slot = process_slot;
            e.wrapper_entry_return_pc = module + 0x1ABAC69;
            e.arguments = {work_buffer, segment, process_context, obs::kExportId, payload, 0, 0};
            break;
        case hook::Kind::PrimaryEnter:
        case hook::Kind::PrimaryReturn:
            e.wrapper_entry_return_pc = module + 0x1BBE11E; e.arguments[0] = composite;
            break;
        case hook::Kind::StoreEnter:
        case hook::Kind::StoreReturn:
            e.wrapper_entry_return_pc = module + 0x1CF0F3B; e.wrapper_entry_return_slot = store_slot;
            e.arguments[0] = bodies + 0x110 * body; e.arguments[1] = input;
            e.result = 0xFFFFFFFB;
            break;
        default: break;
        }
        e.normal_return = kind == hook::Kind::ResetReturn || kind == hook::Kind::ProcessReturn ||
            kind == hook::Kind::PostposeReturn || kind == hook::Kind::PrimaryReturn ||
            kind == hook::Kind::StoreReturn || kind == hook::Kind::Exit;
        return e;
    }
    void finish(bool normal = true, bool cleanup = true, bool observer = true) {
        collector.finish({nonce, normal, cleanup, observer});
    }
    void send(hook::Kind kind, unsigned body = 0) {
        collector.emit(event(kind, body));
        if (kind == hook::Kind::Exit) finish();
    }
    void through_process() {
        send(hook::Kind::Entry); send(hook::Kind::ResetEnter); send(hook::Kind::ResetReturn);
        send(hook::Kind::ProcessEnter); send(hook::Kind::PostposeEnter); send(hook::Kind::PostposeReturn);
        send(hook::Kind::ProcessReturn);
    }
    void body_events(unsigned body) {
        send(hook::Kind::StoreEnter, body); send(hook::Kind::StoreReturn, body);
    }
    void primary_events() {
        send(hook::Kind::PrimaryEnter);
        for (unsigned body = 0; body < body_count; ++body) body_events(body);
        send(hook::Kind::PrimaryReturn);
    }
    void full() { through_process(); primary_events(); send(hook::Kind::Exit); }
    void next() { ++nonce; clock.time += 300000; }
    const col::Sample& first() const { return collector.samples()[0]; }
};

bool complete_chain_is_event_only_and_exit_reads_nothing() {
    Fixture f; CHECK(f.collector.arm()); f.through_process(); f.primary_events();
    CHECK(!f.collector.quiescent());
    CHECK(!col::complete(f.first()));
    const auto reads = f.reader.calls;
    f.reader.forbidden = true; f.send(hook::Kind::Exit); f.collector.stop();
    CHECK(f.collector.quiescent() && f.collector.count() == 1);
    CHECK(f.reader.calls == reads && f.reader.forbidden_calls == 0);
    const auto& s = f.first();
    CHECK(s.published && s.normal_exit && s.failure == col::Failure::none);
    CHECK(col::complete(s));
    CHECK(obs::completed_witness(s.policy) && s.policy.stored_mask == 7);
    CHECK(s.roots.complete && s.matrices.complete && s.before_primary.complete && s.program.complete);
    CHECK(s.before_primary.timing == cap::CaptureTiming::pre_consumer);
    CHECK(s.policy.program_match == obs::ProgramMatch::identity_unverified);
    CHECK(!col::kProgramExecutionWitnessAvailable);
    CHECK(s.command_list == f.commands && s.command_list != s.program.identity.program && s.command_words == 4);
    CHECK(!obs::selected_locator_comparisons_reported_equal(s.policy));
    CHECK(s.policy.locators[1] == obs::LocatorClass::valid_unverified);
    CHECK(s.policy.store_return_eax[0] == 0xFFFFFFFB && s.bodies[0].input[0] == 1);
    CHECK(s.bodies[0].entered && s.bodies[0].returned && s.bodies[2].returned);
    return true;
}
bool absent_or_misordered_stages_fail_closed() {
    for (unsigned omission = 0; omission < 4; ++omission) {
        Fixture f; CHECK(f.collector.arm()); f.send(hook::Kind::Entry);
        if (omission != 0) f.send(hook::Kind::ResetEnter);
        if (omission != 1) f.send(hook::Kind::ResetReturn);
        f.send(hook::Kind::ProcessEnter);
        if (omission != 2) {
            f.send(hook::Kind::PostposeEnter); f.send(hook::Kind::PostposeReturn);
        }
        f.send(hook::Kind::ProcessReturn); f.send(hook::Kind::PrimaryEnter);
        for (unsigned body = 0; body < (omission == 3 ? 2u : 3u); ++body) f.body_events(body);
        f.send(hook::Kind::PrimaryReturn); f.send(hook::Kind::Exit); f.collector.stop();
        CHECK(f.collector.quiescent() && f.collector.count() == 1);
        CHECK(f.first().failure != col::Failure::none && !obs::completed_witness(f.first().policy));
    }
    Fixture f; CHECK(f.collector.arm()); f.send(hook::Kind::Entry); f.send(hook::Kind::ResetEnter);
    f.send(hook::Kind::ResetReturn); f.send(hook::Kind::ResetReturn); f.send(hook::Kind::Exit); f.collector.stop();
    CHECK(f.first().failure != col::Failure::none);
    return true;
}
bool foreign_frames_and_return_sites_fail_closed() {
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
        Fixture f; CHECK(f.collector.arm()); f.send(hook::Kind::Entry); f.send(hook::Kind::ResetEnter);
        if (mutation < 2) {
            auto e = f.event(hook::Kind::ResetReturn);
            if (mutation == 0) e.original_jointblend_return_slot += 0x10;
            else ++e.wrapper_entry_return_pc;
            f.collector.emit(e);
        } else {
            f.send(hook::Kind::ResetReturn);
            auto process = f.event(hook::Kind::ProcessEnter);
            if (mutation == 2) process.arguments[2] += 0x10;
            if (mutation == 3) process.site = hook::ProcessSite::later;
            f.collector.emit(process);
            auto post = f.event(hook::Kind::PostposeEnter);
            if (mutation == 4) post.original_process_return_slot += 0x10;
            if (mutation == 5) ++post.wrapper_entry_return_pc;
            f.collector.emit(post);
        }
        f.send(hook::Kind::Exit); f.collector.stop();
        CHECK(f.collector.quiescent());
        CHECK(f.first().failure == col::Failure::scope || f.first().failure == col::Failure::callsite);
        CHECK(!obs::completed_witness(f.first().policy));
    }
    return true;
}
bool collection_mutations_fail_closed() {
    for (unsigned mutation = 0; mutation < 3; ++mutation) {
        Fixture f; CHECK(f.collector.arm()); f.through_process(); f.send(hook::Kind::PrimaryEnter);
        if (mutation == 0) f.reader.put(f.composite + 0x10, std::uint32_t{2});
        if (mutation == 1) f.reader.put(f.bodies + 0x110 + 0xC4, std::int16_t{5});
        if (mutation == 2) f.reader.put(f.bodies + 0x110 + 0x50, std::uint32_t{999});
        f.body_events(0); f.send(hook::Kind::PrimaryReturn); f.send(hook::Kind::Exit); f.collector.stop();
        CHECK(f.collector.quiescent() && f.first().failure == col::Failure::scope);
        CHECK(!obs::completed_witness(f.first().policy));
    }
    return true;
}
bool store_ancestry_arguments_and_pairs_fail_closed() {
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        Fixture f; CHECK(f.collector.arm()); f.through_process(); f.send(hook::Kind::PrimaryEnter);
        auto enter = f.event(hook::Kind::StoreEnter);
        if (mutation == 0) f.reader.put(f.store_slot + 0xB0, f.module + 0x1CF5873);
        if (mutation == 1) enter.wrapper_entry_return_slot += 0x10;
        if (mutation == 2) enter.arguments[0] += 1;
        if (mutation != 3) f.collector.emit(enter);
        auto returned = f.event(hook::Kind::StoreReturn);
        if (mutation == 4) returned.arguments[1] += 16;
        f.collector.emit(returned); f.send(hook::Kind::Exit); f.collector.stop();
        CHECK(f.collector.quiescent() && f.first().failure != col::Failure::none);
        CHECK(!obs::completed_witness(f.first().policy));
    }
    return true;
}
bool exception_reentry_and_stop_wait_for_outer_exit() {
    for (auto kind : {hook::Kind::Exception, hook::Kind::Reentry}) {
        Fixture f; CHECK(f.collector.arm()); f.send(hook::Kind::Entry); f.send(hook::Kind::ResetEnter);
        f.send(kind); f.collector.stop(); CHECK(!f.collector.requested() && !f.collector.quiescent());
        const auto reads = f.reader.calls;
        f.reader.forbidden = true;
        auto exit = f.event(hook::Kind::Exit); exit.normal_return = kind == hook::Kind::Reentry;
        f.collector.emit(exit);
        CHECK(!f.collector.quiescent() && !f.first().published);
        f.finish(exit.normal_return);
        CHECK(f.collector.quiescent() && f.reader.calls == reads);
        CHECK(f.first().published && f.first().failure == (kind == hook::Kind::Exception ?
            col::Failure::exception : col::Failure::reentry));
    }
    Fixture f; CHECK(f.collector.arm()); f.through_process();
    f.collector.stop(); CHECK(!f.collector.quiescent());
    f.primary_events(); CHECK(!f.collector.quiescent()); f.send(hook::Kind::Exit);
    CHECK(f.collector.quiescent() && f.first().failure == col::Failure::none);
    CHECK(obs::completed_witness(f.first().policy));
    return true;
}
bool foreign_nonce_does_not_finish_or_join_scope() {
    Fixture f; CHECK(f.collector.arm()); f.through_process();
    auto foreign = f.event(hook::Kind::Exit); foreign.invocation_nonce = 999;
    const auto reads = f.reader.calls;
    f.collector.emit(foreign); CHECK(f.reader.calls == reads);
    f.collector.stop(); CHECK(!f.collector.quiescent());
    f.primary_events(); f.send(hook::Kind::Exit);
    CHECK(f.collector.quiescent() && f.first().failure == col::Failure::none);
    return true;
}
bool budget_charges_observer_intervals_not_native_gaps() {
    Fixture f; CHECK(f.collector.arm());
    const hook::Kind events[]{hook::Kind::Entry, hook::Kind::ResetEnter, hook::Kind::ResetReturn,
        hook::Kind::ProcessEnter, hook::Kind::PostposeEnter, hook::Kind::PostposeReturn,
        hook::Kind::ProcessReturn, hook::Kind::PrimaryEnter};
    for (auto kind : events) { f.send(kind); f.clock.time += 100000000; }
    for (unsigned body = 0; body < 3; ++body) {
        f.send(hook::Kind::StoreEnter, body); f.clock.time += 100000000;
        f.send(hook::Kind::StoreReturn, body); f.clock.time += 100000000;
    }
    f.send(hook::Kind::PrimaryReturn); f.clock.time += 100000000; f.send(hook::Kind::Exit);
    f.collector.stop(); CHECK(f.collector.quiescent());
    CHECK(f.first().failure == col::Failure::none && f.first().total_ticks == 17);
    CHECK(f.first().policy.observer_ticks == 17);
    Fixture over; CHECK(over.collector.arm()); over.clock.step = 300; over.full();
    CHECK(over.collector.quiescent() && !over.collector.requested());
    CHECK(over.first().failure == col::Failure::budget && !obs::completed_witness(over.first().policy));
    return true;
}
bool six_samples_pin_one_actor_and_stop() {
    Fixture f; CHECK(f.collector.arm());
    for (unsigned i = 0; i < 6; ++i) { f.full(); f.next(); }
    CHECK(f.collector.quiescent() && !f.collector.requested() && f.collector.count() == 6);
    CHECK(f.collector.probes() == 6);
    for (const auto& s : f.collector.samples()) {
        CHECK(s.failure == col::Failure::none && obs::completed_witness(s.policy));
        CHECK(s.policy.metadata.identity.owner == f.owner && s.policy.metadata.identity.id == 37);
    }
    const auto reads = f.reader.calls; f.full();
    CHECK(f.collector.count() == 6 && f.reader.calls == reads);
    return true;
}
bool generation_or_body_binding_change_stops_session() {
    for (unsigned mutation = 0; mutation < 2; ++mutation) {
        Fixture f; CHECK(f.collector.arm()); f.full(); f.next();
        if (mutation == 0) {
            f.reader.put(f.owner + 0xBC, std::uint32_t{38}); f.reader.put(f.fg + 0x1B0, std::uint32_t{38});
        } else f.reader.put(f.bodies + 0x50, std::uint32_t{777});
        f.full();
        CHECK(f.collector.quiescent() && f.collector.count() == 2 && !f.collector.requested());
        CHECK(f.collector.samples()[0].failure == col::Failure::none);
        CHECK(f.collector.samples()[1].failure == col::Failure::scope);
        CHECK(f.collector.samples()[1].policy.reason == obs::Reason::selection_changed);
    }
    return true;
}
bool skipped_first_actor_does_not_read_full_metadata() {
    Fixture f; f.reader.put(f.owner, f.module + 1); CHECK(f.collector.arm()); f.full(); f.collector.stop();
    CHECK(f.collector.quiescent() && f.collector.count() == 0 && f.reader.calls == 4);
    return true;
}
bool late_exit_failure_cannot_publish_a_success() {
    for (unsigned mutation = 0; mutation < 3; ++mutation) {
        Fixture f; CHECK(f.collector.arm()); f.through_process(); f.primary_events();
        CHECK(obs::completed_witness(f.first().policy));
        auto exit = f.event(hook::Kind::Exit);
        if (mutation == 0) ++exit.job;
        if (mutation == 1) f.clock.time = 0;
        if (mutation == 2) f.clock.step = 1001;
        f.collector.emit(exit); f.collector.stop(); f.finish();
        CHECK(f.collector.quiescent() && f.first().published);
        CHECK(!col::complete(f.first()));
    }
    return true;
}
bool missing_exit_never_releases_or_recycles_owned_buffers() {
    Fixture f; CHECK(f.collector.arm()); f.through_process(); f.primary_events();
    f.collector.stop(); // Models timeout after a fiber was abandoned, not unwound.
    CHECK(!f.collector.requested() && !f.collector.quiescent());
    CHECK(!col::complete(f.first()) && !f.first().published);
    CHECK(!f.collector.arm());
    f.next(); f.full(); // Another nonce cannot replace the missing outer Exit.
    CHECK(!f.collector.quiescent() && !f.first().published);
    return true;
}
bool exit_waits_for_matching_cleanup_completion() {
    Fixture f; CHECK(f.collector.arm()); f.through_process(); f.primary_events();
    const auto reads = f.reader.calls;
    f.reader.forbidden = true;
    f.collector.emit(f.event(hook::Kind::Exit)); f.collector.stop();
    CHECK(f.first().exit_seen && !f.first().published && !col::complete(f.first()));
    CHECK(!f.collector.quiescent());
    f.collector.finish({999, true, true, true});
    CHECK(!f.collector.quiescent() && !f.first().published);
    f.next(); f.full(); // Missing completion cannot be displaced by another scope.
    CHECK(!f.collector.quiescent() && f.collector.count() == 1 && !f.first().published);
    --f.nonce; f.finish();
    CHECK(f.collector.quiescent() && f.first().published);
    // Deliberate next() delay is charged as cleanup, not silently omitted.
    CHECK(f.first().failure == col::Failure::budget);
    const auto ticks = f.first().total_ticks;
    f.finish(false, false, false);
    CHECK(f.first().total_ticks == ticks && f.first().failure == col::Failure::budget);
    CHECK(f.reader.calls == reads && f.reader.forbidden_calls == 0);
    return true;
}
bool cleanup_and_protocol_failures_cannot_publish_success() {
    for (unsigned mutation = 0; mutation < 8; ++mutation) {
        Fixture f; CHECK(f.collector.arm()); f.through_process(); f.primary_events();
        const auto reads = f.reader.calls; f.reader.forbidden = true;
        if (mutation != 5) f.collector.emit(f.event(hook::Kind::Exit));
        if (mutation == 3) f.clock.time = 0;
        if (mutation == 4) f.clock.time += 1001;
        if (mutation == 6) f.collector.emit(f.event(hook::Kind::Exit));
        if (mutation == 7) f.collector.emit(f.event(hook::Kind::StoreEnter));
        f.finish(mutation != 2, mutation != 0, mutation != 1);
        f.collector.stop();
        CHECK(f.collector.quiescent() && f.first().published && !col::complete(f.first()));
        CHECK(f.first().failure != col::Failure::none);
        if (mutation == 0) CHECK(f.first().failure == col::Failure::cleanup);
        if (mutation == 1) CHECK(f.first().failure == col::Failure::reentry);
        if (mutation == 5) CHECK(f.first().failure == col::Failure::incomplete);
        CHECK(f.reader.calls == reads && f.reader.forbidden_calls == 0);
    }
    return true;
}
bool stop_and_admission_share_one_atomic_lifecycle() {
    col::Lease closed; closed.stop();
    CHECK(closed.quiescent() && !closed.arm() && !closed.acquire());
    col::Lease owned; CHECK(owned.arm() && owned.acquire()); owned.stop();
    CHECK(!owned.requested() && !owned.quiescent() && !owned.acquire());
    owned.release(); CHECK(owned.quiescent() && !owned.arm() && !owned.acquire());
    // Repeated two-thread admission/stop ordering. A held lease must never be
    // reported quiescent, including before a sample or nonce has been stored.
    for (unsigned i = 0; i < 256; ++i) {
        col::Lease lease; CHECK(lease.arm());
        std::atomic<bool> go{false}, decided{false}, release{false};
        bool acquired = false;
        std::thread entrant([&] {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            acquired = lease.acquire(); decided.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
            if (acquired) lease.release();
        });
        go.store(true, std::memory_order_release); lease.stop();
        while (!decided.load(std::memory_order_acquire)) std::this_thread::yield();
        const bool correct = lease.quiescent() == !acquired;
        release.store(true, std::memory_order_release); entrant.join();
        CHECK(correct && lease.quiescent() && !lease.acquire());
    }
    return true;
}
struct PausedClock {
    std::atomic<bool> entered{false}, resume{false};
    Clock clock;
    static std::uint64_t now(void* p) noexcept {
        auto& self = *static_cast<PausedClock*>(p);
        if (!self.entered.exchange(true, std::memory_order_acq_rel))
            while (!self.resume.load(std::memory_order_acquire)) std::this_thread::yield();
        return Clock::now(&self.clock);
    }
};
bool stop_during_admission_waits_for_owner_release() {
    PausedClock clock;
    Fixture f(3, {&clock, PausedClock::now, 1000000}); CHECK(f.collector.arm());
    std::thread entrant([&] { f.full(); });
    while (!clock.entered.load(std::memory_order_acquire)) std::this_thread::yield();
    f.collector.stop();
    const bool held = !f.collector.quiescent() && !f.collector.requested();
    clock.resume.store(true, std::memory_order_release); entrant.join();
    CHECK(held && f.collector.quiescent());
    return true;
}
struct ReentrantClock {
    Fixture* fixture{};
    Clock clock;
    bool inject{}, remained_owned{};
    static std::uint64_t now(void* p) noexcept {
        auto& self = *static_cast<ReentrantClock*>(p);
        if (self.inject) {
            self.inject = false;
            self.fixture->finish();
            self.remained_owned = !self.fixture->collector.quiescent() && !self.fixture->first().published;
        }
        return Clock::now(&self.clock);
    }
};
bool recursive_completion_never_releases_the_outer_owner() {
    // Deliberately violate the nonreentrant Clock contract to verify that the
    // Exit/Completion guards are established before callbacks, not after them.
    for (bool during_exit : {false, true}) {
        ReentrantClock clock;
        Fixture f(3, {&clock, ReentrantClock::now, 1000000}); clock.fixture = &f;
        CHECK(f.collector.arm()); f.through_process(); f.primary_events();
        f.reader.forbidden = true;
        clock.inject = during_exit; f.collector.emit(f.event(hook::Kind::Exit));
        clock.inject = !during_exit; f.finish();
        CHECK(clock.remained_owned && f.collector.quiescent());
        CHECK(f.first().published && !col::complete(f.first()) && f.first().failure == col::Failure::scope);
        CHECK(f.reader.forbidden_calls == 0);
    }
    return true;
}
bool process_and_export_provenance_rejects_mismatches() {
    for (unsigned mutation = 0; mutation < 9; ++mutation) {
        Fixture f; CHECK(f.collector.arm());
        f.send(hook::Kind::Entry); f.send(hook::Kind::ResetEnter); f.send(hook::Kind::ResetReturn);
        if (mutation == 0) f.reader.put(f.process_context, f.art + 8);
        auto process = f.event(hook::Kind::ProcessEnter);
        if (mutation == 1) process.arguments[0] = 0;
        if (mutation == 2) process.arguments[1] = 0;
        f.collector.emit(process);
        if (mutation == 3) f.reader.put(f.segment + 0x28, f.raw + 8);
        if (mutation == 4) f.reader.put(f.segment, f.workspace);
        if (mutation == 5) f.reader.put(f.segment + 8, f.process_context + 8);
        f.send(hook::Kind::PostposeEnter); f.send(hook::Kind::PostposeReturn);
        auto returned = f.event(hook::Kind::ProcessReturn);
        if (mutation == 6) returned.arguments[0] += 4;
        if (mutation == 7) ++returned.arguments[1];
        if (mutation == 8) f.reader.put(f.process_context, f.art + 8);
        f.collector.emit(returned); f.primary_events(); f.send(hook::Kind::Exit); f.collector.stop();
        CHECK(f.collector.quiescent() && !col::complete(f.first()));
        CHECK(f.first().failure == col::Failure::scope || f.first().failure == col::Failure::arguments);
    }
    return true;
}
std::uint64_t wall_tick(void*) noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 0;
}
bool collector_cost() {
    LARGE_INTEGER frequency{};CHECK(QueryPerformanceFrequency(&frequency) && frequency.QuadPart>=1000);
    const auto freq=static_cast<std::uint64_t>(frequency.QuadPart);
    std::array<std::uint64_t,128> samples{};
    std::size_t completed{},budget_rejections{},max_reads{},max_bytes{};
    for (auto& sample:samples) {
        // Allocate/map the synthetic objects BEFORE timing. The real Collector
        // and real QPC budget then run over all16 bodies, but Reader is the
        // fixture's map-backed byte reader, not a live game/SEH transport.
        Fixture f(static_cast<unsigned>(obs::kMaxBodies),{nullptr,wall_tick,freq});
        CHECK(f.collector.arm());
        const auto start=wall_tick(nullptr);f.full();f.collector.stop();const auto end=wall_tick(nullptr);
        CHECK(start && end>=start && f.collector.quiescent());
        sample=end-start;
        if (col::complete(f.first())) ++completed;
        else { CHECK(f.first().failure==col::Failure::budget);++budget_rejections; }
        max_reads=std::max(max_reads,f.reader.calls);max_bytes=std::max(max_bytes,f.reader.copied_bytes);
    }
    std::sort(samples.begin(),samples.end());
    const auto us=[freq](auto n){return 1000000.0*static_cast<double>(n)/static_cast<double>(freq);};
    std::printf("OWNED_COLLECTOR_COMPONENT_ONLY bodies=16 n=%zu complete=%zu budget_reject=%zu p50_us=%.3f p95_us=%.3f max_us=%.3f reads_max=%zu bytes_max=%zu\n",
        samples.size(),completed,budget_rejections,us(samples[64]),us(samples[121]),us(samples.back()),max_reads,max_bytes);
    CHECK(completed+budget_rejections==samples.size());
    return true; // A duration is diagnostic data, not automatic game admission.
}
} // namespace

int main(int argc,char** argv) {
    if (argc==2 && std::strcmp(argv[1],"--cost")==0) return collector_cost() ? 0 : 1;
    if (argc!=1) return 2;
    const std::pair<const char*, bool (*)()> tests[]{
        {"complete/exit", complete_chain_is_event_only_and_exit_reads_nothing},
        {"missing stages", absent_or_misordered_stages_fail_closed},
        {"foreign frames", foreign_frames_and_return_sites_fail_closed},
        {"collection mutation", collection_mutations_fail_closed},
        {"store pairing", store_ancestry_arguments_and_pairs_fail_closed},
        {"exception/stop", exception_reentry_and_stop_wait_for_outer_exit},
        {"foreign nonce", foreign_nonce_does_not_finish_or_join_scope},
        {"observer budget", budget_charges_observer_intervals_not_native_gaps},
        {"six samples", six_samples_pin_one_actor_and_stop},
        {"selection change", generation_or_body_binding_change_stops_session},
        {"early filter", skipped_first_actor_does_not_read_full_metadata},
        {"late exit failure", late_exit_failure_cannot_publish_a_success},
        {"abandoned scope", missing_exit_never_releases_or_recycles_owned_buffers},
        {"delayed completion", exit_waits_for_matching_cleanup_completion},
        {"cleanup failures", cleanup_and_protocol_failures_cannot_publish_success},
        {"atomic lease", stop_and_admission_share_one_atomic_lifecycle},
        {"stop during admission", stop_during_admission_waits_for_owner_release},
        {"recursive completion", recursive_completion_never_releases_the_outer_owner},
        {"actual context provenance", process_and_export_provenance_rejects_mismatches},
    };
    std::size_t passed = 0;
    for (const auto& [name, run] : tests) {
        if (!run()) { std::cerr << "FAIL " << name << '\n'; return 1; }
        ++passed;
    }
    std::cout << passed << '/' << std::size(tests) << " passed\n";
    return 0;
}
