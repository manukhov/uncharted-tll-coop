// Included only in the ordinary co-op runtime. No P99/offer/observer pipeline.
// First root-only slice: exact Pirate, unique reciprocal spawner, identity
// parent, normal Perform -> phase40 -> JointBlend, game-driven bodies only.
namespace enemy_root {
namespace wire = coop::enemy_root_sync;
constexpr std::uintptr_t kPirate = 0x294B9D0, kPostblend = 0x1DE9910;
struct Local {
    std::uintptr_t actor{}, record{}, spawner{};
    std::uint32_t generation{};
    wire::Key key{};
    bool operator==(const Local&) const = default;
};
struct Roster {
    std::uintptr_t player{}, manager{}, array{};
    std::uint32_t player_id{}, count{};
    std::array<Local, wire::kActors> rows{};
    bool operator==(const Roster&) const = default;
};
struct Row {
    wire::Packet outgoing{}, incoming{};
    ULONGLONG captured{}, received{}, next_capture{};
    std::uint32_t last_sequence{}, attempted_sequence{};
    bool dirty{};
};
SRWLOCK lock = SRWLOCK_INIT;
Roster roster{};
std::array<Row, wire::kActors> rows{};
std::uint64_t session{}, peer{};
std::uint32_t sequence{}, hello_sequence{};
ULONGLONG peer_seen{}, last_hello{}, last_tick{};
ULONGLONG first_apply{};
bool confirmed{};
// Owned by the serialized tick. Never lose an invalid/unpublished census while
// a native callback holds the mailbox; decide terminal/rebind under that lock.
bool census_gap{};
std::atomic<bool> installed{}, stopped{};
std::atomic<bool> active_scope{};
std::atomic_flag ticking = ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> captured{}, received{}, matched{}, applied{}, rejected{};
std::atomic<unsigned> last_rejection{};
// The network worker owns its socket. It only copies mailbox values: it never
// follows a game pointer, runs census/ordinary, or invokes a native setter.
SOCKET transport_socket = INVALID_SOCKET;
sockaddr_in transport_peer{};
std::atomic<std::uint64_t> next_session{}, published_session{};
std::atomic<ULONGLONG> census_valid_until{};
std::atomic<ULONGLONG> worker_gap_ms{};
std::atomic<std::uint64_t> sent{}, send_dropped{};

// One nonwaiting permit covers ALL synchronous enemy census/native guard work.
// A QPC wall-time cutoff contains repeated work; it cannot preempt one blocked
// OS call/page fault and must never be described as a hard frame-time bound.
std::atomic_flag work_busy = ATOMIC_FLAG_INIT;
std::uint64_t qpc_frequency{}, work_window{}, work_spent{};
unsigned work_queries{};
std::atomic<unsigned> cost_stop{}, cost_site{}, peak_queries{};
std::atomic<std::uint64_t> cost_total_ticks{}, peak_scope_ticks{}, peak_window_ticks{}, query_total{}, work_skips{};
HANDLE stop_request{}, stop_ack{}; // retained for the pinned process lifetime
std::atomic<bool> external_stop{};
std::atomic<bool> native_apply_fault{};
enum : unsigned { kCostClock = 1, kCostTime = 2, kCostQueries = 3 };
void stop_cost(unsigned reason, unsigned site) noexcept {
    unsigned expected{};
    if (cost_stop.compare_exchange_strong(expected, reason)) cost_site.store(site);
    stopped.store(true, std::memory_order_release);
    active_scope.store(false, std::memory_order_release);
}
template<class T> void set_peak(std::atomic<T>& peak, T value) noexcept {
    // Called only by the permit owner; readers are the transport worker.
    if (value > peak.load(std::memory_order_relaxed)) peak.store(value, std::memory_order_relaxed);
}
struct WorkBudget {
    bool owned{};
    unsigned site{};
    std::uint64_t begin{}, last{};
    explicit WorkBudget(unsigned where) noexcept : site(where) {
        owned = !work_busy.test_and_set(std::memory_order_acquire);
        if (!owned) { work_skips.fetch_add(1, std::memory_order_relaxed); return; }
        LARGE_INTEGER now{};
        if (!qpc_frequency || !QueryPerformanceCounter(&now) || now.QuadPart <= 0 ||
            static_cast<std::uint64_t>(now.QuadPart) < work_window) { stop_cost(kCostClock, site); return; }
        begin = last = static_cast<std::uint64_t>(now.QuadPart);
        // Never reset a window mid-scope: a stalled scope stays fully charged.
        if (!work_window || begin-work_window >= qpc_frequency/20) {
            work_window = begin; work_spent = 0; work_queries = 0;
        }
    }
    WorkBudget(const WorkBudget&) = delete;
    WorkBudget& operator=(const WorkBudget&) = delete;
    bool check(bool query = false) noexcept {
        if (!owned || !last) return false;
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now) || now.QuadPart < 0 || static_cast<std::uint64_t>(now.QuadPart) < last) {
            stop_cost(kCostClock, site); return false;
        }
        const auto value = static_cast<std::uint64_t>(now.QuadPart), delta = value-last;
        last = value; work_spent += delta;
        cost_total_ticks.fetch_add(delta, std::memory_order_relaxed);
        set_peak(peak_scope_ticks, value-begin); set_peak(peak_window_ticks, work_spent);
        if (work_spent >= qpc_frequency/1000) { stop_cost(kCostTime, site); return false; }
        if (stopped.load(std::memory_order_acquire)) return false;
        if (query) {
            if (work_queries == 256) { stop_cost(kCostQueries, site); return false; }
            ++work_queries; query_total.fetch_add(1, std::memory_order_relaxed); set_peak(peak_queries, work_queries);
        }
        return true;
    }
    void finish() noexcept {
        if (owned) { check(); owned = false; work_busy.clear(std::memory_order_release); }
    }
    ~WorkBudget() noexcept { finish(); }
};

