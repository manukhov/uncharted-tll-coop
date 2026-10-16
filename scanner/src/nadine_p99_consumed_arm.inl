// Included after consumed_runtime. An explicit PID/creation-bound request only
// permits the NEXT qualifying existing scalar observation to enable sampling.
// No game pointers, owner reads, new getters, logging on the tap, or policy resets.
std::atomic<bool> g_p99_consumed_arm_wait_started{false};
enum class P99ConsumedArmPhase : unsigned { Dormant, WaitingDraw, Claimed, Armed, Stopped };
std::atomic<P99ConsumedArmPhase> g_p99_consumed_arm_phase{P99ConsumedArmPhase::Dormant};
std::uint64_t g_p99_consumed_arm_wait_ms = 0; // Set before worker creation.
std::uint64_t g_p99_consumed_request_ms = 0; // Before WaitingDraw release.
std::uint64_t g_p99_consumed_draw_begin_ms = 0; // Before Armed release.
int g_p99_consumed_arm_slot = -1; // Before Armed release, scalar diagnostic only.
constexpr DWORD kP99ConsumedArmWaitMs = 300000;
// Conservative timing-hint age policy, NOT evidence of steady idle or ownership.
constexpr std::uint64_t kP99ConsumedDrawHintMaxMs = 100;

void p99_consumed_arm_stop(const char* reason) {
    g_p99_consumed_arm_phase.store(P99ConsumedArmPhase::Stopped, std::memory_order_release);
    g_p99_consumed_ready.store(false, std::memory_order_release);
    g_p99_consumed_finished.store(true, std::memory_order_release);
    char line[256]{};
    std::snprintf(line, sizeof(line),
        "P99-CONSUMED ARM-STOP reason=%s; stock-only; no rearm; pose_writes=0", reason);
    boot_log_line(line);
}

// Acquire the one-shot request BEFORE the existing reads; do not replay a sender
// invocation whose values were read before/during request publication.
std::uint64_t p99_consumed_draw_token() {
    if (!kEnableP99ConsumedCensus || !g_net_role_b ||
        g_p99_consumed_arm_phase.load(std::memory_order_acquire) != P99ConsumedArmPhase::WaitingDraw)
        return UINT64_MAX;
    return GetTickCount64();
}

// Called only with the draw state and current-slot art the sender ALREADY read.
// It selects time, not ownership or steady idle. The full owner admission is
// still mandatory. No wait, handle operation or log runs on this game path.
void observe_p99_consumed_draw(std::uint64_t token, int slot,
    std::uint32_t state, std::uint32_t art) {
    if (!kEnableP99ConsumedCensus || !g_net_role_b || state != 2 ||
        token == UINT64_MAX || slot < 0 || slot >= 18 || art != kCoopPistolArtId ||
        g_p99_consumed_arm_phase.load(std::memory_order_acquire) != P99ConsumedArmPhase::WaitingDraw)
        return;
    const auto now = GetTickCount64();
    // An old in-flight read sequence is not a fresh timing hint. A stale hint
    // does not consume the request or any owner/sample budget.
    if (token < g_p99_consumed_request_ms || now < token ||
        now - token > kP99ConsumedDrawHintMaxMs) return;
    auto expected = P99ConsumedArmPhase::WaitingDraw;
    if (!g_p99_consumed_arm_phase.compare_exchange_strong(expected,
            P99ConsumedArmPhase::Claimed, std::memory_order_acq_rel)) return;
    if (now < g_p99_consumed_request_ms || now < g_p99_consumed_arm_wait_ms ||
        now - g_p99_consumed_arm_wait_ms >= kP99ConsumedArmWaitMs ||
        !g_p99_consumed_installed.load(std::memory_order_acquire) ||
        g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire) ||
        g_p99_consumed_fiber.failed()) {
        g_p99_consumed_finished.store(true, std::memory_order_release);
        g_p99_consumed_arm_phase.store(P99ConsumedArmPhase::Stopped, std::memory_order_release);
        return;
    }
    const auto publish_tick = GetTickCount64();
    if (publish_tick < now || publish_tick - token > kP99ConsumedDrawHintMaxMs ||
        publish_tick - g_p99_consumed_arm_wait_ms >= kP99ConsumedArmWaitMs) {
        g_p99_consumed_finished.store(true, std::memory_order_release);
        g_p99_consumed_arm_phase.store(P99ConsumedArmPhase::Stopped, std::memory_order_release);
        return;
    }
    g_p99_consumed_arm_ms = publish_tick;
    g_p99_consumed_draw_begin_ms = token;
    g_p99_consumed_arm_slot = slot;
    // Commit the gate BEFORE ready: a timeout that won Claimed->Stopped must
    // never leave a transient ready window. Armed tells the worker this gate
    // won; it is not proof of ownership, capture or even nonterminal readiness.
    expected = P99ConsumedArmPhase::Claimed;
    if (!g_p99_consumed_arm_phase.compare_exchange_strong(expected,
            P99ConsumedArmPhase::Armed, std::memory_order_release, std::memory_order_relaxed)) return;
    if (g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire)) {
        return;
    }
    g_p99_consumed_ready.store(true, std::memory_order_release);
    // Concurrent terminal state cannot be cleared by this one-shot publication.
    // The outer also checks terminal independently; in-flight work is not cancelled.
    if (g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire))
        g_p99_consumed_ready.store(false, std::memory_order_release);
}

