// Included only in the ordinary co-op runtime. No P99/offer/observer pipeline.
// Root base: exact Pirate, unique reciprocal spawner, identity parent, normal
// Perform -> phase40 -> JointBlend, game-driven bodies only. Separate explicit
// default-OFF ENC2 capability adds only initial-life A -> B HumanDead results
// below; legacy ENR2 retains roots only. Both remain one bounded encounter.
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
    std::uint64_t world_epoch{};
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
std::atomic<ULONGLONG> first_apply{};
void commit_first_effect(ULONGLONG when) noexcept {
    auto prior=first_apply.load(std::memory_order_acquire);
    // A synchronous Dead witness can precede damage's normal return. The
    // qualified call's earlier candidate may only tighten, never extend, time.
    while ((!prior || when<prior) &&
        !first_apply.compare_exchange_weak(prior,when,std::memory_order_acq_rel,std::memory_order_acquire)) {}
}
bool confirmed{};
// Owned by the serialized tick. Never lose an invalid/unpublished census while
// a native callback holds the mailbox; decide terminal/rebind under that lock.
bool census_gap{};
std::atomic<bool> installed{}, stopped{};
std::atomic<bool> active_scope{};
// ENR2 remains one bounded encounter per DLL, even though Player menu rejoin
// can clear readiness/departure flags and publish another world. Never reset
// this pin or adopt a later epoch. An earlier UNPAIRED world does not bind it.
std::atomic<std::uint64_t> bound_world_epoch{};
// Metadata observation only, NOT a native lifetime/relocation lease. The second
// epoch read rejects a publish racing the admitted check without game pointers.
bool scope_current() noexcept {
    if constexpr (!player_world::kEnablePlayerWorldRearm) return true;
    const auto bound = bound_world_epoch.load(std::memory_order_acquire);
    return bound && player_world::local_epoch() == bound &&
           player_world::data_admitted() && player_world::local_epoch() == bound;
}
// After admission, any world close/change or departure is terminal for ENR2.
// Before the first paired world, old menu/departure flags must not poison first
// admission. Legacy mode retains its original departure behavior. Atomics only:
// safe in the socket worker, with no mailbox wait or game-pointer traversal.
bool stop_for_player_lifecycle() noexcept {
    bool world_closed{};
    if constexpr (player_world::kEnablePlayerWorldRearm) {
        if (!bound_world_epoch.load(std::memory_order_acquire)) return false;
        world_closed = !scope_current();
    }
    if (!world_closed && !g_state_net.local_leave_queued.load(std::memory_order_acquire) &&
        !g_state_net.peer_left.load(std::memory_order_acquire) &&
        !g_remote_player_teardown.load(std::memory_order_acquire)) return false;
    stopped.store(true, std::memory_order_release);
    active_scope.store(false, std::memory_order_release);
    return true;
}
// Until bootstrap AND a stable ready Player-world pair are observed, tick must
// not construct a work budget, enter census or inspect game memory. These
// readiness flags are no longer assumed monotonic across Player menu rejoin.
bool startup_admitted() noexcept {
    if (!g_owned_remote_player_ready.load(std::memory_order_acquire) ||
        !g_player_update_hook_ready.load(std::memory_order_acquire)) return false;
    if constexpr (player_world::kEnablePlayerWorldRearm) {
        const auto epoch = player_world::local_epoch();
        if (!epoch || !player_world::peer_ready() || player_world::local_epoch() != epoch) return false;
        std::uint64_t empty{};
        bound_world_epoch.compare_exchange_strong(empty, epoch, std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
        if (stop_for_player_lifecycle() || !scope_current()) return false;
    }
    return true;
}
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

// ONE opt-in capability, default OFF. Initial unique-Key population only: no
// prebind death, respawn adoption, B damage authority or general campaign claim.
bool combat_requested_state{};
std::atomic<bool> combat_hooks_ready{};
bool combat_requested() noexcept {
    wchar_t value[8]{};
    return GetEnvironmentVariableW(L"TLL_ENEMY_COMBAT_SYNC", value, 8) == 1 && value[0] == L'1';
}
bool combat_enabled() noexcept { return combat_requested_state; }
bool combat_ready() noexcept { return installed.load(std::memory_order_acquire) && combat_hooks_ready.load(std::memory_order_acquire); }
enum class NativeLifeKind : std::uint8_t { retiring, dead_commit };
struct NativeLifeEvent {
    NativeLifeKind kind{};
    std::uint64_t worldEpoch{};
    std::uintptr_t record{};
    std::uint32_t generation{};
};
enum class NativeAttemptStatus : std::uint8_t { refused, native_returned, native_fault };
struct NativeDeathAttempt {
    wire::Key key{};
    std::uint64_t worldEpoch{}, session{}, peer{}, lifeId{}, eventId{}, rowRevision{};
    std::uintptr_t record{};
    std::uint32_t generation{};
    ULONGLONG effectStart{}, controlStart{};
};
struct CombatRow {
    std::uint64_t life{}, revision{1}, death_event{}, attempted_event{};
    ULONGLONG last_control{}, control_started{};
    wire::combat::Result result = wire::combat::Result::none;
    bool bound{}, bind_confirmed{}, bind_reply{}, root_seen{}, result_confirmed{};
    bool local_retiring{}, local_dead{}, authority_dead{};
    wire::combat::Refusal refusal = wire::combat::Refusal::none;
    wire::combat::Refusal remote_refusal = wire::combat::Refusal::none;
    wire::combat::Result remote_result = wire::combat::Result::none;
    bool receipt_seen{}, receipt_reply{}, report_acked{}, remote_response{}, response_ack_reply{}, delivery_failed{};
};
std::array<CombatRow, wire::kActors> combat_rows{}; // existing mailbox lock
bool encounter_sealed{};
std::uint64_t next_death_event{};
enum class CensusReject : unsigned { none, header, unknown_weak, changed_key, duplicate, missing, revision_overflow };
struct CensusLocalSummary {
    std::uintptr_t actor, record, spawner;
    std::uint32_t generation, spawner_id, desired_id, variant_id;
    std::array<char,wire::kNameBytes> name;
};
struct CensusRejectContext {
    std::uint32_t stable_count, current_count, old_index, new_index;
    unsigned header_mask, local_mask, witness_bits, name_first_diff;
    std::array<std::uintptr_t,3> old_header, new_header; // manager,array,player
    std::uint64_t old_world, new_world;
    std::uint32_t old_player_id, new_player_id;
    CensusLocalSummary old_local, new_local;
};
static_assert(std::is_trivial_v<CensusRejectContext> && std::is_standard_layout_v<CensusRejectContext> &&
              std::is_trivially_copyable_v<CensusRejectContext>);
// One tick writer, published ONCE. Only copied census values, never pointers to
// be followed later. The private socket worker reads after reason.acquire.
CensusRejectContext census_reject_context{};
std::atomic<CensusReject> first_census_reject{};
std::atomic<std::uint64_t> combat_relocation_refreshes{};
struct ControlLife { wire::Key key{}; std::uint64_t life{}; };
struct ControlBook {
    std::uint64_t world{}, session{}, peer{};
    std::uint32_t count{};
    std::array<ControlLife,wire::kActors> lives{};
};
// Frozen once after COMPLETE initial binding. No native address/weak handle is
// retained in this transport book, and reset_locked never rewrites it.
ControlBook control_book{};
std::atomic<bool> control_book_ready{}, control_closed{};
enum class ControlClose : unsigned { none, world, inert, native_fault, external, socket, unbound };
std::atomic<ControlClose> control_close_reason{};
std::uint32_t control_hello_sequence{}; // mailbox only, never a rebind permission
ULONGLONG control_last_hello{};
std::atomic<std::uint64_t> control_receipts{}, control_refusals{}, control_refusals_received{}, control_report_acks{}, control_delivery_failed{};
std::atomic<unsigned> control_pending{};
bool control_current() noexcept;
void close_control(ControlClose reason) noexcept;
struct NativeBinding {
    wire::Key key{};
    std::uint64_t world{}, session{}, peer{}, life{};
    std::uintptr_t record{};
    std::uint32_t generation{};
};
// Published ONCE. Native observers never follow a saved game pointer or wait
// for the mailbox: per-life monotonic bits are a bounded lossless MPSC mailbox.
std::array<NativeBinding, wire::kActors> native_bindings{};
std::array<std::atomic<unsigned>, wire::kActors> native_life_bits{};
std::array<std::atomic<std::uint64_t>, wire::kActors> native_attempt_event{};
std::array<unsigned, wire::kActors> observed_life_bits{}; // mailbox only
std::atomic<bool> native_bindings_ready{};
std::atomic<bool> native_death_pending_hint{};
std::uint32_t native_binding_count{}; // immutable after ready release
enum : unsigned { kNativeRetiring=1, kNativeDead=2, kNativeReturned=4, kNativeRefused=8, kNativeFault=16 };
std::atomic<std::uint64_t> combat_death_events{}, combat_death_results{}, combat_native_refusals{};
std::atomic<std::uint64_t> combat_death_received{}, combat_native_attempts{}, combat_native_commits{};
std::atomic<std::uint64_t> combat_retiring_observed{}, combat_prebind_refused{}, combat_b_prediction{};
namespace combat_native { bool install(); bool ready(); void append_observer_status(std::ostringstream&); }
bool effect_slice_current(ULONGLONG now) noexcept {
    const auto start=first_apply.load(std::memory_order_acquire);
    // Another callback may publish the first clock after this timestamp was
    // sampled; that is not a clock rollback or an expired slice.
    if (!start || now<start || now-start<=30000) return true;
    stopped.store(true,std::memory_order_release);
    active_scope.store(false,std::memory_order_release);
    native_death_pending_hint.store(false,std::memory_order_release);
    return false;
}
bool native_death_pending() noexcept {
    return combat_enabled() && g_net_role_b && native_death_pending_hint.load(std::memory_order_acquire) &&
        !stopped.load(std::memory_order_acquire) && effect_slice_current(GetTickCount64());
}
void refresh_native_pending_locked() noexcept {
    bool pending{};
    if (g_net_role_b) for (std::size_t i=0; i<roster.count; ++i) {
        const auto& row=combat_rows[i];
        pending=pending || (row.death_event && row.authority_dead && !row.attempted_event &&
            !row.local_retiring && !row.local_dead && row.result==wire::combat::Result::none);
    }
    native_death_pending_hint.store(pending,std::memory_order_release);
}

bool combat_row_writable(std::size_t index) noexcept {
    if (!combat_enabled()) return true;
    if (!native_bindings_ready.load(std::memory_order_acquire) || index >= roster.count) return false;
    const auto& row = combat_rows[index];
    return row.bound && row.bind_confirmed && !row.authority_dead && !row.local_retiring && !row.local_dead &&
        !(native_life_bits[index].load(std::memory_order_acquire) & (kNativeRetiring|kNativeDead));
}
bool combat_known_retiring(std::uintptr_t record, std::uint32_t generation) noexcept {
    if (!combat_enabled() || !native_bindings_ready.load(std::memory_order_acquire)) return false;
    for (std::size_t i=0; i<native_binding_count; ++i)
        if (native_bindings[i].record==record && native_bindings[i].generation==generation)
            return (native_life_bits[i].load(std::memory_order_acquire)&(kNativeRetiring|kNativeDead)) != 0;
    return false;
}
void publish_native_bindings_locked() noexcept {
    if (!combat_enabled() || !encounter_sealed || !session || !peer || !confirmed ||
        native_bindings_ready.load(std::memory_order_acquire)) return;
    for (std::size_t i=0; i<roster.count; ++i)
        if (!combat_rows[i].bound || !combat_rows[i].bind_confirmed || !combat_rows[i].life) return;
    native_binding_count=roster.count;
    for (std::size_t i=0; i<roster.count; ++i) {
        const auto& local=roster.rows[i];
        native_bindings[i]={local.key,roster.world_epoch,session,peer,combat_rows[i].life,local.record,local.generation};
        control_book.lives[i]={local.key,combat_rows[i].life};
    }
    control_book.world=roster.world_epoch; control_book.session=session;
    control_book.peer=peer; control_book.count=roster.count;
    control_hello_sequence=hello_sequence;
    native_bindings_ready.store(true,std::memory_order_release);
    control_book_ready.store(true,std::memory_order_release);
}
void seal_initial_combat_locked() noexcept {
    if (!combat_enabled() || encounter_sealed || !session || !peer || !confirmed || !roster.count) return;
    combat_rows={}; encounter_sealed=true;
    const auto now=GetTickCount64();
    for (auto& row:combat_rows) row.control_started=now;
    for (std::size_t i=0; i<roster.count; ++i) if (!g_net_role_b) {
        combat_rows[i].life=i+1; combat_rows[i].bound=true;
    }
}
void update_death_result_locked(std::size_t index,unsigned bits) noexcept {
    auto& row=combat_rows[index];
    if (!g_net_role_b || !row.death_event || !row.local_dead || row.result!=wire::combat::Result::none) return;
    if (row.attempted_event==row.death_event) {
        // A reservation is not an effect. A synchronous HumanDead entry may
        // precede damage's return; wait for that attempt's terminal status.
        if (!(bits&(kNativeReturned|kNativeRefused|kNativeFault))) return;
        row.result=(bits&kNativeReturned) && !(bits&(kNativeRefused|kNativeFault)) ?
            wire::combat::Result::applied : wire::combat::Result::already_applied;
    } else row.result=wire::combat::Result::already_applied;
    row.last_control=0; row.report_acked=false;
}
void drain_native_life_locked() noexcept {
    if (!combat_enabled() || !native_bindings_ready.load(std::memory_order_acquire)) return;
    for (std::size_t i=0; i<native_binding_count; ++i) {
        const auto bits=native_life_bits[i].load(std::memory_order_acquire);
        const auto fresh=bits&~observed_life_bits[i]; if (!fresh) continue;
        observed_life_bits[i]|=fresh;
        auto& row=combat_rows[i];
        if (fresh&kNativeRetiring) { row.local_retiring=true; ++row.revision; }
        if (fresh&kNativeDead) {
            row.local_dead=true; row.local_retiring=true; ++row.revision;
            if (g_net_role_b && !row.death_event) combat_b_prediction.fetch_add(1,std::memory_order_relaxed);
            if (!g_net_role_b && !row.death_event) {
                if (next_death_event==~std::uint64_t{}) { stopped.store(true); active_scope.store(false); return; }
                row.death_event=++next_death_event; row.authority_dead=true;
                row.control_started=GetTickCount64();
                combat_death_events.fetch_add(1,std::memory_order_relaxed);
            }
        }
        update_death_result_locked(i,bits);
    }
    refresh_native_pending_locked();
}

// One nonwaiting permit covers ALL synchronous enemy census/native guard work.
// A QPC wall-time cutoff contains repeated work; it cannot preempt one blocked
// OS call/page fault and must never be described as a hard frame-time bound.
std::atomic_flag work_busy = ATOMIC_FLAG_INIT;
std::uint64_t qpc_frequency{}, work_window{}, work_spent{}, work_query_ticks{};
std::uint64_t work_ticket{}, paused_ticket{}; // exclusively owned by work_busy.
std::atomic<bool> refresh_required{true};
std::atomic<std::uint64_t> certified_ticket{}, certified_epoch{}, certified_session{};
std::atomic<std::uint64_t> budget_pauses{};
std::atomic<std::uint64_t> census_publish_deferrals{};
std::uintptr_t system_page_size{}; // initialized before installer reads/worker publication.
unsigned work_queries{};
std::atomic<unsigned> cost_stop{}, cost_site{}, peak_queries{};
std::atomic<unsigned> census_stop_reason{}; //1=rejected,2=changed,3=expired.
std::atomic<std::uint64_t> cost_total_ticks{}, peak_scope_ticks{}, peak_window_ticks{}, query_total{}, work_skips{};
HANDLE stop_request{}, stop_ack{}, control_close_ack{}; // pinned process lifetime
std::atomic<bool> external_stop{};
std::atomic<bool> native_apply_fault{};
enum : unsigned { kCostClock = 1, kCostTime = 2, kCostQueries = 3, kCostWindow = 4, kCostPublication = 5 };
void close_control(ControlClose reason) noexcept {
    auto empty=ControlClose::none;
    control_close_reason.compare_exchange_strong(empty,reason,std::memory_order_release,std::memory_order_relaxed);
    control_closed.store(true,std::memory_order_release);
    stopped.store(true,std::memory_order_release);
    active_scope.store(false,std::memory_order_release);
}
bool control_current() noexcept {
    if (!combat_enabled() || !control_book_ready.load(std::memory_order_acquire) ||
        control_closed.load(std::memory_order_acquire)) return false;
    if (g_dll_inert) { close_control(ControlClose::inert); return false; }
    if (native_apply_fault.load(std::memory_order_acquire)) { close_control(ControlClose::native_fault); return false; }
    if (external_stop.load(std::memory_order_acquire)) { close_control(ControlClose::external); return false; }
    if (stop_for_player_lifecycle() || !scope_current() ||
        bound_world_epoch.load(std::memory_order_acquire)!=control_book.world) {
        close_control(ControlClose::world); return false;
    }
    return true; // metadata ONLY: root stopped/certificate/30s never authorize writes here.
}
wire::combat::Refusal stopped_writer_reason(ULONGLONG now) noexcept {
    switch (census_stop_reason.load(std::memory_order_acquire)) {
    case 1: return wire::combat::Refusal::census_rejected;
    case 2: return wire::combat::Refusal::census_changed;
    case 3: return wire::combat::Refusal::census_expired;
    default: break;
    }
    if (cost_stop.load(std::memory_order_acquire)) return wire::combat::Refusal::cost_stopped;
    if (worker_gap_ms.load(std::memory_order_acquire)) return wire::combat::Refusal::worker_gap;
    const auto first=first_apply.load(std::memory_order_acquire);
    if (first && now>=first && now-first>30000) return wire::combat::Refusal::effect_limit;
    return wire::combat::Refusal::root_stopped;
}
void refresh_control_reports_locked(ULONGLONG now) noexcept {
    if (!control_book_ready.load(std::memory_order_acquire)) return;
    unsigned pending{};
    for (std::size_t i=0; i<control_book.count; ++i) {
        auto& row=combat_rows[i];
        if (!row.death_event || !row.control_started || now<row.control_started) continue;
        const auto age=now-row.control_started;
        if (g_net_role_b && row.result==wire::combat::Result::none && row.refusal==wire::combat::Refusal::none) {
            auto reason=wire::combat::Refusal::none;
            if (age>30000) {
                reason=wire::combat::Refusal::event_deadline;
                stopped.store(true,std::memory_order_release); active_scope.store(false,std::memory_order_release);
            } else if (native_life_bits[i].load(std::memory_order_acquire)&kNativeRefused)
                reason=wire::combat::Refusal::native_attempt_refused;
            else if (stopped.load(std::memory_order_acquire)) reason=stopped_writer_reason(now);
            if (reason!=wire::combat::Refusal::none) {
                // Refusal means NO positive native commit witness, not proof
                // that an already-admitted operation had no partial effect.
                row.refusal=reason; row.report_acked=false; row.last_control=0;
                control_refusals.fetch_add(1,std::memory_order_relaxed);
            }
        }
        const bool failed=!g_net_role_b ? age>30000 && !row.remote_response :
            age>35000 && !row.report_acked;
        if (failed && !row.delivery_failed) {
            row.delivery_failed=true;
            control_delivery_failed.fetch_add(1,std::memory_order_relaxed);
        }
        if (!g_net_role_b ? !row.remote_response : !row.report_acked) ++pending;
    }
    control_pending.store(pending,std::memory_order_release);
}
struct CostStopContext {
    unsigned site{}, scope_queries{}, window_queries{}, read_calls{};
    std::uintptr_t read_address{}, query_address{};
    std::size_t read_bytes{};
    std::uint64_t scope_ticks{}, window_ticks{}, scope_query_ticks{}, window_query_ticks{}, last_query_ticks{};
    bool query_pending{}, read_refused{};
};
// Only the work-permit owner writes this, once, BEFORE cost_stop release.
// The worker reads it only after acquiring a nonzero reason; it is immutable.
CostStopContext cost_stop_context{};
CostStopContext first_pause_context{};
unsigned first_pause_reason{}; // immutable after budget_pauses release-publishes1.
void stop_cost(unsigned reason, const CostStopContext& context) noexcept {
    if (!cost_stop.load(std::memory_order_relaxed)) {
        cost_stop_context = context;
        cost_site.store(context.site, std::memory_order_relaxed);
        cost_stop.store(reason, std::memory_order_release);
    }
    stopped.store(true, std::memory_order_release);
    refresh_required.store(true, std::memory_order_release);
    active_scope.store(false, std::memory_order_release);
}

bool certificate_current(std::uint64_t ticket) noexcept {
    // Authorization readers hold work_busy. Worker-visible fields are atomic
    // for status only; no worker is allowed to certify or dereference objects.
    if (!ticket || refresh_required.load(std::memory_order_acquire) ||
        certified_ticket.load(std::memory_order_acquire) != ticket) return false;
    const auto published = published_session.load(std::memory_order_acquire);
    if (!published || certified_session.load(std::memory_order_acquire) != published) return false;
    if constexpr (player_world::kEnablePlayerWorldRearm) {
        return certified_epoch.load(std::memory_order_acquire) ==
                   bound_world_epoch.load(std::memory_order_acquire) && scope_current();
    }
    return true;
}

bool historical_census_expired(ULONGLONG now) noexcept {
    // A never increments applied, even after its roots affect B. Published
    // certificate history, not B's write counter, owns this absorbing expiry.
    const auto historical = certified_session.load(std::memory_order_acquire);
    const auto until = census_valid_until.load(std::memory_order_acquire);
    return historical && historical == published_session.load(std::memory_order_acquire) &&
           until && now > until;
}

void stop_census(unsigned reason) noexcept {
    census_stop_reason.store(reason, std::memory_order_relaxed);
    stopped.store(true, std::memory_order_release);
    active_scope.store(false, std::memory_order_release);
}

enum class Observation { complete, budget_incomplete, rejected, terminal };
template<class T> void set_peak(std::atomic<T>& peak, T value) noexcept {
    // Called only by the permit owner; readers are the transport worker.
    if (value > peak.load(std::memory_order_relaxed)) peak.store(value, std::memory_order_relaxed);
}
struct WorkBudget {
    bool owned{};
    unsigned site{};
    std::uint64_t begin{}, last{};
    std::uint64_t query_ticks{}, last_query_ticks{};
    std::uintptr_t read_address{}, query_address{};
    std::size_t read_bytes{};
    unsigned read_calls{}, scope_queries{};
    std::uint64_t ticket{}, window_begin{};
    bool query_pending{}, incomplete{}, read_refused{}, certificate_seen{};
    CostStopContext context() const noexcept {
        CostStopContext context{};
        context.site = site; context.scope_queries = scope_queries;
        context.window_queries = work_queries; context.read_calls = read_calls;
        context.read_address = read_address; context.query_address = query_address;
        context.read_bytes = read_bytes;
        context.scope_ticks = last >= begin ? last-begin : 0;
        context.window_ticks = work_spent; context.scope_query_ticks = query_ticks;
        context.window_query_ticks = work_query_ticks; context.last_query_ticks = last_query_ticks;
        context.query_pending = query_pending;
        context.read_refused = read_refused;
        return context;
    }
    void stop(unsigned reason) noexcept { stop_cost(reason, context()); }
    void reject_read() noexcept { read_refused = true; }
    Observation outcome(bool complete) const noexcept {
        // A simultaneous API/protection/SEH refusal is not harmless timeout.
        if (stopped.load(std::memory_order_acquire)) return Observation::terminal;
        if (read_refused) return Observation::rejected;
        if (incomplete) return Observation::budget_incomplete;
        return complete ? Observation::complete : Observation::rejected;
    }
    void defer(unsigned reason) noexcept {
        incomplete = true;
        // May run with the mailbox already held: atomics ONLY, no wait/lock.
        refresh_required.store(true, std::memory_order_release);
        active_scope.store(false, std::memory_order_release);
        if (paused_ticket != ticket) {
            paused_ticket = ticket;
            if (!budget_pauses.load(std::memory_order_relaxed)) {
                first_pause_context = context(); first_pause_reason = reason;
            }
            budget_pauses.fetch_add(1, std::memory_order_release);
        }
    }
    explicit WorkBudget(unsigned where) noexcept : site(where) {
        owned = !work_busy.test_and_set(std::memory_order_acquire);
        if (!owned) { work_skips.fetch_add(1, std::memory_order_relaxed); return; }
        LARGE_INTEGER now{};
        if (!qpc_frequency || !QueryPerformanceCounter(&now) || now.QuadPart <= 0 ||
            static_cast<std::uint64_t>(now.QuadPart) < work_window) { stop(kCostClock); return; }
        const auto value = static_cast<std::uint64_t>(now.QuadPart);
        // Never reset a window mid-scope: a stalled scope stays fully charged.
        if (!work_window || value-work_window >= qpc_frequency/20) {
            if (work_ticket == ~std::uint64_t{}) { stop(kCostClock); return; }
            ++work_ticket;
            work_window = value; work_spent = 0; work_queries = 0; work_query_ticks = 0;
            refresh_required.store(true, std::memory_order_release);
            active_scope.store(false, std::memory_order_release);
        }
        ticket = work_ticket; window_begin = work_window; begin = last = value;
        incomplete = paused_ticket == ticket;
    }
    WorkBudget(const WorkBudget&) = delete;
    WorkBudget& operator=(const WorkBudget&) = delete;
    bool check(bool query = false, std::uintptr_t query_at = 0) noexcept {
        if (!owned || !last || incomplete || stopped.load(std::memory_order_acquire)) return false;
        if (site >= 2 && !certificate_seen) {
            if (!certificate_current(ticket)) return false;
            // Only certificate METADATA is stable under this work permit, not
            // native memory. Every copy keeps its SEH/QPC and final setter
            // explicitly rechecks certificate/world/terminal state again.
            certificate_seen = true;
        }
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now) || now.QuadPart < 0 || static_cast<std::uint64_t>(now.QuadPart) < last) {
            stop(kCostClock); return false;
        }
        const auto value = static_cast<std::uint64_t>(now.QuadPart), delta = value-last;
        last = value; work_spent += delta;
        if (query_pending) {
            // Existing pre/post-protection-query QPC samples only: WALL bracket,
            // including scheduling/check/call overhead, NOT pure API CPU time.
            query_ticks += delta; work_query_ticks += delta; last_query_ticks = delta;
            query_pending = false;
        }
        cost_total_ticks.fetch_add(delta, std::memory_order_relaxed);
        set_peak(peak_scope_ticks, value-begin); set_peak(peak_window_ticks, work_spent);
        // Clock/observed read failures remain distinct from resource limits.
        // The 257th query is NOT issued: close this window and require a whole
        // new census, exactly as for time exhaustion. Never retry old fields.
        if (query && work_queries == 256) { defer(kCostQueries); return false; }
        if (work_spent >= qpc_frequency/1000) { defer(kCostTime); return false; }
        if (value-window_begin >= qpc_frequency/20) { defer(kCostWindow); return false; }
        if (stopped.load(std::memory_order_acquire)) return false;
        if (query) {
            ++work_queries; query_total.fetch_add(1, std::memory_order_relaxed); set_peak(peak_queries, work_queries);
            ++scope_queries; query_address = query_at; query_pending = true;
        }
        return true;
    }
    void finish() noexcept {
        if (owned) { check(); owned = false; work_busy.clear(std::memory_order_release); }
    }
    ~WorkBudget() noexcept { finish(); }
};

