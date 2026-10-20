#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dispatchlab/graph.hpp"
#include "dispatchlab/types.hpp"

namespace dispatchlab {

struct ScenarioParams {
    std::uint32_t couriers = 20;
    std::uint32_t orders = 100;
    std::uint32_t restaurants = 0;          // 0 => clamp(orders / 10, 5, 200)
    SimTime order_window_ms = 3 * 3600000;  // orders arrive (Poisson) over this window
    SimTime prep_min_ms = 8 * 60000;
    SimTime prep_max_ms = 20 * 60000;
    SimTime drain_ms = 2 * 3600000;         // simulation ends at window + drain
    double max_delivery_km = 6.0;           // customer within this geodesic radius of the restaurant
    std::string traffic = "free";
    std::uint64_t seed = 1;
    // "uniform" (default): restaurants, couriers and arrival times uniform.
    // "clustered": restaurants in hotspots, couriers biased toward them,
    // popularity-skewed restaurant choice, lunch and dinner arrival peaks.
    std::string demand = "uniform";
};

struct Restaurant {
    std::uint32_t id;
    NodeId location;
};
struct Customer {
    std::uint32_t id;
    NodeId location;
};
struct CourierInit {
    std::uint32_t id;
    NodeId location;
};
struct OrderSpec {
    std::uint32_t id;
    std::uint32_t restaurant_id;
    std::uint32_t customer_id;
    SimTime created_at;
    SimTime preparation_ms;
};

// A fully materialized workload. Generated once and shared by every dispatch
// policy so that comparisons are paired (PRD FR-10).
struct Scenario {
    ScenarioParams params;
    std::string graph_source;
    std::uint32_t graph_nodes = 0;
    std::vector<Restaurant> restaurants;
    std::vector<Customer> customers;
    std::vector<CourierInit> couriers;
    std::vector<OrderSpec> orders;  // sorted by (created_at, id)
    SimTime end_time = 0;

    // FNV-1a over the canonical content (used to prove both policies saw the same input).
    std::uint64_t hash() const;
};

Scenario generate_scenario(const Graph& g, const ScenarioParams& p);
std::string scenario_to_json(const Scenario& s);
Scenario scenario_from_json(const std::string& text);
void save_scenario(const Scenario& s, const std::string& path);
Scenario load_scenario(const std::string& path);

}  // namespace dispatchlab
