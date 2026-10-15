// Included after consumed runtime callbacks and the ordinary callsite helpers.
// Four callback/call-value changes only; no moved native code, pose write,
// loader work, global vtable change, or reactivation of an older hook package.
static_assert(!kEnableP99ConsumedCensus ||
    (!kEnableP99GroupShadow && !kEnableP99SourceObservation &&
     !kEnableP99WristObservation && !kEnableP99WristPreparation &&
     !kEnableLegacyNadineMazurAuthoredPairRepair &&
     !kEnableLegacyNadineMazurSteadyHandsCommandCensus &&
     !kEnableLegacyNadineMazurDrawEndpointRepair &&
     !kEnableLegacyNadineP99SupportConstraint && !kEnableLegacyNadineP99ArmRoll &&
     !kEnableLegacyNadineP99ReachCensus && !kEnableLegacyNadineP99ChloeGripTransfer &&
     !kEnableLegacyNadineActiveWeaponGripOverlayRepair &&
     !kEnableLegacyNadinePistolPoseGripRepair),
    "Consumed census requires exclusive callback/FLS and unchanged command/pose paths");
coop::p99_group_ingress::InstallationGate g_p99_consumed_install_gate;

struct P99ConsumedInstallSite {
    std::uint8_t* address = nullptr;
    std::uint8_t* allocation = nullptr;
    std::size_t size = 0;
    std::array<std::uint8_t, 7> before{}, after{};
    volatile LONG64* atomic_address = nullptr;
    LONG64 before_word = 0, after_word = 0;
    bool word_ready = false;
};

