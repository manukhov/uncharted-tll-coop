// Included inside coop_runtime.cpp's anonymous namespace after group runtime
// helpers. Data phase ONLY: no installer, native evaluator, logging, global
// grant, retarget preparation or pose write. All outputs are caller-owned.
// The adapter separately proves FLS/native Async AND Process invocation, stock
// return, one-shot and budget. Integer identities below are not lifetime leases.
namespace p99_wrist_snapshot = coop::p99_wrist_snapshot;

enum class P99WristReadReject : std::uint8_t {
    None, Output, Gate, NormalJob, Retained, Owner, OwnerIdentity,
    ProcessShape, ProcessIdentity, Program, Commands, ProcessChanged,
    Segment, WorkBuffer, Metadata, Snapshot, ClosingOwner, Exception,
};

bool p99_wrist_read_refuse(P99WristReadReject* reason, P99WristReadReject value) noexcept {
    if (reason != nullptr) *reason = value;
    return false;
}

struct P99WristOwner {
    P99NormalJobIdentity normal{};
    p99_source::Owner owner{};
};

bool p99_wrist_owner_same(const P99WristOwner& a, const P99WristOwner& b) noexcept {
    const auto& x = a.owner;
    const auto& y = b.owner;
    // Do not memcmp padding/floats. Each observation independently passes the
    // existing position/orientation checks; movement values are not identities.
    return a.normal.fg != 0 && a.normal.art != 0 && a.normal.raw != 0 &&
        a.normal.fg == b.normal.fg && a.normal.art == b.normal.art &&
        a.normal.raw == b.normal.raw && x.player == y.player &&
        x.fg == y.fg && x.art == y.art && x.raw == y.raw &&
        x.local_pose == y.local_pose && x.controller == y.controller &&
        x.grenade == y.grenade && x.weapon == y.weapon && x.parent == y.parent &&
        x.retained == y.retained && x.fg_owner == y.fg_owner &&
        x.weapon_slot == y.weapon_slot && x.player_id == y.player_id &&
        x.weapon_id == y.weapon_id && x.slot == y.slot && x.state == y.state;
}

bool seh_p99_wrist_owner(void* native_job, P99WristOwner* out,
    P99WristReadReject* reason = nullptr) {
    if (reason != nullptr) *reason = P99WristReadReject::None;
    if (out == nullptr) return p99_wrist_read_refuse(reason, P99WristReadReject::Output);
    *out = {};
    __try {
        if (!g_net_role_b || !g_state_net.enabled || g_module == 0 || native_job == nullptr)
            return p99_wrist_read_refuse(reason, P99WristReadReject::Gate);
        P99SourceReader reader(&P99_SOURCE_QUERY_FUNCTION, &p99_source_copy);
        P99WristOwner staged{};
        if (!seh_p99_group_normal_job(native_job, &staged.normal, reader))
            return p99_wrist_read_refuse(reason, P99WristReadReject::NormalJob);
        void* const player = p99_source_retained_player(P99SourcePurpose::kOwnedGroupValues, &reader);
        if (player == nullptr)
            return p99_wrist_read_refuse(reason, P99WristReadReject::Retained);
        IdleArmPoseOwnerScope scope{};
        if (!seh_resolve_p99_source_owner_scope(player, &scope,
                P99SourcePurpose::kOwnedGroupValues, &staged.owner, &reader))
            return p99_wrist_read_refuse(reason, P99WristReadReject::Owner);
        if (!scope.valid || staged.owner.fg != staged.normal.fg ||
            staged.owner.art != staged.normal.art || staged.owner.raw != staged.normal.raw)
            return p99_wrist_read_refuse(reason, P99WristReadReject::OwnerIdentity);
        *out = staged;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return p99_wrist_read_refuse(reason, P99WristReadReject::Exception);
    }
}

