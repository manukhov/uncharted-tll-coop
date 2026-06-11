#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

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

struct ModuleInfo {
    DWORD process_id = 0;
    std::uintptr_t base = 0;
    std::size_t size = 0;
};

std::optional<ModuleInfo> find_tll_process() {
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

    DWORD process_id = 0;
    do {
        if (_wcsicmp(process.szExeFile, L"tll.exe") == 0) {
            process_id = process.th32ProcessID;
            break;
        }
    } while (Process32NextW(snapshot.value, &process));

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
            return ModuleInfo{
                process_id,
                reinterpret_cast<std::uintptr_t>(module.modBaseAddr),
                static_cast<std::size_t>(module.modBaseSize),
            };
        }
    } while (Module32NextW(modules.value, &module));

    return std::nullopt;
}

bool dump_region(
    HANDLE process,
    std::uintptr_t address,
    std::size_t size,
    const char* path
) {
    std::vector<std::byte> buffer(size);
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(
            process,
            reinterpret_cast<LPCVOID>(address),
            buffer.data(),
            buffer.size(),
            &bytes_read
        )) {
        std::cerr << "ReadProcessMemory failed at 0x" << std::hex << address
                  << " err=" << std::dec << GetLastError() << '\n';
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(buffer.data()), bytes_read);
    std::cout << path << " <- 0x" << std::hex << address << " (" << std::dec
              << bytes_read << " bytes)\n";
    return true;
}

}  // namespace

int main() {
    const auto module = find_tll_process();
    if (!module) {
        std::cerr << "tll.exe not found\n";
        return 1;
    }

    Handle process;
    process.value = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
        FALSE,
        module->process_id
    );
    if (!process.value) {
        std::cerr << "OpenProcess failed: " << GetLastError() << '\n';
        return 1;
    }

    std::cout << "base=0x" << std::hex << module->base << '\n';

    struct Target {
        std::uintptr_t rva;
        std::size_t size;
        const char* path;
    };

    constexpr std::array<Target, 13> kTargets{{
        {0x140BBFD70 - 0x140000000, 0x500, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140BBFD70.bin"},
        {0x140B44EC0 - 0x140000000, 0x180, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140B44EC0.bin"},
        {0x141685C30 - 0x140000000, 0x220, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_141685C30.bin"},
        {0x140B42310 - 0x140000000, 0x240, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140B42310.bin"},
        {0x141C8D740 - 0x140000000, 0x220, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_141C8D740.bin"},
        {0x140B347C0 - 0x140000000, 0x280, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140B347C0.bin"},
        {0x140A90550 - 0x140000000, 0x520, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140A90550.bin"},
        {0x140A9AAD0 - 0x140000000, 0x520, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140A9AAD0.bin"},
        {0x140B1E400 - 0x140000000, 0x520, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140B1E400.bin"},
        {0x140B1EC04 - 0x140000000, 0x240, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140B1EC04.bin"},
        {0x140B1F0B2 - 0x140000000, 0x240, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140B1F0B2.bin"},
        {0x140B21DE9 - 0x140000000, 0x240, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140B21DE9.bin"},
        {0x140B21F17 - 0x140000000, 0x240, "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\code_140B21F17.bin"},
    }};

    for (const auto& target : kTargets) {
        if (!dump_region(
                process.value,
                module->base + target.rva,
                target.size,
                target.path
            )) {
            return 1;
        }
    }

    return 0;
}