DWORD WINAPI stop_waiter(void*) {
    // No logging, sockets, RNG or game pointers on this emergency path.
    while (!stopped.load(std::memory_order_acquire)) {
        const DWORD state = WaitForSingleObject(stop_request, 50);
        if (state != WAIT_TIMEOUT) {
            external_stop.store(true);
            stopped.store(true, std::memory_order_release);
            active_scope.store(false, std::memory_order_release);
        }
    }
    // ACK means all admitted game work (including a setter already in flight)
    // has left. New scopes recheck stopped after acquiring this same permit.
    while (work_busy.test_and_set(std::memory_order_acquire)) Sleep(1);
    active_scope.store(false, std::memory_order_release);
    SetEvent(stop_ack);
    work_busy.clear(std::memory_order_release);
    return 0;
}
bool start_stop_control() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return false;
    const auto stamp = (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
    char request_name[128]{}, ack_name[128]{};
    std::snprintf(request_name, sizeof(request_name), "Local\\TllEnemyRootStop_v1_%lu_%016llX", GetCurrentProcessId(), stamp);
    std::snprintf(ack_name, sizeof(ack_name), "%s_ACK", request_name);
    const auto fail = [] {
        if (stop_request) CloseHandle(stop_request);
        if (stop_ack) CloseHandle(stop_ack);
        stop_request = stop_ack = nullptr; return false;
    };
    stop_request = CreateEventA(nullptr, TRUE, FALSE, request_name);
    if (!stop_request || GetLastError() == ERROR_ALREADY_EXISTS) return fail();
    stop_ack = CreateEventA(nullptr, TRUE, FALSE, ack_name);
    if (!stop_ack || GetLastError() == ERROR_ALREADY_EXISTS) return fail();
    const HANDLE thread = CreateThread(nullptr, 0, stop_waiter, nullptr, 0, nullptr);
    if (!thread) return fail();
    CloseHandle(thread);
    return true;
}

struct ReadCache {
    struct Region { std::uintptr_t begin{}, end{}; };
    std::array<Region, 64> regions{};
    std::size_t count{};
    WorkBudget* budget{};
};
// Owned initial observations, not atomic snapshots or native lifetime leases.
// Keep exact type/OnKill gates before copying wider actor fields. Final native
// identity reads below remain separate, after name/eligibility processing.
struct RetainedEntry { std::uintptr_t record; std::uint32_t id, pad; };
static_assert(sizeof(RetainedEntry) == 0x10 && offsetof(RetainedEntry, id) == 8);
struct ActorIdentityFields {
    std::uintptr_t record;                         // actor+A8
    std::byte to_generation[0xBC-0xB0];
    std::uint32_t generation;                     // actor+BC
    std::byte to_desired[0xD8-0xC0];
    std::uint32_t desired_id, variant_id;          // actor+D8/DC
};
static_assert(sizeof(ActorIdentityFields) == 0x38);
static_assert(offsetof(ActorIdentityFields, generation) == 0x14);
static_assert(offsetof(ActorIdentityFields, desired_id) == 0x30 &&
              offsetof(ActorIdentityFields, variant_id) == 0x34);
struct SpawnerIdentityFields {
    std::uintptr_t name;                           // spawner+20
    std::uint32_t spawner_id;                      // spawner+28
    std::byte to_record[4];
    std::uintptr_t record;                         // spawner+30
    std::uint32_t generation;                     // spawner+38
    std::byte to_flags[0x51-0x3C];
    std::uint8_t flags;                            // spawner+51
};
static_assert(offsetof(SpawnerIdentityFields, spawner_id) == 8);
static_assert(offsetof(SpawnerIdentityFields, record) == 0x10 &&
              offsetof(SpawnerIdentityFields, generation) == 0x18);
