// Included after the complete owned producer and native entry observations.
// No Process/cmd16 hooks, frame.job publication, or pointer substitution here.
namespace p99_observe = coop::p99_source_observation;
p99_observe::Policy g_p99_source_observation;
std::atomic<bool> g_p99_source_observation_reported{false};
std::uint64_t g_p99_source_observation_install_ms = 0; // Published by ready release.

void p99_source_observation_stop_report(const char* external_reason = nullptr,
    const p99_preselect::Trace* trace = nullptr, std::uint64_t probe_ticks = 0) {
    const auto reason = g_p99_source_observation.reason();
    if (reason == p99_observe::Reason::kNone && external_reason == nullptr) return;
    g_p99_source_observation_ready.store(false, std::memory_order_release);
    if (g_p99_source_observation_reported.exchange(true)) return;
    char line[1024]{};
    if (trace) {
        // The only publication is scalar diagnostic evidence, after the
        // preselection Reader has expired. Never log each retry/probe or cache
        // a trace containing authority to a live Player/weapon/skeleton.
        const auto& e = trace->elapsed;
        std::snprintf(line, sizeof(line),
            "P99-OBSERVE STOP reason=%s probes=%u attempts=%u writes=0 "
            "selected=%u normal=%u stage=%s detail=%s mask=%x clock=%u "
            "probe_ticks=%llu limit=%llu queries=%llu raw_id=%08x params=%u "
            "ticks_setup=%llu ticks_normal=%llu gate=%llu retained=%llu fg=%llu owner=%llu "
            "target=%llu registry=%llu identity=%llu; future jobs stock-only",
            external_reason ? external_reason : p99_observe::reason_name(reason),
            g_p99_source_observation.probes(), g_p99_source_observation.attempts(),
            trace->selected ? 1u : 0u, trace->normal_passed ? 1u : 0u,
            p99_preselect::stage_name(trace->active), trace->detail,
            trace->entered_mask, trace->clock_valid ? 1u : 0u,
            static_cast<unsigned long long>(probe_ticks),
            static_cast<unsigned long long>(g_p99_group_cost_limit),
            static_cast<unsigned long long>(trace->queries), trace->skeleton_id,
            static_cast<unsigned>(trace->parameter_count),
            static_cast<unsigned long long>(e[0]), static_cast<unsigned long long>(e[1]),
            static_cast<unsigned long long>(e[2]), static_cast<unsigned long long>(e[3]),
            static_cast<unsigned long long>(e[4]), static_cast<unsigned long long>(e[5]),
            static_cast<unsigned long long>(e[6]), static_cast<unsigned long long>(e[7]),
            static_cast<unsigned long long>(e[8]));
        boot_log_line(line);
        return;
    }
    std::snprintf(line, sizeof(line),
        "P99-OBSERVE STOP reason=%s probes=%u attempts=%u writes=0 trace=unavailable; future jobs stock-only",
        external_reason ? external_reason : p99_observe::reason_name(reason), g_p99_source_observation.probes(),
        g_p99_source_observation.attempts());
    boot_log_line(line);
}

// A rate-limited owner/pistol preselection, not source materialization. Its
// pointers expire on return; the real producer repeats every owner/page check.
bool p99_source_observation_selected(void* job, p99_preselect::Trace& trace) {
    P99NormalJobIdentity normal{};
    P99ChloeTransferSelection selected{};
    P99SourceReader pages(&P99_SOURCE_QUERY_FUNCTION, &p99_source_copy);
    bool admitted = false;
    p99_preselection_enter(&trace, p99_preselect::Stage::NormalJob, "normal-job");
    if (seh_p99_group_normal_job(job, &normal, pages, &trace)) {
        p99_preselection_enter(&trace, p99_preselect::Stage::SelectGate, "select-gate");
        if (seh_select_p99_chloe_transfer_epoch(job, &selected,
                P99SourcePurpose::kOwnedGroupValues, &pages, &trace)) {
            p99_preselection_enter(&trace, p99_preselect::Stage::Identity, "fg-art-match");
            admitted = reinterpret_cast<std::uintptr_t>(selected.fg_anim_data) == normal.fg &&
                reinterpret_cast<std::uintptr_t>(selected.target_skeleton) == normal.art;
            if (admitted) trace.detail = "selected";
        }
    }
    trace.queries = pages.query_count(); // Scalar counter, no additional page query.
    trace.selected = admitted;
    return admitted;
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
        p99_preselect::Trace trace{};
        trace.start({static_cast<std::uint64_t>(begin.value.QuadPart), begin.valid});
        const bool selected = begin.valid && p99_source_observation_selected(job, trace);
        const auto probe_end = p99_group_cost_start();
        trace.finish({static_cast<std::uint64_t>(probe_end.value.QuadPart), probe_end.valid});
        const auto probe_ticks = begin.valid && probe_end.valid &&
            probe_end.value.QuadPart >= begin.value.QuadPart
            ? static_cast<std::uint64_t>(probe_end.value.QuadPart - begin.value.QuadPart) : 0;
        coop::p99_group_budget::Budget budget{g_p99_group_cost_limit};
        const bool clocks = trace.clock_valid && begin.valid && probe_end.valid &&
            probe_end.value.QuadPart >= begin.value.QuadPart;
        if (!clocks || !budget.charge(static_cast<std::uint64_t>(begin.value.QuadPart),
                static_cast<std::uint64_t>(probe_end.value.QuadPart))) {
            (void)g_p99_source_observation.abort(clocks ? p99_observe::Reason::kBudget : p99_observe::Reason::kClock);
            leased = false;
            p99_source_observation_stop_report(nullptr, &trace, probe_ticks);
            return;
        }
        if (!g_p99_group_fiber.owns(frame) || frame.entry.reentered) {
            (void)g_p99_source_observation.abort(p99_observe::Reason::kProtocol);
            leased = false;
            p99_source_observation_stop_report(nullptr, &trace, probe_ticks);
            return;
        }
        if (!g_p99_source_observation.finish_probe(GetTickCount64(), selected)) {
            leased = false;
            p99_source_observation_stop_report(nullptr, &trace, probe_ticks);
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
        p99_source_observation_stop_report(nullptr, &trace, probe_ticks);
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