void append_budget_pause(std::ostringstream& line) {
    const auto count = budget_pauses.load(std::memory_order_acquire);
    line << " budgetPauses=" << count
         << " publishDeferrals=" << census_publish_deferrals.load(std::memory_order_relaxed)
         << " refreshRequired=" << refresh_required.load(std::memory_order_acquire)
         << " certifiedWindow=" << certified_ticket.load(std::memory_order_acquire);
    if (!count) return;
    const auto context = first_pause_context;
    line << " firstPauseReason=" << first_pause_reason << " firstPauseSite=" << context.site
         << " firstPauseScopeUs=" << context.scope_ticks*1000000/qpc_frequency
         << " firstPauseWindowUs=" << context.window_ticks*1000000/qpc_frequency
         << " firstPauseQueryUs=" << context.scope_query_ticks*1000000/qpc_frequency
         << " firstPauseReads=" << context.read_calls
         << " firstPauseReadRefused=" << context.read_refused;
}
void append_combat_status(std::ostringstream& line) {
    const bool bound=native_bindings_ready.load(std::memory_order_acquire);
    line << " combat=" << combat_enabled() << " livesReady=" << bound
         << " boundLives=" << (bound ? native_binding_count : 0)
         << " deathEvents=" << combat_death_events.load() << " deathReceived=" << combat_death_received.load()
         << " nativeAttempts=" << combat_native_attempts.load() << " nativeCommits=" << combat_native_commits.load()
         << " deathResults=" << combat_death_results.load() << " nativeRefused=" << combat_native_refusals.load()
         << " retiring=" << combat_retiring_observed.load() << " prebindRefused=" << combat_prebind_refused.load()
         << " unsupportedBPrediction=" << combat_b_prediction.load()
         << " relocationRefreshes=" << combat_relocation_refreshes.load();
    const bool control_ready=control_book_ready.load(std::memory_order_acquire);
    const bool closed=control_closed.load(std::memory_order_acquire);
    line << " controlReady=" << control_ready << " controlClosed=" << closed
         << " controlCloseReason=" << static_cast<unsigned>(control_close_reason.load(std::memory_order_acquire))
         << " controlDegraded=" << (control_ready && (stopped.load(std::memory_order_acquire) || closed ||
            control_refusals.load() || control_refusals_received.load() || control_delivery_failed.load()))
         << " eventReceipts=" << control_receipts.load() << " refusedCreated=" << control_refusals.load()
         << " refusedReceived=" << control_refusals_received.load() << " reportAcks=" << control_report_acks.load()
         << " deliveryFailed=" << control_delivery_failed.load() << " controlPending=" << control_pending.load(std::memory_order_acquire);
    combat_native::append_observer_status(line);
    const auto reason=first_census_reject.load(std::memory_order_acquire);
    if (reason==CensusReject::none) return;
    const auto context=census_reject_context;
    line << " censusReject=" << static_cast<unsigned>(reason)
         << " censusStableLives=" << context.stable_count << " censusCurrentLives=" << context.current_count
         << " censusOldIndex=" << context.old_index << " censusNewIndex=" << context.new_index
         << " censusHeaderMask=" << context.header_mask << " censusLocalMask=" << context.local_mask
         << " censusWitness=" << context.witness_bits
         << " censusNameFirstDiff=" << context.name_first_diff;
    if (context.name_first_diff<wire::kNameBytes)
        line << " censusOldNameByte=" << static_cast<unsigned>(static_cast<unsigned char>(context.old_local.name[context.name_first_diff]))
             << " censusNewNameByte=" << static_cast<unsigned>(static_cast<unsigned char>(context.new_local.name[context.name_first_diff]));
    line << std::hex
         << " censusOldHeader=" << context.old_header[0] << ',' << context.old_header[1] << ',' << context.old_header[2]
         << " censusNewHeader=" << context.new_header[0] << ',' << context.new_header[1] << ',' << context.new_header[2]
         << " censusOldWorld=" << context.old_world << " censusNewWorld=" << context.new_world
         << " censusOldPlayerId=" << context.old_player_id << " censusNewPlayerId=" << context.new_player_id
         << " censusOldLocal=" << context.old_local.actor << ',' << context.old_local.record << ',' << context.old_local.generation
         << ',' << context.old_local.spawner << ',' << context.old_local.spawner_id << ',' << context.old_local.desired_id << ',' << context.old_local.variant_id
         << " censusNewLocal=" << context.new_local.actor << ',' << context.new_local.record << ',' << context.new_local.generation
         << ',' << context.new_local.spawner << ',' << context.new_local.spawner_id << ',' << context.new_local.desired_id << ',' << context.new_local.variant_id
         << std::dec;
}

