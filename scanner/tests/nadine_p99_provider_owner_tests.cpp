#include "nadine_p99_provider_owner.h"
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

namespace p=coop::p99_provider;
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* what){++checks;if(!ok){++failures;std::cerr<<what<<'\n';}}
p::Session session(){return {99,123,{21,22},0x500000000,0x8000};}
std::array<p::Selection,3> selections(){
    std::array<p::Selection,3> result{};
    for(unsigned i=0;i<result.size();++i){auto& r=result[i];
        const auto path=L"C:\\owned\\p"+std::to_wstring(i)+L".dll";
        const auto native=L"\\Device\\HarddiskVolume3\\owned\\p"+std::to_wstring(i)+L".dll";
        std::copy(path.begin(),path.end(),r.path.begin());std::copy(native.begin(),native.end(),r.native_path.begin());
        r.base=session().artifact+i*0x10000;r.bytes=0x8000;r.anchor_rva=0x1000;r.prefix.fill(0x90);r.expected_file_hash.fill(static_cast<std::uint8_t>(9+i));
    }return result;
}
struct Ops {
    unsigned tid=8,acquires=0,verifies=0,pins=0,publishes=0;
    unsigned fail_acquire=0,fail_verify=0,fail_pin=0;
    bool after=false,identity_ok=true,publish_ok=true;
    unsigned retained=0,pinned=0;
    std::vector<char> events;
    std::array<p::Selection,3>* mutable_input=nullptr;
    p::Wire wire{};
    std::uint32_t thread() const noexcept{return tid;}
    bool identity(const p::Session& s) const noexcept{return identity_ok && s==session();}
    p::Acquire acquire(const p::Selection& s) noexcept{
        ++acquires;events.push_back('A');
        if(mutable_input){(*mutable_input)[1].prefix.fill(0);(*mutable_input)[1].path.fill(0);}
        if(acquires==fail_acquire && !after)return {};
        ++retained;return {acquires!=fail_acquire,static_cast<std::uintptr_t>(s.base)};
    }
    bool verify(const p::Selection& s,std::uintptr_t ref) noexcept{
        ++verifies;events.push_back('V');return verifies!=fail_verify && s.prefix[0]==0x90 &&
            p::path_shape(s.path,false) && ref==s.base;
    }
    bool pin(const p::Selection&,std::uintptr_t) noexcept{
        ++pins;events.push_back('P');if(pins==fail_pin && !after)return false;
        ++pinned;return pins!=fail_pin;
    }
    bool publish(const p::Session&,const p::Wire& w) noexcept{
        ++publishes;events.push_back('W');if(publish_ok || after)wire=w;return publish_ok;
    }
};
void positive(){
    auto input=selections();Ops ops;p::Owner owner;
    check(owner.prepare(session(),input,ops),"prepare succeeds");
    check(owner.state()==p::State::Retained && ops.publishes==0,"retention alone never publishes");
    check(ops.events==std::vector<char>{'A','A','A','V','V','V','P','P','P'},"all acquire before any validate/PIN");
    check(owner.publish(ops) && owner.state()==p::State::Published,"publish after retain");
    const auto c=p::decode(ops.wire);check(c && c->session==session() && c->count==3,"canonical claims roundtrip");
    check(c && c->records[0].base==session().artifact,"artifact itself always included");
    const auto calls=ops.events.size();check(!owner.publish(ops) && !owner.prepare(session(),input,ops),"no repeat");
    check(calls==ops.events.size(),"repeat has no API effects");
    if(!c)return;
    for(std::size_t i=96;i<128;++i){auto w=ops.wire;w[i]=1;check(!p::decode(w),"reserved header rejects");}
    for(std::size_t i=p::kHeader+3*p::kRecord;i<p::kPage;++i){auto w=ops.wire;w[i]=1;check(!p::decode(w),"zero tail/unused records enforced");}
    for(const auto at:{0u,8u,16u,24u,32u,88u,128u}){auto w=ops.wire;w[at]^=0x40;check(!p::decode(w),"invalid wire layout rejected");}
    check(!p::decode(std::span(ops.wire).first(4095)),"short wire");
    auto claims=*c;claims.records[1].base=claims.records[0].base;check(!p::encode(claims),"overlap refuses");
    claims=*c;claims.records[3].bytes=1;check(!p::encode(claims),"unused record nonzero refuses");
}
void failure_cases(){
    for(unsigned kind=0;kind<3;++kind)for(unsigned nth=1;nth<=3;++nth)for(bool after:{false,true}){
        auto input=selections();Ops ops;ops.after=after;
        if(kind==0)ops.fail_acquire=nth;else if(kind==1)ops.fail_verify=nth;else ops.fail_pin=nth;
        std::size_t calls=0;
        {p::Owner owner;check(!owner.prepare(session(),input,ops),"injected failure refuses");
         check(!owner.publish(ops) && ops.publishes==0,"failed preparation cannot publish");
         calls=ops.events.size();check(!owner.prepare(session(),input,ops),"failed one-shot no retry");
         check(owner.state()==(kind==0?p::State::AcquireUnknown:kind==1?p::State::Refused:p::State::PinUnknown),"failure phase retained");}
        check(calls==ops.events.size(),"destructor has no OS effects");
        check(ops.retained==(kind==0?nth-(after?0:1):3),"acquired refs retained including false-after-effect");
        check(ops.pinned==(kind==2?nth-(after?0:1):0),"partial pins not undone");
    }
    for(bool after:{false,true}){auto input=selections();Ops ops;ops.after=after;ops.publish_ok=false;p::Owner owner;
        check(owner.prepare(session(),input,ops) && !owner.publish(ops),"publication failure");
        check(owner.state()==p::State::PublishUnknown && ops.retained==3 && ops.pinned==3,"resources survive unknown exposure");
        check(p::decode(ops.wire).has_value()==after,"false-after-effect may expose valid wire");
        check(!owner.publish(ops) && ops.publishes==1,"unknown exposure not retried");}
}
void preflight(){
    for(unsigned kind=0;kind<12;++kind){auto input=selections();auto s=session();Ops ops;p::Owner owner;
        switch(kind){case 0:s.pid=0;break;case 1:s.creation=0;break;case 2:s.nonce={};break;
        case 3:s.artifact+=0x10000;break;case 4:input[1].base=input[0].base;break;
        case 5:input[1].bytes=0xFFFF;break;case 6:input[1].anchor_rva=input[1].bytes-31;break;
        case 7:input[1].expected_file_hash={};break;case 8:input[1].path.fill(L'x');break;
        case 9:input[1].native_path[0]=L'C';break;case 10:input[1].path=input[0].path;break;
        case 11:input[1].path[500]=L'x';break;}
        check(!owner.prepare(s,input,ops) && ops.events.empty(),"invalid preflight before OS calls");}
    Ops ops;p::Owner owner;check(!owner.prepare(session(),{},ops),"empty selection refuses");
    std::array<p::Selection,33> many{};p::Owner oversized;check(!oversized.prepare(session(),many,ops),"over count refuses");
    for(auto path:{L"C:\\a\\..\\b",L"C:\\a:stream",L"C:/a",L"\\\\server\\share",L"C:\\a\\"}){
        p::Path pth{};std::copy_n(path,std::char_traits<wchar_t>::length(path),pth.begin());check(!p::path_shape(pth,false),"unsupported path refuses");}
}
void ownership(){
    auto input=selections();Ops ops;ops.mutable_input=&input;p::Owner owner;
    check(owner.prepare(session(),input,ops) && owner.publish(ops),"caller mutation cannot alter owned selection");
    auto copied=selections();Ops a,b;p::Owner wrong;
    check(wrong.prepare(session(),copied,a),"owner bound");const auto calls=a.events.size();
    check(!wrong.publish(b) && b.events.empty() && a.events.size()==calls,"wrong Ops no effects");
    ++a.tid;check(!wrong.publish(a) && a.events.size()==calls,"wrong thread no effects");
    --a.tid;a.identity_ok=false;check(!wrong.publish(a) && wrong.state()==p::State::Refused,"changed session refuses before publish");
    static_assert(!std::is_copy_constructible_v<p::Owner> && !std::is_move_constructible_v<p::Owner>);
}
}
int main(){positive();failure_cases();preflight();ownership();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
