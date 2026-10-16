#include "enemy_physics_observation.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
namespace observation = coop::enemy_physics_observation;
using observation::Observation;
using observation::Status;

// Keep checks active in Release builds.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; \
            return false; \
        } \
    } while (false)

constexpr std::uintptr_t kBase = 0x140000000ULL;
constexpr std::uintptr_t kRecord = 0x10000;
constexpr std::uintptr_t kActor = 0x20000;
constexpr std::uintptr_t kComposite = 0x30000;
constexpr std::uintptr_t kBodies = 0x40000;
constexpr std::uintptr_t kBindings = 0x50000;
constexpr std::uintptr_t kHealth = 0x60000;
constexpr std::uintptr_t kNative = 0x70000;
constexpr std::uint32_t kGeneration = 37;

struct FakeReader {
    struct Request { std::uintptr_t address; std::size_t size; };
    std::map<std::uintptr_t, std::vector<std::byte>> regions;
    std::vector<Request> requests;
    std::function<void(FakeReader&, std::uintptr_t, std::size_t)> before_read;
    std::uintptr_t fail_address{};
    std::size_t fail_call{};
    bool partial_failure{};

    void region(std::uintptr_t address, std::size_t size) {
        regions.emplace(address, std::vector<std::byte>(size));
    }

    std::byte* bytes(std::uintptr_t address, std::size_t size) {
        auto it = regions.upper_bound(address);
        if (it == regions.begin()) return nullptr;
        --it;
        const auto offset = address - it->first;
        if (offset > it->second.size() || size > it->second.size() - offset)
            return nullptr;
        return it->second.data() + offset;
    }

    template <class T>
    void put(std::uintptr_t address, const T& value) {
        auto* destination = bytes(address, sizeof(value));
        if (!destination) throw std::logic_error("FakeReader fixture write outside region");
        std::memcpy(destination, &value, sizeof(value));
    }

    bool operator()(std::uintptr_t address, void* destination, std::size_t size) {
        requests.push_back({address, size});
        if (before_read) before_read(*this, address, size);
        auto* source = bytes(address, size);
        if (!source) return false;
        if ((fail_address != 0 && address == fail_address) ||
            (fail_call != 0 && requests.size() == fail_call)) {
            if (partial_failure && size > 1)
                std::memcpy(destination, source, size - 1);
            return false;
        }
        std::memcpy(destination, source, size);
        return true;
    }

    bool requested(std::uintptr_t address) const {
        return std::any_of(requests.begin(), requests.end(), [address](const Request& r) {
            return r.address == address;
        });
    }
};

observation::Entry entry() { return {kRecord, kGeneration, 0}; }

FakeReader fixture() {
    FakeReader read;
    read.region(kRecord, 0x10);
    read.region(kActor, 0x6a0);
    read.region(kComposite, 0x180);
    read.region(kBodies, observation::kMaxBodies * 0x110);
    read.region(kBindings, observation::kMaxBindings * 0x50);
    read.region(kHealth, 0x48);
    read.region(kNative, 0x170);
    read.put(kRecord + 8, kActor);
    read.put(kActor, kBase + observation::kPirateRva);
    read.put(kActor + 0xa8, kRecord);
    read.put(kActor + 0xbc, kGeneration);
    read.put(kActor + 0x238, kComposite);
    read.put(kActor + 0x4a8, kHealth);
    read.put(kActor + 0x580, std::uintptr_t{0x81000});
    read.put(kActor + 0x698, std::uintptr_t{0x82000});
    read.put(kHealth, kBase + observation::kHealthVtableRva);
    read.put(kHealth + 0x3c, std::int32_t{123});
    read.put(kComposite + 8, kActor);
    read.put(kComposite + 0x10, std::uint32_t{2});
    read.put(kComposite + 0x18, std::uint32_t{1});
    read.put(kComposite + 0x24, std::uint32_t{1});
    read.put(kComposite + 0x48, kBodies);
    read.put(kComposite + 0x58, kBindings);
    read.put(kComposite + 0x60, std::uint32_t{2});
    read.put(kComposite + 0xe8, std::int32_t{9});
    read.put(kBindings, observation::Binding{7, 0});
    read.put(kBindings + 0x50, observation::Binding{9, 1});
    for (std::size_t i = 0; i < 2; ++i) {
        const auto body = kBodies + i * 0x110;
        read.put(body + 0x68, kActor);
        read.put(body + 0x50, static_cast<std::uint32_t>(80 + i));
        read.put(body + 0x54, static_cast<std::uint16_t>(10 + i));
        read.put(body + 0xa0, std::uint32_t{0x4000});
    }
    read.put(kBodies + 0x40, kNative);
    read.put(kBodies + 0xc4, std::int16_t{-1});
    read.put(kBodies + 0xc6, std::uint8_t{1});
    read.put(kBodies + 0x110 + 0xc4, std::int16_t{12});
    read.put(kBodies + 0x110 + 0xc6, std::uint8_t{2});
    read.put(kNative + 0x10, std::uintptr_t{0x90000});
    read.put(kNative + 0x160, std::uint8_t{4});
    return read;
}

