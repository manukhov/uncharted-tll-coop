#include "nadine_p99_group_admission.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {
namespace policy = coop::p99_group_admission;
using policy::Address;
using policy::Reject;
std::size_t checks = 0;

// Execute in Release/NDEBUG too. Do not replace with assert.
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; return false; \
} } while (false)

struct Fixture {
    policy::WorkBuffer work{0x100000, 0x200000, 0x20000, 0x300000, 0x1000,
        0, 0x300E00, 0, 0x1FFF0, 0, 1, 64};
    std::array<policy::Allocation, 64> records{};
    std::array<Address, 64> table{};
    policy::Segment segment{};

    explicit Fixture(std::uint16_t group = 0) {
        // Independent transcription of native sizes/tags; no policy constants.
        const bool g0 = group == 0;
        records[0] = {0, g0 ? 4992u : 1824u, g0 ? 0u : 104u, g0 ? 104u : 38u, 0, 0, 0};
        records[1] = {0x2000, g0 ? 6720u : 2496u, g0 ? 0u : 104u, g0 ? 105u : 39u, 0, 3, 0};
        records[2] = {0x4000, 1072, 0, 266, 0, 5, 0};
        records[63] = {0x1FFF0, 16, 0, 0, 123, 99, 0}; // Unrelated active scratch.
        work.front_count = g0 ? 3 : 2;
        work.front_cursor = records[work.front_count - 1].offset + records[work.front_count - 1].bytes;
        for (std::size_t i = 0; i < work.front_count; ++i) table[i] = 0x300000 + i * 56;
        table[63] = 0x300000 + 63 * 56;
        work.free_head = 0x300000 + work.front_count * 56;
        for (std::size_t i = work.front_count; i < 63; ++i) {
            records[i].next_free = i == 62 ? 0 : 0x300000 + (i + 1) * 56;
        }
        segment = {group, 0x300, 0x200000, 0x202000, 0, g0 ? 0x204000u : 0u};
    }
    policy::Metadata snapshot() const { return {work, records, table}; }
    policy::Decision evaluate() const { return policy::validate_group_allocations(snapshot(), segment); }
};

bool metadata_positive() {
    for (std::uint16_t group = 0; group < 2; ++group) {
        Fixture fixture(group);
        CHECK(fixture.evaluate().accepted());
        // Record indices need not be in allocation order, only the pointer table.
        std::swap(fixture.records[0], fixture.records[1]);
        std::swap(fixture.table[0], fixture.table[1]);
        CHECK(fixture.evaluate().accepted());
        // Inactive pointer slots and free records retain arbitrary old payload.
        fixture.table[20] = std::numeric_limits<Address>::max();
        fixture.records[20].offset = std::numeric_limits<Address>::max();
        CHECK(fixture.evaluate().accepted());
    }
    Fixture full;
    full.work.front_count = 63;
    full.work.front_cursor = 63 * 16;
    full.work.free_head = 0;
    for (std::size_t i = 0; i < 63; ++i) {
        full.records[i] = {16 * i, 16, 0, 0, 0, 8, 0};
        full.table[i] = 0x300000 + i * 56;
    }
    CHECK(policy::validate_work_buffer(full.snapshot()).accepted());
    Fixture empty;
    empty.work.front_count = empty.work.back_count = 0;
    empty.work.front_cursor = 0;
    empty.work.back_cursor = 0x20000;
    empty.work.free_head = 0x300000;
    for (std::size_t i = 0; i < 64; ++i) empty.records[i].next_free = i == 63 ? 0 : 0x300000 + (i + 1) * 56;
    CHECK(policy::validate_work_buffer(empty.snapshot()).accepted());
    CHECK(!empty.evaluate().accepted());
    return true;
}

