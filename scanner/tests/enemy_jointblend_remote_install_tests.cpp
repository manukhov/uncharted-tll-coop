// OWNED hidden self-child only. No game, external PID, executable or input API.
#include "enemy_jointblend_remote_install.h"
#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

namespace {
namespace ri = coop::enemy_jointblend_remote_install;
namespace nat = ri::native;
namespace dbg = ri::debug;
using A = nat::Address;
constexpr DWORD kWait = 5000;
constexpr wchar_t kPrefix[] = L"Local\\TLL-Enemy-Remote-Install-";
unsigned checks{}, failures{};
unsigned scenarios{};
bool check(bool value, const char* text) {
    ++checks; if (!value) { ++failures; std::cerr << "FAIL " << text << " error=" << GetLastError() << '\n'; }
    return value;
}
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void take(HANDLE h) { value = h; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
struct Shared {
    DWORD parent{}, child{};
    std::uint64_t parent_creation{}, child_creation{};
    A main{}, child_view{};
    ri::Prepared prepared{};
    volatile LONG counter{}, remote_calls{};
};
struct View {
    Shared* data{};
    ~View() { if (data) UnmapViewOfFile(data); }
};
std::uint64_t creation(HANDLE process) {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(process, &c, &e, &k, &u)) return 0;
    return (std::uint64_t{c.dwHighDateTime} << 32) | c.dwLowDateTime;
}
std::wstring image(HANDLE process) {
    std::array<wchar_t, 32768> path{}; DWORD n = static_cast<DWORD>(path.size());
    return QueryFullProcessImageNameW(process, 0, path.data(), &n) ? std::wstring(path.data(), n) : L"";
}
LONG read(volatile LONG& x) { return InterlockedCompareExchange(&x, 0, 0); }
DWORD WINAPI remote_probe(void* arg) {
    InterlockedIncrement(&static_cast<Shared*>(arg)->remote_calls);
    return 73;
}
__declspec(noinline) void hook() { MemoryBarrier(); }
bool prepare(ri::Prepared& p) {
    auto* memory = static_cast<std::byte*>(VirtualAlloc(nullptr, 0x7000000, MEM_RESERVE, PAGE_NOACCESS));
    if (!memory) return false;
    p.module = {reinterpret_cast<A>(memory), 0x7000000};
    auto page = [&](A rva) {
        return VirtualAlloc(memory + (rva & ~A{0xFFF}), 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE) != nullptr;
    };
    for (std::size_t i = 0; i < ri::kSites; ++i) {
        const auto& s = nat::kSites[i];
        if (!page(s.rva) || !page(s.target_rva) || !page(0x1000 * (i + 1))) return false;
        std::memcpy(memory + s.rva, s.stock.data(), s.length);
        p.hooks[i] = reinterpret_cast<A>(&hook);
        p.relays[i] = p.module.base + 0x1000 * (i + 1);
        std::array<std::uint8_t, 14> leaf{0xFF, 0x25};
        std::memcpy(leaf.data() + 6, &p.hooks[i], 8);
        std::memcpy(reinterpret_cast<void*>(p.relays[i]), leaf.data(), leaf.size());
    }
    if (!page(nat::kPostposeSlotRva) || !page(nat::kGamePostposeRva)) return false;
    p.postpose = reinterpret_cast<A>(&hook);
    const A callback = p.module.base + nat::kGamePostposeRva;
    std::memcpy(memory + nat::kPostposeSlotRva, &callback, 8);
    for (A cursor = 0; cursor < p.module.size;) {
        MEMORY_BASIC_INFORMATION m{};
        if (!VirtualQuery(memory + cursor, &m, sizeof(m)) || !m.RegionSize) return false;
        if (m.State == MEM_COMMIT) {
            DWORD previous{};
            if (!VirtualProtect(m.BaseAddress, m.RegionSize, PAGE_EXECUTE_READ, &previous)) return false;
        }
        cursor = reinterpret_cast<A>(m.BaseAddress) + m.RegionSize - p.module.base;
    }
    return FlushInstructionCache(GetCurrentProcess(), memory, p.module.size) != FALSE;
}
int child(const std::wstring& base) {
    if (!base.starts_with(kPrefix) || base.size() > 180) return 20;
    Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, base.c_str()));
    if (!mapping) return 21;
    View view{static_cast<Shared*>(MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, 4096))};
    if (!view.data) return 22;
    auto& shared = *view.data;
    Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, shared.parent));
    if (shared.child != GetCurrentProcessId() || shared.child_creation != creation(GetCurrentProcess()) ||
        !parent || shared.parent_creation != creation(parent.value) || image(parent.value) != image(GetCurrentProcess())) return 23;
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, (base + L"-ready").c_str()));
    Handle stop(OpenEventW(SYNCHRONIZE, FALSE, (base + L"-stop").c_str()));
    if (!ready || !stop || !prepare(shared.prepared)) return 24;
    shared.main = reinterpret_cast<A>(GetModuleHandleW(nullptr));
    shared.child_view = reinterpret_cast<A>(view.data);
    if (!SetEvent(ready.value)) return 25;
    const auto deadline = GetTickCount64() + 20000;
    while (WaitForSingleObject(stop.value, 1) == WAIT_TIMEOUT && GetTickCount64() < deadline)
        InterlockedIncrement(&shared.counter);
    return 0;
}
struct Child {
    Handle job, process, thread;
    DWORD pid{};
    bool attached{}, held{};
    DEBUG_EVENT event{};
    ~Child() {
        // Exclusive owned-fixture cleanup. Never used for a game/foreign handle.
        // Kill-on-close applies only to the child created below, even if a failed
        // assertion leaves synthetic code damaged or a debug event unresolved.
        if (process && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.value, 90);
            if (held) ContinueDebugEvent(pid, event.dwThreadId, DBG_CONTINUE);
            if (attached) DebugActiveProcessStop(pid);
            WaitForSingleObject(process.value, 1000);
        }
    }
};
enum class Scenario { normal, rip_in_word, foreign_word, foreign_callback, foreign_protection, foreign_writable, foreign_relay };
#if defined(TLL_ENEMY_REMOTE_INSTALL_AUDIT)
enum class Operation { write, protect, flush };
enum class Effect { before, after, partial, persistent };
struct Fault {
    Operation operation{};
    Effect effect{};
    unsigned ordinal{}, calls{}, hits{};
    bool hit(Operation op) noexcept {
        if (op != operation) return false;
        ++calls;
        if (calls == ordinal || (effect == Effect::persistent && calls > ordinal)) { ++hits; return true; }
        return false;
    }
};
Fault* active_fault{};
BOOL fault_write(HANDLE p, LPVOID a, LPCVOID b, SIZE_T n, SIZE_T* done) noexcept {
    if (!active_fault || !active_fault->hit(Operation::write)) return WriteProcessMemory(p,a,b,n,done);
    *done=0;
    if (active_fault->effect == Effect::after) WriteProcessMemory(p,a,b,n,done);
    // First site's rel32 occupies bytes10..13: writing12 bytes produces a
    // deliberately partial displacement, neither stock nor our full word.
    if (active_fault->effect == Effect::partial) WriteProcessMemory(p,a,b,12,done);
    SetLastError(ERROR_PARTIAL_COPY);return FALSE;
}
BOOL fault_protect(HANDLE p, LPVOID a, SIZE_T n, DWORD protection, PDWORD old) noexcept {
    if (!active_fault || !active_fault->hit(Operation::protect)) return VirtualProtectEx(p,a,n,protection,old);
    if (active_fault->effect == Effect::after) VirtualProtectEx(p,a,n,protection,old);
    SetLastError(ERROR_ACCESS_DENIED);return FALSE;
}
BOOL fault_flush(HANDLE p, LPCVOID a, SIZE_T n) noexcept {
    if (!active_fault || !active_fault->hit(Operation::flush)) return FlushInstructionCache(p,a,n);
    SetLastError(ERROR_ACCESS_DENIED);return FALSE;
}
const ri::Audit audit{fault_write,fault_protect,fault_flush};
struct FaultScope {
    explicit FaultScope(Fault* f) { active_fault=f;ri::g_audit=f ? &audit : nullptr; }
    ~FaultScope() { ri::g_audit=nullptr;active_fault=nullptr; }
};
#endif
void parent(Scenario scenario
#if defined(TLL_ENEMY_REMOTE_INSTALL_AUDIT)
    , Fault* fault=nullptr
#endif
) {
    ++scenarios;
    const auto failures_before=failures;
    const auto exe = image(GetCurrentProcess());
    LARGE_INTEGER nonce{};
    if (!check(!exe.empty() && QueryPerformanceCounter(&nonce), "self identity")) return;
    const auto base = std::wstring(kPrefix) + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(nonce.QuadPart);
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, base.c_str()));
    if (!check(mapping && GetLastError() != ERROR_ALREADY_EXISTS, "new fixture mapping")) return;
    View view{static_cast<Shared*>(MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, 4096))};
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, (base + L"-ready").c_str()));
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, (base + L"-stop").c_str()));
    if (!check(view.data && ready && stop, "fixture handles")) return;
    *view.data = {}; auto& shared = *view.data;
    shared.parent = GetCurrentProcessId(); shared.parent_creation = creation(GetCurrentProcess());
    Child target;
    target.job.take(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!check(target.job && SetInformationJobObject(target.job.value, JobObjectExtendedLimitInformation, &limit, sizeof(limit)), "owned child job")) return;
    auto command = L"\"" + exe + L"\" --fixture-child \"" + base + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    if (!check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
        nullptr, nullptr, &si, &pi) != FALSE, "hidden self-child only")) return;
    target.process.take(pi.hProcess); target.thread.take(pi.hThread); target.pid = pi.dwProcessId;
    if (!check(AssignProcessToJobObject(target.job.value, target.process.value) != FALSE, "assign owned child")) return;
    shared.child = target.pid; shared.child_creation = creation(target.process.value);
    if (!check(shared.child_creation && ResumeThread(target.thread.value) == 1 &&
        WaitForSingleObject(ready.value, kWait) == WAIT_OBJECT_0, "fixture prepared before attach")) return;
    dbg::WindowsOps ops(target.process.value, target.pid, shared.child_creation, shared.main, exe);
    dbg::Session session;
    const bool acquired = session.acquire(ops);
    target.attached = session.state() != dbg::State::Refused;
    target.event = ops.event(); target.held = target.event.dwDebugEventCode != 0;
    if (!check(acquired && session.barrier_ready_for(ops), "identity-bound received stop")) return;
    dbg::WindowsOps other(target.process.value, target.pid, shared.child_creation, shared.main, exe);
    ri::Installer rejected;
    check(!session.barrier_ready_for(other) && rejected.install(shared.prepared, session, other).error == ri::Error::foreign_hold,
        "same target but unowned Ops rejected");
    auto invalid=shared.prepared;invalid.relays[0]=invalid.hooks[0];
    ri::Installer bad_preparation;
    const auto refused=bad_preparation.install(invalid,session,ops);
    check(refused.status==ri::Status::refused && !refused.wrote && !refused.memory_state_verified,
        "unprepared relay refused before any publication");
    if (scenario==Scenario::rip_in_word) {
        // Only our own stopped child. The production transport has no context setter.
        CONTEXT saved{};saved.ContextFlags=CONTEXT_CONTROL;
        if (!check(GetThreadContext(target.thread.value,&saved)!=FALSE,"fixture saves owned child RIP")) return;
        auto moved=saved;
        moved.Rip=((shared.prepared.module.base+nat::kSites[0].rva)&~A{15})+15;
        if (!check(SetThreadContext(target.thread.value,&moved)!=FALSE,"fixture places RIP in unchanged patch-word neighbor")) return;
        ri::Installer rip_rejected;
        const auto result=rip_rejected.install(shared.prepared,session,ops);
        const bool context_restored=SetThreadContext(target.thread.value,&saved)!=FALSE;
        if (!check(result.status==ri::Status::refused && result.error==ri::Error::rip_in_word && !result.wrote &&
            context_restored,"whole16-byte RIP exclusion rejects before publication; owned context restored")) return;
    }
    const auto before = read(shared.counter);
    const auto probe = shared.main + reinterpret_cast<A>(&remote_probe) - reinterpret_cast<A>(GetModuleHandleW(nullptr));
    Handle remote(CreateRemoteThread(target.process.value, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(probe),
        reinterpret_cast<void*>(shared.child_view), 0, nullptr));
    if (!check(remote && WaitForSingleObject(remote.value, 50) == WAIT_TIMEOUT && !read(shared.remote_calls),
        "external-created thread cannot execute under held event")) return;
    ri::Installer installer;
    ri::Result installed{};
