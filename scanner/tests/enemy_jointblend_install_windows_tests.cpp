#include "enemy_jointblend_install_windows.h"

#ifndef TLL_ENEMY_INSTALL_TESTING
#error Build both this test and installer implementation with TLL_ENEMY_INSTALL_TESTING.
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <utility>

namespace {
namespace install = coop::enemy_jointblend_install_windows;
namespace native = coop::enemy_jointblend_native_contract;
using native::Address;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; \
            return false; \
        } \
    } while (false)

__declspec(noinline) Address synthetic_hook(Address value) { return value ^ 0x1234; }
__declspec(noinline) Address synthetic_postpose(Address value) { return value ^ 0x5678; }

struct Fixture {
    static constexpr Address kImageSize = 0x7000000;
    Address base{};
    std::array<native::Word16, install::kSiteCount> stock{};
    std::array<Address, 24> pages{};
    std::size_t page_count{};
    bool valid{};

    Fixture() {
        base = reinterpret_cast<Address>(VirtualAlloc(nullptr, kImageSize, MEM_RESERVE, PAGE_NOACCESS));
        if (base == 0) return;
        for (const auto& spec : native::kSites)
            if (!commit(spec.rva) || !commit(spec.target_rva)) return;
        if (!commit(native::kGamePostposeRva) || !commit(native::kPostposeSlotRva)) return;
        for (std::size_t i = 0; i < native::kSites.size(); ++i) {
            const auto& spec = native::kSites[i];
            auto* word = reinterpret_cast<std::uint8_t*>((base + spec.rva) & ~Address{15});
            for (std::size_t j = 0; j < 16; ++j) word[j] = static_cast<std::uint8_t>(0x90 + j);
            std::memcpy(reinterpret_cast<void*>(base + spec.rva), spec.stock.data(), spec.length);
            std::memcpy(stock[i].data(), word, 16);
        }
        const Address original = base + native::kGamePostposeRva;
        std::memcpy(reinterpret_cast<void*>(base + native::kPostposeSlotRva), &original, 8);
        for (std::size_t i = 0; i < page_count; ++i) {
            DWORD previous{};
            const DWORD protection = pages[i] == ((base + native::kPostposeSlotRva) & ~Address{0xFFF})
                ? PAGE_READWRITE : PAGE_EXECUTE_READ;
            if (!VirtualProtect(reinterpret_cast<void*>(pages[i]), 0x1000, protection, &previous)) return;
        }
        valid = true;
    }
    ~Fixture() { if (base != 0) VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE); }
    bool commit(Address rva) {
        const Address page = (base + rva) & ~Address{0xFFF};
        for (std::size_t i = 0; i < page_count; ++i) if (pages[i] == page) return true;
        if (page_count == pages.size() ||
            !VirtualAlloc(reinterpret_cast<void*>(page), 0x1000, MEM_COMMIT, PAGE_READWRITE)) return false;
        pages[page_count++] = page;
        return true;
    }
    native::ModuleImage module() const { return {base, kImageSize}; }
    std::array<Address, install::kSiteCount> hooks() const {
        std::array<Address, install::kSiteCount> values{};
        values.fill(reinterpret_cast<Address>(&synthetic_hook));
        return values;
    }
    Address wrapper() const { return reinterpret_cast<Address>(&synthetic_postpose); }
    Address callback() const {
        Address value{};
        std::memcpy(&value, reinterpret_cast<void*>(base + native::kPostposeSlotRva), 8);
        return value;
    }
    bool stock_code() const {
        for (std::size_t i = 0; i < native::kSites.size(); ++i)
            if (std::memcmp(reinterpret_cast<void*>((base + native::kSites[i].rva) & ~Address{15}),
                            stock[i].data(), 16) != 0) return false;
        return true;
    }
    bool protections() const {
        for (std::size_t i = 0; i < page_count; ++i) {
            MEMORY_BASIC_INFORMATION information{};
            const DWORD expected = pages[i] == ((base + native::kPostposeSlotRva) & ~Address{0xFFF})
                ? PAGE_READWRITE : PAGE_EXECUTE_READ;
            if (!VirtualQuery(reinterpret_cast<void*>(pages[i]), &information, sizeof(information)) ||
                information.Protect != expected) return false;
        }
        return true;
    }
    bool set_word(std::size_t index, const native::Word16& value) {
        const Address address = (base + native::kSites[index].rva) & ~Address{15};
        DWORD previous{}, ignored{};
        if (!VirtualProtect(reinterpret_cast<void*>(address), 16, PAGE_EXECUTE_READWRITE, &previous)) return false;
        std::memcpy(reinterpret_cast<void*>(address), value.data(), 16);
        return VirtualProtect(reinterpret_cast<void*>(address), 16, previous, &ignored) &&
               FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), 16);
    }
};

