#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include "coop_runtime_offsets.h"

// Only the used runtime globals/constants are modeled; no game getter/resolver
// symbols exist in this translation unit. Offsets come from the real header.
std::uintptr_t g_module = 0;
struct { bool enabled = true; } g_state_net;
volatile bool g_net_role_b = true;
std::atomic<bool> g_local_reload_level{false};
std::atomic<std::uint64_t> g_cached_local_input_buttons{0};
constexpr std::uint64_t kInputBlockButtonBaseMask = 0x8000000000000000ull;
constexpr std::uintptr_t kPlayerIsInCoverFlagOffset = 0x1510;
constexpr std::uint32_t kLegacyNadineAttachTargetLookId = 0xD27C074Fu;
constexpr std::uint32_t kLegacyNadineTargetSkeletonArtId = 0xED25C708u;
constexpr std::uint32_t kLegacyNadineUnderlyingSkeletonId = 0xC39CA709u;
constexpr std::uint32_t kLegacyNadineSkeletonJointCount = 580;
constexpr std::uint32_t kCoopPistolArtId = 0x8075397Du;

enum class Interference { none, turn_over, fault_after_query, fault_owner_after_query, change_pose };
Interference interference = Interference::none;
std::uintptr_t trigger_address = 0, fixture_base = 0;
unsigned query_count = 0, interfere_at_query = 0;
struct ReaderOperation {
    unsigned kind = 0;
    std::uintptr_t base = 0;
    std::size_t offset = 0, size = 0;
    bool operator==(const ReaderOperation& other) const {
        return kind == other.kind && base == other.base && offset == other.offset && size == other.size;
    }
};
std::array<ReaderOperation, 512> reader_operations{};
std::size_t reader_operation_count = 0;
bool record_reader_operations = false, reader_operations_overflow = false;
enum class ControlFault { None, ReadRefused, Mismatch, Protect };
ControlFault control_fault = ControlFault::None;
std::uintptr_t control_fault_address = 0;
std::uint64_t control_fault_value = 0;
unsigned control_fault_snapshot = 0, observed_snapshot = 0;
void record_reader_operation(unsigned kind, std::uintptr_t base, std::size_t offset, std::size_t size) {
    if (!record_reader_operations) return;
    if (reader_operation_count == reader_operations.size()) { reader_operations_overflow = true; return; }
    reader_operations[reader_operation_count++] = {kind, base, offset, size};
}
// Opt-in fixture measurements: real VirtualQuery/reader, no mocked latency.
// These single-thread counters are bounded; printing is outside timed spans.
bool benchmark_active = false, benchmark_clock_valid = true;
std::uint64_t benchmark_vq_ticks = 0, benchmark_vq_max = 0;
unsigned benchmark_queries = 0;
template<class T> void put(std::uintptr_t address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
bool p99_consumed_fixture_read(std::uintptr_t base, std::size_t offset, std::size_t size) {
    if (!record_reader_operations) return true;
    record_reader_operation(1, base, offset, size);
    if (base == fixture_base && offset == 0) ++observed_snapshot;
    if (control_fault == ControlFault::None || observed_snapshot != control_fault_snapshot ||
        base + offset != control_fault_address) return true;
    const auto action = control_fault;
    control_fault = ControlFault::None;
    if (action == ControlFault::ReadRefused) return false; // Controlled range/read-refusal seam.
    if (action == ControlFault::Mismatch) {
        std::memcpy(reinterpret_cast<void*>(base + offset), &control_fault_value, size);
    } else if (action == ControlFault::Protect) {
        // The reader already cached this committed region. Changing protection
        // now exercises its actual volatile load and actual enclosing SEH.
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void*>((base + offset) & ~std::uintptr_t{0xFFF}),
            0x1000, PAGE_NOACCESS, &old);
    }
    return true;
}
SIZE_T WINAPI fixture_query(LPCVOID address, PMEMORY_BASIC_INFORMATION mbi, SIZE_T size) {
    record_reader_operation(2, reinterpret_cast<std::uintptr_t>(address), 0, size);
    LARGE_INTEGER begin{}, end{};
    const bool measure = benchmark_active;
    const bool clock_begin = !measure || QueryPerformanceCounter(&begin) != FALSE;
    const auto result = ::VirtualQuery(address, mbi, size);
    if (measure) {
        ++benchmark_queries;
        if (!QueryPerformanceCounter(&end) || !clock_begin || begin.QuadPart < 0 ||
            end.QuadPart < begin.QuadPart) benchmark_clock_valid = false;
        else {
            const auto ticks = static_cast<std::uint64_t>(end.QuadPart - begin.QuadPart);
            benchmark_vq_ticks += ticks;
            benchmark_vq_max = std::max(benchmark_vq_max, ticks);
        }
    }
    ++query_count;
    if (interference != Interference::none &&
        ((interfere_at_query != 0 && query_count == interfere_at_query) ||
         (trigger_address != 0 && reinterpret_cast<std::uintptr_t>(address) == trigger_address))) {
        const auto action = interference;
        interference = Interference::none;
        if (action == Interference::turn_over) {
            // Fully consistent new identity at the same addresses. Equality of
            // pointers alone must not authorize its rows for the previous owner.
            for (const auto offset : {0x8u, 0x10BCu, 0x61B0u, 0x94E8u})
                put(fixture_base + offset, std::uint32_t{0xABC123u});
        } else if (action == Interference::change_pose) {
            put(fixture_base + 0x6100, fixture_base + 0xC000);
        } else {
            DWORD old = 0;
            const auto page = action == Interference::fault_owner_after_query ?
                fixture_base : fixture_base + 0xB000;
            VirtualProtect(reinterpret_cast<void*>(page),
                0x1000, PAGE_NOACCESS, &old);
        }
    }
    return result;
}
#define VirtualQuery fixture_query
#define TLL_P99_CONSUMED_OWNER_TEST
#include "nadine_p99_consumed_owner.inl"
#undef TLL_P99_CONSUMED_OWNER_TEST
#undef VirtualQuery

