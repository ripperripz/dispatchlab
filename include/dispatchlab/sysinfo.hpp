#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace dispatchlab {

struct SysInfo {
    std::string cpu;
    std::string os;
    std::uint32_t logical_cores = 0;
    std::uint64_t memory_bytes = 0;
    std::string compiler;    // id + version captured at configure time
    std::string build_type;  // CMAKE_BUILD_TYPE
    std::string cxx_flags;   // CMAKE_CXX_FLAGS + release flags
    std::string timestamp_utc;
};

SysInfo system_info();
// Peak resident set size of this process in bytes (getrusage).
std::uint64_t peak_rss_bytes();

// Nearest-rank percentile (p in [0, 100]) of a copy of `v`.
inline double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size())));
    return v[std::min(v.size() - 1, rank == 0 ? 0 : rank - 1)];
}
inline double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}
inline double stddev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double m = mean(v);
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}

}  // namespace dispatchlab
