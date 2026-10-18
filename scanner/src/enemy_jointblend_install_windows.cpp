#include "enemy_jointblend_install_windows.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>
#include <cstring>

#if !defined(_M_X64)
#error This installer requires MSVC-compatible Windows x64 intrinsics and SEH.
#endif

namespace coop::enemy_jointblend_install_windows {
namespace {
constexpr std::size_t kPageBytes = 0x1000;
constexpr std::size_t kRelayBytes = 14;
constexpr LONG kNoMoreEntries = static_cast<LONG>(0x8000001AUL);
// Native interface declaration checked against System Informer's phnt/ntpsapi.h:
// https://github.com/winsiderss/phnt/blob/master/ntpsapi.h (NtGetNextThread).
// Resolve before suspension. Unlike Toolhelp snapshot creation, this bounded
// enumeration requires no user-space snapshot/heap allocation while frozen.
using GetNextThread = LONG (NTAPI*)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);

bool readable(DWORD protect) noexcept {
    if ((protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    const DWORD basic = protect & 0xFF;
    return basic == PAGE_READONLY || basic == PAGE_READWRITE || basic == PAGE_WRITECOPY ||
           basic == PAGE_EXECUTE_READ || basic == PAGE_EXECUTE_READWRITE ||
           basic == PAGE_EXECUTE_WRITECOPY;
}
bool executable(DWORD protect) noexcept {
    if (!readable(protect)) return false;
    const DWORD basic = protect & 0xFF;
    return basic == PAGE_EXECUTE_READ || basic == PAGE_EXECUTE_READWRITE ||
           basic == PAGE_EXECUTE_WRITECOPY;
}
bool region(Address address, std::size_t bytes, bool execute, Address allocation = 0) noexcept {
    if (address == 0 || bytes == 0 || address > native::kAddressMax - bytes) return false;
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &information, sizeof(information)) !=
        sizeof(information)) return false;
    const auto begin = reinterpret_cast<Address>(information.BaseAddress);
    return information.State == MEM_COMMIT && readable(information.Protect) &&
           (!execute || executable(information.Protect)) && address >= begin &&
           address - begin <= information.RegionSize &&
           bytes <= information.RegionSize - (address - begin) &&
           (allocation == 0 || reinterpret_cast<Address>(information.AllocationBase) == allocation);
}
bool copy_bytes(Address address, void* destination, std::size_t bytes) noexcept {
    __try { std::memcpy(destination, reinterpret_cast<const void*>(address), bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool compare_word(Address address, const native::Word16& expected,
                  const native::Word16& replacement) noexcept {
    alignas(16) LONG64 compare[2]{};
    LONG64 desired[2]{};
    std::memcpy(compare, expected.data(), 16);
    std::memcpy(desired, replacement.data(), 16);
    __try {
        return _InterlockedCompareExchange128(reinterpret_cast<volatile LONG64*>(address),
                                               desired[1], desired[0], compare) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool compare_pointer(Address address, Address expected, Address desired) noexcept {
    __try {
        return static_cast<Address>(_InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(address), static_cast<LONG64>(desired),
            static_cast<LONG64>(expected))) == expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool pin_code(Address address) noexcept {
    if (!region(address, 1, true)) return false;
    HMODULE module{};
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                             reinterpret_cast<LPCWSTR>(address), &module) != FALSE;
}
Address allocate_relay(Address instruction_end, Address hook) noexcept {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const Address granularity = info.dwAllocationGranularity;
    if (granularity == 0 || (granularity & (granularity - 1)) != 0) return 0;
    const Address anchor = instruction_end & ~(granularity - 1);
    const Address minimum = reinterpret_cast<Address>(info.lpMinimumApplicationAddress);
    const Address maximum = reinterpret_cast<Address>(info.lpMaximumApplicationAddress);
    // Bounded search, 64 KiB granularity on supported Windows. Failed candidate
    // allocations have never been published; successfully allocated pages are
    // deliberately retained even when later sealing/planning fails.
    for (Address distance = granularity; distance <= 0x7FFF0000; distance += granularity) {
        const Address candidates[2]{anchor >= distance ? anchor - distance : 0,
            anchor <= native::kAddressMax - distance ? anchor + distance : 0};
        for (const Address candidate : candidates) {
            if (candidate < minimum || candidate > maximum ||
                kPageBytes > maximum - candidate || !native::encode_rel32(instruction_end, candidate).valid)
                continue;
            auto* memory = static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate),
                kPageBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            if (memory == nullptr) continue;
            const Address allocated = reinterpret_cast<Address>(memory);
            if (allocated != candidate) return 0;
            const std::uint8_t prefix[6]{0xFF, 0x25, 0, 0, 0, 0};
            std::memcpy(memory, prefix, sizeof(prefix));
            std::memcpy(memory + 6, &hook, sizeof(hook));
            DWORD old{};
            if (!VirtualProtect(memory, kPageBytes, PAGE_EXECUTE_READ, &old) ||
                !FlushInstructionCache(GetCurrentProcess(), memory, kRelayBytes)) return 0;
            return allocated;
        }
    }
    return 0;
}
} // namespace

Result Installer::initialize(ModuleImage module, const std::array<Address, kSiteCount>& hooks,
                             Address postpose_wrapper) noexcept {
    if (busy_.test_and_set(std::memory_order_acquire)) return {Status::kBusy};
    if (attempted_) { busy_.clear(std::memory_order_release); return {Status::kAlreadyAttempted}; }
    attempted_ = true;
    owner_thread_ = GetCurrentThreadId();
    module_ = module;
    postpose_wrapper_ = postpose_wrapper;
    Result result = prepare(hooks);
    if (prepared_) result = transact(true);
    busy_.clear(std::memory_order_release);
    return result;
}

Result Installer::prepare(const std::array<Address, kSiteCount>& hooks) noexcept {
    Result result{};
    if (!native::valid_module(module_) ||
        !native::contains_rva(module_, native::kPostposeSlotRva, 8) || postpose_wrapper_ == 0) {
        result.status = Status::kInvalidInput; return result;
    }
    if (!IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) {
        result.status = Status::kUnsupportedCpu; return result;
    }
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    thread_enumerator_ = reinterpret_cast<Address>(ntdll ? GetProcAddress(ntdll, "NtGetNextThread") : nullptr);
    if (thread_enumerator_ == 0) { result.status = Status::kMissingThreadEnumerator; return result; }
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    if (info.dwPageSize != kPageBytes) { result.status = Status::kInvalidInput; return result; }
    std::array<native::Word16, kSiteCount> words{};
    Address callback{};
    for (std::size_t i = 0; i < kSiteCount; ++i) {
        const auto& spec = native::kSites[i];
        if (!native::contains_rva(module_, spec.rva, spec.length) ||
            !native::contains_rva(module_, spec.target_rva, 1) || hooks[i] == 0 ||
            (hooks[i] >= module_.base && hooks[i] - module_.base < module_.size)) {
            result.status = Status::kInvalidInput; return result;
        }
        const Address word = (module_.base + spec.rva) & ~Address{15};
        if (!region(word, 16, true, module_.base) ||
            !region(module_.base + spec.target_rva, 1, true, module_.base) ||
            !copy_bytes(word, words[i].data(), 16)) {
            result.status = Status::kInvalidMemory; return result;
        }
        const auto stock = native::plan_rel32_site(module_, spec.id, word, words[i], module_.base + spec.target_rva);
        if (!stock.accepted()) { result.status = Status::kStockMismatch; return result; }
    }
    const Address slot = module_.base + native::kPostposeSlotRva;
    if ((postpose_wrapper_ >= module_.base && postpose_wrapper_ - module_.base < module_.size) ||
        !region(module_.base + native::kGamePostposeRva, 1, true, module_.base) ||
        !region(slot, 8, false, module_.base) || !copy_bytes(slot, &callback, 8)) {
        result.status = Status::kInvalidMemory; return result;
    }
    if (!native::validate_postpose_slot(module_, slot, callback)) {
        result.status = Status::kStockMismatch; return result;
    }
    // Pin EVERY supplied target module before any publication, not just this
    // installer's DLL; tests use functions in the already resident test EXE.
    for (const Address hook : hooks) if (!pin_code(hook)) {
        result.status = Status::kPinFailed; result.windows_error = GetLastError(); return result;
    }
    if (!pin_code(postpose_wrapper_) || !pin_code(reinterpret_cast<Address>(&pin_code))) {
        result.status = Status::kPinFailed; result.windows_error = GetLastError(); return result;
    }
    for (std::size_t i = 0; i < kSiteCount; ++i) {
        const auto& spec = native::kSites[i];
        relays_[i] = allocate_relay(module_.base + spec.instruction_end_rva, hooks[i]);
        result.relays_retained = true;
        if (relays_[i] == 0) { result.status = Status::kRelayFailed; return result; }
        plans_[i] = native::plan_rel32_site(module_, spec.id,
            (module_.base + spec.rva) & ~Address{15}, words[i], relays_[i]);
        if (!plans_[i].accepted()) { result.status = Status::kRelayFailed; return result; }
    }
    for (std::size_t i = 0; i <= kSiteCount; ++i) {
        const Address address = i == kSiteCount ? slot : plans_[i].word_address;
        const Address page = address & ~Address{kPageBytes - 1};
        bool seen = false;
        for (std::size_t p = 0; p < page_count_; ++p) seen = seen || pages_[p].address == page;
        if (seen) continue;
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQuery(reinterpret_cast<void*>(page), &information, sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT || !readable(information.Protect)) {
            result.status = Status::kInvalidMemory; return result;
        }
        pages_[page_count_++] = {page, information.Protect, information.Protect, false, false};
    }
    prepared_ = true;
    return result;
}

Status Installer::freeze_and_check(std::uint32_t& error) noexcept {
    const auto enumerate = reinterpret_cast<GetNextThread>(thread_enumerator_);
    const DWORD current = GetCurrentThreadId();
    // Recovery keeps prior owned increments, but reacquires other currently
    // running threads: a previous partial resume may have released them.
    for (unsigned pass = 0; pass < 2; ++pass) {
        HANDLE cursor = nullptr;
        bool cursor_owned = false;
        for (;;) {
            HANDLE next{};
            const LONG status = enumerate(GetCurrentProcess(), cursor,
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION,
                0, 0, &next);
            if (cursor != nullptr && !cursor_owned) CloseHandle(cursor);
            cursor = nullptr;
            cursor_owned = false;
            if (status == kNoMoreEntries) break;
            if (status < 0 || next == nullptr) { error = static_cast<std::uint32_t>(status); return Status::kThreadFreezeFailed; }
            cursor = next;
            const DWORD id = GetThreadId(next);
            if (id == 0) { error = GetLastError(); CloseHandle(next); return Status::kThreadFreezeFailed; }
            if (id == current) continue;
            bool known = false;
            for (std::size_t i = 0; i < thread_count_; ++i) known = known || threads_[i].id == id;
            if (known) continue;
            if (pass != 0) { CloseHandle(next); return Status::kThreadSetChanged; }
            if (thread_count_ == threads_.size()) { CloseHandle(next); return Status::kThreadFreezeFailed; }
            if (SuspendThread(next) == static_cast<DWORD>(-1)) {
                error = GetLastError(); CloseHandle(next); return Status::kThreadFreezeFailed;
            }
            threads_[thread_count_++] = {next, id};
            cursor_owned = true;
        }
    }
    for (std::size_t i = 0; i < thread_count_; ++i) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(threads_[i].handle, &context)) {
            error = GetLastError(); return Status::kThreadFreezeFailed;
        }
        for (const auto& plan : plans_)
            if (context.Rip >= plan.word_address && context.Rip - plan.word_address < 16)
                return Status::kThreadInPatchWord;
    }
    return Status::kNotAttempted;
}

bool Installer::resume_threads() noexcept {
    bool okay = true;
    for (std::size_t i = thread_count_; i > 0; --i) {
        auto& thread = threads_[i - 1];
        if (thread.handle == nullptr) continue;
#if defined(TLL_ENEMY_INSTALL_TESTING)
        if (test_fault_ == TestFault::kResumeOnce && !test_fault_fired_) {
            test_fault_fired_ = true; okay = false; continue;
        }
#endif
        if (ResumeThread(thread.handle) == static_cast<DWORD>(-1)) { okay = false; continue; }
        CloseHandle(thread.handle);
        thread = {};
    }
    std::size_t retained = 0;
    for (std::size_t i = 0; i < thread_count_; ++i)
        if (threads_[i].handle != nullptr) threads_[retained++] = threads_[i];
    thread_count_ = retained;
    return okay;
}

bool Installer::open_pages() noexcept {
    for (std::size_t i = 0; i < page_count_; ++i) {
        auto& page = pages_[i];
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQuery(reinterpret_cast<void*>(page.address), &information, sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT || reinterpret_cast<Address>(information.AllocationBase) != module_.base ||
            information.Protect != (page.opened ? PAGE_EXECUTE_READWRITE : page.original_protection)) return false;
        if (page.opened) continue;
#if defined(TLL_ENEMY_INSTALL_TESTING)
        if (test_fault_ == TestFault::kProtectionOpen && i == 2 && !test_fault_fired_) {
            test_fault_fired_ = true; return false;
        }
#endif
        DWORD previous{};
        if (!VirtualProtect(reinterpret_cast<void*>(page.address), kPageBytes, PAGE_EXECUTE_READWRITE, &previous)) return false;
        page.touched = true;
        page.opened = true;
        // Restore the actual protection we replaced, even if a competing change
        // occurred between query and protect. Such a race still rejects install.
        page.restore_protection = previous;
        if (previous != page.original_protection) return false;
    }
    return true;
}
bool Installer::restore_pages() noexcept {
    bool okay = true;
    for (std::size_t i = 0; i < page_count_; ++i) {
        auto& page = pages_[i];
        if (!page.touched) continue; // Untouched foreign protection is not ours to repair.
        MEMORY_BASIC_INFORMATION before{};
        if (VirtualQuery(reinterpret_cast<void*>(page.address), &before, sizeof(before)) != sizeof(before) ||
            before.State != MEM_COMMIT || reinterpret_cast<Address>(before.AllocationBase) != module_.base ||
            (page.opened && before.Protect != PAGE_EXECUTE_READWRITE)) {
            okay = false;
            continue; // Do not restore through a replaced mapping/foreign protection.
        }
        if (page.opened) {
#if defined(TLL_ENEMY_INSTALL_TESTING)
            if (test_fault_ == TestFault::kProtectionRestoreOnce && !test_fault_fired_) {
                test_fault_fired_ = true; okay = false; continue;
            }
#endif
            DWORD previous{};
            if (!VirtualProtect(reinterpret_cast<void*>(page.address), kPageBytes, page.restore_protection, &previous)) {
                okay = false; continue;
            }
            page.opened = false;
        }
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQuery(reinterpret_cast<void*>(page.address), &information, sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT || information.Protect != page.restore_protection ||
            reinterpret_cast<Address>(information.AllocationBase) != module_.base) okay = false;
    }
    return okay;
}

bool Installer::inspect_words(bool& stock, bool& own, bool& callback_stock,
                              bool& callback_own) const noexcept {
    stock = true; own = true;
    bool known = true;
    for (const auto& plan : plans_) {
        native::Word16 observed{};
        if (!copy_bytes(plan.word_address, observed.data(), observed.size())) return false;
        stock = stock && observed == plan.expected;
        own = own && observed == plan.replacement;
        known = known && (observed == plan.expected || observed == plan.replacement);
    }
    Address callback{};
    if (!copy_bytes(module_.base + native::kPostposeSlotRva, &callback, sizeof(callback))) return false;
    callback_stock = callback == module_.base + native::kGamePostposeRva;
    callback_own = callback == postpose_wrapper_;
    return known && (callback_stock || callback_own);
}

Result Installer::transact(bool install) noexcept {
    Result result{};
    result.published = published_;
    result.relays_retained = true;
    const bool recovering_stop = thread_count_ != 0;
    const Status frozen = freeze_and_check(result.windows_error);
    if (frozen != Status::kNotAttempted) {
        result.status = frozen;
        // A failed fresh census has made no code changes and releases only its
        // own increments. A previously retained unsafe stop needs full recovery,
        // not an automatic thaw merely because this new census failed.
        result.threads_resumed = !recovering_stop ? resume_threads() : false;
        if (!result.threads_resumed && !recovering_stop) result.status = Status::kResumeFailed;
        return result;
    }
    bool stock{}, own{}, callback_stock{}, callback_own{};
    bool known = inspect_words(stock, own, callback_stock, callback_own);
    Status failure = Status::kNotAttempted;
    if (!known || (install && (!stock || !callback_stock))) failure = Status::kForeignMutation;
#if defined(TLL_ENEMY_INSTALL_TESTING)
    if (failure == Status::kNotAttempted && test_fault_ == TestFault::kForeignProtectionBeforeOpen && !test_fault_fired_) {
        test_fault_fired_ = true;
        DWORD ignored{};
        (void)VirtualProtect(reinterpret_cast<void*>(pages_[0].address), kPageBytes, PAGE_READONLY, &ignored);
    }
#endif
    if (failure == Status::kNotAttempted && !open_pages()) failure = Status::kProtectionFailed;
    if (failure == Status::kNotAttempted && install) {
        for (std::size_t i = 0; i < kSiteCount; ++i) {
            const auto& plan = plans_[i];
            if (!compare_word(plan.word_address, plan.expected, plan.replacement)) { failure = Status::kCompareFailed; break; }
            published_ = true;
            ++result.changed_code_words;
#if defined(TLL_ENEMY_INSTALL_TESTING)
            if (i == 2 && test_fault_ == TestFault::kAfterThirdWord) { failure = Status::kTestInjectedFailure; break; }
            if (i == 2 && test_fault_ == TestFault::kForeignThirdWord) {
                auto foreign = plan.replacement;
                foreign[0] ^= 1;
                (void)compare_word(plan.word_address, plan.replacement, foreign);
                failure = Status::kTestInjectedFailure; break;
            }
#endif
        }
        if (failure == Status::kNotAttempted) {
            if (!compare_pointer(module_.base + native::kPostposeSlotRva,
                                 module_.base + native::kGamePostposeRva, postpose_wrapper_)) failure = Status::kCompareFailed;
            else published_ = true;
        }
#if defined(TLL_ENEMY_INSTALL_TESTING)
        if (failure == Status::kNotAttempted && test_fault_ == TestFault::kAfterCallback)
            failure = Status::kTestInjectedFailure;
        if (failure == Status::kNotAttempted && test_fault_ == TestFault::kForeignCallback) {
            (void)compare_pointer(module_.base + native::kPostposeSlotRva, postpose_wrapper_,
                                  module_.base + native::kNdPostposeRva);
            failure = Status::kTestInjectedFailure;
        }
#endif
    }
    // All rollback/restore writes compare exact owned words. Never overwrite a
    // foreign full word or callback, even if its opcode still resembles ours.
    if (!install || (failure != Status::kNotAttempted && published_)) {
        if (open_pages()) {
            (void)compare_pointer(module_.base + native::kPostposeSlotRva, postpose_wrapper_,
                                  module_.base + native::kGamePostposeRva);
            for (std::size_t i = kSiteCount; i > 0; --i)
                (void)compare_word(plans_[i - 1].word_address, plans_[i - 1].replacement, plans_[i - 1].expected);
        } else if (failure == Status::kNotAttempted) failure = Status::kProtectionFailed;
    }
    bool flushed = true;
    for (const auto& plan : plans_) {
#if defined(TLL_ENEMY_INSTALL_TESTING)
        if (test_fault_ == TestFault::kFlushOnce && !test_fault_fired_) {
            test_fault_fired_ = true; flushed = false; continue;
        }
#endif
        if (!FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(plan.word_address), 16)) flushed = false;
    }
    if (!flushed && failure == Status::kNotAttempted) failure = Status::kFlushFailed;
    result.protections_restored = restore_pages();
    if (!result.protections_restored && failure == Status::kNotAttempted) failure = Status::kProtectionRestoreFailed;
    // If final protection/cache verification fails after publication, make one
    // bounded rollback attempt, then require a fully known state before resuming.
    if (install && failure != Status::kNotAttempted && (!flushed || !result.protections_restored)) {
        if (open_pages()) {
            (void)compare_pointer(module_.base + native::kPostposeSlotRva, postpose_wrapper_, module_.base + native::kGamePostposeRva);
            for (const auto& plan : plans_) (void)compare_word(plan.word_address, plan.replacement, plan.expected);
        }
        flushed = true;
        for (const auto& plan : plans_)
            if (!FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(plan.word_address), 16)) flushed = false;
        result.protections_restored = restore_pages();
    }
    known = inspect_words(stock, own, callback_stock, callback_own);
    result.code_is_stock = stock;
    result.callback_is_stock = callback_stock;
    result.published = published_;
    const bool safe = known && flushed && result.protections_restored;
    if (!known) failure = Status::kForeignMutation;
    const bool untouched_refusal = install && !published_ && !recovering_stop && result.protections_restored;
    if (safe || untouched_refusal) result.threads_resumed = resume_threads();
    else result.threads_resumed = thread_count_ == 0;
    if (!result.threads_resumed && safe) failure = Status::kResumeFailed;
    installed_ = install && failure == Status::kNotAttempted && own && callback_own && result.threads_resumed;
    if (installed_) result.status = Status::kInstalled;
    else if (!install && safe && stock && callback_stock && result.threads_resumed) result.status = Status::kRestored;
    else result.status = failure == Status::kNotAttempted ? Status::kCompareFailed : failure;
    return result;
}

Result Installer::restore() noexcept {
    if (busy_.test_and_set(std::memory_order_acquire)) return {Status::kBusy};
    Result result{};
    if (!prepared_ || GetCurrentThreadId() != owner_thread_) result.status = Status::kInvalidInput;
    else result = transact(false);
    busy_.clear(std::memory_order_release);
    return result;
}
} // namespace coop::enemy_jointblend_install_windows
