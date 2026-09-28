#pragma once
#include "nadine_p99_group_readability.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace coop::p99_source_read {
inline constexpr std::size_t kMaximumSourceSpan = 0x40000;

[[nodiscard]] inline bool add(std::uintptr_t base, std::size_t offset,
                              std::uintptr_t& result) noexcept {
    if (base == 0 || offset > std::numeric_limits<std::uintptr_t>::max() - base)
        return false;
    result = base + offset;
    return true;
}

// Exact page checks, never a payload probe. One instance belongs to one
// uninterrupted read-only snapshot; destroy it BEFORE any native call, logging,
// callback or unlock. The sole narrower native-call exception is the dedicated
// p99_retarget_table_admission::Admission wrapper: fixed image+0x3864A00 extent
// only, one held-lock scope, across audited read-only Find1417D0BD0 and
// Resolve1417CD100->1417D1170. It never lends this authority to borrowed payloads.
// Accessibility is not allocator authority or a lifetime lease.
template<class Query, class Copy>
class Reader {
public:
    Reader(Query query, Copy copy) : query_(query), copy_(copy) {}
    [[nodiscard]] bool range(std::uintptr_t address, std::size_t bytes) noexcept {
        if (failed_) return false;
        if (address == 0 || bytes == 0 || bytes > kMaximumSourceSpan ||
            bytes > std::numeric_limits<std::uintptr_t>::max() - address) {
            failed_ = true;
            return false;
        }
        // Generic source spans may reach 0x40000 bytes independently of the
        // retarget table capacity. Keep the per-request 0x20000 bound without
        // silently checking only the source prefix.
        while (bytes != 0) {
            const auto part = bytes > p99_group_readability::kMaximumSpan
                ? p99_group_readability::kMaximumSpan : bytes;
            if (!pages_.readable(address, part, query_)) { failed_ = true; return false; }
            address += part;
            bytes -= part;
        }
        return true;
    }
    [[nodiscard]] bool bytes(std::uintptr_t base, std::size_t offset,
                             void* output, std::size_t count) noexcept {
        std::uintptr_t address = 0;
        if (output == nullptr || !add(base, offset, address) ||
            !range(address, count) || !copy_(address, output, count)) {
            failed_ = true;
            return false;
        }
        return true;
    }
    template<class T>
    [[nodiscard]] bool read(std::uintptr_t base, std::size_t offset, T& output) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        return bytes(base, offset, &output, sizeof(output));
    }
    [[nodiscard]] std::size_t query_count() const noexcept { return pages_.query_count(); }
private:
    Query query_;
    Copy copy_;
    p99_group_readability::Batch pages_{};
    bool failed_ = false;
};

struct WeakPair {
    std::uintptr_t handle = 0;
    std::uint32_t generation = 0;
    friend bool operator==(const WeakPair&, const WeakPair&) = default;
};
template<class R>
[[nodiscard]] bool weak_pair(R& reader, std::uintptr_t address, WeakPair& out) noexcept {
    out = {};
    return reader.range(address, 0x0C) && reader.read(address, 0, out.handle) &&
        reader.read(address, 8, out.generation) && out.handle != 0 && out.generation != 0;
}
template<class R>
[[nodiscard]] bool weak_object(R& reader, const WeakPair& pair,
                               std::uintptr_t& object) noexcept {
    object = 0;
    std::uint32_t generation = 0;
    return pair.handle != 0 && pair.generation != 0 && reader.range(pair.handle, 0x10) &&
        reader.read(pair.handle, 8, object) && object != 0 &&
        reader.read(object, 0xBC, generation) && generation == pair.generation;
}
template<class R>
[[nodiscard]] bool retained_player(R& reader, std::uintptr_t slot,
                                   std::uintptr_t& player) noexcept {
    player = 0;
    WeakPair before{}, after{};
    std::uintptr_t resolved = 0, again = 0;
    if (!weak_pair(reader, slot, before) || !weak_object(reader, before, resolved) ||
        !weak_pair(reader, slot, after) || before != after ||
        !weak_object(reader, after, again) || again != resolved) return false;
    player = resolved;
    return true;
}

