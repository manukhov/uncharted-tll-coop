#define TLL_P99_WRIST_PREPARE_DEFINE_PAGE 1
#include "nadine_p99_wrist_prepare_windows.h"

#pragma section(".p99prep",read,write)
extern "C" {
__declspec(dllexport) __declspec(allocate(".p99prep")) __declspec(align(4096))
coop::p99_wrist_prepare::Page TllP99WristPreparedV1{};
}

namespace coop::p99_wrist_prepare {
#if defined(TLL_P99_WRIST_PREPARE_AUDIT)
const Audit* g_audit=nullptr;
#endif
namespace {
BOOL protect(LPVOID address,SIZE_T size,DWORD value,PDWORD old) noexcept {
#if defined(TLL_P99_WRIST_PREPARE_AUDIT)
    if(g_audit && g_audit->protect)return g_audit->protect(address,size,value,old);
#endif
    return VirtualProtect(address,size,value,old);
}
BOOL flush(HANDLE process,LPCVOID address,SIZE_T size) noexcept {
#if defined(TLL_P99_WRIST_PREPARE_AUDIT)
    if(g_audit && g_audit->flush)return g_audit->flush(process,address,size);
#endif
    return FlushInstructionCache(process,address,size);
}
int memory_fault(DWORD code) noexcept {
    return code==EXCEPTION_ACCESS_VIOLATION || code==EXCEPTION_IN_PAGE_ERROR ||
        code==EXCEPTION_GUARD_PAGE?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH;
}
bool copy_bytes(std::uintptr_t source,void* destination,std::size_t size) noexcept {
    __try {
        auto* out=static_cast<std::uint8_t*>(destination);
        auto* in=reinterpret_cast<const volatile std::uint8_t*>(source);
        for(std::size_t i=0;i<size;++i)out[i]=in[i];
        return true;
    } __except(memory_fault(GetExceptionCode())) {return false;}
}
bool write_bytes(std::uintptr_t destination,const void* source,std::size_t size) noexcept {
    __try {
        auto* out=reinterpret_cast<volatile std::uint8_t*>(destination);
        auto* in=static_cast<const std::uint8_t*>(source);
        for(std::size_t i=0;i<size;++i)out[i]=in[i];
        return true;
    } __except(memory_fault(GetExceptionCode())) {return false;}
}
bool image_header_read(std::uintptr_t module,std::uintptr_t address,void* out,std::size_t size) noexcept {
    const auto end=p99_group_ingress::add(address,size);
    if(!module || !size || !end || address<module || *end-module>0x100000) return false;
    auto cursor=address;
    while(cursor<*end) {
        MEMORY_BASIC_INFORMATION m{};
        if(VirtualQuery(reinterpret_cast<void*>(cursor),&m,sizeof(m))!=sizeof(m) ||
            m.State!=MEM_COMMIT || m.Type!=MEM_IMAGE ||
            reinterpret_cast<std::uintptr_t>(m.AllocationBase)!=module || m.Protect!=PAGE_READONLY) return false;
        const auto last=p99_group_ingress::add(reinterpret_cast<std::uintptr_t>(m.BaseAddress),m.RegionSize);
        if(reinterpret_cast<std::uintptr_t>(m.BaseAddress)>cursor || !last || *last<=cursor)return false;
        cursor=std::min(*last,*end);
    }
    return copy_bytes(address,out,size);
}
std::uintptr_t allocate_rw_near(std::uintptr_t next) noexcept {
    SYSTEM_INFO info{};GetSystemInfo(&info);
    if(info.dwPageSize!=4096 || info.dwAllocationGranularity!=0x10000)return 0;
    const auto origin=next&~std::uintptr_t{0xFFFF};
    // Bounded symmetric search, excluding next's own image allocation. VirtualAlloc
    // claims a FREE reservation itself; VirtualQuery would not create a lease.
    for(std::uintptr_t step=0;step<=0x7FFF;++step) for(unsigned side=0;side<2;++side) {
        if(!step && side)continue;
        const auto distance=step*0x10000;
        std::uintptr_t address=0;
        if(side==0) {
            const auto candidate=p99_group_ingress::add(origin,distance);
            if(!candidate)continue;address=*candidate;
        } else {
            if(distance>=origin)continue;address=origin-distance;
        }
        if(!wh::range(address,4096) || !p99_group_ingress::relative(next,address))continue;
        void* region=VirtualAlloc(reinterpret_cast<void*>(address),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(!region)continue;
        // A mismatched successful address is not used/published. Only this owned
        // unpublished allocation may be released, never a later live relay.
        if(reinterpret_cast<std::uintptr_t>(region)!=address) {VirtualFree(region,0,MEM_RELEASE);return 0;}
        return address;
    }
    return 0;
}
}

bool page_contract(std::uintptr_t address,std::uintptr_t allocation,DWORD type,DWORD protection) noexcept {
    if((address&0xFFF) || !wh::range(address,4096))return false;
    MEMORY_BASIC_INFORMATION m{};
    if(VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))!=sizeof(m) ||
        m.State!=MEM_COMMIT || m.Type!=type || m.Protect!=protection ||
        reinterpret_cast<std::uintptr_t>(m.AllocationBase)!=allocation)return false;
    const auto base=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
    const auto end=p99_group_ingress::add(base,m.RegionSize);
    return base<=address && end && *end>=address+4096;
}
bool bytes_equal(std::uintptr_t address,std::span<const std::uint8_t> bytes) noexcept {
    if(!address || bytes.empty() || !p99_group_ingress::add(address,bytes.size()))return false;
    __try {
        auto* actual=reinterpret_cast<const volatile std::uint8_t*>(address);
        for(std::size_t i=0;i<bytes.size();++i)if(actual[i]!=bytes[i])return false;
        return true;
    } __except(memory_fault(GetExceptionCode())) {return false;}
}
bool image_size(std::uintptr_t module,std::uint64_t& bytes) noexcept {
    bytes=0;
    if(module&0xFFFF)return false;
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!image_header_read(module,module,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew<static_cast<LONG>(sizeof(dos)) || dos.e_lfanew>0xFF000)return false;
    const auto address=p99_group_ingress::add(module,static_cast<std::uintptr_t>(dos.e_lfanew));
    if(!address || !image_header_read(module,*address,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64) ||
        !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections>96 ||
        nt.OptionalHeader.SectionAlignment!=4096 || !wh::range(module,nt.OptionalHeader.SizeOfImage) ||
        (nt.OptionalHeader.SizeOfImage&0xFFF) || nt.OptionalHeader.SizeOfHeaders<*address-module+sizeof(nt) ||
        nt.OptionalHeader.SizeOfHeaders>nt.OptionalHeader.SizeOfImage)return false;
    bytes=nt.OptionalHeader.SizeOfImage;return true;
}
bool process_identity(wh::Offer& o,bool new_nonce) noexcept {
    FILETIME creation{},exit{},kernel{},user{};LARGE_INTEGER frequency{};
    o.pid=GetCurrentProcessId();
    if(!o.pid || !GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user) ||
        !QueryPerformanceFrequency(&frequency) || frequency.QuadPart<1000)return false;
    o.creation=(std::uint64_t{creation.dwHighDateTime}<<32)|creation.dwLowDateTime;
    o.frequency=static_cast<std::uint64_t>(frequency.QuadPart);
    if(!o.creation)return false;
    if(new_nonce && (BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(o.nonce.data()),
        static_cast<ULONG>(sizeof(o.nonce)),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0 || !(o.nonce[0]|o.nonce[1])))return false;
    return true;
}
bool pin_image(std::uintptr_t anchor,std::uintptr_t expected_module) noexcept {
    MEMORY_BASIC_INFORMATION m{};
    if(!anchor || !expected_module || VirtualQuery(reinterpret_cast<void*>(anchor),&m,sizeof(m))!=sizeof(m) ||
        m.Type!=MEM_IMAGE || m.State!=MEM_COMMIT || m.Protect!=PAGE_EXECUTE_READ ||
        reinterpret_cast<std::uintptr_t>(m.AllocationBase)!=expected_module)return false;
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(anchor),&module) && reinterpret_cast<std::uintptr_t>(module)==expected_module;
}
bool Relays::prepare(std::uintptr_t module,const std::array<std::uintptr_t,5>& hooks) noexcept {
    if(started_)return false;started_=true;
    std::array<std::array<std::uint8_t,7>,5> stock{};
    for(std::size_t i=0;i<5;++i) {
        const auto next=p99_group_ingress::add(module,wi::kSites[i].rva+wi::kSites[i].size);
        if(!module || !hooks[i] || !next)return false;
        stock[i]=wi::kSites[i].stock;
        addresses_[i]=allocate_rw_near(*next);
        if(!addresses_[i] || !page_contract(addresses_[i],addresses_[i],MEM_PRIVATE,PAGE_READWRITE))return false;
    }
    const auto p=wi::plan(module,stock,addresses_,hooks);if(!p)return false;plan_=*p;
    for(const auto& s:plan_.sites) {
        if(!write_bytes(s.relay,s.leaf.data(),s.leaf.size()))return false;
        DWORD previous=0;
        if(!protect(reinterpret_cast<void*>(s.relay),4096,PAGE_EXECUTE_READ,&previous) || previous!=PAGE_READWRITE ||
            !flush(GetCurrentProcess(),reinterpret_cast<void*>(s.relay),s.leaf.size()))return false;
    }
    ready_=true;
    if(!verify()){ready_=false;return false;}return true;
}
bool Relays::verify() const noexcept {
    if(!ready_ || !wi::valid(plan_))return false;
    for(const auto& s:plan_.sites)
        if(!page_contract(s.relay,s.relay,MEM_PRIVATE,PAGE_EXECUTE_READ) || !bytes_equal(s.relay,s.leaf))return false;
    return true;
}
bool published(const Page& page,std::uintptr_t dll,const wh::Wire& wire) noexcept {
    const auto address=reinterpret_cast<std::uintptr_t>(&page);
    const std::array<std::uint8_t,4096-wh::kOfferBytes> zero{};
    return page_contract(address,dll,MEM_IMAGE,PAGE_READONLY) && bytes_equal(address,wire) &&
        bytes_equal(address+wh::kOfferBytes,zero) && page_contract(address,dll,MEM_IMAGE,PAGE_READONLY);
}
bool publish(Page& page,std::uintptr_t dll,const wh::Wire& wire) noexcept {
    const auto offer=wh::decode(wire);
    const auto address=reinterpret_cast<std::uintptr_t>(&page);
    const std::array<std::uint8_t,4096> zero{};
    if(!offer || offer->dll!=dll ||
        (!page_contract(address,dll,MEM_IMAGE,PAGE_READWRITE) && !page_contract(address,dll,MEM_IMAGE,PAGE_WRITECOPY)) ||
        !bytes_equal(address,zero))return false;
    // Irreversible exposure starts here, regardless of the return value.
    if(!write_bytes(address,wire.data(),wire.size()))return false;
    MemoryBarrier();
    DWORD previous=0;
    if(!protect(&page,sizeof(page),PAGE_READONLY,&previous) ||
        (previous!=PAGE_READWRITE && previous!=PAGE_WRITECOPY))return false;
    return published(page,dll,wire);
}
bool Resources::prepare(std::uintptr_t main,std::uintptr_t dll,const std::array<std::uintptr_t,5>& hooks) noexcept {
    if(started_)return false;started_=true;
    if(main!=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) || main==dll)return false;
    offer_.module=main;offer_.dll=dll;
    if(!image_size(main,offer_.image_bytes) || !image_size(dll,offer_.dll_bytes) || !process_identity(offer_,true))return false;
    for(const auto& r:p99_wrist_code_manifest::kRegions)
        if(r.rva>=offer_.image_bytes || r.byte_count>offer_.image_bytes-r.rva)return false;
    if(hooks[0]!=hooks[1])return false;
    for(std::size_t i=0;i<hooks.size();++i) {
        if(hooks[i]<dll || hooks[i]-dll>=offer_.dll_bytes)return false;
        for(std::size_t j=0;j<i;++j)
            if(hooks[i]==hooks[j] && !(i==1 && j==0))return false;
        p99_wrist_code::ImageReader reader(dll);std::uint8_t first=0;
        if(!reader.copy(hooks[i],std::span(&first,1)))return false;
        offer_.hooks[i]=hooks[i];
    }
    // Permanently pin before allocating/exposing routes. A later refusal is not
    // an unload request; this intentionally cannot be undone by worker cleanup.
    if(!pin_image(hooks[0],dll))return false;pinned_=true;
    if(!relays_.prepare(main,hooks))return false;
    for(std::size_t i=0;i<5;++i)offer_.relays[i]=relays_.addresses()[i];
    offer_.originals={main+0x1A92A00,main+0x1AB97C0,main+0x217B630};
    const auto p=wh::plan(offer_);
    if(!p || *p!=relays_.plan())return false;
    ready_=true;
    if(!identity(offer_) || !verify(offer_,*p)){ready_=false;return false;}
    return true;
}
bool Resources::identity(const wh::Offer& candidate) const noexcept {
    if(!ready_ || candidate!=offer_ || offer_.module!=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)))return false;
    wh::Offer current{};std::uint64_t main_size=0,dll_size=0;
    return process_identity(current,false) && current.pid==offer_.pid && current.creation==offer_.creation &&
        current.frequency==offer_.frequency && image_size(offer_.module,main_size) && main_size==offer_.image_bytes &&
        image_size(offer_.dll,dll_size) && dll_size==offer_.dll_bytes;
}
bool Resources::verify(const wh::Offer& candidate,const wi::Plan& plan) const noexcept {
    if(!ready_ || !pinned_ || candidate!=offer_ || plan!=relays_.plan() || !relays_.verify())return false;
    p99_wrist_code::ImageReader reader(static_cast<std::uintptr_t>(offer_.dll));
    for(auto hook:offer_.hooks) {std::uint8_t first=0;if(!reader.copy(static_cast<std::uintptr_t>(hook),std::span(&first,1)))return false;}
    return true;
}
} // namespace coop::p99_wrist_prepare
