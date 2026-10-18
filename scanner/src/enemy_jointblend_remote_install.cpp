#include "enemy_jointblend_remote_install.h"
#include <TlHelp32.h>
#include <cstring>

namespace coop::enemy_jointblend_remote_install {
#if defined(TLL_ENEMY_REMOTE_INSTALL_AUDIT)
const Audit* g_audit{};
#endif
namespace {
constexpr Address kPageBytes = 0x1000;
BOOL remote_write(HANDLE process, LPVOID address, LPCVOID data, SIZE_T size, SIZE_T* transferred) noexcept {
#if defined(TLL_ENEMY_REMOTE_INSTALL_AUDIT)
    if (g_audit && g_audit->write) return g_audit->write(process, address, data, size, transferred);
#endif
    return WriteProcessMemory(process, address, data, size, transferred);
}
BOOL remote_protect(HANDLE process, LPVOID address, SIZE_T size, DWORD protection, PDWORD previous) noexcept {
#if defined(TLL_ENEMY_REMOTE_INSTALL_AUDIT)
    if (g_audit && g_audit->protect) return g_audit->protect(process, address, size, protection, previous);
#endif
    return VirtualProtectEx(process, address, size, protection, previous);
}
BOOL remote_flush(HANDLE process, LPCVOID address, SIZE_T size) noexcept {
#if defined(TLL_ENEMY_REMOTE_INSTALL_AUDIT)
    if (g_audit && g_audit->flush) return g_audit->flush(process, address, size);
#endif
    return FlushInstructionCache(process, address, size);
}
bool readable(DWORD p) noexcept {
    if (p & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    const auto b = p & 0xFF;
    return b == PAGE_READONLY || b == PAGE_READWRITE || b == PAGE_WRITECOPY ||
        b == PAGE_EXECUTE_READ || b == PAGE_EXECUTE_READWRITE || b == PAGE_EXECUTE_WRITECOPY;
}
bool executable(DWORD p) noexcept {
    return readable(p) && ((p & 0xFF) == PAGE_EXECUTE_READ ||
        (p & 0xFF) == PAGE_EXECUTE_READWRITE || (p & 0xFF) == PAGE_EXECUTE_WRITECOPY);
}
std::array<std::uint8_t, 14> relay(Address target) noexcept {
    std::array<std::uint8_t, 14> bytes{0xFF, 0x25};
    std::memcpy(bytes.data() + 6, &target, sizeof(target));
    return bytes;
}
}
bool Installer::held() const noexcept {
    return session_ && ops_ && session_->owned_hold_for(*ops_);
}
bool Installer::read(Address address, void* out, std::size_t size) const noexcept {
    SIZE_T transferred{};
    return held() && ReadProcessMemory(ops_->borrowed_process(), reinterpret_cast<const void*>(address),
        out, size, &transferred) && transferred == size;
}
bool Installer::write(Address address, const void* data, std::size_t size) noexcept {
    if (!held()) return false;
    ever_wrote_ = true; // A failed call may have changed bytes.
    SIZE_T transferred{};
    return remote_write(ops_->borrowed_process(), reinterpret_cast<void*>(address),
        data, size, &transferred) && transferred == size;
}
bool Installer::region(Address address, std::size_t size, bool code, Address allocation) const noexcept {
    if (!held() || !address || !size || size > native::kAddressMax - address) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQueryEx(ops_->borrowed_process(), reinterpret_cast<void*>(address), &m, sizeof(m)) != sizeof(m)) return false;
    const auto begin = reinterpret_cast<Address>(m.BaseAddress);
    return m.State == MEM_COMMIT && readable(m.Protect) && (!code || executable(m.Protect)) &&
        address >= begin && address - begin <= m.RegionSize && size <= m.RegionSize - (address - begin) &&
        (!allocation || reinterpret_cast<Address>(m.AllocationBase) == allocation);
}
bool Installer::dependencies() const noexcept {
    if (!region(prepared_.postpose, 1, true)) return false;
    for (std::size_t i = 0; i < kSites; ++i) {
        const auto address = prepared_.relays[i];
        MEMORY_BASIC_INFORMATION m{};
        std::array<std::uint8_t, 14> observed{};
        if (!region(prepared_.hooks[i], 1, true) || !region(address, observed.size(), true) ||
            VirtualQueryEx(ops_->borrowed_process(), reinterpret_cast<void*>(address), &m, sizeof(m)) != sizeof(m) ||
            m.Protect != PAGE_EXECUTE_READ || !read(address, observed.data(), observed.size()) ||
            observed != relay(prepared_.hooks[i])) return false;
    }
    return true; // Byte/RX consistency only; never a module-retention certificate.
}
Error Installer::prepare() noexcept {
    const auto module = prepared_.module;
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    if (!native::valid_module(module) || info.dwPageSize != kPageBytes ||
        !native::contains_rva(module, native::kPostposeSlotRva, 8) || !dependencies()) return Error::invalid_plan;
    for (std::size_t i = 0; i < kSites; ++i) {
        const auto& site = native::kSites[i];
        if (!native::contains_rva(module, site.rva, site.length)) return Error::invalid_plan;
        if (!native::contains_rva(module, site.target_rva, 1) ||
            !region(module.base + site.target_rva, 1, true, module.base)) return Error::memory;
        const auto at = (module.base + site.rva) & ~Address{15};
        native::Word16 stock{};
        if (!region(at, stock.size(), true, module.base) || !read(at, stock.data(), stock.size())) return Error::memory;
        plans_[i] = native::plan_rel32_site(module, site.id, at, stock, prepared_.relays[i]);
        if (!plans_[i].accepted()) return Error::foreign_bytes;
    }
    Address callback{};
    const auto slot = module.base + native::kPostposeSlotRva;
    if (!region(slot, 8, false, module.base) || !read(slot, &callback, sizeof(callback)) ||
        !native::validate_postpose_slot(module, slot, callback) ||
        !region(callback, 1, true, module.base)) return Error::foreign_bytes;
    for (std::size_t i = 0; i <= kSites; ++i) {
        const auto page = (i == kSites ? slot : plans_[i].word_address) & ~(kPageBytes - 1);
        bool found = false;
        for (std::size_t j = 0; j < page_count_; ++j) found |= pages_[j].address == page;
        if (found) continue;
        MEMORY_BASIC_INFORMATION m{};
        if (VirtualQueryEx(ops_->borrowed_process(), reinterpret_cast<void*>(page), &m, sizeof(m)) != sizeof(m) ||
            m.State != MEM_COMMIT || !readable(m.Protect) || reinterpret_cast<Address>(m.AllocationBase) != module.base)
            return Error::memory;
        pages_[page_count_++] = {page, m.Protect, false};
    }
    // No relay/hook may alias any changed page or another relay's byte span.
    for (std::size_t i = 0; i < kSites; ++i) {
        const auto address = prepared_.relays[i];
        if (!address || address > native::kAddressMax - 14 || !prepared_.hooks[i]) return Error::invalid_plan;
        for (std::size_t j = 0; j < page_count_; ++j) {
            const auto page = pages_[j].address;
            if ((address < page + kPageBytes && page < address + 14) ||
                (prepared_.hooks[i] >= page && prepared_.hooks[i] - page < kPageBytes) ||
                (prepared_.postpose >= page && prepared_.postpose - page < kPageBytes)) return Error::invalid_plan;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (address < prepared_.relays[j] + 14 && prepared_.relays[j] < address + 14) return Error::invalid_plan;
        // Do not admit a relay cycle disguised as a prepared wrapper.
        if (prepared_.postpose >= address && prepared_.postpose - address < 14) return Error::invalid_plan;
        for (const auto hook : prepared_.hooks)
            if (hook >= address && hook - address < 14) return Error::invalid_plan;
    }
    ready_ = true;
    return Error::none;
}
Error Installer::check_rips() const noexcept {
    if (!held()) return Error::foreign_hold;
    const DWORD pid = GetProcessId(ops_->borrowed_process());
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return Error::threads;
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    bool more = Thread32First(snapshot, &entry) != FALSE;
    Error result = more ? Error::none : Error::threads;
    std::size_t count{};
    while (more && result == Error::none) {
        if (entry.th32OwnerProcessID == pid) {
            if (++count > 256) { result = Error::threads; break; }
            HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
            if (!thread) { result = Error::threads; break; }
            CONTEXT context{}; context.ContextFlags = CONTEXT_CONTROL;
            if (GetProcessIdOfThread(thread) != pid || !GetThreadContext(thread, &context)) result = Error::threads;
            CloseHandle(thread);
            for (const auto& plan : plans_)
                if (context.Rip >= plan.word_address && context.Rip - plan.word_address < 16) result = Error::rip_in_word;
        }
        more = Thread32Next(snapshot, &entry) != FALSE;
    }
    const auto enumeration_error = GetLastError();
    CloseHandle(snapshot);
    if (result == Error::none && (!count || more || enumeration_error != ERROR_NO_MORE_FILES)) result = Error::threads;
    return held() ? result : Error::foreign_hold;
}
bool Installer::bytes(bool installed) const noexcept {
    for (const auto& plan : plans_) {
        native::Word16 observed{};
        if (!read(plan.word_address, observed.data(), observed.size()) ||
            observed != (installed ? plan.replacement : plan.expected)) return false;
    }
    Address callback{};
    return read(prepared_.module.base + native::kPostposeSlotRva, &callback, 8) &&
        callback == (installed ? prepared_.postpose : prepared_.module.base + native::kGamePostposeRva);
}
bool Installer::known_bytes() const noexcept {
    for (const auto& plan : plans_) {
        native::Word16 observed{};
        if (!read(plan.word_address, observed.data(), observed.size()) ||
            (observed != plan.expected && observed != plan.replacement)) return false;
    }
    Address callback{};
    return read(prepared_.module.base + native::kPostposeSlotRva, &callback, 8) &&
        (callback == prepared_.postpose || callback == prepared_.module.base + native::kGamePostposeRva);
}
bool Installer::open_pages() noexcept {
    for (auto& page : pages_) {
        if (!page.address) continue;
        MEMORY_BASIC_INFORMATION m{};
        if (!held() || VirtualQueryEx(ops_->borrowed_process(), reinterpret_cast<void*>(page.address), &m, sizeof(m)) != sizeof(m) ||
            m.State != MEM_COMMIT || reinterpret_cast<Address>(m.AllocationBase) != prepared_.module.base ||
            (m.Protect != page.protection && !(page.touched && m.Protect == PAGE_EXECUTE_READWRITE))) return false;
        if (page.touched && m.Protect == PAGE_EXECUTE_READWRITE) continue;
        page.touched = true; // Any protect attempt needs fresh verification on failure.
        DWORD previous{};
        if (!remote_protect(ops_->borrowed_process(), reinterpret_cast<void*>(page.address), kPageBytes,
                PAGE_EXECUTE_READWRITE, &previous) || previous != page.protection) return false;
    }
    return true;
}
bool Installer::restore_pages() noexcept {
    bool okay = true;
    for (auto& page : pages_) {
        if (!page.address) continue;
        MEMORY_BASIC_INFORMATION m{};
        if (!held() || VirtualQueryEx(ops_->borrowed_process(), reinterpret_cast<void*>(page.address), &m, sizeof(m)) != sizeof(m) ||
            m.State != MEM_COMMIT || reinterpret_cast<Address>(m.AllocationBase) != prepared_.module.base) { okay = false; continue; }
        if (m.Protect == page.protection) { page.touched = false; continue; }
        if (!page.touched || m.Protect != PAGE_EXECUTE_READWRITE) { okay = false; continue; }
        DWORD previous{};
        const bool restored = remote_protect(ops_->borrowed_process(), reinterpret_cast<void*>(page.address),
            kPageBytes, page.protection, &previous) != FALSE;
        const bool verified = VirtualQueryEx(ops_->borrowed_process(), reinterpret_cast<void*>(page.address),
            &m, sizeof(m)) == sizeof(m) && m.State == MEM_COMMIT &&
            reinterpret_cast<Address>(m.AllocationBase) == prepared_.module.base && m.Protect == page.protection;
        // Once verified original, we no longer own a writable transition. A later
        // foreign RWX change must not masquerade as one of our interrupted calls.
        if (verified) page.touched = false;
        if (!restored || !verified) okay = false;
    }
    return okay;
}
bool Installer::flush() const noexcept {
    bool okay = held();
    for (const auto& plan : plans_)
        okay = held() && remote_flush(ops_->borrowed_process(), reinterpret_cast<void*>(plan.word_address), 16) && okay;
    return okay;
}
bool Installer::rollback_owned() noexcept {
    if (!held() || !known_bytes() || !open_pages()) return false;
    const Address slot = prepared_.module.base + native::kPostposeSlotRva;
    const Address stock = prepared_.module.base + native::kGamePostposeRva;
    Address callback{};
    bool okay = read(slot, &callback, 8);
    if (okay && callback == prepared_.postpose) okay = write(slot, &stock, 8);
    for (const auto& plan : plans_) {
        native::Word16 observed{};
        if (!read(plan.word_address, observed.data(), observed.size())) { okay = false; continue; }
        if (observed == plan.replacement) okay = write(plan.word_address, plan.expected.data(), 16) && okay;
        else if (observed != plan.expected) okay = false; // Partial/foreign byte mixtures stay held.
    }
    return okay;
}
Result Installer::transact(bool installing) noexcept {
    Result result{};
    result.wrote = ever_wrote_;
    if (ever_wrote_) result.status = Status::unsafe_held;
    const auto rip_error = check_rips();
    if (rip_error != Error::none) { result.error = rip_error; return result; }
    if (!dependencies()) { result.error = Error::dependencies; return result; }
    if (!(installing ? bytes(false) : known_bytes())) { result.error = Error::foreign_bytes; return result; }
    bool okay = open_pages();
    if (!okay) result.error = Error::protection;
    if (okay && installing) {
        for (const auto& plan : plans_) {
            native::Word16 current{};
            if (!read(plan.word_address, current.data(), 16) || current != plan.expected) {
                okay = false; result.error = Error::foreign_bytes; break;
            }
            if (!write(plan.word_address, plan.replacement.data(), 16)) {
                okay = false; result.error = Error::write; break;
            }
        }
        if (okay) {
            const Address slot = prepared_.module.base + native::kPostposeSlotRva;
            Address current{};
            okay = read(slot, &current, 8) && current == prepared_.module.base + native::kGamePostposeRva &&
                write(slot, &prepared_.postpose, 8);
            if (!okay) result.error = Error::write;
        }
    } else if (okay) { okay = rollback_owned(); if (!okay) result.error = Error::write; }
    if (!flush()) { okay = false; result.error = Error::flush; }
    result.protections_restored = restore_pages();
    if (!result.protections_restored) { okay = false; result.error = Error::protection; }
    if (!bytes(installing)) { okay = false; if (result.error == Error::none) result.error = Error::foreign_bytes; }
    if (!dependencies()) { okay = false; result.error = Error::dependencies; }
    if (!okay && ever_wrote_ && installing) {
        const bool rolled_back = rollback_owned();
        const bool flushed = flush();
        result.protections_restored = restore_pages();
        result.memory_state_verified = rolled_back && flushed && result.protections_restored && bytes(false) && dependencies();
        result.status = result.memory_state_verified ? Status::rolled_back : Status::unsafe_held;
    } else {
        result.memory_state_verified = okay || (!ever_wrote_ && result.protections_restored && bytes(false) && dependencies());
        result.status = okay ? (installing ? Status::installed : Status::restored) :
            (result.memory_state_verified ? Status::refused : Status::unsafe_held);
    }
    result.wrote = ever_wrote_;
    return result;
}
Result Installer::install(const Prepared& prepared, debug::Session& session, debug::WindowsOps& ops) noexcept {
    if (attempted_) return {Status::refused, Error::already_attempted, ever_wrote_};
    attempted_ = true; session_ = &session; ops_ = &ops; prepared_ = prepared;
    if (!session.barrier_ready_for(ops)) return {Status::refused, Error::foreign_hold};
    const auto error = prepare();
    if (error != Error::none) return {Status::refused, error};
    return transact(true);
}
Result Installer::restore(debug::Session& session, debug::WindowsOps& ops) noexcept {
    if (!ready_ || session_ != &session || ops_ != &ops || !held()) return {Status::refused, Error::foreign_hold, ever_wrote_};
    return transact(false);
}
} // namespace coop::enemy_jointblend_remote_install
