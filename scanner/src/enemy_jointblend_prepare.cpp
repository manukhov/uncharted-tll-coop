#include "enemy_jointblend_prepare.h"
#include "enemy_jointblend_prepare_windows.h"
#include "enemy_jointblend_collector.h"
#include "enemy_jointblend_read_windows.h"
#include <Windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cstring>
#include <new>

namespace prep = coop::enemy_jointblend_prepare;
namespace hooks = coop::enemy_jointblend_hooks;
namespace native = coop::enemy_jointblend_native_contract;
namespace collect = coop::enemy_jointblend_collector;
namespace read = coop::enemy_jointblend_read;
namespace prepared = coop::enemy_jointblend_prepare_windows;
namespace offer = coop::enemy_jointblend_offer;

#pragma section(".enemy", read, write)
#pragma section(".eoffer", read, write)
extern "C" {
__declspec(dllexport) __declspec(allocate(".enemy")) prep::Page TllEnemyBindingV1{};
__declspec(dllexport) __declspec(allocate(".eoffer")) offer::Page TllEnemyOfferV1{};
__declspec(dllexport) std::uint32_t __cdecl TllEnemyPrepareV1(const prep::Request*, std::uint32_t) noexcept;
}
namespace {
constinit std::atomic<bool> attempted{false};
struct PreserveError { DWORD value = GetLastError(); ~PreserveError() { SetLastError(value); } };

bool pages(std::uintptr_t address, std::size_t bytes, std::uintptr_t image = 0, bool code = false) noexcept {
    if (!address || !bytes || bytes > UINTPTR_MAX - address) return false;
    const auto end = address + bytes;
    while (address < end) {
        MEMORY_BASIC_INFORMATION m{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &m, sizeof(m)) != sizeof(m) ||
            m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            (image && (m.Type != MEM_IMAGE || reinterpret_cast<std::uintptr_t>(m.AllocationBase) != image))) return false;
        const auto p = m.Protect & 0xFF;
        if (code ? p != PAGE_EXECUTE_READ :
            (p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY &&
             p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY)) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(m.BaseAddress);
        if (m.RegionSize > UINTPTR_MAX - base || base + m.RegionSize <= address) return false;
        address = base + m.RegionSize;
    }
    return true;
}
bool copy(std::uintptr_t address, void* out, std::size_t bytes, std::uintptr_t image = 0, bool code = false) noexcept {
    if (!pages(address, bytes, image, code)) return false;
    SIZE_T done{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), out, bytes, &done) && done == bytes;
}
bool image_header(std::uintptr_t base, IMAGE_NT_HEADERS64& nt) noexcept {
    IMAGE_DOS_HEADER dos{};
    if ((base & 0xFFFF) || !copy(base, &dos, sizeof(dos), base) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < sizeof(dos) || dos.e_lfanew > 0x100000 ||
        base > UINTPTR_MAX - static_cast<std::uint32_t>(dos.e_lfanew)) return false;
    if (!copy(base + dos.e_lfanew, &nt, sizeof(nt), base)) return false;
    return nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER64) &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.OptionalHeader.SectionAlignment == 4096 &&
        nt.OptionalHeader.SizeOfHeaders >= static_cast<std::uint32_t>(dos.e_lfanew) + sizeof(nt) &&
        nt.OptionalHeader.SizeOfHeaders < nt.OptionalHeader.SizeOfImage &&
        native::valid_module({base, nt.OptionalHeader.SizeOfImage});
}
std::uint64_t creation_time() noexcept {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    return (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
}
// This checks the on-disk retail bytes only. Loaded COW/relocation/dependency
// integrity still requires the future independent external consumer certificate.
bool retail_file(HMODULE module) noexcept {
    wchar_t path[32768]{};
    const auto n = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    if (!n || n >= std::size(path)) return false;
    const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    bool ok = GetFileSizeEx(file, &length) && length.QuadPart == prep::kRetailFileSize &&
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    unsigned char buffer[65536];
    std::uint64_t total{};
    while (ok && total < prep::kRetailFileSize) {
        DWORD got{};
        ok = ReadFile(file, buffer, sizeof(buffer), &got, nullptr) && got != 0 &&
            got <= prep::kRetailFileSize - total && BCryptHashData(hash, buffer, got, 0) >= 0;
        if (ok) total += got;
    }
    std::array<unsigned char, 32> digest{};
    ok = ok && total == prep::kRetailFileSize && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (ok) {
        constexpr char hex[] = "0123456789abcdef";
        for (std::size_t i = 0; i < digest.size(); ++i)
            if (hex[digest[i] >> 4] != prep::kRetailSha256[2*i] || hex[digest[i] & 15] != prep::kRetailSha256[2*i+1]) ok = false;
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return ok;
}
std::uint64_t tick(void*) noexcept {
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) && value.QuadPart > 0 ? static_cast<std::uint64_t>(value.QuadPart) : 0;
}
struct Resources {
    read::Reader reader{};
    collect::Collector<read::Reader> collector;
    prepared::Relays relays{};
    prepared::Publisher publisher{};
    prepared::OfferPublisher offer_publisher{};
    Resources(native::ModuleImage image, std::uint64_t frequency) noexcept : collector(reader, image, {nullptr, tick, frequency}) {}
};
// No destructor, free, unload, worker or arm path. All partial allocations and
// the self PIN are deliberately retained until process exit, including refusals.
prep::Result prepare(const prep::Request* input, std::uint32_t bytes) noexcept {
    using R = prep::Result;
    // Gate before reading caller memory: refusals are terminal, not retries.
    if (attempted.exchange(true, std::memory_order_acq_rel)) return R::reused;
    prep::Request request{};
    if (bytes != sizeof(request) || !copy(reinterpret_cast<std::uintptr_t>(input), &request, sizeof(request)) ||
        !prep::valid_request(request)) return R::input_refused;
    HMODULE self{};
    const auto entry = reinterpret_cast<LPCWSTR>(&TllEnemyPrepareV1);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, entry, &self) ||
        request.pid != GetCurrentProcessId() || request.creation != creation_time() ||
        request.main != reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) ||
        request.self != reinterpret_cast<std::uintptr_t>(self)) return R::identity_refused;
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, entry, &pinned) ||
        pinned != self) return R::pin_refused;
    IMAGE_NT_HEADERS64 main_header{}, self_header{};
    if (!image_header(request.main, main_header) || !image_header(request.self, self_header) ||
        main_header.FileHeader.TimeDateStamp != prep::kRetailStamp ||
        main_header.OptionalHeader.SizeOfImage != prep::kRetailImageSize) return R::image_refused;
    if (!retail_file(reinterpret_cast<HMODULE>(request.main))) return R::retail_file_refused;
    const native::ModuleImage image{request.main, main_header.OptionalHeader.SizeOfImage};
    auto code = [&](std::uint32_t rva, void* out, std::size_t n) noexcept {
        return native::contains_rva(image, rva, n) && copy(image.base+rva, out, n, image.base, true);
    };
    std::uintptr_t current_postpose{};
    if (!prep::code_contracts(code) || !native::contains_rva(image, native::kPostposeSlotRva, sizeof(current_postpose)) ||
        !copy(image.base+native::kPostposeSlotRva, &current_postpose, sizeof(current_postpose), image.base) ||
        current_postpose != image.base+native::kGamePostposeRva) return R::contracts_refused;
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart < 1000 || !tick(nullptr)) return R::resources_refused;
    void* allocation = VirtualAlloc(nullptr, sizeof(Resources), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!allocation) return R::resources_refused;
    auto* resources = ::new(allocation) Resources(image, static_cast<std::uint64_t>(frequency.QuadPart));
    prep::Binding binding{};
    binding.magic = prep::kMagic; binding.size = sizeof(binding); binding.version = prep::kVersion;
    binding.pid = request.pid; binding.creation = request.creation;
    binding.main = image.base; binding.main_size = image.size;
    binding.self = request.self; binding.self_size = self_header.OptionalHeader.SizeOfImage;
    binding.frequency = static_cast<std::uint64_t>(frequency.QuadPart);
    for (std::size_t i = 0; i < binding.originals.size(); ++i) binding.originals[i] = image.base+prep::kFallbacks[i].rva;
    const auto& f = binding.originals;
    const hooks::NativeCalls calls{reinterpret_cast<hooks::JointBlend>(f[0]), reinterpret_cast<hooks::Reset>(f[1]),
        reinterpret_cast<hooks::Process>(f[2]), reinterpret_cast<hooks::Postpose>(f[3]),
        reinterpret_cast<hooks::Primary>(f[4]), reinterpret_cast<hooks::Store>(f[5])};
    if (!hooks::configure(calls, resources->collector.sink())) return R::configuration_refused;
    const auto targets = hooks::targets();
    for (auto address : targets.code) if (!pages(address, 1, request.self, true)) return R::configuration_refused;
    // Fresh, never armed is NOT stopped/quiescent. Calling stop here would
    // permanently consume the future arm. No hook/worker can reach this fresh
    // collector yet; only the configured sink retains it.
    if (!pages(targets.postpose, 1, request.self, true) || resources->collector.requested()) return R::configuration_refused;
    binding.hooks = targets.code; binding.postpose = targets.postpose;
    if (!resources->relays.prepare(image, {binding.self, binding.self_size}, binding.hooks)) return R::resources_refused;
    std::array<std::uint64_t, 2> nonce{};
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(nonce.data()), sizeof(nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
        !(nonce[0] | nonce[1])) return R::resources_refused;
    const auto candidate = offer::from_binding(binding, resources->relays.addresses(), nonce,
        reinterpret_cast<std::uintptr_t>(&TllEnemyPrepareV1), reinterpret_cast<std::uintptr_t>(&TllEnemyOfferV1),
        reinterpret_cast<std::uintptr_t>(&TllEnemyBindingV1));
    if (!offer::valid(candidate) || !resources->relays.verify()) return R::resources_refused;
    // Separate formats, never a larger struct masquerading as BindingV1. On
    // partial publication retain both pages/resources; do not erase or retry.
    if (resources->publisher.publish(&TllEnemyBindingV1, request.self, binding) != prepared::PublishResult::published)
        return R::publication_refused;
    return resources->offer_publisher.publish(&TllEnemyOfferV1, candidate) == prepared::PublishResult::published ?
        R::dormant_bound : R::publication_refused;
}
}
// Explicit caller thread ONLY, outside loader lock, callbacks and debugger hold.
// No custom DllMain, constructor, CreateThread, patch installer or arm call.
extern "C" __declspec(dllexport) std::uint32_t __cdecl TllEnemyPrepareV1(
    const prep::Request* request, std::uint32_t bytes) noexcept {
    PreserveError error;
    return static_cast<std::uint32_t>(prepare(request, bytes));
}
