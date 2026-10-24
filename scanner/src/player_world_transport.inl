// Shared scene admission for all legacy player/world datagram families. A key
// is the native Task/Level/continue/spawn identity, not a raw object address.
// Matching keys still require the users to choose the same authored checkpoint;
// this does not create story progression or reconnect after explicit departure.
namespace player_world {
namespace {
constexpr std::uint32_t kEnvelopeMagic = 0x45575043; // CPWE
constexpr std::uint32_t kControlMagic = 0x43575043;  // CPWC
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kMaximumInnerBytes = sizeof(coop::EnemyPacket);
struct Envelope {
    std::uint32_t magic{kEnvelopeMagic};
    std::uint16_t version{kVersion}, header_bytes{40};
    std::uint64_t process_session{}, local{}, target{};
    std::uint32_t inner_bytes{}, reserved{};
};
struct Control {
    std::uint32_t magic{kControlMagic};
    std::uint16_t version{kVersion};
    std::uint8_t role_b{}, flags{}; // bit0 native primary ready; bit1 closing
    std::uint64_t process_session{}, local{}, echo{};
    Key key{};
    std::uint32_t sequence{}, reserved{};
};
static_assert(sizeof(Key) == 32 && sizeof(Envelope) == 40 && sizeof(Control) == 72);
static_assert(kMaximumInnerBytes == 1424 && kMaximumInnerBytes+sizeof(Envelope) <= 1472);
SRWLOCK world_lock = SRWLOCK_INIT;
std::atomic<bool> closed{true}, paired{false};
std::atomic<std::uint64_t> epoch{};
std::atomic<unsigned> ingress{};
Key local_key{}, peer_key{};
std::uint64_t serial{}, peer_process{}, peer_epoch{}, peer_echo{};
std::uint64_t sent_local{}, sent_echo{}, last_paired_local{}, last_paired_peer{};
std::uint32_t control_sequence{}, peer_sequence{};
bool native_ready{}, peer_native_ready{};

bool departed() noexcept {
    return g_state_net.local_leave_queued.load(std::memory_order_acquire) ||
           g_state_net.peer_left.load(std::memory_order_acquire);
}
bool configured_ip(const sockaddr_in& source, int source_size) noexcept {
    return g_state_net.peer_set && source_size >= static_cast<int>(sizeof(source)) &&
           source.sin_family == AF_INET &&
           source.sin_family == g_state_net.peer_addr.sin_family &&
           source.sin_addr.s_addr == g_state_net.peer_addr.sin_addr.s_addr;
}
bool pin_process(std::uint64_t process) noexcept {
    // Never acquire peer_state_lock while world_lock is held. Control pins the
    // process before native Player state exists; the same identity binds CPLV.
    AcquireSRWLockExclusive(&g_state_net.peer_state_lock);
    const bool valid = process && !departed() &&
        (!g_state_net.peer_state_session_id || g_state_net.peer_state_session_id == process);
    if (valid) g_state_net.peer_state_session_id = process;
    ReleaseSRWLockExclusive(&g_state_net.peer_state_lock);
    return valid;
}
bool update_pair_locked() noexcept {
    const bool previous = paired.load(std::memory_order_relaxed);
    const auto local = epoch.load(std::memory_order_relaxed);
    const bool same_previous = local == last_paired_local && peer_epoch == last_paired_peer;
    const bool both_advanced = !last_paired_local || !last_paired_peer ||
        (local != last_paired_local && peer_epoch != last_paired_peer);
    const bool ready = !closed.load(std::memory_order_acquire) && !departed() &&
        native_ready && peer_native_ready && local && peer_epoch &&
        local_key == peer_key && peer_echo == local && sent_local == local &&
        sent_echo == peer_epoch && (same_previous || both_advanced);
    if (ready) { last_paired_local = local; last_paired_peer = peer_epoch; }
    paired.store(ready, std::memory_order_release);
    return ready && !previous;
}
void report_pair(Token token, const Key& key) noexcept {
    char line[256]{};
    std::snprintf(line, sizeof(line),
        "PLAYER-WORLD PAIRED local=%llu peer=%llu task=0x%X level=0x%X continue=(%X,%X,%X) wanted=(%X,%X) state=%X",
        static_cast<unsigned long long>(token.local), static_cast<unsigned long long>(token.peer),
        key.task_id, key.level_key, key.continue_id04, key.continue_id08, key.continue_id1c,
        key.wanted_actor_id, key.wanted_level_id, key.spawn_state_id);
    boot_log_line(line);
}
bool known_inner(std::uint32_t magic) noexcept {
    switch (magic) {
    case coop::kStatePacketMagic: case coop::kIdleArmPosePacketMagic:
    case coop::kEnemyPacketMagic: case coop::kClientDamageMagic:
    case coop::kPickupPacketMagic: case coop::kWeaponWorldMagic:
    case coop::kWeaponGiveMagic: case coop::kFirePacketMagic:
    case coop::kGrenadeEventMagic: case coop::kGrenadeKinematicsMagic:
    case coop::kBreakableExplosionMagic: case coop::kBreakableProjectileMagic:
    case coop::kBreakableSyncMagic: return true;
    default: return false;
    }
}
} // namespace

void close_for_reset() noexcept {
    if constexpr (!kEnablePlayerWorldRearm) return;
    // Serialize with the guard's FINAL shared-lock admission after it increments
    // ingress. Merely release-storing closed followed by loading ingress==0 is
    // not a cross-atomic admission barrier. On return, prior admitted increments
    // happen-before the caller's drain check; later guards must observe closed.
    AcquireSRWLockExclusive(&world_lock);
    closed.store(true, std::memory_order_release);
    paired.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&world_lock);
}
bool ingress_quiescent() noexcept { return ingress.load(std::memory_order_acquire) == 0; }
std::uint64_t local_epoch() noexcept { return epoch.load(std::memory_order_acquire); }
bool data_admitted() noexcept {
    if constexpr (!kEnablePlayerWorldRearm) return true;
    return !closed.load(std::memory_order_acquire) &&
           paired.load(std::memory_order_acquire) && !departed();
}
bool peer_ready() noexcept { return data_admitted(); }
std::uint64_t capture_epoch() noexcept {
    if constexpr (!kEnablePlayerWorldRearm) return UINT64_MAX;
    return data_admitted() ? local_epoch() : 0;
}