bool installs_and_restores_exact_words() {
    Fixture fixture;
    CHECK(fixture.valid);
    install::Installer installer;
    const auto result = installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper());
    CHECK(result.status == install::Status::kInstalled);
    CHECK(result.changed_code_words == 7);
    CHECK(result.published && result.relays_retained && result.protections_restored && result.threads_resumed);
    CHECK(!result.thread_creation_fenced);
    CHECK(fixture.callback() == fixture.wrapper());
    for (std::size_t i = 0; i < native::kSites.size(); ++i) {
        const auto& spec = native::kSites[i];
        native::Word16 actual{};
        std::memcpy(actual.data(), reinterpret_cast<void*>((fixture.base + spec.rva) & ~Address{15}), 16);
        const std::size_t displacement = (spec.rva & 15) + spec.displacement_offset;
        for (std::size_t j = 0; j < 16; ++j)
            if (j < displacement || j >= displacement + 4) CHECK(actual[j] == fixture.stock[i][j]);
        std::int32_t relative{};
        std::memcpy(&relative, actual.data() + displacement, 4);
        const Address end = fixture.base + spec.instruction_end_rva;
        const Address relay = relative >= 0 ? end + static_cast<Address>(relative)
            : end - static_cast<Address>(-static_cast<std::int64_t>(relative));
        const std::array<std::uint8_t, 6> prefix{0xFF, 0x25, 0, 0, 0, 0};
        CHECK(std::memcmp(reinterpret_cast<void*>(relay), prefix.data(), prefix.size()) == 0);
        Address target{};
        std::memcpy(&target, reinterpret_cast<void*>(relay + 6), 8);
        CHECK(target == reinterpret_cast<Address>(&synthetic_hook));
        MEMORY_BASIC_INFORMATION information{};
        CHECK(VirtualQuery(reinterpret_cast<void*>(relay), &information, sizeof(information)) != 0);
        CHECK(information.Protect == PAGE_EXECUTE_READ);
    }
    CHECK(fixture.protections());
    const auto restored = installer.restore();
    CHECK(restored.status == install::Status::kRestored);
    CHECK(restored.published && restored.relays_retained && restored.threads_resumed);
    CHECK(restored.code_is_stock && restored.callback_is_stock);
    CHECK(fixture.stock_code() && fixture.protections());
    CHECK(fixture.callback() == fixture.base + native::kGamePostposeRva);
    CHECK(installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper()).status == install::Status::kAlreadyAttempted);
    return true;
}

bool preflight_rejects_stock_and_callback_changes() {
    Fixture fixture;
    CHECK(fixture.valid);
    auto corrupted = fixture.stock[0];
    corrupted[native::kSites[0].rva & 15] ^= 1;
    CHECK(fixture.set_word(0, corrupted));
    install::Installer code_installer;
    const auto failed = code_installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper());
    CHECK(failed.status == install::Status::kStockMismatch && !failed.published);
    CHECK(fixture.set_word(0, fixture.stock[0]));
    const Address nd = fixture.base + native::kNdPostposeRva;
    std::memcpy(reinterpret_cast<void*>(fixture.base + native::kPostposeSlotRva), &nd, 8);
    install::Installer callback_installer;
    CHECK(callback_installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper()).status == install::Status::kStockMismatch);
    CHECK(fixture.callback() == nd && fixture.stock_code());
    return true;
}

