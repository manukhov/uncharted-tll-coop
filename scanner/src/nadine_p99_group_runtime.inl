// Included inside coop_runtime.cpp's anonymous namespace, after the source
// producer. This package never writes the persistent Fg+0x100 JointCache.
namespace p99_shadow = coop::p99_group_shadow;
namespace p99_program = coop::p99_group_program_manifest;
namespace p99_admit = coop::p99_group_admission;

struct P99GroupJob : coop::p99_group_scope::State {
    p99_shadow::SourceRows rows{};
    std::uintptr_t fg = 0;
    std::uintptr_t art = 0;
    std::uintptr_t raw = 0;
    std::uintptr_t scratch = 0;
    std::uint64_t nonce = 0;
    coop::p99_group_budget::Budget cost{};
};
struct P99GroupProcess {
    P99GroupJob* job = nullptr;
    const void* parent = nullptr;
    std::uint32_t group = UINT32_MAX;
    bool consumed = false;
};
struct P99FiberFrame : coop::p99_group_fiber::Binding {
    P99GroupJob* job = nullptr;
    P99GroupProcess* process = nullptr;
    std::uintptr_t native_return_slot = 0;
    coop::p99_group_entry::Active entry{};
};
coop::p99_group_fiber::Slot<P99FiberFrame> g_p99_group_fiber;
AnimManagerAsyncObjectUpdateJointBlendJobFunction g_p99_group_async = nullptr;
std::atomic<bool> g_p99_group_failed{false};
std::atomic<unsigned> g_p99_group_reject_logs{0};
std::atomic<unsigned> g_p99_group_apply_logs{0};
std::uint64_t g_p99_group_cost_limit = 0; // Published by shadow_ready release.
coop::p99_group_ingress::InstallationGate g_p99_group_install_gate;

struct P99CostStamp { LARGE_INTEGER value{}; bool valid = false; };
P99CostStamp p99_group_cost_start() {
    P99CostStamp stamp{};
    stamp.valid = QueryPerformanceCounter(&stamp.value) != FALSE && stamp.value.QuadPart >= 0;
    return stamp;
}
bool p99_group_cost_finish(P99GroupJob& job, P99CostStamp begin, const char* stage) {
    const auto end = p99_group_cost_start();
    if (job.cost.charge(static_cast<std::uint64_t>(begin.value.QuadPart),
            static_cast<std::uint64_t>(end.value.QuadPart), begin.valid && end.valid)) return true;
    job.refused = true;
    if (!g_p99_group_failed.exchange(true, std::memory_order_acq_rel)) {
        char line[384]{};
        const auto stage_ticks = begin.valid && end.valid && end.value.QuadPart >= begin.value.QuadPart
            ? static_cast<std::uint64_t>(end.value.QuadPart - begin.value.QuadPart) : 0;
        std::snprintf(line, sizeof(line),
            "P99-GROUP COST-STOP stage=%s completed=%u entered=%u publication=%s stage_ticks=%llu prior_ticks=%llu limit_ticks=%llu; future overlays disabled; no rollback/retry",
            stage, job.completed, job.entered, job.incomplete() ? "PARTIAL" : "NONE",
            static_cast<unsigned long long>(stage_ticks),
            static_cast<unsigned long long>(job.cost.spent),
            static_cast<unsigned long long>(job.cost.limit));
        boot_log_line(line);
    }
    return false;
}

void p99_group_reject(const char* reason) {
#if defined(TLL_P99_PREPARATION_AUDIT)
    (void)reason;
    if (g_p99_preparation_audit.stats) ++g_p99_preparation_audit.stats->rejected_calls;
    return;
#endif
    if (g_p99_group_reject_logs.fetch_add(1, std::memory_order_relaxed) < 24) {
        char line[192]{};
        std::snprintf(line, sizeof(line), "P99-GROUP REFUSED reason=%s", reason);
        boot_log_line(line);
    }
}
bool p99_group_gate_failed(const char* reason) {
    p99_group_reject(reason);
    return false;
}
void p99_group_partial(P99GroupJob& job, const char* reason) {
    job.refused = true;
    if (job.entered != 0 && !g_p99_group_failed.exchange(true)) {
        char line[256]{};
        std::snprintf(line, sizeof(line),
            "P99-GROUP PARTIAL nonce=%llu entered=%u completed=%u reason=%s; future overlays disabled; NO rollback/retry",
            static_cast<unsigned long long>(job.nonce), job.entered, job.completed, reason);
        boot_log_line(line);
    }
}
template<class T> T p99_group_load(const void* pointer, std::size_t offset = 0) {
    T value{};
    std::memcpy(&value, static_cast<const std::byte*>(pointer) + offset, sizeof(value));
    return value;
}
using P99ReadBatch = coop::p99_group_readability::Batch;
bool p99_group_readable(P99ReadBatch& pages, std::uintptr_t address, std::size_t bytes) {
    // The shared fast readable_bytes helper is capped around0xB000 and can
    // NEVER admit the native0x20000 arena. Walk precisely the requested pages
    // here; do not widen that unrelated hot-path probe or skip allocator checks.
    return pages.readable(address, bytes, P99_SOURCE_QUERY_FUNCTION);
}

