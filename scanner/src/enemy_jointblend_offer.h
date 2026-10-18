#pragma once
#include "enemy_jointblend_prepare.h"
#include <algorithm>
#include <optional>
#include <span>

namespace coop::enemy_jointblend_offer {
namespace prep = enemy_jointblend_prepare;
namespace native = enemy_jointblend_native_contract;
using Address = native::Address;
inline constexpr std::uint64_t kMagic = 0x3152464F4A4D4E45; // ENMJOFR1
inline constexpr std::size_t kWords = 48, kBytes = kWords*8;
inline constexpr bool kRuntimeAdmission = false;
using Wire = std::array<std::uint8_t, kBytes>;
// All fields are owned transport VALUES. No HANDLE, FLS index, C++ ownership,
// mutable collector pointer, game object, opcodes or write permission on wire.
// Nonce separates preparations, NOT authentication or proof of configuration.
struct Offer {
    std::uint64_t pid{}, creation{};
    std::array<std::uint64_t, 2> nonce{};
    Address main{}, main_size{}, self{}, self_size{}, frequency{};
    Address entry{}, page{}, binding_page{};
    std::array<Address, 6> originals{};
    std::array<Address, 7> hooks{};
    Address postpose{};
    std::array<Address, 7> relays{};
    bool operator==(const Offer&) const = default;
};
struct alignas(4096) Page { Wire wire{}; std::array<std::uint8_t, 4096-kBytes> reserved{}; };
static_assert(sizeof(Page) == 4096 && std::is_trivially_copyable_v<Page>);
inline bool range(Address base, Address bytes) noexcept {
    return base && bytes && base < INT64_MAX && bytes <= INT64_MAX-base;
}
inline bool overlap(Address a, Address an, Address b, Address bn) noexcept { return a < b+bn && b < a+an; }
// Syntax only; does not establish that any claimed image/address/ABI is real.
inline bool valid(const Offer& o) noexcept {
    if (!o.pid || o.pid > UINT32_MAX || !o.creation || !(o.nonce[0] | o.nonce[1]) ||
        o.frequency < 1000 || o.frequency > INT64_MAX || o.main_size != prep::kRetailImageSize ||
        !range(o.main, o.main_size) || !range(o.self, o.self_size) || o.self_size > UINT32_MAX ||
        (o.main & 0xFFFF) || (o.self & 0xFFFF) || (o.self_size & 4095) ||
        overlap(o.main, o.main_size, o.self, o.self_size)) return false;
    const native::ModuleImage dll{o.self, o.self_size};
    const auto inside = [&](Address a, Address n) { return a >= o.self && native::contains_rva(dll, a-o.self, n); };
    if (!inside(o.entry, 1) || !inside(o.postpose, 1) || !inside(o.page, 4096) || !inside(o.binding_page, 4096) ||
        (o.page & 4095) || (o.binding_page & 4095) || o.page == o.binding_page || o.entry == o.postpose ||
        o.hooks[0] != o.hooks[1]) return false;
    const auto outside_data = [&](Address a) {
        return !overlap(a, 1, o.page, 4096) && !overlap(a, 1, o.binding_page, 4096);
    };
    if (!outside_data(o.entry) || !outside_data(o.postpose)) return false;
    for (std::size_t i = 0; i < o.originals.size(); ++i)
        if (o.originals[i] != o.main+prep::kFallbacks[i].rva) return false;
    for (std::size_t i = 0; i < o.hooks.size(); ++i) {
        const auto h = o.hooks[i], r = o.relays[i];
        if (!inside(h, 1) || !outside_data(h) || h == o.entry || h == o.postpose ||
            !range(r, 4096) || (r & 0xFFFF) || overlap(r, 4096, o.main, o.main_size) ||
            overlap(r, 4096, o.self, o.self_size) ||
            !native::encode_rel32(o.main+native::kSites[i].instruction_end_rva, r).valid) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (r == o.relays[j] || (h == o.hooks[j] && !(i == 1 && j == 0))) return false;
    }
    return true;
}
inline Wire encode(const Offer& o) noexcept {
    std::array<std::uint64_t, kWords> words{kMagic, 1, kBytes, 1, o.pid, o.creation, o.nonce[0], o.nonce[1],
        o.main, o.main_size, o.self, o.self_size, o.frequency, o.entry, o.page, o.binding_page};
    std::copy(o.originals.begin(), o.originals.end(), words.begin()+16);
    std::copy(o.hooks.begin(), o.hooks.end(), words.begin()+22);
    words[29] = o.postpose;
    std::copy(o.relays.begin(), o.relays.end(), words.begin()+30); // words37..47 reserved zero
    Wire wire{};
    for (std::size_t i = 0; i < words.size(); ++i) for (unsigned b = 0; b < 8; ++b)
        wire[8*i+b] = static_cast<std::uint8_t>(words[i] >> (8*b));
    return wire;
}
inline std::optional<Offer> decode(std::span<const std::uint8_t> wire) noexcept {
    if (wire.size() != kBytes) return {};
    std::array<std::uint64_t, kWords> words{};
    for (std::size_t i = 0; i < words.size(); ++i) for (unsigned b = 0; b < 8; ++b)
        words[i] |= std::uint64_t{wire[8*i+b]} << (8*b);
    if (words[0] != kMagic || words[1] != 1 || words[2] != kBytes || words[3] != 1) return {};
    for (std::size_t i = 37; i < words.size(); ++i) if (words[i]) return {};
    Offer o{};
    o.pid = words[4]; o.creation = words[5]; o.nonce = {words[6], words[7]};
    o.main = words[8]; o.main_size = words[9]; o.self = words[10]; o.self_size = words[11];
    o.frequency = words[12]; o.entry = words[13]; o.page = words[14]; o.binding_page = words[15];
    std::copy_n(words.begin()+16, 6, o.originals.begin()); std::copy_n(words.begin()+22, 7, o.hooks.begin());
    o.postpose = words[29]; std::copy_n(words.begin()+30, 7, o.relays.begin());
    return valid(o) ? std::optional{o} : std::nullopt;
}
inline prep::Binding binding(const Offer& o) noexcept {
    prep::Binding b{};
    b.magic = prep::kMagic; b.size = sizeof(b); b.version = prep::kVersion;
    b.pid = static_cast<std::uint32_t>(o.pid); b.creation = o.creation;
    b.main = o.main; b.main_size = o.main_size; b.self = o.self; b.self_size = o.self_size;
    b.frequency = o.frequency; b.originals = o.originals; b.hooks = o.hooks; b.postpose = o.postpose;
    return b;
}
inline Offer from_binding(const prep::Binding& b, const std::array<Address, 7>& relays,
    const std::array<std::uint64_t, 2>& nonce, Address entry, Address page, Address binding_page) noexcept {
    return {b.pid, b.creation, nonce, b.main, b.main_size, b.self, b.self_size, b.frequency,
        entry, page, binding_page, b.originals, b.hooks, b.postpose, relays};
}
}
