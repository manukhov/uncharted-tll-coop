// STAGED menu-cycle observation. Include after install_peer_lifecycle_callsite.
// These numeric events are NOT a game-memory lease or spawn/reset authority.
// An injection can miss the initial Havok init: open_serial==0 is intentionally
// retained until a real zero -> nonzero transition is observed.
void service_player_world_postrender_tail();

namespace player_world_menu_cycle {
constexpr std::uintptr_t kCloseCall = 0x1B3F29F;
constexpr std::uintptr_t kClose = 0x1B3FEE0;
constexpr std::uintptr_t kInitCall = 0x1B3F2B9;
constexpr std::uintptr_t kInit = 0x1B3F3C0;
constexpr std::uintptr_t kFullPostRenderCall = 0x1678DE;
constexpr std::uintptr_t kSubmitAndWait = 0x15B5210;
constexpr std::uintptr_t kFullPostRender = 0x165B50;

using CloseFunction = void(__fastcall*)(bool);
using InitFunction = void(__fastcall*)();
using CallbackFunction = void(__fastcall*)(std::uintptr_t);
using SubmitFunction = std::uintptr_t(__fastcall*)(
    CallbackFunction, std::uintptr_t, std::uint64_t, std::uintptr_t, int, std::uintptr_t);

struct Snapshot {
    std::uint64_t close_cycle{}, opened_cycle{}, open_serial{}, completed_frame{};
    OwnedRemotePlayerIdentity closed_owned{}; // object always zero; numeric weak key only.
    bool valid{}, transition_inflight{};
};
struct Transition {
    std::uintptr_t before{}; // stack only, never published or dereferenced.
    std::uint64_t prior_open_serial{};
    OwnedRemotePlayerIdentity owned{};
    bool owner{}, readable{};
};
SRWLOCK cycle_lock = SRWLOCK_INIT;
Snapshot observation{};
std::uint64_t next_open_serial{}; // cycle_lock only, never reset on a menu.
std::atomic<std::uint64_t> published_open_serial{};
std::atomic<bool> module_ready{}, installation_attempted{}, transition_busy{}, tail_busy{};

bool ready() { return module_ready.load(std::memory_order_acquire); }
bool seh_read_world(std::uintptr_t& value) {
    __try {
        value = *reinterpret_cast<const volatile std::uintptr_t*>(g_module + kHavokWorldRva);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        value = 0;
        return false;
    }
}
void mark_unknown() {
    AcquireSRWLockExclusive(&cycle_lock);
    observation.valid = false; // permanent fail-closed for this installed collector.
    observation.open_serial = 0;
    published_open_serial.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&cycle_lock);
}
Transition begin_transition(bool closing) {
    Transition ticket{};
    bool free = false;
    ticket.owner = transition_busy.compare_exchange_strong(
        free, true, std::memory_order_acq_rel, std::memory_order_acquire);
    if (!ticket.owner) {
        mark_unknown(); // nested/overlapping native calls still forward exactly once.
        return ticket;
    }
    AcquireSRWLockExclusive(&cycle_lock);
    ticket.prior_open_serial = observation.open_serial;
    observation.transition_inflight = true;
    observation.open_serial = 0;
    published_open_serial.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&cycle_lock);
    ticket.readable = seh_read_world(ticket.before);
    if (closing) {
        copy_owned_remote_player_identity(ticket.owned);
        ticket.owned.object = 0; // never retain a raw allocation across the close.
    }
    // A previously observed open cannot already have disappeared before this
    // exact transition: that would be a missed close, not a no-op event.
    if (!ticket.readable || (ticket.prior_open_serial != 0 && ticket.before == 0))
        mark_unknown();
    return ticket;
}
void finish_transition(const Transition& ticket, bool closing, bool normal) {
    if (!ticket.owner) return;
    std::uintptr_t after{};
    const bool readable = normal && ticket.readable && seh_read_world(after);
    AcquireSRWLockExclusive(&cycle_lock);
    if (!readable) observation.valid = false;
    if (observation.valid) {
        if (closing && ticket.before != 0 && after == 0 &&
            observation.close_cycle != UINT64_MAX) {
            ++observation.close_cycle;
            observation.closed_owned = ticket.owned;
        } else if (!closing && ticket.before == 0 && after != 0 &&
                   next_open_serial != UINT64_MAX) {
            observation.opened_cycle = observation.close_cycle;
            observation.open_serial = ++next_open_serial;
        } else if (!closing && ticket.before != 0 && after == ticket.before) {
            // Native HavokInit can legitimately find an existing world. Keep
            // prior observation, including startup serial0; this is NOT a new
            // observed init and cannot satisfy strict menu-rejoin open proof.
            observation.open_serial = ticket.prior_open_serial;
        } else if (closing && ticket.before == 0 && after == 0) {
            // Already closed is also a legitimate no-op. No new cycle, owned
            // retirement claim or opened_cycle is manufactured by this call.
            observation.open_serial = 0;
        } else {
            // Changed nonzero worlds, counter overflow and contradictory
            // sequencing cannot manufacture a physical cycle.
            observation.valid = false;
        }
    }
    if (!observation.valid) observation.open_serial = 0;
    observation.transition_inflight = false;
    published_open_serial.store(observation.open_serial, std::memory_order_release);
    ReleaseSRWLockExclusive(&cycle_lock);
    transition_busy.store(false, std::memory_order_release);
}
std::uint64_t current_open_serial() {
    if (!ready() || transition_busy.load(std::memory_order_acquire)) return 0;
    const auto value = published_open_serial.load(std::memory_order_acquire);
    return transition_busy.load(std::memory_order_acquire) ? 0 : value;
}
bool copy_snapshot(Snapshot& out) {
    AcquireSRWLockShared(&cycle_lock);
    out = observation;
    const bool valid = ready() && out.valid && !out.transition_inflight &&
        !transition_busy.load(std::memory_order_acquire);
    ReleaseSRWLockShared(&cycle_lock);
    return valid;
}
void complete_frame() {
    AcquireSRWLockExclusive(&cycle_lock);
    if (observation.completed_frame == UINT64_MAX) {
        observation.valid = false;
        observation.open_serial = 0;
        published_open_serial.store(0, std::memory_order_release);
    } else {
        // A joined frame can be observed even when startup init was missed.
        // This is a phase witness, never an invented open event or lifetime lease.
        ++observation.completed_frame;
    }
    ReleaseSRWLockExclusive(&cycle_lock);
}

