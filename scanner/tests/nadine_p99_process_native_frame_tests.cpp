#include "nadine_p99_process_native_frame.h"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <source_location>

namespace policy = coop::p99_process_native_frame;
unsigned checks = 0, failures = 0;
void check(bool value, std::source_location where = std::source_location::current()) {
    ++checks;
    if (!value) { ++failures; std::cerr << "failed line " << where.line() << '\n'; }
}

void value_tests() {
    constexpr std::uintptr_t r = 0x100008;
    constexpr auto s = r-policy::kStatusToReturnSlot;
    constexpr auto c = r-policy::kSegmentToReturnSlot;
    check(policy::owns(s,c,r));
    check(!policy::owns(0,c,r));
    check(!policy::owns(s,0,r));
    check(!policy::owns(s,c,0));
    check(!policy::owns(c,s,r));
    for (std::uintptr_t offset = 1; offset <= 256; ++offset) {
        check(!policy::owns(s+offset,c,r));
        check(!policy::owns(s,c+offset,r));
        check(!policy::owns(s,c,r+offset));
        check(!policy::owns(s+offset,c+offset,r));
    }
    check(!policy::owns(UINTPTR_MAX-7,c,8));
    check(!policy::owns(s,UINTPTR_MAX-15,8));
    check(!policy::owns(s+16,c,r+16));
    check(!policy::owns(s,c+16,r+16));
    check(policy::owns(s+16,c+16,r+16)); // Separate, consistently bound frame.
    auto prologue = policy::kNativePrologue;
    auto segment = policy::kSegmentLea, status = policy::kStatusLea;
    check(policy::layout_matches(prologue,segment,status));
    for (std::size_t i=0; i<prologue.size(); ++i) {
        prologue[i] ^= 1;
        check(!policy::layout_matches(prologue,segment,status));
        prologue[i] ^= 1;
    }
    for (std::size_t i=0; i<segment.size(); ++i) {
        segment[i] ^= 1; check(!policy::layout_matches(prologue,segment,status)); segment[i] ^= 1;
        status[i] ^= 1; check(!policy::layout_matches(prologue,segment,status)); status[i] ^= 1;
    }
    check(!policy::layout_matches(prologue,status,segment));
    check(!policy::layout_matches(std::span{prologue}.first(32),segment,status));
    check(!policy::layout_matches(prologue,std::span{segment}.first(3),status));
    check(!policy::layout_matches(prologue,segment,std::span{status}.first(3)));
}

enum class Action { normal, old_recursion, wrapped_recursion, fault, nested_fault };
constexpr DWORD kFault = 0xE0420395;
constexpr std::uint32_t kResult = 0xE123C0D5;
struct Binding {
    std::uint64_t before = 0x0123456789ABCDEF;
    std::uintptr_t slot = 0;
    std::uint64_t after = 0xFEDCBA9876543210;
    Binding* previous = nullptr;
};
struct Job {
    Action action = Action::normal;
    unsigned depth = 0, calls = 0, accepted = 0, refused = 0;
    unsigned clears = 0, abnormal = 0, finalizers = 0, caught = 0;
    std::uint32_t result = 0;
    std::array<std::uint32_t,4> commands{0x01234567,0x89ABCDEF,0x55AA55AA,0xA55AA55A};
    std::uint32_t words = 0xA1234567, group = 0x87654321;
    std::uintptr_t payload5 = 0xF123456789ABCDEF;
    std::uintptr_t payload6 = 0x0123456789ABCDEF;
    std::uintptr_t payload7 = 0x7FFFFFFFFFFFFFFF;
};
Binding* active = nullptr; // Fixture-only single-thread scope, not FLS/runtime.
Job* current = nullptr;
extern "C" std::uint32_t p99_process_frame_fixture(const std::uint32_t*,std::uint32_t,
    const void*,std::uint32_t,const void*,const void*,const void*);
extern "C" unsigned p99_process_frame_abi_probe(void(__fastcall*)());

__declspec(noinline) std::uint32_t invoke_wrapped(Job& job) {
    Binding frame{};
    frame.previous = active;
    active = &frame;
    std::uint32_t result = 0;
    __try {
        result = policy::p99_process_invoke_native(job.commands.data(),job.words,&job,job.group,
            &job.payload5,&job.payload6,&job.payload7,&p99_process_frame_fixture,&frame.slot);
        check(frame.slot == 0);
        ++job.clears;
    } __finally {
        if (AbnormalTermination()) {
            check(frame.slot != 0); // Assembly normal-return clear was bypassed.
            ++job.abnormal;
        }
        frame.slot = 0;
        check(frame.before == 0x0123456789ABCDEF && frame.after == 0xFEDCBA9876543210);
        check(active == &frame);
        active = frame.previous;
        ++job.finalizers;
    }
    return result;
}

