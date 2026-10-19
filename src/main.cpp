// DispatchLab command-line interface. Run `dispatchlab help` for usage.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <csignal>
#include <fstream>

#include "dispatchlab/console_api.hpp"
#include "dispatchlab/counters.hpp"
#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/experiment.hpp"
#include "dispatchlab/graph.hpp"
#include "dispatchlab/hash.hpp"
#include "dispatchlab/http_server.hpp"
#include "dispatchlab/json.hpp"
#include "dispatchlab/road_model.hpp"
#include "dispatchlab/routing.hpp"
#include "dispatchlab/scc.hpp"
#include "dispatchlab/scenario.hpp"
#include "dispatchlab/simulator.hpp"
#include "dispatchlab/tools.hpp"
#include "dispatchlab/traffic.hpp"
#ifdef DL_WITH_OSM
#include "dispatchlab/osm_importer.hpp"
#endif

using namespace dispatchlab;

namespace {

// Minimal "--key value" / "--flag" argument parser.
struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> opts;

    Args(int argc, char** argv, int first) {
        for (int i = first; i < argc; ++i) {
            std::string a = argv[i];
            if (a.rfind("--", 0) == 0) {
                const std::string key = a.substr(2);
                if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) opts[key] = argv[++i];
                else opts[key] = "1";
            } else {
                positional.push_back(a);
            }
        }
    }
    bool has(const std::string& k) const { return opts.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def) const {
        auto it = opts.find(k);
        return it == opts.end() ? def : it->second;
    }
    std::uint64_t num(const std::string& k, std::uint64_t def) const {
        auto it = opts.find(k);
        return it == opts.end() ? def : std::strtoull(it->second.c_str(), nullptr, 10);
    }
    const std::string& pos(std::size_t i, const char* what) const {
        if (i >= positional.size()) throw std::invalid_argument(std::string("missing argument: ") + what);
        return positional[i];
    }
};

double secs_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void usage() {
    std::puts(
        "DispatchLab - deterministic food-delivery routing & dispatch simulator\n"
        "\n"
        "usage: dispatchlab <command> [args]\n"
        "\n"
        "  help\n"
        "      Show this message.\n"
        "  import <in.osm.pbf> <out.graph> [--clip-tricity] [--source LABEL]\n"
        "      Import drivable roads with libosmium, keep the largest SCC, write CSR binary.\n"
        "  info <graph>\n"
        "      Print graph statistics and run integrity + strong-connectivity checks.\n"
        "  route <graph> <src> <dst> [--algo dijkstra|bidirectional|astar]\n"
        "      Shortest travel time between two compact node ids.\n"
        "  gen-scenario <graph> <out.json> [--couriers N] [--orders N] [--restaurants N] [--window-min M]\n"
        "               [--prep-min M] [--prep-max M] [--drain-min M] [--max-delivery-km K]\n"
        "               [--traffic free|moderate|heavy] [--seed S]\n"
        "      Generate a reproducible workload once (shared by all policies).\n"
        "  simulate <graph> <scenario.json> [--policy nearest|greedy_eta] [--algo astar] [--log out.jsonl]\n"
        "           [--summary out.json] [--no-paths]\n"
        "      Run the deterministic discrete-event simulation; prints metrics and final_state_hash.\n"
        "  replay <scenario.json> <events.jsonl> [--expect HASH]\n"
        "      Rebuild the final state from the log alone (no routing, no dispatch) and print its hash.\n"
        "  check-log <scenario.json> <events.jsonl> [--self-test-corruptions]\n"
        "      Check every PRD 13.3 invariant over a log; optionally prove corrupted logs are rejected.\n"
        "  experiment <graph> --config cfg.json [--out experiments/results]\n"
        "      Paired policy comparison over sizes x traffic x seeds; writes CSV/JSON/Markdown.\n"
        "  check-experiment <policy_comparison.csv>\n"
        "      Verify that every cell compared all policies on one identical scenario.\n"
        "  export-viewer <graph> <scenario.json> <events.jsonl> [--out viewer/replay.json]\n"
        "                [--js viewer/replay_data.js] [--results-js viewer/results_data.js] [--policy NAME]\n"
        "      Write the replay document (and optionally the benchmark/experiment bundle) for the\n"
        "      console, so viewer/index.html works from file:// without a server.\n"
        "  serve <graph> [--port 8080] [--root viewer] [--results .] [--quiet]\n"
        "      Run the console on http://127.0.0.1:<port> (loopback only): live routing lab,\n"
        "      simulations, policy comparison, benchmarks and correctness checks.\n"
        "  validate-replay <replay.json>\n"
        "      Validate a viewer replay document and re-check its final state hash.\n"
        "  perf-report --graph-json F --routing-json F --dispatch-json F [--experiments-md F] [--out PERFORMANCE.md]\n"
        "      Regenerate the measured sections of PERFORMANCE.md.\n");
}

