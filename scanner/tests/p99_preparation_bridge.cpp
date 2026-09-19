// Offline-only full production-TU harness. Its DllMain NEVER installs hooks,
// starts threads/networking, or discovers a game. The exports explicitly
// binds a caller-owned mapped image/job for one serialized bounded invocation.
#define TLL_P99_PREPARATION_AUDIT 1
#include "../src/coop_runtime.cpp"

namespace {
volatile LONG g_p99_prepare_export_busy = 0;

bool p99_prepare_buffer(const void* pointer, std::size_t bytes, bool writable) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    if (!address || !bytes || bytes > UINTPTR_MAX - address) return false;
    auto cursor = address;
    const auto end = address + bytes;
    for (unsigned count = 0; cursor < end && count < 64; ++count) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD)) return false;
        const auto protection = info.Protect & 0xFF;
        const bool write = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if ((!write && protection != PAGE_READONLY && protection != PAGE_EXECUTE_READ) ||
            (writable && !write)) return false;
        const auto region = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (region > cursor || !info.RegionSize || info.RegionSize > UINTPTR_MAX - region ||
            region + info.RegionSize <= cursor) return false;
        cursor = std::min(end, region + info.RegionSize);
    }
    return cursor == end;
}

bool p99_prepare_owned_image(std::uintptr_t image) {
    if (!image || image > UINTPTR_MAX - 0x8000000 ||
        !p99_prepare_buffer(reinterpret_cast<void*>(image), 0x1000, false)) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<void*>(image), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.AllocationBase != reinterpret_cast<void*>(image) || memory.Type != MEM_PRIVATE) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) ||
        dos->e_lfanew > 0x1000 - sizeof(IMAGE_NT_HEADERS64)) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.ImageBase != 0x140000000 || nt->OptionalHeader.SizeOfImage < 0x7000000 ||
        nt->OptionalHeader.SizeOfImage > 0x8000000) return false;
    for (const auto rva : {kSkelTableResolveArtItemSkeletonRva, kSkelTableFindRetargetEntryRva,
                          kRetargetPluginDoJointRetargetRva, kAnimJointParamsBlendTransformsByValidBitsRva}) {
        if (VirtualQuery(reinterpret_cast<void*>(image + rva), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.AllocationBase != reinterpret_cast<void*>(image) || memory.State != MEM_COMMIT ||
            (memory.Protect & PAGE_GUARD)) return false;
        const auto protection = memory.Protect & 0xFF;
        if (protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) return false;
    }
    return true; // Full executable hash/registry dependency admission belongs to the pinned caller.
}
}

