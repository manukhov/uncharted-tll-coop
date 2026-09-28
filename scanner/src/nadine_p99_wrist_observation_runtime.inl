// Read-only integration route, intentionally NOT installed/armed by any build.
// Included after wrist_read_runtime.inl. No old writer/preparation authority,
// log/file/network I/O, heap allocation or game-buffer substitution here.
// Owned production-data fault tests exist, not live timing/lifetime evidence.
// Live admission still needs pinned installer preflight, external arm/expiry
// and off-thread owned-result export.
namespace p99_wrist_call = coop::p99_wrist_invoke;
namespace p99_process_frame = coop::p99_process_native_frame;

struct P99WristJob {
    void* native_job = nullptr; // Opaque identity; fresh readers at each phase.
    P99WristOwner owner{};
    coop::p99_group_budget::Budget cost{};
    p99_wrist_call::State invocation{};
    p99_wrist_snapshot::Result result{};
    P99WristReadDiagnostic diagnostic{};
    bool refused = false;
    bool process_seen = false;
};
struct P99WristProcess {
    P99FiberFrame* frame = nullptr;
    P99WristJob* job = nullptr;
    P99WristProcessValues values{};
    std::uintptr_t native_return_slot = 0;
    std::uintptr_t status = 0;
    bool consumed = false;
    bool target = false;
};
struct P99WristPending {
    p99_wrist_snapshot::Payload payload{};
    coop::p99_group_budget::Budget cost{};
    P99CostStamp finish_begin{};
    P99WristReadDiagnostic diagnostic{};
    bool leased = false;
    bool captured = false;
    bool terminal_report = false;
};

// Separate value-only policy; AFE's selected/prepared state cannot authorize
// this route. Ready is never set and this payload has no runtime consumer yet.
p99_observe::Policy g_p99_wrist_policy;
std::atomic<bool> g_p99_wrist_ready{false};
std::atomic<bool> g_p99_wrist_payload_ready{false};
p99_wrist_snapshot::Payload g_p99_wrist_payload{};
P99WristReadDiagnostic g_p99_wrist_diagnostic{};
std::atomic<bool> g_p99_wrist_diagnostic_ready{false};
std::uint64_t g_p99_wrist_cost_limit = 0;

bool p99_wrist_charge(P99WristJob& job, P99CostStamp begin) noexcept {
    const auto end = p99_group_cost_start();
    const bool clock_ok = begin.valid && end.valid &&
        end.value.QuadPart >= begin.value.QuadPart;
    if (job.cost.charge(static_cast<std::uint64_t>(begin.value.QuadPart),
            static_cast<std::uint64_t>(end.value.QuadPart), clock_ok)) return true;
    job.refused = true;
    (void)g_p99_wrist_policy.abort(clock_ok ? p99_observe::Reason::kBudget
                                          : p99_observe::Reason::kClock);
    g_p99_wrist_ready.store(false, std::memory_order_release);
    return false;
}

bool p99_wrist_live(P99FiberFrame& frame, const P99WristJob& job) noexcept {
    return g_p99_wrist_ready.load(std::memory_order_acquire) &&
        g_p99_wrist_policy.reason() == p99_observe::Reason::kNone &&
        g_state_net.enabled && g_net_role_b && !job.refused &&
        !frame.entry.reentered && frame.wrist == &job &&
        g_p99_group_fiber.owns(frame);
}

bool __fastcall p99_wrist_post16(void* cookie, const void* segment) {
    auto& process = *static_cast<P99WristProcess*>(cookie);
    auto& frame = *process.frame;
    auto& job = *process.job;
    const auto begin = p99_group_cost_start();
    p99_wrist_snapshot::Result staged{};
    const bool accepted = begin.valid && p99_wrist_live(frame, job) &&
        frame.wrist_process == &process && process.consumed &&
        p99_process_frame::owns(process.status,
            reinterpret_cast<std::uintptr_t>(segment), process.native_return_slot) &&
        coop::p99_group_native_frame::owns(true, process.values.parent,
            frame.native_return_slot) &&
        seh_p99_wrist_snapshot(job.native_job, job.owner, process.values, segment, &staged,
            nullptr, &job.diagnostic);
    const bool within_cost = p99_wrist_charge(job, begin);
    if (!accepted || !within_cost || !p99_wrist_live(frame, job) ||
        frame.wrist_process != &process) {
        job.refused = true;
        return false;
    }
    job.result = staged; // Private job-owned value, not global publication.
    return true;
}

