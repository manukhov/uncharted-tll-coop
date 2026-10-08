#pragma once
#include "nadine_p99_provider_fixture_support.h"
#include <cstring>

// Bounded own-build fixture locator, not a general DLL or install admission parser.
namespace bootstrap_disk {
struct Image {
    std::vector<std::uint8_t> raw;
    IMAGE_NT_HEADERS64 nt{};
    std::vector<IMAGE_SECTION_HEADER> sections;
    bool read(provider_fixture::File& file){
        LARGE_INTEGER zero{},size{};
        if(!SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN) || !GetFileSizeEx(file.value,&size) ||
           size.QuadPart<sizeof(IMAGE_DOS_HEADER) || size.QuadPart>16*1024*1024)return false;
        raw.resize(static_cast<std::size_t>(size.QuadPart));DWORD got=0;
        if(!ReadFile(file.value,raw.data(),static_cast<DWORD>(raw.size()),&got,nullptr) || got!=raw.size())return false;
        IMAGE_DOS_HEADER dos{};std::memcpy(&dos,raw.data(),sizeof(dos));
        if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<sizeof(dos) ||
           static_cast<std::size_t>(dos.e_lfanew)+sizeof(nt)>raw.size())return false;
        std::memcpy(&nt,raw.data()+dos.e_lfanew,sizeof(nt));
        if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
           nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
           nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64) ||
           !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections>96)return false;
        const auto start=static_cast<std::size_t>(dos.e_lfanew)+sizeof(nt);
        const auto count=nt.FileHeader.NumberOfSections;
        if(start+count*sizeof(IMAGE_SECTION_HEADER)>raw.size())return false;
        sections.resize(count);std::memcpy(sections.data(),raw.data()+start,count*sizeof(IMAGE_SECTION_HEADER));return true;
    }
    std::span<const std::uint8_t> bytes(std::uint64_t rva,std::size_t size) const{
        if(!size || rva>nt.OptionalHeader.SizeOfImage || size>nt.OptionalHeader.SizeOfImage-rva)return {};
        std::span<const std::uint8_t> found;
        for(const auto& s:sections){
            if(rva<s.VirtualAddress || rva-s.VirtualAddress>=s.Misc.VirtualSize)continue;
            const auto offset=rva-s.VirtualAddress;
            if(size>s.Misc.VirtualSize-offset || offset>s.SizeOfRawData || size>s.SizeOfRawData-offset)return {};
            const auto at=std::uint64_t{s.PointerToRawData}+offset;
            if(at>raw.size() || size>raw.size()-at || !found.empty())return {};
            found=std::span(raw).subspan(static_cast<std::size_t>(at),size);
        }return found;
    }
    template<class T> bool value(std::uint64_t rva,T& out)const{
        const auto b=bytes(rva,sizeof(T));if(b.size()!=sizeof(T))return false;std::memcpy(&out,b.data(),sizeof(T));return true;
    }
    std::uint32_t named(std::string_view name)const{
        const auto d=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];IMAGE_EXPORT_DIRECTORY e{};
        if(d.Size<sizeof(e) || d.Size>65536 || !value(d.VirtualAddress,e) ||
           e.NumberOfFunctions!=10 || e.NumberOfNames!=10 || e.Base!=1)return 0;
        for(std::uint32_t i=0;i<e.NumberOfNames;++i){
            std::uint32_t nr=0,rva=0;std::uint16_t ordinal=0;
            if(!value(std::uint64_t{e.AddressOfNames}+i*4,nr) ||
               !value(std::uint64_t{e.AddressOfNameOrdinals}+i*2,ordinal) || ordinal>=e.NumberOfFunctions)return 0;
            std::string actual;
            for(unsigned n=0;n<256;++n){std::uint8_t c=0;if(!value(std::uint64_t{nr}+n,c))return 0;if(!c)break;
                if(c<32 || c>126 || n==255)return 0;actual.push_back(static_cast<char>(c));}
            if(actual!=name)continue;
            if(!value(std::uint64_t{e.AddressOfFunctions}+ordinal*4,rva) || !rva ||
               (rva>=d.VirtualAddress && std::uint64_t{rva}<std::uint64_t{d.VirtualAddress}+d.Size))return 0;
            return rva;
        }return 0;
    }
    bool prefix(std::uint32_t rva,coop::p99_provider::Hash& out)const{
        const auto b=bytes(rva,out.size());if(b.size()!=out.size())return false;
        bool rx=false;for(const auto& s:sections)if(rva>=s.VirtualAddress && rva-s.VirtualAddress<s.Misc.VirtualSize)
            rx=(s.Characteristics&IMAGE_SCN_MEM_EXECUTE) && (s.Characteristics&IMAGE_SCN_MEM_READ) && !(s.Characteristics&IMAGE_SCN_MEM_WRITE);
        if(!rx)return false;
        const auto d=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        for(std::uint64_t at=0;at<d.Size;){IMAGE_BASE_RELOCATION block{};
            if(d.Size-at<sizeof(block) || !value(std::uint64_t{d.VirtualAddress}+at,block) ||
               block.SizeOfBlock<sizeof(block) || (block.SizeOfBlock&1) || block.SizeOfBlock>d.Size-at)return false;
            for(std::uint64_t p=sizeof(block);p<block.SizeOfBlock;p+=2){std::uint16_t fixup=0;
                if(!value(std::uint64_t{d.VirtualAddress}+at+p,fixup))return false;
                const auto type=fixup>>12;if(!type)continue;if(type!=IMAGE_REL_BASED_DIR64)return false;
                const auto target=std::uint64_t{block.VirtualAddress}+(fixup&4095);
                if(target<std::uint64_t{rva}+32 && rva<target+8)return false;
            }at+=block.SizeOfBlock;
        }
        std::copy(b.begin(),b.end(),out.begin());return true;
    }
    bool zero_page(std::uint32_t rva)const{
        for(const auto& s:sections)if(s.VirtualAddress==rva && s.Misc.VirtualSize==4096 &&
           (s.Characteristics&IMAGE_SCN_MEM_READ) && (s.Characteristics&IMAGE_SCN_MEM_WRITE) && !(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)){
            const auto data=bytes(rva,4096);return data.size()==4096 && std::all_of(data.begin(),data.end(),[](auto b){return !b;});
        }return false;
    }
};
} // namespace bootstrap_disk
