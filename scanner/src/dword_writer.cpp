// Minimal runtime dword writer for tll.exe. Accepts either a rebased tll.exe VA
// (0x140...) or a raw process address, writes a 32-bit value, then verifies by
// reading it back.
//
// usage: tll_dword_writer.exe <addr-hex> <value-hex>
#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>

namespace {
struct Handle {
    HANDLE v = nullptr;
    ~Handle() { if (v && v != INVALID_HANDLE_VALUE) CloseHandle(v); }
};

std::optional<std::pair<DWORD, std::uintptr_t>> find_tll() {
    Handle snap; snap.v = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap.v == INVALID_HANDLE_VALUE) return std::nullopt;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap.v, &pe)) do {
        if (_wcsicmp(pe.szExeFile, L"tll.exe") == 0) { pid = pe.th32ProcessID; break; }
    } while (Process32NextW(snap.v, &pe));
    if (!pid) return std::nullopt;
    Handle ms; ms.v = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (ms.v == INVALID_HANDLE_VALUE) return std::nullopt;
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    if (Module32FirstW(ms.v, &me)) do {
        if (_wcsicmp(me.szModule, L"tll.exe") == 0)
            return std::make_pair(pid, reinterpret_cast<std::uintptr_t>(me.modBaseAddr));
    } while (Module32NextW(ms.v, &me));
    return std::nullopt;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        fwprintf(stderr, L"usage: tll_dword_writer.exe <addr-hex> <value-hex>\n");
        return 1;
    }
    const std::uintptr_t input = wcstoull(argv[1], nullptr, 16);
    const std::uint32_t value = static_cast<std::uint32_t>(wcstoul(argv[2], nullptr, 16));
    auto t = find_tll();
    if (!t) { fwprintf(stderr, L"tll.exe not running\n"); return 2; }
    Handle p;
    p.v = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                      FALSE, t->first);
    if (!p.v) { fwprintf(stderr, L"OpenProcess failed: %lu\n", GetLastError()); return 3; }
    const bool looks_like_module_va = input >= 0x140000000ULL && input < 0x150000000ULL;
    const std::uintptr_t addr = looks_like_module_va ? (t->second + (input - 0x140000000ULL)) : input;
    printf("base=0x%llx  input=0x%llx  -> write=0x%llx%s\n",
           (unsigned long long)t->second,
           (unsigned long long)input,
           (unsigned long long)addr,
           looks_like_module_va ? " (rebased VA)" : " (raw address)");
    SIZE_T written = 0;
    if (!WriteProcessMemory(p.v, reinterpret_cast<LPVOID>(addr), &value, sizeof(value), &written) || written != sizeof(value)) {
        fwprintf(stderr, L"WriteProcessMemory failed: %lu\n", GetLastError());
        return 4;
    }
    std::uint32_t verify = 0;
    SIZE_T read = 0;
    if (!ReadProcessMemory(p.v, reinterpret_cast<LPCVOID>(addr), &verify, sizeof(verify), &read) || read != sizeof(verify)) {
        fwprintf(stderr, L"verify ReadProcessMemory failed: %lu\n", GetLastError());
        return 5;
    }
    printf("wrote=0x%08x  verify=0x%08x\n", value, verify);
    return verify == value ? 0 : 6;
}
