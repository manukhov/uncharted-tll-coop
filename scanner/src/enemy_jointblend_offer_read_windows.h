#pragma once
#include "enemy_jointblend_offer.h"
#include "nadine_p99_debug_windows.h"

namespace coop::enemy_jointblend_offer_read {
namespace wire = enemy_jointblend_offer;
namespace debug = p99_debug_lifecycle;
inline constexpr bool kRuntimeAdmission = false;
enum class Error { none, expectation, process, self_image, offer_page, offer_bytes,
    binding_page, code_page, relay, changed, hold };
struct Result { Error error = Error::expectation; std::optional<wire::Offer> candidate{}; };
// Borrowed handle, immutable caller-owned expectation. Expectation must come
// from a separate preparation/build record, not echoing the page being checked.
// That independent production record is NOT implemented: PrepareV1 currently
// exposes its internally generated nonce/relays only through this offer page.
// Verifies process identity, self PE extent, exact two pages, declared ABI code
// page ownership and full RX relays. NO retail-main/code/dependency integrity,
// configured FLS/sink proof, PIN inspection, flush evidence or release authority.
// Source reads are snapshots, not atomicity/retention. No OpenProcess, writes,
// allocation, game calls, attach/continue/detach, logs or installer conversion.
Result read_candidate(HANDLE borrowed, const wire::Offer& expected) noexcept;
// Additional actual debugger-owner/received-event checks before AND after.
// Still only a candidate, never a certificate authorizing install or release.
Result read_held(debug::Session&, debug::WindowsOps&, const wire::Offer& expected) noexcept;
#if defined(TLL_ENEMY_OFFER_READ_AUDIT)
struct Audit {
    SIZE_T (*query)(HANDLE, LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) noexcept{};
    BOOL (*read)(HANDLE, LPCVOID, LPVOID, SIZE_T, SIZE_T*) noexcept{};
};
extern const Audit* g_audit;
#endif
}