std::uint64_t publish_local_world(const Key& key) noexcept {
    if constexpr (!kEnablePlayerWorldRearm) return 0;
    if (!closed.load(std::memory_order_acquire) || !ingress_quiescent() || departed()) return 0;
    AcquireSRWLockExclusive(&world_lock);
    std::uint64_t published{};
    if (closed.load(std::memory_order_acquire) && ingress_quiescent() && !departed() && serial != UINT64_MAX) {
        published = ++serial;
        local_key = key; native_ready = true;
        sent_local = 0; sent_echo = 0;
        paired.store(false, std::memory_order_release);
        epoch.store(published, std::memory_order_release);
        closed.store(false, std::memory_order_release);
        update_pair_locked();
    }
    ReleaseSRWLockExclusive(&world_lock);
    if (published) {
        char line[256]{};
        std::snprintf(line, sizeof(line),
            "PLAYER-WORLD PUBLISHED local=%llu task=0x%X level=0x%X continue=(%X,%X,%X) wanted=(%X,%X) state=%X; awaiting mutual peer readiness",
            static_cast<unsigned long long>(published), key.task_id, key.level_key,
            key.continue_id04, key.continue_id08, key.continue_id1c,
            key.wanted_actor_id, key.wanted_level_id, key.spawn_state_id);
        boot_log_line(line);
    }
    return published;
}

