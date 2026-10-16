#pragma once

#include "coop_protocol.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Offline admission/matching policy only. No game reads, writes, native calls,
// ownership retention or network I/O. Protocol v1 has NO world/session identity:
// a local token only fences cached observations/cursors/bindings. Even a valid
// token CANNOT prove which world/session an arriving v1 packet belongs to. Old-
// world packets after a reset remain unresolved without a future wire epoch and
// handshake. Never use a peer runtime object id as the token, or assume names
// are globally unique. All successful results here are OBSERVATION ONLY.
// STATIC 2026-10-16: legacy name_hash is derived from object+8's32-byte debug
// label (14 prefix + "..." +14 suffix for long names), not the full spawn name.
// Even a collision-free stronger hash cannot restore the omitted name bytes.
namespace coop::enemy_snapshot_policy {

inline constexpr std::size_t kCapacity = kMaxEnemies;
inline constexpr std::size_t kHeaderBytes = offsetof(EnemyPacket, enemies);
inline constexpr float kCoordinateLimit = 1000000.0F;
inline constexpr std::uint64_t kMaximumLocalAgeMs = 1500;
// Conservative policy, NOT a newly confirmed engine invariant. Reject instead
// of repairing/normalizing data; runtime coverage must be assessed separately.
inline constexpr double kQuaternionNormSquaredTolerance = 0.02;

enum class Reason : std::uint8_t {
    kAccepted,
    kWrongPeer,
    kWrongRole,
    kMissingAuthorization,
    kAuthorizationChanged,
    kInvalidLocalClock,
    kStale,
    kNullBytes,
    kShortHeader,
    kBadMagic,
    kBadVersion,
    kCountOverflow,
    kWrongLength,
    kOldSequence,
    kInvalidPosition,
    kInvalidQuaternion,
    kUnknownFlags,
    kInvalidHealth,
    kDuplicateRemoteHash,
    kNotAdmitted,
    kIncompleteEnumeration,
    kLocalOverflow,
    kNullCandidates,
    kDuplicateLocalHash,
    kDuplicateLocalIdentity,
    kInvalidLocalIdentity,
};

struct AdmissionContext {
    bool configured_peer = false;
    bool receiver_is_client = false;
    // Explicit caller authorization for this local observation session, not
    // proof of the incoming packet's world or session (v1 cannot carry that).
    std::uint64_t local_observation_token = 0;
    std::uint64_t received_local_ms = 0;
    std::uint64_t now_local_ms = 0;
};

struct ReceiveCursor {
    std::uint64_t local_observation_token = 0;
    bool has_sequence = false;
    std::uint32_t sequence = 0;
};

struct ObservationAdmission {
    Reason reason = Reason::kNotAdmitted;
    EnemyPacket packet{};
    // Commit this cursor ONLY when accepted(). Rejection intentionally returns
    // an empty cursor; blindly assigning it would reset replay protection.
    ReceiveCursor next_cursor{};
    bool accepted() const { return reason == Reason::kAccepted; }
};

inline bool serial32_newer(std::uint32_t candidate, std::uint32_t previous) {
    const std::uint32_t distance = candidate - previous;
    return distance != 0 && distance < 0x80000000u;
}

inline Reason validate_records(const EnemyPacket& packet) {
    if (packet.magic != kEnemyPacketMagic) return Reason::kBadMagic;
    if (packet.version != kEnemyPacketVersion) return Reason::kBadVersion;
    if (packet.count > kCapacity) return Reason::kCountOverflow;
    for (std::size_t i = 0; i < packet.count; ++i) {
        const EnemyState& enemy = packet.enemies[i];
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(enemy.pos[axis]) ||
                std::fabs(enemy.pos[axis]) >= kCoordinateLimit) {
                return Reason::kInvalidPosition;
            }
        }
        // pos[3] is transported by v1 but not a world coordinate. Reject NaN/
        // infinity too; do not impose an unproven homogeneous-value convention.
        if (!std::isfinite(enemy.pos[3])) return Reason::kInvalidPosition;
        double norm_squared = 0;
        for (float component : enemy.quat) {
            if (!std::isfinite(component)) return Reason::kInvalidQuaternion;
            norm_squared += static_cast<double>(component) * component;
        }
        if (std::fabs(norm_squared - 1.0) > kQuaternionNormSquaredTolerance) {
            return Reason::kInvalidQuaternion;
        }
        if ((enemy.flags & ~kEnemyFlagDead) != 0) return Reason::kUnknownFlags;
        if (enemy.hp < -1) return Reason::kInvalidHealth;
        for (std::size_t j = 0; j < i; ++j) {
            if (enemy.name_hash == packet.enemies[j].name_hash) {
                return Reason::kDuplicateRemoteHash;
            }
        }
    }
    return Reason::kAccepted;
}

