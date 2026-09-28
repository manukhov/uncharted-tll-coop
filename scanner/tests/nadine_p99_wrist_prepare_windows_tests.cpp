#include "nadine_p99_wrist_prepare_windows.h"
#include "nadine_p99_group_fiber.h"
#include "nadine_p99_wrist_remote_read.h"
#include <iostream>
#include <string_view>

namespace prep=coop::p99_wrist_prepare;
namespace wh=coop::p99_wrist_handshake;
namespace wi=coop::p99_wrist_install;
namespace {
unsigned checks=0,failures=0,protect_calls=0,flush_calls=0,fail_protect=0,fail_flush=0;
bool after_effect=false;
void check(bool okay,const char* label){++checks;if(!okay){++failures;std::cerr<<label<<'\n';}}
__declspec(noinline) int hook0(){return 31;}
__declspec(noinline) int hook1(){return 32;}
__declspec(noinline) int hook2(){return 33;}
__declspec(noinline) int hook3(){return 34;}
std::array<std::uintptr_t,5> hooks(){return {
    reinterpret_cast<std::uintptr_t>(&hook0),reinterpret_cast<std::uintptr_t>(&hook0),
    reinterpret_cast<std::uintptr_t>(&hook1),reinterpret_cast<std::uintptr_t>(&hook2),reinterpret_cast<std::uintptr_t>(&hook3)};}
BOOL protect(LPVOID p,SIZE_T n,DWORD value,PDWORD old) noexcept {
    ++protect_calls;
    if(protect_calls==fail_protect && !after_effect)return FALSE;
    const auto result=VirtualProtect(p,n,value,old);
    return protect_calls==fail_protect?FALSE:result;
}
BOOL flush(HANDLE process,LPCVOID p,SIZE_T n) noexcept {
    ++flush_calls;const auto result=FlushInstructionCache(process,p,n);
    return flush_calls==fail_flush?FALSE:result;
}
const prep::Audit audit{protect,flush};
void reset_faults(){protect_calls=flush_calls=fail_protect=fail_flush=0;after_effect=false;prep::g_audit=&audit;}
constexpr std::uintptr_t kProjectedMain=0x140000000;
wh::Offer offer(std::uintptr_t module,const prep::Relays& relays) {
    wh::Offer o{};
    check(prep::process_identity(o,true),"real PID/creation/QPC/random nonce");
    o.module=kProjectedMain;o.image_bytes=0x8000000;o.dll=module;
    check(prep::image_size(module,o.dll_bytes),"own PE64 size");
    const auto h=hooks();for(unsigned i=0;i<5;++i){o.hooks[i]=h[i];o.relays[i]=relays.addresses()[i];}
    o.originals={o.module+0x1A92A00,o.module+0x1AB97C0,o.module+0x217B630};
    check(wh::plan(o).has_value(),"synthetic main identity plus owned relays forms value plan");
    return o;
}
void metadata_tests(std::uintptr_t module,const wchar_t* fixture) {
    std::uint64_t size=123;
    check(prep::image_size(module,size) && size>4096,"read own image extent");
    check(!prep::image_size(module+1,size) && size==0,"unaligned image refuses");
    check(!prep::image_size(0,size) && size==0,"zero image refuses");
    void* private_page=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(private_page!=nullptr,"owned private metadata fixture");
    if(private_page) {
        check(!prep::image_size(reinterpret_cast<std::uintptr_t>(private_page),size),"private page not PE image");
        check(!prep::pin_image(reinterpret_cast<std::uintptr_t>(private_page),module),"private address never pins");
        VirtualFree(private_page,0,MEM_RELEASE);
    }
    wh::Offer a{},b{};check(prep::process_identity(a,true) && prep::process_identity(b,true),"independent process identities");
    check(a.pid==GetCurrentProcessId() && a.pid==b.pid && a.creation==b.creation && a.frequency==b.frequency && a.nonce!=b.nonce,"stable process with distinct session nonces");
    b.nonce={11,22};check(prep::process_identity(b,false) && b.nonce==std::array<std::uint64_t,2>{11,22},"identity recheck never replaces nonce");
    const auto own_dll=LoadLibraryW(fixture);check(own_dll!=nullptr,"load only inert owned fixture DLL");
    if(own_dll) {
        const auto anchor=reinterpret_cast<std::uintptr_t>(GetProcAddress(own_dll,"p99_pin_anchor"));
        check(anchor!=0,"owned pin anchor export");
        check(!prep::pin_image(anchor,module),"wrong DLL identity refuses PIN");
        check(prep::pin_image(anchor,reinterpret_cast<std::uintptr_t>(own_dll)),"permanent PIN by real executable address");
        check(FreeLibrary(own_dll)!=FALSE,"release only loader owned reference");
        HMODULE retained=nullptr;
        check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(anchor),&retained) && retained==own_dll,"DLL remains loaded after initial reference released");
    }
    struct Frame : coop::p99_group_fiber::Binding {};
    coop::p99_group_fiber::Slot<Frame> fls;
    check(!fls.initialized() && fls.initialize() && fls.initialized(),"actual one-owner FLS pair");
    check(!fls.initialize() && !fls.failed() && fls.current()==nullptr,"FLS duplicate init refuses, empty bootstrap valid");
    Frame frame;Frame* previous=nullptr;
    check(fls.enter(frame,previous) && fls.current()==&frame && fls.leave(frame,previous) && fls.current()==nullptr,"actual borrowed frame enter/leave");
    fls.poison();check(fls.initialized() && fls.failed() && fls.current()==nullptr,"poison does not revoke initialized resource lifetime");
    prep::Resources refused;
    check(!refused.prepare(module,module,hooks()) && !refused.prepare(module,module,hooks()),"Resources refuses shared main/DLL identity and retry");
}
void relay_faults() {
    for(unsigned kind=0;kind<3;++kind)for(unsigned nth=1;nth<=5;++nth) {
        reset_faults();
        if(kind==2)fail_flush=nth;else{fail_protect=nth;after_effect=kind==1;}
        std::array<std::uintptr_t,5> retained{};
        {
            prep::Relays failed;
            check(!failed.prepare(kProjectedMain,hooks()) && !failed.verify(),"relay protect/flush failure refuses");
            check(!failed.prepare(kProjectedMain,hooks()),"relay failure consumes one shot");
            retained=failed.addresses();
        }
        for(auto address:retained) {
            MEMORY_BASIC_INFORMATION m{};
            check(address && VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))==sizeof(m) &&
                m.State==MEM_COMMIT && m.Type==MEM_PRIVATE &&
                (m.Protect==PAGE_READWRITE || m.Protect==PAGE_EXECUTE_READ),"all owned relay allocations retained on failure/destruction, never RWX");
        }
    }
    reset_faults();
}
void export_page_tests(std::uintptr_t module,const wh::Wire& wire,std::string_view mode) {
    auto& page=TllP99WristPreparedV1;
    const auto address=reinterpret_cast<std::uintptr_t>(&page);
    check(reinterpret_cast<std::uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(module),"TllP99WristPreparedV1"))==address,"exact exported page address");
    check((address&4095)==0,"dedicated export page aligned");
    // Inspect only THIS owned test executable's loaded section table.
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(module+dos->e_lfanew);
    const auto* section=IMAGE_FIRST_SECTION(nt);bool isolated=false;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        if(std::memcmp(section[i].Name,".p99prep",8)!=0)continue;
        isolated=module+section[i].VirtualAddress==address && section[i].Misc.VirtualSize==4096 &&
            (section[i].Characteristics&IMAGE_SCN_MEM_WRITE) && !(section[i].Characteristics&IMAGE_SCN_MEM_EXECUTE);
    }
    check(isolated,"entire dedicated non-executable 4096B PE section");
    check(!prep::published(page,module,wire),"zero writable export is NOT published");
    std::uint64_t image_bytes=0;check(prep::image_size(module,image_bytes),"publication reader expected image extent");
    const auto read_offer=[&]{return coop::p99_wrist_code::read_prepared_offer(GetCurrentProcess(),module,image_bytes,address);};
    check(!read_offer(),"remote publication reader refuses prepublication RW page");
    auto bad=wire;bad[0]^=1;check(!prep::publish(page,module,bad),"invalid offer no exposure");
    check(!prep::publish(page,module+0x10000,wire),"wrong allocation no exposure");
    reset_faults();
    if(mode=="seal-before" || mode=="seal-after") {fail_protect=1;after_effect=mode=="seal-after";}
    const bool result=prep::publish(page,module,wire);
    check(result==(mode=="normal"),"publication result reflects seal failure");
    check(protect_calls==1 && flush_calls==0,"data publication seals once; no executable flush or patch");
    check(prep::bytes_equal(address,wire),"wire retained even on failed seal, never erased");
    check(prep::published(page,module,wire)==(mode!="seal-before"),"false-after-effect may already expose valid RO wire");
    check(read_offer()==(mode=="seal-before"?std::optional<wh::Offer>{}:wh::decode(wire)),"same wire returned by actual VQEx/RPM publication reader only after RO seal");
    check(!coop::p99_wrist_code::read_prepared_offer(GetCurrentProcess(),module,image_bytes,address+1),"unaligned publication refused");
    check(!coop::p99_wrist_code::read_prepared_offer(GetCurrentProcess(),module,image_bytes+4096,address),"declared image size mismatch refused");
    reset_faults();check(!prep::publish(page,module,wire) && protect_calls==0,"publication cannot overwrite/retry");
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    const std::wstring_view requested=argv[1];
    const std::string_view mode=requested==L"normal"?"normal":requested==L"seal-before"?"seal-before":requested==L"seal-after"?"seal-after":"invalid";
    if(mode=="invalid")return 2;
    // Reserve only an uncommitted synthetic extent so the real allocator cannot
    // place relays inside the declared main image. No PE or retail bytes loaded.
    const auto projected=VirtualAlloc(reinterpret_cast<void*>(kProjectedMain),0x8000000,MEM_RESERVE,PAGE_NOACCESS);
    if(reinterpret_cast<std::uintptr_t>(projected)!=kProjectedMain)return 4;
    const auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(mode=="normal"){metadata_tests(module,argv[2]);relay_faults();}
    reset_faults();prep::Relays relays;
    const bool ready=relays.prepare(kProjectedMain,hooks());
    check(ready && relays.verify(),"five actual RW-to-RX relays, never executed");
    if(!ready)return 3;
    check(protect_calls==5 && flush_calls==5,"every leaf sealed and flushed");
    check(!relays.prepare(kProjectedMain,hooks()),"success cannot reprepare");
    const auto o=offer(module,relays);export_page_tests(module,wh::encode(o),mode);
    check(relays.verify(),"relays still retained after any publication outcome");
    prep::g_audit=nullptr;
    std::cout<<checks<<" checks, "<<failures<<" failures; OWNED_OS_ONLY "<<mode<<'\n';return failures?1:0;
}
