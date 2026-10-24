#pragma once

// Included inside coop_runtime.cpp's private namespace, after the Windows and
// standard-library headers. This interface owns scalar network state only.
namespace player_world {
// Bounded native main-menu rejoin candidate. The exact full-postrender consumer,
// positive retirement, physical cycle and fresh-input package are mandatory.
// This is not a runtime toggle or general seamless/script-reset permission.
// Both peers must use this same build; false retains the old unwrapped protocol.
inline constexpr bool kEnablePlayerWorldRearm = true;
struct Key {
    std::uint32_t task_id{}, level_key{}, continue_id04{}, continue_id08{};
    std::uint32_t continue_id1c{}, wanted_actor_id{}, wanted_level_id{}, spawn_state_id{};
    bool operator==(const Key&) const = default;
};
struct Token { std::uint64_t local{}, peer{}; };

void close_for_reset() noexcept;
bool ingress_quiescent() noexcept;
std::uint64_t publish_local_world(const Key&) noexcept;
std::uint64_t local_epoch() noexcept;
bool peer_ready() noexcept;
bool data_admitted() noexcept;
std::uint64_t capture_epoch() noexcept;
void pump_control();
bool receive_control(const char*, int, const sockaddr_in&, int) noexcept;
int send_datagram(SOCKET, const char*, int, int, const sockaddr*, int,
                  std::uint64_t origin = UINT64_MAX) noexcept;
// Metadata-only, short WorldLock: save the last paired departure token before
// latching leave. No socket/game/log calls and no token for an unpaired world.
void queue_local_leave() noexcept;
bool copy_leave_request(Token&, std::uint64_t& request) noexcept;
int send_leave_datagram(SOCKET, const char*, int, int, const sockaddr*, int,
                        Token, std::uint64_t request) noexcept;
// Called only after close_for_reset() and ingress_quiescent(). Process identity
// and departure metadata survive this reset. ONLY publish_local_world() after
// fresh-primary/retirement proof clears old-world departure tombstones.
void reset_peer_snapshot_for_world() noexcept;

class IngressGuard {
public:
    IngressGuard(char*, int&, const sockaddr_in&, int) noexcept;
    ~IngressGuard() noexcept;
    IngressGuard(const IngressGuard&) = delete;
    IngressGuard& operator=(const IngressGuard&) = delete;
    explicit operator bool() const noexcept { return admitted_; }
private:
    bool counted_{}, admitted_{};
};
} // namespace player_world
