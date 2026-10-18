#pragma once

#include <atomic>
#include <cstdint>

namespace dispatchlab {

// Process-wide instrumentation counters. Every routing search entry point and
// every dispatch decision increments one; replay prints them to demonstrate
// that it reconstructs state without any routing or dispatch.
inline std::atomic<std::uint64_t>& routing_call_counter() {
    static std::atomic<std::uint64_t> c{0};
    return c;
}
inline std::atomic<std::uint64_t>& dispatch_call_counter() {
    static std::atomic<std::uint64_t> c{0};
    return c;
}
inline void count_routing_call() { routing_call_counter().fetch_add(1, std::memory_order_relaxed); }
inline void count_dispatch_call() { dispatch_call_counter().fetch_add(1, std::memory_order_relaxed); }

}  // namespace dispatchlab