void append_cost_stop_context(std::ostringstream& line) {
    const auto reason = cost_stop.load(std::memory_order_acquire);
    if (!reason) return;
    const auto context = cost_stop_context;
    const auto micros = [](std::uint64_t ticks) { return ticks*1000000/qpc_frequency; };
    const auto other = context.scope_ticks >= context.scope_query_ticks
        ? context.scope_ticks-context.scope_query_ticks : 0;
    line << " terminalCostStop=" << reason << " terminalSite=" << context.site
         << " terminalScopeUs=" << micros(context.scope_ticks)
         << " terminalWindowUs=" << micros(context.window_ticks)
         << " terminalQueryBracketUs=" << micros(context.scope_query_ticks)
         << " terminalOtherUs=" << micros(other)
         << " terminalWindowQueryBracketUs=" << micros(context.window_query_ticks)
         << " terminalLastQueryBracketUs=" << micros(context.last_query_ticks)
         << " terminalScopeQueries=" << context.scope_queries
         << " terminalWindowQueries=" << context.window_queries
         << " terminalReadCalls=" << context.read_calls
         << std::hex << " terminalRead=0x" << context.read_address
         << " terminalQuery=0x" << context.query_address << std::dec
         << " terminalReadBytes=" << context.read_bytes
         << " terminalQueryPending=" << context.query_pending;
}

bool publish_native_life_event(const NativeLifeEvent& event) noexcept {
    if (!combat_enabled() || !combat_ready() || stopped.load(std::memory_order_acquire) ||
        event.worldEpoch!=bound_world_epoch.load(std::memory_order_acquire) || !scope_current() ||
        !effect_slice_current(GetTickCount64())) return false;
    if (!native_bindings_ready.load(std::memory_order_acquire)) {
        combat_prebind_refused.fetch_add(1,std::memory_order_relaxed); return false;
    }
    const unsigned bit=event.kind==NativeLifeKind::retiring ? kNativeRetiring :
        event.kind==NativeLifeKind::dead_commit ? kNativeDead : 0;
    if (!bit) return false;
    for (std::size_t i=0; i<native_binding_count; ++i) {
        const auto& binding=native_bindings[i];
        if (binding.world==event.worldEpoch && binding.record==event.record && binding.generation==event.generation &&
            binding.session==published_session.load(std::memory_order_acquire)) {
            if (bit==kNativeDead) commit_first_effect(GetTickCount64());
            const auto old=native_life_bits[i].fetch_or(bit,std::memory_order_release);
            if (!(old&bit)) {
                if (bit==kNativeDead) combat_native_commits.fetch_add(1,std::memory_order_relaxed);
                else combat_retiring_observed.fetch_add(1,std::memory_order_relaxed);
            }
            return true;
        }
    }
    return false; // prebind/foreign generation is unsupported, never adopted.
}
bool peek_native_death(std::uintptr_t record,std::uint32_t generation,NativeDeathAttempt& out) noexcept {
    if (!combat_enabled() || !g_net_role_b || !combat_hooks_ready.load(std::memory_order_acquire) ||
        !native_bindings_ready.load(std::memory_order_acquire) || stopped.load(std::memory_order_acquire) ||
        !scope_current() || !TryAcquireSRWLockExclusive(&lock)) return false;
    drain_native_life_locked();
    const auto now=GetTickCount64();
    if (!effect_slice_current(now)) { ReleaseSRWLockExclusive(&lock); return false; }
    const auto effect_start=first_apply.load(std::memory_order_acquire);
    bool found{};
    for (std::size_t i=0; i<native_binding_count; ++i) {
        const auto& binding=native_bindings[i]; const auto& row=combat_rows[i];
        if (binding.record==record && binding.generation==generation && binding.session==session && binding.peer==peer &&
            row.death_event && row.authority_dead && !row.attempted_event && !row.local_retiring && !row.local_dead &&
            row.result==wire::combat::Result::none && row.control_started && now>=row.control_started &&
            now-row.control_started<=30000) {
            out={binding.key,binding.world,session,peer,row.life,row.death_event,row.revision,record,generation,
                effect_start ? effect_start : now,row.control_started};
            found=true; break;
        }
    }
    ReleaseSRWLockExclusive(&lock); return found;
}
bool try_begin_native_death(const NativeDeathAttempt& attempt) noexcept {
    if (!combat_enabled() || !g_net_role_b || !combat_ready() || !native_bindings_ready.load(std::memory_order_acquire) ||
        stopped.load(std::memory_order_acquire) || !scope_current() ||
        !TryAcquireSRWLockExclusive(&lock)) return false;
    drain_native_life_locked(); bool begun{};
    const auto now=GetTickCount64();
    if (!attempt.effectStart || now<attempt.effectStart || now-attempt.effectStart>30000 ||
        !attempt.controlStart || now<attempt.controlStart || now-attempt.controlStart>30000 ||
        !effect_slice_current(now)) { ReleaseSRWLockExclusive(&lock); return false; }
    for (std::size_t i=0; i<roster.count; ++i) {
        auto& row=combat_rows[i]; const auto& local=roster.rows[i];
        if (attempt.session==session && attempt.peer==peer && attempt.worldEpoch==roster.world_epoch &&
            attempt.lifeId==row.life && attempt.eventId==row.death_event && attempt.rowRevision==row.revision &&
            attempt.controlStart==row.control_started &&
            attempt.record==local.record && attempt.generation==local.generation && attempt.key==local.key &&
            row.authority_dead && !row.attempted_event && !row.local_retiring && !row.local_dead &&
            row.result==wire::combat::Result::none &&
            !(native_life_bits[i].load(std::memory_order_acquire)&(kNativeRetiring|kNativeDead))) {
            row.attempted_event=attempt.eventId;
            combat_native_attempts.fetch_add(1,std::memory_order_relaxed);
            native_attempt_event[i].store(attempt.eventId,std::memory_order_release); begun=true; break;
        }
    }
    refresh_native_pending_locked(); ReleaseSRWLockExclusive(&lock); return begun;
}
bool native_death_attempt_current(const NativeDeathAttempt& attempt) noexcept {
    const auto now=GetTickCount64();
    if (!combat_enabled() || !g_net_role_b || !combat_ready() ||
        !native_bindings_ready.load(std::memory_order_acquire) || stopped.load(std::memory_order_acquire) ||
        !attempt.effectStart || now<attempt.effectStart || now-attempt.effectStart>30000 ||
        !attempt.controlStart || now<attempt.controlStart || now-attempt.controlStart>30000 ||
        !effect_slice_current(now) || !scope_current() ||
        published_session.load(std::memory_order_acquire)!=attempt.session) return false;
    for (std::size_t i=0; i<native_binding_count; ++i) {
        const auto& binding=native_bindings[i];
        if (binding.world==attempt.worldEpoch && binding.session==attempt.session && binding.peer==attempt.peer &&
            binding.life==attempt.lifeId && binding.record==attempt.record && binding.generation==attempt.generation &&
            binding.key==attempt.key && native_attempt_event[i].load(std::memory_order_acquire)==attempt.eventId)
            return !(native_life_bits[i].load(std::memory_order_acquire)&
                (kNativeRetiring|kNativeDead|kNativeReturned|kNativeRefused|kNativeFault));
    }
    return false;
}
void finish_native_death_attempt(const NativeDeathAttempt& attempt,NativeAttemptStatus status) noexcept {
    if (!combat_enabled() || !native_bindings_ready.load(std::memory_order_acquire)) return;
    for (std::size_t i=0; i<native_binding_count; ++i) {
        const auto& binding=native_bindings[i];
        if (binding.world!=attempt.worldEpoch || binding.session!=attempt.session || binding.peer!=attempt.peer ||
            binding.life!=attempt.lifeId || binding.record!=attempt.record || binding.generation!=attempt.generation ||
            native_attempt_event[i].load(std::memory_order_acquire)!=attempt.eventId) continue;
        const unsigned bit=status==NativeAttemptStatus::native_returned ? kNativeReturned :
            status==NativeAttemptStatus::native_fault ? kNativeFault : kNativeRefused;
        if (status==NativeAttemptStatus::native_returned) commit_first_effect(attempt.effectStart);
        native_life_bits[i].fetch_or(bit,std::memory_order_release);
        if (status==NativeAttemptStatus::refused) combat_native_refusals.fetch_add(1,std::memory_order_relaxed);
        if (status==NativeAttemptStatus::native_fault) {
            native_apply_fault.store(true,std::memory_order_release);
            stopped.store(true,std::memory_order_release); refresh_required.store(true,std::memory_order_release);
            active_scope.store(false,std::memory_order_release);
        }
        return; // native_returned is NEVER a positive result.
    }
}

