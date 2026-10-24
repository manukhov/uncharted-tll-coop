#pragma once

// Included inside coop_runtime.cpp's private namespace, after the Windows and
// standard-library headers. This interface owns scalar network state only.
namespace player_world {
// Staged production code only: enable after native retirement, fresh-input
// service and transient-cache invalidation have all been integrated/reviewed.
// False preserves the deployed unwrapped transport and snapshot behavior; this
// is not a second live protocol, runtime toggle, or permission to rearm a game.
inline constexpr bool kEnablePlayerWorldRearm = false;
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
// Called only after close_for_reset() and ingress_quiescent(). Process identity
// and explicit departure tombstones deliberately survive this reset.
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