// This is the actual TWO-argument adapter. The six-argument invoke helper is
// called normally, never installed as if it matched the game's call signature.
std::uint32_t* __fastcall hooked_p99_wrist_cmd16(std::uint32_t* status, const void* segment) {
    const auto stock = reinterpret_cast<p99_wrist_call::Native>(
        g_module + kAnimCmdExecuteJointProgramsRva);
    auto* frame = g_p99_group_fiber.current();
    if (frame == nullptr || frame->wrist == nullptr) return stock(status, segment);
    auto& job = *frame->wrist;
    auto* process = frame->wrist_process;
    const DWORD entry_error = GetLastError();
    const auto begin = p99_group_cost_start();
    const bool admitted = begin.valid && process != nullptr && process->job == &job &&
        p99_wrist_live(*frame, job) && !process->consumed &&
        (!process->target || job.invocation.phase == p99_wrist_call::Phase::ready) &&
        coop::p99_group_native_frame::owns(process->target, process->values.parent,
            frame->native_return_slot) &&
        p99_process_frame::owns(reinterpret_cast<std::uintptr_t>(status),
            reinterpret_cast<std::uintptr_t>(segment), process->native_return_slot);
    const bool within_cost = p99_wrist_charge(job, begin);
    if (!admitted || !within_cost) {
        job.refused = true;
        if (job.invocation.active) job.invocation.phase = p99_wrist_call::Phase::reentered;
        SetLastError(entry_error);
        return stock(status, segment); // Original args, once even on refusal.
    }
    process->consumed = true;
    process->status = reinterpret_cast<std::uintptr_t>(status);
    SetLastError(entry_error);
    // Groups1..7 are expected stock calls in the same Async invocation, not
    // missing/foreign g0 capture. They carry their own Process return-slot too.
    if (!process->target) return stock(status, segment);
    // Stock faults propagate from invoke; there is no surrounding __except or
    // retry. Its contained callback can fill only the unpublished job result.
    return p99_wrist_call::invoke(job.invocation, stock, status, segment,
        &p99_wrist_post16, process);
}

std::uint32_t p99_wrist_process_call(bool first_site, const std::uint32_t* commands,
    std::uint32_t words, const void* context, std::uint32_t group,
    const void* a5, const void* a6, const void* a7) {
    const auto stock = reinterpret_cast<p99_process_frame::Native>(
        g_module + kProcessAnimCmdListSegmentRva);
    auto* frame = g_p99_group_fiber.current();
    if (frame == nullptr || frame->wrist == nullptr)
        return stock(commands, words, context, group, a5, a6, a7);
    auto& job = *frame->wrist;
    auto* previous = frame->wrist_process;
    const DWORD entry_error = GetLastError();
    const auto begin = p99_group_cost_start();
    P99WristProcess process{};
    std::uint32_t result = 0;
    DWORD native_error = entry_error;
    bool returned = false;
    P99CostStamp completion_begin{};
    __try {
        // Hide parent authority throughout EVERY recursive/foreign Process.
        frame->wrist_process = nullptr;
        const bool same = coop::p99_group_native_frame::owns(first_site,
            reinterpret_cast<std::uintptr_t>(context), frame->native_return_slot);
        if (!same || previous != nullptr || group >= 8 || first_site != (group == 0)) {
            frame->entry.reentered = true;
            job.refused = true;
        }
        if (first_site && group == 0 && p99_wrist_live(*frame, job)) {
            const bool first = !job.process_seen;
            job.process_seen = true;
            const bool admitted = first && begin.valid &&
                seh_p99_wrist_process(commands, words, context, group, a6,
                    job.owner, &process.values, nullptr, &job.diagnostic);
            if (!admitted) job.refused = true;
            else {
                process.frame = frame;
                process.job = &job;
                process.target = true;
                frame->wrist_process = &process;
            }
        } else if (!first_site && group > 0 && group < 8 && p99_wrist_live(*frame, job)) {
            process.frame = frame;
            process.job = &job;
            process.values.parent = reinterpret_cast<std::uintptr_t>(context);
            frame->wrist_process = &process;
        }
        const bool within_cost = p99_wrist_charge(job, begin);
        if (!within_cost || !p99_wrist_live(*frame, job)) frame->wrist_process = nullptr;
        SetLastError(entry_error);
        if (frame->wrist_process == &process) {
            result = p99_process_frame::p99_process_invoke_native(commands, words,
                context, group, a5, a6, a7, stock, &process.native_return_slot);
        } else result = stock(commands, words, context, group, a5, a6, a7);
        returned = true;
        native_error = GetLastError();
        completion_begin = p99_group_cost_start();
    } __finally {
        // Both native frames expire even during SEH unwind. A fault in stock
        // cancels the whole owned result, including a previously copied slice.
        process.native_return_slot = 0;
        frame->wrist_process = previous;
        if (!returned || (process.target && (!process.consumed ||
                job.invocation.phase != p99_wrist_call::Phase::captured))) job.refused = true;
    }
    (void)p99_wrist_charge(job, completion_begin);
    SetLastError(native_error);
    return result;
}
std::uint32_t __fastcall hooked_p99_wrist_process0(const std::uint32_t* c,
    std::uint32_t n, const void* p, std::uint32_t g, const void* a5, const void* a6, const void* a7) {
    return p99_wrist_process_call(true, c, n, p, g, a5, a6, a7);
}
std::uint32_t __fastcall hooked_p99_wrist_process1(const std::uint32_t* c,
    std::uint32_t n, const void* p, std::uint32_t g, const void* a5, const void* a6, const void* a7) {
    return p99_wrist_process_call(false, c, n, p, g, a5, a6, a7);
}