bool reject_combat_census_locked(CensusReject reason,const Roster& current,std::size_t old_index,
                                 std::size_t new_index,unsigned header_mask=0) noexcept {
    if (first_census_reject.load(std::memory_order_relaxed)!=CensusReject::none) return false;
    CensusRejectContext context{};
    context.stable_count=roster.count; context.current_count=current.count;
    context.old_index=static_cast<std::uint32_t>(old_index); context.new_index=static_cast<std::uint32_t>(new_index);
    context.header_mask=header_mask;
    context.old_header={roster.manager,roster.array,roster.player};
    context.new_header={current.manager,current.array,current.player};
    context.old_world=roster.world_epoch; context.new_world=current.world_epoch;
    context.old_player_id=roster.player_id; context.new_player_id=current.player_id;
    const auto summarize=[](const Local& local) noexcept {
        return CensusLocalSummary{local.actor,local.record,local.spawner,local.generation,
            local.key.spawner_id,local.key.desired_id,local.key.variant_id,local.key.name};
    };
    if (old_index<roster.count) {
        context.old_local=summarize(roster.rows[old_index]);
        context.witness_bits=native_life_bits[old_index].load(std::memory_order_acquire);
    }
    if (new_index<current.count) context.new_local=summarize(current.rows[new_index]);
    const auto& old=context.old_local; const auto& fresh=context.new_local;
    // Local mask: actor,record,generation,spawner,spawnerId,D8,DC,name.
    context.local_mask=(old.actor!=fresh.actor ? 1u : 0u)|(old.record!=fresh.record ? 2u : 0u)|
        (old.generation!=fresh.generation ? 4u : 0u)|(old.spawner!=fresh.spawner ? 8u : 0u)|
        (old.spawner_id!=fresh.spawner_id ? 16u : 0u)|(old.desired_id!=fresh.desired_id ? 32u : 0u)|
        (old.variant_id!=fresh.variant_id ? 64u : 0u)|(old.name!=fresh.name ? 128u : 0u);
    context.name_first_diff=static_cast<unsigned>(wire::kNameBytes);
    for (std::size_t i=0; i<wire::kNameBytes; ++i) if (old.name[i]!=fresh.name[i]) {
        context.name_first_diff=static_cast<unsigned>(i); break;
    }
    census_reject_context=context;
    first_census_reject.store(reason,std::memory_order_release);
    return false;
}
bool reconcile_combat_census_locked(const Roster& current) noexcept {
    // Header mask: manager,array,world,player,player generation. A normal
    // CharacterManager add/remove does NOT reallocate its fixed entry array.
    const unsigned header_mask=(current.manager!=roster.manager ? 1u : 0u)|
        (current.array!=roster.array ? 2u : 0u)|(current.world_epoch!=roster.world_epoch ? 4u : 0u)|
        (current.player!=roster.player ? 8u : 0u)|(current.player_id!=roster.player_id ? 16u : 0u);
    if (header_mask) return reject_combat_census_locked(CensusReject::header,current,wire::kActors,wire::kActors,header_mask);
    std::array<bool,wire::kActors> seen{};
    std::array<std::size_t,wire::kActors> mapping{};
    for (std::size_t i=0; i<current.count; ++i) {
        const auto& fresh=current.rows[i];
        std::size_t index=roster.count;
        for (std::size_t j=0; j<roster.count; ++j)
            if (fresh.record==roster.rows[j].record && fresh.generation==roster.rows[j].generation) { index=j; break; }
        if (index==roster.count) {
            // Diagnostic candidate ONLY: never admit a new generation or use
            // key/address similarity to adopt an unknown weak identity.
            std::size_t candidate=wire::kActors;
            for (std::size_t j=0; j<roster.count; ++j) if (fresh.record==roster.rows[j].record) { candidate=j; break; }
            if (candidate==wire::kActors) for (std::size_t j=0; j<roster.count; ++j)
                if (fresh.key==roster.rows[j].key) { candidate=j; break; }
            return reject_combat_census_locked(CensusReject::unknown_weak,current,candidate,i);
        }
        if (fresh.key!=roster.rows[index].key)
            return reject_combat_census_locked(CensusReject::changed_key,current,index,i);
        if (seen[index]) return reject_combat_census_locked(CensusReject::duplicate,current,index,i);
        const auto& old=roster.rows[index];
        if ((fresh.actor!=old.actor || fresh.spawner!=old.spawner) && combat_rows[index].revision==~std::uint64_t{})
            return reject_combat_census_locked(CensusReject::revision_overflow,current,index,i);
        seen[index]=true; mapping[i]=index;
    }
    for (std::size_t i=0; i<roster.count; ++i)
        if (!seen[i] && !combat_known_retiring(roster.rows[i].record,roster.rows[i].generation))
            return reject_combat_census_locked(CensusReject::missing,current,i,wire::kActors);
    // Only AFTER every complete-census row and missing-life check succeeds.
    // RelocatableHeap1415FDF01/0D/1F preserves Process A8/BC and rebinds record+8;
    // Pirate+08=1404A9490 fixes its owned pointers. The fresh identity pass has
    // already validated CURRENT actor/backlink/gen/type/OnKill and reciprocal
    // spawner/fullKey. This is between-pass metadata refresh, NOT a lifetime
    // lease. No life/session/sequence/stamp or immutable binding is rewritten.
    for (std::size_t i=0; i<current.count; ++i) {
        const auto index=mapping[i]; auto& old=roster.rows[index]; const auto& fresh=current.rows[i];
        if (fresh.actor!=old.actor || fresh.spawner!=old.spawner) {
            old.actor=fresh.actor; old.spawner=fresh.spawner;
            ++combat_rows[index].revision;
            combat_relocation_refreshes.fetch_add(1,std::memory_order_relaxed);
        }
    }
    return true; // only positive native retiring/dead witnesses may disappear.
}

DWORD WINAPI stop_waiter(void*) {
    // No logging, sockets, RNG or game pointers on this emergency path.
    bool acknowledged{};
    for (;;) {
        const DWORD state = WaitForSingleObject(stop_request, 50);
        if (state != WAIT_TIMEOUT) {
            external_stop.store(true);
            close_control(ControlClose::external);
        }
        if (!stopped.load(std::memory_order_acquire)) continue;
        if (!acknowledged) {
            // ACK still means native work is quiescent, NOT that the values-
            // only critical-event transport is closed after a writer failure.
            while (work_busy.test_and_set(std::memory_order_acquire)) Sleep(1);
            active_scope.store(false, std::memory_order_release);
            SetEvent(stop_ack);
            work_busy.clear(std::memory_order_release);
            acknowledged=true;
        }
        if (!combat_enabled() || !control_book_ready.load(std::memory_order_acquire) ||
            control_closed.load(std::memory_order_acquire) || !g_state_net.running.load(std::memory_order_acquire)) return 0;
        // Retain this existing listener so a later explicit stop closes the
        // surviving control socket too. No new thread and no native rearm.
    }
}
bool start_stop_control() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return false;
    const auto stamp = (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
    char request_name[128]{}, ack_name[128]{}, control_ack_name[160]{};
    std::snprintf(request_name, sizeof(request_name), "Local\\TllEnemyRootStop_v1_%lu_%016llX", GetCurrentProcessId(), stamp);
    std::snprintf(ack_name, sizeof(ack_name), "%s_ACK", request_name);
    std::snprintf(control_ack_name, sizeof(control_ack_name), "%s_CONTROL_CLOSED", request_name);
    const auto fail = [] {
        if (stop_request) CloseHandle(stop_request);
        if (stop_ack) CloseHandle(stop_ack);
        if (control_close_ack) CloseHandle(control_close_ack);
        stop_request = stop_ack = control_close_ack = nullptr; return false;
    };
    stop_request = CreateEventA(nullptr, TRUE, FALSE, request_name);
    if (!stop_request || GetLastError() == ERROR_ALREADY_EXISTS) return fail();
    stop_ack = CreateEventA(nullptr, TRUE, FALSE, ack_name);
    if (!stop_ack || GetLastError() == ERROR_ALREADY_EXISTS) return fail();
    control_close_ack = CreateEventA(nullptr, TRUE, FALSE, control_ack_name);
    if (!control_close_ack || GetLastError() == ERROR_ALREADY_EXISTS) return fail();
    const HANDLE thread = CreateThread(nullptr, 0, stop_waiter, nullptr, 0, nullptr);
    if (!thread) return fail();
    CloseHandle(thread);
    return true;
}

