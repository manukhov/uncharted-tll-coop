#include "nadine_p99_support_constraint.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

namespace {

namespace support = coop::p99_support_constraint;
using support::Locator;
using support::RejectReason;
using support::ReturnChoice;
using support::Transaction;

std::size_t checks = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        ++checks;                                                               \
        if (!(condition)) {                                                     \
            std::cerr << __func__ << ':' << __LINE__ << ": " #condition       \
                      << "\n";                                                  \
            return false;                                                       \
        }                                                                       \
    } while (false)

Locator locator(
    float x, float y, float z,
    float qx, float qy, float qz, float qw) {
    return {x, y, z, 0.0F, qx, qy, qz, qw};
}

bool close(float lhs, float rhs, float tolerance = 0.00002F) {
    return std::abs(lhs - rhs) <= tolerance;
}

bool same_locator(const Locator& lhs, const Locator& rhs) {
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (!close(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

bool authored_constraint_and_formula() {
    CHECK(support::locator_valid(support::kNativeDownSupportConstraint));
    constexpr std::array<std::uint32_t, 8> expected_bits{
        0x3D90ED53u, 0xBC09CC1Cu, 0xBC974151u, 0u,
        0x3EE07B1Au, 0x3D882E76u, 0xBF49A24Fu, 0x3EDB042Du};
    for (std::size_t index = 0; index < expected_bits.size(); ++index) {
        CHECK(std::bit_cast<std::uint32_t>(
                  support::kNativeDownSupportConstraint[index]) ==
              expected_bits[index]);
    }
    const Locator right_prop = locator(
        0.21F, -0.17F, 0.08F,
        0.0F, 0.0F, 0.382683432F, 0.923879533F);
    const Locator desired = support::compose_rigid(
        right_prop, support::kNativeDownSupportConstraint);
    CHECK(support::locator_valid(desired));
    const Locator recovered = support::make_relative_rigid(
        right_prop, desired);
    CHECK(same_locator(recovered, support::kNativeDownSupportConstraint));
    const Locator terminal = support::terminal_left_target(
        right_prop, right_prop, desired);
    CHECK(same_locator(terminal, desired));
    return true;
}

bool complete_transaction_is_atomic() {
    const std::array<Locator, support::kGetterCount> stock{
        locator(0.1F, 0.2F, 0.3F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(0.4F, 0.5F, 0.6F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(0.7F, 0.8F, 0.9F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(1.0F, 1.1F, 1.2F, 0.0F, 0.0F, 0.0F, 1.0F)};
    const Locator desired = support::compose_rigid(
        stock[support::kRightHandProp],
        support::kNativeDownSupportConstraint);
    Transaction transaction{};
    transaction.begin(true);
    CHECK(transaction.observe(0, stock[0], true, nullptr).choice ==
          ReturnChoice::kStock);
    const auto left_decision = transaction.observe(
        1, stock[1], true, nullptr);
    CHECK(left_decision.choice == ReturnChoice::kStagedLeftWrist);
    const Locator* const staged_pointer = &transaction.staged_left_wrist;
    CHECK(same_locator(*staged_pointer, stock[1]));
    CHECK(transaction.observe(2, stock[2], true, nullptr).choice ==
          ReturnChoice::kStock);
    CHECK(same_locator(*staged_pointer, stock[1]));
    const auto final_decision = transaction.observe(
        3, stock[3], true, &desired);
    CHECK(final_decision.choice == ReturnChoice::kCapturedRightHandProp);
    CHECK(transaction.committed);
    CHECK(&transaction.staged_left_wrist == staged_pointer);
    CHECK(same_locator(*staged_pointer, desired));
    CHECK(same_locator(transaction.stock[0], stock[0]));
    CHECK(same_locator(transaction.stock[2], stock[2]));
    const Locator final_target = support::terminal_left_target(
        transaction.stock[2], transaction.stock[2],
        transaction.staged_left_wrist);
    CHECK(same_locator(final_target, desired));
    return true;
}

bool refusal_preserves_stock_at_every_stage() {
    const std::array<Locator, support::kGetterCount> stock{
        locator(0.1F, 0.2F, 0.3F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(0.4F, 0.5F, 0.6F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(0.7F, 0.8F, 0.9F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(1.0F, 1.1F, 1.2F, 0.0F, 0.0F, 0.0F, 1.0F)};
    const Locator desired = support::compose_rigid(
        stock[support::kRightHandProp],
        support::kNativeDownSupportConstraint);
    for (std::size_t failing = 0; failing < support::kGetterCount; ++failing) {
        Transaction transaction{};
        transaction.begin(true);
        for (std::size_t index = 0; index < support::kGetterCount; ++index) {
            const Locator* supplied = index == support::kLeftHandProp
                ? &desired : nullptr;
            const auto decision = transaction.observe(
                index, stock[index], index != failing, supplied);
            if (index == failing) {
                CHECK(decision.choice == ReturnChoice::kStock);
                CHECK(decision.reason == RejectReason::kMetadata);
                break;
            }
        }
        CHECK(!transaction.committed);
        if (failing > support::kLeftWrist) {
            CHECK(same_locator(transaction.staged_left_wrist,
                               stock[support::kLeftWrist]));
        }
    }

    for (std::size_t missing = 0; missing < support::kGetterCount - 1;
         ++missing) {
        Transaction transaction{};
        transaction.begin(true);
        for (std::size_t index = 0; index < support::kGetterCount; ++index) {
            if (index == missing) {
                continue;
            }
            const Locator* supplied = index == support::kLeftHandProp
                ? &desired : nullptr;
            const auto decision = transaction.observe(
                index, stock[index], true, supplied);
            if (index > missing) {
                CHECK(decision.choice == ReturnChoice::kStock);
                CHECK(decision.reason == RejectReason::kOrder);
                break;
            }
        }
        CHECK(!transaction.committed);
        if (missing > support::kLeftWrist) {
            CHECK(same_locator(transaction.staged_left_wrist,
                               stock[support::kLeftWrist]));
        }
    }
    Transaction missing_last{};
    missing_last.begin(true);
    for (std::size_t index = 0; index < support::kLeftHandProp; ++index) {
        CHECK(missing_last.observe(index, stock[index], true, nullptr).reason ==
              RejectReason::kNone);
    }
    CHECK(!missing_last.committed);
    CHECK(same_locator(missing_last.staged_left_wrist,
                       stock[support::kLeftWrist]));
    return true;
}

bool invalid_locators_and_desired_fail_closed() {
    std::array<Locator, support::kGetterCount> stock{
        locator(0.1F, 0.2F, 0.3F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(0.4F, 0.5F, 0.6F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(0.7F, 0.8F, 0.9F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(1.0F, 1.1F, 1.2F, 0.0F, 0.0F, 0.0F, 1.0F)};
    for (std::size_t failing = 0; failing < support::kGetterCount; ++failing) {
        Transaction transaction{};
        transaction.begin(true);
        auto invalid = stock;
        invalid[failing][0] = std::numeric_limits<float>::quiet_NaN();
        for (std::size_t index = 0; index <= failing; ++index) {
            const auto decision = transaction.observe(
                index, invalid[index], true, nullptr);
            if (index == failing) {
                CHECK(decision.reason == RejectReason::kLocator);
            }
        }
        CHECK(!transaction.committed);
        if (failing > support::kLeftWrist) {
            CHECK(same_locator(transaction.staged_left_wrist,
                               stock[support::kLeftWrist]));
        }
    }

    Transaction invalid_desired{};
    invalid_desired.begin(true);
    for (std::size_t index = 0; index < support::kLeftHandProp; ++index) {
        CHECK(invalid_desired.observe(index, stock[index], true, nullptr).reason ==
              RejectReason::kNone);
    }
    Locator bad_desired{};
    const auto decision = invalid_desired.observe(
        support::kLeftHandProp, stock[support::kLeftHandProp], true,
        &bad_desired);
    CHECK(decision.choice == ReturnChoice::kStock);
    CHECK(decision.reason == RejectReason::kDesired);
    CHECK(!invalid_desired.committed);
    CHECK(same_locator(invalid_desired.staged_left_wrist,
                       stock[support::kLeftWrist]));

    Locator nonunit = stock[0];
    nonunit[7] = 1.05F;
    CHECK(!support::locator_valid(nonunit));
    Transaction nonunit_transaction{};
    nonunit_transaction.begin(true);
    const auto nonunit_decision = nonunit_transaction.observe(
        0, nonunit, true, nullptr);
    CHECK(nonunit_decision.choice == ReturnChoice::kStock);
    CHECK(nonunit_decision.reason == RejectReason::kLocator);
    CHECK(!nonunit_transaction.committed);
    return true;
}

bool nested_unselected_call_hides_and_restores_outer_transaction() {
    const std::array<Locator, support::kGetterCount> stock{
        locator(0.1F, 0.2F, 0.3F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(0.4F, 0.5F, 0.6F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(0.7F, 0.8F, 0.9F, 0.0F, 0.0F, 0.0F, 1.0F),
        locator(1.0F, 1.1F, 1.2F, 0.0F, 0.0F, 0.0F, 1.0F)};
    const Locator desired = support::compose_rigid(
        stock[support::kRightHandProp],
        support::kNativeDownSupportConstraint);
    Transaction tls{};
    tls.begin(true);
    CHECK(tls.observe(0, stock[0], true, nullptr).reason ==
          RejectReason::kNone);
    CHECK(tls.observe(1, stock[1], true, nullptr).choice ==
          ReturnChoice::kStagedLeftWrist);
    Locator* const outer_pointer = &tls.staged_left_wrist;
    const Transaction saved_outer = tls;

    tls = {};
    CHECK(!tls.active);
    CHECK(tls.observe(0, stock[0], true, nullptr).choice ==
          ReturnChoice::kStock);
    tls = saved_outer;
    CHECK(&tls.staged_left_wrist == outer_pointer);
    CHECK(same_locator(tls.staged_left_wrist, stock[1]));
    CHECK(tls.observe(2, stock[2], true, nullptr).reason ==
          RejectReason::kNone);
    CHECK(tls.observe(3, stock[3], true, &desired).choice ==
          ReturnChoice::kCapturedRightHandProp);
    CHECK(tls.committed);
    CHECK(same_locator(*outer_pointer, desired));

    Transaction explicit_failure{};
    explicit_failure.begin(true);
    CHECK(explicit_failure.observe(0, stock[0], true, nullptr).reason ==
          RejectReason::kNone);
    CHECK(explicit_failure.observe(1, stock[1], true, nullptr).choice ==
          ReturnChoice::kStagedLeftWrist);
    explicit_failure.fail(RejectReason::kMetadata);
    CHECK(!explicit_failure.committed);
    CHECK(same_locator(explicit_failure.staged_left_wrist, stock[1]));
    CHECK(explicit_failure.observe(2, stock[2], true, nullptr).choice ==
          ReturnChoice::kStock);
    return true;
}

}  // namespace

int main() {
    const bool ok = authored_constraint_and_formula() &&
        complete_transaction_is_atomic() &&
        refusal_preserves_stock_at_every_stage() &&
        invalid_locators_and_desired_fail_closed() &&
        nested_unselected_call_hides_and_restores_outer_transaction();
    if (!ok) {
        return 1;
    }
    std::cout << "nadine_p99_support_constraint_tests: " << checks
              << " checks passed\n";
    return 0;
}
