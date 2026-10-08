// Separate explicit-entry artifact. The existing macro supplies inert DllMain,
// with NO source/native audit seams; old default/8-export ABI targets are unchanged.
#if !defined(TLL_P99_WRIST_ABI_ARTIFACT) || defined(TLL_P99_PREPARATION_AUDIT)
#error Build only as the inert provider-preparation target without audit seams.
#endif
#include "coop_runtime.cpp"
#include "nadine_p99_provider_preparation.h"
#include "nadine_p99_prepared_providers.h"
#include "nadine_p99_provider_windows.h"

namespace bootstrap=coop::p99_provider_preparation;
namespace provider=coop::p99_provider;
namespace bound=coop::p99_prepared_providers;
#pragma section(".p99deps",read,write)
extern "C" {
__declspec(dllexport) __declspec(allocate(".p99deps")) provider::Page TllP99ProviderPreparedV1{};
__declspec(dllexport) std::uint32_t __cdecl TllP99PrepareProvidersV1(const bootstrap::Request*,std::uint32_t) noexcept;
}
namespace {
bootstrap::Request g_provider_request;
bound::Preparation g_provider_bound;
provider::WindowsOps g_provider_ops{TllP99ProviderPreparedV1};
bootstrap::Result g_provider_finish_result=bootstrap::Result::PublicationRefused;

bool copy_provider_request(const bootstrap::Request* source,std::uint32_t bytes) noexcept {
    if(bytes!=sizeof(g_provider_request) || !source ||
       (reinterpret_cast<std::uintptr_t>(source)&7) ||
       !provider::range(reinterpret_cast<std::uintptr_t>(source),bytes))return false;
    // Fixed bounded snapshot BEFORE any OS/profile callback. SEH leaf never
    // dereferences caller memory again, and never retries a partial copy.
    __try {
        auto* out=reinterpret_cast<std::uint8_t*>(&g_provider_request);
        const auto* in=reinterpret_cast<const volatile std::uint8_t*>(source);
        for(std::size_t i=0;i<sizeof(g_provider_request);++i)out[i]=in[i];return true;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION || GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR ||
        GetExceptionCode()==EXCEPTION_GUARD_PAGE?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return false;}
}
bool valid_provider_request() noexcept {
    const auto& r=g_provider_request;
    if(r.bytes!=sizeof(r) || r.version!=bootstrap::kVersion || r.reserved || !r.count || r.count>provider::kMax ||
       !provider::range(r.main,r.main_bytes) || (r.main&0xFFFF) || (r.main_bytes&4095) || r.main_bytes>0xFFFFFFFF ||
       !provider::image(r.artifact,r.artifact_bytes) ||
       (r.main<r.artifact+r.artifact_bytes && r.artifact<r.main+r.main_bytes))return false;
    // This nonce is a syntactic validation placeholder ONLY, never published.
    // Resources generates the real single session after identity checks.
    provider::Claims records{};records.session={r.pid,r.creation,{1,0},r.artifact,r.artifact_bytes};records.count=r.count;
    for(std::size_t i=0;i<r.count;++i){const auto& s=r.selected[i];
        if(!provider::path_shape(s.path,false) || !provider::path_shape(s.native_path,true))return false;
        for(std::size_t j=0;j<i;++j)if(s.path==r.selected[j].path || s.native_path==r.selected[j].native_path)return false;
        records.records[i]={s.base,s.bytes,s.anchor_rva,s.expected_file_hash};
    }
    const provider::Selection zero{};
    for(std::size_t i=r.count;i<r.selected.size();++i)
        if(std::memcmp(&r.selected[i],&zero,sizeof(zero)))return false;
    return provider::valid(records);
}
bool provider_request_identity() noexcept {
    const auto& r=g_provider_request;HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&TllP99PrepareProvidersV1),&self) ||
       reinterpret_cast<std::uintptr_t>(self)!=r.artifact ||
       reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=r.main)return false;
    p99_handshake::Offer current{};std::uint64_t main_bytes=0,artifact_bytes=0;
    return p99_prepare::process_identity(current,false) && current.pid==r.pid && current.creation==r.creation &&
        p99_prepare::image_size(r.main,main_bytes) && main_bytes==r.main_bytes &&
        p99_prepare::image_size(r.artifact,artifact_bytes) && artifact_bytes==r.artifact_bytes;
}
struct ProviderLocalProofs:P99WristPreparationProofs {
    bool finish(const p99_handshake::Offer& offer) noexcept {
        if(!identity(offer)){g_provider_finish_result=bootstrap::Result::IdentityRefused;return false;}
        p99_code::ImageReader reader(g_module);p99_code::Sha256 hash;
        if(!g_p99_wrist_preparation_code.prepare(g_p99_wrist_preparation_resources.plan(),reader,hash)){
            g_provider_finish_result=bootstrap::Result::SnapshotRefused;return false;
        }
        if(!g_p99_group_fiber.initialize() || g_p99_group_fiber.current()!=nullptr || g_p99_group_fiber.failed()){
            g_provider_finish_result=bootstrap::Result::FlsRefused;return false;
        }
        return true;
    }
    bool publish(const p99_handshake::Wire& wire) noexcept {
        return p99_prepare::publish(TllP99WristPreparedV1,g_self_module,wire);
    }
};
}
extern "C" __declspec(dllexport) std::uint32_t __cdecl TllP99PrepareProvidersV1(
    const bootstrap::Request* request,std::uint32_t bytes) noexcept {
    using R=bootstrap::Result;
    const auto result=[](R r) noexcept {return static_cast<std::uint32_t>(r);};
    // Serialize BEFORE even reading caller memory or changing global identities.
    if(g_p99_wrist_preparation_started.exchange(true,std::memory_order_acq_rel))return result(R::Reused);
    if(g_p99_group_shadow_ready.load(std::memory_order_acquire) || g_p99_source_observation_ready.load(std::memory_order_acquire) ||
       g_p99_wrist_ready.load(std::memory_order_acquire) || g_p99_wrist_installed.load(std::memory_order_acquire) ||
       g_p99_wrist_worker_started.load(std::memory_order_acquire) || g_p99_group_fiber.initialized() ||
       !g_p99_group_install_gate.begin())return result(R::ExclusionRefused);
    if(!copy_provider_request(request,bytes) || !valid_provider_request())return result(R::InputRefused);
    if(!provider_request_identity())return result(R::IdentityRefused);
    g_module=static_cast<std::uintptr_t>(g_provider_request.main);
    g_self_module=static_cast<std::uintptr_t>(g_provider_request.artifact);
    const std::array<std::uintptr_t,5> hooks{
        reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_async),reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_async),
        reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_process0),reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_process1),
        reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_cmd16)};
    if(!g_p99_wrist_preparation_resources.prepare(g_module,g_self_module,hooks))return result(R::ResourcesRefused);
    ProviderLocalProofs local;
    if(g_provider_bound.run(g_p99_wrist_preparation_resources.offer(),
        std::span(g_provider_request.selected).first(g_provider_request.count),g_provider_ops,local))return result(R::Prepared);
    if(g_provider_bound.provider_state()!=provider::State::Retained &&
       g_provider_bound.provider_state()!=provider::State::Published &&
       g_provider_bound.provider_state()!=provider::State::PublishUnknown)return result(R::ProviderRefused);
    return result(g_provider_finish_result);
}
