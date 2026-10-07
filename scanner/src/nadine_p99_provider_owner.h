#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace coop::p99_provider {
inline constexpr std::size_t kMax=32,kPath=1024,kPage=4096,kHeader=128,kRecord=64;
inline constexpr std::uint64_t kMagic=0x3130564F52503950ULL; // P9PROV01
using Hash=std::array<std::uint8_t,32>;
using Path=std::array<wchar_t,kPath>;
using Wire=std::array<std::uint8_t,kPage>;
struct Session {
    std::uint64_t pid=0,creation=0;
    std::array<std::uint64_t,2> nonce{};
    std::uint64_t artifact=0,artifact_bytes=0;
    bool operator==(const Session&) const=default;
};
struct Selection {
    Path path{},native_path{};
    std::uint64_t base=0,bytes=0,anchor_rva=0;
    Hash expected_file_hash{},prefix{};
};
struct Record {
    std::uint64_t base=0,bytes=0,anchor_rva=0;
    Hash expected_file_hash{}; // Caller profile identifier, NOT a local hash certificate.
    bool operator==(const Record&) const=default;
};
struct Claims {
    Session session{};
    std::size_t count=0;
    std::array<Record,kMax> records{}; // index0 MUST retain publication artifact itself.
    bool operator==(const Claims&) const=default;
};
struct Acquire {bool okay=false;std::uintptr_t module=0;};
inline bool range(std::uint64_t base,std::uint64_t size) noexcept {
    constexpr std::uint64_t end=0x800000000000ULL;
    return base && base<end && size && size<=end-base;
}
inline bool image(std::uint64_t base,std::uint64_t size) noexcept {
    return range(base,size) && !(base&0xFFFF) && !(size&4095) && size<=64*1024*1024;
}
inline bool nonzero(const Hash& h) noexcept {return std::any_of(h.begin(),h.end(),[](auto v){return v!=0;});}
inline std::wstring_view path_view(const Path& p) noexcept {
    const auto end=std::find(p.begin(),p.end(),L'\0');
    if(end==p.begin() || end==p.end() || std::any_of(end,p.end(),[](auto c){return c!=0;}))return {};
    return {p.data(),static_cast<std::size_t>(end-p.begin())};
}
inline bool path_shape(const Path& p,bool native) noexcept {
    const auto v=path_view(p);
    if(v.empty() || std::any_of(v.begin(),v.end(),[](auto c){return c<32 || c==L'/';}))return false;
    if(native) {if(!v.starts_with(L"\\Device\\"))return false;}
    else if(v.size()<4 || !((v[0]>=L'A' && v[0]<=L'Z') || (v[0]>=L'a' && v[0]<=L'z')) ||
            v[1]!=L':' || v[2]!=L'\\')return false;
    const auto start=native?1u:3u;
    for(std::size_t at=start;at<=v.size();) {
        const auto end=v.find(L'\\',at);
        const auto part=v.substr(at,end==v.npos?v.size()-at:end-at);
        if(part.empty() || part==L"." || part==L".." || part.find(L':')!=part.npos)return false;
        if(end==v.npos)break;at=end+1;
    }
    return true;
}
inline bool valid(const Claims& c) noexcept {
    const auto& s=c.session;
    if(!s.pid || s.pid>0xFFFFFFFF || !s.creation || !(s.nonce[0]|s.nonce[1]) ||
       !image(s.artifact,s.artifact_bytes) || !c.count || c.count>kMax)return false;
    for(std::size_t i=0;i<c.count;++i) {
        const auto& r=c.records[i];
        if(!image(r.base,r.bytes) || !r.anchor_rva || r.anchor_rva>=r.bytes ||
           32>r.bytes-r.anchor_rva || !nonzero(r.expected_file_hash))return false;
        if(!i && (r.base!=s.artifact || r.bytes!=s.artifact_bytes))return false;
        for(std::size_t j=0;j<i;++j) {
            const auto& p=c.records[j];
            if(r.base<p.base+p.bytes && p.base<r.base+r.bytes)return false;
        }
    }
    for(std::size_t i=c.count;i<kMax;++i)if(c.records[i]!=Record{})return false;
    return true;
}
inline void put(Wire& w,std::size_t at,std::uint64_t value) noexcept {
    for(unsigned b=0;b<8;++b)w[at+b]=static_cast<std::uint8_t>(value>>(8*b));
}
inline std::uint64_t get(std::span<const std::uint8_t> w,std::size_t at) noexcept {
    std::uint64_t value=0;for(unsigned b=0;b<8;++b)value|=std::uint64_t{w[at+b]}<<(8*b);return value;
}
inline std::optional<Wire> encode(const Claims& c) noexcept {
    if(!valid(c))return {};Wire w{};const auto& s=c.session;
    const std::array<std::uint64_t,12> words{kMagic,1,kPage,kRecord,c.count,s.pid,s.creation,
        s.nonce[0],s.nonce[1],s.artifact,s.artifact_bytes,1}; // contract1, remaining header zero
    for(std::size_t i=0;i<words.size();++i)put(w,i*8,words[i]);
    for(std::size_t i=0;i<c.count;++i) {
        const auto at=kHeader+i*kRecord;const auto& r=c.records[i];
        put(w,at,i+1);put(w,at+8,r.base);put(w,at+16,r.bytes);put(w,at+24,r.anchor_rva);
        std::copy(r.expected_file_hash.begin(),r.expected_file_hash.end(),w.begin()+at+32);
    }
    return w;
}
inline std::optional<Claims> decode(std::span<const std::uint8_t> w) noexcept {
    if(w.size()!=kPage || get(w,0)!=kMagic || get(w,8)!=1 || get(w,16)!=kPage ||
       get(w,24)!=kRecord || get(w,88)!=1 || !get(w,32) || get(w,32)>kMax)return {};
    Claims c{};c.count=static_cast<std::size_t>(get(w,32));
    c.session={get(w,40),get(w,48),{get(w,56),get(w,64)},get(w,72),get(w,80)};
    for(std::size_t i=0;i<c.count;++i) {
        const auto at=kHeader+i*kRecord;if(get(w,at)!=i+1)return {};
        auto& r=c.records[i];r.base=get(w,at+8);r.bytes=get(w,at+16);r.anchor_rva=get(w,at+24);
        std::copy_n(w.begin()+at+32,32,r.expected_file_hash.begin());
    }
    const auto canonical=encode(c);
    if(!canonical || !std::equal(w.begin(),w.end(),canonical->begin()))return {}; // all padding/reserved zero
    return c;
}