__declspec(noinline) void p99_wrist_candidate(void* native_job,
    P99FiberFrame& frame, P99WristPending& pending, P99CostStamp preparation_begin) {
    const auto stock = reinterpret_cast<coop::p99_group_native_frame::Native>(
        g_module + kAnimManagerAsyncObjectUpdateJointBlendJobRva);
    const DWORD entry_error = GetLastError();
    if (g_p99_wrist_policy.begin_probe(GetTickCount64()) != p99_observe::Probe::kGranted) {
        if (g_p99_wrist_policy.reason() != p99_observe::Reason::kNone)
            g_p99_wrist_ready.store(false, std::memory_order_release);
        SetLastError(entry_error);
        stock(native_job);
        return;
    }
    bool leased = true;
    P99WristJob job{};
    job.native_job = native_job;
    job.cost.limit = g_p99_wrist_cost_limit;
    bool returned = false;
    DWORD native_error = entry_error;
    __try {
        const auto begin = preparation_begin; // Includes frame/job initialization.
        const bool selected = begin.valid &&
            seh_p99_wrist_owner(native_job, &job.owner, nullptr, &job.diagnostic) &&
            g_p99_group_fiber.owns(frame) && !frame.entry.reentered;
        const bool within_cost = p99_wrist_charge(job, begin);
        pending.diagnostic = job.diagnostic;
        if (within_cost) {
            if (g_p99_wrist_policy.finish_probe(GetTickCount64(), selected)) {
                pending.leased = true; // Outer wrapper owns final policy/commit.
                pending.terminal_report = true;
                frame.wrist = &job;
            }
            leased = false; // Either transferred or nonselected probe closed.
            if (g_p99_wrist_policy.reason() != p99_observe::Reason::kNone)
                g_p99_wrist_ready.store(false, std::memory_order_release);
        }
        // Only the still-owned (aborted before selection) lease may publish a
        // terminal diagnostic here. After finish_probe(false) another thread
        // can already own/finish a new probe; its reason is NOT ours to report.
        if (leased) pending.terminal_report = true;
        SetLastError(entry_error);
        coop::p99_group_native_frame::p99_group_invoke_native(native_job, stock,
            &frame.native_return_slot);
        returned = true;
        native_error = GetLastError();
        if (pending.leased) {
            pending.finish_begin = p99_group_cost_start();
            pending.captured = p99_wrist_live(frame, job) && job.process_seen &&
                job.invocation.phase == p99_wrist_call::Phase::captured && job.result.copied();
            if (pending.captured) pending.payload = job.result.payload;
            pending.cost = job.cost;
            pending.diagnostic = job.diagnostic;
        }
    } __finally {
        frame.native_return_slot = 0;
        frame.wrist_process = nullptr;
        frame.wrist = nullptr;
        if (leased || (!returned && pending.leased)) {
            pending.captured = false;
            (void)g_p99_wrist_policy.abort();
            g_p99_wrist_ready.store(false, std::memory_order_release);
        }
    }
    SetLastError(native_error);
}

