#include "enemy_jointblend_native_contract.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <utility>

namespace {
namespace contract = coop::enemy_jointblend_native_contract;
using contract::Address;
using contract::ModuleImage;
using contract::PlanStatus;
using contract::SiteId;
using contract::Word16;

// Deliberately active in Release/NDEBUG builds.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; \
            return false; \
        } \
    } while (false)

constexpr ModuleImage kModule{0x140000000, 0x7000000};

Word16 stock_word(const contract::NativeSite& native) {
    Word16 word{};
    for (std::size_t i = 0; i < word.size(); ++i)
        word[i] = static_cast<std::uint8_t>(0xA0 + i);
    const auto offset = static_cast<std::size_t>(native.rva & 0xF);
    for (std::size_t i = 0; i < native.length; ++i) word[offset + i] = native.stock[i];
    return word;
}

Address word_address(const contract::NativeSite& native) {
    return (kModule.base + native.rva) & ~Address{0xF};
}

std::uint32_t read_rel32(const Word16& word, std::size_t offset) {
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < 4; ++i)
        result |= static_cast<std::uint32_t>(word[offset + i]) << (8 * i);
    return result;
}

bool exact_native_sites() {
    constexpr std::array<std::uint32_t, 7> rvas{
        0x17815B7, 0x1782B3A, 0x1A92BA5, 0x1A92E78, 0x1A932F8, 0x1BBE119, 0x1CF0F36};
    constexpr std::array<std::uint32_t, 7> targets{
        0x1A92A00, 0x1A92A00, 0x16878F0, 0x1AB97C0, 0x1AB97C0, 0x1E5AEE0, 0x1CF3790};
    constexpr std::array<std::uint32_t, 7> returns{
        0, 0, 0x1A92BAA, 0x1A92E7D, 0x1A932FD, 0x1BBE11E, 0x1CF0F3B};
    constexpr std::array<contract::NativeResult, 7> results{
        contract::NativeResult::kVoid, contract::NativeResult::kVoid,
        contract::NativeResult::kPointer, contract::NativeResult::kUInt32,
        contract::NativeResult::kUInt32, contract::NativeResult::kVoid,
        contract::NativeResult::kInt32};
    CHECK(contract::kSites.size() == 7);
    for (std::size_t i = 0; i < contract::kSites.size(); ++i) {
        const auto& native = contract::kSites[i];
        CHECK(static_cast<std::size_t>(native.id) == i);
        CHECK(native.rva == rvas[i]);
        CHECK(native.target_rva == targets[i]);
        CHECK(native.return_rva == returns[i]);
        CHECK(native.target_result == results[i]);
        CHECK(native.instruction_end_rva == native.rva + native.length);
        CHECK(native.displacement_offset + 4 == native.length);
        const auto observed = stock_word(native);
        const auto plan = contract::plan_rel32_site(kModule, native.id, word_address(native),
                                                   observed, kModule.base + native.target_rva);
        CHECK(plan.accepted()); // Also checks bytes against independent target/end RVAs.
        CHECK(plan.expected == observed);
        CHECK(plan.replacement == observed);
    }
    CHECK(contract::site(SiteId::kJointBlendCallbackLea)->instruction == contract::Instruction::kLea);
    CHECK(contract::site(SiteId::kJointBlendTailJump)->instruction == contract::Instruction::kJump);
    CHECK(contract::site(SiteId::kCacheReset)->instruction == contract::Instruction::kCall);
    CHECK(contract::site(SiteId::kCount) == nullptr);
    CHECK(contract::site(static_cast<SiteId>(255)) == nullptr);
    return true;
}

bool preserve_all_neighbors() {
    for (const auto& native : contract::kSites) {
        const auto observed = stock_word(native);
        const auto original_copy = observed;
        const Address new_target = kModule.base + 0x400000;
        const auto plan = contract::plan_rel32_site(kModule, native.id, word_address(native),
                                                   observed, new_target);
        CHECK(plan.accepted());
        CHECK(plan.word_address == word_address(native));
        CHECK(plan.expected == original_copy);
        CHECK(observed == original_copy);
        const auto first = static_cast<std::size_t>((native.rva & 0xF) + native.displacement_offset);
        for (std::size_t i = 0; i < observed.size(); ++i)
            if (i < first || i >= first + 4) CHECK(plan.replacement[i] == observed[i]);
        const auto expected = contract::encode_rel32(kModule.base + native.instruction_end_rva, new_target);
        CHECK(expected.valid);
        CHECK(read_rel32(plan.replacement, first) == expected.bits);
        CHECK(plan.replacement != observed);
    }
    return true;
}

