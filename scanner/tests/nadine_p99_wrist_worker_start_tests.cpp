// Actual startup/worker wrapper, but every OS boundary uses owned fake handles.
// No OS thread/event/file/process, game code, installer or live signal is used.
#include <Windows.h>
#include "nadine_p99_source_observation.h"
#include "nadine_p99_wrist_report.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace start_fixture {
unsigned checks=0,failures=0;
const char* current_case="setup";
void check(bool value,const char* message) {
    ++checks;
    if(!value){++failures;std::cerr<<"FAIL "<<current_case<<": "<<message<<'\n';}
}
enum class Fault { None, Pid, Creation, ZeroCreation, DirectoryMissing, DirectoryFile,
    OutputExists, OutputDenied, OutputParentGone, Module, Event, ExistingEvent,
    Thread, PathException, InlineWorkerException };
struct ThreadExit { DWORD code; };
struct State {
    Fault fault=Fault::None;
    DWORD error=ERROR_SUCCESS;
    unsigned pid_calls=0,creation_calls=0,attributes=0,module_calls=0,event_calls=0,thread_calls=0;
    unsigned event_closes=0,thread_closes=0,module_frees=0,exit_calls=0,logs=0,unwanted=0;
    unsigned modules=0,events=0,threads=0;
    bool log_throws=false,existing_event_signalled=true;
    std::wstring event_name,directory,output;
    LPTHREAD_START_ROUTINE entry=nullptr;
    void* argument=nullptr;
    DWORD exited=MAXDWORD;
    unsigned os_calls()const {
        return pid_calls+creation_calls+attributes+module_calls+event_calls+thread_calls+
            event_closes+thread_closes+module_frees+exit_calls+unwanted;
    }
} state;
constexpr std::uintptr_t kProcess=0x7100,kModule=0x7200,kEvent=0x7300,kThread=0x7400;
HANDLE handle(std::uintptr_t value){return reinterpret_cast<HANDLE>(value);}
DWORD WINAPI pid() {++state.pid_calls;return state.fault==Fault::Pid?0:4242;}
HANDLE WINAPI process(){return handle(kProcess);}
BOOL WINAPI process_times(HANDLE target,LPFILETIME creation,LPFILETIME exited,LPFILETIME kernel,LPFILETIME user) {
    ++state.creation_calls;check(target==handle(kProcess),"creation query uses current synthetic process");
    *creation={};*exited={};*kernel={};*user={};
    if(state.fault==Fault::Creation)return FALSE;
    creation->dwLowDateTime=state.fault==Fault::ZeroCreation?0:123456;
    return TRUE;
}
DWORD WINAPI attributes(LPCWSTR path) {
    ++state.attributes;
    if(state.fault==Fault::PathException)throw std::runtime_error("owned path-query fault");
    if(state.attributes==1) {
        state.directory=path;
        if(state.fault==Fault::DirectoryMissing){state.error=ERROR_PATH_NOT_FOUND;return INVALID_FILE_ATTRIBUTES;}
        if(state.fault==Fault::DirectoryFile)return FILE_ATTRIBUTE_NORMAL;
        return FILE_ATTRIBUTE_DIRECTORY;
    }
    state.output=path;
    if(state.fault==Fault::OutputExists)return FILE_ATTRIBUTE_NORMAL;
    state.error=state.fault==Fault::OutputDenied?ERROR_ACCESS_DENIED:
        state.fault==Fault::OutputParentGone?ERROR_PATH_NOT_FOUND:ERROR_FILE_NOT_FOUND;
    return INVALID_FILE_ATTRIBUTES;
}
BOOL WINAPI module(DWORD flags,LPCWSTR address,HMODULE* out) {
    ++state.module_calls;check(flags==GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS&&address!=nullptr,
        "DLL hold increments reference by worker function address, never unchanged-refcount");
    if(state.fault==Fault::Module)return FALSE;
    ++state.modules;*out=reinterpret_cast<HMODULE>(kModule);return TRUE;
}
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES security,BOOL manual,BOOL initial,LPCWSTR name) {
    ++state.event_calls;state.event_name=name;
    check(security==nullptr&&!manual&&!initial,"new arm event is auto-reset and UNSIGNALLED");
    if(state.fault==Fault::Event){state.error=ERROR_ACCESS_DENIED;return nullptr;}
    ++state.events;state.error=state.fault==Fault::ExistingEvent?ERROR_ALREADY_EXISTS:ERROR_SUCCESS;
    return handle(kEvent);
}
BOOL WINAPI close(HANDLE value) {
    if(value==handle(kEvent)){check(state.events==1,"close owned event exactly once");--state.events;++state.event_closes;}
    else if(value==handle(kThread)){check(state.threads==1,"close creator thread handle exactly once");--state.threads;++state.thread_closes;}
    else check(false,"unexpected close target");
    return TRUE;
}
BOOL WINAPI free_module(HMODULE value) {
    check(value==reinterpret_cast<HMODULE>(kModule)&&state.modules==1,"release held DLL reference exactly once");
    --state.modules;++state.module_frees;return TRUE;
}
[[noreturn]] void WINAPI exit_thread(HMODULE value,DWORD code) {
    ++state.exit_calls;state.exited=code;(void)free_module(value);throw ThreadExit{code};
}
HANDLE WINAPI thread(LPSECURITY_ATTRIBUTES security,SIZE_T stack,LPTHREAD_START_ROUTINE entry,
    LPVOID argument,DWORD flags,LPDWORD identifier) {
    ++state.thread_calls;
    check(security==nullptr&&stack==0&&entry!=nullptr&&argument!=nullptr&&flags==0&&identifier==nullptr,
        "create one ordinary worker with fully initialized argument");
    if(state.fault==Fault::Thread)return nullptr;
    state.entry=entry;state.argument=argument;
    if(state.fault==Fault::InlineWorkerException) {
        state.log_throws=true;
        // A real worker may finish before CreateThread returns. Emulate that
        // ordering, not actual scheduling or Windows thread-exit semantics.
        try{(void)entry(argument);check(false,"worker exit does not return normally");}
        catch(const ThreadExit& end){check(end.code==4,"early worker exception reported as failure");}
    }
    ++state.threads;return handle(kThread);
}
void WINAPI last_error(DWORD value){state.error=value;}
DWORD WINAPI get_error(){return state.error;}
DWORD WINAPI wait(HANDLE,DWORD){++state.unwanted;return WAIT_FAILED;}
void WINAPI sleep(DWORD){++state.unwanted;}
ULONGLONG WINAPI ticks(){++state.unwanted;return 1000;}
HANDLE WINAPI file(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE){++state.unwanted;return INVALID_HANDLE_VALUE;}
BOOL WINAPI write(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED){++state.unwanted;return FALSE;}
} // namespace start_fixture

