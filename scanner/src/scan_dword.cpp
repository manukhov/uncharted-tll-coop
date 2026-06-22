#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace {

struct Handle {
    HANDLE v = nullptr;
    ~Handle() {
        if (v != nullptr && v != INVALID_HANDLE_VALUE) {
            CloseHandle(v);
        }
    }
};

std::optional<std::pair<DWORD, std::uintptr_t>> find_tll() {
    Handle snap;
    snap.v = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap.v == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap.v, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"tll.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap.v, &pe));
    }
    if (pid == 0) {
        return std::nullopt;
    }

    Handle mods;
    mods.v = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (mods.v == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(mods.v, &me)) {
        do {
            if (_wcsicmp(me.szModule, L"tll.exe") == 0) {
                return std::make_pair(pid, reinterpret_cast<std::uintptr_t>(me.modBaseAddr));
            }
        } while (Module32NextW(mods.v, &me));
    }
    return std::nullopt;
}

bool readable_page(DWORD protect) {
    if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0) {
        return false;
    }
    const DWORD p = protect & 0xff;
    return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
           p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE ||
           p == PAGE_EXECUTE_WRITECOPY;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: tll_scan_dword.exe [--range <start-hex> <end-hex>] <dword-hex> [dword-hex...]\n");
        return 1;
    }

    std::uintptr_t range_start = 0;
    std::uintptr_t range_end = UINTPTR_MAX;
    int first_needle = 1;
    if (argc >= 5 && std::wcscmp(argv[1], L"--range") == 0) {
        range_start = wcstoull(argv[2], nullptr, 16);
        range_end = wcstoull(argv[3], nullptr, 16);
        first_needle = 4;
        if (range_end <= range_start) {
            fwprintf(stderr, L"invalid range\n");
            return 1;
        }
    }

    std::vector<std::uint32_t> needles;
    needles.reserve(static_cast<std::size_t>(argc - first_needle));
    for (int i = first_needle; i < argc; ++i) {
        needles.push_back(static_cast<std::uint32_t>(wcstoul(argv[i], nullptr, 16)));
    }

    const auto tll = find_tll();
    if (!tll) {
        fwprintf(stderr, L"tll.exe not running\n");
        return 2;
    }

    Handle process;
    process.v = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, tll->first);
    if (!process.v) {
        fwprintf(stderr, L"OpenProcess failed: %lu\n", GetLastError());
        return 3;
    }

    printf("base=0x%llx\n", static_cast<unsigned long long>(tll->second));

    constexpr std::size_t kChunk = 8 * 1024 * 1024;
    std::uintptr_t address = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    std::vector<std::uint8_t> buffer(kChunk);
    std::vector<std::size_t> counts(needles.size(), 0);
    constexpr std::size_t kPrintLimitPerNeedle = 400;

    while (VirtualQueryEx(process.v, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) ==
           sizeof(mbi)) {
        const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto size = static_cast<std::size_t>(mbi.RegionSize);
        const auto region_end = base + size;
        if (region_end <= range_start) {
            address = region_end;
            if (address <= base) {
                break;
            }
            continue;
        }
        if (base >= range_end) {
            break;
        }
        if (mbi.State == MEM_COMMIT && readable_page(mbi.Protect)) {
            const auto scan_start = std::max(base, range_start);
            const auto scan_end = std::min(region_end, range_end);
            const std::size_t first_off = static_cast<std::size_t>(scan_start - base);
            const std::size_t scan_size = static_cast<std::size_t>(scan_end - scan_start);
            for (std::size_t rel = 0; rel < scan_size; rel += kChunk - 3) {
                const std::size_t off = first_off + rel;
                const std::size_t want = std::min(kChunk, scan_size - rel);
                SIZE_T got = 0;
                if (!ReadProcessMemory(process.v,
                                       reinterpret_cast<LPCVOID>(base + off),
                                       buffer.data(),
                                       want,
                                       &got) ||
                    got < sizeof(std::uint32_t)) {
                    continue;
                }
                for (std::size_t i = 0; i + sizeof(std::uint32_t) <= got; ++i) {
                    std::uint32_t value = 0;
                    std::memcpy(&value, buffer.data() + i, sizeof(value));
                    for (std::size_t n = 0; n < needles.size(); ++n) {
                        if (value != needles[n]) {
                            continue;
                        }
                        if (counts[n] < kPrintLimitPerNeedle) {
                            printf("hit value=0x%08x addr=0x%llx\n",
                                   value,
                                   static_cast<unsigned long long>(base + off + i));
                        }
                        ++counts[n];
                    }
                }
            }
        }
        address = base + size;
        if (address <= base) {
            break;
        }
    }

    for (std::size_t n = 0; n < needles.size(); ++n) {
        printf("count value=0x%08x total=%zu\n", needles[n], counts[n]);
    }
    return 0;
}