Observation configuration() {
    auto read = fixture();
    Observation result;
    if (observation::capture(read, kBase, entry(), result) != Status::ok)
        throw std::logic_error("valid fixture capture failed");
    return result;
}

bool source_classification() {
    observation::Body body{};
    body.owner = kActor;
    body.source = 1;
    body.joint = -1;
    CHECK(std::string_view(observation::source_name(body, kActor)) == "owner-root");
    body.joint = 15;
    CHECK(std::string_view(observation::source_name(body, kActor)) == "owner-root");
    body.source = 4;
    CHECK(std::string_view(observation::source_name(body, kActor)) == "special-4-unclassified");
    body.joint = -1;
    CHECK(std::string_view(observation::source_name(body, kActor)) == "special-4-unclassified");
    body.source = 2;
    CHECK(std::string_view(observation::source_name(body, kActor)) == "unclassified");
    body.joint = 0;
    CHECK(std::string_view(observation::source_name(body, kActor)) == "joint");
    body.owner = kActor + 0x1000;
    CHECK(std::string_view(observation::source_name(body, kActor)) == "foreign-owner");
    for (unsigned mode = 0; mode < 4; ++mode) {
        body.flags = 0x1000001u | (mode << 13);
        CHECK(observation::desired_mode(body) == mode);
    }
    return true;
}

bool import_mapping_requires_exact_complete_binding() {
    auto value = configuration();
    CHECK(observation::valid_bindings(value));
    CHECK(observation::has_root_import_mapping(value)); // Second binding matches E8.
    auto incomplete = value;
    --incomplete.copied_bindings;
    CHECK(!observation::valid_bindings(incomplete));
    CHECK(!observation::has_root_import_mapping(incomplete));
    CHECK(!observation::repeated_configuration(incomplete, incomplete));
    incomplete = value;
    --incomplete.copied_bodies;
    CHECK(!observation::valid_bindings(incomplete));
    CHECK(!observation::has_root_import_mapping(incomplete));
    CHECK(!observation::repeated_configuration(incomplete, incomplete));
    value.root_selector = 8;
    CHECK(!observation::has_root_import_mapping(value));
    value.root_selector = -1;
    CHECK(!observation::has_root_import_mapping(value));
    value.bindings[0].key = -1;
    CHECK(observation::has_root_import_mapping(value));
    value.bindings[1].body_index = value.body_count;
    CHECK(!observation::valid_bindings(value));
    CHECK(!observation::has_root_import_mapping(value));
    value = configuration();
    value.binding_count = 0;
    CHECK(!observation::has_root_import_mapping(value));
    value.binding_count = static_cast<std::uint32_t>(observation::kMaxBindings + 1);
    CHECK(!observation::valid_bindings(value));
    CHECK(!observation::has_root_import_mapping(value));
    value = configuration();
    value.body_count = 0;
    CHECK(!observation::valid_bindings(value));
    CHECK(!observation::has_root_import_mapping(value));
    return true;
}

bool moving_pose_and_transient_flags_are_not_identity() {
    const auto first = configuration();
    auto second = first;
    second.local_pose[0] = 50.0F;
    second.derived_pose[6] = 0.5F;
    second.bodies[0].pose[1] = -20.0F;
    second.bodies[1].pose[3] = 1.0F;
    second.bodies[0].blend = 0.2F;
    second.bodies[0].flags ^= 0x1000041u; // No desired-mode bit changes.
    second.flags0 ^= 0x82u;
    second.flags2 ^= 0x30u;
    second.process_flags ^= 0x10u;
    second.hp = 99;
    CHECK(observation::repeated_configuration(first, second));
    return true;
}

