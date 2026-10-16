#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Pure, offline x64 ABI/byte contracts for tll.exe 1.4.21058.0. No memory
// dereferences, native calls, hook installation or lifetime/phase guarantees.
// A successful plan is NOT authorization to patch a running process. Installation
// still needs a separately reviewed atomicity, protection and relay-lifetime
// protocol. Unknown sites/bytes/frames fail closed; original behavior must remain
// unchanged, including exactly one original call on an unobserved invocation.
namespace coop::enemy_jointblend_native_contract {

using Address = std::uint64_t;
using Word16 = std::array<std::uint8_t, 16>;
inline constexpr Address kAddressMax = std::numeric_limits<Address>::max();

enum class SiteId : std::uint8_t {
    kJointBlendCallbackLea,
    kJointBlendTailJump,
    kCacheReset,
    kGroup0Process,
    kLaterProcess,
    kPrimaryComposite,
    kBodyStore,
    kCount,
};

enum class Instruction : std::uint8_t { kCall, kLea, kJump };
enum class NativeResult : std::uint8_t { kVoid, kPointer, kUInt32, kInt32, kIgnoredOpaque };

struct NativeSite {
    SiteId id;
    std::uint32_t rva;
    std::uint32_t target_rva;
    // Zero for LEA and tail JMP: neither creates a local return address.
    std::uint32_t return_rva;
    std::uint32_t instruction_end_rva;
    Instruction instruction;
    NativeResult target_result;
    std::uint8_t length;
    std::uint8_t displacement_offset;
    std::array<std::uint8_t, 7> stock;
};

inline constexpr std::array<NativeSite, 7> kSites{{
    {SiteId::kJointBlendCallbackLea, 0x17815B7, 0x1A92A00, 0, 0x17815BE,
     Instruction::kLea, NativeResult::kVoid, 7, 3,
     {0x48, 0x8D, 0x05, 0x42, 0x14, 0x31, 0x00}},
    {SiteId::kJointBlendTailJump, 0x1782B3A, 0x1A92A00, 0, 0x1782B3F,
     Instruction::kJump, NativeResult::kVoid, 5, 1,
     {0xE9, 0xC1, 0xFE, 0x30, 0x00, 0, 0}},
    {SiteId::kCacheReset, 0x1A92BA5, 0x16878F0, 0x1A92BAA, 0x1A92BAA,
     Instruction::kCall, NativeResult::kPointer, 5, 1,
     {0xE8, 0x46, 0x4D, 0xBF, 0xFF, 0, 0}},
    {SiteId::kGroup0Process, 0x1A92E78, 0x1AB97C0, 0x1A92E7D, 0x1A92E7D,
     Instruction::kCall, NativeResult::kUInt32, 5, 1,
     {0xE8, 0x43, 0x69, 0x02, 0x00, 0, 0}},
    {SiteId::kLaterProcess, 0x1A932F8, 0x1AB97C0, 0x1A932FD, 0x1A932FD,
     Instruction::kCall, NativeResult::kUInt32, 5, 1,
     {0xE8, 0xC3, 0x64, 0x02, 0x00, 0, 0}},
    {SiteId::kPrimaryComposite, 0x1BBE119, 0x1E5AEE0, 0x1BBE11E, 0x1BBE11E,
     Instruction::kCall, NativeResult::kVoid, 5, 1,
     {0xE8, 0xC2, 0xCD, 0x29, 0x00, 0, 0}},
    {SiteId::kBodyStore, 0x1CF0F36, 0x1CF3790, 0x1CF0F3B, 0x1CF0F3B,
     Instruction::kCall, NativeResult::kInt32, 5, 1,
     {0xE8, 0x55, 0x28, 0x00, 0x00, 0, 0}},
}};

inline constexpr std::uint32_t kRejectedGameFallbackRva = 0x6CD2DC;
inline constexpr std::uint32_t kRejectedBodyRefreshRva = 0x1CF586D;
inline constexpr std::uint32_t kBodyRefreshReturnRva = 0x1CF5872;
inline constexpr std::uint32_t kPostposeIndirectCallRva = 0x1ABAC66;
inline constexpr std::uint32_t kPostposeReturnRva = 0x1ABAC69;
inline constexpr std::uint32_t kPostposeSlotRva = 0x66D0C58;
inline constexpr std::uint32_t kGamePostposeRva = 0x6CD220;
inline constexpr std::uint32_t kNdPostposeRva = 0x1937D20;
inline constexpr std::uint32_t kJointConversionStringId = 0x248B1AAE;
inline constexpr Address kSegmentToOriginalProcessReturnSlot = 0x1628;
inline constexpr Address kBodyStoreToRefreshReturnSlot = 0xB0;

// Windows x64 native ABI. Offsets below are measured at CALLEE entry, before
// its prologue; they are not offsets into a C++ observer's own wrapper frame.
enum class ArgumentLocation : std::uint8_t { kRcx, kRdx, kEdx, kR8, kR9d, kStack };
enum class ArgumentMeaning : std::uint8_t {
    kWords, kWordCount, kAnimContext, kGroup, kPhaseCallback,
    kPostposeCallback, kValidationCallback, kWorkBuffer, kSegment,
    kStringId, kPayload, kJointSetOrNull,
};
struct NativeArgument {
    ArgumentMeaning meaning;
    ArgumentLocation location;
    std::uint8_t entry_stack_offset; // Zero for register arguments.
};
inline constexpr std::array<NativeArgument, 7> kProcessArguments{{
    {ArgumentMeaning::kWords, ArgumentLocation::kRcx, 0},
    {ArgumentMeaning::kWordCount, ArgumentLocation::kEdx, 0},
    {ArgumentMeaning::kAnimContext, ArgumentLocation::kR8, 0},
    {ArgumentMeaning::kGroup, ArgumentLocation::kR9d, 0},
    {ArgumentMeaning::kPhaseCallback, ArgumentLocation::kStack, 0x28},
    {ArgumentMeaning::kPostposeCallback, ArgumentLocation::kStack, 0x30},
    {ArgumentMeaning::kValidationCallback, ArgumentLocation::kStack, 0x38},
}};
inline constexpr std::array<NativeArgument, 6> kPostposeArguments{{
    {ArgumentMeaning::kWorkBuffer, ArgumentLocation::kRcx, 0},
    {ArgumentMeaning::kSegment, ArgumentLocation::kRdx, 0},
    {ArgumentMeaning::kAnimContext, ArgumentLocation::kR8, 0},
    {ArgumentMeaning::kStringId, ArgumentLocation::kR9d, 0},
    {ArgumentMeaning::kPayload, ArgumentLocation::kStack, 0x28},
    {ArgumentMeaning::kJointSetOrNull, ArgumentLocation::kStack, 0x30},
}};
// Process returns its native uint32 result (not bool). Process ignores the
// postpose result; its native return semantics remain opaque, not a C++ void
// signature claim. Reset returns the pointer stored at cache+E0 (matrix bank),
// not the address of that member, and must preserve RAX.
// Primary takes ONLY RCX=CompositeBody, not a world in RDX. Body store takes
// RCX=RigidBody/RDX=converted 32-byte pose and preserves its native int EAX result.
inline constexpr std::uint32_t kJobPhaseCallbackOffset = 0x18;
inline constexpr std::uint32_t kJobPostposeCallbackOffset = 0x20;
inline constexpr std::uint32_t kJobValidationCallbackOffset = 0x28;
inline constexpr NativeResult kProcessResult = NativeResult::kUInt32;
inline constexpr NativeResult kPostposeResult = NativeResult::kIgnoredOpaque;

struct ModuleImage { Address base{}; Address size{}; };

constexpr bool valid_module(ModuleImage module) noexcept {
    return module.base != 0 && (module.base & 0xF) == 0 && module.size != 0 &&
           module.size <= kAddressMax - module.base;
}

constexpr bool contains_rva(ModuleImage module, Address rva, Address size) noexcept {
    return valid_module(module) && size != 0 && rva < module.size &&
           size <= module.size - rva;
}

constexpr bool fits_aligned16(Address address, Address length) noexcept {
    return address != 0 && length != 0 && length <= 16 &&
           length <= kAddressMax - address && (address & 0xF) + length <= 16;
}

constexpr const NativeSite* site(SiteId id) noexcept {
    const auto index = static_cast<std::size_t>(id);
    return index < kSites.size() ? &kSites[index] : nullptr;
}

// Unsigned magnitudes avoid signed overflow, including +/-2 GiB boundaries.
struct Rel32 { bool valid{}; std::uint32_t bits{}; };
constexpr Rel32 encode_rel32(Address instruction_end, Address target) noexcept {
    if (instruction_end == 0 || target == 0) return {};
    if (target >= instruction_end) {
        const Address magnitude = target - instruction_end;
        if (magnitude > 0x7FFFFFFF) return {};
        return {true, static_cast<std::uint32_t>(magnitude)};
    }
    const Address magnitude = instruction_end - target;
    if (magnitude > 0x80000000) return {};
    return {true, std::uint32_t{0} - static_cast<std::uint32_t>(magnitude)};
}

enum class PlanStatus : std::uint8_t {
    kAccepted, kInvalidModule, kUnknownSite, kOutsideModule, kCrossesWord,
    kWrongWordAddress, kStockMismatch, kInvalidStockTarget, kTargetOutOfRange,
};
struct Rel32Plan {
    PlanStatus status{PlanStatus::kUnknownSite};
    Address word_address{};
    Word16 expected{};
    Word16 replacement{};
    constexpr bool accepted() const noexcept { return status == PlanStatus::kAccepted; }
};

// observed_word is a separately supplied copy. Only the four displacement bytes
// change. Expected/replacement are usable only when accepted(); failures return
// zeroed plans. This does not establish that the observed word is still current.
constexpr Rel32Plan plan_rel32_site(ModuleImage module, SiteId id,
                                   Address observed_word_address,
                                   const Word16& observed_word,
                                   Address new_target) noexcept {
    if (!valid_module(module)) return {PlanStatus::kInvalidModule};
    const NativeSite* native = site(id);
    if (native == nullptr) return {PlanStatus::kUnknownSite};
    if (!contains_rva(module, native->rva, native->length) ||
        !contains_rva(module, native->instruction_end_rva, 1) ||
        !contains_rva(module, native->target_rva, 1))
        return {PlanStatus::kOutsideModule};
    const Address address = module.base + native->rva;
    if (!fits_aligned16(address, native->length)) return {PlanStatus::kCrossesWord};
    const Address word_address = address & ~Address{0xF};
    if (observed_word_address != word_address) return {PlanStatus::kWrongWordAddress};
    if (!contains_rva(module, word_address - module.base, 16))
        return {PlanStatus::kOutsideModule};
    const auto offset = static_cast<std::size_t>(address - word_address);
    for (std::size_t i = 0; i < native->length; ++i)
        if (observed_word[offset + i] != native->stock[i])
            return {PlanStatus::kStockMismatch};
    const Address instruction_end = module.base + native->instruction_end_rva;
    const Rel32 old_displacement = encode_rel32(instruction_end, module.base + native->target_rva);
    if (!old_displacement.valid) return {PlanStatus::kInvalidStockTarget};
    for (std::size_t i = 0; i < 4; ++i)
        if (native->stock[native->displacement_offset + i] !=
            static_cast<std::uint8_t>((old_displacement.bits >> (8 * i)) & 0xFF))
            return {PlanStatus::kInvalidStockTarget};
    const Rel32 replacement = encode_rel32(instruction_end, new_target);
    if (!replacement.valid) return {PlanStatus::kTargetOutOfRange};
    Rel32Plan result{PlanStatus::kAccepted, word_address, observed_word, observed_word};
    for (std::size_t i = 0; i < 4; ++i)
        result.replacement[offset + native->displacement_offset + i] =
            static_cast<std::uint8_t>((replacement.bits >> (8 * i)) & 0xFF);
    return result;
}

constexpr bool matches_return(ModuleImage module, Address observed,
                              std::uint32_t expected_rva) noexcept {
    return expected_rva != 0 && contains_rva(module, expected_rva, 1) &&
           observed == module.base + expected_rva;
}

constexpr bool aligned_stack_slot(Address slot) noexcept {
    // Windows x64 CALL entry: RSP is 8 modulo 16, not merely 8-byte aligned.
    return slot != 0 && (slot & 0xF) == 8 && slot <= kAddressMax - 8;
}

struct ProcessFrameWitness {
    Address segment{};
    // Independent original native Process entry slot from the original-call
    // bridge, NOT a C++ wrapper frame and NOT computed from segment+1628.
    Address original_process_return_slot{};
    // Separate wrapper-entry return PC supplied by its native caller. The value
    // IN original_process_return_slot is a bridge continuation, NOT E7D/2FD;
    // this helper does not read that value or claim to validate bridge code.
    Address process_wrapper_entry_return_address{};
    Address postpose_return_address{};
    SiteId process_site{SiteId::kCount};
};

constexpr bool validate_process_frame(ModuleImage module,
                                      const ProcessFrameWitness& witness) noexcept {
    if (witness.process_site != SiteId::kGroup0Process &&
        witness.process_site != SiteId::kLaterProcess) return false;
    const NativeSite* native = site(witness.process_site);
    return witness.segment != 0 && (witness.segment & 0xF) == 0 &&
           aligned_stack_slot(witness.original_process_return_slot) &&
           witness.segment <= kAddressMax - kSegmentToOriginalProcessReturnSlot &&
           witness.segment + kSegmentToOriginalProcessReturnSlot ==
               witness.original_process_return_slot &&
           matches_return(module, witness.process_wrapper_entry_return_address, native->return_rva) &&
           matches_return(module, witness.postpose_return_address, kPostposeReturnRva);
}

constexpr bool validate_group0_process_frame(ModuleImage module,
                                             const ProcessFrameWitness& witness) noexcept {
    return witness.process_site == SiteId::kGroup0Process &&
           validate_process_frame(module, witness);
}

struct BodyStoreFrameWitness {
    Address store_entry_return_slot{};
    Address store_return_address{};
    Address ancestor_return_slot{}; // Independently supplied address and value.
    Address ancestor_return_address{};
};

constexpr bool validate_body_store_frame(ModuleImage module,
                                         const BodyStoreFrameWitness& witness) noexcept {
    return aligned_stack_slot(witness.store_entry_return_slot) &&
           aligned_stack_slot(witness.ancestor_return_slot) &&
           witness.store_entry_return_slot <= kAddressMax - kBodyStoreToRefreshReturnSlot &&
           witness.store_entry_return_slot + kBodyStoreToRefreshReturnSlot ==
               witness.ancestor_return_slot &&
           matches_return(module, witness.store_return_address, 0x1CF0F3B) &&
           matches_return(module, witness.ancestor_return_address, kBodyRefreshReturnRva);
}

// The postpose pointer is registry base+8, NOT +10. Native init can replace it.
// Jobs copy it into job+20. Already queued old Game callbacks can bypass an
// observer: a missing witness must reject. Restoring this global is NOT a drain:
// queued jobs may still retain the observer pointer. Its module/code must remain
// alive until a separately proven drain/lifetime fence; do not auto-reinstall.
constexpr bool validate_postpose_slot(ModuleImage module, Address supplied_slot,
                                      Address supplied_value) noexcept {
    return contains_rva(module, kPostposeSlotRva, 8) &&
           contains_rva(module, kGamePostposeRva, 1) &&
           supplied_slot == module.base + kPostposeSlotRva &&
           (supplied_slot & 7) == 0 && supplied_value == module.base + kGamePostposeRva;
}

} // namespace coop::enemy_jointblend_native_contract
