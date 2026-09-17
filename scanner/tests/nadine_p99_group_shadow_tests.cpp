#include "nadine_p99_group_shadow.h"

#include <iostream>
#include <limits>
#include <utility>

namespace {
namespace policy = coop::p99_group_shadow;
std::size_t checks = 0;
#define CHECK(condition) \
    do { ++checks; if (!(condition)) { \
        std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; \
        return false; } } while (false)

constexpr std::array<policy::GroupShape, 2> shapes{{
    {0, 0, 66, 104}, {1, 104, 38, 38},
}};
// Independent semantic target order, including C39 thumb at locals33/35/37.
constexpr std::array<std::uint16_t, 21> targets{
    28, 30, 105, 107, 109, 111, 137, 139, 141, 113, 115,
    117, 119, 121, 123, 125, 127, 129, 131, 133, 135,
};

struct Fixture {
    std::array<std::byte, 32> before{};
    std::array<std::byte, 104 * 48> original{};
    std::array<std::byte, 32> after{};
    policy::SourceRows sources{};
    Fixture() {
        before.fill(std::byte{0xA7});
        after.fill(std::byte{0xB9});
        for (std::size_t i = 0; i < original.size(); ++i) {
            original[i] = static_cast<std::byte>((i * 13 + 7) & 255);
        }
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const std::array<float, 12> values{
                1, 1, 1, 0, 0, 0, 0, 1,
                static_cast<float>(i) * 0.01F, 2, 3, 0,
            };
            std::memcpy(sources[i].data(), values.data(), 48);
        }
    }
    std::span<const std::byte> view(std::size_t group) const {
        return {original.data(), static_cast<std::size_t>(shapes[group].expanded_count) * 48};
    }
};

bool exact_semantic_mapping_and_original_preservation() {
    for (std::size_t group = 0; group < 2; ++group) {
        Fixture fixture;
        const auto original = fixture.original;
        const auto sources = fixture.sources;
        auto result = policy::build(shapes[group], fixture.view(group), sources,
                                    policy::kCompleteMask);
        CHECK(result.reason == policy::Reject::kNone);
        CHECK(result.shadow.valid);
        CHECK(result.shadow.group == group);
        CHECK(result.shadow.byte_count == fixture.view(group).size());
        CHECK(reinterpret_cast<std::uintptr_t>(result.shadow.bytes.data()) % 16 == 0);
        for (std::size_t row = 0; row < shapes[group].expanded_count; ++row) {
            const auto* expected = original.data() + row * 48;
            for (std::size_t i = 0; i < targets.size(); ++i) {
                if (targets[i] == shapes[group].first_global + row) {
                    expected = sources[i].data();
                }
            }
            for (std::size_t byte = 0; byte < 48; ++byte) {
                CHECK(result.shadow.bytes[row * 48 + byte] == expected[byte]);
            }
        }
        // A native worker can change shadow rows without touching either
        // input. In particular all104 expanded rows were copied, not just66.
        result.shadow.bytes.fill(std::byte{0xCD});
        CHECK(fixture.original == original);
        CHECK(fixture.sources == sources);
        for (const auto byte : fixture.before) CHECK(byte == std::byte{0xA7});
        for (const auto byte : fixture.after) CHECK(byte == std::byte{0xB9});
    }
    return true;
}

bool every_shape_field_and_extent_are_checked() {
    Fixture fixture;
    for (std::size_t group = 0; group < 2; ++group) {
        for (int field = 0; field < 4; ++field) {
            auto shape = shapes[group];
            switch (field) {
            case 0: shape.index ^= 1; break;
            case 1: ++shape.first_global; break;
            case 2: ++shape.parameter_count; break;
            default: ++shape.expanded_count; break;
            }
            auto result = policy::build(shape, fixture.view(group), fixture.sources,
                                        policy::kCompleteMask);
            CHECK(result.reason == policy::Reject::kGroupShape);
            CHECK(!result.shadow.valid);
        }
        for (const std::size_t size : {std::size_t{0}, std::size_t{48},
                                       std::size_t{66 * 48},
                                       fixture.view(group).size() - 1}) {
            auto result = policy::build(shapes[group], {fixture.original.data(), size},
                                        fixture.sources, policy::kCompleteMask);
            CHECK(result.reason == policy::Reject::kOriginalExtent);
            CHECK(!result.shadow.valid);
        }
    }
    for (const auto index : {2u, 7u, 65535u}) {
        auto shape = shapes[0];
        shape.index = static_cast<std::uint16_t>(index);
        CHECK(!policy::exact_shape(shape));
    }
    return true;
}

