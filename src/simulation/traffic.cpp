#include "dispatchlab/traffic.hpp"

#include <stdexcept>

#include "dispatchlab/road_model.hpp"
#include "dispatchlab/rng.hpp"

namespace dispatchlab {

std::string_view traffic_name(TrafficScenario t) {
    switch (t) {
        case TrafficScenario::FreeFlow: return "free";
        case TrafficScenario::Moderate: return "moderate";
        case TrafficScenario::Heavy: return "heavy";
    }
    return "?";
}

TrafficScenario parse_traffic(std::string_view s) {
    if (s == "free" || s == "freeflow" || s == "free_flow") return TrafficScenario::FreeFlow;
    if (s == "moderate") return TrafficScenario::Moderate;
    if (s == "heavy") return TrafficScenario::Heavy;
    throw std::invalid_argument("unknown traffic scenario: " + std::string(s));
}

TrafficProfile traffic_profile(TrafficScenario t) {
    switch (t) {
        case TrafficScenario::FreeFlow: return {0, 1000, 1000, 0, 1000, 1000};
        case TrafficScenario::Moderate: return {500, 1200, 1600, 200, 1050, 1200};
        case TrafficScenario::Heavy: return {800, 1600, 2500, 400, 1100, 1500};
    }
    throw std::logic_error("unreachable");
}

Graph apply_traffic(const Graph& base, TrafficScenario t, std::uint64_t seed) {
    Graph g = base;
    if (t == TrafficScenario::FreeFlow) return g;
    const TrafficProfile p = traffic_profile(t);
    for (std::uint32_t e = 0; e < g.num_edges(); ++e) {
        std::uint64_t x = seed ^ (0x9e3779b97f4a7c15ULL * (static_cast<std::uint64_t>(e) + 1));
        Rng rng(Rng::splitmix64(x));
        const auto rc = static_cast<RoadClass>(g.road_class[e]);
        const bool arterial = g.road_class[e] < kRoadClassCount && road_class_info(rc).arterial;
        const std::uint32_t prob = arterial ? p.arterial_prob_permille : p.local_prob_permille;
        if (rng.below(1000) >= prob) continue;
        const std::uint32_t lo = arterial ? p.arterial_min_permille : p.local_min_permille;
        const std::uint32_t hi = arterial ? p.arterial_max_permille : p.local_max_permille;
        const auto m = static_cast<std::uint64_t>(rng.range(lo, hi));
        const std::uint64_t w = (static_cast<std::uint64_t>(g.weights[e]) * m + 999) / 1000;  // ceil
        if (w > UINT32_MAX) throw std::overflow_error("traffic weight overflow");
        g.weights[e] = static_cast<std::uint32_t>(w);
    }
    build_reverse(g);
    return g;
}

}  // namespace dispatchlab
