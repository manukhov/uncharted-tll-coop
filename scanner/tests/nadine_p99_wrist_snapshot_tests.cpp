#include "nadine_p99_wrist_snapshot.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <tuple>
#include <vector>

namespace s = coop::p99_wrist_snapshot;
namespace a = coop::p99_group_admission;
namespace {
unsigned checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}
struct Access {
    a::Address base;
    std::size_t offset, count;
    friend bool operator==(const Access&, const Access&) = default;
};
struct Fixture {
    // All addresses below are fake identities. bytes() only reads owned arrays.
    a::WorkBuffer work{0x100000,0x200000,0x20000,0x300000,0x1000,
        0x300000+3*56,0x300E00,0x4430,0x20000,3,0,64};
    std::array<a::Allocation,64> records{};
    std::array<a::Address,64> table{};
    s::Context context{};
    s::Bindings bindings{0x140000000,0x400000,0x500000};
    std::array<std::byte,4992> jp{};
    std::array<std::byte,6720> workspace{};
    std::vector<Access> accesses;
    unsigned fail_at = 0;
    bool scribble_before_failure = false;
    Fixture() {
        records[0] = {0,4992,0,104,0,0,0};
        records[1] = {0x2000,6720,0,105,0,3,0};
        records[2] = {0x4000,1072,0,266,0,5,0};
        for (std::size_t i=0;i<3;++i) table[i]=0x300000+56*i;
        for (std::size_t i=3;i<63;++i) records[i].next_free=0x300000+56*(i+1);
        put(0, std::uint64_t{0x100000});
        put(0x10, std::uint64_t{0x143026CB0}); put(0x18, std::uint16_t{6});
        put(0x28, std::uint64_t{0x400000}); put(0x30, std::uint64_t{0x500000});
        put(0x38, std::uint64_t{0x400040}); put(0x40, std::uint64_t{0x400140});
        put(0x50, std::uint64_t{0x200000}); put(0x58, std::uint64_t{0x202000});
        put(0x68, std::uint64_t{0x204000});
        for (std::size_t i=0;i<jp.size();++i) jp[i]=std::byte((i*17+3)%256);
        for (std::size_t i=0;i<workspace.size();++i) workspace[i]=std::byte((i*31+7)%256);
    }
    template<class T> void put(std::size_t off, T value) {
        std::memcpy(context.data()+off,&value,sizeof(value));
    }
    bool bytes(a::Address base, std::size_t off, void* out, std::size_t size) noexcept {
        accesses.push_back({base,off,size});
        if (fail_at == accesses.size()) {
            if (scribble_before_failure) std::memset(out,0xAA,size);
            return false;
        }
        std::span<const std::byte> input;
        if (base == 0x200000) input=jp;
        if (base == 0x202000) input=workspace;
        if (off>input.size() || size>input.size()-off) return false;
        std::memcpy(out,input.data()+off,size);
        return true;
    }
    s::Result capture(s::Attempt& attempt, s::Phase phase=s::Phase::AfterProgram) {
        return attempt.capture(phase,context,bindings,{work,records,table},*this);
    }
};
bool zero(const s::Payload& payload) {
    return std::ranges::all_of(payload.bytes,[](std::byte b){return b==std::byte{};});
}
void successful_copy() {
    Fixture f;
    const auto old_jp=f.jp;
    const auto old_workspace=f.workspace;
    const auto old_context=f.context;
    s::Attempt attempt;
    auto result=f.capture(attempt);
    check(result.copied(),"contract-valid owned copy");
    check(!result.source_admitted() && !result.final_render_admitted(),"no synthetic admission");
    check(attempt.used(),"terminal after success");
    const std::vector<Access> expected{{0x200000,0x480,48},{0x200000,0x4E0,48},
        {0x200000,0x930,48},{0x200000,0x990,48},{0x202000,0x5C0,64}};
    check(f.accesses==expected,"exact five source reads totaling256");
    for (std::size_t i=0;i<4;++i)
        check(std::equal(result.payload.bytes.begin()+48*i,result.payload.bytes.begin()+48*(i+1),
            f.jp.begin()+expected[i].offset),"byte-exact JP order");
    check(std::equal(result.payload.bytes.begin()+192,result.payload.bytes.end(),
        f.workspace.begin()+0x5C0),"64B workspace includes unmodified padding");
    check(f.jp==old_jp && f.workspace==old_workspace && f.context==old_context,"source/context unchanged");
    auto second=f.capture(attempt);
    check(second.reason==s::Reject::Repeated && zero(second.payload),"success cannot repeat");
    check(f.accesses==expected,"no repeat reads");
    std::fill(f.jp.begin(),f.jp.end(),std::byte{});
    std::fill(f.workspace.begin(),f.workspace.end(),std::byte{});
    check(!zero(result.payload),"payload independent of released source");
}
void preserve_uninterpreted_bits() {
    Fixture f;
    const std::array<std::uint32_t,4> unusual{0x80000000,0x7FC01234,0xFF800000,0xDEADBEEF};
    std::memcpy(f.jp.data()+0x480+12,unusual.data(),4);
    std::memcpy(f.jp.data()+0x480+44,unusual.data()+1,4);
    std::memcpy(f.workspace.data()+0x5C0+52,unusual.data()+1,12);
    s::Attempt attempt;
    auto result=f.capture(attempt);
    check(result.copied(),"raw capture is not numeric acceptance");
    check(std::memcmp(result.payload.bytes.data()+12,unusual.data(),4)==0,"negative zero bits preserved");
    check(std::memcmp(result.payload.bytes.data()+44,unusual.data()+1,4)==0,"JP W raw bits preserved");
    check(std::memcmp(result.payload.bytes.data()+192+52,unusual.data()+1,12)==0,"workspace padding raw bits preserved");
}
void every_read_failure_is_terminal_and_empty() {
    for (unsigned fail=1;fail<=5;++fail) {
        Fixture f; f.fail_at=fail; f.scribble_before_failure=true;
        s::Attempt attempt;
        auto result=f.capture(attempt);
        check(result.reason==s::Reject::Read && zero(result.payload),"partial read never published");
        check(f.accesses.size()==fail,"stops on first failure");
        result=f.capture(attempt);
        check(result.reason==s::Reject::Repeated && f.accesses.size()==fail,"no fault retry");
    }
}
void invalid_contexts() {
    const auto refused=[](auto mutate,s::Reject expected) {
        Fixture f; mutate(f); s::Attempt attempt;
        const auto result=f.capture(attempt);
        check(result.reason==expected && zero(result.payload),"malformed context/storage rejected");
        check(f.accesses.empty(),"reject before payload access");
    };
    // +08 must stay zero. Do not transplant the old writer's parent check.
    refused([](auto& f){f.put(8,std::uint64_t{0x600000});},s::Reject::ContextIdentity);
    for (auto off:{0u,0x10u,0x20u,0x28u,0x30u,0x38u,0x40u})
        refused([=](auto& f){f.put(off,std::uint64_t{0xBAD000});},s::Reject::ContextIdentity);
    refused([](auto& f){f.put(0x18,std::uint16_t{5});},s::Reject::ContextIdentity);
    refused([](auto& f){f.put(0x48,std::uint16_t{1});},s::Reject::SegmentState);
    for (auto flags:{1u,2u,0x100u,0x200u,0x300u})
        refused([=](auto& f){f.put(0x4A,static_cast<std::uint16_t>(flags));},s::Reject::SegmentState);
    refused([](auto& f){f.put(0x60,std::uint64_t{0x207000});},s::Reject::SegmentState);
    refused([](auto& f){f.put(0x50,std::uint64_t{0x200010});},s::Reject::Allocation);
    refused([](auto& f){f.put(0x58,std::uint64_t{});},s::Reject::Allocation);
    refused([](auto& f){f.records[0].count=66;},s::Reject::Allocation);
    refused([](auto& f){f.records[1].type=0;},s::Reject::Allocation);
    refused([](auto& f){f.records[2].next_free=0x300000;},s::Reject::Allocation);
    refused([](auto& f){f.work.front_count=0;},s::Reject::Allocation);
    refused([](auto& f){f.bindings.raw=UINT64_MAX-15;},s::Reject::Bindings);
    refused([](auto& f){f.bindings.module=0;},s::Reject::Bindings);
    refused([](auto& f){++f.bindings.scratch;},s::Reject::Bindings);
}
void phase_and_extent() {
    Fixture f; s::Attempt pre;
    auto result=f.capture(pre,s::Phase::BeforeProgram);
    check(result.reason==s::Reject::Phase && f.accesses.empty(),"pre16 cannot read W22");
    check(f.capture(pre).reason==s::Reject::Repeated,"pre16 refusal terminal");
    for (auto bytes:{std::size_t{0},std::size_t{0x7F}}) {
        s::Attempt attempt;
        result=attempt.capture(s::Phase::AfterProgram,{f.context.data(),bytes},f.bindings,
            {f.work,f.records,f.table},f);
        check(result.reason==s::Reject::ContextExtent && f.accesses.empty(),"exact context extent required");
    }
}
} // namespace
int main() {
    successful_copy(); preserve_uninterpreted_bits(); every_read_failure_is_terminal_and_empty();
    invalid_contexts(); phase_and_extent();
    std::cout << checks << " checks, " << failures << " failures; owned fixtures only, no live admission\n";
    return failures ? 1 : 0;
}