// Called only by the dedicated worker after installer returned and all
// suspended threads resumed. Its wait is bounded; no animation thread waits.
DWORD WINAPI await_p99_consumed_arm(void* argument) {
    const auto event = static_cast<HANDLE>(argument);
    DWORD result = WAIT_FAILED;
    __try {
        result = WaitForSingleObject(event, kP99ConsumedArmWaitMs);
    } __except (EXCEPTION_EXECUTE_HANDLER) { result = WAIT_FAILED; }
    CloseHandle(event); // Worker owns the one event handle after CreateThread.
    if (result != WAIT_OBJECT_0) {
        p99_consumed_arm_stop(result == WAIT_TIMEOUT ? "arm-timeout" : "arm-wait-failed");
        return 0;
    }
    __try {
    const auto now = GetTickCount64();
    if (now < g_p99_consumed_arm_wait_ms ||
        now - g_p99_consumed_arm_wait_ms >= kP99ConsumedArmWaitMs ||
        !g_p99_consumed_installed.load(std::memory_order_acquire) ||
        g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire) ||
        g_p99_consumed_fiber.failed()) {
        p99_consumed_arm_stop("request-refused");
        return 0;
    }
    g_p99_consumed_request_ms = now;
    boot_log_line("P99-CONSUMED ARM-REQUESTED awaiting NEXT existing current-slot P99 state2 observation; no probes yet; pose_writes=0");
    if (g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire)) {
        p99_consumed_arm_stop("request-log-terminal"); return 0;
    }
    // No observation from before/during the possibly slow log is retained.
    // Total request + draw wait stays bounded by the original install deadline.
    const auto after_log = GetTickCount64();
    if (after_log < now || after_log - g_p99_consumed_arm_wait_ms >= kP99ConsumedArmWaitMs) {
        p99_consumed_arm_stop("request-log-timeout"); return 0;
    }
    g_p99_consumed_arm_phase.store(P99ConsumedArmPhase::WaitingDraw, std::memory_order_release);
    for (;;) {
        const auto phase = g_p99_consumed_arm_phase.load(std::memory_order_acquire);
        if (phase == P99ConsumedArmPhase::Armed) {
            char line[384]{};
            std::snprintf(line, sizeof(line),
                "P99-CONSUMED ARMED observed-draw tick=%llu request=%llu begin=%llu slot=%d state=2 art=8075397D ready=%u terminal=%u; "
                "marker=async-may-follow-probe; 120s from observation; 8 samples/600 probes; combined 1ms cap; pose_writes=0",
                static_cast<unsigned long long>(g_p99_consumed_arm_ms),
                static_cast<unsigned long long>(g_p99_consumed_request_ms),
                static_cast<unsigned long long>(g_p99_consumed_draw_begin_ms), g_p99_consumed_arm_slot,
                static_cast<unsigned>(g_p99_consumed_ready.load(std::memory_order_acquire)),
                static_cast<unsigned>(g_p99_consumed_finished.load(std::memory_order_acquire) ||
                    g_p99_consumed_failed.load(std::memory_order_acquire)));
            boot_log_line(line);
            if (g_p99_consumed_failed.load(std::memory_order_acquire) ||
                g_p99_consumed_finished.load(std::memory_order_acquire))
                g_p99_consumed_ready.store(false, std::memory_order_release);
            return 0;
        }
        if (phase == P99ConsumedArmPhase::Stopped ||
            g_p99_consumed_failed.load(std::memory_order_acquire) ||
            g_p99_consumed_finished.load(std::memory_order_acquire)) {
            p99_consumed_arm_stop("draw-wait-terminal"); return 0;
        }
        const auto tick = GetTickCount64();
        if (tick < after_log || tick - g_p99_consumed_arm_wait_ms >= kP99ConsumedArmWaitMs) {
            // Race with an immediate observed-draw publication: do not stop an
            // already armed 120s census merely because the 300s request wait ends.
            auto current = g_p99_consumed_arm_phase.load(std::memory_order_acquire);
            if (current == P99ConsumedArmPhase::Armed) continue;
            if (!g_p99_consumed_arm_phase.compare_exchange_strong(current,
                    P99ConsumedArmPhase::Stopped, std::memory_order_acq_rel)) continue;
            p99_consumed_arm_stop("draw-timeout-or-clock"); return 0;
        }
        Sleep(10); // This worker polls owned atomics only; never blocks the game.
    }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_p99_consumed_ready.store(false, std::memory_order_release);
        g_p99_consumed_finished.store(true, std::memory_order_release);
    }
    return 0;
}

