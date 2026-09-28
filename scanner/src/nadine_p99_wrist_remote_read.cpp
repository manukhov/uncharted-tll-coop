#include "nadine_p99_wrist_remote_read.h"

namespace coop::p99_wrist_code {
#if defined(TLL_P99_WRIST_REMOTE_READ_AUDIT)
const RemoteReadAudit* g_remote_read_audit = nullptr;
#endif
namespace {
SIZE_T query(HANDLE process, std::uintptr_t address, MEMORY_BASIC_INFORMATION& result) noexcept {
#if defined(TLL_P99_WRIST_REMOTE_READ_AUDIT)
    if (g_remote_read_audit && g_remote_read_audit->query)
        return g_remote_read_audit->query(process, reinterpret_cast<LPCVOID>(address), &result, sizeof(result));
#endif
    return VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &result, sizeof(result));
}
bool local_span(const void* pointer, std::size_t size) noexcept {
    return pointer && size && p99_group_ingress::add(reinterpret_cast<std::uintptr_t>(pointer), size).has_value();
}
void after_metadata(std::uintptr_t address, std::size_t size) noexcept {
#if defined(TLL_P99_WRIST_REMOTE_READ_AUDIT)
    if (g_remote_read_audit && g_remote_read_audit->after_metadata)
        g_remote_read_audit->after_metadata(address, size);
#else
    (void)address;
    (void)size;
#endif
}
}

RemoteImageReader::RemoteImageReader(HANDLE borrowed, std::uintptr_t module,
    std::span<const std::uintptr_t> writable_pages) noexcept : process_(borrowed), module_(module) {
    if (!borrowed || !module || writable_pages.size() > writable_.size()) return;
    for (const auto page : writable_pages) {
        bool planned = false;
        for (const auto& site : p99_wrist_install::kSites) {
            const auto address = p99_group_ingress::add(module, site.rva);
            if (address && (*address & ~std::uintptr_t{0xFFF}) == page) planned = true;
        }
        if (!planned) return;
        for (std::size_t i = 0; i < count_; ++i) if (writable_[i] == page) return;
        writable_[count_++] = page;
    }
    valid_ = true;
}

bool RemoteImageReader::accessible(std::uintptr_t address, std::size_t size) const noexcept {
    if (!valid_ || address < module_ || !size || size > kMaxBytes ||
        !p99_group_ingress::add(address, size)) return false;
    const auto end = address + size;
    auto cursor = address;
    for (unsigned steps = 0; cursor < end && steps < 64; ++steps) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (query(process_, cursor, mbi) != sizeof(mbi) || mbi.State != MEM_COMMIT ||
            mbi.Type != MEM_IMAGE || reinterpret_cast<std::uintptr_t>(mbi.AllocationBase) != module_) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto last = p99_group_ingress::add(base, mbi.RegionSize);
        if (base > cursor || !last || *last <= cursor) return false;
        const auto next = std::min(*last, end);
        // Exact values reject guard/nocache/writecombine modifiers before RPM.
        if (mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_EXECUTE_WRITECOPY) {
            for (auto page = cursor & ~std::uintptr_t{0xFFF}; page < next;) {
                bool admitted = false;
                for (std::size_t i = 0; i < count_; ++i) if (writable_[i] == page) admitted = true;
                if (!admitted) return false;
                const auto page_end = p99_group_ingress::add(page, p99_wrist_install::kPageBytes);
                if (!page_end) return false;
                page = *page_end;
            }
        } else if (mbi.Protect != PAGE_EXECUTE_READ) return false;
        cursor = next;
    }
    return cursor == end;
}

bool RemoteImageReader::read_exact(std::uintptr_t address, std::uint8_t* out, std::size_t size) const noexcept {
    SIZE_T actual = 0;
    BOOL okay = FALSE;
#if defined(TLL_P99_WRIST_REMOTE_READ_AUDIT)
    if (g_remote_read_audit && g_remote_read_audit->read)
        okay = g_remote_read_audit->read(process_, reinterpret_cast<LPCVOID>(address), out, size, &actual);
    else
#endif
        okay = ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(address), out, size, &actual);
    return okay != FALSE && actual == size;
}

bool RemoteImageReader::copy(std::uintptr_t address, std::span<std::uint8_t> owned) const noexcept {
    if (!local_span(owned.data(), owned.size()) || !accessible(address, owned.size())) return false;
    after_metadata(address, owned.size());
    if (!read_exact(address, owned.data(), owned.size())) return false;
    // Reject an observable allocation/protection change after the query/read.
    // Two metadata checks still do not confer a lease or prevent an ABA race.
    return accessible(address, owned.size());
}

bool RemoteImageReader::equal(std::uintptr_t address, std::span<const std::uint8_t> owned) const noexcept {
    if (!local_span(owned.data(), owned.size()) || !accessible(address, owned.size())) return false;
    after_metadata(address, owned.size());
    std::array<std::uint8_t, kScratchBytes> scratch{};
    for (std::size_t offset = 0; offset < owned.size();) {
        const auto count = std::min(scratch.size(), owned.size() - offset);
        if (!read_exact(address + offset, scratch.data(), count)) return false;
        for (std::size_t i = 0; i < count; ++i) if (scratch[i] != owned[offset + i]) return false;
        offset += count;
    }
    return accessible(address, owned.size());
}
} // namespace coop::p99_wrist_code