struct Contract {
    std::uintptr_t retained_slot = 0;
    std::uintptr_t player_vtable = 0;
    std::uintptr_t controller_vtable = 0;
    std::uintptr_t grenade_vtable = 0;
    std::uintptr_t weapon_vtable = 0;
    std::uint32_t look = 1;
    std::uint32_t art_id = 0xED25C708;
    std::uint32_t skeleton_id = 0xC39CA709;
    std::uint32_t pistol_id = 0x8075397D;
};
struct Owner {
    std::uintptr_t player = 0, fg = 0, art = 0, raw = 0, local_pose = 0;
    std::uintptr_t controller = 0, grenade = 0, weapon = 0, parent = 0;
    WeakPair retained{}, fg_owner{}, weapon_slot{};
    std::uint32_t player_id = 0, weapon_id = 0;
    std::int32_t slot = -1;
    std::uint32_t state = 0;
    std::array<float, 3> position{};
    std::array<float, 4> orientation{};
    std::array<float, 2> move_stick{};
    std::array<float, 4> move_world{};
    float move_magnitude = 0;
};

// Optional first-failure evidence only. No pointers, payload copies, callbacks,
// timing or policy: numeric acceptance remains the caller's separate check.
enum class OwnerCheck : std::uint8_t {
    NotStarted,
    Contract,
    RetainedPair,
    RetainedObject,
    ExpectedPlayer,
    PlayerRange,
    PlayerVtable,
    PlayerState,
    PlayerLook,
    PlayerId,
    FgPointerAndRange,
    FgKind,
    FgOwnerPair,
    FgOwnerObject,
    ArtPointerAndRange,
    ArtId,
    ArtJointCount,
    RawPointerAndRange,
    RawSkeletonId,
    RawJointCount,
    LocalPose,
    ControllerPointerAndRange,
    ControllerVtable,
    ControllerOwner,
    ControllerSlot,
    ControllerState,
    GrenadePointerAndRange,
    GrenadeVtable,
    GrenadeOwner,
    GrenadeState,
    WeaponSlotAddress,
    WeaponPair,
    WeaponObjectAndRange,
    WeaponVtable,
    WeaponId,
    WeaponArt,
    WeaponParent,
    WeaponParentOwner,
    Position,
    Orientation,
    MoveStick,
    MoveWorld,
    MoveMagnitude,
    ClosingRetainedPair,
    ClosingRetainedObject,
    ClosingPlayerVtable,
    ClosingFgPointer,
    ClosingControllerPointer,
    ClosingGrenadePointer,
    ClosingPlayerLook,
    ClosingPlayerState,
    ClosingFgOwnerPair,
    ClosingArtPointer,
    ClosingLocalPose,
    ClosingFgKind,
    ClosingControllerVtable,
    ClosingControllerOwner,
    ClosingControllerSlot,
    ClosingControllerState,
    ClosingGrenadeVtable,
    ClosingGrenadeOwner,
    ClosingGrenadeState,
    ClosingWeaponPair,
    ClosingWeaponObject,
    ClosingWeaponVtable,
    ClosingWeaponArt,
    ClosingWeaponParent,
    ClosingWeaponParentOwner,
    Complete,
    Count
};
struct OwnerObservation {
    OwnerCheck check = OwnerCheck::NotStarted;
    struct Slot {
        bool attempted = false;
        bool read_ok = false;
        std::int32_t value = 0; // Meaningful only when read_ok; no inferred zero.
    } initial_slot{}, closing_slot{};
};
// The runtime caller applies these numeric checks after owner() has completed;
// this tag is evidence of the reached check, never an extra acceptance rule.
enum class OwnerNumericCheck : std::uint8_t { NotStarted, Position, Orientation, Complete };
static_assert(std::is_trivially_copyable_v<OwnerObservation>);
static_assert(std::is_trivially_destructible_v<OwnerObservation>);