#if defined(TLL_ENEMY_REMOTE_INSTALL_AUDIT)
    { FaultScope scope(fault); installed=installer.install(shared.prepared,session,ops); }
    if (fault) {
        if (!check(fault->hits && installed.status != ri::Status::installed && installed.error != ri::Error::none,
            "injected failure never reported as successful install")) return;
        const bool unsafe=fault->effect == Effect::partial || fault->effect == Effect::persistent;
        if (!check(installed.status == (unsafe ? ri::Status::unsafe_held :
                (installed.wrote ? ri::Status::rolled_back : ri::Status::refused)) &&
            installed.memory_state_verified == !unsafe && installed.protections_restored && session.owned_hold_for(ops),
            "fault result matches verified stock or retained uncertain hold")) return;
        check(!session.release(ops,false) && session.owned_hold_for(ops) && !session.barrier_ready_for(ops),
            "failed release keeps exact hold available for explicit recovery");
        if (fault->effect == Effect::partial) {
            const A word=(shared.prepared.module.base+nat::kSites[0].rva)&~A{15};
            nat::Word16 before_word{},after_word{};SIZE_T n{};
            check(ReadProcessMemory(target.process.value,reinterpret_cast<void*>(word),before_word.data(),16,&n) && n==16,
                "read partial word under hold");
            const auto refusal=installer.restore(session,ops);
            check(refusal.status==ri::Status::unsafe_held && !refusal.memory_state_verified &&
                ReadProcessMemory(target.process.value,reinterpret_cast<void*>(word),after_word.data(),16,&n) && n==16 && before_word==after_word,
                "partial word is not guessed or overwritten during recovery");
            return; // Only this owned fixture may be terminated after partial corruption.
        }
    } else
