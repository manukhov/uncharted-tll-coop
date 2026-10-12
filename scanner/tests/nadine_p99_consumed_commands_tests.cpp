#include "nadine_p99_consumed_commands.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace {
namespace policy = coop::p99_consumed_commands;
using policy::Status;
std::size_t checks = 0;

// These checks execute under Release/NDEBUG too.
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
    return false; \
} } while (false)

constexpr std::uint32_t header(std::uint16_t op, std::uint16_t count) {
    return op | (static_cast<std::uint32_t>(count) << 16);
}

bool exact_operands_and_owned_copy() {
    std::array<std::uint32_t, 22> input{
        0x00020000, 0xAABBCCDD,
        0x00060006, 0x13572468, 0x76543210, 0xFEDCBA98, 37, 0x41200000,
        0x00060007, 41, 42, 43, 0x89ABCDEF, 0x3C23D70A,
        0x00020009, 51,
        0x0002000B, 52,
        0x00020005, 0xFFFFFFFF,
        0x00020010, 0x12345678};
    const auto original = input;
    policy::Snapshot snapshot;
    CHECK(policy::decode(input, snapshot) == Status::Complete);
    CHECK(snapshot.complete() && snapshot.framing_complete);
    CHECK(snapshot.input_word_count == 22 && snapshot.copied_word_count == 22);
    CHECK(snapshot.command_count == 7 && snapshot.parsed_word_count == 22);
    CHECK(snapshot.stop_word == 22 && snapshot.unsupported_count == 0);
    CHECK(snapshot.commands[1].word_offset == 2);
    CHECK(snapshot.commands[1].opcode == 6 && snapshot.commands[1].word_count == 6);
    const auto& anim = snapshot.commands[1].animation;
    CHECK(anim.opaque_word1 == 0x13572468);
    CHECK(anim.clip_identity == 0xFEDCBA9876543210ull);
    CHECK(anim.output_lane == 37 && anim.sample_bits == 0x41200000);
    const auto& blend = snapshot.commands[2].blend;
    CHECK(blend.left_lane == 41 && blend.right_lane == 42 && blend.output_lane == 43);
    CHECK(blend.mode == 0x89ABCDEF && blend.weight_bits == 0x3C23D70A);
    CHECK(snapshot.commands[3].opcode == 9 && snapshot.commands[3].lane == 51);
    CHECK(snapshot.commands[4].opcode == 11 && snapshot.commands[4].lane == 52);
    CHECK(snapshot.commands[6].supported_shape);
    for (std::size_t i = 0; i < input.size(); ++i) {
        CHECK(input[i] == original[i]);
        CHECK(snapshot.words[i] == original[i]);
    }
    input.fill(0);
    CHECK(snapshot.words[3] == 0x13572468);
    CHECK(snapshot.commands[1].animation.clip_identity == 0xFEDCBA9876543210ull);
    return true;
}

bool preserve_all_float_bits_and_opaque_addresses() {
    constexpr std::array<std::uint32_t, 10> bits{
        0, 0x80000000, 1, 0x007FFFFF, 0x3F800000, 0x41200000,
        0x7F800000, 0xFF800000, 0x7FC12345, 0x7F800001};
    for (const auto value : bits) {
        const std::array<std::uint32_t, 12> input{
            0x00060006, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, value,
            0x00060007, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, value};
        policy::Snapshot snapshot;
        CHECK(policy::decode(input, snapshot) == Status::Complete);
        CHECK(snapshot.commands[0].animation.clip_identity == UINT64_MAX);
        CHECK(snapshot.commands[0].animation.sample_bits == value);
        CHECK(snapshot.commands[1].blend.weight_bits == value);
        CHECK(snapshot.words[5] == value && snapshot.words[11] == value);
    }
    return true;
}

