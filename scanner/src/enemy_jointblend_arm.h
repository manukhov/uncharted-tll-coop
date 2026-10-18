#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
namespace coop::enemy_jointblend_arm {
inline std::wstring event_name(DWORD pid, std::uint64_t creation) {
    return L"Local\\TLL-Enemy-JointBlend-Arm-v1-" + std::to_wstring(pid) + L"-" + std::to_wstring(creation);
}
} // namespace coop::enemy_jointblend_arm
