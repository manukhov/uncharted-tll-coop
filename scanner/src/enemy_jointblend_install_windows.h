#pragma once

#include "enemy_jointblend_native_contract.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace coop::enemy_jointblend_install_windows {
namespace native = enemy_jointblend_native_contract;
using native::Address;
using native::ModuleImage;
inline constexpr std::size_t kSiteCount = native::kSites.size();

enum class Status : std::uint8_t {
    kNotAttempted, kInstalled, kRestored, kBusy, kAlreadyAttempted,
    kInvalidInput, kUnsupportedCpu, kMissingThreadEnumerator, kStockMismatch,
    kInvalidMemory, kPinFailed, kRelayFailed, kThreadFreezeFailed,
    kThreadSetChanged, kThreadInPatchWord, kProtectionFailed, kCompareFailed,
    kFlushFailed, kProtectionRestoreFailed, kForeignMutation, kResumeFailed,
    kTestInjectedFailure,
};

struct Result {
    Status status{Status::kNotAttempted};
    std::uint32_t windows_error{};
    std::size_t changed_code_words{};
    bool published{}; // Ever published, including subsequently rolled-back writes.
    bool code_is_stock{};
    bool callback_is_stock{};
    bool protections_restored{true};
    bool threads_resumed{true};
    bool relays_retained{};
    // Enumeration is NOT a kernel-wide thread-creation fence. The integration
    // must assess this unresolved publication risk; Installed is not runtime GO.
    bool thread_creation_fenced{false};
};

#if defined(TLL_ENEMY_INSTALL_TESTING)
enum class TestFault : std::uint8_t {
    kNone, kAfterThirdWord, kAfterCallback, kForeignThirdWord, kForeignCallback,
    kProtectionOpen, kProtectionRestoreOnce, kFlushOnce, kResumeOnce,
    kForeignProtectionBeforeOpen,
};
#endif

// Current-process Windows x64 installer, never an attach/inject API.
// Call explicitly on a dedicated non-hook thread, outside DllMain/loader lock.
// Before initialize, publish all wrapper fallbacks/FLS/configuration. They must
// stay valid for process lifetime even on failure/restore. Hooks must forward
// exactly once when inactive. The installer cannot verify their ABI or behavior.
//
// No destructor unhooks, unloads or frees anything. Relay allocations and pinned
// modules intentionally survive for process lifetime. Restoring the callback
// global does not drain already queued callback values. Do not destroy this
// object while a failed result reports threads_resumed=false: it retains handles
// and suspension ownership for explicit restore() recovery on the SAME thread.
// Other successful/failed attempts cannot be automatically retried/reinstalled.
class Installer final {
public:
    Installer() noexcept = default;
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;

    Result initialize(ModuleImage module, const std::array<Address, kSiteCount>& hooks,
                      Address postpose_wrapper) noexcept;
    Result restore() noexcept;

#if defined(TLL_ENEMY_INSTALL_TESTING)
    // Compile-time synthetic-fixture seam, absent from the runtime build.
    void set_test_fault(TestFault fault) noexcept { test_fault_ = fault; }
#endif

private:
    struct Page {
        Address address{};
        std::uint32_t original_protection{};
        std::uint32_t restore_protection{};
        bool opened{};
        bool touched{};
    };
    struct Thread { void* handle{}; std::uint32_t id{}; };
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    bool attempted_{};
    bool prepared_{};
    bool published_{};
    bool installed_{};
    std::uint32_t owner_thread_{};
    ModuleImage module_{};
    Address postpose_wrapper_{};
    Address thread_enumerator_{};
    std::array<native::Rel32Plan, kSiteCount> plans_{};
    std::array<Address, kSiteCount> relays_{};
    std::array<Page, kSiteCount + 1> pages_{};
    std::size_t page_count_{};
    std::array<Thread, 256> threads_{};
    std::size_t thread_count_{};
#if defined(TLL_ENEMY_INSTALL_TESTING)
    TestFault test_fault_{TestFault::kNone};
    bool test_fault_fired_{};
#endif

    Result prepare(const std::array<Address, kSiteCount>& hooks) noexcept;
    Result transact(bool install) noexcept;
    Status freeze_and_check(std::uint32_t& error) noexcept;
    bool resume_threads() noexcept;
    bool open_pages() noexcept;
    bool restore_pages() noexcept;
    bool inspect_words(bool& stock, bool& own, bool& callback_stock,
                       bool& callback_own) const noexcept;
};
} // namespace coop::enemy_jointblend_install_windows
