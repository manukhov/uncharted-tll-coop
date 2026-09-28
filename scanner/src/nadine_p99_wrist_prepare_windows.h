#pragma once
#include "nadine_p99_wrist_handshake.h"
#include "nadine_p99_wrist_code_windows.h"

namespace coop::p99_wrist_prepare {
namespace wh=p99_wrist_handshake;
namespace wi=p99_wrist_install;
struct alignas(4096) Page {
    wh::Wire offer{};
    std::array<std::uint8_t,4096-wh::kOfferBytes> reserved{};
};
static_assert(sizeof(Page)==4096);

// All functions below are OFF-LOADER / OFF-CALLBACK preparation work.
bool image_size(std::uintptr_t module,std::uint64_t& bytes) noexcept;
bool process_identity(wh::Offer& offer,bool new_nonce) noexcept;
bool pin_image(std::uintptr_t anchor,std::uintptr_t expected_module) noexcept;
bool page_contract(std::uintptr_t address,std::uintptr_t allocation,DWORD type,DWORD protection) noexcept;
bool bytes_equal(std::uintptr_t address,std::span<const std::uint8_t> bytes) noexcept;
#if defined(TLL_P99_WRIST_PREPARE_AUDIT)
// Fault injection only in separately built owned fixtures, absent production.
struct Audit {
    BOOL (*protect)(LPVOID,SIZE_T,DWORD,PDWORD) noexcept=nullptr;
    BOOL (*flush)(HANDLE,LPCVOID,SIZE_T) noexcept=nullptr;
};
extern const Audit* g_audit;
#endif

// At most five retained allocations per one-shot owner. Intentionally NO freeing
// destructor/reset: no caller may recall a possibly published/fetched relay.
// RW allocation -> exact 14-byte leaf -> RX -> checked flush. Never RWX.
class Relays {
    std::array<std::uintptr_t,5> addresses_{};
    wi::Plan plan_{};
    bool started_=false,ready_=false;
public:
    bool prepare(std::uintptr_t module,const std::array<std::uintptr_t,5>& hooks) noexcept;
    bool verify() const noexcept;
    const auto& addresses() const noexcept {return addresses_;}
    const wi::Plan& plan() const noexcept {return plan_;}
};

// The page is visible as an export from LoadLibrary onward; the consumer MUST
// require the entire dedicated MEM_IMAGE page to be READONLY before AND after
// copying it, validate the wire, and independently bind process/build/entries.
// The page starts zero/RW. No marker alone grants admission. All route resources
// must be retained irreversibly BEFORE this function's first store, even if it
// returns false after a partial write/protection change. Never retry or erase it.
bool publish(Page& page,std::uintptr_t dll,const wh::Wire& wire) noexcept;
bool published(const Page& page,std::uintptr_t dll,const wh::Wire& wire) noexcept;

class Resources {
    wh::Offer offer_{};
    Relays relays_{};
    bool started_=false,pinned_=false,ready_=false;
public:
    // No FLS, stock hashing, patch, IPC ACK, worker or arm here. Caller supplies
    // actual compiled ABI addresses and completes those checks before publish.
    bool prepare(std::uintptr_t main,std::uintptr_t dll,const std::array<std::uintptr_t,5>& hooks) noexcept;
    bool identity(const wh::Offer& candidate) const noexcept;
    bool verify(const wh::Offer& candidate,const wi::Plan& plan) const noexcept;
    const wh::Offer& offer() const noexcept {return offer_;}
    const wi::Plan& plan() const noexcept {return relays_.plan();}
};
} // namespace coop::p99_wrist_prepare

extern "C" {
#if defined(TLL_P99_WRIST_PREPARE_DEFINE_PAGE)
__declspec(dllexport)
#endif
extern coop::p99_wrist_prepare::Page TllP99WristPreparedV1;
}
