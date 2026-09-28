// Inert full production TU. No DllMain/startup installer; the exported fixture
// calls only PREPARED body against this test's non-retail EXE and expects refusal.
#define TLL_P99_PREPARATION_AUDIT 1
#include "../src/coop_runtime.cpp"

extern "C" __declspec(dllexport) unsigned p99_wrist_prepare_refusal_fixture() {
    unsigned bad=0;
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&p99_wrist_prepare_refusal_fixture),&self))return 0xFFFFFFFF;
    g_module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    g_self_module=reinterpret_cast<std::uintptr_t>(self);
    if(prepare_p99_wrist_runtime())bad|=1;
    if(prepare_p99_wrist_runtime())bad|=2;
    if(!g_p99_wrist_preparation_started.load())bad|=4;
    if(g_p99_group_fiber.initialized() || g_p99_group_fiber.failed())bad|=8;
    if(g_p99_wrist_preparation_gate.state()!=p99_handshake::State::Empty)bad|=16;
    if(g_p99_wrist_ready.load() || g_p99_wrist_installed.load() || g_p99_wrist_worker_started.load())bad|=32;
    if(g_state_net.enabled || g_net_role_b)bad|=64;
    if(g_p99_group_shadow_ready.load() || g_p99_source_observation_ready.load())bad|=128;
    const std::array<std::uint8_t,4096> empty{};
    if(!p99_prepare::bytes_equal(reinterpret_cast<std::uintptr_t>(&TllP99WristPreparedV1),empty))bad|=256;
    if(g_p99_wrist_preparation_code.ready())bad|=512;
    if(g_p99_group_install_gate.begin())bad|=1024; // exclusion consumed, no fallback installer
    if(kEnableP99WristPreparation || kEnableP99WristObservation)bad|=2048;
    return bad;
}
