#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "nadine_p99_consumed_commands.h"
#include "nadine_p99_consumed_frame.h"
#include "nadine_p99_group_fiber.h"
#include "nadine_p99_group_native_frame.h"
#include "nadine_p99_process_native_frame.h"
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

extern "C" std::uintptr_t p99_consumed_fixture_outer(void*);
extern "C" void p99_consumed_fixture_perform(void*);

namespace {
std::atomic<unsigned> checks{0}, failures{0};
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", name); }
}
struct { bool enabled = true; } g_state_net;
bool g_net_role_b = true;
std::atomic<bool> g_local_reload_level{false};
std::uintptr_t g_module = 0;
constexpr std::uintptr_t kRetainedSlotTableRva = 0x10;
constexpr std::uintptr_t kPlayerFgAnimDataOffset = 0x30;
struct P99ConsumedOwner {
    std::uintptr_t job = 0, player = 0, weapon = 0, fg = 0, local_pose = 0;
    std::uintptr_t target_skeleton = 0, job_state = 0;
    std::uint32_t player_id = 0, weapon_id = 0;
};
bool same_owner(const P99ConsumedOwner& a, const P99ConsumedOwner& b) {
    return a.job == b.job && a.player == b.player && a.weapon == b.weapon &&
        a.fg == b.fg && a.local_pose == b.local_pose &&
        a.target_skeleton == b.target_skeleton && a.job_state == b.job_state &&
        a.player_id == b.player_id && a.weapon_id == b.weapon_id;
}
std::uint64_t mock_tick = 10000;
ULONGLONG mock_ticks() { return mock_tick; }
std::int64_t mock_qpc = 0, mock_qpc_step = 0;
unsigned mock_qpc_calls = 0, mock_jump_call = 0;
bool mock_qpc_ok = true, native_cost = false;
BOOL mock_counter(LARGE_INTEGER* output) {
    ++mock_qpc_calls;
    mock_qpc += mock_qpc_step + (mock_qpc_calls == mock_jump_call ? 2000 : 0);
    output->QuadPart = mock_qpc; return mock_qpc_ok ? TRUE : FALSE;
}
BOOL mock_frequency(LARGE_INTEGER* output) { output->QuadPart = 1000000; return TRUE; }
P99ConsumedOwner mock_owner{};
unsigned owner_calls = 0, arm_calls = 0, process_calls = 0, post_calls = 0;
unsigned log_calls = 0, raw_lines = 0, arm_lines = 0, complete_lines = 0;
unsigned budget_lines = 0;
std::size_t max_log = 0;
DWORD main_thread = 0;
std::atomic<bool> block_logs{false}, log_started{false}, fault_log{false};
bool owner_allowed = true, arm_allowed = true, read_allowed = true;
bool throw_process = false, throw_post = false, do_nested = false, do_old_nested = false;
bool duplicate_process = false, corrupt_context = false;
unsigned nested_depth = 0;
constexpr DWORD kTestException = 0xE0421995;
constexpr std::uintptr_t kOuterResult = 0x123456789ABCDEF0ull;
constexpr std::uint32_t kProcessResult = 0xDEADC0DE;
constexpr std::int64_t kPostResult = INT64_C(0x76543210ABCDEF01);
std::uint32_t process_result = 0;
void boot_log_line(const char* line) {
    check(GetCurrentThreadId() != main_thread, "logging never runs on animation callback thread");
    if (fault_log) RaiseException(kTestException, 0, 0, nullptr);
    if (block_logs) {
        log_started.store(true, std::memory_order_release);
        while (block_logs.load(std::memory_order_acquire)) Sleep(1);
    }
    const auto n = std::strlen(line);
    ++log_calls; max_log = std::max(max_log, n);
    check(n < 1500, "every log fits 1500-byte storage / 1600-byte logger");
    raw_lines += std::strstr(line, "P99-CONSUMED words ") != nullptr;
    arm_lines += std::strstr(line, "P99-CONSUMED arms ") != nullptr;
    complete_lines += std::strstr(line, " complete=1 ") != nullptr;
    budget_lines += std::strstr(line, " budget=1 ") != nullptr;
}
// Controlled owner/read seams. The companion owner suite covers the real raw
// reader; this suite includes the ACTUAL runtime hooks/lease/FLS/copy/logger.
struct P99ConsumedRead {
    bool copy(std::uintptr_t address, void* out, std::size_t bytes) {
        if (!read_allowed || address < 0x10000 ||
            bytes > UINTPTR_MAX - address) return false;
        std::memcpy(out, reinterpret_cast<const void*>(address), bytes); return true;
    }
    template<class T> bool read(std::uintptr_t base, std::size_t offset, T& out) {
        return base && offset <= UINTPTR_MAX - base && copy(base + offset, &out, sizeof(out));
    }
};
bool seh_read_p99_consumed_owner(void* job, P99ConsumedOwner* out) {
    ++owner_calls; *out = {};
    if (!owner_allowed || reinterpret_cast<std::uintptr_t>(job) != mock_owner.job) return false;
    *out = mock_owner; return true;
}
bool seh_copy_p99_consumed_arms(const P99ConsumedOwner& owner,
    std::array<std::array<std::byte, 48>, 13>* out) {
    ++arm_calls; *out = {};
    if (!arm_allowed || !same_owner(owner, mock_owner)) return false;
    for (auto& row : *out) row.fill(static_cast<std::byte>(arm_calls));
    return true;
}
#define TLL_P99_CONSUMED_RUNTIME_TEST
#define GetTickCount64 mock_ticks
#define QueryPerformanceCounter mock_counter
#define QueryPerformanceFrequency mock_frequency
#include "nadine_p99_consumed_runtime.inl"
#undef GetTickCount64
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency

