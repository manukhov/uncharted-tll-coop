#pragma once
#include "nadine_p99_debug_lifecycle.h"
#include <Windows.h>
#include <string_view>

namespace coop::p99_debug_lifecycle {
#if defined(TLL_P99_DEBUG_LIFECYCLE_AUDIT)
struct Audit {
    BOOL (*proceed)(DWORD,DWORD,DWORD) noexcept=nullptr;
    BOOL (*detach)(DWORD) noexcept=nullptr;
};
extern const Audit* g_audit;
#endif

// No OpenProcess, PID discovery, memory write, thread-context mutation, or game
// input. Target handle and image string are borrowed and must outlive Session.
// Only call from the dedicated owning debugger thread. No destructor cleanup.
class WindowsOps {
    HANDLE process_=nullptr;
    DWORD pid_=0,owner_=0;
    std::uint64_t creation_=0;
    std::uintptr_t main_=0;
    std::wstring_view image_;
    DEBUG_EVENT event_{};
    bool matches(HANDLE process) const noexcept;
public:
    WindowsOps(HANDLE process,DWORD pid,std::uint64_t creation,std::uintptr_t main,
        std::wstring_view image) noexcept;
    WindowsOps(const WindowsOps&)=delete;
    WindowsOps& operator=(const WindowsOps&)=delete;
    DWORD thread() const noexcept {return GetCurrentThreadId();}
    bool identity() const noexcept;
    bool no_debugger() const noexcept;
    bool attach() const noexcept;
    bool disable_kill() const noexcept;
    bool receive() noexcept;
    bool event_identity() const noexcept;
    bool proceed() const noexcept;
    bool detach() const noexcept;
    const DEBUG_EVENT& event() const noexcept {return event_;}
};
} // namespace coop::p99_debug_lifecycle
