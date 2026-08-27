#include "nadine_back_frame_policy.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

namespace {

namespace policy = coop::nadine_back;
using policy::Decision;
using policy::Disposition;
using policy::Ledger;
using policy::Observation;
using policy::Phase;
using policy::Reason;

// Do not use assert: these checks must execute in the Release/NDEBUG build.
#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n";   \
            return false;                                                       \
        }                                                                       \
    } while (false)

Observation weapon(int slot = 0) {
    Observation result{};
    result.identity = {
        static_cast<std::uintptr_t>(0x1000 + slot * 0x100),
        71u,
        static_cast<std::uint32_t>(101 + slot),
        static_cast<std::uint32_t>(0x10000000 + slot),
    };
    result.raw_weapon = static_cast<std::uintptr_t>(0x10000 + slot * 0x1000);
    result.inventory_slot = slot;
    result.current_slot = -1;
    result.controller_state = 0;
    result.selected_back = true;
    result.controller_read = true;
    return result;
}

Observation draw(Observation observation, float progress) {
    observation.duration_bits = policy::kNativeDurationBits;
    observation.progress = progress;
    observation.to_back = false;
    observation.controller_state = 1;
    observation.current_slot = -1;
    return observation;
}

bool near(float left, float right) {
    return std::fabs(left - right) <= 0.000001F;
}

bool commit_draw(Ledger& ledger, const Observation& observation) {
    const Decision result = policy::prepare(ledger, observation);
    return result.token != 0 && policy::commit(ledger, observation, result.token);
}

bool cold_never_interprets_stale_progress() {
    const std::array<float, 6> stale{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -100.0F, 0.0F, 1.0F, 100.0F,
    };
    const std::array<std::uint32_t, 6> states{0, 1, 2, 4, 5, 0xFFFFFFFFu};
    for (const float progress : stale) {
        for (const std::uint32_t state : states) {
            Ledger ledger{};
            Observation observation = weapon();
            observation.progress = progress;
            observation.controller_state = state;
            const Decision result = policy::prepare(ledger, observation);
            CHECK(result.disposition == Disposition::kFull);
            CHECK(result.weight == 1.0F);
            CHECK(result.token == 0 && !result.terminal_hold);
            CHECK(ledger.slots[0].phase == Phase::kEmpty);
            CHECK(ledger.serial == 0);
        }
    }
    return true;
}

bool active_draw_uses_native_smoothstep() {
    constexpr std::array<std::pair<float, float>, 5> samples{{
        {0.0F, 1.0F}, {0.25F, 0.84375F}, {0.5F, 0.5F},
        {1.0F, 0.0F}, {1.01F, 0.0F},
    }};
    for (const auto& [progress, weight] : samples) {
        Ledger ledger{};
        Observation observation = draw(weapon(), progress);
        // Native active clock is authoritative even if the controller lags.
        observation.controller_state = 0;
        const Decision result = policy::prepare(ledger, observation);
        CHECK(result.disposition == Disposition::kWeighted);
        CHECK(result.reason == Reason::kActiveDraw);
        CHECK(near(result.weight, weight));
        CHECK(result.token != 0 && result.phase == Phase::kPendingDraw);
        CHECK(policy::commit(ledger, observation, result.token));
        CHECK(ledger.slots[0].phase == Phase::kActiveDrawSeen);
        CHECK(near(ledger.slots[0].last_active_progress,
                   progress > 1.0F ? 1.0F : progress));
    }
    return true;
}

bool active_put_uses_native_smoothstep_and_clears_draw() {
    constexpr std::array<std::pair<float, float>, 4> samples{{
        {0.0F, 0.0F}, {0.5F, 0.5F}, {1.0F, 1.0F}, {1.01F, 1.0F},
    }};
    for (const auto& [progress, weight] : samples) {
        Ledger ledger{};
        CHECK(commit_draw(ledger, draw(weapon(), 0.8F)));
        Observation observation = draw(weapon(), progress);
        observation.to_back = true;
        observation.controller_state = 5;
        const Decision result = policy::prepare(ledger, observation);
        CHECK(result.disposition == Disposition::kWeighted);
        CHECK(result.reason == Reason::kActivePut);
        CHECK(near(result.weight, weight));
        CHECK(result.token == 0);
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
    }
    return true;
}

