#include "enemy_snapshot_policy.h"

#include <array>
#include <iostream>
#include <limits>

namespace {
namespace policy = coop::enemy_snapshot_policy;
using policy::Reason;

// Release builds must execute every check.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; \
            return false; \
        } \
    } while (false)

coop::EnemyPacket packet(std::uint16_t count = 2) {
    coop::EnemyPacket result{};
    result.count = count;
    result.sequence = 41;
    for (std::size_t i = 0; i < count && i < policy::kCapacity; ++i) {
        result.enemies[i].name_hash = static_cast<std::uint32_t>(100 + i);
        result.enemies[i].quat[3] = 1;
        result.enemies[i].pos[0] = static_cast<float>(i);
    }
    return result;
}

policy::AdmissionContext context() { return {true, true, 71, 10000, 10010}; }

std::size_t wire_size(const coop::EnemyPacket& value) {
    return policy::kHeaderBytes + value.count * sizeof(coop::EnemyState);
}

policy::ObservationAdmission admit(const coop::EnemyPacket& value) {
    return policy::admit_wire_for_observation(&value, wire_size(value), context(), {});
}

std::array<policy::LocalCandidate, 3> local() {
    return {{{101, 0x1000, 20, 0x2000, true},
             {100, 0x1100, 21, 0x2100, true},
             {102, 0x1200, 22, 0x2200, true}}};
}

bool valid_wire_and_clear_rejection() {
    auto value = packet();
    auto result = admit(value);
    CHECK(result.accepted());
    CHECK(result.packet.count == 2 && result.packet.enemies[1].name_hash == 101);
    CHECK(result.next_cursor.has_sequence && result.next_cursor.sequence == 41);
    CHECK(result.next_cursor.local_observation_token == 71);
    value.magic = 0;
    result = admit(value);
    CHECK(result.reason == Reason::kBadMagic);
    CHECK(result.packet.count == 0 && result.packet.enemies[0].name_hash == 0);
    CHECK(!result.next_cursor.has_sequence && result.next_cursor.local_observation_token == 0);
    return true;
}

bool malformed_lengths_and_header() {
    auto value = packet();
    CHECK(policy::admit_wire_for_observation(nullptr, 0, context(), {}).reason == Reason::kNullBytes);
    for (std::size_t bytes = 0; bytes < policy::kHeaderBytes; ++bytes) {
        CHECK(policy::admit_wire_for_observation(&value, bytes, context(), {}).reason == Reason::kShortHeader);
    }
    CHECK(policy::admit_wire_for_observation(&value, policy::kHeaderBytes, context(), {}).reason == Reason::kWrongLength);
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value) - 1, context(), {}).reason == Reason::kWrongLength);
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value) + 1, context(), {}).reason == Reason::kWrongLength);
    CHECK(policy::admit_wire_for_observation(&value, sizeof(value), context(), {}).reason == Reason::kWrongLength);
    value.version++;
    CHECK(admit(value).reason == Reason::kBadVersion);
    value = packet();
    value.count = 33;
    CHECK(policy::admit_wire_for_observation(&value, sizeof(value), context(), {}).reason == Reason::kCountOverflow);
    CHECK(admit(packet(0)).accepted());
    CHECK(admit(packet(32)).accepted());
    return true;
}

bool scope_clock_and_authorization() {
    const auto value = packet();
    auto check = [&](policy::AdmissionContext scope, Reason expected) {
        return policy::admit_wire_for_observation(&value, wire_size(value), scope, {}).reason == expected;
    };
    auto scope = context(); scope.configured_peer = false;
    CHECK(check(scope, Reason::kWrongPeer));
    scope = context(); scope.receiver_is_client = false;
    CHECK(check(scope, Reason::kWrongRole));
    scope = context(); scope.local_observation_token = 0;
    CHECK(check(scope, Reason::kMissingAuthorization));
    scope = context(); scope.received_local_ms = 0;
    CHECK(check(scope, Reason::kInvalidLocalClock));
    scope = context(); scope.now_local_ms = 9999;
    CHECK(check(scope, Reason::kInvalidLocalClock));
    scope = context(); scope.now_local_ms = 11500;
    CHECK(check(scope, Reason::kAccepted));
    scope.now_local_ms++;
    CHECK(check(scope, Reason::kStale));
    policy::ReceiveCursor cursor{72, true, 40};
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), context(), cursor).reason == Reason::kAuthorizationChanged);
    cursor = {0, true, 40};
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), context(), cursor).reason == Reason::kAuthorizationChanged);
    return true;
}

