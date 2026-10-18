#include "enemy_jointblend_prepare_windows.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <string_view>

namespace p = coop::enemy_jointblend_prepare;
namespace w = coop::enemy_jointblend_prepare_windows;
namespace n = coop::enemy_jointblend_native_contract;
using A = n::Address;
namespace {
unsigned checks{}, failures{};
void check(bool okay, const char* why) { ++checks; if (!okay) { ++failures; std::printf("FAIL %s\n", why); } }
void protect(void* at, DWORD value) { DWORD old{}; if (!VirtualProtect(at, 4096, value, &old)) ExitProcess(2); }
MEMORY_BASIC_INFORMATION query(A at) { MEMORY_BASIC_INFORMATION m{}; VirtualQuery(reinterpret_cast<void*>(at), &m, sizeof(m)); return m; }
using Target = std::uint64_t(__fastcall*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
bool native_fault(A address) {
    __try { (void)reinterpret_cast<Target>(address)(UINT64_MAX, 0, 0, 0, 0, 0); return false; }
    __except(GetExceptionCode() == 0xE1234567 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
}
bool try_store(p::Page* page) {
    __try { reinterpret_cast<volatile std::uint8_t*>(page)[0] ^= 1; return true; }
    __except(GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
}
struct Fixture {
    HMODULE module{};
    p::Page* page{};
    n::ModuleImage main{0x140000000, p::kRetailImageSize}, self{};
    std::array<A, 7> hooks{};
    p::Binding binding{};
    explicit Fixture(const wchar_t* path) {
        module = LoadLibraryW(path); if (!module) ExitProcess(2);
        self.base = reinterpret_cast<A>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(self.base+dos->e_lfanew);
        self.size = nt->OptionalHeader.SizeOfImage;
        if (main.base == self.base) main.base += 0x100000000;
        page = reinterpret_cast<p::Page*>(GetProcAddress(module, "EnemyFixturePage"));
        hooks.fill(reinterpret_cast<A>(GetProcAddress(module, "EnemyFixtureTarget")));
        if (!page || !hooks[0]) ExitProcess(2);
        bool isolated{};
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (self.base+sections[i].VirtualAddress == reinterpret_cast<A>(page)) isolated =
                sections[i].Misc.VirtualSize == 4096 && (sections[i].Characteristics & IMAGE_SCN_MEM_WRITE) &&
                !(sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE);
        check(isolated, "actual dedicated nonexecutable image publication page");
        FILETIME c{}, e{}, k{}, u{}; LARGE_INTEGER frequency{};
        if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) || !QueryPerformanceFrequency(&frequency)) ExitProcess(2);
        binding.magic = p::kMagic; binding.size = sizeof(binding); binding.version = p::kVersion;
        binding.pid = GetCurrentProcessId(); binding.creation = (std::uint64_t{c.dwHighDateTime} << 32) | c.dwLowDateTime;
        binding.main = main.base; binding.main_size = main.size; binding.self = self.base; binding.self_size = self.size;
        binding.frequency = frequency.QuadPart; binding.hooks = hooks; binding.postpose = hooks[0];
        for (std::size_t i = 0; i < binding.originals.size(); ++i) binding.originals[i] = main.base+p::kFallbacks[i].rva;
    }
};

#if defined(TLL_ENEMY_PREPARE_AUDIT)
enum class Fault { none, seal_before, seal_after, seal_old, write_seh, partial_write, tail, binding, readback, flush, exhaust, wrong_allocation };
Fault fault{};
unsigned nth{}, writes{}, protects{}, flushes{}, reads{}, allocations{}, successful_allocations{};
LPVOID fault_allocate(LPVOID at, SIZE_T size, DWORD type, DWORD protection) noexcept {
    ++allocations;
    if (fault == Fault::exhaust && successful_allocations >= nth-1) return nullptr;
    const auto allocated = VirtualAlloc(fault == Fault::wrong_allocation ? nullptr : at, size, type, protection);
    if (allocated) ++successful_allocations;
    return allocated;
}
void before_write(A at, std::size_t) noexcept {
    ++writes;
    if (writes == nth && fault == Fault::write_seh) protect(reinterpret_cast<void*>(at), PAGE_READONLY);
}
bool partial_write(A at, const void* data, std::size_t bytes) noexcept {
    const auto amount = writes == nth && fault == Fault::partial_write ? bytes/2 : bytes;
    std::memcpy(reinterpret_cast<void*>(at), data, amount);
    return amount == bytes;
}
BOOL fault_protect(LPVOID at, SIZE_T size, DWORD value, PDWORD old) noexcept {
    ++protects;
    if (protects == nth) {
        if (fault == Fault::seal_before) return FALSE;
        if (fault == Fault::seal_old) protect(at, PAGE_READONLY);
        if (fault == Fault::binding) reinterpret_cast<std::uint8_t*>(at)[8] ^= 1;
    }
    const BOOL result = VirtualProtect(at, size, value, old);
    return protects == nth && fault == Fault::seal_after ? FALSE : result;
}
BOOL fault_flush(HANDLE process, LPCVOID at, SIZE_T size) noexcept {
    ++flushes;
    const BOOL result = FlushInstructionCache(process, at, size);
    return flushes == nth && fault == Fault::flush ? FALSE : result;
}
void after_read(A at, std::size_t) noexcept {
    ++reads;
    if (fault == Fault::tail && reads == 1) reinterpret_cast<p::Page*>(at)->padding[17] = 1;
    if (fault == Fault::readback && reads == nth) protect(reinterpret_cast<void*>(at), PAGE_READWRITE);
}
w::Audit audit{};
void reset(Fault f, unsigned index = 1) {
    fault = f; nth = index; writes = protects = flushes = reads = allocations = successful_allocations = 0;
    audit = {fault_allocate, fault_protect, fault_flush, before_write, nullptr, after_read};
    if (f == Fault::partial_write) audit.write = partial_write;
    w::g_audit = &audit;
}
#endif

void publication(Fixture& f, std::wstring_view mode) {
    const p::Page zero{};
    check(std::memcmp(f.page, &zero, sizeof(zero)) == 0, "fixture page initially zero");
    check(!w::published(f.page, f.self.base, f.binding), "zero RW page is not a publication");
    HMODULE pinned{};
    check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(f.hooks[0]), &pinned) && pinned == f.module, "retain image before any publication");
    w::PublishResult expected = w::PublishResult::published;
    DWORD guarded_protection{};
    p::Page* target = f.page;
    void* private_page{};
    if (mode == L"invalid") { ++f.binding.originals[0]; expected = w::PublishResult::invalid; }
    else if (mode == L"private") { private_page = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        target = static_cast<p::Page*>(private_page); expected = w::PublishResult::page; }
    else if (mode == L"dirty") { f.page->padding[0] = 1; expected = w::PublishResult::page; }
    else if (mode == L"guard") {
        protect(f.page, PAGE_READWRITE | PAGE_GUARD);
        guarded_protection = query(reinterpret_cast<A>(f.page)).Protect;
        check((guarded_protection & PAGE_GUARD) != 0, "fixture actually has guard before publication");
        expected = w::PublishResult::page;
    }
    else if (mode == L"readonly") { protect(f.page, PAGE_READONLY); expected = w::PublishResult::page; }
#if defined(TLL_ENEMY_PREPARE_AUDIT)
    else if (mode == L"write-seh") { reset(Fault::write_seh); expected = w::PublishResult::write; }
    else if (mode == L"partial") { reset(Fault::partial_write); expected = w::PublishResult::write; }
    else if (mode == L"seal-before") { reset(Fault::seal_before); expected = w::PublishResult::seal; }
    else if (mode == L"seal-after") { reset(Fault::seal_after); expected = w::PublishResult::seal; }
    else if (mode == L"seal-old") { reset(Fault::seal_old); expected = w::PublishResult::seal; }
    else if (mode == L"tail") { reset(Fault::tail); expected = w::PublishResult::verify; }
    else if (mode == L"binding") { reset(Fault::binding); expected = w::PublishResult::verify; }
    else if (mode == L"readback") { reset(Fault::readback, 2); expected = w::PublishResult::verify; }
#endif
    else if (mode != L"normal" && mode != L"concurrent") { check(false, "known publication mode"); return; }
    w::Publisher publisher;
    SetLastError(0xA123B456);
    if (mode == L"concurrent") {
        std::atomic<bool> go{}; std::array<w::PublishResult, 16> results{}; std::vector<std::thread> threads;
        for (std::size_t i = 0; i < results.size(); ++i) threads.emplace_back([&, i] {
            while (!go.load(std::memory_order_acquire)) YieldProcessor();
            results[i] = publisher.publish(target, f.self.base, f.binding);
        });
        go.store(true, std::memory_order_release); for (auto& t : threads) t.join();
        unsigned accepted{}, refused{};
        for (auto result : results) { accepted += result == w::PublishResult::published; refused += result == w::PublishResult::reused; }
        check(accepted == 1 && refused == 15, "one real publication wins 16 racing calls");
    } else {
        check(publisher.publish(target, f.self.base, f.binding) == expected, "exact publication result/failure stage");
        check(GetLastError() == 0xA123B456, "publication preserves LastError");
    }
#if defined(TLL_ENEMY_PREPARE_AUDIT)
    w::g_audit = nullptr;
#endif
    check(publisher.publish(reinterpret_cast<p::Page*>(1), 0, {}) == w::PublishResult::reused, "terminal one-shot before inaccessible repeat");
    const bool full = w::published(f.page, f.self.base, f.binding);
    check(full == (expected == w::PublishResult::published || mode == L"seal-after" || mode == L"seal-old"),
        "RO equality can survive failed sealing; never promote it to full preparation success");
    if (full) check(!try_store(f.page) && w::published(f.page, f.self.base, f.binding), "sealed real image page rejects writes and stays intact");
    if (mode == L"write-seh") check(std::memcmp(f.page, &zero, sizeof(zero)) == 0, "write race contained without changing zero page");
    if (mode == L"partial") check(f.page->binding.magic == p::kMagic && f.page->binding.postpose == 0, "partial publication retained, not erased");
    if (mode == L"guard") check(query(reinterpret_cast<A>(f.page)).Protect == guarded_protection, "existing guard never consumed");
    if (mode == L"readback") check(query(reinterpret_cast<A>(f.page)).Protect == PAGE_READWRITE, "post-copy protection change refused, not repaired");
    if (mode == L"seal-before") check(f.page->binding.magic == p::kMagic && query(reinterpret_cast<A>(f.page)).Protect == PAGE_READWRITE,
        "false-before-effect preserves full exposed RW data and refuses retry");
    if (private_page) VirtualFree(private_page, 0, MEM_RELEASE); // Only our never-accepted test page.
    check(FreeLibrary(f.module) && GetModuleHandleW(nullptr) != nullptr, "release ordinary fixture reference after PIN");
    HMODULE retained{};
    check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(f.hooks[0]), &retained) && retained == f.module, "page/code image retained after ordinary reference released");
}
void retained(const std::array<A, 7>& addresses, unsigned count) {
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        if (i >= count) { check(addresses[i] == 0, "no allocation after first failure"); continue; }
        const auto m = query(addresses[i]);
        check(addresses[i] && m.State == MEM_COMMIT && m.Type == MEM_PRIVATE &&
            reinterpret_cast<A>(m.AllocationBase) == addresses[i] &&
            (m.Protect == PAGE_READWRITE || m.Protect == PAGE_EXECUTE_READ || m.Protect == PAGE_READONLY),
            "allocated page retained after owner destruction/failure, never RWX");
    }
}
void relays(Fixture& f) {
    std::array<A, 7> saved{};
    {
        w::Relays relays;
        SetLastError(0x87654321);
        check(relays.prepare(f.main, f.self, f.hooks), "prepare seven actual retained RX relays");
        check(GetLastError() == 0x87654321 && relays.verify(), "prepare preserves LastError and exact readback verifies");
        saved = relays.addresses();
        for (std::size_t i = 0; i < saved.size(); ++i) {
            check(n::encode_rel32(f.main.base+n::kSites[i].instruction_end_rva, saved[i]).valid, "site-specific rel32 range");
            for (std::size_t j = 0; j < i; ++j) check(saved[i] != saved[j], "independent relay reservation");
            check(reinterpret_cast<Target>(saved[i])(1, 2, 3, 4, 5, 6) == 183 && GetLastError() == 0xAABBCCDD,
                "owned RX leaf forwards all register/stack args and return/error state");
            check(native_fault(saved[i]), "native SEH unwinds through owned frameless tail-jump leaf");
        }
        check(!relays.prepare(f.main, f.self, f.hooks) && relays.verify(), "successful owner refuses second prepare, preserves existing relays");
        // Deliberate test-only foreign mutation; helper must not repair it.
        protect(reinterpret_cast<void*>(saved[0]), PAGE_READWRITE);
        check(!relays.verify(), "foreign writable relay refuses passive revalidation");
        reinterpret_cast<std::uint8_t*>(saved[0])[6] ^= 1;
        protect(reinterpret_cast<void*>(saved[0]), PAGE_EXECUTE_READ);
        check(!relays.verify(), "foreign target bytes refuse passive revalidation");
    }
    retained(saved, 7);
    check(FreeLibrary(f.module), "release relay target loader reference");
    HMODULE pinned{};
    check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(f.hooks[0]), &pinned) && pinned == f.module, "relay targets remain PINned after owner/reference release");
    w::Relays invalid;
    check(!invalid.prepare({UINT64_MAX-15, 4096}, f.self, f.hooks) && !invalid.verify(), "overflowing source image refuses without allocation");
    retained(invalid.addresses(), 0);
    w::Relays mismatch; auto bad = f.hooks; ++bad[1];
    check(!mismatch.prepare(f.main, f.self, bad), "both JointBlend sites must use same target");
    retained(mismatch.addresses(), 0);
}
#if defined(TLL_ENEMY_PREPARE_AUDIT)
void relay_faults(Fixture& f) {
    for (auto kind : {Fault::seal_before, Fault::seal_after, Fault::seal_old, Fault::write_seh,
        Fault::partial_write, Fault::binding, Fault::readback, Fault::flush}) for (unsigned index = 1; index <= 7; ++index) {
        reset(kind, index);
        std::array<A, 7> saved{};
        {
            w::Relays failed;
            check(!failed.prepare(f.main, f.self, f.hooks) && !failed.verify(), "each relay-stage failure refuses full preparation");
            const auto before = allocations;
            check(!failed.prepare(f.main, f.self, f.hooks) && allocations == before, "failure is terminal, no retry allocation");
            saved = failed.addresses();
        }
        retained(saved, index);
    }
    for (unsigned index = 1; index <= 7; ++index) {
        reset(Fault::exhaust, index);
        std::array<A, 7> saved{};
        {
            w::Relays exhausted;
            check(!exhausted.prepare(f.main, f.self, f.hooks) && allocations > 0 && allocations <= 65535*index &&
                successful_allocations == index-1, "near-address exhaustion at each partial stage is bounded");
            const auto attempts = allocations;
            check(!exhausted.prepare(f.main, f.self, f.hooks) && allocations == attempts, "partial exhaustion never retries");
            saved = exhausted.addresses();
        }
        retained(saved, index-1);
    }
    reset(Fault::wrong_allocation);
    w::Relays wrong;
    check(!wrong.prepare(f.main, f.self, f.hooks) && allocations == 1, "unexpected successful address is retained but not accepted");
    retained(wrong.addresses(), 1);
    w::g_audit = nullptr;
}
#endif
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    Fixture fixture(argv[2]);
    const std::wstring_view mode = argv[1];
    if (mode == L"relays") relays(fixture);
#if defined(TLL_ENEMY_PREPARE_AUDIT)
    else if (mode == L"relay-faults") relay_faults(fixture);
#endif
    else publication(fixture, mode);
    std::printf("enemy prepare Windows: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