bool reject_every_stock_byte_mismatch() {
    for (const auto& native : contract::kSites) {
        for (std::size_t i = 0; i < native.length; ++i) {
            auto observed = stock_word(native);
            observed[(native.rva & 0xF) + i] ^= 1;
            const auto result = contract::plan_rel32_site(kModule, native.id, word_address(native),
                                                         observed, kModule.base + 0x1000);
            CHECK(result.status == PlanStatus::kStockMismatch);
            CHECK(!result.accepted());
            CHECK(result.word_address == 0);
            CHECK(result.expected == Word16{});
            CHECK(result.replacement == Word16{});
        }
    }
    return true;
}

bool rel32_boundaries_without_signed_overflow() {
    constexpr Address end = 0x100000000;
    auto result = contract::encode_rel32(end, end + 0x7FFFFFFF);
    CHECK(result.valid && result.bits == 0x7FFFFFFF);
    result = contract::encode_rel32(end, end - 0x80000000);
    CHECK(result.valid && result.bits == 0x80000000);
    result = contract::encode_rel32(end, end);
    CHECK(result.valid && result.bits == 0);
    result = contract::encode_rel32(end, end - 1);
    CHECK(result.valid && result.bits == 0xFFFFFFFF);
    CHECK(!contract::encode_rel32(end, end + 0x80000000).valid);
    CHECK(!contract::encode_rel32(end, end - 0x80000001).valid);
    CHECK(!contract::encode_rel32(0, 1).valid);
    CHECK(!contract::encode_rel32(1, 0).valid);
    CHECK(!contract::encode_rel32(1, contract::kAddressMax).valid);
    CHECK(!contract::encode_rel32(contract::kAddressMax, 1).valid);
    result = contract::encode_rel32(contract::kAddressMax - 1, contract::kAddressMax);
    CHECK(result.valid && result.bits == 1);
    return true;
}

bool plan_target_boundaries() {
    for (const auto& native : contract::kSites) {
        const auto observed = stock_word(native);
        const Address end = kModule.base + native.instruction_end_rva;
        CHECK(contract::plan_rel32_site(kModule, native.id, word_address(native), observed,
                                       end + 0x7FFFFFFF).accepted());
        CHECK(contract::plan_rel32_site(kModule, native.id, word_address(native), observed,
                                       end - 0x80000000).accepted());
        CHECK(contract::plan_rel32_site(kModule, native.id, word_address(native), observed,
                                       end + 0x80000000).status == PlanStatus::kTargetOutOfRange);
        CHECK(contract::plan_rel32_site(kModule, native.id, word_address(native), observed,
                                       end - 0x80000001).status == PlanStatus::kTargetOutOfRange);
        CHECK(contract::plan_rel32_site(kModule, native.id, word_address(native), observed,
                                       0).status == PlanStatus::kTargetOutOfRange);
    }
    return true;
}

bool module_and_word_bounds() {
    const auto& native = *contract::site(SiteId::kBodyStore);
    const auto observed = stock_word(native);
    const Address target = kModule.base + native.target_rva;
    constexpr std::array<ModuleImage, 5> invalid{{
        {0, 0x7000000}, {kModule.base, 0}, {kModule.base + 1, kModule.size},
        {0xFFFFFFFFFFFFFFF0, 0x20}, {0x1000, contract::kAddressMax},
    }};
    for (const auto module : invalid) {
        CHECK(!contract::valid_module(module));
        CHECK(contract::plan_rel32_site(module, native.id, word_address(native), observed,
                                       target).status == PlanStatus::kInvalidModule);
    }
    CHECK(contract::plan_rel32_site(kModule, SiteId::kCount, word_address(native), observed,
                                   target).status == PlanStatus::kUnknownSite);
    CHECK(contract::plan_rel32_site(kModule, native.id, word_address(native) + 1, observed,
                                   target).status == PlanStatus::kWrongWordAddress);
    CHECK(contract::plan_rel32_site(kModule, native.id, word_address(native) + 16, observed,
                                   target).status == PlanStatus::kWrongWordAddress);
    CHECK(contract::plan_rel32_site({kModule.base, native.rva}, native.id, word_address(native),
                                   observed, target).status == PlanStatus::kOutsideModule);
    CHECK(contract::plan_rel32_site({kModule.base, native.target_rva}, native.id, word_address(native),
                                   observed, target).status == PlanStatus::kOutsideModule);
    const auto& reset = *contract::site(SiteId::kCacheReset);
    // Code span and return byte fit, but the observed whole 16-byte word does not.
    CHECK(contract::plan_rel32_site({kModule.base, reset.instruction_end_rva + Address{1}},
                                   reset.id, word_address(reset), stock_word(reset),
                                   kModule.base + reset.target_rva).status == PlanStatus::kOutsideModule);
    CHECK(!contract::contains_rva(kModule, kModule.size, 1));
    CHECK(!contract::contains_rva(kModule, 0, 0));
    CHECK(!contract::contains_rva(kModule, 1, contract::kAddressMax));
    CHECK(contract::contains_rva(kModule, kModule.size - 1, 1));
    return true;
}