void __fastcall hooked_p99_wrist_async(void* native_job) {
    const auto stock = reinterpret_cast<coop::p99_group_native_frame::Native>(
        g_module + kAnimManagerAsyncObjectUpdateJointBlendJobRva);
    // current() also returns null before installation and after FLS poison.
    auto* active = g_p99_group_fiber.current();
    if (!g_p99_wrist_ready.load(std::memory_order_acquire) && active == nullptr) {
        stock(native_job);
        return;
    }
    const DWORD entry_error = GetLastError();
    const auto preparation_begin = p99_group_cost_start();
    P99FiberFrame frame{};
    P99FiberFrame* previous = nullptr;
    if (!g_p99_group_fiber.enter(frame, previous)) {
        // Poison hides any parent pointer before this stock-only fallback.
        g_p99_wrist_ready.store(false, std::memory_order_release);
        SetLastError(entry_error);
        stock(native_job);
        return;
    }
    P99WristPending pending{};
    bool returned = false;
    bool restored = false;
    DWORD native_error = entry_error;
    __try {
        if (previous != nullptr) {
            previous->entry.reentered = true;
            if (previous->wrist != nullptr) previous->wrist->refused = true;
        }
        const bool eligible = previous == nullptr && native_job != nullptr &&
            g_module != 0 && g_state_net.enabled && g_net_role_b &&
            g_p99_wrist_ready.load(std::memory_order_acquire);
        const bool candidate = coop::p99_group_entry::admit(eligible,
            [&](coop::p99_group_entry::Observations& seen) {
                return seh_p99_group_entry_observations(native_job, &seen);
            }, [] { return true; });
        SetLastError(entry_error);
        if (candidate) p99_wrist_candidate(native_job, frame, pending, preparation_begin);
        else stock(native_job);
        returned = true;
        native_error = GetLastError();
    } __finally {
        frame.native_return_slot = 0;
        frame.wrist_process = nullptr;
        frame.wrist = nullptr;
        restored = g_p99_group_fiber.leave(frame, previous);
        if (pending.leased && (!returned || !restored)) {
            pending.captured = false;
            (void)g_p99_wrist_policy.abort();
        }
    }
    if (pending.leased) {
        // Includes copying the private value and expiring/restoring FLS after
        // Async, but never charges the intervening native Async wall time.
        const auto end = p99_group_cost_start();
        const bool clock_ok = pending.finish_begin.valid && end.valid &&
            end.value.QuadPart >= pending.finish_begin.value.QuadPart;
        if (!pending.cost.charge(static_cast<std::uint64_t>(pending.finish_begin.value.QuadPart),
                static_cast<std::uint64_t>(end.value.QuadPart), clock_ok)) {
            pending.captured = false;
            (void)g_p99_wrist_policy.abort(clock_ok ? p99_observe::Reason::kBudget
                                                   : p99_observe::Reason::kClock);
        }
        const auto reason = g_p99_wrist_policy.finish_observation(GetTickCount64(),
            pending.cost, pending.captured && restored && !frame.entry.reentered &&
                g_p99_wrist_ready.load(std::memory_order_acquire) &&
                !g_p99_group_fiber.failed() && g_state_net.enabled && g_net_role_b);
        if (reason == p99_observe::Reason::kSuccess) {
            g_p99_wrist_payload = pending.payload;
            g_p99_wrist_payload_ready.store(true, std::memory_order_release);
        }
        g_p99_wrist_ready.store(false, std::memory_order_release);
    }
    // Own terminal probe/attempt only; never publish from a nonselected probe
    // after its lease was handed to another job. No partial payload is logged
    // or exported here. An off-thread consumer may read this scalar value only
    // after the release flag; the policy is terminal, so no future writer exists.
    if (pending.terminal_report && g_p99_wrist_policy.reason() != p99_observe::Reason::kNone) {
        g_p99_wrist_diagnostic = pending.diagnostic;
        g_p99_wrist_diagnostic_ready.store(true, std::memory_order_release);
    }
    SetLastError(native_error);
}
