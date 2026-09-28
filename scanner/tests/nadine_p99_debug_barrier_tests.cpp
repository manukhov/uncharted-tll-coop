// OWNED-PROCESS OS fixture only: no game, injection, patching, or external PID.
// A request event asks our child to create a thread. Holding a received debug
// event must stop both its counter and request servicing until continuation.
// This does NOT test a concurrent external CreateRemoteThread, installer
// rollback, native-job quiescence, or crash-atomic multi-site publication.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace {
constexpr DWORD kWaitMs = 4000, kChildWaitMs = 9000;
constexpr std::uint64_t kMagic = 0x5042393944424731ull;
constexpr wchar_t kPrefix[] = L"Local\\TLL-P99-DebugBarrier-";
unsigned checks = 0, failures = 0;
bool check(bool okay, const char* label) {
    ++checks;
    if (!okay) { ++failures; std::cerr << "FAIL " << label << " error=" << GetLastError() << '\n'; }
    return okay;
}

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE v = nullptr) noexcept : value(v) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& h) noexcept : value(std::exchange(h.value, nullptr)) {}
    Handle& operator=(Handle&& h) noexcept {
        if (this != &h) { reset(); value = std::exchange(h.value, nullptr); }
        return *this;
    }
    explicit operator bool() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
    void reset() noexcept {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = nullptr;
    }
};
struct Shared {
    std::uint64_t magic = 0;
    DWORD parent_pid = 0, child_pid = 0;
    std::uint64_t parent_creation = 0, child_creation = 0;
    alignas(8) volatile LONG64 counter = 0;
    alignas(8) volatile LONG64 spawned_counter = 0;
    volatile LONG request_seen = 0, spawned = 0;
};
static_assert(sizeof(Shared) <= 4096);
static_assert(alignof(Shared) >= 8);
struct View {
    Shared* value = nullptr;
    ~View() { if (value) UnmapViewOfFile(value); }
};
LONG64 read(volatile LONG64& value) noexcept { return InterlockedCompareExchange64(&value, 0, 0); }
LONG read(volatile LONG& value) noexcept { return InterlockedCompareExchange(&value, 0, 0); }
std::uint64_t creation(HANDLE process) noexcept {
    FILETIME start{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &start, &exit, &kernel, &user)) return 0;
    return (static_cast<std::uint64_t>(start.dwHighDateTime) << 32) | start.dwLowDateTime;
}
std::wstring image(HANDLE process) {
    std::array<wchar_t, 32768> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) return {};
    return {buffer.data(), size};
}
bool identity(HANDLE process, DWORD pid, std::uint64_t started, const std::wstring& expected) {
    return process && pid && pid != GetCurrentProcessId() && started && !expected.empty() &&
        GetProcessId(process) == pid && WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
        creation(process) == started && _wcsicmp(image(process).c_str(), expected.c_str()) == 0;
}
Handle fresh_event(const std::wstring& base, const wchar_t* suffix) {
    Handle result(CreateEventW(nullptr, TRUE, FALSE, (base + suffix).c_str()));
    if (!result || GetLastError() == ERROR_ALREADY_EXISTS) result.reset();
    return result;
}
Handle open_event(const std::wstring& base, const wchar_t* suffix) {
    return Handle(OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (base + suffix).c_str()));
}

struct Worker {
    Shared* shared;
    HANDLE stop, spawned_event;
    bool spawned;
};
DWORD WINAPI count(void* pointer) {
    const auto& worker = *static_cast<Worker*>(pointer);
    if (worker.spawned) {
        InterlockedIncrement(&worker.shared->spawned);
        if (!SetEvent(worker.spawned_event)) return 31;
    }
    const auto deadline = GetTickCount64() + kChildWaitMs;
    for (;;) {
        const DWORD state = WaitForSingleObject(worker.stop, 1);
        if (state == WAIT_OBJECT_0) return 0;
        if (state != WAIT_TIMEOUT || GetTickCount64() >= deadline) return 32;
        InterlockedIncrement64(worker.spawned ? &worker.shared->spawned_counter : &worker.shared->counter);
    }
}