bool metadata_negative() {
    const auto rejected = [](auto mutate) {
        Fixture fixture;
        mutate(fixture);
        return !fixture.evaluate().accepted();
    };
    CHECK(rejected([](auto& f) { f.work.capacity = 65; }));
    CHECK(rejected([](auto& f) { f.work.arena_bytes = 0x21000; }));
    CHECK(rejected([](auto& f) { f.work.metadata_bytes = 0x1001; }));
    CHECK(rejected([](auto& f) { f.work.front_count = UINT32_MAX; }));
    CHECK(rejected([](auto& f) { f.work.back_count = 62; }));
    CHECK(rejected([](auto& f) { f.work.front_cursor = 0x20000; }));
    CHECK(rejected([](auto& f) { f.work.front_cursor -= 16; }));
    CHECK(rejected([](auto& f) { f.work.front_cursor += 16; }));
    CHECK(rejected([](auto& f) { f.work.back_cursor -= 16; }));
    CHECK(rejected([](auto& f) { f.work.back_cursor += 16; }));
    CHECK(rejected([](auto& f) { f.work.back_cursor = UINT64_MAX; }));
    CHECK(rejected([](auto& f) { ++f.work.arena; }));
    CHECK(rejected([](auto& f) { ++f.work.metadata; }));
    CHECK(rejected([](auto& f) { ++f.work.table; }));
    CHECK(rejected([](auto& f) { f.work.address = 0; }));
    CHECK(rejected([](auto& f) { f.work.address = UINT64_MAX - 15; }));
    CHECK(rejected([](auto& f) { f.work.arena = UINT64_MAX - 0xFFFF; }));
    CHECK(rejected([](auto& f) { f.work.metadata = UINT64_MAX - 15; }));
    CHECK(rejected([](auto& f) { f.work.arena = f.work.metadata; }));
    CHECK(rejected([](auto& f) { f.work.address = f.work.metadata; }));
    CHECK(rejected([](auto& f) { f.table[0] = 0; }));
    CHECK(rejected([](auto& f) { f.table[0] = 0x2FFFF8; }));
    CHECK(rejected([](auto& f) { f.table[0] = 0x300E00; }));
    CHECK(rejected([](auto& f) { f.table[0] += 8; }));
    CHECK(rejected([](auto& f) { f.table[1] = f.table[0]; }));
    CHECK(rejected([](auto& f) { std::swap(f.table[0], f.table[1]); }));
    CHECK(rejected([](auto& f) { f.records[1].offset = 16; }));
    CHECK(rejected([](auto& f) { f.records[0].bytes = 0; }));
    CHECK(rejected([](auto& f) { ++f.records[0].offset; }));
    CHECK(rejected([](auto& f) { ++f.records[0].bytes; }));
    CHECK(rejected([](auto& f) { f.records[63].bytes = 32; }));
    CHECK(rejected([](auto& f) { f.records[63].offset = UINT64_MAX - 15; }));
    CHECK(rejected([](auto& f) { f.records[63].first = UINT64_MAX; f.records[63].count = 1; }));
    CHECK(rejected([](auto& f) { f.records[0].next_free = f.work.free_head; }));
    CHECK(rejected([](auto& f) { f.work.free_head = 0; }));
    CHECK(rejected([](auto& f) { f.work.free_head = f.table[0]; }));
    CHECK(rejected([](auto& f) { f.records[62].next_free = f.work.free_head; }));
    CHECK(rejected([](auto& f) { f.records[20].next_free = f.table[63]; }));
    CHECK(rejected([](auto& f) { f.records[20].next_free = 0x300E00; }));
    CHECK(rejected([](auto& f) { f.records[20].next_free = 0; }));
    Fixture fixture;
    CHECK(!policy::validate_work_buffer({fixture.work, std::span(fixture.records).first(63), fixture.table}).accepted());
    CHECK(!policy::validate_work_buffer({fixture.work, fixture.records, std::span(fixture.table).first(63)}).accepted());
    return true;
}

