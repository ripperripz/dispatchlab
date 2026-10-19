#pragma once

// The JSON API behind the local console (`dispatchlab serve`). Every response
// is computed by the engine on request or read from the project's own result
// files; nothing is mocked.
//
//   GET /api/info                      graph, CCH and environment facts
//   GET /api/network                   road geometry for the map
//   GET /api/route?s&t&traffic&trace   all four algorithms on one pair
//   GET /api/simulate?...&policy       one simulation (replay document)
//   GET /api/compare?...               both policies on one scenario
//   GET /api/agreement?queries&seed    live agreement run
//   GET /api/determinism?...           run twice + replay, compare hashes
//   GET /api/selftest                  corrupted-log rejection test
//   GET /api/results                   benchmark + experiment result files
//   GET /<file>                        static console files from --root

#include <memory>
#include <mutex>
#include <string>

#include "dispatchlab/graph.hpp"
#include "dispatchlab/http_server.hpp"

namespace dispatchlab {

struct ConsoleOptions {
    std::string static_root = "viewer";  // console files
    std::string results_root = ".";      // where benchmarks/ and experiments/ live
};

class ConsoleApi {
public:
    ConsoleApi(Graph base, ConsoleOptions opts);
    ~ConsoleApi();
    HttpResponse handle(const HttpRequest& req);
    const Graph& graph() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// {"benchmarks":{graph,routing,dispatch},"experiments":{verify,full}} from the
// result files under `results_root` (missing files become null).
std::string build_results_bundle(const std::string& results_root);

}  // namespace dispatchlab