struct ReadCache {
    std::array<std::uintptr_t, 64> pages{};
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
bool refuse_copy_read(WorkBudget* budget) noexcept {
    if (budget) budget->reject_read();
    return false;
}
bool copy(std::uintptr_t at, void* out, std::size_t size, ReadCache* cache = nullptr) noexcept {
    auto* budget = cache ? cache->budget : nullptr;
    if (budget) { budget->read_address = at; budget->read_bytes = size; ++budget->read_calls; }
    if (budget && !budget->check()) return false;
    if (!size || size > 4096 || at < 0x10000 || at > 0x7FFFFFFFFFFFULL-size) return refuse_copy_read(budget);
    if (cache) {
        // Query ONLY the one/two requested native pages, not the remainder of
        // a potentially enormous same-protection game-heap region. This is
        // lexical observation, not page residency/protection or identity lease.
        const auto page_size = system_page_size;
        if (page_size < 4096 || (page_size & (page_size-1))) return refuse_copy_read(budget);
        const auto mask = page_size-1;
        const auto first = at & ~mask;
        const auto last = (at+size-1) & ~mask; // bounded canonical range above.
        if (last < first || last-first > page_size) return refuse_copy_read(budget);
        std::array<PSAPI_WORKING_SET_EX_INFORMATION, 2> query{};
        std::size_t missing{};
        for (auto page = first;; page = last) {
            bool found{};
            for (std::size_t i = 0; i < cache->count; ++i) {
                if (cache->pages[i] == page) { found = true; break; }
            }
            if (!found) {
                if (missing == query.size()) return refuse_copy_read(budget);
                query[missing++].VirtualAddress = reinterpret_cast<void*>(page);
            }
            if (page == last) break;
        }
        if (missing) {
            if (budget && !budget->check(true, reinterpret_cast<std::uintptr_t>(query[0].VirtualAddress))) return false;
            const auto queried = QueryWorkingSetEx(GetCurrentProcess(), query.data(),
                static_cast<DWORD>(missing*sizeof(query[0])));
            // Preserve observed negative evidence BEFORE the one post-query
            // clock check can also report exhaustion. Inspect only this owned
            // API result: no game read or cache admission after budget failure.
            bool readable = queried != FALSE;
            for (std::size_t i = 0; readable && i < missing; ++i) {
                const auto& attributes = query[i].VirtualAttributes;
                // With Valid==0, protection/other valid-layout members are
                // undefined. Reject without touching or faulting in that page.
                if (!attributes.Valid) { readable = false; break; }
                if (attributes.Bad) { readable = false; break; }
                switch (attributes.Win32Protection) {
                case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
                case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:
                    break;
                default: readable = false; break; // observed GUARD/NOACCESS/modifiers/unknown.
                }
            }
            if (!readable && budget) budget->reject_read();
            const bool within_budget = !budget || budget->check();
            if (!within_budget || !readable) return false;
            for (std::size_t i = 0; i < missing && cache->count < cache->pages.size(); ++i)
                cache->pages[cache->count++] = reinterpret_cast<std::uintptr_t>(query[i].VirtualAddress);
        }
    } else {
        // Cold installer-only reads may target not-yet-resident image pages.
        // Preserve their original region guard; NEVER fall back here from a
        // rejected hot page query and never share this admission across calls.
        for (auto cursor = at; cursor < at+size;) {
            MEMORY_BASIC_INFORMATION m{};
            const auto queried = VirtualQuery(reinterpret_cast<void*>(cursor), &m, sizeof(m));
            if (queried != sizeof(m) || m.State != MEM_COMMIT ||
                (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(m.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
            const auto end = reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;
            if (end <= cursor) return false;
            cursor = end;
        }
    }
    // Reject observed guard protection above. A later native protection change
    // can still fault/consume a guard at memcpy; SEH is not a protection lease.
    bool copied{};
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), size); copied = true; }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
        GetExceptionCode() == EXCEPTION_GUARD_PAGE ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
    // A caught memory fault is not a merely incomplete observation, even when
    // the same operation also crosses the window's time/query allowance.
    if (!copied && budget) budget->reject_read();
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
bool roster_scope_current(const Roster& value, ReadCache& cache,
                          bool check_player_generation = false) noexcept {
    if constexpr (player_world::kEnablePlayerWorldRearm) {
        // Primary was only admission, never input to ordinary() or the setter.
        // Do not dereference it in this mode. This relies on native lifecycle
        // close-before-retire wiring, not an atomic-snapshot lifetime lease.
        return value.world_epoch &&
               value.world_epoch == bound_world_epoch.load(std::memory_order_acquire) &&
               scope_current();
    } else {
        if (value.player != current_player(cache)) return false;
        if (!check_player_generation) return true;
        std::uint32_t generation{};
        return copy(value.player+0xBC, &generation, sizeof(generation), &cache) &&
               generation == value.player_id;
    }
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
        // checks protection/SEH; do not query the same name page twice.
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
    // Reuse exact-page checks inside this pass, never across ticks/native phases.
    ReadCache cache{}; cache.budget = &budget;
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), &cache); };
    Roster value{};
    if constexpr (player_world::kEnablePlayerWorldRearm) {
        value.world_epoch = bound_world_epoch.load(std::memory_order_acquire);
        if (!roster_scope_current(value, cache)) return false;
    } else {
        value.player = current_player(cache);
        if (!value.player || !read(value.player+0xBC, value.player_id) || !value.player_id) return false;
    }
    if (!read(g_module+0x3898630, value.manager) || !read(value.manager, value.array)) return false;
    std::uint32_t count{};
    if (!read(value.manager+0xC, count) || count > 64 || (count && !value.array)) return false;
    // Owned initial entries, not an atomic roster or a cross-phase lease. Copy
    // the complete bounded range through the same protection/SEH/budget path;
    // every entry's object/identity and the final manager tuple still follow.
    // No value-initialization: unused entries must not add another 1KiB clear.
    std::array<RetainedEntry, 64> entries;
    static_assert(std::is_trivial_v<RetainedEntry> &&
                  std::is_trivially_copyable_v<RetainedEntry>);
    if (count && !copy(value.array, entries.data(),
                       static_cast<std::size_t>(count)*sizeof(RetainedEntry), &cache)) return false;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& entry = entries[index];
        // Only the bound CURRENT native D08/HumanDead observer can supply this
        // exclusion. Absence, HP0, a failed read and a new generation cannot.
        if (combat_known_retiring(entry.record,entry.id)) continue;
        std::uintptr_t actor{}, vt{}; std::uint32_t id{};
        if (!entry.record || !read(entry.record+8, actor) || !read(actor, vt) || !read(actor+0xBC, id) || id != entry.id) return false;
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
    if constexpr (player_world::kEnablePlayerWorldRearm) {
        if (!roster_scope_current(value, cache)) return false;
    }
    out = value; return true;
}
void reset_locked() {
    active_scope.store(false, std::memory_order_release);
    refresh_required.store(true, std::memory_order_release);
    certified_ticket.store(0, std::memory_order_release);
    certified_session.store(0, std::memory_order_release);
    certified_epoch.store(0, std::memory_order_release);
    census_valid_until.store(0, std::memory_order_release);
    published_session.store(0, std::memory_order_release);
    session = peer = 0; confirmed = false; peer_seen = last_hello = 0; hello_sequence = 0;
    roster = {}; rows = {};
    // Never rewrite a published observer table. Terminal stop is absorbing;
    // initial prebind retries have no native observer readers yet.
    if (!native_bindings_ready.load(std::memory_order_acquire)) {
        combat_rows={}; encounter_sealed=false;
    }
    native_death_pending_hint.store(false,std::memory_order_release);
}
void receive_frozen_control(const wire::combat::Packet& packet,ULONGLONG now) noexcept {
    if (!control_current() || !TryAcquireSRWLockExclusive(&lock)) return;
    if (!control_current() || packet.sender!=control_book.peer || packet.receiver!=control_book.session) {
        ReleaseSRWLockExclusive(&lock); return;
    }
    if (packet.kind==wire::combat::Kind::hello) {
        if (!control_hello_sequence || wire::newer(packet.sequence,control_hello_sequence)) {
            control_hello_sequence=packet.sequence;
            // Maintain existing root freshness ONLY while its unchanged book
            // remains admitted. Never resurrect reset/stopped root metadata.
            if (!stopped.load(std::memory_order_acquire) && session==control_book.session && peer==control_book.peer) {
                hello_sequence=packet.sequence; peer_seen=now; confirmed=true;
            }
        }
        ReleaseSRWLockExclusive(&lock); return;
    }
    drain_native_life_locked(); // previously admitted numeric observations ONLY
    std::size_t index=control_book.count;
    for (std::size_t i=0; i<control_book.count; ++i)
        if (control_book.lives[i].life==packet.life_id && control_book.lives[i].key==packet.key) { index=i; break; }
    if (index<control_book.count) {
        auto& row=combat_rows[index];
        if (g_net_role_b && packet.kind==wire::combat::Kind::bind && packet.manifest_count==control_book.count) {
            row.bind_reply=true; row.last_control=0;
        } else if (g_net_role_b && packet.kind==wire::combat::Kind::death &&
                   (!row.death_event || row.death_event==packet.event_id)) {
            if (!row.death_event) {
                row.death_event=packet.event_id; row.authority_dead=true; row.control_started=now;
                if (row.revision==~std::uint64_t{}) {
                    stopped.store(true,std::memory_order_release); active_scope.store(false,std::memory_order_release);
                } else ++row.revision;
                combat_death_received.fetch_add(1,std::memory_order_relaxed);
            }
            row.receipt_reply=true; row.last_control=0;
            update_death_result_locked(index,native_life_bits[index].load(std::memory_order_acquire));
        } else if (row.death_event && row.death_event==packet.event_id && row.authority_dead) {
            if (!g_net_role_b && packet.kind==wire::combat::Kind::received) {
                if (!row.receipt_seen) { row.receipt_seen=true; control_receipts.fetch_add(1,std::memory_order_relaxed); }
            } else if (!g_net_role_b && (packet.kind==wire::combat::Kind::result || packet.kind==wire::combat::Kind::refused)) {
                if (!row.receipt_seen) { row.receipt_seen=true; control_receipts.fetch_add(1,std::memory_order_relaxed); }
                if (packet.kind==wire::combat::Kind::result) {
                    if (!row.result_confirmed) {
                        row.result_confirmed=true; row.remote_result=packet.result;
                        combat_death_results.fetch_add(1,std::memory_order_relaxed);
                    }
                    row.remote_refusal=wire::combat::Refusal::none;
                } else if (row.remote_result==wire::combat::Result::none && !row.remote_response) {
                    row.remote_refusal=packet.refusal;
                    control_refusals_received.fetch_add(1,std::memory_order_relaxed);
                }
                // A positive witness may supersede a prior refusal, but a
                // delayed refusal can NEVER downgrade a committed result.
                row.remote_response=true; row.response_ack_reply=true; row.last_control=0;
            } else if (g_net_role_b && packet.kind==wire::combat::Kind::received) {
                const bool positive=row.result!=wire::combat::Result::none && packet.result==row.result &&
                    packet.refusal==wire::combat::Refusal::none;
                const bool negative=row.result==wire::combat::Result::none && packet.result==wire::combat::Result::none &&
                    row.refusal!=wire::combat::Refusal::none && packet.refusal==row.refusal;
                if ((positive || negative) && !row.report_acked) {
                    row.report_acked=true; control_report_acks.fetch_add(1,std::memory_order_relaxed);
                }
            }
        }
    }
    refresh_control_reports_locked(now);
    refresh_native_pending_locked(); // stopped native APIs still reject unconditionally
    ReleaseSRWLockExclusive(&lock);
}
void receive(const void* bytes, int size, const sockaddr_in& source, int source_size, ULONGLONG poll_started) {
    if (source_size < sizeof(source) ||
        source.sin_family != transport_peer.sin_family || source.sin_addr.s_addr != transport_peer.sin_addr.s_addr ||
        source.sin_port != transport_peer.sin_port) return;
    wire::Packet packet{};
    wire::combat::Packet modern{};
    if (combat_enabled()) {
        if (size!=sizeof(modern)) return;
        std::memcpy(&modern,bytes,sizeof(modern));
        if (!wire::combat::valid(modern) || modern.role_b==static_cast<std::uint8_t>(g_net_role_b)) return;
        packet.role_b=modern.role_b; packet.sender=modern.sender; packet.receiver=modern.receiver;
        packet.sequence=modern.sequence; packet.key=modern.key; packet.position=modern.position; packet.rotation=modern.rotation;
        packet.kind=modern.kind==wire::combat::Kind::hello ? wire::Kind::hello : wire::Kind::root;
    } else {
        if (size!=sizeof(packet)) return;
        std::memcpy(&packet,bytes,sizeof(packet));
        if (!wire::valid(packet) || packet.role_b==static_cast<std::uint8_t>(g_net_role_b)) return;
    }
    const auto now = GetTickCount64();
    if (combat_enabled() && control_book_ready.load(std::memory_order_acquire) && modern.kind!=wire::combat::Kind::root) {
        receive_frozen_control(modern,now); return;
    }
    if (combat_enabled() && !effect_slice_current(now)) return;
    if (now-poll_started > 100) {
        worker_gap_ms.store(now-poll_started, std::memory_order_relaxed);
        stopped.store(true); active_scope.store(false, std::memory_order_release); return;
    }
    // Enemy UDP is lossy; never wait for a native animation job.
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    if (!session || stopped.load(std::memory_order_relaxed)) { ReleaseSRWLockExclusive(&lock); return; }
    if (combat_enabled() && modern.kind!=wire::combat::Kind::hello && modern.kind!=wire::combat::Kind::root) {
        if (!confirmed || modern.sender!=peer || modern.receiver!=session || now-peer_seen>1000 || !encounter_sealed) {
            ReleaseSRWLockExclusive(&lock); return;
        }
        drain_native_life_locked();
        std::size_t index=roster.count;
        for (std::size_t i=0; i<roster.count; ++i) if (roster.rows[i].key==modern.key) { index=i; break; }
        if (index<roster.count) {
            auto& row=combat_rows[index];
            if (modern.kind==wire::combat::Kind::bind && g_net_role_b) {
                bool unique=true;
                for (std::size_t i=0; i<roster.count; ++i)
                    if (i!=index && combat_rows[i].life==modern.life_id) unique=false;
                if (unique && modern.manifest_count==roster.count && (!row.life || row.life==modern.life_id)) {
                    row.life=modern.life_id; row.bound=row.bind_confirmed=true; row.bind_reply=true; row.last_control=0;
                    publish_native_bindings_locked();
                }
            } else if (modern.life_id==row.life && row.bound && row.bind_confirmed) {
                if (modern.kind==wire::combat::Kind::death && g_net_role_b &&
                    native_bindings_ready.load(std::memory_order_acquire) && (!row.death_event || row.death_event==modern.event_id)) {
                    if (!row.death_event) {
                        row.death_event=modern.event_id; row.authority_dead=true; ++row.revision;
                        row.control_started=now;
                        combat_death_received.fetch_add(1,std::memory_order_relaxed);
                    }
                    update_death_result_locked(index,native_life_bits[index].load(std::memory_order_acquire));
                    row.last_control=0;
                } else if (modern.kind==wire::combat::Kind::result && !g_net_role_b &&
                    row.authority_dead && row.death_event==modern.event_id && !row.result_confirmed) {
                    row.result_confirmed=true; combat_death_results.fetch_add(1,std::memory_order_relaxed);
                }
            } else if (modern.kind==wire::combat::Kind::bound && !g_net_role_b && row.bound && modern.life_id==row.life &&
                       modern.manifest_count==roster.count) {
                row.bind_confirmed=true; publish_native_bindings_locked();
            }
        }
        refresh_native_pending_locked();
        ReleaseSRWLockExclusive(&lock); return;
    }
    if (packet.kind == wire::Kind::hello) {
        if (hello_sequence && now-peer_seen < 2000 && !wire::newer(packet.sequence, hello_sequence)) {
            ReleaseSRWLockExclusive(&lock); return;
        }
        if (peer != packet.sender) {
            if (first_apply.load(std::memory_order_acquire) || captured.load(std::memory_order_relaxed) || (combat_enabled() && encounter_sealed)) {
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
            if (combat_enabled() && (modern.life_id!=combat_rows[i].life || !combat_row_writable(i))) break;
            auto& row = rows[i];
            if (!row.last_sequence || wire::newer(packet.sequence, row.last_sequence)) {
                row.incoming = packet; row.received = now; row.last_sequence = packet.sequence;
                if (combat_enabled()) { combat_rows[i].root_seen=true; combat_rows[i].bind_reply=false; }
                matched.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
void tick() {
    if (stop_for_player_lifecycle()) return;
    if (!installed.load(std::memory_order_acquire) || !g_state_net.enabled || !g_state_net.peer_set) return;
    if (stopped.load()) {
        active_scope.store(false, std::memory_order_release);
        return;
    }
    if (combat_enabled() && !effect_slice_current(GetTickCount64())) return;
    if (!startup_admitted()) return;
    if (ticking.test_and_set()) return;
    if (historical_census_expired(GetTickCount64())) {
        stop_census(3); ticking.clear(); return;
    }
    const auto now = GetTickCount64();
    if (now-last_tick < 50 && !refresh_required.load(std::memory_order_acquire)) { ticking.clear(); return; }
    if constexpr (player_world::kEnablePlayerWorldRearm) {
        if (stop_for_player_lifecycle() || !scope_current()) { ticking.clear(); return; }
    }
    WorkBudget budget(1);
    // No observation was attempted on contention. Do not refresh the existing
    // 150ms lease or last_tick; the next game update can try without waiting.
    if (!budget.check()) { ticking.clear(); return; }
    last_tick = now;
    Roster current{};
    bool valid = census(current, budget);
    auto result = budget.outcome(valid);
    if (result == Observation::complete && !budget.check()) { valid = false; result = budget.outcome(false); }
    if (result == Observation::budget_incomplete) {
        // No partial observation, lease refresh, nonce reset or old-certificate
        // reopening. A later window must start a wholly new native census.
        if (historical_census_expired(GetTickCount64())) stop_census(3);
        ticking.clear(); return;
    }
    valid = result == Observation::complete;
    if (!valid) {
        census_gap = true;
        active_scope.store(false, std::memory_order_release);
        if (applied.load(std::memory_order_acquire) || captured.load(std::memory_order_acquire) ||
            (combat_enabled() && encounter_sealed)) stop_census(1);
    }
    // This runs from Player/Nadine update: never wait for a receiver/native job.
    // An unpublished census must not leave the writer enabled until next tick.
    if (!TryAcquireSRWLockExclusive(&lock)) {
        if (valid && !census_gap && !stopped.load(std::memory_order_acquire)) {
            // A COMPLETE valid observation that lost the metadata mailbox is
            // not negative native identity/read evidence. Discard its values,
            // close this window and require a NEW complete census, exactly as
            // with a resource deferral. Never renew the historical deadline,
            // reset a terminal stop or publish/cache this abandoned snapshot.
            census_publish_deferrals.fetch_add(1, std::memory_order_relaxed);
            budget.defer(kCostPublication);
            if (historical_census_expired(GetTickCount64())) stop_census(3);
        } else {
            // Preserve an ACTUAL failed observation (and any prior gap) even
            // if the mailbox is also contended. It must never become a retry
            // permission just because publication was unavailable.
            census_gap = true;
            active_scope.store(false, std::memory_order_release);
        }
        ticking.clear(); return;
    }
    // Check the OLD absolute deadline with a POST-census timestamp, before any
    // reset or renewal can erase it. This applies to both A and B certificates.
    if (historical_census_expired(GetTickCount64())) {
        stop_census(3);
        reset_locked(); ReleaseSRWLockExclusive(&lock); ticking.clear(); return;
    }
    if (census_gap) {
        // An in-flight first apply may have completed after the failed try-lock.
        // Once the lock is ours, it cannot race this decision. Before any apply
        // reset the session; after one apply the gap is terminal, never rearm.
        if (applied.load(std::memory_order_acquire) || captured.load(std::memory_order_acquire) ||
            (combat_enabled() && encounter_sealed)) stop_census(1);
        reset_locked(); census_gap = false;
    }
    if (stopped.load()) {
        ReleaseSRWLockExclusive(&lock); ticking.clear(); return;
    }
    if (!valid) {
        if (applied.load(std::memory_order_relaxed) || captured.load(std::memory_order_relaxed) ||
            (combat_enabled() && encounter_sealed)) stop_census(1);
        reset_locked();
    }
    else {
        if (combat_enabled() && encounter_sealed) {
            drain_native_life_locked();
            if (!reconcile_combat_census_locked(current)) { stop_census(2); reset_locked(); }
        } else if (session && current != roster && (applied.load(std::memory_order_relaxed) || captured.load(std::memory_order_relaxed))) {
            // First experiment never silently rebinds across a death/load/spawn.
            stop_census(2); reset_locked();
        } else if (!session || current != roster) {
            // Do not change historical metadata after a known budget abort.
            if (!budget.check()) { ReleaseSRWLockExclusive(&lock); ticking.clear(); return; }
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
    if (combat_enabled() && valid && session && confirmed && !stopped.load(std::memory_order_acquire)) {
        seal_initial_combat_locked(); publish_native_bindings_locked(); drain_native_life_locked();
    }
    const bool within_budget = budget.check();
    const bool within_world = !player_world::kEnablePlayerWorldRearm || scope_current();
    if (!within_world) stopped.store(true, std::memory_order_release);
    const auto publish_now = GetTickCount64();
    if (historical_census_expired(publish_now)) stop_census(3);
    if (within_budget && publish_now > now+150) budget.defer(kCostWindow);
    if (valid && session && within_budget && !budget.incomplete && within_world && !stopped.load(std::memory_order_acquire)) {
        // All game-side certificate readers hold work_busy. Ticket is committed
        // after its fields; refresh barrier/admission are published LAST.
        census_valid_until.store(now+150, std::memory_order_release);
        certified_epoch.store(current.world_epoch, std::memory_order_release);
        certified_session.store(session, std::memory_order_release);
        certified_ticket.store(budget.ticket, std::memory_order_release);
        refresh_required.store(false, std::memory_order_release);
        active_scope.store(confirmed && publish_now-peer_seen <= 1000 && !stopped.load(std::memory_order_acquire), std::memory_order_release);
    } else {
        // In particular, never zero/renew historical expiry on budget pause.
        active_scope.store(false, std::memory_order_release);
    }
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
    struct Pending {
        wire::Packet packet{}; ULONGLONG captured{};
        wire::combat::Packet modern_packet{};
        std::size_t index=wire::kActors;
        ULONGLONG control_started{};
        unsigned control_max_age{};
        bool modern{}, control{}, frozen{};
    };
    ULONGLONG last_send{}, last_log{}, last_poll{};
    while (g_state_net.running.load(std::memory_order_acquire)) {
        if (combat_enabled() && control_book_ready.load(std::memory_order_acquire)) {
            if (!control_current()) break;
        } else if (stop_for_player_lifecycle() || stopped.load(std::memory_order_acquire)) {
            close_control(ControlClose::unbound); break;
        }
        // Retain the native writer's original absorbing 30s stop. A frozen
        // values-only book can still report what was received/refused afterward.
        if (combat_enabled()) effect_slice_current(GetTickCount64());
        if (stopped.load(std::memory_order_acquire) && !control_book_ready.load(std::memory_order_acquire)) {
            close_control(ControlClose::unbound); break;
        }
        if (!stopped.load(std::memory_order_acquire) && !next_session.load(std::memory_order_acquire)) {
            std::uint64_t nonce{};
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&nonce), sizeof(nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || !nonce) {
                close_control(ControlClose::unbound); break;
            }
            next_session.store(nonce, std::memory_order_release);
        }
        if (!installed.load(std::memory_order_acquire)) { Sleep(10); continue; }
        const auto poll_started = GetTickCount64();
        // A slow provider, file write or scheduler must not turn a queued old
        // datagram into a fresh root. No draining/rearming after a worker gap.
        if (last_poll && poll_started-last_poll > 100) {
            worker_gap_ms.store(poll_started-last_poll, std::memory_order_relaxed);
            stopped.store(true); active_scope.store(false, std::memory_order_release);
            if (!combat_enabled() || !control_book_ready.load(std::memory_order_acquire)) break;
        }
        last_poll = poll_started;
        // Bounded draining, even under unrelated or malformed UDP traffic.
        // WSAEWOULDBLOCK is normal; always sleep before another poll.
        for (unsigned i = 0; i < 64 && !control_closed.load(std::memory_order_acquire) &&
             (!stopped.load(std::memory_order_acquire) || (combat_enabled() && control_book_ready.load(std::memory_order_acquire))); ++i) {
            std::array<char,sizeof(wire::combat::Packet)> packet{}; sockaddr_in source{}; int source_size = sizeof(source);
            const int bytes = recvfrom(transport_socket, packet.data(), static_cast<int>(packet.size()), 0,
                reinterpret_cast<sockaddr*>(&source), &source_size);
            if (bytes == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (error == WSAEWOULDBLOCK) break;
                if (error == WSAEMSGSIZE || error == WSAECONNRESET) continue;
                close_control(ControlClose::socket); break;
            }
            receive(packet.data(), bytes, source, source_size, poll_started);
        }
        const auto now = GetTickCount64();
        std::array<Pending, wire::kActors*2+1> pending{}; std::size_t count{};
        if (now-last_send >= 50 && TryAcquireSRWLockExclusive(&lock)) {
            last_send = now;
            if (combat_enabled()) {
                drain_native_life_locked();
                if (!stopped.load(std::memory_order_acquire)) publish_native_bindings_locked();
                refresh_control_reports_locked(now);
            }
            const auto modernize = [](Pending& item,wire::combat::Kind kind,std::uint64_t life=0,
                                      std::uint64_t event=0,wire::combat::Result result=wire::combat::Result::none) {
                item.modern=true;
                auto& output=item.modern_packet;
                output.kind=kind; output.role_b=item.packet.role_b; output.sender=item.packet.sender;
                output.receiver=item.packet.receiver; output.sequence=item.packet.sequence; output.key=item.packet.key;
                output.life_id=life; output.event_id=event; output.result=result;
                if (kind==wire::combat::Kind::root) { output.position=item.packet.position; output.rotation=item.packet.rotation; }
            };
            const bool fresh = session && now <= census_valid_until.load(std::memory_order_acquire) && !stopped.load();
            if (fresh && now-last_hello >= 200 && (!combat_enabled() || !control_book_ready.load(std::memory_order_acquire))) {
                wire::Packet hello{}; hello.role_b = static_cast<std::uint8_t>(g_net_role_b);
                hello.sender = session; hello.receiver = peer; hello.sequence = ++sequence;
                pending[count] = {hello, now};
                if (combat_enabled()) modernize(pending[count],wire::combat::Kind::hello);
                ++count; last_hello = now;
            }
            if (fresh && !refresh_required.load(std::memory_order_acquire) && active_scope.load(std::memory_order_acquire) && confirmed && peer && now-peer_seen <= 1000 && !g_net_role_b) {
                for (std::uint32_t i = 0; i < roster.count; ++i) {
                    auto& row = rows[i];
                    if (!combat_row_writable(i)) continue;
                    if (!row.dirty) continue;
                    row.dirty = false;
                    if (now-row.captured > 150) continue;
                    auto packet = row.outgoing;
                    packet.sender = session; packet.receiver = peer; packet.sequence = ++sequence;
                    pending[count] = {packet, row.captured}; pending[count].index=i;
                    if (combat_enabled()) modernize(pending[count],wire::combat::Kind::root,combat_rows[i].life);
                    ++count;
                }
            }
            // The frozen CONTROL book survives a failed root writer. These
            // packets contain values only; they cannot mint a native admission.
            if (combat_enabled() && control_book_ready.load(std::memory_order_acquire) && control_current()) {
                const auto stage=[&](wire::combat::Kind kind,std::size_t index,std::uint64_t event,
                                     wire::combat::Result result=wire::combat::Result::none,
                                     wire::combat::Refusal refusal=wire::combat::Refusal::none) {
                    if (count==pending.size()) return false;
                    auto& item=pending[count++]; item={}; item.modern=item.control=item.frozen=true; item.index=index;
                    item.packet.sender=control_book.session; item.packet.receiver=control_book.peer;
                    auto& output=item.modern_packet; output.kind=kind; output.role_b=static_cast<std::uint8_t>(g_net_role_b);
                    output.sender=control_book.session; output.receiver=control_book.peer;
                    output.sequence=++sequence; if (!output.sequence) output.sequence=++sequence;
                    output.event_id=event; output.result=result; output.refusal=refusal;
                    if (index<control_book.count) {
                        output.life_id=control_book.lives[index].life; output.key=control_book.lives[index].key;
                        item.control_started=combat_rows[index].control_started;
                        item.control_max_age=kind==wire::combat::Kind::result || kind==wire::combat::Kind::refused ||
                            (kind==wire::combat::Kind::received && !g_net_role_b) ? 35000u : 30000u;
                    }
                    if (kind==wire::combat::Kind::bound) output.manifest_count=control_book.count;
                    return true;
                };
                // Session heartbeat, not a root/census freshness certificate.
                if (now-control_last_hello>=200 && stage(wire::combat::Kind::hello,wire::kActors,0)) control_last_hello=now;
                for (std::size_t i=0; i<control_book.count && count<pending.size(); ++i) {
                    auto& row=combat_rows[i];
                    if (row.last_control && now-row.last_control<100) continue;
                    if (!row.control_started || now<row.control_started) continue;
                    const auto age=now-row.control_started;
                    bool staged{};
                    if (!g_net_role_b) {
                        if (row.response_ack_reply && age<=35000) {
                            staged=stage(wire::combat::Kind::received,i,row.death_event,row.remote_result,row.remote_refusal);
                            if (staged) row.response_ack_reply=false;
                        } else if (row.death_event && !row.remote_response && age<=30000)
                            staged=stage(wire::combat::Kind::death,i,row.death_event);
                    } else if (row.death_event) {
                        if (!row.report_acked && age<=35000 && row.result!=wire::combat::Result::none)
                            staged=stage(wire::combat::Kind::result,i,row.death_event,row.result);
                        else if (!row.report_acked && age<=35000 && row.refusal!=wire::combat::Refusal::none)
                            staged=stage(wire::combat::Kind::refused,i,row.death_event,wire::combat::Result::none,row.refusal);
                        else if (row.receipt_reply && age<=30000) {
                            staged=stage(wire::combat::Kind::received,i,row.death_event);
                            if (staged) row.receipt_reply=false;
                        }
                    } else if (row.bind_reply && age<=30000) {
                        staged=stage(wire::combat::Kind::bound,i,0);
                        if (staged) row.bind_reply=false;
                    }
                    if (staged) row.last_control=now;
                }
            } else if (combat_enabled() && session && peer && confirmed && encounter_sealed && scope_current() && !stopped.load()) {
                // Initial binding still requires the original complete census.
                // An incomplete prebind world cannot invent a frozen book.
                for (std::size_t i=0; i<roster.count && count<pending.size(); ++i) {
                    auto& row=combat_rows[i];
                    if (row.last_control && now-row.last_control<100) continue;
                    wire::combat::Kind kind{}; bool send_control{};
                    if (!g_net_role_b && row.death_event && !row.result_confirmed) { kind=wire::combat::Kind::death; send_control=true; }
                    else if (g_net_role_b && row.death_event && row.result!=wire::combat::Result::none) { kind=wire::combat::Kind::result; send_control=true; }
                    else if (!g_net_role_b && row.bound && !row.bind_confirmed) { kind=wire::combat::Kind::bind; send_control=true; }
                    else if (g_net_role_b && row.bind_reply && row.bound) { kind=wire::combat::Kind::bound; send_control=true; }
                    if (!send_control) continue;
                    // Finite retransmission lifetime even before any native
                    // effect. Duplicates never restamp control_started. The
                    // shared first-effect deadline can only close us earlier.
                    if (!row.control_started || now-row.control_started>30000) {
                        stopped.store(true,std::memory_order_release);
                        active_scope.store(false,std::memory_order_release); break;
                    }
                    wire::Packet header{}; header.role_b=static_cast<std::uint8_t>(g_net_role_b);
                    header.sender=session; header.receiver=peer; header.key=roster.rows[i].key;
                    header.sequence=++sequence; if (!header.sequence) header.sequence=++sequence;
                    pending[count]={header,now}; pending[count].control=true; pending[count].index=i;
                    modernize(pending[count],kind,row.life,
                        kind==wire::combat::Kind::death || kind==wire::combat::Kind::result ? row.death_event : 0,
                        kind==wire::combat::Kind::result ? row.result : wire::combat::Result::none);
                    if (kind==wire::combat::Kind::bind || kind==wire::combat::Kind::bound)
                        pending[count].modern_packet.manifest_count=roster.count;
                    ++count; row.last_control=now;
                    if (kind==wire::combat::Kind::bound) row.bind_reply=false;
                }
            }
            const auto local_count = roster.count; const bool paired = confirmed;
            ReleaseSRWLockExclusive(&lock);
            // No lock or game pointer survives into any socket/log/provider call.
            // A full send buffer drops this snapshot, never queues/retries it.
            for (std::size_t i = 0; i < count; ++i) {
                const auto& item = pending[i]; const auto send_now = GetTickCount64();
                if (item.frozen) {
                    if (!control_current() || item.packet.sender!=control_book.session || item.packet.receiver!=control_book.peer ||
                        (item.control_max_age && (send_now<item.control_started || send_now-item.control_started>item.control_max_age))) continue;
                } else if (stopped.load() || item.packet.sender != published_session.load(std::memory_order_acquire) ||
                    (combat_enabled() && (!scope_current() || !effect_slice_current(send_now))) ||
                    (!item.control && (send_now > census_valid_until.load(std::memory_order_acquire) || send_now-item.captured > 150 ||
                    (item.packet.kind == wire::Kind::root && !active_scope.load(std::memory_order_acquire))))) continue;
                if (item.modern && !item.control && item.index<wire::kActors &&
                    (native_life_bits[item.index].load(std::memory_order_acquire)&(kNativeRetiring|kNativeDead))) continue;
                const auto* data=item.modern ? reinterpret_cast<const char*>(&item.modern_packet) : reinterpret_cast<const char*>(&item.packet);
                const int size=item.modern ? static_cast<int>(sizeof(item.modern_packet)) : static_cast<int>(sizeof(item.packet));
                const int bytes = sendto(transport_socket, data, size, 0,
                    reinterpret_cast<const sockaddr*>(&transport_peer), sizeof(transport_peer));
                if (bytes == size) sent.fetch_add(1, std::memory_order_relaxed);
                else send_dropped.fetch_add(1, std::memory_order_relaxed);
            }
            if (now-last_log >= 2000) {
                last_log = now; std::ostringstream line;
                line << "ENEMY-ROOT role=" << (g_net_role_b ? 'B' : 'A') << " actors=" << local_count << " paired=" << paired
                     << " worldEpoch=" << bound_world_epoch.load(std::memory_order_acquire)
                     << " capture=" << captured.load() << " recv=" << received.load() << " match=" << matched.load()
                     << " apply=" << applied.load() << " reject=" << rejected.load() << " sent=" << sent.load()
                     << " sendDrop=" << send_dropped.load() << " lastGate=" << last_rejection.load() << " stopped=" << stopped.load()
                     << " costStop=" << cost_stop.load() << " costSite=" << cost_site.load()
                     << " censusStop=" << census_stop_reason.load()
                     << " peakScopeUs=" << peak_scope_ticks.load()*1000000/qpc_frequency
                     << " peakWindowUs=" << peak_window_ticks.load()*1000000/qpc_frequency
                     << " queries=" << query_total.load() << " peakQueries=" << peak_queries.load() << " workSkips=" << work_skips.load();
                append_cost_stop_context(line);
                append_budget_pause(line);
                append_combat_status(line);
                write_status(line.str());
            }
        }
        Sleep(10);
    }
    if (!control_closed.load(std::memory_order_acquire))
        close_control(control_book_ready.load(std::memory_order_acquire) ? ControlClose::world : ControlClose::unbound);
    const bool socket_closed=closesocket(transport_socket)==0;
    const int close_error=socket_closed ? 0 : WSAGetLastError();
    if (socket_closed) transport_socket=INVALID_SOCKET;
    WSACleanup();
    if (socket_closed && control_close_ack) SetEvent(control_close_ack);
    std::ostringstream line; line << "ENEMY-ROOT STOPPED apply=" << applied.load() << " match=" << matched.load()
        << " worldEpoch=" << bound_world_epoch.load(std::memory_order_acquire)
        << " workerGapMs=" << worker_gap_ms.load()
        << " external=" << external_stop.load() << " nativeFault=" << native_apply_fault.load()
        << " controlSocketClosed=" << socket_closed << " controlSocketCloseError=" << close_error
        << " costStop=" << cost_stop.load() << " costSite=" << cost_site.load()
        << " censusStop=" << census_stop_reason.load()
        << " peakScopeUs=" << peak_scope_ticks.load()*1000000/qpc_frequency
        << " peakWindowUs=" << peak_window_ticks.load()*1000000/qpc_frequency
        << " queries=" << query_total.load() << " peakQueries=" << peak_queries.load()
        << "; corrections closed; native AI continues; terminal latch retained";
    append_cost_stop_context(line);
    append_budget_pause(line);
    append_combat_status(line);
    write_status(line.str());
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    return 0;
}

bool start_transport() {
    sockaddr_in local{}; int size = sizeof(local);
    if (!g_state_net.enabled || !g_state_net.peer_set ||
        getsockname(g_state_net.recv_socket, reinterpret_cast<sockaddr*>(&local), &size) == SOCKET_ERROR ||
        size != sizeof(local) || local.sin_family != AF_INET) {
        if (control_close_ack) SetEvent(control_close_ack); return false;
    }
    const auto local_port = ntohs(local.sin_port), peer_port = ntohs(g_state_net.peer_addr.sin_port);
    if ((local_port != coop::kStatePort && local_port != coop::kStatePort+1) ||
        (peer_port != coop::kStatePort && peer_port != coop::kStatePort+1)) {
        if (control_close_ack) SetEvent(control_close_ack); return false;
    }
    // Derive ports from actual binds, NOT role B: on separate PCs both state
    // sockets bind31338. Only this socket becomes nonblocking; baseline is intact.
    local.sin_port = htons(local_port+2);
    transport_peer = g_state_net.peer_addr; transport_peer.sin_port = htons(peer_port+2);
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) {
        if (control_close_ack) SetEvent(control_close_ack); return false;
    }
    transport_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const auto fail = [] {
        const bool closed=transport_socket==INVALID_SOCKET || closesocket(transport_socket)==0;
        if (closed) transport_socket=INVALID_SOCKET;
        WSACleanup();
        if (closed && control_close_ack) SetEvent(control_close_ack);
        return false;
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

void apply_root_and_release(void* object, const void* transform, WorkBudget* budget, ULONGLONG slice_start,
                            std::size_t index, std::uint64_t revision) {
    using SetLocalRoot = void(__fastcall*)(void*, const void*);
    // Finally cleans up even if an outer native SEH handler resumes the game.
    // Do NOT catch/translate the game's exception or count a partial write as
    // success. Normal return leaves the permit with the enclosing scope.
    __try {
        if (!budget->check() || !certificate_current(budget->ticket)) return;
        if (combat_enabled() && !effect_slice_current(GetTickCount64())) return;
        if (!combat_row_writable(index) || (combat_enabled() && combat_rows[index].revision!=revision)) return;
        if constexpr (player_world::kEnablePlayerWorldRearm) {
            if (stop_for_player_lifecycle() || !scope_current()) return;
        }
        reinterpret_cast<SetLocalRoot>(g_module+0x1E7D0D0)(object, transform);
        // Commit the effect clock ONLY after normal native return, while the
        // same mailbox still excludes peer/reset processing. An abandoned
        // candidate must not masquerade as an applied effect on the next hello.
        commit_first_effect(slice_start);
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

struct BodyImportFields {
    std::uintptr_t native;
    std::uint8_t gap08[0x20];
    std::uintptr_t owner;
    std::uint8_t gap30[0x2C];
    float blend;
    std::uint32_t flags;
    std::uint8_t gap64[0x20];
    std::int16_t joint;
    std::uint8_t source;
};
static_assert(std::is_trivial_v<BodyImportFields> && std::is_standard_layout_v<BodyImportFields> &&
              std::is_trivially_copyable_v<BodyImportFields>);
static_assert(offsetof(BodyImportFields, native) == 0x00 && offsetof(BodyImportFields, owner) == 0x28 &&
              offsetof(BodyImportFields, blend) == 0x5C && offsetof(BodyImportFields, flags) == 0x60 &&
              offsetof(BodyImportFields, joint) == 0x84 && offsetof(BodyImportFields, source) == 0x86);
constexpr std::size_t kBodyImportSpan = offsetof(BodyImportFields, source) + sizeof(BodyImportFields::source);
static_assert(kBodyImportSpan == 0x87 && kBodyImportSpan <= sizeof(BodyImportFields));

struct OrdinaryJobFields {
    std::uintptr_t control, fg;
    std::uint8_t gap10[4];
    std::uint8_t flag;
    std::uint8_t gap15[0xB];
    std::uintptr_t postpose;
};
static_assert(std::is_trivial_v<OrdinaryJobFields> && std::is_standard_layout_v<OrdinaryJobFields> &&
              std::is_trivially_copyable_v<OrdinaryJobFields>);
static_assert(sizeof(OrdinaryJobFields) == 0x28 && offsetof(OrdinaryJobFields, control) == 0 &&
              offsetof(OrdinaryJobFields, fg) == 8 && offsetof(OrdinaryJobFields, flag) == 0x14 &&
              offsetof(OrdinaryJobFields, postpose) == 0x20);
struct FgOrdinaryHeaderFields {
    std::uint32_t flags;
    std::uint8_t gap04, perform, mode, gap07, export_flag;
    std::uint8_t gap09[7];
    std::uintptr_t control;
};
static_assert(std::is_trivial_v<FgOrdinaryHeaderFields> && std::is_standard_layout_v<FgOrdinaryHeaderFields> &&
              std::is_trivially_copyable_v<FgOrdinaryHeaderFields>);
static_assert(sizeof(FgOrdinaryHeaderFields) == 0x18 && offsetof(FgOrdinaryHeaderFields, flags) == 0 &&
              offsetof(FgOrdinaryHeaderFields, perform) == 5 && offsetof(FgOrdinaryHeaderFields, mode) == 6 &&
              offsetof(FgOrdinaryHeaderFields, export_flag) == 8 && offsetof(FgOrdinaryHeaderFields, control) == 0x10);
struct FgWeakIdentityFields {
    std::uintptr_t record;
    std::uint32_t generation;
};
static_assert(std::is_trivial_v<FgWeakIdentityFields> && std::is_standard_layout_v<FgWeakIdentityFields> &&
              std::is_trivially_copyable_v<FgWeakIdentityFields>);
static_assert(sizeof(FgWeakIdentityFields) == 0x10 && offsetof(FgWeakIdentityFields, record) == 0 &&
              offsetof(FgWeakIdentityFields, generation) == 8);
// Read through Fg+1B3 only, excluding the POD's four trailing alignment bytes.
constexpr std::size_t kFgWeakIdentitySpan = offsetof(FgWeakIdentityFields, generation) + sizeof(FgWeakIdentityFields::generation);
static_assert(kFgWeakIdentitySpan == 0xC && kFgWeakIdentitySpan <= sizeof(FgWeakIdentityFields));
struct FgCustomCallbackFields { std::uintptr_t first, second; };
static_assert(std::is_trivial_v<FgCustomCallbackFields> && std::is_standard_layout_v<FgCustomCallbackFields> &&
              std::is_trivially_copyable_v<FgCustomCallbackFields>);
static_assert(sizeof(FgCustomCallbackFields) == 0x10 && offsetof(FgCustomCallbackFields, first) == 0 &&
              offsetof(FgCustomCallbackFields, second) == 8);

bool ordinary(std::uintptr_t actor, std::uintptr_t job, std::array<float, 8>& transform, unsigned& gate, ReadCache& cache) noexcept {
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), &cache); };
    gate = 1;
    std::uintptr_t fg{}, record_actor{}, owner_record{}, owner_control{}, control_fg{};
    std::uintptr_t art{}, active_art{}, cache_art{}, raw{}, art_raw{}, composite{}, composite_owner{}, bodies{};
    std::uint32_t id{}, database{}, skeleton{}, parent_key{}, parent_generation{}, body_count{}, bindings{}, gate18{}, gate24{};
    std::uint8_t force{}, cached{}, composite_flags{};
    std::uintptr_t local_pose{};
    // Owned, uninitialized PODs: a failed full-span guarded copy short-circuits
    // before any field use. Spans can inspect gaps/late fields earlier and so
    // refuse more strictly. Every ordinary() call obtains NEW observations;
    // neither a batch nor the lexical page cache is an atomic/lifetime lease.
    OrdinaryJobFields job_fields;
    FgWeakIdentityFields fg_identity;
    FgOrdinaryHeaderFields fg_header;
    FgCustomCallbackFields custom;
    if (!read(actor+0x1A8, fg) || !read(job, job_fields) || job_fields.fg != fg || job_fields.flag ||
        job_fields.postpose != g_module+0x6CD220 ||
        !read(actor+0xA8, owner_record) || !copy(fg+0x1A8, &fg_identity, kFgWeakIdentitySpan, &cache) ||
        fg_identity.record != owner_record || !read(fg_identity.record+8, record_actor) || record_actor != actor ||
        !read(actor+0xBC, id) || !id || fg_identity.generation != id ||
        !job_fields.control || !read(fg, fg_header) || fg_header.control != job_fields.control ||
        !read(actor+0x1A0, owner_control) || owner_control != job_fields.control ||
        !read(job_fields.control+0x20, control_fg) || control_fg != fg ||
        (fg_header.flags & 0x60) != 0x60 || !fg_header.perform || !fg_header.mode || !(fg_header.export_flag & 1) ||
        !read(fg+0x298, custom) || custom.first || custom.second ||
        !read(g_module+0x3682F49, force) || !read(g_module+0x3682F4B, cached) || cached || !read(fg+0x100, local_pose)) return false;
    if (((force || (fg_header.flags & 2)) && local_pose ? 2 : fg_header.mode) != 2) return false;
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
                // Copy only [body+0x40, body+0xC7), not trailing POD padding.
                // This may inspect protected gaps/late fields earlier and thus
                // reject more strictly; it is not an atomic observation/lease
                // or a guarantee of fewer protection queries. Native pointers
                // are followed only after the original field checks below.
                BodyImportFields fields;
                std::uintptr_t world{};
                if (!copy(body+0x40, &fields, kBodyImportSpan, &cache) || fields.owner != actor ||
                    ((fields.flags >> 13) & 3) != 0 || (fields.flags & 0x400) ||
                    (fields.native && (!read(fields.native+0x10, world) || world)) ||
                    (fields.source != 1 && ((fields.source != 2 && fields.source != 3) || fields.joint < 0 || fields.joint >= 90))) return false;
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
        // Same guarded full span as the disabled ragdoll, including its stricter
        // gap/read-order behavior; blend remains irrelevant in that loop.
        BodyImportFields fields;
        std::uintptr_t body_world{}; std::uint8_t motion{};
        if (!copy(body+0x40, &fields, kBodyImportSpan, &cache) || fields.owner != actor || !fields.native ||
            ((fields.flags >> 13) & 3) != 2 || (fields.flags & 1) || !std::isfinite(fields.blend) || fields.blend > 0 ||
            !read(fields.native+0x160, motion) || motion != 4 ||
            !read(fields.native+0x10, body_world) || !body_world) return false;
        if (fields.source == 1) ++root_count;
        else if (fields.source != 3 || fields.joint < 0 || fields.joint >= 90) return false;
    }
    // No health mutation. A live exact Pirate must still have positive raw HP.
    std::uintptr_t health{}, health_vt{}; std::int32_t hp{};
    gate = 7;
    if (!read(actor+0x4A8, health) || !health || !read(health, health_vt) ||
        health_vt != g_module+0x2BBE128 || !read(health+0x38, hp) || hp <= 0) return false;
    return root_count == 1;
}
void postblend(void* object, void* native_job, std::uintptr_t pc) noexcept {
    if (stop_for_player_lifecycle() || pc != g_module+0x1A929A7 || !installed.load(std::memory_order_acquire) || !active_scope.load(std::memory_order_acquire) ||
        stopped.load(std::memory_order_relaxed) || g_dll_inert) return;
    const auto actor = reinterpret_cast<std::uintptr_t>(object);
    // Metadata-only preflight rejects no-work callbacks before charging the
    // shared work window. Never dereference actor or reserve a row here, and
    // release this lock BEFORE attempting the work permit (no lock inversion).
    const auto preflight_now = GetTickCount64();
    if (combat_enabled() && !effect_slice_current(preflight_now)) return;
    if (preflight_now > census_valid_until.load(std::memory_order_acquire)) return;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    bool preflight_due{};
    if (session && confirmed && active_scope.load(std::memory_order_acquire) &&
        preflight_now-peer_seen <= 1000 && !stopped.load(std::memory_order_acquire)) {
        for (std::size_t i = 0; i < roster.count; ++i) if (roster.rows[i].actor == actor) {
            if (!combat_row_writable(i)) break;
            const auto& row = rows[i];
            preflight_due = !g_net_role_b ? preflight_now >= row.next_capture :
                row.received && preflight_now-row.received <= 200 &&
                row.incoming.sender == peer && row.incoming.receiver == session &&
                row.incoming.sequence != row.attempted_sequence;
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (!preflight_due) return;
    if constexpr (player_world::kEnablePlayerWorldRearm) {
        if (stop_for_player_lifecycle() || !scope_current()) return;
    }
    WorkBudget budget(2);
    if (!budget.check()) return;
    // Metadata-only scheduling first. A roster pointer match is NOT admission:
    // recheck after obtaining the permit before reserving the exact sequence.
    // All identity/ordinary/final generation checks below remain mandatory.
    const auto schedule_now = GetTickCount64();
    if (!certificate_current(budget.ticket) || !installed.load(std::memory_order_acquire) ||
        !active_scope.load(std::memory_order_acquire) ||
        stopped.load(std::memory_order_acquire) || g_dll_inert ||
        schedule_now > census_valid_until.load(std::memory_order_acquire)) return;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    bool due{};
    // Select values, NEVER admit a native pointer from the mailbox. The final
    // pass below must rediscover this exact weak/key tuple from native self.
    wire::Packet scheduled_packet{};
    Local scheduled_local{};
    std::size_t scheduled_index = roster.count;
    std::uint64_t scheduled_revision{};
    const auto scheduled_session = session;
    if (session && confirmed && active_scope.load(std::memory_order_acquire) &&
        schedule_now <= census_valid_until.load(std::memory_order_acquire) &&
        schedule_now-peer_seen <= 1000 && !stopped.load(std::memory_order_acquire) && !g_dll_inert) {
        for (std::size_t i = 0; i < roster.count; ++i) if (roster.rows[i].actor == actor) {
            if (!combat_row_writable(i)) break;
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
            if (due) { scheduled_local = roster.rows[i]; scheduled_index = i; scheduled_revision=combat_rows[i].revision; }
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (!due) return;
    // ONE fresh final observation, after the scheduling mailbox was released.
    // Former B discovery + final passes both traversed the same native graphs;
    // discovery had no side effect/retention and selected only these copied
    // mailbox values. Keep the FINAL identity/ordinary/late checks, not fields
    // or protection observations borrowed from an earlier callback/worker.
    ReadCache cache{}; cache.budget = &budget;
    const auto read = [&cache]<class T>(std::uintptr_t at, T& value) noexcept { return copy(at, &value, sizeof(value), &cache); };
    Local local{}; std::array<float, 8> fresh{}; unsigned gate{};
    if (!identity(actor, local, cache) || !(local == scheduled_local) ||
        !ordinary(actor, reinterpret_cast<std::uintptr_t>(native_job), fresh, gate, cache)) {
        last_rejection.store(gate, std::memory_order_relaxed);
        rejected.fetch_add(1, std::memory_order_relaxed); return;
    }
    const auto now = GetTickCount64();
    if (!TryAcquireSRWLockExclusive(&lock)) return; // never wait in an animation job
    if (!session || session != scheduled_session || !active_scope.load(std::memory_order_acquire) || stopped.load() ||
        now > census_valid_until.load(std::memory_order_acquire) || !roster_scope_current(roster, cache) ||
        scheduled_index >= roster.count || !(roster.rows[scheduled_index] == local) || !combat_row_writable(scheduled_index) ||
        (combat_enabled() && combat_rows[scheduled_index].revision!=scheduled_revision)) {
        ReleaseSRWLockExclusive(&lock); return;
    }
    const auto index = scheduled_index;
    auto& row = rows[index];
    wire::Packet packet{};
    if (!g_net_role_b) {
        packet.kind = wire::Kind::root; packet.key = local.key;
        std::copy_n(fresh.begin(), 3, packet.position.begin()); std::copy_n(fresh.begin()+4, 4, packet.rotation.begin());
        if (wire::valid_pose(packet) && budget.check()) {
            row.outgoing = packet; row.captured = now; row.dirty = true;
            captured.fetch_add(1, std::memory_order_relaxed);
        }
        ReleaseSRWLockExclusive(&lock); return;
    }
    if (!confirmed || now-peer_seen > 1000 || !row.received || now-row.received > 200 ||
        scheduled_packet.sender != peer || scheduled_packet.receiver != session ||
        row.incoming.sequence != scheduled_packet.sequence) {
        ReleaseSRWLockExclusive(&lock); return;
    }
    packet = scheduled_packet;
    // First prototype never teleports a mismatched encounter across the map.
    double fresh_distance{};
    for (unsigned axis = 0; axis < 3; ++axis) { const double delta = packet.position[axis]-fresh[axis]; fresh_distance += delta*delta; }
    if (!(fresh_distance <= 100.0)) { ReleaseSRWLockExclusive(&lock); return; }
    const auto prior_effect=first_apply.load(std::memory_order_acquire);
    const auto slice_start = prior_effect ? prior_effect : now;
    if (now-slice_start > 30000) { stopped.store(true); ReleaseSRWLockExclusive(&lock); return; }
    const auto apply_now = GetTickCount64();
    std::uintptr_t manager{};
    const bool still_current = !g_dll_inert && active_scope.load(std::memory_order_acquire) && !stopped.load() &&
        apply_now <= census_valid_until.load(std::memory_order_acquire) &&
        session == scheduled_session && session == packet.receiver && peer == packet.sender &&
        confirmed && apply_now-peer_seen <= 1000 && index < roster.count && roster.rows[index] == local &&
        rows[index].received && apply_now-rows[index].received <= 200 && apply_now-slice_start <= 30000 &&
        rows[index].incoming.sequence == packet.sequence &&
        roster_scope_current(roster, cache, true) &&
        read(g_module+0x3898630, manager) && manager == roster.manager;
    if (!still_current) { ReleaseSRWLockExclusive(&lock); return; }
    // ordinary() walks animation/physics after identity(). Recheck kill state
    // and generations after that walk, before the final freshness/cost cutoff.
    // These are actual budgeted SEH reads, NOT a lifetime lease across the setter.
    std::uint8_t actor_kill{}; std::uint32_t actor_id{};
    if (!read(actor+0xBC, actor_id) || actor_id != local.generation ||
        !read(actor+0x40, actor_kill) || (actor_kill & 0x04)) {
        ReleaseSRWLockExclusive(&lock); return;
    }
    if constexpr (!player_world::kEnablePlayerWorldRearm) {
        std::uint32_t player_id{}; std::uint8_t player_kill{};
        if (!read(roster.player+0xBC, player_id) || player_id != roster.player_id ||
            !read(roster.player+0x40, player_kill) || (player_kill & 0x04)) {
            ReleaseSRWLockExclusive(&lock); return;
        }
    }
    // The final memory reads can themselves be delayed. Do not use a timestamp
    // captured before them to authorize a write after the packet/scope expired.
    const auto write_now = GetTickCount64();
    if (!budget.check() || g_dll_inert || stopped.load() || !active_scope.load(std::memory_order_acquire) ||
        write_now > census_valid_until.load(std::memory_order_acquire) ||
        write_now-peer_seen > 1000 || write_now-rows[index].received > 200 || write_now-slice_start > 30000) {
        ReleaseSRWLockExclusive(&lock); return;
    }
    std::copy_n(packet.position.begin(), 3, fresh.begin()); // keep receiver +13C service lane
    std::copy(packet.rotation.begin(), packet.rotation.end(), fresh.begin()+4);
    apply_root_and_release(object, fresh.data(), &budget, slice_start,index,scheduled_revision);
}
} // namespace enemy_root

#include "enemy_combat_native.inl"

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
    enemy_root::combat_requested_state=enemy_root::combat_requested();
    if (enemy_root::combat_enabled() && !player_world::kEnablePlayerWorldRearm) {
        boot_log_line("ENEMY-COMBAT refused: paired Player-world lifecycle is required"); return false;
    }
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto page_size = static_cast<std::uintptr_t>(info.dwPageSize);
    if (page_size < 4096 || (page_size & (page_size-1))) {
        boot_log_line("ENEMY-ROOT installation refused: unsupported native page size"); return false;
    }
    enemy_root::system_page_size = page_size;
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
    const bool root_ready = changed && sealed;
    const bool native_ready = !enemy_root::combat_enabled() || (root_ready && enemy_root::combat_native::install());
    enemy_root::combat_hooks_ready.store(enemy_root::combat_enabled() && native_ready,std::memory_order_release);
    const bool ready = root_ready && native_ready;
    if (!ready) enemy_root::stopped.store(true);
    enemy_root::installed.store(ready, std::memory_order_release);
    boot_log_line(!ready ? "ENEMY-ROOT installation refused; stock forwarding only" :
        enemy_root::combat_enabled() ? "ENEMY-ROOT native Pirate+408 root replication installed (admission=window-census-final; budget=defer-time-query-census; ENC2 initial-life death capability opt-in)" :
        "ENEMY-ROOT native Pirate+408 root replication installed (readGuard=QueryWorkingSetEx-page; admission=window-census-final; budget=defer-time-query-census; ordinary=POD; 30-second first slice; no HP/AI writes)");
    if (enemy_root::combat_enabled()) boot_log_line(ready ?
        "ENEMY-COMBAT READY optin=1 protocol=ENC2v2 initialLives=32-max control=world-values-only receipt!=commit positiveResult=HumanDead-only; native-stop/no-rearm; no respawn/B-authority/full-shots" :
        "ENEMY-COMBAT REFUSED native package; all effects fail closed");
    return ready;
}
