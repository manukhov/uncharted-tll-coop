#include "nadine_p99_provider_literal.h"
#pragma section(".p99rx",execute,read)
extern "C" {
__declspec(dllexport) __declspec(allocate(".p99rx"))
extern const std::array<std::uint8_t,32> P99ProviderLiteral=kProviderLiteral;
}
