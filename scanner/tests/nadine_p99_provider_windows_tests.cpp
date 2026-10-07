#include "nadine_p99_provider_windows.h"
#include "nadine_p99_provider_literal.h"
#include <bcrypt.h>
#include <psapi.h>
#include <iostream>
#include <string>
#include <vector>

namespace p=coop::p99_provider;
#pragma section(".p99dep",read,write)
__declspec(allocate(".p99dep")) __declspec(align(4096)) p::Page g_page{};
#pragma section(".p99rx",execute,read)
__declspec(allocate(".p99rx")) const auto g_anchor=kProviderLiteral;

namespace {
unsigned checks=0,failures=0;
bool check(bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<name<<" error="<<GetLastError()<<'\n';}return ok;}
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    File()=default;File(const File&)=delete;File& operator=(const File&)=delete;
    ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
bool make_expected(const std::wstring& filename,std::uintptr_t base,std::uintptr_t anchor,File& held,p::Selection& s){
    if(filename.empty() || filename.size()>=p::kPath)return false;
    std::copy(filename.begin(),filename.end(),s.path.begin());
    held.value=CreateFileW(filename.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(held.value==INVALID_HANDLE_VALUE)return false;
    const auto n=GetFinalPathNameByHandleW(held.value,s.native_path.data(),static_cast<DWORD>(s.native_path.size()),VOLUME_NAME_NT);
    if(!n || n>=s.native_path.size())return false;
    LARGE_INTEGER size{};if(!GetFileSizeEx(held.value,&size) || size.QuadPart<=0 || size.QuadPart>16*1024*1024)return false;
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(size.QuadPart));DWORD got=0;
    if(!ReadFile(held.value,raw.data(),static_cast<DWORD>(raw.size()),&got,nullptr) || got!=raw.size())return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0))return false;
    const auto hashed=BCryptHash(algorithm,nullptr,0,raw.data(),static_cast<ULONG>(raw.size()),s.expected_file_hash.data(),32)==0;
    BCryptCloseAlgorithmProvider(algorithm,0);if(!hashed)return false;
    // Own built executable/DLL only, bounds checked disk header size; prefix is
    // the independently defined literal above, never a copy from loaded code.
    if(raw.size()<sizeof(IMAGE_DOS_HEADER))return false;
    IMAGE_DOS_HEADER dos{};std::copy_n(raw.data(),sizeof(dos),reinterpret_cast<std::uint8_t*>(&dos));
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<sizeof(dos) ||
       static_cast<std::size_t>(dos.e_lfanew)+sizeof(IMAGE_NT_HEADERS64)>raw.size())return false;
    IMAGE_NT_HEADERS64 nt{};std::copy_n(raw.data()+dos.e_lfanew,sizeof(nt),reinterpret_cast<std::uint8_t*>(&nt));
    if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
    s.base=base;s.bytes=nt.OptionalHeader.SizeOfImage;s.anchor_rva=anchor-base;s.prefix=kProviderLiteral;return true;
}
p::Session session(std::uintptr_t base,std::uint64_t bytes){
    FILETIME start{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&start,&exit,&kernel,&user);
    return {GetCurrentProcessId(),(std::uint64_t{start.dwHighDateTime}<<32)|start.dwLowDateTime,
            {0x99112233,0x44556677},base,bytes}; // fixed test nonce, NOT authentication
}
struct Audit {
    p::WindowsOps os{g_page};
    std::wstring mode;
    unsigned acquires=0,pins=0,stores=0;
    unsigned retained=0,pinned=0;
    explicit Audit(std::wstring value):mode(std::move(value)){}
    std::uint32_t thread() const noexcept{return os.thread();}
    bool identity(const p::Session& s) const noexcept{return os.identity(s);}
    bool fault(const wchar_t* prefix,unsigned call)const noexcept{
        return mode==std::wstring(prefix)+std::to_wstring(call);
    }
    p::Acquire acquire(const p::Selection& s) noexcept{
        ++acquires;if(fault(L"acquire-before-",acquires))return {};
        auto result=os.acquire(s);if(result.okay)++retained;
        if(fault(L"acquire-after-",acquires))result.okay=false;return result;
    }
    bool verify(const p::Selection& s,std::uintptr_t ref)const noexcept{return os.verify(s,ref);}
    bool pin(const p::Selection& s,std::uintptr_t ref) noexcept{
        ++pins;if(fault(L"pin-before-",pins))return false;
        const bool result=os.pin(s,ref);if(result)++pinned;
        return fault(L"pin-after-",pins)?false:result;
    }
    bool publish(const p::Session& s,const p::Wire& wire) noexcept{
        ++stores;
        if(mode==L"partial-write"){std::copy_n(wire.begin(),24,g_page.bytes.begin());return false;}
        if(mode==L"seal-before"){g_page.bytes=wire;return false;}
        const bool result=os.publish(s,wire);return mode==L"seal-after"?false:result;
    }
};
struct Cross {p::Owner* owner;Audit* ops;bool result=true;};
DWORD WINAPI wrong_thread(void* value){auto& c=*static_cast<Cross*>(value);c.result=c.owner->publish(*c.ops);return 0;}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const std::wstring mode=argv[1];
    const std::array<std::wstring_view,17> modes{L"normal",L"writable-rw",L"acquire-before-1",L"acquire-after-1",L"acquire-before-2",L"acquire-after-2",
        L"pin-before-1",L"pin-after-1",L"pin-before-2",L"pin-after-2",L"partial-write",L"seal-before",L"seal-after",
        L"wrong-prefix",L"wrong-path",L"wrong-size",L"wrong-session"};
    if(std::find(modes.begin(),modes.end(),mode)==modes.end())return 2;
    std::array<wchar_t,32768> own{};
    const auto chars=GetModuleFileNameW(nullptr,own.data(),static_cast<DWORD>(own.size()));
    if(!chars || chars>=own.size())return 3;
    const std::wstring executable(own.data(),chars);
    const auto file=executable.substr(0,executable.find_last_of(L'\\')+1)+L"tll_p99_provider_fixture.dll";
    HMODULE initial=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!check(initial!=nullptr,"load only fixed owned literal fixture"))return 1;
    const auto literal=GetProcAddress(initial,"P99ProviderLiteral"); // locate data, never call
    const auto self=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::array<p::Selection,2> expected{};File self_file,dll_file;
    if(!check(make_expected(executable,self,reinterpret_cast<std::uintptr_t>(g_anchor.data()),self_file,expected[0]) &&
              make_expected(file,reinterpret_cast<std::uintptr_t>(initial),reinterpret_cast<std::uintptr_t>(literal),dll_file,expected[1]),
              "held file hashes/native paths plus deterministic prefixes")){FreeLibrary(initial);return 1;}
    auto s=session(self,expected[0].bytes);Audit ops(mode);
    const auto address=reinterpret_cast<std::uintptr_t>(&g_page);
    if(mode==L"writable-rw"){
        DWORD previous=0;check(VirtualProtect(&g_page,p::kPage,PAGE_READWRITE,&previous)!=FALSE,"explicit initial RW fixture");
    }
    check(!p::read_claims(GetCurrentProcess(),s,address),"initial RW/zero page not claims");
    if(mode==L"wrong-prefix")expected[1].prefix[7]^=1;
    if(mode==L"wrong-path"){
        const auto filename=p::path_view(expected[1].native_path).find_last_of(L'\\')+1;
        expected[1].native_path[filename]=L'T'; // keep valid syntax; exact mapped spelling differs
    }
    if(mode==L"wrong-size")expected[1].bytes+=4096;
    if(mode==L"wrong-session")++s.creation;
    const bool should_publish=mode==L"normal" || mode==L"writable-rw";
    const bool should_prepare=should_publish || mode==L"partial-write" || mode==L"seal-before" || mode==L"seal-after";
    bool prepared=false,published=false;
    {
        p::Owner owner;prepared=owner.prepare(s,expected,ops);
        check(prepared==should_prepare,"prepare prediction");
        if(prepared){
            Audit other(mode);check(!owner.publish(other) && other.stores==0,"different backend cannot publish");
            Cross context{&owner,&ops};HANDLE thread=CreateThread(nullptr,0,wrong_thread,&context,0,nullptr);
            check(thread!=nullptr,"owned thread fixture");
            if(thread){
                // A timed-out thread still borrows these stack objects. End only
                // this owned test process; never unwind them or TerminateThread.
                if(!check(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"foreign thread joins before borrowed stack expires"))ExitProcess(1);
                check(!context.result && ops.stores==0,"foreign thread no publication");CloseHandle(thread);
            }
        }
        published=owner.publish(ops);
        check(published==should_publish,"publication prediction");
        check(!owner.prepare(s,expected,ops) && !owner.publish(ops),"one-shot no retry");
        if(should_prepare)check(ops.retained==2 && ops.pinned==2,"all providers including page artifact retained before stores");
        if(!should_prepare)check(ops.stores==0,"failed preparation never stores");
        if(mode==L"wrong-path" || mode==L"wrong-prefix" || mode==L"wrong-size")
            check(ops.retained==2 && ops.pinned==0,"valid selections retained but mismatch refuses before first PIN");
        if(mode==L"seal-after")check(owner.state()==p::State::PublishUnknown,"false-after-effect not upgraded to local success");
    }
    const auto calls=ops.stores;
    const auto copied=p::read_claims(GetCurrentProcess(),s,address);
    const bool readable=should_publish || mode==L"seal-after";
    check(copied.has_value()==readable,"RO claims readable only after actual seal");
    if(copied){
        check(copied->count==2 && copied->records[1].expected_file_hash==expected[1].expected_file_hash,"claims retain expected hash identity");
        auto wrong=s;++wrong.nonce[0];check(!p::read_claims(GetCurrentProcess(),wrong,address),"different session nonce refuses");
        wrong=s;++wrong.creation;check(!p::read_claims(GetCurrentProcess(),wrong,address),"different process creation refuses");
        check(!p::read_claims(GetCurrentProcess(),s,address+1),"unaligned publication refuses");
    }
    const auto dll_base=reinterpret_cast<std::uintptr_t>(initial);
    const auto releasing=initial;initial=nullptr;check(FreeLibrary(releasing)!=FALSE,"drop only test-owned initial load reference");
    MEMORY_BASIC_INFORMATION m{};check(VirtualQuery(reinterpret_cast<void*>(dll_base),&m,sizeof(m))==sizeof(m),"inspect owned mapping after owner destructor");
    const bool dll_retained=ops.retained==2;
    check(dll_retained?(m.Type==MEM_IMAGE && reinterpret_cast<std::uintptr_t>(m.AllocationBase)==dll_base):m.State==MEM_FREE,
          "actual acquired refs survive destruction/failure; no acquired ref means unload");
    check(calls==ops.stores,"destructor never publishes/erases");
    std::cout<<checks<<" checks, "<<failures<<" failures; retained="<<ops.retained<<" pinned="<<ops.pinned<<" stores="<<ops.stores<<'\n';
    return failures?1:0;
}