bool terminal_hold_repeats_without_progress_or_controller() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.8F);
    CHECK(commit_draw(ledger, observation));
    observation.duration_bits = 0;
    observation.progress = 1.0F;
    Decision result = policy::prepare(ledger, observation);
    CHECK(result.terminal_hold && result.weight == 0.0F);
    CHECK(result.token == 0 && result.terminal_ordinal == 1);
    CHECK(result.phase == Phase::kTerminalHold);
    observation.progress = std::numeric_limits<float>::quiet_NaN();
    observation.controller_read = false;
    for (std::uint32_t ordinal = 2; ordinal <= 12; ++ordinal) {
        result = policy::prepare(ledger, observation);
        CHECK(result.terminal_hold && result.weight == 0.0F);
        CHECK(result.terminal_ordinal == ordinal);
        CHECK(!result.raw_relocated);
    }
    return true;
}

bool terminal_relocation_uses_logical_identity() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.2F);
    CHECK(commit_draw(ledger, observation));
    observation.duration_bits = 0;
    observation.raw_weapon += 0x4000;
    observation.progress = std::numeric_limits<float>::quiet_NaN();
    Decision result = policy::prepare(ledger, observation);
    CHECK(result.terminal_hold && result.raw_relocated);
    CHECK(result.weight == 0.0F && result.terminal_ordinal == 1);
    observation.raw_weapon += 0x4000;
    observation.controller_read = false;
    result = policy::prepare(ledger, observation);
    CHECK(result.terminal_hold && result.raw_relocated);
    CHECK(result.terminal_ordinal == 2);
    CHECK(ledger.slots[0].last_raw_weapon == observation.raw_weapon);
    return true;
}

bool no_active_draw_cannot_authorize_terminal() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 1.0F);
    observation.duration_bits = 0;
    observation.raw_weapon += 0x4000;
    const Decision result = policy::prepare(ledger, observation);
    CHECK(result.disposition == Disposition::kFull && result.weight == 1.0F);
    CHECK(!result.terminal_hold && result.token == 0);
    CHECK(ledger.slots[0].phase == Phase::kEmpty);
    return true;
}

bool pending_draw_is_not_a_committed_ticket() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.7F);
    const Decision pending = policy::prepare(ledger, observation);
    CHECK(pending.token != 0);
    Observation terminal = observation;
    terminal.duration_bits = 0;
    terminal.progress = 1.0F;
    const Decision result = policy::prepare(ledger, terminal);
    CHECK(result.disposition == Disposition::kFull);
    CHECK(result.reason == Reason::kUncommittedDraw);
    CHECK(!result.terminal_hold);
    CHECK(!policy::commit(ledger, observation, pending.token));
    CHECK(ledger.slots[0].phase == Phase::kEmpty);
    return true;
}

bool terminal_entry_requires_exact_positive_evidence() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.7F);
    CHECK(commit_draw(ledger, observation));
    observation.duration_bits = 0;
    observation.progress = 1.0F;
    observation.controller_read = false;
    CHECK(!policy::prepare(ledger, observation).terminal_hold);
    observation.controller_read = true;
    observation.current_slot = 0;
    CHECK(!policy::prepare(ledger, observation).terminal_hold);
    observation.current_slot = -1;
    observation.controller_state = 5;
    CHECK(!policy::prepare(ledger, observation).terminal_hold);
    observation.controller_state = 1;
    observation.progress = 0.998F;
    CHECK(!policy::prepare(ledger, observation).terminal_hold);
    observation.progress = std::numeric_limits<float>::quiet_NaN();
    CHECK(!policy::prepare(ledger, observation).terminal_hold);
    CHECK(ledger.slots[0].phase == Phase::kActiveDrawSeen);
    observation.progress = 0.999F;
    CHECK(policy::prepare(ledger, observation).terminal_hold);
    return true;
}

bool selected_exit_clears_every_phase() {
    for (int phase = 0; phase < 3; ++phase) {
        Ledger ledger{};
        Observation observation = draw(weapon(), 0.9F);
        const Decision pending = policy::prepare(ledger, observation);
        if (phase >= 1) {
            CHECK(policy::commit(ledger, observation, pending.token));
        }
        if (phase == 2) {
            observation.duration_bits = 0;
            observation.progress = 1.0F;
            CHECK(policy::prepare(ledger, observation).terminal_hold);
        }
        observation.selected_back = false;
        CHECK(policy::reconcile(ledger, observation));
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
        CHECK(policy::prepare(ledger, observation).disposition ==
              Disposition::kStock);
        CHECK(!policy::commit(ledger, observation, pending.token));
    }
    return true;
}