void __fastcall hooked_close(bool full_shutdown) {
    const auto stock = reinterpret_cast<CloseFunction>(g_module + kClose);
    if (!ready()) { stock(full_shutdown); return; }
    const DWORD entry_error = GetLastError();
    // Close admission BEFORE HavokClose's availability/generation writes and
    // physics teardown, not merely after observing its returned zero world.
    // Even a legitimate already-closed native no-op must not reopen admission.
    player_world::close_for_reset();
    const Transition ticket = begin_transition(true);
    DWORD native_error = entry_error;
    bool normal = false;
    SetLastError(entry_error);
    __try {
        stock(full_shutdown);
        native_error = GetLastError();
        normal = true;
    } __finally {
        const DWORD final_error = normal ? native_error : GetLastError();
        finish_transition(ticket, true, normal);
        SetLastError(final_error);
    }
}
void __fastcall hooked_init() {
    const auto stock = reinterpret_cast<InitFunction>(g_module + kInit);
    if (!ready()) { stock(); return; }
    const DWORD entry_error = GetLastError();
    const Transition ticket = begin_transition(false);
    DWORD native_error = entry_error;
    bool normal = false;
    SetLastError(entry_error);
    __try {
        stock();
        native_error = GetLastError();
        normal = true;
    } __finally {
        const DWORD final_error = normal ? native_error : GetLastError();
        finish_transition(ticket, false, normal);
        SetLastError(final_error);
    }
}
void __fastcall hooked_full_postrender(std::uintptr_t data) {
    const auto stock = reinterpret_cast<CallbackFunction>(g_module + kFullPostRender);
    if (!ready()) { stock(data); return; }
    const DWORD entry_error = GetLastError();
    bool free = false;
    const bool owner = tail_busy.compare_exchange_strong(
        free, true, std::memory_order_acq_rel, std::memory_order_acquire);
    if (!owner) mark_unknown();
    DWORD native_error = entry_error;
    bool stock_returned = false, all_returned = false;
    SetLastError(entry_error);
    __try {
        stock(data); // original full callback exactly once, including its native jobs.
        native_error = GetLastError();
        stock_returned = true;
        if (owner) {
            complete_frame();
            service_player_world_postrender_tail();
        }
        all_returned = true;
    } __finally {
        const DWORD final_error = stock_returned ? native_error : GetLastError();
        if (owner) {
            if (!all_returned) mark_unknown();
            tail_busy.store(false, std::memory_order_release);
        }
        SetLastError(final_error);
    }
}
std::uintptr_t __fastcall hooked_submit_full_postrender(
    CallbackFunction callback, std::uintptr_t data, std::uint64_t route,
    std::uintptr_t file, int line, std::uintptr_t function) {
    const auto stock = reinterpret_cast<SubmitFunction>(g_module + kSubmitAndWait);
    if (!ready() || reinterpret_cast<std::uintptr_t>(callback) != g_module + kFullPostRender)
        return stock(callback, data, route, file, line, function);
    const DWORD entry_error = GetLastError();
    std::uintptr_t result{};
    DWORD native_error = entry_error;
    bool normal = false;
    SetLastError(entry_error);
    __try {
        result = stock(&hooked_full_postrender, data, route, file, line, function);
        native_error = GetLastError();
        normal = true;
    } __finally {
        const DWORD final_error = normal ? native_error : GetLastError();
        if (!normal) mark_unknown();
        SetLastError(final_error);
    }
    return result; // preserve opaque native RAX, not merely AL.
}