bool allocation_tags() {
    for (std::uint16_t group = 0; group < 2; ++group) {
        for (std::size_t target = 0; target < (group == 0 ? 3u : 2u); ++target) {
            for (int field = 0; field < 5; ++field) {
                Fixture f(group);
                auto& record = f.records[target];
                switch (field) {
                case 0: record.bytes -= 16; break;
                case 1: ++record.first; break;
                case 2: ++record.count; break;
                case 3: ++record.flags; break;
                case 4: ++record.type; break;
                }
                CHECK(!f.evaluate().accepted());
            }
        }
        Fixture f(group);
        f.segment.joint_params += 16;
        CHECK(f.evaluate().reason == Reject::kMissingAllocation);
        f = Fixture(group);
        f.segment.joint_params = f.segment.matrices;
        CHECK(!f.evaluate().accepted());
        f = Fixture(group);
        f.segment.scalars = 0x123400;
        CHECK(f.evaluate().reason == Reject::kSegmentState);
        f = Fixture(group);
        f.segment.flags |= 2;
        CHECK(f.evaluate().reason == Reject::kSegmentState);
        f = Fixture(group);
        f.segment.flags |= 1;
        CHECK(f.evaluate().reason == Reject::kSegmentState);
    }
    Fixture f(1);
    f.segment.auxiliary = 0x204000;
    CHECK(f.evaluate().reason == Reject::kSegmentState);
    f.segment.group = 2;
    CHECK(!f.evaluate().accepted());
    return true;
}

policy::Isolation isolation_fixture() {
    return {{0x400000, 6480}, {0x500000, 128}, {0x500100, 128},
        {0x600000, 4992}, {0x700000, 1008}};
}

bool isolation() {
    Fixture f;
    auto values = isolation_fixture();
    CHECK(policy::validate_isolation(f.work, f.segment, values).accepted());
    values.shadow.start = f.work.arena + 16;
    CHECK(!policy::validate_isolation(f.work, f.segment, values).accepted());
    values = isolation_fixture(); values.external_scratch.start = f.work.metadata;
    CHECK(!policy::validate_isolation(f.work, f.segment, values).accepted());
    values = isolation_fixture(); values.copied_context.start = values.segment_context.start;
    CHECK(!policy::validate_isolation(f.work, f.segment, values).accepted());
    values = isolation_fixture(); values.owned_sources.start = values.shadow.start;
    CHECK(!policy::validate_isolation(f.work, f.segment, values).accepted());
    values = isolation_fixture(); --values.external_scratch.bytes;
    CHECK(!policy::validate_isolation(f.work, f.segment, values).accepted());
    values = isolation_fixture(); values.shadow.bytes = 4991;
    CHECK(!policy::validate_isolation(f.work, f.segment, values).accepted());
    values = isolation_fixture(); values.shadow.start = UINT64_MAX - 15;
    CHECK(!policy::validate_isolation(f.work, f.segment, values).accepted());
    values = isolation_fixture(); ++values.shadow.start;
    CHECK(!policy::validate_isolation(f.work, f.segment, values).accepted());
    CHECK(policy::valid_range({0x1000, 16}));
    CHECK(!policy::valid_range({0x1000, 0}));
    CHECK(!policy::valid_range({0, 16}));
    CHECK(!policy::overlaps({0x1000, 16}, {0x1010, 16}));
    CHECK(policy::overlaps({0x1000, 16}, {0x100F, 16}));
    return true;
}

struct Program {
    policy::CommandBindings binding{0x140000000, 0x1406CD220, 0x810000, 0x820000,
        0x830000, 0x840000, {0x850000, 0x860000, 0x870000}, {0x880000, 0x890000, 0x8A0000}};
    std::vector<std::uint32_t> words;
    std::vector<std::size_t> starts;
    std::uint16_t group;

