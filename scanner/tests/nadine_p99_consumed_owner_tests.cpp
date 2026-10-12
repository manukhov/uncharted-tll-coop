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
template<class T> void put(std::uintptr_t address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
SIZE_T WINAPI fixture_query(LPCVOID address, PMEMORY_BASIC_INFORMATION mbi, SIZE_T size) {
    const auto result = ::VirtualQuery(address, mbi, size);
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
#include "nadine_p99_consumed_owner.inl"
#undef VirtualQuery

namespace {
using Reject = P99ConsumedOwnerReject;
unsigned checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", name); }
}
struct Fixture {
    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(
        VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE));
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
}

int main() {
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
    std::printf("P99 consumed owner: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
