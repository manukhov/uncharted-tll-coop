// Field-only scope for the bounded normal-Process observer. Include after the
// runtime constants/helpers. No virtual/native getter, resolver, or game write.
// A readable/equal snapshot is not a lifetime lease or consumed-source proof.
// Diagnostic stages identify the rejected group, not a specific predicate or
// the reason for any earlier capture that did not record this value.
enum class P99ConsumedOwnerReject : std::uint32_t {
    None, Disabled, Input, LocalPlayer, NativeJob, RetainedFg, Skeleton,
    Control, Weapon, Motion, Changed, ReadFault
};

enum class P99ConsumedControlCheck : std::uint32_t {
    None, WeaponPointer, WeaponVtable, WeaponOwner, Slot, Draw,
    GrenadePointer, GrenadeVtable, GrenadeOwner, GrenadeState
};
enum class P99ConsumedControlOutcome : std::uint32_t {
    None, ReadRefused, ValueMismatch, Seh, Passed
};
struct P99ConsumedControlTrace {
    std::uint32_t snapshot = 0;
    P99ConsumedControlCheck check = P99ConsumedControlCheck::None;
    P99ConsumedControlOutcome outcome = P99ConsumedControlOutcome::None;
    std::uint32_t valid = 0; // 1=slot, 2=draw, 4=grenade; only this snapshot.
    std::int32_t slot = -1;
    std::uint32_t draw = 0, grenade = 0;
};

void p99_consumed_owner_stage(P99ConsumedOwnerReject* reason,
    P99ConsumedOwnerReject stage) {
    if (reason != nullptr) *reason = stage;
}

struct P99ConsumedOwner {
    std::uintptr_t job = 0, player = 0, weapon = 0, fg = 0, local_pose = 0;
    std::uintptr_t target_skeleton = 0, underlying_skeleton = 0, control = 0;
    std::uintptr_t retained_handle = 0, weapon_handle = 0, parent_handle = 0;
    std::uintptr_t grenade_control = 0, job_state = 0;
    std::int32_t slot = -1;
    std::uint32_t player_id = 0, weapon_id = 0;
};

bool same_owner(const P99ConsumedOwner& a, const P99ConsumedOwner& b) {
    return a.job == b.job && a.player == b.player && a.weapon == b.weapon &&
        a.fg == b.fg && a.local_pose == b.local_pose &&
        a.target_skeleton == b.target_skeleton &&
        a.underlying_skeleton == b.underlying_skeleton && a.control == b.control &&
        a.retained_handle == b.retained_handle && a.weapon_handle == b.weapon_handle &&
        a.parent_handle == b.parent_handle && a.grenade_control == b.grenade_control &&
        a.job_state == b.job_state && a.slot == b.slot &&
        a.player_id == b.player_id && a.weapon_id == b.weapon_id;
}

// Query only touched regions, not readable_bytes' broad forward scan. Cache is
// private to one callback-free field snapshot; every payload read is fresh.
// SEH below protects against protection changes after VirtualQuery, not ABA.
struct P99ConsumedRead {
    struct Region { std::uintptr_t first = 0, end = 0; };
    std::array<Region, 16> regions{};
    std::size_t used = 0;
    bool range(std::uintptr_t address, std::size_t count) {
        if (address == 0 || count == 0 ||
            count > std::numeric_limits<std::uintptr_t>::max() - address) return false;
        const auto end = address + count;
        while (address < end) {
            std::uintptr_t next = 0;
            for (std::size_t i = 0; i < used; ++i)
                if (address >= regions[i].first && address < regions[i].end) {
                    next = regions[i].end;
                    break;
                }
            if (next == 0) {
                MEMORY_BASIC_INFORMATION mbi{};
                if (used == regions.size() ||
                    VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) !=
                        sizeof(mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD))
                    return false;
                const DWORD p = mbi.Protect & 0xFFu;
                if (p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY &&
                    p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE &&
                    p != PAGE_EXECUTE_WRITECOPY) return false;
                const auto first = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
                if (mbi.RegionSize > std::numeric_limits<std::uintptr_t>::max() - first)
                    return false;
                next = first + mbi.RegionSize;
                if (address < first || address >= next) return false;
                regions[used++] = {first, next};
            }
            address = std::min(end, next);
        }
        return true;
    }
    bool copy(std::uintptr_t address, void* output, std::size_t count) {
        if (!range(address, count)) return false;
        const auto* source = reinterpret_cast<const volatile std::byte*>(address);
        auto* target = static_cast<std::byte*>(output);
        for (std::size_t i = 0; i < count; ++i) target[i] = source[i];
        return true;
    }
    template<class T> bool read(std::uintptr_t base, std::size_t offset, T& output) {
#if defined(TLL_P99_CONSUMED_OWNER_TEST)
        if (!p99_consumed_fixture_read(base, offset, sizeof(T))) return false;
#endif
        return base != 0 && offset <= std::numeric_limits<std::uintptr_t>::max() - base &&
            copy(base + offset, &output, sizeof(output));
    }
};