struct P99WristProcessValues {
    std::uintptr_t scratch = 0;
    std::array<std::uint32_t, p99_admit::kMaxCommandWords> commands{};
    std::uint32_t words = 0;
    std::uintptr_t commands_address = 0;
    std::uintptr_t parent = 0;
    std::uintptr_t callback = 0;
};

// Only owned region/table copies reach the pure manifest validator. Approvals
// are local to this callback-free read phase, including all live dispatch words.
bool p99_wrist_program_exact(P99SourceReader& reader, std::uintptr_t raw) {
    if (!reader.range(raw, p99_program::kRequiredRawExtent)) return false;
    std::array<std::byte, sizeof(p99_program::kExpectedWords)> bytes{};
    std::array<p99_program::RegionView, p99_program::kRegions.size()> regions{};
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const auto& spec = p99_program::kRegions[i];
        const auto offset = static_cast<std::size_t>(spec.expected_word_first) * 4;
        if (offset > bytes.size() || spec.byte_count > bytes.size() - offset ||
            !reader.bytes(raw, spec.raw_relative, bytes.data() + offset, spec.byte_count)) return false;
        regions[i] = {spec.id, spec.raw_relative, {bytes.data() + offset, spec.byte_count}};
    }
    std::array<std::uintptr_t, p99_program::kBuiltinSnapshotEntries> builtin{};
    std::array<std::uintptr_t, p99_program::kCustomCount> custom{};
    std::uint16_t custom_count = 0;
    if (!reader.read(g_module, p99_program::kCustomCountRva, custom_count) ||
        custom_count != p99_program::kCustomCount ||
        !reader.bytes(g_module, p99_program::kBuiltinTableRva, builtin.data(), sizeof(builtin)) ||
        !reader.bytes(g_module, p99_program::kCustomTableRva, custom.data(), sizeof(custom))) return false;
    p99_program::Bindings binding{};
    binding.module_base = g_module;
    binding.builtin_table_address = g_module + p99_program::kBuiltinTableRva;
    binding.custom_table_address = g_module + p99_program::kCustomTableRva;
    binding.custom_count = custom_count;
    binding.builtin_entries = builtin;
    binding.custom_entries = custom;
    return p99_program::validate(regions, binding).admitted();
}

