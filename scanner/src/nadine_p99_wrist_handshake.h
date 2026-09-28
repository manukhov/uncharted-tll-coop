#pragma once
#include "nadine_p99_wrist_code_snapshot.h"
#include <atomic>
#include <limits>

namespace coop::p99_wrist_handshake {
namespace wi=p99_wrist_install;
namespace cm=p99_wrist_code_manifest;
inline constexpr std::uint64_t kMagic=0x3150525453573950ULL; // P9WSTRP1
inline constexpr std::uint64_t kContract=0x0000000000000001ULL;
inline constexpr std::size_t kOfferWords=30, kOfferBytes=kOfferWords*8;

// Owned transport values, not pointers to target objects. No C++ struct layout,
// atomics, HANDLEs or FLS indices cross a process boundary. A nonce prevents stale
// sessions; it is NOT authentication. Publisher/consumer still need independently
// verified process, image/build, ABI entries, resource ownership and code evidence.
struct Offer {
    std::uint64_t pid=0,creation=0;
    std::array<std::uint64_t,2> nonce{};
    std::uint64_t module=0,image_bytes=0,dll=0,dll_bytes=0,frequency=0;
    std::array<std::uint64_t,3> originals{};
    std::array<std::uint64_t,5> hooks{},relays{};
    bool operator==(const Offer&) const = default;
};
using Wire=std::array<std::uint8_t,kOfferBytes>;

inline Wire encode(const Offer& o) noexcept {
    std::array<std::uint64_t,kOfferWords> words{
        kMagic,1,kOfferBytes,o.pid,o.creation,o.nonce[0],o.nonce[1],
        o.module,o.image_bytes,o.dll,o.dll_bytes,o.frequency};
    std::copy(o.originals.begin(),o.originals.end(),words.begin()+12);
    std::copy(o.hooks.begin(),o.hooks.end(),words.begin()+15);
    std::copy(o.relays.begin(),o.relays.end(),words.begin()+20);
    words[25]=kContract; // 26..29 reserved, required zero
    Wire wire{};
    for(std::size_t i=0;i<words.size();++i)
        for(unsigned b=0;b<8;++b) wire[i*8+b]=static_cast<std::uint8_t>(words[i]>>(b*8));
    return wire;
}

inline bool range(std::uint64_t base,std::uint64_t size) noexcept {
    constexpr auto max=static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max());
    return base && size && base<max && size<=max-base;
}
inline bool overlap(std::uint64_t a,std::uint64_t an,std::uint64_t b,std::uint64_t bn) noexcept {
    // Callers first validate both ranges.
    return a<b+bn && b<a+an;
}

// Reconstruct ONLY our five exact replacements. Wire fields never supply opcodes,
// sizes, page ownership, or a writable-address list. This is syntactic validation,
// not evidence that any declared allocation or native/ABI target actually exists.
inline std::optional<wi::Plan> plan(const Offer& o) noexcept {
    static_assert(sizeof(std::uintptr_t)==8,"Windows x64 protocol");
    if(!o.pid || o.pid>0xFFFFFFFFULL || !o.creation || !(o.nonce[0]|o.nonce[1]) ||
        o.frequency<1000 || o.frequency>static_cast<std::uint64_t>(INT64_MAX) ||
        !range(o.module,o.image_bytes) || !range(o.dll,o.dll_bytes) ||
        (o.module&0xFFFF) || (o.dll&0xFFFF) || (o.image_bytes&0xFFF) || (o.dll_bytes&0xFFF) ||
        o.image_bytes>0xFFFFFFFFULL || o.dll_bytes>0xFFFFFFFFULL ||
        overlap(o.module,o.image_bytes,o.dll,o.dll_bytes)) return {};
    for(const auto& r:cm::kRegions)
        if(r.rva>=o.image_bytes || r.byte_count>o.image_bytes-r.rva) return {};
    constexpr std::array<std::uint64_t,3> rvas{0x1A92A00,0x1AB97C0,0x217B630};
    for(std::size_t i=0;i<rvas.size();++i)
        if(rvas[i]>=o.image_bytes || o.originals[i]!=o.module+rvas[i]) return {};
    if(o.hooks[0]!=o.hooks[1]) return {}; // both ingress routes share Async ABI
    std::array<std::array<std::uint8_t,7>,5> stock{};
    std::array<std::uintptr_t,5> relays{},hooks{};
    for(std::size_t i=0;i<5;++i) {
        if(o.hooks[i]<o.dll || o.hooks[i]-o.dll>=o.dll_bytes ||
            !range(o.relays[i],wi::kPageBytes) || (o.relays[i]&0xFFF) ||
            overlap(o.relays[i],wi::kPageBytes,o.module,o.image_bytes) ||
            overlap(o.relays[i],wi::kPageBytes,o.dll,o.dll_bytes)) return {};
        for(std::size_t j=0;j<i;++j)
            if(o.relays[i]==o.relays[j] || (o.hooks[i]==o.hooks[j] && !(i==1 && j==0))) return {};
        stock[i]=wi::kSites[i].stock;
        relays[i]=static_cast<std::uintptr_t>(o.relays[i]);
        hooks[i]=static_cast<std::uintptr_t>(o.hooks[i]);
    }
    return wi::plan(static_cast<std::uintptr_t>(o.module),stock,relays,hooks);
}