bool positive_stable_back_cancels_but_own_drawn_state_does_not() {
    for (const std::uint32_t state : std::array<std::uint32_t, 3>{0, 2, 4}) {
        Ledger ledger{};
        Observation observation = draw(weapon(), 0.9F);
        CHECK(commit_draw(ledger, observation));
        observation.duration_bits = 0;
        observation.progress = 1.0F;
        CHECK(policy::prepare(ledger, observation).terminal_hold);
        observation.controller_state = state;
        observation.current_slot = state == 0 ? -1 : 1;
        observation.progress = std::numeric_limits<float>::quiet_NaN();
        const Decision result = policy::prepare(ledger, observation);
        CHECK(result.reason == Reason::kStableBackCancel);
        CHECK(result.weight == 1.0F && !result.terminal_hold);
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
    }
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.9F);
    CHECK(commit_draw(ledger, observation));
    observation.duration_bits = 0;
    observation.progress = 1.0F;
    CHECK(policy::prepare(ledger, observation).terminal_hold);
    observation.controller_state = 2;
    observation.current_slot = 0;
    CHECK(policy::prepare(ledger, observation).terminal_hold);
    observation.current_slot = -1; // Not positive evidence of another weapon.
    CHECK(policy::prepare(ledger, observation).terminal_hold);
    return true;
}

bool another_weapon_transition_does_not_clear_this_ticket() {
    Ledger ledger{};
    Observation first = draw(weapon(0), 0.7F);
    Observation second = draw(weapon(1), 0.6F);
    CHECK(commit_draw(ledger, first));
    CHECK(commit_draw(ledger, second));
    second.to_back = true;
    CHECK(policy::prepare(ledger, second).reason == Reason::kActivePut);
    CHECK(ledger.slots[1].phase == Phase::kEmpty);
    CHECK(ledger.slots[0].phase == Phase::kActiveDrawSeen);
    first.duration_bits = 0;
    first.progress = 1.0F;
    first.controller_state = 5;
    first.current_slot = 1;
    CHECK(!policy::prepare(ledger, first).terminal_hold);
    CHECK(ledger.slots[0].phase == Phase::kActiveDrawSeen);
    first.controller_state = 1;
    first.current_slot = -1;
    CHECK(policy::prepare(ledger, first).terminal_hold);
    first.controller_state = 5;
    first.current_slot = 1;
    CHECK(policy::prepare(ledger, first).terminal_hold);
    second.selected_back = false;
    policy::reconcile(ledger, second);
    CHECK(ledger.slots[0].phase == Phase::kTerminalHold);
    return true;
}

bool completed_put_clears_without_reading_stale_progress() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.8F);
    CHECK(commit_draw(ledger, observation));
    observation.duration_bits = 0;
    observation.to_back = true;
    observation.progress = std::numeric_limits<float>::quiet_NaN();
    const Decision result = policy::prepare(ledger, observation);
    CHECK(result.disposition == Disposition::kFull && result.weight == 1.0F);
    CHECK(ledger.slots[0].phase == Phase::kEmpty);
    return true;
}

bool malformed_active_clock_fails_stock_and_clears() {
    const std::array<float, 5> invalid_progress{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -0.0001F, 1.0101F, 100.0F,
    };
    for (const float progress : invalid_progress) {
        Ledger ledger{};
        Observation observation = draw(weapon(), 0.5F);
        CHECK(commit_draw(ledger, observation));
        observation.progress = progress;
        const Decision result = policy::prepare(ledger, observation);
        CHECK(result.disposition == Disposition::kStock);
        CHECK(result.reason == Reason::kMalformedProgress);
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
    }
    constexpr std::array<std::uint32_t, 4> invalid_duration{
        0x3E4CCCCDu, 0x80000000u, 0x7FC00000u, 1u,
    };
    for (const std::uint32_t bits : invalid_duration) {
        Ledger ledger{};
        Observation observation = draw(weapon(), 0.5F);
        CHECK(commit_draw(ledger, observation));
        observation.duration_bits = bits;
        const Decision result = policy::prepare(ledger, observation);
        CHECK(result.disposition == Disposition::kStock);
        CHECK(result.reason == Reason::kMalformedDuration);
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
    }
    return true;
}