static_assert(offsetof(SpawnerIdentityFields, flags) == 0x31);
// Do not read the C++ structure's trailing alignment padding past spawner+51.
constexpr std::size_t kSpawnerIdentitySpan = offsetof(SpawnerIdentityFields, flags)+1;
static_assert(kSpawnerIdentitySpan == 0x32 && kSpawnerIdentitySpan <= sizeof(SpawnerIdentityFields));
bool copy(std::uintptr_t at, void* out, std::size_t size, ReadCache* cache = nullptr) noexcept {
    auto* budget = cache ? cache->budget : nullptr;
    if (budget && !budget->check()) return false;
    if (!size || size > 4096 || at < 0x10000 || at > 0x7FFFFFFFFFFFULL-size) return false;
    for (auto cursor = at; cursor < at+size;) {
        bool found{};
        if (cache) for (std::size_t i = 0; i < cache->count; ++i) {
            if (cache->regions[i].begin <= cursor && cursor < cache->regions[i].end) {
                cursor = cache->regions[i].end; found = true; break;
            }
        }
        if (found) continue;
        MEMORY_BASIC_INFORMATION m{};
        if (budget && !budget->check(true)) return false;
        const auto queried = VirtualQuery(reinterpret_cast<void*>(cursor), &m, sizeof(m));
        if (budget && !budget->check()) return false;
        if (queried != sizeof(m) || m.State != MEM_COMMIT ||
            (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(m.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
        const auto end = reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;
        if (end <= cursor) return false;
        if (cache && cache->count < cache->regions.size()) cache->regions[cache->count++] = {reinterpret_cast<std::uintptr_t>(m.BaseAddress), end};
        cursor = end;
    }
    bool copied{};
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), size); copied = true; }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
        GetExceptionCode() == EXCEPTION_GUARD_PAGE ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
    return (!budget || budget->check()) && copied;
}
template<class T> bool read(std::uintptr_t at, T& out) noexcept { return copy(at, &out, sizeof(out)); }
// Admission helpers must share their caller's lexical cache. An optional local
// fallback eagerly zeroed ~1KiB on every call, even when it was never selected.
std::uintptr_t current_player(ReadCache& cache) noexcept {
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), &cache); };
    RetainedEntry entry, after;
    std::uintptr_t actor{}, vt{}, actor_after{};
    std::uint32_t actor_id{}, actor_id_after{}; std::uint8_t kill_flags{};
    // ProcessKillTree1416A5E2A marks enteredOnKill before virtual+190 cleanup;
    // generation+BC remains valid until1416A6044. Identity alone is not live admission.
    if (!read(g_module+kRetainedSlotTableRva, entry) || !entry.record || !entry.id ||
        !read(entry.record+8, actor) || !read(actor, vt) || vt != g_module+kPlayerVtableRva ||
        !read(actor+0x40, kill_flags) || (kill_flags & 0x04) || !read(actor+0xBC, actor_id) || actor_id != entry.id ||
        !read(g_module+kRetainedSlotTableRva, after) || after.record != entry.record || after.id != entry.id ||
        !read(entry.record+8, actor_after) || actor_after != actor ||
        !read(actor+0xBC, actor_id_after) || actor_id_after != entry.id ||
        !read(actor+0x40, kill_flags) || (kill_flags & 0x04)) return 0;
    return actor;
}
bool identity(std::uintptr_t actor, Local& out, ReadCache& cache) noexcept {
    // Lexical cache, discarded before any native callback or mailbox handoff.
    // Every actual copy still has memory-fault SEH; this is not a page lease.
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), &cache); };
    Local value{}; value.actor = actor;
    std::uintptr_t vtable{}, bound_actor{}; std::uint8_t kill_flags{};
    if (!read(actor, vtable) || vtable != g_module+kPirate ||
        !read(actor+0x40, kill_flags) || (kill_flags & 0x04)) return false;
    ActorIdentityFields actor_fields; SpawnerIdentityFields spawner_fields;
    if (!read(actor+0xA8, actor_fields) || !actor_fields.record || !actor_fields.generation ||
        !read(actor+0x150, value.spawner) || !value.spawner ||
        !read(actor_fields.record+8, bound_actor) || bound_actor != actor ||
        !copy(value.spawner+0x20, &spawner_fields, kSpawnerIdentitySpan, &cache) ||
        spawner_fields.record != actor_fields.record || spawner_fields.generation != actor_fields.generation ||
        (spawner_fields.flags & 0x18)) return false;
    value.record = actor_fields.record; value.generation = actor_fields.generation;
    value.key.desired_id = actor_fields.desired_id; value.key.variant_id = actor_fields.variant_id;
    value.key.spawner_id = spawner_fields.spawner_id;
    const auto name = spawner_fields.name;
    bool ended{};
    for (std::size_t i = 0; i < value.key.name.size();) {
        // Windows AMD64 base pages are4KiB. Stop at each page boundary so a
        // terminator before an unreadable next page remains valid. copy() still
        // checks protection/SEH; do not query the same name region twice.
        const auto available = std::size_t{0x1000}-((name+i)&0xFFF);
        const auto size = std::min(available, value.key.name.size()-i);
        if (!copy(name+i, value.key.name.data()+i, size, &cache)) return false;
        for (std::size_t j = i; j < i+size; ++j) if (!value.key.name[j]) {
            std::fill(value.key.name.begin()+j, value.key.name.end(), '\0'); ended = true; break;
        }
        if (ended) break;
        i += size;
    }
    std::uintptr_t name_after{}, spawner_after{}, actor_after{}; std::uint32_t id_after{};
    if (!ended || !wire::valid_key(value.key) || !read(value.spawner+0x20, name_after) || name_after != name ||
        !read(actor+0x150, spawner_after) || spawner_after != value.spawner ||
        !read(value.record+8, actor_after) || actor_after != actor ||
        !read(actor+0xBC, id_after) || id_after != value.generation ||
        !read(actor+0x40, kill_flags) || (kill_flags & 0x04)) return false;
    out = value; return true;
}
bool census(Roster& out, WorkBudget& budget) noexcept {
    // One synchronous read pass, no game callbacks or pointer publication.
    // Reuse region checks inside this pass, never across ticks/native phases.
    ReadCache cache{}; cache.budget = &budget;
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), &cache); };
    Roster value{};
    value.player = current_player(cache);
    if (!value.player || !read(value.player+0xBC, value.player_id) || !value.player_id ||
        !read(g_module+0x3898630, value.manager) || !read(value.manager, value.array)) return false;
    std::uint32_t count{};
    if (!read(value.manager+0xC, count) || count > 64 || (count && !value.array)) return false;
    for (std::uint32_t index = 0; index < count; ++index) {
        struct Entry { std::uintptr_t record; std::uint32_t id, pad; } entry{};
        std::uintptr_t actor{}, vt{}; std::uint32_t id{};
        const auto at = value.array+16*index;
        if (!read(at, entry) || !entry.record || !read(entry.record+8, actor) || !read(actor, vt) || !read(actor+0xBC, id) || id != entry.id) return false;
        if (vt != g_module+kPirate) continue;
        if (value.count == value.rows.size()) return false;
        Local local{};
        if (!identity(actor, local, cache) || local.record != entry.record || local.generation != entry.id) return false;
        for (std::uint32_t previous = 0; previous < value.count; ++previous)
            if (value.rows[previous].key == local.key || value.rows[previous].actor == actor) return false;
        value.rows[value.count++] = local;
    }
    std::uintptr_t manager{}, array{}; std::uint32_t after{};
    if (!read(g_module+0x3898630, manager) || manager != value.manager || !read(manager, array) || array != value.array ||
        !read(manager+0xC, after) || after != count) return false;
    // CharacterManager order is not identity; swapping player rows must not
    // invalidate a stable encounter. Never use this order to match peers.
    std::sort(value.rows.begin(), value.rows.begin()+value.count, [](const Local& a, const Local& b) { return a.actor < b.actor; });
    out = value; return true;
}
void reset_locked() {
    active_scope.store(false, std::memory_order_release);
    census_valid_until.store(0, std::memory_order_release);
    published_session.store(0, std::memory_order_release);
    session = peer = 0; confirmed = false; peer_seen = last_hello = 0; hello_sequence = 0;
    roster = {}; rows = {};
}
void receive(const void* bytes, int size, const sockaddr_in& source, int source_size, ULONGLONG poll_started) {
    if (size != sizeof(wire::Packet) || source_size < sizeof(source) ||
        source.sin_family != transport_peer.sin_family || source.sin_addr.s_addr != transport_peer.sin_addr.s_addr ||
        source.sin_port != transport_peer.sin_port) return;
    wire::Packet packet{}; std::memcpy(&packet, bytes, sizeof(packet));
    if (!wire::valid(packet) || packet.role_b == static_cast<std::uint8_t>(g_net_role_b)) return;
    const auto now = GetTickCount64();
    if (now-poll_started > 100) {
        worker_gap_ms.store(now-poll_started, std::memory_order_relaxed);
        stopped.store(true); active_scope.store(false, std::memory_order_release); return;
    }
    // Enemy UDP is lossy; never wait for a native animation job.
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    if (!session || stopped.load(std::memory_order_relaxed)) { ReleaseSRWLockExclusive(&lock); return; }
    if (packet.kind == wire::Kind::hello) {
        if (hello_sequence && now-peer_seen < 2000 && !wire::newer(packet.sequence, hello_sequence)) {
            ReleaseSRWLockExclusive(&lock); return;
        }
        if (peer != packet.sender) {
            if (first_apply) {
                stopped.store(true); active_scope.store(false); ReleaseSRWLockExclusive(&lock); return;
            }
            peer = packet.sender; confirmed = false;
            for (auto& row : rows) { row.incoming = {}; row.received = 0; row.last_sequence = row.attempted_sequence = 0; }
        }
        hello_sequence = packet.sequence; peer_seen = now;
        confirmed = packet.receiver == session;
    } else if (g_net_role_b && confirmed && packet.sender == peer && packet.receiver == session && now-peer_seen <= 1000) {
        received.fetch_add(1, std::memory_order_relaxed);
        for (std::uint32_t i = 0; i < roster.count; ++i) if (roster.rows[i].key == packet.key) {
            auto& row = rows[i];
            if (!row.last_sequence || wire::newer(packet.sequence, row.last_sequence)) {
                row.incoming = packet; row.received = now; row.last_sequence = packet.sequence;
                matched.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
void tick() {
    if (!installed.load(std::memory_order_acquire) || !g_state_net.enabled || !g_state_net.peer_set) return;
    if (stopped.load()) {
        active_scope.store(false, std::memory_order_release);
        return;
    }
    if (ticking.test_and_set()) return;
    const auto now = GetTickCount64();
    if (now-last_tick < 50) { ticking.clear(); return; }
    WorkBudget budget(1);
    // No observation was attempted on contention. Do not refresh the existing
    // 150ms lease or last_tick; the next game update can try without waiting.
    if (!budget.check()) { ticking.clear(); return; }
    last_tick = now;
    Roster current{};
    const bool valid = census(current, budget) && budget.check();
    if (!valid) {
        census_gap = true;
        active_scope.store(false, std::memory_order_release);
        if (applied.load(std::memory_order_acquire)) stopped.store(true);
    }
    // This runs from Player/Nadine update: never wait for a receiver/native job.
    // An unpublished census must not leave the writer enabled until next tick.
    if (!TryAcquireSRWLockExclusive(&lock)) {
        census_gap = true;
        active_scope.store(false, std::memory_order_release);
        ticking.clear(); return;
    }
    if (census_gap) {
        // An in-flight first apply may have completed after the failed try-lock.
        // Once the lock is ours, it cannot race this decision. Before any apply
        // reset the session; after one apply the gap is terminal, never rearm.
        if (applied.load(std::memory_order_acquire)) stopped.store(true);
        reset_locked(); census_gap = false;
    }
    if (session && now > census_valid_until.load(std::memory_order_acquire) && applied.load()) {
        stopped.store(true); reset_locked();
    }
    if (stopped.load()) {
        ReleaseSRWLockExclusive(&lock); ticking.clear(); return;
    }
    if (!valid) {
        if (applied.load(std::memory_order_relaxed)) stopped.store(true);
        reset_locked();
    }
    else {
        if (session && current != roster && applied.load(std::memory_order_relaxed)) {
            // First experiment never silently rebinds across a death/load/spawn.
            stopped.store(true); reset_locked();
        } else if (!session || current != roster) {
            reset_locked();
            // A worker-prepared nonce, not a crypto-provider call in game update.
            // No nonce yet means no session; a later census may try again.
            session = next_session.exchange(0, std::memory_order_acq_rel);
            if (session) {
                roster = current;
                published_session.store(session, std::memory_order_release);
            }
        }
    }
    const bool within_budget = budget.check();
    census_valid_until.store(valid && session && within_budget ? now+150 : 0, std::memory_order_release);
    active_scope.store(session && confirmed && now-peer_seen <= 1000 && within_budget && !stopped.load(), std::memory_order_release);
    ReleaseSRWLockExclusive(&lock);
    ticking.clear();
}

DWORD WINAPI transport_worker(void*) {
    // Worker-private file: do not contend on the baseline g_log_lock while
    // WriteFile blocks. The game threads never call this logger or own its lock.
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), R"(C:\Users\Vladimir\Desktop\TLLCoopResearch\reports\enemy_root_%lu.log)", GetCurrentProcessId());
    const HANDLE log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto write_status = [log](const std::string& line) {
        if (log == INVALID_HANDLE_VALUE) return;
        const auto record = line+'\n'; DWORD written{};
        WriteFile(log, record.data(), static_cast<DWORD>(record.size()), &written, nullptr);
    };
    struct Pending { wire::Packet packet; ULONGLONG captured; };
    ULONGLONG last_send{}, last_log{}, last_poll{};
    while (!stopped.load(std::memory_order_acquire) && g_state_net.running.load()) {
        if (!next_session.load(std::memory_order_acquire)) {
            std::uint64_t nonce{};
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&nonce), sizeof(nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || !nonce) {
                stopped.store(true); break;
            }
            next_session.store(nonce, std::memory_order_release);
        }
        if (!installed.load(std::memory_order_acquire)) { Sleep(10); continue; }
        const auto poll_started = GetTickCount64();
        // A slow provider, file write or scheduler must not turn a queued old
        // datagram into a fresh root. No draining/rearming after a worker gap.
        if (last_poll && poll_started-last_poll > 100) {
            worker_gap_ms.store(poll_started-last_poll, std::memory_order_relaxed);
            stopped.store(true); active_scope.store(false, std::memory_order_release); break;
        }
        last_poll = poll_started;
        // Bounded draining, even under unrelated or malformed UDP traffic.
        // WSAEWOULDBLOCK is normal; always sleep before another poll.
        for (unsigned i = 0; i < 64 && !stopped.load(); ++i) {
            wire::Packet packet{}; sockaddr_in source{}; int source_size = sizeof(source);
            const int bytes = recvfrom(transport_socket, reinterpret_cast<char*>(&packet), sizeof(packet), 0,
                reinterpret_cast<sockaddr*>(&source), &source_size);
            if (bytes == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK) break;
                if (error == WSAEMSGSIZE || error == WSAECONNRESET) continue;
                stopped.store(true); break;
            }
            receive(&packet, bytes, source, source_size, poll_started);
        }
        const auto now = GetTickCount64();
        std::array<Pending, wire::kActors+1> pending{}; std::size_t count{};
        if (now-last_send >= 50 && TryAcquireSRWLockExclusive(&lock)) {
            last_send = now;
            const bool fresh = session && now <= census_valid_until.load(std::memory_order_acquire) && !stopped.load();
            if (fresh && now-last_hello >= 200) {
                wire::Packet hello{}; hello.role_b = static_cast<std::uint8_t>(g_net_role_b);
                hello.sender = session; hello.receiver = peer; hello.sequence = ++sequence;
                pending[count++] = {hello, now}; last_hello = now;
            }
            if (fresh && active_scope.load(std::memory_order_acquire) && confirmed && peer && now-peer_seen <= 1000 && !g_net_role_b) {
                for (std::uint32_t i = 0; i < roster.count; ++i) {
                    auto& row = rows[i];
                    if (!row.dirty) continue;
                    row.dirty = false;
                    if (now-row.captured > 150) continue;
                    auto packet = row.outgoing;
                    packet.sender = session; packet.receiver = peer; packet.sequence = ++sequence;
                    pending[count++] = {packet, row.captured};
                }
            }
            const auto local_count = roster.count; const bool paired = confirmed;
            ReleaseSRWLockExclusive(&lock);
            // No lock or game pointer survives into any socket/log/provider call.
            // A full send buffer drops this snapshot, never queues/retries it.
            for (std::size_t i = 0; i < count; ++i) {
                const auto& item = pending[i]; const auto send_now = GetTickCount64();
                if (stopped.load() || item.packet.sender != published_session.load(std::memory_order_acquire) ||
                    send_now > census_valid_until.load(std::memory_order_acquire) || send_now-item.captured > 150 ||
                    (item.packet.kind == wire::Kind::root && !active_scope.load(std::memory_order_acquire))) continue;
                const int bytes = sendto(transport_socket, reinterpret_cast<const char*>(&item.packet), sizeof(item.packet), 0,
                    reinterpret_cast<const sockaddr*>(&transport_peer), sizeof(transport_peer));
                if (bytes == sizeof(item.packet)) sent.fetch_add(1, std::memory_order_relaxed);
                else send_dropped.fetch_add(1, std::memory_order_relaxed);
            }
            if (now-last_log >= 2000) {
                last_log = now; std::ostringstream line;
                line << "ENEMY-ROOT role=" << (g_net_role_b ? 'B' : 'A') << " actors=" << local_count << " paired=" << paired
                     << " capture=" << captured.load() << " recv=" << received.load() << " match=" << matched.load()
                     << " apply=" << applied.load() << " reject=" << rejected.load() << " sent=" << sent.load()
                     << " sendDrop=" << send_dropped.load() << " lastGate=" << last_rejection.load() << " stopped=" << stopped.load()
                     << " costStop=" << cost_stop.load() << " costSite=" << cost_site.load()
                     << " peakScopeUs=" << peak_scope_ticks.load()*1000000/qpc_frequency
                     << " peakWindowUs=" << peak_window_ticks.load()*1000000/qpc_frequency
                     << " queries=" << query_total.load() << " peakQueries=" << peak_queries.load() << " workSkips=" << work_skips.load();
                write_status(line.str());
            }
        }
        Sleep(10);
    }
    stopped.store(true, std::memory_order_release);
    active_scope.store(false, std::memory_order_release);
    closesocket(transport_socket); WSACleanup();
    std::ostringstream line; line << "ENEMY-ROOT STOPPED apply=" << applied.load() << " match=" << matched.load()
        << " workerGapMs=" << worker_gap_ms.load()
        << " external=" << external_stop.load() << " nativeFault=" << native_apply_fault.load()
        << " costStop=" << cost_stop.load() << " costSite=" << cost_site.load()
        << " peakScopeUs=" << peak_scope_ticks.load()*1000000/qpc_frequency
        << " peakWindowUs=" << peak_window_ticks.load()*1000000/qpc_frequency
        << " queries=" << query_total.load() << " peakQueries=" << peak_queries.load()
        << "; native AI/root resumes; no automatic rearm";
    write_status(line.str());
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    return 0;
}