// creation_stamp is the real inline helper; only its Win32 query is replaced.
#define GetProcessTimes start_fixture::process_times
#include "nadine_p99_observation_arm.h"
#undef GetProcessTimes
namespace p99_observe=coop::p99_source_observation;
coop::p99_source_observation::Policy g_p99_wrist_policy;
coop::p99_wrist_report::Mailbox g_p99_wrist_report;
std::atomic<bool> g_p99_wrist_ready{false};
std::uint64_t g_p99_wrist_cost_limit=0;
// This translation unit alone exercises the otherwise dead start branch.
// The production hard-FALSE feature flag is neither included nor modified.
bool kEnableP99WristObservation=false;
bool p99_wrist_poll_expiry(){++start_fixture::state.unwanted;return false;}
void boot_log_line(const char*) {
    ++start_fixture::state.logs;
    if(start_fixture::state.log_throws)throw std::runtime_error("owned logger fault");
}
#define GetCurrentProcessId start_fixture::pid
#define GetCurrentProcess start_fixture::process
#define GetFileAttributesW start_fixture::attributes
#define GetModuleHandleExW start_fixture::module
#define CreateEventW start_fixture::event
#define CreateThread start_fixture::thread
#define CloseHandle start_fixture::close
#define FreeLibrary start_fixture::free_module
#define FreeLibraryAndExitThread start_fixture::exit_thread
#define SetLastError start_fixture::last_error
#define GetLastError start_fixture::get_error
#define WaitForSingleObject start_fixture::wait
#define Sleep start_fixture::sleep
#define GetTickCount64 start_fixture::ticks
#define CreateFileW start_fixture::file
#define WriteFile start_fixture::write
#include "../src/nadine_p99_wrist_worker_runtime.inl"
#undef WriteFile
#undef CreateFileW
#undef GetTickCount64
#undef Sleep
#undef WaitForSingleObject
#undef GetLastError
#undef SetLastError
#undef FreeLibraryAndExitThread
#undef FreeLibrary
#undef CloseHandle
#undef CreateThread
#undef CreateEventW
#undef GetModuleHandleExW
#undef GetFileAttributesW
#undef GetCurrentProcess
#undef GetCurrentProcessId

