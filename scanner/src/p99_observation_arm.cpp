#include "nadine_p99_observation_arm.h"
#include "enemy_jointblend_arm.h"
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
enum class Protocol { kSource, kWrist, kCensus, kEnemy };
constexpr std::array kProtocols{Protocol::kSource, Protocol::kWrist, Protocol::kCensus, Protocol::kEnemy};
#if ((defined(TLL_ENEMY_JOINTBLEND_ARM) && TLL_ENEMY_JOINTBLEND_ARM) + \
     (defined(TLL_P99_WRIST_ARM) && TLL_P99_WRIST_ARM) + \
     (defined(TLL_P99_CONSUMED_ARM) && TLL_P99_CONSUMED_ARM)) > 1
#error Select exactly one observation arming protocol
#endif
#if defined(TLL_ENEMY_JOINTBLEND_ARM) && TLL_ENEMY_JOINTBLEND_ARM
constexpr auto kProtocol = Protocol::kEnemy;
constexpr auto kProgramName = L"tll_enemy_jointblend_arm";
#elif defined(TLL_P99_CONSUMED_ARM) && TLL_P99_CONSUMED_ARM
constexpr auto kProtocol = Protocol::kCensus;
constexpr auto kProgramName = L"tll_p99_consumed_arm";
#elif defined(TLL_P99_WRIST_ARM) && TLL_P99_WRIST_ARM
constexpr auto kProtocol = Protocol::kWrist;
constexpr auto kProgramName = L"tll_p99_wrist_arm";
#else
constexpr auto kProtocol = Protocol::kSource;
constexpr auto kProgramName = L"tll_p99_observation_arm";
#endif

std::wstring event_name(Protocol protocol, DWORD pid, std::uint64_t creation) {
    switch (protocol) {
    case Protocol::kSource: return arm::event_name(pid, creation);
    case Protocol::kWrist: return arm::wrist_event_name(pid, creation);
    case Protocol::kCensus: return arm::census_event_name(pid, creation);
    case Protocol::kEnemy: return coop::enemy_jointblend_arm::event_name(pid, creation);
    }
    return {};
}

