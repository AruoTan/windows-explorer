#pragma once
#include <atomic>
#include <cstdint>

namespace DriveNavFilter {
inline void* ReadSlot(void** slot) {
    static_assert(std::atomic_ref<void*>::is_always_lock_free);
    // No read/modify/write instruction against a read-only vtable page.
    return std::atomic_ref<void*>(*slot).load(std::memory_order_acquire);
}
template <typename Protection>
std::uint32_t ExchangePointer(Protection& protection, void** slot,
                              void* expected, void* replacement) {
    typename Protection::Previous previous{};
    std::uint32_t error = protection.MakeWritable(slot, previous);
    if (error) return error;
    bool changed = std::atomic_ref<void*>(*slot).compare_exchange_strong(expected, replacement);
    error = protection.Restore(slot, previous);
    if (!changed) return 170; // ERROR_BUSY: preserve another extension's new pointer.
    return error;
}
}
