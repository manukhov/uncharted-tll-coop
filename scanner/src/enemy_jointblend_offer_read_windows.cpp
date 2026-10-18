#include "enemy_jointblend_offer_read_windows.h"
#include <cstring>

namespace coop::enemy_jointblend_offer_read {
#if defined(TLL_ENEMY_OFFER_READ_AUDIT)
const Audit* g_audit{};
#endif
namespace {
using A = wire::Address;
struct PreserveError { DWORD value = GetLastError(); ~PreserveError() { SetLastError(value); } };
SIZE_T query(HANDLE process, A address, MEMORY_BASIC_INFORMATION& m) noexcept {
#if defined(TLL_ENEMY_OFFER_READ_AUDIT)
    if (g_audit && g_audit->query) return g_audit->query(process, reinterpret_cast<void*>(address), &m, sizeof(m));
#endif
    return VirtualQueryEx(process, reinterpret_cast<void*>(address), &m, sizeof(m));
}
bool read(HANDLE process, A address, void* out, std::size_t bytes) noexcept {
    SIZE_T count{};
    BOOL okay{};
#if defined(TLL_ENEMY_OFFER_READ_AUDIT)
    if (g_audit && g_audit->read) okay = g_audit->read(process, reinterpret_cast<void*>(address), out, bytes, &count);
    else
#endif
        okay = ReadProcessMemory(process, reinterpret_cast<void*>(address), out, bytes, &count);
    return okay && count == bytes;
}
bool region(HANDLE process, A at, std::size_t bytes, A owner, DWORD type, DWORD protection) noexcept {
    if (!wire::range(at, bytes) || bytes > 4096) return false;
    const auto end = at+bytes;
    while (at < end) {
        MEMORY_BASIC_INFORMATION m{};
        if (query(process, at, m) != sizeof(m) || m.State != MEM_COMMIT || m.Type != type ||
            m.Protect != protection || reinterpret_cast<A>(m.AllocationBase) != owner) return false;
        const auto base = reinterpret_cast<A>(m.BaseAddress);
        if (base > at || !wire::range(base, m.RegionSize) || base+m.RegionSize <= at) return false;
        at = base+m.RegionSize;
    }
    return true;
}
bool copy(HANDLE process, A at, void* out, std::size_t bytes, A owner, DWORD type, DWORD protection) noexcept {
    return region(process, at, bytes, owner, type, protection) && read(process, at, out, bytes) &&
        region(process, at, bytes, owner, type, protection);
}
bool identity(HANDLE process, const wire::Offer& o) noexcept {
    FILETIME c{}, e{}, k{}, u{};
    return GetProcessId(process) == o.pid && GetProcessTimes(process, &c, &e, &k, &u) &&
        ((std::uint64_t{c.dwHighDateTime} << 32) | c.dwLowDateTime) == o.creation;
}
bool self_image(HANDLE process, const wire::Offer& o) noexcept {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!copy(process, o.self, &dos, sizeof(dos), o.self, MEM_IMAGE, PAGE_READONLY) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos) || dos.e_lfanew > 0x100000 ||
        static_cast<A>(dos.e_lfanew)+sizeof(nt) > o.self_size ||
        !copy(process, o.self+dos.e_lfanew, &nt, sizeof(nt), o.self, MEM_IMAGE, PAGE_READONLY)) return false;
    return nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER64) &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.OptionalHeader.SectionAlignment == 4096 &&
        nt.OptionalHeader.SizeOfImage == o.self_size && nt.OptionalHeader.SizeOfHeaders <= o.self_size &&
        nt.OptionalHeader.SizeOfHeaders >= static_cast<A>(dos.e_lfanew)+sizeof(nt);
}
}
Result read_candidate(HANDLE process, const wire::Offer& expected) noexcept {
    PreserveError error;
    if (!wire::valid(expected)) return {Error::expectation, {}};
    if (!identity(process, expected)) return {Error::process, {}};
    if (!self_image(process, expected)) return {Error::self_image, {}};
    wire::Page page{};
    if (!copy(process, expected.page, &page, sizeof(page), expected.self, MEM_IMAGE, PAGE_READONLY)) return {Error::offer_page, {}};
    const auto candidate = wire::decode(page.wire);
    if (!candidate || *candidate != expected || std::any_of(page.reserved.begin(), page.reserved.end(), [](auto b) { return b != 0; }))
        return {Error::offer_bytes, {}};
    wire::prep::Page binding{}, required{}; required.binding = wire::binding(expected);
    if (!copy(process, expected.binding_page, &binding, sizeof(binding), expected.self, MEM_IMAGE, PAGE_READONLY) ||
        std::memcmp(&binding, &required, sizeof(binding))) return {Error::binding_page, {}};
    const auto code = [&](A at) { return region(process, at, 1, expected.self, MEM_IMAGE, PAGE_EXECUTE_READ); };
    if (!code(expected.entry) || !code(expected.postpose)) return {Error::code_page, {}};
    for (std::size_t i = 0; i < expected.hooks.size(); ++i) {
        if (!code(expected.hooks[i])) return {Error::code_page, {}};
        std::array<std::uint8_t, 4096> actual{}, required_leaf{0xFF, 0x25};
        std::memcpy(required_leaf.data()+6, &expected.hooks[i], sizeof(A));
        if (!copy(process, expected.relays[i], actual.data(), actual.size(), expected.relays[i], MEM_PRIVATE, PAGE_EXECUTE_READ) ||
            actual != required_leaf) return {Error::relay, {}};
    }
    wire::Page final{};
    if (!copy(process, expected.page, &final, sizeof(final), expected.self, MEM_IMAGE, PAGE_READONLY) ||
        std::memcmp(&page, &final, sizeof(final)) || !identity(process, expected)) return {Error::changed, {}};
    return {Error::none, candidate};
}
Result read_held(debug::Session& session, debug::WindowsOps& ops, const wire::Offer& expected) noexcept {
    PreserveError error;
    if (!session.barrier_ready_for(ops) || expected.main != ops.borrowed_main()) return {Error::hold, {}};
    const auto result = read_candidate(ops.borrowed_process(), expected);
    if (!session.barrier_ready_for(ops)) return {Error::hold, {}};
    return result;
}
}