bool identity_replacement_does_not_inherit_draw_authority() {
    for (int field = 0; field < 4; ++field) {
        Ledger ledger{};
        Observation observation = draw(weapon(), 0.5F);
        CHECK(commit_draw(ledger, observation));
        switch (field) {
        case 0: ++observation.identity.fg; break;
        case 1: ++observation.identity.owner_generation; break;
        case 2: ++observation.identity.weapon_generation; break;
        case 3: ++observation.identity.art; break;
        }
        observation.duration_bits = 0;
        observation.progress = 1.0F;
        const Decision result = policy::prepare(ledger, observation);
        CHECK(result.disposition == Disposition::kFull);
        CHECK(!result.terminal_hold);
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
    }
    return true;
}

bool inventory_slot_migration_invalidates_old_slot() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.6F);
    CHECK(commit_draw(ledger, observation));
    observation.inventory_slot = 22;
    observation.duration_bits = 0;
    observation.progress = 1.0F;
    CHECK(!policy::prepare(ledger, observation).terminal_hold);
    CHECK(ledger.slots[0].phase == Phase::kEmpty);
    CHECK(ledger.slots[22].phase == Phase::kEmpty);
    observation.duration_bits = policy::kNativeDurationBits;
    CHECK(commit_draw(ledger, observation));
    observation.inventory_slot = 23;
    CHECK(policy::prepare(ledger, observation).disposition == Disposition::kStock);
    CHECK(ledger.slots[22].phase == Phase::kEmpty);
    return true;
}

bool invalid_scope_clears_only_the_observed_slot() {
    for (int field = 0; field < 5; ++field) {
        Ledger ledger{};
        Observation observation = draw(weapon(0), 0.5F);
        CHECK(commit_draw(ledger, observation));
        CHECK(commit_draw(ledger, draw(weapon(1), 0.5F)));
        switch (field) {
        case 0: observation.raw_weapon = 0; break;
        case 1: observation.identity.fg = 0; break;
        case 2: observation.identity.owner_generation = 0; break;
        case 3: observation.identity.weapon_generation = 0; break;
        case 4: observation.identity.art = 0; break;
        }
        CHECK(policy::prepare(ledger, observation).disposition ==
              Disposition::kStock);
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
        CHECK(ledger.slots[1].phase == Phase::kActiveDrawSeen);
    }
    return true;
}

bool stale_commit_and_cancel_cannot_affect_newer_reservation() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.3F);
    const Decision first = policy::prepare(ledger, observation);
    observation.progress = 0.4F;
    const Decision second = policy::prepare(ledger, observation);
    CHECK(first.token != 0 && second.token > first.token);
    CHECK(!policy::commit(ledger, observation, first.token));
    policy::cancel(ledger, 0, first.token);
    CHECK(ledger.slots[0].phase == Phase::kPendingDraw);
    CHECK(ledger.slots[0].serial == second.token);
    CHECK(policy::commit(ledger, observation, second.token));
    policy::cancel(ledger, 0, second.token);
    CHECK(ledger.slots[0].phase == Phase::kActiveDrawSeen);
    CHECK(!policy::commit(ledger, observation, second.token));
    return true;
}

bool put_or_exit_supersedes_pending_commit() {
    for (const bool put : {false, true}) {
        Ledger ledger{};
        const Observation observation = draw(weapon(), 0.3F);
        const Decision pending = policy::prepare(ledger, observation);
        Observation superseding = observation;
        if (put) {
            superseding.to_back = true;
            policy::prepare(ledger, superseding);
        } else {
            superseding.selected_back = false;
            policy::reconcile(ledger, superseding);
        }
        CHECK(!policy::commit(ledger, observation, pending.token));
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
    }
    return true;
}

