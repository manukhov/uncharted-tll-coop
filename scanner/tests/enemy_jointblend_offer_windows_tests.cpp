// OWNED inert fixture only. Declared retail main extent/RVAs are metadata, NOT
// mapped retail code. Never calls TllEnemyPrepareV1 or any native game target.
#include "enemy_jointblend_offer_read_windows.h"
#include "enemy_jointblend_prepare_windows.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace o = coop::enemy_jointblend_offer;
namespace r = coop::enemy_jointblend_offer_read;
namespace w = coop::enemy_jointblend_prepare_windows;
namespace d = r::debug;
using A = o::Address;
namespace {
unsigned checks{}, failures{};
bool check(bool value, const char* why) {
    ++checks; if (!value) { ++failures; std::printf("FAIL %s error=%lu\n", why, GetLastError()); } return value;
}
void protect(A at, DWORD value) { DWORD old{}; if (!VirtualProtect(reinterpret_cast<void*>(at), 4096, value, &old)) ExitProcess(2); }
std::uint64_t creation(HANDLE process) {
    FILETIME c{}, e{}, k{}, u{}; if (!GetProcessTimes(process, &c, &e, &k, &u)) return 0;
    return (std::uint64_t{c.dwHighDateTime} << 32) | c.dwLowDateTime;
}
std::wstring image(HANDLE process) {
    std::array<wchar_t, 32768> path{}; DWORD count = static_cast<DWORD>(path.size());
    return QueryFullProcessImageNameW(process, 0, path.data(), &count) ? std::wstring(path.data(), count) : L"";
}
struct Fixture {
    HMODULE module{}; o::prep::Page* binding_page{}; o::Page* page{};
    w::Relays relays; w::Publisher binding_publisher; w::OfferPublisher publisher;
    o::Offer expected{};
    explicit Fixture(const wchar_t* path) {
        module = LoadLibraryW(path); if (!module) ExitProcess(2);
        const auto address = [&](const char* name) { return reinterpret_cast<A>(GetProcAddress(module, name)); };
        expected.self = reinterpret_cast<A>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(expected.self+dos->e_lfanew);
        expected.self_size = nt->OptionalHeader.SizeOfImage;
        expected.main = reinterpret_cast<A>(GetModuleHandleW(nullptr));
        expected.main_size = o::prep::kRetailImageSize; // declared arithmetic only
        // Reserve the otherwise unmapped suffix of this SMALL test executable's
        // declared main extent. It remains NOACCESS, not synthetic retail code;
        // real retail MEM_IMAGE naturally prevents relay allocations inside it.
        const auto* main_dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(expected.main);
        const auto* main_nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(expected.main+main_dos->e_lfanew);
        const A suffix = (static_cast<A>(main_nt->OptionalHeader.SizeOfImage)+0xFFFF) & ~A{0xFFFF};
        if (suffix >= expected.main_size || VirtualAlloc(reinterpret_cast<void*>(expected.main+suffix),
            expected.main_size-suffix, MEM_RESERVE, PAGE_NOACCESS) != reinterpret_cast<void*>(expected.main+suffix)) ExitProcess(6);
        expected.pid = GetCurrentProcessId(); expected.creation = creation(GetCurrentProcess());
        expected.nonce = {0x1122334455667788, expected.creation};
        LARGE_INTEGER frequency{}; if (!QueryPerformanceFrequency(&frequency)) ExitProcess(2);
        expected.frequency = frequency.QuadPart;
        expected.entry = address("EnemyFixtureEntry"); expected.postpose = address("EnemyFixturePostpose");
        expected.hooks = {address("EnemyFixtureTarget"), address("EnemyFixtureTarget"), address("EnemyFixtureReset"),
            address("EnemyFixtureProcess0"), address("EnemyFixtureProcess1"), address("EnemyFixturePrimary"), address("EnemyFixtureStore")};
        expected.page = address("EnemyFixtureOffer"); expected.binding_page = address("EnemyFixturePage");
        page = reinterpret_cast<o::Page*>(expected.page); binding_page = reinterpret_cast<o::prep::Page*>(expected.binding_page);
        for (std::size_t i = 0; i < expected.originals.size(); ++i) expected.originals[i] = expected.main+o::prep::kFallbacks[i].rva;
        if (!relays.prepare({expected.main, expected.main_size}, {expected.self, expected.self_size}, expected.hooks)) ExitProcess(3);
        expected.relays = relays.addresses();
        if (!check(o::valid(expected), "owned offer shape valid") ||
            !check(binding_publisher.publish(binding_page, expected.self, o::binding(expected)) == w::PublishResult::published,
                "owned legacy binding published")) ExitProcess(4);
    }
    void publish() { if (publisher.publish(page, expected) != w::PublishResult::published) ExitProcess(5); }
};
void refused(const o::Offer& expected, r::Error error, const char* why) {
    SetLastError(0x91827364);
    const auto result = r::read_candidate(GetCurrentProcess(), expected);
    check(result.error == error && !result.candidate && GetLastError() == 0x91827364, why);
}
void accepted(const Fixture& f) {
    SetLastError(0x12345678);
    const auto result = r::read_candidate(GetCurrentProcess(), f.expected);
    check(result.error == r::Error::none && result.candidate == f.expected && GetLastError() == 0x12345678,
        "actual RO image offer/binding, RX private relays and process identity read without mutation");
}
void mutate(A base, std::size_t offset, DWORD original, const o::Offer& expected, r::Error error) {
    protect(base, PAGE_READWRITE); auto* bytes = reinterpret_cast<std::uint8_t*>(base); bytes[offset] ^= 1;
    protect(base, original); refused(expected, error, "foreign byte change refuses with empty result");
    protect(base, PAGE_READWRITE); bytes[offset] ^= 1; protect(base, original);
}
void mutations(Fixture& f) {
    auto bad = f.expected; ++bad.pid; refused(bad, r::Error::process, "wrong real PID");
    bad = f.expected; ++bad.creation; refused(bad, r::Error::process, "stale process creation");
    bad = f.expected; ++bad.nonce[0]; refused(bad, r::Error::offer_bytes, "stale preparation nonce");
    bad = f.expected; ++bad.entry; refused(bad, r::Error::offer_bytes, "different expected entry");
    bad = f.expected; bad.self_size += 4096; refused(bad, r::Error::self_image, "declared extent differs from actual self PE");
    bad = f.expected; ++bad.originals[0]; refused(bad, r::Error::expectation, "invalid native address arithmetic");
    for (DWORD protection : {PAGE_READWRITE, PAGE_READONLY | PAGE_GUARD, PAGE_NOACCESS}) {
        protect(f.expected.page, protection); refused(f.expected, r::Error::offer_page, "offer protection mismatch");
        MEMORY_BASIC_INFORMATION m{}; VirtualQuery(f.page, &m, sizeof(m));
        check(m.Protect == protection, "reader never consumes guard or repairs protection"); protect(f.expected.page, PAGE_READONLY);
    }
    mutate(f.expected.page, 0, PAGE_READONLY, f.expected, r::Error::offer_bytes);
    mutate(f.expected.page, 4095, PAGE_READONLY, f.expected, r::Error::offer_bytes);
    mutate(f.expected.binding_page, 16, PAGE_READONLY, f.expected, r::Error::binding_page);
    mutate(f.expected.binding_page, 4095, PAGE_READONLY, f.expected, r::Error::binding_page);
    for (auto address : f.expected.relays) {
        mutate(address, 6, PAGE_EXECUTE_READ, f.expected, r::Error::relay);
        mutate(address, 4095, PAGE_EXECUTE_READ, f.expected, r::Error::relay);
        protect(address, PAGE_READWRITE); refused(f.expected, r::Error::relay, "writable relay refuses"); protect(address, PAGE_EXECUTE_READ);
    }
    // No target function executes while this test-only code page is nonexecuting.
    const auto code_page = f.expected.entry & ~A{4095}; protect(code_page, PAGE_READONLY);
    refused(f.expected, r::Error::code_page, "non-RX entry page refuses"); protect(code_page, PAGE_EXECUTE_READ);
    const auto path = image(GetCurrentProcess());
    d::WindowsOps ops(GetCurrentProcess(), GetCurrentProcessId(), f.expected.creation, f.expected.main, path);
    d::Session fresh;
    check(r::read_held(fresh, ops, f.expected).error == r::Error::hold, "unheld Session cannot read as held");
    check(!r::read_candidate(nullptr, f.expected).candidate, "invalid borrowed process refuses");
    accepted(f);
}
#if defined(TLL_ENEMY_OFFER_READ_AUDIT)
enum class Fault { short_true, false_full, query_short, owner, type, protection, copied_byte, changed_page };
Fault fault{}; unsigned ordinal{}, calls{}, hits{};
bool hit() { ++calls; if (calls != ordinal) return false; ++hits; return true; }
SIZE_T fault_query(HANDLE process, LPCVOID at, PMEMORY_BASIC_INFORMATION m, SIZE_T size) noexcept {
    auto result = VirtualQueryEx(process, at, m, size);
    if (fault >= Fault::query_short && fault <= Fault::protection && hit()) {
        if (fault == Fault::query_short) --result;
        if (fault == Fault::owner) m->AllocationBase = nullptr;
        if (fault == Fault::type) m->Type = MEM_MAPPED;
        if (fault == Fault::protection) m->Protect = PAGE_READWRITE;
    }
    return result;
}
BOOL fault_read(HANDLE process, LPCVOID at, LPVOID out, SIZE_T size, SIZE_T* count) noexcept {
    const auto okay = ReadProcessMemory(process, at, out, size, count);
    if ((fault <= Fault::false_full || fault >= Fault::copied_byte) && hit()) {
        if (fault == Fault::short_true) { if (*count) --*count; return TRUE; }
        if (fault == Fault::false_full) return FALSE;
        if (fault == Fault::copied_byte) static_cast<std::uint8_t*>(out)[0] ^= 1;
        if (fault == Fault::changed_page) {
            // Test-only current-process mutation after a successful copy.
            const auto address = reinterpret_cast<A>(at);
            protect(address, PAGE_READWRITE); reinterpret_cast<std::uint8_t*>(address)[4095] ^= 1; protect(address, PAGE_READONLY);
        }
    }
    return okay;
}
void read_faults(Fixture& f) {
    const r::Audit audit{fault_query, fault_read};
    // Full successful schedule: 12 RPMs; 24 before/after region queries + 9 code checks.
    for (const auto kind : {Fault::short_true, Fault::false_full, Fault::query_short, Fault::owner, Fault::type, Fault::protection, Fault::copied_byte}) {
        const unsigned total = kind >= Fault::query_short && kind <= Fault::protection ? 33 : 12;
        for (unsigned index = 1; index <= total; ++index) {
            fault = kind; ordinal = index; calls = hits = 0; r::g_audit = &audit;
            const auto result = r::read_candidate(GetCurrentProcess(), f.expected); r::g_audit = nullptr;
            check(hits == 1 && result.error != r::Error::none && !result.candidate, "every query/copy stage fails closed with no candidate");
        }
    }
    fault = Fault::changed_page; ordinal = 3; calls = hits = 0; r::g_audit = &audit;
    const auto result = r::read_candidate(GetCurrentProcess(), f.expected); r::g_audit = nullptr;
    check(hits == 1 && result.error == r::Error::changed && !result.candidate, "late offer mutation detected at final read");
    protect(f.expected.page, PAGE_READWRITE); reinterpret_cast<std::uint8_t*>(f.page)[4095] ^= 1; protect(f.expected.page, PAGE_READONLY);
    accepted(f);
}
std::wstring_view publish_fault; unsigned publication_reads{};
BOOL publication_protect(LPVOID at, SIZE_T size, DWORD value, PDWORD old) noexcept {
    if (publish_fault == L"seal-before") return FALSE;
    const auto okay = VirtualProtect(at, size, value, old);
    return publish_fault == L"seal-after" ? FALSE : okay;
}
bool publication_write(A at, const void* data, std::size_t bytes) noexcept {
    std::memcpy(reinterpret_cast<void*>(at), data, bytes/2); return false;
}
void publication_read(A at, std::size_t) noexcept {
    if (++publication_reads == 2) protect(at, PAGE_READWRITE);
}
void publication_faults(Fixture& f, std::wstring_view mode) {
    publish_fault = mode; w::Audit audit{};
    auto expected = w::PublishResult::seal;
    if (mode == L"partial") { audit.write = publication_write; expected = w::PublishResult::write; }
    else if (mode == L"readback") { audit.after_read = publication_read; expected = w::PublishResult::verify; }
    else audit.protect = publication_protect;
    w::g_audit = &audit;
    check(f.publisher.publish(f.page, f.expected) == expected, "offer publication reports exact failing stage");
    w::g_audit = nullptr;
    check(f.publisher.publish(f.page, f.expected) == w::PublishResult::reused, "failed offer publication cannot retry");
    check(w::published(f.page, f.expected) == (mode == L"seal-after"), "passive RO equality can survive failure, not prepare success");
    if (mode == L"seal-after") {
        accepted(f); // Deliberate counterexample: candidate success is NOT preparation success.
    } else check(!r::read_candidate(GetCurrentProcess(), f.expected).candidate, "partial/RW publication refuses read candidate");
    check(w::published(f.binding_page, f.expected.self, o::binding(f.expected)), "prior BindingV1 retained after failed OfferV1");
}
#endif
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Shared { DWORD parent{}, pid{}; std::uint64_t parent_creation{}, creation{}; o::Offer expected{}; };
struct View { Shared* data{}; ~View() { if (data) UnmapViewOfFile(data); } };
constexpr wchar_t kPrefix[] = L"Local\\TLL-Enemy-Offer-";
int child(const wchar_t* fixture, const std::wstring& base) {
    if (!base.starts_with(kPrefix) || base.size() > 180) return 20;
    Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, base.c_str()));
    View view{static_cast<Shared*>(MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, 4096))};
    if (!view.data) return 21;
    auto& s = *view.data;
    Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, s.parent));
    if (s.pid != GetCurrentProcessId() || s.creation != creation(GetCurrentProcess()) || !parent.value ||
        s.parent_creation != creation(parent.value) || image(parent.value) != image(GetCurrentProcess())) return 22;
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, (base+L"-ready").c_str()));
    Handle stop(OpenEventW(SYNCHRONIZE, FALSE, (base+L"-stop").c_str()));
    if (!ready.value || !stop.value) return 23;
    Fixture f(fixture);
    s.expected = f.expected; // Independent OWNED fixture setup record, not decoded from published page.
    f.publish(); if (!SetEvent(ready.value)) return 24;
    return WaitForSingleObject(stop.value, 20000) == WAIT_OBJECT_0 ? 0 : 25;
}
struct Child {
    Handle job, process, thread; DWORD pid{}; bool attached{}, held{}; DEBUG_EVENT event{};
    ~Child() {
        // Only this function's CREATED self-child, never a game or external PID.
        if (process.value && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.value, 90);
            if (held) ContinueDebugEvent(pid, event.dwThreadId, DBG_CONTINUE);
            if (attached) DebugActiveProcessStop(pid);
            WaitForSingleObject(process.value, 1000);
        }
    }
};
void child_test(const wchar_t* fixture) {
    const auto exe = image(GetCurrentProcess()); LARGE_INTEGER nonce{}; QueryPerformanceCounter(&nonce);
    const auto base = std::wstring(kPrefix)+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(nonce.QuadPart);
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, base.c_str()));
    if (!check(mapping.value && GetLastError() != ERROR_ALREADY_EXISTS, "new test-only mapping")) return;
    View view{static_cast<Shared*>(MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, 4096))};
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, (base+L"-ready").c_str()));
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, (base+L"-stop").c_str()));
    if (!check(view.data && ready.value && stop.value, "test-only handshake handles")) return;
    *view.data = {}; auto& s = *view.data; s.parent = GetCurrentProcessId(); s.parent_creation = creation(GetCurrentProcess());
    Child target; target.job.value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!check(target.job.value && SetInformationJobObject(target.job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "owned child job")) return;
    auto command = L"\""+exe+L"\" --fixture-child \""+fixture+L"\" \""+base+L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    if (!check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
        nullptr, nullptr, &startup, &process) != FALSE, "create hidden self-child")) return;
    target.process.value = process.hProcess; target.thread.value = process.hThread; target.pid = process.dwProcessId;
    if (!check(AssignProcessToJobObject(target.job.value, target.process.value) != FALSE, "job owns child")) return;
    s.pid = target.pid; s.creation = creation(target.process.value);
    if (!check(s.creation && ResumeThread(target.thread.value) == 1 && WaitForSingleObject(ready.value, 5000) == WAIT_OBJECT_0,
        "owned child publishes while running, before debugger attach")) return;
    const auto expected = s.expected;
    check(r::read_candidate(target.process.value, expected).candidate == expected, "actual cross-process read succeeds as candidate only");
    d::WindowsOps ops(target.process.value, target.pid, s.creation, expected.main, exe); d::Session session;
    check(r::read_held(session, ops, expected).error == r::Error::hold, "fresh real-child Session refuses");
    const bool acquired = session.acquire(ops); target.attached = session.state() != d::State::Refused;
    target.event = ops.event(); target.held = target.event.dwDebugEventCode != 0;
    if (!check(acquired && session.barrier_ready_for(ops), "actual identity-bound received debug event")) return;
    check(r::read_held(session, ops, expected).candidate == expected, "reader uses held real-child event");
    d::WindowsOps foreign(target.process.value, target.pid, s.creation, expected.main, exe);
    check(r::read_held(session, foreign, expected).error == r::Error::hold, "different Ops object cannot borrow hold");
    auto wrong = expected; wrong.main += 0x10000;
    check(r::read_held(session, ops, wrong).error == r::Error::hold, "wrong main rejected by held gate");
    r::Result wrong_thread;
    std::thread other([&] { wrong_thread = r::read_held(session, ops, expected); }); other.join();
    check(wrong_thread.error == r::Error::hold && !wrong_thread.candidate, "foreign thread cannot borrow hold");
    // Independent fixture fact: no writes/install/arms have occurred. Candidate
    // success is deliberately NOT used to derive this release decision.
    const bool released = session.release(ops, true);
    target.held = session.held(); target.attached = !session.acknowledged();
    if (!check(released, "explicit read-only fixture release")) return;
    check(r::read_held(session, ops, expected).error == r::Error::hold, "stale released session refuses");
    SetEvent(stop.value);
    check(WaitForSingleObject(target.process.value, 5000) == WAIT_OBJECT_0, "owned child exits cleanly");
    DWORD code{}; check(GetExitCodeProcess(target.process.value, &code) && code == 0, "owned child successful exit");
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring_view(argv[1]) == L"--fixture-child") return child(argv[2], argv[3]);
    if (argc != 3) return 2;
    const std::wstring_view mode(argv[1]);
    if (mode == L"child") child_test(argv[2]);
    else {
        Fixture f(argv[2]);
#if defined(TLL_ENEMY_OFFER_READ_AUDIT)
        if (mode == L"partial" || mode == L"seal-before" || mode == L"seal-after" || mode == L"readback") publication_faults(f, mode);
        else
#endif
        {
            if (mode == L"concurrent") {
                std::atomic<bool> go{}; std::array<w::PublishResult, 16> results{}; std::vector<std::thread> threads;
                for (std::size_t i = 0; i < results.size(); ++i) threads.emplace_back([&, i] {
                    while (!go.load(std::memory_order_acquire)) YieldProcessor(); results[i] = f.publisher.publish(f.page, f.expected);
                });
                go.store(true, std::memory_order_release); for (auto& t : threads) t.join();
                check(std::count(results.begin(), results.end(), w::PublishResult::published) == 1 &&
                    std::count(results.begin(), results.end(), w::PublishResult::reused) == 15, "one offer publication wins 16 concurrent attempts");
            } else f.publish();
            check(f.publisher.publish(reinterpret_cast<o::Page*>(1), {}) == w::PublishResult::reused, "one-shot gate precedes inaccessible repeat");
            accepted(f);
            if (mode == L"mutations") mutations(f);
#if defined(TLL_ENEMY_OFFER_READ_AUDIT)
            else if (mode == L"read-faults") read_faults(f);
#endif
            else check(mode == L"normal" || mode == L"concurrent", "known fixture mode");
        }
    }
    check(!r::kRuntimeAdmission && !o::kRuntimeAdmission, "candidate never authorizes native writes");
    std::printf("enemy offer Windows: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