namespace {
using Reject = P99ConsumedOwnerReject;
unsigned checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", name); }
}
struct Fixture {
    explicit Fixture(std::size_t allocation_bytes = 0x10000)
        : base(reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, allocation_bytes,
            MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE))) {}
    std::uintptr_t base = 0;
    ~Fixture() { if (base) VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE); }
    void reset() {
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void*>(base), 0x10000, PAGE_READWRITE, &old);
        std::memset(reinterpret_cast<void*>(base), 0, 0x10000);
        // Numeric module identity only. No fake native vtable is callable.
        g_module = base - kRetainedSlotTableRva;
        fixture_base = base;
        interference = Interference::none;
        trigger_address = interfere_at_query = query_count = 0;
        record_reader_operations = reader_operations_overflow = false;
        reader_operation_count = 0; observed_snapshot = 0;
        control_fault = ControlFault::None; control_fault_address = 0;
        control_fault_value = 0; control_fault_snapshot = 0;
        g_state_net.enabled = true; g_net_role_b = true;
        g_local_reload_level.store(false); g_cached_local_input_buttons.store(0);
        put(base, base + 0x100); put(base + 8, std::uint32_t{0x123u});
        put(base + 0x108, base + 0x1000);
        put(base + 0x1000, g_module + kPlayerVtableRva);
        put(base + 0x10BC, std::uint32_t{0x123u});
        put(base + 0x1170, kLegacyNadineAttachTargetLookId);
        put(base + 0x11A8, base + 0x6000);
        put(base + 0x2D88, base + 0x7000);
        put(base + 0x2E50, base + 0x8000);
        put(base + 0x6006, std::uint8_t{2}); put(base + 0x6010, base + 0xA500);
        put(base + 0x6018, base + 0xA000); put(base + 0x6100, base + 0xB000);
        put(base + 0x61A8, base + 0x100); put(base + 0x61B0, std::uint32_t{0x123u});
        put(base + 0x7000, g_module + kPlayerWeaponControllerVtableRva);
        put(base + 0x7108, base + 0x1000); put(base + 0x7134, std::uint32_t{2});
        put(base + 0x7148, base + 0xA200); put(base + 0x7150, std::uint32_t{0x456u});
        put(base + 0x8000, g_module + kFastGrenadeControllerVtableRva);
        put(base + 0x8108, base + 0x1000);
        put(base + 0x9000, g_module + kProcessWeaponVtableRva);
        put(base + 0x9072, std::uint8_t{0x5A}); put(base + 0x90BC, std::uint32_t{0x456u});
        put(base + 0x94E0, base + 0xA300); put(base + 0x94E8, std::uint32_t{0x123u});
        put(base + 0x9860, kCoopPistolArtId);
        put(base + 0xA024, kLegacyNadineTargetSkeletonArtId);
        put(base + 0xA034, kLegacyNadineSkeletonJointCount); put(base + 0xA040, base + 0xA100);
        put(base + 0xA104, kLegacyNadineUnderlyingSkeletonId);
        put(base + 0xA110, std::uint16_t{580});
        put(base + 0xA208, base + 0x9000); put(base + 0xA308, base + 0x1000);
        put(base + 0xA400, base + 0xA500); put(base + 0xA408, base + 0x6000);
        for (std::size_t i = 0; i < 31 * 48; ++i)
            put(base + 0xB000 + i, static_cast<std::uint8_t>(i * 73 + 29));
        for (std::size_t row = 0; row < 31; ++row) {
            put(base + 0xB000 + row * 48 + 12, std::uint32_t{0x7FC12345});
            put(base + 0xB000 + row * 48 + 44, std::uint32_t{0xFFC54321});
        }
        std::memcpy(reinterpret_cast<void*>(base + 0xC000),
            reinterpret_cast<const void*>(base + 0xB000), 31 * 48);
    }
    void* job() const { return reinterpret_cast<void*>(base + 0xA400); }
    template<class T> void refuse(std::size_t offset, T value, Reject expected, const char* name) {
        reset(); put(base + offset, value);
        P99ConsumedOwner owner{}; owner.player = owner.weapon = 42; owner.slot = 3;
        Reject reason = Reject::None;
        check(!seh_read_p99_consumed_owner(job(), &owner, &reason) &&
            same_owner(owner, P99ConsumedOwner{}) && reason == expected, name);
    }
};