bool failed_stock_postconditions_cancel_only_the_matching_token() {
    for (int field = 0; field < 8; ++field) {
        Ledger ledger{};
        Observation after = draw(weapon(), 0.3F);
        const Decision pending = policy::prepare(ledger, after);
        CHECK(commit_draw(ledger, draw(weapon(1), 0.6F)));
        switch (field) {
        case 0: after.duration_bits = 0; break;
        case 1: after.to_back = true; break;
        case 2: after.selected_back = false; break;
        case 3: ++after.identity.art; break;
        case 4: after.inventory_slot = 2; break;
        case 5: after.raw_weapon = 0; break;
        case 6: after.progress = std::numeric_limits<float>::quiet_NaN(); break;
        case 7: after.inventory_slot = -1; break;
        }
        CHECK(!policy::commit(ledger, after, pending.token));
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
        CHECK(ledger.slots[1].phase == Phase::kActiveDrawSeen);
    }
    return true;
}

bool explicit_cancel_only_removes_its_pending_slot() {
    Ledger ledger{};
    const Observation observation = draw(weapon(), 0.3F);
    const Decision pending = policy::prepare(ledger, observation);
    CHECK(commit_draw(ledger, draw(weapon(1), 0.6F)));
    policy::cancel(ledger, 1, pending.token);
    CHECK(ledger.slots[0].phase == Phase::kPendingDraw);
    policy::cancel(ledger, 0, pending.token);
    CHECK(ledger.slots[0].phase == Phase::kEmpty);
    CHECK(ledger.slots[1].phase == Phase::kActiveDrawSeen);
    CHECK(!policy::commit(ledger, observation, pending.token));
    return true;
}

bool raw_relocation_during_stock_is_not_identity_replacement() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.3F);
    const Decision pending = policy::prepare(ledger, observation);
    observation.raw_weapon += 0x5000;
    CHECK(policy::commit(ledger, observation, pending.token));
    CHECK(ledger.slots[0].last_raw_weapon == observation.raw_weapon);
    CHECK(ledger.slots[0].phase == Phase::kActiveDrawSeen);
    return true;
}

bool token_exhaustion_never_reuses_stale_authority() {
    Ledger ledger{};
    ledger.serial = std::numeric_limits<std::uint64_t>::max() - 1;
    const Observation observation = draw(weapon(), 0.3F);
    const Decision last = policy::prepare(ledger, observation);
    CHECK(last.token == std::numeric_limits<std::uint64_t>::max());
    CHECK(policy::commit(ledger, observation, last.token));
    const Decision exhausted = policy::prepare(ledger, observation);
    CHECK(exhausted.disposition == Disposition::kStock);
    CHECK(exhausted.reason == Reason::kTokenExhausted);
    CHECK(exhausted.token == 0);
    CHECK(!policy::commit(ledger, observation, last.token));
    CHECK(ledger.slots[0].phase == Phase::kEmpty);
    return true;
}

bool terminal_counter_saturates_without_dropping_hold() {
    Ledger ledger{};
    Observation observation = draw(weapon(), 0.8F);
    CHECK(commit_draw(ledger, observation));
    observation.duration_bits = 0;
    observation.progress = 1.0F;
    CHECK(policy::prepare(ledger, observation).terminal_hold);
    ledger.slots[0].terminal_callbacks =
        std::numeric_limits<std::uint32_t>::max();
    const Decision result = policy::prepare(ledger, observation);
    CHECK(result.terminal_hold && result.weight == 0.0F);
    CHECK(result.terminal_ordinal == std::numeric_limits<std::uint32_t>::max());
    return true;
}

bool negative_evidence_revokes_active_and_terminal_only_for_this_weapon() {
    for (const bool terminal : {false, true}) {
        Ledger ledger{};
        Observation observation = draw(weapon(), 0.8F);
        CHECK(commit_draw(ledger, observation));
        CHECK(commit_draw(ledger, draw(weapon(1), 0.4F)));
        if (terminal) {
            observation.duration_bits = 0;
            observation.progress = 1.0F;
            CHECK(policy::prepare(ledger, observation).terminal_hold);
        }
        // A relocated raw pointer does not hide a complete identity match.
        observation.raw_weapon += 0x5000;
        CHECK(policy::invalidate(ledger, observation));
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
        CHECK(ledger.slots[1].phase == Phase::kActiveDrawSeen);
        observation.duration_bits = 0;
        observation.progress = 1.0F;
        CHECK(!policy::prepare(ledger, observation).terminal_hold);
        CHECK(!policy::invalidate(ledger, observation));
    }
    return true;
}

