#pragma once
#include "nadine_p99_wrist_code_snapshot.h"
#include <Windows.h>
#include <bcrypt.h>

namespace coop::p99_wrist_code {
#if defined(TLL_P99_WRIST_CODE_AUDIT)
// Private fixture seam, absent from the production object. Invoked only after
// the real metadata check, to model a protection race before the SEH leaf read.
extern void (*g_before_code_read)(std::uintptr_t,std::size_t) noexcept;
#endif
// Off-thread, BEFORE suspension only. Opening/closing/digest may enter provider
// code and allocate. No handle or provider call is part of Snapshot::matches.
class Sha256 {
    BCRYPT_ALG_HANDLE algorithm_=nullptr;
public:
    Sha256() noexcept;
    ~Sha256();
    Sha256(const Sha256&)=delete;
    Sha256& operator=(const Sha256&)=delete;
    bool digest(std::span<const std::uint8_t> owned,std::array<std::uint8_t,32>& result) noexcept;
};

// Same-process, fixed image allocation only. MEM_IMAGE is not proof of unchanged
// code (copy-on-write pages still report MEM_IMAGE); Snapshot hashes/compares it.
// Writable pages are ONLY an explicit subset of the five planned patch pages.
// This reader does not pin a module or make a VirtualQuery snapshot into a lease.
class ImageReader {
    std::uintptr_t module_=0;
    std::array<std::uintptr_t,p99_wrist_install::kSiteCount> writable_{};
    std::size_t count_=0;
    bool valid_=false;
    bool accessible(std::uintptr_t address,std::size_t size) const noexcept;
public:
    explicit ImageReader(std::uintptr_t module,std::span<const std::uintptr_t> writable={}) noexcept;
    bool copy(std::uintptr_t address,std::span<std::uint8_t> owned) const noexcept;
    bool equal(std::uintptr_t address,std::span<const std::uint8_t> owned) const noexcept;
};
} // namespace coop::p99_wrist_code
