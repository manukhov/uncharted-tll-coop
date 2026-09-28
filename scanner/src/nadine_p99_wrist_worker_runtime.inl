// Included after the route. OFF-THREAD lifecycle only, currently unreachable:
// no installer sets installed and kEnableP99WristObservation remains false.
std::atomic<bool> g_p99_wrist_installed{false};
std::atomic<bool> g_p99_wrist_worker_started{false};
struct P99WristWorkerSetup {
    HANDLE arm_event = nullptr;
    HMODULE module_reference = nullptr;
    std::uint64_t creation = 0, frequency = 0;
    DWORD pid = 0;
    wchar_t output_path[1024]{};
};
P99WristWorkerSetup g_p99_wrist_worker_setup{};
constexpr DWORD kP99WristArmWaitMs = 300000, kP99WristPollMs = 10;
constexpr std::uint64_t kP99WristDrainMs = 5000;

bool p99_wrist_export(const P99WristWorkerSetup& setup,
    const coop::p99_wrist_report::Record& result, std::uint64_t arm_ms) {
    const auto bytes = coop::p99_wrist_report::json(result, setup.pid,
        setup.creation, arm_ms, setup.frequency);
    // No clobber of an earlier run/result. A failed partial write is reported,
    // never labelled as a successful capture. Only the worker owns this handle.
    const auto file = CreateFileW(setup.output_path, GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool okay = bytes.size() <= MAXDWORD &&
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size();
    const bool closed = CloseHandle(file) != FALSE;
    return okay && closed;
}

// Kept separate from the thread/module-reference exit for owned fixture tests.
DWORD p99_wrist_worker_body(P99WristWorkerSetup& setup) noexcept {
    std::uint64_t arm_ms = 0;
    try {
        const auto waited = WaitForSingleObject(setup.arm_event, kP99WristArmWaitMs);
        CloseHandle(setup.arm_event); setup.arm_event = nullptr;
        coop::p99_wrist_report::Record result{};
        if (waited != WAIT_OBJECT_0) {
            // Still unarmed: there is no callback lease to steal here.
            result.reason = g_p99_wrist_policy.abort(waited == WAIT_TIMEOUT ?
                p99_observe::Reason::kExpired : p99_observe::Reason::kException);
            (void)g_p99_wrist_report.publish(result);
            boot_log_line(waited == WAIT_TIMEOUT ? "P99-WRIST arm-timeout; probes=0; writes=0" :
                "P99-WRIST arm-wait-failed; probes=0; writes=0");
        } else {
            arm_ms = GetTickCount64();
            if (!g_p99_wrist_installed.load(std::memory_order_acquire) ||
                !g_p99_wrist_policy.arm(arm_ms, setup.frequency)) {
                result.reason = g_p99_wrist_policy.abort(p99_observe::Reason::kProtocol);
                (void)g_p99_wrist_report.publish(result);
            } else {
                // immutable cost_limit was initialized before worker creation.
                boot_log_line("P99-WRIST ARMED; one owned post16 attempt; 120s lifetime; 1ms split overhead limit; game-data-writes=0");
                g_p99_wrist_ready.store(true, std::memory_order_release);
                while (!g_p99_wrist_report.read(result)) {
                    const auto now = GetTickCount64();
                    (void)p99_wrist_poll_expiry();
                    if (g_p99_wrist_report.read(result)) break;
                    if (now < arm_ms || now - arm_ms >= p99_observe::kLifetimeMs + kP99WristDrainMs) {
                        // Bounded worker, not preemption. A still-active owner
                        // will reject after stock returns. Do not borrow its
                        // reason, read its private data or manufacture a report.
                        g_p99_wrist_ready.store(false, std::memory_order_release);
                        boot_log_line("P99-WRIST export-unavailable: active owner did not retire before worker drain deadline; no payload exported");
                        return 2;
                    }
                    Sleep(kP99WristPollMs);
                }
            }
        }
        g_p99_wrist_ready.store(false, std::memory_order_release);
        if (!g_p99_wrist_report.read(result) || !p99_wrist_export(setup, result, arm_ms)) {
            boot_log_line("P99-WRIST export-failed; no complete result file confirmed");
            return 3;
        }
        boot_log_line("P99-WRIST report saved off-thread; inspect reason/payload_bytes; source and final-render admission remain false");
        return 0;
    } catch (...) {
        if (setup.arm_event) { CloseHandle(setup.arm_event); setup.arm_event = nullptr; }
        g_p99_wrist_ready.store(false, std::memory_order_release);
        boot_log_line("P99-WRIST worker exception; no successful export claimed");
        return 4;
    }
}

DWORD WINAPI await_p99_wrist_arm(void* argument) {
    auto& setup = *static_cast<P99WristWorkerSetup*>(argument);
    const auto result = p99_wrist_worker_body(setup);
    FreeLibraryAndExitThread(setup.module_reference, result);
}

bool start_p99_wrist_worker(std::uint64_t qpc_frequency) {
    if (!kEnableP99WristObservation || !g_p99_wrist_installed.load(std::memory_order_acquire) ||
        g_p99_wrist_worker_started.exchange(true, std::memory_order_acq_rel)) return false;
    auto& setup = g_p99_wrist_worker_setup;
    setup.pid = GetCurrentProcessId(); setup.frequency = qpc_frequency;
    if (qpc_frequency < 1000 ||
        !coop::p99_observation_arm::creation_stamp(GetCurrentProcess(), setup.creation)) return false;
    g_p99_wrist_cost_limit = coop::p99_group_budget::one_millisecond(qpc_frequency);
    if (swprintf_s(setup.output_path,
        L"C:\\Users\\Vladimir\\Desktop\\TLLCoopResearch\\reports\\p99-wrist-%lu-%llu.json",
        static_cast<unsigned long>(setup.pid), static_cast<unsigned long long>(setup.creation)) < 0) return false;
    std::wstring name;
    try { name = coop::p99_observation_arm::wrist_event_name(setup.pid, setup.creation); }
    catch (...) { return false; } // No OS handles acquired yet.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&await_p99_wrist_arm), &setup.module_reference)) return false;
    SetLastError(ERROR_SUCCESS);
    setup.arm_event = CreateEventW(nullptr, FALSE, FALSE, name.c_str());
    const auto error = GetLastError();
    if (!setup.arm_event || error == ERROR_ALREADY_EXISTS) {
        if (setup.arm_event) CloseHandle(setup.arm_event);
        setup.arm_event = nullptr; FreeLibrary(setup.module_reference); return false;
    }
    const auto thread = CreateThread(nullptr, 0, &await_p99_wrist_arm, &setup, 0, nullptr);
    if (!thread) {
        CloseHandle(setup.arm_event); setup.arm_event = nullptr;
        FreeLibrary(setup.module_reference); return false;
    }
    CloseHandle(thread);
    char line[256]{};
    std::snprintf(line, sizeof(line), "P99-WRIST ARM-WAIT pid=%lu creation=%llu; separate wrist event; no automatic arm",
        static_cast<unsigned long>(setup.pid), static_cast<unsigned long long>(setup.creation));
    boot_log_line(line);
    return true;
}