bool repeated_configuration_checks_all_owned_links() {
    const auto first = configuration();
    const std::vector<std::function<void(Observation&)>> changes = {
        [](Observation& x) { ++x.entry.record; },
        [](Observation& x) { ++x.entry.id; },
        [](Observation& x) { ++x.actor; },
        [](Observation& x) { ++x.composite; },
        [](Observation& x) { ++x.body_array; },
        [](Observation& x) { ++x.binding_array; },
        [](Observation& x) { ++x.auxiliary; },
        [](Observation& x) { ++x.ragdoll; },
        [](Observation& x) { ++x.optional_body; },
        [](Observation& x) { --x.body_count; },
        [](Observation& x) { --x.binding_count; },
        [](Observation& x) { ++x.root_selector; },
        [](Observation& x) { ++x.bodies[1].owner; },
        [](Observation& x) { ++x.bodies[1].native; },
        [](Observation& x) { ++x.bodies[1].handle_id; },
        [](Observation& x) { ++x.bodies[1].handle_index; },
        [](Observation& x) { ++x.bodies[1].joint; },
        [](Observation& x) { ++x.bodies[1].source; },
        [](Observation& x) { x.bodies[1].flags ^= 0x2000u; },
        [](Observation& x) { ++x.bodies[1].native_motion; },
        [](Observation& x) { x.bodies[1].in_world = !x.bodies[1].in_world; },
        [](Observation& x) { ++x.bindings[1].key; },
        [](Observation& x) { x.bindings[1].body_index = 0; }
    };
    for (const auto& change : changes) {
        auto second = first;
        change(second);
        CHECK(!observation::repeated_configuration(first, second));
        CHECK(!observation::repeated_configuration(second, first));
    }
    auto over = first;
    over.body_count = static_cast<std::uint32_t>(observation::kMaxBodies + 1);
    CHECK(!observation::repeated_configuration(over, over));
    over = first;
    over.binding_count = static_cast<std::uint32_t>(observation::kMaxBindings + 1);
    CHECK(!observation::repeated_configuration(over, over));
    return true;
}

bool complete_capture_copies_typed_fields() {
    auto read = fixture();
    const std::array<float, 8> pose{1, 2, 3, 1, 0, 0, 0, 1};
    const std::array<char, 32> name{'t', 'e', 's', 't'};
    read.put(kActor + 8, name);
    read.put(kActor + 0x130, pose);
    read.put(kActor + 0x240, pose);
    read.put(kBodies, pose);
    read.put(kBodies + 0x9c, 0.25F);
    Observation value;
    CHECK(observation::capture(read, kBase, entry(), value) == Status::ok);
    CHECK(value.actor == kActor && value.composite == kComposite);
    CHECK(value.body_count == 2 && value.binding_count == 2);
    CHECK(value.copied_bodies == 2 && value.copied_bindings == 2);
    CHECK(value.gate18 == 1 && value.gate24 == 1 && value.root_selector == 9);
    CHECK(value.ragdoll == 0x81000 && value.optional_body == 0x82000);
    CHECK(value.local_pose == pose && value.derived_pose == pose);
    CHECK(value.debug_name == name && value.bodies[0].pose == pose);
    CHECK(value.bodies[0].blend == 0.25F);
    CHECK(value.bodies[0].native == kNative && value.bodies[0].native_motion == 4);
    CHECK(value.bodies[0].in_world && !value.bodies[1].in_world);
    CHECK(value.bodies[0].handle_id == 80 && value.bodies[0].handle_index == 10);
    CHECK(value.bodies[0].joint == -1 && value.bodies[1].joint == 12);
    CHECK(value.health_known && value.hp == 123);
    return true;
}

bool null_and_empty_composite_are_observations() {
    auto read = fixture();
    read.put(kActor + 0x238, std::uintptr_t{0});
    Observation value = configuration();
    CHECK(observation::capture(read, kBase, entry(), value) == Status::ok);
    CHECK(value.composite == 0 && value.body_count == 0 && value.binding_count == 0);
    CHECK(!read.requested(kComposite));
    CHECK(read.requested(kActor + 0xbc)); // Absence must still recheck generation.
    read = fixture();
    read.put(kComposite + 0x10, std::uint32_t{0});
    read.put(kComposite + 0x60, std::uint32_t{0});
    read.put(kComposite + 0x48, std::uintptr_t{0});
    read.put(kComposite + 0x58, std::uintptr_t{0});
    CHECK(observation::capture(read, kBase, entry(), value) == Status::ok);
    CHECK(value.composite == kComposite && value.body_count == 0 && value.binding_count == 0);
    CHECK(!read.requested(kBodies) && !read.requested(kBindings));
    CHECK(observation::valid_bindings(value));
    CHECK(!observation::has_root_import_mapping(value));
    return true;
}