bool p99_group_program_exact(P99ReadBatch& pages, std::uintptr_t raw, std::uintptr_t custom, std::uint16_t count) {
    if (!p99_group_readable(pages, raw, p99_program::kRequiredRawExtent) ||
        custom != g_module + p99_program::kCustomTableRva || count != p99_program::kCustomCount)
        return false;
    std::array<p99_program::RegionView, p99_program::kRegions.size()> regions{};
    for (std::size_t i=0;i<regions.size();++i) {
        const auto& spec=p99_program::kRegions[i];
        regions[i]={spec.id,spec.raw_relative,
            {reinterpret_cast<const std::byte*>(raw+spec.raw_relative),spec.byte_count}};
    }
    std::array<std::uintptr_t,p99_program::kBuiltinSnapshotEntries> builtin{};
    std::array<std::uintptr_t,p99_program::kCustomCount> customs{};
    std::memcpy(builtin.data(),reinterpret_cast<const void*>(g_module+p99_program::kBuiltinTableRva),sizeof(builtin));
    std::memcpy(customs.data(),reinterpret_cast<const void*>(custom),sizeof(customs));
    p99_program::Bindings bindings{};
    bindings.module_base=g_module;
    bindings.builtin_table_address=g_module+p99_program::kBuiltinTableRva;
    bindings.custom_table_address=custom;
    bindings.custom_count=count;
    bindings.builtin_entries=builtin;
    bindings.custom_entries=customs;
    return p99_program::validate(regions,bindings).admitted();
}

