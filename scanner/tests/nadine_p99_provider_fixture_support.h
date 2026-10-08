#pragma once
#include "nadine_p99_provider_windows.h"
#include "nadine_p99_provider_literal.h"
#include <bcrypt.h>
#include <string>
#include <vector>

namespace provider_fixture {
namespace p=coop::p99_provider;
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    File()=default;File(const File&)=delete;File& operator=(const File&)=delete;
    ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
inline bool make_expected(const std::wstring& filename,std::uintptr_t base,std::uintptr_t anchor,File& held,p::Selection& s){
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
    // Only own built files. Prefix is fixed literal data, never learned from a mapping.
    if(raw.size()<sizeof(IMAGE_DOS_HEADER))return false;
    IMAGE_DOS_HEADER dos{};std::copy_n(raw.data(),sizeof(dos),reinterpret_cast<std::uint8_t*>(&dos));
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<sizeof(dos) ||
       static_cast<std::size_t>(dos.e_lfanew)+sizeof(IMAGE_NT_HEADERS64)>raw.size())return false;
    IMAGE_NT_HEADERS64 nt{};std::copy_n(raw.data()+dos.e_lfanew,sizeof(nt),reinterpret_cast<std::uint8_t*>(&nt));
    if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
    s.base=base;s.bytes=nt.OptionalHeader.SizeOfImage;s.anchor_rva=anchor-base;s.prefix=kProviderLiteral;return true;
}
} // namespace provider_fixture