bool fixed_lengths_and_wrong_shapes() {
    // Independent transcription of audited fixed forms, not the decoder's table.
    constexpr std::array<std::array<std::uint16_t, 2>, 25> shapes{{
        {0,2},{1,2},{2,4},{3,2},{4,2},{5,2},{6,6},{7,6},{8,2},
        {9,2},{10,10},{11,2},{12,10},{13,4},{14,6},{15,4},{16,2},
        {17,4},{18,6},{20,6},{26,10},{27,6},{34,10},{35,4},{36,4}}};
    for (const auto shape : shapes) {
        for (std::uint16_t count = 2; count <= 12; count += 2) {
            std::array<std::uint32_t, 12> input{};
            input[0] = header(shape[0], count);
            for (std::uint16_t i = 1; i < count; ++i) input[i] = 0xBADC0000u + i;
            policy::Snapshot snapshot;
            const auto status = policy::decode({input.data(), count}, snapshot);
            if (count == shape[1]) {
                CHECK(status == Status::Complete);
                CHECK(snapshot.command_count == 1 && snapshot.commands[0].supported_shape);
                CHECK(snapshot.commands[0].word_count == count);
                for (std::uint16_t i = 0; i < count; ++i) CHECK(snapshot.words[i] == input[i]);
            } else {
                CHECK(status == Status::WrongKnownLength);
                CHECK(!snapshot.complete() && !snapshot.framing_complete);
                CHECK(snapshot.command_count == 0 && snapshot.stop_word == 0);
            }
        }
    }
    return true;
}

bool unknowns_do_not_hide_later_commands() {
    const std::array<std::uint32_t, 26> input{
        0x000200FF, 0xAABBCCDD,
        0x00060006, 3, 0x89ABCDEF, 0x01234567, 4, 0x3F800000,
        0x00060018, 0x248B1AAE, 0, 0, 0, 0,
        0x000A0017, 0xBCA80631, 1, 2, 3, 4, 5, 6, 7, 8,
        0x0002000B, 9};
    policy::Snapshot snapshot;
    CHECK(policy::decode(input, snapshot) == Status::Unsupported);
    CHECK(!snapshot.complete() && snapshot.framing_complete);
    CHECK(snapshot.command_count == 5 && snapshot.unsupported_count == 3);
    CHECK(snapshot.stop_word == input.size());
    CHECK(!snapshot.commands[0].supported_shape && !snapshot.commands[2].supported_shape);
    CHECK(!snapshot.commands[3].supported_shape);
    CHECK(snapshot.commands[1].animation.clip_identity == 0x0123456789ABCDEFull);
    CHECK(snapshot.commands[4].opcode == 11 && snapshot.commands[4].lane == 9);
    for (std::size_t i = 0; i < input.size(); ++i) CHECK(snapshot.words[i] == input[i]);
    // An unknown followed by a broken header is malformed, not merely unsupported.
    const std::array<std::uint32_t, 4> broken{0x000200FF, 1, 0, 2};
    CHECK(policy::decode(broken, snapshot) == Status::MalformedLength);
    CHECK(snapshot.unsupported_count == 1 && snapshot.command_count == 1);
    CHECK(snapshot.stop_word == 2 && !snapshot.framing_complete);
    return true;
}

bool malformed_and_truncated() {
    policy::Snapshot snapshot;
    CHECK(policy::decode({}, snapshot) == Status::Empty);
    CHECK(!snapshot.complete() && !snapshot.framing_complete);
    for (const auto count : std::array<std::uint16_t, 5>{0, 1, 3, 5, 65535}) {
        const std::array<std::uint32_t, 6> input{header(6, count), 1, 2, 3, 4, 5};
        CHECK(policy::decode(input, snapshot) == Status::MalformedLength);
        CHECK(snapshot.command_count == 0 && snapshot.stop_word == 0);
    }
    const std::array<std::uint32_t, 6> input{0x00060006, 1, 2, 3, 4, 5};
    for (std::size_t size = 1; size < input.size(); ++size) {
        CHECK(policy::decode({input.data(), size}, snapshot) == Status::TruncatedCommand);
        CHECK(!snapshot.complete() && !snapshot.framing_complete);
        CHECK(snapshot.parsed_word_count == 0 && snapshot.command_count == 0);
    }
    const std::array<std::uint32_t, 3> tail{0x00020009, 17, 0xFFFEFFFF};
    CHECK(policy::decode(tail, snapshot) == Status::TruncatedCommand);
    CHECK(snapshot.command_count == 1 && snapshot.commands[0].lane == 17);
    CHECK(snapshot.parsed_word_count == 2 && snapshot.stop_word == 2);
    return true;
}

