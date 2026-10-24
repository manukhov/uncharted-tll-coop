// Fresh native primary-spawn inputs. This is staged with the world transport:
// no hook below is installed while kEnablePlayerWorldRearm is false. In
// particular, collecting a recipe does NOT authorize a second Player spawn.
namespace player_world_spawn {
constexpr std::uintptr_t kPrimaryCall = 0x13C076A;
constexpr std::uintptr_t kScriptCall = 0xF73C34;
constexpr std::uintptr_t kPersistentCopy = 0x13C07E0;
constexpr std::uintptr_t kObserverResetSlot = 0x2A7E270;
constexpr std::uintptr_t kObserverReset = 0x13C6AF0;
constexpr std::size_t kContinueBytes = 0x88;
constexpr std::size_t kPersistentBytes = 0xE20;

using SpawnFunction = void*(__fastcall*)(
    void*, std::uint32_t, const void*, void*, std::uintptr_t, const void*);
using PersistentCopyFunction = void*(__fastcall*)(void*, const void*);
using ObserverResetFunction = std::uintptr_t(__fastcall*)(void*, unsigned, std::uint8_t);

struct Recipe {
    alignas(16) std::uint8_t continue_values[kContinueBytes]{};
    alignas(16) std::uint8_t persistent_values[kPersistentBytes]{};
    OwnedRemotePlayerIdentity primary{};
    player_world::Key key{};
    std::uint64_t reset_serial{}, capture_serial{}, physical_open_serial{}, physical_close_cycle{};
    bool has_persistent{}, has_task_key{}, valid{};
};
static_assert(kPersistentBytes == 3616);
SRWLOCK recipe_lock = SRWLOCK_INIT;
Recipe recipe{};
std::atomic<std::uint64_t> reset_serial{}, capture_serial{};
std::atomic<unsigned> reset_inflight{};
std::atomic<bool> capture_ready{}, reset_failed{};

// Called only while these inputs are borrowed by a native Spawn call. Nothing
// here retains Continue, CharacterPersistentState, TaskManager or stack pointers.
bool seh_copy_inputs(const void* native_continue, const void* persistent,
                     void* manager, Recipe& out) {
    __try {
        std::memcpy(out.continue_values, native_continue, kContinueBytes);
        if (persistent != nullptr) {
            reinterpret_cast<PersistentCopyFunction>(g_module + kPersistentCopy)(
                out.persistent_values, persistent);
            out.has_persistent = true;
        }
        const auto* values = reinterpret_cast<const std::uint32_t*>(out.continue_values);
        out.key.continue_id04 = values[1];
        out.key.continue_id08 = values[2];
        out.key.wanted_actor_id = values[3];
        out.key.wanted_level_id = values[4];
        out.key.continue_id1c = values[7];
        out.key.spawn_state_id = values[0x6C / 4];
        if (manager != nullptr) {
            const auto address = reinterpret_cast<std::uintptr_t>(manager);
            const auto vtable = *reinterpret_cast<const std::uintptr_t*>(address);
            if (*reinterpret_cast<const std::uintptr_t*>(vtable +
                    kTaskManagerMaintainCompanionsVtableOffset) !=
                        g_module + kTaskManagerMaintainCompanionsRva) return false;
            const auto task = *reinterpret_cast<const std::uintptr_t*>(address + 0x100);
            if (!task) return false;
            out.key.task_id = *reinterpret_cast<const std::uint32_t*>(task + 8);
            out.key.level_key = *reinterpret_cast<const std::uint32_t*>(task + 0x20);
            out.has_task_key = true;
        }
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool seh_bind_primary_output(const void* output, OwnedRemotePlayerIdentity& identity) {
    __try {
        const auto out = reinterpret_cast<std::uintptr_t>(output);
        const auto slot = g_module + kRetainedSlotTableRva;
        identity.record = *reinterpret_cast<const volatile std::uintptr_t*>(out);
        identity.generation = *reinterpret_cast<const volatile std::uint32_t*>(out + 8);
        if (!identity.record || !identity.generation ||
            *reinterpret_cast<const volatile std::uintptr_t*>(slot) != identity.record ||
            *reinterpret_cast<const volatile std::uint32_t*>(slot + 8) != identity.generation) return false;
        identity.object = *reinterpret_cast<const volatile std::uintptr_t*>(identity.record + 8);
        if (!seh_matches_owned_remote_reset_player(reinterpret_cast<void*>(identity.object), identity)) return false;
        return *reinterpret_cast<const volatile std::uintptr_t*>(out) == identity.record &&
            *reinterpret_cast<const volatile std::uint32_t*>(out + 8) == identity.generation &&
            *reinterpret_cast<const volatile std::uintptr_t*>(slot) == identity.record &&
            *reinterpret_cast<const volatile std::uint32_t*>(slot + 8) == identity.generation;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

void publish(Recipe& value) {
    bool published = false;
    AcquireSRWLockExclusive(&recipe_lock);
    if (capture_ready.load(std::memory_order_acquire) &&
        !reset_failed.load(std::memory_order_acquire) &&
        value.reset_serial == reset_serial.load(std::memory_order_acquire)) {
        // A duplicate native retry must not invent another location epoch.
        if (!recipe.valid || recipe.reset_serial != value.reset_serial ||
            recipe.physical_close_cycle != value.physical_close_cycle ||
            recipe.primary.record != value.primary.record ||
            recipe.primary.generation != value.primary.generation) {
            value.capture_serial = capture_serial.fetch_add(1, std::memory_order_relaxed) + 1;
            value.valid = true;
            recipe = value;
            published = true;
        }
    }
    ReleaseSRWLockExclusive(&recipe_lock);
    if (published) {
        // A fresh primary is not interchangeable with the previous scene's
        // snapshots, including a script path that bypassed the reset observer.
        player_world::close_for_reset();
        boot_log_line("PLAYER-WORLD fresh PRIMARY recipe owned: Continue88/PersistentE20; no replay authorized");
    }
}

// The TaskManager seam transfers its live R14 into the unused native R9.
// Both R9 and arg5 are unused in the complete retail callee, but script callers
// still receive an exactly-once call with their original six arguments.
void* capture_call(void* output, std::uint32_t route, const void* native_continue,
                   void* arg4, std::uintptr_t arg5, const void* persistent,
                   bool task_manager_call) {
    const DWORD entry_error = GetLastError();
    Recipe candidate{};
    candidate.reset_serial = reset_serial.load(std::memory_order_acquire);
    candidate.physical_open_serial = player_world_open_serial_for_capture();
    candidate.physical_close_cycle = player_world_close_cycle_for_capture();
    const bool copied = route == 0 && candidate.physical_close_cycle != UINT64_MAX &&
        capture_ready.load(std::memory_order_acquire) &&
        seh_copy_inputs(native_continue, persistent, task_manager_call ? arg4 : nullptr, candidate);
    SetLastError(entry_error);
    void* const result = reinterpret_cast<SpawnFunction>(g_module + kSpawnFromDescriptorRva)(
        output, route, native_continue, task_manager_call ? nullptr : arg4, arg5, persistent);
    const DWORD native_error = GetLastError();
    if (copied && result == output &&
        candidate.reset_serial == reset_serial.load(std::memory_order_acquire) &&
        candidate.physical_open_serial == player_world_open_serial_for_capture() &&
        candidate.physical_close_cycle == player_world_close_cycle_for_capture() &&
        seh_bind_primary_output(output, candidate.primary)) publish(candidate);
    SetLastError(native_error);
    return result;
}
void* __fastcall hooked_primary(void* out, std::uint32_t route, const void* cont,
    void* manager, std::uintptr_t unused, const void* state) {
    return capture_call(out, route, cont, manager, unused, state, true);
}
void* __fastcall hooked_script(void* out, std::uint32_t route, const void* cont,
    void* unused4, std::uintptr_t unused5, const void* state) {
    return capture_call(out, route, cont, unused4, unused5, state, false);
}

std::uintptr_t __fastcall hooked_observer_reset(void* observer, unsigned reason, std::uint8_t flag) {
    const DWORD entry_error = GetLastError();
    // Close before native kills primary, not merely at its later ActorReset call.
    // This is an invalidation boundary, NOT a proof that old workers are drained.
    reset_inflight.fetch_add(1, std::memory_order_acq_rel);
    reset_serial.fetch_add(1, std::memory_order_acq_rel);
    player_world::close_for_reset();
    AcquireSRWLockExclusive(&recipe_lock);
    recipe.valid = false;
    ReleaseSRWLockExclusive(&recipe_lock);
    bool normal = false;
    std::uintptr_t result{};
    DWORD native_error = entry_error;
    SetLastError(entry_error);
    __try {
        result = reinterpret_cast<ObserverResetFunction>(g_module + kObserverReset)(observer, reason, flag);
        native_error = GetLastError();
        normal = true;
    } __finally {
        if (!normal) reset_failed.store(true, std::memory_order_release);
        // Graph Reset still has work after the observer returns; direct actor
        // resets are outside this observer too. Do not publish a "completion"
        // ticket from this counter, including after nested/overlapping calls.
        reset_inflight.fetch_sub(1, std::memory_order_acq_rel);
    }
    SetLastError(native_error);
    return result;
}

// Numeric snapshot only; consumers must independently establish the native
// lifetime/context boundary and rebind primary before any native mutation.
bool copy_recipe(Recipe& out) {
    AcquireSRWLockShared(&recipe_lock);
    const bool valid = recipe.valid && capture_ready.load(std::memory_order_acquire) &&
        !reset_failed.load(std::memory_order_acquire) &&
        recipe.reset_serial == reset_serial.load(std::memory_order_acquire);
    if (valid) out = recipe;
    ReleaseSRWLockShared(&recipe_lock);
    return valid;
}

bool install() {
    if constexpr (!player_world::kEnablePlayerWorldRearm) return false;
    auto* const observer_slot = reinterpret_cast<std::uintptr_t*>(g_module + kObserverResetSlot);
    if (*observer_slot != g_module + kObserverReset) return false;
    std::uint8_t* primary_call{};
    std::uint8_t* primary_relay{};
    std::uint8_t* script_call{};
    std::uint8_t* script_relay{};
    std::int32_t primary_delta{}, script_delta{};
    // Prepare plain14B relay, then allocate a distinct17B context relay. Do not
    // append a register transfer beyond the plain allocation.
    if (!prepare_plain_callsite_relay(kPrimaryCall, kSpawnFromDescriptorRva,
            reinterpret_cast<const void*>(&hooked_primary), primary_call, primary_relay, primary_delta)) return false;
    VirtualFree(primary_relay, 0, MEM_RELEASE);
    primary_relay = static_cast<std::uint8_t*>(allocate_executable_near(primary_call, 17));
    if (!primary_relay) return false;
    constexpr std::uint8_t code[]{0x4D, 0x8B, 0xCE, 0xFF, 0x25, 0, 0, 0, 0}; // mov r9,r14; jmp [rip]
    std::memcpy(primary_relay, code, sizeof(code));
    const auto hook = reinterpret_cast<std::uintptr_t>(&hooked_primary);
    std::memcpy(primary_relay + sizeof(code), &hook, sizeof(hook));
    const auto delta = reinterpret_cast<std::intptr_t>(primary_relay) - reinterpret_cast<std::intptr_t>(primary_call + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) { VirtualFree(primary_relay, 0, MEM_RELEASE); return false; }
    primary_delta = static_cast<std::int32_t>(delta);
    FlushInstructionCache(GetCurrentProcess(), primary_relay, 17);
    if (!prepare_plain_callsite_relay(kScriptCall, kSpawnFromDescriptorRva,
            reinterpret_cast<const void*>(&hooked_script), script_call, script_relay, script_delta)) {
        VirtualFree(primary_relay, 0, MEM_RELEASE); return false;
    }
    SuspendedProcessThreads suspended{};
    if (!suspend_other_process_threads(suspended)) {
        VirtualFree(primary_relay, 0, MEM_RELEASE); VirtualFree(script_relay, 0, MEM_RELEASE); return false;
    }
    constexpr std::uint8_t primary_expected[]{0xE8, 0xA1, 0x11, 0xD3, 0xFF};
    constexpr std::uint8_t script_expected[]{0xE8, 0xD7, 0xDC, 0x17, 0x00};
    DWORD old_primary{}, old_script{}, old_observer{}, ignored{};
    const bool unchanged = *observer_slot == g_module + kObserverReset &&
        std::memcmp(primary_call, primary_expected, 5) == 0 &&
        std::memcmp(script_call, script_expected, 5) == 0;
    const bool p = unchanged && VirtualProtect(primary_call, 5, PAGE_EXECUTE_READWRITE, &old_primary);
    const bool s = p && VirtualProtect(script_call, 5, PAGE_EXECUTE_READWRITE, &old_script);
    const bool o = s && VirtualProtect(observer_slot, sizeof(*observer_slot), PAGE_READWRITE, &old_observer);
    if (!o) {
        if (s) VirtualProtect(script_call, 5, old_script, &ignored);
        if (p) VirtualProtect(primary_call, 5, old_primary, &ignored);
        resume_suspended_process_threads(suspended);
        VirtualFree(primary_relay, 0, MEM_RELEASE); VirtualFree(script_relay, 0, MEM_RELEASE); return false;
    }
    std::memcpy(primary_call + 1, &primary_delta, 4);
    std::memcpy(script_call + 1, &script_delta, 4);
    *observer_slot = reinterpret_cast<std::uintptr_t>(&hooked_observer_reset);
    FlushInstructionCache(GetCurrentProcess(), primary_call, 5);
    FlushInstructionCache(GetCurrentProcess(), script_call, 5);
    const bool restored_primary = VirtualProtect(primary_call, 5, old_primary, &ignored) != FALSE;
    const bool restored_script = VirtualProtect(script_call, 5, old_script, &ignored) != FALSE;
    const bool restored_observer = VirtualProtect(observer_slot, sizeof(*observer_slot), old_observer, &ignored) != FALSE;
    const bool ready = restored_primary && restored_script && restored_observer &&
        std::memcmp(primary_call + 1, &primary_delta, 4) == 0 &&
        std::memcmp(script_call + 1, &script_delta, 4) == 0 &&
        *observer_slot == reinterpret_cast<std::uintptr_t>(&hooked_observer_reset);
    capture_ready.store(ready, std::memory_order_release);
    resume_suspended_process_threads(suspended);
    return ready;
}
} // namespace player_world_spawn
