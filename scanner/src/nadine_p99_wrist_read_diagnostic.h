#pragma once
#include "nadine_p99_source_read.h"
#include "nadine_p99_wrist_snapshot.h"
#include <cstdint>
#include <type_traits>

namespace coop::p99_wrist_read_diagnostic {
enum class Reject : std::uint8_t {
    None, Output, Gate, NormalJob, Retained, Owner, OwnerIdentity,
    ProcessShape, ProcessIdentity, Program, Commands, ProcessChanged,
    Segment, WorkBuffer, Metadata, Snapshot, ClosingOwner, Exception,
};
enum class Phase : std::uint8_t { None, Owner, Process, Payload, ClosingOwner };
// Owned scalar first-failure evidence. No addresses, static-string pointers,
// callbacks, page certificates or source payload. A successful snapshot is
// not admitted to the game by this diagnostic and cannot relax any predicate.
struct Diagnostic {
    Reject reason = Reject::None;
    Phase phase = Phase::None;
    p99_source_read::OwnerObservation owner{};
    p99_source_read::OwnerNumericCheck numeric = p99_source_read::OwnerNumericCheck::NotStarted;
    bool command_checked = false;
    p99_group_admission::Reject command = p99_group_admission::Reject::kNone;
    p99_wrist_snapshot::Reject snapshot = p99_wrist_snapshot::Reject::Phase;
    bool snapshot_attempted = false;
    p99_group_admission::Reject allocation = p99_group_admission::Reject::kNone;
};
static_assert(std::is_trivially_copyable_v<Diagnostic>);
static_assert(std::is_trivially_destructible_v<Diagnostic>);
} // namespace coop::p99_wrist_read_diagnostic
