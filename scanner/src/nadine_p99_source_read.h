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
        // The source registry allows 4096 * 0x40 bytes. Keep the existing
        // per-request 0x20000 bound without silently checking only its prefix.
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

// Native 1410A1F90 weak-resolution semantics, restricted to the admitted P99
// current slot 0..17. No vslot call, inventory scan, GetWeaponBySlot +0x72 write,
// retain/release or registry operation. Repeated reads detect observed turnover,
// not arbitrary ABA/concurrent destruction; the normal native job lease remains required.
template<class R>
[[nodiscard]] bool owner(R& reader, const Contract& c, std::uintptr_t expected_player,
                         Owner& output) noexcept {
    output = {};
    Owner value{};
    std::uintptr_t pointer = 0, selected_record = 0;
    std::uint32_t word = 0;
    std::uint16_t half = 0;
    std::uint8_t byte = 0;
    if (c.retained_slot == 0 || c.player_vtable == 0 || c.controller_vtable == 0 ||
        c.grenade_vtable == 0 || c.weapon_vtable == 0 ||
        !weak_pair(reader, c.retained_slot, value.retained) ||
        !weak_object(reader, value.retained, value.player) ||
        (expected_player != 0 && value.player != expected_player) ||
        !reader.range(value.player, 0x4014) ||
        !reader.read(value.player, 0, pointer) || pointer != c.player_vtable ||
        !reader.read(value.player, 0x4010, word) || word != 0 ||
        !reader.read(value.player, 0x170, word) || word != c.look ||
        !reader.read(value.player, 0xBC, value.player_id) || value.player_id == 0 ||
        value.player_id != value.retained.generation ||
        !reader.read(value.player, 0x1A8, value.fg) || !reader.range(value.fg, 0x1B4) ||
        !reader.read(value.fg, 6, byte) || byte != 2 ||
        !reader.read(value.fg, 0x1A8, value.fg_owner.handle) ||
        !reader.read(value.fg, 0x1B0, value.fg_owner.generation) ||
        value.fg_owner != value.retained || !weak_object(reader, value.fg_owner, pointer) ||
        pointer != value.player ||
        !reader.read(value.fg, 0x18, value.art) || !reader.range(value.art, 0x48) ||
        !reader.read(value.art, 0x24, word) || word != c.art_id ||
        !reader.read(value.art, 0x34, word) || word != 580 ||
        !reader.read(value.art, 0x40, value.raw) || !reader.range(value.raw, 0x12) ||
        !reader.read(value.raw, 4, word) || word != c.skeleton_id ||
        !reader.read(value.raw, 0x10, half) || half != 580 ||
        !reader.read(value.fg, 0x100, value.local_pose) ||
        !reader.range(value.local_pose, 66 * 48) ||
        !reader.read(value.player, 0x1D88, value.controller) ||
        !reader.range(value.controller, 0x138) ||
        !reader.read(value.controller, 0, pointer) || pointer != c.controller_vtable ||
        !reader.read(value.controller, 0x108, pointer) || pointer != value.player ||
        !reader.read(value.controller, 0x120, value.slot) || value.slot < 0 || value.slot >= 18 ||
        !reader.read(value.controller, 0x134, value.state) || value.state != 2 ||
        !reader.read(value.player, 0x1E50, value.grenade) || !reader.range(value.grenade, 0x124) ||
        !reader.read(value.grenade, 0, pointer) || pointer != c.grenade_vtable ||
        !reader.read(value.grenade, 0x108, pointer) || pointer != value.player ||
        !reader.read(value.grenade, 0x120, word) || word != 0 ||
        !add(value.controller, 0x148 + 0x10 * static_cast<std::size_t>(value.slot), selected_record) ||
        !weak_pair(reader, selected_record, value.weapon_slot) ||
        !weak_object(reader, value.weapon_slot, value.weapon) || !reader.range(value.weapon, 0x864) ||
        !reader.read(value.weapon, 0, pointer) || pointer != c.weapon_vtable ||
        !reader.read(value.weapon, 0xBC, value.weapon_id) || value.weapon_id != value.weapon_slot.generation ||
        !reader.read(value.weapon, 0x860, word) || word != c.pistol_id ||
        !reader.read(value.weapon, 0x4E0, value.parent) || !reader.range(value.parent, 0x10) ||
        !reader.read(value.parent, 8, pointer) || pointer != value.player ||
        !reader.read(value.player, 0x240, value.position) ||
        !reader.read(value.player, 0x140, value.orientation) ||
        !reader.read(value.player, 0x39C8, value.move_stick) ||
        !reader.read(value.player, 0x3A30, value.move_world) ||
        !reader.read(value.player, 0x3A1C, value.move_magnitude)) return false;

    WeakPair again{};
    std::int32_t slot = -1;
    // All reads below are fresh payload reads, even when their page certificate
    // is reused inside this callback-free snapshot.
    if (!weak_pair(reader, c.retained_slot, again) || again != value.retained ||
        !weak_object(reader, again, pointer) || pointer != value.player ||
        !reader.read(value.player, 0, pointer) || pointer != c.player_vtable ||
        !reader.read(value.player, 0x1A8, pointer) || pointer != value.fg ||
        !reader.read(value.player, 0x1D88, pointer) || pointer != value.controller ||
        !reader.read(value.player, 0x1E50, pointer) || pointer != value.grenade ||
        !reader.read(value.player, 0x170, word) || word != c.look ||
        !reader.read(value.player, 0x4010, word) || word != 0 ||
        !reader.read(value.fg, 0x1A8, again.handle) ||
        !reader.read(value.fg, 0x1B0, again.generation) || again != value.fg_owner ||
        !reader.read(value.fg, 0x18, pointer) || pointer != value.art ||
        !reader.read(value.fg, 0x100, pointer) || pointer != value.local_pose ||
        !reader.read(value.fg, 6, byte) || byte != 2 ||
        !reader.read(value.controller, 0, pointer) || pointer != c.controller_vtable ||
        !reader.read(value.controller, 0x108, pointer) || pointer != value.player ||
        !reader.read(value.controller, 0x120, slot) || slot != value.slot ||
        !reader.read(value.controller, 0x134, word) || word != value.state ||
        !reader.read(value.grenade, 0, pointer) || pointer != c.grenade_vtable ||
        !reader.read(value.grenade, 0x108, pointer) || pointer != value.player ||
        !reader.read(value.grenade, 0x120, word) || word != 0 ||
        !weak_pair(reader, selected_record, again) || again != value.weapon_slot ||
        !weak_object(reader, again, pointer) || pointer != value.weapon ||
        !reader.read(value.weapon, 0, pointer) || pointer != c.weapon_vtable ||
        !reader.read(value.weapon, 0x860, word) || word != c.pistol_id ||
        !reader.read(value.weapon, 0x4E0, pointer) || pointer != value.parent ||
        !reader.read(value.parent, 8, pointer) || pointer != value.player) return false;
    output = value;
    return true;
}
} // namespace coop::p99_source_read
