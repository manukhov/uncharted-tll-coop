// Runtime probe: is NetGameManager (0x143DD70C0) populated in single-player
// campaign? External ReadProcessMemory reader (no injection). Resolves ASLR
// base via the loaded tll.exe module and reads the manager's 10 player slots
// (stride 0x43D0), printing candidate id/index/flag fields per slot plus a raw
// dump for offline analysis.
//
// Decision rule:
//   - any slot with a non-default occupancy field set  -> manager is LIVE in
//     campaign (clean ctxA second-player route)
//   - all slots empty/default                          -> manager is DORMANT in
//     campaign (need net-infra bringup or buddy-elevation)
//
// Run while actually in-game in a single-player campaign chapter.
#include <Windows.h>
#include <TlHelp32.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <vector>

namespace {

struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) CloseHandle(value);
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

std::optional<ModuleInfo> find_tll() {
    Handle snap;
    snap.value = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap.value == INVALID_HANDLE_VALUE) return std::nullopt;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap.value, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"tll.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap.value, &pe));
    }
    if (pid == 0) return std::nullopt;

    Handle mods;
    mods.value = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (mods.value == INVALID_HANDLE_VALUE) return std::nullopt;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(mods.value, &me)) {
        do {
            if (_wcsicmp(me.szModule, L"tll.exe") == 0) {
                return ModuleInfo{pid,
                                  reinterpret_cast<std::uintptr_t>(me.modBaseAddr),
                                  static_cast<std::size_t>(me.modBaseSize)};
            }
        } while (Module32NextW(mods.value, &me));
    }
    return std::nullopt;
}

template <typename T>
T at(const std::vector<std::byte>& buf, std::size_t off) {
    T v{};
    if (off + sizeof(T) <= buf.size()) std::memcpy(&v, buf.data() + off, sizeof(T));
    return v;
}

}  // namespace

int main() {
    const auto mod = find_tll();
    if (!mod) {
        std::cerr << "tll.exe not found (is the game running?)\n";
        return 1;
    }
    Handle proc;
    proc.value = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                             mod->process_id);
    if (!proc.value) {
        std::cerr << "OpenProcess failed: " << GetLastError() << '\n';
        return 1;
    }

    constexpr std::uintptr_t kPreferredBase = 0x140000000;
    constexpr std::uintptr_t kManagerVA = 0x143DD70C0;
    constexpr std::size_t kSlotStride = 0x43D0;
    constexpr int kSlots = 10;
    constexpr std::size_t kRegion = kSlotStride * kSlots + 0x600;

    const std::uintptr_t manager = mod->base + (kManagerVA - kPreferredBase);
    std::cout << "base=0x" << std::hex << mod->base << "  manager=0x" << manager
              << std::dec << "\n";

    std::vector<std::byte> buf(kRegion);
    SIZE_T got = 0;
    if (!ReadProcessMemory(proc.value, reinterpret_cast<LPCVOID>(manager),
                           buf.data(), buf.size(), &got)) {
        std::cerr << "ReadProcessMemory failed at manager: " << GetLastError()
                  << '\n';
        return 1;
    }
    buf.resize(got);

    // Raw dump for offline analysis.
    {
        std::ofstream out(
            "C:\\Users\\vladimir\\Desktop\\TLLCoopResearch\\reports\\netmgr_region.bin",
            std::ios::binary);
        out.write(reinterpret_cast<const char*>(buf.data()),
                  static_cast<std::streamsize>(buf.size()));
    }
    std::cout << "raw -> reports\\netmgr_region.bin (" << buf.size() << " bytes)\n\n";

    // Candidate per-slot fields discovered statically.
    struct F { std::size_t off; const char* name; };
    constexpr std::array<F, 9> fields{{
        {0x1DC, "id@1DC"},   {0x1E0, "idx@1E0"}, {0x1E8, "local@1E8"},
        {0x59C, "id@59C"},   {0x5A0, "idx@5A0"}, {0x3C8, "obj@3C8"},
        {0x3D0, "id@3D0"},   {0x220, "x220"},    {0x455, "b@455"},
    }};

    int occupied = 0;
    for (int i = 0; i < kSlots; ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * kSlotStride;
        std::cout << "slot[" << i << "] @+0x" << std::hex << base << std::dec << "  ";
        bool any = false;
        for (const auto& f : fields) {
            if (f.name[0] == 'o') {  // obj/x220 are 64-bit
                auto v = at<std::uint64_t>(buf, base + f.off);
                std::cout << f.name << "=0x" << std::hex << v << std::dec << " ";
                if (v != 0 && v != 0xffffffffffffffffULL) any = true;
            } else {
                auto v = at<std::uint32_t>(buf, base + f.off);
                std::cout << f.name << "=0x" << std::hex << v << std::dec << " ";
                if (f.off == 0x1DC || f.off == 0x59C) {
                    if (v != 0 && v != 0xffffffffu) any = true;
                }
            }
        }
        std::cout << (any ? " <== OCCUPIED" : "") << "\n";
        if (any) ++occupied;
    }

    std::cout << "\nRESULT: " << occupied << "/" << kSlots
              << " slots look occupied -> NetGameManager is "
              << (occupied ? "LIVE in this session" : "DORMANT in this session")
              << "\n";
    return 0;
}
