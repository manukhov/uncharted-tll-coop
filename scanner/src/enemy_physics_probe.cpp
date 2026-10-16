// Bounded external configuration observation, never a native-phase snapshot.
// No injection, process writes, remote calls, suspension or game input.
#include "enemy_physics_observation.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <utility>

namespace {
namespace obs = coop::enemy_physics_observation;
struct Handle {
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Reader {
    HANDLE process{};
    std::uint64_t calls{}, bytes{};
    bool operator()(std::uintptr_t address, void* destination, std::size_t size) {
        // Reject invalid/overflowed user-space ranges before requesting a copy.
        constexpr std::uintptr_t max_address = 0x00007fffffffffffULL;
        if (address < 0x10000 || address > max_address || !size ||
            size - 1 > max_address - address) return false;
        ++calls;
        SIZE_T copied{};
        const auto ok = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address),
                                          destination, size, &copied);
        bytes += copied;
        return ok && copied == size;
    }
};
bool positive_number(const wchar_t* text, unsigned long maximum, unsigned long& out) {
    if (!text || *text < L'1' || *text > L'9') return false;
    errno = 0;
    wchar_t* end{};
    out = std::wcstoul(text, &end, 10);
    return errno != ERANGE && *end == 0 && out > 0 && out <= maximum;
}
std::uintptr_t main_module(DWORD pid) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)};
    if (snapshot.value == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    if (!Module32FirstW(snapshot.value, &module) || _wcsicmp(module.szModule, L"tll.exe"))
        return 0;
    return reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
}
bool supported_image(Reader& read, std::uintptr_t base) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!base || !read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        !read(base + static_cast<std::uintptr_t>(dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage <= 0x3898638) return false;
    // Fail closed for a layout other than the statically audited exact Pirate.
    constexpr std::array<std::pair<std::uintptr_t, std::uintptr_t>, 3> slots{{
        {0x408, 0x1de9910}, {0x410, 0x4aa090}, {0x450, 0x1bc03f0}}};
    for (const auto& [offset, rva] : slots) {
        std::uintptr_t target{};
        if (!read(base + obs::kPirateRva + offset, &target, sizeof(target)) || target != base + rva)
            return false;
    }
    return true;
}
bool running(HANDLE process) {
    DWORD code{};
    return GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
}
void pointer(std::uintptr_t value) { std::printf("\"0x%llx\"", static_cast<unsigned long long>(value)); }
void json_text(const char* value, std::size_t maximum) {
    std::putchar('"');
    for (std::size_t i = 0; i < maximum && value[i]; ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c == '"' || c == '\\') std::printf("\\%c", c);
        else if (c < 32 || c >= 127) std::printf("\\u%04x", static_cast<unsigned>(c));
        else std::putchar(c);
    }
    std::putchar('"');
}
void scalar(float value) {
    if (std::isfinite(value)) std::printf("%.9g", static_cast<double>(value));
    else std::printf("null");
}
template<std::size_t N> void floats(const std::array<float, N>& values) {
    std::putchar('[');
    for (std::size_t i = 0; i < N; ++i) { if (i) std::putchar(','); scalar(values[i]); }
    std::putchar(']');
}
void observation(const obs::Observation& o, obs::Status status) {
    std::printf("{\"status\":\"%s\",\"record\":", obs::status_name(status)); pointer(o.entry.record);
    std::printf(",\"id\":%u,\"actor\":", o.entry.id); pointer(o.actor);
    std::printf(",\"debug_name_abbreviated_not_identity\":"); json_text(o.debug_name.data(), o.debug_name.size());
    std::printf(",\"process_flags\":%u,\"owner_flags360\":%u,\"hp\":", o.process_flags, o.owner_flags360);
    if (o.health_known) std::printf("%d", o.hp); else std::printf("null");
    std::printf(",\"composite\":"); pointer(o.composite);
    std::printf(",\"ragdoll_unfollowed\":"); pointer(o.ragdoll);
    std::printf(",\"optional_body_unfollowed\":"); pointer(o.optional_body);
    std::printf(",\"body_array\":"); pointer(o.body_array);
    std::printf(",\"binding_array\":"); pointer(o.binding_array);
    std::printf(",\"auxiliary\":"); pointer(o.auxiliary);
    std::printf(",\"body_count\":%u,\"binding_count\":%u,\"copied_bodies\":%u,\"copied_bindings\":%u,"
                "\"flags0\":%u,\"flags2\":%u,\"gate18\":%u,\"gate24\":%u,\"root_selector\":%d,"
                "\"root_import_mapping_potential\":", o.body_count, o.binding_count,
                o.copied_bodies, o.copied_bindings, o.flags0, o.flags2, o.gate18, o.gate24, o.root_selector);
    if (status == obs::Status::ok && obs::valid_bindings(o))
        std::printf("%s", obs::has_root_import_mapping(o) ? "true" : "false");
    else std::printf("null");
    std::printf(",\"local_pose\":"); floats(o.local_pose);
    std::printf(",\"derived_pose\":"); floats(o.derived_pose);
    std::printf(",\"bodies\":[");
    for (std::size_t i = 0; i < o.copied_bodies; ++i) {
        const auto& b = o.bodies[i];
        if (i) std::putchar(',');
        std::printf("{\"index\":%zu,\"owner\":", i); pointer(b.owner);
        std::printf(",\"native\":"); pointer(b.native);
        std::printf(",\"handle_id\":%u,\"handle_index\":%u,\"flags\":%u,\"desired_mode\":%u,"
                    "\"native_motion_different_enum\":%u,\"in_world\":%s,\"joint\":%d,\"source\":%u,"
                    "\"source_class\":\"%s\",\"blend\":", b.handle_id, b.handle_index, b.flags,
                    obs::desired_mode(b), b.native_motion, b.in_world ? "true" : "false", b.joint, b.source,
                    obs::source_name(b, o.actor));
        scalar(b.blend); std::printf(",\"pose\":"); floats(b.pose); std::putchar('}');
    }
    std::printf("],\"bindings\":[");
    for (std::size_t i = 0; i < o.copied_bindings; ++i) {
        if (i) std::putchar(',');
        std::printf("{\"key\":%d,\"body_index\":%u}", o.bindings[i].key, o.bindings[i].body_index);
    }
    std::printf("]}");
}
struct Roster { std::uintptr_t manager{}, array{}; std::uint32_t count{}; };
bool roster(Reader& read, std::uintptr_t base, Roster& result) {
    result = {};
    if (!read(base + 0x3898630, &result.manager, sizeof(result.manager)) || !result.manager) return false;
    std::array<std::byte, 16> bytes{};
    if (!read(result.manager, bytes.data(), bytes.size())) return false;
    result.array = obs::field<std::uintptr_t>(bytes, 0);
    result.count = obs::field<std::uint32_t>(bytes, 0xc);
    return !result.count || result.array != 0;
}
bool same_roster(const Roster& a, const Roster& b) {
    return a.manager == b.manager && a.array == b.array && a.count == b.count;
}
bool row(Reader& read, std::uintptr_t base, const Roster& original, std::size_t index, obs::Entry& entry) {
    Roster current{};
    return roster(read, base, current) && same_roster(current, original) && index < current.count &&
           read(current.array + index * sizeof(obs::Entry), &entry, sizeof(entry));
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    unsigned long pid{}, rounds = 6;
    if ((argc != 3 && argc != 5) || std::wcscmp(argv[1], L"--pid") ||
        !positive_number(argv[2], std::numeric_limits<DWORD>::max(), pid) ||
        (argc == 5 && (std::wcscmp(argv[3], L"--rounds") || !positive_number(argv[4], 6, rounds)))) {
        std::fprintf(stderr, "usage: tll_enemy_physics_probe --pid N [--rounds 1..6]\n");
        return 1;
    }
    Handle process{OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.value) { std::fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError()); return 2; }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process.value, &created, &exited, &kernel, &user)) return 2;
    const auto base = main_module(pid);
    Reader read{process.value};
    if (!running(process.value) || !supported_image(read, base)) {
        std::fprintf(stderr, "Unsupported image, modified Pirate slots, or exited process; no roster read.\n");
        return 3;
    }
    const auto creation = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    std::printf("{\"type\":\"scope\",\"schema\":1,\"pid\":%lu,\"creation_filetime\":%llu,\"base\":", pid,
                static_cast<unsigned long long>(creation)); pointer(base);
    std::printf(",\"read_only\":true,\"atomic_snapshot\":false,\"write_admission\":false,"
                "\"max_rows\":64,\"max_bodies\":32,\"max_bindings\":32,\"rounds\":%lu}\n", rounds);
    unsigned observed{}, repeated{}, positive_hp{};
    for (unsigned long round = 0; round < rounds; ++round) {
        if (!running(process.value)) { std::printf("{\"type\":\"terminated\"}\n"); return 4; }
        const auto begin = GetTickCount64();
        const auto calls = read.calls, bytes = read.bytes;
        Roster start{};
        const bool roster_ok = roster(read, base, start);
        const auto count = roster_ok ? std::min<std::size_t>(start.count, obs::kMaxActors) : 0;
        unsigned row_changes{}, refusals{};
        for (std::size_t i = 0; i < count; ++i) {
            obs::Observation first{}, second{};
            obs::Entry a{}, b{};
            const bool first_row = row(read, base, start, i, a);
            const auto first_status = first_row ? obs::capture(read, base, a, first) : obs::Status::unreadable;
            const bool second_row = row(read, base, start, i, b);
            const auto second_status = second_row ? obs::capture(read, base, b, second) : obs::Status::unreadable;
            obs::Entry after{};
            const bool same_row = first_row && second_row && row(read, base, start, i, after) &&
                a.record == b.record && a.id == b.id && b.record == after.record && b.id == after.id;
            const bool both_ok = first_status == obs::Status::ok && second_status == obs::Status::ok;
            const bool repeat = same_row && both_ok && obs::repeated_configuration(first, second);
            if (!same_row) ++row_changes;
            if (!both_ok) ++refusals;
            if (second_status == obs::Status::ok) ++observed;
            if (repeat) ++repeated;
            if (repeat && first.health_known && second.health_known && first.hp > 0 && second.hp > 0) ++positive_hp;
            std::printf("{\"type\":\"row\",\"round\":%lu,\"index\":%zu,\"same_roster_row\":%s,"
                        "\"repeated_configuration_not_lease\":%s,\"first\":", round, i,
                        same_row ? "true" : "false", repeat ? "true" : "false");
            observation(first, first_status); std::printf(",\"second\":"); observation(second, second_status);
            std::printf("}\n");
        }
        Roster end{};
        const bool header_same = roster_ok && roster(read, base, end) && same_roster(start, end);
        std::printf("{\"type\":\"round\",\"round\":%lu,\"roster_readable\":%s,\"header_unchanged_not_atomic\":%s,"
                    "\"reported_count\":%u,\"sampled_rows\":%zu,\"over_cap_partial\":%s,"
                    "\"changed_rows\":%u,\"refused_pairs\":%u,\"read_calls\":%llu,\"copied_bytes\":%llu,\"elapsed_ms\":%llu}\n",
                    round, roster_ok ? "true" : "false", header_same ? "true" : "false", start.count, count,
                    start.count > obs::kMaxActors ? "true" : "false", row_changes, refusals,
                    static_cast<unsigned long long>(read.calls - calls), static_cast<unsigned long long>(read.bytes - bytes),
                    static_cast<unsigned long long>(GetTickCount64() - begin));
        std::fflush(stdout);
        if (round + 1 < rounds) Sleep(500);
    }
    std::printf("{\"type\":\"summary\",\"observed_exact_pirate_rows\":%u,\"repeated_config_rows\":%u,"
                "\"repeated_positive_hp_rows\":%u,\"unique_enemy_count\":null,\"behavior_validated\":false}\n",
                observed, repeated, positive_hp);
    return positive_hp ? 0 : 5; // No qualifying data is inconclusive, not a passed exam.
}
