#include "enemy_jointblend_prepare.h"
#include <Windows.h>

// Inert owned test image, not a substitute retail host. Nothing calls the
// production prepare entry here; only the exact shared publication/relay code.
#pragma section(".epub", read, write)
extern "C" {
__declspec(dllexport) __declspec(allocate(".epub")) coop::enemy_jointblend_prepare::Page EnemyFixturePage{};
__declspec(dllexport) __declspec(noinline) std::uint64_t __fastcall EnemyFixtureTarget(
    std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e, std::uint64_t f) {
    if (a == UINT64_MAX) RaiseException(0xE1234567, 0, 0, nullptr);
    SetLastError(0xAABBCCDD);
    return a + 3*b + 5*c + 7*d + 11*e + 13*f;
}
}
