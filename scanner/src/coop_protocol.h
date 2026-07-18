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
constexpr std::uint16_t kStatePacketVersion = 10;

// StatePacket.flags extension bits (v7+):
// 0x0100 — camera_quat mirrors sender CameraManager(0) @+0x1B0.
constexpr std::uint16_t kStateFlagPeerCamera = 0x0100;
// 0x0200 (v8) — pose_seed carries the sender's screen-0 camera POSE SEED
// (CameraManager(0) -> *(mgr+0xB0) -> +0x60): 32 bytes = position float4 +
// orientation quat float4. This is the FIRE/AIM ray source
// (ResolveScreenRoutePoseSeedOrDefault); the receiver overrides the Second
// player's screen-1 seed with it so shots/aim follow the peer's crosshair.
constexpr std::uint16_t kStateFlagPoseSeed = 0x0200;
// 0x0400 — free-look head sync: camera_quat valid while sender orbits camera
// without ADS (|yaw_cam - yaw_body| > threshold). Distinct from 0x0100 (ADS aim).
constexpr std::uint16_t kStateFlagPeerFreeLook = 0x0400;
// 0x0800 (v8+) — peer is genuinely in cover (Player+0x1510 bit0 / is-in-cover?).
constexpr std::uint16_t kStateFlagPeerInCover = 0x0800;
// 0x1000 — one-shot cover-enter command edge (not try-enter-cover approach).
constexpr std::uint16_t kStateFlagPeerCoverEnterPulse = 0x1000;
// 0x2000 (v9) - exact world quaternion captured from the sender local Player's
// FgAnimData_PublishWorldTransform input. The receiver substitutes it at the same
// puppet publication boundary, avoiding camera-axis and local-root conversion.
constexpr std::uint16_t kStateFlagFgAnimWorldQuat = 0x2000;
// 0x4000 (v10) - post-blend native aim-add up/down channel values.
constexpr std::uint16_t kStateFlagAimPitch = 0x4000;
// 0x8000 (v10) - native GunMove stationary turn selector inputs/results.
constexpr std::uint16_t kStateFlagAimFootTurn = 0x8000;

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
    float camera_quat[4]{};           // sender gameplay camera rotation (v7, flag 0x100)
    float pose_seed[8]{};             // v8 flag 0x200: screen-0 pose seed pos[4]+quat[4]
    float fganim_world_quat[4]{};      // v9 flag 0x2000: exact FgAnim world quat
    std::uint64_t input_buttons = 0;  // complete resolved button field, block +0x518
    std::uint64_t buttons_pressed = 0;
    std::uint64_t buttons_released = 0;
    std::int32_t health = -1;         // Player HealthSystem current HP
    std::int32_t max_health = -1;
    // Flag 0x80: held_weapon_art_id + gun_state are valid on the sender.
    // gun_state: 0 = GunHolstered, 1 = GunOut (SkillWaitOnGunState convention).
    std::uint32_t held_weapon_art_id = 0;
    std::uint8_t gun_state = 0xFF;
    // Actual slot attached to rightHand/leftHand on the sender. The field name is
    // retained for wire compatibility with the unfinished version-6 draft.
    std::uint8_t weapon_selected_mode = 0;
    std::uint16_t held_weapon_reserved = 0;
    std::uint8_t weapon_snapshot[kWeaponSnapshotBytes]{};
    // v10 extension. Keeping it at the end preserves the complete v9 prefix.
    float aim_pitch_up = 0.0F;
    float aim_pitch_down = 0.0F;
    float gun_move_turn_direction[4]{};
    float anim_turn_yaw_rate = 0.0F;
    std::int16_t gun_move_current_child = -1;
    std::int16_t gun_move_selected_child = -1;
};

static_assert(sizeof(StatePacket) == 504);
static_assert(offsetof(StatePacket, fganim_world_quat) == 144);
static_assert(offsetof(StatePacket, aim_pitch_up) == 472);
static_assert(offsetof(StatePacket, gun_move_turn_direction) == 480);

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

// One-shot pickup-animation relay. PickupInteractControl::RequestInteract marks
// the start of the authored pickup action. The receiver resolves its matching
// WeaponPickup and publishes only player-requested-interact for the puppet; world
// pickup consumption and weapon inventory remain owned by WeaponGivePacket.
constexpr std::uint32_t kPickupPacketMagic = 0x4B434950;  // "PICK"
constexpr std::uint16_t kPickupPacketVersion = 4;
constexpr std::uint16_t kPickupFlagControllerStartArgs = 0x0001;

