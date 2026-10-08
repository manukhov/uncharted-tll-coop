#include "nadine_p99_provider_preparation.h"
#include "nadine_p99_bootstrap_disk.h"
#include "nadine_p99_wrist_code_manifest.h"
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
namespace boot=coop::p99_provider_preparation;
namespace pp=coop::p99_provider;
#if defined(TLL_P99_BOOTSTRAP_PAD)
#pragma bss_seg(".p99pad")
__declspec(align(4096)) volatile unsigned char g_padding[0x2600000];
#pragma bss_seg()
#endif
namespace {
unsigned checks=0,failures=0;
bool check(bool b,const char* what){++checks;if(!b){++failures;std::cerr<<what<<" error="<<GetLastError()<<'\n';}return b;}
bool empty_page(std::uintptr_t base,std::uintptr_t address){
    MEMORY_BASIC_INFORMATION m{};std::array<std::uint8_t,4096> bytes{};SIZE_T got=0;
    return !(address&4095) && VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))==sizeof(m) &&
        m.AllocationBase==reinterpret_cast<void*>(base) && m.Type==MEM_IMAGE && m.State==MEM_COMMIT &&
        (m.Protect==PAGE_READWRITE || m.Protect==PAGE_WRITECOPY) &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes.data(),bytes.size(),&got) &&
        got==bytes.size() && std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0;});
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const std::wstring mode=argv[1];
    const std::array<std::wstring_view,19> modes{L"empty",L"null",L"noaccess",L"partial",L"unaligned",L"short",L"version",L"reserved",L"oversize",
        L"tail",L"path",L"duplicate",L"wrong-self",L"wrong-main",L"wrong-creation",L"wrong-size",L"normal",L"prefix",L"race"};
    if(std::find(modes.begin(),modes.end(),mode)==modes.end())return 2;
#if defined(TLL_P99_BOOTSTRAP_PAD)
    g_padding[0]=1; // Own writable BSS, never executable retail data.
#endif
    std::array<wchar_t,32768> own{};const auto chars=GetModuleFileNameW(nullptr,own.data(),static_cast<DWORD>(own.size()));
    if(!chars || chars>=own.size())return 3;const std::wstring executable(own.data(),chars);
    const auto filename=executable.substr(0,executable.find_last_of(L'\\')+1)+L"tll_p99_provider_preparation.dll";
    provider_fixture::File held;pp::Selection selected{};
    if(!check(provider_fixture::make_expected(filename,0,0,held,selected),"lock/hash own artifact before loading"))return 1;
    bootstrap_disk::Image disk;if(!check(disk.read(held),"bounded disk locator metadata"))return 1;
    const auto entry=disk.named("TllP99PrepareProvidersV1"),offer=disk.named("TllP99WristPreparedV1"),providers=disk.named("TllP99ProviderPreparedV1");
    if(!check(entry && offer && providers && offer!=providers && disk.zero_page(offer) && disk.zero_page(providers) &&
        disk.prefix(entry,selected.prefix),"two distinct zero RW disk sections and unrelocated RX entry prefix"))return 1;
    HMODULE loaded=LoadLibraryExW(filename.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!check(loaded!=nullptr,"load only new inert artifact"))return 1;
    const auto base=reinterpret_cast<std::uintptr_t>(loaded);
    const auto prepare=reinterpret_cast<boot::Prepare>(GetProcAddress(loaded,"TllP99PrepareProvidersV1"));
    if(!check(reinterpret_cast<std::uintptr_t>(prepare)==base+entry &&
        reinterpret_cast<std::uintptr_t>(GetProcAddress(loaded,"TllP99WristPreparedV1"))==base+offer &&
        reinterpret_cast<std::uintptr_t>(GetProcAddress(loaded,"TllP99ProviderPreparedV1"))==base+providers,"loaded locators equal independent disk RVAs"))return 1;
    check(empty_page(base,base+offer) && empty_page(base,base+providers),"DllMain never prepares either page");
    auto request=std::make_unique<boot::Request>();request->count=1;request->artifact=base;request->artifact_bytes=selected.bytes;
    selected.base=base;selected.anchor_rva=entry;request->selected[0]=selected;
    request->pid=GetCurrentProcessId();FILETIME start{},exit{},kernel{},user{};
    if(!check(GetProcessTimes(GetCurrentProcess(),&start,&exit,&kernel,&user)!=FALSE,"actual process creation"))return 1;
    request->creation=(std::uint64_t{start.dwHighDateTime}<<32)|start.dwLowDateTime;
    request->main=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(request->main);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(request->main+dos->e_lfanew);
    request->main_bytes=nt->OptionalHeader.SizeOfImage; // own built test EXE only
