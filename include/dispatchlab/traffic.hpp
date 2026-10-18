#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "dispatchlab/graph.hpp"

namespace dispatchlab {

// Synthetic traffic scenarios (PRD §8): w'(e) = ceil(w(e) * m(e)), m(e) >= 1,
// fixed for a whole simulation run. Multipliers are stored in per-mille.
//
//   free      : m = 1.000 on every edge
//   moderate  : arterial edges (trunk/primary/secondary + links) congested with
//               probability 0.50, m ~ U[1.20, 1.60]; other edges with
//               probability 0.20, m ~ U[1.05, 1.20]
//   heavy     : arterial with probability 0.80, m ~ U[1.60, 2.50]; other edges
//               with probability 0.40, m ~ U[1.10, 1.50]
//
// Selection and magnitude are drawn per directed edge from Rng(seed ^ edge id
// mix), so a scenario is fully determined by (graph, scenario, seed).
// Because m >= 1 the A* heuristic (based on free-flow v_max) stays admissible.
enum class TrafficScenario { FreeFlow, Moderate, Heavy };

struct TrafficProfile {
    std::uint32_t arterial_prob_permille;
    std::uint32_t arterial_min_permille, arterial_max_permille;
    std::uint32_t local_prob_permille;
    std::uint32_t local_min_permille, local_max_permille;
};

std::string_view traffic_name(TrafficScenario t);
TrafficScenario parse_traffic(std::string_view s);
TrafficProfile traffic_profile(TrafficScenario t);

// Returns a copy of `base` with traffic-adjusted weights (reverse CSR rebuilt).
Graph apply_traffic(const Graph& base, TrafficScenario t, std::uint64_t seed);

}  // namespace dispatchlab
