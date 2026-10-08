#pragma once
#include "nadine_p99_prepared_providers.h"
#include "nadine_p99_provider_windows.h"
#include "nadine_p99_wrist_remote_read.h"

namespace coop::p99_prepared_providers {
struct Pair {
    wh::Offer offer{};
    pp::Claims providers{};
    bool operator==(const Pair&) const=default;
};
// Caller independently binds expected values and resolves BOTH separate pages.
// Borrowed HANDLE only; copies are NOT atomic, PIN/whole-byte integrity evidence,
// complete dependency coverage, a lifetime lease, ACK, or install admission.
std::optional<Pair> read_pair(HANDLE borrowed,Pair expected,std::uintptr_t offer_page,
    std::uintptr_t provider_page) noexcept;
} // namespace coop::p99_prepared_providers