constexpr std::string_view sent_message(Protocol protocol) noexcept {
    return protocol == Protocol::kCensus ?
        "Census request sent to existing event; a fresh eligible observation is still required. Not an ARMED or admission confirmation.\n" :
        "Signal sent to existing event; not an ARMED or admission confirmation.\n";
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

// The explicit protocol is private to this TU so self-test can exercise all
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
    check(arm::census_event_name(12, 345) == L"Local\\TLL-P99-Consumed-Request-v2-12-345", "exact census request event name");
    check(arm::census_event_name(12, 345) != L"Local\\TLL-P99-Consumed-Arm-v1-12-345",
        "census request differs from old immediate-arm protocol");
    check(sent_message(Protocol::kCensus) ==
        "Census request sent to existing event; a fresh eligible observation is still required. Not an ARMED or admission confirmation.\n",
        "census sent message confirms only request, not arming");
    for (const auto protocol : {Protocol::kSource, Protocol::kWrist})
        check(sent_message(protocol) == "Signal sent to existing event; not an ARMED or admission confirmation.\n",
            "source and wrist sent messages unchanged");
    for (const auto protocol : kProtocols) {
        check(event_name(protocol, 12, 345) != event_name(protocol, 13, 345), "PID names separated");
        check(event_name(protocol, 12, 345) != event_name(protocol, 12, 346), "creation names separated");
        check(event_name(protocol, 1, 23) != event_name(protocol, 12, 3), "decimal components separated");
        for (const auto other : kProtocols)
            if (other != protocol)
                check(event_name(protocol, 12, 345) != event_name(other, 12, 345), "protocol namespaces separated");
    }

    const DWORD pid = GetCurrentProcessId();
    std::uint64_t creation = 0;
    std::array<wchar_t, 32768> image{};
    DWORD length = static_cast<DWORD>(image.size());
    if (!arm::creation_stamp(GetCurrentProcess(), creation) || creation == 0 ||
        !QueryFullProcessImageNameW(GetCurrentProcess(), 0, image.data(), &length)) return 1;
    const fs::path executable(std::wstring_view(image.data(), length));
    const auto source_name = arm::event_name(pid, creation);
    const auto wrist_name = arm::wrist_event_name(pid, creation);
    const auto census_name = arm::census_event_name(pid, creation);
    const auto enemy_name = coop::enemy_jointblend_arm::event_name(pid, creation);
    for (const auto protocol : kProtocols) {
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
    const auto old_census_name = L"Local\\TLL-P99-Consumed-Arm-v1-" +
        std::to_wstring(pid) + L"-" + std::to_wstring(creation);
    {
        Handle absent(OpenEventW(SYNCHRONIZE, FALSE, old_census_name.c_str()));
        if (absent) { std::cerr << "Old census self-test event unexpectedly exists; refusing to alter it\n"; return 1; }
    }
    Handle old_census(CreateEventW(nullptr, FALSE, FALSE, old_census_name.c_str()));
    if (!old_census || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
    check(signal_existing_for(Protocol::kCensus, pid, creation, executable) == Result::kEvent,
        "old immediate-arm event does not substitute for absent census request event");
    check(WaitForSingleObject(old_census.value, 0) == WAIT_TIMEOUT,
        "census request never signals old immediate-arm event");
    {
        Handle absent(OpenEventW(SYNCHRONIZE, FALSE, census_name.c_str()));
        check(!absent, "old immediate-arm event does not create a census request event");
    }
    for (const auto protocol : kProtocols) {
        const auto name = event_name(protocol, pid, creation);
        Handle existing(CreateEventW(nullptr, FALSE, FALSE, name.c_str()));
        if (!existing || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
        for (const auto other : kProtocols) {
            if (other == protocol) continue;
            check(signal_existing_for(other, pid, creation, executable) == Result::kEvent,
                "other existing protocol does not substitute for absent selected event");
            check(WaitForSingleObject(existing.value, 0) == WAIT_TIMEOUT,
                "missing protocol signal leaves other existing event untouched");
            Handle absent(OpenEventW(SYNCHRONIZE, FALSE, event_name(other, pid, creation).c_str()));
            check(!absent, "missing protocol remains absent after refusal");
        }
    }
    Handle source(CreateEventW(nullptr, FALSE, FALSE, source_name.c_str()));
    if (!source || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
    Handle wrist(CreateEventW(nullptr, FALSE, FALSE, wrist_name.c_str()));
    if (!wrist || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
    Handle census(CreateEventW(nullptr, FALSE, FALSE, census_name.c_str()));
    if (!census || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
    Handle enemy(CreateEventW(nullptr, FALSE, FALSE, enemy_name.c_str()));
    if (!enemy || GetLastError() == ERROR_ALREADY_EXISTS) return 1;
    check(SetEvent(old_census.value) != FALSE, "private old immediate-arm fixture signal succeeds");
    check(WaitForSingleObject(census.value, 0) == WAIT_TIMEOUT,
        "old immediate-arm signal never becomes a census request");
    check(WaitForSingleObject(old_census.value, 0) == WAIT_OBJECT_0,
        "old immediate-arm fixture signal stays in its own namespace");
    const auto selected_event = [&](Protocol protocol) {
        switch (protocol) {
        case Protocol::kSource: return source.value;
        case Protocol::kWrist: return wrist.value;
        case Protocol::kCensus: return census.value;
        case Protocol::kEnemy: return enemy.value;
        }
        return static_cast<HANDLE>(nullptr);
    };
    const auto unchanged = [&] {
        check(WaitForSingleObject(source.value, 0) == WAIT_TIMEOUT, "refusal did not signal source event");
        check(WaitForSingleObject(wrist.value, 0) == WAIT_TIMEOUT, "refusal did not signal wrist event");
        check(WaitForSingleObject(census.value, 0) == WAIT_TIMEOUT, "refusal did not signal census event");
        check(WaitForSingleObject(enemy.value, 0) == WAIT_TIMEOUT, "refusal did not signal enemy event");
        check(WaitForSingleObject(old_census.value, 0) == WAIT_TIMEOUT, "operation did not signal old census event");
    };
    for (const auto protocol : kProtocols) {
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
        const auto selected = selected_event(protocol);
        check(WaitForSingleObject(selected, 0) == WAIT_OBJECT_0, "selected protocol signal observed");
        check(WaitForSingleObject(selected, 0) == WAIT_TIMEOUT, "one auto-reset signal only");
        for (const auto other : kProtocols)
            if (other != protocol)
                check(WaitForSingleObject(selected_event(other), 0) == WAIT_TIMEOUT,
                    "signal never crosses protocol namespace");
        check(WaitForSingleObject(old_census.value, 0) == WAIT_TIMEOUT,
            "selected protocol signal never reaches old immediate-arm census");
    }
    check(signal_existing(pid, creation, executable) == Result::kSent, "compile-time CLI protocol signals existing event");
    check(WaitForSingleObject(selected_event(kProtocol), 0) == WAIT_OBJECT_0,
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
        std::cout << sent_message(kProtocol);
    else
        std::cerr << "Signal refused/failed; code=" << static_cast<int>(result) << '\n';
    return static_cast<int>(result);
}
