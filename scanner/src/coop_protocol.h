#pragma once

#include <cstdint>

namespace coop {

constexpr std::uint16_t kHostInputPort = 31337;
constexpr std::uint32_t kInputPacketMagic = 0x504F4F43;  // "COOP"
constexpr std::uint16_t kInputPacketVersion = 1;

struct InputPacket {
    std::uint32_t magic = kInputPacketMagic;
    std::uint16_t version = kInputPacketVersion;
    std::uint16_t buttons = 0;
    std::uint32_t sequence = 0;
    std::uint32_t timestamp_ms = 0;
    float move_forward = 0.0F;
    float move_right = 0.0F;
    float look_yaw_delta = 0.0F;
    float look_pitch_delta = 0.0F;
};

// Variant A (two instances) player state replication. Each instance sends its
// LOCAL player's transform every tick over UDP; the peer applies it to the
// remote-player puppet (a second Player spawned via the idx=1 spawn path).
constexpr std::uint16_t kStatePort = 31338;
constexpr std::uint32_t kStatePacketMagic = 0x53504F43;  // "COPS"
constexpr std::uint16_t kStatePacketVersion = 1;

struct StatePacket {
    std::uint32_t magic = kStatePacketMagic;
    std::uint16_t version = kStatePacketVersion;
    std::uint16_t flags = 0;          // bit0 = local player valid/present
    std::uint32_t sequence = 0;
    std::uint32_t timestamp_ms = 0;
    float pos[4]{};                   // world position block (mirrors Player+0x240)
    float quat[4]{};                  // orientation quaternion (mirrors Player+0x140)
    float velocity[3]{};              // for interpolation/extrapolation
    std::uint32_t anim_state = 0;     // coarse anim/state id (filled later)
};

// Phase 3 (host-authoritative world sync): the HOST (role A) enumerates the
// CharacterManager, picks out enemy NPCs, and sends their transforms; the CLIENT
// (role B) matches each by name-hash to its own local enemy, disables its AI, and
// applies the host transform. Sent on the same UDP socket pair as StatePacket;
// dispatched by magic.
constexpr std::uint32_t kEnemyPacketMagic = 0x454E4F43;  // "CONE"
constexpr std::uint16_t kEnemyPacketVersion = 1;
constexpr int kMaxEnemies = 32;  // CharacterManager cap is 32

struct EnemyState {
    std::uint32_t name_hash = 0;  // FNV-1a of the actor name (stable cross-instance)
    float pos[4]{};               // world position block (Char+0x240)
    float quat[4]{};              // orientation quaternion (Char+0x140)
    std::uint32_t flags = 0;      // bit0 = dead (host vtable became ProcessRagdoll)
    std::int32_t hp = -1;         // host current HP (HealthSystem+0x3c); -1 = unknown
};
constexpr std::uint32_t kEnemyFlagDead = 0x1;

struct EnemyPacket {
    std::uint32_t magic = kEnemyPacketMagic;
    std::uint16_t version = kEnemyPacketVersion;
    std::uint16_t count = 0;
    std::uint32_t sequence = 0;
    std::uint32_t timestamp_ms = 0;
    EnemyState enemies[kMaxEnemies]{};
};

// Client -> host damage relay: when the CLIENT player damages a puppet enemy, the
// client's NPC-damage hook fires; we send {enemy name-hash, damage} to the host, and
// the host re-applies it authoritatively (publish a fresh AttackInfo) on its matching
// enemy. Host stays the single source of truth (enemy state replicates back).
constexpr std::uint32_t kClientDamageMagic = 0x444E4F43;  // "COND"
struct ClientDamagePacket {
    std::uint32_t magic = kClientDamageMagic;
    std::uint32_t name_hash = 0;  // FNV-1a of the target enemy actor name
    float damage = 0.0f;
};

}  // namespace coop