int cmd_import(const Args& a) {
#ifdef DL_WITH_OSM
    const std::string in = a.pos(0, "input .osm.pbf");
    const std::string out = a.pos(1, "output .graph");
    ImportOptions opts;
    if (a.has("clip-tricity")) opts.clip = kTricityBBox;
    opts.source_label = a.get("source", in);
    if (opts.clip) {
        char buf[160];
        std::snprintf(buf, sizeof buf, " clip=[%.2f,%.2f,%.2f,%.2f]", opts.clip->min_lon,
                      opts.clip->min_lat, opts.clip->max_lon, opts.clip->max_lat);
        opts.source_label += buf;
    }
    const auto t0 = std::chrono::steady_clock::now();
    ImportStats st;
    Graph g = import_osm(in, opts, &st);
    const double t_import = secs_since(t0);
    save_graph(g, out);
    std::printf(
        "import: highway_ways=%llu used=%llu rejected_access=%llu oneway_fwd=%llu oneway_bwd=%llu "
        "segments_clipped=%llu\n"
        "import: before_scc nodes=%u edges=%u scc_count=%u\n"
        "import: largest_scc nodes=%u edges=%u max_speed_kmh=%u time=%.2fs -> %s\n",
        (unsigned long long)st.highway_ways_seen, (unsigned long long)st.ways_used,
        (unsigned long long)st.ways_rejected_access, (unsigned long long)st.ways_oneway_forward,
        (unsigned long long)st.ways_oneway_backward, (unsigned long long)st.segments_clipped,
        st.nodes_before_scc, st.edges_before_scc, st.scc_count, st.nodes, st.edges, st.max_speed_kmh,
        t_import, out.c_str());
    return 0;
#else
    (void)a;
    std::fprintf(stderr, "built without libosmium (DISPATCHLAB_WITH_OSM=OFF)\n");
    return 1;
#endif
}

int cmd_info(const Args& a) {
    const auto t0 = std::chrono::steady_clock::now();
    Graph g = load_graph(a.pos(0, "graph"));
    const double t_load = secs_since(t0);
    const std::string err = validate(g);
    const bool sc = is_strongly_connected(g);
    const std::uint32_t comps = strongly_connected_components(g).num_components();
    std::uint64_t total_len = 0;
    for (auto l : g.lengths_mm) total_len += l;
    std::vector<std::uint32_t> per_class(kRoadClassCount, 0);
    for (auto c : g.road_class) per_class[c < kRoadClassCount ? c : 0]++;
    bool ids_sorted = true;
    for (std::size_t i = 1; i < g.osm_ids.size(); ++i) {
        if (g.osm_ids[i] <= g.osm_ids[i - 1]) ids_sorted = false;
    }
    std::printf("graph: %s\n", a.pos(0, "graph").c_str());
    std::printf("source: %s\n", g.meta.source.c_str());
    std::printf("nodes=%u edges=%u max_speed_kmh=%u total_length_km=%.1f load_time=%.3fs memory=%.1fMB\n",
                g.num_nodes(), g.num_edges(), g.meta.max_speed_kmh, total_len / 1e6, t_load,
                g.memory_bytes() / 1048576.0);
    for (std::size_t c = 0; c < kRoadClassCount; ++c) {
        if (per_class[c]) std::printf("  class %-15s edges=%u speed=%u km/h\n",
                                      std::string(kRoadClasses[c].highway).c_str(), per_class[c],
                                      kRoadClasses[c].speed_kmh);
    }
    std::printf("validate=%s strongly_connected=%s scc_components=%u osm_ids_sorted_unique=%s\n",
                err.empty() ? "OK" : err.c_str(), sc ? "yes" : "no", comps, ids_sorted ? "yes" : "no");
    return err.empty() && sc && comps == 1 && ids_sorted ? 0 : 1;
}

