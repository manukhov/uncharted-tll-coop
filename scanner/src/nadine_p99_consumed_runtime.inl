// Include inside coop_runtime.cpp's anonymous namespace, AFTER consumed_owner.
// Headers consumed_commands/frame, group_fiber/native_frame and
// process_native_frame must have been included outside the namespace.
#if defined(TLL_P99_CONSUMED_RUNTIME_TEST) || defined(TLL_P99_CONSUMED_CENSUS_EXAM)
constexpr bool kEnableP99ConsumedCensus = true; // Explicit isolated test/exam build only.
#else
constexpr bool kEnableP99ConsumedCensus = false;
#endif
std::atomic<bool> g_p99_consumed_installed{false}, g_p99_consumed_ready{false};
std::uint64_t g_p99_consumed_arm_ms = 0; // Immutable after ready release publication.
std::atomic<bool> g_p99_consumed_failed{false}, g_p99_consumed_finished{false};
coop::p99_consumed_frame::Native g_p99_consumed_outer = nullptr;
coop::p99_process_native_frame::Native g_p99_consumed_process = nullptr;

struct P99ConsumedFrame : coop::p99_group_fiber::Binding {
    P99ConsumedOwner owner{};
    std::uintptr_t native_return_slot = 0;
    std::uint64_t tick = 0, elapsed = 0, end_tick = 0;
    std::uint64_t observer_ticks = 0;
    std::uint64_t probe_ticks = 0, probe_retry_tick = 0;
    unsigned sample = 0, process0_count = 0, post_count = 0;
    unsigned probe = 0, slow_probes = 0;
    P99ConsumedOwnerReject probe_reason = P99ConsumedOwnerReject::None;
    bool ambiguous = false, process_returned = false, post_returned = false;
    bool budget_exceeded = false, clock_valid = true;
    bool probe_only = false, probe_admitted = false, probe_budget = false;
    bool probe_clock = true, probe_terminal = false;
};
static_assert(sizeof(P99ConsumedFrame) < 512);
coop::p99_group_fiber::Slot<P99ConsumedFrame> g_p99_consumed_fiber;
std::atomic_flag g_p99_consumed_lease = ATOMIC_FLAG_INIT;
// Access ONLY while holding lease; no frame pointers survive the invocation.
std::uint64_t g_p99_consumed_next_probe = 0;
std::uint64_t g_p99_consumed_next_sample = 0;
unsigned g_p99_consumed_probes = 0, g_p99_consumed_attempts = 0;
unsigned g_p99_consumed_slow_probes = 0, g_p99_consumed_probe_reports = 0;
std::uint64_t g_p99_consumed_cost_limit = 0; // QPF-derived 1ms, before ready publication.
struct P99ConsumedCostStamp { LARGE_INTEGER value{}; bool valid = false; };
P99ConsumedCostStamp p99_consumed_cost_start() {
    P99ConsumedCostStamp stamp{};
    stamp.valid = QueryPerformanceCounter(&stamp.value) != FALSE && stamp.value.QuadPart >= 0;
    return stamp;
}
bool p99_consumed_charge(P99ConsumedFrame& frame, P99ConsumedCostStamp begin) {
    const auto end = p99_consumed_cost_start();
    frame.clock_valid = frame.clock_valid && begin.valid && end.valid &&
        end.value.QuadPart >= begin.value.QuadPart;
    const auto delta = frame.clock_valid
        ? static_cast<std::uint64_t>(end.value.QuadPart - begin.value.QuadPart) : 0;
    if (!frame.clock_valid || g_p99_consumed_cost_limit == 0 ||
        delta > UINT64_MAX - frame.observer_ticks) frame.budget_exceeded = true;
    else {
        frame.observer_ticks += delta;
        frame.budget_exceeded = frame.budget_exceeded ||
            frame.observer_ticks > g_p99_consumed_cost_limit;
    }
    if (frame.budget_exceeded) g_p99_consumed_finished.store(true, std::memory_order_release);
    return !frame.budget_exceeded;
}
bool p99_consumed_probe_charge(P99ConsumedFrame& frame, P99ConsumedCostStamp begin) {
    const auto end = p99_consumed_cost_start();
    frame.probe_clock = begin.valid && end.valid &&
        end.value.QuadPart >= begin.value.QuadPart && g_p99_consumed_cost_limit != 0;
    if (frame.probe_clock)
        frame.probe_ticks = static_cast<std::uint64_t>(end.value.QuadPart - begin.value.QuadPart);
    frame.probe_budget = !frame.probe_clock || frame.probe_ticks > g_p99_consumed_cost_limit;
    if (!frame.probe_clock) g_p99_consumed_finished.store(true, std::memory_order_release);
    else if (frame.probe_budget) {
        // Only an unadmitted probe gets ONE later retry. An admitted probe
        // already spent its observation budget and must never enter FLS.
        if (frame.probe_admitted || ++g_p99_consumed_slow_probes >= 2 ||
            frame.tick > UINT64_MAX - 2000)
            g_p99_consumed_finished.store(true, std::memory_order_release);
        else {
            g_p99_consumed_next_probe = frame.tick + 2000;
            frame.probe_retry_tick = g_p99_consumed_next_probe;
        }
    }
    frame.slow_probes = g_p99_consumed_slow_probes;
    return !frame.probe_budget;
}
struct P99ConsumedClip {
    std::uint64_t identity = 0, data = 0;
    std::uint32_t id = 0, flags = 0, database = 0, skeleton = 0;
    std::uint16_t samples = 0;
    bool valid = false;
};
struct P99ConsumedCapture {
    std::array<std::uint32_t, 4096> raw{};
    coop::p99_consumed_commands::Snapshot decoded{};
    std::array<P99ConsumedClip, 64> clips{};
    std::array<std::array<std::byte, 48>, 13> pre{}, post{};
    std::uint32_t supplied_words = 0, clip_count = 0;
    bool copied = false, clips_complete = false, clip_limit = false;
    bool pre_valid = false, post_valid = false;
};
P99ConsumedCapture g_p99_consumed_capture; // Large data NEVER lives on a fiber stack.

