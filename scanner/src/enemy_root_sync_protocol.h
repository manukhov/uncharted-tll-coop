#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Separate from legacy CONE and StatePacket. ENR2 v1 below remains root-only;
// the explicit opt-in ENC2 channel adds initial-life bind/death/result values.
// Neither channel transports pointers, runtime handles or general AI commands.
namespace coop::enemy_root_sync {
inline constexpr std::uint32_t kMagic = 0x32524E45; // ENR2
inline constexpr std::uint16_t kVersion = 1;
inline constexpr std::size_t kActors = 32, kNameBytes = 192;
enum class Kind : std::uint8_t { hello = 1, root = 2 };
struct Key {
    std::uint32_t spawner_id{}, desired_id{}, variant_id{};
    std::array<char, kNameBytes> name{}; // full spawner provenance, NOT debug label
    bool operator==(const Key&) const = default;
};
struct Packet {
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    Kind kind = Kind::hello;
    std::uint8_t role_b{};
    std::uint64_t sender{}, receiver{}; // independently generated local roster sessions
    std::uint32_t sequence{}, reserved{};
    Key key{};
    std::array<float, 3> position{}; // local root xyz; parent identity REQUIRED on both ends
    std::array<float, 4> rotation{};
};
static_assert(sizeof(Packet) == 264 && std::is_trivially_copyable_v<Packet>);
inline bool valid_key(const Key& key) {
    if (!key.spawner_id || !key.name[0]) return false;
    const auto* end = static_cast<const char*>(std::memchr(key.name.data(), 0, key.name.size()));
    if (!end) return false;
    for (const char* at = end; at != key.name.data()+key.name.size(); ++at) if (*at) return false;
    return true;
}
inline bool valid_pose(const Packet& p) {
    for (float v : p.position) if (!std::isfinite(v) || std::fabs(v) >= 1000000.0F) return false;
    double norm{};
    for (float v : p.rotation) { if (!std::isfinite(v)) return false; norm += double(v)*v; }
    return std::fabs(norm-1.0) <= 0.02;
}
inline bool valid(const Packet& p) {
    if (p.magic != kMagic || p.version != kVersion || p.reserved || p.role_b > 1 || !p.sender || !p.sequence) return false;
    if (p.kind == Kind::hello) return p.key == Key{} && p.position == std::array<float, 3>{} && p.rotation == std::array<float, 4>{};
    return p.kind == Kind::root && p.role_b == 0 && p.receiver && valid_key(p.key) && valid_pose(p);
}
inline bool newer(std::uint32_t value, std::uint32_t previous) {
    const auto distance = value-previous; return distance && distance < 0x80000000u;
}

// Integrated initial-life combat channel. Deliberately a different magic/size:
// an enabled peer never interprets a legacy root as a life-bound command.
namespace combat {
inline constexpr std::uint32_t kMagic = 0x32434E45; // ENC2
inline constexpr std::uint16_t kVersion = 2;
enum class Kind : std::uint8_t { hello = 1, root, bind, bound, death, result, received, refused };
enum class Result : std::uint32_t { none, applied, already_applied };
enum class Refusal : std::uint32_t {
    none, root_stopped, census_rejected, census_changed, census_expired,
    cost_stopped, worker_gap, effect_limit, native_attempt_refused, event_deadline
};
struct Packet {
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    Kind kind = Kind::hello;
    std::uint8_t role_b{};
    std::uint64_t sender{}, receiver{};
    std::uint32_t sequence{};
    Refusal refusal = Refusal::none;
    std::uint64_t life_id{}, event_id{};
    Key key{};
    Result result = Result::none;
    std::uint32_t manifest_count{};
    std::array<float, 3> position{};
    std::array<float, 4> rotation{};
};
static_assert(sizeof(Packet) == 288 && std::is_trivially_copyable_v<Packet>);
static_assert(sizeof(Refusal)==4 && offsetof(Packet,refusal)==28 && offsetof(Packet,event_id)==40);
inline bool valid(const Packet& p) {
    if (p.magic != kMagic || p.version != kVersion || p.role_b > 1 || !p.sender ||
        !p.sequence || p.refusal>Refusal::event_deadline) return false;
    const bool positive=p.result==Result::applied || p.result==Result::already_applied;
    const bool zero_pose = p.position == std::array<float, 3>{} && p.rotation == std::array<float, 4>{};
    if (p.kind == Kind::hello)
        return !p.life_id && !p.event_id && !p.manifest_count && p.key == Key{} && p.result == Result::none &&
            p.refusal==Refusal::none && zero_pose;
    if (!p.receiver || !p.life_id || !valid_key(p.key)) return false;
    if (p.kind == Kind::root) {
        ::coop::enemy_root_sync::Packet pose{}; pose.position = p.position; pose.rotation = p.rotation;
        return !p.role_b && !p.event_id && !p.manifest_count && p.result == Result::none &&
            p.refusal==Refusal::none && valid_pose(pose);
    }
    if (!zero_pose) return false;
    if (p.kind == Kind::bind || p.kind == Kind::bound)
        return p.role_b == (p.kind == Kind::bound ? 1 : 0) && !p.event_id && p.result == Result::none &&
            p.refusal==Refusal::none && p.manifest_count && p.manifest_count<=kActors;
    if (p.manifest_count) return false;
    if (!p.event_id) return false;
    if (p.kind == Kind::death) return !p.role_b && p.result==Result::none && p.refusal==Refusal::none;
    if (p.kind == Kind::result) return p.role_b && positive && p.refusal==Refusal::none;
    if (p.kind == Kind::refused) return p.role_b && p.result==Result::none && p.refusal!=Refusal::none;
    // B acknowledges event storage only. A acknowledges the exact B report;
    // neither receipt permits a native write or proves a committed death.
    return p.kind==Kind::received && (p.role_b ? p.result==Result::none && p.refusal==Refusal::none :
        (positive && p.refusal==Refusal::none) || (p.result==Result::none && p.refusal!=Refusal::none));
}
} // namespace combat
}
