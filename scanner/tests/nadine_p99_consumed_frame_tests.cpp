#include "nadine_p99_consumed_frame.h"
#include "nadine_p99_group_native_frame.h"

#include <Windows.h>
#include <intrin.h>
#include <cstdio>

namespace frame = coop::p99_consumed_frame;
extern "C" std::uintptr_t p99_consumed_fixture_outer(void*);
extern "C" void p99_consumed_fixture_perform(void*);
extern "C" void p99_consumed_fixture_post_relay(void*);

namespace {
unsigned checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", name); }
}
struct Job {
    std::uintptr_t unused = 0;
    std::uintptr_t fg_cookie = 0x12345678;
    std::uint32_t flags = 0;
    std::uintptr_t process_callback = 0;
    std::uintptr_t post_callback = 0;
};
static_assert(offsetof(Job, flags) == 0x10);
static_assert(offsetof(Job, process_callback) == 0x18 && offsetof(Job, post_callback) == 0x20);
constexpr std::uint32_t kSkipProcess = 1, kSkipPost = 2, kNested = 4;
constexpr std::uint32_t kThrowProcess = 8, kThrowPost = 16;
constexpr std::uint32_t kTail = 32, kThrowTail = 64;
constexpr std::uint32_t kDirectPerform = 128;
constexpr std::uint32_t kDuplicatePost = 256;
constexpr DWORD kFixtureException = 0xE0421995;
std::uintptr_t native_return_slot = 0;
unsigned process_calls = 0, post_calls = 0, rejected_process = 0, rejected_post = 0;
unsigned nested_depth = 0;
unsigned tail_calls = 0;
std::uintptr_t native_result = 0;
constexpr std::uintptr_t kNormalResult = 0x123456789ABCDEF0;
constexpr std::uintptr_t kTailResult = 0xFEDCBA9876543210;

