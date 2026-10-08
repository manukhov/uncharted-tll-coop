#pragma once
#include "nadine_p99_provider_owner.h"
#include "nadine_p99_wrist_handshake.h"

namespace coop::p99_prepared_providers {
namespace pp=p99_provider;
namespace wh=p99_wrist_handshake;
inline pp::Session session(const wh::Offer& o) noexcept {
    return {o.pid,o.creation,o.nonce,o.dll,o.dll_bytes};
}
inline std::optional<pp::Claims> expected_claims(const wh::Offer& o,
    std::span<const pp::Selection> selected) noexcept {
    if(!wh::plan(o) || selected.empty() || selected.size()>pp::kMax)return {};
    pp::Claims c{};c.session=session(o);c.count=selected.size();
    for(std::size_t i=0;i<c.count;++i){const auto& s=selected[i];
        c.records[i]={s.base,s.bytes,s.anchor_rva,s.expected_file_hash};
    }
    if(!pp::valid(c))return {};return c;
}
enum class State {Fresh,Refused,ProviderUnknown,OfferUnknown,Prepared};
// Isolated integration boundary, not a game installer. Caller first prepares
// Resources (which generates the ONLY nonce); all its routes remain retained.
// OFF-LOADER, one owner thread, stable Ops/pages, no retry or destructor cleanup.
// Local.finish prepares Snapshot/FLS; remaining proof methods are Gate's normal
// identity/resources/sites/code checks. All must refuse without retail evidence.
// Provider publication is not PREPARED. Neither page is an install capability.
class Preparation {
    pp::Owner providers_;
    wh::Gate gate_;
    pp::Claims expected_{};
    State state_=State::Fresh;
public:
    Preparation()=default;
    Preparation(const Preparation&)=delete;Preparation& operator=(const Preparation&)=delete;
    State state() const noexcept {return state_;}
    pp::State provider_state() const noexcept {return providers_.state();}
    wh::State gate_state() const noexcept {return gate_.state();}
    template<class ProviderOps,class Local>
    bool run(const wh::Offer offer,std::span<const pp::Selection> selected,ProviderOps& ops,Local& local) noexcept {
        static_assert(noexcept(local.finish(offer)) && noexcept(ops.read(session(offer))) &&
            noexcept(local.publish(wh::Wire{})));
        if(state_!=State::Fresh)return false;state_=State::Refused;
        const auto claims=expected_claims(offer,selected);if(!claims)return false;
        expected_=*claims; // Owned inventory BEFORE provider/Local callbacks.
        if(!providers_.prepare(expected_.session,selected,ops) || !local.finish(offer))return false;
        state_=State::ProviderUnknown;
        if(!providers_.publish(ops))return false; // FALSE-after-seal still MUST stop.
        state_=State::Refused;
        if(ops.read(expected_.session)!=std::optional{expected_} || !gate_.prepare(offer,local))return false;
        const auto wire=gate_.publication();
        // Recheck the exact inventory after local proof callbacks, not only nonce.
        if(!wire || *wire!=wh::encode(offer) || ops.read(expected_.session)!=std::optional{expected_}){
            (void)gate_.expire();return false;
        }
        state_=State::OfferUnknown;
        if(!local.publish(*wire)){(void)gate_.expire();return false;}
        state_=State::Prepared;return true;
    }
};
} // namespace coop::p99_prepared_providers
