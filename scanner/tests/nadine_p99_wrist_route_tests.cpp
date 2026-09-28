// Executes the ACTUAL uninstalled route .inl, with real FLS/policies/bridges.
// Owner/grammar/program/storage admission and QPC/ticks are deliberately fake.
// This cannot prove their production implementations, installer, game ownership,
// pose values, timing or gameplay. Every native target below is a test fixture.
#include <Windows.h>
#include "nadine_p99_group_entry.h"
#include "nadine_p99_group_fiber.h"
#include "nadine_p99_group_native_frame.h"
#include "nadine_p99_process_native_frame.h"
#include "nadine_p99_source_observation.h"
#include "nadine_p99_wrist_invoke.h"
#include "nadine_p99_wrist_snapshot.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <source_location>
#include <string_view>

unsigned checks=0, failures=0;
void check(bool value, std::source_location where=std::source_location::current()) {
    ++checks;
    if (!value) { ++failures; std::cerr << "failed line " << where.line() << '\n'; }
}
namespace p99_observe=coop::p99_source_observation;
namespace p99_wrist_snapshot=coop::p99_wrist_snapshot;
struct P99GroupJob;
struct P99GroupProcess;
struct P99WristJob;
struct P99WristProcess;
// Match the route's containing FLS binding. Legacy pointers must stay null.
struct P99FiberFrame : coop::p99_group_fiber::Binding {
    P99GroupJob* job=nullptr;
    P99GroupProcess* process=nullptr;
    std::uintptr_t native_return_slot=0;
    coop::p99_group_entry::Active entry{};
    P99WristJob* wrist=nullptr;
    P99WristProcess* wrist_process=nullptr;
};
coop::p99_group_fiber::Slot<P99FiberFrame> g_p99_group_fiber;
// Fake data-phase values. The included route treats owner as an opaque token;
// only Process.parent is consumed by its actual native-frame checks.
struct P99WristOwner { unsigned token=0; };
struct P99WristProcessValues { std::uintptr_t parent=0; };
struct { bool enabled=true; } g_state_net;
bool g_net_role_b=true;
std::uintptr_t g_module=0;
constexpr std::uintptr_t kAnimManagerAsyncObjectUpdateJointBlendJobRva=0x1A92A00;
constexpr std::uintptr_t kProcessAnimCmdListSegmentRva=0x1AB97C0;
constexpr std::uintptr_t kAnimCmdExecuteJointProgramsRva=0x217B630;

enum class Action { normal, owner_refuse, process_refuse, read_refuse, callback_fault,
    cmd16_fault, process_fault, async_fault, budget_before, budget_after, expiry,
    old_async, wrapped_async, old_process, nested_cmd16, other_lease_fault,
    disable_ready, disable_role, poison_scope, duplicate_g0 };
struct Fixture {
    Action action=Action::normal;
    unsigned async_calls=0, process_calls=0, cmd16_calls=0, cmd16_completed=0;
    unsigned owners=0, process_admissions=0, snapshots=0, caught=0, depth=0;
    unsigned process_depth=0, cmd_depth=0, current_group=0;
    std::array<std::array<const void*,2>,2> parents{};
    std::array<std::uint32_t,4> commands{0x1234,0x5678,0x9ABC,0xDEF0};
    std::array<std::uintptr_t,3> stack_args{0xAAAA,0xBBBB,0xCCCC};
    std::uint32_t* expected_status=nullptr;
    const void* expected_segment=nullptr;
    bool async_body_finished=false, foreign_lease=false;
};
Fixture* fixture=nullptr;
std::uint64_t fake_ms=1000, fake_ticks=100;
constexpr DWORD kNativeFault=0xE0420495, kCaptureFault=0xE0420595;
constexpr std::uint32_t kProcessResult=0xF1234ABC;
struct P99CostStamp { LARGE_INTEGER value{}; bool valid=false; };
P99CostStamp p99_group_cost_start() {
    fake_ticks+=fixture->action==Action::budget_before ? 2000 : 1;
    P99CostStamp stamp{};
    stamp.value.QuadPart=static_cast<LONGLONG>(fake_ticks);
    stamp.valid=true;
    return stamp;
}
ULONGLONG fixture_tick_count() { return fake_ms; }
bool seh_p99_group_entry_observations(void* job,
    coop::p99_group_entry::Observations* out) {
    check(job==fixture);
    out->slot_before=out->fg=out->slot_after={0xA000,7};
    return true;
}
bool seh_p99_wrist_owner(void* job,P99WristOwner* out) {
    ++fixture->owners;
    check(job==fixture);
    out->token=17;
    return fixture->action!=Action::owner_refuse && fixture->action!=Action::other_lease_fault;
}
bool seh_p99_wrist_process(const std::uint32_t* commands,std::uint32_t words,
    const void* parent,std::uint32_t group,const void* callback,
    const P99WristOwner& owner,P99WristProcessValues* out) {
    ++fixture->process_admissions;
    check(commands==fixture->commands.data() && words==4 && group==0);
    check(callback==&fixture->stack_args[1] && owner.token==17);
    out->parent=reinterpret_cast<std::uintptr_t>(parent);
    return fixture->action!=Action::process_refuse;
}
bool seh_p99_wrist_snapshot(void*,const P99WristOwner&,
    const P99WristProcessValues&,const void*,p99_wrist_snapshot::Result*);