bool seh_p99_wrist_process(const std::uint32_t* commands, std::uint32_t words,
    const void* parent, std::uint32_t group, const void* callback,
    const P99WristOwner& owner, P99WristProcessValues* out,
    P99WristReadReject* reason = nullptr) {
    if (reason != nullptr) *reason = P99WristReadReject::None;
    if (out == nullptr) return p99_wrist_read_refuse(reason, P99WristReadReject::Output);
    *out = {};
    __try {
        if (!g_net_role_b || !g_state_net.enabled || g_module == 0 || group != 0 ||
            words == 0 || words > p99_admit::kMaxCommandWords)
            return p99_wrist_read_refuse(reason, P99WristReadReject::ProcessShape);
        P99SourceReader reader(&P99_SOURCE_QUERY_FUNCTION, &p99_source_copy);
        const auto& normal = owner.normal;
        if (normal.fg == 0 || normal.art == 0 || normal.raw == 0 ||
            normal.fg != owner.owner.fg || normal.art != owner.owner.art || normal.raw != owner.owner.raw ||
            !reader.range(normal.fg, 0x2A8) || !reader.range(normal.art, 0x48))
            return p99_wrist_read_refuse(reason, P99WristReadReject::ProcessIdentity);
        std::uintptr_t art = 0, cached_art = 0, override_callback = 0, raw = 0;
        std::uint8_t mode = 0, cached_override = 0;
        if (!reader.read(normal.fg, 0x18, art) || art != normal.art ||
            !reader.read(normal.fg, 0x1A0, cached_art) || cached_art != normal.art ||
            !reader.read(normal.fg, 0x2A0, override_callback) || override_callback != 0 ||
            !reader.read(normal.fg, 6, mode) || mode != 2 ||
            !reader.read(normal.art, 0x40, raw) || raw != normal.raw ||
            !reader.read(g_module, 0x3682F4B, cached_override) || cached_override != 0)
            return p99_wrist_read_refuse(reason, P99WristReadReject::ProcessIdentity);
        const auto parent_address = reinterpret_cast<std::uintptr_t>(parent);
        std::array<std::byte, 0x28> context{};
        if (!reader.bytes(parent_address, 0, context.data(), context.size()))
            return p99_wrist_read_refuse(reason, P99WristReadReject::ProcessIdentity);
        P99WristProcessValues staged{};
        staged.scratch = p99_group_load<std::uintptr_t>(context.data(), 0x18);
        if (p99_group_load<std::uintptr_t>(context.data()) != normal.art ||
            p99_group_load<std::uintptr_t>(context.data(), 0x10) != normal.fg + 0x1C0 ||
            p99_group_load<std::uint32_t>(context.data(), 0x20) != 0 ||
            (staged.scratch & 15) != 0 || !reader.range(staged.scratch, 6480))
            return p99_wrist_read_refuse(reason, P99WristReadReject::ProcessIdentity);
        if (!p99_wrist_program_exact(reader, normal.raw))
            return p99_wrist_read_refuse(reason, P99WristReadReject::Program);
        p99_admit::CommandBindings binding{};
        binding.image_base = g_module;
        binding.postpose_callback = reinterpret_cast<std::uintptr_t>(callback);
        if (!reader.read(normal.fg, 0x100, binding.pose_source) ||
            binding.pose_source != owner.owner.local_pose ||
            !reader.read(normal.fg, 0x120, binding.scalar_source) ||
            !reader.read(normal.fg, 0x118, binding.cmd15_source) ||
            !reader.read(normal.fg, 0x110, binding.conversion_destination) ||
            !reader.read(normal.fg, 0x200, binding.sphere_arguments[0]) ||
            !reader.read(normal.fg, 0x230, binding.palette_arguments[0]) ||
            !reader.read(normal.fg, 0x240, binding.palette_arguments[1]))
            return p99_wrist_read_refuse(reason, P99WristReadReject::Commands);
        binding.sphere_arguments[1] = normal.fg + 0x1C0;
        binding.sphere_arguments[2] = normal.fg;
        binding.palette_arguments[2] = normal.fg + 0x220;
        if (!reader.bytes(reinterpret_cast<std::uintptr_t>(commands), 0,
                staged.commands.data(), words * sizeof(std::uint32_t)) ||
            !p99_admit::validate_commands(0, {staged.commands.data(), words}, binding).accepted())
            return p99_wrist_read_refuse(reason, P99WristReadReject::Commands);
        staged.words = words;
        staged.commands_address = reinterpret_cast<std::uintptr_t>(commands);
        staged.parent = parent_address;
        staged.callback = reinterpret_cast<std::uintptr_t>(callback);
        *out = staged;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return p99_wrist_read_refuse(reason, P99WristReadReject::Exception);
    }
}

