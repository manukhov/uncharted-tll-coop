// Include after p99_consumed_log and before the outer wrapper. Successful
// publication transfers the existing capture lease; neither capture buffers nor
// any game pointer are read by the worker except as already-owned value bytes.
// Event/thread/FLS/DLL lifetime is process-long after successful initialization.
HANDLE g_p99_consumed_log_event = nullptr;
HANDLE g_p99_consumed_log_thread = nullptr;
std::atomic_flag g_p99_consumed_log_init_started = ATOMIC_FLAG_INIT;
std::atomic<bool> g_p99_consumed_log_initialized{false};
std::atomic<bool> g_p99_consumed_log_pending{false};
P99ConsumedFrame g_p99_consumed_log_frame{};

void p99_consumed_log_stop() {
    g_p99_consumed_failed.store(true, std::memory_order_release);
    g_p99_consumed_finished.store(true, std::memory_order_release);
}

DWORD WINAPI p99_consumed_log_worker(void*) {
    for (;;) {
        const DWORD waited = WaitForSingleObject(g_p99_consumed_log_event, INFINITE);
        if (waited != WAIT_OBJECT_0) {
            p99_consumed_log_stop();
            // Only an outstanding publication can transfer the capture lease.
            if (g_p99_consumed_log_pending.exchange(false, std::memory_order_acq_rel))
                g_p99_consumed_lease.clear(std::memory_order_release);
            return 0;
        }
        if (!g_p99_consumed_log_pending.exchange(false, std::memory_order_acq_rel))
            continue;
        bool faulted = false;
        __try {
            __try {
                // This consumes only copied commands/clip fields/arm rows and
                // inert metadata. No resource/owner/frame pointer is followed.
                p99_consumed_log(g_p99_consumed_log_frame);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                faulted = true;
            }
        } __finally {
            if (faulted) p99_consumed_log_stop();
            g_p99_consumed_lease.clear(std::memory_order_release);
        }
        if (faulted) return 0;
    }
}

bool initialize_p99_consumed_log_worker() {
    // The installer invokes this before publishing any retail patch. A failed
    // creation is not retried; no successful worker resources are reclaimed.
    if (g_p99_consumed_log_init_started.test_and_set(std::memory_order_acq_rel))
        return g_p99_consumed_log_initialized.load(std::memory_order_acquire);
    g_p99_consumed_log_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_p99_consumed_log_event) { p99_consumed_log_stop(); return false; }
    g_p99_consumed_log_thread = CreateThread(nullptr, 0, &p99_consumed_log_worker,
        nullptr, 0, nullptr);
    if (!g_p99_consumed_log_thread) {
        CloseHandle(g_p99_consumed_log_event);
        g_p99_consumed_log_event = nullptr;
        p99_consumed_log_stop();
        return false;
    }
    g_p99_consumed_log_initialized.store(true, std::memory_order_release);
    return true;
}

bool publish_p99_consumed_log(const P99ConsumedFrame& frame) {
    // Caller holds the capture lease and must clear it on false, but MUST NOT
    // clear it on true. pending=false does not mean drained: wait for lease.
    if (!g_p99_consumed_log_initialized.load(std::memory_order_acquire) ||
        g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_log_pending.load(std::memory_order_acquire)) {
        p99_consumed_log_stop();
        return false;
    }
    g_p99_consumed_log_frame = frame;
    g_p99_consumed_log_frame.identity = {};
    g_p99_consumed_log_frame.active = false;
    g_p99_consumed_log_frame.native_return_slot = 0;
    g_p99_consumed_log_pending.store(true, std::memory_order_release);
    // This private auto-reset event is initially unsignaled, is signaled only
    // here, and its successful resources are never closed/reused. A failure
    // therefore leaves publication untransferred; no worker can consume it.
    if (!SetEvent(g_p99_consumed_log_event)) {
        g_p99_consumed_log_pending.store(false, std::memory_order_release);
        p99_consumed_log_stop();
        return false;
    }
    return true;
}
