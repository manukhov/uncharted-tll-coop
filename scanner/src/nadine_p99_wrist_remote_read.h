#pragma once
#include "nadine_p99_wrist_code_snapshot.h"
#include <Windows.h>

namespace coop::p99_wrist_code {
#if defined(TLL_P99_WRIST_REMOTE_READ_AUDIT)
// Private, serial fixture seam. Absent from the production object. No callbacks
// are installed by the reader; null members retain the real Win32 operation.
struct RemoteReadAudit {
    SIZE_T (*query)(HANDLE, LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) noexcept = nullptr;
    BOOL (*read)(HANDLE, LPCVOID, LPVOID, SIZE_T, SIZE_T*) noexcept = nullptr;
    void (*after_metadata)(std::uintptr_t, std::size_t) noexcept = nullptr;
};
extern const RemoteReadAudit* g_remote_read_audit;
#endif

// READ ONLY transport for Snapshot. The caller owns the HANDLE, the module/code
// lifetime, valid local buffers and any debug-event/synchronization boundary.
// No PID/creation admission, module pin, debugger operation, native call or write
// to the remote process is performed here. MEM_IMAGE is not code integrity.
// Writable exceptions are ONLY an explicit subset of the five existing patch
// pages. No game addresses or alternate site lists are duplicated here.
class RemoteImageReader {
    HANDLE process_ = nullptr; // borrowed; GetCurrentProcess() pseudo handle is valid
    std::uintptr_t module_ = 0;
    std::array<std::uintptr_t, p99_wrist_install::kSiteCount> writable_{};
    std::size_t count_ = 0;
    bool valid_ = false;
    bool accessible(std::uintptr_t address, std::size_t size) const noexcept;
    bool read_exact(std::uintptr_t address, std::uint8_t* out, std::size_t size) const noexcept;
public:
    static constexpr std::size_t kMaxBytes = 0x10000, kScratchBytes = 512;
    explicit RemoteImageReader(HANDLE borrowed, std::uintptr_t module,
        std::span<const std::uintptr_t> writable_pages = {}) noexcept;
    // On failure the caller-owned output may contain partial bytes; never use it
    // as accepted evidence. Source reads and comparisons are not atomic snapshots.
    bool copy(std::uintptr_t address, std::span<std::uint8_t> owned) const noexcept;
    // Fixed stack scratch only; no heap/provider/hash/log work. Every RPM must
    // return TRUE AND the exact requested count. No remote span is over-read.
    bool equal(std::uintptr_t address, std::span<const std::uint8_t> owned) const noexcept;
};
} // namespace coop::p99_wrist_code
