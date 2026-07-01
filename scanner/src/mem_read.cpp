// Generic runtime memory reader for tll.exe. Resolves ASLR base and dumps N
// bytes either from a preferred-base VA inside tll.exe (0x140...) or from a
// raw process address / heap object handle (e.g. 0x10E0...).
//
// usage: tll_read.exe <addr-hex> <byte-count-hex>
//   tll_read.exe 0x14697AD50 0x40   # tll.exe VA -> rebased
//   tll_read.exe 0x10E0200160 0x40  # raw process address
#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace {
struct Handle {
    HANDLE v = nullptr;
    ~Handle() { if (v && v != INVALID_HANDLE_VALUE) CloseHandle(v); }
};

std::optional<std::pair<DWORD, std::uintptr_t>> find_tll(DWORD forced_pid) {
    Handle snap; snap.v = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap.v == INVALID_HANDLE_VALUE) return std::nullopt;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap.v, &pe)) do {
        if (_wcsicmp(pe.szExeFile, L"tll.exe") == 0 &&
            (forced_pid == 0 || forced_pid == pe.th32ProcessID)) {
            pid = pe.th32ProcessID;
            break;
        }
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
    int first = 1;
    DWORD forced_pid = 0;
    if (argc == 5 && std::wcscmp(argv[1], L"--pid") == 0) {
        forced_pid = static_cast<DWORD>(std::wcstoul(argv[2], nullptr, 10));
        first = 3;
    } else if (argc != 3) {
        fwprintf(stderr, L"usage: tll_read.exe [--pid N] <addr-hex> <count-hex>\n");
        return 1;
    }
    const std::uintptr_t input = wcstoull(argv[first], nullptr, 16);
    const std::size_t n = static_cast<std::size_t>(wcstoull(argv[first + 1], nullptr, 16));
    auto t = find_tll(forced_pid);
    if (!t) { fwprintf(stderr, L"tll.exe not running\n"); return 2; }
    Handle p; p.v = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, t->first);
    if (!p.v) { fwprintf(stderr, L"OpenProcess failed: %lu\n", GetLastError()); return 3; }
    const bool looks_like_module_va = input >= 0x140000000ULL && input < 0x150000000ULL;
    const std::uintptr_t addr = looks_like_module_va ? (t->second + (input - 0x140000000ULL)) : input;
    printf("base=0x%llx  input=0x%llx  -> read=0x%llx%s\n",
           (unsigned long long)t->second,
           (unsigned long long)input,
           (unsigned long long)addr,
           looks_like_module_va ? " (rebased VA)" : " (raw address)");
    std::vector<std::uint8_t> buf(n);
    SIZE_T got = 0;
    if (!ReadProcessMemory(p.v, reinterpret_cast<LPCVOID>(addr), buf.data(), n, &got)) {
        fwprintf(stderr, L"ReadProcessMemory failed: %lu\n", GetLastError()); return 4;
    }
    for (std::size_t i = 0; i + 8 <= got; i += 8) {
        std::uint64_t q; memcpy(&q, buf.data() + i, 8);
        printf("+0x%02zx: 0x%016llx\n", i, (unsigned long long)q);
    }
    return 0;
}
