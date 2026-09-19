#include <Windows.h>
#include <TlHelp32.h>
#include "dll_injection_completion.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <optional>

namespace {

struct Handle {
    HANDLE value = nullptr;

    ~Handle() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle() = default;
};

std::optional<DWORD> find_process_id() {
    Handle snapshot;
    snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot.value == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (!Process32FirstW(snapshot.value, &process)) {
        return std::nullopt;
    }

    do {
        if (_wcsicmp(process.szExeFile, L"tll.exe") == 0) {
            return process.th32ProcessID;
        }
    } while (Process32NextW(snapshot.value, &process));

    return std::nullopt;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    // Optional explicit target PID via "--pid <N>" (needed to inject into a
    // SPECIFIC instance when two tll.exe are running for single-PC co-op).
    std::optional<DWORD> forced_pid;
    std::filesystem::path dll_path;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--pid" && i + 1 < argc) {
            forced_pid = static_cast<DWORD>(_wtoi(argv[++i]));
        } else {
            dll_path = a;  // treat any other arg as the DLL path
        }
    }
    if (dll_path.empty()) {
        std::array<wchar_t, MAX_PATH> executable_path{};
        GetModuleFileNameW(
            nullptr,
            executable_path.data(),
            static_cast<DWORD>(executable_path.size())
        );
        dll_path = std::filesystem::path(executable_path.data()).parent_path() /
                   L"tll_coop_runtime.dll";
    }
    dll_path = std::filesystem::absolute(dll_path);

    if (!std::filesystem::exists(dll_path)) {
        std::wcerr << L"DLL not found: " << dll_path << L'\n';
        return 1;
    }

    const auto process_id = forced_pid ? forced_pid : find_process_id();
    if (!process_id) {
        std::cerr << "tll.exe is not running\n";
        return 2;
    }

    Handle process;
    process.value = OpenProcess(
        PROCESS_CREATE_THREAD |
            PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE |
            PROCESS_VM_READ,
        FALSE,
        *process_id
    );
    if (process.value == nullptr) {
        std::cerr << "OpenProcess failed: " << GetLastError() << '\n';
        return 3;
    }

    const auto bytes = (dll_path.native().size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(
        process.value,
        nullptr,
        bytes,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (remote_path == nullptr) {
        std::cerr << "VirtualAllocEx failed: " << GetLastError() << '\n';
        return 4;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(
            process.value,
            remote_path,
            dll_path.c_str(),
            bytes,
            &written
        ) ||
        written != bytes) {
        std::cerr << "WriteProcessMemory failed: " << GetLastError() << '\n';
        VirtualFreeEx(process.value, remote_path, 0, MEM_RELEASE);
        return 5;
    }

    const auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")
    );
    Handle thread;
    thread.value = CreateRemoteThread(
        process.value,
        nullptr,
        0,
        load_library,
        remote_path,
        0,
        nullptr
    );
    if (thread.value == nullptr) {
        std::cerr << "CreateRemoteThread failed: " << GetLastError() << '\n';
        VirtualFreeEx(process.value, remote_path, 0, MEM_RELEASE);
        return 6;
    }

    const DWORD wait_result = WaitForSingleObject(thread.value, 10000);
    const DWORD wait_error = wait_result == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    DWORD exit_query_error = ERROR_SUCCESS;
    const auto completion = coop::injection::finish_load_thread(wait_result,
        [&](DWORD& module) {
            if (GetExitCodeThread(thread.value, &module)) return true;
            exit_query_error = GetLastError();
            return false;
        },
        [&] { VirtualFreeEx(process.value, remote_path, 0, MEM_RELEASE); });
    using Completion = coop::injection::Completion;
    if (completion == Completion::kTimedOut || completion == Completion::kWaitFailed ||
        completion == Completion::kUnexpectedWait) {
        std::cerr << "INJECTION_INCONCLUSIVE wait=" << wait_result
                  << " error=" << wait_error
                  << "; remote DLL path retained because LoadLibrary may still use it. "
                     "Target process/thread left running; do not retry injection blindly.\n";
        return completion == Completion::kTimedOut ? 8 : 9;
    }
    if (completion == Completion::kExitQueryFailed) {
        std::cerr << "INJECTION_INCONCLUSIVE: thread completed but GetExitCodeThread failed: "
                  << exit_query_error << '\n';
        return 10;
    }
    if (completion == Completion::kStillActive) {
        std::cerr << "INJECTION_INCONCLUSIVE: completed thread reported STILL_ACTIVE\n";
        return 11;
    }
    if (completion == Completion::kLoadFailed) {
        std::cerr << "LoadLibraryW failed in tll.exe\n";
        return 7;
    }

    std::wcout << dll_path.filename().native() << L" loaded into PID "
               << *process_id << L'\n';
    return 0;
}
