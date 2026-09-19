#include "nadine_p99_retarget_table_admission.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>

namespace t = coop::p99_retarget_table_admission;
namespace {
unsigned checks = 0, failures = 0;
void check(bool value, const char* description) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << description << '\n'; }
}
struct Fixture {
    static constexpr std::uintptr_t image = 0x140000000;
    static constexpr std::uintptr_t table = image + t::kTableRva;
    std::size_t region_bytes = t::kTableEndRva - t::kTableRva;
    std::uintptr_t guard = 0;
    unsigned queries = 0, copies = 0, admitted_requests = 0;
    bool query(std::uintptr_t address, coop::p99_group_readability::Region& region) {
        ++queries;
        if (address < table || address - table >= t::kTableEndRva - t::kTableRva) return false;
        const auto base = table + ((address - table) / region_bytes) * region_bytes;
        region = {base, region_bytes, true, true, guard >= base && guard - base < region_bytes};
        return true;
    }
    auto admission(std::uintptr_t base = image) {
        return t::Admission(base,
            [this](auto address, auto& region) { return query(address, region); },
            [this](auto, auto*, auto) { ++copies; return false; });
    }
};

void fixed_scope_reuse() {
    Fixture fixture;
    // Same 9 + 4 + 4 complete-table requests as capture, postcheck and seal.
    // This fake models admission only; it never calls native code or copies data.
    for (const unsigned requests : {9u, 4u, 4u}) {
        auto scope = fixture.admission();
        static_assert(!std::is_copy_constructible_v<decltype(scope)>);
        static_assert(!std::is_move_constructible_v<decltype(scope)>);
        static_assert(std::is_trivially_destructible_v<decltype(scope)>);
        check(scope.query_count() == 0, "construction does not query or touch payload");
        for (unsigned i = 0; i < requests; ++i) {
            check(scope.range(Fixture::image, 175), "every full-table admission remains required");
            ++fixture.admitted_requests;
        }
        check(scope.query_count() == 1, "one broad synthetic region is queried once per scope");
    }
    check(fixture.queries == 3 && fixture.admitted_requests == 17 && fixture.copies == 0,
          "three lexical scopes retain all requests with no table payload cache");
}

void growth_and_poison() {
    // Abstract regions are relative to the table, not OS page aligned.
    Fixture fixture;
    fixture.region_bytes = 0x1000;
    auto scope = fixture.admission();
    check(scope.range(Fixture::image, 175) && fixture.queries == 3, "initial 175 entries checked");
    check(scope.range(Fixture::image, 256) && fixture.queries == 4,
          "growth to native capacity 256 covers 0x4000 bytes and the newly exposed region");
    check(scope.range(Fixture::image, 175) && fixture.queries == 4, "shrinking count still admitted");
    check(!scope.range(Fixture::image, 0) && !scope.range(Fixture::image, 175) && fixture.queries == 4,
          "invalid later count poisons scope despite earlier page admission");

    Fixture tail;
    tail.region_bytes = 0x1000;
    tail.guard = Fixture::table + 0x3000;
    auto growing = tail.admission();
    check(growing.range(Fixture::image, 175), "guard outside current extent is not accessed");
    check(!growing.range(Fixture::image, 256) && tail.queries == 4 && tail.copies == 0,
          "newly included final guarded region rejects complete larger request");
    tail.guard = 0;
    check(!growing.range(Fixture::image, 175) && tail.queries == 4,
          "failed expanded scope cannot resurrect an earlier safe prefix");
    {
        auto renewed = tail.admission();
        check(renewed.range(Fixture::image, 256) && tail.queries == 8,
              "new scope re-queries all four regions after failed scope");
    }
}

void aligned_page_topology() {
    constexpr auto first_page = Fixture::table & ~std::uintptr_t{0xFFF};
    constexpr auto last_page = Fixture::image + 0x3868000;
    static_assert(Fixture::table - first_page == 0xA00);
    static_assert(Fixture::image + t::kTableEndRva - last_page == 0xA00);
    std::array<std::uintptr_t, 10> queried_pages{};
    unsigned queries = 0, copies = 0;
    std::uintptr_t guard_page = 0;
    auto query = [&](std::uintptr_t address, coop::p99_group_readability::Region& region) {
        const auto page = address & ~std::uintptr_t{0xFFF};
        if (queries < queried_pages.size()) queried_pages[queries] = page;
        ++queries;
        if (address < Fixture::table || address >= Fixture::image + t::kTableEndRva)
            return false;
        region = {page, 0x1000, true, true, page == guard_page};
        return true;
    };
    auto copy = [&](auto, auto*, auto) { ++copies; return false; };
    {
        t::Admission scope(Fixture::image, query, copy);
        check(scope.range(Fixture::image, 175) && queries == 4,
              "unaligned native table prefix covers four aligned 4KiB pages");
        check(scope.range(Fixture::image, 256) && queries == 5 &&
              queried_pages[4] == last_page,
              "native capacity includes the fifth partial page at RVA3868000");
        check(scope.range(Fixture::image, 175) && queries == 5 && copies == 0,
              "aligned-page growth retains prefix admission without payload touches");
    }
    queries = 0;
    queried_pages.fill(0);
    guard_page = last_page;
    {
        t::Admission scope(Fixture::image, query, copy);
        check(scope.range(Fixture::image, 175) && queries == 4,
              "last native page guard lies outside the 175-entry prefix");
        check(!scope.range(Fixture::image, 256) && queries == 5 &&
              queried_pages[4] == last_page && guard_page == last_page && copies == 0,
              "guarded fifth partial page rejects full capacity without consuming guard");
        guard_page = 0;
        check(!scope.range(Fixture::image, 175) && queries == 5,
              "failed native-page scope remains poisoned after guard removal");
    }
    {
        t::Admission fresh(Fixture::image, query, copy);
        check(fresh.range(Fixture::image, 256) && queries == 10 && copies == 0,
              "fresh scope re-queries all five aligned pages after guard removal");
        for (unsigned i = 0; i < 5; ++i) {
            check(queried_pages[5 + i] == first_page + i * 0x1000,
                  "fresh aligned-page traversal includes both partial endpoints");
        }
    }
}

