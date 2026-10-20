#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "dispatchlab/graph.hpp"
#include "dispatchlab/routing.hpp"
#include "dispatchlab/types.hpp"

namespace dispatchlab {

struct ExperimentSize {
    std::string name;
    std::uint32_t couriers;
    std::uint32_t orders;
    SimTime window_ms;
};

struct ExperimentConfig {
    std::string id = "experiment";
    std::vector<ExperimentSize> sizes;
    std::vector<std::string> traffic;
    std::vector<std::uint64_t> seeds;
    std::vector<std::string> policies = {"nearest", "greedy_eta"};
    Algorithm routing = Algorithm::kAStar;
    SimTime prep_min_ms = 8 * 60000;
    SimTime prep_max_ms = 20 * 60000;
    SimTime drain_ms = 2 * 3600000;
    double max_delivery_km = 6.0;
    unsigned threads = 0;  // 0 => hardware concurrency
};

ExperimentConfig load_experiment_config(const std::string& path);

struct ExperimentRow {
    std::string experiment_id, size, traffic, policy, routing;
    std::uint32_t couriers = 0, orders = 0;
    std::uint64_t seed = 0;
    std::uint64_t scenario_hash = 0, final_state_hash = 0;
    std::uint32_t completed = 0, undelivered = 0;
    double avg_delivery_min = 0, p50_delivery_min = 0, p95_delivery_min = 0, avg_wait_min = 0;
    double utilization = 0, total_distance_km = 0;
    std::uint64_t dispatch_decisions = 0, routing_queries = 0, events = 0;
    double assign_latency_mean_us = 0, assign_latency_p95_us = 0;
    double sim_wall_s = 0;
};

// Runs every (size, traffic, seed) cell. Each cell generates ONE scenario and
// runs every policy on that same object (paired comparison). Cells run in
// parallel; the returned rows are in deterministic (size, traffic, seed,
// policy) order regardless of thread count.
std::vector<ExperimentRow> run_experiment(const Graph& base, const ExperimentConfig& cfg,
                                          const std::function<void(const std::string&)>& progress = {});

// Writes policy_comparison.csv, policy_comparison.json (with reproducibility
// metadata) and policy_summary.md (paired statistics) into out_dir.
void write_experiment_results(const std::vector<ExperimentRow>& rows, const ExperimentConfig& cfg,
                              const Graph& base, double total_wall_s, const std::string& out_dir);

// Fairness check over a results CSV: each (size, traffic, seed) cell has every
// policy exactly once, all with the same scenario hash; >= 2 seeds per
// (size, traffic); completed + undelivered == orders. Returns "" if OK.
std::string check_experiment_csv(const std::string& path, std::string* report = nullptr);

}  // namespace dispatchlab
