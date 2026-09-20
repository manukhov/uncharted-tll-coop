#include "nadine_p99_observation_arm.h"
#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
namespace fs = std::filesystem;
namespace arm = coop::p99_observation_arm;
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
enum class Result : int { kSent, kArguments = 2, kProcess, kCreation, kImage, kEvent, kSignal };

bool decimal(std::wstring_view text, std::uint64_t maximum, std::uint64_t& value) noexcept {
    value = 0;
    if (text.empty()) return false;
    for (const wchar_t character : text) {
        if (character < L'0' || character > L'9') return false;
        const auto digit = static_cast<std::uint64_t>(character - L'0');
        if (digit > maximum || value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return value != 0;
}

Result signal_existing(DWORD pid, std::uint64_t creation, const fs::path& expected) {
    if (pid == 0 || creation == 0 || !expected.is_absolute()) return Result::kArguments;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
    if (!process || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) return Result::kProcess;
    std::uint64_t actual_creation = 0;
    if (!arm::creation_stamp(process.value, actual_creation) || actual_creation != creation)
        return Result::kCreation;
    std::array<wchar_t, 32768> image{};
    DWORD length = static_cast<DWORD>(image.size());
    if (!QueryFullProcessImageNameW(process.value, 0, image.data(), &length)) return Result::kImage;
    std::error_code error;
    if (!fs::equivalent(fs::path(std::wstring_view(image.data(), length)), expected, error) || error)
        return Result::kImage;
    // Never create the event: its absence means no installed waiting observer.
    const auto name = arm::event_name(pid, creation);
    Handle event(OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str()));
    if (!event) return Result::kEvent;
    if (WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) return Result::kProcess;
    return SetEvent(event.value) ? Result::kSent : Result::kSignal;
}

int self_test() {
    int checks = 0, failures = 0;
    const auto check = [&](bool passed, const char* label) {
        ++checks;
        if (!passed) { ++failures; std::cerr << "FAIL " << label << '\n'; }
    };
    std::uint64_t value = 0;
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    check(decimal(L"18446744073709551615", maximum, value) && value == maximum, "decimal UINT64_MAX");
    check(!decimal(L"18446744073709551616", maximum, value), "decimal overflow rejected");
    check(!decimal(L"4294967296", MAXDWORD, value), "PID overflow rejected");
    for (const auto invalid : {L"", L"0", L"-1", L"+1", L" 1", L"1 ", L"1x"})
        check(!decimal(invalid, maximum, value), "strict positive decimal");
    check(arm::event_name(12, 345) == L"Local\\TLL-P99-Source-Arm-v1-12-345", "exact event name");
    check(arm::event_name(12, 345) != arm::event_name(13, 345), "PID names separated");
    check(arm::event_name(12, 345) != arm::event_name(12, 346), "creation names separated");
    check(arm::event_name(1, 23) != arm::event_name(12, 3), "decimal components separated");

    const DWORD pid = GetCurrentProcessId();
    std::uint64_t creation = 0;
    std::array<wchar_t, 32768> image{};
    DWORD length = static_cast<DWORD>(image.size());
    if (!arm::creation_stamp(GetCurrentProcess(), creation) || creation == 0 ||
        !QueryFullProcessImageNameW(GetCurrentProcess(), 0, image.data(), &length)) return 1;
    const fs::path executable(std::wstring_view(image.data(), length));
    const auto name = arm::event_name(pid, creation);
    {
        Handle absent(OpenEventW(SYNCHRONIZE, FALSE, name.c_str()));
        if (absent) { std::cerr << "Self-test event unexpectedly exists; refusing to alter it\n"; return 1; }
    }
    check(signal_existing(pid, creation, executable) == Result::kEvent, "missing event refused");
    {
        Handle absent(OpenEventW(SYNCHRONIZE, FALSE, name.c_str()));
        check(!absent, "signal path did not create missing event");
    }
    // Event creation is confined to this private test and this process identity.
    Handle event(CreateEventW(nullptr, FALSE, FALSE, name.c_str()));
    if (!event || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
    const auto unchanged = [&] {
        check(WaitForSingleObject(event.value, 0) == WAIT_TIMEOUT, "refusal did not signal owned event");
    };
    check(signal_existing(0, creation, executable) == Result::kArguments, "zero PID refused");
    unchanged();
    check(signal_existing(MAXDWORD, creation, executable) == Result::kProcess, "invalid PID refused");
    unchanged();
    check(signal_existing(pid, creation + 1, executable) == Result::kCreation, "wrong creation refused");
    unchanged();
    check(signal_existing(pid, 0, executable) == Result::kArguments, "zero creation refused");
    unchanged();
    check(signal_existing(pid, creation, L"relative.exe") == Result::kArguments, "relative image refused");
    unchanged();
    check(signal_existing(pid, creation, executable.parent_path()) == Result::kImage, "different existing image refused");
    unchanged();
    check(signal_existing(pid, creation, executable) == Result::kSent, "own event signalled");
    check(WaitForSingleObject(event.value, 0) == WAIT_OBJECT_0, "own signal observed");
    check(WaitForSingleObject(event.value, 0) == WAIT_TIMEOUT, "one auto-reset signal only");
    check(WaitForSingleObject(GetCurrentProcess(), 0) == WAIT_TIMEOUT, "own process remains live");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--self-test") return self_test();
    std::uint64_t pid = 0, creation = 0;
    if (argc != 7 || std::wstring_view(argv[1]) != L"--pid" ||
        std::wstring_view(argv[3]) != L"--creation" || std::wstring_view(argv[5]) != L"--expected-image" ||
        !decimal(argv[2], MAXDWORD, pid) ||
        !decimal(argv[4], (std::numeric_limits<std::uint64_t>::max)(), creation)) {
        std::wcerr << L"Usage: tll_p99_observation_arm --pid N --creation FILETIME --expected-image ABSOLUTE_EXE\n";
        return static_cast<int>(Result::kArguments);
    }
    const auto result = signal_existing(static_cast<DWORD>(pid), creation, fs::path(argv[6]));
    if (result == Result::kSent)
        std::cout << "Signal sent to existing event; not an ARMED or admission confirmation.\n";
    else
        std::cerr << "Signal refused/failed; code=" << static_cast<int>(result) << '\n';
    return static_cast<int>(result);
}
