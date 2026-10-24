// Native consumer for the bounded campaign main-menu -> manual-load workflow.
// Called ONLY after the original whole postrender callback returned normally,
// still inside its native job/FLS while the outer game-loop waits for that job.
// This is not an arbitrary-thread spawn API or a universal script-reset lease.
namespace player_world_service {
struct State {
    std::uint64_t capture{}, epoch{}, close_cycle{}, open_serial{};
    OwnedRemotePlayerIdentity primary{};
    const char* last_wait{};
};
State state{}; // exact full-postrender callback only, no socket-thread access.

void wait_for(const char* reason) {
    if (state.last_wait == reason) return;
    state.last_wait = reason;
    char line[192]{};
    std::snprintf(line, sizeof(line), "PLAYER-WORLD WAIT %s", reason);
    boot_log_line(line);
}
bool same_weak(const OwnedRemotePlayerIdentity& a, const OwnedRemotePlayerIdentity& b) {
    return a.record != 0 && a.record == b.record && a.generation == b.generation;
}
bool current_primary_matches(const player_world_spawn::Recipe& value) {
    void* const primary = retained_player_object(0);
    return primary != nullptr && player_object_stable(primary) &&
        player_world_spawn::seh_primary_heap_matches(primary, value.primary, value.primary_heap);
}
bool cycle_matches(const player_world_spawn::Recipe& value,
                   const player_world_menu_cycle::Snapshot& cycle) {
    if (!cycle.valid || cycle.transition_inflight || !cycle.completed_frame ||
        value.physical_close_cycle != cycle.close_cycle ||
        (value.physical_open_serial != 0 &&
         value.physical_open_serial != cycle.open_serial)) return false;
    // A native primary may finish after Close but before pending Init. Its
    // owned values are bound to that exact closed generation, not the previous
    // world; full-tail admission still requires the observed matching open.
    // Startup init may precede injection. Permit that initial serial0 only
    // before ANY observed physical close; do not fabricate an open event.
    return cycle.close_cycle == 0 ||
        (cycle.open_serial != 0 && cycle.opened_cycle == cycle.close_cycle);
}

void clear_retired_actor_registry() {
    // The caller has POSITIVE old free + joined native phase, never a weak
    // pointer read-fault or ready=false-only "quiescence" assumption.
    AcquireSRWLockExclusive(&g_actor_identity_lock);
    g_owned_remote_player_ready.store(false, std::memory_order_release);
    g_owned_remote_player = {};
    g_admitted_primary_ready = false;
    g_admitted_primary = {};
    g_stock_nadine_identity = {};
    g_stock_nadine_identity_state.store(0, std::memory_order_release);
    g_companion_spawn_gate_identity = {};
    g_companion_spawn_gate_armed.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_actor_identity_lock);
    g_stock_nadine_retired_generations.store(0, std::memory_order_release);
    g_stock_nadine_respawn_budget_logged.store(false, std::memory_order_release);
    g_companion_spawn_suppressed_logged.store(false, std::memory_order_release);
    g_companion_deferred_record = 0;
    g_companion_deferred_generation = 0;
    g_peer_retirement_walk_done = false;
    g_peer_phase_guard_reported = false;
    g_remote_player_ownership_refused.store(false, std::memory_order_release);
    g_remote_player_spawn_consumed.store(false, std::memory_order_release);
    g_remote_player_teardown.store(false, std::memory_order_release);
    g_autotest_done = false;
    g_slot1_auto_reissue_done = true; // never use the legacy Nadine/VEH replay path.
    g_puppet_net_apply = false;
    g_drive_slot1 = false;
    g_auto_stick_active = false;
    g_last_nadine_object = nullptr;
    g_controlled_object = nullptr;
    g_clone_cached = nullptr;
    g_object_states.clear();
}

