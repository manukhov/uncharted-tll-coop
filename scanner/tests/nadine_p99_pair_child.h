#pragma once
#include "nadine_p99_prepared_pair_windows.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Same-build OWNED fixture transport only. No production protocol, arbitrary PID
// discovery, process opening, game launch, callback, patch or native export call.
namespace pair_child {
namespace bp=coop::p99_prepared_providers;
namespace pp=coop::p99_provider;
namespace wh=coop::p99_wrist_handshake;
struct Packet {
    std::uint64_t magic=0x3154534554525039ULL,bytes=sizeof(Packet);
    std::uintptr_t offer_page=0,provider_page=0;
    std::uint64_t readable=0;
    wh::Wire offer{};
    pp::Wire providers{};
};
struct Handle {
    HANDLE h=nullptr;
    Handle()=default;Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    ~Handle(){if(h)CloseHandle(h);}
};
inline HANDLE number(const wchar_t* s){
    if(!s || !*s || *s==L'-' || *s==L'+')return nullptr;
    errno=0;wchar_t* end=nullptr;const auto v=std::wcstoull(s,&end,10);
    return errno || !v || !end || *end?nullptr:reinterpret_cast<HANDLE>(v);
}
inline int read(HANDLE process,HANDLE mapping){
    if(!process || !mapping)return 70;
    const auto view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(Packet));if(!view)return 71;
    Packet data{};std::memcpy(&data,view,sizeof(data));UnmapViewOfFile(view);
    const auto offer=wh::decode(data.offer);const auto claims=pp::decode(data.providers);
    if(data.magic!=Packet{}.magic || data.bytes!=sizeof(Packet) || data.readable>1 || !offer || !claims)return 72;
    const bp::Pair expected{*offer,*claims};
    if(bp::read_pair(process,expected,data.offer_page,data.provider_page).has_value()!=(data.readable!=0))return 73;
    auto stale=expected;++stale.offer.nonce[0];++stale.providers.session.nonce[0];
    if(bp::read_pair(process,stale,data.offer_page,data.provider_page))return 74;
    stale=expected;stale.providers.records[0].expected_file_hash[0]^=1;
    if(bp::read_pair(process,stale,data.offer_page,data.provider_page))return 75;
    return 0;
}
inline bool run(const std::wstring& executable,const bp::Pair& expected,
    std::uintptr_t offer_page,std::uintptr_t provider_page,bool readable){
    const auto encoded=pp::encode(expected.providers);if(!encoded)return false;
    Packet packet{};packet.offer_page=offer_page;packet.provider_page=provider_page;
    packet.readable=readable;packet.offer=wh::encode(expected.offer);packet.providers=*encoded;
    Handle original,mapping,process;
    original.h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Packet),nullptr);
    if(!original.h)return false;
    auto view=MapViewOfFile(original.h,FILE_MAP_WRITE,0,0,sizeof(Packet));if(!view)return false;
    std::memcpy(view,&packet,sizeof(packet));MemoryBarrier();UnmapViewOfFile(view);
    if(!DuplicateHandle(GetCurrentProcess(),original.h,GetCurrentProcess(),&mapping.h,FILE_MAP_READ,TRUE,0) ||
       !DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&process.h,
           PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,TRUE,0))return false;
    const std::array<HANDLE,2> inherited{mapping.h,process.h};
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    if(!bytes || bytes>65536)return false;
    std::vector<std::uint8_t> storage(bytes);
    auto attrs=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attrs,1,0,&bytes))return false;
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attrs;
    std::wstring command=L"\""+executable+L"\" --pair-reader "+
        std::to_wstring(reinterpret_cast<std::uintptr_t>(process.h))+L" "+
        std::to_wstring(reinterpret_cast<std::uintptr_t>(mapping.h));
    PROCESS_INFORMATION child{};
    const bool started=UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        const_cast<HANDLE*>(inherited.data()),sizeof(inherited),nullptr,nullptr) &&
        CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
            CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&child);
    DeleteProcThreadAttributeList(attrs);if(!started)return false;
    Handle child_process,child_thread;child_process.h=child.hProcess;child_thread.h=child.hThread;
    if(WaitForSingleObject(child.hProcess,10000)!=WAIT_OBJECT_0){
        // Only the process just created above. It reads owned fixture pages only.
        (void)TerminateProcess(child.hProcess,76);(void)WaitForSingleObject(child.hProcess,5000);return false;
    }
    DWORD status=77;return GetExitCodeProcess(child.hProcess,&status) && status==0;
}
} // namespace pair_child