// Only time is substituted. Route function bodies themselves are included,
// not rewritten as a state-machine model. Windows FLS/SEH are not mocked.
#define GetTickCount64 fixture_tick_count
#include "../src/nadine_p99_wrist_observation_runtime.inl"
#undef GetTickCount64

bool seh_p99_wrist_snapshot(void* job,const P99WristOwner& owner,
    const P99WristProcessValues& process,const void* segment,
    p99_wrist_snapshot::Result* out) {
    ++fixture->snapshots;
    check(job==fixture && owner.token==17 && fixture->current_group==0);
    check(segment==fixture->expected_segment && fixture->cmd16_completed>=1);
    check(process.parent==reinterpret_cast<std::uintptr_t>(fixture->parents[fixture->depth][0]));
    check(!g_p99_wrist_payload_ready.load() && !fixture->async_body_finished);
    auto* frame=g_p99_group_fiber.current();
    check(frame && frame->wrist && frame->wrist_process && frame->job==nullptr && frame->process==nullptr);
    if (fixture->action==Action::callback_fault) {
        std::memset(out->payload.bytes.data(),0xA5,19); // Unpublished partial read.
        RaiseException(kCaptureFault,0,0,nullptr);
    }
    if (fixture->action==Action::read_refuse) return false;
    out->reason=p99_wrist_snapshot::Reject::None;
    for (std::size_t i=0;i<out->payload.bytes.size();++i)
        out->payload.bytes[i]=static_cast<std::byte>((i*13+7)&255);
    if (fixture->action==Action::budget_after) fake_ticks+=2000;
    return true;
}

extern "C" void p99_wrist_route_async_fixture(void*);
extern "C" std::uint32_t p99_process_frame_fixture(const std::uint32_t*,std::uint32_t,
    const void*,std::uint32_t,const void*,const void*,const void*);

std::uint32_t* __fastcall fixture_cmd16(std::uint32_t* status,const void* segment) {
    ++fixture->cmd16_calls;
    check(status==fixture->expected_status && segment==fixture->expected_segment);
    check(!g_p99_wrist_payload_ready.load());
    *status=0; // Explicit fake stock write to its fixture-owned stack output.
    if (fixture->action==Action::cmd16_fault && fixture->current_group==0)
        RaiseException(kNativeFault,0,0,nullptr);
    if (fixture->action==Action::nested_cmd16 && fixture->current_group==0 && fixture->cmd_depth==0) {
        ++fixture->cmd_depth;
        check(hooked_p99_wrist_cmd16(status,segment)==status);
        --fixture->cmd_depth;
    }
    ++fixture->cmd16_completed;
    return status;
}

extern "C" std::uint32_t p99_process_fixture_body(const std::uint32_t* commands,
    std::uint32_t words,const void* context,std::uint32_t group,
    const void* a5,const void* a6,const void* a7,std::uintptr_t actual_slot,
    std::uintptr_t status,std::uintptr_t segment) {
    ++fixture->process_calls;
    check(commands==fixture->commands.data() && words==4 && group<8);
    check(context==fixture->parents[fixture->depth][group==0?0:1]);
    check(a5==&fixture->stack_args[0] && a6==&fixture->stack_args[1] && a7==&fixture->stack_args[2]);
    check(coop::p99_process_native_frame::owns(status,segment,actual_slot));
    if (fixture->action==Action::old_process && group==0 && fixture->process_depth==0) {
        ++fixture->process_depth;
        check(p99_process_frame_fixture(commands,words,context,group,a5,a6,a7)==kProcessResult);
        --fixture->process_depth;
    }
    fixture->current_group=group;
    fixture->expected_status=reinterpret_cast<std::uint32_t*>(status);
    fixture->expected_segment=reinterpret_cast<const void*>(segment);
    check(hooked_p99_wrist_cmd16(fixture->expected_status,fixture->expected_segment)==fixture->expected_status);
    check(!g_p99_wrist_payload_ready.load());
    if (fixture->action==Action::process_fault && group==0) RaiseException(kNativeFault,0,0,nullptr);
    return kProcessResult;
}

