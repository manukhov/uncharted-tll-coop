#pragma once
#include "nadine_p99_group_ingress.h"
#include <array>
#include <span>

namespace coop::p99_wrist_install {
inline constexpr std::size_t kSiteCount=5, kPageBytes=0x1000;
struct Spec {
    std::uintptr_t rva, target_rva;
    std::size_t size, displacement;
    std::array<std::uint8_t,7> stock;
};
inline constexpr std::array<Spec,kSiteCount> kSites{{
    {0x17815B7,0x1A92A00,7,3,{0x48,0x8D,0x05,0x42,0x14,0x31,0}},
    {0x1782B3A,0x1A92A00,5,1,{0xE9,0xC1,0xFE,0x30,0}},
    {0x1A92E78,0x1AB97C0,5,1,{0xE8,0x43,0x69,0x02,0}},
    {0x1A932F8,0x1AB97C0,5,1,{0xE8,0xC3,0x64,0x02,0}},
    {0x1ABA973,0x217B630,5,1,{0xE8,0xB8,0x0C,0x6C,0}},
}};
struct Site {
    std::uintptr_t address=0, relay=0;
    std::size_t size=0, page=0;
    std::array<std::uint8_t,7> before{},after{};
    std::array<std::uint8_t,14> leaf{};
    bool operator==(const Site&) const = default;
};
struct Plan {
    std::array<Site,kSiteCount> sites{};
    std::array<std::uintptr_t,kSiteCount> pages{};
    std::size_t page_count=0;
    std::uintptr_t module=0;
    bool operator==(const Plan&) const = default;
};
using Bytes=std::span<const std::uint8_t>;

// Pure values only. Caller must supply separately allocated reachable relays,
// correct ABI hook entries, loaded code integrity and real quiescence proof.
inline std::optional<Plan> plan(std::uintptr_t module,
    const std::array<std::array<std::uint8_t,7>,kSiteCount>& observed,
    const std::array<std::uintptr_t,kSiteCount>& relays,
    const std::array<std::uintptr_t,kSiteCount>& hooks) noexcept {
    if (!module) return {};
    Plan result{}; result.module=module;
    for(std::size_t i=0;i<kSiteCount;++i) {
        const auto& spec=kSites[i]; auto& site=result.sites[i];
        const auto address=p99_group_ingress::add(module,spec.rva);
        const auto native=p99_group_ingress::add(module,spec.target_rva);
        if(!address || !native || !relays[i] || !hooks[i] ||
            !p99_group_ingress::add(relays[i],14) ||
            !std::equal(observed[i].begin(),observed[i].begin()+spec.size,spec.stock.begin())) return {};
        const auto next=p99_group_ingress::add(*address,spec.size);
        if(!next || (*address>>12)!=((*next-1)>>12)) return {};
        const auto original=p99_group_ingress::relative(*next,*native);
        const auto replacement=p99_group_ingress::relative(*next,relays[i]);
        std::int32_t seen=0; std::memcpy(&seen,observed[i].data()+spec.displacement,4);
        if(!original || seen!=*original || !replacement) return {};
        site.address=*address; site.relay=relays[i]; site.size=spec.size;
        site.before=observed[i]; site.after=observed[i];
        std::memcpy(site.after.data()+spec.displacement,&*replacement,4);
        site.leaf[0]=0xFF; site.leaf[1]=0x25;
        std::memcpy(site.leaf.data()+6,&hooks[i],8);
        const auto page=*address&~std::uintptr_t{0xFFF};
        if(!p99_group_ingress::add(page,kPageBytes)) return {};
        std::size_t index=0;
        while(index<result.page_count && result.pages[index]!=page) ++index;
        if(index==result.page_count) result.pages[result.page_count++]=page;
        site.page=index;
    }
    // Allocations cannot alias a patch page, another relay or any patch site.
    for(std::size_t i=0;i<kSiteCount;++i) {
        const auto first=result.sites[i].relay, last=first+14;
        for(std::size_t p=0;p<result.page_count;++p)
            if(first<result.pages[p]+kPageBytes && result.pages[p]<last) return {};
        for(std::size_t j=0;j<i;++j)
            if(first<result.sites[j].relay+14 && result.sites[j].relay<last) return {};
    }
    return result;
}

// Rebuild the value contract before invoking any OS adapter. Public structures
// are transport values, not authority to patch arbitrary addresses/opcodes.
inline bool valid(const Plan& p) noexcept {
    std::array<std::array<std::uint8_t,7>,kSiteCount> before{};
    std::array<std::uintptr_t,kSiteCount> relays{},hooks{};
    for(std::size_t i=0;i<kSiteCount;++i) {
        before[i]=p.sites[i].before; relays[i]=p.sites[i].relay;
        std::memcpy(&hooks[i],p.sites[i].leaf.data()+6,8);
    }
    const auto rebuilt=plan(p.module,before,relays,hooks);
    return rebuilt && *rebuilt==p;
}

enum class Outcome { Installed, CleanRefusal, Uncertain };
enum class CodeStage {Stock, Installed};
struct Result {
    Outcome outcome=Outcome::CleanRefusal;
    bool wrote=false, retain_relays=false, resumed=true, protections_restored=true;
};

// Adapter requirements (all noexcept):
// integrity(stage): compare independently certified stock/own-installed code;
// no unverified displacement masking or acceptance of a mixture of stages;
// relays_ready(plan): exact leaf bytes, committed immutable RX, flushed before
// publication, certified ABI targets AND retained relay/target/module lifetime;
// match(addr,bytes), inspect(page), writable(page), original(page): reject-only;
// freeze()/quiescent(plan)/thaw(safe_to_resume): own every suspended-thread handle;
// thaw(false) MUST preserve the stop, never resume/detach an uncertain target.
// On failure the adapter must outlive Result and expose explicit recovery; an
// auto-detach destructor is not allowed to release such a safety hold.
// open(page)/restore(page): only the captured original RX page contract;
// write(addr,bytes): may partially write even when returning false; flush(addr,n).
// No heap/loader/crypto/log operations while frozen. This core is NOT itself
// proof that a thread snapshot is atomic or that new threads cannot appear.
template<class Ops>
Result apply(const Plan& p, Ops& ops) noexcept {
    static_assert(noexcept(ops.integrity(CodeStage::Stock)) && noexcept(ops.relays_ready(p)) &&
        noexcept(ops.match(std::uintptr_t{},Bytes{})) && noexcept(ops.inspect(std::uintptr_t{})) &&
        noexcept(ops.freeze()) && noexcept(ops.quiescent(p)) && noexcept(ops.thaw(false)) &&
        noexcept(ops.writable(std::uintptr_t{})) && noexcept(ops.original(std::uintptr_t{})) &&
        noexcept(ops.open(std::uintptr_t{})) && noexcept(ops.restore(std::uintptr_t{})) &&
        noexcept(ops.write(std::uintptr_t{},Bytes{})) && noexcept(ops.flush(std::uintptr_t{},std::size_t{})),
        "Suspended-thread transaction adapters must not throw");
    Result result{};
    if(!valid(p)) return result;
    const auto match=[&](bool installed) noexcept {
        bool okay=true;
        for(const auto& s:p.sites) {
            if(!s.address || s.size<5 || s.size>7 || s.page>=p.page_count ||
                (s.address&~std::uintptr_t{0xFFF})!=p.pages[s.page] ||
                ((s.address+s.size-1)&~std::uintptr_t{0xFFF})!=p.pages[s.page]) return false;
            okay=ops.match(s.address,{(installed?s.after:s.before).data(),s.size}) && okay;
        }
        return okay;
    };
    if(!ops.integrity(CodeStage::Stock) || !ops.relays_ready(p) || !match(false)) return result;
    for(std::size_t i=0;i<p.page_count;++i) if(!ops.inspect(p.pages[i])) return result;
    const bool frozen=ops.freeze();
    bool okay=frozen && ops.quiescent(p) && ops.integrity(CodeStage::Stock) && ops.relays_ready(p) && match(false);
    bool touched_protection=false;
    if(okay) for(std::size_t i=0;i<p.page_count;++i) {
        touched_protection=true;
        if(!ops.open(p.pages[i]) || !ops.writable(p.pages[i])) { okay=false; break; }
    }
    if(okay) for(const auto& s:p.sites) {
        result.wrote=true; // BEFORE attempt, even a failed/partial write exposes bytes.
        if(!ops.write(s.address,{s.after.data(),s.size}) || !ops.flush(s.address,s.size)) {
            okay=false; break;
        }
    }
    if(okay) okay=match(true) && ops.integrity(CodeStage::Installed) && ops.relays_ready(p);
    const auto restore=[&]() noexcept {
        bool restored=true;
        for(std::size_t i=0;i<p.page_count;++i) {
            // Never assume failure leaves the previous protection unchanged.
            if(!ops.original(p.pages[i])) (void)ops.restore(p.pages[i]);
            restored=ops.original(p.pages[i]) && restored;
        }
        return restored;
    };
    bool restored=!touched_protection || restore();
    if(okay) okay=restored && match(true) && ops.integrity(CodeStage::Installed) && ops.relays_ready(p);
    bool rollback_flush=true;
    if(!okay && result.wrote) {
        // Recover as much as possible without writing through an unproved page.
        std::array<bool,kSiteCount> writable{};
        for(std::size_t i=0;i<p.page_count;++i) {
            if(!ops.writable(p.pages[i])) (void)ops.open(p.pages[i]);
            writable[i]=ops.writable(p.pages[i]);
        }
        for(const auto& s:p.sites) {
            if(!writable[s.page]) { rollback_flush=false; continue; }
            const bool wrote=ops.write(s.address,{s.before.data(),s.size});
            const bool flushed=ops.flush(s.address,s.size);
            rollback_flush=wrote && flushed && rollback_flush;
        }
        restored=false;
    }
    for(unsigned n=0;!restored && n<3;++n) restored=restore();
    // A queued/fetched relay may remain reachable even after all five sites are
    // stock again. Restoring those bytes alone cannot cure a late code/leaf/RX/
    // lifetime failure, so successful rollback must certify those dependencies.
    const bool clean=!okay && restored && rollback_flush && match(false) &&
        (!result.wrote || (ops.integrity(CodeStage::Stock) && ops.relays_ready(p)));
    result.protections_restored=restored;
    // Release only verified installed/stock state. A failed freeze may still
    // own a partial stop; its adapter must also recover that ownership safely.
    result.resumed=ops.thaw(okay || clean);
    result.outcome=okay && result.resumed ? Outcome::Installed :
        clean && result.resumed ? Outcome::CleanRefusal : Outcome::Uncertain;
    // Even verified rollback cannot recall a relay fetched by a racing/queued
    // callback. Once any write was attempted, never free relays or their DLL.
    result.retain_relays=result.wrote || result.outcome==Outcome::Uncertain;
    return result;
}
} // namespace coop::p99_wrist_install
