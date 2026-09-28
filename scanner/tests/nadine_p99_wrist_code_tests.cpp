#include "nadine_p99_wrist_code_windows.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace wc=coop::p99_wrist_code;
namespace cm=coop::p99_wrist_code_manifest;
namespace wi=coop::p99_wrist_install;
#pragma section(".p99audit",read,execute)
// Dedicated nonexecuted data in this test's image, never a game/module page.
__declspec(allocate(".p99audit")) __declspec(align(4096)) const std::uint8_t audit_pages[8192]={0x51};
namespace {
unsigned checks=0,failures=0;
void check(bool yes,const char* label) {++checks;if(!yes){++failures;std::cerr<<label<<'\n';}}
wi::Plan plan() {
    constexpr std::uintptr_t base=0x140000000;
    std::array<std::array<std::uint8_t,7>,5> stock{};
    std::array<std::uintptr_t,5> relays{},hooks{};
    for(std::size_t i=0;i<5;++i) {
        stock[i]=wi::kSites[i].stock;relays[i]=base+0x2600000+i*32;hooks[i]=base+0x2700000+i*32;
    }
    return wi::plan(base,stock,relays,hooks).value();
}
// File reader only: no PE load, retail execution, or game process. Actual SHA256
// checks below certify all copied regions against the generated pinned manifest.
std::vector<std::vector<std::uint8_t>> load_regions(const char* path) {
    std::ifstream file(path,std::ios::binary);
    const auto read=[&](std::size_t offset,void* dest,std::size_t size) {
        file.seekg(static_cast<std::streamoff>(offset));file.read(static_cast<char*>(dest),size);
        if(!file) throw std::runtime_error("read pinned PE fixture");
    };
    IMAGE_DOS_HEADER dos{};read(0,&dos,sizeof(dos));
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0) throw std::runtime_error("DOS identity");
    IMAGE_NT_HEADERS64 nt{};read(dos.e_lfanew,&nt,sizeof(nt));
    if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.FileHeader.NumberOfSections>64)
        throw std::runtime_error("PE identity");
    std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
    read(dos.e_lfanew+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader,
        sections.data(),sections.size()*sizeof(IMAGE_SECTION_HEADER));
    std::vector<std::vector<std::uint8_t>> result;
    for(const auto& r:cm::kRegions) {
        bool found=false;std::vector<std::uint8_t> raw(r.byte_count);
        for(const auto& s:sections) if(r.rva>=s.VirtualAddress &&
            r.rva+r.byte_count<=s.VirtualAddress+s.SizeOfRawData) {
            if(found) throw std::runtime_error("ambiguous region");
            found=true;read(s.PointerToRawData+r.rva-s.VirtualAddress,raw.data(),raw.size());
        }
        if(!found) throw std::runtime_error("missing region");
        result.push_back(std::move(raw));
    }
    return result;
}
struct Reader {
    const wi::Plan& p;
    std::vector<std::vector<std::uint8_t>> contents;
    unsigned copies=0,compares=0,fail_copy=0,fail_compare=0;
    Reader(const wi::Plan& patch,const std::vector<std::vector<std::uint8_t>>& data):p(patch),contents(data){}
    std::size_t index(std::uintptr_t address,std::size_t size) const noexcept {
        for(std::size_t i=0;i<cm::kRegions.size();++i)
            if(p.module+cm::kRegions[i].rva==address && cm::kRegions[i].byte_count==size) return i;
        return cm::kRegions.size();
    }
    bool copy(std::uintptr_t address,std::span<std::uint8_t> bytes) noexcept {
        ++copies;const auto i=index(address,bytes.size());if(i==contents.size())return false;
        std::copy_n(contents[i].begin(),copies==fail_copy?bytes.size()/2:bytes.size(),bytes.begin());
        return copies!=fail_copy;
    }
    bool equal(std::uintptr_t address,std::span<const std::uint8_t> bytes) noexcept {
        ++compares;const auto i=index(address,bytes.size());
        return i<contents.size() && compares!=fail_compare && std::equal(bytes.begin(),bytes.end(),contents[i].begin());
    }
    void installed() {
        for(std::size_t i=0;i<cm::kRegions.size();++i) for(std::size_t c=0;c<3;++c) {
            const auto& r=cm::kRegions[i];const auto& s=p.sites[c+2];
            if(s.address>=p.module+r.rva && s.address+5<=p.module+r.rva+r.byte_count)
                std::copy_n(s.after.begin(),5,contents[i].begin()+s.address-p.module-r.rva);
        }
    }
};
struct Hasher {
    wc::Sha256 native;
    unsigned calls=0,fail=0;
    bool digest(std::span<const std::uint8_t> bytes,std::array<std::uint8_t,32>& out) noexcept {
        ++calls;if(calls==fail) {out.fill(0xAA);return false;}
        return native.digest(bytes,out);
    }
};
void snapshot_tests(const std::vector<std::vector<std::uint8_t>>& data) {
    const auto p=plan();Reader reader(p,data);Hasher hash;
    auto snapshot=std::make_unique<wc::Snapshot>();
    check(!snapshot->matches(cm::Stage::Stock,reader) && reader.compares==0,"unprepared snapshot never reads");
    check(snapshot->prepare(p,reader,hash) && snapshot->ready() && hash.calls==63 && reader.copies==63,
        "all63 actual SHA256 regions prepared");
    check(snapshot->matches(cm::Stage::Stock,reader),"stock fresh compare");
    check(!snapshot->matches(cm::Stage::Installed,reader),"stock is not installed");
    reader.installed();
    check(snapshot->matches(cm::Stage::Installed,reader),"exact own installed CALLs accepted");
    check(!snapshot->matches(cm::Stage::Stock,reader),"installed is not stock");
    const auto calls=hash.calls;
    for(std::size_t i=0;i<reader.contents.size();++i) {
        reader.contents[i][0]^=1;
        check(!snapshot->matches(cm::Stage::Installed,reader),"mutation in each code region refuses");
        reader.contents[i][0]^=1;
        check(snapshot->matches(cm::Stage::Installed,reader),"restored owned bytes match");
    }
    check(hash.calls==calls && reader.copies==63,"matches never hashes or recaptures baseline");
    check(!snapshot->matches(static_cast<cm::Stage>(7),reader),"invalid stage refuses");
    for(unsigned i=1;i<=63;++i) {
        reader.compares=0;reader.fail_compare=i;
        check(!snapshot->matches(cm::Stage::Installed,reader) && reader.compares==i,"each fresh compare failure refuses");
    }
    reader.fail_compare=0;
    check(!snapshot->prepare(p,reader,hash) && !snapshot->ready() &&
        snapshot->diagnostic().reason==wc::Failure::Reused,"reprepare refuses and invalidates stale evidence");
    for(unsigned i=1;i<=63;++i) {
        for(unsigned mode=0;mode<3;++mode) {
            Reader failing(p,data);Hasher h;auto s=std::make_unique<wc::Snapshot>();
            if(mode==0)failing.fail_copy=i;
            if(mode==1)h.fail=i;
            if(mode==2)failing.contents[i-1][0]^=1;
            const auto expected=mode==0?wc::Failure::Read:mode==1?wc::Failure::Hash:wc::Failure::Digest;
            check(!s->prepare(p,failing,h) && !s->ready() && s->diagnostic().region==i-1 &&
                s->diagnostic().reason==expected,"every partial-read/hash/changed-region refusal isolated");
            check(!s->matches(cm::Stage::Stock,failing) && failing.compares==0,"partial preparation cannot compare as ready");
        }
    }
    for(std::size_t c=0;c<3;++c) for(std::size_t byte=0;byte<5;++byte) {
        Reader failing(p,data);Hasher h;auto s=std::make_unique<wc::Snapshot>();
        for(std::size_t i=0;i<cm::kRegions.size();++i) {
            const auto& r=cm::kRegions[i];const auto rva=cm::kCallsites[c].rva;
            if(rva>=r.rva && rva<r.rva+r.byte_count) failing.contents[i][rva-r.rva+byte]^=1;
        }
        check(!s->prepare(p,failing,h) && s->diagnostic().reason==wc::Failure::Call,"each foreign stock CALL byte refuses");
    }
    auto bad=p;bad.sites[0].address++;
    Reader untouched(p,data);Hasher h;auto s=std::make_unique<wc::Snapshot>();
    check(!s->prepare(bad,untouched,h) && untouched.copies==0 && h.calls==0,"invalid plan refuses before reads and crypto");
}
__declspec(noinline) unsigned native_owned(unsigned x) {return x*17+31;}
void fault_second_page(std::uintptr_t,std::size_t) noexcept {
    DWORD old=0;
    (void)VirtualProtect(const_cast<std::uint8_t*>(audit_pages)+4096,4096,PAGE_NOACCESS,&old);
}
void windows_reader_tests() {
    const auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto address=reinterpret_cast<std::uintptr_t>(&native_owned);
    wc::ImageReader reader(module);
    std::array<std::uint8_t,8> bytes{};
    check(reader.copy(address,bytes) && reader.equal(address,bytes),"real own MEM_IMAGE RX checked-copy/compare");
    bytes[0]^=1;check(!reader.equal(address,bytes),"real fresh byte comparison refuses mismatch");bytes[0]^=1;
    wc::ImageReader wrong(module+0x10000);
    check(!wrong.copy(address,bytes),"foreign AllocationBase refuses");
    std::array<std::uintptr_t,1> unplanned{address&~std::uintptr_t{0xFFF}};
    wc::ImageReader invalid(module,unplanned);
    check(!invalid.copy(address,bytes),"arbitrary writable-page whitelist refuses");
    check(!reader.copy(0,bytes) && !reader.copy(UINTPTR_MAX-2,bytes) &&
        !reader.copy(address,{}),"null/overflow/zero extents refuse");
    const auto owned_address=reinterpret_cast<std::uintptr_t>(audit_pages);
    std::array<std::uint8_t,16> crossing{};
    check(reader.copy(owned_address+4096-8,crossing),"real image two-page copy");
    DWORD page_old=0;
    for(const DWORD protection:{DWORD(PAGE_READONLY),DWORD(PAGE_NOACCESS),DWORD(PAGE_EXECUTE),
        DWORD(PAGE_EXECUTE_READWRITE),DWORD(PAGE_EXECUTE_READ|PAGE_GUARD)}) {
        check(VirtualProtect(const_cast<std::uint8_t*>(audit_pages),4096,protection,&page_old)!=FALSE,
            "change owned image fixture protection");
        MEMORY_BASIC_INFORMATION before{};VirtualQuery(audit_pages,&before,sizeof(before));
        check(!reader.copy(owned_address,bytes),"non admitted executable protection refused");
        MEMORY_BASIC_INFORMATION mbi{};VirtualQuery(audit_pages,&mbi,sizeof(mbi));
        check(mbi.Type==MEM_IMAGE && mbi.Protect==before.Protect,"metadata query did not consume guard or alter protection");
        if(protection==PAGE_EXECUTE_READWRITE)
            check(before.Protect==PAGE_EXECUTE_READWRITE || before.Protect==PAGE_EXECUTE_WRITECOPY,
                "owned image writable request permits observed COW metadata");
        check(VirtualProtect(const_cast<std::uint8_t*>(audit_pages),4096,PAGE_EXECUTE_READ,&page_old)!=FALSE,
            "restore owned image fixture protection");
    }
    crossing.fill(0xAA);wc::g_before_code_read=&fault_second_page;
    check(!reader.copy(owned_address+4096-8,crossing),"SEH catches post-query page loss after partial copy");
    wc::g_before_code_read=nullptr;
    check(std::all_of(crossing.begin(),crossing.begin()+8,[](auto value){return value==0;}) &&
        std::all_of(crossing.begin()+8,crossing.end(),[](auto value){return value==0xAA;}),
        "partial read is not mistaken for complete owned evidence");
    check(VirtualProtect(const_cast<std::uint8_t*>(audit_pages)+4096,4096,PAGE_EXECUTE_READ,&page_old)!=FALSE,
        "restore partial-read fixture");
    crossing.fill(0);wc::g_before_code_read=&fault_second_page;
    check(!reader.equal(owned_address+4096-8,crossing),"SEH catches post-query loss during comparison");
    wc::g_before_code_read=nullptr;
    check(VirtualProtect(const_cast<std::uint8_t*>(audit_pages)+4096,4096,PAGE_EXECUTE_READ,&page_old)!=FALSE &&
        reader.equal(owned_address+4096-8,crossing),"subsequent compare rechecks restored image metadata");
    auto* memory=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    check(memory!=nullptr,"owned private fixture");
    if(memory) {
        wc::ImageReader private_reader(reinterpret_cast<std::uintptr_t>(memory));
        check(!private_reader.copy(reinterpret_cast<std::uintptr_t>(memory),bytes),"private allocation cannot impersonate image");
        DWORD old=0;check(VirtualProtect(memory,4096,PAGE_READWRITE|PAGE_GUARD,&old)!=FALSE,"guard private fixture");
        check(!private_reader.copy(reinterpret_cast<std::uintptr_t>(memory),bytes),"guard refused without reading");
        MEMORY_BASIC_INFORMATION mbi{};VirtualQuery(memory,&mbi,sizeof(mbi));
        check((mbi.Protect&PAGE_GUARD)!=0,"guard alarm unconsumed");
        check(VirtualFree(memory,0,MEM_RELEASE)!=FALSE,"free own fixture");
    }
    wc::Sha256 sha;std::array<std::uint8_t,32> digest{};
    const std::array<std::uint8_t,3> abc{'a','b','c'};
    const std::array<std::uint8_t,32> abc_hash{0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    check(sha.digest(abc,digest) && digest==abc_hash,"real SHA256 known answer");
    check(!sha.digest({},digest) && digest==std::array<std::uint8_t,32>{},"invalid hash input clears stale output");
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {snapshot_tests(load_regions(argv[1]));windows_reader_tests();}
    catch(const std::exception& e) {check(false,e.what());}
    std::cout<<"loaded code evidence checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
