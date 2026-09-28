#include "nadine_p99_wrist_remote_read.h"
#include <iostream>
#include <vector>

#if !defined(TLL_P99_WRIST_REMOTE_READ_AUDIT)
#error Compile this test and nadine_p99_wrist_remote_read.cpp with the private audit seam.
#endif
namespace wc = coop::p99_wrist_code;
namespace wi = coop::p99_wrist_install;
#pragma section(".p99rr", read, execute)
// Dedicated nonexecuted constant bytes in THIS test's image. Never game code.
__declspec(allocate(".p99rr")) __declspec(align(4096)) const std::uint8_t image_bytes[8192] = {0x73, 0x19};
namespace {
unsigned checks = 0, failures = 0;
void check(bool okay, const char* label) {
    ++checks;
    if (!okay) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
struct Fake {
    static constexpr std::uintptr_t module = 0x180000000;
    HANDLE handle = reinterpret_cast<HANDLE>(0x1234);
    std::uintptr_t origin = module + 0x1000;
    std::array<std::uint8_t, wc::RemoteImageReader::kMaxBytes> bytes{};
    std::array<MEMORY_BASIC_INFORMATION, 70> regions{};
    std::size_t region_count = 1, queries = 0, reads = 0, requested_total = 0, largest_read = 0;
    std::size_t fail_query = 0, short_query = 0, fail_read = 0;
    unsigned read_fault = 0, mutation = 0, metadata_fault = 0;
    bool invalid_request = false;
    Fake() {
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i * 37 + 11);
        region(0, origin, bytes.size());
    }
    void region(std::size_t i, std::uintptr_t base, std::size_t size, DWORD protection = PAGE_EXECUTE_READ) {
        regions[i] = {};
        regions[i].BaseAddress = reinterpret_cast<void*>(base);
        regions[i].AllocationBase = reinterpret_cast<void*>(module);
        regions[i].RegionSize = size;
        regions[i].State = MEM_COMMIT;
        regions[i].Type = MEM_IMAGE;
        regions[i].Protect = protection;
    }
    void reset_counts() { queries = reads = requested_total = largest_read = 0; invalid_request = false; }
};
Fake* current = nullptr;
SIZE_T query(HANDLE process, LPCVOID pointer, PMEMORY_BASIC_INFORMATION out, SIZE_T size) noexcept {
    auto& f = *current;
    ++f.queries;
    if (process != f.handle || size != sizeof(*out) || f.queries == f.fail_query) return 0;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (std::size_t i = 0; i < f.region_count; ++i) {
        const auto start = reinterpret_cast<std::uintptr_t>(f.regions[i].BaseAddress);
        const auto last = coop::p99_group_ingress::add(start, f.regions[i].RegionSize);
        if (last && start <= address && address < *last) {
            *out = f.regions[i];
            if (f.metadata_fault == 1) out->BaseAddress = reinterpret_cast<void*>(address + 1);
            if (f.metadata_fault == 2) { out->BaseAddress = const_cast<void*>(pointer); out->RegionSize = 0; }
            if (f.metadata_fault == 3) out->RegionSize = UINTPTR_MAX;
            return f.queries == f.short_query ? sizeof(*out) - 1 : sizeof(*out);
        }
    }
    return 0;
}
BOOL read(HANDLE process, LPCVOID pointer, LPVOID output, SIZE_T size, SIZE_T* actual) noexcept {
    auto& f = *current;
    ++f.reads;
    f.largest_read = std::max(f.largest_read, size);
    f.requested_total += size;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    if (process != f.handle || address < f.origin || size > f.bytes.size() ||
        address - f.origin > f.bytes.size() - size) { f.invalid_request = true; return FALSE; }
    auto count = size;
    BOOL success = TRUE;
    if (f.reads == f.fail_read) {
        if (f.read_fault == 1) { count = size / 2; success = FALSE; }
        if (f.read_fault == 2) count = size - 1; // TRUE with a short count
        if (f.read_fault == 3) success = FALSE; // FALSE even after a full copy
    }
    std::copy_n(f.bytes.begin() + (address - f.origin), count, static_cast<std::uint8_t*>(output));
    *actual = f.reads == f.fail_read && f.read_fault == 4 ? size + 1 : count;
    return success;
}
void mutate(std::uintptr_t, std::size_t) noexcept {
    auto& f = *current;
    if (f.mutation == 1) { f.fail_read = f.reads + 1; f.read_fault = 1; }
    if (f.mutation == 2) f.regions[0].Protect = PAGE_READONLY;
    if (f.mutation == 3) f.regions[0].AllocationBase = reinterpret_cast<void*>(Fake::module + 0x10000);
    if (f.mutation == 4) f.bytes[0] ^= 1;
}
const wc::RemoteReadAudit fake_ops{query, read, mutate};
struct AuditScope {
    explicit AuditScope(Fake& f) { current = &f; wc::g_remote_read_audit = &fake_ops; }
    ~AuditScope() { wc::g_remote_read_audit = nullptr; current = nullptr; }
};

void fake_tests() {
    Fake f;
    AuditScope scope(f);
    wc::RemoteImageReader reader(f.handle, Fake::module);
    std::array<std::uint8_t, 1041> owned{};
    check(reader.copy(f.origin, owned) && std::equal(owned.begin(), owned.end(), f.bytes.begin()), "exact fake copy");
    f.reset_counts();
    check(reader.equal(f.origin, owned) && f.reads == 3 && f.largest_read == 512 &&
        f.requested_total == owned.size() && !f.invalid_request, "fixed scratch exact chunk and final tail");
    for (const std::size_t at : {std::size_t{0}, std::size_t{511}, std::size_t{512}, owned.size() - 1}) {
        owned[at] ^= 1;
        check(!reader.equal(f.origin, owned), "mismatch at chunk boundary or tail refuses");
        owned[at] ^= 1;
    }
    for (unsigned mode = 1; mode <= 4; ++mode) {
        f.reset_counts(); f.fail_read = 1; f.read_fault = mode;
        check(!reader.copy(f.origin, owned), "copy refuses partial false, short true, full false, overcount true");
        for (std::size_t chunk = 1; chunk <= 3; ++chunk) {
            f.reset_counts(); f.fail_read = chunk;
            check(!reader.equal(f.origin, std::span(f.bytes).first(owned.size())) && f.reads == chunk,
                "equal rejects each exact-count failure without later read");
        }
    }
    f.fail_read = 0;
    for (const DWORD protection : {DWORD(PAGE_READONLY), DWORD(PAGE_READWRITE), DWORD(PAGE_NOACCESS),
        DWORD(PAGE_EXECUTE), DWORD(PAGE_EXECUTE_READWRITE), DWORD(PAGE_EXECUTE_WRITECOPY),
        DWORD(PAGE_EXECUTE_READ | PAGE_GUARD), DWORD(PAGE_EXECUTE_READ | PAGE_NOCACHE)}) {
        f.regions[0].Protect = protection; f.reset_counts();
        check(!reader.copy(f.origin, owned) && !reader.equal(f.origin, owned) && f.reads == 0,
            "unapproved protection rejects before RPM including guard");
    }
    f.region(0, f.origin, f.bytes.size());
    for (unsigned mode = 0; mode < 4; ++mode) {
        if (mode == 0) f.regions[0].State = MEM_RESERVE;
        if (mode == 1) f.regions[0].Type = MEM_PRIVATE;
        if (mode == 2) f.regions[0].Type = MEM_MAPPED;
        if (mode == 3) f.regions[0].AllocationBase = reinterpret_cast<void*>(Fake::module + 4096);
        f.reset_counts();
        check(!reader.copy(f.origin, owned) && f.reads == 0, "state type or image allocation refuses");
        f.region(0, f.origin, f.bytes.size());
    }
    f.reset_counts(); f.short_query = 1;
    check(!reader.copy(f.origin, owned) && !f.reads, "short VirtualQueryEx result refuses");
    f.short_query = 0;
    for (unsigned malformed = 1; malformed <= 3; ++malformed) {
        f.metadata_fault = malformed; f.reset_counts();
        check(!reader.copy(f.origin, owned) && !f.reads, "malformed metadata base nonprogress or overflow refuses");
    }
    f.metadata_fault = 0;
    for (std::size_t query_number : {std::size_t{1}, std::size_t{2}}) {
        f.reset_counts(); f.fail_query = query_number;
        check(!reader.copy(f.origin, owned), "metadata failure before or after exact read refuses");
    }
    f.fail_query = 0;
    for (unsigned change = 1; change <= 3; ++change) {
        f.reset_counts(); f.mutation = change; f.fail_read = 0;
        check(!reader.copy(f.origin, owned), "post-query read fault/protection/allocation change refuses copy");
        f.region(0, f.origin, f.bytes.size()); f.reset_counts(); f.fail_read = 0;
        check(!reader.equal(f.origin, std::span(f.bytes).first(owned.size())),
            "post-query fault/protection/allocation change refuses equal");
        f.region(0, f.origin, f.bytes.size());
    }
    f.mutation = 0; f.fail_read = 0;
    check(reader.copy(f.origin, owned), "restore accepted owned bytes");
    f.mutation = 4;
    check(!reader.equal(f.origin, owned), "fresh byte mutation after metadata is compared, not cached");
    f.mutation = 0; f.bytes[0] ^= 1;
    f.reset_counts();
    wc::RemoteImageReader null_handle(nullptr, Fake::module), null_module(f.handle, 0);
    check(!null_handle.copy(f.origin, owned) && !null_module.copy(f.origin, owned) && f.queries == 0,
        "null constructor contracts refuse without APIs");
    wc::RemoteImageReader invalid_handle(reinterpret_cast<HANDLE>(0x1235), Fake::module);
    check(!invalid_handle.copy(f.origin, owned) && !f.reads, "failed handle query cannot become readable");
    check(!reader.copy(Fake::module - 1, owned) && !reader.copy(UINTPTR_MAX - 3, owned) &&
        !reader.copy(f.origin, {}) && !reader.equal(f.origin, {}), "below-image overflow and empty refuse");
    std::vector<std::uint8_t> oversized(wc::RemoteImageReader::kMaxBytes + 1);
    check(!reader.copy(f.origin, oversized) && !reader.equal(f.origin, oversized), "bounded maximum extent");
    std::array<std::uint8_t, wc::RemoteImageReader::kMaxBytes> maximum{};
    check(reader.copy(f.origin, maximum), "maximum exact extent copy");
    f.reset_counts();
    check(reader.equal(f.origin, maximum) && f.reads == 128 && f.largest_read == 512,
        "maximum equal stays bounded fixed scratch");
    f.region_count = 2;
    f.region(0, f.origin, 4096); f.region(1, f.origin + 4096, 4096);
    check(reader.copy(f.origin + 4090, owned), "walk adjacent queried image regions");
    f.regions[1].AllocationBase = reinterpret_cast<void*>(Fake::module + 4096);
    f.reset_counts();
    check(!reader.copy(f.origin + 4090, owned) && !f.reads, "second allocation cannot be crossed");
    f.region_count = 65;
    for (std::size_t i = 0; i < 65; ++i) f.region(i, f.origin + i, 1);
    f.reset_counts();
    check(!reader.copy(f.origin, std::span(owned).first(65)) && f.queries == 64 && !f.reads,
        "bounded query loop refuses adversarial fragmentation");
}

void whitelist_tests() {
    Fake f;
    AuditScope scope(f);
    std::array<std::uintptr_t, wi::kSiteCount> pages{};
    for (std::size_t i = 0; i < pages.size(); ++i) pages[i] = (Fake::module + wi::kSites[i].rva) & ~std::uintptr_t{0xFFF};
    wc::RemoteImageReader all(f.handle, Fake::module, pages);
    std::array<std::uint8_t, 32> owned{};
    for (const auto page : pages) for (const DWORD protection : {DWORD(PAGE_EXECUTE_READWRITE), DWORD(PAGE_EXECUTE_WRITECOPY)}) {
        f.origin = page; f.region(0, page, 4096, protection);
        check(all.copy(page, owned) && all.equal(page, owned), "each of five explicit RXW/EXECOPY pages accepted");
        wc::RemoteImageReader none(f.handle, Fake::module);
        check(!none.copy(page, owned), "same writable page is denied without explicit whitelist");
    }
    auto duplicate = pages; duplicate[1] = duplicate[0];
    const std::array<std::uintptr_t, 1> arbitrary{Fake::module + 4096};
    std::array<std::uintptr_t, 6> too_many{};
    wc::RemoteImageReader bad1(f.handle, Fake::module, duplicate), bad2(f.handle, Fake::module, arbitrary);
    wc::RemoteImageReader bad3(f.handle, Fake::module, too_many);
    f.reset_counts();
    check(!bad1.copy(f.origin, owned) && !bad2.copy(f.origin, owned) && !bad3.copy(f.origin, owned) && !f.queries,
        "duplicate arbitrary or too-many patch pages refuse construction");
    f.origin = pages[0]; f.region(0, f.origin, 8192, PAGE_EXECUTE_READWRITE);
    wc::RemoteImageReader first_only(f.handle, Fake::module, std::span(pages).first(1));
    check(!first_only.copy(f.origin + 4090, owned), "writable region cannot leak into non-whitelisted adjacent page");
    f.region(0, f.origin, 4096, PAGE_EXECUTE_READWRITE | PAGE_GUARD);
    f.reset_counts();
    check(!all.copy(f.origin, owned) && !f.reads, "whitelist never strips protection modifiers");
}

void real_current_process_tests() {
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto address = reinterpret_cast<std::uintptr_t>(image_bytes);
    wc::RemoteImageReader reader(GetCurrentProcess(), module);
    std::array<std::uint8_t, 1041> owned{};
    check(reader.copy(address, owned) && owned[0] == 0x73 && owned[1] == 0x19 && reader.equal(address, owned),
        "real VQEx/RPM reads own dedicated MEM_IMAGE RX section via pseudo handle");
    owned.back() ^= 1;
    check(!reader.equal(address, owned), "real exact fresh remote comparison refuses mismatch");
    std::array<std::uint8_t, 32> crossing{};
    check(reader.copy(address + 4090, crossing) && reader.equal(address + 4090, crossing), "real exact page crossing");
    wc::RemoteImageReader wrong(GetCurrentProcess(), module + 4096);
    check(!wrong.copy(address, owned), "real wrong AllocationBase refuses");
    check(!reader.copy(reinterpret_cast<std::uintptr_t>(owned.data()), crossing), "real private stack source refuses");
    HANDLE limited = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    check(limited != nullptr, "open own process without read permission");
    if (limited) {
        wc::RemoteImageReader no_read(limited, module);
        check(!no_read.copy(address, owned) && !no_read.equal(address, owned), "missing VM_READ refuses exact RPM");
        CloseHandle(limited);
    }
    HANDLE full = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, GetCurrentProcessId());
    check(full != nullptr, "open own real read-only process handle");
    if (full) {
        { wc::RemoteImageReader borrowed(full, module); check(borrowed.copy(address, owned), "real borrowed process handle"); }
        DWORD flags = 0;
        check(GetHandleInformation(full, &flags) != FALSE, "reader never closes borrowed handle");
        CloseHandle(full);
    }
    wc::Snapshot empty;
    check(!empty.matches(coop::p99_wrist_code_manifest::Stage::Stock, reader), "Snapshot accepts transport API and rejects unprepared evidence");
}
} // namespace
int main() {
    fake_tests(); whitelist_tests(); real_current_process_tests();
    std::cout << checks << " checks, " << failures << " failures; READ_ONLY_OWN_IMAGE_AND_SYNTHETIC_FAULTS\n";
    return failures ? 1 : 0;
}