    std::size_t add(std::uint16_t opcode, std::uint16_t length) {
        const auto start = words.size();
        starts.push_back(start);
        words.resize(start + length, 0xCCCCCCCC); // Native padding is not promised zero.
        words[start] = opcode | (static_cast<std::uint32_t>(length) << 16);
        return start;
    }
    void ptr(std::size_t word, Address address) {
        words[word] = static_cast<std::uint32_t>(address);
        words[word + 1] = static_cast<std::uint32_t>(address >> 32);
    }
    explicit Program(std::uint16_t index = 0, bool sphere = false) : group(index) {
        add(0, 2); add(1, 2);
        auto i = add(2, 4); words[i + 1] = index; words[i + 2] = 1;
        if (index == 0) {
            i = add(9, 2); words[i + 1] = 0;
        } else {
            i = add(6, 6); ptr(i + 2, 0x900000); words[i + 4] = 0;
            i = add(7, 6); words[i + 1] = words[i + 2] = words[i + 3] = 0;
        }
        add(3, 2); add(4, 2);
        if (index == 0) {
            i = add(14, 6); ptr(i + 2, 0x810000); ptr(i + 4, 0x820000);
        }
        i = add(15, 4); ptr(i + 2, 0x830000);
        add(16, 2);
        i = add(17, 4); ptr(i + 2, 0x820000);
        if (index == 0) {
            i = add(24, 6); words[i + 1] = 0x248B1AAE; ptr(i + 2, 0); ptr(i + 4, 0x840000);
            if (sphere) {
                i = add(24, 10); words[i + 1] = 0x514C0761; ptr(i + 2, 0);
                ptr(i + 4, 0x850000); ptr(i + 6, 0x860000); ptr(i + 8, 0x870000);
            }
        }
        i = add(26, 10); ptr(i + 2, 0x880000); ptr(i + 4, 0x890000); ptr(i + 6, 0x8A0000);
        words[i + 8] = index == 0 ? 0 : 104; words[i + 9] = index == 0 ? 104 : 38;
        add(5, 2);
    }
    policy::CommandResult evaluate() const { return policy::validate_commands(group, words, binding); }
};

bool commands_positive() {
    Program g0;
    CHECK(g0.words.size() == 48);
    CHECK(g0.evaluate().accepted());
    CHECK(g0.evaluate().cmd16_word == 24);
    CHECK(!g0.evaluate().known_native_prefix);
    Program sphere(0, true);
    CHECK(sphere.words.size() == 58);
    CHECK(sphere.evaluate().accepted());
    Program g1(1);
    CHECK(g1.evaluate().accepted());
    CHECK(g1.evaluate().known_native_prefix);
    CHECK(g1.evaluate().prefix_words == 22);
    CHECK(g1.evaluate().cmd16_word == 28);
    // Every whitelisted direct prefix shape can appear inside an open lane;
    // its payload remains native and is NOT declared validated by the parser.
    constexpr std::array<std::array<std::uint16_t, 2>, 13> shapes{{
        {6,6}, {7,6}, {8,2}, {9,2}, {10,10}, {11,2}, {12,10},
        {13,4}, {18,6}, {20,6}, {34,10}, {35,4}, {36,4}}};
    for (auto shape : shapes) {
        Program f(1);
        std::vector<std::uint32_t> command(shape[1], 0);
        command[0] = shape[0] | (static_cast<std::uint32_t>(shape[1]) << 16);
        f.words.insert(f.words.begin() + 8, command.begin(), command.end());
        CHECK(f.evaluate().accepted());
    }
    return true;
}