bool serial_order_rollover_and_peer_timestamp() {
    auto value = packet();
    auto scope = context();
    policy::ReceiveCursor cursor{71, true, 41};
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), scope, cursor).reason == Reason::kOldSequence);
    value.sequence = 40;
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), scope, cursor).reason == Reason::kOldSequence);
    value.sequence = 42;
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), scope, cursor).accepted());
    cursor.sequence = 0xFFFFFFFFu; value.sequence = 0;
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), scope, cursor).accepted());
    cursor.sequence = 0; value.sequence = 0xFFFFFFFFu;
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), scope, cursor).reason == Reason::kOldSequence);
    value.sequence = 0x80000000u;
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), scope, cursor).reason == Reason::kOldSequence);
    value.sequence = 1; value.timestamp_ms = 0xFFFFFFFFu;
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), scope, cursor).accepted());
    value.timestamp_ms = 0; scope.now_local_ms = 11501;
    CHECK(policy::admit_wire_for_observation(&value, wire_size(value), scope, cursor).reason == Reason::kStale);
    return true;
}

bool finite_position_and_quaternion_policy() {
    constexpr auto infinity = std::numeric_limits<float>::infinity();
    constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (float invalid : {infinity, -infinity, nan, 1000000.0F, -1000000.0F}) {
            auto value = packet(); value.enemies[0].pos[axis] = invalid;
            CHECK(admit(value).reason == Reason::kInvalidPosition);
        }
        auto value = packet(); value.enemies[0].pos[axis] = 999999.0F;
        CHECK(admit(value).accepted());
    }
    auto value = packet(); value.enemies[0].pos[3] = nan;
    CHECK(admit(value).reason == Reason::kInvalidPosition);
    value = packet(); value.enemies[0].quat[3] = 0;
    CHECK(admit(value).reason == Reason::kInvalidQuaternion);
    for (unsigned axis = 0; axis < 4; ++axis) {
        for (float invalid : {infinity, nan, std::numeric_limits<float>::max()}) {
            value = packet(); value.enemies[0].quat[axis] = invalid;
            CHECK(admit(value).reason == Reason::kInvalidQuaternion);
        }
    }
    value = packet(); value.enemies[0].quat[3] = 1.005F;
    CHECK(admit(value).accepted());
    value.enemies[0].quat[3] = 1.02F;
    CHECK(admit(value).reason == Reason::kInvalidQuaternion);
    value.enemies[0].quat[3] = -1;
    CHECK(admit(value).accepted());
    return true;
}

bool flags_health_and_remote_duplicates() {
    auto value = packet(); value.enemies[0].flags = 2;
    CHECK(admit(value).reason == Reason::kUnknownFlags);
    value.enemies[0].flags = coop::kEnemyFlagDead;
    CHECK(admit(value).accepted());
    for (std::int32_t hp : {-1, 0, 100, std::numeric_limits<std::int32_t>::max()}) {
        value.enemies[0].hp = hp;
        CHECK(admit(value).accepted());
    }
    value.enemies[0].hp = -2;
    CHECK(admit(value).reason == Reason::kInvalidHealth);
    value = packet(); value.enemies[1].name_hash = value.enemies[0].name_hash;
    CHECK(admit(value).reason == Reason::kDuplicateRemoteHash);
    value = packet(32); value.enemies[31].name_hash = value.enemies[0].name_hash;
    CHECK(admit(value).reason == Reason::kDuplicateRemoteHash);
    return true;
}

bool unique_matching_missing_actors_and_order() {
    auto candidates = local();
    const auto admitted = admit(packet());
    auto result = policy::match_snapshot_for_observation(admitted, candidates.data(), candidates.size(), true);
    CHECK(result.accepted() && result.matched == 2);
    CHECK(result.unmatched_remote == 0 && result.unmatched_local == 1);
    CHECK(result.bindings[0].remote_index == 0 && result.bindings[0].local_index == 1);
    CHECK(result.bindings[1].remote_index == 1 && result.bindings[1].local_index == 0);
    candidates[1].eligible = false;
    result = policy::match_snapshot_for_observation(admitted, candidates.data(), candidates.size(), true);
    CHECK(result.accepted() && result.matched == 1);
    CHECK(result.unmatched_remote == 1 && result.unmatched_local == 1);
    result = policy::match_snapshot_for_observation(admitted, nullptr, 0, true);
    CHECK(result.accepted() && result.matched == 0 && result.unmatched_remote == 2);
    return true;
}

