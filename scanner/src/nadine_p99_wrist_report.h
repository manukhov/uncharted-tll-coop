#pragma once
#include "nadine_p99_source_observation.h"
#include "nadine_p99_wrist_read_diagnostic.h"
#include <atomic>
#include <cstdint>
#include <string>

namespace coop::p99_wrist_report {
// This is an owned diagnostic value, not a source/owner lifetime certificate.
struct Record {
    p99_source_observation::Reason reason = p99_source_observation::Reason::kNone;
    std::uint32_t probes = 0, attempts = 0, diagnostic_probe = 0;
    std::uint64_t spent_ticks = 0;
    bool normal_return = false, captured = false, diagnostic_available = false;
    p99_wrist_read_diagnostic::Diagnostic diagnostic{};
    p99_wrist_snapshot::Payload payload{};
};
static_assert(std::is_trivially_copyable_v<Record>);
static_assert(std::is_trivially_destructible_v<Record>);

// One immutable publication. No waits/spins, source pointers, allocation or I/O
// in publish. The worker reads only after acquire of the final ready state.
class Mailbox {
    enum class State { Empty, Writing, Ready };
    static_assert(std::atomic<State>::is_always_lock_free);
    std::atomic<State> state_{State::Empty};
    Record value_{};
public:
    bool publish(const Record& record) noexcept {
        if (record.reason == p99_source_observation::Reason::kNone) return false;
        auto expected = State::Empty;
        if (!state_.compare_exchange_strong(expected, State::Writing, std::memory_order_acq_rel)) return false;
        value_ = record;
        // Never export stale/partial private bytes on a refusal or unwind.
        value_.captured = record.captured && record.normal_return &&
            record.reason == p99_source_observation::Reason::kSuccess;
        if (!value_.captured) value_.payload = {};
        state_.store(State::Ready, std::memory_order_release);
        return true;
    }
    bool read(Record& out) const noexcept {
        if (state_.load(std::memory_order_acquire) != State::Ready) return false;
        out = value_;
        return true;
    }
};

// OFF-THREAD ONLY. Explicit scalar JSON and raw hex avoid ABI padding and float
// normalization. Zero payload bytes on failure; no address or live authority.
inline std::string json(const Record& r, std::uint32_t pid, std::uint64_t creation,
                        std::uint64_t arm_ms, std::uint64_t qpc_frequency) {
    std::string out = "{\"schema\":\"tll-p99-wrist-v1\"";
    const auto number = [&](const char* name, auto value) {
        out += ",\""; out += name; out += "\":"; out += std::to_string(value);
    };
    number("pid",pid); number("creation",creation); number("arm_ms",arm_ms);
    number("qpc_frequency",qpc_frequency); number("overhead_ticks",r.spent_ticks);
    out += ",\"reason\":\""; out += p99_source_observation::reason_name(r.reason); out += '"';
    number("probes",r.probes); number("attempts",r.attempts);
    number("normal_return",r.normal_return); number("diagnostic_available",r.diagnostic_available);
    number("diagnostic_probe",r.diagnostic_probe);
    const auto& d=r.diagnostic;
    number("phase",static_cast<unsigned>(d.phase)); number("read_reject",static_cast<unsigned>(d.reason));
    number("owner_check",static_cast<unsigned>(d.owner.check)); number("numeric_check",static_cast<unsigned>(d.numeric));
    number("initial_slot_attempted",d.owner.initial_slot.attempted);
    number("initial_slot_read_ok",d.owner.initial_slot.read_ok); number("initial_slot",d.owner.initial_slot.value);
    number("closing_slot_attempted",d.owner.closing_slot.attempted);
    number("closing_slot_read_ok",d.owner.closing_slot.read_ok); number("closing_slot",d.owner.closing_slot.value);
    number("command_checked",d.command_checked); number("command_reject",static_cast<unsigned>(d.command));
    number("snapshot_attempted",d.snapshot_attempted); number("snapshot_reject",static_cast<unsigned>(d.snapshot));
    number("allocation_reject",static_cast<unsigned>(d.allocation));
    const bool captured=r.captured && r.normal_return && r.reason==p99_source_observation::Reason::kSuccess;
    number("payload_bytes",captured ? 256 : 0);
    out += ",\"source_admitted\":false,\"final_render_admitted\":false,\"game_data_writes\":0";
    out += ",\"boundary\":\"141ABA978\",\"layout\":\"JP24,JP26,JP49,JP51,W22\",\"payload_hex\":\"";
    if (captured) for (const auto b:r.payload.bytes) {
        constexpr char hex[]="0123456789abcdef";
        const auto byte=static_cast<unsigned>(b); out+=hex[byte>>4]; out+=hex[byte&15];
    }
    out += "\"}\n";
    return out;
}
} // namespace coop::p99_wrist_report
