#include "nadine_p99_wrist_install.h"
#include "nadine_p99_wrist_code_manifest.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <vector>

namespace wi=coop::p99_wrist_install;
namespace {
unsigned checks=0,failures=0;
void check(bool okay,const char* label) {
    ++checks; if(!okay) { ++failures; std::cerr<<label<<'\n'; }
}
__declspec(noinline) void owned_hook() {}
wi::Plan make_plan(std::uintptr_t base=0x140000000,bool actual=false) {
    std::array<std::array<std::uint8_t,7>,5> stock{};
    std::array<std::uintptr_t,5> relays{},hooks{};
    for(std::size_t i=0;i<5;++i) {
        stock[i]=wi::kSites[i].stock; relays[i]=base+0x2000000+i*32;
        hooks[i]=actual?reinterpret_cast<std::uintptr_t>(&owned_hook):base+0x2100000+i*32;
    }
    return wi::plan(base,stock,relays,hooks).value();
}
enum class Op:unsigned {Integrity,Relays,Match,Inspect,Freeze,Quiescent,Open,Writable,
    Original,Restore,Write,Flush,Thaw,Count};
constexpr auto count=static_cast<unsigned>(Op::Count);
struct Fault {Op op=Op::Count; unsigned nth=0; bool forever=false,after=false;};
struct Ops {
    wi::Plan p;
    std::array<std::array<std::uint8_t,7>,5> bytes{};
    std::array<bool,5> rw{};
    std::array<unsigned,count> calls{};
    std::array<Fault,2> faults{};
    bool suspended=false,unsafe_write=false,real=false;
    void* allocation=nullptr;
    explicit Ops(bool actual=false):p(make_plan()),real(actual) {
        if(real) {
            allocation=VirtualAlloc(nullptr,0x2110000,MEM_RESERVE,PAGE_NOACCESS);
            if(!allocation) { check(false,"reserve owned pages"); real=false; }
            else p=make_plan(reinterpret_cast<std::uintptr_t>(allocation),true);
        }
        for(std::size_t i=0;i<5;++i) bytes[i]=p.sites[i].before;
        if(real) {
            check(VirtualAlloc(reinterpret_cast<void*>(p.sites[0].relay),4096,MEM_COMMIT,
                PAGE_READWRITE)!=nullptr,"commit owned relay page");
            for(const auto& s:p.sites) std::memcpy(reinterpret_cast<void*>(s.relay),s.leaf.data(),s.leaf.size());
            DWORD old_relay=0;
            check(VirtualProtect(reinterpret_cast<void*>(p.sites[0].relay),4096,PAGE_EXECUTE_READ,
                &old_relay)!=FALSE && FlushInstructionCache(GetCurrentProcess(),
                reinterpret_cast<void*>(p.sites[0].relay),4096)!=FALSE,"seal and flush owned relay page");
            for(std::size_t i=0;i<p.page_count;++i)
                check(VirtualAlloc(reinterpret_cast<void*>(p.pages[i]),4096,MEM_COMMIT,
                    PAGE_READWRITE)!=nullptr,"commit owned page");
            for(const auto& s:p.sites) std::memcpy(reinterpret_cast<void*>(s.address),s.before.data(),s.size);
            for(std::size_t i=0;i<p.page_count;++i) {
                DWORD old=0; check(VirtualProtect(reinterpret_cast<void*>(p.pages[i]),4096,
                    PAGE_EXECUTE_READ,&old)!=FALSE,"seal owned page");
            }
        }
    }
    ~Ops() { if(allocation) check(VirtualFree(allocation,0,MEM_RELEASE)!=FALSE,"free owned fixture"); }
    unsigned page(std::uintptr_t value) const noexcept {
        for(unsigned i=0;i<p.page_count;++i) if(p.pages[i]==value) return i;
        return 5;
    }
    unsigned site(std::uintptr_t value) const noexcept {
        for(unsigned i=0;i<5;++i) if(p.sites[i].address==value) return i;
        return 5;
    }
    bool fault(Op op,bool* after=nullptr) noexcept {
        const auto n=++calls[static_cast<unsigned>(op)];
        for(const auto& f:faults) if(f.op==op && (n==f.nth || (f.forever && n>=f.nth))) {
            if(after) *after=f.after; return true;
        }
        return false;
    }
    bool protection(std::uintptr_t value,DWORD wanted) const noexcept {
        const auto i=page(value); if(i==5) return false;
        if(!real) return rw[i]==(wanted==PAGE_EXECUTE_READWRITE);
        MEMORY_BASIC_INFORMATION mbi{};
        return VirtualQuery(reinterpret_cast<void*>(value),&mbi,sizeof(mbi))==sizeof(mbi) &&
            mbi.State==MEM_COMMIT && mbi.Protect==wanted;
    }
    bool set(std::uintptr_t value,bool writable) noexcept {
        const auto i=page(value); if(i==5) return false;
        if(real) {
            DWORD old=0;
            if(!VirtualProtect(reinterpret_cast<void*>(value),4096,
                writable?PAGE_EXECUTE_READWRITE:PAGE_EXECUTE_READ,&old)) return false;
        }
        rw[i]=writable; return true;
    }
    bool integrity(wi::CodeStage stage) noexcept {
        return !fault(Op::Integrity) && all(stage==wi::CodeStage::Installed);
    }
    bool relays_ready(const wi::Plan& candidate) noexcept {
        if(fault(Op::Relays)) return false;
        if(!real) return candidate==p;
        for(const auto& s:candidate.sites) {
            MEMORY_BASIC_INFORMATION mbi{};
            if(VirtualQuery(reinterpret_cast<void*>(s.relay),&mbi,sizeof(mbi))!=sizeof(mbi) ||
                mbi.State!=MEM_COMMIT || mbi.Protect!=PAGE_EXECUTE_READ ||
                std::memcmp(reinterpret_cast<void*>(s.relay),s.leaf.data(),s.leaf.size())) return false;
            std::uintptr_t hook=0; std::memcpy(&hook,s.leaf.data()+6,8);
            if(hook!=reinterpret_cast<std::uintptr_t>(&owned_hook)) return false;
        }
        // This fixture owns the never-executed leaf memory through all checks.
        // It does NOT certify game callback ABI or permanent DLL retention.
        return true;
    }
    bool match(std::uintptr_t address,wi::Bytes expected) noexcept {
        if(fault(Op::Match)) return false;
        const auto i=site(address); if(i==5 || expected.size()!=p.sites[i].size) return false;
        return std::memcmp(real?reinterpret_cast<const void*>(address):bytes[i].data(),
            expected.data(),expected.size())==0;
    }
    bool inspect(std::uintptr_t value) noexcept { return !fault(Op::Inspect) && protection(value,PAGE_EXECUTE_READ); }
    bool freeze() noexcept { suspended=true; return !fault(Op::Freeze); }
    bool quiescent(const wi::Plan&) noexcept { return suspended && !fault(Op::Quiescent); }
    bool thaw(bool safe_to_resume) noexcept {
        if(fault(Op::Thaw) || !safe_to_resume) return false;
        suspended=false;return true;
    }
    bool writable(std::uintptr_t value) noexcept { return !fault(Op::Writable) && protection(value,PAGE_EXECUTE_READWRITE); }
    bool original(std::uintptr_t value) noexcept { return !fault(Op::Original) && protection(value,PAGE_EXECUTE_READ); }
    bool open(std::uintptr_t value) noexcept {
        bool after=false; const bool fail=fault(Op::Open,&after);
        if(fail && !after) return false;
        const bool done=set(value,true); return done && !fail;
    }
    bool restore(std::uintptr_t value) noexcept {
        bool after=false; const bool fail=fault(Op::Restore,&after);
        if(fail && !after) return false;
        const bool done=set(value,false); return done && !fail;
    }
    bool write(std::uintptr_t address,wi::Bytes value) noexcept {
        bool partial=false; const bool fail=fault(Op::Write,&partial);
        const auto i=site(address);
        if(i==5 || !suspended || !protection(p.pages[p.sites[i].page],PAGE_EXECUTE_READWRITE)) {
            unsafe_write=true; return false;
        }
        if(fail && !partial) return false;
        const auto n=fail?value.size()/2:value.size();
        std::memcpy(real?reinterpret_cast<void*>(address):bytes[i].data(),value.data(),n);
        return !fail;
    }
    bool flush(std::uintptr_t address,std::size_t size) noexcept {
        if(fault(Op::Flush)) return false;
        return !real || FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),size)!=FALSE;
    }
    bool all(bool installed) noexcept {
        for(const auto& s:p.sites) if(std::memcmp(real?reinterpret_cast<const void*>(s.address):
            bytes[site(s.address)].data(),(installed?s.after:s.before).data(),s.size)) return false;
        return true;
    }
    bool sealed() const noexcept {
        for(std::size_t i=0;i<p.page_count;++i) if(!protection(p.pages[i],PAGE_EXECUTE_READ)) return false;
        return true;
    }
};
void planner_tests() {
    auto p=make_plan(); check(wi::valid(p) && p.page_count==5,"five sites on five pages");
    std::array<std::array<std::uint8_t,7>,5> stock{};
    std::array<std::uintptr_t,5> relays{},hooks{};
    for(std::size_t i=0;i<5;++i) {
        stock[i]=p.sites[i].before; relays[i]=p.sites[i].relay;
        std::memcpy(&hooks[i],p.sites[i].leaf.data()+6,8);
    }
    for(std::size_t i=0;i<5;++i) {
        std::int32_t displacement=0;
        std::memcpy(&displacement,p.sites[i].after.data()+wi::kSites[i].displacement,4);
        check(static_cast<std::int64_t>(p.sites[i].address+p.sites[i].size)+displacement==
            static_cast<std::int64_t>(relays[i]),"resolved relay");
        for(std::size_t byte=0;byte<wi::kSites[i].size;++byte) {
            stock[i][byte]^=1;
            check(!wi::plan(p.module,stock,relays,hooks),"foreign stock byte rejected");
            stock[i][byte]^=1;
        }
        auto bad=p; bad.sites[i].after[0]^=1;
        Ops ops; check(wi::apply(bad,ops).outcome==wi::Outcome::CleanRefusal &&
            ops.calls[0]==0,"malformed plan never reaches adapter");
    }
    check(!wi::plan(0,stock,relays,hooks),"null module");
    check(!wi::plan(UINTPTR_MAX-0x100,stock,relays,hooks),"module overflow");
    auto changed=relays; changed[0]=UINTPTR_MAX-10;
    check(!wi::plan(p.module,stock,changed,hooks),"relay overflow");
    changed=relays; changed[0]=p.sites[0].address;
    check(!wi::plan(p.module,stock,changed,hooks),"relay overlaps patch page");
    changed=relays; changed[1]=changed[0]+13;
    check(!wi::plan(p.module,stock,changed,hooks),"relay overlap");
    changed=relays; changed[0]=p.sites[0].address+7+0x80000000ull;
    check(!wi::plan(p.module,stock,changed,hooks),"positive rel32 overflow");
    changed[0]=p.sites[0].address+7+0x7FFFFFFFull;
    check(wi::plan(p.module,stock,changed,hooks).has_value(),"positive rel32 edge");
    changed[0]=p.sites[0].address+7-0x80000000ull;
    check(wi::plan(p.module,stock,changed,hooks).has_value(),"negative rel32 edge");
    changed[0]-=1;
    check(!wi::plan(p.module,stock,changed,hooks),"negative rel32 overflow");
    for(unsigned field=0;field<7;++field) {
        auto bad=p;
        switch(field) {
        case 0:bad.page_count=6;break; case 1:bad.page_count=0;break;
        case 2:bad.sites[0].size=SIZE_MAX;break; case 3:bad.sites[0].page=9;break;
        case 4:bad.sites[0].address=UINTPTR_MAX;break; case 5:bad.pages[0]+=4096;break;
        case 6:bad.sites[0].leaf[2]=1;break;
        }
        check(!wi::valid(bad),"tampered plan rejected");
    }
}
void normalization_tests() {
    namespace manifest=coop::p99_wrist_code_manifest;
    static_assert(!manifest::kTransitiveClosureComplete && !manifest::kSourceAdmission);
    const auto patch=make_plan();
    std::array<manifest::CallBytes,3> expected{};
    for(std::size_t i=0;i<expected.size();++i) {
        const auto& site=patch.sites[i+2];
        check(site.address-patch.module==manifest::kCallsites[i].rva && site.size==5,
            "manifest and distinct planner have identical three CALL sites");
        std::copy_n(site.after.begin(),5,expected[i].begin());
    }
    for(std::size_t i=0;i<manifest::kRegions.size();++i) {
        const auto& region=manifest::kRegions[i];
        std::vector<std::uint8_t> stock(region.byte_count);
        for(std::size_t byte=0;byte<stock.size();++byte) stock[byte]=static_cast<std::uint8_t>(byte*37+11);
        auto installed=stock;
        for(std::size_t c=0;c<expected.size();++c) {
            const auto& call=manifest::kCallsites[c];
            if(call.rva<region.rva || call.rva-region.rva>=region.byte_count) continue;
            std::copy(call.stock.begin(),call.stock.end(),stock.begin()+(call.rva-region.rva));
            std::copy(expected[c].begin(),expected[c].end(),installed.begin()+(call.rva-region.rva));
        }
        auto owned=stock;
        check(manifest::canonicalize(i,owned,manifest::Stage::Stock) && owned==stock,
            "stock canonicalization preserves owned bytes");
        owned=installed;
        check(manifest::canonicalize(i,owned,manifest::Stage::Installed,expected) && owned==stock,
            "installed canonicalization accepts only own complete plan");
        check(!manifest::canonicalize(i,owned,manifest::Stage::Installed),"installed requires all three plans");
        check(!manifest::canonicalize(i,owned,manifest::Stage::Stock,expected),"stock cannot mask installed plan");
        check(!manifest::canonicalize(i,owned,static_cast<manifest::Stage>(99)),"unknown code stage rejected");
        check(!manifest::canonicalize(i,std::span(owned).first(owned.size()-1),manifest::Stage::Stock),
            "short code region rejected");
        for(std::size_t c=0;c<expected.size();++c) {
            const auto& call=manifest::kCallsites[c];
            if(call.rva<region.rva || call.rva-region.rva>=region.byte_count) continue;
            for(std::size_t byte=0;byte<5;++byte) {
                owned=installed; owned[call.rva-region.rva+byte]^=1;
                const auto before=owned;
                check(!manifest::canonicalize(i,owned,manifest::Stage::Installed,expected) && owned==before,
                    "foreign CALL byte rejects without any partial normalization");
            }
        }
        owned=installed; owned[0]^=1;
        const auto changed=owned[0];
        check(manifest::canonicalize(i,owned,manifest::Stage::Installed,expected) && owned[0]==changed,
            "non CALL byte is not hidden from later full-region hash");
    }
    std::vector<std::uint8_t> empty;
    check(!manifest::canonicalize(manifest::kRegions.size(),empty,manifest::Stage::Stock),
        "unknown code region rejected");
}
void transaction_tests(bool real) {
    Ops baseline(real); const auto okay=wi::apply(baseline.p,baseline);
    check(okay.outcome==wi::Outcome::Installed && okay.wrote && okay.retain_relays &&
        okay.resumed && okay.protections_restored && baseline.all(true) && baseline.sealed() &&
        !baseline.suspended && !baseline.unsafe_write,"successful owned transaction");
    const auto counts=baseline.calls;
    for(unsigned op=0;op<count;++op) for(unsigned nth=1;nth<=counts[op];++nth)
        for(bool after:{false,true}) {
            Ops ops(real); ops.faults[0]={static_cast<Op>(op),nth,false,after};
            const auto result=wi::apply(ops.p,ops);
            check(!ops.unsafe_write,"never write through unproved page");
            if(op==static_cast<unsigned>(Op::Thaw)) {
                check(result.outcome==wi::Outcome::Uncertain && result.retain_relays &&
                    !result.resumed,"resume failure is uncertain");
            } else {
                // A false return AFTER successful protection restoration is
                // recoverable by independently checking the actual protection.
                const bool installed=result.outcome==wi::Outcome::Installed;
                check(result.outcome!=wi::Outcome::Uncertain && ops.all(installed) &&
                    ops.sealed() && !ops.suspended && result.resumed &&
                    result.protections_restored,"single fault recovers verified state");
                check(!result.wrote || result.retain_relays,"rollback retains exposed relay lifetime");
            }
        }
    for(Op op:{Op::Write,Op::Flush,Op::Restore,Op::Original}) {
        Ops ops(real); ops.faults[0]={op,1,true,op!=Op::Restore};
        const auto result=wi::apply(ops.p,ops);
        check(result.outcome==wi::Outcome::Uncertain && result.retain_relays &&
            !result.resumed && ops.suspended && !ops.unsafe_write,
            "persistent write/flush/protection uncertainty preserves the safety hold");
    }
    for(Op op:{Op::Integrity,Op::Relays}) {
        Ops ops(real); ops.faults[0]={op,3,true,false};
        const auto result=wi::apply(ops.p,ops);
        check(result.wrote && ops.all(false) && ops.sealed() &&
            result.outcome==wi::Outcome::Uncertain && !result.resumed && ops.suspended &&
            result.retain_relays,"late persistent code/relay loss cannot resume after stock rollback");
    }
    Ops restore_fault(real); restore_fault.faults[0]={Op::Restore,1,true,false};
    const auto uncertain=wi::apply(restore_fault.p,restore_fault);
    check(uncertain.outcome==wi::Outcome::Uncertain && !uncertain.protections_restored &&
        uncertain.retain_relays && !uncertain.resumed && restore_fault.suspended,
        "unrestored protection cannot resume");
    Ops reopen_fault(real);
    reopen_fault.faults[0]={Op::Match,11,false,false}; // post-write match, after two stock passes
    reopen_fault.faults[1]={Op::Open,6,true,false}; // rollback cannot reopen restored pages
    const auto reopen=wi::apply(reopen_fault.p,reopen_fault);
    check(reopen.outcome==wi::Outcome::Uncertain && reopen.retain_relays &&
        !reopen.resumed && reopen_fault.suspended && !reopen_fault.unsafe_write,
        "failed rollback reopen neither writes RX nor resumes");
    if(real) {
        Ops bad_leaf(true); DWORD old=0;
        auto* relay=reinterpret_cast<std::uint8_t*>(bad_leaf.p.sites[0].relay);
        check(VirtualProtect(relay,4096,PAGE_READWRITE,&old)!=FALSE,"unseal owned corrupted leaf");
        relay[6]^=1;
        check(VirtualProtect(relay,4096,PAGE_EXECUTE_READ,&old)!=FALSE,"reseal owned corrupted leaf");
        const auto refused=wi::apply(bad_leaf.p,bad_leaf);
        check(refused.outcome==wi::Outcome::CleanRefusal && !refused.wrote &&
            bad_leaf.calls[static_cast<unsigned>(Op::Freeze)]==0 && bad_leaf.all(false),
            "changed leaf target refused before freezing or publishing");
        Ops bad_protection(true);
        check(VirtualProtect(reinterpret_cast<void*>(bad_protection.p.sites[0].relay),4096,
            PAGE_READONLY,&old)!=FALSE,"remove owned relay executable protection");
        const auto not_rx=wi::apply(bad_protection.p,bad_protection);
        check(not_rx.outcome==wi::Outcome::CleanRefusal && !not_rx.wrote &&
            bad_protection.calls[static_cast<unsigned>(Op::Freeze)]==0,
            "non executable leaf refused before freezing");
    }
}
}
int main() {
    planner_tests(); normalization_tests(); transaction_tests(false); transaction_tests(true);
    std::cout<<"wrist installation owned checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
