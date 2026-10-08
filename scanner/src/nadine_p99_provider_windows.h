#pragma once
#include "nadine_p99_provider_owner.h"
#include <Windows.h>

namespace coop::p99_provider {
struct alignas(kPage) Page {Wire bytes{};};
static_assert(sizeof(Page)==kPage);
// Separate component, not linked to ordinary runtime or retained ABI artifact.
class WindowsOps {
    Page& page_;
public:
    explicit WindowsOps(Page& page) noexcept:page_(page){}
    WindowsOps(const WindowsOps&)=delete;WindowsOps& operator=(const WindowsOps&)=delete;
    std::uint32_t thread() const noexcept;
    bool identity(const Session&) const noexcept;
    Acquire acquire(const Selection&) noexcept;
    bool verify(const Selection&,std::uintptr_t retained) const noexcept;
    bool pin(const Selection&,std::uintptr_t retained) noexcept;
    bool publish(const Session&,const Wire&) noexcept;
    std::optional<Claims> read(const Session&) const noexcept;
};
// Read-only borrowed process handle. No process open/discovery, PIN certificate,
// native callback, implicit ACK or installer permission. Read RO page twice.
std::optional<Claims> read_claims(HANDLE process,const Session& expected,std::uintptr_t address) noexcept;
} // namespace coop::p99_provider