template<class T> bool p99_read_consumed_control(P99ConsumedRead& reader,
    std::uintptr_t base, std::size_t offset, T& output, P99ConsumedControlTrace* trace,
    P99ConsumedControlCheck check) {
    if (trace) { trace->check = check; trace->outcome = P99ConsumedControlOutcome::None; }
    if (reader.read(base, offset, output)) return true;
    if (trace) trace->outcome = P99ConsumedControlOutcome::ReadRefused;
    return false;
}
bool p99_consumed_control_matches(bool matches, P99ConsumedControlTrace* trace) {
    if (!matches && trace) trace->outcome = P99ConsumedControlOutcome::ValueMismatch;
    return matches;
}

bool p99_read_consumed_owner_fields(void* job, P99ConsumedOwner& value,
    P99ConsumedOwnerReject* reason = nullptr, P99ConsumedControlTrace* trace = nullptr) {
    value = {};
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::Disabled);
    if (g_module == 0 || !g_state_net.enabled || !g_net_role_b) return false;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::NativeJob);
    if (job == nullptr) return false;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::Input);
    if (g_local_reload_level.load(std::memory_order_acquire)) return false;
    const auto buttons = g_cached_local_input_buttons.load(std::memory_order_acquire) &
        ~kInputBlockButtonBaseMask;
    if (buttons != 0 && buttons != 0x9ull) return false;
    P99ConsumedRead r{};
    value.job = reinterpret_cast<std::uintptr_t>(job);
    std::uintptr_t p = 0, owner_handle = 0;
    std::uint32_t word = 0, owner_id = 0;
    std::uint16_t half = 0;
    std::uint8_t byte = 0;
    const auto retained = g_module + kRetainedSlotTableRva;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::LocalPlayer);
    if (!r.read(retained, 0, value.retained_handle) ||
        !r.read(retained, 8, value.player_id) || value.player_id == 0 ||
        !r.read(value.retained_handle, 8, value.player) ||
        !r.read(value.player, 0, p) || p != g_module + kPlayerVtableRva ||
        !r.read(value.player, kGameObjectRuntimeIdOffset, word) || word != value.player_id ||
        !r.read(value.player, 0x4010, word) || word != 0 ||
        !r.read(value.player, 0x170, word) || word != kLegacyNadineAttachTargetLookId ||
        !r.read(value.player, kPlayerIsInCoverFlagOffset, byte) || (byte & 1u) != 0)
        return false;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::NativeJob);
    if (!r.read(value.player, kPlayerFgAnimDataOffset, value.fg) ||
        !r.read(value.job, 8, p) || p != value.fg ||
        !r.read(value.job, 0, value.job_state) || value.job_state == 0 ||
        !r.read(value.fg, 0x10, p) || p != value.job_state ||
        !r.read(value.job, 0x14, byte) || byte != 0 ||
        !r.read(value.fg, 6, byte) || byte != 2) return false;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::RetainedFg);
    if (!r.read(value.fg, kFgAnimDataRetainedHandleOffset, owner_handle) ||
        owner_handle != value.retained_handle ||
        !r.read(value.fg, kFgAnimDataRetainedIdOffset, owner_id) || owner_id != value.player_id ||
        !r.read(owner_handle, 8, p) || p != value.player) return false;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::Skeleton);
    if (!r.read(value.fg, kFgAnimDataSkeletonOffset, value.target_skeleton) ||
        !r.read(value.target_skeleton, 0x24, word) || word != kLegacyNadineTargetSkeletonArtId ||
        !r.read(value.target_skeleton, kSkeletonJointCountOffset, word) ||
        word != kLegacyNadineSkeletonJointCount ||
        !r.read(value.target_skeleton, 0x40, value.underlying_skeleton) ||
        !r.read(value.underlying_skeleton, 4, word) || word != kLegacyNadineUnderlyingSkeletonId ||
        !r.read(value.underlying_skeleton, 0x10, half) || half != kLegacyNadineSkeletonJointCount ||
        !r.read(value.fg, kFgAnimDataJointCacheLocalPoseOffset, value.local_pose) ||
        !r.range(value.local_pose, 31 * 48)) return false;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::Control);
    using Control = P99ConsumedControlCheck;
    // Same reads, order and short-circuit predicates as the original chain.
    // Trace copies ONLY already-read scalars; it never fills in skipped fields.
    if (!p99_read_consumed_control(r, value.player, kPlayerWeaponControllerOffset,
            value.control, trace, Control::WeaponPointer) ||
        !p99_read_consumed_control(r, value.control, 0, p, trace, Control::WeaponVtable) ||
        !p99_consumed_control_matches(p == g_module + kPlayerWeaponControllerVtableRva, trace) ||
        !p99_read_consumed_control(r, value.control, 0x108, p, trace, Control::WeaponOwner) ||
        !p99_consumed_control_matches(p == value.player, trace) ||
        !p99_read_consumed_control(r, value.control, kWeaponCtrlCurrentSlotOffset,
            value.slot, trace, Control::Slot)) return false;
    if (trace) { trace->slot = value.slot; trace->valid |= 1u; }
    if (!p99_consumed_control_matches(value.slot >= 0 && value.slot < 18, trace) ||
        !p99_read_consumed_control(r, value.control, kWeaponCtrlDrawStateOffset,
            word, trace, Control::Draw)) return false;
    if (trace) { trace->draw = word; trace->valid |= 2u; }
    if (!p99_consumed_control_matches(word == 2, trace) ||
        !p99_read_consumed_control(r, value.player, kPlayerFastGrenadeControllerOffset,
            value.grenade_control, trace, Control::GrenadePointer) ||
        !p99_read_consumed_control(r, value.grenade_control, 0, p, trace, Control::GrenadeVtable) ||
        !p99_consumed_control_matches(p == g_module + kFastGrenadeControllerVtableRva, trace) ||
        !p99_read_consumed_control(r, value.grenade_control, 0x108, p, trace, Control::GrenadeOwner) ||
        !p99_consumed_control_matches(p == value.player, trace) ||
        !p99_read_consumed_control(r, value.grenade_control, 0x120, word,
            trace, Control::GrenadeState)) return false;
    if (trace) { trace->grenade = word; trace->valid |= 4u; }
    if (!p99_consumed_control_matches(word == 0, trace)) return false;
    if (trace) { trace->check = Control::None; trace->outcome = P99ConsumedControlOutcome::Passed; }
    // Native1410A1F90 uses these weak records but writes weapon+72 on success.
    // Read ONLY the current slot; neither call that vslot nor scan other arts.
    const auto record_offset = 0x148 + static_cast<std::size_t>(value.slot) * 0x10;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::Weapon);
    if (!r.read(value.control, record_offset, value.weapon_handle) ||
        !r.read(value.control, record_offset + 8, value.weapon_id) || value.weapon_id == 0 ||
        !r.read(value.weapon_handle, 8, value.weapon) ||
        !r.read(value.weapon, 0, p) || p != g_module + kProcessWeaponVtableRva ||
        !r.read(value.weapon, kGameObjectRuntimeIdOffset, word) || word != value.weapon_id ||
        !r.read(value.weapon, 0x860, word) || word != kCoopPistolArtId ||
        !r.read(value.weapon, 0x4E0, value.parent_handle) ||
        !r.read(value.parent_handle, 8, p) || p != value.player ||
        !r.read(value.weapon, 0x4E8, word) || word != value.player_id) return false;
    float stick[2]{}, move[4]{}, magnitude = 0;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::Motion);
    if (!r.read(value.player, 0x39C8, stick) || !r.read(value.player, 0x3A30, move) ||
        !r.read(value.player, 0x3A1C, magnitude)) return false;
    for (float x : stick) if (!std::isfinite(x) || std::abs(x) > 0.025F) return false;
    for (float x : move) if (!std::isfinite(x) || std::abs(x) > 0.025F) return false;
    if (!std::isfinite(magnitude) || std::abs(magnitude) > 0.025F) return false;
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::None);
    return true;
}

