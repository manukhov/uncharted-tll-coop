// Owned-process inclusive cost probe, NOT a game benchmark/admission certificate.
// No external PID, process attach, patch, input, or runtime configuration file.
// Deliberately links the real core without the FLS audit instrumentation.
#include "enemy_jointblend_hooks.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace h = coop::enemy_jointblend_hooks;
namespace {
constexpr DWORD kException=0xE0426128;
constexpr std::size_t kWarm=2048,kCold=64;
unsigned failures{};
bool wanted{}, copy_sink{};
std::uint64_t calls{}, events{}, exits{}, reentries{}, completions{};
alignas(64) std::array<std::byte,0x40B0+90*64> input{},output{};
h::NativeCalls wrapped{};
h::Process later{};
std::uint64_t frequency{};
struct Job {
    bool hooks{},throw_store{},reenter{},stop{},nested{};
    unsigned pause_ms{};
    std::uintptr_t checksum{};
};
std::uint64_t tick() noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 0;
}
void check(bool value,const char* label) {
    if (!value) { ++failures;std::printf("FAIL %s\n",label); }
}
__declspec(noinline) void* __fastcall native_reset(void* p,const void* root) { ++calls;return const_cast<void*>(root ? root : p); }
__declspec(noinline) std::uintptr_t __fastcall native_postpose(void*,void*,const void*,std::uint32_t id,const void* a5,const void* a6) {
    ++calls;return id ^ reinterpret_cast<std::uintptr_t>(a5) ^ reinterpret_cast<std::uintptr_t>(a6);
}
__declspec(noinline) std::uint32_t __fastcall native_process(const std::uint32_t* commands,std::uint32_t words,
    const void* context,std::uint32_t group,const void* a5,const void* a6,const void* a7) {
    ++calls;auto& job=*const_cast<Job*>(static_cast<const Job*>(context));
    const auto f=job.hooks ? wrapped.postpose : native_postpose;
    job.checksum ^= f(&job,&job,commands,group,a5,a6);
    return words ^ static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(a7));
}
__declspec(noinline) std::int32_t __fastcall native_store(void* p,const void* pose) {
    ++calls;
    if (static_cast<Job*>(p)->throw_store) RaiseException(kException,0,0,nullptr);
    return static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(pose));
}
__declspec(noinline) void __fastcall native_primary(void* p) {
    ++calls;auto& job=*static_cast<Job*>(p);
    const auto f=job.hooks ? wrapped.store : native_store;
    for (std::uintptr_t i=0;i<14;++i) job.checksum ^= static_cast<std::uint32_t>(f(p,reinterpret_cast<void*>(i+1)));
}
__declspec(noinline) void __fastcall native_jointblend(void* p) {
    ++calls;auto& job=*static_cast<Job*>(p);
    if (job.stop) wanted=false;
    if (job.pause_ms) Sleep(job.pause_ms); // Makes inclusive-native semantics falsifiable.
    if (job.reenter && !job.nested) {
        job.nested=true;
        (job.hooks ? wrapped.jointblend : native_jointblend)(p);
        job.nested=false;
    }
    const auto reset=job.hooks ? wrapped.reset : native_reset;
    job.checksum ^= reinterpret_cast<std::uintptr_t>(reset(p,p));
    for (unsigned group=0;group<2;++group) {
        const auto process=job.hooks ? (group ? later : wrapped.process) : native_process;
        job.checksum ^= process(reinterpret_cast<const std::uint32_t*>(p),0xFEDC1234,p,group,p,p,p);
    }
    (job.hooks ? wrapped.primary : native_primary)(p);
    SetLastError(0x1234);
}
bool requested(void*) noexcept { return wanted; }
void emit(void*,const h::Event& event) noexcept {
    ++events;
    if (event.kind==h::Kind::Exit) ++exits;
    if (event.kind==h::Kind::Reentry) ++reentries;
    // Explicit synthetic payload, not a replacement for measuring Collector's
    // pointer guards/metadata reads on an admitted live native invocation.
    if (copy_sink && event.kind==h::Kind::Entry) std::memcpy(output.data(),input.data(),0x40B0);
    if (copy_sink && event.kind==h::Kind::PostposeReturn)
        std::memcpy(output.data()+0x40B0,input.data()+0x40B0,90*64);
}
__declspec(noinline) bool invoke(h::JointBlend function,Job& job) {
    __try { function(&job); }
    __except(GetExceptionCode()==kException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
void complete(void*,const h::Completion& completion) noexcept {
    ++completions;
    check(completion.invocation_nonce && completion.cleanup_ok,"post-cleanup scalar completion");
}
std::uint64_t measure(Job& job,bool expected_throw) {
    const auto start=tick();
    const bool caught=invoke(job.hooks ? wrapped.jointblend : native_jointblend,job);
    const auto finish=tick(); // AFTER the complete wrapper return or SEH cleanup.
    check(caught==expected_throw,"native exception preserved");
    check(start && finish>=start,"monotonic external cost clock");
    return finish>=start ? finish-start : 0;
}
template<std::size_t N> void report(const char* name,std::array<std::uint64_t,N>& values) {
    std::sort(values.begin(),values.end());
    const auto us=[](std::uint64_t value){return 1000000.0*static_cast<double>(value)/static_cast<double>(frequency);};
    const auto over=std::count_if(values.begin(),values.end(),[](auto v){return v>frequency/1000;});
    std::printf("%s n=%zu p50_us=%.3f p95_us=%.3f p99_us=%.3f max_us=%.3f over_1ms=%zu\n",
        name,N,us(values[N/2]),us(values[N*95/100]),us(values[N*99/100]),us(values.back()),static_cast<std::size_t>(over));
}
void warm(const char* name,Job job,bool armed,bool copying) {
    std::array<std::uint64_t,kWarm> samples{};
    copy_sink=copying;
    const auto old_calls=calls,old_exits=exits,old_events=events,old_reentries=reentries,old_completions=completions;
    for (auto& sample:samples) { wanted=armed;sample=measure(job,job.throw_store); }
    check(calls>old_calls,"native calls never omitted");
    if (job.hooks && armed) check(exits-old_exits==kWarm && completions-old_completions==kWarm,
        "one exit and cleanup completion per complete measured wrapper");
    else check(events==old_events && completions==old_completions,"direct/dormant path has no observations");
    if (job.reenter && job.hooks && armed) check(reentries-old_reentries==kWarm,"nested stopped scopes rejected");
    report(name,samples);
}
struct Cold {
    bool armed{};
    void* caller{};
    std::uint64_t elapsed{};
};
DWORD WINAPI cold_thread(void* opaque) {
    auto& state=*static_cast<Cold*>(opaque);
    wanted=state.armed;Job job{};job.hooks=true;
    state.elapsed=measure(job,false);
    return 0;
}
void WINAPI cold_fiber(void* opaque) {
    auto& state=*static_cast<Cold*>(opaque);cold_thread(opaque);SwitchToFiber(state.caller);
}
void cold(bool fiber,bool armed) {
    std::array<std::uint64_t,kCold> samples{};
    void* caller=fiber ? ConvertThreadToFiber(nullptr) : nullptr;
    if (fiber && !caller) { check(false,"cold caller fiber");return; }
    for (auto& sample:samples) {
        Cold state{armed,caller};
        // Creation, scheduling/startup and destruction are OUTSIDE the timed
        // interval. First wrapper FLS access inside that new context is included.
        if (fiber) {
            void* f=CreateFiber(0,cold_fiber,&state);
            if (!f) { check(false,"cold fiber creation");break; }
            SwitchToFiber(f);DeleteFiber(f);
        } else {
            HANDLE t=CreateThread(nullptr,0,cold_thread,&state,0,nullptr);
            if (!t) { check(false,"cold thread creation");break; }
            if (WaitForSingleObject(t,5000)!=WAIT_OBJECT_0) ExitProcess(2);
            CloseHandle(t);
        }
        sample=state.elapsed;
    }
    if (fiber) check(ConvertFiberToThread()!=FALSE,"cold caller cleanup");
    report(fiber ? (armed ? "cold-fiber-armed" : "cold-fiber-dormant") :
        (armed ? "cold-thread-armed" : "cold-thread-dormant"),samples);
}
}
int main() {
    LARGE_INTEGER f{};
    if (!QueryPerformanceFrequency(&f) || f.QuadPart<1000) return 2;
    frequency=static_cast<std::uint64_t>(f.QuadPart);
    input.fill(std::byte{0x5A});
    check(h::configure({native_jointblend,native_reset,native_process,native_postpose,native_primary,native_store},
        {nullptr,requested,emit,complete}),"configure real core");
    const auto t=h::targets();
    wrapped={reinterpret_cast<h::JointBlend>(t.code[0]),reinterpret_cast<h::Reset>(t.code[2]),
        reinterpret_cast<h::Process>(t.code[3]),reinterpret_cast<h::Postpose>(t.postpose),
        reinterpret_cast<h::Primary>(t.code[5]),reinterpret_cast<h::Store>(t.code[6])};
    later=reinterpret_cast<h::Process>(t.code[4]);
    std::printf("OWNED_INCLUSIVE_COST_ONLY frequency=%llu; no subtraction; no game/collector/1ms admission\n",frequency);
    cold(false,false);cold(true,false);cold(false,true);cold(true,true);
    Job job{};warm("warm-direct-native",job,false,false);
    job.hooks=true;warm("warm-dormant",job,false,false);
    warm("warm-armed-empty-sink",job,true,false);
    warm("warm-armed-synthetic-copy",job,true,true);
    check(output==input,"synthetic bytes actually copied");
    job.stop=true;warm("warm-stop-in-native",job,true,false);
    job.reenter=true;warm("warm-nested-after-stop",job,true,false);
    job={};job.hooks=true;job.throw_store=true;warm("warm-SEH-store",job,true,false);
    job={};job.hooks=true;job.pause_ms=3;wanted=true;
    const auto inclusive=measure(job,false);
    check(inclusive>frequency/1000,"native delay remains inside reported envelope, not falsely subtracted");
    std::printf("slow-native-inclusive-us=%.3f failures=%u\n",1000000.0*inclusive/frequency,failures);
    return failures ? 1 : 0;
}
