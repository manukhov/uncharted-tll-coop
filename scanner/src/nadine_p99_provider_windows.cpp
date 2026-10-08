#include "nadine_p99_provider_windows.h"
#include <psapi.h>

namespace coop::p99_provider {
namespace {
bool process_identity(HANDLE process,const Session& s) noexcept {
    FILETIME start{},exit{},kernel{},user{};
    return WaitForSingleObject(process,0)==WAIT_TIMEOUT && GetProcessId(process)==s.pid &&
        GetProcessTimes(process,&start,&exit,&kernel,&user) &&
        ((std::uint64_t{start.dwHighDateTime}<<32)|start.dwLowDateTime)==s.creation;
}
bool query(HANDLE process,std::uintptr_t allocation,std::uintptr_t address,std::size_t count,
           DWORD protection) noexcept {
    if(!range(address,count))return false;
    auto cursor=address;unsigned work=0;
    while(cursor<address+count) {
        MEMORY_BASIC_INFORMATION m{};
        if(++work>64 || VirtualQueryEx(process,reinterpret_cast<void*>(cursor),&m,sizeof(m))!=sizeof(m) ||
           m.State!=MEM_COMMIT || m.Type!=MEM_IMAGE || m.Protect!=protection ||
           reinterpret_cast<std::uintptr_t>(m.AllocationBase)!=allocation)return false;
        const auto first=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
        if(!range(first,m.RegionSize) || first>cursor || first+m.RegionSize<=cursor)return false;
        cursor=std::min<std::uintptr_t>(address+count,first+m.RegionSize);
    }
    return true;
}
bool copy(HANDLE process,std::uintptr_t allocation,std::uintptr_t address,
          std::span<std::uint8_t> out,DWORD protection) noexcept {
    SIZE_T actual=0;
    return !out.empty() && query(process,allocation,address,out.size(),protection) &&
        ReadProcessMemory(process,reinterpret_cast<void*>(address),out.data(),out.size(),&actual) &&
        actual==out.size() && query(process,allocation,address,out.size(),protection);
}
template<class T> bool structure(std::uintptr_t base,std::uintptr_t address,T& out) noexcept {
    return copy(GetCurrentProcess(),base,address,
                {reinterpret_cast<std::uint8_t*>(&out),sizeof(out)},PAGE_READONLY);
}
bool extent(const Selection& s) noexcept {
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!structure(s.base,s.base,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
       dos.e_lfanew<sizeof(dos) || dos.e_lfanew>0x100000 ||
       std::uint64_t{static_cast<unsigned>(dos.e_lfanew)}+sizeof(nt)>s.bytes ||
       !structure(s.base,s.base+dos.e_lfanew,nt))return false;
    return nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
        (nt.FileHeader.Characteristics&IMAGE_FILE_EXECUTABLE_IMAGE) &&
        nt.FileHeader.SizeOfOptionalHeader==sizeof(IMAGE_OPTIONAL_HEADER64) &&
        nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.OptionalHeader.SectionAlignment==4096 && nt.OptionalHeader.SizeOfImage==s.bytes;
}
bool mapped_path(const Selection& s) noexcept {
    Path actual{};
    const auto count=GetMappedFileNameW(GetCurrentProcess(),reinterpret_cast<void*>(s.base),
                                       actual.data(),static_cast<DWORD>(actual.size()));
    return count && count<actual.size() && path_view(actual).size()==count && actual==s.native_path;
}
int memory_fault(DWORD code) noexcept {
    return code==EXCEPTION_ACCESS_VIOLATION || code==EXCEPTION_IN_PAGE_ERROR || code==EXCEPTION_GUARD_PAGE?
        EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH;
}
bool store(Page& page,const Wire& wire) noexcept {
    __try {
        auto* out=reinterpret_cast<volatile std::uint8_t*>(&page);
        for(std::size_t i=0;i<wire.size();++i)out[i]=wire[i];return true;
    } __except(memory_fault(GetExceptionCode())) {return false;}
}
}
std::uint32_t WindowsOps::thread() const noexcept {return GetCurrentThreadId();}
bool WindowsOps::identity(const Session& s) const noexcept {
    return image(s.artifact,s.artifact_bytes) && s.creation && s.pid &&
        process_identity(GetCurrentProcess(),s);
}
Acquire WindowsOps::acquire(const Selection& s) noexcept {
    if(!path_shape(s.path,false))return {};
    HMODULE module=nullptr;
    const bool okay=GetModuleHandleExW(0,s.path.data(),&module)!=FALSE;
    return {okay,reinterpret_cast<std::uintptr_t>(module)};
}
bool WindowsOps::verify(const Selection& s,std::uintptr_t retained) const noexcept {
    if(retained!=s.base || !image(s.base,s.bytes) || !s.anchor_rva || s.anchor_rva>=s.bytes ||
       s.prefix.size()>s.bytes-s.anchor_rva || !path_shape(s.native_path,true))return false;
    Hash actual{};
    return mapped_path(s) && extent(s) &&
        copy(GetCurrentProcess(),s.base,s.base+s.anchor_rva,actual,PAGE_EXECUTE_READ) &&
        actual==s.prefix && mapped_path(s) && extent(s);
}
bool WindowsOps::pin(const Selection& s,std::uintptr_t retained) noexcept {
    // Full-path reference already acquired by Owner; never PIN a naked discovery.
    if(!verify(s,retained))return false;
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(s.base+s.anchor_rva),&module) && reinterpret_cast<std::uintptr_t>(module)==retained;
}
bool WindowsOps::publish(const Session& session,const Wire& wire) noexcept {
    const auto claims=decode(wire);const auto address=reinterpret_cast<std::uintptr_t>(&page_);
    if(!claims || claims->session!=session || !identity(session) || (address&4095) ||
       address<session.artifact || address-session.artifact>session.artifact_bytes ||
       kPage>session.artifact_bytes-(address-session.artifact))return false;
    Wire initial{};
    DWORD protection=PAGE_READWRITE;
    if(!query(GetCurrentProcess(),session.artifact,address,kPage,protection))protection=PAGE_WRITECOPY;
    if(!copy(GetCurrentProcess(),session.artifact,address,initial,protection) ||
       initial!=Wire{} || !store(page_,wire))return false;
    // A write to a WRITECOPY image page can make its protection READWRITE.
    // Revalidate the complete stored page and current writable protection;
    // comparing VirtualProtect's old value with the pre-write value is wrong.
    MemoryBarrier();Wire stored{};
    protection=PAGE_READWRITE;
    if(!query(GetCurrentProcess(),session.artifact,address,kPage,protection))protection=PAGE_WRITECOPY;
    if(!copy(GetCurrentProcess(),session.artifact,address,stored,protection) || stored!=wire)return false;
    DWORD previous=0;
    if(!VirtualProtect(&page_,kPage,PAGE_READONLY,&previous) || previous!=protection)return false;
    return read_claims(GetCurrentProcess(),session,address)==claims;
}
std::optional<Claims> read_claims(HANDLE process,const Session& expected,std::uintptr_t address) noexcept {
    if(!process || !image(expected.artifact,expected.artifact_bytes) || (address&4095) ||
       address<expected.artifact || address-expected.artifact>expected.artifact_bytes ||
       kPage>expected.artifact_bytes-(address-expected.artifact) || !process_identity(process,expected))return {};
    Wire first{},second{};
    if(!copy(process,expected.artifact,address,first,PAGE_READONLY))return {};
    const auto claims=decode(first);
    if(!claims || claims->session!=expected || !copy(process,expected.artifact,address,second,PAGE_READONLY) ||
       first!=second || !process_identity(process,expected))return {};
    return claims; // Claimed pins/expected hashes require independent verification; NOT admission.
}
std::optional<Claims> WindowsOps::read(const Session& expected) const noexcept {
    return read_claims(GetCurrentProcess(),expected,reinterpret_cast<std::uintptr_t>(&page_));
}
} // namespace coop::p99_provider