[[nodiscard]] constexpr const char* owner_check_name(OwnerCheck check) noexcept {
    switch (check) {
    case OwnerCheck::NotStarted: return "not-started";
    case OwnerCheck::Contract: return "contract";
    case OwnerCheck::RetainedPair: return "retained-pair";
    case OwnerCheck::RetainedObject: return "retained-object";
    case OwnerCheck::ExpectedPlayer: return "expected-player";
    case OwnerCheck::PlayerRange: return "player-range";
    case OwnerCheck::PlayerVtable: return "player-vtable";
    case OwnerCheck::PlayerState: return "player-state";
    case OwnerCheck::PlayerLook: return "player-look";
    case OwnerCheck::PlayerId: return "player-id";
    case OwnerCheck::FgPointerAndRange: return "fg-pointer-and-range";
    case OwnerCheck::FgKind: return "fg-kind";
    case OwnerCheck::FgOwnerPair: return "fg-owner-pair";
    case OwnerCheck::FgOwnerObject: return "fg-owner-object";
    case OwnerCheck::ArtPointerAndRange: return "art-pointer-and-range";
    case OwnerCheck::ArtId: return "art-id";
    case OwnerCheck::ArtJointCount: return "art-joint-count";
    case OwnerCheck::RawPointerAndRange: return "raw-pointer-and-range";
    case OwnerCheck::RawSkeletonId: return "raw-skeleton-id";
    case OwnerCheck::RawJointCount: return "raw-joint-count";
    case OwnerCheck::LocalPose: return "local-pose";
    case OwnerCheck::ControllerPointerAndRange: return "controller-pointer-and-range";
    case OwnerCheck::ControllerVtable: return "controller-vtable";
    case OwnerCheck::ControllerOwner: return "controller-owner";
    case OwnerCheck::ControllerSlot: return "controller-slot";
    case OwnerCheck::ControllerState: return "controller-state";
    case OwnerCheck::GrenadePointerAndRange: return "grenade-pointer-and-range";
    case OwnerCheck::GrenadeVtable: return "grenade-vtable";
    case OwnerCheck::GrenadeOwner: return "grenade-owner";
    case OwnerCheck::GrenadeState: return "grenade-state";
    case OwnerCheck::WeaponSlotAddress: return "weapon-slot-address";
    case OwnerCheck::WeaponPair: return "weapon-pair";
    case OwnerCheck::WeaponObjectAndRange: return "weapon-object-and-range";
    case OwnerCheck::WeaponVtable: return "weapon-vtable";
    case OwnerCheck::WeaponId: return "weapon-id";
    case OwnerCheck::WeaponArt: return "weapon-art";
    case OwnerCheck::WeaponParent: return "weapon-parent";
    case OwnerCheck::WeaponParentOwner: return "weapon-parent-owner";
    case OwnerCheck::Position: return "position";
    case OwnerCheck::Orientation: return "orientation";
    case OwnerCheck::MoveStick: return "move-stick";
    case OwnerCheck::MoveWorld: return "move-world";
    case OwnerCheck::MoveMagnitude: return "move-magnitude";
    case OwnerCheck::ClosingRetainedPair: return "closing-retained-pair";
    case OwnerCheck::ClosingRetainedObject: return "closing-retained-object";
    case OwnerCheck::ClosingPlayerVtable: return "closing-player-vtable";
    case OwnerCheck::ClosingFgPointer: return "closing-fg-pointer";
    case OwnerCheck::ClosingControllerPointer: return "closing-controller-pointer";
    case OwnerCheck::ClosingGrenadePointer: return "closing-grenade-pointer";
    case OwnerCheck::ClosingPlayerLook: return "closing-player-look";
    case OwnerCheck::ClosingPlayerState: return "closing-player-state";
    case OwnerCheck::ClosingFgOwnerPair: return "closing-fg-owner-pair";
    case OwnerCheck::ClosingArtPointer: return "closing-art-pointer";
    case OwnerCheck::ClosingLocalPose: return "closing-local-pose";
    case OwnerCheck::ClosingFgKind: return "closing-fg-kind";
    case OwnerCheck::ClosingControllerVtable: return "closing-controller-vtable";
    case OwnerCheck::ClosingControllerOwner: return "closing-controller-owner";
    case OwnerCheck::ClosingControllerSlot: return "closing-controller-slot";
    case OwnerCheck::ClosingControllerState: return "closing-controller-state";
    case OwnerCheck::ClosingGrenadeVtable: return "closing-grenade-vtable";
    case OwnerCheck::ClosingGrenadeOwner: return "closing-grenade-owner";
    case OwnerCheck::ClosingGrenadeState: return "closing-grenade-state";
    case OwnerCheck::ClosingWeaponPair: return "closing-weapon-pair";
    case OwnerCheck::ClosingWeaponObject: return "closing-weapon-object";
    case OwnerCheck::ClosingWeaponVtable: return "closing-weapon-vtable";
    case OwnerCheck::ClosingWeaponArt: return "closing-weapon-art";
    case OwnerCheck::ClosingWeaponParent: return "closing-weapon-parent";
    case OwnerCheck::ClosingWeaponParentOwner: return "closing-weapon-parent-owner";
    case OwnerCheck::Complete: return "complete";
    default: return "unknown";
    }
}