bool caps_preserve_partial_counts_without_array_reads() {
    for (const auto offset : {std::uintptr_t{0x10}, std::uintptr_t{0x60}}) {
        auto read = fixture();
        read.put(kComposite + offset, std::uint32_t{33});
        Observation value;
        CHECK(observation::capture(read, kBase, entry(), value) == Status::over_cap);
        CHECK(value.actor == kActor && value.composite == kComposite);
        CHECK(value.body_count == 33 || value.binding_count == 33);
        CHECK(value.copied_bodies == 0 && value.copied_bindings == 0);
        CHECK(!read.requested(kBodies) && !read.requested(kBindings));
    }
    auto read = fixture();
    read.put(kComposite + 0x10, std::uint32_t{32});
    read.put(kComposite + 0x60, std::uint32_t{32});
    Observation value;
    CHECK(observation::capture(read, kBase, entry(), value) == Status::ok);
    CHECK(value.body_count == 32 && value.binding_count == 32);
    CHECK(value.copied_bodies == 32 && value.copied_bindings == 32);
    CHECK(read.requested(kBodies + 31 * 0x110));
    CHECK(read.requested(kBindings + 31 * 0x50));
    return true;
}

bool incomplete_reads_never_report_success() {
    for (const auto address : {kRecord + 8, kActor, kComposite, kBodies,
                               kBodies + 0x110, kBindings, kBindings + 0x50,
                               kNative + 0x10, kNative + 0x160, kActor + 0xbc}) {
        for (bool partial : {false, true}) {
            auto read = fixture();
            read.fail_address = address;
            read.partial_failure = partial;
            Observation value;
            CHECK(observation::capture(read, kBase, entry(), value) == Status::unreadable);
        }
    }
    auto read = fixture();
    read.put(kComposite + 0x48, std::uintptr_t{0});
    Observation value;
    CHECK(observation::capture(read, kBase, entry(), value) == Status::unreadable);
    read = fixture();
    read.put(kComposite + 0x58, std::uintptr_t{0});
    CHECK(observation::capture(read, kBase, entry(), value) == Status::unreadable);
    return true;
}

bool partial_outputs_count_only_complete_subrows() {
    struct Failure {
        std::uintptr_t address;
        std::uint32_t bodies;
        std::uint32_t bindings;
    };
    const Failure failures[] = {
        {kBodies, 0, 0},
        {kNative + 0x10, 0, 0},
        {kNative + 0x160, 0, 0},
        {kBodies + 0x110, 1, 0},
        {kBindings, 2, 0},
        {kBindings + 0x50, 2, 1},
        {kActor + 0xbc, 2, 2}
    };
    for (const auto& failure : failures) {
        auto read = fixture();
        read.fail_address = failure.address;
        read.partial_failure = true;
        Observation value;
        CHECK(observation::capture(read, kBase, entry(), value) == Status::unreadable);
        CHECK(value.body_count == 2 && value.binding_count == 2);
        CHECK(value.copied_bodies == failure.bodies);
        CHECK(value.copied_bindings == failure.bindings);
    }
    return true;
}

bool hp_requires_exact_type_and_complete_read() {
    auto read = fixture();
    read.put(kHealth + 0x3c, std::int32_t{-27});
    Observation value;
    CHECK(observation::capture(read, kBase, entry(), value) == Status::ok);
    CHECK(value.health_known && value.hp == -27); // Signed integer, not float.
    read.put(kHealth, kBase + observation::kHealthVtableRva + 8);
    CHECK(observation::capture(read, kBase, entry(), value) == Status::ok);
    CHECK(!value.health_known && value.hp == 0);
    read = fixture();
    read.fail_address = kHealth;
    read.partial_failure = true;
    CHECK(observation::capture(read, kBase, entry(), value) == Status::ok);
    CHECK(!value.health_known && value.hp == 0);
    read = fixture();
    read.put(kActor + 0x4a8, std::uintptr_t{0});
    CHECK(observation::capture(read, kBase, entry(), value) == Status::ok);
    CHECK(!value.health_known && !read.requested(kHealth));
    return true;
}

