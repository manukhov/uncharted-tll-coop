// Pure owned-value test; no process handles or retail code invocation.
#include "nadine_p99_operand_identity.h"
#include <array>
#include <cstdio>

namespace m = coop::p99_source_materialize;
namespace identity = coop::p99_operand_identity;
struct Operands {
    bool valid = true;
    struct Entry {
        bool valid = true;
        friend bool operator==(const Entry&, const Entry&) = default;
    } row20{}, row157{};
    std::array<m::Record, 21> row20_records{}, row157_records{};
    std::array<m::Row, 21> chloe_references{}, nadine_references{};
    friend bool operator==(const Operands&, const Operands&) = default;
};
unsigned checks = 0, failures = 0;
void check(bool condition) { ++checks; if (!condition) ++failures; }
Operands fixture() {
    Operands value;
    for (std::size_t index = 0; index < 21; ++index) {
        const auto* row = coop::p99_native_operand_fixture::kRows[index].data();
        std::memcpy(value.row20_records[index].data(), row, 46);
        std::memcpy(value.row157_records[index].data(), row + 46, 46);
        std::memcpy(value.chloe_references[index].data(), row + 92, 48);
        std::memcpy(value.nadine_references[index].data(), row + 140, 48);
    }
    return value;
}
int main() {
    const auto original = fixture();
    check(identity::compare(original).admitted());
    for (unsigned flag = 0; flag < 3; ++flag) {
        auto value = original;
        if (flag == 0) value.valid = false;
        if (flag == 1) value.row20.valid = false;
        if (flag == 2) value.row157.valid = false;
        check(identity::compare(value).difference == identity::Difference::kInvalid);
    }
    for (std::size_t index = 0; index < 21; ++index) {
        for (unsigned part = 0; part < 4; ++part) {
            const auto bytes = part < 2 ? 46u : 48u;
            const auto expected = static_cast<identity::Difference>(static_cast<unsigned>(identity::Difference::kRow20) + part);
            for (std::size_t offset = 0; offset < bytes; ++offset) {
                for (unsigned bit = 0; bit < 8; ++bit) {
                    auto value = original;
                    auto* data = part == 0 ? value.row20_records[index].data() :
                        part == 1 ? value.row157_records[index].data() :
                        part == 2 ? value.chloe_references[index].data() : value.nadine_references[index].data();
                    data[offset] ^= static_cast<std::byte>(1u << bit);
                    const auto before = value;
                    const auto result = identity::compare(value);
                    if (part < 2 && offset == 45) check(result.admitted());
                    else check(result.difference == expected && result.row == index);
                    check(value == before);
                }
            }
        }
    }
    // No cached admission: every fresh owned value is compared, including the
    // last byte in the last row, even if previous inputs were identical.
    auto changed = original;
    check(identity::compare(changed).admitted());
    changed.nadine_references.back().back() ^= std::byte{1};
    check(!identity::compare(changed).admitted());
    for (std::size_t index = 0; index < 21; ++index) {
        for (unsigned part = 0; part < 2; ++part) {
            for (unsigned padding = 0; padding < 256; ++padding) {
                auto value = original;
                auto& record = part ? value.row157_records[index] : value.row20_records[index];
                record[45] = static_cast<std::byte>(padding);
                check(identity::compare(value).admitted());
            }
        }
    }
    std::printf("P99 conditional operand identity: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