inline ObservationAdmission admit_wire_for_observation(const void* bytes, std::size_t byte_count,
                            const AdmissionContext& context,
                            const ReceiveCursor& cursor) {
    auto reject = [](Reason reason) {
        ObservationAdmission result{};
        result.reason = reason;
        return result;
    };
    if (!context.configured_peer) return reject(Reason::kWrongPeer);
    if (!context.receiver_is_client) return reject(Reason::kWrongRole);
    if (context.local_observation_token == 0) {
        return reject(Reason::kMissingAuthorization);
    }
    if ((cursor.has_sequence || cursor.local_observation_token != 0) &&
        cursor.local_observation_token != context.local_observation_token) {
        return reject(Reason::kAuthorizationChanged);
    }
    if (context.received_local_ms == 0 ||
        context.now_local_ms < context.received_local_ms) {
        return reject(Reason::kInvalidLocalClock);
    }
    if (context.now_local_ms - context.received_local_ms > kMaximumLocalAgeMs) {
        return reject(Reason::kStale);
    }
    if (bytes == nullptr) return reject(Reason::kNullBytes);
    if (byte_count < kHeaderBytes) return reject(Reason::kShortHeader);
    EnemyPacket packet{};
    std::memcpy(&packet, bytes, kHeaderBytes);
    if (packet.magic != kEnemyPacketMagic) return reject(Reason::kBadMagic);
    if (packet.version != kEnemyPacketVersion) return reject(Reason::kBadVersion);
    if (packet.count > kCapacity) return reject(Reason::kCountOverflow);
    const std::size_t exact_size = kHeaderBytes + packet.count * sizeof(EnemyState);
    if (byte_count != exact_size) return reject(Reason::kWrongLength);
    if (cursor.has_sequence && !serial32_newer(packet.sequence, cursor.sequence)) {
        return reject(Reason::kOldSequence);
    }
    std::memcpy(&packet, bytes, exact_size);
    const Reason record_reason = validate_records(packet);
    if (record_reason != Reason::kAccepted) return reject(record_reason);
    ObservationAdmission result{};
    result.reason = Reason::kAccepted;
    result.packet = packet;
    result.next_cursor = {context.local_observation_token, true, packet.sequence};
    return result;
}

struct LocalCandidate {
    std::uint32_t name_hash = 0;
    std::uintptr_t record = 0;
    std::uint32_t id = 0;
    std::uintptr_t object = 0;
    bool eligible = false;
};

struct Binding {
    std::size_t remote_index = 0;
    std::size_t local_index = 0;
    LocalCandidate identity{};
    std::uint64_t local_observation_token = 0;
};

struct ObservationMatches {
    Reason reason = Reason::kNotAdmitted;
    std::array<Binding, kCapacity> bindings{};
    std::size_t matched = 0;
    std::size_t unmatched_remote = 0;
    std::size_t unmatched_local = 0;
    bool accepted() const { return reason == Reason::kAccepted; }
};

// Candidates must come from one complete bounded enumeration. Any duplicate,
// including an ineligible row, invalidates the whole mapping. No proximity or
// array-order fallback. Missing actors are counted, never spawned/despawned.
// Unique 32-bit hashes still do not prove full name equality or cross-world
// identity; acceptance here cannot authorize writes to any matched actor.
inline ObservationMatches match_snapshot_for_observation(const ObservationAdmission& admission,
                                  const LocalCandidate* candidates,
                                  std::size_t candidate_count,
                                  bool enumeration_complete) {
    auto reject = [](Reason reason) {
        ObservationMatches result{};
        result.reason = reason;
        return result;
    };
    if (!admission.accepted() || !admission.next_cursor.has_sequence ||
        admission.next_cursor.local_observation_token == 0 ||
        admission.next_cursor.sequence != admission.packet.sequence) {
        return reject(Reason::kNotAdmitted);
    }
    const Reason record_reason = validate_records(admission.packet);
    if (record_reason != Reason::kAccepted) return reject(record_reason);
    if (candidate_count > kCapacity) return reject(Reason::kLocalOverflow);
    if (!enumeration_complete) return reject(Reason::kIncompleteEnumeration);
    if (candidate_count != 0 && candidates == nullptr) {
        return reject(Reason::kNullCandidates);
    }
    for (std::size_t i = 0; i < candidate_count; ++i) {
        const LocalCandidate& candidate = candidates[i];
        if (candidate.eligible && (candidate.record == 0 || candidate.object == 0)) {
            return reject(Reason::kInvalidLocalIdentity);
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (candidate.name_hash == candidates[j].name_hash) {
                return reject(Reason::kDuplicateLocalHash);
            }
            if ((candidate.record != 0 && candidate.record == candidates[j].record) ||
                (candidate.object != 0 && candidate.object == candidates[j].object)) {
                return reject(Reason::kDuplicateLocalIdentity);
            }
        }
    }
    ObservationMatches result{};
    result.reason = Reason::kAccepted;
    for (std::size_t i = 0; i < candidate_count; ++i) {
        if (candidates[i].eligible) ++result.unmatched_local;
    }
    for (std::size_t remote = 0; remote < admission.packet.count; ++remote) {
        bool found = false;
        for (std::size_t local = 0; local < candidate_count; ++local) {
            const LocalCandidate& candidate = candidates[local];
            if (!candidate.eligible || candidate.name_hash !=
                    admission.packet.enemies[remote].name_hash) continue;
            result.bindings[result.matched++] = {
                remote, local, candidate, admission.next_cursor.local_observation_token};
            --result.unmatched_local;
            found = true;
            break;
        }
        if (!found) ++result.unmatched_remote;
    }
    return result;
}

// Equality of nonretained identity bytes only. This does not validate pointers,
// retain objects or grant mutation authority. Caller must obtain a fresh live
// candidate and separately revalidate age/lifetime. This remains observation-only;
// even a matching local token cannot validate an incoming v1 packet's world.
inline bool binding_still_matches(const Binding& binding,
                                  const LocalCandidate& fresh,
                                  std::uint64_t authorized_token) {
    const LocalCandidate& old = binding.identity;
    return authorized_token != 0 &&
           binding.local_observation_token == authorized_token &&
           old.eligible && fresh.eligible && old.record != 0 && old.object != 0 &&
           old.name_hash == fresh.name_hash && old.record == fresh.record &&
           old.id == fresh.id && old.object == fresh.object;
}

} // namespace coop::enemy_snapshot_policy
