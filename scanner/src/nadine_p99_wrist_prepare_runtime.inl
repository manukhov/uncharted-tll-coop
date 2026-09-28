// Off-loader PREPARED-only branch. Hard-disabled in production. It returns before
// all ordinary legacy/appearance/network installers and NEVER patches five sites,
// acknowledges installation, starts an observer, sets READY, or calls retail code.
namespace p99_prepare = coop::p99_wrist_prepare;
namespace p99_handshake = coop::p99_wrist_handshake;
namespace p99_code = coop::p99_wrist_code;
std::atomic<bool> g_p99_wrist_preparation_started{false};
p99_prepare::Resources g_p99_wrist_preparation_resources;
p99_code::Snapshot g_p99_wrist_preparation_code;
p99_handshake::Gate g_p99_wrist_preparation_gate;

struct P99WristPreparationProofs {
    // Local self-consistency only: concrete entries are compiled above and PIN
    // retains their allocation. These predicates do NOT certify the entire DLL
    // build/ABI/bridge bytes for an external writer; it must do that independently.
    bool identity(const p99_handshake::Offer& offer) noexcept {
        return g_module==offer.module && g_self_module==offer.dll &&
            g_p99_wrist_preparation_resources.identity(offer);
    }
    bool resources(const p99_handshake::Offer& offer,const coop::p99_wrist_install::Plan& plan) noexcept {
        return g_p99_group_fiber.initialized() && !g_p99_group_fiber.failed() &&
            g_p99_wrist_preparation_code.ready() &&
            g_p99_wrist_preparation_resources.verify(offer,plan);
    }
    bool sites(const coop::p99_wrist_install::Plan& plan,coop::p99_wrist_code_manifest::Stage stage) noexcept {
        if(stage!=coop::p99_wrist_code_manifest::Stage::Stock &&
            stage!=coop::p99_wrist_code_manifest::Stage::Installed)return false;
        p99_code::ImageReader reader(g_module);
        for(const auto& site:plan.sites) {
            const auto& bytes=stage==coop::p99_wrist_code_manifest::Stage::Stock?site.before:site.after;
            if(!reader.equal(site.address,std::span(bytes).first(site.size)))return false;
        }
        return true;
    }
    bool code(coop::p99_wrist_code_manifest::Stage stage) noexcept {
        p99_code::ImageReader reader(g_module);
        return g_p99_wrist_preparation_code.matches(stage,reader);
    }
    bool detached() noexcept {
        BOOL debugger=TRUE;
        return !IsDebuggerPresent() && CheckRemoteDebuggerPresent(GetCurrentProcess(),&debugger) && !debugger;
    }
};

bool prepare_p99_wrist_runtime() noexcept {
    if(g_p99_wrist_preparation_started.exchange(true,std::memory_order_acq_rel))return false;
    if(g_p99_group_shadow_ready.load(std::memory_order_acquire) ||
        g_p99_source_observation_ready.load(std::memory_order_acquire) ||
        g_p99_wrist_ready.load(std::memory_order_acquire) ||
        g_p99_wrist_installed.load(std::memory_order_acquire) ||
        g_p99_wrist_worker_started.load(std::memory_order_acquire) ||
        g_p99_group_fiber.initialized() || !g_p99_group_install_gate.begin())return false;
    const std::array<std::uintptr_t,5> hooks{
        reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_async),
        reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_async),
        reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_process0),
        reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_process1),
        reinterpret_cast<std::uintptr_t>(&hooked_p99_wrist_cmd16)};
    if(!g_p99_wrist_preparation_resources.prepare(g_module,g_self_module,hooks))return false;
    p99_code::ImageReader reader(g_module);p99_code::Sha256 hash;
    if(!g_p99_wrist_preparation_code.prepare(g_p99_wrist_preparation_resources.plan(),reader,hash) ||
        !g_p99_group_fiber.initialize())return false;
    // Bootstrap THIS worker's empty slot while still off-thread. Other fibers
    // may still need their own lazy FLS storage later; no zero-overhead claim.
    if(g_p99_group_fiber.current()!=nullptr || g_p99_group_fiber.failed())return false;
    P99WristPreparationProofs proof;
    if(!g_p99_wrist_preparation_gate.prepare(g_p99_wrist_preparation_resources.offer(),proof))return false;
    const auto wire=g_p99_wrist_preparation_gate.publication();
    // DLL is permanently pinned and all relay/FLS allocations are retained before
    // the first potentially visible wire store, even if sealing reports failure.
    if(!wire || !p99_prepare::publish(TllP99WristPreparedV1,g_self_module,*wire)) {
        (void)g_p99_wrist_preparation_gate.expire();return false;
    }
    return true;
}