struct ControlTraceRun {
    std::array<ReaderOperation, 512> operations{};
    std::size_t count = 0;
    P99ConsumedOwner owner{};
    Reject reason = Reject::None;
    P99ConsumedControlTrace trace{};
    bool accepted = false;
};
ControlTraceRun run_control_trace(Fixture& f, bool traced, unsigned snapshot,
    std::size_t offset, ControlFault fault, std::uint64_t bad_value = 0) {
    f.reset();
    record_reader_operations = true; control_fault_snapshot = snapshot;
    control_fault_address = f.base + offset; control_fault = fault;
    control_fault_value = bad_value;
    ControlTraceRun result{};
    result.owner.player = 42; result.owner.slot = 12;
    result.trace.snapshot = 77; result.trace.valid = 7;
    result.trace.slot = 9; result.trace.draw = result.trace.grenade = 123;
    result.accepted = seh_read_p99_consumed_owner(f.job(), &result.owner, &result.reason,
        traced ? &result.trace : nullptr);
    check(!reader_operations_overflow, "bounded read/query transcript does not truncate");
    result.count = reader_operation_count;
    std::copy_n(reader_operations.begin(), result.count, result.operations.begin());
    record_reader_operations = false;
    return result;
}
void compare_control_runs(const ControlTraceRun& plain, const ControlTraceRun& traced) {
    check(plain.accepted == traced.accepted && plain.reason == traced.reason &&
        same_owner(plain.owner, traced.owner), "trace preserves acceptance, reason and exact owner");
    check(plain.count == traced.count &&
        std::equal(plain.operations.begin(), plain.operations.begin() + plain.count, traced.operations.begin()),
        "traced/untraced logical reads and VirtualQuery calls are identical and ordered");
}
void control_trace_tests(Fixture& f) {
    using Check = P99ConsumedControlCheck;
    using Outcome = P99ConsumedControlOutcome;
    const std::array<std::size_t, 9> offsets{
        0x2D88, 0x7000, 0x7108, 0x7120, 0x7134, 0x2E50, 0x8000, 0x8108, 0x8120};
    for (unsigned snapshot : {1u, 2u}) for (unsigned i = 0; i < offsets.size(); ++i) {
        const auto check_id = static_cast<Check>(i + 1);
        for (const auto fault : {ControlFault::ReadRefused, ControlFault::Protect}) {
            const auto plain = run_control_trace(f, false, snapshot, offsets[i], fault);
            const auto traced = run_control_trace(f, true, snapshot, offsets[i], fault);
            compare_control_runs(plain, traced);
            const auto valid = i <= 3 ? 0u : i == 4 ? 1u : 3u;
            check(!traced.accepted && same_owner(traced.owner, P99ConsumedOwner{}) &&
                traced.reason == (fault == ControlFault::Protect ? Reject::ReadFault : Reject::Control) &&
                traced.trace.snapshot == snapshot && traced.trace.check == check_id &&
                traced.trace.outcome == (fault == ControlFault::Protect ? Outcome::Seh : Outcome::ReadRefused),
                "every Control read distinguishes refusal from real SEH in both snapshots");
            check(traced.trace.valid == valid &&
                ((valid & 1) || traced.trace.slot == -1) &&
                ((valid & 2) || traced.trace.draw == 0) && traced.trace.grenade == 0,
                "read failure preserves only already-completed scalar reads of this snapshot");
            const auto& last = traced.operations[traced.count - 1];
            check(last.kind == 1 && last.base + last.offset == f.base + offsets[i],
                "failed Control read short-circuits all later reads and queries");
        }
        if (i == 0 || i == 5) continue; // Pointer reads have no separate non-null predicate.
        const auto bad = i == 3 ? UINT64_C(0xFFFFFFFF) : i == 8 ? 1u : 0u;
        const auto plain = run_control_trace(f, false, snapshot, offsets[i], ControlFault::Mismatch, bad);
        const auto traced = run_control_trace(f, true, snapshot, offsets[i], ControlFault::Mismatch, bad);
        compare_control_runs(plain, traced);
        const auto valid = i < 3 ? 0u : i == 3 ? 1u : i < 8 ? 3u : 7u;
        check(!traced.accepted && same_owner(traced.owner, P99ConsumedOwner{}) &&
            traced.reason == Reject::Control && traced.trace.snapshot == snapshot &&
            traced.trace.check == check_id && traced.trace.outcome == Outcome::ValueMismatch &&
            traced.trace.valid == valid, "every Control value predicate reports both snapshots exactly");
        if (i == 3) check(traced.trace.slot == -1 && traced.trace.draw == 0 &&
            traced.trace.grenade == 0, "negative slot is valid evidence, draw/grenade remain unread");
        if (i == 4) check(traced.trace.slot == 0 && traced.trace.draw == 0 &&
            traced.trace.grenade == 0, "draw mismatch captures value without reading grenade state");
        if (i == 8) check(traced.trace.slot == 0 && traced.trace.draw == 2 &&
            traced.trace.grenade == 1, "grenade mismatch retains all three read scalars");
        const auto& last = traced.operations[traced.count - 1];
        check(last.kind == 1 && last.base + last.offset == f.base + offsets[i],
            "mismatched Control value never executes a later read");
    }
    for (unsigned snapshot : {1u, 2u}) {
        const auto plain = run_control_trace(f, false, snapshot, 0x7120, ControlFault::Mismatch, 18);
        const auto traced = run_control_trace(f, true, snapshot, 0x7120, ControlFault::Mismatch, 18);
        compare_control_runs(plain, traced);
        check(traced.trace.snapshot == snapshot && traced.trace.check == Check::Slot &&
            traced.trace.outcome == Outcome::ValueMismatch && traced.trace.valid == 1 && traced.trace.slot == 18,
            "upper slot bound remains a distinct observed value");
        for (const auto offset : {0x2D88u, 0x2E50u}) {
            const auto null_plain = run_control_trace(f, false, snapshot, offset, ControlFault::Mismatch);
            const auto null_traced = run_control_trace(f, true, snapshot, offset, ControlFault::Mismatch);
            compare_control_runs(null_plain, null_traced);
            check(null_traced.trace.snapshot == snapshot && null_traced.reason == Reject::Control &&
                null_traced.trace.check == (offset == 0x2D88 ? Check::WeaponVtable : Check::GrenadeVtable) &&
                null_traced.trace.outcome == Outcome::ReadRefused,
                "null controller remains a failed subsequent vtable read, not a new pointer guard");
        }
    }
    const auto plain = run_control_trace(f, false, 0, 0, ControlFault::None);
    const auto traced = run_control_trace(f, true, 0, 0, ControlFault::None);
    compare_control_runs(plain, traced);
    check(traced.accepted && traced.trace.snapshot == 2 && traced.trace.check == Check::None &&
        traced.trace.outcome == Outcome::Passed && traced.trace.valid == 7 &&
        traced.trace.slot == 0 && traced.trace.draw == 2 && traced.trace.grenade == 0,
        "successful owner exposes only second-snapshot Control values");
    // Verify the unchanged nine-read subsequence independently of traced-vs-
    // untraced equality, so both paths cannot silently acquire extra fields.
    unsigned found = 0;
    for (std::size_t n = 0; n < traced.count; ++n) {
        const auto& operation = traced.operations[n];
        if (operation.kind != 1) continue;
        const auto absolute = operation.base + operation.offset;
        if (std::find(offsets.begin(), offsets.end(), absolute - f.base) == offsets.end()) continue;
        check(absolute == f.base + offsets[found % offsets.size()], "Control read order matches original chain");
        ++found;
    }
    check(found == 18, "exactly nine Control reads in each of two successful snapshots");
    f.reset();
    P99ConsumedControlTrace early{}; early.snapshot = 99; early.valid = 7;
    Reject reason{}; P99ConsumedOwner owner{};
    check(!seh_read_p99_consumed_owner(nullptr, &owner, &reason, &early) &&
        early.snapshot == 1 && early.valid == 0 && early.check == Check::None && early.outcome == Outcome::None,
        "failure before Control cannot leak a stale trace");
}
}

