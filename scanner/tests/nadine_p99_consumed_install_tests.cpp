#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "nadine_p99_consumed_frame.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>

// Only unrelated installer orchestration is stubbed. The actual production
// word capture/CAS/cache/SEH helpers below are compiled and executed unchanged.
constexpr bool kEnableP99ConsumedCensus = false, kEnableP99GroupShadow = false;
constexpr bool kEnableP99SourceObservation = false, kEnableP99WristObservation = false;
constexpr bool kEnableP99WristPreparation = false, kEnableLegacyNadineMazurAuthoredPairRepair = false;
constexpr bool kEnableLegacyNadineMazurSteadyHandsCommandCensus = false;
constexpr bool kEnableLegacyNadineMazurDrawEndpointRepair = false;
constexpr bool kEnableLegacyNadineP99SupportConstraint = false, kEnableLegacyNadineP99ArmRoll = false;
constexpr bool kEnableLegacyNadineP99ReachCensus = false, kEnableLegacyNadineP99ChloeGripTransfer = false;
constexpr bool kEnableLegacyNadineActiveWeaponGripOverlayRepair = false;
constexpr bool kEnableLegacyNadinePistolPoseGripRepair = false;
constexpr std::uintptr_t kProcessAnimCmdListSegmentRva = 0x1AB97C0;
std::uintptr_t g_module = 0;
bool g_enable_aim_case6_probe = false;
std::atomic<bool> g_p99_consumed_ready{false}, g_p99_consumed_failed{false};
bool initialize_p99_consumed_census() { return false; }
std::uintptr_t hooked_p99_consumed_outer(void*) { return 0; }
std::uint32_t hooked_p99_consumed_process(const std::uint32_t*, std::uint32_t,
    const void*, std::uint32_t, const void*, const void*, const void*) { return 0; }
std::int64_t hooked_p99_consumed_post(void*) { return 0; }
void boot_log_line(const char*) {}
void* allocate_executable_near(void*, std::size_t) { return nullptr; }
bool prepare_plain_callsite_relay(std::uintptr_t, std::uintptr_t, const void*,
    std::uint8_t*&, std::uint8_t*&, std::int32_t&) { return false; }
bool prepare_six_byte_indirect_callsite_relay(std::uintptr_t,
    const std::array<std::uint8_t, 6>&, const void*, std::uint8_t*&, std::uint8_t*&,
    std::array<std::uint8_t, 6>&) { return false; }
struct SuspendedProcessThreads { std::array<HANDLE, 1> handles{}; std::size_t count = 0; };
bool suspend_other_process_threads(SuspendedProcessThreads&, DWORD) { return false; }
void resume_suspended_process_threads(SuspendedProcessThreads&) {}

#include "nadine_p99_consumed_install.inl"

unsigned checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", name); }
}
bool all_words(const std::array<P99ConsumedInstallSite, 4>& sites, bool patched) {
    for (const auto& s : sites)
        if (*s.atomic_address != (patched ? s.after_word : s.before_word)) return false;
    return true;
}

int main() {
    namespace frame = coop::p99_consumed_frame;
    check(!install_p99_consumed_census(), "disabled install cannot publish");
    check(!seh_p99_consumed_install_layout(), "null image refused");
    auto* allocation = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x4000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!allocation) return 2;
    std::array<P99ConsumedInstallSite, 4> sites{};
    constexpr std::array<std::size_t, 4> offsets{0, 2, 1, 1};
    constexpr std::array<std::size_t, 4> lengths{7, 5, 5, 6};
    for (std::size_t i = 0; i < sites.size(); ++i) {
        auto& s = sites[i];
        auto* word = allocation + i * 0x1000 + 0x100;
        std::memset(word, 0xA0 + static_cast<int>(i), 8);
        s.address = word + offsets[i]; s.size = lengths[i];
        if (i < 2) s.before = frame::kIngressSites[i].stock;
        else if (i == 2) std::copy(frame::kProcessCallsiteStock.begin(),
            frame::kProcessCallsiteStock.end(), s.before.begin());
        else std::copy(frame::kPostCallsiteStock.begin(), frame::kPostCallsiteStock.end(), s.before.begin());
        s.after = s.before; s.after[1] ^= 0x55;
        std::memcpy(s.address, s.before.data(), s.size);
    }
    check(seh_p99_consumed_install_capture_words(sites), "capture real aligned words");
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const auto* before = reinterpret_cast<const std::uint8_t*>(&sites[i].before_word);
        const auto* after = reinterpret_cast<const std::uint8_t*>(&sites[i].after_word);
        for (std::size_t b = 0; b < 8; ++b)
            if (b < offsets[i] || b >= offsets[i] + lengths[i])
                check(before[b] == after[b], "neighbour bytes unchanged");
        check((reinterpret_cast<std::uintptr_t>(sites[i].atomic_address) & 7u) == 0,
            "atomic destination aligned");
    }
    check(seh_p99_consumed_install_write(sites, true) && all_words(sites, true), "stock to patched");
    check(seh_p99_consumed_install_known_words(sites), "patched words known");
    check(seh_p99_consumed_install_write(sites, false) && all_words(sites, false), "patched to stock");
    check(seh_p99_consumed_install_write(sites, false), "stock rollback idempotent");
    InterlockedExchange64(sites[1].atomic_address, sites[1].after_word);
    check(seh_p99_consumed_install_known_words(sites), "mixed exact states known");
    check(seh_p99_consumed_install_write(sites, true) && all_words(sites, true), "mixed publication idempotent");
    const LONG64 foreign = sites[2].before_word ^ 0x0100000000000000LL;
    InterlockedExchange64(sites[2].atomic_address, foreign);
    check(!seh_p99_consumed_install_write(sites, false), "foreign rollback refused");
    check(*sites[2].atomic_address == foreign, "foreign word never overwritten");
    check(!seh_p99_consumed_install_known_words(sites), "foreign word not classified safe");
    // Test-owned page restoration, not production's response to a foreign word.
    InterlockedExchange64(sites[2].atomic_address, sites[2].before_word);
    check(seh_p99_consumed_install_write(sites, false) && all_words(sites, false), "known rollback after external repair");

    DWORD old = 0, ignored = 0;
    check(VirtualProtect(allocation + 0x2000, 0x1000, PAGE_READONLY, &old) != FALSE,
        "third site fault armed");
    check(!seh_p99_consumed_install_write(sites, true), "real CAS write fault refused");
    check(*sites[0].atomic_address == sites[0].after_word &&
        *sites[1].atomic_address == sites[1].after_word &&
        *sites[2].atomic_address == sites[2].before_word &&
        *sites[3].atomic_address == sites[3].before_word, "fault leaves only complete stock or patched words");
    check(seh_p99_consumed_install_known_words(sites), "mixed fault state has no partial instruction");
    check(VirtualProtect(allocation + 0x2000, 0x1000, old, &ignored) != FALSE,
        "fault protection restored");
    check(seh_p99_consumed_install_write(sites, false) && all_words(sites, false), "rollback after real fault");
    auto invalid = sites;
    invalid[0].address += 2;
    check(!seh_p99_consumed_install_capture_words(invalid), "cross-word patch refused");
    check(VirtualFree(allocation, 0, MEM_RELEASE) != FALSE, "test-owned pages released");
    std::printf("P99 consumed install helpers: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