namespace start_fixture {
void reset(Fault fault=Fault::None) {
    state={};state.fault=fault;kEnableP99WristObservation=true;
    g_p99_wrist_installed.store(true);g_p99_wrist_worker_started.store(false);
    g_p99_wrist_worker_setup={};g_p99_wrist_cost_limit=0;g_p99_wrist_ready.store(false);
}
void no_implicit_arm() {
    check(!g_p99_wrist_ready.load()&&g_p99_wrist_policy.reason()==p99_observe::Reason::kNone&&
        g_p99_wrist_policy.probes()==0&&g_p99_wrist_policy.attempts()==0,"startup did not arm/consume probe or make route ready");
    check(g_p99_wrist_policy.begin_probe_detailed(1000)==p99_observe::ProbeDetailed::kStopped,
        "policy remains unarmed until separate worker event wait");
    coop::p99_wrist_report::Record result{};
    check(!g_p99_wrist_report.read(result),"startup never invents observation report");
    check(state.unwanted==0,"startup does not wait/sleep/evaluate/export");
}
void cleared() {
    const auto& s=g_p99_wrist_worker_setup;
    check(s.arm_event==nullptr&&s.module_reference==nullptr&&s.creation==0&&s.frequency==0&&s.pid==0,
        "all failed startup setup identities and handles cleared");
    bool zero=true;for(auto ch:s.output_path)zero=zero&&ch==0;
    check(zero&&g_p99_wrist_cost_limit==0,"failed startup has no stale output path or cost limit");
    check(state.modules==0&&state.events==0&&state.threads==0,"all pre-thread resources balanced");
}
void startup_fault_matrix() {
    struct Case{const char* name;Fault fault;std::uint64_t frequency;const wchar_t* directory;};
    const std::array cases{
        Case{"zero frequency",Fault::None,0,L"C:\\owned-reports"},
        Case{"low frequency",Fault::None,999,L"C:\\owned-reports"},
        Case{"null directory",Fault::None,1000000,nullptr},
        Case{"zero PID",Fault::Pid,1000000,L"C:\\owned-reports"},
        Case{"creation failure",Fault::Creation,1000000,L"C:\\owned-reports"},
        Case{"zero creation",Fault::ZeroCreation,1000000,L"C:\\owned-reports"},
        Case{"empty directory",Fault::None,1000000,L""},
        Case{"relative directory",Fault::None,1000000,L"reports"},
        Case{"drive-relative directory",Fault::None,1000000,L"C:reports"},
        Case{"root-relative directory",Fault::None,1000000,L"\\reports"},
        Case{"UNC missing share",Fault::None,1000000,L"\\\\server\\"},
        Case{"device namespace",Fault::None,1000000,L"\\\\.\\pipe\\reports"},
        Case{"directory absent",Fault::DirectoryMissing,1000000,L"C:\\owned-reports"},
        Case{"directory is file",Fault::DirectoryFile,1000000,L"C:\\owned-reports"},
        Case{"output already exists",Fault::OutputExists,1000000,L"C:\\owned-reports"},
        Case{"output permission denied",Fault::OutputDenied,1000000,L"C:\\owned-reports"},
        Case{"output directory disappeared",Fault::OutputParentGone,1000000,L"C:\\owned-reports"},
        Case{"path exception",Fault::PathException,1000000,L"C:\\owned-reports"},
        Case{"module hold failed",Fault::Module,1000000,L"C:\\owned-reports"},
        Case{"event create failed",Fault::Event,1000000,L"C:\\owned-reports"},
        Case{"stale event already exists",Fault::ExistingEvent,1000000,L"C:\\owned-reports"},
        Case{"thread create failed",Fault::Thread,1000000,L"C:\\owned-reports"},
    };
    for(const auto& c:cases) {
        current_case=c.name;reset(c.fault);
        check(!start_p99_wrist_worker(c.frequency,c.directory),"startup fails closed");
        check(g_p99_wrist_worker_started.load(),"owned startup refusal consumes one-shot latch");
        cleared();no_implicit_arm();
        check(state.logs==0,"startup performs no logging on caller thread");
        if(c.fault==Fault::Event||c.fault==Fault::ExistingEvent||c.fault==Fault::Thread)
            check(state.module_calls==1&&state.module_frees==1,"post-module refusal releases exactly one DLL hold");
        if(c.fault==Fault::ExistingEvent||c.fault==Fault::Thread)
            check(state.event_closes==1,"post-event refusal closes exactly its own event handle");
        if(c.fault==Fault::ExistingEvent)
            check(state.existing_event_signalled&&state.thread_calls==0,"existing event neither consumed nor used to start worker");
        const auto calls=state.os_calls();state.fault=Fault::None;
        check(!start_p99_wrist_worker(1000000,L"C:\\owned-reports")&&state.os_calls()==calls,
            "repeated startup after failure neither retries nor touches resources");
    }
    current_case="oversize output path";reset();
    const auto long_path=L"C:\\"+std::wstring(1020,L'x');
    check(!start_p99_wrist_worker(1000000,long_path.c_str()),"oversize path rejected without secure-CRT abort");
    check(state.attributes==1&&state.module_calls==0,"length checked before output/module/event actions");
    cleared();no_implicit_arm();
}
void feature_gates() {
    current_case="disabled source feature";reset();kEnableP99WristObservation=false;
    check(!start_p99_wrist_worker(1000000,L"C:\\owned-reports")&&!g_p99_wrist_worker_started.load()&&state.os_calls()==0,
        "disabled feature leaves startup untouched");
    current_case="not installed";reset();g_p99_wrist_installed.store(false);
    check(!start_p99_wrist_worker(1000000,L"C:\\owned-reports")&&!g_p99_wrist_worker_started.load()&&state.os_calls()==0,
        "uninstalled route cannot consume startup latch or acquire handles");
}
void normal_start_and_early_worker_exit() {
    for(const auto directory:{L"C:\\owned-reports",L"C:\\owned-reports\\",L"C:/owned-reports/",L"\\\\server\\share\\reports"}) {
        current_case="start creates worker but does not arm";reset();
        check(start_p99_wrist_worker(1000000,directory),"valid startup creates exactly one worker");
        const auto& setup=g_p99_wrist_worker_setup;
        std::wstring expected=directory;
        if(expected.back()!=L'\\'&&expected.back()!=L'/')expected+=L'\\';
        expected+=L"p99-wrist-4242-123456.json";
        check(setup.output_path==expected&&state.output==expected,"explicit directory and exact PID/creation report path");
        check(state.event_name==L"Local\\TLL-P99-Wrist-Arm-v1-4242-123456","separate exact wrist arm protocol");
        check(setup.pid==4242&&setup.creation==123456&&setup.frequency==1000000&&g_p99_wrist_cost_limit==1000,
            "worker gets initialized immutable identity and budget");
        check(state.thread_calls==1&&state.thread_closes==1&&state.threads==0&&state.modules==1&&state.events==1&&
            setup.module_reference==reinterpret_cast<HMODULE>(kModule)&&setup.arm_event==handle(kEvent),
            "success closes creator thread handle and transfers event/DLL hold");
        check(state.entry==&await_p99_wrist_arm&&state.argument==&g_p99_wrist_worker_setup,"actual wrapper and stable setup passed to thread");
        no_implicit_arm();check(state.logs==0,"no ARM-WAIT log on startup caller");
        const auto calls=state.os_calls();
        check(!start_p99_wrist_worker(1000000,directory)&&state.os_calls()==calls&&state.modules==1&&state.events==1,
            "repeat success cannot close resources owned by existing worker");
        // Exercise the actual worker+exit wrapper using a faulting logger. This
        // must contain a second logger failure, close event and release DLL once.
        state.log_throws=true;
        try{(void)state.entry(state.argument);check(false,"worker wrapper must exit");}
        catch(const ThreadExit& end){check(end.code==4,"logger failure exits worker with explicit failure");}
        check(state.logs==2&&state.exit_calls==1&&state.module_frees==1&&state.event_closes==1&&
            state.modules==0&&state.events==0&&g_p99_wrist_worker_setup.module_reference==nullptr&&
            g_p99_wrist_worker_setup.arm_event==nullptr,"double logger failure cannot leak DLL/event or stale pointer");
        no_implicit_arm();
    }
    current_case="worker retires before CreateThread returns";reset(Fault::InlineWorkerException);
    check(start_p99_wrist_worker(1000000,L"C:\\owned-reports"),"thread creation remains successful after early worker exit");
    check(g_p99_wrist_worker_started.load()&&state.modules==0&&state.events==0&&state.threads==0&&
        state.module_frees==1&&state.event_closes==1&&state.thread_closes==1&&state.exit_calls==1,
        "creator never double-frees resources already released by early worker");
    check(g_p99_wrist_worker_setup.module_reference==nullptr&&g_p99_wrist_worker_setup.arm_event==nullptr,
        "creator does not overwrite early worker's cleared ownership fields");
    no_implicit_arm();
}
} // namespace start_fixture

int main() {
    start_fixture::feature_gates();
    start_fixture::startup_fault_matrix();
    start_fixture::normal_start_and_early_worker_exit();
    std::cout<<start_fixture::checks<<" checks, "<<start_fixture::failures
        <<" failures; actual startup with synthetic Win32 handles only\n";
    return start_fixture::failures?1:0;
}
