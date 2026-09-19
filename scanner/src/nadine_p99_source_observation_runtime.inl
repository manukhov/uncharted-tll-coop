// Included after the complete owned producer and native entry observations.
// No Process/cmd16 hooks, frame.job publication, or pointer substitution here.
namespace p99_observe = coop::p99_source_observation;
p99_observe::Policy g_p99_source_observation;
std::atomic<bool> g_p99_source_observation_reported{false};
std::uint64_t g_p99_source_observation_install_ms = 0; // Published by ready release.

void p99_source_observation_stop_report(const char* external_reason = nullptr) {
    const auto reason = g_p99_source_observation.reason();
    if (reason == p99_observe::Reason::kNone && external_reason == nullptr) return;
    g_p99_source_observation_ready.store(false, std::memory_order_release);
    if (g_p99_source_observation_reported.exchange(true)) return;
    char line[256]{};
    std::snprintf(line, sizeof(line),
        "P99-OBSERVE STOP reason=%s probes=%u attempts=%u writes=0; future jobs stock-only",
        external_reason ? external_reason : p99_observe::reason_name(reason), g_p99_source_observation.probes(),
        g_p99_source_observation.attempts());
    boot_log_line(line);
}

// A rate-limited owner/pistol preselection, not source materialization. Its
// pointers expire on return; the real producer repeats every owner/page check.
bool p99_source_observation_selected(void* job) {
    P99GroupJob normal{};
    P99ChloeTransferEpoch selected{};
    P99SourceReader pages(&P99_SOURCE_QUERY_FUNCTION, &p99_source_copy);
    return seh_p99_group_normal_job(job, &normal, pages) &&
        seh_select_p99_chloe_transfer_epoch(job, &selected,
            P99SourcePurpose::kOwnedGroupValues, &pages) &&
        reinterpret_cast<std::uintptr_t>(selected.fg_anim_data) == normal.fg &&
        reinterpret_cast<std::uintptr_t>(selected.target_skeleton) == normal.art;
}

// Once admitted by policy, no lease/snapshot escapes this function. Native
// stock animation is called by the wrapper only AFTER this stack has expired.
__declspec(noinline) void p99_source_observation_candidate(void* job, P99FiberFrame& frame) {
    const auto grant = g_p99_source_observation.begin_probe(GetTickCount64());
    if (grant != p99_observe::Probe::kGranted) {
        if (grant == p99_observe::Probe::kStopped) p99_source_observation_stop_report();
        return;
    }
    bool leased = true;
    __try {
        const auto begin = p99_group_cost_start();
        const bool selected = begin.valid && p99_source_observation_selected(job);
        const auto probe_end = p99_group_cost_start();
        coop::p99_group_budget::Budget budget{g_p99_group_cost_limit};
        const bool clocks = begin.valid && probe_end.valid &&
            probe_end.value.QuadPart >= begin.value.QuadPart;
        if (!clocks || !budget.charge(static_cast<std::uint64_t>(begin.value.QuadPart),
                static_cast<std::uint64_t>(probe_end.value.QuadPart))) {
            (void)g_p99_source_observation.abort(clocks ? p99_observe::Reason::kBudget : p99_observe::Reason::kClock);
            leased = false;
            p99_source_observation_stop_report();
            return;
        }
        if (!g_p99_group_fiber.owns(frame) || frame.entry.reentered) {
            (void)g_p99_source_observation.abort(p99_observe::Reason::kProtocol);
            leased = false;
            p99_source_observation_stop_report();
            return;
        }
        if (!g_p99_source_observation.finish_probe(GetTickCount64(), selected)) {
            leased = false;
            p99_source_observation_stop_report();
            return;
        }
        P99GroupJob owned{};
        P99PreparationObservation observation{};
        const bool prepared = prepare_p99_group_values(job, &owned, &observation);
        const auto end = p99_group_cost_start();
        const bool scope_valid = g_p99_group_fiber.owns(frame) && !frame.entry.reentered;
        // Stop new admissions before publication. The only values published
        // are scalar diagnostics; owned.rows is never assigned to frame.job.
        g_p99_source_observation_ready.store(false, std::memory_order_release);
        const bool clock_valid = begin.valid && end.valid && end.value.QuadPart >= begin.value.QuadPart;
        const auto ticks = clock_valid ? end.value.QuadPart - begin.value.QuadPart : 0;
        const auto stop = g_p99_source_observation.finish_preparation(
            static_cast<std::uint64_t>(begin.value.QuadPart),
            static_cast<std::uint64_t>(end.value.QuadPart), clock_valid, prepared && scope_valid);
        leased = false;
        char line[384]{};
        std::snprintf(line, sizeof(line),
            "P99-OBSERVE RESULT prepared=%u scope=%u stage=%s detail=%s stop=%s ticks=%llu limit=%llu output_bytes=%u writes=0",
            prepared ? 1u : 0u, scope_valid ? 1u : 0u, observation.stage, observation.reason,
            p99_observe::reason_name(stop), static_cast<unsigned long long>(ticks),
            static_cast<unsigned long long>(g_p99_group_cost_limit), prepared ? 1008u : 0u);
        boot_log_line(line); // Outside the measured read/math interval and registry lock.
        p99_source_observation_stop_report();
    } __finally {
        if (leased) {
            g_p99_source_observation.abandon();
            g_p99_source_observation_ready.store(false, std::memory_order_release);
        }
    }
}

void __fastcall hooked_p99_source_observation_async(void* job) {
    if (g_p99_source_observation_ready.load(std::memory_order_acquire)) {
        const auto now = GetTickCount64();
        if (now < g_p99_source_observation_install_ms ||
            now - g_p99_source_observation_install_ms >= p99_observe::kLifetimeMs)
            p99_source_observation_stop_report(now < g_p99_source_observation_install_ms ? "clock" : "expired");
    }
    if (!g_p99_source_observation_ready.load(std::memory_order_acquire)) {
        g_p99_group_async(job);
        return;
    }
    P99FiberFrame frame{};
    P99FiberFrame* previous = nullptr;
    if (!g_p99_group_fiber.enter(frame, previous)) {
        p99_source_observation_stop_report("fls-enter");
        g_p99_group_async(job);
        return;
    }
    __try {
        if (previous) coop::p99_group_entry::mark_parent(&previous->entry);
        const bool eligible = previous == nullptr && job != nullptr && g_module != 0 &&
            g_state_net.enabled && g_net_role_b &&
            g_p99_source_observation_ready.load(std::memory_order_acquire);
        const bool candidate = coop::p99_group_entry::admit(eligible,
            [&](coop::p99_group_entry::Observations& seen) { return seh_p99_group_entry_observations(job, &seen); },
            [] { return true; });
        if (candidate) p99_source_observation_candidate(job, frame);
        // Stock exactly once, no catch/retry and no shared pose context.
        coop::p99_group_native_frame::p99_group_invoke_native(
            job, g_p99_group_async, &frame.native_return_slot);
    } __finally {
        frame.native_return_slot = 0;
        if (!g_p99_group_fiber.leave(frame, previous))
            p99_source_observation_stop_report("fls-leave");
    }
}
