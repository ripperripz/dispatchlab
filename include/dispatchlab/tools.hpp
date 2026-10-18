#pragma once

#include <string>
#include <vector>

#include "dispatchlab/graph.hpp"
#include "dispatchlab/scenario.hpp"
#include "dispatchlab/simulator.hpp"

namespace dispatchlab {

struct ViewerMeta {
    std::string policy;
    std::string routing;
};

// Builds the self-contained replay document consumed by viewer/index.html:
// road geometry, entities, orders and the full event log (with route paths).
// Throws if a travel event has no path (simulate with paths enabled).
std::string build_viewer_replay(const Graph& g, const Scenario& sc, const std::vector<LogEvent>& log,
                                const ViewerMeta& meta);

// Structural + semantic validation of a replay document; replays its events
// through the StateMachine and compares the final hash with meta. Returns ""
// if valid, else the first problem.
std::string validate_viewer_replay(const std::string& json_text, std::string* summary = nullptr);

// Generates PERFORMANCE.md from benchmark JSON files. Text outside the
// <!-- BEGIN GENERATED --> / <!-- END GENERATED --> markers of an existing
// file is preserved (hand-written analysis).
void write_performance_report(const std::string& graph_json, const std::string& routing_json,
                              const std::string& dispatch_json, const std::string& experiments_md,
                              const std::string& out_path);

}  // namespace dispatchlab
