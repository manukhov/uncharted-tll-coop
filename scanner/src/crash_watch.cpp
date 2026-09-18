// Explicit PID-bound external debugger. Never installs hooks/VEH or changes
// exception handling (except the debugger's initial attach breakpoint).
#include <Windows.h>
#include <DbgHelp.h>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
namespace fs = std::filesystem;
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
std::atomic<bool> stopping{false};
BOOL WINAPI control(DWORD code) {
    if (code == CTRL_C_EVENT || code == CTRL_BREAK_EVENT) { stopping = true; return TRUE; }
    return FALSE;
}
struct Attached {
    DWORD pid = 0;
    ~Attached() { if (pid) DebugActiveProcessStop(pid); }
};
struct ExceptionNote {
    DWORD code = 0, thread = 0;
    std::uintptr_t address = 0;
    bool first = false;
};
struct Result {
    bool ready = false, exited = false, detached = false, dump = false;
    DWORD exit_code = 0, exception_code = 0;
    std::uint64_t first_chance = 0;
    int error = 0;
};
bool image_matches(HANDLE process, const fs::path& expected) {
    std::array<wchar_t, 32768> name{};
    DWORD size = static_cast<DWORD>(name.size());
    if (!QueryFullProcessImageNameW(process, 0, name.data(), &size)) return false;
    std::error_code error;
    const bool same = fs::equivalent(fs::path(name.data()), expected, error);
    return same && !error;
}
bool dump_exception(HANDLE process, DWORD pid, const DEBUG_EVENT& event,
                    const fs::path& path, std::ofstream& log) {
    Handle thread(OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, event.dwThreadId));
    CONTEXT context{};
    context.ContextFlags = CONTEXT_ALL;
    if (!thread || !GetThreadContext(thread.value, &context)) {
        log << "DUMP_CONTEXT_FAILED error=" << GetLastError() << '\n';
        return false;
    }
    EXCEPTION_RECORD record = event.u.Exception.ExceptionRecord;
    record.ExceptionRecord = nullptr; // Do not expose a target-space nested pointer as local.
    EXCEPTION_POINTERS pointers{&record, &context};
    MINIDUMP_EXCEPTION_INFORMATION exception{event.dwThreadId, &pointers, FALSE};
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) { log << "DUMP_CREATE_FAILED error=" << GetLastError() << '\n'; return false; }
    const auto flags = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory);
    const bool ok = MiniDumpWriteDump(process, pid, file.value, flags, &exception, nullptr, nullptr) != FALSE;
    const DWORD error = ok ? 0 : GetLastError();
    FlushFileBuffers(file.value);
    log << "SECOND_CHANCE_DUMP ok=" << ok << " error=" << error
        << " code=0x" << std::hex << record.ExceptionCode
        << " rip=0x" << context.Rip << " rsp=0x" << context.Rsp << std::dec << '\n';
    return ok;
}

