#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace dispatchlab {

using NodeId = std::uint32_t;
using EdgeId = std::uint32_t;
// Edge weights are travel times in integer milliseconds.
using Weight = std::uint32_t;
// Path costs are accumulated in 64 bits. A path has at most 2^32 edges of at
// most 2^32-1 ms each, so the true sum always fits; sat_add() still guards it.
using Dist = std::uint64_t;
// Simulation timestamps (milliseconds since scenario start).
using SimTime = std::int64_t;

inline constexpr NodeId kInvalidNode = std::numeric_limits<NodeId>::max();
inline constexpr Dist kInfDist = std::numeric_limits<Dist>::max();

// Overflow-protected accumulation: never wraps, throws if the sum would reach
// the infinity sentinel.
inline Dist sat_add(Dist a, Weight w) {
    if (a >= kInfDist - static_cast<Dist>(w)) {
        throw std::overflow_error("path cost overflow");
    }
    return a + static_cast<Dist>(w);
}

}  // namespace dispatchlab
