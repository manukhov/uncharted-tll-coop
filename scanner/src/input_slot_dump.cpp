#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uintptr_t kPreferredBase = 0x140000000ULL;
constexpr std::uintptr_t kRetainedSlotTableRva = 0x3EEBE80;
constexpr std::uintptr_t kPlayerVtableRva = 0x2A4BFE8;
constexpr std::uintptr_t kNadineBuddyVtableRva = 0x2949638;
constexpr std::uint32_t kChloeBaseHash = 0xB70B7EAF;
constexpr std::uint32_t kNadineBaseHash = 0x8A419C8E;

struct Handle {
    HANDLE v = nullptr;
    ~Handle() {
        if (v != nullptr && v != INVALID_HANDLE_VALUE) {
            CloseHandle(v);
        }
    }
};

struct ProcessInfo {
    DWORD pid = 0;
    std::uintptr_t base = 0;
};

std::optional<ProcessInfo> find_tll() {
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
                return ProcessInfo{pid, reinterpret_cast<std::uintptr_t>(me.modBaseAddr)};
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

template <typename T>
bool read_value(HANDLE process, std::uintptr_t address, T& out) {
    SIZE_T got = 0;
    return ReadProcessMemory(process,
                             reinterpret_cast<LPCVOID>(address),
                             &out,
                             sizeof(out),
                             &got) != 0 &&
           got == sizeof(out);
}

bool read_bytes(HANDLE process, std::uintptr_t address, void* out, std::size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(process,
                             reinterpret_cast<LPCVOID>(address),
                             out,
                             size,
                             &got) != 0 &&
           got == size;
}

std::string read_name(HANDLE process, std::uintptr_t object) {
    std::array<char, 64> buf{};
    if (!read_bytes(process, object + 8, buf.data(), buf.size() - 1)) {
        return {};
    }
    const auto end = std::find(buf.begin(), buf.end(), '\0');
    return std::string(buf.begin(), end);
}

std::uintptr_t read_qword(HANDLE process, std::uintptr_t address) {
    std::uintptr_t value = 0;
    read_value(process, address, value);
    return value;
}

std::uint32_t read_dword(HANDLE process, std::uintptr_t address) {
    std::uint32_t value = 0;
    read_value(process, address, value);
    return value;
}

bool write_dword(HANDLE process, std::uintptr_t address, std::uint32_t value) {
    SIZE_T written = 0;
    return WriteProcessMemory(process,
                              reinterpret_cast<LPVOID>(address),
                              &value,
                              sizeof(value),
                              &written) != 0 &&
           written == sizeof(value);
}

const char* known_hash_name(std::uint32_t value) {
    if (value == kChloeBaseHash) {
        return "chloe-base";
    }
    if (value == kNadineBaseHash) {
        return "nadine-base";
    }
    return nullptr;
}

bool parse_slot(const wchar_t* text, int& slot_out) {
    if (_wcsicmp(text, L"all") == 0) {
        slot_out = -1;
        return true;
    }
    if ((_wcsnicmp(text, L"slot", 4) == 0) && text[4] >= L'0' && text[4] <= L'5' && text[5] == L'\0') {
        slot_out = static_cast<int>(text[4] - L'0');
        return true;
    }
    if (text[0] >= L'0' && text[0] <= L'5' && text[1] == L'\0') {
        slot_out = static_cast<int>(text[0] - L'0');
        return true;
    }
    return false;
}

bool parse_hash(const wchar_t* text, std::uint32_t& value_out) {
    if (_wcsicmp(text, L"chloe") == 0 || _wcsicmp(text, L"chloe-base") == 0) {
        value_out = kChloeBaseHash;
        return true;
    }
    if (_wcsicmp(text, L"nadine") == 0 || _wcsicmp(text, L"nadine-base") == 0) {
        value_out = kNadineBaseHash;
        return true;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(text, &end, 16);
    if (end != text && *end == L'\0') {
        value_out = static_cast<std::uint32_t>(parsed);
        return true;
    }
    return false;
}

void print_qword_field(HANDLE process,
                       std::uintptr_t base,
                       std::uintptr_t module,
                       const char* name,
                       std::uintptr_t off) {
    const std::uintptr_t value = read_qword(process, base + off);
    std::printf(" %s=0x%llx", name, static_cast<unsigned long long>(value));
    if (value >= module && value < module + 0x7000000ULL) {
        std::printf("(rva=0x%llx)", static_cast<unsigned long long>(value - module));
    }
}

void print_dword_field(HANDLE process, std::uintptr_t base, const char* name, std::uintptr_t off) {
    const std::uint32_t value = read_dword(process, base + off);
    std::printf(" %s=0x%08x", name, value);
}

void dump_object(HANDLE process, std::uintptr_t module, const char* label, std::uintptr_t object) {
    std::printf("%s=0x%llx", label, static_cast<unsigned long long>(object));
    if (object == 0) {
        std::printf("\n");
        return;
    }

    const std::uintptr_t vtable = read_qword(process, object);
    const std::string name = read_name(process, object);
    std::printf(" vt=0x%llx", static_cast<unsigned long long>(vtable));
    if (vtable >= module) {
        std::printf("(rva=0x%llx)", static_cast<unsigned long long>(vtable - module));
    }
    if (!name.empty()) {
        std::printf(" name='%s'", name.c_str());
    }
    std::printf("\n");

    std::printf("  dword:");
    print_dword_field(process, object, "+108", 0x108);
    print_dword_field(process, object, "+10c", 0x10c);
    print_dword_field(process, object, "+164", 0x164);
    print_dword_field(process, object, "+170", 0x170);
    print_dword_field(process, object, "+174", 0x174);
    print_dword_field(process, object, "+178", 0x178);
    print_dword_field(process, object, "+17c", 0x17c);
    print_dword_field(process, object, "+180", 0x180);
    print_dword_field(process, object, "+190", 0x190);
    std::printf("\n");

    std::printf("  qword:");
    print_qword_field(process, object, module, "+030", 0x30);
    print_qword_field(process, object, module, "+0b0", 0xB0);
    print_qword_field(process, object, module, "+198", 0x198);
    print_qword_field(process, object, module, "+1a0", 0x1A0);
    print_qword_field(process, object, module, "+1a8", 0x1A8);
    print_qword_field(process, object, module, "+3828", 0x3828);
    std::printf("\n");

    const std::uintptr_t field30 = read_qword(process, object + 0x30);
    if (field30 != 0) {
        const std::uintptr_t vt30 = read_qword(process, field30);
        std::printf("  +030.obj vt=0x%llx", static_cast<unsigned long long>(vt30));
        if (vt30 >= module) {
            std::printf("(rva=0x%llx)", static_cast<unsigned long long>(vt30 - module));
        }
        std::printf("\n");
    }

    const std::uintptr_t snap = read_qword(process, object + 0xB0);
    if (snap != 0) {
        const std::uintptr_t snap_vt = read_qword(process, snap);
        const std::string snap_name = read_name(process, snap);
        const std::uintptr_t look_pair = read_qword(process, snap + 0x140);
        const std::uint32_t look_hash = read_dword(process, snap + 0x144);
        std::printf("  +0b0.obj=0x%llx vt=0x%llx",
                    static_cast<unsigned long long>(snap),
                    static_cast<unsigned long long>(snap_vt));
        if (snap_vt >= module) {
            std::printf("(rva=0x%llx)", static_cast<unsigned long long>(snap_vt - module));
        }
        if (!snap_name.empty()) {
            std::printf(" name='%s'", snap_name.c_str());
        }
        std::printf("\n");
        std::printf("    snapshot+140=0x%016llx snapshot+144=0x%08x",
                    static_cast<unsigned long long>(look_pair),
                    look_hash);
        if (const char* known = known_hash_name(look_hash)) {
            std::printf(" (%s)", known);
        }
        std::printf("\n");
    }
}

std::vector<std::uintptr_t> find_objects_by_vtable(HANDLE process,
                                                   std::uintptr_t wanted_vtable,
                                                   const char* wanted_name) {
    std::vector<std::uintptr_t> result;
    constexpr std::size_t kChunk = 8 * 1024 * 1024;
    std::vector<std::uint8_t> buffer(kChunk);

    // The live game objects observed so far live in this heap band. Scanning
    // the whole process is much slower because it includes large asset heaps.
    constexpr std::uintptr_t kScanStart = 0x1000000000ULL;
    constexpr std::uintptr_t kScanEnd = 0x1200000000ULL;
    std::uintptr_t address = kScanStart;
    MEMORY_BASIC_INFORMATION mbi{};
    while (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) ==
           sizeof(mbi)) {
        const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto size = static_cast<std::size_t>(mbi.RegionSize);
        if (base >= kScanEnd) {
            break;
        }
        if (mbi.State == MEM_COMMIT && readable_page(mbi.Protect)) {
            for (std::size_t off = 0; off < size; off += kChunk - 7) {
                const std::size_t want = std::min(kChunk, size - off);
                SIZE_T got = 0;
                if (!ReadProcessMemory(process,
                                       reinterpret_cast<LPCVOID>(base + off),
                                       buffer.data(),
                                       want,
                                       &got) ||
                    got < sizeof(std::uintptr_t)) {
                    continue;
                }
                for (std::size_t i = 0; i + sizeof(std::uintptr_t) <= got; i += 8) {
                    std::uintptr_t value = 0;
                    std::memcpy(&value, buffer.data() + i, sizeof(value));
                    if (value != wanted_vtable) {
                        continue;
                    }
                    const std::uintptr_t object = base + off + i;
                    if (wanted_name != nullptr && wanted_name[0] != '\0') {
                        const std::string name = read_name(process, object);
                        if (name != wanted_name) {
                            continue;
                        }
                    }
                    result.push_back(object);
                    if (result.size() >= 16) {
                        return result;
                    }
                }
            }
        }
        address = base + size;
        if (address <= base) {
            break;
        }
    }
    return result;
}

}  // namespace