__declspec(noinline) void verify_unwind(std::uintptr_t actual_slot, bool exact) {
    CONTEXT c{};
    RtlCaptureContext(&c);
    const auto bridge = reinterpret_cast<DWORD64>(&policy::p99_process_invoke_native);
    bool found = false;
    for (unsigned depth=0; depth<20 && c.Rip; ++depth) {
        DWORD64 image=0, establisher=0;
        void* handler = nullptr;
        auto* entry = RtlLookupFunctionEntry(c.Rip,&image,nullptr);
        check(entry != nullptr);
        if (!entry) break;
        if (image+entry->BeginAddress == bridge) {
            found = true;
            const auto sp = c.Rsp;
            check(sp == active->slot+8);
            check((sp == actual_slot+8) == exact);
            const auto return_pc = *reinterpret_cast<const DWORD64*>(sp+0x58);
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,c.Rip,entry,&c,&handler,&establisher,nullptr);
            check(c.Rsp == sp+0x60 && c.Rip == return_pc);
            break;
        }
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,c.Rip,entry,&c,&handler,&establisher,nullptr);
    }
    check(found);
}

extern "C" std::uint32_t p99_process_fixture_body(const std::uint32_t* commands,
    std::uint32_t words,const void* context,std::uint32_t group,
    const void* a5,const void* a6,const void* a7,std::uintptr_t actual_slot,
    std::uintptr_t status,std::uintptr_t segment) {
    auto& job = *static_cast<Job*>(const_cast<void*>(context));
    ++job.calls;
    check(commands == job.commands.data() && words == job.words);
    check(context == &job && group == job.group);
    check(a5 == &job.payload5 && a6 == &job.payload6 && a7 == &job.payload7);
    check((actual_slot & 15u) == 8 && (status & 15u) == 8 && (segment & 15u) == 0);
    check(policy::owns(status,segment,actual_slot));
    check(active != nullptr);
    if (!active) return 0;
    const bool exact = !(job.action == Action::old_recursion && job.depth != 0);
    check(policy::owns(status,segment,active->slot) == exact);
    if (exact) ++job.accepted; else ++job.refused;
    if (active->previous) check(!policy::owns(status,segment,active->previous->slot));
    verify_unwind(actual_slot,exact);
    if (job.action == Action::fault || (job.action == Action::nested_fault && job.depth != 0))
        RaiseException(kFault,0,0,nullptr);
    if (job.depth == 0 && job.action != Action::normal) {
        const auto before = active->slot;
        ++job.depth;
        const auto result = job.action == Action::old_recursion
            ? p99_process_frame_fixture(commands,words,context,group,a5,a6,a7)
            : invoke_wrapped(job);
        --job.depth;
        check(result == kResult && active->slot == before);
        check(policy::owns(status,segment,active->slot));
    }
    return kResult;
}

__declspec(noinline) void __fastcall abi_thunk() {
    __try { current->result = invoke_wrapped(*current); }
    __except (GetExceptionCode() == kFault ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        ++current->caught;
        check(active == nullptr);
    }
}

void run_case(Action action,unsigned calls,unsigned accepted,unsigned refused,
    unsigned clears,unsigned abnormal,unsigned finalizers,unsigned caught) {
    Job job{};
    job.action = action;
    current = &job;
    check(p99_process_frame_abi_probe(&abi_thunk) == 0);
    check(active == nullptr);
    check(job.calls == calls && job.accepted == accepted && job.refused == refused);
    check(job.clears == clears && job.abnormal == abnormal && job.finalizers == finalizers);
    check(job.caught == caught && job.result == (caught ? 0u : kResult));
    current = nullptr;
}

int main() {
    value_tests();
    DWORD64 image = 0;
    for (auto address : {reinterpret_cast<DWORD64>(&policy::p99_process_invoke_native),
                        reinterpret_cast<DWORD64>(&p99_process_frame_fixture),
                        reinterpret_cast<DWORD64>(&p99_process_frame_abi_probe)})
        check(RtlLookupFunctionEntry(address,&image,nullptr) != nullptr);
    const auto bridge = reinterpret_cast<DWORD64>(&policy::p99_process_invoke_native);
    auto* entry = RtlLookupFunctionEntry(bridge,&image,nullptr);
    if (entry) {
        const auto* info = reinterpret_cast<const unsigned char*>(image+entry->UnwindData);
        check((info[0]&7) == 1 && (info[0]>>3) == 0 && info[2] == 1);
        check((info[5]&15) == 2 && (info[5]>>4) == 10); // ALLOC_SMALL (10+1)*8=58.
    }
    run_case(Action::normal,1,1,0,1,0,1,0);
    run_case(Action::old_recursion,2,1,1,1,0,1,0);
    run_case(Action::wrapped_recursion,2,2,0,2,0,2,0);
    run_case(Action::fault,1,1,0,0,1,1,1);
    run_case(Action::nested_fault,2,2,0,0,2,2,1);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
