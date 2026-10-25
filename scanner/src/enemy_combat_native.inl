// Narrow, default-OFF A -> B gameplay-death adapter. Include after the
// enemy_root helpers/metadata bridge. Native arguments are borrowed ONLY for
// their current engine callback; queued events contain numeric identities.
namespace enemy_root::combat_native {
constexpr std::uintptr_t kPirateCleanupSlot = kPirate + 0xD08;
constexpr std::uintptr_t kPirateCleanup = 0x49E670;
constexpr std::uintptr_t kHumanActiveVtable = 0x2949558;
constexpr std::uintptr_t kHumanActiveUpdate = 0x4A0660;
constexpr std::uintptr_t kHumanDeadVtable = 0x2949590;
constexpr std::uintptr_t kHumanDeadEnter = 0x4A0780;
constexpr std::uintptr_t kDirectDamage = 0x1DEE1D0;
constexpr std::uintptr_t kHealthVtable = 0x2BBE128;

// The cleanup virtual's native callers pass RCX=NPC only. Preserve its opaque
// RAX; the decompiler's extra volatile/XMM arguments are not an ABI contract.
using CleanupFunction = std::uintptr_t(__fastcall*)(void*);
// Process state dispatch1416A574A and state entry1416A411C pass RCX=state only.
using StateFunction = void(__fastcall*)(void*);
using CurrentOwnerFunction = void*(__fastcall*)();
using DirectDamageFunction = std::int32_t(__fastcall*)(
    void*, std::int32_t, std::uint8_t, std::int32_t, bool);

std::atomic<bool> hooks_ready{}, installation_attempted{};

bool ready() { return hooks_ready.load(std::memory_order_acquire); }

std::uint64_t current_world() noexcept {
    if constexpr (!player_world::kEnablePlayerWorldRearm) return 0;
    if (!ready() || !combat_enabled() || !combat_ready() ||
        !installed.load(std::memory_order_acquire) ||
        stopped.load(std::memory_order_acquire) || g_dll_inert ||
        stop_for_player_lifecycle() || !scope_current()) return 0;
    const auto epoch = bound_world_epoch.load(std::memory_order_acquire);
    return epoch && player_world::local_epoch() == epoch ? epoch : 0;
}

// SEH guards only the CURRENT native callback argument, not a cached effect
// target. This cheap selector has no pointer publication and cannot authorize
// damage. Full authored identity/page/budget validation follows a due ticket.
bool seh_current_actor(std::uintptr_t actor, NativeLifeEvent& value) noexcept {
    if (!actor) return false;
    __try {
        const auto ptr = [](std::uintptr_t at) {
            return *reinterpret_cast<const volatile std::uintptr_t*>(at);
        };
        const auto id = [](std::uintptr_t at) {
            return *reinterpret_cast<const volatile std::uint32_t*>(at);
        };
        const auto flags = *reinterpret_cast<const volatile std::uint8_t*>(actor + 0x40);
        // +40 bit01 is Process-dead, NOT health-dead: KillTree asserts
        // !IsProcessDead@1416A6030, zeroes BC@6044, then sets01@604D.
        // HumanDead entry is invoked before its optional native KillTree;
        // normal entry therefore still admits a positive BC with both bits0.
        if (ptr(actor) != g_module + kPirate || (flags & 0x05)) return false;
        const auto record = ptr(actor + 0xA8);
        const auto generation = id(actor + 0xBC);
        if (!record || !generation || ptr(record + 8) != actor) return false;
        const auto current = reinterpret_cast<CurrentOwnerFunction>(
            g_module + kThreadContextGetCurrentProcessRva)();
        if (reinterpret_cast<std::uintptr_t>(current) != actor ||
            ptr(actor) != g_module + kPirate || ptr(actor + 0xA8) != record ||
            id(actor + 0xBC) != generation || ptr(record + 8) != actor ||
            (*reinterpret_cast<const volatile std::uint8_t*>(actor + 0x40) & 0x05)) return false;
        value.record = record;
        value.generation = generation;
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
                GetExceptionCode() == EXCEPTION_GUARD_PAGE
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool seh_current_state(void* state, std::uintptr_t expected_vtable,
                       NativeLifeEvent& value, std::uintptr_t& actor) noexcept {
    if (!state) return false;
    __try {
        const auto at = reinterpret_cast<std::uintptr_t>(state);
        const auto ptr = [](std::uintptr_t address) {
            return *reinterpret_cast<const volatile std::uintptr_t*>(address);
        };
        if (ptr(at) != g_module + expected_vtable) return false;
        const auto owner = ptr(at + 8);
        if (!seh_current_actor(owner, value)) return false;
        const auto fsm = ptr(owner + 0xC0);
        if (!fsm || ptr(fsm) != at || ptr(at + 8) != owner ||
            ptr(at) != g_module + expected_vtable) return false;
        actor = owner;
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
                GetExceptionCode() == EXCEPTION_GUARD_PAGE
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool capture_state_event(void* state, NativeLifeEvent& event) noexcept {
    const auto epoch = current_world();
    std::uintptr_t actor{};
    if (!epoch || !seh_current_state(state, kHumanDeadVtable, event, actor) ||
        current_world() != epoch) return false;
    event.kind = NativeLifeKind::dead_commit;
    event.worldEpoch = epoch;
    return true;
}

std::uintptr_t __fastcall hooked_cleanup(void* object) {
    const DWORD entry_error = GetLastError();
    // Restrict the positive pre-cleanup witness to the two proved native
    // Active HP-dead branches. Unknown callers still receive stock once.
    const auto pc = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto epoch = current_world();
    NativeLifeEvent event{};
    if (epoch && (pc == g_module + 0x42D27E || pc == g_module + 0x42D5D3) &&
        seh_current_actor(reinterpret_cast<std::uintptr_t>(object), event) &&
        current_world() == epoch) {
        event.kind = NativeLifeKind::retiring;
        event.worldEpoch = epoch;
        // Metadata-only monotonic publication maps an ALREADY bound tuple;
        // no lock/drop and no unknown tuple adoption or game pointer escapes.
        publish_native_life_event(event);
    }
    SetLastError(entry_error);
    return reinterpret_cast<CleanupFunction>(g_module + kPirateCleanup)(object);
}

void __fastcall hooked_dead_enter(void* state) {
    const DWORD entry_error = GetLastError();
    NativeLifeEvent event{};
    const bool have_event = capture_state_event(state, event);
    SetLastError(entry_error);
    // OnEnter may KillTree its owner. An abnormal native return propagates;
    // there is no success publication and no post-call actor/state read.
    reinterpret_cast<StateFunction>(g_module + kHumanDeadEnter)(state);
    const DWORD native_error = GetLastError();
    if (have_event && current_world() == event.worldEpoch)
        publish_native_life_event(event);
    SetLastError(native_error);
}

// All these observations are lexical to the CURRENT Active callback. The
// returned maximum is the exact native HS+C0 formula (+44 plus +48), with
// overflow/bounds refused. No HP field, mode, scale or policy is written here.
bool validate_damage_target(void* state, std::uintptr_t actor,
                            const NativeDeathAttempt& attempt,
                            ReadCache& cache, std::int32_t& maximum,
                            std::uintptr_t& health) noexcept {
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept {
        return copy(at, &value, sizeof(value), &cache);
    };
    Local local{};
    if (!identity(actor, local, cache) || local.record != attempt.record ||
        local.generation != attempt.generation || !(local.key == attempt.key)) return false;
    const auto at = reinterpret_cast<std::uintptr_t>(state);
    std::uintptr_t state_vt{}, owner{}, fsm{}, current_state{}, health_vt{}, world{};
    std::int32_t hp{}, base{}, bonus{}, mode{};
    std::uint8_t flags{};
    float scale{};
    if (!read(at, state_vt) || state_vt != g_module + kHumanActiveVtable ||
        !read(at + 8, owner) || owner != actor || !read(actor + 0xC0, fsm) || !fsm ||
        !read(fsm, current_state) || current_state != at ||
        !read(actor + 0x40, flags) || (flags & 0x05) ||
        !read(g_module + 0x6917C20, world) || !world ||
        !read(actor + 0x4A8, health) || !health ||
        !read(health, health_vt) || health_vt != g_module + kHealthVtable ||
        !read(health + 0x38, hp) || hp <= 0 ||
        !read(health + 0x44, base) || base <= 0 ||
        !read(health + 0x48, bonus) || bonus < 0 ||
        base > (std::numeric_limits<std::int32_t>::max)() - bonus ||
        !read(health + 0x68, mode) || mode != 0 ||
        !read(health + 0x90, scale) || !std::isfinite(scale) || scale != 1.0f) return false;
    // Native factory141FCBB20 seeds mode0@141FCBBA8, floor+6C=1@BBAB,
    // override+70=1@BBB2 and scale1@BBC7. With exact mode0, damage141FCB210
    // skips ALL floor/override policy branches; only the explicit D00 minimum
    // argument0 clamps health. Do not reject/default-edit unused fields1/1,
    // or broaden admission to modes1/2/3.
    maximum = base + bonus;
    if (hp > maximum) return false;
    // No protection cache or native pointer follows the native damage call.
    // Recheck the late owner/HS tuple after the complete identity/field walk.
    std::uintptr_t health_after{}, actor_after{}, record_after{}, state_after{}, fsm_after{};
    std::uint32_t generation_after{};
    if (!read(actor + 0x4A8, health_after) || health_after != health ||
        !read(actor + 0xA8, record_after) || record_after != attempt.record ||
        !read(attempt.record + 8, actor_after) || actor_after != actor ||
        !read(actor + 0xBC, generation_after) || generation_after != attempt.generation ||
        !read(actor + 0xC0, fsm_after) || fsm_after != fsm ||
        !read(fsm, state_after) || state_after != at ||
        !read(actor + 0x40, flags) || (flags & 0x05)) return false;
    NativeLifeEvent current{};
    std::uintptr_t current_actor{};
    return seh_current_state(state, kHumanActiveVtable, current, current_actor) &&
        current_actor == actor && current.record == attempt.record &&
        current.generation == attempt.generation;
}

bool late_health_matches(std::uintptr_t actor, std::uintptr_t health,
                         std::int32_t maximum, WorkBudget& budget) noexcept {
    // The FLS accessor above was a native call. Do not reuse its predecessor's
    // protection observations across it, even for this final short recheck.
    ReadCache cache{};
    cache.budget = &budget;
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept {
        return copy(at, &value, sizeof(value), &cache);
    };
    std::uintptr_t current{}, vt{};
    std::int32_t hp{}, base{}, bonus{}, mode{};
    float scale{};
    return read(actor + 0x4A8, current) && current == health &&
        read(health, vt) && vt == g_module + kHealthVtable &&
        read(health + 0x38, hp) && hp > 0 && hp <= maximum &&
        read(health + 0x44, base) && base > 0 &&
        read(health + 0x48, bonus) && bonus >= 0 &&
        base <= (std::numeric_limits<std::int32_t>::max)() - bonus && base + bonus == maximum &&
        read(health + 0x68, mode) && mode == 0 &&
        read(health + 0x90, scale) && std::isfinite(scale) && scale == 1.0f;
}

// The native write has no exception conversion. An engine exception propagates
// unchanged; metadata records a fault, never an ACK or a retry permission.
void invoke_damage(void* object, std::int32_t maximum,
                   const NativeDeathAttempt& attempt) {
    // Shared clock/intent publication is metadata only. Its final cutoff is
    // AFTER all current-object reads and immediately before the native call;
    // a normal effect commits the immutable candidate, never resets a clock.
    if (!native_death_attempt_current(attempt)) {
        finish_native_death_attempt(attempt, NativeAttemptStatus::refused);
        return;
    }
    bool returned{};
    __try {
        reinterpret_cast<DirectDamageFunction>(g_module + kDirectDamage)(
            object, maximum, 0, 0, false);
        returned = true;
    } __finally {
        const DWORD native_error = GetLastError();
        finish_native_death_attempt(attempt, returned ? NativeAttemptStatus::native_returned :
                                                     NativeAttemptStatus::native_fault);
        SetLastError(native_error);
    }
}

void service_active(void* state) {
    // A single atomic metadata hint keeps normal B frames free of native
    // tuple/FLS reads and mailbox contention when there is no death to apply.
    if (!g_net_role_b || !native_death_pending()) return;
    const auto epoch = current_world();
    NativeLifeEvent selected{};
    std::uintptr_t actor{};
    if (!epoch || !seh_current_state(state, kHumanActiveVtable, selected, actor)) return;
    NativeDeathAttempt attempt{};
    if (!peek_native_death(selected.record, selected.generation, attempt) ||
        attempt.worldEpoch != epoch || attempt.record != selected.record ||
        attempt.generation != selected.generation) return;
    WorkBudget budget(3);
    if (!budget.check() || !certificate_current(budget.ticket) || current_world() != epoch) return;
    ReadCache cache{};
    cache.budget = &budget;
    std::int32_t maximum{};
    std::uintptr_t health{};
    if (!validate_damage_target(state, actor, attempt, cache, maximum, health) ||
        !budget.check() || !certificate_current(budget.ticket) || current_world() != epoch) return;
    // Only now consume this exact event's one permitted native attempt.
    // Contention/refusal before this point remains eligible for a later fresh
    // callback. After reservation an abort is fail-closed, never an auto-retry.
    if (!try_begin_native_death(attempt)) return;
    NativeLifeEvent final{};
    std::uintptr_t final_actor{};
    if (!late_health_matches(actor, health, maximum, budget) ||
        // The health graph/protection calls may have been delayed. Recheck
        // CURRENT self/state/FSM/weak/enteredOnKill AFTER that complete walk.
        // Its native FLS accessor follows the destroyed lexical health cache;
        // no protection cache is reused across the accessor or damage call.
        !seh_current_state(state, kHumanActiveVtable, final, final_actor) ||
        final_actor != actor || final.record != attempt.record ||
        final.generation != attempt.generation ||
        !budget.check() || !certificate_current(budget.ticket) || current_world() != epoch) {
        finish_native_death_attempt(attempt, NativeAttemptStatus::refused);
        return;
    }
    invoke_damage(reinterpret_cast<void*>(actor), maximum, attempt);
}

void __fastcall hooked_active_update(void* state) {
    const DWORD entry_error = GetLastError();
    service_active(state);
    SetLastError(entry_error);
    // All ordinary refusals still execute the original CURRENT Active once.
    // It owns health-dead -> cleanup -> Human::Dead; no direct Die/state force.
    reinterpret_cast<StateFunction>(g_module + kHumanActiveUpdate)(state);
}

struct HookSlot {
    std::uintptr_t slot, stock;
    void* replacement;
};

bool publish_slot(const HookSlot& hook) noexcept {
    auto** slot = reinterpret_cast<void**>(g_module + hook.slot);
    auto* stock = reinterpret_cast<void*>(g_module + hook.stock);
    DWORD old{};
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    const bool changed = InterlockedCompareExchangePointer(slot, hook.replacement, stock) == stock;
    DWORD ignored{};
    bool sealed = VirtualProtect(slot, sizeof(void*), old, &ignored) != FALSE;
    if (!sealed) sealed = VirtualProtect(slot, sizeof(void*), old, &ignored) != FALSE;
    // Published hooks/stock targets remain resident even on partial failure.
    // hooks_ready stays false, so every such entry is stock forwarding only.
    return changed && sealed;
}

bool install() {
    if (!combat_enabled()) return false;
    if (installation_attempted.exchange(true, std::memory_order_acq_rel)) return ready();
    const std::array<HookSlot, 3> hooks{{
        {kPirateCleanupSlot, kPirateCleanup, reinterpret_cast<void*>(&hooked_cleanup)},
        {kHumanDeadVtable + 0x10, kHumanDeadEnter, reinterpret_cast<void*>(&hooked_dead_enter)},
        {kHumanActiveVtable + 0x20, kHumanActiveUpdate, reinterpret_cast<void*>(&hooked_active_update)},
    }};
    for (const auto& hook : hooks) {
        std::uintptr_t target{};
        if (!read(g_module + hook.slot, target) || target != g_module + hook.stock) return false;
    }
    // Verify the health virtual family used by the stock D00 operation. No
    // target is resolved from an unverified runtime object or writable packet.
    constexpr std::array<std::pair<std::uintptr_t, std::uintptr_t>, 6> health_slots{{
        {0x20, 0x1FCB210}, {0x90, 0x1FCAEB0}, {0xB8, 0x1FCAFA0}, {0xC0, 0x1FCA9C0},
        {0x1B8, 0x1FCA840}, {0x1C0, 0x1FCAE40},
    }};
    std::uintptr_t damage{};
    if (!read(g_module + kPirate + 0xD00, damage) || damage != g_module + kDirectDamage) return false;
    for (const auto& item : health_slots) {
        std::uintptr_t target{};
        if (!read(g_module + kHealthVtable + item.first, target) || target != g_module + item.second) return false;
    }
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&hooked_active_update), &pinned) ||
        reinterpret_cast<std::uintptr_t>(pinned) != g_self_module) return false;
    for (const auto& hook : hooks) if (!publish_slot(hook)) return false;
    hooks_ready.store(true, std::memory_order_release);
    return true;
}
} // namespace enemy_root::combat_native