void print_usage() {
    std::fprintf(stderr,
                 "usage: tll_input_slot_dump.exe [--set-look <slot0..slot5|all> <chloe|nadine|hex>]\n");
}

int wmain(int argc, wchar_t** argv) {
    bool set_look = false;
    int patch_slot = -2;
    std::uint32_t patch_hash = 0;
    if (argc != 1) {
        if (argc == 4 && std::wcscmp(argv[1], L"--set-look") == 0 &&
            parse_slot(argv[2], patch_slot) && parse_hash(argv[3], patch_hash)) {
            set_look = true;
        } else {
            print_usage();
            return 1;
        }
    }

    const auto info = find_tll();
    if (!info) {
        std::fprintf(stderr, "tll.exe not running\n");
        return 2;
    }

    Handle process;
    DWORD access = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
    if (set_look) {
        access |= PROCESS_VM_WRITE | PROCESS_VM_OPERATION;
    }
    process.v = OpenProcess(access, FALSE, info->pid);
    if (!process.v) {
        std::fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError());
        return 3;
    }

    std::printf("pid=%lu base=0x%llx\n",
                info->pid,
                static_cast<unsigned long long>(info->base));

    const std::uintptr_t table = info->base + kRetainedSlotTableRva;
    std::printf("retained_table=0x%llx\n", static_cast<unsigned long long>(table));
    for (int slot = 0; slot < 6; ++slot) {
        const std::uintptr_t entry = read_qword(process.v, table + static_cast<std::uintptr_t>(slot) * 0x10);
        const std::uint32_t id = read_dword(process.v, table + static_cast<std::uintptr_t>(slot) * 0x10 + 8);
        std::uintptr_t object = 0;
        if (entry != 0) {
            object = read_qword(process.v, entry + 8);
        }
        std::printf("slot%d entry=0x%llx id=0x%08x object=0x%llx\n",
                    slot,
                    static_cast<unsigned long long>(entry),
                    id,
                    static_cast<unsigned long long>(object));
        if (set_look && (patch_slot == -1 || patch_slot == slot)) {
            if (object == 0) {
                std::printf("  set-look skipped: empty slot\n");
            } else {
                const std::uintptr_t snap = read_qword(process.v, object + 0xB0);
                if (snap == 0) {
                    std::printf("  set-look skipped: no snapshot at player+0xb0\n");
                } else {
                    const std::uintptr_t addr = snap + 0x144;
                    const std::uint32_t before = read_dword(process.v, addr);
                    const bool ok = write_dword(process.v, addr, patch_hash);
                    const std::uint32_t after = read_dword(process.v, addr);
                    std::printf("  set-look snapshot+144 addr=0x%llx before=0x%08x after=0x%08x %s\n",
                                static_cast<unsigned long long>(addr),
                                before,
                                after,
                                ok && after == patch_hash ? "ok" : "FAILED");
                }
            }
        }
        char label[16]{};
        std::snprintf(label, sizeof(label), "  slot%d", slot);
        dump_object(process.v, info->base, label, object);
    }
    std::fflush(stdout);

    const auto nadines = find_objects_by_vtable(process.v,
                                                info->base + kNadineBuddyVtableRva,
                                                "nadine-buddy");
    for (std::size_t i = 0; i < nadines.size(); ++i) {
        char label[32]{};
        std::snprintf(label, sizeof(label), "nadine[%zu]", i);
        dump_object(process.v, info->base, label, nadines[i]);
    }

    return 0;
}