int child(const std::wstring& base) {
    if (!base.starts_with(kPrefix) || base.size() > 180) return 20;
    Handle mapping(OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, base.c_str()));
    if (!mapping) return 21;
    View view{static_cast<Shared*>(MapViewOfFile(mapping.value, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 4096))};
    if (!view.value) return 22;
    Shared& shared = *view.value;
    MemoryBarrier();
    if (shared.magic != kMagic || shared.child_pid != GetCurrentProcessId() ||
        shared.child_creation != creation(GetCurrentProcess())) return 23;
    Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, shared.parent_pid));
    if (!identity(parent.value, shared.parent_pid, shared.parent_creation, image(GetCurrentProcess()))) return 24;
    auto ready = open_event(base, L"-ready"), request = open_event(base, L"-request");
    auto spawned = open_event(base, L"-spawned"), stop = open_event(base, L"-stop");
    if (!ready || !request || !spawned || !stop) return 25;
    Worker first{&shared, stop.value, spawned.value, false};
    Worker second{&shared, stop.value, spawned.value, true};
    Handle counter(CreateThread(nullptr, 0, count, &first, 0, nullptr));
    if (!counter) return 26;
    if (!SetEvent(ready.value)) {
        SetEvent(stop.value);
        if (WaitForSingleObject(counter.value, kWaitMs) != WAIT_OBJECT_0) ExitProcess(30);
        return 26;
    }
    const HANDLE waits[]{stop.value, request.value};
    const DWORD request_state = WaitForMultipleObjects(2, waits, FALSE, kChildWaitMs);
    Handle created;
    int result = 0;
    if (request_state == WAIT_OBJECT_0 + 1) {
        InterlockedIncrement(&shared.request_seen);
        created = Handle(CreateThread(nullptr, 0, count, &second, 0, nullptr));
        if (!created) result = 27;
        else if (WaitForSingleObject(stop.value, kChildWaitMs) != WAIT_OBJECT_0) result = 28;
    } else if (request_state != WAIT_OBJECT_0) result = 29;
    SetEvent(stop.value);
    // Do not return through stack-owned Worker records while a worker may still
    // use them. ExitProcess applies ONLY to this explicit owned fixture child.
    if (WaitForSingleObject(counter.value, kWaitMs) != WAIT_OBJECT_0) ExitProcess(30);
    if (created && WaitForSingleObject(created.value, kWaitMs) != WAIT_OBJECT_0) ExitProcess(30);
    DWORD worker_exit = STILL_ACTIVE;
    if (!GetExitCodeThread(counter.value, &worker_exit) || worker_exit != 0) result = 30;
    if (created && (!GetExitCodeThread(created.value, &worker_exit) || worker_exit != 0)) result = 30;
    return result;
}

