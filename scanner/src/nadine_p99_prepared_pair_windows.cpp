#include "nadine_p99_prepared_pair_windows.h"

namespace coop::p99_prepared_providers {
std::optional<Pair> read_pair(HANDLE process,Pair expected,std::uintptr_t offer_page,
    std::uintptr_t provider_page) noexcept {
    const auto s=session(expected.offer);
    const auto page_ok=[&](std::uintptr_t p) noexcept {
        return !(p&4095) && p>=s.artifact && p-s.artifact<s.artifact_bytes &&
            4096<=s.artifact_bytes-(p-s.artifact);
    };
    if(!wh::plan(expected.offer) || !pp::valid(expected.providers) ||
       expected.providers.session!=s || offer_page==provider_page ||
       !page_ok(offer_page) || !page_ok(provider_page))return {};
    const auto offers=[&]() noexcept {
        return p99_wrist_code::read_prepared_offer(process,s.artifact,s.artifact_bytes,offer_page);
    };
    // No observed bytes become expectations. Re-read both independently versioned
    // immutable pages; observable mismatch refuses, but matching reads are no lease.
    for(unsigned pass=0;pass<2;++pass)
        if(pp::read_claims(process,s,provider_page)!=std::optional{expected.providers} ||
           offers()!=std::optional{expected.offer})return {};
    // End with a fresh PID/creation+claims read, not just the offer's claimed PID.
    if(pp::read_claims(process,s,provider_page)!=std::optional{expected.providers})return {};
    return expected;
}
} // namespace coop::p99_prepared_providers
