// Included after consumed_runtime. Only a fresh explicit PID/creation-bound
// signal enables sampling. No game pointers, owner reads, or policy resets.
std::atomic<bool> g_p99_consumed_arm_wait_started{false};
std::atomic<bool> g_p99_consumed_arm_published{false};
std::uint64_t g_p99_consumed_arm_wait_ms = 0; // Set before worker creation.
constexpr DWORD kP99ConsumedArmWaitMs = 300000;

void p99_consumed_arm_stop(const char* reason) {
    g_p99_consumed_ready.store(false, std::memory_order_release);
    g_p99_consumed_finished.store(true, std::memory_order_release);
    char line[256]{};
    std::snprintf(line, sizeof(line),
        "P99-CONSUMED ARM-STOP reason=%s; stock-only; no rearm; pose_writes=0", reason);
    boot_log_line(line);
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
        g_p99_consumed_fiber.failed() ||
        g_p99_consumed_arm_published.exchange(true, std::memory_order_acq_rel)) {
        p99_consumed_arm_stop("arm-refused");
        return 0;
    }
    g_p99_consumed_arm_ms = now;
    // Marker precedes publication: even the first callback cannot log before
    // ARMED. This grants time, never owner/weapon/animation acceptance.
    boot_log_line("P99-CONSUMED ARMED explicit signal; 120s from arm; 8 samples/600 probes; combined 1ms cap; pose_writes=0");
    if (g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire)) return 0;
    g_p99_consumed_ready.store(true, std::memory_order_release);
    // Readiness cannot clear terminal flags; the outer independently refuses
    // terminal state. Already in-flight native/capture work is not cancelled.
    if (g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire))
        g_p99_consumed_ready.store(false, std::memory_order_release);
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
        "P99-CONSUMED ARM-WAIT pid=%lu creation=%llu timeout_ms=%lu; probes=0; stock-only; no automatic arm",
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