bool seh_read_p99_consumed_owner(void* job, P99ConsumedOwner* output,
    P99ConsumedOwnerReject* reason = nullptr, P99ConsumedControlTrace* trace = nullptr) {
    if (trace) *trace = {};
    p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::Input);
    if (output == nullptr) return false;
    *output = {};
    __try {
        P99ConsumedOwner before{}, after{};
        if (trace) trace->snapshot = 1;
        if (!p99_read_consumed_owner_fields(job, before, reason, trace)) return false;
        if (trace) { *trace = {}; trace->snapshot = 2; }
        if (!p99_read_consumed_owner_fields(job, after, reason, trace)) return false;
        p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::Changed);
        if (!same_owner(before, after)) return false;
        *output = after;
        p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::None);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *output = {};
        if (trace && trace->check != P99ConsumedControlCheck::None)
            trace->outcome = P99ConsumedControlOutcome::Seh;
        p99_consumed_owner_stage(reason, P99ConsumedOwnerReject::ReadFault);
        return false;
    }
}

bool seh_copy_p99_consumed_arms(const P99ConsumedOwner& owner,
    std::array<std::array<std::byte, 48>, 13>* output) {
    if (output == nullptr) return false;
    *output = {};
    __try {
        P99ConsumedOwner before{}, after{};
        std::array<std::array<std::byte, 48>, 13> rows{};
        P99ConsumedRead reader{};
        if (!seh_read_p99_consumed_owner(reinterpret_cast<void*>(owner.job), &before) ||
            !same_owner(owner, before) ||
            !reader.read(before.local_pose, 18 * 48, rows) ||
            !seh_read_p99_consumed_owner(reinterpret_cast<void*>(owner.job), &after) ||
            !same_owner(before, after)) return false;
        // Raw JointCache: S.xyz@00, Q@10, T.xyz@20; preserve padding/W bits.
        // This delimits all +408 procedural work, not Feather alone or rendering.
        *output = rows;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
