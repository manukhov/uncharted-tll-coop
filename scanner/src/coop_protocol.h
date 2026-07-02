#pragma once

#include <cstddef>
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

// Two-instance player snapshot. This packet carries continuous state only.
// One-shot gameplay actions need a separate reliable ordered event stream.
constexpr std::uint16_t kStatePort = 31338;
constexpr std::uint32_t kStatePacketMagic = 0x53504F43;  // "COPS"
constexpr std::uint16_t kStatePacketVersion = 5;

// Contiguous native NetSnapshot weapon region [0x3f4, 0x504). It contains the
// eight inventory ids, per-slot state, selected weapon classes and the four
// detailed weapon records used by NetSnapshotWeapons.
constexpr std::size_t kWeaponSnapshotBytes = 0x110;

struct StatePacket {
    std::uint32_t magic = kStatePacketMagic;
    std::uint16_t version = kStatePacketVersion;
    std::uint16_t flags = 0;          // bits 0..3: player/health/movement/buttons; bit4 teleport
    std::uint32_t sequence = 0;
    std::uint32_t timestamp_ms = 0;
    float pos[4]{};                   // world position block (mirrors Player+0x240)
    float quat[4]{};                  // orientation quaternion (mirrors Player+0x140)
    float velocity[3]{};              // for interpolation/extrapolation
    float move_stick[2]{};            // PlayerInputCommandState raw move pair (+0x28)
    float move_world[4]{};            // native world move vector (+0x90, index 0)
    float move_magnitude = 0.0F;       // native move magnitude (+0x7C, index 0)
    float look_stick[2]{};            // resolved slot-0 right stick, block +0x08/+0x0C
    std::uint64_t input_buttons = 0;  // complete resolved button field, block +0x518
    std::uint64_t buttons_pressed = 0;
    std::uint64_t buttons_released = 0;
    std::int32_t health = -1;         // Player HealthSystem current HP
    std::int32_t max_health = -1;
    std::uint8_t weapon_snapshot[kWeaponSnapshotBytes]{};
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