Result watch(DWORD pid, const fs::path& expected, const fs::path& directory,
             DWORD seconds, HANDLE ready_event = nullptr) {
    Result result{};
    if (!pid || pid == GetCurrentProcessId() || seconds == 0 || seconds > 3600 || !expected.is_absolute()) {
        result.error = 2; return result;
    }
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!process || !image_matches(process.value, expected)) { result.error = 3; return result; }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process.value, &created, &exited, &kernel, &user)) { result.error = 3; return result; }
    // A new directory is mandatory: never mix sessions or overwrite a dump.
    std::error_code error;
    if (!directory.is_absolute() || !fs::create_directory(directory, error) || error) {
        result.error = 4; return result;
    }
    std::ofstream log(directory / L"events.txt");
    if (!log) { result.error = 4; return result; }
    log << "pid=" << pid << " creation=" << created.dwHighDateTime << ':' << created.dwLowDateTime
        << " timeout_seconds=" << seconds << "\nSECOND_CHANCE_ONLY; debugger changes timing; no perf verdict\n";
    log.flush();
    if (!DebugActiveProcess(pid)) { log << "ATTACH_FAILED error=" << GetLastError() << '\n'; result.error = 5; return result; }
    Attached attached{pid};
    if (!DebugSetProcessKillOnExit(FALSE)) {
        log << "KILL_ON_EXIT_POLICY_FAILED error=" << GetLastError() << '\n';
        result.error = 6;
        // Default debugger policy kills the target on debugger-thread exit.
        // A failed policy change AND failed detach must never return through
        // that unsafe default. This exceptional safety hold ignores timeout.
        log << "SAFETY_HOLD: do not terminate watcher until safe policy or detach succeeds\n";
        log.flush();
        for (;;) {
            if (DebugActiveProcessStop(pid)) { attached.pid = 0; result.detached = true; return result; }
            if (DebugSetProcessKillOnExit(FALSE)) return result; // RAII may now detach/exit safely.
            if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) { attached.pid = 0; return result; }
            Sleep(100);
        }
    }
    // Revalidate identity after attaching, before claiming READY.
    if (!image_matches(process.value, expected)) { result.error = 3; return result; }
    std::array<ExceptionNote, 32> recent{};
    std::size_t note_count = 0;
    bool attach_breakpoint = true;
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000;
    while (!stopping.load() && GetTickCount64() < deadline) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEventEx(&event, 100)) {
            if (GetLastError() == ERROR_SEM_TIMEOUT) continue;
            log << "WAIT_FAILED error=" << GetLastError() << '\n'; result.error = 7; break;
        }
        DWORD continuation = DBG_CONTINUE;
        bool ready_now = false;
        bool exit_now = false;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
        } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            const auto& info = event.u.Exception;
            if (attach_breakpoint && info.dwFirstChance && info.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {
                attach_breakpoint = false;
                ready_now = true; // Only the attach breakpoint is consumed.
            } else {
                continuation = DBG_EXCEPTION_NOT_HANDLED;
                recent[note_count++ % recent.size()] = {info.ExceptionRecord.ExceptionCode,
                    event.dwThreadId, reinterpret_cast<std::uintptr_t>(info.ExceptionRecord.ExceptionAddress),
                    info.dwFirstChance != 0};
                if (info.dwFirstChance) ++result.first_chance;
                else if (result.exception_code == 0) {
                    result.exception_code = info.ExceptionRecord.ExceptionCode;
                    result.dump = dump_exception(process.value, pid, event, directory / L"second-chance.dmp", log);
                    log.flush();
                }
            }
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            result.exited = true;
            result.exit_code = event.u.ExitProcess.dwExitCode;
            log << "EXIT code=0x" << std::hex << result.exit_code << std::dec
                << " second_chance=" << (result.exception_code != 0) << '\n';
            exit_now = true;
        }
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continuation)) {
            log << "CONTINUE_FAILED error=" << GetLastError() << '\n'; result.error = 8; break;
        }
        if (ready_now) {
            result.ready = true;
            std::ofstream ready(directory / L"ready.txt");
            ready << "pid=" << pid << " creation=" << created.dwHighDateTime << ':' << created.dwLowDateTime << '\n';
            ready.flush();
            if (!ready) { result.error = 4; break; }
            if (ready_event && !SetEvent(ready_event)) { result.error = 9; break; }
            log << "READY (attach breakpoint continued)\n"; log.flush();
        }
        if (exit_now) { attached.pid = 0; break; }
        if (!log) { result.error = 4; break; }
    }
    if (attached.pid) {
        result.detached = DebugActiveProcessStop(pid) != FALSE;
        if (result.detached) attached.pid = 0;
        else { log << "DETACH_FAILED error=" << GetLastError() << '\n'; result.error = 10; }
        log << "DETACHED=" << result.detached << " timeout_or_stop=" << (!result.error) << '\n';
    }
    log << "first_chance_count=" << result.first_chance << " last_events_not_crash_attribution:\n";
    const auto start = note_count > recent.size() ? note_count - recent.size() : 0;
    for (auto n = start; n < note_count; ++n) {
        const auto& note = recent[n % recent.size()];
        log << "code=0x" << std::hex << note.code << " pc=0x" << note.address << std::dec
            << " tid=" << note.thread << " first=" << note.first << '\n';
    }
    if (result.exited && result.exception_code == 0)
        log << "NO_SECOND_CHANCE_CONTEXT: exit code/last handled exception cannot establish crash PC\n";
    log.flush();
    return result;
}