bool incomplete_failure_capture_invalidates_by_raw_only() {
    for (const bool terminal : {false, true}) {
        Ledger ledger{};
        Observation observation = draw(weapon(), 0.8F);
        CHECK(commit_draw(ledger, observation));
        CHECK(commit_draw(ledger, draw(weapon(1), 0.4F)));
        if (terminal) {
            observation.duration_bits = 0;
            observation.progress = 1.0F;
            CHECK(policy::prepare(ledger, observation).terminal_hold);
        }
        Observation failed{};
        failed.raw_weapon = observation.raw_weapon;
        // Even a misleading/unverified slot cannot revoke the other weapon.
        failed.inventory_slot = 1;
        CHECK(policy::invalidate(ledger, failed));
        CHECK(ledger.slots[0].phase == Phase::kEmpty);
        CHECK(ledger.slots[1].phase == Phase::kActiveDrawSeen);
    }
    return true;
}

bool incomplete_unmatched_evidence_cannot_clear_by_slot_or_partial_identity() {
    Ledger ledger{};
    const Observation observation = draw(weapon(), 0.8F);
    CHECK(commit_draw(ledger, observation));
    CHECK(commit_draw(ledger, draw(weapon(1), 0.4F)));
    Observation failed{};
    failed.inventory_slot = 0;
    failed.identity.fg = observation.identity.fg;
    CHECK(!policy::invalidate(ledger, failed));
    failed.raw_weapon = 0xFFFFFFFFu;
    CHECK(!policy::invalidate(ledger, failed));
    failed.identity = observation.identity;
    ++failed.identity.art;
    CHECK(!policy::invalidate(ledger, failed));
    CHECK(ledger.slots[0].phase == Phase::kActiveDrawSeen);
    CHECK(ledger.slots[1].phase == Phase::kActiveDrawSeen);
    // The same raw pointer still revokes if the captured identity has changed.
    failed.raw_weapon = observation.raw_weapon;
    CHECK(policy::invalidate(ledger, failed));
    CHECK(ledger.slots[0].phase == Phase::kEmpty);
    CHECK(ledger.slots[1].phase == Phase::kActiveDrawSeen);
    return true;
}

bool invalidation_prevents_stale_commit_without_touching_new_tokens() {
    for (const bool complete_identity : {false, true}) {
        Ledger ledger{};
        const Observation observation = draw(weapon(), 0.4F);
        const Decision pending = policy::prepare(ledger, observation);
        CHECK(commit_draw(ledger, draw(weapon(1), 0.4F)));
        Observation failed{};
        if (complete_identity) {
            failed.identity = observation.identity;
        } else {
            failed.raw_weapon = observation.raw_weapon;
        }
        CHECK(policy::invalidate(ledger, failed));
        CHECK(!policy::commit(ledger, observation, pending.token));
        const Decision next = policy::prepare(ledger, observation);
        CHECK(next.token > pending.token);
        CHECK(!policy::commit(ledger, observation, pending.token));
        policy::cancel(ledger, 0, pending.token);
        CHECK(policy::commit(ledger, observation, next.token));
        CHECK(ledger.slots[0].phase == Phase::kActiveDrawSeen);
        CHECK(ledger.slots[1].phase == Phase::kActiveDrawSeen);
    }
    return true;
}

Observation secondary_back(Observation observation, float progress) {
    observation.duration_bits = policy::kSecondaryBackDurationBits;
    observation.progress = progress;
    observation.secondary_back_blend_admitted = true;
    return observation;
}

bool admitted_secondary_blend_stays_full_for_the_whole_clock() {
    for (const int slot : {1, 17}) {
        for (const float progress : {0.0F, 0.25F, 0.5F, 1.0F, 1.01F}) {
            for (const bool stale_to_back : {false, true}) {
                for (const std::uint32_t state : {0u, 1u, 2u, 4u, 5u}) {
                    Ledger ledger{};
                    Observation observation = secondary_back(weapon(slot), progress);
                    observation.to_back = stale_to_back;
                    observation.controller_state = state;
                    observation.current_slot = slot;
                    observation.controller_read = false;
                    const Decision result = policy::prepare(ledger, observation);
                    CHECK(result.disposition == Disposition::kFull);
                    CHECK(result.reason == Reason::kSecondaryBackBlend);
                    CHECK(result.weight == 1.0F && result.token == 0);
                    CHECK(!result.terminal_hold && result.phase == Phase::kEmpty);
                    CHECK(ledger.serial == 0);
                    CHECK(ledger.slots[slot].phase == Phase::kEmpty);
                }
            }
        }
    }
    return true;
}