struct Job {
    std::uintptr_t vtable = 0, fg = 0;
    std::uint32_t flags = 0, pad = 0;
    std::uintptr_t process = 0, post = 0, unused = 0, player_fg = 0;
};
static_assert(offsetof(Job, process) == 0x18 && offsetof(Job, post) == 0x20);
static_assert(offsetof(Job, player_fg) == kPlayerFgAnimDataOffset);
Job job;
std::array<std::uintptr_t, 130> table{};
std::array<std::byte, 0x240> fg{};
std::array<std::uintptr_t, 2> retained{};
std::array<std::uintptr_t, 4> module{};
std::array<std::byte, 0x48> art{};
std::array<std::array<std::byte, 0x48>, 65> clips{};
std::array<std::byte, 0x24> clip_data{};
std::array<std::uint32_t, 4096> words{};
std::uint32_t word_count = 12;

template<class T, std::size_t N> void put(std::array<std::byte, N>& bytes,
    std::size_t offset, T value) { std::memcpy(bytes.data() + offset, &value, sizeof(value)); }

std::uint32_t __fastcall native_process(const std::uint32_t* command_words,
    std::uint32_t count, const void* context, std::uint32_t group,
    const void* a5, const void* a6, const void* a7) {
    ++process_calls;
    if (native_cost) mock_qpc += 1000000; // Must not count as observer cost.
    check(command_words == words.data() && count == word_count && context != nullptr && group == 0,
        "original first four Process operands preserved");
    check(reinterpret_cast<std::uintptr_t>(a5) == 0x55 &&
        reinterpret_cast<std::uintptr_t>(a6) == 0x66 &&
        reinterpret_cast<std::uintptr_t>(a7) == 0x77, "original three stack operands preserved");
    if ((do_nested || do_old_nested) && nested_depth == 0) {
        ++nested_depth;
        const auto result = do_nested ? hooked_p99_consumed_outer(&job)
            : p99_consumed_fixture_outer(&job);
        check(result == kOuterResult, "nested native return preserved");
        --nested_depth;
    }
    if (throw_process) RaiseException(kTestException, 0, 0, nullptr);
    return kProcessResult;
}
std::int64_t __fastcall native_post(void* owner) {
    ++post_calls;
    if (native_cost) mock_qpc += 1000000;
    check(owner == &job, "unchanged owner to vslot408");
    if (throw_post) RaiseException(kTestException, 0, 0, nullptr);
    return kPostResult;
}
void reset() {
    check(g_p99_consumed_fiber.current() == nullptr, "previous FLS scope cleared");
    check(!g_p99_consumed_lease.test_and_set(), "previous singleton lease cleared");
    g_p99_consumed_lease.clear();
    g_p99_consumed_ready = true; g_p99_consumed_failed = false; g_p99_consumed_finished = false;
    g_p99_consumed_first_local = g_p99_consumed_next_probe = g_p99_consumed_next_sample = 0;
    g_p99_consumed_probes = g_p99_consumed_attempts = 0;
    owner_calls = arm_calls = process_calls = post_calls = log_calls = 0;
    raw_lines = arm_lines = complete_lines = budget_lines = 0; max_log = 0;
    owner_allowed = arm_allowed = read_allowed = true;
    throw_process = throw_post = do_nested = do_old_nested = duplicate_process = corrupt_context = false;
    nested_depth = 0; mock_tick = 10000; job.flags = 0;
    mock_qpc = mock_qpc_step = 0; mock_qpc_calls = mock_jump_call = 0;
    mock_qpc_ok = true; native_cost = false;
    block_logs = log_started = fault_log = false;
    word_count = 12; words.fill(0);
    const auto clip = reinterpret_cast<std::uintptr_t>(clips[0].data());
    words[0] = 0x60006; words[1] = 0x12345678;
    words[2] = static_cast<std::uint32_t>(clip); words[3] = static_cast<std::uint32_t>(clip >> 32);
    words[4] = 7; words[5] = 0x41200000;
    words[6] = 0x60007; words[7] = 1; words[8] = 7; words[9] = 0;
    words[10] = 1; words[11] = 0x3F800000;
    job.fg = job.player_fg = reinterpret_cast<std::uintptr_t>(fg.data());
    g_p99_consumed_outer = &p99_consumed_fixture_outer;
    g_p99_consumed_process = &native_process;
}
void drain_log() {
    const auto limit = GetTickCount64() + 2000;
    while (g_p99_consumed_lease.test(std::memory_order_acquire) && GetTickCount64() < limit) Sleep(1);
    check(!g_p99_consumed_lease.test(std::memory_order_acquire), "bounded worker drain completes");
    if (g_p99_consumed_lease.test(std::memory_order_acquire)) ExitProcess(2);
}
bool invoke_catching(bool drain = true) {
    __try {
        check(hooked_p99_consumed_outer(&job) == kOuterResult, "full-width outer return unchanged");
        if (drain) drain_log();
    } __except (GetExceptionCode() == kTestException
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
void basic_and_failures() {
    reset(); check(!invoke_catching(), "normal native completes");
    check(process_calls == 1 && post_calls == 1 && process_result == kProcessResult,
        "native callbacks exactly once and Process result exact");
    check(g_p99_consumed_capture.copied && g_p99_consumed_capture.clips[0].valid,
        "owned command and live clip metadata captured");
    check(g_p99_consumed_capture.clips[0].id == 0x7D11D4FD &&
        g_p99_consumed_capture.clips[0].samples == 337, "clip ID and sample metadata exact");
    check(complete_lines == 1 && arm_lines == 2 && raw_lines == 1, "joined output complete");
    for (std::uint32_t flags : {1u, 2u, 3u, 0x100u}) {
        reset(); job.flags = flags; invoke_catching();
        check(complete_lines == 0, "missing/duplicate native stage cannot be complete");
    }
    reset(); duplicate_process = true; invoke_catching();
    check(process_calls == 2 && complete_lines == 0, "duplicate same-frame g0 invalidates");
    reset(); corrupt_context = true; invoke_catching();
    check(!g_p99_consumed_capture.copied && complete_lines == 0, "wrong root refuses command join");
    reset(); owner_allowed = false; invoke_catching();
    check(process_calls == 1 && post_calls == 1 && log_calls == 0 &&
        g_p99_consumed_attempts == 0, "unselected fallback once without capture");
    reset(); arm_allowed = false; invoke_catching();
    check(arm_lines == 0 && complete_lines == 0, "failed arm reads cannot produce joined result");
    reset(); read_allowed = false; invoke_catching();
    check(raw_lines == 0 && complete_lines == 0, "failed raw read stays explicit incomplete");
    for (unsigned which = 0; which < 2; ++which) {
        reset(); throw_process = which == 0; throw_post = which == 1;
        check(invoke_catching(), "stock SEH propagated, not swallowed");
        check(log_calls == 0, "no success log during stock unwind");
        check(g_p99_consumed_fiber.current() == nullptr, "SEH clears FLS frame");
        check(!g_p99_consumed_lease.test_and_set(), "SEH releases singleton lease");
        g_p99_consumed_lease.clear();
    }
    reset(); do_nested = true; invoke_catching();
    check(process_calls == 2 && post_calls == 2 && complete_lines == 0,
        "wrapped nested call remains stock and invalidates parent");
    reset(); do_old_nested = true; invoke_catching();
    check(process_calls == 2 && post_calls == 2 && arm_calls == 2 && complete_lines == 0,
        "unwrapped foreign native frame cannot borrow markers or look complete");
    reset(); g_p99_consumed_lease.test_and_set(); invoke_catching(false);
    check(owner_calls == 0 && log_calls == 0, "busy singleton skips expensive capture");
    check(g_p99_consumed_lease.test_and_set(), "foreign invocation does not release owned lease");
    g_p99_consumed_lease.clear();
}
void worker_and_cost() {
    reset(); block_logs = true; invoke_catching(false);
    const auto limit = GetTickCount64() + 2000;
    while (!log_started.load(std::memory_order_acquire) && GetTickCount64() < limit) Sleep(1);
    check(log_started && g_p99_consumed_lease.test() && !g_p99_consumed_log_pending,
        "native callback returns while worker owns pending capture");
    check(!g_p99_consumed_log_frame.active &&
        g_p99_consumed_log_frame.native_return_slot == 0 &&
        g_p99_consumed_log_frame.identity.fiber == nullptr,
        "published metadata has no live FLS/native-frame binding");
    const auto before = owner_calls; invoke_catching(false);
    check(owner_calls == before, "slow worker cannot stall/reuse capture on callback");
    block_logs = false; drain_log();
    check(g_p99_consumed_log_frame.end_tick == 10000, "native closure timestamp not worker drain time");
    reset(); native_cost = true; mock_qpc_step = 1; invoke_catching();
    check(complete_lines == 1 && budget_lines == 0 && g_p99_consumed_log_frame.observer_ticks == 5,
        "budget excludes all stock native execution time");
    reset(); mock_qpc_step = 2000; invoke_catching();
    check(g_p99_consumed_finished && budget_lines == 1 && complete_lines == 0 && arm_calls == 0,
        "owner-read budget stop disables further deep reads but calls stock");
    reset(); mock_jump_call = 6; invoke_catching();
    check(g_p99_consumed_finished && g_p99_consumed_capture.clip_count == 1 &&
        !g_p99_consumed_capture.clips_complete && budget_lines == 1 && arm_calls == 0,
        "clip checkpoint stops after bounded current metadata read");
    reset(); mock_qpc_ok = false; invoke_catching();
    check(g_p99_consumed_finished && budget_lines == 1 &&
        !g_p99_consumed_log_frame.clock_valid, "invalid QPC fails closed with explicit log");
    reset(); owner_allowed = false; mock_qpc_step = 2000; invoke_catching();
    check(g_p99_consumed_attempts == 0 && budget_lines == 1 &&
        g_p99_consumed_log_frame.sample == 0, "unadmitted expensive probe has separate stop evidence");
}
void time_and_limits() {
    reset(); job.fg = 0; invoke_catching();
    check(g_p99_consumed_probes == 0 && g_p99_consumed_attempts == 0 && owner_calls == 0,
        "foreign Fg spends no probe/sample counters");
    reset(); invoke_catching(); const auto first_owner_calls = owner_calls;
    mock_tick += 199; invoke_catching();
    check(g_p99_consumed_probes == 1 && owner_calls == first_owner_calls, "200ms probe throttle");
    ++mock_tick; invoke_catching(); check(g_p99_consumed_attempts == 2, "first sample gap 200ms");
    const auto second_owner_calls = owner_calls;
    mock_tick += 200; invoke_catching();
    check(g_p99_consumed_probes == 3 && owner_calls == second_owner_calls,
        "sparse sample gap avoids owner probes between due samples");
    mock_tick = 12200;
    for (unsigned i = 2; i < 8; ++i, mock_tick += 2000) invoke_catching();
    check(g_p99_consumed_attempts == 8 && g_p99_consumed_finished,
        "eight admitted attempts irreversibly stop capture");
    check(g_p99_consumed_first_local == 10000 && mock_tick - 2000 >= 20000,
        "sample allocation reaches at least ten seconds late");
    const auto probes = g_p99_consumed_probes; const auto owners = owner_calls;
    invoke_catching(); check(g_p99_consumed_probes == probes && owner_calls == owners,
        "budget done has no later expensive probes");
    reset(); owner_allowed = false;
    for (unsigned i = 0; i < 600; ++i, mock_tick += 200) invoke_catching();
    check(g_p99_consumed_probes == 600 && g_p99_consumed_finished, "600 failed probes terminate");
    const auto capped = owner_calls; invoke_catching();
    check(owner_calls == capped, "no owner reads after probe cap");
    reset(); owner_allowed = false; invoke_catching(); mock_tick += 120001; invoke_catching();
    check(g_p99_consumed_finished && g_p99_consumed_probes == 1, "120s local-Fg deadline enforced");
}
void parser_and_logging() {
    reset(); words[0] = 0; invoke_catching();
    check(g_p99_consumed_capture.decoded.status == coop::p99_consumed_commands::Status::MalformedLength &&
        complete_lines == 0 && raw_lines == 1, "malformed span retained but incomplete");
    reset(); words[6] = 0x60018; invoke_catching();
    check(g_p99_consumed_capture.decoded.status == coop::p99_consumed_commands::Status::Unsupported &&
        complete_lines == 0, "variable plugin is explicitly unsupported");
    reset(); words[2] = 1; words[3] = 0; invoke_catching();
    check(!g_p99_consumed_capture.clips_complete && complete_lines == 0,
        "unreadable clip is not deferred or mislabeled valid");
    reset(); word_count = 4097; invoke_catching();
    check(!g_p99_consumed_capture.copied && raw_lines == 0, "oversized supplied command list refused");
    reset(); word_count = 4096;
    for (std::size_t i = 0; i < words.size(); i += 2) { words[i] = 0x20009; words[i + 1] = 0; }
    invoke_catching(); check(raw_lines == 32 && complete_lines == 0,
        "raw chunks <=128 words and decoder command cap visible");
    reset(); word_count = 65 * 6;
    for (std::uint32_t i = 0; i < 65; ++i) {
        const auto ptr = reinterpret_cast<std::uintptr_t>(clips[i].data());
        words[i * 6] = 0x60006; words[i * 6 + 1] = 0;
        words[i * 6 + 2] = static_cast<std::uint32_t>(ptr);
        words[i * 6 + 3] = static_cast<std::uint32_t>(ptr >> 32);
        words[i * 6 + 4] = i; words[i * 6 + 5] = 0;
    }
    invoke_catching(); check(g_p99_consumed_capture.clip_count == 64 &&
        g_p99_consumed_capture.clip_limit && complete_lines == 0, "clip census bounded with explicit truncation");
    check(max_log < 1500, "maximum raw/arm/metadata logs never truncate");
}
} // namespace

extern "C" std::uint32_t p99_consumed_fixture_process_cpp(const std::uint32_t*,
    std::uint32_t, const void* context, std::uint32_t group,
    const void* a5, const void* a6, const void* a7) {
    auto* bytes = const_cast<std::byte*>(static_cast<const std::byte*>(context));
    auto root = mock_owner.fg + 0x1C0 + (corrupt_context ? 8 : 0);
    std::memcpy(bytes, &mock_owner.target_skeleton, 8);
    std::memcpy(bytes + 0x10, &root, 8);
    process_result = hooked_p99_consumed_process(words.data(), word_count, context, group, a5, a6, a7);
    if (duplicate_process) process_result =
        hooked_p99_consumed_process(words.data(), word_count, context, group, a5, a6, a7);
    return process_result;
}
extern "C" void p99_consumed_fixture_post_cpp(void*) { check(false, "unused fallback post fixture"); }
extern "C" std::uintptr_t p99_consumed_fixture_tail_cpp(void*) { return kOuterResult; }

int main() {
    main_thread = GetCurrentThreadId();
    g_module = reinterpret_cast<std::uintptr_t>(module.data());
    module[2] = reinterpret_cast<std::uintptr_t>(retained.data());
    retained[1] = reinterpret_cast<std::uintptr_t>(&job);
    table[0x408 / sizeof(std::uintptr_t)] = reinterpret_cast<std::uintptr_t>(&native_post);
    job.vtable = reinterpret_cast<std::uintptr_t>(table.data());
    job.post = reinterpret_cast<std::uintptr_t>(&hooked_p99_consumed_post);
    job.process = reinterpret_cast<std::uintptr_t>(&p99_consumed_fixture_process_cpp);
    put(fg, 0x10, job.vtable);
    mock_owner = {reinterpret_cast<std::uintptr_t>(&job), reinterpret_cast<std::uintptr_t>(&job),
        0x10101010, reinterpret_cast<std::uintptr_t>(fg.data()), 0x20202020,
        reinterpret_cast<std::uintptr_t>(art.data()), job.vtable, 123, 456};
    for (auto& clip : clips) {
        put(clip, 0x18, std::uint32_t{0x7D11D4FD}); put(clip, 0x24, std::uint32_t{0xB});
        put(clip, 0x28, reinterpret_cast<std::uintptr_t>(clip_data.data()));
        put(clip, 0x40, std::uint32_t{0x27E57F43});
    }
    put(clip_data, 4, std::uint32_t{0xFD6DBE03}); put(clip_data, 0x22, std::uint16_t{337});
    check(initialize_p99_consumed_census(), "FLS and original targets initialize before publication");
    basic_and_failures(); time_and_limits(); parser_and_logging(); worker_and_cost();
    reset(); // Final cleanup audit, including after the largest capture.
    fault_log = true; invoke_catching(); // Last: worker failure is intentionally irreversible.
    check(g_p99_consumed_failed && g_p99_consumed_finished,
        "worker log SEH poisons collector and releases singleton lease");
    check(WaitForSingleObject(g_p99_consumed_log_thread, 2000) == WAIT_OBJECT_0,
        "faulted worker exits without retaining the capture lease");
    std::printf("P99 consumed runtime: %u checks, %u failures\n", checks.load(), failures.load());
    return failures ? 1 : 0;
}