bool install() {
    if constexpr (!player_world::kEnablePlayerWorldRearm) return false;
    if (installation_attempted.exchange(true, std::memory_order_acq_rel)) return false;
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&hooked_full_postrender), &pinned) ||
        reinterpret_cast<std::uintptr_t>(pinned) != g_self_module) return false;
    constexpr std::array<std::uint8_t, 5> close_bytes{0xE8, 0x3C, 0x0C, 0, 0};
    constexpr std::array<std::uint8_t, 5> init_bytes{0xE8, 0x02, 0x01, 0, 0};
    constexpr std::array<std::uint8_t, 5> frame_bytes{0xE8, 0x2D, 0xD9, 0x44, 0x01};
    if (std::memcmp(reinterpret_cast<const void*>(g_module + kCloseCall), close_bytes.data(), 5) ||
        std::memcmp(reinterpret_cast<const void*>(g_module + kInitCall), init_bytes.data(), 5) ||
        std::memcmp(reinterpret_cast<const void*>(g_module + kFullPostRenderCall), frame_bytes.data(), 5))
        return false;
    const bool close = install_peer_lifecycle_callsite(
        kCloseCall, kClose, reinterpret_cast<const void*>(&hooked_close), false);
    const bool init = close && install_peer_lifecycle_callsite(
        kInitCall, kInit, reinterpret_cast<const void*>(&hooked_init), false);
    const bool frame = init && install_peer_lifecycle_callsite(
        kFullPostRenderCall, kSubmitAndWait,
        reinterpret_cast<const void*>(&hooked_submit_full_postrender), false);
    const bool installed = close && init && frame;
    if (installed) {
        AcquireSRWLockExclusive(&cycle_lock);
        observation.valid = true; // no bootstrap open serial is fabricated.
        ReleaseSRWLockExclusive(&cycle_lock);
    }
    // Partial installation stays resident, stock-only and unobserved forever.
    module_ready.store(installed, std::memory_order_release);
    return installed;
}
} // namespace player_world_menu_cycle