__declspec(noinline) std::uintptr_t invoke(Job* job) {
    // This finally also executes after a real native-stack SEH unwind. No
    // exception handler inside the bridge catches or retries the stock call.
    std::uintptr_t result = 0;
    __try {
        result = frame::p99_consumed_invoke_native(
            job, &p99_consumed_fixture_outer, &native_return_slot);
    } __finally {
        native_return_slot = 0;
    }
    return result;
}
bool invoke_catching(Job* job) {
    __try { native_result = invoke(job); }
    __except (GetExceptionCode() == kFixtureException
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
void reset_counts() {
    process_calls = post_calls = rejected_process = rejected_post = nested_depth = 0;
    tail_calls = 0;
    native_result = 0;
}
void fixture_case(std::uint32_t flags, unsigned process, unsigned post, bool throws) {
    reset_counts();
    Job job{};
    job.flags = flags;
    check(invoke_catching(&job) == throws, "exception disposition");
    check(native_return_slot == 0, "outer finally expires scope");
    check(process_calls == process && post_calls == post, "native callback counts");
    check(rejected_process == ((flags & (kNested | kDirectPerform)) ? 1u : 0u),
        "foreign Process refused");
    check(rejected_post == ((flags & kNested) ? 1u : 0u), "foreign post refused");
    check(tail_calls == ((flags & kTail) ? 1u : 0u), "tail completion count");
    check(native_result == (throws ? 0 : ((flags & kTail) ? kTailResult : kNormalResult)),
        "native full-width integer return preserved");
}
} // namespace

extern "C" __declspec(noinline) std::uint32_t p99_consumed_fixture_process_cpp(
    const std::uint32_t* words, std::uint32_t count, const void* context,
    std::uint32_t group, const void* a5, const void* a6, const void* a7) {
    ++process_calls;
    const bool owned = frame::owns_process(
        reinterpret_cast<std::uintptr_t>(context), native_return_slot);
    check(owned == (nested_depth == 0), "compiled context+A8 ownership");
    if (!owned) ++rejected_process;
    check(count == 7 && group == 0 && reinterpret_cast<std::uintptr_t>(a5) == 0x55 &&
        reinterpret_cast<std::uintptr_t>(a6) == 0x66 &&
        reinterpret_cast<std::uintptr_t>(a7) == 0x77, "seven native arguments");
    const auto* job = reinterpret_cast<const Job*>(words);
    if ((job->flags & kNested) && nested_depth == 0) {
        const auto parent = native_return_slot;
        ++nested_depth;
        // Old queued/unwrapped callbacks must not inherit the parent's ticket,
        // even with the EXACT SAME job/Fg and the parent still on this stack.
        p99_consumed_fixture_outer(const_cast<Job*>(job));
        --nested_depth;
        check(native_return_slot == parent && frame::owns_process(
            reinterpret_cast<std::uintptr_t>(context), parent), "parent survives nested rejection");
    }
    if ((job->flags & kDirectPerform) && nested_depth == 0) {
        ++nested_depth;
        p99_consumed_fixture_perform(const_cast<Job*>(job));
        --nested_depth;
    }
    if (job->flags & kThrowProcess) RaiseException(kFixtureException, 0, 0, nullptr);
    return count;
}

extern "C" __declspec(noinline) void p99_consumed_fixture_post_cpp(void* pointer) {
    ++post_calls;
    const auto slot = reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    const bool owned = frame::owns_post(slot, native_return_slot);
    check(owned == (nested_depth == 0), "compiled noinline post slot+30 ownership");
    if (!owned) ++rejected_post;
    if (static_cast<Job*>(pointer)->flags & kThrowPost)
        RaiseException(kFixtureException, 0, 0, nullptr);
}

extern "C" __declspec(noinline) std::uintptr_t p99_consumed_fixture_tail_cpp(void* pointer) {
    ++tail_calls;
    check(reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()) == native_return_slot,
        "native epilogue tail target retains outer return slot");
    if (static_cast<Job*>(pointer)->flags & kThrowTail)
        RaiseException(kFixtureException, 0, 0, nullptr);
    return kTailResult;
}