bool sixteen_byte_containment_and_rejected_seams() {
    for (const auto& native : contract::kSites)
        CHECK(contract::fits_aligned16(kModule.base + native.rva, native.length));
    CHECK(contract::fits_aligned16(0x1000, 16));
    CHECK(contract::fits_aligned16(0x100B, 5));
    CHECK(!contract::fits_aligned16(0x100C, 5));
    CHECK(!contract::fits_aligned16(0x1000, 17));
    CHECK(!contract::fits_aligned16(0x1000, 0));
    CHECK(!contract::fits_aligned16(0, 5));
    CHECK(!contract::fits_aligned16(contract::kAddressMax - 3, 5));
    CHECK(!contract::fits_aligned16(kModule.base + contract::kRejectedGameFallbackRva, 5));
    CHECK(!contract::fits_aligned16(kModule.base + contract::kRejectedBodyRefreshRva, 5));
    CHECK(contract::kRejectedGameFallbackRva == 0x6CD2DC);
    CHECK(contract::kRejectedBodyRefreshRva == 0x1CF586D);
    return true;
}

contract::ProcessFrameWitness process_witness() {
    // Independently specified fixture values; expected slot is not computed by
    // the helper from Segment and cannot serve as its own ancestry evidence.
    return {0x7FE9E0, 0x800008, kModule.base + 0x1A92E7D,
            kModule.base + 0x1ABAC69, SiteId::kGroup0Process};
}

bool original_process_frame() {
    const auto correct = process_witness();
    CHECK(contract::validate_process_frame(kModule, correct));
    CHECK(contract::validate_group0_process_frame(kModule, correct));
    auto changed = correct;
    changed.process_site = SiteId::kLaterProcess;
    CHECK(!contract::validate_process_frame(kModule, changed));
    changed.process_wrapper_entry_return_address = kModule.base + 0x1A932FD;
    CHECK(contract::validate_process_frame(kModule, changed));
    CHECK(!contract::validate_group0_process_frame(kModule, changed));
    changed.process_site = SiteId::kPrimaryComposite;
    CHECK(!contract::validate_process_frame(kModule, changed));
    changed.process_site = static_cast<SiteId>(255);
    CHECK(!contract::validate_process_frame(kModule, changed));
    for (const Address bad_slot : {Address{0}, Address{0x800001}, Address{0x800010},
                                   contract::kAddressMax - 7}) {
        changed = correct;
        changed.original_process_return_slot = bad_slot;
        CHECK(!contract::validate_process_frame(kModule, changed));
    }
    for (const Address bad_segment : {Address{0}, Address{0x7FE9E1}, Address{0x7FE9F0},
                                      contract::kAddressMax - 0xFF}) {
        changed = correct;
        changed.segment = bad_segment;
        CHECK(!contract::validate_process_frame(kModule, changed));
    }
    changed = correct;
    changed.segment += 8;
    changed.original_process_return_slot += 8;
    // Same +1628 relationship is insufficient with wrong native alignments.
    CHECK(!contract::validate_process_frame(kModule, changed));
    changed = correct;
    changed.process_wrapper_entry_return_address += 1;
    CHECK(!contract::validate_process_frame(kModule, changed));
    changed = correct;
    // A bridge continuation is not the separate game wrapper-entry return PC.
    changed.process_wrapper_entry_return_address = 0x180001000;
    CHECK(!contract::validate_process_frame(kModule, changed));
    changed = correct;
    changed.postpose_return_address -= 1;
    CHECK(!contract::validate_process_frame(kModule, changed));
    CHECK(!contract::validate_process_frame({kModule.base, 0x1ABAC69}, correct));
    CHECK(!contract::validate_process_frame({0, kModule.size}, correct));
    return true;
}

bool body_store_native_ancestor() {
    const contract::BodyStoreFrameWitness correct{
        0x900008, kModule.base + 0x1CF0F3B, 0x9000B8, kModule.base + 0x1CF5872};
    CHECK(contract::validate_body_store_frame(kModule, correct));
    auto changed = correct;
    changed.store_return_address += 1;
    CHECK(!contract::validate_body_store_frame(kModule, changed));
    changed = correct;
    changed.ancestor_return_address = kModule.base + 0x1BBE11E;
    CHECK(!contract::validate_body_store_frame(kModule, changed));
    changed = correct;
    changed.ancestor_return_address = 0;
    CHECK(!contract::validate_body_store_frame(kModule, changed));
    for (const Address bad_slot : {Address{0}, Address{0x9000B9}, Address{0x9000C8}}) {
        changed = correct;
        changed.ancestor_return_slot = bad_slot;
        CHECK(!contract::validate_body_store_frame(kModule, changed));
    }
    for (const Address bad_slot : {Address{0}, Address{0x900009}, Address{0x900018},
                                   contract::kAddressMax - 0x77}) {
        changed = correct;
        changed.store_entry_return_slot = bad_slot;
        CHECK(!contract::validate_body_store_frame(kModule, changed));
    }
    changed = correct;
    changed.store_entry_return_slot += 8;
    changed.ancestor_return_slot += 8;
    // The B0 relation alone cannot admit a non-native entry alignment.
    CHECK(!contract::validate_body_store_frame(kModule, changed));
    CHECK(!contract::validate_body_store_frame({kModule.base, 0x1CF5872}, correct));
    return true;
}