void pump_control() {
    if constexpr (!kEnablePlayerWorldRearm) return;
    static ULONGLONG last_send{}; // existing receive worker only
    if (!g_state_net.enabled || !g_state_net.peer_set || departed() ||
        g_state_net.recv_socket == INVALID_SOCKET || !g_state_net.idle_arm_pose_session_id) return;
    const auto now = GetTickCount64();
    if (now-last_send < 50) return;
    Control packet{};
    AcquireSRWLockExclusive(&world_lock);
    packet.process_session = g_state_net.idle_arm_pose_session_id;
    packet.local = epoch.load(std::memory_order_relaxed);
    packet.echo = peer_epoch; packet.key = local_key;
    packet.role_b = g_net_role_b ? 1 : 0;
    packet.flags = closed.load(std::memory_order_acquire) ? 2 : (native_ready ? 1 : 0);
    packet.sequence = ++control_sequence;
    if (!packet.sequence) packet.sequence = ++control_sequence;
    ReleaseSRWLockExclusive(&world_lock);
    if (!packet.local) return;
    last_send = now;
    const int sent = sendto(g_state_net.recv_socket, reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
        reinterpret_cast<const sockaddr*>(&g_state_net.peer_addr), sizeof(g_state_net.peer_addr));
    if (sent != static_cast<int>(sizeof(packet))) return;
    bool newly_paired{};
    AcquireSRWLockExclusive(&world_lock);
    if (packet.local == epoch.load(std::memory_order_relaxed) && packet.echo == peer_epoch && (packet.flags & 1)) {
        sent_local = packet.local; sent_echo = packet.echo;
        newly_paired = update_pair_locked();
    }
    ReleaseSRWLockExclusive(&world_lock);
    if (newly_paired) report_pair({packet.local, packet.echo}, packet.key);
}

bool receive_control(const char* bytes, int size, const sockaddr_in& source, int source_size) noexcept {
    if constexpr (!kEnablePlayerWorldRearm) return false;
    std::uint32_t magic{};
    if (size < static_cast<int>(sizeof(magic))) return false;
    std::memcpy(&magic, bytes, sizeof(magic));
    if (magic != kControlMagic) return false;
    Control packet{};
    if (size != static_cast<int>(sizeof(packet)) || !configured_ip(source, source_size) ||
        source.sin_port != g_state_net.peer_addr.sin_port) return true;
    std::memcpy(&packet, bytes, sizeof(packet));
    if (packet.version != kVersion || packet.reserved || packet.role_b > 1 ||
        packet.role_b == (g_net_role_b ? 1 : 0) || (packet.flags & ~3u) ||
        (packet.flags & 3u) == 3u || !packet.local || !packet.sequence ||
        !pin_process(packet.process_session)) return true;
    bool newly_paired{};
    Token token{};
    Key paired_key{};
    AcquireSRWLockExclusive(&world_lock);
    if ((!peer_process || peer_process == packet.process_session) &&
        (!peer_sequence || static_cast<std::int32_t>(packet.sequence-peer_sequence) > 0) &&
        packet.local >= peer_epoch) {
        peer_process = packet.process_session; peer_sequence = packet.sequence;
        peer_epoch = packet.local; peer_echo = packet.echo; peer_key = packet.key;
        peer_native_ready = (packet.flags & 1) != 0;
        newly_paired = update_pair_locked();
        if (newly_paired) { token = {epoch.load(std::memory_order_relaxed), peer_epoch}; paired_key = local_key; }
    }
    ReleaseSRWLockExclusive(&world_lock);
    if (newly_paired) report_pair(token, paired_key);
    return true;
}

