#pragma once
#include "enemy_jointblend_capture.h"
#include "enemy_jointblend_hooks.h"
#include "enemy_jointblend_native_contract.h"
#include "enemy_jointblend_lease.h"
#include "nadine_p99_group_native_frame.h"
#include <atomic>

namespace coop::enemy_jointblend_collector {
namespace obs = enemy_jointblend_observation;
namespace cap = enemy_jointblend_capture;
namespace hooks = enemy_jointblend_hooks;
namespace native = enemy_jointblend_native_contract;
// Collector timing alone is NOT the complete wrapper/FLS/relay overhead.
// These integration prerequisites remain closed; no runtime startup uses this
// collector yet. Synthetic tests deliberately exercise only its local contract.
inline constexpr bool kCompleteWrapperBudgetMeasured = false;
inline constexpr bool kRuntimeAdmission = false;
// Cmd24 export proves neither that cmd16 ran nor which program operators ran.
inline constexpr bool kProgramExecutionWitnessAvailable = false;
using Address = std::uintptr_t;
using Words8 = std::array<std::uint32_t, 8>;
enum class Failure : std::uint8_t { none, clock, read, scope, callsite, arguments, incomplete, exception, reentry, budget, cleanup };
struct BodyCapture {
    Words8 input{}, stored{}, locator{};
    std::array<std::uint64_t, 2> valid{};
    Address input_address{};
    bool entered{}, returned{};
};
struct Sample {
    obs::Scope policy{};
    obs::RootCopy roots{};
    cap::MatrixCapture matrices{};
    cap::LocatorCapture before_primary{};
    cap::ProgramCapture program{};
    std::array<BodyCapture, obs::kMaxBodies> bodies{};
    cap::Filter filter{};
    cap::Status read_status{};
    Failure failure{};
    std::uint64_t nonce{}, pending_ticks{}, total_ticks{}, exit_tick{};
    Address job{}, process_context{}, process_wrapper_pc{};
    Address command_list{}; // Generated transient commands, NOT raw skeleton program bytes.
    std::uint32_t command_words{};
    bool begin_attempted{}, begun{}, reset_entered{}, process_active{}, normal_exit{}, published{};
    bool exit_seen{}, exit_processing{}, completion_started{}, cleanup_ok{}, observer_ok{};
};
// Policy closure/Exit alone is insufficient: cleanup and completion can still
// fail after primary-return closed the event trace. Read only while quiescent.
inline bool complete(const Sample& sample) noexcept {
    return sample.published && sample.exit_seen && sample.completion_started &&
        sample.cleanup_ok && sample.observer_ok && sample.normal_exit && sample.failure == Failure::none &&
        obs::completed_witness(sample.policy);
}
struct Clock {
    void* context{};
    std::uint64_t (*now)(void*) noexcept{}; // Bounded, nonthrowing, nonreentrant; no game access. Zero means failure.
    std::uint64_t frequency{};
};

// One fixed, process-lifetime collector. Only one current native invocation
// owns mutable data. Worker consumes it ONLY after quiescent() acquire succeeds.
// Stop prevents new scopes; it does not cancel or expose an in-flight sample.
template<class Reader>
class Collector {
public:
    Collector(Reader& reader, native::ModuleImage image, Clock clock) noexcept
        : reader_(reader), image_(image), clock_(clock) {}
    bool arm() noexcept {
        if (!clock_.now || clock_.frequency < 1000 || !native::valid_module(image_)) return false;
        return lease_.arm();
    }
    void stop() noexcept { lease_.stop(); }
    bool requested() const noexcept { return lease_.requested(); }
    bool quiescent() const noexcept { return lease_.quiescent(); }
    const std::array<Sample, obs::kMaxSamples>& samples() const noexcept { return samples_; }
    std::size_t count() const noexcept { return count_; } // Only after quiescent().
    std::uint32_t probes() const noexcept { return probes_; } // Only after quiescent().
    hooks::Sink sink() noexcept {
        return {this, [](void* p) noexcept { return static_cast<Collector*>(p)->requested(); },
            [](void* p, const hooks::Event& e) noexcept { static_cast<Collector*>(p)->emit(e); },
            [](void* p, const hooks::Completion& c) noexcept { static_cast<Collector*>(p)->finish(c); }};
    }
    void emit(const hooks::Event& e) noexcept {
        if (e.kind == hooks::Kind::Entry) { enter(e); return; }
        if (!e.invocation_nonce || active_.load(std::memory_order_acquire) != e.invocation_nonce) return;
        if (!count_ || samples_[count_-1].nonce!=e.invocation_nonce) { stop(); return; }
        auto& s = samples_[count_ - 1];
        if (s.exit_seen || s.completion_started) { fail(s,Failure::scope);stop();return; }
        if (e.kind == hooks::Kind::Exit) {
            s.exit_seen=true;s.exit_processing=true; // Before any clock callback.
            const auto start=tick();
            // No native pointer dereferences after original JointBlend returns.
            if (e.job != s.job) fail(s, Failure::scope);
            s.normal_exit = e.normal_return;
            if (!e.normal_return) fail(s, Failure::exception);
            if (!s.begun) { if (!s.begin_attempted) ++session_.attempted; fail(s, Failure::incomplete); }
            else if (!obs::completed_witness(s.policy)) fail(s, Failure::incomplete);
            s.exit_tick=charge(s,start);
            s.exit_processing=false;
            return;
        }
        const auto start = tick();
        if (e.kind == hooks::Kind::Exception || e.kind == hooks::Kind::Reentry) {
            fail(s, e.kind == hooks::Kind::Exception ? Failure::exception : Failure::reentry);
            if (s.begun) obs::reentered(s.policy);
        } else if (s.failure == Failure::none) {
            observe(s, e);
        }
        charge(s, start);
    }
    void finish(const hooks::Completion& c) noexcept {
        if (!c.invocation_nonce || active_.load(std::memory_order_acquire)!=c.invocation_nonce) return;
        if (!count_ || samples_[count_-1].nonce!=c.invocation_nonce) { stop();return; }
        auto& s=samples_[count_-1];
        if (s.exit_processing || s.completion_started) { fail(s,Failure::scope);stop();return; }
        s.completion_started=true; // Reentrant/duplicate completion never releases the outer owner.
        if (!s.exit_seen) fail(s,Failure::incomplete);
        if (c.normal_return!=s.normal_exit) fail(s,Failure::scope);
        s.cleanup_ok=c.cleanup_ok;s.observer_ok=c.observer_ok;
        if (!c.cleanup_ok) fail(s,Failure::cleanup);
        if (!c.observer_ok) fail(s,Failure::reentry);
        // No game addresses or Reader calls here. Charge the previously missing
        // Exit-to-cleanup interval, not an inclusive native-time estimate.
        charge(s,s.exit_tick);
        if (count_==obs::kMaxSamples || session_.stopped || s.failure!=Failure::none) stop();
        s.published=true; // Last output mutation, before release of the one-word lease.
        release_empty();
    }
private:
    Reader& reader_;
    native::ModuleImage image_{};
    Clock clock_{};
    std::atomic<std::uint64_t> active_{0};
    Lease lease_{};
    obs::Session session_{};
    std::array<Sample, obs::kMaxSamples> samples_{};
    std::size_t count_{};
    std::uint32_t probes_{};
    Address selected_owner_{};
    std::uint64_t next_tick_{};
    std::uint64_t tick() const noexcept { return clock_.now ? clock_.now(clock_.context) : 0; }
    static void fail(Sample& s, Failure why) noexcept { if (s.failure == Failure::none) s.failure = why; }
    bool read_ok(Sample& s, cap::Status status) noexcept {
        if (status.ok()) return true;
        s.read_status = status; fail(s, Failure::read); return false;
    }
    std::uint64_t charge(Sample& s, std::uint64_t start) noexcept {
        const auto end = tick();
        if (!start || !end || end < start) { fail(s, Failure::clock); stop(); return 0; }
        const auto elapsed = end - start;
        const auto limit = clock_.frequency / 1000;
        if (s.total_ticks > limit || elapsed > limit - s.total_ticks) {
            fail(s, Failure::budget); if (s.begun) obs::reject(s.policy, obs::Reason::budget); stop(); return end;
        }
        s.total_ticks += elapsed;
        if (s.begun) { if (!obs::charge(s.policy, 0, elapsed)) fail(s, Failure::budget); }
        else s.pending_ticks += elapsed;
        return end;
    }
    void enter(const hooks::Event& e) noexcept {
        if (!e.invocation_nonce || !lease_.acquire()) return;
        // The single-word lease is busy BEFORE publishing the nonce or counters.
        active_.store(e.invocation_nonce, std::memory_order_release);
        if (!requested()) { release_empty(); return; }
        const auto start = tick();
        if (!start || count_ >= obs::kMaxSamples) { stop(); release_empty(); return; }
        if (start < next_tick_) { release_empty(); return; }
        if (++probes_ > 600) { stop(); release_empty(); return; }
        cap::Filter filter{};
        const auto status = cap::fast_filter(reader_, static_cast<Address>(image_.base), e.job, filter);
        if (!status.ok() || (selected_owner_ && filter.owner != selected_owner_)) {
            const auto end = tick();
            if (!end || end < start || end - start > clock_.frequency / 1000) stop();
            release_empty(); return;
        }
        if (!selected_owner_) selected_owner_ = filter.owner;
        auto& s = samples_[count_++]; // Never reset/reuse large buffers on game path.
        s.nonce = e.invocation_nonce; s.job = e.job; s.filter = filter;
        const auto gap = clock_.frequency / 4;
        if (start > UINT64_MAX - gap) { fail(s, Failure::clock); stop(); }
        else next_tick_ = start + gap;
        charge(s, start);
    }
    void release_empty() noexcept {
        active_.store(0, std::memory_order_release);
        lease_.release();
    }
    bool frame(Sample& s, const hooks::Event& e) noexcept {
        if (e.job != s.job || e.invocation_nonce != s.nonce || !e.original_jointblend_return_slot ||
            (s.begun && e.original_jointblend_return_slot != s.policy.metadata.identity.native_return_slot)) {
            fail(s, Failure::scope); return false;
        }
        return true;
    }
    bool fresh(Sample& s, const hooks::Event& e, obs::Metadata& m) noexcept {
        if (!frame(s, e) || !read_ok(s, cap::read_metadata(reader_, static_cast<Address>(image_.base), e.job,
                e.original_jointblend_return_slot, m,
                s.begun ? cap::MetadataRead::single_pass : cap::MetadataRead::paired_pass))) return false;
        if (m.identity.owner != s.filter.owner || m.identity.record != s.filter.record || m.identity.fg != s.filter.fg ||
            (s.begun && !(m.identity == s.policy.metadata.identity))) {
            fail(s, Failure::scope); return false;
        }
        if (s.begun) {
            if (m.body_count != s.policy.metadata.body_count || m.selected_count != s.policy.metadata.selected_count ||
                m.selected_joints != s.policy.metadata.selected_joints) { fail(s, Failure::scope); return false; }
            for (std::size_t i = 0; i < m.body_count; ++i)
                if (!(m.bodies[i] == s.policy.metadata.bodies[i])) { fail(s, Failure::scope); return false; }
        }
        return true;
    }
    bool return_at(Sample& s, const hooks::Event& e, std::uint32_t rva) noexcept {
        if (native::matches_return(image_, e.wrapper_entry_return_pc, rva)) return true;
        fail(s, Failure::callsite); return false;
    }
    static obs::Witness witness(const Sample& s, const obs::Metadata& m) noexcept { return {m.identity, s.nonce, true}; }
    bool context_art(Sample& s, Address context, const obs::Metadata& m) noexcept {
        Address art{};
        if (!read_ok(s, cap::detail::read(reader_, context, 0, art))) return false;
        if (art == m.identity.art) return true;
        fail(s, Failure::scope); return false;
    }
    bool export_args(Sample& s, const hooks::Event& e, const obs::Metadata& m, obs::Export& out) noexcept {
        const native::ProcessFrameWitness f{e.arguments[1], e.original_process_return_slot,
            s.process_wrapper_pc, e.wrapper_entry_return_pc, native::SiteId::kGroup0Process};
        if (!s.process_active || e.arguments[2] != s.process_context || !native::validate_group0_process_frame(image_, f)) {
            fail(s, Failure::scope); return false;
        }
        Address raw{}, work{}, context{};
        if (!context_art(s, e.arguments[2], m) ||
            !read_ok(s, cap::detail::read(reader_, e.arguments[1], 0, work)) ||
            !read_ok(s, cap::detail::read(reader_, e.arguments[1], 8, context)) ||
            !read_ok(s, cap::detail::read(reader_, e.arguments[1], 0x28, raw))) return false;
        if (!work || work != e.arguments[0] || context != s.process_context || raw != m.identity.raw) {
            fail(s, Failure::scope); return false;
        }
        out.id = static_cast<std::uint32_t>(e.arguments[3]);
        if (!read_ok(s, cap::detail::read(reader_, e.arguments[1], 0x38, out.descriptor)) ||
            !read_ok(s, cap::detail::read(reader_, e.arguments[1], 0x58, out.workspace)) ||
            !read_ok(s, cap::detail::read(reader_, e.arguments[4], 0, out.destination))) return false;
        out.first = m.identity.first; out.count = m.identity.workspace_count;
        return true;
    }
    void observe(Sample& s, const hooks::Event& e) noexcept {
        using K = hooks::Kind;
        if ((e.kind == K::PostposeEnter || e.kind == K::PostposeReturn) &&
            e.arguments[3] != obs::kExportId) return;
        if ((e.kind == K::ProcessEnter || e.kind == K::ProcessReturn) && e.arguments[3] != 0) return;
        obs::Metadata m{};
        if (!fresh(s, e, m)) return;
        auto w = witness(s, m);
        switch (e.kind) {
        case K::ResetEnter:
            if (s.begun || !return_at(s, e, 0x1A92BAA) || e.arguments[0] != m.identity.cache ||
                e.arguments[1] != m.identity.fg + 0x1C0) { fail(s, Failure::arguments); return; }
            s.begin_attempted = true;
            if (!obs::begin(s.policy, session_, m, s.nonce, clock_.frequency)) { fail(s, Failure::scope); return; }
            s.begun = true; s.reset_entered = true;
            if (!obs::charge(s.policy, 0, s.pending_ticks)) { fail(s, Failure::budget); return; }
            // Capture only exact fixed-size candidate bytes; unknown extents remain unverified.
            if (m.identity.program_bytes == cap::kCandidateProgramBytes && m.identity.program_relative == 0x5340 &&
                m.identity.group_table_relative == 0x40)
                read_ok(s, cap::read_program_once(reader_, m, s.program));
            break;
        case K::ResetReturn:
            if (!s.begun || !s.reset_entered || !e.normal_return || !return_at(s, e, 0x1A92BAA) ||
                e.arguments[0] != m.identity.cache || e.arguments[1] != m.identity.fg + 0x1C0 ||
                e.result != m.identity.matrix_bank) { fail(s, Failure::arguments); return; }
            if (read_ok(s, cap::read_roots(reader_, m, s.roots))) obs::reset_returned(s.policy, w, s.roots);
            break;
        case K::ProcessEnter:
            if (!s.begun || e.site != hooks::ProcessSite::first || s.process_active ||
                !return_at(s, e, 0x1A92E7D) || !p99_group_native_frame::owns(true, e.arguments[2], e.original_jointblend_return_slot)) {
                fail(s, Failure::scope); return;
            }
            if (!e.arguments[0] || !e.arguments[1] || e.arguments[1] > UINT32_MAX ||
                !obs::fits_span(e.arguments[0], e.arguments[1] * 4)) { fail(s, Failure::arguments); return; }
            if (!context_art(s, e.arguments[2], m)) return;
            s.command_list = e.arguments[0]; s.command_words = static_cast<std::uint32_t>(e.arguments[1]);
            s.process_context = e.arguments[2]; s.process_wrapper_pc = e.wrapper_entry_return_pc;
            s.process_active = true;
            obs::process_entered(s.policy, w, 0, true); break;
        case K::PostposeEnter:
        case K::PostposeReturn: {
            obs::Export out{};
            if (!export_args(s, e, m, out)) return;
            if (e.kind == K::PostposeEnter) obs::cmd24_entered(s.policy, w, out);
            else if (!e.normal_return) fail(s, Failure::exception);
            else if (obs::cmd24_returned(s.policy, w, out))
                read_ok(s, cap::read_group0_matrices(reader_, m, cap::CaptureTiming::export_returned, s.matrices));
            break;
        }
        case K::ProcessReturn:
            if (!e.normal_return || !s.process_active || e.site != hooks::ProcessSite::first ||
                e.arguments[2] != s.process_context || e.arguments[0] != s.command_list ||
                e.arguments[1] != s.command_words || !return_at(s, e, 0x1A92E7D)) { fail(s, Failure::scope); return; }
            if (!context_art(s, e.arguments[2], m)) return;
            s.process_active = false; obs::process_returned(s.policy, w); break;
        case K::PrimaryEnter:
            if (!return_at(s, e, 0x1BBE11E) || !obs::primary_entered(s.policy, w, e.arguments[0])) break;
            read_ok(s, cap::read_selected_locators(reader_, m, cap::CaptureTiming::pre_consumer, s.before_primary)); break;
        case K::PrimaryReturn:
            if (!e.normal_return || !return_at(s, e, 0x1BBE11E) || e.arguments[0] != m.identity.composite) {
                fail(s, Failure::arguments); return;
            }
            if (obs::primary_returned(s.policy, w)) obs::close(s.policy, w);
            break;
        case K::StoreEnter:
        case K::StoreReturn: {
            Address ancestor_value{}, ancestor_slot{};
            if (!cap::detail::add(e.wrapper_entry_return_slot, native::kBodyStoreToRefreshReturnSlot, ancestor_slot) ||
                !read_ok(s, cap::detail::read(reader_, ancestor_slot, 0, ancestor_value)) ||
                !native::validate_body_store_frame(image_, {e.wrapper_entry_return_slot, e.wrapper_entry_return_pc,
                    ancestor_slot, ancestor_value})) { fail(s, Failure::callsite); return; }
            if (s.policy.stage != obs::Stage::primary_entered) { fail(s, Failure::scope); return; }
            std::size_t index = 0;
            while (index < m.body_count && m.bodies[index].body != e.arguments[0]) ++index;
            if (index == m.body_count) { fail(s, Failure::arguments); return; }
            auto& b = s.bodies[index];
            if (e.kind == K::StoreEnter) {
                if (b.entered) { fail(s, Failure::reentry); return; }
                b.entered = true; b.input_address = e.arguments[1];
                read_ok(s, cap::detail::read(reader_, e.arguments[1], 0, b.input));
            } else {
                if (!e.normal_return || !b.entered || b.returned || b.input_address != e.arguments[1]) {
                    fail(s, Failure::scope); return;
                }
                b.returned = true;
                if (!read_ok(s, cap::detail::read(reader_, e.arguments[0], 0, b.stored))) return;
                obs::Locator locator{};
                if (m.bodies[index].source == 3) {
                    const auto joint = static_cast<unsigned>(m.bodies[index].joint);
                    if (!read_ok(s, cap::detail::read(reader_, m.identity.cache, 0x40, b.valid)) ||
                        !read_ok(s, cap::detail::read(reader_, m.identity.locator_bank, 32u * joint, b.locator))) return;
                    std::array<std::uint64_t, 2> after_bits{};
                    if (!read_ok(s, cap::detail::read(reader_, m.identity.cache, 0x40, after_bits))) return;
                    if (b.valid != after_bits) { fail(s, Failure::scope); return; }
                    locator = {obs::CapturePoint::post_store, (b.valid[joint / 64] & (UINT64_C(1) << (joint % 64))) != 0,
                        obs::Comparison::not_performed};
                }
                const obs::StoreWitness store{obs::kBodyStoreRva, obs::kBodyStoreCallRva, obs::kBodyStoreReturnRva,
                    obs::kRefreshAncestorReturnRva, static_cast<std::uint32_t>(e.result), true, true};
                obs::body_stored(s.policy, w, index, m.bodies[index], store, locator);
            }
            break;
        }
        default: fail(s, Failure::scope); break;
        }
        if (s.begun && s.policy.reason != obs::Reason::none) fail(s, Failure::scope);
        // Fresh post-copy metadata recheck: still reject-only, not a lease.
        if (s.failure == Failure::none) { obs::Metadata after{}; fresh(s, e, after); }
    }
};
} // namespace coop::enemy_jointblend_collector