void fresh_scope_guard() {
    Fixture fixture;
    {
        auto before_unlock = fixture.admission();
        check(before_unlock.range(Fixture::image, 175), "first lock scope admits initial table");
    }
    fixture.guard = Fixture::table;
    for (unsigned phase = 0; phase < 2; ++phase) {
        auto after_relock = fixture.admission();
        if (after_relock.range(Fixture::image, 175)) ++fixture.admitted_requests;
        check(fixture.queries == phase + 2 && fixture.admitted_requests == 0 && fixture.copies == 0 &&
              fixture.guard == Fixture::table,
              "fresh lock scope observes new guard before lookup without consuming it");
    }
}

void invalid_identity_and_bounds() {
    for (const auto count : std::array<std::uint32_t, 4>{0, 257, 4096, 0xFFFFFFFFu}) {
        Fixture fixture;
        auto scope = fixture.admission();
        check(!scope.range(Fixture::image, count) && fixture.queries == 0,
              "bad count is rejected before page query or native lookup");
        check(!scope.range(Fixture::image, 175), "bad first count permanently refuses scope");
    }
    for (const auto image : std::array<std::uintptr_t, 3>{0,
            std::numeric_limits<std::uintptr_t>::max() - t::kTableRva + 1,
            std::numeric_limits<std::uintptr_t>::max() - t::kTableRva - 0x1F}) {
        Fixture fixture;
        auto scope = fixture.admission(image);
        check(!scope.range(image, 1) && fixture.queries == 0 && fixture.copies == 0,
              "null image, table-address overflow and end overflow refuse before query");
    }
    Fixture fixture;
    auto scope = fixture.admission();
    check(scope.range(Fixture::image, 175), "pin one image identity");
    check(!scope.range(Fixture::image + 0x40, 175) && fixture.queries == 1,
          "no arbitrary entry or borrowed-resource address accepted as current image");
    check(!scope.range(Fixture::image, 175), "image mismatch cannot revive old page observations");
}

std::size_t occurrences(const std::string& value, const std::string& needle) {
    std::size_t count = 0, position = 0;
    while ((position = value.find(needle, position)) != std::string::npos) {
        ++count; position += needle.size();
    }
    return count;
}
std::string between(const std::string& source, const char* first, const char* next) {
    const auto begin = source.find(first);
    if (begin == std::string::npos) return {};
    const auto end = source.find(next, begin + 1);
    return end == std::string::npos ? std::string{} : source.substr(begin, end - begin);
}
void wiring(const std::filesystem::path& source_path) {
    std::ifstream input(source_path, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    check(!source.empty(), "runtime source available for lexical wiring tripwires");
    const auto capture = between(source,
        "bool seh_capture_p99_chloe_transfer_locked_operands_while_locked(",
        "bool capture_p99_chloe_transfer_locked_operands(");
    const auto identity = between(source,
        "bool seh_p99_chloe_transfer_registry_identity_while_locked(",
        "bool p99_chloe_transfer_registry_identity_exact(");
    const std::string declaration = "P99RetargetTableAdmission table_admission(purpose);";
    check(occurrences(source, declaration) == 2 && occurrences(capture, declaration) == 1 &&
          occurrences(identity, declaration) == 1,
          "only capture and each independent identity invocation own a lexical table scope");
    check(occurrences(capture, "&table_admission") == 6 &&
          occurrences(identity, "&table_admission") == 3,
          "all entry helpers and all closing identity lookups use their same local scope");
    for (const auto* function : {&capture, &identity}) {
        check(function->find("p99_chloe_transfer_unlock_skel_table(") == std::string::npos &&
              function->find("evaluate_group(") == std::string::npos &&
              function->find("evaluate_joint(") == std::string::npos,
              "table scope ends inside while_locked functions before unlock or native math");
    }
    const auto find = between(source, "void* p99_source_find_entry(", "void* p99_source_retained_player(");
    const std::string maximum = "coop::p99_retarget_table_admission::kMaximumEntries";
    check(occurrences(source, maximum) == 6,
          "all six P99 retarget count gates use the native capacity constant");
    check(find.find("count == 0 || count > " + maximum) != std::string::npos &&
          find.find("table_admission->range(count)") != std::string::npos &&
          find.find("kSkelTableRetargetEntryCountRva") != std::string::npos &&
          find.find("kSkelTableFindRetargetEntryRva)(source, target)") != std::string::npos,
          "native Find retains fresh count bounds, full range request and actual lookup");
}
} // namespace

int main(int argc, char** argv) {
    fixed_scope_reuse();
    growth_and_poison();
    aligned_page_topology();
    fresh_scope_guard();
    invalid_identity_and_bounds();
    if (argc == 2) wiring(argv[1]);
    else check(false, "runtime source path required");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