int send_datagram(SOCKET socket, const char* bytes, int size, int flags,
                  const sockaddr* target, int target_size, std::uint64_t origin) noexcept {
    if constexpr (!kEnablePlayerWorldRearm) return sendto(socket, bytes, size, flags, target, target_size);
    if (size < static_cast<int>(sizeof(std::uint32_t)) || size > static_cast<int>(kMaximumInnerBytes) || !bytes) {
        WSASetLastError(WSAEMSGSIZE); return SOCKET_ERROR;
    }
    std::uint32_t magic{}; std::memcpy(&magic, bytes, sizeof(magic));
    if (!known_inner(magic)) { WSASetLastError(WSAEINVAL); return SOCKET_ERROR; }
    Envelope envelope{};
    AcquireSRWLockShared(&world_lock);
    const bool valid = data_admitted() &&
        (origin == UINT64_MAX || (origin && origin == epoch.load(std::memory_order_relaxed)));
    if (valid) {
        envelope.process_session = g_state_net.idle_arm_pose_session_id;
        envelope.local = epoch.load(std::memory_order_relaxed); envelope.target = peer_epoch;
        envelope.inner_bytes = static_cast<std::uint32_t>(size);
    }
    ReleaseSRWLockShared(&world_lock);
    if (!valid) { WSASetLastError(WSAEWOULDBLOCK); return SOCKET_ERROR; }
    alignas(8) std::array<char, sizeof(Envelope)+kMaximumInnerBytes> wire;
    std::memcpy(wire.data(), &envelope, sizeof(envelope));
    std::memcpy(wire.data()+sizeof(envelope), bytes, static_cast<std::size_t>(size));
    const int sent = sendto(socket, wire.data(), static_cast<int>(sizeof(envelope))+size, flags, target, target_size);
    // Preserve legacy exact-size success checks and diagnostic payload lengths.
    return sent == static_cast<int>(sizeof(envelope))+size ? size : SOCKET_ERROR;
}

IngressGuard::IngressGuard(char* bytes, int& size, const sockaddr_in& source, int source_size) noexcept {
    if constexpr (!kEnablePlayerWorldRearm) { admitted_ = true; return; }
    Envelope envelope{};
    if (size < static_cast<int>(sizeof(envelope)) || !configured_ip(source, source_size)) return;
    std::memcpy(&envelope, bytes, sizeof(envelope));
    if (envelope.magic != kEnvelopeMagic || envelope.version != kVersion ||
        envelope.header_bytes != sizeof(envelope) || envelope.reserved ||
        envelope.inner_bytes < sizeof(std::uint32_t) || envelope.inner_bytes > kMaximumInnerBytes ||
        size != static_cast<int>(sizeof(envelope)+envelope.inner_bytes)) return;
    std::uint32_t magic{}; std::memcpy(&magic, bytes+sizeof(envelope), sizeof(magic));
    if (!known_inner(magic)) return;
    ingress.fetch_add(1, std::memory_order_acq_rel); counted_ = true;
    if (!data_admitted()) return;
    AcquireSRWLockShared(&g_state_net.peer_state_lock);
    const bool process_valid = !departed() && envelope.process_session &&
        envelope.process_session == g_state_net.peer_state_session_id;
    ReleaseSRWLockShared(&g_state_net.peer_state_lock);
    if (!process_valid) return;
    AcquireSRWLockShared(&world_lock);
    admitted_ = data_admitted() && envelope.process_session == peer_process &&
        envelope.local == peer_epoch && envelope.target == epoch.load(std::memory_order_relaxed);
    ReleaseSRWLockShared(&world_lock);
    if (!admitted_) return;
    std::memmove(bytes, bytes+sizeof(envelope), envelope.inner_bytes);
    size = static_cast<int>(envelope.inner_bytes);
}
IngressGuard::~IngressGuard() noexcept {
    if (counted_) ingress.fetch_sub(1, std::memory_order_release);
}

void reset_peer_snapshot_for_world() noexcept {
    if constexpr (!kEnablePlayerWorldRearm) return;
    if (!closed.load(std::memory_order_acquire) || !ingress_quiescent()) return;
    AcquireSRWLockExclusive(&g_state_net.peer_state_lock);
    g_state_net.peer_state = {};
    g_state_net.peer_state_world_epoch = 0;
    g_state_net.peer_state_received_ms.store(0, std::memory_order_release);
    g_state_net.has_peer_state.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&g_state_net.peer_idle_arm_pose_lock);
    g_state_net.peer_idle_arm_pose = {};
    g_state_net.peer_idle_arm_pose_assembly = {};
    g_state_net.peer_idle_arm_pose_received_ms.store(0, std::memory_order_release);
    g_state_net.has_peer_idle_arm_pose.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_state_net.peer_idle_arm_pose_lock);
    ReleaseSRWLockExclusive(&g_state_net.peer_state_lock);
}
} // namespace player_world
