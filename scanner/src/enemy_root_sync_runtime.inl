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
    std::uint32_t last_sequence{};
    bool dirty{};
};
SRWLOCK lock = SRWLOCK_INIT;
Roster roster{};
std::array<Row, wire::kActors> rows{};
std::uint64_t session{}, peer{};
std::uint32_t sequence{}, hello_sequence{};
ULONGLONG peer_seen{}, last_hello{}, last_tick{}, last_log{};
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

struct ReadCache {
    struct Region { std::uintptr_t begin{}, end{}; };
    std::array<Region, 64> regions{};
    std::size_t count{};
};
bool copy(std::uintptr_t at, void* out, std::size_t size, ReadCache* cache = nullptr) noexcept {
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
        if (VirtualQuery(reinterpret_cast<void*>(cursor), &m, sizeof(m)) != sizeof(m) || m.State != MEM_COMMIT ||
            (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(m.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
        const auto end = reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;
        if (end <= cursor) return false;
        if (cache && cache->count < cache->regions.size()) cache->regions[cache->count++] = {reinterpret_cast<std::uintptr_t>(m.BaseAddress), end};
        cursor = end;
    }
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), size); return true; }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
        GetExceptionCode() == EXCEPTION_GUARD_PAGE ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
}
template<class T> bool read(std::uintptr_t at, T& out) noexcept { return copy(at, &out, sizeof(out)); }
std::uintptr_t current_player(ReadCache* shared = nullptr) noexcept {
    ReadCache local_cache{};
    auto* cache = shared ? shared : &local_cache;
    const auto read = [cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), cache); };
    std::uintptr_t record{}, actor{}, vt{}, record_after{}, actor_after{}; std::uint32_t id{}, actor_id{}, id_after{};
    if (!read(g_module+kRetainedSlotTableRva, record) || !read(g_module+kRetainedSlotTableRva+8, id) || !id ||
        !read(record+8, actor) || !read(actor, vt) || vt != g_module+kPlayerVtableRva || !read(actor+0xBC, actor_id) || actor_id != id ||
        !read(g_module+kRetainedSlotTableRva, record_after) || record_after != record ||
        !read(g_module+kRetainedSlotTableRva+8, id_after) || id_after != id ||
        !read(record+8, actor_after) || actor_after != actor) return 0;
    return actor;
}
bool identity(std::uintptr_t actor, Local& out, ReadCache* shared = nullptr) noexcept {
    // Lexical cache, discarded before any native callback or mailbox handoff.
    // Every actual copy still has memory-fault SEH; this is not a page lease.
    ReadCache local_cache{};
    auto* cache = shared ? shared : &local_cache;
    const auto read = [cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), cache); };
    Local value{}; value.actor = actor;
    std::uintptr_t vtable{}, bound_actor{}, owner_record{}, name{};
    std::uint32_t bound_id{}; std::uint8_t flags{};
    if (!read(actor, vtable) || vtable != g_module+kPirate || !read(actor+0xA8, value.record) ||
        !read(actor+0xBC, value.generation) || !value.generation || !read(actor+0x150, value.spawner) || !value.spawner ||
        !read(value.record+8, bound_actor) || bound_actor != actor ||
        !read(value.spawner+0x30, owner_record) || owner_record != value.record ||
        !read(value.spawner+0x38, bound_id) || bound_id != value.generation ||
        !read(value.spawner+0x51, flags) || (flags & 0x18) || !read(value.spawner+0x20, name) ||
        !read(value.spawner+0x28, value.key.spawner_id) || !read(actor+0xD8, value.key.desired_id) ||
        !read(actor+0xDC, value.key.variant_id)) return false;
    bool ended{};
    for (std::size_t i = 0; i < value.key.name.size();) {
        // Windows AMD64 base pages are4KiB. Stop at each page boundary so a
        // terminator before an unreadable next page remains valid. copy() still
        // checks protection/SEH; do not query the same name region twice.
        const auto available = std::size_t{0x1000}-((name+i)&0xFFF);
        const auto size = std::min(available, value.key.name.size()-i);
        if (!copy(name+i, value.key.name.data()+i, size, cache)) return false;
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
        !read(actor+0xBC, id_after) || id_after != value.generation) return false;
    out = value; return true;
}
bool census(Roster& out) noexcept {
    // One synchronous read pass, no game callbacks or pointer publication.
    // Reuse region checks inside this pass, never across ticks/native phases.
    ReadCache cache{};
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), &cache); };
    Roster value{};
    value.player = current_player(&cache);
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
        if (!identity(actor, local, &cache) || local.record != entry.record || local.generation != entry.id) return false;
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
    session = peer = 0; confirmed = false; peer_seen = last_hello = 0; hello_sequence = 0;
    roster = {}; rows = {};
}
void receive(const void* bytes, int size, const sockaddr_in& source, int source_size) {
    if (size != sizeof(wire::Packet) || source_size < sizeof(source) || !g_state_net.peer_set ||
        source.sin_family != g_state_net.peer_addr.sin_family || source.sin_addr.s_addr != g_state_net.peer_addr.sin_addr.s_addr ||
        source.sin_port != g_state_net.peer_addr.sin_port) return;
    wire::Packet packet{}; std::memcpy(&packet, bytes, sizeof(packet));
    if (!wire::valid(packet) || packet.role_b == static_cast<std::uint8_t>(g_net_role_b)) return;
    const auto now = GetTickCount64();
    // Enemy UDP is lossy; contention must not stall the shared player receiver.
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
            for (auto& row : rows) { row.incoming = {}; row.received = 0; row.last_sequence = 0; }
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
void send(const wire::Packet& packet) {
    // Bound socket is essential: the legacy send socket has an ephemeral source
    // port and cannot pass the exact configured-endpoint check above.
    sendto(g_state_net.recv_socket, reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
        reinterpret_cast<const sockaddr*>(&g_state_net.peer_addr), sizeof(g_state_net.peer_addr));
}
void tick() {
    if (!installed.load(std::memory_order_acquire) || !g_state_net.enabled || !g_state_net.peer_set) return;
    if (stopped.load()) {
        active_scope.store(false, std::memory_order_release);
        static std::atomic<bool> reported{};
        if (!reported.exchange(true)) {
            std::ostringstream line; line << "ENEMY-ROOT STOPPED apply=" << applied.load() << " match=" << matched.load()
                << "; native AI/root resumes; no automatic rearm";
            boot_log_line(line.str().c_str());
        }
        return;
    }
    if (ticking.test_and_set()) return;
    const auto now = GetTickCount64();
    if (now-last_tick < 50) { ticking.clear(); return; } last_tick = now;
    Roster current{};
    const bool valid = census(current);
    if (!valid) {
        census_gap = true;
        active_scope.store(false, std::memory_order_release);
        if (applied.load(std::memory_order_acquire)) stopped.store(true);
    }
    std::array<wire::Packet, wire::kActors+1> pending{}; std::size_t count{};
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
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&session), sizeof(session), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) session = 0;
            if (session) roster = current;
        }
        if (session && now-last_hello >= 200) {
            wire::Packet hello{}; hello.role_b = static_cast<std::uint8_t>(g_net_role_b);
            hello.sender = session; hello.receiver = peer; hello.sequence = ++sequence;
            pending[count++] = hello; last_hello = now;
        }
        if (session && confirmed && peer && now-peer_seen <= 1000 && !g_net_role_b) {
            for (std::uint32_t i = 0; i < roster.count; ++i) {
                auto& row = rows[i];
                if (!row.dirty || now-row.captured > 150) continue;
                auto& packet = pending[count++]; packet = row.outgoing;
                packet.sender = session; packet.receiver = peer; packet.sequence = ++sequence;
                row.dirty = false;
            }
        }
    }
    const auto local_count = roster.count; const bool paired = confirmed;
    active_scope.store(session && confirmed && now-peer_seen <= 1000 && !stopped.load(), std::memory_order_release);
    ReleaseSRWLockExclusive(&lock);
    for (std::size_t i = 0; i < count; ++i) send(pending[i]);
    if (now-last_log >= 2000) {
        last_log = now; std::ostringstream line;
        line << "ENEMY-ROOT role=" << (g_net_role_b ? 'B' : 'A') << " actors=" << local_count << " paired=" << paired
             << " capture=" << captured.load() << " recv=" << received.load() << " match=" << matched.load()
             << " apply=" << applied.load() << " reject=" << rejected.load();
        line << " lastGate=" << last_rejection.load() << " stopped=" << stopped.load();
        boot_log_line(line.str().c_str());
    }
    ticking.clear();
}