struct OwnedChild {
    Handle job, process, initial_thread;
    DWORD pid = 0;
    std::uint64_t started = 0;
    HANDLE stop = nullptr; // Parent-owned event outlives this object.
    bool attached = false, held = false;
    DEBUG_EVENT event{};
    ~OwnedChild() {
        if (!process) return;
        if (stop) SetEvent(stop);
        // No target bytes are ever patched by this fixture, so releasing any
        // remaining event on a test failure cannot release a partial install.
        if (held) {
            const DWORD status = event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
                event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT
                ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
            if (ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)) held = false;
        }
        if (attached && DebugActiveProcessStop(pid)) attached = false;
        if (WaitForSingleObject(process.value, 1000) != WAIT_OBJECT_0) {
            // Only the handle returned by our own CreateProcessW can reach here.
            TerminateProcess(process.value, 90);
            if (attached && DebugActiveProcessStop(pid)) attached = false;
            WaitForSingleObject(process.value, 1000);
        }
        // Job owns ONLY our child. Closing it is a final bounded cleanup even
        // when debugging cleanup or TerminateProcess failed.
        job.reset();
    }
};
void close_file(const DEBUG_EVENT& event) noexcept {
    if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT && event.u.CreateProcessInfo.hFile)
        CloseHandle(event.u.CreateProcessInfo.hFile);
    if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile)
        CloseHandle(event.u.LoadDll.hFile);
}
bool receive(OwnedChild& target, DWORD timeout) {
    if (target.held || !WaitForDebugEvent(&target.event, timeout)) return false;
    target.held = true;
    close_file(target.event);
    return target.event.dwProcessId == target.pid;
}
bool proceed(OwnedChild& target, DWORD status = DBG_CONTINUE) {
    if (!target.held || target.event.dwProcessId != target.pid ||
        !ContinueDebugEvent(target.pid, target.event.dwThreadId, status)) return false;
    target.held = false;
    return true;
}
bool finish_attach(OwnedChild& target) {
    const auto deadline = GetTickCount64() + kWaitMs;
    while (GetTickCount64() < deadline) {
        if (!receive(target, 100)) {
            if (!target.held && GetLastError() == ERROR_SEM_TIMEOUT) continue;
            return false;
        }
        const auto type = target.event.dwDebugEventCode;
        if (type == EXCEPTION_DEBUG_EVENT) {
            // Our child has no application breakpoints. Only this controlled
            // fixture can classify the first first-chance breakpoint this way.
            if (!target.event.u.Exception.dwFirstChance ||
                target.event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT) return false;
            return proceed(target);
        }
        if (type != CREATE_THREAD_DEBUG_EVENT && type != LOAD_DLL_DEBUG_EVENT &&
            type != OUTPUT_DEBUG_STRING_EVENT && type != EXIT_THREAD_DEBUG_EVENT) return false;
        if (!proceed(target)) return false;
    }
    return false;
}
bool progress(volatile LONG64& counter, DWORD timeout) {
    const auto baseline = read(counter);
    const auto deadline = GetTickCount64() + timeout;
    do {
        if (read(counter) > baseline) return true;
        Sleep(1);
    } while (GetTickCount64() < deadline);
    return false;
}

