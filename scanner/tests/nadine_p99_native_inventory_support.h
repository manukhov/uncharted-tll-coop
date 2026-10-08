#pragma once
#include "nadine_p99_native_inventory_generated.h"
#include "nadine_p99_provider_fixture_support.h"
#include <psapi.h>
#include <iostream>

// This owned-process fixture is deliberately not a target inventory/installer.
// The generated header is reviewed disk data, never a Request wire protocol.
namespace native_fixture {
namespace p=coop::p99_provider;
namespace n=native_inventory;
static_assert(n::pins.size()==8 && n::bindings.size()==59); // this seven-provider exam, not the general Request cap
using Request=coop::p99_provider_preparation::Request;
enum class EvidenceFault {None,LastSpan,LastIat}; // fault EXPECTATIONS only, never mapped data
inline bool hash(std::span<const std::uint8_t> data,p::Hash& out){
    if(data.empty() || data.size()>4*1024*1024)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0))return false;
    const auto okay=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(data.data()),static_cast<ULONG>(data.size()),out.data(),32)==0;
    return BCryptCloseAlgorithmProvider(algorithm,0)==0 && okay;
}
inline bool same(const MEMORY_BASIC_INFORMATION& a,const MEMORY_BASIC_INFORMATION& b){
    return a.BaseAddress==b.BaseAddress && a.AllocationBase==b.AllocationBase &&
        a.AllocationProtect==b.AllocationProtect && a.RegionSize==b.RegionSize &&
        a.State==b.State && a.Protect==b.Protect && a.Type==b.Type;
}
inline bool read(const p::Selection& s,std::uint32_t rva,std::span<std::uint8_t> out,DWORD protection){
    if(out.empty() || rva>s.bytes || out.size()>s.bytes-rva)return false;
    for(std::size_t offset=0;offset<out.size();){
        const auto address=s.base+rva+offset;MEMORY_BASIC_INFORMATION before{},after{};
        if(VirtualQuery(reinterpret_cast<void*>(address),&before,sizeof(before))!=sizeof(before) ||
           before.AllocationBase!=reinterpret_cast<void*>(s.base) || before.State!=MEM_COMMIT ||
           before.Type!=MEM_IMAGE || before.Protect!=protection)return false;
        const auto first=reinterpret_cast<std::uintptr_t>(before.BaseAddress);
        if(first>address || !p::range(first,before.RegionSize) || first+before.RegionSize<=address)return false;
        const auto count=std::min({out.size()-offset,std::size_t{4096},static_cast<std::size_t>(first+before.RegionSize-address)});
        SIZE_T got=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out.data()+offset,count,&got) ||
           got!=count || VirtualQuery(reinterpret_cast<void*>(address),&after,sizeof(after))!=sizeof(after) || !same(before,after))return false;
        offset+=count;
    }return true;
}
inline bool mapped_path(const p::Selection& s){
    p::Path actual{};
    const auto count=GetMappedFileNameW(GetCurrentProcess(),reinterpret_cast<void*>(s.base),actual.data(),static_cast<DWORD>(actual.size()));
    return count && count<actual.size() && p::path_view(actual).size()==count && actual==s.native_path;
}
struct Inventory {
    std::array<provider_fixture::File,n::pins.size()-1> files;
    std::array<p::Selection,n::pins.size()-1> selected{};
    std::array<HMODULE,n::pins.size()-1> refs{};
    std::array<bool,n::pins.size()-1> known_refs{};
    bool opened=false;
    std::size_t verified_files=0,matched_bindings=0,hash_refused_index=p::kMax;
    ~Inventory(){release();}
    bool release(){
        bool okay=true;
        for(std::size_t i=0;i<refs.size();++i){
            if(!known_refs[i])continue; // ambiguous failed acquisition survives to process exit
            const auto owned=refs[i];refs[i]=nullptr;known_refs[i]=false;
            if(owned && !FreeLibrary(owned))okay=false;
        }
        return okay; // only these caller refs; never DLL Owner's refs/PINs
    }
    bool open(const p::Selection& artifact,bool bad_hash){
        const auto& pin=n::pins[0];
        if(artifact.expected_file_hash!=pin.hash || artifact.bytes!=pin.bytes || artifact.prefix!=pin.prefix)return false;
        verified_files=1;
        for(std::size_t i=1;i<n::pins.size();++i){
            const auto& expected=n::pins[i];auto& actual=selected[i-1];auto sha=expected.hash;
            if(bad_hash && i==n::pins.size()-1)sha[0]^=1;
            if(!provider_fixture::make_expected(std::wstring(expected.path),0,0,files[i-1],actual))return false;
            if(actual.expected_file_hash!=sha){hash_refused_index=i;return false;}
            if(actual.bytes!=expected.bytes ||
               !p::path_shape(actual.path,false) || !p::path_shape(actual.native_path,true))return false;
            actual.anchor_rva=expected.anchor;actual.prefix=expected.prefix;
            ++verified_files;
        }
        opened=true;return true;
    }
    bool acquire(Request& request){
        if(!opened || request.count!=1)return false;
        // No base-name lookup or LoadLibrary for providers. Already loaded full
        // paths must match, and every returned reference is retained through call.
        for(std::size_t i=0;i<selected.size();++i){
            auto& s=selected[i];const bool got=GetModuleHandleExW(0,s.path.data(),&refs[i])!=FALSE;
            known_refs[i]=got && refs[i];
            if(!got || !refs[i])return false;
            s.base=reinterpret_cast<std::uintptr_t>(refs[i]);p::Path path{};
            const auto count=GetModuleFileNameW(refs[i],path.data(),static_cast<DWORD>(path.size()));
            if(!count || count>=path.size() || CompareStringOrdinal(path.data(),static_cast<int>(count),
                s.path.data(),static_cast<int>(p::path_view(s.path).size()),TRUE)!=CSTR_EQUAL || !mapped_path(s))return false;
            request.selected[i+1]=s;
        }
        request.count=static_cast<std::uint32_t>(n::pins.size());return true;
    }
    bool evidence(const Request& request,std::size_t& bytes,EvidenceFault fault=EvidenceFault::None){
        bytes=0;matched_bindings=0;
        if(request.count!=n::pins.size())return false;
        for(std::size_t i=0;i<n::pins.size();++i)if(!mapped_path(request.selected[i]))return false;
        for(const auto& span:n::spans){
            if(span.provider>=request.count || !span.size || span.size>4*1024*1024)return false;
            std::vector<std::uint8_t> actual(span.size);p::Hash sha{};
            auto expected=span.hash;
            if(fault==EvidenceFault::LastSpan && &span==&n::spans.back())expected[0]^=1;
            if(!read(request.selected[span.provider],span.rva,actual,span.protect) || !hash(actual,sha) || sha!=expected)return false;
            bytes+=span.size;
        }
        for(const auto& binding:n::bindings){
            if(!binding.provider || binding.provider>=request.count || binding.rva>=request.selected[binding.provider].bytes)return false;
            std::array<std::uint8_t,8> actual{};std::uintptr_t address=0;
            if(!read(request.selected[0],binding.iat,actual,PAGE_READONLY))return false;
            std::memcpy(&address,actual.data(),8);
            auto expected=request.selected[binding.provider].base+binding.rva;
            if(fault==EvidenceFault::LastIat && &binding==&n::bindings.back())++expected;
            if(address!=expected)return false;
            ++matched_bindings;
        }
        for(std::size_t i=0;i<n::pins.size();++i)if(!mapped_path(request.selected[i]))return false;
        return true;
    }
};
} // namespace native_fixture
