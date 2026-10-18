#include "enemy_jointblend_read_windows.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
namespace rd = coop::enemy_jointblend_read;
namespace {
unsigned checks{},failures{};
void check(bool yes,const char* why) { ++checks;if (!yes) { ++failures;std::printf("FAIL %s\n",why); } }
struct Clock {
    std::uint64_t value=100,step=0;
    static std::uint64_t now(void* p) noexcept { auto& c=*static_cast<Clock*>(p);const auto v=c.value;c.value+=c.step;return v; }
};
void protect(void* p,std::size_t n,DWORD access) {
    DWORD old{};if (!VirtualProtect(p,n,access,&old)) ExitProcess(2);
}
}
int main() {
    SYSTEM_INFO info{};GetSystemInfo(&info);const auto page=info.dwPageSize;
    auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,3*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (!memory) return 2;
    std::memset(memory,0x5A,3*page);protect(memory,3*page,PAGE_READONLY);
    const auto at=reinterpret_cast<std::uintptr_t>(memory);
    std::array<std::byte,64> output{};
    Clock clock;rd::Reader reader;
    check(!reader.copy(at,output.data(),16) && reader.totals().queries==0,"no read outside event");
    check(reader.begin_event(100,100,Clock::now,&clock),"begin event");
    SetLastError(0x12345678);
    check(reader.copy(at,output.data(),16) && output[15]==std::byte{0x5A},"real readonly full copy");
    check(GetLastError()==0x12345678,"query and copy preserve LastError");
    const auto queries=reader.totals().queries;
    check(reader.copy(at+32,output.data(),16) && reader.totals().queries==queries,"same-phase region reuse");
    reader.end_event();
    protect(memory+page,page,PAGE_READONLY|PAGE_GUARD);
    check(reader.begin_event(100,100,Clock::now,&clock),"fresh guard event");
    const auto copies=reader.totals().copies;
    check(!reader.copy(at+page-16,output.data(),32) && reader.failure()==rd::Failure::pages,"interior guard rejected before payload");
    MEMORY_BASIC_INFORMATION region{};VirtualQuery(memory+page,&region,sizeof(region));
    check((region.Protect&PAGE_GUARD)!=0 && reader.totals().copies==copies && reader.totals().faults==0,"existing guard stays set, no partial payload attempt");
    reader.end_event();protect(memory+page,page,PAGE_READONLY);

    // Deterministically simulate a protection change AFTER an event's query.
    // This deliberate test-only intervention is not permission to retain the
    // cache across native work. SEH must contain the cached-query race.
    check(reader.begin_event(100,100,Clock::now,&clock),"begin stale-query fault event");
    check(reader.copy(at,output.data(),16),"prime readable region");
    protect(memory+page,page,PAGE_NOACCESS);
    check(!reader.copy(at+page-16,output.data(),32) && reader.failure()==rd::Failure::exception && reader.totals().faults==1,
        "copy fault contained even after readable preflight, partial output not accepted");
    const auto failed_queries=reader.totals().queries;
    check(!reader.copy(at,output.data(),16) && reader.totals().queries==failed_queries,"no retry within failed event");
    reader.end_event();
    check(reader.begin_event(100,100,Clock::now,&clock),"fresh noaccess event");
    check(!reader.copy(at+page,output.data(),16) && reader.failure()==rd::Failure::pages && reader.totals().faults==1,
        "new event discards cache and rejects noaccess without touching it");
    reader.end_event();protect(memory+page,page,PAGE_READONLY);
    check(reader.begin_event(100,100,Clock::now,&clock) && reader.copy(at+page,output.data(),16),"new valid event does not inherit stale rejection");
    check(!reader.begin_event(100,100,Clock::now,&clock) && reader.active() && reader.failure()==rd::Failure::scope,"nested begin does not reset owned cache");
    reader.end_event();

    for (unsigned bad=0;bad<5;++bad) {
        check(reader.begin_event(100,100,Clock::now,&clock),"span test begin");
        const auto q=reader.totals().queries;
        const auto source=bad==0 ? 0 : bad==1 ? std::numeric_limits<std::uintptr_t>::max()-8 : at;
        const auto count=bad==2 ? 0u : bad==3 ? 0x40B1u : 16u;
        void* dst=bad==4 ? memory : output.data();
        check(!reader.copy(source,dst,count) && reader.failure()==rd::Failure::span && reader.totals().queries==q,"bad spans/alias rejected before query");
        reader.end_event();
    }
    clock.value=100;clock.step=1;
    check(reader.begin_event(100,3,Clock::now,&clock),"budget begin");
    output.fill(std::byte{0x11});const auto before=reader.totals().copies;
    check(!reader.copy(at,output.data(),16) && reader.failure()==rd::Failure::budget && reader.totals().copies==before,
        "query consumes allowance: no payload copy starts afterwards");
    check(output[0]==std::byte{0x11},"budget rejection leaves output untouched");
    reader.end_event();clock.value=0;
    check(!reader.begin_event(100,3,Clock::now,&clock) && !reader.active() && reader.failure()==rd::Failure::clock,"bad clock never opens phase");
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("%u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
