#include "nadine_p99_group_readability.h"
#include <array>
#include <iostream>
#include <span>

int main() {
    namespace p=coop::p99_group_readability;
    unsigned checks=0,failures=0;
    const auto check=[&](bool pass){++checks;if(!pass)++failures;};
    const auto run=[](std::uintptr_t address,std::size_t bytes,std::span<const p::Region> regions){
        return p::readable(address,bytes,[&](std::uintptr_t cursor,p::Region& out){
            for(const auto& r:regions) if(cursor>=r.base && cursor-r.base<r.bytes){out=r;return true;}
            return false;
        });
    };
    constexpr std::uintptr_t base=0x100000;
    std::array<p::Region,33> pages{};
    for(std::size_t i=0;i<pages.size();++i) pages[i]={base+i*0x1000,0x1000,true,true,false};
    check(run(base,0x20000,pages)); // Native arena: larger than legacy0xB000 probe.
    check(run(base+127,0x20000,pages)); // Unaligned arena spans33 pages.
    for(std::size_t i=0;i<pages.size();++i){
        auto bad=pages; bad[i].readable=false; check(!run(base+127,0x20000,bad));
        bad=pages; bad[i].guarded=true; check(!run(base+127,0x20000,bad));
        bad=pages; bad[i].committed=false; check(!run(base+127,0x20000,bad));
    }
    const std::array<p::Region,1> one{{{base,0x40000,true,true,false}}};
    check(run(base+64,0x20000,one));
    check(!run(base,0x20001,one)); check(!run(base,0,one)); check(!run(0,8,one));
    check(!run(UINTPTR_MAX-7,8,one)); check(!run(base,0x20000,std::span(pages).first(31)));
    // Executable/readable mixtures and split VirtualQuery regions are allowed;
    // missing, zero-size, backwards and overflowing query responses are not.
    for(const p::Region invalid:std::array<p::Region,4>{{
        {base,0,true,true,false},{base+1,0x1000,true,true,false},
        {base-0x1000,0x1000,true,true,false},{0,UINTPTR_MAX,true,true,false}}}){
        const auto start=invalid.bytes==UINTPTR_MAX?UINTPTR_MAX-32:base;
        check(!p::readable(start,16,[&](auto,p::Region& out){out=invalid;
            if(invalid.bytes==UINTPTR_MAX)out.base=1; return true;}));
    }
    unsigned calls=0;
    check(!p::readable(base,65,[&](auto cursor,p::Region& out){++calls;out={cursor,1,true,true,false};return true;}));
    check(calls==64);
    // The AGN fast helper can never authorize128KiB, regardless of alignment.
    for(unsigned offset=0;offset<4096;++offset){
        const unsigned fast_extent=offset?0xC000-offset:0xB000;
        check(fast_extent<0x20000);
    }
    {
        p::Batch stage;
        unsigned queries=0;
        const auto query=[&](auto,p::Region& out){++queries;out=one[0];return true;};
        check(stage.readable(base+32,0x20000,query));
        check(stage.readable(base+96,128,query));
        check(stage.readable(base+0x23000,256,query));
        check(stage.readable(base,8,query));
        check(queries==1 && stage.query_count()==1);
        check(!stage.readable(UINTPTR_MAX-7,8,query));
        check(!stage.readable(base,0x20001,query));
        check(!stage.readable(base,8,query)); // Invalid extent poisons the batch.
        check(queries==1);
    }
    for(std::size_t guarded=0;guarded<pages.size();++guarded){
        p::Batch stage;
        unsigned queries=0;
        const auto query=[&](auto cursor,p::Region& out){
            ++queries;
            for(std::size_t i=0;i<pages.size();++i)
                if(cursor>=pages[i].base && cursor-pages[i].base<pages[i].bytes){
                    out=pages[i];out.guarded=i==guarded;return true;
                }
            return false;
        };
        check(!stage.readable(base+127,0x20000,query));
        check(queries==guarded+1);
        const auto failed_queries=queries;
        check(!stage.readable(base,1,query)); // Entire failed stage stays closed.
        check(queries==failed_queries);
    }
    {
        p::Batch first;
        bool guarded=false;
        unsigned queries=0;
        const auto query=[&](auto,p::Region& out){++queries;out=one[0];out.guarded=guarded;return true;};
        check(first.readable(base,0x20000,query));
        // Native boundary: no reuse of first, even if pointers are identical.
        guarded=true;
        p::Batch next;
        check(!next.readable(base,0x20000,query));
        check(next.query_count()==1); // Failed query counts toward the bound.
        check(queries==2);
    }
    {
        p::Batch stage;
        unsigned queries=0;
        const auto query=[&](auto cursor,p::Region& out){++queries;out={cursor,1,true,true,false};return true;};
        check(stage.readable(base,64,query));
        check(!stage.readable(base+64,1,query)); // Total stage cap, not per span.
        check(queries==64);
    }
    for(const p::Region invalid:std::array<p::Region,5>{{
        {base,0,true,true,false},{base+1,0x1000,true,true,false},
        {base-0x1000,0x1000,true,true,false},{base,0x1000,false,true,false},
        {base,0x1000,true,false,false}}}){
        p::Batch stage;
        check(!stage.readable(base,8,[&](auto,p::Region& out){out=invalid;return true;}));
    }
    {
        p::Batch stage;
        check(!stage.readable(UINTPTR_MAX-32,16,[](auto,p::Region& out){out={1,UINTPTR_MAX,true,true,false};return true;}));
        p::Batch gap;
        check(!gap.readable(base,0x20000,[&](auto cursor,p::Region& out){
            if(cursor!=base)return false;out={base,0x1000,true,true,false};return true;}));
    }
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
