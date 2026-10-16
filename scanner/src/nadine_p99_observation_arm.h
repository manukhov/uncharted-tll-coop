#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

namespace coop::p99_observation_arm {
// An exact process instance, not a reusable PID or an acknowledgement of arming.
[[nodiscard]] inline std::wstring event_name(DWORD pid, std::uint64_t creation) {
    return L"Local\\TLL-P99-Source-Arm-v1-" + std::to_wstring(pid) +
        L"-" + std::to_wstring(creation);
}
// Separate protocol: an old source/preparation signal must not arm post16 reads.
[[nodiscard]] inline std::wstring wrist_event_name(DWORD pid, std::uint64_t creation) {
    return L"Local\\TLL-P99-Wrist-Arm-v1-" + std::to_wstring(pid) +
        L"-" + std::to_wstring(creation);
}
// This requests the consumed-command census; it does not arm it immediately.
// A fresh eligible observation is still required. The v2 namespace cannot
// signal the old immediate-arm census or either source/wrist observer.
[[nodiscard]] inline std::wstring census_event_name(DWORD pid, std::uint64_t creation) {
    return L"Local\\TLL-P99-Consumed-Request-v2-" + std::to_wstring(pid) +
        L"-" + std::to_wstring(creation);
}

[[nodiscard]] inline bool creation_stamp(HANDLE process, std::uint64_t& creation) noexcept {
    creation = 0;
    FILETIME started{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &started, &exited, &kernel, &user)) return false;
    creation = (static_cast<std::uint64_t>(started.dwHighDateTime) << 32) |
        started.dwLowDateTime;
    return true;
}
} // namespace coop::p99_observation_arm
