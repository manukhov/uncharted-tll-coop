#include "nadine_p99_group_program_manifest.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

namespace {
namespace policy = coop::p99_group_program_manifest;
std::size_t checks = 0;
#define CHECK(condition) \
    do { ++checks; if (!(condition)) { \
        std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; \
        return false; } } while (false)

struct Fixture {
    std::array<std::vector<std::byte>, 7> data;
    std::array<policy::RegionView, 7> views{};
    std::array<std::uintptr_t, policy::kBuiltinSnapshotEntries> builtin{};
    std::array<std::uintptr_t, policy::kCustomCount> custom{};
    policy::Bindings bindings{};
    Fixture() {
        for (std::size_t i = 0; i < data.size(); ++i) {
            const auto& region = policy::kRegions[i];
            data[i].resize(region.byte_count);
            for (std::size_t j = 0; j < data[i].size(); ++j)
                data[i][j] = static_cast<std::byte>(
                    policy::kExpectedWords[region.expected_word_first + j / 4] >>
                    (8 * (j % 4)));
            views[i] = {region.id, region.raw_relative, data[i]};
        }
        bindings.module_base = 0x7FF600000000ULL; // Deliberately not retail base.
        bindings.builtin_table_address = bindings.module_base + policy::kBuiltinTableRva;
        bindings.custom_table_address = bindings.module_base + policy::kCustomTableRva;
        bindings.custom_count = policy::kCustomCount;
        for (const auto& binding : policy::kBindings) {
            auto& slot = binding.opcode < 128 ? builtin[binding.opcode] : custom[binding.opcode - 128];
            slot = bindings.module_base + binding.handler_rva;
        }
        bindings.builtin_entries = builtin;
        bindings.custom_entries = custom;
    }
};

bool every_pinned_byte_is_compared() {
    Fixture fixture;
    CHECK(policy::validate(fixture.views, fixture.bindings).admitted());
    for (std::size_t region = 0; region < fixture.data.size(); ++region) {
        for (auto& byte : fixture.data[region]) {
            byte ^= std::byte{1};
            const auto result = policy::validate(fixture.views, fixture.bindings);
            CHECK(result.reason == policy::Reject::kRegionBytes);
            CHECK(result.detail == region);
            byte ^= std::byte{1};
        }
    }
    CHECK(policy::validate(fixture.views, fixture.bindings).admitted());
    return true;
}

bool region_identity_and_extent_fail_closed() {
    Fixture fixture;
    CHECK(policy::validate_regions({}).reason == policy::Reject::kRegionCount);
    CHECK(policy::validate_regions(std::span(fixture.views).first(6)).reason == policy::Reject::kRegionCount);
    for (std::size_t i = 0; i < fixture.views.size(); ++i) {
        auto views = fixture.views;
        ++views[i].raw_relative;
        CHECK(policy::validate_regions(views).reason == policy::Reject::kRegionIdentity);
        views = fixture.views;
        views[i].id = static_cast<policy::Region>(255);
        CHECK(policy::validate_regions(views).reason == policy::Reject::kRegionIdentity);
        views = fixture.views;
        views[i].bytes = views[i].bytes.first(views[i].bytes.size() - 1);
        CHECK(policy::validate_regions(views).reason == policy::Reject::kRegionExtent);
        views[i].bytes = {};
        CHECK(policy::validate_regions(views).reason == policy::Reject::kRegionExtent);
    }
    auto swapped = fixture.views;
    std::swap(swapped[5], swapped[6]);
    CHECK(policy::validate_regions(swapped).reason == policy::Reject::kRegionIdentity);
    return true;
}

bool bindings_are_module_relative_and_complete() {
    Fixture fixture;
    CHECK(policy::validate_bindings(fixture.bindings).admitted());
    for (const auto& binding : policy::kBindings) {
        auto& slot = binding.opcode < 128 ? fixture.builtin[binding.opcode] : fixture.custom[binding.opcode - 128];
        ++slot;
        const auto result = policy::validate_bindings(fixture.bindings);
        CHECK(result.reason == policy::Reject::kHandler);
        CHECK(result.detail == binding.opcode);
        --slot;
    }
    auto bad = fixture.bindings;
    bad.module_base = 0;
    CHECK(policy::validate_bindings(bad).reason == policy::Reject::kModuleBase);
    bad.module_base = std::numeric_limits<std::uintptr_t>::max();
    CHECK(policy::validate_bindings(bad).reason == policy::Reject::kModuleBase);
    bad = fixture.bindings;
    ++bad.builtin_table_address;
    CHECK(policy::validate_bindings(bad).reason == policy::Reject::kTableIdentity);
    bad = fixture.bindings;
    ++bad.custom_table_address;
    CHECK(policy::validate_bindings(bad).reason == policy::Reject::kTableIdentity);
    bad = fixture.bindings;
    bad.custom_count = 5;
    CHECK(policy::validate_bindings(bad).reason == policy::Reject::kCustomCount);
    bad = fixture.bindings;
    bad.builtin_entries = bad.builtin_entries.first(54);
    CHECK(policy::validate_bindings(bad).reason == policy::Reject::kBindingExtent);
    bad = fixture.bindings;
    bad.custom_entries = bad.custom_entries.first(5);
    CHECK(policy::validate_bindings(bad).reason == policy::Reject::kBindingExtent);
    // This unused builtin slot is explicitly outside the exact consumed set.
    fixture.builtin[0] = 123;
    CHECK(policy::validate_bindings(fixture.bindings).admitted());
    return true;
}

bool independently_loaded_asset_matches(const char* path) {
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    const std::vector<char> file{std::istreambuf_iterator<char>{input}, {}};
    CHECK(file.size() == 732928);
    Fixture fixture;
    std::array<policy::RegionView, 7> views{};
    const auto bytes = std::as_bytes(std::span(file));
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& region = policy::kRegions[i];
        views[i] = {region.id, region.raw_relative,
            bytes.subspan(policy::kRawFileOffset + region.raw_relative, region.byte_count)};
    }
    CHECK(policy::validate(views, fixture.bindings).admitted());
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: manifest-tests path/to/nadine-dlc-base.pak\n";
        return 2;
    }
    const std::array<std::pair<const char*, bool (*)()>, 3> tests{{
        {"every pinned byte", every_pinned_byte_is_compared},
        {"region identity/extent", region_identity_and_extent_fail_closed},
        {"native bindings", bindings_are_module_relative_and_complete},
    }};
    for (const auto& [name, run] : tests) {
        if (!run()) {
            std::cerr << "FAIL: " << name << '\n';
            return 1;
        }
    }
    if (!independently_loaded_asset_matches(argv[1])) return 1;
    std::cout << "4/4 tests passed; " << checks << " checks (Release-safe)\n";
}