bool start_transport() {
    sockaddr_in local{}; int size = sizeof(local);
    if (!g_state_net.enabled || !g_state_net.peer_set ||
        getsockname(g_state_net.recv_socket, reinterpret_cast<sockaddr*>(&local), &size) == SOCKET_ERROR ||
        size != sizeof(local) || local.sin_family != AF_INET) return false;
    const auto local_port = ntohs(local.sin_port), peer_port = ntohs(g_state_net.peer_addr.sin_port);
    if ((local_port != coop::kStatePort && local_port != coop::kStatePort+1) ||
        (peer_port != coop::kStatePort && peer_port != coop::kStatePort+1)) return false;
    // Derive ports from actual binds, NOT role B: on separate PCs both state
    // sockets bind31338. Only this socket becomes nonblocking; baseline is intact.
    local.sin_port = htons(local_port+2);
    transport_peer = g_state_net.peer_addr; transport_peer.sin_port = htons(peer_port+2);
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) return false;
    transport_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const auto fail = [] {
        if (transport_socket != INVALID_SOCKET) closesocket(transport_socket);
        transport_socket = INVALID_SOCKET; WSACleanup(); return false;
    };
    if (transport_socket == INVALID_SOCKET) return fail();
    BOOL exclusive = TRUE; u_long nonblocking = 1;
    if (setsockopt(transport_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) ||
        ioctlsocket(transport_socket, FIONBIO, &nonblocking) ||
        bind(transport_socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local))) return fail();
    const HANDLE thread = CreateThread(nullptr, 0, transport_worker, nullptr, 0, nullptr);
    if (!thread) return fail();
    CloseHandle(thread); // pinned module; worker alone closes its socket on stop
    std::ostringstream line; line << "ENEMY-ROOT dedicated nonblocking UDP local=" << local_port+2 << " peer=" << peer_port+2;
    boot_log_line(line.str().c_str());
    return true;
}

