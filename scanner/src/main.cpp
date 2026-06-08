#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr std::uintptr_t kNadineBuddyVtableRva = 0x2949638;
constexpr std::uintptr_t kPlayerVtableRva = 0x2A4BFE8;
constexpr std::size_t kChunkSize = 4 * 1024 * 1024;
constexpr std::size_t kObjectSnapshotSize = 0xF480;

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

bool is_readable(const MEMORY_BASIC_INFORMATION& memory) {
    if (memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) != 0) {
        return false;
    }

    constexpr DWORD readable =
        PAGE_READONLY |
        PAGE_READWRITE |
        PAGE_WRITECOPY |
        PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE |
        PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & readable) != 0;
}

std::string read_inline_name(HANDLE process, std::uintptr_t address) {
    std::array<char, 33> buffer{};
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(
            process,
            reinterpret_cast<LPCVOID>(address),
            buffer.data(),
            buffer.size() - 1,
            &bytes_read
        )) {
        return {};
    }

    buffer[std::min<std::size_t>(bytes_read, buffer.size() - 1)] = '\0';
    return std::string(buffer.data(), strnlen_s(buffer.data(), buffer.size()));
}

template <std::size_t Count>
std::array<std::uintptr_t, Count> read_pointers(
    HANDLE process,
    std::uintptr_t address
) {
    std::array<std::uintptr_t, Count> pointers{};
    SIZE_T bytes_read = 0;
    ReadProcessMemory(
        process,
        reinterpret_cast<LPCVOID>(address),
        pointers.data(),
        sizeof(pointers),
        &bytes_read
    );
    return pointers;
}

std::vector<std::uintptr_t> scan_for_pointer(
    HANDLE process,
    std::uintptr_t target
) {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);

    auto current = reinterpret_cast<std::uintptr_t>(
        system.lpMinimumApplicationAddress
    );
    const auto maximum = reinterpret_cast<std::uintptr_t>(
        system.lpMaximumApplicationAddress
    );

    std::vector<std::uintptr_t> matches;
    std::vector<std::byte> buffer(kChunkSize + sizeof(target));

    while (current < maximum) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQueryEx(
                process,
                reinterpret_cast<LPCVOID>(current),
                &memory,
                sizeof(memory)
            ) == 0) {
            break;
        }

        const auto region_base =
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        const auto region_size = static_cast<std::size_t>(memory.RegionSize);

        if (is_readable(memory)) {
            std::size_t offset = 0;
            while (offset < region_size) {
                const auto requested = std::min(
                    kChunkSize,
                    region_size - offset
                );
                SIZE_T bytes_read = 0;
                const auto chunk_address = region_base + offset;

                if (ReadProcessMemory(
                        process,
                        reinterpret_cast<LPCVOID>(chunk_address),
                        buffer.data(),
                        requested,
                        &bytes_read
                    )) {
                    for (std::size_t index = 0;
                         index + sizeof(target) <= bytes_read;
                         index += alignof(std::uintptr_t)) {
                        std::uintptr_t value = 0;
                        std::memcpy(
                            &value,
                            buffer.data() + index,
                            sizeof(value)
                        );
                        if (value == target) {
                            matches.push_back(chunk_address + index);
                        }
                    }
                }

                offset += requested;
            }
        }

        if (region_base + region_size <= current) {
            break;
        }
        current = region_base + region_size;
    }

    return matches;
}

bool write_snapshot(
    HANDLE process,
    std::uintptr_t address,
    const char* path
) {
    std::vector<std::byte> snapshot(kObjectSnapshotSize);
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(
            process,
            reinterpret_cast<LPCVOID>(address),
            snapshot.data(),
            snapshot.size(),
            &bytes_read
        )) {
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    output.write(
        reinterpret_cast<const char*>(snapshot.data()),
        static_cast<std::streamsize>(bytes_read)
    );
    return output.good();
}

void print_hex(std::uintptr_t value) {
    std::cout << "\"0x"
              << std::hex << std::uppercase << value
              << std::dec << "\"";
}

}  // namespace

