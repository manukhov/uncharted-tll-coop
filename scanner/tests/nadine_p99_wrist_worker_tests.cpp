// Reuse the actual route's owned native/FLS fixture, without its case runner.
// No retail code; production data admission is tested by wrist_read_tests.
#define TLL_P99_WRIST_ROUTE_FIXTURE_ONLY 1
#include "nadine_p99_wrist_route_tests.cpp"
#include "nadine_p99_observation_arm.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <barrier>
#include <vector>

constexpr bool kEnableP99WristObservation=false;
std::string worker_case;
std::vector<std::string> worker_logs;
unsigned worker_sleeps=0;
void boot_log_line(const char* line) { worker_logs.emplace_back(line); }
DWORD worker_wait(HANDLE event,DWORD timeout) {
    check(timeout==300000); // No wall-clock five-minute wait in a fixture.
    return WaitForSingleObject(event,0);
}
void worker_sleep(DWORD duration) {
    check(duration==10); ++worker_sleeps;
    if (worker_case=="capture" || worker_case=="export-exists") run_actual_route();
    else if (worker_case=="last-refusal") {
        run_actual_route(); fake_ms+=p99_observe::kLifetimeMs;
    } else if (worker_case=="active-timeout") {
        check(g_p99_wrist_policy.begin_probe(fake_ms)==p99_observe::Probe::kGranted);
        fake_ms+=p99_observe::kLifetimeMs+5000;
    } else fake_ms+=p99_observe::kLifetimeMs;
}
#define GetTickCount64 fixture_tick_count
#define WaitForSingleObject worker_wait
#define Sleep worker_sleep
#include "../src/nadine_p99_wrist_worker_runtime.inl"
#undef Sleep
#undef WaitForSingleObject
#undef GetTickCount64

void mailbox_tests() {
    namespace r=coop::p99_wrist_report;
    r::Mailbox box; r::Record out{};
    check(!box.read(out) && !box.publish(out));
    std::atomic<unsigned> winners{0}; std::barrier start(9);
    std::array<std::thread,8> writers;
    for (unsigned n=0;n<writers.size();++n) writers[n]=std::thread([&,n] {
        r::Record value{}; value.reason=p99_observe::Reason::kSuccess;
        value.normal_return=value.captured=true; value.diagnostic_probe=n+1;
        value.payload.bytes.fill(std::byte(n)); start.arrive_and_wait();
        if (box.publish(value)) ++winners;
    });
    start.arrive_and_wait(); for(auto& t:writers)t.join();
    check(winners==1 && box.read(out));
    check(out.diagnostic_probe>=1 && out.diagnostic_probe<=8);
    for(const auto b:out.payload.bytes) check(b==std::byte(out.diagnostic_probe-1));
    const auto json=r::json(out,12,345,1000,1000000);
    check(json.find("\"payload_bytes\":256")!=std::string::npos);
    check(json.find("\"source_admitted\":false")!=std::string::npos);
    for (bool returned:{false,true}) {
        r::Mailbox refused; out.reason=p99_observe::Reason::kRefused;
        out.captured=true; out.normal_return=returned; out.payload.bytes.fill(std::byte{0xAA});
        check(refused.publish(out) && refused.read(out) && !out.captured);
        for (const auto b:out.payload.bytes) check(b==std::byte{});
        check(r::json(out,12,345,0,0).find("\"payload_hex\":\"\"")!=std::string::npos);
    }
}

int main(int argc,char** argv) {
    if(argc!=3) return 2;
    worker_case=argv[1]; const std::filesystem::path root=argv[2];
    if (!root.is_absolute()) return 2;
    const std::array<std::string_view,6> allowed{
        "capture","idle-expiry","last-refusal","active-timeout","arm-timeout","export-exists"};
    if (std::find(allowed.begin(),allowed.end(),worker_case)==allowed.end()) return 2;
    mailbox_tests();
    Fixture data{}; fixture=&data;
    if(worker_case=="last-refusal") data.action=Action::owner_refuse;
    check(make_fixture_image() && g_p99_group_fiber.initialize());
    check(!start_p99_wrist_worker(1000000) && !g_p99_wrist_worker_started.load());
    g_p99_wrist_installed.store(true); g_p99_wrist_cost_limit=1000;
    P99WristWorkerSetup setup{}; setup.pid=GetCurrentProcessId(); setup.frequency=1000000;
    check(coop::p99_observation_arm::creation_stamp(GetCurrentProcess(),setup.creation));
    std::filesystem::create_directories(root);
    const auto path=root/(worker_case+"-"+std::to_string(setup.pid)+"-"+std::to_string(setup.creation)+".json");
    check(wcscpy_s(setup.output_path,path.c_str())==0);
    if(worker_case=="export-exists") { std::ofstream file(path); file<<"sentinel"; }
    setup.arm_event=CreateEventW(nullptr,FALSE,worker_case!="arm-timeout",nullptr);
    check(setup.arm_event!=nullptr);
    const auto result=p99_wrist_worker_body(setup);
    check(setup.arm_event==nullptr && !g_p99_wrist_ready.load());
    const auto expected=worker_case=="active-timeout" ? 2u : worker_case=="export-exists" ? 3u : 0u;
    check(result==expected);
    check(worker_sleeps==(worker_case=="arm-timeout"?0u:1u));
    coop::p99_wrist_report::Record report{};
    if(worker_case=="active-timeout") {
        check(!g_p99_wrist_report.read(report));
        check(g_p99_wrist_policy.reason()==p99_observe::Reason::kNone);
        check(g_p99_wrist_policy.probes()==1 && !std::filesystem::exists(path));
        (void)g_p99_wrist_policy.abort(); // Only the explicit fake lease owner.
    } else {
        check(g_p99_wrist_report.read(report));
        std::ifstream file(path); const std::string text{std::istreambuf_iterator<char>{file},{}};
        if(worker_case=="export-exists") check(text=="sentinel");
        else {
            check(text.starts_with("{\"schema\":\"tll-p99-wrist-v1\"") && text.ends_with("}\n"));
            check(text.find("\"source_admitted\":false")!=std::string::npos);
            check(text.find("\"payload_bytes\":"+std::string(worker_case=="capture"?"256":"0"))!=std::string::npos);
        }
        if(worker_case=="last-refusal") {
            check(report.reason==p99_observe::Reason::kExpired && report.probes==1 && report.attempts==0);
            check(report.diagnostic_available && report.diagnostic_probe==1 &&
                report.diagnostic.reason==P99WristReadReject::Owner);
        }
        if(worker_case=="idle-expiry" || worker_case=="arm-timeout")
            check(report.reason==p99_observe::Reason::kExpired && !report.diagnostic_available && report.probes==0);
    }
    check(VirtualFree(reinterpret_cast<void*>(g_module),0,MEM_RELEASE)!=FALSE);
    std::cout<<worker_case<<": "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