bool matching_failures_never_leave_partial_bindings() {
    auto admitted = admit(packet());
    auto candidates = local();
    auto check = [&](Reason expected, std::size_t count, bool complete = true) {
        const auto result = policy::match_snapshot_for_observation(admitted, candidates.data(), count, complete);
        return result.reason == expected && result.matched == 0 &&
               result.bindings[0].identity.object == 0;
    };
    CHECK(check(Reason::kLocalOverflow, 33));
    CHECK(check(Reason::kIncompleteEnumeration, 3, false));
    CHECK(policy::match_snapshot_for_observation(admitted, nullptr, 1, true).reason == Reason::kNullCandidates);
    candidates[2].name_hash = candidates[0].name_hash;
    CHECK(check(Reason::kDuplicateLocalHash, 3));
    candidates[2].eligible = false;
    CHECK(check(Reason::kDuplicateLocalHash, 3));
    candidates = local(); candidates[2].record = candidates[0].record;
    CHECK(check(Reason::kDuplicateLocalIdentity, 3));
    candidates = local(); candidates[2].object = candidates[0].object;
    CHECK(check(Reason::kDuplicateLocalIdentity, 3));
    candidates = local(); candidates[2].record = 0;
    CHECK(check(Reason::kInvalidLocalIdentity, 3));
    candidates = local(); candidates[2].object = 0;
    CHECK(check(Reason::kInvalidLocalIdentity, 3));
    candidates = local(); admitted.packet.enemies[1].name_hash = admitted.packet.enemies[0].name_hash;
    CHECK(check(Reason::kDuplicateRemoteHash, 3));
    admitted = {};
    CHECK(check(Reason::kNotAdmitted, 3));
    return true;
}

bool local_generation_and_authorization_drift() {
    auto candidates = local();
    const auto result = policy::match_snapshot_for_observation(admit(packet()), candidates.data(), 3, true);
    const auto binding = result.bindings[0];
    const auto original = candidates[binding.local_index];
    CHECK(policy::binding_still_matches(binding, original, 71));
    CHECK(!policy::binding_still_matches(binding, original, 0));
    CHECK(!policy::binding_still_matches(binding, original, 72));
    auto fresh = original; ++fresh.id;
    CHECK(!policy::binding_still_matches(binding, fresh, 71));
    fresh = original; ++fresh.record;
    CHECK(!policy::binding_still_matches(binding, fresh, 71));
    fresh = original; ++fresh.object;
    CHECK(!policy::binding_still_matches(binding, fresh, 71));
    fresh = original; ++fresh.name_hash;
    CHECK(!policy::binding_still_matches(binding, fresh, 71));
    fresh = original; fresh.eligible = false;
    CHECK(!policy::binding_still_matches(binding, fresh, 71));
    CHECK(!policy::binding_still_matches({}, original, 71));
    return true;
}

bool local_token_does_not_prove_incoming_world() {
    // Legacy v1 contains no wire epoch. A reset cursor and fresh local token
    // cannot distinguish a delayed previous-world packet from a current one.
    // Acceptance is deliberately ONLY permission to observe/compare bytes.
    const auto old_world_packet = packet();
    auto fresh_scope = context();
    fresh_scope.local_observation_token = 99;
    const auto result = policy::admit_wire_for_observation(
        &old_world_packet, wire_size(old_world_packet), fresh_scope, {});
    CHECK(result.accepted());
    CHECK(result.next_cursor.local_observation_token == 99);
    return true;
}
} // namespace

int main() {
    constexpr std::array tests{
        valid_wire_and_clear_rejection,
        malformed_lengths_and_header,
        scope_clock_and_authorization,
        serial_order_rollover_and_peer_timestamp,
        finite_position_and_quaternion_policy,
        flags_health_and_remote_duplicates,
        unique_matching_missing_actors_and_order,
        matching_failures_never_leave_partial_bindings,
        local_generation_and_authorization_drift,
        local_token_does_not_prove_incoming_world,
    };
    unsigned failed = 0;
    for (auto test : tests) if (!test()) ++failed;
    std::cout << "enemy_snapshot_policy: " << tests.size() - failed << '/'
              << tests.size() << " cases passed\n";
    return failed == 0 ? 0 : 1;
}
