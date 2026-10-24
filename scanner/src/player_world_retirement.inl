// STAGED, collection only. Include after the owned-player identity helpers and
// native reset predicate definitions. No code in this file authorizes a spawn,
// clears a leave tombstone, changes the owned registry, or leases game memory.
namespace player_world_retirement {
inline constexpr bool kEnableRetirementObservation = player_world::kEnablePlayerWorldRearm;
constexpr std::uintptr_t kOnKillRva = 0x10D84B0;
constexpr std::uintptr_t kOnKillSlot = kPlayerVtableRva + 0x190;
constexpr std::uintptr_t kUnlinkPostZeroCall = 0x15FCADF;
constexpr std::uintptr_t kHeapValidateRva = 0x15FCEC0;
constexpr std::uintptr_t kHeapClearRva = 0x15FC6A0;

using OnKillFunction = std::uintptr_t(__fastcall*)(void*);
using HeapValidateFunction = std::uintptr_t(__fastcall*)(void*);
using HeapClearFunction = std::uintptr_t(__fastcall*)(void*);

enum class Phase : unsigned {
    kEmpty, kOnKillRunning, kOnKillReturned, kUnlinkValidation, kRetired, kUnknown,
};
struct Snapshot {
    std::uintptr_t record{}, heap{};
    std::uint32_t generation{};
    std::uint64_t serial{};
    Phase phase{Phase::kEmpty};
};
struct Ticket {
    std::uintptr_t record{}, heap{};
    std::uint32_t generation{};
    std::uint64_t serial{};
};
SRWLOCK witness_lock = SRWLOCK_INIT;
Snapshot witness{};
std::uint64_t next_serial{}; // witness_lock only; never reused/reset by a menu.
std::atomic<bool> ready{}, installation_attempted{};
// Metadata-only rejection filter for the common heap-free callsite. A match
// is NOT authority: the locked witness/ticket checks remain mandatory below.
std::atomic<std::uintptr_t> watch_record{}, watch_heap{};

void clear_watch_locked() {
    watch_record.store(0, std::memory_order_release);
    watch_heap.store(0, std::memory_order_relaxed);
}

bool same_identity(const Snapshot& value, const OwnedRemotePlayerIdentity& owned) {
    return value.record == owned.record && value.generation == owned.generation;
}
bool same_ticket(const Snapshot& value, const Ticket& ticket) {
    return ticket.serial != 0 && value.serial == ticket.serial &&
        value.record == ticket.record && value.generation == ticket.generation &&
        value.heap == ticket.heap;
}
Ticket make_ticket(const Snapshot& value) {
    return {value.record, value.heap, value.generation, value.serial};
}

// Only the CURRENT native Player::OnKill argument is inspected. The regular
// owned reset predicate intentionally rejects enteredOnKill; this observer must
// instead REQUIRE that native bit, while BC still contains the captured ID.
bool seh_match_entered_onkill(void* player, const OwnedRemotePlayerIdentity& owned,
                             std::uintptr_t& heap) {
    if (!player || !owned.record || !owned.generation) return false;
    __try {
        const auto address = reinterpret_cast<std::uintptr_t>(player);
        const auto read_pointer = [](std::uintptr_t at) {
            return *reinterpret_cast<const volatile std::uintptr_t*>(at);
        };
        const auto read_id = [](std::uintptr_t at) {
            return *reinterpret_cast<const volatile std::uint32_t*>(at);
        };
        const auto flags = *reinterpret_cast<const volatile std::uint8_t*>(address + 0x40);
        if (read_pointer(address) != g_module + kPlayerVtableRva ||
            read_pointer(address + 0xA8) != owned.record ||
            read_id(address + 0xBC) != owned.generation ||
            !(flags & 0x04) || (flags & 0x01) ||
            read_pointer(owned.record + 8) != address) return false;
        heap = read_pointer(address + 0xA0);
        const auto final_flags = *reinterpret_cast<const volatile std::uint8_t*>(address + 0x40);
        return heap != 0 && read_pointer(address) == g_module + kPlayerVtableRva &&
            read_pointer(address + 0xA8) == owned.record &&
            read_id(address + 0xBC) == owned.generation &&
            (final_flags & 0x04) && !(final_flags & 0x01) &&
            read_pointer(owned.record + 8) == address &&
            read_pointer(address + 0xA0) == heap;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

Ticket arm_onkill(void* player) {
    OwnedRemotePlayerIdentity owned{};
    std::uintptr_t heap{};
    if (!copy_owned_remote_player_identity(owned) ||
        !seh_match_entered_onkill(player, owned, heap)) return {};
    Ticket ticket{};
    AcquireSRWLockExclusive(&witness_lock);
    // A new publication may replace only a completed previous witness. A
    // duplicate/nested OnKill is not allowed to supply another return ticket.
    if ((witness.phase == Phase::kEmpty ||
         (witness.phase == Phase::kRetired && !same_identity(witness, owned))) &&
        next_serial != UINT64_MAX) {
        witness = {owned.record, heap, owned.generation, ++next_serial, Phase::kOnKillRunning};
        ticket = make_ticket(witness);
        // Publish before entering stock OnKill. Its normal return precedes
        // the actual free, so the matching free cannot outrun this filter.
        watch_heap.store(heap, std::memory_order_relaxed);
        watch_record.store(owned.record, std::memory_order_release);
    } else if (witness.phase != Phase::kRetired && witness.phase != Phase::kUnknown) {
        witness.phase = Phase::kUnknown;
        clear_watch_locked();
    }
    ReleaseSRWLockExclusive(&witness_lock);
    return ticket;
}
void finish_onkill(const Ticket& ticket, bool normal) {
    if (!ticket.serial) return;
    AcquireSRWLockExclusive(&witness_lock);
    if (same_ticket(witness, ticket) && witness.phase == Phase::kOnKillRunning) {
        witness.phase = normal ? Phase::kOnKillReturned : Phase::kUnknown;
        if (!normal) clear_watch_locked();
    }
    ReleaseSRWLockExclusive(&witness_lock);
}
Ticket begin_unlink_validation(std::uintptr_t heap, std::uintptr_t record) {
    Ticket ticket{};
    AcquireSRWLockExclusive(&witness_lock);
    if (witness.heap == heap && witness.record == record) {
        if (witness.phase == Phase::kOnKillReturned) {
            witness.phase = Phase::kUnlinkValidation;
            ticket = make_ticket(witness);
        } else if (witness.phase == Phase::kOnKillRunning) {
            // An unexpected free during the callback is not a completed kill.
            witness.phase = Phase::kUnknown;
            clear_watch_locked();
        }
    }
    ReleaseSRWLockExclusive(&witness_lock);
    return ticket;
}
void finish_unlink_validation(const Ticket& ticket, bool normal_and_valid) {
    if (!ticket.serial) return;
    AcquireSRWLockExclusive(&witness_lock);
    if (same_ticket(witness, ticket) && witness.phase == Phase::kUnlinkValidation) {
        witness.phase = normal_and_valid ? Phase::kRetired : Phase::kUnknown;
        clear_watch_locked(); // keep numeric retired history, not an active watch.
    }
    ReleaseSRWLockExclusive(&witness_lock);
}
void invalidate_pending_heap(std::uintptr_t heap) {
    AcquireSRWLockExclusive(&witness_lock);
    if (witness.heap == heap && witness.phase != Phase::kEmpty &&
        witness.phase != Phase::kRetired) {
        witness.phase = Phase::kUnknown;
        clear_watch_locked();
    }
    // An already observed old free remains historical fact after heap reset.
    ReleaseSRWLockExclusive(&witness_lock);
}

std::uintptr_t __fastcall hooked_onkill(void* player) {
    const auto stock = reinterpret_cast<OnKillFunction>(g_module + kOnKillRva);
    if (!ready.load(std::memory_order_acquire)) return stock(player);
    const DWORD entry_error = GetLastError();
    const Ticket ticket = arm_onkill(player);
    std::uintptr_t result{};
    DWORD native_error = entry_error;
    bool normal = false;
    SetLastError(entry_error);
    __try {
        result = stock(player);
        native_error = GetLastError();
        normal = true;
    } __finally {
        const DWORD final_error = normal ? native_error : GetLastError();
        finish_onkill(ticket, normal);
        SetLastError(final_error);
    }
    return result;
}

// The exact call at1415FCADF is common to ALL RelocatableHeap::Free callers.
// Native code has ALREADY zeroed record+0/+8/+20 and still owns heap+D0 until
//1415FCAE5. RDI is copied into otherwise unused RDX by the private relay.
// Nothing here reads record/object/heap memory or calls other game routines.
std::uintptr_t __fastcall hooked_postzero_validate(void* heap, void* record) {
    const auto stock = reinterpret_cast<HeapValidateFunction>(g_module + kHeapValidateRva);
    if (!ready.load(std::memory_order_acquire) ||
        watch_record.load(std::memory_order_acquire) != reinterpret_cast<std::uintptr_t>(record) ||
        watch_heap.load(std::memory_order_relaxed) != reinterpret_cast<std::uintptr_t>(heap))
        return stock(heap); // unrelated frees: no SRW/GetLastError/pointer query.
    const DWORD entry_error = GetLastError();
    const Ticket ticket = begin_unlink_validation(
        reinterpret_cast<std::uintptr_t>(heap), reinterpret_cast<std::uintptr_t>(record));
    std::uintptr_t result{};
    DWORD native_error = entry_error;
    bool normal = false;
    SetLastError(entry_error);
    __try {
        result = stock(heap); // exactly the original validator, not a new probe
        native_error = GetLastError();
        normal = true;
    } __finally {
        const DWORD final_error = normal ? native_error : GetLastError();
        // Native validator's bool is AL; preserve its complete opaque RAX.
        finish_unlink_validation(ticket, normal && static_cast<std::uint8_t>(result) == 1);
        SetLastError(final_error);
    }
    return result;
}
std::uintptr_t __fastcall hooked_heap_clear(void* heap) {
    const auto stock = reinterpret_cast<HeapClearFunction>(g_module + kHeapClearRva);
    if (!ready.load(std::memory_order_acquire)) return stock(heap);
    const DWORD entry_error = GetLastError();
    invalidate_pending_heap(reinterpret_cast<std::uintptr_t>(heap));
    SetLastError(entry_error);
    return stock(heap);
}

// Numeric event only. A positive old free does NOT exclude an unrelated reset,
// publish a new owned actor, establish Havok readiness or authorize a new spawn.
bool copy_retired(const OwnedRemotePlayerIdentity& expected, Snapshot& out) {
    AcquireSRWLockShared(&witness_lock);
    const bool retired = ready.load(std::memory_order_acquire) &&
        witness.phase == Phase::kRetired && same_identity(witness, expected);
    if (retired) out = witness;
    ReleaseSRWLockShared(&witness_lock);
    return retired;
}

struct NativeCall {
    std::uintptr_t rva{}, target{};
    std::array<std::uint8_t, 5> expected{};
    std::uint8_t* address{};
    std::uint8_t* relay{};
    std::int32_t displacement{};
    DWORD old_protection{};
    bool writable{}, protection_owner{};
};
bool prepare_call(NativeCall& site, const void* hook, bool record_context) {
    site.address = reinterpret_cast<std::uint8_t*>(g_module + site.rva);
    if (std::memcmp(site.address, site.expected.data(), site.expected.size()) != 0) return false;
    std::int32_t old_delta{};
    std::memcpy(&old_delta, site.address + 1, sizeof(old_delta));
    if (reinterpret_cast<std::uintptr_t>(site.address + 5) +
            static_cast<std::intptr_t>(old_delta) != g_module + site.target) return false;
    const std::size_t size = record_context ? 17 : 14;
    site.relay = static_cast<std::uint8_t*>(allocate_executable_near(site.address, size));
    if (!site.relay) return false;
    const std::size_t offset = record_context ? 3 : 0;
    if (record_context) {
        constexpr std::uint8_t move[]{0x48, 0x8B, 0xD7}; // mov rdx,rdi
        std::memcpy(site.relay, move, sizeof(move));
    }
    constexpr std::uint8_t jump[]{0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(site.relay + offset, jump, sizeof(jump));
    const auto target = reinterpret_cast<std::uintptr_t>(hook);
    std::memcpy(site.relay + offset + sizeof(jump), &target, sizeof(target));
    const auto delta = reinterpret_cast<std::intptr_t>(site.relay) -
        reinterpret_cast<std::intptr_t>(site.address + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) return false;
    site.displacement = static_cast<std::int32_t>(delta);
    DWORD previous{};
    return VirtualProtect(site.relay, size, PAGE_EXECUTE_READ, &previous) != FALSE &&
        FlushInstructionCache(GetCurrentProcess(), site.relay, size) != FALSE;
}
bool install() {
    if constexpr (!kEnableRetirementObservation || !player_world::kEnablePlayerWorldRearm) return false;
    if (installation_attempted.exchange(true, std::memory_order_acq_rel)) return false;
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&hooked_onkill), &pinned) ||
        reinterpret_cast<std::uintptr_t>(pinned) != g_self_module) return false;
    auto* const onkill_slot = reinterpret_cast<std::uintptr_t*>(g_module + kOnKillSlot);
    if (*onkill_slot != g_module + kOnKillRva) return false;
    std::array<NativeCall, 4> sites{{
        {kUnlinkPostZeroCall, kHeapValidateRva, {0xE8, 0xDC, 0x03, 0, 0}},
        {0x15FC30C, kHeapClearRva, {0xE8, 0x8F, 0x03, 0, 0}},
        {0x15FC67E, kHeapClearRva, {0xE8, 0x1D, 0, 0, 0}},
        {0x21FCC5E, kHeapClearRva, {0xE8, 0x3D, 0xFA, 0x3F, 0xFF}},
    }};
    const auto free_unpublished = [&sites] {
        for (auto& site : sites) if (site.relay) VirtualFree(site.relay, 0, MEM_RELEASE);
    };
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const auto hook = i == 0 ? reinterpret_cast<const void*>(&hooked_postzero_validate)
                                 : reinterpret_cast<const void*>(&hooked_heap_clear);
        if (!prepare_call(sites[i], hook, i == 0)) { free_unpublished(); return false; }
    }
    SuspendedProcessThreads suspended{};
    if (!suspend_other_process_threads(suspended)) { free_unpublished(); return false; }
    bool unchanged = *onkill_slot == g_module + kOnKillRva;
    for (const auto& site : sites)
        unchanged = unchanged && std::memcmp(site.address, site.expected.data(), 5) == 0;
    bool writable = unchanged;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        if (!writable) break;
        auto& site = sites[i];
        // Three native calls share one code page. Changing/restoring each
        // separately would save RWX for the later calls and leave the page RWX.
        const auto page = reinterpret_cast<std::uintptr_t>(site.address) & ~std::uintptr_t{0xFFF};
        for (std::size_t j = 0; j < i; ++j) {
            if (sites[j].protection_owner &&
                (reinterpret_cast<std::uintptr_t>(sites[j].address) & ~std::uintptr_t{0xFFF}) == page) {
                site.writable = true;
                break;
            }
        }
        if (!site.writable) {
            site.writable = VirtualProtect(site.address, 5, PAGE_EXECUTE_READWRITE, &site.old_protection) != FALSE;
            site.protection_owner = site.writable;
        }
        writable = site.writable;
    }
    DWORD old_onkill{}, ignored{};
    const bool slot_writable = writable &&
        VirtualProtect(onkill_slot, sizeof(*onkill_slot), PAGE_READWRITE, &old_onkill) != FALSE;
    if (!slot_writable) {
        for (auto& site : sites) if (site.protection_owner)
            VirtualProtect(site.address, 5, site.old_protection, &ignored);
        resume_suspended_process_threads(suspended);
        free_unpublished();
        return false;
    }
    // From first publication onward, all relays stay resident. A later failure
    // leaves ready=false and exactly-once stock forwarding, never dangling hooks.
    for (auto& site : sites) std::memcpy(site.address + 1, &site.displacement, 4);
    *onkill_slot = reinterpret_cast<std::uintptr_t>(&hooked_onkill);
    bool sealed = true;
    for (auto& site : sites) {
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), site.address, 5) != FALSE;
        sealed = sealed && flushed && site.address[0] == 0xE8 &&
            std::memcmp(site.address + 1, &site.displacement, 4) == 0;
    }
    for (auto& site : sites) if (site.protection_owner) {
        const bool restored = VirtualProtect(site.address, 5, site.old_protection, &ignored) != FALSE;
        sealed = sealed && restored;
    }
    const bool slot_restored = VirtualProtect(onkill_slot, sizeof(*onkill_slot), old_onkill, &ignored) != FALSE;
    sealed = sealed && slot_restored && *onkill_slot == reinterpret_cast<std::uintptr_t>(&hooked_onkill);
    ready.store(sealed, std::memory_order_release);
    resume_suspended_process_threads(suspended);
    return sealed;
}
} // namespace player_world_retirement