void apply_root_and_release(void* object, const void* transform, WorkBudget* budget) {
    using SetLocalRoot = void(__fastcall*)(void*, const void*);
    // Finally cleans up even if an outer native SEH handler resumes the game.
    // Do NOT catch/translate the game's exception or count a partial write as
    // success. Normal return leaves the permit with the enclosing scope.
    __try {
        reinterpret_cast<SetLocalRoot>(g_module+0x1E7D0D0)(object, transform);
        applied.fetch_add(1, std::memory_order_relaxed);
    } __finally {
        ReleaseSRWLockExclusive(&lock);
        if (AbnormalTermination()) {
            native_apply_fault.store(true);
            stopped.store(true, std::memory_order_release);
            active_scope.store(false, std::memory_order_release);
            budget->finish();
        }
    }
}

bool ordinary(std::uintptr_t actor, std::uintptr_t job, std::array<float, 8>& transform, unsigned& gate, ReadCache& cache) noexcept {
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), &cache); };
    gate = 1;
    std::uintptr_t fg{}, record{}, record_actor{}, owner_record{}, control{}, fg_control{}, owner_control{}, control_fg{};
    std::uintptr_t job_fg{}, custom{}, art{}, active_art{}, cache_art{}, raw{}, art_raw{}, composite{}, composite_owner{}, bodies{};
    std::uint32_t id{}, fg_id{}, flags{}, database{}, skeleton{}, parent_key{}, parent_generation{}, body_count{}, bindings{}, gate18{}, gate24{};
    std::uint8_t job_flag{}, perform{}, mode{}, export_flag{}, force{}, cached{}, composite_flags{};
    std::uintptr_t local_pose{}, postpose{};
    if (!read(actor+0x1A8, fg) || !read(job+8, job_fg) || job_fg != fg || !read(job+0x14, job_flag) || job_flag ||
        !read(job+0x20, postpose) || postpose != g_module+0x6CD220 ||
        !read(actor+0xA8, owner_record) || !read(fg+0x1A8, record) || record != owner_record || !read(record+8, record_actor) || record_actor != actor ||
        !read(actor+0xBC, id) || !id || !read(fg+0x1B0, fg_id) || fg_id != id ||
        !read(job, control) || !control || !read(fg+0x10, fg_control) || fg_control != control ||
        !read(actor+0x1A0, owner_control) || owner_control != control || !read(control+0x20, control_fg) || control_fg != fg ||
        !read(fg, flags) || (flags & 0x60) != 0x60 || !read(fg+5, perform) || !perform ||
        !read(fg+6, mode) || !mode || !read(fg+8, export_flag) || !(export_flag & 1) ||
        !read(fg+0x298, custom) || custom || !read(fg+0x2A0, custom) || custom ||
        !read(g_module+0x3682F49, force) || !read(g_module+0x3682F4B, cached) || cached || !read(fg+0x100, local_pose)) return false;
    if (((force || (flags & 2)) && local_pose ? 2 : mode) != 2) return false;
    gate = 2;
    if (!read(fg+0x18, art) || !art || !read(fg+0x1A0, active_art) || art != active_art ||
        !read(fg+0xF0, cache_art) || art != cache_art || !read(fg+0xF8, raw) || !raw ||
        !read(art+0x40, art_raw) || art_raw != raw || !read(art+0x24, database) || !read(raw+4, skeleton)) return false;
    if (!((database == 0x2628A5DC && skeleton == 0xBD7EEE2F) || (database == 0xF181B564 && skeleton == 0xFC65A17A))) return false;
    std::uint32_t group_relative{}; std::array<std::uint16_t, 10> group{};
    gate = 3;
    if (!read(raw+0x2C, group_relative) || !group_relative || !read(raw+group_relative, group) || group[0] || group[8] != 90 || group[9] != 55) return false;
    std::array<float, 8> parent{}; std::uintptr_t parent_slot{};
    gate = 4;
    if (!read(actor+0x108, parent_key) || parent_key || !read(actor+0x10C, parent_generation) || parent_generation ||
        !read(g_module+0x386FD00, parent_slot) || parent_slot || !read(actor+0x110, parent) ||
        parent[0] != 0 || parent[1] != 0 || parent[2] != 0 || parent[4] != 0 || parent[5] != 0 || parent[6] != 0 || parent[7] != 1 ||
        !read(actor+0x130, transform)) return false;
    gate = 5;
    std::uintptr_t physics_world{}, auxiliary{}, optional{}, ragdoll{};
    if (!read(actor+0x698, optional) || optional || !read(actor+0x580, ragdoll)) return false;
    if (ragdoll) {
        std::uintptr_t ragdoll_owner{}; std::uint32_t ragdoll_count{};
        if (!read(ragdoll+8, ragdoll_owner) || ragdoll_owner != actor || !read(ragdoll+0x10, ragdoll_count) || ragdoll_count > 32) return false;
        if (ragdoll_count) {
            // Living NPCs can own a nonempty, disabled ragdoll. Admit only the
            // native mode0/no-world case: no bindings to import, no dynamic
            // body to fight the primary collider, no penetration/buoyancy job.
            std::uintptr_t ragdoll_bodies{}, runtime{}, runtime_owner{}, penetrator{}, penetrator_owner{};
            std::uint32_t ragdoll_bindings{}, game_count{}, physics_count{}, disabled_count{};
            std::uint8_t dirty_bindings{}, buoyancy{}, penetration{};
            if (!read(ragdoll+0x48, ragdoll_bodies) || !ragdoll_bodies ||
                !read(ragdoll+0x60, ragdoll_bindings) || ragdoll_bindings || !read(ragdoll+0x6C, dirty_bindings) || dirty_bindings ||
                !read(ragdoll+0x18, game_count) || game_count || !read(ragdoll+0x1C, physics_count) || physics_count ||
                !read(ragdoll+0x24, disabled_count) || disabled_count != ragdoll_count ||
                !read(ragdoll+0x120, runtime) || !runtime || !read(runtime, runtime_owner) || runtime_owner != ragdoll ||
                !read(runtime+0x68, buoyancy) || buoyancy || !read(runtime+0x58, penetrator) || !penetrator ||
                !read(penetrator, penetrator_owner) || penetrator_owner != ragdoll ||
                !read(penetrator+0x3E, penetration) || penetration) return false;
            for (std::uint32_t i = 0; i < ragdoll_count; ++i) {
                const auto body = ragdoll_bodies+0x110*i;
                std::uintptr_t owner{}, native{}, world{}; std::uint32_t body_flags{}; std::uint8_t source{}; std::int16_t joint{};
                if (!read(body+0x68, owner) || owner != actor || !read(body+0xA0, body_flags) ||
                    ((body_flags >> 13) & 3) != 0 || (body_flags & 0x400) || !read(body+0x40, native) ||
                    (native && (!read(native+0x10, world) || world)) ||
                    !read(body+0xC6, source) || !read(body+0xC4, joint) ||
                    (source != 1 && ((source != 2 && source != 3) || joint < 0 || joint >= 90))) return false;
            }
        }
    }
    if (!read(g_module+0x6917C20, physics_world) || !physics_world || !read(actor+0x238, composite) || !composite ||
        !read(composite+8, composite_owner) || composite_owner != actor || !read(composite+0x48, bodies) || !bodies ||
        !read(composite+0x10, body_count) || !body_count || body_count > 16 || !read(composite+0x60, bindings) ||
        !read(composite+0x120, auxiliary) || (auxiliary && bindings) || !read(composite+0x18, gate18) ||
        !read(composite+0x24, gate24) || !read(composite+2, composite_flags) || (!(gate18+gate24) && !(composite_flags & 0x10))) return false;
    unsigned root_count{};
    gate = 6;
    for (std::uint32_t i = 0; i < body_count; ++i) {
        const auto body = bodies+0x110*i;
        std::uintptr_t owner{}, native{}, body_world{}; std::uint32_t body_flags{}; std::uint8_t source{}, motion{}; std::int16_t joint{}; float blend{};
        if (!read(body+0x68, owner) || owner != actor || !read(body+0x40, native) || !native || !read(body+0xA0, body_flags) ||
            ((body_flags >> 13) & 3) != 2 || (body_flags & 1) || !read(body+0x9C, blend) || !std::isfinite(blend) || blend > 0 ||
            !read(body+0xC6, source) || !read(body+0xC4, joint) || !read(native+0x160, motion) || motion != 4 ||
            !read(native+0x10, body_world) || !body_world) return false;
        if (source == 1) ++root_count;
        else if (source != 3 || joint < 0 || joint >= 90) return false;
    }
    // No health mutation. A live exact Pirate must still have positive raw HP.
    std::uintptr_t health{}, health_vt{}; std::int32_t hp{};
    gate = 7;
    if (!read(actor+0x4A8, health) || !health || !read(health, health_vt) ||
        health_vt != g_module+0x2BBE128 || !read(health+0x38, hp) || hp <= 0) return false;
    return root_count == 1;
}
void postblend(void* object, void* native_job, std::uintptr_t pc) noexcept {
    if (pc != g_module+0x1A929A7 || !installed.load(std::memory_order_acquire) || !active_scope.load(std::memory_order_acquire) ||
        stopped.load(std::memory_order_relaxed) || g_dll_inert) return;
    WorkBudget budget(2);
    if (!budget.check()) return;
    const auto actor = reinterpret_cast<std::uintptr_t>(object);
    // Metadata-only scheduling first. A roster pointer match is NOT admission:
    // all identity/ordinary/final generation checks below remain mandatory.
    const auto schedule_now = GetTickCount64();
    if (schedule_now > census_valid_until.load(std::memory_order_acquire)) return;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    bool due{};
    wire::Packet scheduled_packet{}; const auto scheduled_session = session;
    if (session && confirmed && schedule_now-peer_seen <= 1000 && !stopped.load()) {
        for (std::size_t i = 0; i < roster.count; ++i) if (roster.rows[i].actor == actor) {
            auto& row = rows[i];
            if (!g_net_role_b) {
                due = schedule_now >= row.next_capture;
                if (due) row.next_capture = schedule_now+50;
            } else {
                due = row.received && schedule_now-row.received <= 200 &&
                    row.incoming.sender == peer && row.incoming.receiver == session &&
                    row.incoming.sequence != row.attempted_sequence;
                if (due) {
                    row.attempted_sequence = row.incoming.sequence;
                    scheduled_packet = row.incoming;
                }
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (!due) return;
    // Cache protections only for this callback, after stock and before setter.
    // Repeated identity/eligibility checks below still reread actual game data.
    ReadCache cache{}; cache.budget = &budget;
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), &cache); };
    Local local{}; std::array<float, 8> transform{}; unsigned gate{};
    if (!identity(actor, local, cache) || !ordinary(actor, reinterpret_cast<std::uintptr_t>(native_job), transform, gate, cache)) {
        last_rejection.store(gate, std::memory_order_relaxed);
        rejected.fetch_add(1, std::memory_order_relaxed); return;
    }
    const auto now = GetTickCount64();
    if (!TryAcquireSRWLockExclusive(&lock)) return; // never wait in an animation job
    if (!session || session != scheduled_session || !active_scope.load(std::memory_order_acquire) || stopped.load() ||
        now > census_valid_until.load(std::memory_order_acquire) || roster.player != current_player(cache)) {
        ReleaseSRWLockExclusive(&lock); return;
    }
    std::size_t index = roster.count;
    for (std::size_t i = 0; i < roster.count; ++i) if (roster.rows[i] == local) { index = i; break; }
    wire::Packet packet{}; bool use{}; const auto local_session = session;
    if (index < roster.count) {
        auto& row = rows[index];
        if (!g_net_role_b) {
            packet.kind = wire::Kind::root; packet.key = local.key;
            std::copy_n(transform.begin(), 3, packet.position.begin()); std::copy_n(transform.begin()+4, 4, packet.rotation.begin());
            if (wire::valid_pose(packet) && budget.check()) { row.outgoing = packet; row.captured = now; row.dirty = true; captured.fetch_add(1, std::memory_order_relaxed); }
        } else if (confirmed && now-peer_seen <= 1000 && row.received && now-row.received <= 200 &&
            scheduled_packet.sender == peer && scheduled_packet.receiver == session &&
            row.incoming.sequence == scheduled_packet.sequence) {
            packet = scheduled_packet;
            double distance{};
            for (unsigned axis = 0; axis < 3; ++axis) { const double delta = packet.position[axis]-transform[axis]; distance += delta*delta; }
            // First prototype never teleports a mismatched encounter across the map.
            use = distance <= 100.0;
            if (use && !first_apply) first_apply = now;
            if (first_apply && now-first_apply > 30000) { stopped.store(true); use = false; }
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (!use) return;
    // Refresh identity/eligibility after releasing the mailbox, never dereference
    // an actor obtained from the receive thread. Current native caller owns self.
    Local after{}; std::array<float, 8> fresh{};
    if (!identity(actor, after, cache) || !(after == local) || !ordinary(actor, reinterpret_cast<std::uintptr_t>(native_job), fresh, gate, cache)) return;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    const auto apply_now = GetTickCount64();
    std::uintptr_t manager{}; std::uint32_t player_id{};
    const bool still_current = !g_dll_inert && active_scope.load(std::memory_order_acquire) && !stopped.load() &&
        apply_now <= census_valid_until.load(std::memory_order_acquire) &&
        session == local_session && session == packet.receiver && peer == packet.sender &&
        confirmed && apply_now-peer_seen <= 1000 && index < roster.count && roster.rows[index] == local &&
        rows[index].received && apply_now-rows[index].received <= 200 && first_apply && apply_now-first_apply <= 30000 &&
        rows[index].incoming.sequence == packet.sequence &&
        roster.player == current_player(cache) && read(roster.player+0xBC, player_id) && player_id == roster.player_id &&
        read(g_module+0x3898630, manager) && manager == roster.manager;
    if (!still_current) { ReleaseSRWLockExclusive(&lock); return; }
    double fresh_distance{};
    for (unsigned axis = 0; axis < 3; ++axis) { const double delta = packet.position[axis]-fresh[axis]; fresh_distance += delta*delta; }
    if (!(fresh_distance <= 100.0)) { ReleaseSRWLockExclusive(&lock); return; }
    // ordinary() walks animation/physics after identity(). Recheck kill state
    // and generations after that walk, before the final freshness/cost cutoff.
    // These are actual budgeted SEH reads, NOT a lifetime lease across the setter.
    std::uint8_t actor_kill{}, player_kill{}; std::uint32_t actor_id{};
    if (!read(actor+0xBC, actor_id) || actor_id != local.generation ||
        !read(actor+0x40, actor_kill) || (actor_kill & 0x04) ||
        !read(roster.player+0xBC, player_id) || player_id != roster.player_id ||
        !read(roster.player+0x40, player_kill) || (player_kill & 0x04)) {
        ReleaseSRWLockExclusive(&lock); return;
    }
    // The final memory reads can themselves be delayed. Do not use a timestamp
    // captured before them to authorize a write after the packet/scope expired.
    const auto write_now = GetTickCount64();
    if (!budget.check() || g_dll_inert || stopped.load() || !active_scope.load(std::memory_order_acquire) ||
        write_now > census_valid_until.load(std::memory_order_acquire) ||
        write_now-peer_seen > 1000 || write_now-rows[index].received > 200 || write_now-first_apply > 30000) {
        ReleaseSRWLockExclusive(&lock); return;
    }
    std::copy_n(packet.position.begin(), 3, fresh.begin()); // keep receiver +13C service lane
    std::copy(packet.rotation.begin(), packet.rotation.end(), fresh.begin()+4);
    apply_root_and_release(object, fresh.data(), &budget);
}
} // namespace enemy_root

extern "C" std::uintptr_t TllEnemyRootPostblendBridge(void*);
extern "C" std::uintptr_t __fastcall TllEnemyRootPostblend(void* owner, void* job) {
    const auto pc = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    using Stock = std::uintptr_t(__fastcall*)(void*);
    const auto result = reinterpret_cast<Stock>(g_module+enemy_root::kPostblend)(owner);
    const DWORD error = GetLastError();
    enemy_root::postblend(owner, job, pc);
    SetLastError(error); return result;
}
bool install_enemy_root_sync() {
    constexpr std::array<std::uint8_t, 32> original{
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,
        0x8B,0x01,0x48,0x8B,0xF1,0xFF,0x90,0xC0,0x03,0x00,0x00,0x48,0x8B,0x48,0x58,0x48};
    constexpr std::array<std::uint8_t, 35> setter{
        0xC5,0xF8,0x10,0x02,0xC5,0xF8,0x11,0x81,0x30,0x01,0x00,0x00,0xC5,0xF8,0x10,0x4A,0x10,
        0xC5,0xF8,0x11,0x89,0x40,0x01,0x00,0x00,0x48,0x8B,0x01,0x48,0xFF,0xA0,0x08,0x02,0x00,0x00};
    std::array<std::uint8_t, 32> actual{}; std::array<std::uint8_t, 35> actual_setter{};
    std::array<std::uint8_t, 6> call{}; std::array<std::uint8_t, 3> empty_notify{};
    std::uintptr_t notify{};
    if (!enemy_root::read(g_module+enemy_root::kPostblend, actual) || actual != original ||
        !enemy_root::read(g_module+0x1E7D0D0, actual_setter) || actual_setter != setter ||
        !enemy_root::read(g_module+enemy_root::kPirate+0x208, notify) || notify != g_module+0x1D4E10 ||
        !enemy_root::read(notify, empty_notify) || empty_notify != std::array<std::uint8_t, 3>{0xC2,0x00,0x00} ||
        !enemy_root::read(g_module+0x1A929A1, call) || call != std::array<std::uint8_t, 6>{0xFF,0x90,0x08,0x04,0x00,0x00}) {
        boot_log_line("ENEMY-ROOT install refused: retail vtable/call/setter bytes differ"); return false;
    }
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&TllEnemyRootPostblend), &pinned) || reinterpret_cast<std::uintptr_t>(pinned) != g_self_module) return false;
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart < 1000) return false;
    enemy_root::qpc_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
    if (!enemy_root::start_stop_control()) {
        boot_log_line("ENEMY-ROOT installation refused: terminal stop control unavailable"); return false;
    }
    if (!enemy_root::start_transport()) {
        enemy_root::stopped.store(true);
        boot_log_line("ENEMY-ROOT installation refused: dedicated transport unavailable"); return false;
    }
    auto** slot = reinterpret_cast<void**>(g_module+enemy_root::kPirate+0x408);
    DWORD old{};
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        enemy_root::stopped.store(true); return false;
    }
    auto* expected = reinterpret_cast<void*>(g_module+enemy_root::kPostblend);
    auto* bridge = reinterpret_cast<void*>(&TllEnemyRootPostblendBridge);
    const bool changed = InterlockedCompareExchangePointer(slot, bridge, expected) == expected;
    DWORD ignored{};
    const bool sealed = VirtualProtect(slot, sizeof(void*), old, &ignored) != FALSE;
    // Pointer publication is one aligned atomic exchange, not a torn instruction
    // patch. Already-loaded stock targets remain stock; no relay is freed.
    const bool ready = changed && sealed;
    if (!ready) enemy_root::stopped.store(true);
    enemy_root::installed.store(ready, std::memory_order_release);
    boot_log_line(ready ? "ENEMY-ROOT native Pirate+408 root replication installed (30-second first slice; no HP/AI writes)" :
        "ENEMY-ROOT installation refused; stock forwarding only");
    return ready;
}
