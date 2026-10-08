#include "nadine_p99_prepared_pair_windows.h"
#include "nadine_p99_wrist_prepare_windows.h"
#include "nadine_p99_group_fiber.h"
#include "nadine_p99_provider_fixture_support.h"
#include "nadine_p99_pair_child.h"
#include <iostream>
#include <string>
namespace bp=coop::p99_prepared_providers;
namespace pp=coop::p99_provider;
namespace wh=coop::p99_wrist_handshake;
namespace prep=coop::p99_wrist_prepare;
namespace wi=coop::p99_wrist_install;
namespace cm=coop::p99_wrist_code_manifest;

#pragma section(".p99deps",read,write)
__declspec(allocate(".p99deps")) pp::Page g_providers{};
#pragma section(".p99rx",execute,read)
__declspec(allocate(".p99rx")) const auto g_anchor=kProviderLiteral;
namespace {
unsigned checks=0,failures=0;
bool check(bool b,const char* what){++checks;if(!b){++failures;std::cerr<<what<<" error="<<GetLastError()<<'\n';}return b;}
constexpr std::uintptr_t kMain=0x140000000;
struct Providers {
    pp::WindowsOps os{g_providers};std::wstring mode;
    unsigned acquired=0,pinned=0,stores=0,reads=0;
    explicit Providers(std::wstring m):mode(std::move(m)){}
    std::uint32_t thread()const noexcept{return os.thread();}
    bool identity(const pp::Session& s)const noexcept{return os.identity(s);}
    pp::Acquire acquire(const pp::Selection& s)noexcept{auto r=os.acquire(s);if(r.okay)++acquired;return r;}
    bool verify(const pp::Selection& s,std::uintptr_t m)const noexcept{return os.verify(s,m);}
    bool pin(const pp::Selection& s,std::uintptr_t m)noexcept{const auto r=os.pin(s,m);if(r)++pinned;return r;}
    bool publish(const pp::Session& s,const pp::Wire& w)noexcept{
        ++stores;if(mode==L"provider-before")return false;
        const auto r=os.publish(s,w);return mode==L"provider-after"?false:r;
    }
    std::optional<pp::Claims> read(const pp::Session& s)noexcept{
        ++reads;auto c=os.read(s);
        if(mode==L"read-missing")return {};
        if(c && mode==L"read-session")++c->session.nonce[0];
        if(c && (mode==L"read-inventory" || (mode==L"read-after-proof" && reads==2)))c->records[1].expected_file_hash[0]^=1;
        return c;
    }
};
struct Frame:coop::p99_group_fiber::Binding{};
struct Local {
    Providers& p;prep::Relays& relays;wh::Offer expected;
    coop::p99_group_fiber::Slot<Frame> fls;
    unsigned finishes=0,stores=0;bool order=true;
    bool finish(const wh::Offer& o)noexcept{
        ++finishes;order=order && p.acquired==2 && p.pinned==2 && !p.stores;
        return p.mode!=L"finish-fail" && o==expected && fls.initialize() && fls.current()==nullptr && !fls.failed();
    }
    bool identity(const wh::Offer& o)noexcept{
        order=order && p.stores==1;
        wh::Offer current{};
        return p.mode!=L"proof-fail" && o==expected && prep::process_identity(current,false) &&
            current.pid==o.pid && current.creation==o.creation && current.frequency==o.frequency;
    }
    bool resources(const wh::Offer& o,const wi::Plan& plan)noexcept{
        return o==expected && fls.initialized() && !fls.failed() && relays.verify() && plan==relays.plan();
    }
    // Explicit fixture-only synthetic main: these two predicates assert NO retail
    // stock-code evidence. This exam tests coordination/real publication only.
    bool sites(const wi::Plan& plan,cm::Stage s)noexcept{return s==cm::Stage::Stock && plan==relays.plan();}
    bool code(cm::Stage s)noexcept{return s==cm::Stage::Stock;}
    bool publish(const wh::Wire& w)noexcept{
        ++stores;order=order && p.reads==2 && p.os.read(bp::session(expected)).has_value();
        if(p.mode==L"offer-before")return false;
        if(p.mode==L"offer-partial"){std::copy_n(w.begin(),16,TllP99WristPreparedV1.offer.begin());return false;}
        const auto r=prep::publish(TllP99WristPreparedV1,expected.dll,w);return p.mode==L"offer-after"?false:r;
    }
};
}
int wmain(int argc,wchar_t** argv){
    if(argc==4 && std::wstring_view(argv[1])==L"--pair-reader"){
        pair_child::Handle process,mapping;process.h=pair_child::number(argv[2]);mapping.h=pair_child::number(argv[3]);
        return pair_child::read(process.h,mapping.h);
    }
    if(argc!=2)return 2;const std::wstring mode=argv[1];
    const std::array<std::wstring_view,13> modes{L"normal",L"wrong-prefix",L"provider-before",L"provider-after",
        L"read-missing",L"read-session",L"read-inventory",L"read-after-proof",L"finish-fail",L"proof-fail",
        L"offer-before",L"offer-after",L"offer-partial"};
    if(std::find(modes.begin(),modes.end(),mode)==modes.end())return 2;
    std::array<wchar_t,32768> path{};const auto n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!n || n>=path.size())return 3;const std::wstring executable(path.data(),n);
    const auto fixture=executable.substr(0,executable.find_last_of(L'\\')+1)+L"tll_p99_provider_fixture.dll";
    const auto dll=LoadLibraryExW(fixture.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!check(dll!=nullptr,"load only adjacent owned fixture"))return 1;
    const auto self=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto anchor=reinterpret_cast<std::uintptr_t>(g_anchor.data());
    std::array<pp::Selection,2> selected{};provider_fixture::File a,b;
    if(!check(provider_fixture::make_expected(executable,self,anchor,a,selected[0]) &&
        provider_fixture::make_expected(fixture,reinterpret_cast<std::uintptr_t>(dll),
            reinterpret_cast<std::uintptr_t>(GetProcAddress(dll,"P99ProviderLiteral")),b,selected[1]),"held own file snapshots"))return 1;
    const auto reserved=VirtualAlloc(reinterpret_cast<void*>(kMain),0x8000000,MEM_RESERVE,PAGE_NOACCESS);
    if(!check(reinterpret_cast<std::uintptr_t>(reserved)==kMain,"only synthetic uncommitted main extent"))return 1;
    prep::Relays relays;
    const std::array<std::uintptr_t,5> hooks{anchor,anchor,anchor+8,anchor+16,anchor+24}; // literal DATA, never executed
    if(!check(relays.prepare(kMain,hooks) && relays.verify(),"actual five retained RX relays"))return 1;
    wh::Offer o{};if(!check(prep::process_identity(o,true),"one actual random session nonce"))return 1;
    o.module=kMain;o.image_bytes=0x8000000;o.dll=self;o.dll_bytes=selected[0].bytes;o.hooks=hooks;o.relays=relays.addresses();
    o.originals={kMain+0x1A92A00,kMain+0x1AB97C0,kMain+0x217B630};
    const auto expected=bp::expected_claims(o,selected);if(!check(expected.has_value(),"independent expected pair"))return 1;
    const bp::Pair pair{o,*expected};
    const auto offer_page=reinterpret_cast<std::uintptr_t>(&TllP99WristPreparedV1);
    const auto provider_page=reinterpret_cast<std::uintptr_t>(&g_providers);
    check(offer_page!=provider_page && !bp::read_pair(GetCurrentProcess(),pair,offer_page,provider_page),"separate initially unpublished pages");
    if(mode==L"wrong-prefix")selected[1].prefix[7]^=1;
    Providers providers(mode);Local local{providers,relays,o};
    {
        bp::Preparation bound;
        check(bound.run(o,selected,providers,local)==(mode==L"normal"),"coordinated publication prediction");
        const auto calls=providers.stores+local.stores;
        check(!bound.run(o,selected,providers,local) && calls==providers.stores+local.stores,"no retry after any outcome");
        if(mode==L"provider-after")check(bound.provider_state()==pp::State::PublishUnknown && bound.gate_state()==wh::State::Empty,"unknown provider publication blocks Gate entirely");
        if(mode==L"offer-after")check(bound.state()==bp::State::OfferUnknown && bound.gate_state()==wh::State::Refused,"unknown offer publication not local success");
    }
    check(local.order,"retain then finish then claims then proofs then offer");
    const bool provider_readable=mode!=L"wrong-prefix" && mode!=L"finish-fail" && mode!=L"provider-before";
    check(providers.os.read(bp::session(o)).has_value()==provider_readable,"claims survive later refusal/destructor");
    const bool readable=mode==L"normal" || mode==L"offer-after";
    check(bp::read_pair(GetCurrentProcess(),pair,offer_page,provider_page).has_value()==readable,"paired RO reads require both pages, not local success flag");
    check(pair_child::run(executable,pair,offer_page,provider_page,readable),"own hidden child verifies parent pages with read-only inherited handle and independent input values");
    if(local.stores==0)check(prep::bytes_equal(offer_page,std::array<std::uint8_t,4096>{}),"failure before offer leaves whole page zero");
    if(readable){
        for(unsigned field=0;field<7;++field){auto bad=pair;
            switch(field){case 0:++bad.offer.nonce[0];break;case 1:++bad.providers.session.nonce[0];break;
            case 2:++bad.offer.creation;++bad.providers.session.creation;break;
            case 3:bad.providers.records[1].expected_file_hash[0]^=1;break;
            case 4:bad.providers.records[1].anchor_rva+=1;break;
            case 5:bad.providers.count=1;bad.providers.records[1]={};break;
            case 6:bad.offer.frequency+=1;break;}
            check(!bp::read_pair(GetCurrentProcess(),bad,offer_page,provider_page),"changed expected session/inventory/offer refuses");
        }
        check(!bp::read_pair(GetCurrentProcess(),pair,offer_page,offer_page),"same page refuses");
        check(!bp::read_pair(GetCurrentProcess(),pair,offer_page+1,provider_page),"unaligned page refuses");
        check(!bp::read_pair(GetCurrentProcess(),pair,provider_page,offer_page),"swapped channels refuse");
    }
    check(FreeLibrary(dll)!=FALSE,"release only initial owned fixture reference");
    MEMORY_BASIC_INFORMATION m{};
    check(VirtualQuery(dll,&m,sizeof(m))==sizeof(m) && m.Type==MEM_IMAGE && m.AllocationBase==dll,"provider refs survive all outcomes and destructor");
    check(relays.verify(),"never-called relay resources remain retained");
    std::cout<<checks<<" checks, "<<failures<<" failures; OWNED_COORDINATION_ONLY\n";return failures?1:0;
}
