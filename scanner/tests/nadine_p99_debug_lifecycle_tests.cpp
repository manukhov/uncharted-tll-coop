#include "nadine_p99_debug_lifecycle.h"
#include <array>
#include <iostream>
namespace dl=coop::p99_debug_lifecycle;
namespace {
unsigned checks=0,failures=0;
void check(bool value,const char* label){++checks;if(!value){++failures;std::cerr<<label<<'\n';}}
enum Call{Identity,Unattached,Attach,Kill,Receive,EventIdentity,Continue,Detach};
struct Ops {
    std::uint32_t tid=7;
    std::array<Call,32> calls{};unsigned count=0,fail=0;
    bool after_effect=false,attached=false,stopped=false,killed_on_exit=true;
    std::uint32_t thread() const noexcept{return tid;}
    bool record(Call c) noexcept{calls[count++]=c;return count!=fail;}
    bool identity() noexcept{return record(Identity);}
    bool no_debugger() noexcept{return record(Unattached);}
    bool attach() noexcept{const bool okay=record(Attach);if(okay||after_effect)attached=true;return okay;}
    bool disable_kill() noexcept{const bool okay=record(Kill);if(okay||after_effect)killed_on_exit=false;return okay;}
    bool receive() noexcept{const bool okay=record(Receive);if(okay||after_effect)stopped=true;return okay;}
    bool event_identity() noexcept{return record(EventIdentity);}
    bool proceed() noexcept{const bool okay=record(Continue);if(okay||after_effect)stopped=false;return okay;}
    bool detach() noexcept{const bool okay=record(Detach);if(okay||after_effect)attached=false;return okay;}
};
void test() {
    for(unsigned fail=1;fail<=6;++fail)for(bool effect:{false,true}) {
        Ops ops;ops.fail=fail;ops.after_effect=effect;
        {
            dl::Session s;
            check(!s.acquire(ops) && ops.count==fail,"acquire refuses each failed prerequisite");
            const std::array expected{dl::State::Refused,dl::State::Refused,dl::State::AttachUnknown,
                dl::State::KillPolicyFailed,dl::State::AttachedNoEvent,dl::State::EventRejected};
            check(s.state()==expected[fail-1] && !s.held() && !s.barrier_ready() && !s.acknowledged(),"no acquired stop from failure/unknown event");
            check(!s.acquire(ops) && !s.release(ops,true) && ops.count==fail,"refused attempt cannot replay or release without hold");
        }
        check(ops.count==fail,"destruction makes NO implicit continue/detach/kill");
    }
    for(unsigned fail=1;fail<=4;++fail)for(bool effect:{false,true}) {
        Ops ops;dl::Session s;
        check(s.acquire(ops) && s.barrier_ready() && ops.attached && ops.stopped && !ops.killed_on_exit,"actual held condition after six successful steps");
        const auto before=ops.count;
        check(!s.release(ops,false) && s.held() && !s.barrier_ready() && ops.count==before && ops.stopped,"unsafe release performs zero OS calls and retains stop");
        ops.fail=before+fail;ops.after_effect=effect;
        check(!s.release(ops,true) && !s.held() && !s.barrier_ready() && !s.acknowledged(),"any release attempt revokes stop certificate");
        const std::array expected{dl::State::ReleaseUnknown,dl::State::ContinuedDetachFailed,
            dl::State::DetachedUnverified,dl::State::DetachedUnverified};
        check(s.state()==expected[fail-1] && ops.count==before+fail,"release and detach failures remain distinct");
        check(!s.release(ops,true) && !s.release(ops,false) && ops.count==before+fail,"no implicit retry/rollback on stale held state");
        if(fail==1)check(ops.stopped==!effect,"false continue may or may not have released actual stop");
    }
    {
        Ops ops;dl::Session s;check(s.acquire(ops),"success acquire");
        Ops other;
        check(!s.release(other,true) && s.held() && !other.count && ops.count==6,"same-thread foreign target/ops cannot release or ACK this session");
        ops.tid=9;check(!s.release(ops,true) && s.held() && ops.count==6,"foreign owner cannot release");
        ops.tid=7;check(s.release(ops,true) && s.acknowledged() && !s.held() && !ops.attached && !ops.stopped,"only verified detach can ACK");
        check(ops.calls[6]==Continue && ops.calls[7]==Detach && ops.calls[8]==Unattached && ops.calls[9]==Identity,"fixed release order and fresh detach/identity checks");
        check(!s.release(ops,true) && !s.acquire(ops) && ops.count==10,"success is one-shot");
    }
    {
        Ops ops;ops.tid=0;dl::Session s;check(!s.acquire(ops) && !ops.count,"missing owner before OS action");
    }
    {
        Ops ops;
        {dl::Session s;check(s.acquire(ops) && !s.release(ops,false),"owned unresolved hold fixture");}
        check(ops.count==6 && ops.stopped && ops.attached,"no auto-detach destructor on uncertain write state");
    }
}
}
int main(){test();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