enum class State {Fresh,Refused,AcquireUnknown,PinUnknown,Retained,PublishUnknown,Published};
// OWNER THREAD ONLY, off loader lock/callbacks/debugger hold. Caller retains the
// executing artifact before entry. Index0 must be that artifact and is PINned
// along with every selected provider BEFORE the first publication store.
// No destructor/reset/unpin/FreeLibrary. Successful refs AND ambiguous outcomes
// intentionally survive even failure/worker exit; no published data is recalled.
// This publishes CLAIMS, never byte-integrity, complete dependency or install admission.
class Owner {
    State state_=State::Fresh;
    std::uint32_t thread_=0;
    const void* ops_=nullptr;
    Claims claims_{};
    std::array<Selection,kMax> selected_{};
    std::array<std::uintptr_t,kMax> refs_{};
    template<class Ops> bool same(Ops& ops) const noexcept {
        return ops_==std::addressof(ops) && thread_==ops.thread();
    }
public:
    Owner()=default;
    Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
    State state() const noexcept {return state_;}
    template<class Ops> bool prepare(Session session,std::span<const Selection> selected,Ops& ops) noexcept {
        static_assert(noexcept(ops.thread()) && noexcept(ops.identity(session)) &&
            noexcept(ops.acquire(selected[0])) && noexcept(ops.verify(selected[0],std::uintptr_t{})) &&
            noexcept(ops.pin(selected[0],std::uintptr_t{})));
        if(state_!=State::Fresh)return false;state_=State::Refused;
        if(selected.empty() || selected.size()>kMax)return false;
        claims_.session=session;claims_.count=selected.size();
        std::copy(selected.begin(),selected.end(),selected_.begin()); // OWN before any OS action
        for(std::size_t i=0;i<claims_.count;++i) {
            const auto& s=selected_[i];
            if(!path_shape(s.path,false) || !path_shape(s.native_path,true))return false;
            for(std::size_t j=0;j<i;++j)if(s.path==selected_[j].path || s.native_path==selected_[j].native_path)return false;
            claims_.records[i]={s.base,s.bytes,s.anchor_rva,s.expected_file_hash};
        }
        if(!valid(claims_))return false;
        ops_=std::addressof(ops);thread_=ops.thread();
        if(!thread_ || !ops.identity(session))return false;
        for(std::size_t i=0;i<claims_.count;++i) {
            state_=State::AcquireUnknown;const auto got=ops.acquire(selected_[i]);
            refs_[i]=got.module; // preserve output EVEN on FALSE-after-effect
            if(!got.okay || got.module!=selected_[i].base)return false;
        }
        state_=State::Refused;
        for(std::size_t i=0;i<claims_.count;++i)
            if(!same(ops) || !ops.identity(session) || !ops.verify(selected_[i],refs_[i]))return false;
        for(std::size_t i=0;i<claims_.count;++i) {
            if(!same(ops) || !ops.identity(session))return false;
            state_=State::PinUnknown;
            if(!ops.pin(selected_[i],refs_[i]))return false;
        }
        state_=State::Refused;
        if(!same(ops) || !ops.identity(session))return false;
        state_=State::Retained;return true;
    }
    template<class Ops> bool publish(Ops& ops) noexcept {
        static_assert(noexcept(ops.publish(claims_.session,Wire{})));
        if(state_!=State::Retained || !same(ops))return false;
        state_=State::Refused;
        if(!ops.identity(claims_.session))return false;
        for(std::size_t i=0;i<claims_.count;++i)if(!ops.verify(selected_[i],refs_[i]))return false;
        const auto wire=encode(claims_);if(!wire)return false;
        // A failed publisher can already have exposed valid RO claims. Never retry.
        state_=State::PublishUnknown;if(!ops.publish(claims_.session,*wire))return false;
        state_=State::Published;return true;
    }
};
} // namespace coop::p99_provider