bool exact_limits_and_reset() {
    std::array<std::uint32_t, 4097> input{};
    policy::Snapshot snapshot;
    // Exactly 4096 words and 512 recognized commands: 384*10 + 128*2.
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < 512; ++i) {
        const std::uint16_t count = i < 384 ? 10 : 2;
        input[cursor] = header(i < 384 ? 34 : 9, count);
        for (std::uint16_t j = 1; j < count; ++j) input[cursor + j] = 0xD0000000u + j;
        cursor += count;
    }
    CHECK(cursor == 4096);
    CHECK(policy::decode({input.data(), 4096}, snapshot) == Status::Complete);
    CHECK(snapshot.command_count == 512 && snapshot.parsed_word_count == 4096);
    CHECK(snapshot.commands[511].opcode == 9 && snapshot.commands[511].word_offset == 4094);
    CHECK(policy::decode(input, snapshot) == Status::WordLimit);
    CHECK(snapshot.input_word_count == 4097 && snapshot.copied_word_count == 0);
    CHECK(snapshot.command_count == 0 && !snapshot.framing_complete);
    CHECK(snapshot.commands[511].opcode == 0 && snapshot.words[4095] == 0);
    for (std::size_t i = 0; i < 513; ++i) {
        input[i * 2] = 0x00020009;
        input[i * 2 + 1] = static_cast<std::uint32_t>(i);
    }
    CHECK(policy::decode({input.data(), 1024}, snapshot) == Status::Complete);
    CHECK(snapshot.command_count == 512);
    CHECK(policy::decode({input.data(), 1026}, snapshot) == Status::CommandLimit);
    CHECK(snapshot.copied_word_count == 1026 && snapshot.command_count == 512);
    CHECK(snapshot.parsed_word_count == 1024 && snapshot.stop_word == 1024);
    CHECK(!snapshot.complete() && !snapshot.framing_complete);
    CHECK(snapshot.commands[511].lane == 511 && snapshot.words[1025] == 512);
    CHECK(policy::decode({}, snapshot) == Status::Empty);
    CHECK(snapshot.commands[511].lane == 0 && snapshot.copied_word_count == 0);
    return true;
}

bool bounded_arbitrary_words() {
    struct Guarded {
        std::uint64_t before = 0x123456789ABCDEF0ull;
        policy::Snapshot snapshot;
        std::uint64_t after = 0xFEDCBA9876543210ull;
    } guarded;
    std::array<std::uint32_t, 128> input{};
    std::uint32_t seed = 0x12345678;
    for (std::size_t trial = 0; trial < 2048; ++trial) {
        for (auto& value : input) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            value = seed;
        }
        const auto original = input;
        const auto size = trial % (input.size() + 1);
        policy::decode({input.data(), size}, guarded.snapshot);
        CHECK(guarded.before == 0x123456789ABCDEF0ull);
        CHECK(guarded.after == 0xFEDCBA9876543210ull);
        CHECK(guarded.snapshot.copied_word_count == size);
        CHECK(guarded.snapshot.parsed_word_count <= size);
        CHECK(guarded.snapshot.command_count <= 512);
        CHECK(input == original);
    }
    return true;
}
} // namespace

int main() {
    const std::array tests{
        exact_operands_and_owned_copy,
        preserve_all_float_bits_and_opaque_addresses,
        fixed_lengths_and_wrong_shapes,
        unknowns_do_not_hide_later_commands,
        malformed_and_truncated,
        exact_limits_and_reset,
        bounded_arbitrary_words};
    for (const auto test : tests) if (!test()) return 1;
    std::printf("P99 consumed commands: %zu tests, %zu checks passed\n", tests.size(), checks);
    return 0;
}