bool ordinary(std::uintptr_t actor, std::uintptr_t job, std::array<float, 8>& transform, unsigned& gate) noexcept {
    ReadCache cache{};
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
    const auto actor = reinterpret_cast<std::uintptr_t>(object);
    // Metadata-only scheduling first. A roster pointer match is NOT admission:
    // all identity/ordinary/final generation checks below remain mandatory.
    const auto schedule_now = GetTickCount64();
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    bool due{};
    if (session && confirmed && schedule_now-peer_seen <= 1000 && !stopped.load()) {
        for (std::size_t i = 0; i < roster.count; ++i) if (roster.rows[i].actor == actor) {
            auto& row = rows[i];
            if (!g_net_role_b) {
                due = schedule_now >= row.next_capture;
                if (due) row.next_capture = schedule_now+50;
            } else {
                due = row.received && schedule_now-row.received <= 200 &&
                    row.incoming.sender == peer && row.incoming.receiver == session;
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (!due) return;
    Local local{}; std::array<float, 8> transform{}; unsigned gate{};
    if (!identity(actor, local) || !ordinary(actor, reinterpret_cast<std::uintptr_t>(native_job), transform, gate)) {
        last_rejection.store(gate, std::memory_order_relaxed);
        rejected.fetch_add(1, std::memory_order_relaxed); return;
    }
    const auto now = GetTickCount64();
    if (!TryAcquireSRWLockExclusive(&lock)) return; // never wait in an animation job
    if (!session || roster.player != current_player()) {
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
            if (wire::valid_pose(packet)) { row.outgoing = packet; row.captured = now; row.dirty = true; captured.fetch_add(1, std::memory_order_relaxed); }
        } else if (confirmed && now-peer_seen <= 1000 && row.received && now-row.received <= 200 &&
            row.incoming.sender == peer && row.incoming.receiver == session) {
            packet = row.incoming;
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
    if (!identity(actor, after) || !(after == local) || !ordinary(actor, reinterpret_cast<std::uintptr_t>(native_job), fresh, gate)) return;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    const auto apply_now = GetTickCount64();
    std::uintptr_t manager{}; std::uint32_t player_id{};
    const bool still_current = !g_dll_inert && active_scope.load(std::memory_order_acquire) && !stopped.load() &&
        session == local_session && session == packet.receiver && peer == packet.sender &&
        confirmed && apply_now-peer_seen <= 1000 && index < roster.count && roster.rows[index] == local &&
        rows[index].received && apply_now-rows[index].received <= 200 && first_apply && apply_now-first_apply <= 30000 &&
        rows[index].incoming.sequence == packet.sequence &&
        roster.player == current_player() && read(roster.player+0xBC, player_id) && player_id == roster.player_id &&
        read(g_module+0x3898630, manager) && manager == roster.manager;
    if (!still_current) { ReleaseSRWLockExclusive(&lock); return; }
    double fresh_distance{};
    for (unsigned axis = 0; axis < 3; ++axis) { const double delta = packet.position[axis]-fresh[axis]; fresh_distance += delta*delta; }
    if (!(fresh_distance <= 100.0)) { ReleaseSRWLockExclusive(&lock); return; }
    std::copy_n(packet.position.begin(), 3, fresh.begin()); // keep receiver +13C service lane
    std::copy(packet.rotation.begin(), packet.rotation.end(), fresh.begin()+4);
    using SetLocalRoot = void(__fastcall*)(void*, const void*);
    reinterpret_cast<SetLocalRoot>(g_module+0x1E7D0D0)(object, fresh.data());
    applied.fetch_add(1, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&lock);
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
    auto** slot = reinterpret_cast<void**>(g_module+enemy_root::kPirate+0x408);
    DWORD old{};
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    auto* expected = reinterpret_cast<void*>(g_module+enemy_root::kPostblend);
    auto* bridge = reinterpret_cast<void*>(&TllEnemyRootPostblendBridge);
    const bool changed = InterlockedCompareExchangePointer(slot, bridge, expected) == expected;
    DWORD ignored{};
    const bool sealed = VirtualProtect(slot, sizeof(void*), old, &ignored) != FALSE;
    // Pointer publication is one aligned atomic exchange, not a torn instruction
    // patch. Already-loaded stock targets remain stock; no relay is freed.
    const bool ready = changed && sealed;
    enemy_root::installed.store(ready, std::memory_order_release);
    boot_log_line(ready ? "ENEMY-ROOT native Pirate+408 root replication installed (30-second first slice; no HP/AI writes)" :
        "ENEMY-ROOT installation refused; stock forwarding only");
    return ready;
}
