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

}  // namespace coop