bool seh_p99_consumed_install_layout() {
    namespace frame = coop::p99_consumed_frame;
    __try {
        if (!g_module || g_module > UINTPTR_MAX - kProcessAnimCmdListSegmentRva)
            return false;
        constexpr std::array<std::uint8_t, 12> process_prologue{
            0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
        return frame::layout_matches(
            {reinterpret_cast<const std::uint8_t*>(g_module + frame::kOuterRva),
                frame::kOuterPrologue.size()},
            {reinterpret_cast<const std::uint8_t*>(g_module + frame::kPerformRva),
                frame::kPerformPrologue.size()},
            {reinterpret_cast<const std::uint8_t*>(g_module + frame::kProcessContextLeaRva),
                frame::kProcessContextLea.size()}) &&
            std::memcmp(reinterpret_cast<const void*>(g_module + frame::kPerformCallsiteRva),
                frame::kPerformCallsiteStock.data(), frame::kPerformCallsiteStock.size()) == 0 &&
            std::memcmp(reinterpret_cast<const void*>(g_module + kProcessAnimCmdListSegmentRva),
                process_prologue.data(), process_prologue.size()) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool seh_p99_consumed_install_match(const std::array<P99ConsumedInstallSite, 4>& sites,
    bool patched) {
    __try {
        for (const auto& site : sites)
            if (!site.address || !site.size ||
                std::memcmp(site.address, (patched ? site.after : site.before).data(), site.size) ||
                (site.word_ready && *site.atomic_address !=
                    (patched ? site.after_word : site.before_word)))
                return false;
        return seh_p99_consumed_install_layout();
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool seh_p99_consumed_install_capture_words(std::array<P99ConsumedInstallSite, 4>& sites) {
    __try {
        for (auto& site : sites) {
            const auto address = reinterpret_cast<std::uintptr_t>(site.address);
            const auto offset = address & 7u;
            if (offset + site.size > sizeof(LONG64)) return false;
            site.atomic_address = reinterpret_cast<volatile LONG64*>(address & ~std::uintptr_t{7});
            site.before_word = *site.atomic_address;
            // Pin unchanged neighbouring instruction bytes as part of the
            // transaction, without replacing those bytes with guessed values.
            if (std::memcmp(reinterpret_cast<const std::uint8_t*>(&site.before_word) + offset,
                    site.before.data(), site.size)) return false;
            site.after_word = site.before_word;
            std::memcpy(reinterpret_cast<std::uint8_t*>(&site.after_word) + offset,
                site.after.data(), site.size);
            site.word_ready = true;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool seh_p99_consumed_install_write(const std::array<P99ConsumedInstallSite, 4>& sites,
    bool patched) {
    __try {
        bool success = true;
        for (const auto& site : sites) {
            if (!site.word_ready) return false;
            const LONG64 expected = patched ? site.before_word : site.after_word;
            const LONG64 replacement = patched ? site.after_word : site.before_word;
            // All four sites fit ONE aligned eight-byte word. This cannot
            // publish an instruction prefix and then fault halfway through it.
            // A foreign word is never overwritten by a rollback guess.
            const LONG64 observed = InterlockedCompareExchange64(
                site.atomic_address, replacement, expected);
            if (observed != expected && observed != replacement) success = false;
            if (!FlushInstructionCache(GetCurrentProcess(),
                    const_cast<const LONG64*>(site.atomic_address), sizeof(LONG64))) success = false;
        }
        return success;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool seh_p99_consumed_install_known_words(const std::array<P99ConsumedInstallSite, 4>& sites) {
    __try {
        bool success = true;
        for (const auto& site : sites) {
            if (!site.word_ready) return false;
            const LONG64 observed = *site.atomic_address;
            if (observed != site.before_word && observed != site.after_word) success = false;
            if (!FlushInstructionCache(GetCurrentProcess(),
                    const_cast<const LONG64*>(site.atomic_address), sizeof(LONG64))) success = false;
        }
        return success;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool install_p99_consumed_census() {
    namespace frame = coop::p99_consumed_frame;
    if constexpr (!kEnableP99ConsumedCensus) return false;
    if (!g_p99_consumed_install_gate.begin())
        return g_p99_consumed_installed.load(std::memory_order_acquire);

    std::array<P99ConsumedInstallSite, 4> sites{};
    const auto fail_unpublished = [&](const char* reason) {
        g_p99_consumed_failed.store(true, std::memory_order_release);
        // This path is only used before the first retail instruction write.
        for (auto& site : sites) if (site.allocation) {
            VirtualFree(site.allocation, 0, MEM_RELEASE);
            site.allocation = nullptr;
        }
        boot_log_line(reason);
        return false;
    };
    if (g_enable_aim_case6_probe)
        return fail_unpublished("P99-CONSUMED SKIPPED incompatible aim-case6 probe");
    if (g_p99_consumed_failed.load(std::memory_order_acquire) ||
        !seh_p99_consumed_install_layout() || !initialize_p99_consumed_census())
        return fail_unpublished("P99-CONSUMED SKIPPED layout or native/FLS initialization");

    for (std::size_t i = 0; i < frame::kIngressSites.size(); ++i) {
        const auto& spec = frame::kIngressSites[i];
        auto& site = sites[i];
        site.address = reinterpret_cast<std::uint8_t*>(g_module + spec.rva);
        site.size = spec.size;
        site.before = spec.stock;
    }
    sites[2].address = reinterpret_cast<std::uint8_t*>(g_module + frame::kProcessCallsiteRva);
    sites[2].size = frame::kProcessCallsiteStock.size();
    std::copy(frame::kProcessCallsiteStock.begin(), frame::kProcessCallsiteStock.end(),
        sites[2].before.begin());
    sites[3].address = reinterpret_cast<std::uint8_t*>(g_module + frame::kPostCallsiteRva);
    sites[3].size = frame::kPostCallsiteStock.size();
    std::copy(frame::kPostCallsiteStock.begin(), frame::kPostCallsiteStock.end(),
        sites[3].before.begin());
    if (!seh_p99_consumed_install_match(sites, false))
        return fail_unpublished("P99-CONSUMED SKIPPED four-site stock preflight");

    // Allocate, initialize native fallbacks/FLS, and seal every leaf relay
    // before suspending other threads. No heap allocation in the transaction.
    for (std::size_t i = 0; i < frame::kIngressSites.size(); ++i) {
        auto& site = sites[i];
        site.allocation = static_cast<std::uint8_t*>(allocate_executable_near(
            site.address, coop::p99_group_ingress::kRelayBytes));
        const auto patch = frame::plan_ingress(i, g_module,
            {site.before.data(), site.size}, reinterpret_cast<std::uintptr_t>(site.allocation),
            reinterpret_cast<std::uintptr_t>(&hooked_p99_consumed_outer));
        if (!patch) return fail_unpublished("P99-CONSUMED SKIPPED ingress relay planning");
        site.after = patch->instruction;
        std::memcpy(site.allocation, patch->relay.data(), patch->relay.size());
    }
    std::int32_t process_displacement = 0;
    if (!prepare_plain_callsite_relay(frame::kProcessCallsiteRva,
            kProcessAnimCmdListSegmentRva, reinterpret_cast<const void*>(&hooked_p99_consumed_process),
            sites[2].address, sites[2].allocation, process_displacement))
        return fail_unpublished("P99-CONSUMED SKIPPED Process relay preparation");
    sites[2].after = sites[2].before;
    sites[2].after[0] = 0xE8;
    std::memcpy(sites[2].after.data() + 1, &process_displacement, 4);
    std::array<std::uint8_t, 6> post_patch{};
    if (!prepare_six_byte_indirect_callsite_relay(frame::kPostCallsiteRva,
            frame::kPostCallsiteStock, reinterpret_cast<const void*>(&hooked_p99_consumed_post),
            sites[3].address, sites[3].allocation, post_patch))
        return fail_unpublished("P99-CONSUMED SKIPPED postblend relay preparation");
    std::copy(post_patch.begin(), post_patch.end(), sites[3].after.begin());
    for (const auto& site : sites) {
        DWORD ignored = 0;
        if (!VirtualProtect(site.allocation, coop::p99_group_ingress::kRelayBytes,
                PAGE_EXECUTE_READ, &ignored) ||
            !FlushInstructionCache(GetCurrentProcess(), site.allocation,
                coop::p99_group_ingress::kRelayBytes))
            return fail_unpublished("P99-CONSUMED SKIPPED relay RX/cache preparation");
    }
    if (!seh_p99_consumed_install_capture_words(sites))
        return fail_unpublished("P99-CONSUMED SKIPPED aligned-word or neighbour preflight");

    struct Page { void* address = nullptr; DWORD old = 0; bool writable = false; };
    std::array<Page, 4> pages{};
    std::size_t page_count = 0;
    for (const auto& site : sites) {
        const auto address = reinterpret_cast<std::uintptr_t>(site.atomic_address);
        if (address > UINTPTR_MAX - sizeof(LONG64) ||
            (address >> 12) != ((address + sizeof(LONG64) - 1) >> 12))
            return fail_unpublished("P99-CONSUMED SKIPPED callsite page boundary");
        auto* base = reinterpret_cast<void*>(address & ~std::uintptr_t{0xFFF});
        bool found = false;
        for (std::size_t i = 0; i < page_count; ++i) found |= pages[i].address == base;
        if (!found) pages[page_count++].address = base;
    }

    SuspendedProcessThreads suspended{};
    if (!suspend_other_process_threads(suspended, THREAD_GET_CONTEXT))
        return fail_unpublished("P99-CONSUMED SKIPPED thread suspension");
    bool safe = seh_p99_consumed_install_match(sites, false);
    for (std::size_t i = 0; safe && i < suspended.count; ++i) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(suspended.handles[i], &context)) { safe = false; break; }
        for (const auto& site : sites) {
            const auto address = reinterpret_cast<std::uintptr_t>(site.atomic_address);
            if (context.Rip >= address && context.Rip < address + sizeof(LONG64)) safe = false;
        }
    }
    bool wrote = false, installed = false;
    if (safe) {
        for (std::size_t i = 0; i < page_count; ++i) {
            auto& page = pages[i];
            page.writable = VirtualProtect(page.address, 0x1000, PAGE_EXECUTE_READWRITE,
                &page.old) != FALSE;
            if (!page.writable) { safe = false; break; }
        }
    }
    if (safe) {
        wrote = true; // Set BEFORE the first possibly published whole word.
        installed = seh_p99_consumed_install_write(sites, true) &&
            seh_p99_consumed_install_match(sites, true);
        if (!installed) {
            for (unsigned attempt = 0; attempt < 3; ++attempt)
                if (seh_p99_consumed_install_write(sites, false) &&
                    seh_p99_consumed_install_match(sites, false)) break;
        }
    }
    const auto restore_pages = [&]() {
        bool success = true;
        for (std::size_t i = 0; i < page_count; ++i) if (pages[i].writable) {
            DWORD ignored = 0;
            if (VirtualProtect(pages[i].address, 0x1000, pages[i].old, &ignored))
                pages[i].writable = false;
            else success = false;
        }
        return success;
    };
    bool restored = restore_pages();
    if (!restored) {
        if (installed) {
            bool reopened = true;
            for (std::size_t i = 0; i < page_count; ++i) if (!pages[i].writable) {
                DWORD ignored = 0;
                if (VirtualProtect(pages[i].address, 0x1000, PAGE_EXECUTE_READWRITE, &ignored))
                    pages[i].writable = true;
                else reopened = false;
            }
            if (reopened) (void)seh_p99_consumed_install_write(sites, false);
            installed = false;
        }
        for (unsigned attempt = 0; attempt < 3 && !restored; ++attempt)
            restored = restore_pages();
    }
    // Only exact stock/patched words with successful cache flush AND known
    // restored protections earn the pass-through classification. An unrelated
    // mutation or unrecoverable protection failure remains explicitly UNSAFE,
    // never a successful install or authorization for a runtime test.
    const bool known_safe = restored && seh_p99_consumed_install_known_words(sites);
    const bool ready = installed && known_safe &&
        !g_p99_consumed_failed.load(std::memory_order_acquire) &&
        seh_p99_consumed_install_match(sites, true);
    const bool clean = !ready && known_safe && seh_p99_consumed_install_match(sites, false);
    if (ready) g_p99_consumed_installed.store(true, std::memory_order_release);
    else g_p99_consumed_failed.store(true, std::memory_order_release);
    resume_suspended_process_threads(suspended);

    // Never free ANY relay after the first retail write, even after verified
    // rollback: retain potentially published callback values for process life.
    // Native fallbacks and FLS also remain initialized; ready=false is inert.
    if (!wrote) for (auto& site : sites) if (site.allocation) {
        VirtualFree(site.allocation, 0, MEM_RELEASE);
        site.allocation = nullptr;
    }
    boot_log_line(ready
        ? "P99-CONSUMED INSTALLED four sites; UNARMED stock-only; native prologues intact; observation only; pose_writes=0"
        : clean && known_safe ? "P99-CONSUMED SKIPPED stock or verified rollback; no retry"
        : known_safe ? "P99-CONSUMED PARTIAL exact stock/patched words; hooks inert; relays retained"
        : "P99-CONSUMED UNSAFE code/cache/protection state; no runtime GO; relays retained");
    return ready;
}
