#pragma once
#include "nadine_p99_wrist_code_manifest.h"
#include "nadine_p99_wrist_install.h"
#include <utility>

namespace coop::p99_wrist_code {
namespace manifest=p99_wrist_code_manifest;
constexpr std::size_t covered_bytes() noexcept {
    std::size_t bytes=0;
    for(const auto& r:manifest::kRegions) bytes+=r.byte_count;
    return bytes;
}
inline constexpr std::size_t kBytes=covered_bytes();
static_assert(kBytes==46690);
enum class Failure {None, Reused, Plan, Address, Read, Call, Hash, Digest};
struct Diagnostic {Failure reason=Failure::None; std::size_t region=0;};

// Caller-owned, immutable-after-prepare byte evidence, NOT native/source admission.
// Allocate/touch this ~94KB object BEFORE any suspension. prepare may use crypto;
// matches does not hash, allocate, log, normalize, or call prepare. Caller supplies
// module/code lifetime and a synchronization boundary. There is no retry/re-arm.
class Snapshot {
    std::array<std::uint8_t,kBytes> stock_{},installed_{};
    std::uintptr_t module_=0;
    bool started_=false,ready_=false;
    Diagnostic diagnostic_{};
public:
    Snapshot()=default;
    Snapshot(const Snapshot&)=delete;
    Snapshot& operator=(const Snapshot&)=delete;
    [[nodiscard]] bool ready() const noexcept {return ready_;}
    [[nodiscard]] Diagnostic diagnostic() const noexcept {return diagnostic_;}

    // Reader.copy performs checked copies; Hasher.digest receives owned bytes.
    // Both return bool and are noexcept; partial copies/hashes are never accepted.
    template<class Reader,class Hasher>
    bool prepare(const p99_wrist_install::Plan& plan,Reader& reader,Hasher& hasher) noexcept {
        static_assert(noexcept(reader.copy(std::uintptr_t{},std::span<std::uint8_t>{})) &&
            noexcept(hasher.digest(std::span<const std::uint8_t>{},std::declval<std::array<std::uint8_t,32>&>())));
        if(started_) {ready_=false;diagnostic_.reason=Failure::Reused;return false;}
        started_=true;
        if(!p99_wrist_install::valid(plan)) {diagnostic_.reason=Failure::Plan;return false;}
        module_=plan.module;
        std::size_t offset=0;
        for(std::size_t i=0;i<manifest::kRegions.size();++i) {
            diagnostic_.region=i;
            const auto& r=manifest::kRegions[i];
            const auto address=p99_group_ingress::add(module_,r.rva);
            if(!address || !p99_group_ingress::add(*address,r.byte_count)) {
                diagnostic_.reason=Failure::Address;return false;
            }
            auto owned=std::span(stock_).subspan(offset,r.byte_count);
            if(!reader.copy(*address,owned)) {diagnostic_.reason=Failure::Read;return false;}
            if(!manifest::canonicalize(i,owned,manifest::Stage::Stock)) {
                diagnostic_.reason=Failure::Call;return false;
            }
            std::array<std::uint8_t,32> hash{};
            if(!hasher.digest(owned,hash)) {diagnostic_.reason=Failure::Hash;return false;}
            if(hash!=r.sha256) {diagnostic_.reason=Failure::Digest;return false;}
            std::copy(owned.begin(),owned.end(),installed_.begin()+offset);
            for(std::size_t c=0;c<manifest::kCallsites.size();++c) {
                const auto& call=manifest::kCallsites[c];
                const auto& site=plan.sites[c+2];
                if(site.address-module_!=call.rva || site.size!=call.stock.size()) {
                    diagnostic_.reason=Failure::Plan;return false;
                }
                if(call.rva<r.rva || call.rva-r.rva>=r.byte_count) continue;
                std::copy_n(site.after.begin(),site.size,installed_.begin()+offset+call.rva-r.rva);
            }
            offset+=r.byte_count;
        }
        ready_=true;diagnostic_={};return true;
    }

    // Reader.equal is a fresh protected read/compare, NOT a cached VQ result.
    // Explicit stage means foreign CALLs cannot become a wildcard during install.
    template<class Reader>
    [[nodiscard]] bool matches(manifest::Stage stage,Reader& reader) const noexcept {
        static_assert(noexcept(reader.equal(std::uintptr_t{},std::span<const std::uint8_t>{})));
        if(!ready_ || (stage!=manifest::Stage::Stock && stage!=manifest::Stage::Installed)) return false;
        const auto& bytes=stage==manifest::Stage::Stock?stock_:installed_;
        std::size_t offset=0;
        for(const auto& r:manifest::kRegions) {
            if(!reader.equal(module_+r.rva,std::span(bytes).subspan(offset,r.byte_count))) return false;
            offset+=r.byte_count;
        }
        return true;
    }
};
} // namespace coop::p99_wrist_code