struct PickupPacket {
    std::uint32_t magic = kPickupPacketMagic;
    std::uint16_t version = kPickupPacketVersion;
    std::uint16_t flags = 0;
    std::uint32_t sequence = 0;
    std::uint32_t pickup_object_id = 0;  // WeaponPickup NdGameObject runtime id
    std::uint32_t weapon_art_id = 0;     // validates the same object cross-copy
    // Context-replay pickup animation (2026-07-12): the two ids Player_Publish
    // RequestedInteract 0x140C87D70 reads from its interactable arg (pickup+0x38
    // class id, pickup+0xD8 name id). The receiver publishes the player-requested-
    // interact descriptor into the puppet's actor slot with these, under a
    // ContextProcess=puppet TLS scope, so the puppet's own anim graph bends it.
    std::uint32_t interact_class_id = 0;
    std::uint32_t interact_name_id = 0;
    // v4: exact POD arg3 consumed by PlayerPickupController::Start/Queue
    // (vtable+0x38, 0x140F49CA0). Receiver queues it on puppet+0x1EB8 with
    // object=null, which the native function explicitly supports.
    std::uint8_t controller_start_args[32]{};
};

// One-shot picked-up-weapon give relay (EVENT semantics, 2026-07-06 night).
// Sender: GiveWeapon+9 diagnostic detour observes the script-event give
// (ret_rva 0xDD4208, local player controller) and captures the REAL 0xC0
// WeaponInfo block. Runtime dump proved the block is pure POD (world-space drop
// transform at +0x00..0x1F, def/name hashes, ammo counts at +0x62/+0x84, byte
// flags) - no pointers, so a raw cross-process copy is valid. Receiver gives it
// to the puppet ONCE via controller vt+0x158 (seq-deduped, no retry loop - the
// retry loops were the "weapon fountain" root in all three prior attempts).
constexpr std::uint32_t kWeaponGiveMagic = 0x47504F43;  // "COPG"
constexpr std::uint16_t kWeaponGiveVersion = 2;
constexpr std::size_t kWeaponInfoBytes = 0xC0;

struct WeaponGivePacket {
    std::uint32_t magic = kWeaponGiveMagic;
    std::uint16_t version = kWeaponGiveVersion;
    std::uint8_t draw_flag = 0;  // GiveWeapon a3 as observed on the sender
    std::int8_t manager_slot = -1;  // sender find_weapon_slot_by_art_id at capture
    std::uint32_t sequence = 0;
    std::uint32_t weapon_art_id = 0;  // info+0x80, for logging/sanity
    std::uint8_t weapon_info[kWeaponInfoBytes]{};
};

// One-shot fire relay: local PlayerFireController::Fire captured on the sender
// and replayed on the remote retained Player via the same native entry point.
constexpr std::uint32_t kFirePacketMagic = 0x45524946;  // "FIRE"
constexpr std::uint16_t kFirePacketVersion = 5;
constexpr std::uint16_t kFireFlagFinalDirection = 0x0001;
constexpr std::uint16_t kFireFlagAmmoSnapshot = 0x0002;
constexpr std::uint16_t kFireFlagFinalOrigin = 0x0004;
constexpr std::size_t kFireArgsBytes = 0x100;

struct FirePacket {
    std::uint32_t magic = kFirePacketMagic;
    std::uint16_t version = kFirePacketVersion;
    std::uint16_t flags = 0;
    std::uint32_t sequence = 0;
    // Sender Player -> PlayerFireController byte offset at fire time (Chloe 0x15cf30,
    // Nadine 0x15adb0). Zero on version-1 packets => receiver uses known fallbacks.
    std::uint32_t controller_offset = 0;
    std::uint32_t weapon_art_id = 0;  // Weapon+0x860 on the sender
    double scalar = 0.0;
    std::uint8_t fire_args[kFireArgsBytes]{};
    // v3: sender's exact selected SpawnInfo+0x28 launch direction. Receiver
    // writes it to the selected record +0x30/+0x40 before SpawnProjectile.
    float final_direction[4]{};
    // v4: authoritative live ProcessWeapon ammo immediately before this shot.
    // The receiver writes it only to the retained puppet's matching weapon;
    // native fire then consumes exactly one round locally.
    std::int16_t magazine_before = -1;
    std::int16_t reserve_before = -1;
    std::uint32_t ammo_reserved = 0;
    // v5: sender's exact selected SpawnInfo+0x28 launch origin. Receiver writes
    // it to selected-record +0x20 (instant collision start) and +0x70 (base
    // projectile/tracer origin) before native projectile initialization.
    float final_origin[4]{};
};

static_assert(offsetof(FirePacket, final_direction) == 288);
static_assert(offsetof(FirePacket, magazine_before) == 304);
static_assert(offsetof(FirePacket, final_origin) == 312);
static_assert(sizeof(FirePacket) == 328);

// Native NetEventEmitGrenade payload relay. The stock event serializer covers
// bytes +0x18..+0xB5 of the 0xC0-byte event; these fields are POD and its Execute
// method owns the complete ProcessSpawnInfo/descriptor materialization path.
constexpr std::uint32_t kGrenadeEventMagic = 0x454E5247;  // "GRNE"
constexpr std::uint16_t kGrenadeEventVersion = 3;
constexpr std::size_t kGrenadeEventPayloadOffset = 0x18;
constexpr std::size_t kGrenadeEventPayloadBytes = 0x9E;

