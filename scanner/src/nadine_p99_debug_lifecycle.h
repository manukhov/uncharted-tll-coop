#pragma once
#include <cstdint>
#include <memory>

namespace coop::p99_debug_lifecycle {
enum class State {
    Fresh, Refused, AttachUnknown, AttachedNoEvent, KillPolicyFailed,
    EventRejected, Held, UnsafeHeld, ReleaseUnknown, ContinuedDetachFailed,
    DetachedUnverified, Detached
};

// One dedicated debugger thread, one target, one attempt. This is ONLY stop/
// release ownership, not image/ABI/RIP/quiescence or patch admission. Ops bind an
// independently selected PID/creation/image/main-base and retain its handle.
//
// Destruction performs NO continue/detach/kill. Caller MUST keep this object,
// Ops and its debugger thread alive until explicit recovery/completion. Windows
// auto-detaches on debugger thread exit with KillOnExit(FALSE): this class is NOT
// crash-atomic and a stack destructor cannot make thread exit safe after writes.
class Session {
    State state_=State::Fresh;
    std::uint32_t owner_=0;
    const void* operations_=nullptr;
public:
    Session()=default;
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    State state() const noexcept {return state_;} // owner-thread only
    bool held() const noexcept {return state_==State::Held || state_==State::UnsafeHeld;}
    bool barrier_ready() const noexcept {return state_==State::Held;} // NOT patch admission
    // UnsafeHeld still owns the same received event: explicit recovery may use
    // it, but it is not permission to start a fresh installation or release.
    template<class Ops> bool owned_hold_for(const Ops& ops) const noexcept {
        static_assert(noexcept(ops.thread()) && noexcept(ops.identity()));
        return held() && operations_==std::addressof(ops) &&
            owner_==ops.thread() && ops.identity();
    }
    template<class Ops> bool barrier_ready_for(const Ops& ops) const noexcept {
        return state_==State::Held && owned_hold_for(ops);
    }
    bool acknowledged() const noexcept {return state_==State::Detached;}

    template<class Ops> bool acquire(Ops& ops) noexcept {
        static_assert(noexcept(ops.thread()) && noexcept(ops.identity()) && noexcept(ops.no_debugger()) &&
            noexcept(ops.attach()) && noexcept(ops.disable_kill()) && noexcept(ops.receive()) && noexcept(ops.event_identity()));
        if(state_!=State::Fresh)return false;
        operations_=std::addressof(ops);owner_=ops.thread();state_=State::Refused;
        if(!owner_ || !ops.identity() || !ops.no_debugger())return false;
        // Failed OS calls are not upgraded to a no-effect/held guarantee.
        state_=State::AttachUnknown;
        if(!ops.attach())return false;
        state_=State::KillPolicyFailed;
        if(!ops.disable_kill())return false;
        state_=State::AttachedNoEvent;
        if(!ops.receive())return false;
        state_=State::EventRejected;
        if(!ops.event_identity())return false;
        state_=State::Held;return true;
    }

    // safe means caller has freshly certified installed OR restored stock bytes,
    // protections, cache flush and reachable relay/target lifetimes. Never infer
    // that boolean merely from transaction failure or five restored instructions.
    template<class Ops> bool release(Ops& ops,bool safe) noexcept {
        static_assert(noexcept(ops.thread()) && noexcept(ops.proceed()) && noexcept(ops.detach()) &&
            noexcept(ops.no_debugger()) && noexcept(ops.identity()));
        if(operations_!=std::addressof(ops) || ops.thread()!=owner_ || !held())return false;
        if(!safe){state_=State::UnsafeHeld;return false;} // absolutely NO OS release
        // Relinquish the known-held certificate BEFORE calling Continue. FALSE
        // may be ambiguous to a transport/fault wrapper; never rollback under a
        // stale held flag after any attempted release.
        state_=State::ReleaseUnknown;
        if(!ops.proceed())return false;
        state_=State::ContinuedDetachFailed;
        if(!ops.detach())return false;
        state_=State::DetachedUnverified;
        if(!ops.no_debugger() || !ops.identity())return false;
        state_=State::Detached;return true;
    }
};
} // namespace coop::p99_debug_lifecycle