namespace {
struct CostSample {
    std::uint64_t wall = 0, queries = 0, vq = 0, max_vq = 0;
    bool valid = false, accepted = false;
    Reject reason = Reject::None;
};
CostSample measure_owner(Fixture& fixture) {
    CostSample sample{};
    P99ConsumedOwner owner{};
    LARGE_INTEGER begin{}, end{};
    benchmark_clock_valid = true; benchmark_vq_ticks = benchmark_vq_max = 0;
    benchmark_queries = 0;
    const bool start_valid = QueryPerformanceCounter(&begin) != FALSE;
    benchmark_active = true;
    sample.accepted = seh_read_p99_consumed_owner(fixture.job(), &owner, &sample.reason);
    benchmark_active = false;
    sample.valid = QueryPerformanceCounter(&end) != FALSE && start_valid &&
        begin.QuadPart >= 0 && end.QuadPart >= begin.QuadPart && benchmark_clock_valid;
    if (sample.valid) sample.wall = static_cast<std::uint64_t>(end.QuadPart - begin.QuadPart);
    sample.queries = benchmark_queries; sample.vq = benchmark_vq_ticks; sample.max_vq = benchmark_vq_max;
    return sample;
}
bool prepare_benchmark(Fixture& fixture, unsigned mode, bool fragmented) {
    if (!fixture.base || fixture.base <= kRetainedSlotTableRva) return false;
    fixture.reset(); // Only the first64KiB is touched even in the large region.
    if (mode == 1) put(fixture.base + 0x7120, std::int32_t{-1});
    if (mode == 2) put(fixture.base + 0x7134, std::uint32_t{0});
    if (fragmented) for (std::size_t page = 0; page < 16; ++page) {
        DWORD old = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(fixture.base + page * 0x1000), 0x1000,
            page % 2 ? PAGE_READONLY : PAGE_READWRITE, &old)) return false;
    }
    return true;
}
unsigned print_costs(const char* layout, const char* mode, const char* phase,
    const std::array<CostSample, 64>& samples, std::size_t count, double frequency) {
    unsigned accepted = 0, invalid = 0, unexpected = 0;
    const bool expect_accepted = std::strcmp(mode, "admitted") == 0;
    for (std::size_t i = 0; i < count; ++i) {
        accepted += samples[i].accepted; invalid += !samples[i].valid;
        unexpected += samples[i].accepted != expect_accepted ||
            samples[i].reason != (expect_accepted ? Reject::None : Reject::Control);
    }
    std::printf("OWNER-BENCH layout=%s mode=%s phase=%s n=%zu accepted=%u invalid_clock=%u unexpected=%u",
        layout, mode, phase, count, accepted, invalid, unexpected);
    // No timing threshold is a test assertion or a game-worker cost claim.
    for (unsigned metric = 0; metric < 4; ++metric) {
        std::array<std::uint64_t, 64> sorted{};
        for (std::size_t i = 0; i < count; ++i) sorted[i] = metric == 0 ? samples[i].queries :
            metric == 1 ? samples[i].wall : metric == 2 ? samples[i].vq : samples[i].max_vq;
        std::sort(sorted.begin(), sorted.begin() + count);
        const double factor = metric == 0 ? 1.0 : 1000000.0 / frequency;
        const char* label = metric == 0 ? "qcount" : metric == 1 ? "wall_us" :
            metric == 2 ? "vq_sum_us" : "vq_max_us";
        std::printf(" %s[min,p50,p95,max]=%.3f,%.3f,%.3f,%.3f", label,
            sorted[0] * factor, sorted[(count - 1) / 2] * factor,
            sorted[((count * 95 + 99) / 100) - 1] * factor, sorted[count - 1] * factor);
    }
    std::puts("");
    return invalid + unexpected;
}
int benchmark_owner() {
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return 2;
    std::puts("OWNER-BENCH actual reader + actual VirtualQuery; instrumented wall time, not CPU time. "
        "first_after_setup uses fresh allocations but touched fixture pages, NOT proven cold OS pages. "
        "Large regions commit16/64MiB but touch only64KiB; no working-set claim.");
    const char* layouts[] = {"contiguous64KiB", "fragmented64KiB", "contiguous16MiB", "contiguous64MiB"};
    const std::size_t sizes[] = {0x10000, 0x10000, 0x1000000, 0x4000000};
    const char* modes[] = {"admitted", "slot_minus1", "draw0"};
    unsigned integrity_errors = 0;
    for (unsigned layout = 0; layout < 4; ++layout) for (unsigned mode = 0; mode < 3; ++mode) {
        std::array<CostSample, 64> samples{};
        for (unsigned i = 0; i < 32; ++i) {
            Fixture fresh(sizes[layout]);
            if (!prepare_benchmark(fresh, mode, layout == 1)) return 2;
            samples[i] = measure_owner(fresh);
        }
        integrity_errors += print_costs(layouts[layout], modes[mode], "first_after_setup", samples, 32,
            static_cast<double>(frequency.QuadPart));
        Fixture warm(sizes[layout]);
        if (!prepare_benchmark(warm, mode, layout == 1)) return 2;
        measure_owner(warm); // Explicit warmup outside the recorded distribution.
        for (unsigned i = 0; i < 64; ++i) samples[i] = measure_owner(warm);
        integrity_errors += print_costs(layouts[layout], modes[mode], "warm", samples, 64,
            static_cast<double>(frequency.QuadPart));
    }
    std::printf("OWNER-BENCH integrity_errors=%u; no timing thresholds\n", integrity_errors);
    return integrity_errors ? 1 : 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) return benchmark_owner();
    if (argc != 1) { std::puts("Usage: nadine_p99_consumed_owner_tests [--benchmark]"); return 2; }
    Fixture f;
    if (!f.base || f.base <= kRetainedSlotTableRva) {
        std::printf("Fixture allocation failed: base=%p error=%lu pointerSize=%zu\n",
            reinterpret_cast<void*>(f.base), GetLastError(), sizeof(void*));
        return 2;
    }
    f.reset();
    P99ConsumedOwner owner{};
    Reject reason = Reject::ReadFault;
    std::array<std::array<std::byte, 48>, 13> rows{};
    static_assert(sizeof(rows) == 624);
    const std::vector<std::byte> original(reinterpret_cast<std::byte*>(f.base),
        reinterpret_cast<std::byte*>(f.base + 0x10000));
    check(seh_read_p99_consumed_owner(f.job(), &owner), "valid owner");
    check(seh_read_p99_consumed_owner(f.job(), &owner, &reason) && reason == Reject::None,
        "valid owner clears rejection reason");
    check(owner.player == f.base + 0x1000 && owner.weapon == f.base + 0x9000 &&
        owner.slot == 0 && owner.player_id == 0x123 && owner.weapon_id == 0x456, "identity pins");
    check(seh_copy_p99_consumed_arms(owner, &rows), "valid arms");
    check(std::memcmp(rows.data(), reinterpret_cast<const void*>(f.base + 0xB000 + 18 * 48),
        sizeof(rows)) == 0, "exact rows and NaN W bits");
    check(std::memcmp(original.data(), reinterpret_cast<const void*>(f.base), original.size()) == 0,
        "no fixture bytes written, including weapon+72");
    f.refuse(8, std::uint32_t{0}, Reject::LocalPlayer, "retained zero generation");
    f.refuse(0x10BC, std::uint32_t{0xABC}, Reject::LocalPlayer, "player generation mismatch");
    f.refuse(0x61B0, std::uint32_t{0xABC}, Reject::RetainedFg, "Fg generation mismatch");
    f.refuse(0x90BC, std::uint32_t{0xABC}, Reject::Weapon, "weapon generation mismatch");
    f.refuse(0x94E8, std::uint32_t{0xABC}, Reject::Weapon, "parent generation mismatch");
    f.refuse(0x1170, std::uint32_t{0}, Reject::LocalPlayer, "wrong look");
    f.refuse(0x5010, std::uint32_t{1}, Reject::LocalPlayer, "wrong screen");
    f.refuse(0x2510, std::uint8_t{1}, Reject::LocalPlayer, "cover");
    f.refuse(0x7120, std::int32_t{-1}, Reject::Control, "negative slot");
    f.refuse(0x7120, std::int32_t{18}, Reject::Control, "slot outside conservative extent");
    f.refuse(0x7134, std::uint32_t{1}, Reject::Control, "draw state");
    f.refuse(0x8120, std::uint32_t{1}, Reject::Control, "grenade state");
    f.refuse(0x9860, std::uint32_t{0}, Reject::Weapon, "wrong weapon art");
    f.refuse(0xA104, std::uint32_t{0}, Reject::Skeleton, "wrong skeleton");
    f.refuse(0xA034, std::uint32_t{579}, Reject::Skeleton, "wrong joint count");
    f.refuse(0xA414, std::uint8_t{1}, Reject::NativeJob, "job callback disabled");
    f.refuse(0xA400, std::uintptr_t{1}, Reject::NativeJob, "job state mismatch");
    f.refuse(0xA408, std::uintptr_t{1}, Reject::NativeJob, "job Fg mismatch");
    f.refuse(0x6006, std::uint8_t{1}, Reject::NativeJob, "wrong Fg kind");
    f.refuse(0x49C8, 0.03F, Reject::Motion, "movement");
    f.refuse(0x4A30, std::numeric_limits<float>::quiet_NaN(), Reject::Motion, "nonfinite movement");
    f.reset(); g_local_reload_level.store(true);
    check(!seh_read_p99_consumed_owner(f.job(), &owner, &reason) &&
        reason == Reject::Input, "reload");
    f.reset(); g_net_role_b = false;
    check(!seh_read_p99_consumed_owner(f.job(), &owner, &reason) &&
        reason == Reject::Disabled, "wrong role");
    f.reset(); g_state_net.enabled = false;
    check(!seh_read_p99_consumed_owner(f.job(), &owner, &reason) &&
        reason == Reject::Disabled, "disabled");
    f.reset(); g_module = 0;
    check(!seh_read_p99_consumed_owner(f.job(), &owner, &reason) &&
        reason == Reject::Disabled, "module absent");
    f.reset(); g_cached_local_input_buttons.store(kInputBlockButtonBaseMask | 0x9);
    check(seh_read_p99_consumed_owner(f.job(), &owner, &reason) &&
        reason == Reject::None, "neutral9 with base bit");
    g_cached_local_input_buttons.store(0x10);
    check(!seh_read_p99_consumed_owner(f.job(), &owner, &reason) &&
        reason == Reject::Input, "nonneutral command");
    f.reset(); interference = Interference::turn_over; interfere_at_query = 2;
    check(!seh_read_p99_consumed_owner(f.job(), &owner, &reason) &&
        same_owner(owner, P99ConsumedOwner{}) && reason == Reject::Changed,
        "turnover between closing identity reads");
    f.reset(); interference = Interference::fault_owner_after_query; interfere_at_query = 1;
    check(!seh_read_p99_consumed_owner(f.job(), &owner, &reason) &&
        same_owner(owner, P99ConsumedOwner{}) && reason == Reject::ReadFault,
        "owner SEH differs from field-stage refusal");
    for (auto action : {Interference::turn_over, Interference::change_pose, Interference::fault_after_query}) {
        f.reset(); check(seh_read_p99_consumed_owner(f.job(), &owner), "interference preflight");
        interference = action; trigger_address = f.base + 0xB000 + 18 * 48;
        std::memset(rows.data(), 0xA5, sizeof(rows));
        const std::array<std::array<std::byte, 48>, 13> zero{};
        check(!seh_copy_p99_consumed_arms(owner, &rows) && rows == zero, "reject raced copy and clear output");
    }
    f.reset(); DWORD old = 0;
    VirtualProtect(reinterpret_cast<void*>(f.base + 0xB000), 0x1000, PAGE_NOACCESS, &old);
    check(!seh_read_p99_consumed_owner(f.job(), &owner, &reason) &&
        reason == Reject::Skeleton, "unreadable pose retains field stage");
    f.reset(); P99ConsumedRead r;
    check(!r.range(0, 1) && !r.range(f.base, 0) &&
        !r.range(std::numeric_limits<std::uintptr_t>::max() - 3, 8), "range overflow/null");
    check(!seh_read_p99_consumed_owner(nullptr, &owner, &reason) &&
        reason == Reject::NativeJob &&
        !seh_read_p99_consumed_owner(f.job(), nullptr, &reason) && reason == Reject::Input &&
        !seh_copy_p99_consumed_arms(owner, nullptr), "null arguments");
    control_trace_tests(f);
    std::printf("P99 consumed owner: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
