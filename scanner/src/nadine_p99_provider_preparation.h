#pragma once
#include "nadine_p99_provider_owner.h"
#include <cstddef>
#include <type_traits>

namespace coop::p99_provider_preparation {
// Explicit SAME-BUILD Windows x64 local ABI, not an IPC wire or trusted-profile
// discovery service. Caller independently validates provider hashes/prefixes,
// owns readable immutable input during the call and retains the DLL load ref.
// Call only off loader lock/callback/debugger hold. No native adapters may run.
// Prepared covers only the selected inventory and local preparation, NOT IAT,
// API-set/forwarder/dependency closure or install admission. Expected file hashes
// are caller profile identifiers, not certificates computed by this entry.
inline constexpr std::uint32_t kVersion=1;
struct Request {
    std::uint32_t bytes=sizeof(Request),version=kVersion,count=0,reserved=0;
    std::uint64_t pid=0,creation=0,main=0,main_bytes=0,artifact=0,artifact_bytes=0;
    std::array<p99_provider::Selection,p99_provider::kMax> selected{};
};
static_assert(sizeof(wchar_t)==2 && sizeof(std::uintptr_t)==8);
static_assert(sizeof(p99_provider::Selection)==4184 && offsetof(Request,selected)==64);
static_assert(sizeof(Request)==133952);
static_assert(std::is_standard_layout_v<Request> && std::is_trivially_copyable_v<Request>);
// Refusal codes describe a stage, never permission to release retained resources.
// In particular ResourcesRefused can occur after irreversible self-PIN/allocations.
enum class Result:std::uint32_t {
    Prepared=0,Reused=1,InputRefused=2,IdentityRefused=3,ExclusionRefused=4,
    ResourcesRefused=5,ProviderRefused=6,SnapshotRefused=7,FlsRefused=8,PublicationRefused=9
};
using Prepare=std::uint32_t(__cdecl*)(const Request*,std::uint32_t);
} // namespace coop::p99_provider_preparation
