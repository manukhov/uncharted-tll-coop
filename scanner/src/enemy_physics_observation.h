#pragma once

// External, read-only configuration diagnostics. Repeated values are NOT an
// atomic snapshot, a lifetime lease, a cross-copy identity or write admission.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace coop::enemy_physics_observation {
constexpr std::size_t kMaxActors = 64;
constexpr std::size_t kMaxBodies = 32;
constexpr std::size_t kMaxBindings = 32;
constexpr std::uintptr_t kPirateRva = 0x294b9d0;
constexpr std::uintptr_t kHealthVtableRva = 0x2bbe128;

template <class T, std::size_t N>
T field(const std::array<std::byte, N>& bytes, std::size_t offset) {
    T result{};
    if (offset <= N && sizeof(T) <= N - offset)
        std::memcpy(&result, bytes.data() + offset, sizeof(T));
    return result;
}

enum class Status { ok, unreadable, non_pirate, identity_changed, over_cap,
                    composite_owner_mismatch };
inline const char* status_name(Status s) {
    switch (s) {
    case Status::ok: return "observed";
    case Status::unreadable: return "unreadable";
    case Status::non_pirate: return "non-pirate";
    case Status::identity_changed: return "identity-changed";
    case Status::over_cap: return "over-cap-partial";
    case Status::composite_owner_mismatch: return "composite-owner-mismatch";
    }
    return "unknown";
}
struct Entry { std::uintptr_t record{}; std::uint32_t id{}, padding{}; };
static_assert(sizeof(Entry) == 16);
struct Binding { std::int32_t key{}; std::uint32_t body_index{}; };
struct Body {
    std::uintptr_t owner{}, native{};
    std::uint32_t handle_id{}, flags{};
    std::uint16_t handle_index{};
    std::int16_t joint{};
    std::uint8_t source{}, native_motion{};
    bool in_world{};
    float blend{};
    std::array<float, 8> pose{};
};
struct Observation {
    Entry entry{};
    std::uintptr_t actor{}, composite{}, body_array{}, binding_array{}, auxiliary{};
    std::uintptr_t ragdoll{}, optional_body{}; // Observe inline only; never follow.
    std::uint32_t process_flags{}, body_count{}, binding_count{}, gate18{}, gate24{};
    std::uint32_t copied_bodies{}, copied_bindings{};
    std::int32_t root_selector{-1}, raw_hp38{}, previous_effective_hp3c{};
    bool health_known{};
    std::uint8_t flags0{}, flags2{}, owner_flags360{};
    std::array<char, 32> debug_name{};
    std::array<float, 8> local_pose{};
    // +250 and +280 are NOT the second half of a world position/quaternion.
    std::array<float, 4> world_position240{}, raw_local250{}, raw_offset_point280{};
    std::array<Body, kMaxBodies> bodies{};
    std::array<Binding, kMaxBindings> bindings{};
};
inline unsigned desired_mode(const Body& b) { return (b.flags >> 13) & 3u; }
inline const char* source_name(const Body& b, std::uintptr_t actor) {
    if (b.owner != actor) return "foreign-owner";
    if (b.source == 1) return "owner-root";
    if (b.source == 4) return "special-4-unclassified";
    if (b.joint >= 0) return "joint";
    return "unclassified";
}
inline bool valid_bindings(const Observation& o) {
    if (o.binding_count > kMaxBindings || o.body_count > kMaxBodies ||
        o.copied_bodies != o.body_count || o.copied_bindings != o.binding_count) return false;
    for (std::size_t i = 0; i < o.binding_count; ++i)
        if (o.bindings[i].body_index >= o.body_count) return false;
    return true;
}
// This describes a mapping that CAN import a root, not execution of the gated
// SyncPhysicsToGame path. Body statuses/phase are deliberately not inferred.
inline bool has_root_import_mapping(const Observation& o) {
    if (!o.binding_count || !valid_bindings(o)) return false;
    if (o.bindings[0].key == -1) return true;
    if (o.root_selector >= 0)
        for (std::size_t i = 0; i < o.binding_count; ++i)
            if (o.bindings[i].key == o.root_selector) return true;
    return false;
}
inline bool repeated_configuration(const Observation& a, const Observation& b) {
    if (a.entry.record != b.entry.record || a.entry.id != b.entry.id ||
        a.actor != b.actor || a.composite != b.composite ||
        a.ragdoll != b.ragdoll || a.optional_body != b.optional_body ||
        a.body_array != b.body_array || a.binding_array != b.binding_array ||
        a.auxiliary != b.auxiliary || a.body_count != b.body_count ||
        a.binding_count != b.binding_count || a.root_selector != b.root_selector ||
        a.body_count > kMaxBodies || a.binding_count > kMaxBindings ||
        a.copied_bodies != a.body_count || b.copied_bodies != b.body_count ||
        a.copied_bindings != a.binding_count || b.copied_bindings != b.binding_count) return false;
    for (std::size_t i = 0; i < a.body_count; ++i) {
        const auto& x = a.bodies[i]; const auto& y = b.bodies[i];
        if (x.owner != y.owner || x.native != y.native ||
            x.handle_id != y.handle_id || x.handle_index != y.handle_index ||
            x.joint != y.joint || x.source != y.source ||
            desired_mode(x) != desired_mode(y) || x.native_motion != y.native_motion ||
            x.in_world != y.in_world) return false;
    }
    for (std::size_t i = 0; i < a.binding_count; ++i)
        if (a.bindings[i].key != b.bindings[i].key ||
            a.bindings[i].body_index != b.bindings[i].body_index) return false;
    return true; // Moving poses and transient A0 bits need not match.
}

