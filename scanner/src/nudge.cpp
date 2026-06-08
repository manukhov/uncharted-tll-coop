#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <iostream>
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
    std::wstring_view value(text);
    if (value.starts_with(L"0x") || value.starts_with(L"0X")) {
        value.remove_prefix(2);
    }

    std::string narrow(value.begin(), value.end());
    std::uintptr_t address = 0;
    const auto result = std::from_chars(
        narrow.data(),
        narrow.data() + narrow.size(),
        address,
        16
    );
    if (result.ec != std::errc{} || result.ptr != narrow.data() + narrow.size()) {
        return std::nullopt;
    }
    return address;
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
        std::cerr << "usage: tll_nudge.exe <nadine-object-address>\n";
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