inline std::optional<Offer> decode(std::span<const std::uint8_t> wire) noexcept {
    if(wire.size()!=kOfferBytes) return {};
    std::array<std::uint64_t,kOfferWords> words{};
    for(std::size_t i=0;i<words.size();++i)
        for(unsigned b=0;b<8;++b) words[i]|=std::uint64_t{wire[i*8+b]}<<(b*8);
    if(words[0]!=kMagic || words[1]!=1 || words[2]!=kOfferBytes || words[25]!=kContract ||
        words[26] || words[27] || words[28] || words[29]) return {};
    Offer o{};
    o.pid=words[3];o.creation=words[4];o.nonce={words[5],words[6]};
    o.module=words[7];o.image_bytes=words[8];o.dll=words[9];o.dll_bytes=words[10];o.frequency=words[11];
    std::copy_n(words.begin()+12,3,o.originals.begin());
    std::copy_n(words.begin()+15,5,o.hooks.begin());
    std::copy_n(words.begin()+20,5,o.relays.begin());
    if(!plan(o)) return {};
    return o;
}

enum class Completion : std::uint64_t {
    ResumedDetached=1, CleanRefusal=2, HeldUncertain=3, ContinuedDetachFailed=4
};
using AckWire=std::array<std::uint8_t,kOfferBytes+8>;
inline AckWire acknowledge(const Wire& offer,Completion outcome) noexcept {
    AckWire wire{};std::copy(offer.begin(),offer.end(),wire.begin());
    for(unsigned b=0;b<8;++b) wire[kOfferBytes+b]=static_cast<std::uint8_t>(static_cast<std::uint64_t>(outcome)>>(b*8));
    return wire;
}

enum class State {Empty,Preparing,Prepared,Verifying,Installed,Refused};
// LOCAL owner only. IPC publication/nonce generation and OS adapters are NOT
// implemented here. No remote boolean can arm a probe. All Ops are off-thread;
// no callbacks/loggers or permission to mutate game data are supplied by the wire.
//
// Ops contract:
// identity(offer): fresh current PID/creation/module/build/ABI identity.
// resources(offer,plan): retained module+bridges, initialized unpoisoned FLS,
// immutable RX/flushed exact relay leaves, immutable native addresses/Snapshot.
// sites(plan,stage) and code(stage): fresh checked bytes/protections, own exact
// installed plan only, no displacement masking, no learning a new baseline.
// detached(): fresh OS check; no debugger attached. These checks are necessary,
// not an exclusion lease against a later third-party writer/debugger.
//
// After PREPARED can be published, ALL route resources must survive process life,
// including Refused/timeouts/rollback and worker exit. This class neither owns nor
// frees them. Installer ACK cannot certify target safe-resume or lifetime by itself.
class Gate {
    std::atomic<State> state_{State::Empty};
    Offer offer_{};
    Wire wire_{};
    wi::Plan plan_{};
public:
    State state() const noexcept {return state_.load(std::memory_order_acquire);}
    template<class Ops>
    bool prepare(Offer candidate,Ops& ops) noexcept {
        static_assert(noexcept(ops.identity(candidate)) && noexcept(ops.resources(candidate,plan_)) &&
            noexcept(ops.sites(plan_,cm::Stage::Stock)) && noexcept(ops.code(cm::Stage::Stock)));
        auto empty=State::Empty;
        if(!state_.compare_exchange_strong(empty,State::Preparing,std::memory_order_acq_rel)) return false;
        const auto p=plan(candidate);
        if(!p || !ops.identity(candidate) || !ops.resources(candidate,*p) ||
            !ops.sites(*p,cm::Stage::Stock) || !ops.code(cm::Stage::Stock)) {
            state_.store(State::Refused,std::memory_order_release);return false;
        }
        offer_=candidate;wire_=encode(candidate);plan_=*p;
        state_.store(State::Prepared,std::memory_order_release);return true;
    }
    // Immutable value COPY; no mutable reference or address is published. Called
    // only while Prepared. A concurrent ACK may consume it, never changes bytes.
    std::optional<Wire> publication() const noexcept {
        if(state()!=State::Prepared) return {};
        return wire_;
    }
    template<class Ops>
    bool accept(std::span<const std::uint8_t> ack,Ops& ops) noexcept {
        static_assert(noexcept(ops.identity(offer_)) && noexcept(ops.resources(offer_,plan_)) &&
            noexcept(ops.sites(plan_,cm::Stage::Installed)) && noexcept(ops.code(cm::Stage::Installed)) &&
            noexcept(ops.detached()));
        if(state()!=State::Prepared || ack.size()!=AckWire{}.size() ||
            !std::equal(wire_.begin(),wire_.end(),ack.begin())) return false;
        std::uint64_t outcome=0;
        for(unsigned b=0;b<8;++b) outcome|=std::uint64_t{ack[kOfferBytes+b]}<<(b*8);
        auto prepared=State::Prepared;
        if(!state_.compare_exchange_strong(prepared,State::Verifying,std::memory_order_acq_rel)) return false;
        const bool okay=outcome==static_cast<std::uint64_t>(Completion::ResumedDetached) &&
            ops.identity(offer_) && ops.detached() && ops.resources(offer_,plan_) &&
            ops.sites(plan_,cm::Stage::Installed) && ops.code(cm::Stage::Installed) &&
            ops.detached();
        state_.store(okay?State::Installed:State::Refused,std::memory_order_release);
        // Installed only permits subsequent separate worker startup; no READY or
        // ARMED bit exists here, and no automatic worker/event is created.
        return okay;
    }
    // One-shot expiry/refusal, not cancellation of a concurrent verifier and not
    // authority to unpatch/free. It does not revoke metadata already copied out.
    bool expire() noexcept {
        auto prepared=State::Prepared;
        return state_.compare_exchange_strong(prepared,State::Refused,std::memory_order_acq_rel);
    }
};
} // namespace coop::p99_wrist_handshake