static int p99_prepare_audit_impl(std::uintptr_t image, void* job,
    std::uint32_t iterations, std::byte* output, std::uint32_t output_bytes,
    coop::p99_preparation_audit::Stats* stats,
    coop::p99_preparation_audit::GuardMutation* guard,
    coop::p99_preparation_audit::QueryTrace* trace) {
    using namespace coop::p99_preparation_audit;
    if (!p99_prepare_buffer(stats, sizeof(Stats), true)) return -1;
    __try {
        if (stats->size != sizeof(Stats) || stats->version != kVersion) return -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    *stats = {};
    if (trace) {
        if (!p99_prepare_buffer(trace, sizeof(QueryTrace), true)) return -2;
        __try {
            if (trace->size != sizeof(QueryTrace) || trace->version != kVersion ||
                trace->capacity == 0 || trace->capacity > kQueryTraceCapacity ||
                trace->reserved != 0 || iterations != 1) return -2;
            trace->count = trace->overflow = 0;
            std::memset(trace->entries, 0, sizeof(trace->entries));
        } __except (EXCEPTION_EXECUTE_HANDLER) { return -2; }
    }
    if (guard) {
        if (!p99_prepare_buffer(guard, sizeof(GuardMutation), true) ||
            guard->size != sizeof(GuardMutation) || iterations != 1 ||
            (guard->stage != static_cast<std::uint32_t>(Stage::kPostcheck) &&
             guard->stage != static_cast<std::uint32_t>(Stage::kSeal))) return -2;
        guard->applied = guard->old_protection = guard->observed_protection = guard->error = 0;
    }
    if (!image || !job || !output || iterations == 0 || iterations > kMaximumIterations ||
        output_bytes != kOutputBytes || !p99_prepare_buffer(output, output_bytes, true)) return -2;
    std::memset(output, 0, output_bytes);
    if (InterlockedCompareExchange(&g_p99_prepare_export_busy, 1, 0) != 0) return -3;
    int result = -4;
    __try {
        __try {
            if (!p99_prepare_owned_image(image) || !p99_prepare_buffer(job, 0x38, false)) __leave;
            LARGE_INTEGER frequency{};
            if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
                result = -6;
                stats->failed_stage = static_cast<std::uint32_t>(Stage::kClock);
                __leave;
            }
            stats->qpc_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
            g_module = image;
            g_state_net.enabled = true;
            g_net_role_b = true;
            g_p99_preparation_audit = {image, job, stats, false, guard, trace};
            result = 0;
            for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
                const auto begin = p99_preparation_audit_begin(Stage::kNormalJob);
                P99GroupJob prepared{};
                const bool okay = prepare_p99_group_values(job, &prepared);
                p99_preparation_audit_end(begin, stats->total_ticks);
                if (g_p99_preparation_audit.clock_failed) {
                    stats->failed_stage = static_cast<std::uint32_t>(Stage::kClock);
                    result = -6;
                    break;
                }
                if (!okay) { result = -5; break; }
                // A bounded trace may fill, but it must never bypass a later
                // preparation check. Refuse only after the complete call above.
                if (trace && trace->overflow != 0) { result = -8; break; }
                static_assert(sizeof(prepared.rows) == kOutputBytes);
                if (iteration != 0 && std::memcmp(output, prepared.rows.data(), kOutputBytes) != 0) {
                    ++stats->rejected_calls;
                    result = -5;
                    break;
                }
                std::memcpy(output, prepared.rows.data(), kOutputBytes);
                ++stats->completed_iterations;
                stats->failed_stage = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { result = -7; }
    } __finally {
        if (guard && guard->applied) {
            const auto table_page = (image + kSkelTableRetargetEntriesRva) & ~std::uintptr_t{0xFFF};
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<void*>(table_page), &info, sizeof(info)) == sizeof(info))
                guard->observed_protection = info.Protect;
            else guard->error = GetLastError();
            DWORD ignored = 0;
            if (!VirtualProtect(reinterpret_cast<void*>(table_page), 0x1000,
                    guard->old_protection, &ignored)) guard->error = GetLastError();
        }
        g_p99_preparation_audit = {};
        g_module = 0;
        g_state_net.enabled = false;
        g_net_role_b = false;
        InterlockedExchange(&g_p99_prepare_export_busy, 0);
    }
    if (result != 0) std::memset(output, 0, output_bytes);
    return result;
}

extern "C" __declspec(dllexport) int P99PrepareAudit(std::uintptr_t image, void* job,
    std::uint32_t iterations, std::byte* output, std::uint32_t output_bytes,
    coop::p99_preparation_audit::Stats* stats) {
    return p99_prepare_audit_impl(image, job, iterations, output, output_bytes, stats, nullptr, nullptr);
}

extern "C" __declspec(dllexport) int P99PrepareAuditGuardAtStage(std::uintptr_t image, void* job,
    std::byte* output, std::uint32_t output_bytes, coop::p99_preparation_audit::Stats* stats,
    coop::p99_preparation_audit::GuardMutation* guard) {
    if (!guard) return -2;
    return p99_prepare_audit_impl(image, job, 1, output, output_bytes, stats, guard, nullptr);
}

extern "C" __declspec(dllexport) int P99PrepareAuditTrace(std::uintptr_t image, void* job,
    std::byte* output, std::uint32_t output_bytes, coop::p99_preparation_audit::Stats* stats,
    coop::p99_preparation_audit::QueryTrace* trace) {
    if (!trace) return -2;
    return p99_prepare_audit_impl(image, job, 1, output, output_bytes, stats, nullptr, trace);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, void*) {
    return TRUE;
}