struct GrenadeEventPacket {
    std::uint32_t magic = kGrenadeEventMagic;
    std::uint16_t version = kGrenadeEventVersion;
    std::uint16_t reserved = 0;
    std::uint32_t sequence = 0;
    std::uint32_t grenade_art_id = 0;
    std::uint8_t event_payload[kGrenadeEventPayloadBytes]{};
    // v2: the flying grenade's live NdGameObject transform captured at send time
    // (pos @+0x240, velocity @+0x260). The receiver force-writes velocity onto the
    // spawned grenade after Execute so it flies the arc instead of dropping+detonating
    // at the spawn point (the serialized event alone did not carry usable velocity).
    float pos[4]{};
    float vel[4]{};
    // v3: orientation captured at send time (quat @+0x140 on ProjectileGrenade).
    float quat[4]{};
};

// In-flight kinematics stream: host authoritative pos/vel samples (~16 Hz) after the
// initial GRNE spawn event. Peer physics does not integrate relayed velocity alone.
constexpr std::uint32_t kGrenadeKinematicsMagic = 0x4B4E5247;  // "GRNK"

struct GrenadeKinematicsPacket {
    std::uint32_t magic = kGrenadeKinematicsMagic;
    std::uint32_t sequence = 0;
    float pos[4]{};
    float vel[4]{};
};

// SimpleBreakable wall/env break relay (native NetEventBreakableAttack*WithResults POD).
constexpr std::uint32_t kBreakableExplosionMagic = 0x574B5242;   // "BRKW"
constexpr std::uint32_t kBreakableProjectileMagic = 0x504B5242;  // "BRKP"
constexpr std::uint16_t kBreakableEventPacketVersion = 2;
constexpr std::size_t kBreakableExplosionEventBytes = 0x130;     // 304
constexpr std::size_t kBreakableProjectileEventBytes = 0xB0;     // 176
constexpr std::size_t kBreakableExplosionPlainEventBytes = 0xE0; // 224
constexpr std::size_t kBreakableProjectilePlainEventBytes = 0x60; // 96
constexpr std::uint8_t kBreakableNetEventTypeExplosionWithResults = 0x3F;
constexpr std::uint8_t kBreakableNetEventTypeProjectileWithResults = 0x3D;
constexpr std::uint8_t kBreakableNetEventTypeExplosionPlain = 0xFE;
constexpr std::uint8_t kBreakableNetEventTypeProjectilePlain = 0xFC;
constexpr std::uint32_t kBreakableNetEventTagExplosionPlain = 16842814u;
constexpr std::uint32_t kBreakableNetEventTagProjectilePlain = 16842812u;
constexpr std::size_t kBreakableSyncEventBytes = 0x60;             // 96
constexpr std::uint8_t kBreakableNetEventTypeSync = 0x6B;
constexpr std::uint32_t kBreakableNetEventTagSync = 0x0101006Bu;
constexpr std::uint32_t kBreakableSyncMagic = 0x534B5242;        // "BRKS"

struct BreakableSyncPacket {
    std::uint32_t magic = kBreakableSyncMagic;
    std::uint16_t version = kBreakableEventPacketVersion;
    std::uint8_t event_kind = kBreakableNetEventTypeSync;
    std::uint8_t reserved = 0;
    std::uint32_t sequence = 0;
    std::uint32_t net_object_id = 0;
    std::uint32_t scene_hash = 0;  // v2: SimpleBreakable+0x38 stable scene id
    std::uint8_t event_blob[kBreakableSyncEventBytes]{};
    float world_pos[3]{};  // v2: host SimpleBreakable world anchor for peer lookup
};

struct BreakableExplosionPacket {
    std::uint32_t magic = kBreakableExplosionMagic;
    std::uint16_t version = kBreakableEventPacketVersion;
    std::uint8_t event_kind = 0;
    std::uint8_t reserved = 0;
    std::uint32_t sequence = 0;
    std::uint32_t net_object_id = 0;
    std::uint8_t event_blob[kBreakableExplosionEventBytes]{};
};

struct BreakableProjectilePacket {
    std::uint32_t magic = kBreakableProjectileMagic;
    std::uint16_t version = kBreakableEventPacketVersion;
    std::uint8_t event_kind = 0;
    std::uint8_t reserved = 0;
    std::uint32_t sequence = 0;
    std::uint32_t net_object_id = 0;
    std::uint8_t event_blob[kBreakableProjectileEventBytes]{};
};

// Version 1 wire size: no controller_offset field (weapon_art_id immediately after sequence).
constexpr std::size_t kFirePacketV1Bytes =
    offsetof(FirePacket, controller_offset) + sizeof(std::uint32_t) +
    sizeof(double) + kFireArgsBytes;
// Version 3 ended immediately after final_direction.
constexpr std::size_t kFirePacketV3Bytes =
    offsetof(FirePacket, magazine_before);
// Version 4 ended immediately before final_origin.
constexpr std::size_t kFirePacketV4Bytes =
    offsetof(FirePacket, final_origin);

}  // namespace coop