int cmd_route(const Args& a) {
    Graph g = load_graph(a.pos(0, "graph"));
    const auto s = static_cast<NodeId>(std::stoul(a.pos(1, "src")));
    const auto t = static_cast<NodeId>(std::stoul(a.pos(2, "dst")));
    Router router(g, parse_algorithm(a.get("algo", "astar")));
    const auto t0 = std::chrono::steady_clock::now();
    RouteResult r = router.route(s, t);
    const double us = secs_since(t0) * 1e6;
    if (!r.reachable()) {
        std::printf("unreachable (settled=%u)\n", r.stats.settled);
        return 1;
    }
    std::printf("algo=%s cost_ms=%llu (%.1f min) hops=%zu length_km=%.3f settled=%u time_us=%.0f\n",
                std::string(algorithm_name(router.algorithm())).c_str(), (unsigned long long)r.cost,
                r.cost / 60000.0, r.path.size() - 1, path_length_mm(g, r.path) / 1e6, r.stats.settled, us);
    return 0;
}

// Applies the scenario's traffic to the base graph (fixed for the whole run).
Graph traffic_graph(const Graph& base, const Scenario& sc) {
    return apply_traffic(base, parse_traffic(sc.params.traffic), sc.params.seed);
}

int cmd_gen_scenario(const Args& a) {
    Graph g = load_graph(a.pos(0, "graph"));
    ScenarioParams p;
    p.couriers = static_cast<std::uint32_t>(a.num("couriers", p.couriers));
    p.orders = static_cast<std::uint32_t>(a.num("orders", p.orders));
    p.restaurants = static_cast<std::uint32_t>(a.num("restaurants", 0));
    p.order_window_ms = static_cast<SimTime>(a.num("window-min", 180)) * 60000;
    p.prep_min_ms = static_cast<SimTime>(a.num("prep-min", 8)) * 60000;
    p.prep_max_ms = static_cast<SimTime>(a.num("prep-max", 20)) * 60000;
    p.drain_ms = static_cast<SimTime>(a.num("drain-min", 120)) * 60000;
    p.max_delivery_km = std::stod(a.get("max-delivery-km", "6"));
    p.traffic = std::string(traffic_name(parse_traffic(a.get("traffic", "free"))));
    p.seed = a.num("seed", 1);
    Scenario sc = generate_scenario(g, p);
    save_scenario(sc, a.pos(1, "output scenario"));
    std::printf("scenario: couriers=%zu orders=%zu restaurants=%zu traffic=%s seed=%llu end_time_min=%lld "
                "scenario_hash=%s -> %s\n",
                sc.couriers.size(), sc.orders.size(), sc.restaurants.size(), sc.params.traffic.c_str(),
                (unsigned long long)sc.params.seed, (long long)(sc.end_time / 60000), hex64(sc.hash()).c_str(),
                a.pos(1, "output scenario").c_str());
    return 0;
}

void print_metrics(const SimMetrics& m) {
    std::printf("orders_total=%u orders_created=%u orders_delivered=%u orders_undelivered=%u\n", m.orders_total,
                m.orders_created, m.orders_delivered, m.orders_undelivered);
    std::printf("avg_delivery_min=%.3f p50_delivery_min=%.3f p95_delivery_min=%.3f max_delivery_min=%.3f "
                "avg_wait_for_courier_min=%.3f\n",
                m.avg_delivery_min, m.p50_delivery_min, m.p95_delivery_min, m.max_delivery_min,
                m.avg_wait_for_courier_min);
    std::printf("courier_utilization=%.4f total_distance_km=%.3f events=%llu last_event_min=%.2f\n",
                m.courier_utilization, m.total_distance_km, (unsigned long long)m.events, m.last_event_time / 60000.0);
    std::printf("dispatch_decisions=%llu assign_latency_mean_us=%.1f assign_latency_p95_us=%.1f routing_queries=%llu\n",
                (unsigned long long)m.dispatch_decisions, m.assign_latency_mean_us, m.assign_latency_p95_us,
                (unsigned long long)m.routing_queries);
}

