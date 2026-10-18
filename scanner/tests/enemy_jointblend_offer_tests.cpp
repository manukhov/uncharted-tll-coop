#include "enemy_jointblend_offer.h"
#include <cstdio>

namespace o = coop::enemy_jointblend_offer;
namespace {
unsigned checks{}, failures{};
void check(bool value, const char* why) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", why); } }
o::Offer fixture() {
    o::Offer v{}; v.pid = 123; v.creation = 456; v.nonce = {789, 1011};
    v.main = 0x140000000; v.main_size = o::prep::kRetailImageSize; v.self = 0x180000000; v.self_size = 0x20000;
    v.frequency = 10000000; v.entry = v.self+0x1000; v.page = v.self+0x4000; v.binding_page = v.self+0x5000;
    for (std::size_t i = 0; i < v.originals.size(); ++i) v.originals[i] = v.main+o::prep::kFallbacks[i].rva;
    for (std::size_t i = 0; i < v.hooks.size(); ++i) { v.hooks[i] = v.self+0x2000+0x100*i; v.relays[i] = 0x150000000+0x10000*i; }
    v.hooks[1] = v.hooks[0]; v.postpose = v.self+0x3000; return v;
}
}
int main() {
    const auto good = fixture(); const auto wire = o::encode(good);
    check(o::valid(good) && o::decode(wire) == good, "fixed little-endian wire round trip");
    check(wire[0] == 'E' && wire[4] == 'O' && wire[7] == '1' && wire[8] == 1, "explicit byte order, magic and version");
    for (std::size_t n = 0; n < wire.size(); ++n) check(!o::decode(std::span(wire).first(n)), "every truncated length refuses");
    std::array<std::uint8_t, o::kBytes+1> long_wire{};
    std::copy(wire.begin(), wire.end(), long_wire.begin()); check(!o::decode(long_wire), "trailing payload refuses");
    for (const auto index : {0, 8, 16, 24}) { auto bad = wire; bad[index] ^= 1; check(!o::decode(bad), "magic/version/size/contract mismatch refuses"); }
    for (std::size_t i = 37*8; i < wire.size(); ++i) { auto bad = wire; bad[i] = 1; check(!o::decode(bad), "every reserved byte refuses"); }
    auto refuse = [&](o::Offer bad, const char* why) { check(!o::valid(bad) && !o::decode(o::encode(bad)), why); };
    auto bad = good; bad.pid = UINT64_MAX; refuse(bad, "PID width");
    bad = good; bad.nonce = {}; refuse(bad, "empty nonce");
    bad = good; bad.creation = 0; refuse(bad, "empty creation");
    bad = good; bad.frequency = UINT64_MAX; refuse(bad, "frequency overflow");
    bad = good; ++bad.main; refuse(bad, "main alignment");
    bad = good; bad.main_size = 4096; refuse(bad, "retail extent");
    bad = good; bad.self = UINT64_MAX-4095; refuse(bad, "image overflow");
    bad = good; bad.self = bad.main; refuse(bad, "overlapping images");
    bad = good; ++bad.self_size; refuse(bad, "self page alignment");
    bad = good; bad.entry = bad.page; refuse(bad, "entry in data");
    bad = good; bad.postpose = bad.entry; refuse(bad, "postpose/entry alias");
    bad = good; ++bad.page; refuse(bad, "offer page alignment");
    bad = good; bad.page = bad.binding_page; refuse(bad, "two formats cannot share page");
    bad = good; bad.binding_page = bad.self+bad.self_size; refuse(bad, "binding outside DLL");
    for (std::size_t i = 0; i < good.originals.size(); ++i) { bad = good; ++bad.originals[i]; refuse(bad, "each native fallback pinned to exact RVA"); }
    for (std::size_t i = 0; i < good.hooks.size(); ++i) {
        bad = good; bad.hooks[i] = bad.postpose; refuse(bad, "hook/postpose alias");
        bad = good; bad.hooks[i] = bad.page; refuse(bad, "hook in data");
        bad = good; bad.relays[i] = bad.main+0x10000; refuse(bad, "relay inside main");
        bad = good; bad.relays[i] = bad.self; refuse(bad, "relay inside DLL");
        bad = good; bad.relays[i] = 0x700000000000; refuse(bad, "relay outside rel32");
        bad = good; ++bad.relays[i]; refuse(bad, "relay reservation alignment");
    }
    bad = good; bad.hooks[1] += 16; refuse(bad, "ingress hooks differ");
    bad = good; bad.hooks[4] = bad.hooks[3]; refuse(bad, "non-ingress hook alias");
    bad = good; bad.relays[1] = bad.relays[0]; refuse(bad, "duplicate relay");
    const auto binding = o::binding(good);
    check(o::from_binding(binding, good.relays, good.nonce, good.entry, good.page, good.binding_page) == good,
        "legacy binding conversion leaves wire/version independent");
    check(!o::kRuntimeAdmission, "syntax is not runtime admission");
    std::printf("enemy offer codec: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