void handled_fixture_exception() {
    __try { RaiseException(0xE0429901, 0, 0, nullptr); }
    __except (GetExceptionCode() == 0xE0429901 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
}
int fixture(const std::wstring& mode, const wchar_t* event_name) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, event_name);
    const auto started_name = std::wstring(event_name) + L"-started";
    HANDLE started = OpenEventW(EVENT_MODIFY_STATE, FALSE, started_name.c_str());
    if (!started || !SetEvent(started)) return 90;
    CloseHandle(started);
    if (!go || WaitForSingleObject(go, 15000) != WAIT_OBJECT_0) return 90;
    CloseHandle(go);
    if (mode == L"handled" || mode == L"handled-exit") {
        handled_fixture_exception();
        return mode == L"handled" ? 0 : 77;
    }
    if (mode == L"unhandled") RaiseException(0xE0429902, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    if (mode == L"breakpoint") RaiseException(EXCEPTION_BREAKPOINT, 0, 0, nullptr);
    if (mode == L"failfast") RaiseFailFastException(nullptr, nullptr, 0);
    if (mode == L"timeout") { Sleep(1500); return 0; }
    return 91;
}
bool valid_dump(const fs::path& path, DWORD code) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!file) return false;
    Handle mapping(CreateFileMappingW(file.value, nullptr, PAGE_READONLY, 0, 0, nullptr));
    if (!mapping) return false;
    void* base = MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, 0);
    if (!base) return false;
    PMINIDUMP_DIRECTORY entry = nullptr; void* stream = nullptr; ULONG bytes = 0;
    bool valid = MiniDumpReadDumpStream(base, ExceptionStream, &entry, &stream, &bytes) &&
        bytes >= sizeof(MINIDUMP_EXCEPTION_STREAM) &&
        static_cast<MINIDUMP_EXCEPTION_STREAM*>(stream)->ExceptionRecord.ExceptionCode == code;
    DWORD fault_thread = 0;
    if (valid) {
        const auto* exception = static_cast<MINIDUMP_EXCEPTION_STREAM*>(stream);
        LARGE_INTEGER size{};
        fault_thread = exception->ThreadId;
        const auto& stored = exception->ThreadContext;
        valid = fault_thread != 0 && GetFileSizeEx(file.value, &size) &&
            stored.DataSize >= sizeof(CONTEXT) &&
            static_cast<std::uint64_t>(stored.Rva) + stored.DataSize <= static_cast<std::uint64_t>(size.QuadPart);
        if (valid) {
            CONTEXT context{};
            std::memcpy(&context, static_cast<const std::byte*>(base) + stored.Rva, sizeof(context));
            valid = context.Rip != 0 && context.Rsp != 0;
        }
    }
    valid = valid && MiniDumpReadDumpStream(base, ModuleListStream, &entry, &stream, &bytes) &&
        bytes >= sizeof(ULONG32) && static_cast<MINIDUMP_MODULE_LIST*>(stream)->NumberOfModules != 0;
    valid = valid && MiniDumpReadDumpStream(base, ThreadListStream, &entry, &stream, &bytes) &&
        bytes >= sizeof(ULONG32) && static_cast<MINIDUMP_THREAD_LIST*>(stream)->NumberOfThreads != 0;
    if (valid) {
        const auto* list = static_cast<MINIDUMP_THREAD_LIST*>(stream);
        bool found = false;
        valid = static_cast<std::uint64_t>(list->NumberOfThreads) * sizeof(MINIDUMP_THREAD) + sizeof(ULONG32) <= bytes;
        if (valid) for (ULONG32 i = 0; i < list->NumberOfThreads; ++i) {
            if (list->Threads[i].ThreadId == fault_thread && list->Threads[i].Stack.Memory.DataSize != 0) found = true;
        }
        valid = valid && found;
    }
    UnmapViewOfFile(base); return valid;
}
int self_test(const fs::path& parent, const fs::path& executable) {
    std::error_code error;
    fs::create_directories(parent, error);
    if (error) return 2;
    const auto root = parent / (L"run-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!fs::create_directory(root)) return 2;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << label << '\n'; } };
    for (const std::wstring mode : {L"handled", L"handled-exit", L"unhandled", L"breakpoint", L"failfast", L"timeout"}) {
        const auto event_name = L"Local\\TllCrashWatchTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + mode;
        Handle go(CreateEventW(nullptr, TRUE, FALSE, event_name.c_str()));
        Handle started(CreateEventW(nullptr, TRUE, FALSE, (event_name + L"-started").c_str()));
        std::wstring command = L"\"" + executable.wstring() + L"\" --fixture " + mode + L" \"" + event_name + L"\"";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
        const bool launched = go && started && CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &startup, &child);
        check(launched, "launch fixture"); if (!launched) continue;
        Handle process(child.hProcess), thread(child.hThread);
        // Finish loader startup before attaching. This also makes the fixture's
        // breakpoint distinct from the debugger-generated attach breakpoint.
        check(WaitForSingleObject(started.value, 5000) == WAIT_OBJECT_0, "fixture reached ready gate");
        // PID-bound identity refusal must occur before any debugger attachment.
        check(watch(child.dwProcessId, executable.parent_path() / L"wrong.exe", root / (mode + L"-wrong"), 2).error == 3,
            "reject wrong image");
        check(watch(child.dwProcessId, executable, root, 2).error == 4, "refuse existing output directory");
        const auto session = root / mode;
        const auto result = watch(child.dwProcessId, executable, session, mode == L"timeout" ? 1 : 10, go.value);
        check(result.error == 0 && result.ready, "watch ready without errors");
        const bool fatal = mode == L"unhandled" || mode == L"breakpoint" || mode == L"failfast";
        if (fatal) {
            check(result.exited && result.exception_code != 0 && result.dump, "capture second chance");
            check(valid_dump(session / L"second-chance.dmp", result.exception_code), "dump exception/module/thread streams");
            if (mode == L"unhandled") check(result.exception_code == 0xE0429902, "exact exception code");
            if (mode == L"breakpoint") check(result.exception_code == EXCEPTION_BREAKPOINT, "do not swallow program breakpoint");
        } else {
            check(!result.dump && !fs::exists(session / L"second-chance.dmp"), "no handled-exception dump");
            if (mode == L"timeout") {
                check(result.detached && !result.exited, "timeout detaches without killing");
                check(WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT, "child is still alive after timeout detach");
            }
            else check(result.exited && result.exit_code == (mode == L"handled" ? 0u : 77u) && result.first_chance >= 1,
                "handler ran and exit code preserved");
        }
        check(WaitForSingleObject(process.value, 5000) == WAIT_OBJECT_0, "fixture exits after debugger detach/continue");
        DWORD child_exit = STILL_ACTIVE;
        check(GetExitCodeProcess(process.value, &child_exit) &&
            child_exit == (fatal ? result.exception_code : mode == L"handled-exit" ? 77u : 0u),
            "actual target exit status preserved, including timeout survivor");
        // Test-created child only; this path never applies to a user's target.
        if (WaitForSingleObject(process.value, 0) != WAIT_OBJECT_0) TerminateProcess(process.value, 92);
    }
    std::cout << checks << " checks, " << failures << " failures; artifacts=" << root.string() << '\n';
    return failures ? 1 : 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    std::array<wchar_t, 32768> buffer{};
    if (!GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()))) return 2;
    if (argc == 4 && std::wstring(argv[1]) == L"--fixture") return fixture(argv[2], argv[3]);
    if (argc == 3 && std::wstring(argv[1]) == L"--self-test") return self_test(fs::absolute(argv[2]), fs::path(buffer.data()));
    if (argc != 9 || std::wstring(argv[1]) != L"--pid" || std::wstring(argv[3]) != L"--expected-image" ||
        std::wstring(argv[5]) != L"--out" || std::wstring(argv[7]) != L"--seconds") {
        std::wcerr << L"Usage: tll_crash_watch --pid N --expected-image ABSOLUTE_EXE --out NEW_ABSOLUTE_DIR --seconds 1..3600\n";
        return 2;
    }
    wchar_t* end = nullptr;
    const auto pid = wcstoul(argv[2], &end, 10);
    if (!pid || !end || *end) return 2;
    const auto seconds = wcstoul(argv[8], &end, 10);
    if (!seconds || seconds > 3600 || !end || *end) return 2;
    SetConsoleCtrlHandler(control, TRUE);
    const auto result = watch(pid, fs::path(argv[4]), fs::path(argv[6]), seconds);
    std::cout << "ready=" << result.ready << " exited=" << result.exited << " dump=" << result.dump
        << " detached=" << result.detached << " error=" << result.error << '\n';
    return result.error ? result.error : (!result.ready || (result.exception_code && !result.dump) ? 11 : 0);
}
