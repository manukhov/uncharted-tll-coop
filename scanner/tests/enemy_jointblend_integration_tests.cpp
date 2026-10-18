// Real core + Collector + Windows Reader, owned memory/native fixtures ONLY.
// Exact return PCs are executable fixture calls, never rewritten Event fields.
// Test-only unwind-registered thunks and MASM frames model the pinned geometry;
// they are not the retail game, its installation, asset execution or scheduling.
#include "enemy_jointblend_collector.h"
#include "enemy_jointblend_read_windows.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace col = coop::enemy_jointblend_collector;
namespace cap = coop::enemy_jointblend_capture;
namespace obs = coop::enemy_jointblend_observation;
namespace hook = coop::enemy_jointblend_hooks;
namespace rd = coop::enemy_jointblend_read;
using A = std::uintptr_t;
extern "C" void enemy_fixture_jointblend(void*);
extern "C" std::uint32_t enemy_fixture_process(const std::uint32_t*,std::uint32_t,const void*,std::uint32_t,const void*,const void*,const void*);
namespace {
constexpr DWORD kException = 0xE0426130, kError = 0x31415926;
unsigned failures{};
void check(bool yes,const char* text) { if (!yes) { ++failures;std::printf("FAIL %s\n",text); } }
std::uint64_t now(void*) noexcept { LARGE_INTEGER v{};return QueryPerformanceCounter(&v) ? v.QuadPart : 0; }
std::uint64_t fixed_tick(void*) noexcept { return 1000000; }
hook::Targets targets{};
struct Fixture;
Fixture* current{}; // All fixture invocations serialized; never used by production.

struct Image {
    static constexpr std::size_t size = 0x7000000;
    A base{};
    std::vector<RUNTIME_FUNCTION> unwind;
    bool registered{};
    Image() : base(reinterpret_cast<A>(VirtualAlloc(nullptr,size,MEM_RESERVE,PAGE_NOACCESS))) {
        if (!base) throw "image reserve";
        page(0x10000); page(cap::kForceMode2Rva);
    }
    ~Image() {
        if (registered) RtlDeleteFunctionTable(unwind.data());
        if (base) VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
    }
    void page(std::uint32_t rva) {
        if (!VirtualAlloc(reinterpret_cast<void*>(base+(rva&~0xFFFu)),0x1000,MEM_COMMIT,PAGE_READWRITE)) throw "image commit";
    }
    // A normal unwind-described x64 call thunk. It forwards all register args
    // and n stack args, and CALL really pushes the required fixture return PC.
    A thunk(std::uint32_t return_rva,A target,unsigned stack,unsigned stack_args=0) {
        const auto start=return_rva-0x80;
        page(start);
        auto* code=reinterpret_cast<unsigned char*>(base+start);
        std::memset(code,0x90,0x90);
        std::size_t at=0;
        auto byte=[&](unsigned v){code[at++]=static_cast<unsigned char>(v);};
        auto dword=[&](unsigned v){std::memcpy(code+at,&v,4);at+=4;};
        byte(0x48);byte(0x81);byte(0xEC);dword(stack);
        for (unsigned i=0;i<stack_args;++i) {
            byte(0x48);byte(0x8B);byte(0x84);byte(0x24);dword(stack+0x28+8*i);
            byte(0x48);byte(0x89);byte(0x44);byte(0x24);byte(0x20+8*i);
        }
        if (at>0x80-12) throw "thunk overflow";
        at=0x80-12;byte(0x48);byte(0xB8);std::memcpy(code+at,&target,8);at+=8;
        byte(0xFF);byte(0xD0); // Return at start+80 exactly.
        byte(0x48);byte(0x81);byte(0xC4);dword(stack);byte(0xC3);
        const auto unwind_rva=0x10000u+static_cast<unsigned>(unwind.size())*8u;
        const unsigned short scaled=static_cast<unsigned short>(stack/8);
        unsigned char info[8]{1,7,2,0,7,1,static_cast<unsigned char>(scaled),static_cast<unsigned char>(scaled>>8)};
        std::memcpy(reinterpret_cast<void*>(base+unwind_rva),info,sizeof(info));
        unwind.push_back({start,start+static_cast<DWORD>(at),unwind_rva});
        return base+start;
    }
    void seal() {
        std::sort(unwind.begin(),unwind.end(),[](const auto& a,const auto& b){return a.BeginAddress<b.BeginAddress;});
        for (const auto& entry:unwind) {
            DWORD old{};
            if (!VirtualProtect(reinterpret_cast<void*>((base+entry.BeginAddress)&~A{0xFFF}),0x1000,PAGE_EXECUTE_READ,&old)) throw "code protection";
        }
        DWORD old{};
        if (!VirtualProtect(reinterpret_cast<void*>(base+0x10000),0x1000,PAGE_READONLY,&old) ||
            !VirtualProtect(reinterpret_cast<void*>((base+cap::kForceMode2Rva)&~A{0xFFF}),0x1000,PAGE_READONLY,&old) ||
            !FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base),size)) throw "seal image";
        registered=RtlAddFunctionTable(unwind.data(),static_cast<DWORD>(unwind.size()),base)!=FALSE;
        if (!registered) throw "unwind register";
    }
};
struct Fixture {
    Image image;
    A arena{},job{},owner{},record{},fg{},control{},art{},raw{},composite{},bodies{},matrices{},locators{},native{},pose{},commands{},payload{};
    rd::Reader reader;
    std::unique_ptr<col::Collector<rd::Reader>> collector;
    hook::Reset reset{};
    hook::Process process{};
    hook::Postpose postpose{};
    hook::Primary primary{};
    hook::Store refresh{};
    unsigned body_count=16;
    std::array<unsigned,6> calls{};
    bool revoke_art{},throw_store{},stop_native{},wrong_return{},wrong_context{};
    std::uint64_t exit_reads{};
    unsigned native_pause_ms{};
    static constexpr std::size_t arena_size=0x40000;
    template<class T> static void put(A at,const T& value) { std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value)); }
    Fixture(bool real_clock,std::uint64_t frequency) {
        arena=reinterpret_cast<A>(VirtualAlloc(nullptr,arena_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if (!arena) throw "arena";
        job=arena;owner=arena+0x1000;record=arena+0x2000;fg=arena+0x3000;control=arena+0x4000;
        art=arena+0x5000;raw=arena+0x6000;composite=arena+0x10000;bodies=arena+0x11000;
        matrices=arena+0x13000;locators=arena+0x15000;native=arena+0x17000;pose=arena+0x19000;
        commands=arena+0x1A000;payload=arena+0x1B000;
        put(job,control);put(job+8,fg);put(owner,image.base+obs::kPirateRva);put(record+8,owner);
        put(owner+0xA8,record);put(owner+0xBC,std::uint32_t{37});put(owner+0x1A0,control);put(owner+0x1A8,fg);put(owner+0x238,composite);
        put(control+0x20,fg);put(fg,std::uint32_t{0x20});put(fg+6,std::uint8_t{2});put(fg+8,std::uint8_t{1});
        put(fg+0x10,control);put(fg+0x18,art);put(fg+0x1A0,art);put(fg+0x1A8,record);put(fg+0x1B0,std::uint32_t{37});
        put(fg+0xF0,art);put(fg+0xF8,raw);put(fg+0x100,pose);put(fg+0x108,locators);put(fg+0x110,matrices);
        put(art+0x24,obs::kNormalDatabase);put(art+0x30,std::uint32_t{6});put(art+0x40,raw);
        put(raw+4,obs::kNormalSkeleton);put(raw+0x16,std::uint8_t{6});put(raw+0x2C,std::uint32_t{0x40});put(raw+0x34,std::uint32_t{0x140});
        put(raw+0x4F,std::uint8_t{1});put(raw+0x50,std::uint16_t{90});put(raw+0x52,std::uint16_t{55});
        put(raw+0x142,std::uint16_t{0x40B0});put(raw+0x144,std::uint32_t{0x5340});
        put(composite+8,owner);put(composite+0x10,body_count);put(composite+0x48,bodies);
        for (unsigned b=0;b<body_count;++b) {
            const A body=bodies+0x110*b,n=native+0x180*b;
            put(n+0x160,std::uint8_t{4});put(body+0x40,n);put(body+0x68,owner);
            put(body+0x50,std::uint32_t{100+b});put(body+0x54,static_cast<std::uint16_t>(10+b));
            put(body+0xA0,std::uint32_t{2u<<13});put(body+0xC4,static_cast<std::int16_t>(b+2));put(body+0xC6,static_cast<std::uint8_t>(b ? 3 : 1));
        }
        put(fg+0x70,UINT64_C(0x3FFF8));put(payload,matrices);put(pose,col::Words8{1,2,3,4,5,6,7,8});
        reset=reinterpret_cast<hook::Reset>(image.thunk(0x1A92BAA,targets.code[2],0x28));
        process=reinterpret_cast<hook::Process>(image.thunk(0x1A92E7D,targets.code[3],0x38,3));
        postpose=reinterpret_cast<hook::Postpose>(image.thunk(0x1ABAC69,targets.postpose,0x38,2));
        primary=reinterpret_cast<hook::Primary>(image.thunk(0x1BBE11E,targets.code[5],0x28));
        const auto body_refresh=image.thunk(0x1CF0F3B,targets.code[6],0xA8);
        refresh=reinterpret_cast<hook::Store>(image.thunk(0x1CF5872,body_refresh,0x28));
        image.seal();
        protect(art,0xB000,PAGE_READONLY); // Art, raw and authored program are immutable fixture sources.
        collector=std::make_unique<col::Collector<rd::Reader>>(reader,
            coop::enemy_jointblend_native_contract::ModuleImage{image.base,Image::size},
            col::Clock{nullptr,real_clock ? now : fixed_tick,frequency});
    }
    ~Fixture() { if (arena) VirtualFree(reinterpret_cast<void*>(arena),0,MEM_RELEASE); }
    static void protect(A at,std::size_t bytes,DWORD access) {
        DWORD old{};if (!VirtualProtect(reinterpret_cast<void*>(at),bytes,access,&old)) throw "fixture protect";
    }
};
void* __fastcall native_reset(void*,const void*) { ++current->calls[1];SetLastError(kError);return reinterpret_cast<void*>(current->matrices); }
std::uintptr_t __fastcall native_postpose(void*,void*,const void*,std::uint32_t,const void*,const void*) {
    ++current->calls[3];SetLastError(kError);return UINT64_C(0xFEDCBA9876543210);
}
std::int32_t __fastcall native_store(void* body,const void* pose) {
    ++current->calls[5];
    if (current->throw_store) RaiseException(kException,0,0,nullptr);
    std::memcpy(body,pose,32);SetLastError(kError);return -5;
}
void __fastcall native_primary(void*) {
    ++current->calls[4];
    for (unsigned b=0;b<current->body_count;++b)
        check(current->refresh(reinterpret_cast<void*>(current->bodies+0x110*b),reinterpret_cast<void*>(current->pose))==-5,"Store EAX through real ancestry");
    SetLastError(kError);
}
bool requested(void*) noexcept { return current && current->collector->requested(); }
void emit(void*,const hook::Event& e) noexcept {
    if (e.kind==hook::Kind::Exit) current->exit_reads=current->reader.totals().attempts;
    current->collector->emit(e);
}
void complete(void*,const hook::Completion& c) noexcept {
    current->collector->finish(c);
    check(current->reader.totals().attempts==current->exit_reads,"Exit and Completion never read memory");
}
bool invoke() {
    __try { reinterpret_cast<hook::JointBlend>(targets.code[0])(reinterpret_cast<void*>(current->job)); }
    __except (GetExceptionCode()==kException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
struct Run { std::uint64_t elapsed{}; bool threw{}; void* caller{}; };
DWORD WINAPI thread_run(void* p) {
    auto& run=*static_cast<Run*>(p);const auto start=now(nullptr);run.threw=invoke();run.elapsed=now(nullptr)-start;return 0;
}
void WINAPI fiber_run(void* p) { thread_run(p);SwitchToFiber(static_cast<Run*>(p)->caller); }
Run measure(unsigned kind) {
    Run run;
    if (!kind) thread_run(&run);
    else if (kind==1) {
        HANDLE thread=CreateThread(nullptr,0,thread_run,&run,0,nullptr);
        if (!thread || WaitForSingleObject(thread,10000)!=WAIT_OBJECT_0) ExitProcess(2);
        CloseHandle(thread);
    } else {
        run.caller=ConvertThreadToFiber(nullptr);if (!run.caller) ExitProcess(2);
        void* fiber=CreateFiber(0,fiber_run,&run);if (!fiber) ExitProcess(2);
        SwitchToFiber(fiber);DeleteFiber(fiber);
        if (!ConvertFiberToThread()) ExitProcess(2);
    }
    return run;
}
}
extern "C" void enemy_fixture_jointblend_body(void*,void* context) {
    auto& f=*current;++f.calls[0];Fixture::put(reinterpret_cast<A>(context),f.art);
    if (f.stop_native) f.collector->stop();
    if (f.native_pause_ms) Sleep(f.native_pause_ms);
    const auto reset=f.wrong_return ? reinterpret_cast<hook::Reset>(targets.code[2]) : f.reset;
    check(reset(reinterpret_cast<void*>(f.fg+0x30),reinterpret_cast<void*>(f.fg+0x1C0))==reinterpret_cast<void*>(f.matrices),"Reset native pointer result");
    if (f.revoke_art) Fixture::protect(f.art,0x1000,PAGE_NOACCESS);
    if (f.wrong_context) { context=static_cast<std::byte*>(context)+16;Fixture::put(reinterpret_cast<A>(context),f.art); }
    check(f.process(reinterpret_cast<const std::uint32_t*>(f.commands),4,context,0,nullptr,nullptr,nullptr)==0xFEDC1234,"Process seven-argument bridge result");
    f.primary(reinterpret_cast<void*>(f.composite));SetLastError(kError);
}
extern "C" std::uint32_t enemy_fixture_process_body(void* context,void* segment,const void*) {
    auto& f=*current;++f.calls[2];const auto s=reinterpret_cast<A>(segment);
    Fixture::put(s,f.pose);Fixture::put(s+8,reinterpret_cast<A>(context));Fixture::put(s+0x28,f.raw);
    Fixture::put(s+0x38,f.raw+0x40);Fixture::put(s+0x58,f.matrices);
    check(f.postpose(reinterpret_cast<void*>(f.pose),segment,context,obs::kExportId,reinterpret_cast<void*>(f.payload),nullptr)==UINT64_C(0xFEDCBA9876543210),"Postpose opaque result");
    SetLastError(kError);return 0xFEDC1234;
}
int main() {
    LARGE_INTEGER freq{};if (!QueryPerformanceFrequency(&freq) || freq.QuadPart<1000) return 2;
    if (!hook::configure({enemy_fixture_jointblend,native_reset,enemy_fixture_process,native_postpose,native_primary,native_store},
        {nullptr,requested,emit,complete})) return 2;
    targets=hook::targets();
    for (unsigned mode=0;mode<8;++mode) {
        Fixture f(false,static_cast<std::uint64_t>(freq.QuadPart));current=&f;
        if (mode==1) Fixture::put(f.owner,A{1}); // Early non-Pirate filter.
        if (mode==2) Fixture::protect(f.art,0x1000,PAGE_NOACCESS);
        f.revoke_art=mode==3;f.throw_store=mode==4;f.stop_native=mode==5;
        f.wrong_return=mode==6;f.wrong_context=mode==7;
        check(f.collector->arm(),"arm owned integration");
        check(invoke()==f.throw_store,"native SEH crosses real thunk/MASM unwind");
        if (!f.throw_store) check(GetLastError()==kError,"native LastError preserved by complete integration");
        f.collector->stop();check(f.collector->quiescent() && !f.reader.active(),"read phase and collector lease ended");
        check(f.calls[0]==1 && f.calls[1]==1 && f.calls[2]==1 && f.calls[3]==1 && f.calls[4]==1,"native stages exactly once");
        check(f.calls[5]==(f.throw_store ? 1u : f.body_count),"native stores not retried or omitted");
        if (mode==1) check(f.collector->count()==0 && f.reader.totals().copies==4,"early filter reads four scalars only");
        else {
            check(f.collector->count()==1,"one captured scope");
            const auto& sample=f.collector->samples()[0];
            const bool expected=mode==0 || mode==5;
            check(col::complete(sample)==expected,"complete vs rejected real integration");
            if (expected) check(sample.policy.stored_mask==0xFFFF && sample.bodies[15].returned,"all16 stores witnessed through real frames");
            else check(sample.published && sample.failure!=col::Failure::none,"failure diagnostic published only after completion");
        }
        const auto reads=f.reader.totals().attempts;
        check(!invoke() || f.throw_store,"stopped native forwarding");
        check(f.reader.totals().attempts==reads,"stopped wrapper does not read");
        current=nullptr;
    }
    const auto us=[&](auto v){return 1e6*static_cast<double>(v)/static_cast<double>(freq.QuadPart);};
    for (unsigned kind=0;kind<3;++kind) {
        std::array<std::uint64_t,64> elapsed{};
        std::size_t complete_count{},budget_count{};
        std::uint64_t max_queries{},max_bytes{};
        for (auto& ticks:elapsed) {
            Fixture f(true,static_cast<std::uint64_t>(freq.QuadPart));current=&f;
            check(f.collector->arm(),"arm timed integration");
            const auto run=measure(kind);check(!run.threw,"timed native returns");ticks=run.elapsed;
            f.collector->stop();check(f.collector->quiescent(),"timed collector quiescent");
            const auto& s=f.collector->samples()[0];
            if (col::complete(s)) ++complete_count;
            else { ++budget_count;check(s.failure==col::Failure::budget || f.reader.failure()==rd::Failure::budget,"timed rejection is budget, not fixture provenance"); }
            max_queries=std::max(max_queries,f.reader.totals().queries);max_bytes=std::max(max_bytes,f.reader.totals().bytes);
            current=nullptr;
        }
        std::sort(elapsed.begin(),elapsed.end());
        std::printf("OWNED_FULL_PATH kind=%u fixtures=64 complete=%zu budget_reject=%zu p50_us=%.3f p95_us=%.3f max_us=%.3f queries_max=%llu bytes_max=%llu\n",
            kind,complete_count,budget_count,us(elapsed[32]),us(elapsed[60]),us(elapsed.back()),max_queries,max_bytes);
    }
    {
        Fixture f(true,static_cast<std::uint64_t>(freq.QuadPart));current=&f;f.native_pause_ms=3;
        check(f.collector->arm(),"arm delayed native");const auto run=measure(0);f.collector->stop();
        check(!run.threw && f.collector->quiescent() && run.elapsed>static_cast<std::uint64_t>(freq.QuadPart)/1000,"external envelope includes native delay");
        std::printf("slow-native-inclusive-us=%.3f failures=%u\n",us(run.elapsed),failures);current=nullptr;
    }
    return failures ? 1 : 0;
}
