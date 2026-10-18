#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Separate from legacy CONE and StatePacket. A root-only prototype: no health,
// death, AI commands, pointers or runtime handles cross the wire.
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
}
