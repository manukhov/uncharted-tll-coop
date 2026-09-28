#include "nadine_p99_debug_windows.h"
#include <array>

namespace coop::p99_debug_lifecycle {
#if defined(TLL_P99_DEBUG_LIFECYCLE_AUDIT)
const Audit* g_audit=nullptr;
#endif
WindowsOps::WindowsOps(HANDLE process,DWORD pid,std::uint64_t creation,std::uintptr_t main,
    std::wstring_view image) noexcept:process_(process),pid_(pid),owner_(GetCurrentThreadId()),
    creation_(creation),main_(main),image_(image){}
bool WindowsOps::matches(HANDLE process) const noexcept {
    if(!process || !pid_ || pid_==GetCurrentProcessId() || !creation_ || !main_ || image_.empty() ||
        image_.size()>=32768 || image_.find(L'\0')!=std::wstring_view::npos || GetProcessId(process)!=pid_ ||
        WaitForSingleObject(process,0)!=WAIT_TIMEOUT)return false;
    FILETIME start{},exit{},kernel{},user{};
    if(!GetProcessTimes(process,&start,&exit,&kernel,&user) ||
        ((std::uint64_t{start.dwHighDateTime}<<32)|start.dwLowDateTime)!=creation_)return false;
    std::array<wchar_t,32768> path{};DWORD count=static_cast<DWORD>(path.size());
    return QueryFullProcessImageNameW(process,0,path.data(),&count) && count==image_.size() &&
        CompareStringOrdinal(path.data(),static_cast<int>(count),image_.data(),static_cast<int>(image_.size()),TRUE)==CSTR_EQUAL;
}
bool WindowsOps::identity() const noexcept {return thread()==owner_ && matches(process_);}
bool WindowsOps::no_debugger() const noexcept {
    BOOL debugged=TRUE;
    return thread()==owner_ && CheckRemoteDebuggerPresent(process_,&debugged) && !debugged;
}
bool WindowsOps::attach() const noexcept {return thread()==owner_ && DebugActiveProcess(pid_)!=FALSE;}
bool WindowsOps::disable_kill() const noexcept {return thread()==owner_ && DebugSetProcessKillOnExit(FALSE)!=FALSE;}
bool WindowsOps::receive() noexcept {
    if(thread()!=owner_)return false;
    event_={};
    if(!WaitForDebugEvent(&event_,4000))return false;
    // Only file handles are debugger-owned here. Process/thread debug-event
    // handles follow the OS debug connection lifecycle, not our borrowed handle.
    if(event_.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT && event_.u.CreateProcessInfo.hFile) {
        const auto file=event_.u.CreateProcessInfo.hFile;event_.u.CreateProcessInfo.hFile=nullptr;
        if(!CloseHandle(file))return false;
    }
    if(event_.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && event_.u.LoadDll.hFile) {
        const auto file=event_.u.LoadDll.hFile;event_.u.LoadDll.hFile=nullptr;
        if(!CloseHandle(file))return false;
    }
    return true;
}
bool WindowsOps::event_identity() const noexcept {
    return thread()==owner_ && event_.dwProcessId==pid_ && event_.dwThreadId &&
        event_.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT &&
        event_.u.CreateProcessInfo.lpStartAddress==nullptr &&
        reinterpret_cast<std::uintptr_t>(event_.u.CreateProcessInfo.lpBaseOfImage)==main_ &&
        matches(event_.u.CreateProcessInfo.hProcess) && identity();
}
bool WindowsOps::proceed() const noexcept {
    if(!event_identity())return false; // Only the owned synthetic NONEXCEPTION event
#if defined(TLL_P99_DEBUG_LIFECYCLE_AUDIT)
    if(g_audit && g_audit->proceed)return g_audit->proceed(pid_,event_.dwThreadId,DBG_CONTINUE)!=FALSE;
#endif
    return ContinueDebugEvent(pid_,event_.dwThreadId,DBG_CONTINUE)!=FALSE;
}
bool WindowsOps::detach() const noexcept {
    if(!identity())return false;
#if defined(TLL_P99_DEBUG_LIFECYCLE_AUDIT)
    if(g_audit && g_audit->detach)return g_audit->detach(pid_)!=FALSE;
#endif
    return DebugActiveProcessStop(pid_)!=FALSE;
}
} // namespace coop::p99_debug_lifecycle