// Reader must copy bytes externally, without dereferencing the remote pointer.
// No pointer from an earlier pass is reused: each call starts at the roster row.
template <class Reader>
Status capture(Reader& read, std::uintptr_t base, Entry entry, Observation& out) {
    out = {}; out.entry = entry;
    if (!entry.record || !entry.id ||
        !read(entry.record + 8, &out.actor, sizeof(out.actor))) return Status::unreadable;
    std::array<std::byte, 0x6a0> actor{};
    if (!read(out.actor, actor.data(), actor.size())) return Status::unreadable;
    if (field<std::uintptr_t>(actor, 0) != base + kPirateRva) return Status::non_pirate;
    if (field<std::uintptr_t>(actor, 0xa8) != entry.record ||
        field<std::uint32_t>(actor, 0xbc) != entry.id) return Status::identity_changed;
    out.process_flags = field<std::uint32_t>(actor, 0x40);
    out.owner_flags360 = field<std::uint8_t>(actor, 0x360);
    out.debug_name = field<std::array<char, 32>>(actor, 8);
    out.local_pose = field<std::array<float, 8>>(actor, 0x130);
    out.world_position240 = field<std::array<float, 4>>(actor, 0x240);
    out.raw_local250 = field<std::array<float, 4>>(actor, 0x250);
    out.raw_offset_point280 = field<std::array<float, 4>>(actor, 0x280);
    out.composite = field<std::uintptr_t>(actor, 0x238);
    out.ragdoll = field<std::uintptr_t>(actor, 0x580);
    out.optional_body = field<std::uintptr_t>(actor, 0x698);
    // Every successful outcome, including observed absence, rechecks identity.
    const auto recheck = [&]() {
        std::uintptr_t object_after{}, record_after{}, composite_after{}, vtable_after{};
        std::uint32_t id_after{};
        if (!read(entry.record + 8, &object_after, sizeof(object_after)) ||
            !read(out.actor, &vtable_after, sizeof(vtable_after)) ||
            !read(out.actor + 0xa8, &record_after, sizeof(record_after)) ||
            !read(out.actor + 0xbc, &id_after, sizeof(id_after)) ||
            !read(out.actor + 0x238, &composite_after, sizeof(composite_after)))
            return Status::unreadable;
        return object_after == out.actor && record_after == entry.record &&
               id_after == entry.id && composite_after == out.composite &&
               vtable_after == base + kPirateRva ? Status::ok : Status::identity_changed;
    };
    const auto health = field<std::uintptr_t>(actor, 0x4a8);
    std::array<std::byte, 0x48> health_bytes{};
    if (health && read(health, health_bytes.data(), health_bytes.size()) &&
        field<std::uintptr_t>(health_bytes, 0) == base + kHealthVtableRva) {
        out.health_known = true;
        // The effective native getter additionally applies regeneration/caps.
        out.raw_hp38 = field<std::int32_t>(health_bytes, 0x38);
        out.previous_effective_hp3c = field<std::int32_t>(health_bytes, 0x3c);
    }
    if (!out.composite) return recheck(); // Preserve observed absence.
    std::array<std::byte, 0x128> comp{};
    if (!read(out.composite, comp.data(), comp.size())) return Status::unreadable;
    if (field<std::uintptr_t>(comp, 8) != out.actor) return Status::composite_owner_mismatch;
    out.flags0 = field<std::uint8_t>(comp, 0);
    out.flags2 = field<std::uint8_t>(comp, 2);
    out.body_count = field<std::uint32_t>(comp, 0x10);
    out.gate18 = field<std::uint32_t>(comp, 0x18);
    out.gate24 = field<std::uint32_t>(comp, 0x24);
    out.body_array = field<std::uintptr_t>(comp, 0x48);
    out.binding_array = field<std::uintptr_t>(comp, 0x58);
    out.binding_count = field<std::uint32_t>(comp, 0x60);
    out.root_selector = field<std::int32_t>(comp, 0xe8);
    out.auxiliary = field<std::uintptr_t>(comp, 0x120);
    if (out.body_count > kMaxBodies || out.binding_count > kMaxBindings)
        return Status::over_cap;
    for (std::size_t i = 0; i < out.body_count; ++i) {
        std::array<std::byte, 0x110> bytes{};
        if (!out.body_array || !read(out.body_array + i * 0x110, bytes.data(), bytes.size()))
            return Status::unreadable;
        auto& b = out.bodies[i];
        b.pose = field<std::array<float, 8>>(bytes, 0);
        b.native = field<std::uintptr_t>(bytes, 0x40);
        b.handle_id = field<std::uint32_t>(bytes, 0x50);
        b.handle_index = field<std::uint16_t>(bytes, 0x54);
        b.owner = field<std::uintptr_t>(bytes, 0x68);
        b.blend = field<float>(bytes, 0x9c);
        b.flags = field<std::uint32_t>(bytes, 0xa0);
        b.joint = field<std::int16_t>(bytes, 0xc4);
        b.source = field<std::uint8_t>(bytes, 0xc6);
        if (b.native) {
            std::uintptr_t world{};
            if (!read(b.native + 0x10, &world, sizeof(world)) ||
                !read(b.native + 0x160, &b.native_motion, sizeof(b.native_motion)))
                return Status::unreadable;
            b.in_world = world != 0;
        }
        ++out.copied_bodies;
    }
    for (std::size_t i = 0; i < out.binding_count; ++i) {
        if (!out.binding_array ||
            !read(out.binding_array + i * 0x50, &out.bindings[i], sizeof(Binding)))
            return Status::unreadable;
        ++out.copied_bindings;
    }
    // Generation/reciprocity recheck after all reads; still not an atomic lease.
    return recheck();
}
} // namespace coop::enemy_physics_observation