#else
    installed=installer.install(shared.prepared,session,ops);
#endif
    if (!check(installed.status == ri::Status::installed && installed.wrote && installed.memory_state_verified &&
        installed.protections_restored && session.barrier_ready_for(ops), "seven words and callback installed under hold")) return;
    check(read(shared.counter) == before && !read(shared.remote_calls), "old and new threads stopped throughout publication");
    if (scenario != Scenario::normal && scenario != Scenario::rip_in_word) {
        A word = (shared.prepared.module.base + nat::kSites[0].rva) & ~A{15};
        if (scenario==Scenario::foreign_callback) word=shared.prepared.module.base+nat::kPostposeSlotRva;
        if (scenario==Scenario::foreign_relay) word=shared.prepared.relays[0];
        const std::uint8_t foreign = 0xCC; DWORD previous{}; SIZE_T n{};
        const bool protection_only=scenario==Scenario::foreign_protection || scenario==Scenario::foreign_writable;
        const DWORD foreign_protect=scenario==Scenario::foreign_writable ? PAGE_EXECUTE_READWRITE : PAGE_READONLY;
        if (protection_only)
            check(VirtualProtectEx(target.process.value,reinterpret_cast<void*>(word),16,foreign_protect,&previous)!=FALSE,
                "fixture foreign protection");
        else check(VirtualProtectEx(target.process.value, reinterpret_cast<void*>(word), 16, PAGE_EXECUTE_READWRITE, &previous) &&
            WriteProcessMemory(target.process.value, reinterpret_cast<void*>(word), &foreign, 1, &n) && n == 1 &&
            VirtualProtectEx(target.process.value, reinterpret_cast<void*>(word), 16, previous, &previous), "fixture foreign mutation");
        const auto refusal = installer.restore(session, ops);
        check(refusal.status == ri::Status::unsafe_held && !refusal.memory_state_verified && session.held(), "foreign bytes retain hold");
        std::uint8_t actual{};
        MEMORY_BASIC_INFORMATION memory{};
        if (protection_only)
            check(VirtualQueryEx(target.process.value,reinterpret_cast<void*>(word),&memory,sizeof(memory))==sizeof(memory) &&
                memory.Protect==foreign_protect,"foreign protection is never overwritten by recovery");
        else check(ReadProcessMemory(target.process.value, reinterpret_cast<void*>(word), &actual, 1, &n) && actual == foreign,
            "foreign byte never overwritten by recovery");
        return; // Only owned child cleanup terminates this intentionally damaged fixture.
    }
    const auto restored = installer.restore(session, ops);
    if (!check(restored.status == ri::Status::restored && restored.memory_state_verified && restored.protections_restored,
        "exact stock words and protection restored")) return;
    check(read(shared.counter) == before && !read(shared.remote_calls), "no thread ran between installation and rollback");
    // Fixture owns/pins all preparation for child's lifetime; this transport's
    // memory_state_verified alone is NOT sufficient for a production release.
    const bool released = session.release(ops, restored.memory_state_verified);
    target.held = false; target.attached = !released;
    if (!check(released && session.acknowledged(), "verified release and detach")) return;
    check(!session.barrier_ready_for(ops) && installer.restore(session, ops).error == ri::Error::foreign_hold,
        "detached session cannot write using stale stop");
    DWORD remote_result{};
    check(WaitForSingleObject(remote.value, kWait) == WAIT_OBJECT_0 && GetExitCodeThread(remote.value, &remote_result) &&
        remote_result == 73 && read(shared.remote_calls) == 1, "new external thread runs only after release");
    const auto deadline = GetTickCount64() + kWait;
    while (read(shared.counter) == before && GetTickCount64() < deadline) Sleep(1);
    check(read(shared.counter) > before, "existing thread resumes after detach");
    check(SetEvent(stop.value) && WaitForSingleObject(target.process.value, kWait) == WAIT_OBJECT_0, "normal child exit");
    DWORD exit{}; check(GetExitCodeProcess(target.process.value, &exit) && exit == 0, "child success");
    if (failures != failures_before) std::cerr << "scenario " << scenarios << " failed\n";
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--fixture-child") return child(argv[2]);
    if (argc != 1) return 2; // No foreign PID or executable options.
    parent(Scenario::normal); parent(Scenario::rip_in_word); parent(Scenario::foreign_word); parent(Scenario::foreign_callback);
    parent(Scenario::foreign_protection); parent(Scenario::foreign_writable); parent(Scenario::foreign_relay);
#if defined(TLL_ENEMY_REMOTE_INSTALL_AUDIT)
    for (const auto operation : {Operation::write,Operation::protect,Operation::flush}) {
        // Seven distinct patch pages: seven open plus seven restore attempts.
        const unsigned count=operation==Operation::write ? 8u : operation==Operation::protect ? 14u : 7u;
        for (unsigned ordinal=1;ordinal<=count;++ordinal) {
            for (const auto effect : {Effect::before,Effect::after}) {
                if (operation==Operation::flush && effect==Effect::after) continue;
                Fault fault{operation,effect,ordinal};parent(Scenario::normal,&fault);
            }
        }
    }
    Fault partial{Operation::write,Effect::partial,1};parent(Scenario::normal,&partial);
    Fault persistent{Operation::flush,Effect::persistent,1};parent(Scenario::normal,&persistent);
#endif
    std::cout << scenarios << " scenarios, " << checks << " checks, " << failures << " failures; OWNED_REMOTE_INSTALL_ONLY\n";
    return failures ? 1 : 0;
}