bool spawn_once(const player_world_spawn::Recipe& value) {
    if (retained_player_object(1) != nullptr) {
        wait_for("foreign/live secondary slot; no adoption");
        return false;
    }
    bool unused = false;
    if (!g_remote_player_spawn_consumed.compare_exchange_strong(
            unused, true, std::memory_order_acq_rel, std::memory_order_acquire)) return false;
    alignas(16) std::uint8_t output[16]{};
    void* result{};
    const bool prior_swap = g_appearance_swap_active;
    const bool swap = g_want_nadine_body && !g_net_role_b;
    g_inject_look = false;
    if (swap) {
        g_nadine_appearance_hash = 0xD27C074Fu;
        g_appearance_swap_active = true;
    }
    __try {
        result = reinterpret_cast<player_world_spawn::SpawnFunction>(g_module + kSpawnFromDescriptorRva)(
            output, 1, value.continue_values, nullptr, 0,
            value.has_persistent ? value.persistent_values : nullptr);
    } __finally {
        g_appearance_swap_active = prior_swap;
        g_slot1_visual_override_active = false;
    }
    // Bind our exact synchronous OUTPUT before checking for a context change,
    // so a verified native actor is still owned for later native reset cleanup.
    OwnedRemotePlayerIdentity owned{};
    const bool owned_ok = result == output &&
        seh_capture_owned_remote_player_identity(owned, output) &&
        publish_owned_remote_player_identity(owned);
    player_world_menu_cycle::Snapshot after{};
    const bool context_ok = player_world_menu_cycle::copy_snapshot(after) &&
        cycle_matches(value, after) && current_primary_matches(value) &&
        value.reset_serial == player_world_spawn::reset_serial.load(std::memory_order_acquire) &&
        !player_world_spawn::reset_inflight.load(std::memory_order_acquire) &&
        !player_world_spawn::reset_failed.load(std::memory_order_acquire);
    if (!owned_ok || !context_ok) {
        g_remote_player_ownership_refused.store(true, std::memory_order_release);
        g_remote_player_teardown.store(true, std::memory_order_release);
        g_puppet_net_apply = false;
        player_world::close_for_reset();
        wait_for("native spawn output/context REFUSED; consumed, no retry");
        return false;
    }
    g_autotest_done = true;
    g_puppet_net_apply = player_world::peer_ready();
    state.last_wait = nullptr;
    char line[256]{};
    std::snprintf(line, sizeof(line),
        "PLAYER-WORLD OWNED epoch=%llu capture=%llu record=0x%llX generation=%u sync=%u",
        static_cast<unsigned long long>(state.epoch),
        static_cast<unsigned long long>(value.capture_serial),
        static_cast<unsigned long long>(owned.record), owned.generation, g_puppet_net_apply ? 1u : 0u);
    boot_log_line(line);
    coop::StatePacket peer{};
    if (copy_last_peer_state_snapshot(peer)) apply_peer_health_to_slot1(peer, true);
    return true;
}
} // namespace player_world_service

