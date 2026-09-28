#include "nadine_p99_wrist_handshake.h"
#include <iostream>
#include <thread>

namespace wh=coop::p99_wrist_handshake;
namespace cm=coop::p99_wrist_code_manifest;
namespace wi=coop::p99_wrist_install;
namespace {
unsigned checks=0,failures=0;
void check(bool yes,const char* label) {++checks;if(!yes){++failures;std::cerr<<label<<'\n';}}
wh::Offer offer() {
    wh::Offer o{};o.pid=123;o.creation=456;o.nonce={789,987};
    o.module=0x140000000;o.image_bytes=0x8000000;
    o.dll=0x7FF000000000;o.dll_bytes=0x100000;o.frequency=10000000;
    o.originals={o.module+0x1A92A00,o.module+0x1AB97C0,o.module+0x217B630};
    o.hooks={o.dll+0x1000,o.dll+0x1000,o.dll+0x2000,o.dll+0x3000,o.dll+0x4000};
    for(unsigned i=0;i<5;++i)o.relays[i]=o.module+0x10000000+i*0x1000;
    return o;
}
struct Ops {
    unsigned calls=0,fail=0,stocks=0,installed=0,detach_checks=0;
    bool good() noexcept {return ++calls!=fail;}
    bool identity(const wh::Offer&) noexcept {return good();}
    bool resources(const wh::Offer&,const wi::Plan&) noexcept {return good();}
    bool sites(const wi::Plan&,cm::Stage stage) noexcept {
        stage==cm::Stage::Stock?++stocks:++installed;return good();
    }
    bool code(cm::Stage stage) noexcept {stage==cm::Stage::Stock?++stocks:++installed;return good();}
    bool detached() noexcept {++detach_checks;return good();}
};
struct PausedOps : Ops {
    unsigned pause_at=0;
    std::atomic<bool> entered{false},release{false};
    bool identity(const wh::Offer& o) noexcept {
        const bool result=Ops::identity(o);
        if(calls==pause_at) {
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire)) std::this_thread::yield();
        }
        return result;
    }
    void await_entry() const noexcept {
        while(!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    }
};
void value_tests() {
    const auto o=offer();const auto wire=wh::encode(o);
    check(wh::decode(wire)==o,"round trip explicit little endian");
    check(wire[0]==0x50 && wire[3]==0x53 && wire[8]==1,"wire layout");
    check(wh::plan(o).has_value(),"reconstructed exact five-site plan");
    for(std::size_t n=0;n<wire.size();++n)
        check(!wh::decode(std::span(wire).first(n)),"truncation refuses");
    std::array<std::uint8_t,wh::kOfferBytes+1> extra{};
    std::copy(wire.begin(),wire.end(),extra.begin());check(!wh::decode(extra),"trailing bytes refuse");
    for(const auto word:{0,1,2,25,26,27,28,29}) for(unsigned byte=0;byte<8;++byte) {
        auto w=wire;w[word*8+byte]^=1;check(!wh::decode(w),"header/reserved bit refuses");
    }
    const auto rejects=[&](wh::Offer v){check(!wh::plan(v),"invalid offer refuses");check(!wh::decode(wh::encode(v)),"wire invalid offer refuses");};
    auto v=o;v.pid=0;rejects(v);v=o;v.pid=0x100000000;rejects(v);
    v=o;v.creation=0;rejects(v);v=o;v.nonce={};rejects(v);
    v=o;v.frequency=0;rejects(v);v=o;v.frequency=UINT64_MAX;rejects(v);
    v=o;v.frequency=1;rejects(v);v=o;v.frequency=999;rejects(v);
    v=o;v.frequency=1000;check(wh::plan(v).has_value(),"worker minimum frequency");
    for(auto member:{&wh::Offer::module,&wh::Offer::dll}) {
        v=o;v.*member=0;rejects(v);v=o;(v.*member)++;rejects(v);
        v=o;v.*member=UINT64_MAX-0xFFF;rejects(v);
    }
    for(auto member:{&wh::Offer::image_bytes,&wh::Offer::dll_bytes}) {
        v=o;v.*member=0;rejects(v);v=o;(v.*member)++;rejects(v);
        v=o;v.*member=0x100000000;rejects(v);
    }
    v=o;v.image_bytes=0x2000000;rejects(v);v=o;v.dll=o.module;rejects(v);
    for(unsigned i=0;i<3;++i){v=o;++v.originals[i];rejects(v);}
    for(unsigned i=0;i<5;++i) {
        v=o;v.hooks[i]=o.dll-1;rejects(v);v=o;v.hooks[i]=o.dll+o.dll_bytes;rejects(v);
        v=o;v.relays[i]=o.module;rejects(v);v=o;v.relays[i]=o.dll;rejects(v);
        v=o;v.relays[i]=0;rejects(v);v=o;++v.relays[i];rejects(v);
        v=o;v.relays[i]=0x7FF100000000;rejects(v);
        v=o;v.relays[i]=UINT64_MAX-0xFFF;rejects(v);
        if(i){v=o;v.relays[i]=o.relays[0];rejects(v);}
        if(i>1){v=o;v.hooks[i]=o.hooks[0];rejects(v);}
    }
    v=o;v.hooks[1]+=16;rejects(v);
}
void gate_tests() {
    const auto o=offer();const auto wire=wh::encode(o);
    const auto ack=wh::acknowledge(wire,wh::Completion::ResumedDetached);
    {
        wh::Gate gate;Ops ops;
        check(!gate.publication() && !gate.accept(ack,ops) && !gate.expire() && ops.calls==0,"empty cannot ack/publish/expire");
        auto bad=o;bad.pid=0;check(!gate.prepare(bad,ops) && ops.calls==0,"invalid prepare no ops");
        check(gate.state()==wh::State::Refused && !gate.prepare(o,ops),"invalid one-shot consumed");
    }
    for(unsigned f=1;f<=4;++f) {
        wh::Gate gate;Ops ops;ops.fail=f;
        check(!gate.prepare(o,ops) && ops.calls==f,"prepare local proof fails");
        check(gate.state()==wh::State::Refused && !gate.publication() && !gate.accept(ack,ops),"no partial publication");
    }
    {
        wh::Gate gate;Ops ops;check(gate.prepare(o,ops),"prepare good");
        check(gate.state()==wh::State::Prepared && ops.stocks==2 && ops.installed==0,"stock only prep");
        auto published=gate.publication();check(published==wire,"owned immutable copy");
        (*published)[3]^=1;check(gate.publication()==wire,"caller mutation isolated");
        check(!gate.prepare(o,ops) && ops.calls==4,"duplicate prep no work");
        for(std::size_t b=0;b<wh::kOfferBytes;++b) {
            auto stale=ack;stale[b]^=1;
            check(!gate.accept(stale,ops) && gate.state()==wh::State::Prepared && ops.calls==4,"every identity byte stale refuses without consuming current session");
        }
        for(std::size_t n=0;n<ack.size();++n)
            check(!gate.accept(std::span(ack).first(n),ops) && ops.calls==4,"partial ack refuses");
        check(gate.accept(ack,ops) && gate.state()==wh::State::Installed,"fresh local verified installed");
        check(ops.calls==10 && ops.installed==2 && ops.detach_checks==2,"ack checks exact installed and detach twice");
        check(!gate.publication() && !gate.accept(ack,ops) && !gate.expire() && ops.calls==10,"success no replay/retry/free authority");
    }
    for(unsigned f=1;f<=6;++f) {
        wh::Gate gate;Ops ops;check(gate.prepare(o,ops),"prepare failure fixture");ops.fail=ops.calls+f;
        check(!gate.accept(ack,ops) && gate.state()==wh::State::Refused,"ack local failure terminal");
        check(ops.calls==4+f && !gate.accept(ack,ops),"failed ack cannot retry");
    }
    for(auto outcome:{wh::Completion::CleanRefusal,wh::Completion::HeldUncertain,
        wh::Completion::ContinuedDetachFailed,static_cast<wh::Completion>(0),static_cast<wh::Completion>(UINT64_MAX)}) {
        wh::Gate gate;Ops ops;check(gate.prepare(o,ops),"prepare outcome fixture");
        check(!gate.accept(wh::acknowledge(wire,outcome),ops) && ops.calls==4 &&
            gate.state()==wh::State::Refused,"non-success terminal; no arm or local repair attempted");
    }
    {
        wh::Gate gate;Ops ops;check(gate.prepare(o,ops) && gate.expire(),"prepared can expire");
        check(!gate.accept(ack,ops) && !gate.prepare(o,ops) && !gate.expire() && ops.calls==4,"expiry retains one-shot refusal");
    }
    {
        wh::Gate gate;PausedOps ops;ops.pause_at=1;bool result=false;
        std::thread t([&]{result=gate.prepare(o,ops);});ops.await_entry();
        check(gate.state()==wh::State::Preparing && !gate.publication(),"preparing not visible as prepared");
        check(!gate.prepare(o,ops) && !gate.accept(ack,ops) && !gate.expire(),"preparer owns initialization exclusively");
        ops.release.store(true,std::memory_order_release);t.join();
        check(result && ops.calls==4 && gate.publication()==wire,"single preparer publishes complete values");
    }
    {
        wh::Gate gate;PausedOps ops;check(gate.prepare(o,ops),"prepare paused verifier");
        ops.pause_at=5;bool result=false;
        std::thread t([&]{result=gate.accept(ack,ops);});ops.await_entry();
        check(gate.state()==wh::State::Verifying && !gate.expire(),"expiry cannot cancel owned verification");
        check(!gate.publication() && !gate.accept(ack,ops),"no new publication or duplicate ack while verifying");
        ops.release.store(true,std::memory_order_release);t.join();
        check(result && ops.calls==10 && gate.state()==wh::State::Installed,"owned verifier completes after losing expiry");
    }
    {
        wh::Gate gate;Ops ops;check(gate.prepare(o,ops),"prepare expiry wins fixture");
        std::atomic<bool> start{false};bool result=true;
        std::thread t([&]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();result=gate.accept(ack,ops);});
        check(gate.expire(),"expiry wins before accept CAS");
        start.store(true,std::memory_order_release);t.join();
        check(!result && ops.calls==4 && gate.state()==wh::State::Refused,"losing accept no local verification");
    }
    // Only one verifier may access Ops even if an IPC notification is duplicated.
    for(unsigned n=0;n<32;++n) {
        wh::Gate gate;Ops ops;check(gate.prepare(o,ops),"prepare concurrent ack");
        std::atomic<bool> start{false};bool a=false,b=false;
        std::thread t0([&]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();a=gate.accept(ack,ops);});
        std::thread t1([&]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();b=gate.accept(ack,ops);});
        start.store(true,std::memory_order_release);t0.join();t1.join();
        check(a!=b && ops.calls==10 && gate.state()==wh::State::Installed,"exactly one concurrent verifier");
    }
    for(unsigned n=0;n<32;++n) {
        wh::Gate gate;Ops ops;check(gate.prepare(o,ops),"prepare concurrent publication");
        std::atomic<bool> start{false};std::optional<wh::Wire> copied;bool result=false;
        std::thread t0([&]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();copied=gate.publication();});
        std::thread t1([&]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();result=n%2?gate.expire():gate.accept(ack,ops);});
        start.store(true,std::memory_order_release);t0.join();t1.join();
        check(result && (!copied || *copied==wire),"concurrent publication absent or whole immutable offer");
    }
}
}
int main(){value_tests();gate_tests();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