bool invalid_input_has_no_publication() {
    Fixture fixture;
    CHECK(fixture.valid);
    install::Installer invalid;
    CHECK(invalid.initialize({fixture.base, 1}, fixture.hooks(), fixture.wrapper()).status == install::Status::kInvalidInput);
    auto hooks = fixture.hooks();
    hooks[3] = 0;
    install::Installer null_hook;
    CHECK(null_hook.initialize(fixture.module(), hooks, fixture.wrapper()).status == install::Status::kInvalidInput);
    hooks[3] = fixture.base + native::kSites[3].target_rva;
    install::Installer native_as_hook;
    CHECK(native_as_hook.initialize(fixture.module(), hooks, fixture.wrapper()).status == install::Status::kInvalidInput);
    CHECK(fixture.stock_code() && fixture.protections());
    return true;
}

bool injected_failure_rolls_back() {
    for (const auto fault : {install::TestFault::kAfterThirdWord, install::TestFault::kAfterCallback}) {
        Fixture fixture;
        CHECK(fixture.valid);
        install::Installer installer;
        installer.set_test_fault(fault);
        const auto result = installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper());
        CHECK(result.status == install::Status::kTestInjectedFailure);
        CHECK(result.published && result.relays_retained && result.threads_resumed);
        CHECK(result.code_is_stock && result.callback_is_stock && result.protections_restored);
        CHECK(fixture.stock_code() && fixture.protections());
    }
    return true;
}

bool rollback_never_overwrites_foreign_word() {
    Fixture fixture;
    CHECK(fixture.valid);
    install::Installer installer;
    installer.set_test_fault(install::TestFault::kForeignThirdWord);
    const auto result = installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper());
    CHECK(result.status == install::Status::kForeignMutation);
    CHECK(result.published && result.relays_retained && result.protections_restored);
    const Address third = (fixture.base + native::kSites[2].rva) & ~Address{15};
    CHECK(*reinterpret_cast<std::uint8_t*>(third) == static_cast<std::uint8_t>(fixture.stock[2][0] ^ 1));
    // Synthetic fixture repair only; the installer itself never guesses a
    // foreign word. Restore can then recover its retained stop, if any.
    CHECK(fixture.set_word(2, fixture.stock[2]));
    const auto restored = installer.restore();
    CHECK(restored.status == install::Status::kRestored && restored.threads_resumed);
    CHECK(fixture.stock_code() && fixture.protections());
    return true;
}

bool rollback_never_overwrites_foreign_callback() {
    Fixture fixture;
    CHECK(fixture.valid);
    install::Installer installer;
    installer.set_test_fault(install::TestFault::kForeignCallback);
    const auto result = installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper());
    CHECK(result.status == install::Status::kForeignMutation && result.published);
    CHECK(fixture.callback() == fixture.base + native::kNdPostposeRva);
    CHECK(fixture.stock_code());
    const Address game = fixture.base + native::kGamePostposeRva;
    std::memcpy(reinterpret_cast<void*>(fixture.base + native::kPostposeSlotRva), &game, 8);
    CHECK(installer.restore().status == install::Status::kRestored);
    return true;
}

bool protection_and_flush_failures_recover() {
    for (const auto fault : {install::TestFault::kProtectionOpen,
                            install::TestFault::kProtectionRestoreOnce, install::TestFault::kFlushOnce}) {
        Fixture fixture;
        CHECK(fixture.valid);
        install::Installer installer;
        installer.set_test_fault(fault);
        const auto result = installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper());
        const auto expected = fault == install::TestFault::kProtectionOpen ? install::Status::kProtectionFailed :
            fault == install::TestFault::kFlushOnce ? install::Status::kFlushFailed : install::Status::kProtectionRestoreFailed;
        CHECK(result.status == expected);
        CHECK(result.published == (fault != install::TestFault::kProtectionOpen));
        CHECK(result.code_is_stock && result.callback_is_stock && result.protections_restored && result.threads_resumed);
        CHECK(fixture.stock_code() && fixture.protections());
    }
    return true;
}