int wmain() {
    const auto module = find_tll_process();
    if (!module) {
        std::cerr << "tll.exe is not running\n";
        return 1;
    }

    Handle process;
    process.value = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
        FALSE,
        module->process_id
    );
    if (process.value == nullptr) {
        std::cerr << "OpenProcess failed: " << GetLastError() << '\n';
        return 2;
    }

    const auto vtable = module->base + kNadineBuddyVtableRva;
    const auto player_vtable = module->base + kPlayerVtableRva;
    auto candidates = scan_for_pointer(process.value, vtable);
    auto player_candidates = scan_for_pointer(process.value, player_vtable);

    const auto live_nadine = std::find_if(
        candidates.begin(),
        candidates.end(),
        [&](std::uintptr_t candidate) {
            return read_inline_name(process.value, candidate + 8)
                == "nadine-buddy";
        }
    );
    const auto live_player = std::find_if(
        player_candidates.begin(),
        player_candidates.end(),
        [&](std::uintptr_t candidate) {
            return read_inline_name(process.value, candidate + 8) == "Player";
        }
    );

    if (live_nadine != candidates.end()) {
        write_snapshot(
            process.value,
            *live_nadine,
            "nadine_object.bin"
        );
    }
    if (live_player != player_candidates.end()) {
        write_snapshot(
            process.value,
            *live_player,
            "player_object.bin"
        );
    }

    std::cout << "{\n"
              << "  \"process_id\": " << module->process_id << ",\n"
              << "  \"module_base\": ";
    print_hex(module->base);
    std::cout << ",\n  \"nadine_buddy_vtable\": ";
    print_hex(vtable);
    std::cout << ",\n  \"player_vtable\": ";
    print_hex(player_vtable);
    std::cout << ",\n  \"nadine_buddy_candidates\": [\n";

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto candidate = candidates[index];
        const auto name = read_inline_name(process.value, candidate + 8);
        const auto object_pointers = read_pointers<16>(
            process.value,
            candidate
        );
        const auto vtable_entries = read_pointers<16>(
            process.value,
            vtable
        );

        std::cout << "    {\"address\": ";
        print_hex(candidate);
        std::cout << ", \"inline_name\": \"" << name << "\",\n"
                  << "     \"object_qwords\": [";
        for (std::size_t pointer_index = 0;
             pointer_index < object_pointers.size();
             ++pointer_index) {
            if (pointer_index != 0) {
                std::cout << ", ";
            }
            print_hex(object_pointers[pointer_index]);
        }
        std::cout << "],\n     \"vtable_entries\": [";
        for (std::size_t entry_index = 0;
             entry_index < vtable_entries.size();
             ++entry_index) {
            if (entry_index != 0) {
                std::cout << ", ";
            }
            print_hex(vtable_entries[entry_index]);
        }
        std::cout << "]}";
        if (index + 1 != candidates.size()) {
            std::cout << ',';
        }
        std::cout << '\n';
    }

    std::cout << "  ],\n  \"player_candidates\": [\n";
    for (std::size_t index = 0; index < player_candidates.size(); ++index) {
        const auto candidate = player_candidates[index];
        const auto name = read_inline_name(process.value, candidate + 8);
        const auto object_pointers = read_pointers<16>(
            process.value,
            candidate
        );

        std::cout << "    {\"address\": ";
        print_hex(candidate);
        std::cout << ", \"inline_name\": \"" << name << "\",\n"
                  << "     \"object_qwords\": [";
        for (std::size_t pointer_index = 0;
             pointer_index < object_pointers.size();
             ++pointer_index) {
            if (pointer_index != 0) {
                std::cout << ", ";
            }
            print_hex(object_pointers[pointer_index]);
        }
        std::cout << "]}";
        if (index + 1 != player_candidates.size()) {
            std::cout << ',';
        }
        std::cout << '\n';
    }

    std::cout << "  ]\n}\n";
    return 0;
}
