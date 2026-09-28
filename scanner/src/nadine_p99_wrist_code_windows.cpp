#include "nadine_p99_wrist_code_windows.h"

namespace coop::p99_wrist_code {
#if defined(TLL_P99_WRIST_CODE_AUDIT)
void (*g_before_code_read)(std::uintptr_t,std::size_t) noexcept=nullptr;
#endif
namespace {
int read_fault(DWORD code) noexcept {
    return code==EXCEPTION_ACCESS_VIOLATION || code==EXCEPTION_IN_PAGE_ERROR ||
        code==EXCEPTION_GUARD_PAGE ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
// POD-only SEH leaf functions: a protection/unmap race refuses even after a
// successful VirtualQuery. No CRT memcpy/memcmp can over-read the exact extent.
bool copy_bytes(std::uintptr_t address,std::uint8_t* out,std::size_t size) noexcept {
    __try {
        const auto* src=reinterpret_cast<const volatile std::uint8_t*>(address);
        for(std::size_t i=0;i<size;++i) out[i]=src[i];
        return true;
    } __except(read_fault(GetExceptionCode())) {return false;}
}
bool equal_bytes(std::uintptr_t address,const std::uint8_t* expected,std::size_t size) noexcept {
    __try {
        const auto* src=reinterpret_cast<const volatile std::uint8_t*>(address);
        for(std::size_t i=0;i<size;++i) if(src[i]!=expected[i]) return false;
        return true;
    } __except(read_fault(GetExceptionCode())) {return false;}
}
}

Sha256::Sha256() noexcept {
    BCRYPT_ALG_HANDLE provider=nullptr;
    if(BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0) algorithm_=provider;
}
Sha256::~Sha256() {if(algorithm_) (void)BCryptCloseAlgorithmProvider(algorithm_,0);}
bool Sha256::digest(std::span<const std::uint8_t> owned,std::array<std::uint8_t,32>& result) noexcept {
    result={};
    if(!algorithm_ || owned.empty() || owned.size()>std::numeric_limits<ULONG>::max()) return false;
    std::array<std::uint8_t,32> value{};
    if(BCryptHash(algorithm_,nullptr,0,const_cast<PUCHAR>(owned.data()),static_cast<ULONG>(owned.size()),
        value.data(),static_cast<ULONG>(value.size()))!=0) return false;
    result=value;return true;
}

ImageReader::ImageReader(std::uintptr_t module,std::span<const std::uintptr_t> writable) noexcept
    :module_(module) {
    if(!module || writable.size()>writable_.size()) return;
    for(const auto page:writable) {
        bool planned=false;
        for(const auto& site:p99_wrist_install::kSites) {
            const auto address=p99_group_ingress::add(module,site.rva);
            if(address && (*address&~std::uintptr_t{0xFFF})==page) planned=true;
        }
        if(!planned) return;
        for(std::size_t i=0;i<count_;++i) if(writable_[i]==page) return;
        writable_[count_++]=page;
    }
    valid_=true;
}
bool ImageReader::accessible(std::uintptr_t address,std::size_t size) const noexcept {
    if(!valid_ || address<module_ || !size || size>0x10000 ||
        !p99_group_ingress::add(address,size)) return false;
    const auto end=address+size;
    auto cursor=address;
    for(unsigned steps=0;cursor<end && steps<64;++steps) {
        MEMORY_BASIC_INFORMATION mbi{};
        if(VirtualQuery(reinterpret_cast<void*>(cursor),&mbi,sizeof(mbi))!=sizeof(mbi) ||
            mbi.State!=MEM_COMMIT || mbi.Type!=MEM_IMAGE ||
            reinterpret_cast<std::uintptr_t>(mbi.AllocationBase)!=module_) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto last=p99_group_ingress::add(base,mbi.RegionSize);
        if(base>cursor || !last || *last<=cursor) return false;
        const auto next=std::min(*last,end);
        // A writable SEC_IMAGE page may remain EXECUTE_WRITECOPY until its
        // first COW write. Both are allowed ONLY on explicitly owned patch pages.
        if(mbi.Protect==PAGE_EXECUTE_READWRITE || mbi.Protect==PAGE_EXECUTE_WRITECOPY) {
            for(auto page=cursor&~std::uintptr_t{0xFFF};page<next;) {
                bool admitted=false;
                for(std::size_t i=0;i<count_;++i) if(writable_[i]==page) admitted=true;
                if(!admitted) return false;
                const auto page_end=p99_group_ingress::add(page,0x1000);
                if(!page_end) return false;
                page=*page_end;
            }
        } else if(mbi.Protect!=PAGE_EXECUTE_READ) return false;
        cursor=next;
    }
    return cursor==end;
}
bool ImageReader::copy(std::uintptr_t address,std::span<std::uint8_t> owned) const noexcept {
    if(!accessible(address,owned.size())) return false;
#if defined(TLL_P99_WRIST_CODE_AUDIT)
    if(g_before_code_read) g_before_code_read(address,owned.size());
#endif
    return copy_bytes(address,owned.data(),owned.size());
}
bool ImageReader::equal(std::uintptr_t address,std::span<const std::uint8_t> owned) const noexcept {
    if(!accessible(address,owned.size())) return false;
#if defined(TLL_P99_WRIST_CODE_AUDIT)
    if(g_before_code_read) g_before_code_read(address,owned.size());
#endif
    return equal_bytes(address,owned.data(),owned.size());
}
} // namespace coop::p99_wrist_code
