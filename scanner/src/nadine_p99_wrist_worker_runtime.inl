// Included after the route. OFF-THREAD lifecycle only, currently unreachable:
// no installer sets installed and kEnableP99WristObservation remains false.
std::atomic<bool> g_p99_wrist_installed{false};
// One consumed startup attempt, including a pre-thread refusal. It is NOT a
// statement that a thread exists, and a failure must never silently retry.
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
        // Startup logging belongs to the worker, never the installer/callback.
        char line[256]{};
        std::snprintf(line, sizeof(line), "P99-WRIST ARM-WAIT pid=%lu creation=%llu; separate wrist event; no automatic arm",
            static_cast<unsigned long>(setup.pid), static_cast<unsigned long long>(setup.creation));
        boot_log_line(line);
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
        // Logging itself may have caused the exception. Do not let a second
        // logging exception escape this noexcept worker and skip DLL release.
        try { boot_log_line("P99-WRIST worker exception; no successful export claimed"); }
        catch (...) {}
        return 4;
    }
}

DWORD WINAPI await_p99_wrist_arm(void* argument) {
    auto& setup = *static_cast<P99WristWorkerSetup*>(argument);
    const auto result = p99_wrist_worker_body(setup);
    const auto reference = setup.module_reference;
    setup.module_reference = nullptr;
    FreeLibraryAndExitThread(reference, result);
}

bool p99_wrist_absolute_directory(const std::wstring& path) noexcept {
    if (path.size() >= 3 &&
        ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) return true;
    // Ordinary UNC server/share, not device namespaces or root-relative paths.
    if (path.size() < 5 || path[0] != L'\\' || path[1] != L'\\' ||
        path[2] == L'?' || path[2] == L'.') return false;
    const auto slash = path.find(L'\\', 2);
    return slash != std::wstring::npos && slash > 2 && slash + 1 < path.size() &&
        path[slash + 1] != L'\\';
}

// Caller supplies an existing absolute directory; no implicit game directory,
// hardcoded workstation path, directory creation or pre-created report file.
// CREATE_NEW in the worker remains the final no-clobber authority after this
// advisory preflight. A refused startup consumes its one-shot latch.
bool start_p99_wrist_worker(std::uint64_t qpc_frequency, const wchar_t* output_directory) {
    if (!kEnableP99WristObservation || !g_p99_wrist_installed.load(std::memory_order_acquire) ||
        g_p99_wrist_worker_started.exchange(true, std::memory_order_acq_rel)) return false;
    g_p99_wrist_worker_setup = {};
    g_p99_wrist_cost_limit = 0;
    P99WristWorkerSetup setup{};
    bool transferred = false;
    const auto refuse = [&]() noexcept {
        if (setup.arm_event) { CloseHandle(setup.arm_event); setup.arm_event = nullptr; }
        if (setup.module_reference) { FreeLibrary(setup.module_reference); setup.module_reference = nullptr; }
        g_p99_wrist_worker_setup = {};
        g_p99_wrist_cost_limit = 0;
        return false;
    };
    try {
        if (qpc_frequency < 1000 || output_directory == nullptr) return refuse();
        setup.pid = GetCurrentProcessId(); setup.frequency = qpc_frequency;
        if (setup.pid == 0 || !coop::p99_observation_arm::creation_stamp(GetCurrentProcess(), setup.creation) ||
            setup.creation == 0) return refuse();
        const std::wstring directory(output_directory);
        if (!p99_wrist_absolute_directory(directory)) return refuse();
        const auto directory_attributes = GetFileAttributesW(directory.c_str());
        if (directory_attributes == INVALID_FILE_ATTRIBUTES ||
            (directory_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return refuse();
        auto output = directory;
        if (output.back() != L'\\' && output.back() != L'/') output += L'\\';
        output += L"p99-wrist-" + std::to_wstring(setup.pid) + L"-" +
            std::to_wstring(setup.creation) + L".json";
        if (output.size() >= sizeof(setup.output_path) / sizeof(setup.output_path[0])) return refuse();
        SetLastError(ERROR_SUCCESS);
        const auto output_attributes = GetFileAttributesW(output.c_str());
        if (output_attributes != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND) return refuse();
        std::memcpy(setup.output_path, output.c_str(), (output.size() + 1) * sizeof(wchar_t));
        const auto name = coop::p99_observation_arm::wrist_event_name(setup.pid, setup.creation);
        HMODULE reference = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&await_p99_wrist_arm), &reference)) return refuse();
        setup.module_reference = reference;
        SetLastError(ERROR_SUCCESS);
        setup.arm_event = CreateEventW(nullptr, FALSE, FALSE, name.c_str());
        const auto error = GetLastError();
        if (!setup.arm_event || error == ERROR_ALREADY_EXISTS) return refuse();
        g_p99_wrist_worker_setup = setup;
        g_p99_wrist_cost_limit = coop::p99_group_budget::one_millisecond(qpc_frequency);
        const auto thread = CreateThread(nullptr, 0, &await_p99_wrist_arm, &g_p99_wrist_worker_setup, 0, nullptr);
        if (!thread) return refuse();
        transferred = true;
        // Successful creation TRANSFERS the event and DLL reference to the
        // worker, which may already be running. No further allocations/logging
        // or failing operation may enter the pre-thread cleanup path.
        (void)CloseHandle(thread);
        return true;
    } catch (...) {
        // Keep this barrier even if later maintenance adds a throwing operation
        // after creation. A live worker's resources can never be rolled back.
        return transferred ? true : refuse();
    }
}
