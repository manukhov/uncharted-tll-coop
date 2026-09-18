#include "nadine_p99_source_materialize.h"
#include "nadine_p99_source_read.h"
#include "nadine_p99_source_native_guard.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace m = coop::p99_source_materialize;
namespace r = coop::p99_source_read;
namespace o = coop::p99_chloe_order;
namespace e = coop::p99_chloe_source_rows;
namespace guard = coop::p99_source_native_guard;
namespace {
unsigned checks = 0, failures = 0;
void check(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}
bool row_valid(const std::byte* bytes) {
    if (!bytes) return false;
    std::array<float, 12> values{};
    std::memcpy(values.data(), bytes, sizeof(values));
    float norm = 0;
    for (unsigned i = 0; i < values.size(); ++i) {
        if (i == 3 || i == 11) continue; // Native unowned W lanes.
        if (!std::isfinite(values[i])) return false;
        if (i >= 4 && i <= 7) {
            if (std::abs(values[i]) > 1.01F) return false;
            norm += values[i] * values[i];
        }
    }
    return norm > 0.90F && norm < 1.10F;
}
m::Row reference_row(std::size_t index, bool nadine) {
    const float offset = static_cast<float>(index + 1) * (nadine ? 0.125F : 0.25F);
    const std::array<float, 12> values{1, 1, 1, 0, 0, 0, 0, 1, offset, -offset, offset * 2, 0};
    m::Row row{};
    std::memcpy(row.data(), values.data(), sizeof(values));
    return row;
}
m::Record record(std::size_t index, bool second) {
    const auto& t = o::kExpectedTransfers[index];
    m::Record value{};
    // Valid scalar half payload, including two identity reference quaternions.
    m::store(value.data() + 0x0C, std::uint16_t{0x3C00});
    m::store(value.data() + 0x1A, std::uint16_t{0x3C00});
    m::store(value.data() + 0x24, second ? t.chloe_global : t.player_global);
    m::store(value.data() + 0x26, static_cast<std::uint16_t>(second
        ? o::parameter_index_for_global(o::kChloeGroups, t.chloe_global)
        : o::parameter_index_for_global(o::kPlayerGroups, t.player_global)));
    m::store(value.data() + 0x28, second ? t.nadine_global : t.chloe_global);
    m::store(value.data() + 0x2A, static_cast<std::uint16_t>(second
        ? o::parameter_index_for_global(o::kNadineGroups, t.nadine_global)
        : o::parameter_index_for_global(o::kChloeGroups, t.chloe_global)));
    value[0x2C] = static_cast<std::byte>(t.mode);
    value[0x2D] = std::byte{0xA7}; // Unowned tail padding must remain permitted.
    return value;
}
struct Operands {
    bool valid = true;
    struct Entry { bool valid = true; } row20{}, row157{};
    std::array<m::Record, 21> row20_records{}, row157_records{};
    std::array<m::Row, 21> chloe_references{}, nadine_references{};
};

// A checked serialized owner graph followed by owned operand copies. It uses
// production Reader/owner, not a fixture that returns Owner directly. This is
// deliberately NOT the live SkelTable lock/registry/native-resolver adapter.
struct Fixture {
    static constexpr std::uintptr_t base = 0x100000, slot = base, retained = base + 0x1000;
    static constexpr std::uintptr_t player = base + 0x2000, fg = base + 0x8000;
    static constexpr std::uintptr_t art = base + 0x9000, raw = base + 0xA000, pose = base + 0xB000;
    static constexpr std::uintptr_t controller = base + 0xC000, grenade = base + 0xD000;
    static constexpr std::uintptr_t weapon = base + 0xE000, handle = base + 0xF000, parent = base + 0x10000;
    static constexpr std::uintptr_t record20 = base + 0x11000, record157 = base + 0x12000;
    static constexpr std::uintptr_t refs_chloe = base + 0x13000, refs_nadine = base + 0x14000;
    std::vector<std::byte> data = std::vector<std::byte>(0x15000);
    r::Contract contract{slot, 0x142A4BFE8, 0x142A47F90, 0x142A227A0, 0x142A9BF48};
    std::uintptr_t guarded_page = 0;
    Fixture() {
        put(slot, retained); put(slot + 8, std::uint32_t{0x123}); put(retained + 8, player);
        put(player, contract.player_vtable); put(player + 0xBC, std::uint32_t{0x123});
        put(player + 0x170, contract.look); put(player + 0x1A8, fg);
        put(player + 0x1D88, controller); put(player + 0x1E50, grenade);
        put(player + 0x140, std::array<float, 4>{0, 0, 0, 1});
        put(player + 0x240, std::array<float, 3>{1, 2, 3});
        put(fg + 6, std::uint8_t{2}); put(fg + 0x18, art); put(fg + 0x100, pose);
        put(fg + 0x1A8, retained); put(fg + 0x1B0, std::uint32_t{0x123});
        put(art + 0x24, contract.art_id); put(art + 0x34, std::uint32_t{580}); put(art + 0x40, raw);
        put(raw + 4, contract.skeleton_id); put(raw + 0x10, std::uint16_t{580});
        put(controller, contract.controller_vtable); put(controller + 0x108, player);
        put(controller + 0x120, std::int32_t{2}); put(controller + 0x134, std::uint32_t{2});
        put(controller + 0x168, handle); put(controller + 0x170, std::uint32_t{0x456});
        put(grenade, contract.grenade_vtable); put(grenade + 0x108, player);
        put(handle + 8, weapon); put(weapon, contract.weapon_vtable); put(weapon + 0x72, std::uint8_t{0x35});
        put(weapon + 0xBC, std::uint32_t{0x456}); put(weapon + 0x860, contract.pistol_id);
        put(weapon + 0x4E0, parent); put(parent + 8, player);
        for (std::size_t i = 0; i < 21; ++i) {
            put(record20 + i * sizeof(m::Record), record(i, false));
            put(record157 + i * sizeof(m::Record), record(i, true));
            put(refs_chloe + i * sizeof(m::Row), reference_row(i, false));
            put(refs_nadine + i * sizeof(m::Row), reference_row(i, true));
        }
    }
    template<class T> void put(std::uintptr_t address, const T& value) {
        std::memcpy(data.data() + address - base, &value, sizeof(value));
    }
    bool query(std::uintptr_t address, coop::p99_group_readability::Region& out) const {
        if (address < base || address - base >= data.size()) return false;
        const auto page = address & ~std::uintptr_t{0xFFF};
        out = {page, 0x1000, true, true, page == guarded_page};
        return true;
    }
    bool copy(std::uintptr_t address, void* output, std::size_t bytes) const {
        if (address < base || bytes > data.size() || address - base > data.size() - bytes) return false;
        std::memcpy(output, data.data() + address - base, bytes);
        return true;
    }
    bool capture(r::Owner& owner, Operands& operands) const {
        r::Reader reader([&](auto address, auto& out) { return query(address, out); },
                         [&](auto address, auto* out, auto bytes) { return copy(address, out, bytes); });
        operands = {};
        operands.valid = false;
        if (!r::owner(reader, contract, player, owner)) return false;
        for (std::size_t i = 0; i < 21; ++i) {
            if (!reader.read(record20, i * sizeof(m::Record), operands.row20_records[i]) ||
                !reader.read(record157, i * sizeof(m::Record), operands.row157_records[i]) ||
                !reader.read(refs_chloe, i * sizeof(m::Row), operands.chloe_references[i]) ||
                !reader.read(refs_nadine, i * sizeof(m::Row), operands.nadine_references[i])) return false;
        }
        operands.valid = true;
        return true; // Reader/certificates die before any injected operation.
    }
};

// Honest scalar oracle: row20 T=2*source+reference; blend T=left+5*additive;
// row157 T=3*source+reference. All other lanes preserve the player source.
// These are intentionally NOT implementations of the engine's transforms.
struct ScalarMath {
    std::size_t index = 0;
    unsigned phase = 0, calls = 0, joints = 0, blends = 0;
    int fail_at = -1;
    bool invalid_result = false;
    std::vector<std::size_t> rows;
    bool joint(const void* record_bytes, const void* source, const void* reference, bool flag, void* output) {
        const bool second = phase == 2;
        check(phase == 0 || second, "DoJoint order");
        check(!flag, "native destination-reference flag remains false");
        const auto expected_record = record(index, second);
        check(std::memcmp(record_bytes, expected_record.data(), 0x2D) == 0, "exact semantic record routing");
        const auto expected_reference = reference_row(index, second);
        check(std::memcmp(reference, expected_reference.data(), sizeof(m::Row)) == 0, "exact reference routing");
        if (!second) {
            rows.push_back(index);
            check(std::memcmp(source, e::kRows[index].player_words.data(), sizeof(m::Row)) == 0, "exact embedded player routing");
        }
        ++joints;
        const auto this_call = calls++;
        if (static_cast<int>(this_call) == fail_at && !invalid_result) return false;
        std::memcpy(output, source, sizeof(m::Row));
        auto* out = static_cast<std::byte*>(output);
        const auto* input = static_cast<const std::byte*>(source);
        const auto* ref = static_cast<const std::byte*>(reference);
        for (unsigned lane = 8; lane <= 10; ++lane) {
            m::store(out + lane * 4, m::load<float>(input + lane * 4) * (second ? 3.0F : 2.0F) + m::load<float>(ref + lane * 4));
        }
        if (static_cast<int>(this_call) == fail_at) m::store(out + 8 * 4, std::numeric_limits<float>::infinity());
        phase = second ? 0 : 1;
        return true;
    }
    void blend(std::uint16_t encoding, void* lhs, void* rhs, float weight, std::uint16_t field, void* scratch) {
        check(phase == 1, "blend occurs between retarget calls");
        check(encoding == 0x0108 && weight == 1.0F && field == 0 && scratch == nullptr, "native blend argument contract");
        auto* left = static_cast<std::byte*>(lhs);
        const auto* right = static_cast<const std::byte*>(rhs);
        check(m::load<std::uint64_t>(left + 96) == 0x8000000000000000ull &&
              m::load<std::uint64_t>(right + 96) == 0x8000000000000000ull &&
              m::load<std::uint64_t>(left + 104) == 0 && m::load<std::uint64_t>(right + 104) == 0, "one-row MSB valid bit payload");
        check(std::memcmp(right, e::kRows[index].chloe_additive_words.data(), sizeof(m::Row)) == 0, "exact additive routing");
        ++blends;
        const auto this_call = calls++;
        for (unsigned lane = 8; lane <= 10; ++lane)
            m::store(left + lane * 4, m::load<float>(left + lane * 4) + 5.0F * m::load<float>(right + lane * 4));
        if (static_cast<int>(this_call) == fail_at) {
            if (invalid_result) m::store(left + 8 * 4, std::numeric_limits<float>::quiet_NaN());
            else m::store(left + 104, std::uint64_t{1});
        }
        phase = 2;
    }
    bool evaluate(const m::Record& first, const m::Record& second, const o::ExpectedJointTransfer& transfer,
        const m::Row& player, const m::Row& additive, const m::Row& chloe, const m::Row& nadine, m::Row* output) {
        index = static_cast<std::size_t>(&transfer - o::kExpectedTransfers.data());
        return m::evaluate_joint(first, second, transfer, player, additive, chloe, nadine, output,
            row_valid, guard::record_valid,
            [&](auto... args) { return joint(args...); }, [&](auto... args) { blend(args...); });
    }
};
bool group(const Operands& operands, unsigned number, ScalarMath& math, m::GroupResult& output) {
    m::SourceRows player{}, chloe{};
    if (!m::build_source_rows(true, number, &player, row_valid) ||
        !m::build_source_rows(false, number, &chloe, row_valid)) return false;
    return m::evaluate_group(operands, player, chloe, number, &output,
        [&](auto&&... args) { return math.evaluate(args...); });
}
m::Row expected_row(std::size_t index) {
    m::Row result{};
    std::memcpy(result.data(), e::kRows[index].player_words.data(), result.size());
    const auto first = reference_row(index, false), second = reference_row(index, true);
    for (unsigned lane = 8; lane <= 10; ++lane) {
        const float player = m::load<float>(result.data() + lane * 4);
        const float additive = std::bit_cast<float>(e::kRows[index].chloe_additive_words[lane]);
        float value = player * 2.0F + m::load<float>(first.data() + lane * 4);
        value += 5.0F * additive;
        value = value * 3.0F + m::load<float>(second.data() + lane * 4);
        m::store(result.data() + lane * 4, value);
    }
    return result;
}
void complete_regression() {
    Fixture fixture;
    const auto before = fixture.data;
    r::Owner owner{};
    Operands operands{};
    check(fixture.capture(owner, operands), "checked owner and operands copied");
    check(owner.player == Fixture::player && owner.weapon == Fixture::weapon && owner.slot == 2 &&
          owner.player_id == 0x123 && owner.weapon_id == 0x456, "checked owner governs fixture evaluation");
    ScalarMath math;
    std::array<m::GroupResult, 2> output{};
    for (unsigned g = 0; g != 2; ++g) check(group(operands, g, math, output[g]), "complete shared group evaluates");
    check(math.calls == 63 && math.joints == 42 && math.blends == 21 && math.rows.size() == 21, "all 21 rows exactly once, 42 retargets and 21 blends");
    for (std::size_t i = 0; i < 21; ++i) {
        const auto g = i < 2 ? 0u : 1u;
        check(math.rows[i] == i, "full semantic loop order");
        check(output[g].present[i] == 1 && output[1 - g].present[i] == 0, "exact two-plus-nineteen partition");
        check(output[g].rows[i] == expected_row(i), "all 48 bytes equal scalar fixture oracle");
        check(output[1 - g].rows[i] == m::Row{}, "unselected row stays zero");
        check(std::memcmp(output[g].player_base.rows[i].data(), e::kRows[i].player_words.data(), 48) == 0 &&
              std::memcmp(output[g].chloe_additive.rows[i].data(), e::kRows[i].chloe_additive_words.data(), 48) == 0, "source operands byte exact");
    }
    m::Mask p0{}, p1{}, c0{}, c1{}, n0{}, n1{};
    p0[0] = 0x140000000ull; p1[10] = 0x1555555555ull;
    c0[0] = 0x28000000000ull; c1[4] = 0x1555555555000ull;
    n0[0] = 0x50000000ull; n1[1] = 0xAAAAAA0000000000ull; n1[2] = 0x2AAAull;
    check(output[0].player_base.valid_mask == p0 && output[1].player_base.valid_mask == p1, "literal frozen player masks");
    check(output[0].chloe_additive.valid_mask == c0 && output[1].chloe_additive.valid_mask == c1, "literal frozen Chloe input masks");
    check(output[0].chloe_composite_mask == c0 && output[1].chloe_composite_mask == c1, "literal frozen Chloe composite masks");
    check(output[0].nadine_output_mask == n0 && output[1].nadine_output_mask == n1, "literal frozen Nadine masks incl thumb reorder");
    check(output[0].player_base.source_group == 0 && output[1].player_base.source_group == 7 &&
          output[0].chloe_additive.source_group == 0 && output[1].chloe_additive.source_group == 2, "exact source group routing");
    r::Owner after{}; Operands again{};
    check(fixture.capture(after, again) && after.player_id == owner.player_id && after.weapon_id == owner.weapon_id,
          "fresh owner resolution after native-style boundary");
    check(fixture.data == before, "all input bytes unchanged including weapon72 and record padding");
}

void rejection_matrix() {
    Fixture fixture;
    r::Owner owner{}; Operands operands{};
    check(fixture.capture(owner, operands), "failure fixture captures");
    for (unsigned g = 0; g < 2; ++g) {
        const unsigned rows = g == 0 ? 2 : 19;
        for (unsigned call = 0; call < rows * 3; ++call) for (bool invalid : {false, true}) {
            ScalarMath math; math.fail_at = static_cast<int>(call); math.invalid_result = invalid;
            m::GroupResult output; output.valid = true;
            check(!group(operands, g, math, output), "every native step failure refuses group");
            check(output == m::GroupResult{}, "no partial result survives failure");
            check(math.calls == call + 1, "failed operation is never retried or followed by another native call");
        }
        m::SourceRows p{}, c{};
        check(m::build_source_rows(true, g, &p, row_valid) && m::build_source_rows(false, g, &c, row_valid), "build failure inputs");
        for (unsigned i = (g == 0 ? 0u : 2u); i < (g == 0 ? 2u : 21u); ++i) for (bool player : {false, true}) {
            auto broken_p = p, broken_c = c;
            (player ? broken_p : broken_c).present[i] = 0;
            ScalarMath math; m::GroupResult output;
            check(!m::evaluate_group(operands, broken_p, broken_c, g, &output,
                [&](auto&&... args) { return math.evaluate(args...); }), "every missing source row rejects");
            check(output == m::GroupResult{}, "missing source cannot publish prefix");
        }
    }
    for (const auto address : {Fixture::weapon, Fixture::record20, Fixture::record157, Fixture::refs_chloe, Fixture::refs_nadine}) {
        Fixture broken; broken.guarded_page = address;
        Operands output{};
        check(!broken.capture(owner, output) && !output.valid, "unreadable owner/operand never reaches shared math");
    }
    fixture.put(Fixture::weapon + 0xBC, std::uint32_t{0x457});
    check(!fixture.capture(owner, operands), "weapon generation mismatch blocks pipeline before materialization");
}

void direct_joint_admission() {
    for (std::size_t index = 0; index < 21; ++index) {
        const auto& t = o::kExpectedTransfers[index];
        m::Row player{}, additive{};
        std::memcpy(player.data(), e::kRows[index].player_words.data(), 48);
        std::memcpy(additive.data(), e::kRows[index].chloe_additive_words.data(), 48);
        const auto chloe = reference_row(index, false), nadine = reference_row(index, true);
        const auto run = [&](const m::Record& first, const m::Record& second) {
            unsigned calls = 0;
            m::Row output; output.fill(std::byte{0x5A});
            const bool ok = m::evaluate_joint(first, second, t, player, additive, chloe, nadine, &output,
                row_valid, guard::record_valid,
                [&](auto...) { ++calls; return true; }, [&](auto...) { ++calls; });
            check(!ok && calls == 0 && output == m::Row{}, "bad operand rejected before ANY native call");
        };
        for (bool second : {false, true}) {
            for (std::size_t offset = 0x24; offset <= 0x2C; ++offset) {
                auto first = record(index, false), last = record(index, true);
                (second ? last : first)[offset] ^= std::byte{0x01};
                run(first, last);
            }
            for (std::size_t offset = 0; offset < 0x24; offset += 2) for (const auto bad : {std::uint16_t{0x7C00}, std::uint16_t{0xFC00}, std::uint16_t{0x7E01}}) {
                auto first = record(index, false), last = record(index, true);
                m::store((second ? last : first).data() + offset, bad);
                run(first, last);
            }
            for (const std::size_t offset : {0x06u, 0x14u}) {
                auto first = record(index, false), last = record(index, true);
                auto& broken = second ? last : first;
                for (std::size_t lane = 0; lane < 4; ++lane) m::store(broken.data() + offset + lane * 2, std::uint16_t{0x8000});
                run(first, last);
            }
        }
        for (unsigned input = 0; input < 4; ++input) {
            std::array<m::Row, 4> rows{player, additive, chloe, nadine};
            m::store(rows[input].data() + 4 * 4, std::numeric_limits<float>::quiet_NaN());
            unsigned calls = 0;
            m::Row output;
            check(!m::evaluate_joint(record(index, false), record(index, true), t,
                rows[0], rows[1], rows[2], rows[3], &output, row_valid, guard::record_valid,
                [&](auto...) { ++calls; return true; }, [&](auto...) { ++calls; }) && calls == 0 && output == m::Row{},
                "all four malformed expanded operands refused before native call");
        }
        // Exact metadata is captured before callbacks and checked after them.
        // Mutation here is injected into our own mutable records, never game memory.
        for (bool second : {false, true}) for (std::size_t offset = 0x24; offset <= 0x2D; ++offset) {
            auto first = record(index, false), last = record(index, true);
            unsigned calls = 0;
            m::Row output;
            const bool success = m::evaluate_joint(first, last, t, player, additive, chloe, nadine,
                &output, row_valid, guard::record_valid,
                [&](const void*, const void* source, const void*, bool, void* destination) {
                    std::memcpy(destination, source, sizeof(m::Row));
                    if (calls++ == 0) (second ? last : first)[offset] ^= std::byte{0x01};
                    return true;
                }, [&](auto...) { ++calls; });
            check(!success && calls == 3 && output == m::Row{}, "native-side metadata/padding mutation refuses completed value without retry");
        }
    }
}
void bounds_and_builder_failure() {
    for (auto group_number : {2u, 255u, std::numeric_limits<unsigned>::max()}) {
        m::SourceRows output; output.valid = true;
        check(!m::build_source_rows(true, group_number, &output, row_valid) && output == m::SourceRows{}, "invalid source group clears stale output");
        Operands operands; ScalarMath math; m::GroupResult result; result.valid = true;
        m::SourceRows p, c; p.valid = c.valid = true; p.target_group = c.target_group = static_cast<std::uint8_t>(group_number);
        check(!m::evaluate_group(operands, p, c, group_number, &result,
              [&](auto&&... args) { return math.evaluate(args...); }) && result == m::GroupResult{} && math.calls == 0,
              "invalid target group refuses without invoking native callback");
    }
    for (unsigned g = 0; g < 2; ++g) for (bool player : {false, true}) {
        const unsigned rows = g == 0 ? 2 : 19;
        for (unsigned bad = 0; bad < rows; ++bad) {
            unsigned seen = 0;
            m::SourceRows output; output.valid = true;
            check(!m::build_source_rows(player, g, &output, [&](auto* bytes) { return seen++ != bad && row_valid(bytes); }) &&
                  output == m::SourceRows{}, "every embedded source validation failure clears complete result");
        }
    }
}
void native_guard_boundaries() {
    const auto original = record(0, false);
    check(guard::record_valid(original), "finite identity packed reference guard admits mode1");
    for (unsigned mode = 0; mode < 256; ++mode) {
        auto value = original; value[0x2C] = static_cast<std::byte>(mode);
        check(guard::record_valid(value) == (mode == 1 || mode == 3), "only statically closed modes1/3 admitted");
    }
    for (unsigned tail = 0; tail < 256; ++tail) {
        auto value = original; value[0x2D] = static_cast<std::byte>(tail);
        check(guard::record_valid(value), "tail padding byte is never interpreted as mode");
    }
    for (std::size_t count = 0; count < original.size(); ++count)
        check(!guard::record_valid(std::span(original.data(), count)), "every truncated record rejected");
    std::array<std::byte, 47> oversized{};
    std::memcpy(oversized.data(), original.data(), original.size());
    check(!guard::record_valid(oversized), "oversized record rejected");
    for (const auto start : {0x06u, 0x14u}) {
        auto value = original;
        for (unsigned lane = 0; lane < 4; ++lane) m::store(value.data() + start + lane * 2, std::uint16_t{0});
        check(!guard::record_valid(value), "positive zero packed quaternion rejected");
        for (unsigned lane = 0; lane < 4; ++lane) {
            auto single = value;
            m::store(single.data() + start + lane * 2, std::uint16_t{1});
            check(guard::record_valid(single), "minimum half subnormal is finite and nonzero");
            m::store(single.data() + start + lane * 2, std::uint16_t{0x8001});
            check(guard::record_valid(single), "negative minimum half subnormal admitted");
            m::store(single.data() + start + lane * 2, std::uint16_t{0x7BFF});
            check(guard::record_valid(single), "maximum finite half component admitted");
        }
    }
}
} // namespace
int main() {
    complete_regression();
    rejection_matrix();
    direct_joint_admission();
    bounds_and_builder_failure();
    native_guard_boundaries();
    std::cout << "P99 source materialize " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
