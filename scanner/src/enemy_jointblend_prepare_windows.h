#pragma once
#include "enemy_jointblend_prepare.h"
#include <Windows.h>
#include <atomic>

// Off-loader/off-callback local preparation. No game writes, installer, arm,
// worker, target calls, cleanup/unload or external release admission.
namespace coop::enemy_jointblend_prepare_windows {
namespace prep = enemy_jointblend_prepare;
namespace native = enemy_jointblend_native_contract;
using native::Address;
inline constexpr std::size_t kPageBytes = 4096;
using RelayBytes = std::array<std::uint8_t, 14>;

// A one-shot owner retains every allocation even on partial failure. The
// target's actual MEM_IMAGE is PINned before allocation. Each leaf is a plain
// absolute tail jump; it changes no registers, stack slots or return addresses.
// Sites 0/1 have separate relays to the same JointBlend wrapper; postpose stays
// a direct callback, NOT an eighth relay. Does not certify target instructions.
// prepare has one-shot admission; verify/addresses are owner-only, or after a
// separate join/serialization proves prepare returned. No concurrent snapshots.
class Relays final {
public:
    Relays() = default;
    Relays(const Relays&) = delete;
    Relays& operator=(const Relays&) = delete;
    bool prepare(native::ModuleImage main, native::ModuleImage self,
        const std::array<Address, 7>& hooks) noexcept;
    bool verify() const noexcept;
    // Owned addresses for diagnostics even on refusal; NEVER install unless
    // the full future external certificate is independently satisfied.
    const auto& addresses() const noexcept { return addresses_; }
private:
    std::array<Address, 7> addresses_{}, hooks_{};
    native::ModuleImage main_{}, self_{};
    std::atomic<bool> attempted_{false};
    bool ready_{};
};

enum class PublishResult { published, reused, invalid, page, write, seal, verify };
class Publisher final {
public:
    Publisher() = default;
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;
    // Caller already retains all referenced resources. A failure may leave a
    // partial/full RW or RO page. Never erase, unseal or retry after any result.
    PublishResult publish(prep::Page*, Address self, const prep::Binding&) noexcept;
private:
    std::atomic<bool> attempted_{false};
};
// Local readback only, not ownership/ABI/identity/release proof. Can be true
// after a reported seal failure that took effect: success requires the entire
// preparation protocol, not this predicate, a magic, or page protection alone.
bool published(const prep::Page*, Address self, const prep::Binding&) noexcept;

#if defined(TLL_ENEMY_PREPARE_AUDIT)
// Separate owned fixture build only; not present in the production library/DLL.
struct Audit {
    LPVOID (*allocate)(LPVOID, SIZE_T, DWORD, DWORD) noexcept{};
    BOOL (*protect)(LPVOID, SIZE_T, DWORD, PDWORD) noexcept{};
    BOOL (*flush)(HANDLE, LPCVOID, SIZE_T) noexcept{};
    void (*before_write)(Address, std::size_t) noexcept{};
    // Models a partial write followed by failure. Production uses the SEH leaf.
    bool (*write)(Address, const void*, std::size_t) noexcept{};
    void (*after_read)(Address, std::size_t) noexcept{};
};
extern const Audit* g_audit;
#endif
}
