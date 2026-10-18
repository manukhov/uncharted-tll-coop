#pragma once
#include "enemy_jointblend_native_contract.h"
#include "nadine_p99_debug_windows.h"

namespace coop::enemy_jointblend_remote_install {
namespace native = enemy_jointblend_native_contract;
namespace debug = p99_debug_lifecycle;
using native::Address;
inline constexpr std::size_t kSites = native::kSites.size();
inline constexpr bool kRuntimeAdmission = false;

#if defined(TLL_ENEMY_REMOTE_INSTALL_AUDIT)
// Compiled only in the owned-child fault fixture, never in the real library.
struct Audit {
    BOOL (*write)(HANDLE, LPVOID, LPCVOID, SIZE_T, SIZE_T*) noexcept{};
    BOOL (*protect)(HANDLE, LPVOID, SIZE_T, DWORD, PDWORD) noexcept{};
    BOOL (*flush)(HANDLE, LPCVOID, SIZE_T) noexcept{};
};
extern const Audit* g_audit;
#endif

// Prepared by the target BEFORE a debugger hold. Callbacks, fallbacks, FLS and
// permanent RX relays must already exist; no target-side work runs while held.
// This transport does not certify a preparation offer, DLL integrity or lifetime.
struct Prepared {
    native::ModuleImage module{};
    std::array<Address, kSites> hooks{}, relays{};
    Address postpose{};
};
enum class Status { refused, installed, restored, rolled_back, unsafe_held };
enum class Error { none, already_attempted, foreign_hold, invalid_plan, memory,
    threads, rip_in_word, foreign_bytes, protection, write, flush, dependencies };
struct Result {
    Status status{Status::refused};
    Error error{};
    bool wrote{}, protections_restored{}, memory_state_verified{};
    // NEVER interpret this as full release admission: prepared ABI/configuration,
    // target/module retention and the worker protocol require separate proof.
};

// External-only, dedicated debugger-owner thread. Uses an already received and
// identity-bound held event, not SuspendThread/enumeration as a stop fence.
// Never calls Continue/Detach, executes target code, allocates remote relays,
// changes thread contexts or frees anything. Destructor has no OS side effects.
// Keep Session/Ops/debugger thread alive after unsafe_held; Windows auto-detach
// on debugger-thread death is NOT crash-atomic rollback.
// Restore is explicit recovery under the SAME still-held Session, including
// UnsafeHeld. Reattachment to remove a previously released install is not supported.
class Installer final {
public:
    Installer() = default;
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;
    Result install(const Prepared&, debug::Session&, debug::WindowsOps&) noexcept;
    Result restore(debug::Session&, debug::WindowsOps&) noexcept;
private:
    struct Page { Address address{}; DWORD protection{}; bool touched{}; };
    Prepared prepared_{};
    std::array<native::Rel32Plan, kSites> plans_{};
    std::array<Page, kSites + 1> pages_{};
    std::size_t page_count_{};
    debug::Session* session_{};
    debug::WindowsOps* ops_{};
    bool attempted_{}, ready_{}, ever_wrote_{};
    bool held() const noexcept;
    bool read(Address, void*, std::size_t) const noexcept;
    bool write(Address, const void*, std::size_t) noexcept;
    bool region(Address, std::size_t, bool, Address allocation = 0) const noexcept;
    bool dependencies() const noexcept;
    bool bytes(bool installed) const noexcept;
    bool known_bytes() const noexcept;
    Error prepare() noexcept;
    Error check_rips() const noexcept;
    bool open_pages() noexcept;
    bool restore_pages() noexcept;
    bool flush() const noexcept;
    bool rollback_owned() noexcept;
    Result transact(bool installing) noexcept;
};
} // namespace coop::enemy_jointblend_remote_install