bool seh_p99_wrist_snapshot(void* native_job, const P99WristOwner& expected,
    const P99WristProcessValues& process, const void* segment,
    p99_wrist_snapshot::Result* out, P99WristReadReject* reason = nullptr) {
    if (reason != nullptr) *reason = P99WristReadReject::None;
    if (out == nullptr) return p99_wrist_read_refuse(reason, P99WristReadReject::Output);
    *out = {};
    __try {
        P99WristOwner before{};
        if (!seh_p99_wrist_owner(native_job, &before, reason) || !p99_wrist_owner_same(expected, before))
            return p99_wrist_read_refuse(reason, P99WristReadReject::OwnerIdentity);
        P99WristProcessValues current{};
        if (!seh_p99_wrist_process(reinterpret_cast<const std::uint32_t*>(process.commands_address),
                process.words, reinterpret_cast<const void*>(process.parent), 0,
                reinterpret_cast<const void*>(process.callback), before, &current, reason)) return false;
        if (current.scratch != process.scratch || current.parent != process.parent ||
            current.callback != process.callback || current.commands_address != process.commands_address ||
            current.words != process.words || std::memcmp(current.commands.data(), process.commands.data(),
                process.words * sizeof(std::uint32_t)) != 0)
            return p99_wrist_read_refuse(reason, P99WristReadReject::ProcessChanged);
        p99_wrist_snapshot::Result staged{};
        {
            // This Reader dies before the separate closing owner observation.
            P99SourceReader reader(&P99_SOURCE_QUERY_FUNCTION, &p99_source_copy);
            p99_wrist_snapshot::Context context{};
            if (!reader.bytes(reinterpret_cast<std::uintptr_t>(segment), 0, context.data(), context.size()))
                return p99_wrist_read_refuse(reason, P99WristReadReject::Segment);
            p99_admit::WorkBuffer work{};
            work.address = p99_wrist_snapshot::load<std::uintptr_t>(context, 0);
            std::array<std::byte, 88> work_bytes{};
            if (!reader.bytes(work.address, 0, work_bytes.data(), work_bytes.size()))
                return p99_wrist_read_refuse(reason, P99WristReadReject::WorkBuffer);
            const auto* w = work_bytes.data();
            work.arena = p99_group_load<std::uint64_t>(w, 0);
            work.arena_bytes = p99_group_load<std::uint64_t>(w, 8);
            work.metadata = p99_group_load<std::uint64_t>(w, 0x10);
            work.metadata_bytes = p99_group_load<std::uint64_t>(w, 0x18);
            work.free_head = p99_group_load<std::uint64_t>(w, 0x20);
            work.table = p99_group_load<std::uint64_t>(w, 0x28);
            work.front_cursor = p99_group_load<std::uint64_t>(w, 0x30);
            work.back_cursor = p99_group_load<std::uint64_t>(w, 0x38);
            work.front_count = p99_group_load<std::uint32_t>(w, 0x48);
            work.back_count = p99_group_load<std::uint32_t>(w, 0x4C);
            work.capacity = p99_group_load<std::uint32_t>(w, 0x50);
            std::array<p99_admit::Allocation, p99_admit::kCapacity> records{};
            std::array<p99_admit::Address, p99_admit::kCapacity> table{};
            if (work.arena_bytes != p99_admit::kArenaBytes || work.metadata_bytes != p99_admit::kMetadataBytes ||
                !reader.range(work.arena, p99_admit::kArenaBytes) ||
                !reader.bytes(work.metadata, 0, records.data(), sizeof(records)) ||
                !reader.bytes(work.metadata, p99_admit::kTableOffset, table.data(), sizeof(table)))
                return p99_wrist_read_refuse(reason, P99WristReadReject::Metadata);
            const p99_admit::Metadata metadata{work, records, table};
            p99_wrist_snapshot::Attempt attempt;
            staged = attempt.capture(p99_wrist_snapshot::Phase::AfterProgram, context,
                {g_module, before.normal.raw, current.scratch}, metadata, reader);
            if (!staged.copied()) return p99_wrist_read_refuse(reason, P99WristReadReject::Snapshot);
        }
        P99WristOwner after{};
        // seh_p99_wrist_owner constructs a DISTINCT fresh Reader. No old page
        // certificate or selection is promoted to authority for the closing read.
        if (!seh_p99_wrist_owner(native_job, &after, reason) || !p99_wrist_owner_same(expected, after) ||
            !p99_wrist_owner_same(before, after))
            return p99_wrist_read_refuse(reason, P99WristReadReject::ClosingOwner);
        *out = staged;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return p99_wrist_read_refuse(reason, P99WristReadReject::Exception);
    }
}