extern "C" void p99_wrist_route_async_body(void* job,void* first,void* second,
    std::uintptr_t actual_slot) {
    ++fixture->async_calls;
    check(job==fixture && fixture->depth<2);
    fixture->parents[fixture->depth]={first,second};
    check(coop::p99_group_native_frame::owns(true,reinterpret_cast<std::uintptr_t>(first),actual_slot));
    check(coop::p99_group_native_frame::owns(false,reinterpret_cast<std::uintptr_t>(second),actual_slot));
    if (fixture->action==Action::other_lease_fault) {
        fake_ms+=p99_observe::kProbeIntervalMs+1;
        check(g_p99_wrist_policy.begin_probe(fake_ms)==p99_observe::Probe::kGranted);
        fixture->foreign_lease=true;
        RaiseException(kNativeFault,0,0,nullptr);
    }
    if ((fixture->action==Action::old_async || fixture->action==Action::wrapped_async) && fixture->depth==0) {
        ++fixture->depth;
        if (fixture->action==Action::old_async) p99_wrist_route_async_fixture(job);
        else hooked_p99_wrist_async(job);
        --fixture->depth;
    }
    for (std::uint32_t group=0;group<8;++group) {
        const auto call=group==0?&hooked_p99_wrist_process0:&hooked_p99_wrist_process1;
        check(call(fixture->commands.data(),4,group==0?first:second,group,
            &fixture->stack_args[0],&fixture->stack_args[1],&fixture->stack_args[2])==kProcessResult);
        check(!g_p99_wrist_payload_ready.load());
        if (group==0) {
            if (fixture->action==Action::disable_ready) g_p99_wrist_ready.store(false);
            if (fixture->action==Action::disable_role) g_net_role_b=false;
            if (fixture->action==Action::poison_scope) g_p99_group_fiber.poison();
            if (fixture->action==Action::duplicate_g0)
                check(hooked_p99_wrist_process0(fixture->commands.data(),4,first,0,
                    &fixture->stack_args[0],&fixture->stack_args[1],&fixture->stack_args[2])==kProcessResult);
        }
    }
    if (fixture->action==Action::async_fault) RaiseException(kNativeFault,0,0,nullptr);
    if (fixture->action==Action::expiry) fake_ms+=p99_observe::kLifetimeMs;
    // Parent can still cancel before its normal return; no global publish here.
    if (fixture->depth==0) fixture->async_body_finished=true;
}

bool make_fixture_image() {
    constexpr std::size_t extent=0x2180000;
    auto* image=static_cast<std::byte*>(VirtualAlloc(nullptr,extent,MEM_RESERVE,PAGE_NOACCESS));
    if (!image) return false;
    g_module=reinterpret_cast<std::uintptr_t>(image);
    const std::array<std::pair<std::uintptr_t,std::uintptr_t>,3> sites{{
        {kAnimManagerAsyncObjectUpdateJointBlendJobRva,reinterpret_cast<std::uintptr_t>(&p99_wrist_route_async_fixture)},
        {kProcessAnimCmdListSegmentRva,reinterpret_cast<std::uintptr_t>(&p99_process_frame_fixture)},
        {kAnimCmdExecuteJointProgramsRva,reinterpret_cast<std::uintptr_t>(&fixture_cmd16)}}};
    for (const auto& [rva,target]:sites) {
        auto* page=image+(rva&~std::uintptr_t{0xFFF});
        if (!VirtualAlloc(page,0x1000,MEM_COMMIT,PAGE_READWRITE)) return false;
        std::array<std::byte,14> jump{};
        jump[0]=std::byte{0xFF};jump[1]=std::byte{0x25};
        std::memcpy(jump.data()+6,&target,sizeof(target));
        std::memcpy(image+rva,jump.data(),jump.size());
        DWORD old=0;
        if (!VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&old)) return false;
        FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    }
    return true;
}

void run_actual_route() {
    __try { hooked_p99_wrist_async(fixture); }
    __except (GetExceptionCode()==kNativeFault ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        ++fixture->caught;
    }
}

