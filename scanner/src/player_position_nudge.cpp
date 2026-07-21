#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

namespace {

constexpr std::uintptr_t kNadineBuddyVtableRva = 0x2949638;
constexpr std::uintptr_t kLocalPositionOffset = 0x130;

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

struct Vec4 {
    float x;
    float y;
    float z;
    float w;
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

std::optional<std::uintptr_t> parse_address(const wchar_t* text) {
    const wchar_t* digits = text;
    if (digits[0] == L'0' && (digits[1] == L'x' || digits[1] == L'X')) {
        digits += 2;
    }

    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long long parsed = std::wcstoull(digits, &end, 16);
    if (errno == ERANGE || end == digits || *end != L'\0' ||
        parsed > std::numeric_limits<std::uintptr_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uintptr_t>(parsed);
}

template <typename T>
bool read_value(HANDLE process, std::uintptr_t address, T& value) {
    SIZE_T bytes_read = 0;
    return ReadProcessMemory(
               process,
               reinterpret_cast<LPCVOID>(address),
               &value,
               sizeof(value),
               &bytes_read
           ) &&
           bytes_read == sizeof(value);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "usage: tll_player_nudge.exe <nadine-object-address>\n";
        return 1;
    }

    const auto object_address = parse_address(argv[1]);
    const auto game = find_tll_process();
    if (!object_address || !game) {
        std::cerr << "invalid address or tll.exe is not running\n";
        return 2;
    }

    Handle process;
    process.value = OpenProcess(
        PROCESS_QUERY_INFORMATION |
            PROCESS_VM_READ |
            PROCESS_VM_WRITE |
            PROCESS_VM_OPERATION,
        FALSE,
        game->id
    );
    if (process.value == nullptr) {
        std::cerr << "OpenProcess failed: " << GetLastError() << '\n';
        return 3;
    }

    std::uintptr_t vtable = 0;
    std::array<char, 13> name{};
    std::uint64_t parent_handle = 0;
    Vec4 position{};
    if (!read_value(process.value, *object_address, vtable) ||
        !read_value(process.value, *object_address + 8, name) ||
        !read_value(process.value, *object_address + 0x108, parent_handle) ||
        !read_value(
            process.value,
            *object_address + kLocalPositionOffset,
            position
        )) {
        std::cerr << "failed to read NadineBuddy object\n";
        return 4;
    }

    const auto expected_vtable =
        game->module_base + kNadineBuddyVtableRva;
    if (vtable != expected_vtable ||
        std::string_view(name.data()) != "nadine-buddy") {
        std::cerr << "address no longer points to NadineBuddy\n";
        return 5;
    }
    if (parent_handle != 0) {
        std::cerr << "NadineBuddy is parented; refusing to change transform\n";
        return 6;
    }

    const Vec4 original = position;
    position.x += 1.0F;

    SIZE_T bytes_written = 0;
    if (!WriteProcessMemory(
            process.value,
            reinterpret_cast<LPVOID>(
                *object_address + kLocalPositionOffset
            ),
            &position,
            sizeof(position),
            &bytes_written
        ) ||
        bytes_written != sizeof(position)) {
        std::cerr << "WriteProcessMemory failed: " << GetLastError() << '\n';
        return 7;
    }

    std::cout << "NadineBuddy 0x" << std::hex << *object_address << std::dec
              << " position: (" << original.x << ", " << original.y << ", "
              << original.z << ") -> (" << position.x << ", " << position.y
              << ", " << position.z << ")\n";
    return 0;
}