bool start_p99_consumed_arm_wait() {
    if (!kEnableP99ConsumedCensus ||
        !g_p99_consumed_installed.load(std::memory_order_acquire) ||
        g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire) ||
        g_p99_consumed_arm_wait_started.exchange(true, std::memory_order_acq_rel)) return false;
    std::uint64_t creation = 0;
    if (!coop::p99_observation_arm::creation_stamp(GetCurrentProcess(), creation)) {
        p99_consumed_arm_stop("arm-process-identity");
        return false;
    }
    const auto name = coop::p99_observation_arm::census_event_name(GetCurrentProcessId(), creation);
    SetLastError(ERROR_SUCCESS);
    const auto event = CreateEventW(nullptr, FALSE, FALSE, name.c_str());
    const auto error = GetLastError();
    if (event == nullptr || error == ERROR_ALREADY_EXISTS) {
        if (event != nullptr) CloseHandle(event);
        p99_consumed_arm_stop("arm-event-create-or-stale");
        return false;
    }
    g_p99_consumed_arm_wait_ms = GetTickCount64();
    char line[256]{};
    std::snprintf(line, sizeof(line),
        "P99-CONSUMED ARM-WAIT pid=%lu creation=%llu timeout_ms=%lu; protocol=request-v2; probes=0; stock-only until request AND fresh observed P99 draw",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(creation), static_cast<unsigned long>(kP99ConsumedArmWaitMs));
    boot_log_line(line);
    const auto worker = CreateThread(nullptr, 0, &await_p99_consumed_arm, event, 0, nullptr);
    if (worker == nullptr) {
        CloseHandle(event);
        p99_consumed_arm_stop("arm-thread-create");
        return false;
    }
    CloseHandle(worker); // Process lifetime state; no join on the animation path.
    return true;
}
