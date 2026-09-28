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
enum class Protocol { kSource, kWrist };
#if defined(TLL_P99_WRIST_ARM) && TLL_P99_WRIST_ARM
constexpr auto kProtocol = Protocol::kWrist;
constexpr auto kProgramName = L"tll_p99_wrist_arm";
#else
constexpr auto kProtocol = Protocol::kSource;
constexpr auto kProgramName = L"tll_p99_observation_arm";
#endif

std::wstring event_name(Protocol protocol, DWORD pid, std::uint64_t creation) {
    return protocol == Protocol::kWrist ? arm::wrist_event_name(pid, creation) :
        arm::event_name(pid, creation);
}

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

// The explicit protocol is private to this TU so self-test can exercise both
// namespaces. The command-line path always uses its compile-time protocol.
Result signal_existing_for(Protocol protocol, DWORD pid, std::uint64_t creation, const fs::path& expected) {
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
    const auto name = event_name(protocol, pid, creation);
    Handle event(OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str()));
    if (!event) return Result::kEvent;
    if (WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) return Result::kProcess;
    return SetEvent(event.value) ? Result::kSent : Result::kSignal;
}

Result signal_existing(DWORD pid, std::uint64_t creation, const fs::path& expected) {
    return signal_existing_for(kProtocol, pid, creation, expected);
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
    check(arm::wrist_event_name(12, 345) == L"Local\\TLL-P99-Wrist-Arm-v1-12-345", "exact wrist event name");
    check(arm::event_name(12, 345) != arm::wrist_event_name(12, 345), "source/wrist protocols separated");
    check(arm::event_name(12, 345) != arm::event_name(13, 345), "PID names separated");
    check(arm::event_name(12, 345) != arm::event_name(12, 346), "creation names separated");
    check(arm::event_name(1, 23) != arm::event_name(12, 3), "decimal components separated");
    check(arm::wrist_event_name(12, 345) != arm::wrist_event_name(13, 345), "wrist PID names separated");
    check(arm::wrist_event_name(12, 345) != arm::wrist_event_name(12, 346), "wrist creation names separated");
    check(arm::wrist_event_name(1, 23) != arm::wrist_event_name(12, 3), "wrist decimal components separated");

    const DWORD pid = GetCurrentProcessId();
    std::uint64_t creation = 0;
    std::array<wchar_t, 32768> image{};
    DWORD length = static_cast<DWORD>(image.size());
    if (!arm::creation_stamp(GetCurrentProcess(), creation) || creation == 0 ||
        !QueryFullProcessImageNameW(GetCurrentProcess(), 0, image.data(), &length)) return 1;
    const fs::path executable(std::wstring_view(image.data(), length));
    const auto source_name = arm::event_name(pid, creation);
    const auto wrist_name = arm::wrist_event_name(pid, creation);
    for (const auto protocol : {Protocol::kSource, Protocol::kWrist}) {
        const auto name = event_name(protocol, pid, creation);
        {
            Handle absent(OpenEventW(SYNCHRONIZE, FALSE, name.c_str()));
            if (absent) { std::cerr << "Self-test event unexpectedly exists; refusing to alter it\n"; return 1; }
        }
        check(signal_existing_for(protocol, pid, creation, executable) == Result::kEvent, "missing protocol event refused");
        Handle absent(OpenEventW(SYNCHRONIZE, FALSE, name.c_str()));
        check(!absent, "signal path did not create missing protocol event");
    }
    check(signal_existing(pid, creation, executable) == Result::kEvent, "selected CLI protocol also refuses missing event");
    // Event creation is confined to this private test and this process identity.
    Handle source(CreateEventW(nullptr, FALSE, FALSE, source_name.c_str()));
    if (!source || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
    check(signal_existing_for(Protocol::kWrist, pid, creation, executable) == Result::kEvent,
        "source event does not substitute for absent wrist event");
    check(WaitForSingleObject(source.value, 0) == WAIT_TIMEOUT, "missing wrist signal leaves source untouched");
    Handle wrist(CreateEventW(nullptr, FALSE, FALSE, wrist_name.c_str()));
    if (!wrist || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
    const auto unchanged = [&] {
        check(WaitForSingleObject(source.value, 0) == WAIT_TIMEOUT, "refusal did not signal source event");
        check(WaitForSingleObject(wrist.value, 0) == WAIT_TIMEOUT, "refusal did not signal wrist event");
    };
    for (const auto protocol : {Protocol::kSource, Protocol::kWrist}) {
        check(signal_existing_for(protocol, 0, creation, executable) == Result::kArguments, "zero PID refused");
        unchanged();
        check(signal_existing_for(protocol, MAXDWORD, creation, executable) == Result::kProcess, "invalid PID refused");
        unchanged();
        check(signal_existing_for(protocol, pid, creation + 1, executable) == Result::kCreation, "stale creation refused");
        unchanged();
        check(signal_existing_for(protocol, pid, 0, executable) == Result::kArguments, "zero creation refused");
        unchanged();
        check(signal_existing_for(protocol, pid, creation, L"relative.exe") == Result::kArguments, "relative image refused");
        unchanged();
        check(signal_existing_for(protocol, pid, creation, executable.parent_path()) == Result::kImage, "different existing image refused");
        unchanged();
        check(signal_existing_for(protocol, pid, creation, executable) == Result::kSent, "only existing selected protocol event signalled");
        const auto selected = protocol == Protocol::kWrist ? wrist.value : source.value;
        const auto other = protocol == Protocol::kWrist ? source.value : wrist.value;
        check(WaitForSingleObject(selected, 0) == WAIT_OBJECT_0, "selected protocol signal observed");
        check(WaitForSingleObject(selected, 0) == WAIT_TIMEOUT, "one auto-reset signal only");
        check(WaitForSingleObject(other, 0) == WAIT_TIMEOUT, "signal never crosses protocol namespace");
    }
    check(signal_existing(pid, creation, executable) == Result::kSent, "compile-time CLI protocol signals existing event");
    check(WaitForSingleObject(kProtocol == Protocol::kWrist ? wrist.value : source.value, 0) == WAIT_OBJECT_0,
        "compiled CLI selects correct namespace");
    unchanged();
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
        std::wcerr << L"Usage: " << kProgramName << L" --pid N --creation FILETIME --expected-image ABSOLUTE_EXE\n";
        return static_cast<int>(Result::kArguments);
    }
    const auto result = signal_existing(static_cast<DWORD>(pid), creation, fs::path(argv[6]));
    if (result == Result::kSent)
        std::cout << "Signal sent to existing event; not an ARMED or admission confirmation.\n";
    else
        std::cerr << "Signal refused/failed; code=" << static_cast<int>(result) << '\n';
    return static_cast<int>(result);
}