bool global_postpose_slot_contract() {
    const Address slot = kModule.base + 0x66D0C58;
    const Address game = kModule.base + 0x6CD220;
    CHECK(contract::validate_postpose_slot(kModule, slot, game));
    CHECK(!contract::validate_postpose_slot(kModule, slot, kModule.base + 0x1937D20));
    CHECK(!contract::validate_postpose_slot(kModule, slot, game + 1));
    CHECK(!contract::validate_postpose_slot(kModule, slot, 0));
    CHECK(!contract::validate_postpose_slot(kModule, slot + 8, game));
    CHECK(!contract::validate_postpose_slot(kModule, slot + 1, game));
    CHECK(!contract::validate_postpose_slot({kModule.base, 0x66D0C5F}, slot, game));
    CHECK(contract::validate_postpose_slot({kModule.base, 0x66D0C60}, slot, game));
    return true;
}

bool six_and_seven_argument_abi() {
    using contract::ArgumentLocation;
    using contract::ArgumentMeaning;
    CHECK(contract::kProcessArguments.size() == 7);
    CHECK(contract::kPostposeArguments.size() == 6);
    CHECK(contract::kProcessResult == contract::NativeResult::kUInt32);
    CHECK(contract::kPostposeResult == contract::NativeResult::kIgnoredOpaque);
    CHECK(contract::kProcessArguments[0].meaning == ArgumentMeaning::kWords);
    CHECK(contract::kProcessArguments[1].location == ArgumentLocation::kEdx);
    CHECK(contract::kProcessArguments[4].entry_stack_offset == 0x28);
    CHECK(contract::kProcessArguments[5].meaning == ArgumentMeaning::kPostposeCallback);
    CHECK(contract::kProcessArguments[5].entry_stack_offset == 0x30);
    CHECK(contract::kProcessArguments[6].entry_stack_offset == 0x38);
    CHECK(contract::kPostposeArguments[1].meaning == ArgumentMeaning::kSegment);
    CHECK(contract::kPostposeArguments[1].location == ArgumentLocation::kRdx);
    CHECK(contract::kPostposeArguments[4].meaning == ArgumentMeaning::kPayload);
    CHECK(contract::kPostposeArguments[4].entry_stack_offset == 0x28);
    CHECK(contract::kPostposeArguments[5].meaning == ArgumentMeaning::kJointSetOrNull);
    CHECK(contract::kPostposeArguments[5].entry_stack_offset == 0x30);
    CHECK(contract::kJobPhaseCallbackOffset == 0x18);
    CHECK(contract::kJobPostposeCallbackOffset == 0x20);
    CHECK(contract::kJobValidationCallbackOffset == 0x28);
    CHECK(contract::kJointConversionStringId == 0x248B1AAE);
    CHECK(contract::kPostposeIndirectCallRva == 0x1ABAC66);
    CHECK(contract::kPostposeReturnRva == 0x1ABAC69);
    CHECK(!contract::matches_return(kModule, kModule.base, 0));
    CHECK(!contract::matches_return(kModule, kModule.base + 0x17815BE, 0));
    return true;
}

} // namespace

int main() {
    using Test = std::pair<const char*, bool (*)()>;
    const std::array<Test, 11> tests{{
        {"exact native sites", exact_native_sites},
        {"preserve all neighbors", preserve_all_neighbors},
        {"reject every stock byte mismatch", reject_every_stock_byte_mismatch},
        {"rel32 overflow boundaries", rel32_boundaries_without_signed_overflow},
        {"plan target boundaries", plan_target_boundaries},
        {"module and word bounds", module_and_word_bounds},
        {"16-byte containment and rejected seams", sixteen_byte_containment_and_rejected_seams},
        {"original Process frame", original_process_frame},
        {"body store native ancestor", body_store_native_ancestor},
        {"global postpose slot", global_postpose_slot_contract},
        {"six and seven argument ABI", six_and_seven_argument_abi},
    }};
    std::size_t passed = 0;
    for (const auto& test : tests) {
        if (!test.second()) {
            std::cerr << "FAIL: " << test.first << '\n';
            return 1;
        }
        ++passed;
    }
    std::cout << passed << '/' << tests.size() << " native contract tests passed\n";
    return 0;
}
