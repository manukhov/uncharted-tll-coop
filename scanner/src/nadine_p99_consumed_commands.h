#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

// Value-only inventory of an ALREADY OWNED command-word span. This module does
// not read game memory, dereference encoded addresses, invoke the evaluator or
// establish owner/group/pass identity. The caller supplies the entire span;
// its extent, lifetime and association with normal Process are separate checks.
namespace coop::p99_consumed_commands {

inline constexpr std::size_t kMaxWords = 4096;
inline constexpr std::size_t kMaxCommands = 512;

enum class Status : std::uint8_t {
    Empty,
    Complete,
    Unsupported,
    WordLimit,
    CommandLimit,
    MalformedLength,
    TruncatedCommand,
    WrongKnownLength,
};

struct Animation {
    std::uint32_t opaque_word1 = 0;
    std::uint64_t clip_identity = 0; // Integer only, never a retained resource.
    std::uint32_t output_lane = 0;
    std::uint32_t sample_bits = 0;   // Native sample coordinate, NOT phase [0,1].
};

struct Blend {
    std::uint32_t left_lane = 0;
    std::uint32_t right_lane = 0;
    std::uint32_t output_lane = 0;
    std::uint32_t mode = 0;
    std::uint32_t weight_bits = 0;
};

struct Command {
    std::uint32_t word_offset = 0;
    std::uint16_t opcode = 0;
    std::uint16_t word_count = 0;
    bool supported_shape = false;
    Animation animation{}; // Populated only for exact cmd6.
    Blend blend{};         // Populated only for exact cmd7.
    std::uint32_t lane = 0; // Cmd9 and cmd11 remain DISTINCT opcodes.
};

struct Snapshot {
    Status status = Status::Empty;
    std::size_t input_word_count = 0;
    std::uint32_t copied_word_count = 0;
    std::uint32_t parsed_word_count = 0;
    std::uint32_t command_count = 0;
    std::uint32_t unsupported_count = 0;
    // On failure, the first unparsed/rejected command's offset. At the end of
    // a fully framed span, equals copied_word_count (also for Unsupported).
    std::uint32_t stop_word = 0;
    bool framing_complete = false;
    std::array<std::uint32_t, kMaxWords> words{};
    std::array<Command, kMaxCommands> commands{};

    // Complete means all supplied words have RECOGNIZED SHAPES. It does NOT
    // validate full-list grammar, lane/mode ranges, numeric finiteness, masks,
    // source labels, callbacks, clip lifetime or successful native execution.
    [[nodiscard]] constexpr bool complete() const noexcept {
        return status == Status::Complete;
    }
};

// Fixed forms already audited in nadine_p99_group_admission.h's
// native_prefix_length and its suffix, plus the normal Perform cmd27/6-word
// export (coop_runtime.cpp's consume_suffix). This is deliberately not a
// whitelist for executing or rewriting commands. Plugin23/24 have variable
// payloads: without a payload-specific schema they are UNSUPPORTED here.
[[nodiscard]] constexpr std::uint16_t fixed_word_count(
    std::uint16_t opcode) noexcept {
    switch (opcode) {
    case 0: case 1: case 3: case 4: case 5: case 8: case 9: case 11: case 16:
        return 2;
    case 2: case 13: case 15: case 17: case 35: case 36:
        return 4;
    case 6: case 7: case 14: case 18: case 20: case 27:
        return 6;
    case 10: case 12: case 26: case 34:
        return 10;
    default:
        return 0;
    }
}

// Native framing: low16 = opcode, high16 = DWORD count INCLUDING the header.
// The existing audited envelope requires >=2 and an even count. Odd/one-word
// variants are conservatively incomplete, not silently admitted. Unsupported
// commands still get their raw words and boundary recorded, so they do not hide
// later cmd6/7/9/11 records. Any later framing failure supersedes Unsupported.
// output must not overlap input; input is caller-owned, never a game pointer.
// No dynamic allocation and no floating-point conversions are performed.
inline Status decode(std::span<const std::uint32_t> input,
    Snapshot& output) noexcept {
    output = {};
    output.input_word_count = input.size();
    if (input.empty()) return output.status;
    if (input.size() > kMaxWords) {
        output.status = Status::WordLimit;
        return output.status;
    }
    output.copied_word_count = static_cast<std::uint32_t>(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) output.words[i] = input[i];

    std::uint32_t cursor = 0;
    while (cursor < output.copied_word_count) {
        output.stop_word = cursor;
        if (output.command_count == kMaxCommands) {
            output.status = Status::CommandLimit;
            return output.status;
        }
        const auto header = output.words[cursor];
        const auto opcode = static_cast<std::uint16_t>(header & 0xFFFFu);
        const auto count = static_cast<std::uint16_t>(header >> 16);
        if (count < 2 || (count & 1u) != 0) {
            output.status = Status::MalformedLength;
            return output.status;
        }
        if (count > output.copied_word_count - cursor) {
            output.status = Status::TruncatedCommand;
            return output.status;
        }
        const auto expected = fixed_word_count(opcode);
        if (expected != 0 && count != expected) {
            output.status = Status::WrongKnownLength;
            return output.status;
        }

        auto& command = output.commands[output.command_count++];
        command.word_offset = cursor;
        command.opcode = opcode;
        command.word_count = count;
        command.supported_shape = expected != 0;
        if (expected == 0) ++output.unsupported_count;
        if (opcode == 6) {
            command.animation.opaque_word1 = output.words[cursor + 1];
            command.animation.clip_identity =
                static_cast<std::uint64_t>(output.words[cursor + 2]) |
                (static_cast<std::uint64_t>(output.words[cursor + 3]) << 32);
            command.animation.output_lane = output.words[cursor + 4];
            command.animation.sample_bits = output.words[cursor + 5];
        } else if (opcode == 7) {
            command.blend.left_lane = output.words[cursor + 1];
            command.blend.right_lane = output.words[cursor + 2];
            command.blend.output_lane = output.words[cursor + 3];
            command.blend.mode = output.words[cursor + 4];
            command.blend.weight_bits = output.words[cursor + 5];
        } else if (opcode == 9 || opcode == 11) {
            command.lane = output.words[cursor + 1];
        }
        cursor += count;
        output.parsed_word_count = cursor;
    }
    output.stop_word = cursor;
    output.framing_complete = true;
    output.status = output.unsupported_count == 0
        ? Status::Complete : Status::Unsupported;
    return output.status;
}

static_assert(std::is_standard_layout_v<Snapshot>);
static_assert(std::is_trivially_copyable_v<Snapshot>);
static_assert(std::is_trivially_destructible_v<Snapshot>);

} // namespace coop::p99_consumed_commands
