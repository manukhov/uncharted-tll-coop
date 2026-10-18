#include "enemy_jointblend_prepare.h"
#include "enemy_jointblend_offer.h"
#include "enemy_jointblend_collector.h"
#include "enemy_jointblend_read_windows.h"
#include <Windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace p = coop::enemy_jointblend_prepare;
namespace n = coop::enemy_jointblend_native_contract;
namespace hooks = coop::enemy_jointblend_hooks;
namespace {
unsigned checks{}, failures{};
void check(bool ok, const char* why) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", why); } }
std::uint64_t creation() {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) ExitProcess(2);
    return (std::uint64_t{c.dwHighDateTime} << 32) | c.dwLowDateTime;
}
bool zero(const p::Page* page) {
    const p::Page empty{};
    return std::memcmp(page, &empty, sizeof(empty)) == 0;
}
bool protection(const void* at, DWORD expected) {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(at, &m, sizeof(m)) == sizeof(m) && m.State == MEM_COMMIT && m.Protect == expected;
}
void protect(void* at, DWORD value) {
    DWORD old{}; if (!VirtualProtect(at, 4096, value, &old)) ExitProcess(2);
}
std::array<unsigned, 6> calls{};
void __fastcall jointblend(void*) { ++calls[0]; }
void* __fastcall reset(void*, const void*) { ++calls[1]; return reinterpret_cast<void*>(0x1230); }
std::uint32_t __fastcall process(const std::uint32_t*, std::uint32_t, const void*, std::uint32_t, const void*, const void*, const void*) {
    ++calls[2]; return 0xFEDCBA98;
}
std::uintptr_t __fastcall postpose(void*, void*, const void*, std::uint32_t, const void*, const void*) { ++calls[3]; return 0x123456789ABCDEFull; }
void __fastcall primary(void*) { ++calls[4]; }
std::int32_t __fastcall body_store(void*, const void*) { ++calls[5]; return -17; }
std::uint64_t now(void*) noexcept { return 100; }
void dormant_case() {
    // Owned functions only: this is NOT a positive production prepare in a
    // fake retail host. Its strict image/hash checks are never bypassed.
    auto reader = std::make_unique<coop::enemy_jointblend_read::Reader>();
    using Collector = coop::enemy_jointblend_collector::Collector<coop::enemy_jointblend_read::Reader>;
    auto collector = std::make_unique<Collector>(*reader, n::ModuleImage{0x140000000, p::kRetailImageSize},
        coop::enemy_jointblend_collector::Clock{nullptr, now, 1000000});
    check(!collector->requested(), "fresh collector is dormant");
    check(!collector->quiescent(), "fresh is NOT stopped/quiescent; never use quiescent as prepare admission");
    check(hooks::configure({jointblend, reset, process, postpose, primary, body_store}, collector->sink()), "configure dormant sink without arming");
    const auto target = hooks::targets();
    for (auto at : target.code) check(at != 0, "configured hook target is present");
    check(target.postpose != 0, "configured postpose target is present");
    reinterpret_cast<hooks::JointBlend>(target.code[0])(nullptr);
    reinterpret_cast<hooks::JointBlend>(target.code[1])(nullptr);
    check(reinterpret_cast<hooks::Reset>(target.code[2])(nullptr, nullptr) == reinterpret_cast<void*>(0x1230), "dormant Reset forwards pointer result");
    for (const auto i : {3, 4}) check(reinterpret_cast<hooks::Process>(target.code[i])(nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr) == 0xFEDCBA98,
        "dormant Process forwards uint32 result");
    check(reinterpret_cast<hooks::Postpose>(target.postpose)(nullptr, nullptr, nullptr, 0, nullptr, nullptr) == 0x123456789ABCDEFull,
        "dormant Game postpose forwards opaque RAX");
    reinterpret_cast<hooks::Primary>(target.code[5])(nullptr);
    check(reinterpret_cast<hooks::Store>(target.code[6])(nullptr, nullptr) == -17, "dormant Store forwards signed result");
    check(calls == std::array<unsigned, 6>{2, 1, 2, 1, 1, 1}, "each dormant entry forwards exactly once");
    check(!collector->requested() && reader->totals().queries == 0 && reader->totals().copies == 0,
        "dormant preparation performs no observation reads");
    check(collector->arm(), "preparation has not consumed the future one-shot arm");
    collector->stop();
    check(collector->quiescent(), "only stopped collector admits worker quiescence");
    // Hooks retain their sink: emulate the process-lifetime production owner.
    (void)collector.release();
    (void)reader.release();
}
void dll_case(const wchar_t* mode, const wchar_t* path) {
    const auto module = LoadLibraryW(path);
    check(module != nullptr, "load explicit preparation DLL"); if (!module) return;
    const auto entry = reinterpret_cast<p::Entry>(GetProcAddress(module, "TllEnemyPrepareV1"));
    const auto* page = reinterpret_cast<const p::Page*>(GetProcAddress(module, "TllEnemyBindingV1"));
    const auto* offer = reinterpret_cast<const coop::enemy_jointblend_offer::Page*>(GetProcAddress(module, "TllEnemyOfferV1"));
    const coop::enemy_jointblend_offer::Page empty_offer{};
    check(offer && std::memcmp(offer, &empty_offer, sizeof(empty_offer)) == 0, "separate external offer starts zero/inert");
    check(entry && page, "exact exported ABI"); if (!entry || !page) return;
    check((reinterpret_cast<std::uintptr_t>(page) & 4095) == 0 && zero(page), "load is inert: dedicated binding page stays zero");
    check(protection(page, PAGE_READWRITE) || protection(page, PAGE_WRITECOPY), "unpublished page is not a read-only offer");
    if (!wcscmp(mode, L"inert")) {
        check(FreeLibrary(module) != 0 && GetModuleHandleW(path) == nullptr, "load alone does not pin or retain DLL");
        const auto reloaded = LoadLibraryW(path);
        check(reloaded != nullptr, "load again after ordinary unload");
        const auto* again = reinterpret_cast<const p::Page*>(GetProcAddress(reloaded, "TllEnemyBindingV1"));
        check(again && zero(again), "reloaded page is still inert");
        if (reloaded) FreeLibrary(reloaded);
        return;
    }
    p::Request request{};
    request.pid = GetCurrentProcessId(); request.creation = creation();
    request.main = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    request.self = reinterpret_cast<std::uintptr_t>(module);
    auto bytes = static_cast<std::uint32_t>(sizeof(request));
    const p::Request* input = &request;
    p::Result expected = p::Result::input_refused;
    bool retained = false;
    void* allocated{};
    if (!wcscmp(mode, L"size")) --bytes;
    else if (!wcscmp(mode, L"version")) ++request.version;
    else if (!wcscmp(mode, L"reserved")) request.reserved64 = 1;
    else if (!wcscmp(mode, L"pid")) { ++request.pid; expected = p::Result::identity_refused; }
    else if (!wcscmp(mode, L"creation")) { ++request.creation; expected = p::Result::identity_refused; }
    else if (!wcscmp(mode, L"main")) { request.main += 0x10000; expected = p::Result::identity_refused; }
    else if (!wcscmp(mode, L"self")) { request.self += 0x10000; expected = p::Result::identity_refused; }
    else if (!wcscmp(mode, L"null")) input = nullptr;
    else if (!wcscmp(mode, L"guard") || !wcscmp(mode, L"split")) {
        allocated = VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!allocated) ExitProcess(2);
        const auto delta = !wcscmp(mode, L"guard") ? 4096 : 4096-sizeof(request)/2;
        auto* place = static_cast<std::byte*>(allocated)+delta;
        std::memcpy(place, &request, sizeof(request)); input = reinterpret_cast<p::Request*>(place);
        protect(static_cast<std::byte*>(allocated)+4096, PAGE_READWRITE | PAGE_GUARD);
    } else if (!wcscmp(mode, L"late") || !wcscmp(mode, L"concurrent")) {
        expected = p::Result::image_refused; retained = true;
    } else { check(false, "known case"); return; }
    if (!wcscmp(mode, L"concurrent")) {
        std::atomic<bool> start{};
        std::array<std::uint32_t, 16> results{};
        std::vector<std::thread> threads;
        for (std::size_t i = 0; i < results.size(); ++i) threads.emplace_back([&, i] {
            while (!start.load(std::memory_order_acquire)) YieldProcessor();
            results[i] = entry(input, bytes);
        });
        start.store(true, std::memory_order_release);
        for (auto& thread : threads) thread.join();
        unsigned winner{}, refused{};
        for (auto r : results) { winner += r == static_cast<std::uint32_t>(expected); refused += r == static_cast<std::uint32_t>(p::Result::reused); }
        check(winner == 1 && refused == 15, "one attempt wins concurrent admission; no retry after failure");
    } else {
        SetLastError(0xEA551234);
        check(entry(input, bytes) == static_cast<std::uint32_t>(expected), "exact refusal result");
        check(GetLastError() == 0xEA551234, "entry preserves LastError");
    }
    check(zero(page), "refused prepare never publishes binding");
    check(offer && std::memcmp(offer, &empty_offer, sizeof(empty_offer)) == 0, "refused prepare never publishes external offer");
    // A reused call must not even inspect a deliberately inaccessible request.
    check(entry(reinterpret_cast<const p::Request*>(1), sizeof(request)) == static_cast<std::uint32_t>(p::Result::reused),
        "repeat refuses before touching invalid caller memory");
    if (allocated) {
        check(protection(static_cast<std::byte*>(allocated)+4096, PAGE_READWRITE | PAGE_GUARD), "request guard remains unconsumed");
        VirtualFree(allocated, 0, MEM_RELEASE);
    }
    check(FreeLibrary(module) != 0, "release ordinary module reference");
    check((GetModuleHandleW(path) != nullptr) == retained, "late refusal retains self PIN; early refusal does not");
    if (retained) {
        check(zero(page), "pinned late failure still has zero page after FreeLibrary");
        check(entry(nullptr, 0) == static_cast<std::uint32_t>(p::Result::reused), "pinned failed instance remains terminal");
    }
}
void retail_case(const wchar_t* path) {
    const auto file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) { check(false, "open retail file (never execute)"); return; }
    LARGE_INTEGER length{}; GetFileSizeEx(file, &length);
    check(length.QuadPart == p::kRetailFileSize, "pinned retail file size");
    const auto mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    const auto* data = static_cast<const std::byte*>(mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr);
    if (!data) { check(false, "read-only raw file map"); CloseHandle(file); return; }
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    std::array<unsigned char, 32> digest{};
    const bool hashed = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0 &&
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data)), static_cast<ULONG>(length.QuadPart), 0) >= 0 &&
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    char hex[65]{};
    for (std::size_t i = 0; i < digest.size(); ++i) sprintf_s(hex+2*i, 3, "%02x", digest[i]);
    const bool identity = hashed && std::strcmp(hex, p::kRetailSha256) == 0;
    check(identity, "exact SHA256 before parsing pinned retail bytes");
    if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (identity) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(data+dos->e_lfanew);
        check(nt->FileHeader.TimeDateStamp == p::kRetailStamp && nt->OptionalHeader.SizeOfImage == p::kRetailImageSize,
            "retail PE profile matches local binding gate");
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        auto read = [&](std::uint32_t rva, void* out, std::size_t bytes) noexcept {
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
                const auto& s = sections[i];
                if (rva >= s.VirtualAddress && rva-s.VirtualAddress < s.SizeOfRawData &&
                    bytes <= s.SizeOfRawData-(rva-s.VirtualAddress) && (s.Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
                    const auto offset = std::uint64_t{s.PointerToRawData}+rva-s.VirtualAddress;
                    if (offset+bytes > static_cast<std::uint64_t>(length.QuadPart)) return false;
                    std::memcpy(out, data+offset, bytes); return true;
                }
            }
            return false;
        };
        check(p::code_contracts(read), "all six fallback prefixes and seven sites match actual retail");
        std::array<std::uint32_t, 13> rvas{};
        for (std::size_t i = 0; i < p::kFallbacks.size(); ++i) rvas[i] = p::kFallbacks[i].rva;
        for (std::size_t i = 0; i < n::kSites.size(); ++i) rvas[i+6] = n::kSites[i].rva;
        for (auto corrupt : rvas) {
            auto changed = [&](std::uint32_t rva, void* out, std::size_t bytes) noexcept {
                if (!read(rva, out, bytes)) return false;
                if (rva == corrupt) static_cast<std::uint8_t*>(out)[0] ^= 1;
                return true;
            };
            check(!p::code_contracts(changed), "independent wrong fallback/site bytes refuse");
            auto missing = [&](std::uint32_t rva, void* out, std::size_t bytes) noexcept { return rva != corrupt && read(rva, out, bytes); };
            check(!p::code_contracts(missing), "independent unreadable fallback/site refuses");
        }
        check(p::kFallbacks[3].rva == n::kGamePostposeRva && p::kFallbacks[3].rva != n::kNdPostposeRva,
            "fallback is GAME postpose, not ND operator dispatcher");
    }
    UnmapViewOfFile(data); CloseHandle(mapping); CloseHandle(file);
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    if (!wcscmp(argv[1], L"retail")) retail_case(argv[2]);
    else if (!wcscmp(argv[1], L"dormant")) dormant_case();
    else dll_case(argv[1], argv[2]);
    std::printf("enemy prepare: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