bool secondary_blend_clears_each_draw_phase_without_reserving() {
    for (const Phase phase : {
            Phase::kPendingDraw, Phase::kActiveDrawSeen, Phase::kTerminalHold}) {
        Ledger ledger{};
        const Observation active = draw(weapon(1), 0.6F);
        const Decision pending = policy::prepare(ledger, active);
        CHECK(pending.token != 0);
        if (phase != Phase::kPendingDraw) {
            CHECK(policy::commit(ledger, active, pending.token));
        }
        if (phase == Phase::kTerminalHold) {
            Observation terminal = active;
            terminal.duration_bits = 0;
            terminal.progress = 1.0F;
            CHECK(policy::prepare(ledger, terminal).terminal_hold);
        }
        CHECK(ledger.slots[1].phase == phase);
        CHECK(commit_draw(ledger, draw(weapon(17), 0.4F)));
        const std::uint64_t serial_before = ledger.serial;
        const Decision result =
            policy::prepare(ledger, secondary_back(active, 0.5F));
        CHECK(result.disposition == Disposition::kFull && result.weight == 1.0F);
        CHECK(result.reason == Reason::kSecondaryBackBlend && result.token == 0);
        CHECK(ledger.serial == serial_before);
        CHECK(ledger.slots[1].phase == Phase::kEmpty);
        CHECK(ledger.slots[17].phase == Phase::kActiveDrawSeen);
        CHECK(!policy::commit(ledger, active, pending.token));
    }
    return true;
}

bool secondary_terminal_remains_full_without_draw_authority() {
    Ledger ledger{};
    Observation observation = draw(weapon(1), 0.7F);
    CHECK(commit_draw(ledger, observation));
    observation = secondary_back(observation, 0.5F);
    CHECK(policy::prepare(ledger, observation).weight == 1.0F);
    observation.duration_bits = 0;
    observation.secondary_back_blend_admitted = false;
    observation.raw_weapon += 0x4000;
    for (const float stale_progress : {
            1.0F, 0.0F, std::numeric_limits<float>::quiet_NaN()}) {
        observation.progress = stale_progress;
        const Decision result = policy::prepare(ledger, observation);
        CHECK(result.disposition == Disposition::kFull && result.weight == 1.0F);
        CHECK(result.reason == Reason::kColdBack);
        CHECK(!result.terminal_hold && result.token == 0);
        CHECK(ledger.slots[1].phase == Phase::kEmpty);
    }
    return true;
}

bool secondary_blend_cannot_commit_a_draw_ticket() {
    Ledger ledger{};
    const Observation secondary = secondary_back(weapon(1), 0.5F);
    const Decision result = policy::prepare(ledger, secondary);
    CHECK(result.token == 0 && ledger.serial == 0);
    CHECK(!policy::commit(ledger, secondary, result.token));
    CHECK(!policy::commit(ledger, secondary, 123u));
    const Observation active = draw(weapon(1), 0.5F);
    const Decision pending = policy::prepare(ledger, active);
    CHECK(pending.token != 0);
    CHECK(!policy::commit(ledger, secondary, pending.token));
    CHECK(ledger.slots[1].phase == Phase::kEmpty);

    // A stale admission boolean cannot reinterpret the separate 0.1 Draw clock.
    Observation next_draw = active;
    next_draw.secondary_back_blend_admitted = true;
    const Decision next = policy::prepare(ledger, next_draw);
    CHECK(next.disposition == Disposition::kWeighted);
    CHECK(next.reason == Reason::kActiveDraw && near(next.weight, 0.5F));
    CHECK(next.token > pending.token);
    CHECK(policy::commit(ledger, next_draw, next.token));
    return true;
}

bool admitted_secondary_invalid_progress_fails_stock_and_clears() {
    for (const float progress : {
            -0.01F, 1.0101F, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()}) {
        Ledger ledger{};
        CHECK(commit_draw(ledger, draw(weapon(1), 0.5F)));
        CHECK(commit_draw(ledger, draw(weapon(17), 0.4F)));
        const Decision result =
            policy::prepare(ledger, secondary_back(weapon(1), progress));
        CHECK(result.disposition == Disposition::kStock);
        CHECK(result.reason == Reason::kMalformedProgress && result.token == 0);
        CHECK(ledger.slots[1].phase == Phase::kEmpty);
        CHECK(ledger.slots[17].phase == Phase::kActiveDrawSeen);
    }
    return true;
}