int main(int argc,char** argv) {
    // One fresh OS process per terminal policy case: no artificial policy/FLS reset.
    constexpr std::array<std::pair<std::string_view,Action>,20> cases{{
        {"normal",Action::normal},{"owner-refuse",Action::owner_refuse},
        {"process-refuse",Action::process_refuse},{"read-refuse",Action::read_refuse},
        {"callback-fault",Action::callback_fault},{"cmd16-fault",Action::cmd16_fault},
        {"process-fault",Action::process_fault},{"async-fault",Action::async_fault},
        {"budget-before",Action::budget_before},{"budget-after",Action::budget_after},
        {"expiry",Action::expiry},{"old-async",Action::old_async},
        {"wrapped-async",Action::wrapped_async},{"old-process",Action::old_process},
        {"nested-cmd16",Action::nested_cmd16},{"other-lease-fault",Action::other_lease_fault},
        {"disable-ready",Action::disable_ready},{"disable-role",Action::disable_role},
        {"poison-scope",Action::poison_scope},{"duplicate-g0",Action::duplicate_g0}}};
    if (argc!=2) { std::cerr << "one named case required\n"; return 2; }
    Fixture data{};
    bool found=false;
    for (const auto& [name,action]:cases) if (name==argv[1]) { data.action=action;found=true; }
    if (!found) return 2;
    fixture=&data;
    if (!make_fixture_image()) return 3;
    check(g_p99_group_fiber.initialize());
    check(g_p99_wrist_policy.arm(fake_ms,1000000));
    g_p99_wrist_cost_limit=1000;
    g_p99_wrist_ready.store(true);
    run_actual_route();
    check(g_p99_group_fiber.current()==nullptr);
    const bool success=data.action==Action::normal;
    check(g_p99_wrist_payload_ready.load()==success);
    if (success) {
        check(data.async_body_finished && data.async_calls==1 && data.process_calls==8 && data.cmd16_calls==8);
        check(data.snapshots==1 && data.owners==1 && data.process_admissions==1);
        check(g_p99_wrist_policy.reason()==p99_observe::Reason::kSuccess);
        for (std::size_t i=0;i<g_p99_wrist_payload.bytes.size();++i)
            check(g_p99_wrist_payload.bytes[i]==static_cast<std::byte>((i*13+7)&255));
    } else for (const auto byte:g_p99_wrist_payload.bytes) check(byte==std::byte{});
    const bool fault=data.action==Action::cmd16_fault || data.action==Action::process_fault ||
        data.action==Action::async_fault || data.action==Action::other_lease_fault;
    check(data.caught==(fault?1u:0u));
    if (data.action==Action::other_lease_fault) {
        check(data.foreign_lease && g_p99_wrist_policy.reason()==p99_observe::Reason::kNone);
        check(g_p99_wrist_policy.begin_probe(fake_ms)==p99_observe::Probe::kBusy);
        check(!g_p99_wrist_policy.finish_probe(fake_ms,false)); // End the explicitly fake other lease.
        check(g_p99_wrist_policy.reason()==p99_observe::Reason::kNone);
    } else if (data.action==Action::owner_refuse) {
        check(g_p99_wrist_policy.reason()==p99_observe::Reason::kNone && data.snapshots==0);
        check(g_p99_wrist_policy.attempts()==0 && data.process_calls==8 && data.cmd16_calls==8);
    } else if (data.action==Action::budget_before || data.action==Action::budget_after)
        check(g_p99_wrist_policy.reason()==p99_observe::Reason::kBudget);
    else if (data.action==Action::expiry)
        check(g_p99_wrist_policy.reason()==p99_observe::Reason::kExpired);
    else if (fault) check(g_p99_wrist_policy.reason()==p99_observe::Reason::kException);
    else if (!success) check(g_p99_wrist_policy.reason()==p99_observe::Reason::kRefused);
    if (data.action==Action::old_async || data.action==Action::wrapped_async)
        check(data.async_calls==2 && data.process_calls==16 && data.cmd16_calls==16 && data.snapshots==0);
    if (data.action==Action::old_process)
        check(data.process_calls==9 && data.cmd16_calls==9 && data.snapshots==0);
    if (data.action==Action::nested_cmd16)
        check(data.process_calls==8 && data.cmd16_calls==9 && data.snapshots==0);
    if (data.action==Action::cmd16_fault) check(data.cmd16_calls==1 && data.snapshots==0);
    if (data.action==Action::callback_fault) check(data.cmd16_calls==8 && data.snapshots==1);
    if (data.action==Action::process_fault)
        check(data.process_calls==1 && data.cmd16_calls==1 && data.snapshots==1);
    if (data.action==Action::read_refuse || data.action==Action::async_fault ||
        data.action==Action::budget_after || data.action==Action::expiry)
        check(data.process_calls==8 && data.cmd16_calls==8 && data.snapshots==1);
    if (data.action==Action::process_refuse || data.action==Action::budget_before)
        check(data.process_calls==8 && data.cmd16_calls==8 && data.snapshots==0);
    if (data.action==Action::disable_ready || data.action==Action::disable_role ||
        data.action==Action::poison_scope)
        check(data.async_calls==1 && data.process_calls==8 && data.cmd16_calls==8 && data.snapshots==1);
    if (data.action==Action::poison_scope) check(g_p99_group_fiber.failed());
    if (data.action==Action::duplicate_g0)
        check(data.async_calls==1 && data.process_calls==9 && data.cmd16_calls==9 && data.snapshots==1);
    // Fixture image only; all native/FLS scopes have ended. No game allocation.
    check(VirtualFree(reinterpret_cast<void*>(g_module),0,MEM_RELEASE)!=FALSE);
    std::cout << argv[1] << ": " << checks << " checks, " << failures << " failures\n";
    return failures?1:0;
}
