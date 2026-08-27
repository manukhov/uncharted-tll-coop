#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// Pure item-presentation policy. The caller admits the live weapon, owner,
// inventory record and authored frame before prepare(), and serializes EVERY
// access to a Ledger externally. No Windows, clocks, poses or game-memory writes
// belong here. The separately proven Mazur ledger is deliberately not shared.
namespace coop::nadine_back {

inline constexpr std::size_t kInventorySlots = 23;
inline constexpr std::uint32_t kNativeDurationBits = 0x3DCCCCCDu;
inline constexpr std::uint32_t kSecondaryBackDurationBits = 0x3E4CCCCDu;

struct Identity {
    std::uintptr_t fg = 0;
    std::uint32_t owner_generation = 0;
    std::uint32_t weapon_generation = 0;
    std::uint32_t art = 0;

    friend constexpr bool operator==(const Identity&, const Identity&) = default;
};

struct Observation {
    Identity identity{};
    std::uintptr_t raw_weapon = 0;
    int inventory_slot = -1;
    int current_slot = -1;
    std::uint32_t controller_state = 0xFFFFFFFFu;
    std::uint32_t duration_bits = 0;
    float progress = 0.0F;
    bool to_back = false;
    bool selected_back = false;
    bool controller_read = false;
    // Runtime proved the subject's exact secondary endpoint and owned
    // dual-slot back-binding path. Duration 0.2 alone grants no authority.
    bool secondary_back_blend_admitted = false;
};

enum class Phase : std::uint8_t {
    kEmpty,
    kPendingDraw,
    kActiveDrawSeen,
    kTerminalHold,
};

struct SlotState {
    Identity identity{};
    std::uintptr_t last_raw_weapon = 0;
    std::uint64_t serial = 0;
    Phase phase = Phase::kEmpty;
    float last_active_progress = 0.0F;
    std::uint32_t terminal_callbacks = 0;
};

struct Ledger {
    std::array<SlotState, kInventorySlots> slots{};
    // Never reuse a token, including on integer overflow. Exhaustion fails to
    // stock; an arbitrarily delayed commit can therefore never gain authority.
    std::uint64_t serial = 0;
};

enum class Disposition : std::uint8_t {
    kStock,
    kFull,
    kWeighted,
};

enum class Reason : std::uint8_t {
    kInvalidScope,
    kSelectedOther,
    kColdBack,
    kStableBackCancel,
    kActiveDraw,
    kActivePut,
    kTerminalDraw,
    kAwaitingTerminal,
    kUncommittedDraw,
    kMalformedDuration,
    kMalformedProgress,
    kTokenExhausted,
    kSecondaryBackBlend,
};

struct Decision {
    Disposition disposition = Disposition::kStock;
    Reason reason = Reason::kInvalidScope;
    float weight = 0.0F;
    // Nonzero ONLY for a newly reserved active Draw. The caller commits this
    // token only after stock succeeded and a fresh live observation was read.
    std::uint64_t token = 0;
    Phase phase = Phase::kEmpty;
    bool terminal_hold = false;
    bool raw_relocated = false;
    std::uint32_t terminal_ordinal = 0;
};

inline constexpr bool valid_slot(int slot) noexcept {
    return slot >= 0 && static_cast<std::size_t>(slot) < kInventorySlots;
}

inline constexpr bool valid_identity(const Identity& identity) noexcept {
    return identity.fg != 0 && identity.owner_generation != 0 &&
        identity.weapon_generation != 0 && identity.art != 0;
}

inline constexpr bool valid_scope(const Observation& observation) noexcept {
    return valid_slot(observation.inventory_slot) &&
        valid_identity(observation.identity) && observation.raw_weapon != 0;
}

inline bool valid_progress(float progress) noexcept {
    return std::isfinite(progress) && progress >= 0.0F && progress <= 1.01F;
}

inline float clamped_progress(float progress) noexcept {
    return progress > 1.0F ? 1.0F : progress;
}

inline float smoothstep(float progress) noexcept {
    const float p = clamped_progress(progress);
    return p * p * (3.0F - 2.0F * p);
}

inline bool positive_stable_back(const Observation& observation) noexcept {
    if (!observation.controller_read) {
        return false;
    }
    if (observation.controller_state == 0 && observation.current_slot < 0) {
        return true;
    }
    return (observation.controller_state == 2 ||
            observation.controller_state == 4) &&
        valid_slot(observation.current_slot) &&
        observation.current_slot != observation.inventory_slot;
}

// Also call on an admitted identity whose selected endpoint is no longer AD,
// before the authored-frame fast path returns. An observed replacement affects
// only its inventory slot, or the same retained Fg moving between slots; another
// weapon's controller transition must not clear this weapon's Draw authority.
inline bool reconcile(Ledger& ledger, const Observation& observation) noexcept {
    bool cleared = false;
    for (std::size_t index = 0; index < ledger.slots.size(); ++index) {
        SlotState& state = ledger.slots[index];
        if (state.phase == Phase::kEmpty) {
            continue;
        }
        const bool observed_slot = valid_slot(observation.inventory_slot) &&
            index == static_cast<std::size_t>(observation.inventory_slot);
        const bool fg_moved_slot = !observed_slot &&
            observation.identity.fg != 0 &&
            state.identity.fg == observation.identity.fg;
        if (fg_moved_slot ||
            (observed_slot &&
             (!valid_scope(observation) || !observation.selected_back ||
              !(state.identity == observation.identity)))) {
            state = {};
            cleared = true;
        }
    }
    return cleared;
}

// Negative evidence from failed runtime admission, transform resolution or stock
// execution. A partial capture grants no slot/phase authority: invalidate only
// the complete matching logical identity or the exact nonzero raw object last
// observed by that entry. Raw equality is sufficient to revoke, never to grant.
// Verified slot replacements still use reconcile(); unrelated weapons survive.
inline bool invalidate(Ledger& ledger,
                       const Observation& observation) noexcept {
    bool cleared = false;
    for (SlotState& state : ledger.slots) {
        if (state.phase == Phase::kEmpty) {
            continue;
        }
        const bool logical_match = valid_identity(observation.identity) &&
            state.identity == observation.identity;
        const bool raw_match = observation.raw_weapon != 0 &&
            state.last_raw_weapon == observation.raw_weapon;
        if (logical_match || raw_match) {
            state = {};
            cleared = true;
        }
    }
    return cleared;
}

inline Decision prepare(Ledger& ledger, const Observation& observation) noexcept {
    reconcile(ledger, observation);
    if (!valid_scope(observation)) {
        return {};
    }
    if (!observation.selected_back) {
        Decision result{};
        result.reason = Reason::kSelectedOther;
        return result;
    }

    SlotState& state =
        ledger.slots[static_cast<std::size_t>(observation.inventory_slot)];
    Decision result{};
    if (observation.duration_bits == kNativeDurationBits) {
        // Active native clock outranks leading/lagging controller state.
        if (!valid_progress(observation.progress)) {
            state = {};
            result.reason = Reason::kMalformedProgress;
            return result;
        }
        result.disposition = Disposition::kWeighted;
        const float weight = smoothstep(observation.progress);
        if (observation.to_back) {
            state = {};
            result.weight = weight;
            result.reason = Reason::kActivePut;
            return result;
        }

        state = {};
        if (ledger.serial == std::numeric_limits<std::uint64_t>::max()) {
            result.disposition = Disposition::kStock;
            result.reason = Reason::kTokenExhausted;
            return result;
        }
        state.identity = observation.identity;
        state.last_raw_weapon = observation.raw_weapon;
        state.serial = ++ledger.serial;
        state.phase = Phase::kPendingDraw;
        result.weight = 1.0F - weight;
        result.reason = Reason::kActiveDraw;
        result.token = state.serial;
        result.phase = state.phase;
        return result;
    }

    if (observation.duration_bits == kSecondaryBackDurationBits &&
        observation.secondary_back_blend_admitted) {
        // Both endpoints belong on the back. Native +4F0/+500 interpolation
        // continues untouched underneath a constant authored parent/depth.
        // This supersedes Draw authority but never creates a Draw ticket.
        state = {};
        if (!valid_progress(observation.progress)) {
            result.reason = Reason::kMalformedProgress;
            return result;
        }
        result.disposition = Disposition::kFull;
        result.weight = 1.0F;
        result.reason = Reason::kSecondaryBackBlend;
        return result;
    }

    if (observation.duration_bits != 0) {
        state = {};
        result.reason = Reason::kMalformedDuration;
        return result;
    }

    result.disposition = Disposition::kFull;
    result.weight = 1.0F;
    result.reason = Reason::kColdBack;
    // No reading/interpreting progress here: ctor/rebind initialize duration,
    // but have no contract for the stale progress or endpoint bytes.
    if (observation.to_back) {
        state = {};
        return result;
    }
    if (positive_stable_back(observation)) {
        state = {};
        result.reason = Reason::kStableBackCancel;
        return result;
    }
    if (state.phase == Phase::kPendingDraw) {
        // A duration-zero observation supersedes an uncommitted reservation.
        // An older stock call cannot later authorize this phase out of order.
        state = {};
        result.reason = Reason::kUncommittedDraw;
        return result;
    }

    const bool relocated = state.phase != Phase::kEmpty &&
        state.last_raw_weapon != observation.raw_weapon;
    if (state.phase == Phase::kActiveDrawSeen &&
        observation.controller_read && observation.controller_state == 1 &&
        observation.current_slot < 0) {
        // Only a committed active Draw permits interpreting terminal progress.
        // Relocation is authorized by retained Fg/runtime identity, not a sample
        // threshold copied from the previous raw allocation.
        const bool same_raw_terminal = !relocated &&
            valid_progress(observation.progress) &&
            observation.progress >= 0.999F;
        if (relocated || same_raw_terminal) {
            state.phase = Phase::kTerminalHold;
            state.terminal_callbacks = 0;
        }
    }
    if (state.phase == Phase::kTerminalHold) {
        if (state.terminal_callbacks <
            std::numeric_limits<std::uint32_t>::max()) {
            ++state.terminal_callbacks;
        }
        state.last_raw_weapon = observation.raw_weapon;
        result.disposition = Disposition::kWeighted;
        result.reason = Reason::kTerminalDraw;
        result.weight = 0.0F;
        result.phase = state.phase;
        result.terminal_hold = true;
        result.raw_relocated = relocated;
        result.terminal_ordinal = state.terminal_callbacks;
        return result;
    }
    if (state.phase == Phase::kActiveDrawSeen) {
        result.reason = Reason::kAwaitingTerminal;
        result.phase = state.phase;
    }
    return result;
}

inline void cancel(Ledger& ledger, int inventory_slot,
                   std::uint64_t token) noexcept {
    if (!valid_slot(inventory_slot) || token == 0) {
        return;
    }
    SlotState& state = ledger.slots[static_cast<std::size_t>(inventory_slot)];
    if (state.phase == Phase::kPendingDraw && state.serial == token) {
        state = {};
    }
}

// Call only after successful stock execution AND successful authored frame/depth
// processing. The observation must be freshly re-read after that call. A failed
// postcondition invalidates only this pending token, never a newer reservation.
inline bool commit(Ledger& ledger, const Observation& after,
                   std::uint64_t token) noexcept {
    if (token == 0) {
        return false;
    }
    for (std::size_t index = 0; index < ledger.slots.size(); ++index) {
        SlotState& state = ledger.slots[index];
        if (state.phase != Phase::kPendingDraw || state.serial != token) {
            continue;
        }
        if (!valid_scope(after) ||
            index != static_cast<std::size_t>(after.inventory_slot) ||
            !(state.identity == after.identity) || !after.selected_back ||
            after.duration_bits != kNativeDurationBits || after.to_back ||
            !valid_progress(after.progress)) {
            state = {};
            return false;
        }
        state.phase = Phase::kActiveDrawSeen;
        state.last_raw_weapon = after.raw_weapon;
        state.last_active_progress = clamped_progress(after.progress);
        return true;
    }
    return false;
}

} // namespace coop::nadine_back