bool seh_p99_group_normal_job(void* job, P99GroupJob* output, P99SourceReader& pages) {
    __try {
        if (!pages.range(reinterpret_cast<std::uintptr_t>(job), 0x38) ||
            p99_group_load<std::uintptr_t>(job) == 0) return false;
        const auto fg = p99_group_load<std::uintptr_t>(job, 8);
        if (!pages.range(fg, 0x2A8)) return false;
        const auto* f = reinterpret_cast<const void*>(fg);
        const auto art = p99_group_load<std::uintptr_t>(f, 0x18);
        if (p99_group_load<std::uint8_t>(f, 6) != 2 ||
            (p99_group_load<std::uint8_t>(job, 0x14) != 0 &&
             (p99_group_load<std::uint8_t>(f, 8) & 8) == 0) ||
            p99_group_load<std::uintptr_t>(f, 0x2A0) != 0 ||
            art == 0 || art != p99_group_load<std::uintptr_t>(f, 0x1A0) ||
            *reinterpret_cast<const std::uint8_t*>(g_module + 0x3682F4B) != 0 ||
            !pages.range(art, 0x48)) return false;
        const auto raw = p99_group_load<std::uintptr_t>(reinterpret_cast<const void*>(art), 0x40);
        if (!pages.range(raw, 0x40) ||
            p99_group_load<std::uint32_t>(reinterpret_cast<const void*>(raw), 4) != p99_order::kNadineSkeletonId ||
            p99_group_load<std::uint16_t>(reinterpret_cast<const void*>(raw), 0x1A) != 6480) return false;
        output->fg = fg;
        output->art = art;
        output->raw = raw;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool prepare_p99_group_values(void* job, P99GroupJob* output) {
#if defined(TLL_P99_PREPARATION_AUDIT)
    if (!g_p99_preparation_audit.stats || g_p99_preparation_audit.image != g_module ||
        g_p99_preparation_audit.job != job) return false;
    g_p99_preparation_audit.stats->failed_stage = static_cast<std::uint32_t>(p99_prep_audit::Stage::kNormalJob);
#endif
    P99ChloeTransferEpoch epoch{};
    P99ChloeTransferTicket ticket{};
    constexpr auto purpose = P99SourcePurpose::kOwnedGroupValues;
    {
        // No native callback, logging, locking or source materialization in
        // this lexical phase. All normal-job and owner payload reads remain.
        P99SourceReader selection_pages(&P99_SOURCE_QUERY_FUNCTION, &p99_source_copy);
        if (!seh_p99_group_normal_job(job, output, selection_pages)) return false;
        P99_PREP_AUDIT_BEGIN(select, kSelect);
        if (!seh_select_p99_chloe_transfer_epoch(job, &epoch, purpose, &selection_pages)) return false;
        P99_PREP_AUDIT_END(select);
        if (reinterpret_cast<std::uintptr_t>(epoch.fg_anim_data) != output->fg ||
            reinterpret_cast<std::uintptr_t>(epoch.target_skeleton) != output->art) return false;
    }
    const auto result = seh_materialize_p99_chloe_embedded_epoch(&epoch, purpose);
    if (result != P99ChloeTransferMaterializeResult::kSuccess) {
        p99_group_reject(p99_chloe_transfer_materialize_reject(result));
        return false;
    }
    P99_PREP_AUDIT_BEGIN(seal, kSeal);
    if (!seal_p99_chloe_transfer_ticket(epoch, &ticket, purpose)) {
        p99_group_reject("owned-source-policy");
        return false;
    }
    P99_PREP_AUDIT_END(seal);
    output->rows = ticket.rows;
    output->nonce = ticket.nonce;
    // No owner, weapon, source/registry or local-pose pointer survives this
    // function. Fg/raw are opaque current-job identities, not a future lease.
    return true;
}

bool seh_p99_group_entry_observations(void* job, coop::p99_group_entry::Observations* output) {
    __try {
        // Native entry contract only: both producers give this invocation a
        // live job/Fg. Read no retained-cell/Player/weapon/Art pointer here.
        // Async producer141781502; sync141782AF3; native reader141A92A43.
        const auto* slot = reinterpret_cast<const void*>(g_module + kRetainedSlotTableRva);
        output->slot_before = {p99_group_load<std::uintptr_t>(slot),
            p99_group_load<std::uint32_t>(slot, 8)};
        if (!coop::p99_group_entry::same(output->slot_before, output->slot_before)) return false;
        const auto fg = p99_group_load<std::uintptr_t>(job, 8);
        if (fg == 0) return false;
        const auto* f = reinterpret_cast<const void*>(fg);
        // Exact copies of Process+ A8/BC: Fg producer14177F975,
        // slot producers1410F17E9/1410F3AC9; equality consumer1410F0DD0.
        output->fg = {p99_group_load<std::uintptr_t>(f, kFgAnimDataRetainedHandleOffset),
            p99_group_load<std::uint32_t>(f, kFgAnimDataRetainedIdOffset)};
        // Volatile reload: detect observed turnover, not an atomic pair/lease.
        const auto* again = reinterpret_cast<const volatile std::byte*>(slot);
        output->slot_after = {*reinterpret_cast<const volatile std::uintptr_t*>(again),
            *reinterpret_cast<const volatile std::uint32_t*>(again + 8)};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// No large source/job values are initialized for foreign animation objects.
// Keep this separate from the outer native-entry/fiber wrapper.
__declspec(noinline) void p99_group_candidate_async(void* job, P99FiberFrame& frame) {
    const auto begin = p99_group_cost_start();
    P99GroupJob current{};
    current.cost.limit = g_p99_group_cost_limit;
    const bool prepared = begin.valid && prepare_p99_group_values(job, &current);
    const bool within_cost = p99_group_cost_finish(current, begin, "source");
    if (prepared && within_cost && g_p99_group_fiber.owns(frame) && !frame.entry.reentered &&
        !g_p99_group_failed.load(std::memory_order_acquire)) frame.job = &current;
    __try { coop::p99_group_native_frame::p99_group_invoke_native(
        job, g_p99_group_async, &frame.native_return_slot); }
    __finally {
        frame.native_return_slot = 0;
        frame.job = nullptr;
        if (current.incomplete()) p99_group_partial(current, "job-ended-without-both-groups");
    }
}

void __fastcall hooked_p99_group_async(void* job) {
    P99FiberFrame frame{};
    P99FiberFrame* previous = nullptr;
    if (!g_p99_group_fiber.enter(frame, previous)) {
        g_p99_group_failed.store(true, std::memory_order_release);
        g_p99_group_async(job);
        return;
    }
    // No exception handler retries stock. The finally block expires all scope
    // identities even when native execution unwinds exceptionally.
    __try {
        if (previous != nullptr) {
            coop::p99_group_entry::mark_parent(&previous->entry);
            if (previous->job != nullptr) p99_group_partial(*previous->job, "nested-async");
        }
        const bool eligible = previous == nullptr &&
            job != nullptr && g_module != 0 && g_state_net.enabled && g_net_role_b &&
            g_p99_group_shadow_ready.load(std::memory_order_acquire) &&
            !g_p99_group_failed.load(std::memory_order_relaxed);
        const bool candidate = coop::p99_group_entry::admit(eligible,
            [&](coop::p99_group_entry::Observations& seen) { return seh_p99_group_entry_observations(job, &seen); },
            [] { return true; });
        if (candidate) p99_group_candidate_async(job, frame);
        else coop::p99_group_native_frame::p99_group_invoke_native(
            job, g_p99_group_async, &frame.native_return_slot);
    }
    __finally {
        frame.native_return_slot = 0;
        if (!g_p99_group_fiber.leave(frame, previous))
            g_p99_group_failed.store(true, std::memory_order_release);
    }
}

// Admission/glue is defined below; all pointer reads are isolated from native
// execution so a validation fault can refuse before, never retry after, cmd16.
bool seh_prepare_p99_group_process(const std::uint32_t*, std::uint32_t,
    const void*, std::uint32_t, const void*, P99GroupJob&);
bool seh_prepare_p99_group_shadow(const void*, P99GroupProcess*,
    p99_shadow::Shadow*, p99_shadow::Context*);

std::uint32_t p99_group_process_call(bool first_site,
    const std::uint32_t* commands, std::uint32_t words, const void* context,
    std::uint32_t group, const void* a5, const void* a6, const void* a7) {
    const auto stock = reinterpret_cast<ProcessAnimCmdListSegmentFunction>(
        g_module + kProcessAnimCmdListSegmentRva);
    auto* frame = g_p99_group_fiber.current();
    if (frame == nullptr || g_p99_group_failed.load(std::memory_order_acquire))
        return stock(commands, words, context, group, a5, a6, a7);
    auto* previous = frame->process;
    P99GroupProcess process{};
    auto* job = frame->job;
    std::uint32_t result = 0;
    __try {
      frame->process = nullptr;
      const auto native_scope = coop::p99_group_native_frame::admit(first_site,
          reinterpret_cast<std::uintptr_t>(context), frame->native_return_slot, group);
      if (!native_scope.same_invocation) {
        // An old/unwrapped recursive native call can have the SAME Fg and job
        // pointer. Hide its Process scope for the complete stock invocation,
        // then restore under finally; it must not inherit the parent's cmd16.
        frame->entry.reentered = true;
        if (job != nullptr) p99_group_partial(*job, "foreign-native-frame");
      }
      if (native_scope.may_consider_overlay && job != nullptr && !job->refused) {
        const auto begin = p99_group_cost_start();
        const bool prepared = begin.valid && previous == nullptr && job->can_start(group,first_site) &&
            !g_p99_group_failed.load(std::memory_order_relaxed) &&
            seh_prepare_p99_group_process(commands, words, context, group, a6, *job);
        const bool within_cost = p99_group_cost_finish(*job, begin, group == 0 ? "process0" : "process1");
        if (!prepared || !within_cost || job->refused || frame->entry.reentered ||
            !g_p99_group_fiber.owns(*frame) ||
            g_p99_group_failed.load(std::memory_order_acquire)) {
            p99_group_partial(*job, "process-admission");
            p99_group_reject("process-admission");
        } else {
            process.job = job;
            process.parent = context;
            process.group = group;
            frame->process = &process;
        }
      }
      result = stock(commands, words, context, group, a5, a6, a7);
    }
    __finally {
        frame->process = previous;
        if (process.job != nullptr && !process.consumed)
            p99_group_partial(*process.job, "process-missed-cmd16");
    }
    return result;
}
std::uint32_t __fastcall hooked_p99_group_process0(
    const std::uint32_t* c, std::uint32_t n, const void* p,
    std::uint32_t g, const void* a5, const void* a6, const void* a7) {
    return p99_group_process_call(true, c, n, p, g, a5, a6, a7);
}
std::uint32_t __fastcall hooked_p99_group_process1(
    const std::uint32_t* c, std::uint32_t n, const void* p,
    std::uint32_t g, const void* a5, const void* a6, const void* a7) {
    return p99_group_process_call(false, c, n, p, g, a5, a6, a7);
}
std::uint32_t* __fastcall hooked_p99_group_cmd16(std::uint32_t* status, const void* context) {
    const auto stock = reinterpret_cast<AnimCmdExecuteJointProgramsFunction>(
        g_module + kAnimCmdExecuteJointProgramsRva);
    auto* frame = g_p99_group_fiber.current();
    if (frame == nullptr || g_p99_group_failed.load(std::memory_order_acquire)) return stock(status, context);
    auto* process = frame->process;
    if (process == nullptr || process->job == nullptr) return stock(status, context);
    auto& job = *process->job;
    const auto begin = p99_group_cost_start();
    p99_shadow::Shadow shadow{}; // Final stable address, never moved below.
    alignas(16) p99_shadow::Context copied{};
    const bool prepared = begin.valid && !process->consumed && job.can_start(process->group,process->group==0) &&
        coop::p99_group_native_frame::owns(process->group==0,
            reinterpret_cast<std::uintptr_t>(process->parent), frame->native_return_slot) &&
        !g_p99_group_failed.load(std::memory_order_acquire) &&
        seh_prepare_p99_group_shadow(context, process, &shadow, &copied);
    const bool within_cost = p99_group_cost_finish(job, begin, process->group == 0 ? "cmd16-0" : "cmd16-1");
    if (!prepared || !within_cost || job.refused || frame->entry.reentered ||
        !g_p99_group_fiber.owns(*frame) ||
        g_p99_group_failed.load(std::memory_order_acquire)) {
        p99_group_partial(job, "cmd16-admission");
        p99_group_reject("cmd16-admission");
        return stock(status, context);
    }
    if (!job.enter(process->group)) {
        p99_group_partial(job, "cmd16-sequence-changed");
        return stock(status, context);
    }
    process->consumed = true;
    auto* result = stock(status, copied.data()); // Exactly once; no retry.
    if (!job.finish_after_native(process->group, g_p99_group_fiber.owns(*frame) &&
            coop::p99_group_native_frame::owns(process->group==0,
                reinterpret_cast<std::uintptr_t>(process->parent), frame->native_return_slot),
            g_p99_group_failed.load(std::memory_order_acquire))) {
        p99_group_partial(job, "cmd16-scope-lost-after-native");
        return result;
    }
    if (job.completed == 3 && g_p99_group_apply_logs.fetch_add(1) < 8) {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "P99-GROUP EXECUTED nonce=%llu groups=2 rows=21 persistent-writes=0; visual result unverified",
            static_cast<unsigned long long>(job.nonce));
        boot_log_line(line);
    }
    return result;
}

bool p99_group_identity_exact(P99ReadBatch& pages, const P99GroupJob& job) {
    if (!p99_group_readable(pages,job.fg,0x2A8) || !p99_group_readable(pages,job.art,0x48)) return false;
    const auto* fg=reinterpret_cast<const void*>(job.fg);
    return p99_group_load<std::uintptr_t>(fg,0x18)==job.art &&
        p99_group_load<std::uintptr_t>(fg,0x1A0)==job.art &&
        p99_group_load<std::uintptr_t>(fg,0x2A0)==0 &&
        p99_group_load<std::uint8_t>(fg,6)==2 &&
        p99_group_load<std::uintptr_t>(reinterpret_cast<const void*>(job.art),0x40)==job.raw &&
        *reinterpret_cast<const std::uint8_t*>(g_module+0x3682F4B)==0;
}

bool seh_prepare_p99_group_process(const std::uint32_t* commands, std::uint32_t words,
    const void* context, std::uint32_t group, const void* callback, P99GroupJob& job) {
    P99ReadBatch pages; // Never reused by cmd16 or the other group's Process.
    __try {
        if (group>1 || words==0 || words>p99_admit::kMaxCommandWords ||
            !p99_group_identity_exact(pages,job) ||
            !p99_group_readable(pages,reinterpret_cast<std::uintptr_t>(context),0x28) ||
            !p99_group_readable(pages,reinterpret_cast<std::uintptr_t>(commands),words*sizeof(std::uint32_t)) ||
            p99_group_load<std::uintptr_t>(context)!=job.art ||
            p99_group_load<std::uintptr_t>(context,0x10)!=job.fg+0x1C0 ||
            p99_group_load<std::uint32_t>(context,0x20)!=0) return false;
        const auto scratch=p99_group_load<std::uintptr_t>(context,0x18);
        if (!p99_group_readable(pages,scratch,6480) || (scratch&15)!=0 ||
            (group==1 && scratch!=job.scratch)) return false;
        const auto count=*reinterpret_cast<const std::uint16_t*>(g_module+p99_program::kCustomCountRva);
        if (!p99_group_program_exact(pages,job.raw,g_module+p99_program::kCustomTableRva,count)) return false;
        const auto* fg=reinterpret_cast<const void*>(job.fg);
        p99_admit::CommandBindings binding{};
        binding.image_base=g_module;
        binding.postpose_callback=reinterpret_cast<std::uintptr_t>(callback);
        binding.pose_source=p99_group_load<std::uintptr_t>(fg,0x100);
        binding.scalar_source=p99_group_load<std::uintptr_t>(fg,0x120);
        binding.cmd15_source=p99_group_load<std::uintptr_t>(fg,0x118);
        binding.conversion_destination=p99_group_load<std::uintptr_t>(fg,0x110);
        binding.sphere_arguments={p99_group_load<std::uintptr_t>(fg,0x200),job.fg+0x1C0,job.fg};
        const std::uint64_t offset=group==0?0:104*48;
        const auto palette0=p99_group_load<std::uintptr_t>(fg,0x230);
        const auto palette1=p99_group_load<std::uintptr_t>(fg,0x240);
        if (palette0>UINTPTR_MAX-offset || palette1>UINTPTR_MAX-offset) return false;
        binding.palette_arguments={palette0+offset,palette1+offset,job.fg+0x220};
        std::array<std::uint32_t,p99_admit::kMaxCommandWords> copied{};
        std::memcpy(copied.data(),commands,words*sizeof(std::uint32_t));
        const auto decision=p99_admit::validate_commands(static_cast<std::uint16_t>(group),
            {copied.data(),words},binding);
        if (!decision.accepted()) {
            char reason[96]{};
            std::snprintf(reason,sizeof(reason),"command-g%u-code%u",group,static_cast<unsigned>(decision.reason));
            p99_group_reject(reason);
            return false;
        }
        if (group==0) job.scratch=scratch;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool seh_prepare_p99_group_shadow(const void* context, P99GroupProcess* process,
    p99_shadow::Shadow* shadow, p99_shadow::Context* copied) {
    P99ReadBatch pages; // Destroyed before native cmd16; no cross-group approval.
    __try {
        const auto& job=*process->job;
        if (!p99_group_identity_exact(pages,job) ||
            !p99_group_readable(pages,reinterpret_cast<std::uintptr_t>(context),0x80))
            return p99_group_gate_failed("cmd16-job/context-range");
        alignas(16) p99_shadow::Context original{};
        std::memcpy(original.data(),context,original.size());
        const auto* c=original.data();
        const auto group=p99_group_load<std::uint16_t>(c,0x48);
        const auto custom=p99_group_load<std::uintptr_t>(c,0x10);
        const auto custom_count=p99_group_load<std::uint16_t>(c,0x18);
        if (group!=process->group || group>1 ||
            p99_group_load<std::uintptr_t>(c,8)!=reinterpret_cast<std::uintptr_t>(process->parent) ||
            p99_group_load<std::uint32_t>(process->parent,0x20)!=0 ||
            (p99_group_load<std::uint16_t>(c,0x4A)&0x300)!=0 ||
            p99_group_load<std::uintptr_t>(c,0x20)!=0 ||
            p99_group_load<std::uintptr_t>(c,0x28)!=job.raw ||
            p99_group_load<std::uintptr_t>(c,0x30)!=job.scratch ||
            !p99_group_program_exact(pages,job.raw,custom,custom_count))
            return p99_group_gate_failed("cmd16-context/program-binding");
        const auto descriptor=job.raw+0x40+group*0x20;
        const auto processing_index=p99_group_load<std::uint16_t>(reinterpret_cast<const void*>(descriptor),2);
        // Program manifest pins both descriptor tables before these reads.
        const auto processing=job.raw+0x140+static_cast<std::uintptr_t>(processing_index)*10;
        if (processing_index!=group || p99_group_load<std::uintptr_t>(c,0x38)!=descriptor ||
            p99_group_load<std::uintptr_t>(c,0x40)!=processing)
            return p99_group_gate_failed("cmd16-descriptors");
        p99_admit::WorkBuffer work{};
        work.address=p99_group_load<std::uintptr_t>(c);
        if (!p99_group_readable(pages,work.address,88)) return p99_group_gate_failed("cmd16-workbuffer-range");
        const auto* w=reinterpret_cast<const void*>(work.address);
        work.arena=p99_group_load<std::uint64_t>(w,0);
        work.arena_bytes=p99_group_load<std::uint64_t>(w,8);
        work.metadata=p99_group_load<std::uint64_t>(w,0x10);
        work.metadata_bytes=p99_group_load<std::uint64_t>(w,0x18);
        work.free_head=p99_group_load<std::uint64_t>(w,0x20);
        work.table=p99_group_load<std::uint64_t>(w,0x28);
        work.front_cursor=p99_group_load<std::uint64_t>(w,0x30);
        work.back_cursor=p99_group_load<std::uint64_t>(w,0x38);
        work.front_count=p99_group_load<std::uint32_t>(w,0x48);
        work.back_count=p99_group_load<std::uint32_t>(w,0x4C);
        work.capacity=p99_group_load<std::uint32_t>(w,0x50);
        if (work.arena_bytes!=p99_admit::kArenaBytes || work.metadata_bytes!=p99_admit::kMetadataBytes ||
            !p99_group_readable(pages,work.arena,p99_admit::kArenaBytes) ||
            !p99_group_readable(pages,work.metadata,p99_admit::kMetadataBytes))
            return p99_group_gate_failed("cmd16-arena/metadata-range");
        std::array<p99_admit::Allocation,p99_admit::kCapacity> records{};
        std::array<p99_admit::Address,p99_admit::kCapacity> table{};
        std::memcpy(records.data(),reinterpret_cast<const void*>(work.metadata),sizeof(records));
        std::memcpy(table.data(),reinterpret_cast<const void*>(work.metadata+p99_admit::kTableOffset),sizeof(table));
        const p99_admit::Metadata metadata{work,records,table};
        const p99_admit::Segment segment{group,p99_group_load<std::uint16_t>(c,0x4A),
            p99_group_load<std::uintptr_t>(c,0x50),p99_group_load<std::uintptr_t>(c,0x58),
            p99_group_load<std::uintptr_t>(c,0x60),p99_group_load<std::uintptr_t>(c,0x68)};
        const auto allocation=p99_admit::validate_group_allocations(metadata,segment);
        if (!allocation.accepted()) {
            char reason[96]{};
            std::snprintf(reason,sizeof(reason),"allocation-g%u-code%u",group,static_cast<unsigned>(allocation.reason));
            p99_group_reject(reason); return false;
        }
        const p99_admit::Isolation isolation{{job.scratch,6480},
            {reinterpret_cast<std::uintptr_t>(context),0x80},
            {reinterpret_cast<std::uintptr_t>(copied->data()),0x80},
            {reinterpret_cast<std::uintptr_t>(shadow->bytes.data()),shadow->bytes.size()},
            {reinterpret_cast<std::uintptr_t>(job.rows.data()),sizeof(job.rows)}};
        if (!p99_admit::validate_isolation(work,segment,isolation).accepted())
            return p99_group_gate_failed("cmd16-isolation");
        const auto& shape=p99_order::kNadineGroups[group];
        auto result=p99_shadow::build({group,shape.first,shape.parameter_count,shape.count},
            {reinterpret_cast<const std::byte*>(segment.joint_params),shape.count*p99_shadow::kRowBytes},
            job.rows,p99_shadow::kCompleteMask);
        if (result.reason!=p99_shadow::Reject::kNone) return p99_group_gate_failed("cmd16-shadow-values");
        *shadow=result.shadow; // Move value BEFORE forming self-referential copy.
        const auto context_copy=p99_shadow::copied_context(original,*shadow);
        if (!context_copy) return p99_group_gate_failed("cmd16-context-copy");
        *copied=*context_copy;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return p99_group_gate_failed("cmd16-read-exception"); }
}

bool install_p99_group_shadow() {
    if (!g_p99_group_install_gate.begin())
        return g_p99_group_shadow_ready.load(std::memory_order_acquire);
    g_p99_group_shadow_ready.store(false, std::memory_order_release);
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart < 1000) return false;
    g_p99_group_cost_limit = coop::p99_group_budget::one_millisecond(
        static_cast<std::uint64_t>(frequency.QuadPart));
    if (g_enable_aim_case6_probe || kEnableLegacyNadineP99ChloeGripTransfer ||
        kEnableLegacyNadineMazurSteadyHandsCommandCensus ||
        kEnableLegacyNadineMazurDrawEndpointRepair ||
        kEnableLegacyNadinePistolPoseGripRepair ||
        kEnableLegacyNadineActiveWeaponGripOverlayRepair) return false;
    if (!g_p99_group_fiber.initialize()) return false;
    // Verification only. Never copy or overwrite this unwind-described code.
    constexpr std::array<std::uint8_t,12> process_prologue{
        0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
    struct Site {
        std::uint8_t* address = nullptr;
        std::uint8_t* allocation = nullptr;
        std::size_t size = 0;
        std::array<std::uint8_t,7> before{}, after{};
    };
    std::array<Site,5> sites{};
    const auto* native_async = reinterpret_cast<const std::uint8_t*>(
        g_module + kAnimManagerAsyncObjectUpdateJointBlendJobRva);
    static_assert(kAnimManagerAsyncObjectUpdateJointBlendJobRva ==
        coop::p99_group_ingress::kNativeJobRva);
    const auto stock_process = [&]() {
        return std::memcmp(reinterpret_cast<const void*>(g_module + kProcessAnimCmdListSegmentRva),
            process_prologue.data(), process_prologue.size()) == 0 &&
            coop::p99_group_native_frame::layout_matches(
                {native_async, coop::p99_group_native_frame::kNativePrologue.size()},
                {reinterpret_cast<const std::uint8_t*>(g_module +
                    coop::p99_group_native_frame::kContextLeaRvas[0]), 8},
                {reinterpret_cast<const std::uint8_t*>(g_module +
                    coop::p99_group_native_frame::kContextLeaRvas[1]), 8});
    };
    const auto free_allocations = [&]() {
        for (auto& s : sites) if (s.allocation != nullptr) VirtualFree(s.allocation,0,MEM_RELEASE);
    };
    if (!stock_process()) return false;
    for (std::size_t i=0;i<coop::p99_group_ingress::kSites.size();++i) {
        auto& s=sites[i];
        const auto& spec=coop::p99_group_ingress::kSites[i];
        s.address=reinterpret_cast<std::uint8_t*>(g_module+spec.rva);
        s.size=spec.size;
        std::memcpy(s.before.data(),s.address,s.size);
        // Reject a changed instruction before allocating executable memory.
        if (std::memcmp(s.before.data(),spec.stock.data(),s.size)!=0) {
            free_allocations(); return false;
        }
        s.allocation=static_cast<std::uint8_t*>(allocate_executable_near(
            s.address,coop::p99_group_ingress::kRelayBytes));
        const auto patch=coop::p99_group_ingress::plan(i,g_module,
            {s.before.data(),s.size},reinterpret_cast<std::uintptr_t>(s.allocation),
            reinterpret_cast<std::uintptr_t>(&hooked_p99_group_async));
        if (!patch) { free_allocations(); return false; }
        s.after=patch->instruction;
        std::memcpy(s.allocation,patch->relay.data(),patch->relay.size());
        FlushInstructionCache(GetCurrentProcess(),s.allocation,patch->relay.size());
    }
    constexpr std::array<std::uintptr_t,3> rvas{0x1A92E78,0x1A932F8,kAnimCmdExecuteJointProgramsCallsiteRva};
    const std::array<const void*,3> hooks{reinterpret_cast<const void*>(&hooked_p99_group_process0),
        reinterpret_cast<const void*>(&hooked_p99_group_process1),reinterpret_cast<const void*>(&hooked_p99_group_cmd16)};
    for (std::size_t i=0;i<3;++i) {
        auto& s=sites[i+2]; std::int32_t displacement=0; s.size=5;
        if (!prepare_plain_callsite_relay(rvas[i],i==2?kAnimCmdExecuteJointProgramsRva:kProcessAnimCmdListSegmentRva,
            hooks[i],s.address,s.allocation,displacement)) { free_allocations(); return false; }
        std::memcpy(s.before.data(),s.address,5);
        s.after=s.before; s.after[0]=0xE8; std::memcpy(s.after.data()+1,&displacement,4);
    }
    // The unwind-described call bridge CALLs the complete native function and
    // publishes its exact return slot. No moved prologue or jump-back.
    g_p99_group_async = reinterpret_cast<AnimManagerAsyncObjectUpdateJointBlendJobFunction>(
        g_module + kAnimManagerAsyncObjectUpdateJointBlendJobRva);
    const auto all_match = [&](bool patched) {
        for (const auto& s:sites) if (std::memcmp(s.address,(patched?s.after:s.before).data(),s.size)!=0) return false;
        return stock_process();
    };
    // Collect unique pages BEFORE suspending threads (no allocating while
    // another suspended thread might own a process heap lock).
    struct Page { void* address=nullptr; DWORD old=0; bool writable=false; };
    std::array<Page,5> pages{}; std::size_t page_count=0;
    for (const auto& s:sites) {
        const auto address=reinterpret_cast<std::uintptr_t>(s.address);
        if ((address>>12)!=((address+s.size-1)>>12)) { free_allocations(); return false; }
        auto* base=reinterpret_cast<void*>(address&~std::uintptr_t{0xFFF});
        bool found=false; for(std::size_t i=0;i<page_count;++i) found|=pages[i].address==base;
        if(!found) pages[page_count++].address=base;
    }
    SuspendedProcessThreads suspended{};
    if(!suspend_other_process_threads(suspended,THREAD_GET_CONTEXT)) { free_allocations(); return false; }
    bool safe=all_match(false);
    for(std::size_t i=0;safe&&i<suspended.count;++i) {
        CONTEXT c{}; c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(suspended.handles[i],&c)) {safe=false;break;}
        for(const auto& s:sites) {
            const auto address=reinterpret_cast<std::uintptr_t>(s.address);
            if(c.Rip>=address&&c.Rip<address+s.size) safe=false;
        }
    }
    bool wrote=false,installed=false,restored=true;
    if(safe) {
        for(std::size_t i=0;i<page_count;++i) {
            pages[i].writable=VirtualProtect(pages[i].address,0x1000,PAGE_EXECUTE_READWRITE,&pages[i].old)!=0;
            if(!pages[i].writable) {safe=false;break;}
        }
    }
    if(safe) {
        wrote=true;
        for(const auto& s:sites) {std::memcpy(s.address,s.after.data(),s.size);FlushInstructionCache(GetCurrentProcess(),s.address,s.size);}
        installed=all_match(true);
        if(!installed) for(const auto& s:sites) {std::memcpy(s.address,s.before.data(),s.size);FlushInstructionCache(GetCurrentProcess(),s.address,s.size);}
    }
    const auto restore_pages = [&]() {
        bool success=true;
        for(std::size_t i=0;i<page_count;++i) if(pages[i].writable) {
            DWORD ignored=0;
            if(VirtualProtect(pages[i].address,0x1000,pages[i].old,&ignored)) pages[i].writable=false;
            else success=false; // Preserve known writable state on failure.
        }
        return success;
    };
    restored=restore_pages();
    if(!restored) {
        if(installed) {
            bool reopened=true;
            for(std::size_t i=0;i<page_count;++i) if(!pages[i].writable) {
                DWORD ignored=0;
                if(VirtualProtect(pages[i].address,0x1000,PAGE_EXECUTE_READWRITE,&ignored)) pages[i].writable=true;
                else reopened=false;
            }
            if(reopened) for(const auto& s:sites) {std::memcpy(s.address,s.before.data(),s.size);FlushInstructionCache(GetCurrentProcess(),s.address,s.size);}
            installed=false; // Any restoration fault makes this package inert.
        }
        // Also covers partial protect before writes and failed-patch rollback.
        // Never reset a failed page's state or pretend an unknown page is RX.
        for(unsigned attempt=0;attempt<3&&!restored;++attempt) restored=restore_pages();
    }
    const bool ready=installed&&restored&&all_match(true);
    const bool clean=!ready&&restored&&all_match(false);
    if(ready) g_p99_group_shadow_ready.store(true,std::memory_order_release);
    resume_suspended_process_threads(suspended);
    // Partial installation must retain every potentially reachable leaf relay;
    // ready=false makes all reachable hooks stock-only.
    if(clean||(!wrote&&restored)) free_allocations();
    boot_log_line(ready?"P99-GROUP INSTALLED five sites; native Async prologue intact; original buffers preserved":
        (clean||(!wrote&&restored))?"P99-GROUP SKIPPED stock/preflight or verified rollback":
            "P99-GROUP PARTIAL pass-through; allocations retained; inspect code protections");
    return ready;
}
