#include "enemy_jointblend_prepare_windows.h"
#include <cstring>

namespace coop::enemy_jointblend_prepare_windows {
#if defined(TLL_ENEMY_PREPARE_AUDIT)
const Audit* g_audit{};
#endif
namespace {
struct PreserveError { DWORD value = GetLastError(); ~PreserveError() { SetLastError(value); } };
bool page_contract(Address at, Address allocation, DWORD type, DWORD protection) noexcept {
    if (!at || (at & 4095) || at > native::kAddressMax-kPageBytes) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<void*>(at), &m, sizeof(m)) != sizeof(m) || m.State != MEM_COMMIT ||
        m.Type != type || m.Protect != protection || reinterpret_cast<Address>(m.AllocationBase) != allocation) return false;
    const auto base = reinterpret_cast<Address>(m.BaseAddress);
    return base <= at && m.RegionSize <= native::kAddressMax-base && base+m.RegionSize >= at+kPageBytes;
}
bool image_code(Address at, native::ModuleImage image) noexcept {
    if (at < image.base || !native::contains_rva(image, at-image.base, 1)) return false;
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(reinterpret_cast<void*>(at), &m, sizeof(m)) == sizeof(m) && m.State == MEM_COMMIT &&
        m.Type == MEM_IMAGE && m.Protect == PAGE_EXECUTE_READ && reinterpret_cast<Address>(m.AllocationBase) == image.base;
}
bool pin_code(Address at, native::ModuleImage image) noexcept {
    if (!image_code(at, image)) return false;
    HMODULE module{};
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(at), &module) && reinterpret_cast<Address>(module) == image.base;
}
BOOL protect(LPVOID at, SIZE_T size, DWORD value, PDWORD old) noexcept {
#if defined(TLL_ENEMY_PREPARE_AUDIT)
    if (g_audit && g_audit->protect) return g_audit->protect(at, size, value, old);
#endif
    return VirtualProtect(at, size, value, old);
}
BOOL flush(LPCVOID at, SIZE_T size) noexcept {
#if defined(TLL_ENEMY_PREPARE_AUDIT)
    if (g_audit && g_audit->flush) return g_audit->flush(GetCurrentProcess(), at, size);
#endif
    return FlushInstructionCache(GetCurrentProcess(), at, size);
}
int memory_fault(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR || code == EXCEPTION_GUARD_PAGE ?
        EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
// POD-only leaves: no C++ ownership/unwinding inside __try. Only memory faults
// are contained. Preflight protection is a snapshot, not a lease.
bool read_leaf(Address source, void* destination, std::size_t size) noexcept {
    __try {
        auto* out = static_cast<std::uint8_t*>(destination);
        const auto* in = reinterpret_cast<const volatile std::uint8_t*>(source);
        for (std::size_t i = 0; i < size; ++i) out[i] = in[i];
        return true;
    } __except(memory_fault(GetExceptionCode())) { return false; }
}
bool write_leaf(Address destination, const void* source, std::size_t size) noexcept {
    __try {
        auto* out = reinterpret_cast<volatile std::uint8_t*>(destination);
        const auto* in = static_cast<const std::uint8_t*>(source);
        for (std::size_t i = 0; i < size; ++i) out[i] = in[i];
        return true;
    } __except(memory_fault(GetExceptionCode())) { return false; }
}
bool read_bytes(Address source, void* out, std::size_t size) noexcept {
    if (!read_leaf(source, out, size)) return false;
#if defined(TLL_ENEMY_PREPARE_AUDIT)
    if (g_audit && g_audit->after_read) g_audit->after_read(source, size);
#endif
    return true;
}
bool write_bytes(Address target, const void* in, std::size_t size) noexcept {
#if defined(TLL_ENEMY_PREPARE_AUDIT)
    if (g_audit && g_audit->before_write) g_audit->before_write(target, size);
    if (g_audit && g_audit->write) return g_audit->write(target, in, size);
#endif
    return write_leaf(target, in, size);
}
DWORD writable_image_page(Address at, Address self) noexcept {
    if (page_contract(at, self, MEM_IMAGE, PAGE_READWRITE)) return PAGE_READWRITE;
    if (page_contract(at, self, MEM_IMAGE, PAGE_WRITECOPY)) return PAGE_WRITECOPY;
    return 0;
}
RelayBytes leaf(Address target) noexcept {
    RelayBytes out{0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(out.data()+6, &target, sizeof(target));
    return out;
}
bool relay_matches(Address at, Address target) noexcept {
    if (!page_contract(at, at, MEM_PRIVATE, PAGE_EXECUTE_READ)) return false;
    std::array<std::uint8_t, kPageBytes> actual{}, expected{};
    const auto code = leaf(target);
    std::memcpy(expected.data(), code.data(), code.size());
    return read_bytes(at, actual.data(), actual.size()) && actual == expected &&
        page_contract(at, at, MEM_PRIVATE, PAGE_EXECUTE_READ);
}
struct Allocation { Address actual{}, requested{}; };
Allocation allocate_near(Address next) noexcept {
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    if (info.dwPageSize != kPageBytes || info.dwAllocationGranularity != 0x10000) return {};
    const Address anchor = next & ~Address{0xFFFF};
    const Address minimum = reinterpret_cast<Address>(info.lpMinimumApplicationAddress);
    const Address maximum = reinterpret_cast<Address>(info.lpMaximumApplicationAddress);
    // Bounded, allocation-granularity search. VirtualAlloc claims free ranges;
    // no enumeration-based ownership and no game/image protection changes.
    for (Address step = 0; step <= 0x7FFF; ++step) for (unsigned side = 0; side < 2; ++side) {
        if (!step && side) continue;
        const auto distance = step*0x10000;
        Address candidate{};
        if (!side) { if (distance > native::kAddressMax-anchor) continue; candidate = anchor+distance; }
        else { if (distance >= anchor) continue; candidate = anchor-distance; }
        if (candidate < minimum || candidate > maximum || kPageBytes > maximum-candidate ||
            !native::encode_rel32(next, candidate).valid) continue;
        LPVOID memory{};
#if defined(TLL_ENEMY_PREPARE_AUDIT)
        if (g_audit && g_audit->allocate) memory = g_audit->allocate(reinterpret_cast<void*>(candidate),
            kPageBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        else
#endif
            memory = VirtualAlloc(reinterpret_cast<void*>(candidate), kPageBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        const auto allocated = reinterpret_cast<Address>(memory);
        // Record ANY successful allocation in the caller before checking it.
        // An unexpected address is retained, never freed or published.
        if (allocated) return {allocated, candidate};
    }
    return {};
}
bool valid_binding(const prep::Binding& b, Address self) noexcept {
    const native::ModuleImage main{b.main, b.main_size}, dll{b.self, b.self_size};
    if (b.magic != prep::kMagic || b.size != sizeof(b) || b.version != prep::kVersion || b.reserved ||
        !b.pid || !b.creation || b.frequency < 1000 || b.self != self || main.base == dll.base ||
        main.size != prep::kRetailImageSize || !native::valid_module(main) || !native::valid_module(dll)) return false;
    for (std::size_t i = 0; i < b.originals.size(); ++i) if (b.originals[i] != main.base+prep::kFallbacks[i].rva) return false;
    for (const auto hook : b.hooks) if (hook < dll.base || !native::contains_rva(dll, hook-dll.base, 1)) return false;
    return b.hooks[0] == b.hooks[1] && b.postpose >= dll.base && native::contains_rva(dll, b.postpose-dll.base, 1);
}
}

bool Relays::prepare(native::ModuleImage main, native::ModuleImage self, const std::array<Address, 7>& hooks) noexcept {
    PreserveError error;
    if (attempted_.exchange(true, std::memory_order_acq_rel)) return false;
    if (!native::valid_module(main) || !native::valid_module(self) || main.base == self.base || hooks[0] != hooks[1]) return false;
    for (std::size_t i = 0; i < hooks.size(); ++i)
        if (!native::contains_rva(main, native::kSites[i].instruction_end_rva, 1) || !image_code(hooks[i], self)) return false;
    if (!pin_code(hooks[0], self)) return false;
    main_ = main; self_ = self; hooks_ = hooks;
    for (std::size_t i = 0; i < addresses_.size(); ++i) {
        const auto next = main.base+native::kSites[i].instruction_end_rva;
        const auto allocation = allocate_near(next);
        addresses_[i] = allocation.actual; // Retain even if any later operation fails.
        const auto address = addresses_[i];
        if (address != allocation.requested || (address & 0xFFFF) || !native::encode_rel32(next, address).valid ||
            !page_contract(address, address, MEM_PRIVATE, PAGE_READWRITE)) return false;
        for (std::size_t previous = 0; previous < i; ++previous) if (addresses_[previous] == address) return false;
        const auto code = leaf(hooks[i]);
        if (!write_bytes(address, code.data(), code.size()) || !page_contract(address, address, MEM_PRIVATE, PAGE_READWRITE)) return false;
        DWORD previous{};
        if (!protect(reinterpret_cast<void*>(address), kPageBytes, PAGE_EXECUTE_READ, &previous) || previous != PAGE_READWRITE ||
            !flush(reinterpret_cast<void*>(address), code.size()) || !relay_matches(address, hooks[i])) return false;
    }
    ready_ = true;
    if (!verify()) { ready_ = false; return false; }
    return true;
}
bool Relays::verify() const noexcept {
    PreserveError error;
    if (!ready_) return false;
    for (std::size_t i = 0; i < addresses_.size(); ++i)
        if (!image_code(hooks_[i], self_) || !native::encode_rel32(main_.base+native::kSites[i].instruction_end_rva, addresses_[i]).valid ||
            !relay_matches(addresses_[i], hooks_[i])) return false;
    return true;
}
bool published(const prep::Page* page, Address self, const prep::Binding& binding) noexcept {
    PreserveError error;
    const auto at = reinterpret_cast<Address>(page);
    if (!valid_binding(binding, self) || !page_contract(at, self, MEM_IMAGE, PAGE_READONLY)) return false;
    prep::Page expected{}, actual{}; expected.binding = binding;
    return read_bytes(at, &actual, sizeof(actual)) && std::memcmp(&actual, &expected, sizeof(actual)) == 0 &&
        page_contract(at, self, MEM_IMAGE, PAGE_READONLY);
}
PublishResult Publisher::publish(prep::Page* page, Address self, const prep::Binding& binding) noexcept {
    PreserveError error;
    using R = PublishResult;
    if (attempted_.exchange(true, std::memory_order_acq_rel)) return R::reused;
    if (!valid_binding(binding, self)) return R::invalid;
    const auto at = reinterpret_cast<Address>(page);
    if (!writable_image_page(at, self)) return R::page;
    const prep::Page zero{};
    prep::Page before{};
    if (!read_bytes(at, &before, sizeof(before)) || std::memcmp(&before, &zero, sizeof(before)) ||
        !writable_image_page(at, self)) return R::page;
    // Irreversible exposure begins here. Snapshot checks cannot prevent a
    // concurrent protection change; the POD SEH leaf contains memory faults.
    if (!write_bytes(at, &binding, sizeof(binding))) return R::write;
    MemoryBarrier();
    // COW writes can legitimately turn WRITECOPY into READWRITE. Sample AFTER
    // the write, then require VirtualProtect to report exactly that protection.
    const DWORD writable = writable_image_page(at, self);
    if (!writable) return R::seal;
    DWORD previous{};
    if (!protect(page, sizeof(*page), PAGE_READONLY, &previous) || previous != writable) return R::seal;
    return published(page, self, binding) ? R::published : R::verify;
}
}