// Reject-only, a handful of volatile loads before any VQ or owner probe. SEH
// permits refusing stale values; a match is NOT lifetime/owner admission.
bool seh_p99_consumed_local_fg(void* job) {
    __try {
        if (!job || !g_module || !g_state_net.enabled || !g_net_role_b ||
            g_local_reload_level.load(std::memory_order_acquire)) return false;
        const auto handle = *reinterpret_cast<const volatile std::uintptr_t*>(
            g_module + kRetainedSlotTableRva);
        if (!handle) return false;
        const auto player = *reinterpret_cast<const volatile std::uintptr_t*>(handle + 8);
        if (!player) return false;
        const auto fg = *reinterpret_cast<const volatile std::uintptr_t*>(
            player + kPlayerFgAnimDataOffset);
        return fg && fg == *reinterpret_cast<const volatile std::uintptr_t*>(
            static_cast<const std::byte*>(job) + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool p99_consumed_clip_fields(P99ConsumedRead& r, P99ConsumedClip& c) {
    return r.read(c.identity, 0x18, c.id) && r.read(c.identity, 0x24, c.flags) &&
        r.read(c.identity, 0x28, c.data) && c.data &&
        r.read(c.identity, 0x40, c.database) && r.read(c.data, 4, c.skeleton) &&
        r.read(c.data, 0x22, c.samples);
}
bool seh_p99_consumed_clip(std::uint64_t identity, P99ConsumedClip* output) {
    *output = {}; output->identity = identity;
    __try {
        P99ConsumedRead r{};
        P99ConsumedClip a{}, b{}; a.identity = b.identity = identity;
        if (!p99_consumed_clip_fields(r, a) || !p99_consumed_clip_fields(r, b) ||
            a.data != b.data || a.id != b.id || a.flags != b.flags ||
            a.database != b.database || a.skeleton != b.skeleton ||
            a.samples != b.samples) return false;
        b.valid = true; *output = b; return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool seh_p99_consumed_copy_commands(P99ConsumedFrame& frame,
    const std::uint32_t* commands, std::uint32_t count, const void* context) {
    auto& capture = g_p99_consumed_capture;
    capture.supplied_words = count;
    __try {
        if (!commands || !context || count == 0 || count > capture.raw.size()) return false;
        P99ConsumedOwner owner{};
        P99ConsumedRead r{};
        std::uintptr_t art = 0, root = 0, fg = 0, state = 0, check = 0;
        const auto ctx = reinterpret_cast<std::uintptr_t>(context);
        if (!seh_read_p99_consumed_owner(reinterpret_cast<void*>(frame.owner.job), &owner) ||
            !same_owner(frame.owner, owner) ||
            owner.fg > std::numeric_limits<std::uintptr_t>::max() - 0x1C0 ||
            !r.read(ctx, 0, art) || art != owner.target_skeleton ||
            !r.read(ctx, 0x10, root) || root != owner.fg + 0x1C0 ||
            !r.read(owner.job, 8, fg) || fg != owner.fg ||
            !r.read(owner.job, 0, state) || state != owner.job_state ||
            !r.read(fg, 0x10, check) || check != state ||
            !r.copy(reinterpret_cast<std::uintptr_t>(commands), capture.raw.data(), count * 4ull))
            return false;
        // Exact second value read, not merely a header hash or pointer comparison.
        for (std::uint32_t i = 0; i < count; ++i)
            if (capture.raw[i] != static_cast<const volatile std::uint32_t*>(commands)[i])
                return false;
        if (!r.read(ctx, 0, check) || check != art ||
            !r.read(ctx, 0x10, check) || check != root ||
            !r.read(owner.job, 8, check) || check != fg) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// All resource dereferences occur here, BEFORE returning to native Process.
// Logging only uses these copied values, never decoded clip pointers later.
void p99_consumed_capture_commands(P99ConsumedFrame& frame,
    const std::uint32_t* commands, std::uint32_t count, const void* context) {
    auto& capture = g_p99_consumed_capture;
    const auto start = p99_consumed_cost_start();
    capture.copied = seh_p99_consumed_copy_commands(frame, commands, count, context);
    if (capture.copied)
        coop::p99_consumed_commands::decode({capture.raw.data(), count}, capture.decoded);
    if (!p99_consumed_charge(frame, start) || !capture.copied) return;
    capture.clips_complete = true;
    for (std::uint32_t i = 0; i < capture.decoded.command_count; ++i) {
        const auto& command = capture.decoded.commands[i];
        if (command.opcode != 6 || !command.supported_shape) continue;
        const auto clip = command.animation.clip_identity;
        bool seen = false;
        for (std::uint32_t j = 0; j < capture.clip_count; ++j)
            seen = seen || capture.clips[j].identity == clip;
        if (seen) continue;
        if (capture.clip_count == capture.clips.size()) {
            capture.clip_limit = true; capture.clips_complete = false; break;
        }
        const auto clip_start = p99_consumed_cost_start();
        if (!seh_p99_consumed_clip(clip, &capture.clips[capture.clip_count++]))
            capture.clips_complete = false;
        if (!p99_consumed_charge(frame, clip_start)) {
            capture.clips_complete = false; break;
        }
    }
}

std::uint32_t __fastcall hooked_p99_consumed_process(const std::uint32_t* commands,
    std::uint32_t count, const void* context, std::uint32_t group,
    const void* a5, const void* a6, const void* a7) {
    const auto native = g_p99_consumed_process;
    if (!native) return 0; // Installer must initialize BEFORE publishing any site.
    auto* frame = g_p99_consumed_fiber.current();
    const bool same = frame && coop::p99_consumed_frame::owns_process(
        reinterpret_cast<std::uintptr_t>(context), frame->native_return_slot);
    if (frame && !same) frame->ambiguous = true;
    if (same && group == 0) {
        if (++frame->process0_count != 1 || frame->post_count != 0) frame->ambiguous = true;
        else if (!frame->budget_exceeded) p99_consumed_capture_commands(*frame, commands, count, context);
    }
    const auto result = native(commands, count, context, group, a5, a6, a7);
    if (same && group == 0) {
        if (g_p99_consumed_fiber.owns(*frame)) frame->process_returned = true;
        else frame->ambiguous = true;
    }
    return result;
}

__declspec(noinline) std::int64_t __fastcall hooked_p99_consumed_post(void* owner) {
    using Native = std::int64_t(__fastcall*)(void*);
    const auto native = reinterpret_cast<Native>(
        (*static_cast<std::uintptr_t* const*>(owner))[0x408 / sizeof(std::uintptr_t)]);
    const auto return_slot = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    auto* frame = g_p99_consumed_fiber.current();
    const bool same = frame && coop::p99_consumed_frame::owns_post(
        return_slot, frame->native_return_slot);
    if (frame && !same) frame->ambiguous = true;
    bool capture = false;
    if (same) {
        if (++frame->post_count != 1 || reinterpret_cast<std::uintptr_t>(owner) != frame->owner.player)
            frame->ambiguous = true;
        else {
            capture = true;
            if (!frame->budget_exceeded) {
                const auto start = p99_consumed_cost_start();
                g_p99_consumed_capture.pre_valid =
                    seh_copy_p99_consumed_arms(frame->owner, &g_p99_consumed_capture.pre);
                p99_consumed_charge(*frame, start);
            }
        }
    }
    const auto result = native(owner); // No SEH catch may swallow a stock exception.
    if (capture) {
        if (!g_p99_consumed_fiber.owns(*frame)) frame->ambiguous = true;
        else {
            frame->post_returned = true;
            if (!frame->budget_exceeded) {
                const auto start = p99_consumed_cost_start();
                g_p99_consumed_capture.post_valid =
                    seh_copy_p99_consumed_arms(frame->owner, &g_p99_consumed_capture.post);
                p99_consumed_charge(*frame, start);
            }
        }
    }
    return result;
}

void p99_consumed_log_arms(unsigned sample, const char* phase, bool valid,
    const std::array<std::array<std::byte, 48>, 13>& rows) {
    if (!valid) return;
    char line[1500]{};
    auto pos = static_cast<std::size_t>(std::snprintf(line, sizeof(line),
        "P99-CONSUMED arms sample=%u phase=%s first=18 count=13 bytes=624 hex=", sample, phase));
    constexpr char hex[] = "0123456789ABCDEF";
    for (const auto& row : rows) for (const auto byte : row) {
        const auto value = std::to_integer<unsigned>(byte);
        line[pos++] = hex[value >> 4]; line[pos++] = hex[value & 15];
    }
    line[pos] = 0; boot_log_line(line);
}
void p99_consumed_log(const P99ConsumedFrame& frame) {
    char line[1500]{};
    if (frame.probe_only) {
        // A refused probe did not acquire any capture payload. In particular,
        // do NOT inspect leftover clips/rows from an earlier admitted sample.
        std::snprintf(line, sizeof(line),
            "P99-CONSUMED PROBE index=%u tick=%llu elapsed=%llu end=%llu admitted=%u "
            "reason=%u cost=%llu budget=%u clock=%u limit=%llu slow=%u retry=%llu "
            "terminal=%u capture=none",
            frame.probe, static_cast<unsigned long long>(frame.tick),
            static_cast<unsigned long long>(frame.elapsed), static_cast<unsigned long long>(frame.end_tick),
            static_cast<unsigned>(frame.probe_admitted), static_cast<unsigned>(frame.probe_reason),
            static_cast<unsigned long long>(frame.probe_ticks), static_cast<unsigned>(frame.probe_budget),
            static_cast<unsigned>(frame.probe_clock), static_cast<unsigned long long>(g_p99_consumed_cost_limit),
            frame.slow_probes, static_cast<unsigned long long>(frame.probe_retry_tick),
            static_cast<unsigned>(frame.probe_terminal));
        boot_log_line(line);
        return;
    }
    const auto& c = g_p99_consumed_capture;
    const bool complete = !frame.ambiguous && !frame.budget_exceeded && frame.process0_count == 1 &&
        frame.process_returned && frame.post_count == 1 && frame.post_returned &&
        c.copied && c.decoded.complete() && c.clips_complete && c.pre_valid && c.post_valid;
    std::snprintf(line, sizeof(line),
        "P99-CONSUMED sample=%u tick=%llu elapsed=%llu end=%llu continuity=instant-not-steady "
        "job=%llX player=%llX pid=%u fg=%llX weapon=%llX wid=%u pose=%llX "
        "g0=%u g0returned=%u post=%u postreturned=%u ambiguous=%u copied=%u supplied=%u "
        "status=%u framing=%u parsed=%u cmds=%u unsupported=%u clips=%u clipok=%u cliplimit=%u "
        "pre=%u after=%u complete=%u cost=%llu budget=%u clock=%u limit=%llu probe_cost=%llu boundary=whole408-not-Feather",
        frame.sample, static_cast<unsigned long long>(frame.tick),
        static_cast<unsigned long long>(frame.elapsed), static_cast<unsigned long long>(frame.end_tick),
        static_cast<unsigned long long>(frame.owner.job),
        static_cast<unsigned long long>(frame.owner.player), frame.owner.player_id,
        static_cast<unsigned long long>(frame.owner.fg),
        static_cast<unsigned long long>(frame.owner.weapon), frame.owner.weapon_id,
        static_cast<unsigned long long>(frame.owner.local_pose), frame.process0_count,
        static_cast<unsigned>(frame.process_returned), frame.post_count,
        static_cast<unsigned>(frame.post_returned), static_cast<unsigned>(frame.ambiguous),
        static_cast<unsigned>(c.copied), c.supplied_words, static_cast<unsigned>(c.decoded.status),
        static_cast<unsigned>(c.decoded.framing_complete), c.decoded.parsed_word_count,
        c.decoded.command_count, c.decoded.unsupported_count, c.clip_count,
        static_cast<unsigned>(c.clips_complete), static_cast<unsigned>(c.clip_limit),
        static_cast<unsigned>(c.pre_valid), static_cast<unsigned>(c.post_valid),
        static_cast<unsigned>(complete), static_cast<unsigned long long>(frame.observer_ticks),
        static_cast<unsigned>(frame.budget_exceeded), static_cast<unsigned>(frame.clock_valid),
        static_cast<unsigned long long>(g_p99_consumed_cost_limit),
        static_cast<unsigned long long>(frame.probe_ticks));
    boot_log_line(line);
    if (c.copied) for (std::uint32_t start = 0; start < c.supplied_words; start += 128) {
        const auto count = std::min<std::uint32_t>(128, c.supplied_words - start);
        auto pos = static_cast<std::size_t>(std::snprintf(line, sizeof(line),
            "P99-CONSUMED words sample=%u offset=%u count=%u hex=", frame.sample, start, count));
        for (std::uint32_t i = 0; i < count; ++i)
            pos += static_cast<std::size_t>(std::snprintf(line + pos, sizeof(line) - pos,
                "%08X%s", c.raw[start + i], i + 1 == count ? "" : ","));
        boot_log_line(line);
    }
    for (std::uint32_t i = 0; i < c.clip_count; ++i) {
        const auto& clip = c.clips[i];
        std::snprintf(line, sizeof(line),
            "P99-CONSUMED clip sample=%u index=%u ptr=%llX valid=%u id=%08X flags=%08X "
            "data=%llX database=%08X skeleton=%08X samples=%u",
            frame.sample, i, static_cast<unsigned long long>(clip.identity),
            static_cast<unsigned>(clip.valid), clip.id, clip.flags,
            static_cast<unsigned long long>(clip.data), clip.database, clip.skeleton,
            static_cast<unsigned>(clip.samples));
        boot_log_line(line);
    }
    p99_consumed_log_arms(frame.sample, "pre408", c.pre_valid, c.pre);
    p99_consumed_log_arms(frame.sample, "post408", c.post_valid, c.post);
}

#include "nadine_p99_consumed_log_worker.inl"

std::uintptr_t __fastcall hooked_p99_consumed_outer(void* job) {
    const auto native = g_p99_consumed_outer;
    if (!native) return 0;
    if (!kEnableP99ConsumedCensus || !g_p99_consumed_ready.load(std::memory_order_acquire) ||
        g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire) ||
        g_p99_consumed_fiber.failed()) return native(job);
    // Check the arm-relative lifetime BEFORE reading even the reject-only Fg
    // prefilter. An absent/late local Player must not extend the capture window.
    const auto entry_tick = GetTickCount64();
    if (entry_tick < g_p99_consumed_arm_ms || entry_tick - g_p99_consumed_arm_ms >= 120000) {
        g_p99_consumed_finished.store(true, std::memory_order_release);
        return native(job);
    }
    if (auto* nested = g_p99_consumed_fiber.current()) {
        nested->ambiguous = true; return native(job);
    }
    if (!seh_p99_consumed_local_fg(job) || g_p99_consumed_lease.test_and_set(std::memory_order_acquire)) {
        return native(job);
    }
    // A callback may pass the first gate just before another sample or the
    // worker stops collection and releases the buffer. Recheck after acquire.
    if (g_p99_consumed_failed.load(std::memory_order_acquire) ||
        g_p99_consumed_finished.load(std::memory_order_acquire) ||
        g_p99_consumed_fiber.failed()) {
        g_p99_consumed_lease.clear(std::memory_order_release);
        return native(job);
    }
    P99ConsumedFrame frame{};
    P99ConsumedFrame* previous = nullptr;
    std::uintptr_t result = 0;
    bool transferred = false;
    __try {
        const auto now = GetTickCount64();
        const auto elapsed = now - g_p99_consumed_arm_ms;
        bool selected = false;
        if (now < g_p99_consumed_arm_ms || elapsed >= 120000 ||
            g_p99_consumed_probes >= 600 || g_p99_consumed_attempts >= 8)
            g_p99_consumed_finished.store(true, std::memory_order_release);
        if (now >= g_p99_consumed_arm_ms && elapsed < 120000 &&
            g_p99_consumed_probes < 600 && g_p99_consumed_attempts < 8 &&
            now >= g_p99_consumed_next_probe) {
            g_p99_consumed_next_probe = now + 200;
            ++g_p99_consumed_probes;
            if (now >= g_p99_consumed_next_sample) {
                frame.tick = now; frame.elapsed = elapsed; frame.probe = g_p99_consumed_probes;
                const auto start = p99_consumed_cost_start();
                if (start.valid)
                    frame.probe_admitted = seh_read_p99_consumed_owner(job, &frame.owner, &frame.probe_reason);
                const bool probe_ok = p99_consumed_probe_charge(frame, start);
                if (frame.probe_admitted && probe_ok) {
                    // Successful admission is part of the same 1ms sample
                    // budget, not an extra 1ms allowance on top of capture.
                    frame.observer_ticks = frame.probe_ticks;
                    frame.sample = ++g_p99_consumed_attempts;
                    g_p99_consumed_next_sample = now + (frame.sample == 1 ? 200 : 2000);
                    const auto clear_start = p99_consumed_cost_start();
                    std::memset(&g_p99_consumed_capture, 0, sizeof(g_p99_consumed_capture));
                    p99_consumed_charge(frame, clear_start);
                    selected = g_p99_consumed_fiber.enter(frame, previous);
                } else if (frame.probe_budget || g_p99_consumed_probe_reports < 8) {
                    frame.probe_only = true;
                    ++g_p99_consumed_probe_reports;
                }
            }
        }
        if (!selected) {
            result = native(job);
        }
        else {
            result = coop::p99_consumed_frame::p99_consumed_invoke_native(
                job, native, &frame.native_return_slot);
        }
        frame.end_tick = GetTickCount64();
        frame.native_return_slot = 0;
        if (frame.active && !g_p99_consumed_fiber.leave(frame, previous)) frame.ambiguous = true;
        if (g_p99_consumed_attempts >= 8 || g_p99_consumed_probes >= 600)
            g_p99_consumed_finished.store(true, std::memory_order_release);
        frame.probe_terminal = g_p99_consumed_finished.load(std::memory_order_acquire);
        if (selected || frame.budget_exceeded || frame.probe_only)
            transferred = publish_p99_consumed_log(frame);
    } __finally {
        frame.native_return_slot = 0;
        if (frame.active) g_p99_consumed_fiber.leave(frame, previous);
        // Worker can finish BEFORE publish returns: touch no lease-owned globals
        // after a successful transfer. Native SEH reaches the false branch.
        if (!transferred) {
            if (g_p99_consumed_attempts >= 8 || g_p99_consumed_probes >= 600)
                g_p99_consumed_finished.store(true, std::memory_order_release);
            g_p99_consumed_lease.clear(std::memory_order_release);
        }
    }
    return result;
}

bool initialize_p99_consumed_census() {
    // Main installs/validates all four sites. Only the later arm worker may
    // publish ready; installation and initialization cannot admit game reads.
    if (!g_module || g_p99_consumed_installed.load(std::memory_order_acquire) ||
        g_p99_consumed_ready.load(std::memory_order_acquire)) return false;
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart < 1000) return false;
    g_p99_consumed_cost_limit = static_cast<std::uint64_t>(frequency.QuadPart) / 1000;
    g_p99_consumed_outer = reinterpret_cast<coop::p99_consumed_frame::Native>(
        g_module + coop::p99_consumed_frame::kOuterRva);
    g_p99_consumed_process = reinterpret_cast<coop::p99_process_native_frame::Native>(
        g_module + coop::p99_process_native_frame::kProcessRva);
    // Touch only our own fixed storage before any retail site is published.
    // Volatile stores keep the pre-touch real even with whole-program optimization.
    auto* bytes = reinterpret_cast<volatile std::byte*>(&g_p99_consumed_capture);
    for (std::size_t i = 0; i < sizeof(g_p99_consumed_capture); ++i) bytes[i] = std::byte{0};
    return g_p99_consumed_fiber.initialize() && initialize_p99_consumed_log_worker();
}