void service_player_world_postrender_tail() {
    if constexpr (!player_world::kEnablePlayerWorldRearm) return;
    using namespace player_world_service;
    if (g_dll_inert || !g_enable_remote_player_autospawn ||
        !g_state_net.enabled || !g_state_net.running.load(std::memory_order_acquire)) return;
    if (!g_remote_player_reset_ready.load(std::memory_order_acquire) ||
        !g_peer_lifecycle_ready.load(std::memory_order_acquire) ||
        !g_player_update_hook_ready.load(std::memory_order_acquire) ||
        !player_world_retirement::ready.load(std::memory_order_acquire) ||
        !player_world_spawn::capture_ready.load(std::memory_order_acquire) ||
        !player_world_menu_cycle::ready()) { wait_for("native package not ready"); return; }
    if (g_play_as_clone || g_hide_clone_from_slot || g_enable_legacy_clone_autospawn) {
        wait_for("legacy manual clone experiment active"); return;
    }
    player_world_spawn::Recipe value{};
    player_world_menu_cycle::Snapshot cycle{};
    if (!player_world_spawn::copy_recipe(value) || !value.has_task_key ||
        !player_world_menu_cycle::copy_snapshot(cycle) || !cycle_matches(value, cycle)) {
        wait_for("fresh campaign recipe / physical menu cycle"); return;
    }
    if (player_world_spawn::reset_inflight.load(std::memory_order_acquire) ||
        player_world_spawn::reset_failed.load(std::memory_order_acquire)) {
        wait_for("native reset still active/unknown"); return;
    }
    if (state.capture == value.capture_serial &&
        g_owned_remote_player_ready.load(std::memory_order_acquire)) return;
    void* root{};
    if (!seh_live_world_retirement_ready(&root) || !current_primary_matches(value)) {
        wait_for("live world/TLS/kill/primary identity"); return;
    }
    if (state.capture != value.capture_serial) {
        if (state.capture && (cycle.close_cycle <= state.close_cycle ||
                              cycle.open_serial <= state.open_serial)) {
            wait_for("fresh primary without a NEW manual-menu physical cycle"); return;
        }
        OwnedRemotePlayerIdentity previous{};
        player_world_retirement::Snapshot retired{};
        if (copy_owned_remote_player_identity(previous)) {
            if (!cycle.close_cycle || !same_weak(previous, cycle.closed_owned) ||
                !player_world_retirement::copy_retired(previous, retired)) {
                wait_for("positive old-owned free + matching menu cycle"); return;
            }
        } else if (g_remote_player_spawn_consumed.load(std::memory_order_acquire) ||
                   g_remote_player_ownership_refused.load(std::memory_order_acquire)) {
            wait_for("untracked/failed prior spawn; no rearm"); return;
        }
        if (retained_player_object(1) != nullptr) { wait_for("secondary slot not retired"); return; }
        player_world::close_for_reset();
        if (!player_world::ingress_quiescent()) { wait_for("old ingress draining"); return; }
        // Exact postrender's process/script/animation jobs have joined. The
        // normal frame cannot advance until this callback returns. No native
        // resource/pointer is held under these short metadata publications.
        clear_player_world_transients();
        clear_retired_actor_registry();
        // Publish this numeric tuple while transport is still CLOSED. Native
        // OnKill/heap-clear observers can invalidate it without ever following
        // a primary pointer from an enemy animation job.
        stage_admitted_primary_identity(value.primary, value.primary_heap);
        const auto epoch = player_world::publish_local_world(value.key);
        if (!epoch) { wait_for("new world publication refused"); return; }
        if (!bind_admitted_primary_epoch(value.primary, epoch)) {
            player_world::close_for_reset();
            wait_for("primary invalidated during world publication"); return;
        }
        state.capture = value.capture_serial;
        state.primary = value.primary;
        state.epoch = epoch;
        state.close_cycle = cycle.close_cycle;
        state.open_serial = cycle.open_serial;
        state.last_wait = nullptr;
        char line[320]{};
        std::snprintf(line, sizeof(line),
            "PLAYER-WORLD PREPARED epoch=%llu capture=%llu reset=%llu close=%llu opened=%llu openSerial=%llu oldRecord=0x%llX oldGeneration=%u freeTicket=%llu primaryRecord=0x%llX primaryGeneration=%u",
            static_cast<unsigned long long>(epoch),
            static_cast<unsigned long long>(value.capture_serial),
            static_cast<unsigned long long>(value.reset_serial),
            static_cast<unsigned long long>(cycle.close_cycle),
            static_cast<unsigned long long>(cycle.opened_cycle),
            static_cast<unsigned long long>(cycle.open_serial),
            static_cast<unsigned long long>(previous.record), previous.generation,
            static_cast<unsigned long long>(retired.serial),
            static_cast<unsigned long long>(value.primary.record), value.primary.generation);
        boot_log_line(line);
        return; // handshake progresses on the existing socket worker, no wait.
    }
    if (!player_world::peer_ready()) { wait_for("matching fresh peer world"); return; }
    if (!g_nadine_companion_donor_cached.load(std::memory_order_acquire) ||
        (g_want_nadine_body && !g_nadine_visual_identity_valid)) {
        wait_for("native companion donor/visual bootstrap"); return;
    }
    spawn_once(value);
}
