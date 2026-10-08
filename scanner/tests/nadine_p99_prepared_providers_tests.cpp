#include "nadine_p99_prepared_providers.h"
#include <iostream>
#include <string>
#include <vector>
namespace bp=coop::p99_prepared_providers;
namespace pp=coop::p99_provider;
namespace wh=coop::p99_wrist_handshake;
namespace wi=coop::p99_wrist_install;
namespace cm=coop::p99_wrist_code_manifest;
namespace {
unsigned checks=0,failures=0;
void check(bool b,const char* what){++checks;if(!b){++failures;std::cerr<<what<<'\n';}}
wh::Offer offer(){
    wh::Offer o{};o.pid=123;o.creation=456;o.nonce={789,987};
    o.module=0x140000000;o.image_bytes=0x8000000;o.dll=0x7FF000000000;o.dll_bytes=0x100000;o.frequency=10000000;
    o.originals={o.module+0x1A92A00,o.module+0x1AB97C0,o.module+0x217B630};
    o.hooks={o.dll+0x1000,o.dll+0x1000,o.dll+0x2000,o.dll+0x3000,o.dll+0x4000};
    for(unsigned i=0;i<5;++i)o.relays[i]=o.module+0x10000000+i*0x1000;return o;
}
auto selections(){
    std::array<pp::Selection,2> v{};
    for(unsigned i=0;i<2;++i){auto& s=v[i];
        const auto path=L"C:\\owned\\p"+std::to_wstring(i)+L".dll";
        const auto native=L"\\Device\\Disk\\p"+std::to_wstring(i)+L".dll";
        std::copy(path.begin(),path.end(),s.path.begin());std::copy(native.begin(),native.end(),s.native_path.begin());
        s.base=offer().dll+i*0x200000;s.bytes=offer().dll_bytes;s.anchor_rva=0x1000;
        s.prefix.fill(0x90);s.expected_file_hash.fill(static_cast<std::uint8_t>(i+1));
    }return v;
}
struct Provider {
    std::vector<char> events;
    unsigned fail=0,read_fault=0,reads=0,tid=1;
    pp::Wire wire{};
    std::uint32_t thread()const noexcept{return tid;}
    bool identity(const pp::Session& s)const noexcept{return s==bp::session(offer());}
    pp::Acquire acquire(const pp::Selection& s) noexcept{events.push_back('A');return {fail!=1,s.base};}
    bool verify(const pp::Selection& s,std::uintptr_t ref)noexcept{events.push_back('V');return fail!=2 && s.base==ref && s.prefix[0]==0x90;}
    bool pin(const pp::Selection&,std::uintptr_t)noexcept{events.push_back('P');return fail!=3;}
    bool publish(const pp::Session&,const pp::Wire& w)noexcept{events.push_back('C');if(fail!=4)wire=w;return fail!=4 && fail!=5;}
    std::optional<pp::Claims> read(const pp::Session&) noexcept{
        events.push_back('R');++reads;auto c=pp::decode(wire);
        if(reads==read_fault && c)c->records[1].expected_file_hash[0]^=1;return c;
    }
};
struct Local {
    Provider& p;unsigned fail=0,publishes=0;wh::Wire wire{};bool mutable_finish=false,mutate_proof=false;
    std::array<pp::Selection,2>* input=nullptr;
    bool finish(const wh::Offer&)noexcept{p.events.push_back('F');if(input){(*input)[1].expected_file_hash.fill(0);(*input)[1].prefix.fill(0);}return fail!=1;}
    bool finish(wh::Offer& o)noexcept{mutable_finish=true;++o.nonce[0];return true;}
    bool identity(const wh::Offer&)noexcept{p.events.push_back('I');return fail!=2;}
    bool identity(wh::Offer& o)noexcept{if(mutate_proof)++o.nonce[0];return identity(static_cast<const wh::Offer&>(o));}
    bool resources(const wh::Offer&,const wi::Plan&)noexcept{p.events.push_back('S');return fail!=3;}
    bool sites(const wi::Plan&,cm::Stage)noexcept{p.events.push_back('T');return fail!=4;}
    bool code(cm::Stage)noexcept{p.events.push_back('B');return fail!=5;}
    bool publish(const wh::Wire& w)noexcept{++publishes;p.events.push_back('O');if(fail!=6)wire=w;return fail!=6 && fail!=7;}
};
void positive(){
    auto v=selections();Provider p;Local l{p};l.input=&v;bp::Preparation b;
    check(b.run(offer(),v,p,l),"positive binding with owned input mutation");
    check(p.events==std::vector<char>{'A','A','V','V','P','P','F','V','V','C','R','I','S','T','B','R','O'},"all retained before finish, claims before gate before offer");
    check(b.state()==bp::State::Prepared && b.provider_state()==pp::State::Published && b.gate_state()==wh::State::Prepared,"only fully successful local preparation");
    check(wh::decode(l.wire)==offer(),"exact unchanged240B OfferV1");
    check(!l.mutable_finish,"finish receives immutable owned Offer, never mutable overload");
    for(unsigned n=26*8;n<30*8;++n)check(l.wire[n]==0,"V1 reserved still zero");
    const auto calls=p.events.size();check(!b.run(offer(),v,p,l) && calls==p.events.size(),"no success retry");
}
void refusals(){
    {Provider p;Local l{p};l.mutate_proof=true;bp::Preparation b;
     check(!b.run(offer(),selections(),p,l) && l.publishes==0 && b.gate_state()==wh::State::Refused,
         "proof-mutated Gate candidate cannot publish a different Offer/session");}
    for(unsigned f=1;f<=5;++f){Provider p;p.fail=f;Local l{p};bp::Preparation b;
        check(!b.run(offer(),selections(),p,l) && l.publishes==0 && b.gate_state()==wh::State::Empty,"provider failure never enters Gate");
        check(pp::decode(p.wire).has_value()==(f==5),"provider false-after-effect can leave claims");
        if(f>=4)check(b.state()==bp::State::ProviderUnknown,"unknown provider store outcome retained");
        const auto n=p.events.size();check(!b.run(offer(),selections(),p,l) && n==p.events.size(),"provider failure no retry");
    }
    for(unsigned f=1;f<=7;++f){Provider p;Local l{p};l.fail=f;bp::Preparation b;
        check(!b.run(offer(),selections(),p,l),"later failure refuses");
        check(l.publishes==(f>=6?1u:0u),"offer store only after all proofs");
        check(pp::decode(p.wire).has_value()==(f!=1),"claims retained after later gate/store refusal");
        check(wh::decode(l.wire).has_value()==(f==7),"offer false-after-effect possible");
        check(b.gate_state()==(f==1?wh::State::Empty:wh::State::Refused),"later refusal gate state");
        if(f>=6)check(b.state()==bp::State::OfferUnknown,"unknown offer exposure not local success");
    }
    for(unsigned nth=1;nth<=2;++nth){Provider p;p.read_fault=nth;Local l{p};bp::Preparation b;
        check(!b.run(offer(),selections(),p,l) && l.publishes==0,"wrong exact inventory refuses at either readback");
        check(b.gate_state()==(nth==1?wh::State::Empty:wh::State::Refused),"read failure before/after gate distinguished");
    }
    for(unsigned f=0;f<6;++f){auto v=selections();auto o=offer();
        if(f==0)o.nonce={};if(f==1)o.pid=0;if(f==2)v[0].base+=0x10000;
        if(f==3)v[1].base=v[0].base;if(f==4)v[1].expected_file_hash={};if(f==5)v[1].path.fill(0);
        Provider p;Local l{p};bp::Preparation b;
        check(!b.run(o,v,p,l) && p.events.empty(),"invalid candidate/profile no provider or local effects");
    }
}
}
int main(){positive();refusals();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