int cmd_simulate(const Args& a) {
    Graph base = load_graph(a.pos(0, "graph"));
    Scenario sc = load_scenario(a.pos(1, "scenario"));
    Graph g = traffic_graph(base, sc);
    auto policy = make_policy(a.get("policy", "greedy_eta"), g);
    const Algorithm algo = parse_algorithm(a.get("algo", "astar"));
    SimOptions opts;
    opts.log_paths = !a.has("no-paths");
    Simulator sim(g, sc, *policy, algo, opts);
    const auto t0 = std::chrono::steady_clock::now();
    SimResult r = sim.run();
    const double wall = secs_since(t0);
    if (a.has("log")) {
        std::ofstream out(a.get("log", ""), std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write log");
        for (const auto& e : r.log) out << log_event_to_json(e, opts.log_paths) << '\n';
    }
    std::printf("policy=%s routing=%s traffic=%s seed=%llu scenario_hash=%s\n", std::string(policy->name()).c_str(),
                std::string(algorithm_name(algo)).c_str(), sc.params.traffic.c_str(),
                (unsigned long long)sc.params.seed, hex64(sc.hash()).c_str());
    print_metrics(r.metrics);
    std::printf("sim_wall_s=%.3f\n", wall);
    std::printf("final_state_hash=%s\n", hex64(r.final_state_hash).c_str());
    if (a.has("summary")) {
        json::Writer w;
        const SimMetrics& m = r.metrics;
        w.begin_object();
        w.kv("policy", policy->name()).kv("routing", algorithm_name(algo));
        w.kv("scenario_hash", hex64(sc.hash())).kv("final_state_hash", hex64(r.final_state_hash));
        w.kv("orders_total", m.orders_total).kv("orders_delivered", m.orders_delivered);
        w.kv("orders_undelivered", m.orders_undelivered).kv("avg_delivery_min", m.avg_delivery_min);
        w.kv("p95_delivery_min", m.p95_delivery_min).kv("courier_utilization", m.courier_utilization);
        w.kv("total_distance_km", m.total_distance_km).kv("events", m.events);
        w.kv("assign_latency_mean_us", m.assign_latency_mean_us).kv("routing_queries", m.routing_queries);
        w.end_object();
        json::write_file(a.get("summary", ""), w.str());
    }
    return 0;
}

int cmd_replay(const Args& a) {
    Scenario sc = load_scenario(a.pos(0, "scenario"));
    std::vector<LogEvent> log = read_event_log(a.pos(1, "event log"));
    const std::uint64_t r0 = routing_call_counter().load(), d0 = dispatch_call_counter().load();
    ReplayResult r = replay(sc, log);
    const std::uint64_t routing = routing_call_counter().load() - r0;
    const std::uint64_t dispatch = dispatch_call_counter().load() - d0;
    const SimMetrics m = compute_metrics(sc, r.state);
    std::printf("replayed events=%llu orders_delivered=%u avg_delivery_min=%.3f total_distance_km=%.3f\n",
                (unsigned long long)r.events, m.orders_delivered, m.avg_delivery_min, m.total_distance_km);
    std::printf("dispatch_calls=%llu routing_calls=%llu\n", (unsigned long long)dispatch, (unsigned long long)routing);
    std::printf("replayed_state_hash=%s\n", hex64(r.final_state_hash).c_str());
    if (a.has("expect") && a.get("expect", "") != hex64(r.final_state_hash)) {
        std::fprintf(stderr, "replay hash mismatch: expected %s\n", a.get("expect", "").c_str());
        return 1;
    }
    return routing == 0 && dispatch == 0 ? 0 : 1;
}

int cmd_check_log(const Args& a) {
    Scenario sc = load_scenario(a.pos(0, "scenario"));
    std::vector<LogEvent> log = read_event_log(a.pos(1, "event log"));
    if (!a.has("self-test-corruptions")) {
        ReplayResult r = replay(sc, log);  // paranoid: global invariants after every event
        std::printf("checked %llu events: delivered=%u created=%u (delivered <= created), no double delivery, "
                    "no pickup before arrival+ready, no delivery before pickup, monotone time, one order per courier\n",
                    (unsigned long long)r.events, r.state.delivered, r.state.created);
        std::printf("INVARIANTS_OK\n");
        return 0;
    }
    replay(sc, log);  // the original must be valid
    const auto bad = make_corrupted_logs(log);
    int detected = 0;
    for (const auto& [kind, blog] : bad) {
        try {
            replay(sc, blog);
            std::printf("  %-28s NOT detected\n", kind.c_str());
        } catch (const InvariantViolation& e) {
            ++detected;
            std::printf("  %-28s detected: %s\n", kind.c_str(), e.what());
        }
    }
    std::printf("CORRUPTIONS_DETECTED %d/%zu\n", detected, bad.size());
    return detected == static_cast<int>(bad.size()) && bad.size() == 5 ? 0 : 1;
}

int cmd_experiment(const Args& a) {
    Graph g = load_graph(a.pos(0, "graph"));
    ExperimentConfig cfg = load_experiment_config(a.get("config", "experiments/configs/verify.json"));
    const std::string out = a.get("out", "experiments/results");
    const auto t0 = std::chrono::steady_clock::now();
    auto rows = run_experiment(g, cfg, [](const std::string& msg) { std::printf("  %s\n", msg.c_str()); });
    const double wall = secs_since(t0);
    write_experiment_results(rows, cfg, g, wall, out);
    std::printf("experiment %s: %zu runs in %.1fs -> %s/policy_comparison.{csv,json}, policy_summary.md\n",
                cfg.id.c_str(), rows.size(), wall, out.c_str());
    return 0;
}

int cmd_check_experiment(const Args& a) {
    std::string report;
    const std::string err = check_experiment_csv(a.pos(0, "csv"), &report);
    if (!err.empty()) {
        std::printf("EXPERIMENT_FAIR_FAIL: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s\nEXPERIMENT_FAIR_OK\n", report.c_str());
    return 0;
}

int cmd_export_viewer(const Args& a) {
    Graph base = load_graph(a.pos(0, "graph"));
    Scenario sc = load_scenario(a.pos(1, "scenario"));
    std::vector<LogEvent> log = read_event_log(a.pos(2, "event log"));
    ViewerMeta meta;
    meta.policy = a.get("policy", "");
    meta.routing = a.get("algo", "astar");
    if (meta.policy.empty()) meta.policy = "unknown";
    const std::string doc = build_viewer_replay(base, sc, log, meta);
    const std::string out = a.get("out", "viewer/replay.json");
    json::write_file(out, doc);
    if (a.has("js")) json::write_file(a.get("js", ""), "window.DISPATCHLAB_REPLAY = " + doc + ";\n");
    if (a.has("results-js")) {
        json::write_file(a.get("results-js", ""),
                         "window.DISPATCHLAB_RESULTS = " + build_results_bundle(a.get("results", ".")) + ";\n");
    }
    std::printf("viewer replay: %zu events, %.1f MB -> %s%s%s\n", log.size(), doc.size() / 1e6, out.c_str(),
                a.has("js") ? " and " : "", a.get("js", "").c_str());
    return 0;
}

int cmd_validate_replay(const Args& a) {
    std::string summary;
    const std::string err = validate_viewer_replay(json::read_file(a.pos(0, "replay.json")), &summary);
    if (!err.empty()) {
        std::printf("REPLAY_JSON_INVALID: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s\nREPLAY_JSON_OK\n", summary.c_str());
    return 0;
}

int cmd_serve(const Args& a) {
    std::signal(SIGPIPE, SIG_IGN);
    const auto t0 = std::chrono::steady_clock::now();
    Graph g = load_graph(a.pos(0, "graph"));
    ConsoleOptions opts;
    opts.static_root = a.get("root", "viewer");
    opts.results_root = a.get("results", ".");
    ConsoleApi api(std::move(g), opts);
    const auto port = static_cast<std::uint16_t>(a.num("port", 8080));
    HttpServer server(port, [&api](const HttpRequest& req) { return api.handle(req); }, !a.has("quiet"));
    std::printf("DispatchLab console ready in %.2fs: %u nodes, %u edges (CCH built)\n", secs_since(t0),
                api.graph().num_nodes(), api.graph().num_edges());
    std::printf("listening http://127.0.0.1:%u/  (loopback only; Ctrl+C to stop)\n", server.port());
    std::fflush(stdout);
    server.run();
    return 0;
}

int cmd_perf_report(const Args& a) {
    write_performance_report(a.get("graph-json", "benchmarks/results/graph.json"),
                             a.get("routing-json", "benchmarks/results/routing.json"),
                             a.get("dispatch-json", "benchmarks/results/dispatch.json"),
                             a.get("experiments-md", "experiments/results/policy_summary.md"),
                             a.get("out", "PERFORMANCE.md"));
    std::printf("wrote %s\n", a.get("out", "PERFORMANCE.md").c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        Args args(argc, argv, 2);
        if (cmd == "import") return cmd_import(args);
        if (cmd == "info") return cmd_info(args);
        if (cmd == "route") return cmd_route(args);
        if (cmd == "gen-scenario") return cmd_gen_scenario(args);
        if (cmd == "simulate") return cmd_simulate(args);
        if (cmd == "replay") return cmd_replay(args);
        if (cmd == "check-log") return cmd_check_log(args);
        if (cmd == "experiment") return cmd_experiment(args);
        if (cmd == "check-experiment") return cmd_check_experiment(args);
        if (cmd == "export-viewer") return cmd_export_viewer(args);
        if (cmd == "validate-replay") return cmd_validate_replay(args);
        if (cmd == "perf-report") return cmd_perf_report(args);
        if (cmd == "serve") return cmd_serve(args);
        if (cmd == "help" || cmd == "--help" || cmd == "-h") {
            usage();
            return 0;
        }
        std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
        usage();
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