bool commands_negative() {
    for (const auto group : {0u, 1u}) {
        const Program valid(static_cast<std::uint16_t>(group));
        for (std::size_t size = 0; size < valid.words.size(); ++size) {
            CHECK(!policy::validate_commands(valid.group, std::span(valid.words).first(size), valid.binding).accepted());
        }
        for (const auto start : valid.starts) {
            for (const auto length : {0u, 1u, 3u, 0xFFFFu}) {
                auto f = valid;
                f.words[start] = (f.words[start] & 0xFFFFu) | (length << 16);
                CHECK(!f.evaluate().accepted());
            }
        }
        auto f = valid; f.words.push_back(0x00020005); f.words.push_back(0);
        CHECK(!f.evaluate().accepted());
        f = valid; f.words[0] = 0x00020005;
        CHECK(!f.evaluate().accepted());
        f = valid; f.words[valid.evaluate().cmd16_word] = 0x00020009;
        CHECK(!f.evaluate().accepted());
        f = valid; f.words[2] = 0x00020010;
        CHECK(!f.evaluate().accepted());
        f = valid; ++f.binding.postpose_callback;
        CHECK(f.evaluate().reason == Reject::kCallback);
        f = valid; f.binding.image_base = UINT64_MAX;
        CHECK(f.evaluate().reason == Reject::kCallback);
        f = valid; ++f.binding.scalar_source;
        CHECK(f.evaluate().reason == Reject::kCommandPayload);
        f = valid; ++f.binding.cmd15_source;
        CHECK(f.evaluate().reason == Reject::kCommandPayload);
        for (std::size_t i = 0; i < 3; ++i) {
            f = valid; ++f.binding.palette_arguments[i];
            CHECK(f.evaluate().reason == Reject::kCommandPayload);
        }
        f = valid; ++f.words[f.starts[f.starts.size() - 2] + 8];
        CHECK(!f.evaluate().accepted());
        f = valid; ++f.words[f.starts[f.starts.size() - 2] + 9];
        CHECK(!f.evaluate().accepted());
    }
    Program f;
    f.words[f.starts[10] + 2] = 1;
    CHECK(f.evaluate().reason == Reject::kCommandPayload);
    f = Program(); ++f.words[f.starts[10] + 1];
    CHECK(!f.evaluate().accepted());
    f = Program(); ++f.binding.pose_source;
    CHECK(!f.evaluate().accepted());
    f = Program(); ++f.binding.conversion_destination;
    CHECK(!f.evaluate().accepted());
    f = Program(); f.words[f.starts[2] + 2] = 2;
    CHECK(!f.evaluate().accepted());
    f = Program(); f.words[f.starts[3] + 1] = 1;
    CHECK(!f.evaluate().accepted());
    f = Program(0, true); f.words[f.starts[11] + 2] = 1;
    CHECK(!f.evaluate().accepted());
    f = Program(0, true); ++f.words[f.starts[11] + 1];
    CHECK(!f.evaluate().accepted());
    for (std::size_t i = 0; i < 3; ++i) {
        f = Program(0, true); ++f.binding.sphere_arguments[i];
        CHECK(!f.evaluate().accepted());
    }
    for (const auto opcode : {0u,1u,4u,5u,14u,15u,16u,17u,19u,21u,22u,23u,24u,25u,26u,27u,28u,32u,33u,37u,0xFFFFu}) {
        f = Program(1); f.words[f.starts[3]] = opcode | 0x60000;
        CHECK(!f.evaluate().accepted());
    }
    f = Program(1); f.words[f.starts[3]] = 0x00060003;
    CHECK(!f.evaluate().accepted());
    f = Program(1); f.words[f.starts[2] + 2] = 0;
    CHECK(!f.evaluate().accepted());
    f = Program(1); f.words[f.starts[2] + 2] = UINT32_MAX;
    CHECK(!f.evaluate().accepted());
    f = Program(1); f.words[f.starts[5]] = 0x00020009; // Missing lane-close.
    CHECK(!f.evaluate().accepted());
    f = Program(); f.group = 2;
    CHECK(!f.evaluate().accepted());
    f.words.assign(4098, 0x00020009);
    CHECK(!f.evaluate().accepted());
    return true;
}

bool command_exhaustive_boundaries() {
    // A one-bit change to any g0 instruction header cannot broaden admission.
    for (const bool sphere : {false, true}) {
        const Program valid(0, sphere);
        for (const auto start : valid.starts) {
            for (unsigned bit = 0; bit < 32; ++bit) {
                auto changed = valid;
                changed.words[start] ^= (1u << bit);
                CHECK(!changed.evaluate().accepted());
            }
        }
    }
    // Exhaust every opcode in a six-word prefix slot. Only the four explicit
    // direct native six-word shapes may pass; every unknown opcode must fail.
    Program prefix(1);
    for (std::uint32_t opcode = 0; opcode <= 0xFFFF; ++opcode) {
        prefix.words[prefix.starts[3]] = 0x60000 | opcode;
        const bool known_six = opcode == 6 || opcode == 7 || opcode == 18 || opcode == 20;
        CHECK(prefix.evaluate().accepted() == known_six);
    }
    return true;
}
} // namespace

int main() {
    const std::array tests{metadata_positive, metadata_negative, allocation_tags,
        isolation, commands_positive, commands_negative, command_exhaustive_boundaries};
    std::size_t passed = 0;
    for (auto test : tests) if (test()) ++passed;
    std::cout << passed << '/' << tests.size() << " tests, " << checks << " checks\n";
    return passed == tests.size() ? 0 : 1;
}
