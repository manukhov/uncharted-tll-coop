#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace coop::p99_group_fiber {

struct Identity {
    void* fiber = nullptr;
    DWORD thread = 0; // Only used for a thread that has not become a fiber.
    bool operator==(const Identity&) const = default;
};
struct Binding {
    Identity identity{};
    bool active = false;
};
struct ReadResult { void* value; DWORD status; };
template<class Read, class Bootstrap>
bool read_bootstrapped(Read read, Bootstrap bootstrap, void*& value) noexcept {
    auto result = read();
    if (result.value == nullptr && result.status == ERROR_INVALID_PARAMETER && bootstrap())
        result = read();
    value = result.value;
    return value != nullptr || result.status == ERROR_SUCCESS;
}

struct WindowsApi {
    inline static std::atomic<DWORD> bootstrap_index{FLS_OUT_OF_INDEXES};
    inline static const unsigned char bootstrap_marker = 0;
    static DWORD allocate() noexcept {
        const DWORD error = GetLastError();
        const DWORD index = FlsAlloc(nullptr); // No destructor for borrowed stack storage.
        if (index != FLS_OUT_OF_INDEXES) {
            const DWORD bootstrap = FlsAlloc(nullptr);
            if (bootstrap == FLS_OUT_OF_INDEXES) {
                FlsFree(index); // Unpublished allocation; no hook can reach it yet.
                SetLastError(error);
                return FLS_OUT_OF_INDEXES;
            }
            bootstrap_index.store(bootstrap, std::memory_order_release);
        }
        SetLastError(error);
        return index;
    }
    static Identity identity() noexcept {
        return IsThreadAFiber() ? Identity{GetCurrentFiber(), 0} : Identity{nullptr, GetCurrentThreadId()};
    }
    static bool read(DWORD index, void*& value) noexcept {
        const DWORD error = GetLastError();
        // Windows can return NULL/87 before this fiber's FLS array exists.
        // Do NOT assume any read failure means empty and never clear the
        // frame slot to bootstrap. Materialize a DISTINCT reserved slot and
        // strictly reread the original, preserving any existing frame.
        const bool ok = read_bootstrapped([&]() {
            SetLastError(ERROR_SUCCESS);
            void* result = FlsGetValue(index);
            return ReadResult{result, GetLastError()};
        }, []() {
            const auto bootstrap = bootstrap_index.load(std::memory_order_acquire);
            return bootstrap != FLS_OUT_OF_INDEXES &&
                FlsSetValue(bootstrap, const_cast<unsigned char*>(&bootstrap_marker)) != FALSE;
        }, value);
        SetLastError(error);
        return ok;
    }
    static bool write(DWORD index, void* value) noexcept {
        const DWORD error = GetLastError();
        const bool ok = FlsSetValue(index, value) != FALSE;
        SetLastError(error);
        return ok;
    }
};

// A frame FLS index (plus WindowsApi's separate bootstrap index), allocated
// BEFORE publishing any hook. Never free/reuse while callbacks or suspended
// fibers can still reach this DLL. WindowsApi is used by one package Slot.
// Frame derives from Binding and is owned by the wrapper's stack. No pointer
// escapes the current fiber; our frame is stack-only. Windows may allocate
// its per-fiber FLS array on the first successful FlsSetValue.
template<class Frame, class Api = WindowsApi>
class Slot {
public:
    bool initialize() noexcept { // Once, by the installer's irreversible gate.
        if (index_.load(std::memory_order_acquire) != FLS_OUT_OF_INDEXES || failed()) return false;
        const auto index = Api::allocate();
        if (index == FLS_OUT_OF_INDEXES) { poison(); return false; }
        index_.store(index, std::memory_order_release);
        return true;
    }
    bool failed() const noexcept { return failed_.load(std::memory_order_acquire); }
    bool initialized() const noexcept {
        return index_.load(std::memory_order_acquire) != FLS_OUT_OF_INDEXES;
    }
    void poison() noexcept { failed_.store(true, std::memory_order_release); }

    Frame* current() noexcept {
        // Crucially BEFORE touching the slot/pointee: failed restoration may
        // have left a dead stack address in FLS. Poison is irreversible.
        if (failed()) return nullptr;
        const auto index = index_.load(std::memory_order_acquire);
        if (index == FLS_OUT_OF_INDEXES) return nullptr;
        void* raw = nullptr;
        if (!Api::read(index, raw)) { poison(); return nullptr; }
        auto* frame = static_cast<Frame*>(raw);
        if (frame && (!frame->active || !(frame->identity == Api::identity()))) {
            poison(); return nullptr;
        }
        return frame;
    }
    bool owns(const Frame& frame) noexcept { return current() == &frame; }

    bool enter(Frame& frame, Frame*& previous) noexcept {
        previous = current();
        const auto index = index_.load(std::memory_order_acquire);
        if (failed() || index == FLS_OUT_OF_INDEXES || frame.active) return false;
        frame.identity = Api::identity();
        frame.active = true;
        if (!Api::write(index, &frame)) {
            frame.active = false;
            poison(); // Hide any previous scope BEFORE the stock fallback.
            return false;
        }
        return true;
    }
    bool leave(Frame& frame, Frame* previous) noexcept {
        // Attempt restoration even after another fiber poisoned this slot.
        // Compare raw values without dereferencing a possibly failed slot.
        const auto index = index_.load(std::memory_order_acquire);
        void* raw = nullptr;
        const bool exact = frame.active && frame.identity == Api::identity() &&
            index != FLS_OUT_OF_INDEXES && Api::read(index, raw) && raw == &frame;
        const bool restored = exact && Api::write(index, previous);
        frame.active = false;
        if (!restored) poison();
        return restored;
    }

private:
    std::atomic<DWORD> index_{FLS_OUT_OF_INDEXES};
    std::atomic<bool> failed_{false};
};

} // namespace coop::p99_group_fiber
