#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "dispatchlab/types.hpp"

namespace dispatchlab {

// IUGG mean Earth radius in metres.
inline constexpr double kEarthRadiusM = 6371008.8;

// Great-circle distance in metres. This is the single distance function used
// for edge lengths, the A* heuristic and the Nearest dispatch policy, so the
// triangle inequality that makes the A* heuristic admissible holds between them.
inline double haversine_m(double lat1, double lon1, double lat2, double lon2) {
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
    const double p1 = lat1 * kDeg;
    const double p2 = lat2 * kDeg;
    const double dp = (lat2 - lat1) * kDeg;
    const double dl = (lon2 - lon1) * kDeg;
    const double sdp = std::sin(dp * 0.5);
    const double sdl = std::sin(dl * 0.5);
    double a = sdp * sdp + std::cos(p1) * std::cos(p2) * sdl * sdl;
    a = std::min(1.0, std::max(0.0, a));
    return 2.0 * kEarthRadiusM * std::asin(std::sqrt(a));
}

// Travel time in ms for `metres` at `speed_kmh`, rounded UP and at least 1 ms.
// Rounding up guarantees w >= metres / v_max, which the A* heuristic relies on.
inline Weight travel_time_ms(double metres, std::uint32_t speed_kmh) {
    const double ms = metres * 3600.0 / static_cast<double>(speed_kmh);
    const double up = std::ceil(ms);
    if (up < 1.0) return 1;
    if (up >= 4294967295.0) throw std::overflow_error("edge weight exceeds uint32");
    return static_cast<Weight>(up);
}

// Lower bound (ms) on the travel time over a geodesic distance at v_max.
// Rounded DOWN so it never exceeds any rounded-up edge-weight sum.
inline Dist lower_bound_ms(double metres, std::uint32_t vmax_kmh) {
    const double ms = metres * 3600.0 / static_cast<double>(vmax_kmh);
    return ms <= 0.0 ? 0 : static_cast<Dist>(std::floor(ms));
}

}  // namespace dispatchlab
