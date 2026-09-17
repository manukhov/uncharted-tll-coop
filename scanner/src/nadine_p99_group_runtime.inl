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
};
struct P99GroupProcess {
    P99GroupJob* job = nullptr;
    const void* parent = nullptr;
    std::uint32_t group = UINT32_MAX;
    bool consumed = false;
};
thread_local P99GroupJob* g_p99_group_job = nullptr;
thread_local P99GroupProcess* g_p99_group_process = nullptr;
AnimManagerAsyncObjectUpdateJointBlendJobFunction g_p99_group_async = nullptr;
std::atomic<bool> g_p99_group_failed{false};
std::atomic<unsigned> g_p99_group_reject_logs{0};
std::atomic<unsigned> g_p99_group_apply_logs{0};

void p99_group_reject(const char* reason) {
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
bool p99_group_readable(std::uintptr_t address, std::size_t bytes) {
    // The shared fast readable_bytes helper is capped around0xB000 and can
    // NEVER admit the native0x20000 arena. Walk precisely the requested pages
    // here; do not widen that unrelated hot-path probe or skip allocator checks.
    return coop::p99_group_readability::readable(address,bytes,
        [](std::uintptr_t cursor,coop::p99_group_readability::Region& region) {
            MEMORY_BASIC_INFORMATION mbi{};
            if(VirtualQuery(reinterpret_cast<const void*>(cursor),&mbi,sizeof(mbi))!=sizeof(mbi)) return false;
            const auto protection=mbi.Protect&0xFF;
            const bool accessible=protection==PAGE_READONLY || protection==PAGE_READWRITE ||
                protection==PAGE_EXECUTE_READ || protection==PAGE_EXECUTE_READWRITE ||
                protection==PAGE_WRITECOPY || protection==PAGE_EXECUTE_WRITECOPY;
            region={reinterpret_cast<std::uintptr_t>(mbi.BaseAddress),mbi.RegionSize,
                mbi.State==MEM_COMMIT,accessible,(mbi.Protect&PAGE_GUARD)!=0};
            return true;
        });
}

bool p99_group_program_exact(std::uintptr_t raw, std::uintptr_t custom, std::uint16_t count) {
    if (!p99_group_readable(raw, p99_program::kRequiredRawExtent) ||
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

bool seh_p99_group_normal_job(void* job, P99GroupJob* output) {
    __try {
        if (!p99_group_readable(reinterpret_cast<std::uintptr_t>(job), 0x38) ||
            p99_group_load<std::uintptr_t>(job) == 0) return false;
        const auto fg = p99_group_load<std::uintptr_t>(job, 8);
        if (!p99_group_readable(fg, 0x2A8)) return false;
        const auto* f = reinterpret_cast<const void*>(fg);
        const auto art = p99_group_load<std::uintptr_t>(f, 0x18);
        if (p99_group_load<std::uint8_t>(f, 6) != 2 ||
            (p99_group_load<std::uint8_t>(job, 0x14) != 0 &&
             (p99_group_load<std::uint8_t>(f, 8) & 8) == 0) ||
            p99_group_load<std::uintptr_t>(f, 0x2A0) != 0 ||
            art == 0 || art != p99_group_load<std::uintptr_t>(f, 0x1A0) ||
            *reinterpret_cast<const std::uint8_t*>(g_module + 0x3682F4B) != 0 ||
            !p99_group_readable(art, 0x48)) return false;
        const auto raw = p99_group_load<std::uintptr_t>(reinterpret_cast<const void*>(art), 0x40);
        if (!p99_group_readable(raw, 0x40) ||
            p99_group_load<std::uint32_t>(reinterpret_cast<const void*>(raw), 4) != p99_order::kNadineSkeletonId ||
            p99_group_load<std::uint16_t>(reinterpret_cast<const void*>(raw), 0x1A) != 6480) return false;
        output->fg = fg;
        output->art = art;
        output->raw = raw;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool prepare_p99_group_values(void* job, P99GroupJob* output) {
    if (!seh_p99_group_normal_job(job, output)) return false;
    P99ChloeTransferEpoch epoch{};
    P99ChloeTransferTicket ticket{};
    constexpr auto purpose = P99SourcePurpose::kOwnedGroupValues;
    if (!seh_select_p99_chloe_transfer_epoch(job, &epoch, purpose)) return false;
    if (reinterpret_cast<std::uintptr_t>(epoch.fg_anim_data) != output->fg ||
        reinterpret_cast<std::uintptr_t>(epoch.target_skeleton) != output->art) return false;
    const auto result = seh_materialize_p99_chloe_embedded_epoch(&epoch, purpose);
    if (result != P99ChloeTransferMaterializeResult::kSuccess) {
        p99_group_reject(p99_chloe_transfer_materialize_reject(result));
        return false;
    }
    if (!seal_p99_chloe_transfer_ticket(epoch, &ticket, purpose)) {
        p99_group_reject("owned-source-policy");
        return false;
    }
    output->rows = ticket.rows;
    output->nonce = ticket.nonce;
    // No owner, weapon, source/registry or local-pose pointer survives this
    // function. Fg/raw are opaque current-job identities, not a future lease.
    return true;
}

void __fastcall hooked_p99_group_async(void* job) {
    auto* previous_job = g_p99_group_job;
    auto* previous_process = g_p99_group_process;
    P99GroupJob current{};
    g_p99_group_job = nullptr;
    g_p99_group_process = nullptr;
    if (previous_job != nullptr) {
        p99_group_partial(*previous_job, "nested-async");
    } else if (g_p99_group_shadow_ready.load(std::memory_order_acquire) &&
               !g_p99_group_failed.load(std::memory_order_relaxed) &&
               prepare_p99_group_values(job, &current)) {
        g_p99_group_job = &current;
    }
    // No exception handler retries stock. The finally block expires all scope
    // identities even when native execution unwinds exceptionally.
    __try { g_p99_group_async(job); }
    __finally {
        if (current.incomplete())
            p99_group_partial(current, "job-ended-without-both-groups");
        g_p99_group_process = previous_process;
        g_p99_group_job = previous_job;
    }
}

// Admission/glue is defined below; all pointer reads are isolated from native
// execution so a validation fault can refuse before, never retry after, cmd16.
bool seh_prepare_p99_group_process(const std::uint32_t*, std::uint32_t,
    const void*, std::uint32_t, const void*, P99GroupProcess*);
bool seh_prepare_p99_group_shadow(const void*, P99GroupProcess*,
    p99_shadow::Shadow*, p99_shadow::Context*);

std::uint32_t p99_group_process_call(bool first_site,
    const std::uint32_t* commands, std::uint32_t words, const void* context,
    std::uint32_t group, const void* a5, const void* a6, const void* a7) {
    const auto stock = reinterpret_cast<ProcessAnimCmdListSegmentFunction>(
        g_module + kProcessAnimCmdListSegmentRva);
    auto* previous = g_p99_group_process;
    P99GroupProcess process{};
    auto* job = g_p99_group_job;
    g_p99_group_process = nullptr;
    if (job != nullptr && !job->refused && group <= 1) {
        if (previous != nullptr || !job->can_start(group,first_site) ||
            g_p99_group_failed.load(std::memory_order_relaxed) ||
            !seh_prepare_p99_group_process(commands, words, context, group, a6, &process)) {
            p99_group_partial(*job, "process-admission");
            p99_group_reject("process-admission");
        } else {
            process.job = job;
            process.parent = context;
            process.group = group;
            g_p99_group_process = &process;
        }
    }
    std::uint32_t result = 0;
    __try { result = stock(commands, words, context, group, a5, a6, a7); }
    __finally {
        if (process.job != nullptr && !process.consumed)
            p99_group_partial(*process.job, "process-missed-cmd16");
        g_p99_group_process = previous;
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
    auto* process = g_p99_group_process;
    if (process == nullptr || process->job == nullptr) return stock(status, context);
    auto& job = *process->job;
    p99_shadow::Shadow shadow{}; // Final stable address, never moved below.
    alignas(16) p99_shadow::Context copied{};
    if (process->consumed || !job.can_start(process->group,process->group==0) ||
        !seh_prepare_p99_group_shadow(context, process, &shadow, &copied)) {
        p99_group_partial(job, "cmd16-admission");
        p99_group_reject("cmd16-admission");
        return stock(status, context);
    }
    process->consumed = true;
    (void)job.enter(process->group); // can_start proved above; TLS not shared.
    auto* result = stock(status, copied.data()); // Exactly once; no retry.
    job.finish(process->group);
    if (job.completed == 3 && g_p99_group_apply_logs.fetch_add(1) < 8) {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "P99-GROUP EXECUTED nonce=%llu groups=2 rows=21 persistent-writes=0; visual result unverified",
            static_cast<unsigned long long>(job.nonce));
        boot_log_line(line);
    }
    return result;
}

bool p99_group_identity_exact(const P99GroupJob& job) {
    if (!p99_group_readable(job.fg,0x2A8) || !p99_group_readable(job.art,0x48)) return false;
    const auto* fg=reinterpret_cast<const void*>(job.fg);
    return p99_group_load<std::uintptr_t>(fg,0x18)==job.art &&
        p99_group_load<std::uintptr_t>(fg,0x1A0)==job.art &&
        p99_group_load<std::uintptr_t>(fg,0x2A0)==0 &&
        p99_group_load<std::uint8_t>(fg,6)==2 &&
        p99_group_load<std::uintptr_t>(reinterpret_cast<const void*>(job.art),0x40)==job.raw &&
        *reinterpret_cast<const std::uint8_t*>(g_module+0x3682F4B)==0;
}

bool seh_prepare_p99_group_process(const std::uint32_t* commands, std::uint32_t words,
    const void* context, std::uint32_t group, const void* callback, P99GroupProcess*) {
    __try {
        auto& job=*g_p99_group_job;
        if (group>1 || words==0 || words>p99_admit::kMaxCommandWords ||
            !p99_group_identity_exact(job) ||
            !p99_group_readable(reinterpret_cast<std::uintptr_t>(context),0x28) ||
            !p99_group_readable(reinterpret_cast<std::uintptr_t>(commands),words*sizeof(std::uint32_t)) ||
            p99_group_load<std::uintptr_t>(context)!=job.art ||
            p99_group_load<std::uintptr_t>(context,0x10)!=job.fg+0x1C0 ||
            p99_group_load<std::uint32_t>(context,0x20)!=0) return false;
        const auto scratch=p99_group_load<std::uintptr_t>(context,0x18);
        if (!p99_group_readable(scratch,6480) || (scratch&15)!=0 ||
            (group==1 && scratch!=job.scratch)) return false;
        const auto count=*reinterpret_cast<const std::uint16_t*>(g_module+p99_program::kCustomCountRva);
        if (!p99_group_program_exact(job.raw,g_module+p99_program::kCustomTableRva,count)) return false;
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
    __try {
        const auto& job=*process->job;
        if (!p99_group_identity_exact(job) ||
            !p99_group_readable(reinterpret_cast<std::uintptr_t>(context),0x80))
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
            !p99_group_program_exact(job.raw,custom,custom_count))
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
        if (!p99_group_readable(work.address,88)) return p99_group_gate_failed("cmd16-workbuffer-range");
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
            !p99_group_readable(work.arena,p99_admit::kArenaBytes) ||
            !p99_group_readable(work.metadata,p99_admit::kMetadataBytes))
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
    g_p99_group_shadow_ready.store(false, std::memory_order_release);
    if (g_enable_aim_case6_probe || kEnableLegacyNadineP99ChloeGripTransfer ||
        kEnableLegacyNadineMazurSteadyHandsCommandCensus ||
        kEnableLegacyNadineMazurDrawEndpointRepair ||
        kEnableLegacyNadinePistolPoseGripRepair ||
        kEnableLegacyNadineActiveWeaponGripOverlayRepair) return false;
    constexpr std::array<std::uint8_t,43> prologue{
        0x48,0x8B,0xC4,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x81,0xEC,0xD0,0x05,0,0,0x48,0xC7,0x80,0x48,0xFB,0xFF,
        0xFF,0xFE,0xFF,0xFF,0xFF,0x48,0x89,0x58,0x10,0x48,0x89,0x70,
        0x18,0xC5,0xF8,0x29,0x70,0xC8};
    constexpr std::array<std::uint8_t,12> process_prologue{
        0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
    struct Site {
        std::uint8_t* address = nullptr;
        std::uint8_t* allocation = nullptr;
        std::size_t size = 0;
        std::array<std::uint8_t,43> before{}, after{};
    };
    std::array<Site,4> sites{};
    const auto stock_process = [&]() {
        return std::memcmp(reinterpret_cast<const void*>(g_module + kProcessAnimCmdListSegmentRva),
            process_prologue.data(), process_prologue.size()) == 0;
    };
    const auto free_allocations = [&]() {
        for (auto& s : sites) if (s.allocation != nullptr) VirtualFree(s.allocation,0,MEM_RELEASE);
        g_p99_group_async = nullptr;
    };
    auto& async = sites[0];
    async.address = reinterpret_cast<std::uint8_t*>(g_module + kAnimManagerAsyncObjectUpdateJointBlendJobRva);
    async.size = prologue.size();
    async.before = prologue;
    if (!stock_process() || std::memcmp(async.address,prologue.data(),prologue.size()) != 0) return false;
    // RIP-relative absolute jump preserves RAX, unlike the older generic hook.
    async.allocation = static_cast<std::uint8_t*>(VirtualAlloc(nullptr,prologue.size()+14,
        MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if (async.allocation == nullptr) return false;
    const auto absolute_jump = [](std::uint8_t* at, std::uintptr_t target) {
        at[0]=0xFF; at[1]=0x25; std::memset(at+2,0,4); std::memcpy(at+6,&target,8);
    };
    std::memcpy(async.allocation,prologue.data(),prologue.size());
    absolute_jump(async.allocation+prologue.size(),reinterpret_cast<std::uintptr_t>(async.address+prologue.size()));
    async.after.fill(0x90);
    absolute_jump(async.after.data(),reinterpret_cast<std::uintptr_t>(&hooked_p99_group_async));
    FlushInstructionCache(GetCurrentProcess(),async.allocation,prologue.size()+14);
    constexpr std::array<std::uintptr_t,3> rvas{0x1A92E78,0x1A932F8,kAnimCmdExecuteJointProgramsCallsiteRva};
    const std::array<const void*,3> hooks{reinterpret_cast<const void*>(&hooked_p99_group_process0),
        reinterpret_cast<const void*>(&hooked_p99_group_process1),reinterpret_cast<const void*>(&hooked_p99_group_cmd16)};
    for (std::size_t i=0;i<3;++i) {
        auto& s=sites[i+1]; std::int32_t displacement=0; s.size=5;
        if (!prepare_plain_callsite_relay(rvas[i],i==2?kAnimCmdExecuteJointProgramsRva:kProcessAnimCmdListSegmentRva,
            hooks[i],s.address,s.allocation,displacement)) { free_allocations(); return false; }
        std::memcpy(s.before.data(),s.address,5);
        s.after=s.before; s.after[0]=0xE8; std::memcpy(s.after.data()+1,&displacement,4);
    }
    g_p99_group_async = reinterpret_cast<AnimManagerAsyncObjectUpdateJointBlendJobFunction>(async.allocation);
    const auto all_match = [&](bool patched) {
        for (const auto& s:sites) if (std::memcmp(s.address,(patched?s.after:s.before).data(),s.size)!=0) return false;
        return stock_process();
    };
    // Collect unique pages BEFORE suspending threads (no allocating while
    // another suspended thread might own a process heap lock).
    struct Page { void* address=nullptr; DWORD old=0; bool writable=false; };
    std::array<Page,4> pages{}; std::size_t page_count=0;
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
    // Partial installation must retain every potentially reachable trampoline;
    // ready=false makes all reachable hooks stock-only.
    if(clean||(!wrote&&restored)) free_allocations();
    boot_log_line(ready?"P99-GROUP INSTALLED four sites; original buffers preserved":
        (clean||(!wrote&&restored))?"P99-GROUP SKIPPED stock/preflight or verified rollback":
            "P99-GROUP PARTIAL pass-through; allocations retained; inspect code protections");
    return ready;
}