void parent() {
    const auto executable = image(GetCurrentProcess());
    LARGE_INTEGER nonce{};
    if (!check(!executable.empty() && QueryPerformanceCounter(&nonce), "self identity and nonce")) return;
    const auto base = std::wstring(kPrefix) + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(nonce.QuadPart);
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, base.c_str()));
    const bool new_mapping = mapping && GetLastError() != ERROR_ALREADY_EXISTS;
    if (!check(new_mapping, "new private fixture mapping")) return;
    View view{static_cast<Shared*>(MapViewOfFile(mapping.value, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 4096))};
    if (!check(view.value != nullptr, "owned shared mapping")) return;
    ZeroMemory(view.value, 4096);
    auto ready = fresh_event(base, L"-ready"), request = fresh_event(base, L"-request");
    auto spawned = fresh_event(base, L"-spawned"), stop = fresh_event(base, L"-stop");
    if (!check(ready && request && spawned && stop, "new private fixture events")) return;
    OwnedChild target;
    target.stop = stop.value;
    target.job = Handle(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!check(target.job && SetInformationJobObject(target.job.value, JobObjectExtendedLimitInformation,
        &limit, sizeof(limit)), "owned kill-on-close job")) return;
    auto& shared = *view.value;
    shared.magic = kMagic;
    shared.parent_pid = GetCurrentProcessId();
    shared.parent_creation = creation(GetCurrentProcess());
    std::wstring command = L"\"" + executable + L"\" --fixture-child \"" + base + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child_info{};
    // Suspended launch is ONLY for race-free job assignment/identity publication;
    // it is not the debug-event barrier under test. The child runs before attach.
    if (!check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &child_info) != FALSE,
        "launch only our own child executable")) return;
    target.process = Handle(child_info.hProcess);
    target.initial_thread = Handle(child_info.hThread);
    target.pid = child_info.dwProcessId;
    target.started = creation(target.process.value);
    if (!check(AssignProcessToJobObject(target.job.value, target.process.value) != FALSE, "assign owned child job")) return;
    if (!check(identity(target.process.value, target.pid, target.started, executable), "PID creation and image identity")) return;
    check(!identity(target.process.value, target.pid + 1, target.started, executable), "wrong PID refuses before attach");
    check(!identity(target.process.value, target.pid, target.started ^ 1, executable), "wrong creation refuses before attach");
    check(!identity(target.process.value, target.pid, target.started, executable + L".wrong"), "wrong image refuses before attach");
    shared.child_pid = target.pid;
    shared.child_creation = target.started;
    MemoryBarrier();
    if (!check(ResumeThread(target.initial_thread.value) == 1, "run child before debugger attach")) return;
    if (!check(WaitForSingleObject(ready.value, kWaitMs) == WAIT_OBJECT_0, "child ready handshake")) return;
    if (!check(progress(shared.counter, kWaitMs), "counter really progresses before attach")) return;
    BOOL debugged = FALSE;
    if (!check(CheckRemoteDebuggerPresent(target.process.value, &debugged) && !debugged, "own child has no previous debugger")) return;
    if (!check(identity(target.process.value, target.pid, target.started, executable), "fresh identity before sole attach")) return;
    if (!check(DebugActiveProcess(target.pid) != FALSE, "attach only CreateProcess child PID")) return;
    target.attached = true;
    if (!check(DebugSetProcessKillOnExit(FALSE) != FALSE, "disable debugger kill-on-exit")) return;
    if (!check(receive(target, kWaitMs), "receive owned held event")) return;
    if (!check(target.event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT, "first attach event is synthetic process event")) return;
    check(target.event.u.CreateProcessInfo.lpStartAddress == nullptr, "attach event is not an early entrypoint claim");
    if (!check(identity(target.event.u.CreateProcessInfo.hProcess, target.pid, target.started, executable),
        "debug event handle matches owned creation and image")) return;
    const auto before = read(shared.counter);
    if (!check(SetEvent(request.value) != FALSE, "external request while event remains held")) return;
    check(WaitForSingleObject(spawned.value, 150) == WAIT_TIMEOUT, "requested child thread cannot enter while held");
    check(read(shared.counter) == before, "existing counter has zero progress during held event");
    check(read(shared.request_seen) == 0 && read(shared.spawned) == 0 && read(shared.spawned_counter) == 0,
        "child cannot service request or start new user thread while held");
    check(identity(target.process.value, target.pid, target.started, executable), "held target still same live child");
    if (!check(proceed(target), "continue held event only after observations")) return;
    if (!check(finish_attach(target), "drain attach events and continue attach breakpoint")) return;
    if (!check(DebugActiveProcessStop(target.pid) != FALSE, "checked detach from owned child")) return;
    target.attached = false;
    debugged = TRUE;
    check(CheckRemoteDebuggerPresent(target.process.value, &debugged) && !debugged, "detach is externally observable");
    if (!check(WaitForSingleObject(spawned.value, kWaitMs) == WAIT_OBJECT_0, "queued thread request runs after continue")) return;
    check(read(shared.request_seen) == 1 && read(shared.spawned) == 1, "exactly one requested thread started");
    check(progress(shared.counter, kWaitMs), "old counter resumes after continue and detach");
    check(progress(shared.spawned_counter, kWaitMs), "new user thread progresses after continue and detach");
    if (!check(SetEvent(stop.value) != FALSE, "normal child stop request")) return;
    if (!check(WaitForSingleObject(target.process.value, kWaitMs) == WAIT_OBJECT_0, "owned child exits normally")) return;
    DWORD exit_code = STILL_ACTIVE;
    check(GetExitCodeProcess(target.process.value, &exit_code) && exit_code == 0, "child success exit code");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--fixture-child") return child(argv[2]);
    if (argc != 1) {
        std::cerr << "No PID or external executable arguments are accepted.\n";
        return 2;
    }
    parent();
    std::cout << checks << " checks, " << failures << " failures; OWNED_PROCESS_DEBUG_EVENT_ONLY\n";
    return failures ? 1 : 0;
}
