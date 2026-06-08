#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

constexpr std::uintptr_t kDisableBuddyTeleportRva = 0x3D441AD;

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

struct ProcessInfo {
    DWORD id = 0;
    std::uintptr_t module_base = 0;
};

std::optional<ProcessInfo> find_tll_process() {
    Handle processes;
    processes.value = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes.value == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (!Process32FirstW(processes.value, &process)) {
        return std::nullopt;
    }

    DWORD process_id = 0;
    do {
        if (_wcsicmp(process.szExeFile, L"tll.exe") == 0) {
            process_id = process.th32ProcessID;
            break;
        }
    } while (Process32NextW(processes.value, &process));
    if (process_id == 0) {
        return std::nullopt;
    }

    Handle modules;
    modules.value = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        process_id
    );
    if (modules.value == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (!Module32FirstW(modules.value, &module)) {
        return std::nullopt;
    }
    do {
        if (_wcsicmp(module.szModule, L"tll.exe") == 0) {
            return ProcessInfo{
                process_id,
                reinterpret_cast<std::uintptr_t>(module.modBaseAddr),
            };
        }
    } while (Module32NextW(modules.value, &module));

    return std::nullopt;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 ||
        (std::wstring_view(argv[1]) != L"on" &&
         std::wstring_view(argv[1]) != L"off")) {
        std::cerr << "usage: tll_buddy_teleport.exe <on|off>\n";
        return 1;
    }

    const auto game = find_tll_process();
    if (!game) {
        std::cerr << "tll.exe is not running\n";
        return 2;
    }

    Handle process;
    process.value = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
        FALSE,
        game->id
    );
    if (process.value == nullptr) {
        std::cerr << "OpenProcess failed: " << GetLastError() << '\n';
        return 3;
    }

    // The engine flag is named "Disable Buddy Teleport".
    const std::uint8_t value =
        std::wstring_view(argv[1]) == L"off" ? 1 : 0;
    const auto address = game->module_base + kDisableBuddyTeleportRva;
    SIZE_T written = 0;
    if (!WriteProcessMemory(
            process.value,
            reinterpret_cast<void*>(address),
            &value,
            sizeof(value),
            &written
        ) ||
        written != sizeof(value)) {
        std::cerr << "WriteProcessMemory failed: " << GetLastError() << '\n';
        return 4;
    }

    std::uint8_t verified = 0xFF;
    SIZE_T read = 0;
    if (!ReadProcessMemory(
            process.value,
            reinterpret_cast<const void*>(address),
            &verified,
            sizeof(verified),
            &read
        ) ||
        read != sizeof(verified) ||
        verified != value) {
        std::cerr << "failed to verify buddy teleport flag\n";
        return 5;
    }

    std::cout << "automatic buddy teleport "
              << (value == 0 ? "enabled" : "disabled") << '\n';
    return 0;
}