bool complete_sources_required_for_either_group() {
    Fixture fixture;
    for (std::size_t group = 0; group < 2; ++group) {
        for (std::size_t bit = 0; bit < 32; ++bit) {
            auto result = policy::build(shapes[group], fixture.view(group), fixture.sources,
                                        policy::kCompleteMask ^ (1u << bit));
            CHECK(result.reason == policy::Reject::kIncompleteSources);
            CHECK(!result.shadow.valid);
            CHECK(result.shadow.byte_count == 0);
        }
    }
    return true;
}

bool all_numeric_source_components_checked_before_copy() {
    for (std::size_t group = 0; group < 2; ++group) {
        for (std::size_t row = 0; row < targets.size(); ++row) {
            for (const auto component : {0u, 1u, 2u, 4u, 5u, 6u, 7u, 8u, 9u, 10u}) {
                Fixture fixture;
                const float bad = std::numeric_limits<float>::quiet_NaN();
                std::memcpy(fixture.sources[row].data() + component * 4, &bad, 4);
                auto result = policy::build(shapes[group], fixture.view(group), fixture.sources,
                                            policy::kCompleteMask);
                CHECK(result.reason == policy::Reject::kSourceValue);
                CHECK(!result.shadow.valid);
                CHECK(result.shadow.byte_count == 0);
                for (const auto byte : result.shadow.bytes) CHECK(byte == std::byte{});
            }
        }
    }
    return true;
}

bool quaternion_validation_and_unused_w_lanes() {
    Fixture fixture;
    for (const auto component : {3u, 11u}) {
        const std::uint32_t raw = 0x7FC01234;
        std::memcpy(fixture.sources[0].data() + component * 4, &raw, 4);
    }
    auto result = policy::build(shapes[0], fixture.view(0), fixture.sources, policy::kCompleteMask);
    CHECK(result.shadow.valid);
    CHECK(std::memcmp(result.shadow.bytes.data() + 28 * 48, fixture.sources[0].data(), 48) == 0);
    for (const auto bad_qw : {0.0F, 2.0F, std::numeric_limits<float>::infinity()}) {
        auto changed = fixture.sources;
        std::memcpy(changed[0].data() + 28, &bad_qw, 4);
        CHECK(policy::build(shapes[0], fixture.view(0), changed, policy::kCompleteMask).reason ==
              policy::Reject::kSourceValue);
    }
    return true;
}

bool context_is_copied_without_original_pointer_mutation() {
    Fixture fixture;
    policy::Context native{};
    for (std::size_t i = 0; i < native.size(); ++i) native[i] = static_cast<std::byte>(i ^ 0xE5);
    const auto before = native;
    auto result = policy::build(shapes[0], fixture.view(0), fixture.sources, policy::kCompleteMask);
    auto copy = policy::copied_context(native, result.shadow);
    CHECK(copy.has_value());
    CHECK(native == before);
    for (std::size_t i = 0; i < native.size(); ++i) {
        if (i < 0x50 || i >= 0x58) CHECK((*copy)[i] == native[i]);
    }
    std::uintptr_t pointer = 0;
    std::memcpy(&pointer, copy->data() + 0x50, 8);
    CHECK(pointer == reinterpret_cast<std::uintptr_t>(result.shadow.bytes.data()));
    result.shadow.valid = false;
    CHECK(!policy::copied_context(native, result.shadow));
    result.shadow.valid = true;
    result.shadow.byte_count = 66 * 48;
    CHECK(!policy::copied_context(native, result.shadow));
    result.shadow.byte_count = 104 * 48;
    result.shadow.group = 0xFF;
    CHECK(!policy::copied_context(native, result.shadow));
    return true;
}
} // namespace

int main() {
    const std::array<std::pair<const char*, bool (*)()>, 6> tests{{
        {"semantic mapping and unchanged originals", exact_semantic_mapping_and_original_preservation},
        {"shape and exact expanded extents", every_shape_field_and_extent_are_checked},
        {"complete immutable sources", complete_sources_required_for_either_group},
        {"all numeric components before any copy", all_numeric_source_components_checked_before_copy},
        {"quaternion and opaque W lanes", quaternion_validation_and_unused_w_lanes},
        {"copied context ownership", context_is_copied_without_original_pointer_mutation},
    }};
    for (const auto& [name, run] : tests) {
        if (!run()) {
            std::cerr << "FAIL: " << name << '\n';
            return 1;
        }
    }
    std::cout << tests.size() << '/' << tests.size() << " tests passed; " << checks
              << " checks (Release-safe)\n";
}
