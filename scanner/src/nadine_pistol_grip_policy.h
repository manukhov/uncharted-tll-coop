#pragma once

#include <array>
#include <cstdint>

// Pure, build-pinned overlay-row admission. The runtime caller must first prove
// the retained local legacy-Nadine owner, its active owned weapon overlay set,
// and the native pose-consumer/cursor context. This file grants no authority to
// alter assets, replace final clip IDs, copy poses, or admit other weapons.
namespace coop::pistol_grip {

inline constexpr std::uint32_t kP99ArtId = 0x8075397Du;
inline constexpr std::uint32_t kBurstPistolArtId = 0xB6B0DD17u;
inline constexpr std::uint32_t kP99WeaponSetId = 0x43DC560Cu;
inline constexpr std::uint32_t kBurstWeaponSetId = 0x926FDDA0u;
inline constexpr std::uint32_t kCampaignChloeSetId = 0x4C9FB65Cu;
inline constexpr std::uint32_t kCampaignChloeSelector = 0x3F334972u;

inline constexpr bool supported_art(std::uint32_t art) noexcept {
    return art == kP99ArtId || art == kBurstPistolArtId;
}

inline constexpr std::uint32_t expected_weapon_set(std::uint32_t art) noexcept {
    return art == kP99ArtId ? kP99WeaponSetId :
        (art == kBurstPistolArtId ? kBurstWeaponSetId : 0u);
}

enum class GripRowKind : std::uint8_t {
    kNone,
    kPistolRightHand,
    kBurstRightHand,
    kBurstAimHands,
    kPistolAimHands,
};

struct RowEvidence {
    std::uint32_t art_id = 0;
    std::uint32_t set_id = 0;
    std::uint32_t selector = 0;
    std::uint32_t source = 0;
    std::uint32_t target = 0;
    // Seven exact DWORDs at native overlay-row +08, +0C, ... +20.
    std::array<std::uint32_t, 7> payload{};
    std::uint8_t type = 0;
    std::uint8_t byte25 = 0;
    std::uint16_t flags = 0;
    std::uint8_t cursor_kind = 0;
    std::uint8_t cursor_flags = 0;
};

// STATIC dc1/player-mesh-overlays.bin: set 4C9FB65C, selector 3F334972,
// table DFA38, 72 unique consecutive rows E2620..E3138 (stride 28 hex).
// Every listed source targets 68238D0B, type 1, flags 0, payload +08..20 zero.
// None of these sources/that target occur in the authored vill-Nadine set
// 6567DEA7 (selector 41666B5A). This is not a blanket target-ID allowlist.
inline constexpr std::array<std::uint32_t, 72> kPistolAimSources{
    0x9DCD31BCu, 0x6B59254Eu, 0x418147E1u, 0x311E3C9Bu,
    0x1E4917C2u, 0x5DF463F2u, 0x6ED66CB8u, 0x9EEDC65Au,
    0x2E7D04E7u, 0x781DFA12u, 0x08828168u, 0x27D5AA31u,
    0x1D147D10u, 0x574AD14Bu, 0xDE0DD8B8u, 0x79A1F334u,
    0x18E4B240u, 0xAA3393F5u, 0x3B7C43EFu, 0xC5AECB10u,
    0x7779EAA5u, 0x31561847u, 0x391F7D3Bu, 0x8BC85C8Eu,
    0xE549AED2u, 0x26E403E0u, 0x94332255u, 0x95D6D5A8u,
    0xE8F5D295u, 0x5A22F320u, 0x64686F76u, 0xC49B9633u,
    0x764CB786u, 0xAE4A7F9Cu, 0x75D2BA1Cu, 0xC7059BA9u,
    0xE47CF3F1u, 0xEAA9CFF3u, 0x587EEE46u, 0x94E3888Bu,
    0x24B81E86u, 0x966F3F33u, 0xB658CFD2u, 0x97D7BF83u,
    0x25009E36u, 0x7CC7C621u, 0x4C9A4FECu, 0xFE4D6E59u,
    0xA9FC650Eu, 0xD6EA3A24u, 0x643D1B91u, 0x4C94252Du,
    0x8D13BF50u, 0x3FC49EE5u, 0xF60D5FE6u, 0x841C9342u,
    0x36CBB2F7u, 0x8692249Cu, 0x4A0D4237u, 0xF8DA6382u,
    0x772C9E42u, 0x66630691u, 0xD4B42724u, 0x1A46BDF7u,
    0x481DDA1Cu, 0xFACAFBA9u, 0x46846353u, 0xC03624B0u,
    0x72E10505u, 0x361B1829u, 0x0E27F5C5u, 0xBCF0D470u,
};

inline constexpr bool is_pistol_aim_source(std::uint32_t source) noexcept {
    for (const std::uint32_t candidate : kPistolAimSources) {
        if (source == candidate) {
            return true;
        }
    }
    return false;
}

inline constexpr GripRowKind classify_row(const RowEvidence& row) noexcept {
    if (!supported_art(row.art_id) || row.type != 1 || row.byte25 != 0 ||
        row.flags != 0 || row.cursor_kind != 0 || row.cursor_flags != 0) {
        return GripRowKind::kNone;
    }
    for (const std::uint32_t value : row.payload) {
        if (value != 0) {
            return GripRowKind::kNone;
        }
    }

    if (row.set_id == kCampaignChloeSetId &&
        row.selector == kCampaignChloeSelector) {
        // Row E25F8. Admit both owned pistol arts: set iteration order must not
        // decide whether the common Chloe additive precedes Colt's type-0 row.
        if (row.source == 0xD350A452u && row.target == 0xB8FC6B25u) {
            return GripRowKind::kPistolRightHand;
        }
        if (row.target == 0x68238D0Bu && is_pistol_aim_source(row.source)) {
            return GripRowKind::kPistolAimHands;
        }
    }

    // STATIC player-specific-weapon-overlays.bin rows 36C78/36CA0 in the
    // B6B0DD17-owned set. Preserve its type-0 D350A452 -> 9A64704F row 35EB8:
    // the Colt pose is distinct from P99 and must never be replaced with it.
    if (row.art_id == kBurstPistolArtId && row.set_id == kBurstWeaponSetId &&
        row.selector == kBurstPistolArtId) {
        if (row.source == 0x9A64704Fu && row.target == 0xB8FC6B25u) {
            return GripRowKind::kBurstRightHand;
        }
        if (row.source == 0x522E6370u && row.target == 0x1CB03721u) {
            return GripRowKind::kBurstAimHands;
        }
    }
    return GripRowKind::kNone;
}

inline constexpr const char* row_kind_name(GripRowKind kind) noexcept {
    switch (kind) {
    case GripRowKind::kPistolRightHand: return "pistol-right-hand";
    case GripRowKind::kBurstRightHand: return "burst-right-hand";
    case GripRowKind::kBurstAimHands: return "burst-aim-hands";
    case GripRowKind::kPistolAimHands: return "pistol-aim-hands";
    default: return "none";
    }
}

} // namespace coop::pistol_grip