bool untouched_foreign_protection_is_not_repaired() {
    Fixture fixture;
    CHECK(fixture.valid);
    install::Installer installer;
    installer.set_test_fault(install::TestFault::kForeignProtectionBeforeOpen);
    const auto result = installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper());
    CHECK(result.status == install::Status::kProtectionFailed);
    CHECK(!result.published && result.threads_resumed && result.protections_restored);
    MEMORY_BASIC_INFORMATION information{};
    const Address page = (fixture.base + native::kSites[0].rva) & ~Address{0xFFF};
    CHECK(VirtualQuery(reinterpret_cast<void*>(page), &information, sizeof(information)) != 0);
    CHECK(information.Protect == PAGE_READONLY);
    CHECK(fixture.stock_code());
    return true;
}

DWORD WINAPI trivial_thread(void*) { return 0; }

bool resume_failure_retains_ownership_until_explicit_restore() {
    Fixture fixture;
    CHECK(fixture.valid);
    std::array<HANDLE, 2> threads{
        CreateThread(nullptr, 0, &trivial_thread, nullptr, CREATE_SUSPENDED, nullptr),
        CreateThread(nullptr, 0, &trivial_thread, nullptr, CREATE_SUSPENDED, nullptr)};
    CHECK(threads[0] != nullptr && threads[1] != nullptr);
    install::Installer installer;
    installer.set_test_fault(install::TestFault::kResumeOnce);
    const auto result = installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper());
    CHECK(result.status == install::Status::kResumeFailed && result.published && !result.threads_resumed);
    const auto restored = installer.restore();
    CHECK(restored.status == install::Status::kRestored && restored.threads_resumed);
    CHECK(fixture.stock_code() && fixture.protections());
    // One helper was resumed successfully on the first attempt; recovery must
    // reacquire it while leaving the retained helper's owned count unchanged.
    for (HANDLE thread : threads) {
        CHECK(ResumeThread(thread) == 1); // Original fixture-owned suspension remains.
        CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
        CloseHandle(thread);
    }
    return true;
}

bool suspended_rip_in_entire_word_is_rejected() {
    Fixture fixture;
    CHECK(fixture.valid);
    HANDLE thread = CreateThread(nullptr, 0, &trivial_thread, nullptr, CREATE_SUSPENDED, nullptr);
    CHECK(thread != nullptr);
    CONTEXT original{};
    original.ContextFlags = CONTEXT_CONTROL;
    CHECK(GetThreadContext(thread, &original));
    CONTEXT changed = original;
    // First word's byte0 is a NEIGHBOR, outside the seven-byte LEA span.
    changed.Rip = (fixture.base + native::kSites[0].rva) & ~Address{15};
    CHECK(SetThreadContext(thread, &changed));
    install::Installer installer;
    const auto result = installer.initialize(fixture.module(), fixture.hooks(), fixture.wrapper());
    CHECK(result.status == install::Status::kThreadInPatchWord);
    CHECK(!result.published && result.threads_resumed && fixture.stock_code());
    CHECK(SetThreadContext(thread, &original));
    CHECK(ResumeThread(thread) == 1); // Installer restored ONLY its own increment.
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    return true;
}
} // namespace

int main() {
    if (!IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) {
        std::cerr << "Synthetic installer tests require CMPXCHG16B\n";
        return 1;
    }
    using Test = std::pair<const char*, bool (*)()>;
    const std::array<Test, 10> tests{{
        {"install/restore exact words", installs_and_restores_exact_words},
        {"stock/callback preflight", preflight_rejects_stock_and_callback_changes},
        {"invalid inputs", invalid_input_has_no_publication},
        {"transaction rollback", injected_failure_rolls_back},
        {"foreign word remains untouched", rollback_never_overwrites_foreign_word},
        {"foreign callback remains untouched", rollback_never_overwrites_foreign_callback},
        {"protection/cache failure recovery", protection_and_flush_failures_recover},
        {"untouched foreign protection", untouched_foreign_protection_is_not_repaired},
        {"resume failure retains ownership", resume_failure_retains_ownership_until_explicit_restore},
        {"full-word RIP exclusion", suspended_rip_in_entire_word_is_rejected},
    }};
    std::size_t passed = 0;
    for (const auto& test : tests) {
        if (!test.second()) { std::cerr << "FAIL: " << test.first << '\n'; return 1; }
        ++passed;
    }
    std::cout << passed << '/' << tests.size() << " Windows synthetic installer tests passed\n";
    return 0;
}