// Native 1410A1F90 weak-resolution semantics, restricted to the admitted P99
// current slot 0..17. No vslot call, inventory scan, GetWeaponBySlot +0x72 write,
// retain/release or registry operation. Repeated reads detect observed turnover,
// not arbitrary ABA/concurrent destruction; the normal native job lease remains required.
template<class R>
[[nodiscard]] bool owner(R& reader, const Contract& c, std::uintptr_t expected_player,
                         Owner& output, OwnerObservation* observation = nullptr) noexcept {
    output = {};
    if (observation != nullptr) *observation = {};
    // A marker is sequenced immediately before its original field group. It
    // cannot read game memory, alter a condition or advance after a refusal.
    const auto mark = [observation](OwnerCheck check) noexcept {
        if (observation != nullptr) observation->check = check;
        return true;
    };
    // Observe the result of the ORIGINAL read, including a failed copy. Never
    // reread a slot just for diagnostics or change the existing numeric gate.
    const auto read_slot = [&](std::uintptr_t controller, std::int32_t& slot,
                               bool closing) noexcept {
        auto* seen = observation == nullptr ? nullptr :
            (closing ? &observation->closing_slot : &observation->initial_slot);
        if (seen != nullptr) seen->attempted = true;
        const bool okay = reader.read(controller, 0x120, slot);
        if (seen != nullptr) {
            seen->read_ok = okay;
            seen->value = okay ? slot : 0;
        }
        return okay;
    };
    Owner value{};
    std::uintptr_t pointer = 0, selected_record = 0;
    std::uint32_t word = 0;
    std::uint16_t half = 0;
    std::uint8_t byte = 0;
    if (!mark(OwnerCheck::Contract) ||
        c.retained_slot == 0 || c.player_vtable == 0 || c.controller_vtable == 0 ||
        c.grenade_vtable == 0 || c.weapon_vtable == 0 ||
        !mark(OwnerCheck::RetainedPair) ||
        !weak_pair(reader, c.retained_slot, value.retained) ||
        !mark(OwnerCheck::RetainedObject) ||
        !weak_object(reader, value.retained, value.player) ||
        !mark(OwnerCheck::ExpectedPlayer) ||
        (expected_player != 0 && value.player != expected_player) ||
        !mark(OwnerCheck::PlayerRange) ||
        !reader.range(value.player, 0x4014) ||
        !mark(OwnerCheck::PlayerVtable) ||
        !reader.read(value.player, 0, pointer) || pointer != c.player_vtable ||
        !mark(OwnerCheck::PlayerState) ||
        !reader.read(value.player, 0x4010, word) || word != 0 ||
        !mark(OwnerCheck::PlayerLook) ||
        !reader.read(value.player, 0x170, word) || word != c.look ||
        !mark(OwnerCheck::PlayerId) ||
        !reader.read(value.player, 0xBC, value.player_id) || value.player_id == 0 ||
        value.player_id != value.retained.generation ||
        !mark(OwnerCheck::FgPointerAndRange) ||
        !reader.read(value.player, 0x1A8, value.fg) || !reader.range(value.fg, 0x1B4) ||
        !mark(OwnerCheck::FgKind) ||
        !reader.read(value.fg, 6, byte) || byte != 2 ||
        !mark(OwnerCheck::FgOwnerPair) ||
        !reader.read(value.fg, 0x1A8, value.fg_owner.handle) ||
        !reader.read(value.fg, 0x1B0, value.fg_owner.generation) ||
        value.fg_owner != value.retained || !mark(OwnerCheck::FgOwnerObject) ||
        !weak_object(reader, value.fg_owner, pointer) ||
        pointer != value.player ||
        !mark(OwnerCheck::ArtPointerAndRange) ||
        !reader.read(value.fg, 0x18, value.art) || !reader.range(value.art, 0x48) ||
        !mark(OwnerCheck::ArtId) ||
        !reader.read(value.art, 0x24, word) || word != c.art_id ||
        !mark(OwnerCheck::ArtJointCount) ||
        !reader.read(value.art, 0x34, word) || word != 580 ||
        !mark(OwnerCheck::RawPointerAndRange) ||
        !reader.read(value.art, 0x40, value.raw) || !reader.range(value.raw, 0x12) ||
        !mark(OwnerCheck::RawSkeletonId) ||
        !reader.read(value.raw, 4, word) || word != c.skeleton_id ||
        !mark(OwnerCheck::RawJointCount) ||
        !reader.read(value.raw, 0x10, half) || half != 580 ||
        !mark(OwnerCheck::LocalPose) ||
        !reader.read(value.fg, 0x100, value.local_pose) ||
        !reader.range(value.local_pose, 66 * 48) ||
        !mark(OwnerCheck::ControllerPointerAndRange) ||
        !reader.read(value.player, 0x1D88, value.controller) ||
        !reader.range(value.controller, 0x138) ||
        !mark(OwnerCheck::ControllerVtable) ||
        !reader.read(value.controller, 0, pointer) || pointer != c.controller_vtable ||
        !mark(OwnerCheck::ControllerOwner) ||
        !reader.read(value.controller, 0x108, pointer) || pointer != value.player ||
        // Conservative P99 admission, not the native inventory extent. Native
        // GetWeaponBySlot accepts0..22; ordinary P99 Give class0 selects slot0.
        // Keep this gate unchanged: historical controller-slot refusal did not
        // capture read-success/value and cannot justify a wider admission.
        !mark(OwnerCheck::ControllerSlot) ||
        !read_slot(value.controller, value.slot, false) || value.slot < 0 || value.slot >= 18 ||
        !mark(OwnerCheck::ControllerState) ||
        !reader.read(value.controller, 0x134, value.state) || value.state != 2 ||
        !mark(OwnerCheck::GrenadePointerAndRange) ||
        !reader.read(value.player, 0x1E50, value.grenade) || !reader.range(value.grenade, 0x124) ||
        !mark(OwnerCheck::GrenadeVtable) ||
        !reader.read(value.grenade, 0, pointer) || pointer != c.grenade_vtable ||
        !mark(OwnerCheck::GrenadeOwner) ||
        !reader.read(value.grenade, 0x108, pointer) || pointer != value.player ||
        !mark(OwnerCheck::GrenadeState) ||
        !reader.read(value.grenade, 0x120, word) || word != 0 ||
        !mark(OwnerCheck::WeaponSlotAddress) ||
        !add(value.controller, 0x148 + 0x10 * static_cast<std::size_t>(value.slot), selected_record) ||
        !mark(OwnerCheck::WeaponPair) ||
        !weak_pair(reader, selected_record, value.weapon_slot) ||
        !mark(OwnerCheck::WeaponObjectAndRange) ||
        !weak_object(reader, value.weapon_slot, value.weapon) || !reader.range(value.weapon, 0x864) ||
        !mark(OwnerCheck::WeaponVtable) ||
        !reader.read(value.weapon, 0, pointer) || pointer != c.weapon_vtable ||
        !mark(OwnerCheck::WeaponId) ||
        !reader.read(value.weapon, 0xBC, value.weapon_id) || value.weapon_id != value.weapon_slot.generation ||
        !mark(OwnerCheck::WeaponArt) ||
        !reader.read(value.weapon, 0x860, word) || word != c.pistol_id ||
        !mark(OwnerCheck::WeaponParent) ||
        !reader.read(value.weapon, 0x4E0, value.parent) || !reader.range(value.parent, 0x10) ||
        !mark(OwnerCheck::WeaponParentOwner) ||
        !reader.read(value.parent, 8, pointer) || pointer != value.player ||
        !mark(OwnerCheck::Position) ||
        !reader.read(value.player, 0x240, value.position) ||
        !mark(OwnerCheck::Orientation) ||
        !reader.read(value.player, 0x140, value.orientation) ||
        !mark(OwnerCheck::MoveStick) ||
        !reader.read(value.player, 0x39C8, value.move_stick) ||
        !mark(OwnerCheck::MoveWorld) ||
        !reader.read(value.player, 0x3A30, value.move_world) ||
        !mark(OwnerCheck::MoveMagnitude) ||
        !reader.read(value.player, 0x3A1C, value.move_magnitude)) return false;

    WeakPair again{};
    std::int32_t slot = -1;
    // All reads below are fresh payload reads, even when their page certificate
    // is reused inside this callback-free snapshot.
    if (!mark(OwnerCheck::ClosingRetainedPair) ||
        !weak_pair(reader, c.retained_slot, again) || again != value.retained ||
        !mark(OwnerCheck::ClosingRetainedObject) ||
        !weak_object(reader, again, pointer) || pointer != value.player ||
        !mark(OwnerCheck::ClosingPlayerVtable) ||
        !reader.read(value.player, 0, pointer) || pointer != c.player_vtable ||
        !mark(OwnerCheck::ClosingFgPointer) ||
        !reader.read(value.player, 0x1A8, pointer) || pointer != value.fg ||
        !mark(OwnerCheck::ClosingControllerPointer) ||
        !reader.read(value.player, 0x1D88, pointer) || pointer != value.controller ||
        !mark(OwnerCheck::ClosingGrenadePointer) ||
        !reader.read(value.player, 0x1E50, pointer) || pointer != value.grenade ||
        !mark(OwnerCheck::ClosingPlayerLook) ||
        !reader.read(value.player, 0x170, word) || word != c.look ||
        !mark(OwnerCheck::ClosingPlayerState) ||
        !reader.read(value.player, 0x4010, word) || word != 0 ||
        !mark(OwnerCheck::ClosingFgOwnerPair) ||
        !reader.read(value.fg, 0x1A8, again.handle) ||
        !reader.read(value.fg, 0x1B0, again.generation) || again != value.fg_owner ||
        !mark(OwnerCheck::ClosingArtPointer) ||
        !reader.read(value.fg, 0x18, pointer) || pointer != value.art ||
        !mark(OwnerCheck::ClosingLocalPose) ||
        !reader.read(value.fg, 0x100, pointer) || pointer != value.local_pose ||
        !mark(OwnerCheck::ClosingFgKind) ||
        !reader.read(value.fg, 6, byte) || byte != 2 ||
        !mark(OwnerCheck::ClosingControllerVtable) ||
        !reader.read(value.controller, 0, pointer) || pointer != c.controller_vtable ||
        !mark(OwnerCheck::ClosingControllerOwner) ||
        !reader.read(value.controller, 0x108, pointer) || pointer != value.player ||
        !mark(OwnerCheck::ClosingControllerSlot) ||
        !read_slot(value.controller, slot, true) || slot != value.slot ||
        !mark(OwnerCheck::ClosingControllerState) ||
        !reader.read(value.controller, 0x134, word) || word != value.state ||
        !mark(OwnerCheck::ClosingGrenadeVtable) ||
        !reader.read(value.grenade, 0, pointer) || pointer != c.grenade_vtable ||
        !mark(OwnerCheck::ClosingGrenadeOwner) ||
        !reader.read(value.grenade, 0x108, pointer) || pointer != value.player ||
        !mark(OwnerCheck::ClosingGrenadeState) ||
        !reader.read(value.grenade, 0x120, word) || word != 0 ||
        !mark(OwnerCheck::ClosingWeaponPair) ||
        !weak_pair(reader, selected_record, again) || again != value.weapon_slot ||
        !mark(OwnerCheck::ClosingWeaponObject) ||
        !weak_object(reader, again, pointer) || pointer != value.weapon ||
        !mark(OwnerCheck::ClosingWeaponVtable) ||
        !reader.read(value.weapon, 0, pointer) || pointer != c.weapon_vtable ||
        !mark(OwnerCheck::ClosingWeaponArt) ||
        !reader.read(value.weapon, 0x860, word) || word != c.pistol_id ||
        !mark(OwnerCheck::ClosingWeaponParent) ||
        !reader.read(value.weapon, 0x4E0, pointer) || pointer != value.parent ||
        !mark(OwnerCheck::ClosingWeaponParentOwner) ||
        !reader.read(value.parent, 8, pointer) || pointer != value.player) return false;
    output = value;
    mark(OwnerCheck::Complete);
    return true;
}
} // namespace coop::p99_source_read
