#pragma once
#include "enemy_jointblend_native_contract.h"
#include <type_traits>

// Local, dormant binding only. NOT an external install offer, a whole mapped
// code certificate, or permission to arm/patch/release a debugger stop.
namespace coop::enemy_jointblend_prepare {
namespace native = enemy_jointblend_native_contract;
using Address = std::uint64_t;
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint64_t kMagic = 0x31444E424A4D4E45; // ENMJBND1
inline constexpr std::uint32_t kRetailStamp = 0x649F75A0;
inline constexpr std::uint32_t kRetailImageSize = 0x07047000;
inline constexpr std::uint64_t kRetailFileSize = 60851712;
inline constexpr char kRetailSha256[] =
    "ed1371a4b47961abc160e92aa0b9bdfd787638fa1a39ba2403b423ab45c210be";
inline constexpr bool kRuntimeAdmission = false;

enum class Result : std::uint32_t {
    dormant_bound = 1, reused, input_refused, identity_refused, pin_refused,
    image_refused, retail_file_refused, contracts_refused, resources_refused,
    configuration_refused, publication_refused,
};
struct Request {
    std::uint32_t size = sizeof(Request), version = kVersion, pid{}, reserved{};
    std::uint64_t creation{}, main{}, self{}, reserved64{};
};
static_assert(sizeof(Request) == 48 && std::is_trivially_copyable_v<Request>);
constexpr bool valid_request(const Request& r) noexcept {
    return r.size == sizeof(Request) && r.version == kVersion && r.pid &&
        r.creation && r.main && r.self && r.main != r.self && !r.reserved && !r.reserved64;
}

// Contains no collector pointers, caller-supplied callbacks, relay addresses,
// arm token or executable release authorization. Zero until explicit prepare.
// The full dedicated 4096-byte page must be RO and match a successful response;
// a magic/prefix alone is never acceptance (publication can fail after effect).
struct Binding {
    std::uint64_t magic{};
    std::uint32_t size{}, version{}, pid{}, reserved{};
    std::uint64_t creation{}, main{}, main_size{}, self{}, self_size{}, frequency{};
    std::array<Address, 6> originals{}; // JointBlend, Reset, Process, GAME postpose, Primary, Store
    std::array<Address, 7> hooks{};
    Address postpose{};
};
static_assert(sizeof(Binding) == 184 && offsetof(Binding, originals) == 72 &&
    offsetof(Binding, hooks) == 120 && offsetof(Binding, postpose) == 176);
struct alignas(4096) Page {
    Binding binding{};
    std::array<std::uint8_t, 4096 - sizeof(Binding)> padding{};
};
static_assert(sizeof(Page) == 4096 && std::is_trivially_copyable_v<Page>);
using Entry = std::uint32_t(__cdecl*)(const Request*, std::uint32_t) noexcept;

struct Fallback {
    std::uint32_t rva;
    std::array<std::uint8_t, 32> prefix;
};
// Exact byte prefixes rechecked against the SHA256-pinned retail file above.
// Relative instructions need no base fixups. These are not transitive closure.
inline constexpr std::array<Fallback, 6> kFallbacks{{
    {0x1A92A00, {0x48,0x8B,0xC4,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xD0,
        0x05,0x00,0x00,0x48,0xC7,0x80,0x48,0xFB,0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0x48,0x89}},
    {0x16878F0, {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0xFA,
        0x48,0x8B,0x89,0xC8,0x00,0x00,0x00,0x80,0x79,0x16,0x00,0x76,0x0A,0x8B,0x41,0x2C}},
    {0x1AB97C0, {0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,
        0x90,0xEA,0xFF,0xFF,0xB8,0x70,0x16,0x00,0x00,0xE8,0x32,0x27,0xA3,0x00,0x48,0x2B}},
    {native::kGamePostposeRva, {0x40,0x57,0x48,0x83,0xEC,0x60,0x48,0xC7,0x44,0x24,0x30,0xFE,0xFF,0xFF,0xFF,0x48,
        0x89,0x5C,0x24,0x70,0x41,0x81,0xF9,0xDB,0x63,0x0F,0xAE,0x77,0x6A,0x74,0x24,0x41}},
    {0x1E5AEE0, {0x48,0x83,0x3D,0x38,0xCD,0xAB,0x04,0x00,0x48,0x8B,0xD1,0x74,0x64,0x0F,0xB6,0x41,
        0x02,0xA8,0x20,0x74,0x44,0x45,0x33,0xC0,0x44,0x39,0x41,0x10,0x76,0x36,0x66,0x90}},
    {0x1CF3790, {0x8B,0x81,0xA0,0x00,0x00,0x00,0x84,0xC0,0x78,0x58,0xA8,0x40,0x74,0x15,0xC5,0xF8,
        0x10,0x01,0xC5,0xF8,0x11,0x41,0x20,0xC5,0xF8,0x10,0x49,0x10,0xC5,0xF8,0x11,0x49}},
}};

// Reader copies RVA ranges, requiring executable nonwritable image pages for
// code. Shared by the live LOCAL check and a nonexecuting retail-file audit.
template<class Reader>
bool code_contracts(Reader& read) noexcept {
    for (const auto& fallback : kFallbacks) {
        std::array<std::uint8_t, 32> bytes{};
        if (!read(fallback.rva, bytes.data(), bytes.size()) || bytes != fallback.prefix) return false;
    }
    for (const auto& site : native::kSites) {
        std::array<std::uint8_t, 7> bytes{};
        if (!read(site.rva, bytes.data(), site.length)) return false;
        for (std::size_t i = 0; i < site.length; ++i) if (bytes[i] != site.stock[i]) return false;
    }
    return true;
}
}