bool identity_and_foreign_ownership() {
    auto read = fixture();
    Observation value;
    CHECK(observation::capture(read, kBase, {}, value) == Status::unreadable);
    CHECK(read.requests.empty());
    read.put(kActor, kBase + observation::kPirateRva + 8);
    CHECK(observation::capture(read, kBase, entry(), value) == Status::non_pirate);
    read = fixture();
    read.put(kActor + 0xbc, kGeneration + 1);
    CHECK(observation::capture(read, kBase, entry(), value) == Status::identity_changed);
    read = fixture();
    read.put(kActor + 0xa8, kRecord + 0x100);
    CHECK(observation::capture(read, kBase, entry(), value) == Status::identity_changed);
    read = fixture();
    read.put(kComposite + 8, kActor + 0x1000);
    CHECK(observation::capture(read, kBase, entry(), value) == Status::composite_owner_mismatch);
    read = fixture();
    read.put(kBodies + 0x68, kActor + 0x1000);
    CHECK(observation::capture(read, kBase, entry(), value) == Status::ok);
    CHECK(std::string_view(observation::source_name(value.bodies[0], kActor)) == "foreign-owner");
    return true;
}

bool generation_and_reciprocity_mutation_during_capture() {
    const std::vector<std::function<void(FakeReader&)>> changes = {
        [](FakeReader& r) { r.put(kRecord + 8, kActor + 0x1000); },
        [](FakeReader& r) { r.put(kActor, kBase + observation::kPirateRva + 8); },
        [](FakeReader& r) { r.put(kActor + 0xa8, kRecord + 0x1000); },
        [](FakeReader& r) { r.put(kActor + 0xbc, kGeneration + 1); },
        [](FakeReader& r) { r.put(kActor + 0x238, kComposite + 0x1000); }
    };
    for (const auto& change : changes) {
        auto read = fixture();
        read.before_read = [change](FakeReader& r, std::uintptr_t address, std::size_t) {
            if (address == kBodies) change(r);
        };
        Observation value;
        CHECK(observation::capture(read, kBase, entry(), value) == Status::identity_changed);
    }
    auto read = fixture();
    read.put(kActor + 0x238, std::uintptr_t{0});
    read.before_read = [](FakeReader& r, std::uintptr_t address, std::size_t) {
        if (address == kHealth) r.put(kActor + 0xbc, kGeneration + 1);
    };
    Observation value;
    CHECK(observation::capture(read, kBase, entry(), value) == Status::identity_changed);
    return true;
}

bool new_pass_starts_from_roster_and_field_bounds() {
    auto read = fixture();
    Observation first;
    CHECK(observation::capture(read, kBase, entry(), first) == Status::ok);
    read.put(kRecord + 8, std::uintptr_t{0xa0000}); // Unmapped replacement, no stale actor reuse.
    read.requests.clear();
    Observation second = first;
    CHECK(observation::capture(read, kBase, entry(), second) == Status::unreadable);
    CHECK(!read.requests.empty() && read.requests[0].address == kRecord + 8);
    CHECK(!read.requested(kActor) && second.body_count == 0);
    std::array<std::byte, 8> bytes{};
    const std::uint32_t known = 0x12345678;
    std::memcpy(bytes.data() + 4, &known, sizeof(known));
    CHECK(observation::field<std::uint32_t>(bytes, 4) == known);
    CHECK(observation::field<std::uint32_t>(bytes, 5) == 0);
    CHECK(observation::field<std::uint32_t>(bytes, 9) == 0);
    return true;
}
} // namespace

int main() {
    using Test = std::pair<const char*, bool (*)()>;
    const Test tests[] = {
        {"source classification", source_classification},
        {"import mapping", import_mapping_requires_exact_complete_binding},
        {"moving raw observations", moving_pose_and_transient_flags_are_not_identity},
        {"all repeated links", repeated_configuration_checks_all_owned_links},
        {"typed field capture", complete_capture_copies_typed_fields},
        {"absence and empty", null_and_empty_composite_are_observations},
        {"bounded partial counts", caps_preserve_partial_counts_without_array_reads},
        {"incomplete reads", incomplete_reads_never_report_success},
        {"complete subrow counters", partial_outputs_count_only_complete_subrows},
        {"typed HP", hp_requires_exact_type_and_complete_read},
        {"identity and owner", identity_and_foreign_ownership},
        {"in-pass mutation", generation_and_reciprocity_mutation_during_capture},
        {"fresh pass and field bounds", new_pass_starts_from_roster_and_field_bounds}
    };
    std::size_t passed = 0;
    for (const auto& test : tests) {
        if (!test.second()) {
            std::cerr << "FAILED: " << test.first << '\n';
            return 1;
        }
        ++passed;
    }
    std::cout << "Enemy physics observation: " << passed << '/' << std::size(tests)
              << " offline groups passed; no process access or write admission.\n";
    return 0;
}