int main() {
    check(frame::layout_matches(frame::kOuterPrologue, frame::kPerformPrologue,
        frame::kProcessContextLea), "pinned layout");
    auto changed = frame::kPerformPrologue;
    for (std::size_t i = 0; i < changed.size(); ++i) {
        changed[i] ^= 1;
        check(!frame::layout_matches(frame::kOuterPrologue, changed,
            frame::kProcessContextLea), "changed Perform byte refused");
        changed[i] ^= 1;
    }
    auto changed_outer = frame::kOuterPrologue;
    for (std::size_t i = 0; i < changed_outer.size(); ++i) {
        changed_outer[i] ^= 1;
        check(!frame::layout_matches(changed_outer, frame::kPerformPrologue,
            frame::kProcessContextLea), "changed outer byte refused");
        changed_outer[i] ^= 1;
    }
    auto changed_lea = frame::kProcessContextLea;
    for (std::size_t i = 0; i < changed_lea.size(); ++i) {
        changed_lea[i] ^= 1;
        check(!frame::layout_matches(frame::kOuterPrologue, frame::kPerformPrologue,
            changed_lea), "changed context LEA refused");
        changed_lea[i] ^= 1;
    }
    check(frame::owns_process(0x1000, 0x10A8), "pure Process equality");
    check(frame::owns_post(0x1078, 0x10A8), "pure post equality");
    check(!frame::owns_process(0, 0xA8) && !frame::owns_post(0, 0x30), "null refused");
    check(!frame::owns_process(0x1001, 0x10A9) && !frame::owns_post(0x1079, 0x10A9),
        "misalignment refused");
    check(!frame::owns_process(UINTPTR_MAX - 15, 0x98) &&
        !frame::owns_post(UINTPTR_MAX - 7, 0x28), "overflow refused");
    check(!frame::owns_process(0x1000, 0x20A8) &&
        !frame::owns_post(0x1078, 0x20A8), "different invocation refused");

    constexpr std::uintptr_t module = 0x140000000, relay = 0x141780000, hook = 0x180000000;
    for (std::size_t i = 0; i < frame::kIngressSites.size(); ++i) {
        const auto& site = frame::kIngressSites[i];
        const auto bytes = std::span(site.stock).first(site.size);
        const auto patch = frame::plan_ingress(i, module, bytes, relay, hook);
        check(patch.has_value(), "ingress plan");
        if (patch) {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, patch->instruction.data() + site.displacement_offset, 4);
            check(module + site.rva + site.size + displacement == relay, "new callback target");
            check(patch->instruction[0] == site.stock[0] &&
                patch->relay[0] == 0xFF && patch->relay[1] == 0x25, "stack-neutral leaf relay");
            std::uintptr_t target = 0;
            std::memcpy(&target, patch->relay.data() + 6, sizeof(target));
            check(target == hook, "relay destination");
        }
        auto bad = site.stock;
        for (std::size_t n = 0; n < site.size; ++n) {
            bad[n] ^= 1;
            check(!frame::plan_ingress(i, module, std::span(bad).first(site.size), relay, hook),
                "changed ingress byte refused");
            bad[n] ^= 1;
        }
        check(!frame::plan_ingress(i, module, bytes, UINTPTR_MAX - 1, hook), "relay overflow refused");
        check(!frame::plan_ingress(i, UINTPTR_MAX, bytes, relay, hook), "module overflow refused");
        check(!frame::plan_ingress(i, module, bytes, 0x700000000000, hook), "far relay refused");
    }
    check(!frame::plan_ingress(2, module, {}, relay, hook), "invalid ingress refused");

    DWORD64 image = 0;
    check(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&p99_consumed_fixture_outer), &image, nullptr)
        != nullptr, "outer fixture unwind registered");
    check(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&p99_consumed_fixture_perform), &image, nullptr)
        != nullptr, "Perform fixture unwind registered");
    check(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(
        &coop::p99_group_native_frame::p99_group_invoke_native), &image, nullptr)
        != nullptr, "shared native bridge unwind registered");
    check(reinterpret_cast<std::uintptr_t>(&frame::p99_consumed_invoke_native) ==
        reinterpret_cast<std::uintptr_t>(&coop::p99_group_native_frame::p99_group_invoke_native),
        "typed alias shares exact existing bridge body and unwind");
    check(std::memcmp(reinterpret_cast<const void*>(&p99_consumed_fixture_outer),
        frame::kOuterPrologue.data(), frame::kOuterPrologue.size()) == 0,
        "compiled outer has exact retail prologue bytes");
    check(std::memcmp(reinterpret_cast<const void*>(&p99_consumed_fixture_perform),
        frame::kPerformPrologue.data(), frame::kPerformPrologue.size()) == 0,
        "compiled Perform has exact retail prologue bytes");

    fixture_case(0, 1, 1, false);
    fixture_case(kSkipProcess, 0, 1, false);
    fixture_case(kSkipPost, 1, 0, false);
    fixture_case(kSkipProcess | kSkipPost, 0, 0, false);
    fixture_case(kNested, 2, 2, false);
    fixture_case(kDirectPerform, 2, 1, false);
    fixture_case(kDuplicatePost, 1, 2, false);
    fixture_case(kThrowProcess, 1, 0, true);
    fixture_case(kThrowPost, 1, 1, true);
    fixture_case(kTail, 1, 1, false);
    fixture_case(kTail | kSkipProcess | kSkipPost, 0, 0, false);
    fixture_case(kTail | kThrowTail, 1, 1, true);
    fixture_case(0, 1, 1, false); // reused call stack/job storage after exceptions
    std::printf("P99 consumed frame: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
