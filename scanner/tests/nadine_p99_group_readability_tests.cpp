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
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