bool unadmitted_or_nonback_secondary_clock_grants_no_authority() {
    for (const float progress : {0.0F, 0.5F, 1.0F}) {
        Ledger ledger{};
        CHECK(commit_draw(ledger, draw(weapon(1), 0.4F)));
        Observation observation = secondary_back(weapon(1), progress);
        observation.secondary_back_blend_admitted = false;
        const Decision result = policy::prepare(ledger, observation);
        CHECK(result.disposition == Disposition::kStock);
        CHECK(result.reason == Reason::kMalformedDuration);
        CHECK(ledger.slots[1].phase == Phase::kEmpty && result.token == 0);
    }
    Ledger ledger{};
    CHECK(commit_draw(ledger, draw(weapon(1), 0.4F)));
    Observation observation = secondary_back(weapon(1), 0.5F);
    observation.selected_back = false;
    Decision result = policy::prepare(ledger, observation);
    CHECK(result.disposition == Disposition::kStock);
    CHECK(result.reason == Reason::kSelectedOther);
    CHECK(ledger.slots[1].phase == Phase::kEmpty && result.token == 0);
    observation.selected_back = true;
    observation.duration_bits = policy::kSecondaryBackDurationBits + 1u;
    result = policy::prepare(ledger, observation);
    CHECK(result.disposition == Disposition::kStock);
    CHECK(result.reason == Reason::kMalformedDuration);
    observation.duration_bits = policy::kSecondaryBackDurationBits;
    observation.inventory_slot = -1;
    result = policy::prepare(ledger, observation);
    CHECK(result.disposition == Disposition::kStock);
    CHECK(result.reason == Reason::kInvalidScope && result.token == 0);
    return true;
}

} // namespace

int main() {
    constexpr std::array tests{
        cold_never_interprets_stale_progress,
        active_draw_uses_native_smoothstep,
        active_put_uses_native_smoothstep_and_clears_draw,
        terminal_hold_repeats_without_progress_or_controller,
        terminal_relocation_uses_logical_identity,
        no_active_draw_cannot_authorize_terminal,
        pending_draw_is_not_a_committed_ticket,
        terminal_entry_requires_exact_positive_evidence,
        selected_exit_clears_every_phase,
        positive_stable_back_cancels_but_own_drawn_state_does_not,
        another_weapon_transition_does_not_clear_this_ticket,
        completed_put_clears_without_reading_stale_progress,
        malformed_active_clock_fails_stock_and_clears,
        identity_replacement_does_not_inherit_draw_authority,
        inventory_slot_migration_invalidates_old_slot,
        invalid_scope_clears_only_the_observed_slot,
        stale_commit_and_cancel_cannot_affect_newer_reservation,
        put_or_exit_supersedes_pending_commit,
        failed_stock_postconditions_cancel_only_the_matching_token,
        explicit_cancel_only_removes_its_pending_slot,
        raw_relocation_during_stock_is_not_identity_replacement,
        token_exhaustion_never_reuses_stale_authority,
        terminal_counter_saturates_without_dropping_hold,
        negative_evidence_revokes_active_and_terminal_only_for_this_weapon,
        incomplete_failure_capture_invalidates_by_raw_only,
        incomplete_unmatched_evidence_cannot_clear_by_slot_or_partial_identity,
        invalidation_prevents_stale_commit_without_touching_new_tokens,
        admitted_secondary_blend_stays_full_for_the_whole_clock,
        secondary_blend_clears_each_draw_phase_without_reserving,
        secondary_terminal_remains_full_without_draw_authority,
        secondary_blend_cannot_commit_a_draw_ticket,
        admitted_secondary_invalid_progress_fails_stock_and_clears,
        unadmitted_or_nonback_secondary_clock_grants_no_authority,
    };
    unsigned failures = 0;
    for (const auto test : tests) {
        if (!test()) {
            ++failures;
        }
    }
    std::cout << "nadine_back_frame_policy: "
              << tests.size() - failures << '/' << tests.size()
              << " cases passed\n";
    return failures == 0 ? 0 : 1;
}