#if defined(TLL_P99_BOOTSTRAP_PAD)
    MEMORY_BASIC_INFORMATION first_region{};
    const auto first_address=request->main+coop::p99_wrist_code_manifest::kRegions[0].rva;
    check(VirtualQuery(reinterpret_cast<void*>(first_address),&first_region,sizeof(first_region))==sizeof(first_region) &&
        first_region.AllocationBase==reinterpret_cast<void*>(request->main) && first_region.Type==MEM_IMAGE &&
        (first_region.Protect==PAGE_READWRITE || first_region.Protect==PAGE_WRITECOPY),
        "first required retail region is owned non-RX BSS, so actual Snapshot reader must refuse");
#endif
    auto expected=boot::Result::InputRefused;
    if(mode==L"empty"){request->count=0;request->selected[0]={};}
    if(mode==L"version")++request->version;if(mode==L"reserved")request->reserved=1;
    if(mode==L"oversize")request->count=33;if(mode==L"tail")request->selected[31].prefix[0]=1;
    if(mode==L"path")request->selected[0].path[0]=L'/';
    if(mode==L"duplicate"){request->count=2;request->selected[1]=request->selected[0];}
    if(mode==L"wrong-self"){request->artifact+=0x10000;request->selected[0].base=request->artifact;expected=boot::Result::IdentityRefused;}
    if(mode==L"wrong-main"){request->main+=0x10000;expected=boot::Result::IdentityRefused;}
    if(mode==L"wrong-creation"){++request->creation;expected=boot::Result::IdentityRefused;}
    if(mode==L"wrong-size"){request->main_bytes+=4096;expected=boot::Result::IdentityRefused;}
    if(mode==L"prefix")request->selected[0].prefix[0]^=1;
    if(mode==L"normal" || mode==L"prefix" || mode==L"race"){
#if defined(TLL_P99_BOOTSTRAP_PAD)
        expected=mode==L"prefix"?boot::Result::ProviderRefused:boot::Result::SnapshotRefused;
#else
        expected=boot::Result::ResourcesRefused;
#endif
    }
    void* unreadable=nullptr;
    if(mode==L"noaccess" || mode==L"partial"){
        unreadable=VirtualAlloc(nullptr,sizeof(boot::Request),MEM_RESERVE|MEM_COMMIT,mode==L"noaccess"?PAGE_NOACCESS:PAGE_READWRITE);
        if(!check(unreadable!=nullptr,"own inaccessible input"))return 1;
        if(mode==L"partial"){
            std::memcpy(unreadable,request.get(),sizeof(*request));DWORD old=0;
            auto* last=static_cast<std::uint8_t*>(unreadable)+((sizeof(*request)-1)&~std::size_t{4095});
            if(!check(VirtualProtect(last,4096,PAGE_NOACCESS,&old)!=FALSE,"make only last input page inaccessible"))return 1;
        }
    }
    const auto input=mode==L"null"?nullptr:unreadable?static_cast<const boot::Request*>(unreadable):
        mode==L"unaligned"?reinterpret_cast<const boot::Request*>(reinterpret_cast<const std::uint8_t*>(request.get())+1):request.get();
    const auto size=static_cast<std::uint32_t>(sizeof(*request)-(mode==L"short"?1:0));
    std::uint32_t result=0;
    if(mode==L"race"){
        std::atomic<bool> go=false;std::array<std::uint32_t,2> results{};
        const auto call=[&](unsigned i){while(!go.load(std::memory_order_acquire))std::this_thread::yield();results[i]=prepare(input,size);};
        std::thread first(call,0),second(call,1);go.store(true,std::memory_order_release);first.join();second.join();
        const auto reused=static_cast<std::uint32_t>(boot::Result::Reused);
        check((results[0]==reused)!=(results[1]==reused),"exactly one concurrent entry consumes one shot");
        result=results[0]==reused?results[1]:results[0];
    }else result=prepare(input,size);
    check(result==static_cast<std::uint32_t>(expected),"actual full-TU refusal at predicted stage");
    check(prepare(nullptr,sizeof(*request))==static_cast<std::uint32_t>(boot::Result::Reused),"repeat refuses before dereferencing caller input");
    check(empty_page(base,base+offer) && empty_page(base,base+providers),"both pages stay zero/writable after actual refusal");
    if(unreadable)check(VirtualFree(unreadable,0,MEM_RELEASE)!=FALSE,"release only own input page");
    check(FreeLibrary(loaded)!=FALSE,"release initial artifact reference only");
    MEMORY_BASIC_INFORMATION mapping{};check(VirtualQuery(reinterpret_cast<void*>(base),&mapping,sizeof(mapping))==sizeof(mapping),"inspect artifact lifetime after own loader release");
    const bool retained=expected==boot::Result::ProviderRefused || expected==boot::Result::SnapshotRefused;
    check(retained?(mapping.Type==MEM_IMAGE && mapping.AllocationBase==reinterpret_cast<void*>(base)):mapping.State==MEM_FREE,
        "early refusal has no PIN; later real Resources/Owner PIN survives refusal");
    std::cout<<checks<<" checks, "<<failures<<" failures; result="<<result<<" OWNED_FULL_TU_REFUSAL_ONLY\n";
    return failures?1:0;
}
