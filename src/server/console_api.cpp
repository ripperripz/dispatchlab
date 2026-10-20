#include "dispatchlab/console_api.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <stdexcept>

#include "dispatchlab/cch.hpp"
#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/hash.hpp"
#include "dispatchlab/json.hpp"
#include "dispatchlab/rng.hpp"
#include "dispatchlab/road_model.hpp"
#include "dispatchlab/routing.hpp"
#include "dispatchlab/scc.hpp"
#include "dispatchlab/scenario.hpp"
#include "dispatchlab/simulator.hpp"
#include "dispatchlab/sysinfo.hpp"
#include "dispatchlab/tools.hpp"
#include "dispatchlab/traffic.hpp"

namespace dispatchlab {

namespace {

using Clock = std::chrono::steady_clock;

double us_since(Clock::time_point t0) { return std::chrono::duration<double, std::micro>(Clock::now() - t0).count(); }

std::string read_file_or_empty(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

HttpResponse json_response(std::string body) {
    HttpResponse r;
    r.body = std::move(body);
    r.headers.push_back({"Cache-Control", "no-store"});
    return r;
}

HttpResponse error_response(int status, const std::string& msg) {
    HttpResponse r;
    r.status = status;
    r.body = "{\"error\":\"" + json::escape(msg) + "\"}";
    return r;
}

void write_metrics(json::Writer& w, const SimMetrics& m) {
    w.begin_object();
    w.kv("orders_total", m.orders_total).kv("orders_created", m.orders_created);
    w.kv("orders_delivered", m.orders_delivered).kv("orders_undelivered", m.orders_undelivered);
    w.kv("avg_delivery_min", m.avg_delivery_min).kv("p50_delivery_min", m.p50_delivery_min);
    w.kv("p95_delivery_min", m.p95_delivery_min).kv("max_delivery_min", m.max_delivery_min);
    w.kv("avg_wait_for_courier_min", m.avg_wait_for_courier_min);
    w.kv("courier_utilization", m.courier_utilization).kv("total_distance_km", m.total_distance_km);
    w.kv("dispatch_decisions", m.dispatch_decisions);
    w.kv("assign_latency_mean_us", m.assign_latency_mean_us).kv("assign_latency_p95_us", m.assign_latency_p95_us);
    w.kv("routing_queries", m.routing_queries).kv("events", m.events);
    w.kv("last_event_time", static_cast<std::int64_t>(m.last_event_time));
    w.end_object();
}

// Scenario parameters from a request, with the same defaults as the CLI
// (`gen-scenario`), so a console run reproduces the CLI's hash exactly.
ScenarioParams scenario_params(const HttpRequest& req, std::uint32_t def_couriers, std::uint32_t def_orders) {
    ScenarioParams p;
    p.couriers = static_cast<std::uint32_t>(req.int_param("couriers", def_couriers, 1, 1000));
    p.orders = static_cast<std::uint32_t>(req.int_param("orders", def_orders, 1, 5000));
    p.restaurants = static_cast<std::uint32_t>(req.int_param("restaurants", 0, 0, 500));
    p.order_window_ms = req.int_param("window", 180, 10, 1440) * 60000;
    p.prep_min_ms = req.int_param("prep_min", 8, 0, 120) * 60000;
    p.prep_max_ms = req.int_param("prep_max", 20, 0, 120) * 60000;
    if (p.prep_min_ms > p.prep_max_ms) throw std::invalid_argument("prep_min must not exceed prep_max");
    p.drain_ms = req.int_param("drain", 120, 0, 1440) * 60000;
    p.traffic = std::string(traffic_name(parse_traffic(req.param("traffic", "moderate"))));
    p.seed = static_cast<std::uint64_t>(req.int_param("seed", 42, 0, 2147483647));
    return p;
}

void write_scenario_summary(json::Writer& w, const Scenario& sc) {
    w.begin_object();
    w.kv("couriers", static_cast<std::uint64_t>(sc.couriers.size()));
    w.kv("orders", static_cast<std::uint64_t>(sc.orders.size()));
    w.kv("restaurants", static_cast<std::uint64_t>(sc.restaurants.size()));
    w.kv("traffic", sc.params.traffic).kv("seed", sc.params.seed);
    w.kv("window_min", static_cast<std::int64_t>(sc.params.order_window_ms / 60000));
    w.kv("end_time", static_cast<std::int64_t>(sc.end_time));
    w.kv("scenario_hash", hex64(sc.hash()));
    w.end_object();
}

struct TrafficContext {
    Graph graph;
    std::unique_ptr<CCH> cch;  // prototype; threads query cheap copies sharing its hierarchy
    std::shared_ptr<const UnitVectors> unit;
};

// Per-thread query engines for one traffic context. Each worker thread owns
// its workspaces, so queries run concurrently without locks; the CCH copy
// shares the read-only hierarchy with the prototype.
struct Engines {
    Dijkstra dij;
    BidirectionalDijkstra bi;
    AStar astar;
    CCH cch;
    explicit Engines(const TrafficContext& c)
        : dij(make_dijkstra(c.graph)), bi(c.graph), astar(c.graph, Heuristic::Chord, c.unit), cch(*c.cch) {}
};

Engines& engines_for(const TrafficContext& ctx) {
    // Contexts live for the server's lifetime, so their address is a stable key.
    thread_local std::map<const TrafficContext*, std::unique_ptr<Engines>> local;
    auto& slot = local[&ctx];
    if (!slot) slot = std::make_unique<Engines>(ctx);
    return *slot;
}

}  // namespace

struct ConsoleApi::Impl {
    Graph base;
    ConsoleOptions opts;
    std::string network_json;
    std::unique_ptr<CCH> base_cch;  // topology built once; customized per traffic scenario
    std::mutex mu;
    std::map<std::string, std::shared_ptr<TrafficContext>> traffic;
    std::mutex heavy_mu;  // serializes simulation-sized requests so they do not starve each other

    std::shared_ptr<TrafficContext> traffic_context(const std::string& name) {
        const TrafficScenario t = parse_traffic(name);
        const std::string key(traffic_name(t));
        std::lock_guard<std::mutex> lock(mu);
        auto it = traffic.find(key);
        if (it != traffic.end()) return it->second;
        auto ctx = std::make_shared<TrafficContext>();
        ctx->graph = apply_traffic(base, t, 1);  // routing lab uses traffic seed 1
        ctx->cch = std::make_unique<CCH>(*base_cch);
        ctx->cch->customize(ctx->graph);
        ctx->unit = make_unit_vectors(ctx->graph);
        traffic[key] = ctx;
        return ctx;
    }

    HttpResponse info();
    HttpResponse route(const HttpRequest& req);
    HttpResponse distance(const HttpRequest& req);
    HttpResponse simulate(const HttpRequest& req);
    HttpResponse compare(const HttpRequest& req);
    HttpResponse agreement(const HttpRequest& req);
    HttpResponse determinism(const HttpRequest& req);
    HttpResponse selftest(const HttpRequest& req);
    HttpResponse serve_static(const HttpRequest& req);
};

ConsoleApi::ConsoleApi(Graph base, ConsoleOptions opts) : impl_(std::make_unique<Impl>()) {
    impl_->base = std::move(base);
    impl_->opts = std::move(opts);
    const Graph& g = impl_->base;
    impl_->base_cch = std::make_unique<CCH>(g);

    // Network geometry, built once: same shape as the viewer replay's network fields.
    double min_lon = 1e9, min_lat = 1e9, max_lon = -1e9, max_lat = -1e9;
    for (NodeId v = 0; v < g.num_nodes(); ++v) {
        min_lon = std::min(min_lon, g.lon[v]);
        max_lon = std::max(max_lon, g.lon[v]);
        min_lat = std::min(min_lat, g.lat[v]);
        max_lat = std::max(max_lat, g.lat[v]);
    }
    json::Writer w;
    w.begin_object();
    w.key("bbox").begin_array().value(min_lon, 6).value(min_lat, 6).value(max_lon, 6).value(max_lat, 6).end_array();
    w.key("nodes").begin_array();
    for (NodeId v = 0; v < g.num_nodes(); ++v) w.value(g.lon[v], 5).value(g.lat[v], 5);
    w.end_array();
    std::vector<std::uint8_t> arterial;
    w.key("edges").begin_array();
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (auto e = g.out_begin(u); e < g.out_end(u); ++e) {
            const NodeId v = g.targets[e];
            if (u < v || g.find_edge(v, u) == UINT32_MAX) {
                w.value(u).value(v);
                const auto rc = g.road_class[e];
                arterial.push_back(rc < kRoadClassCount && road_class_info(static_cast<RoadClass>(rc)).arterial ? 1 : 0);
            }
        }
    }
    w.end_array();
    w.key("edge_arterial").begin_array();
    for (auto a : arterial) w.value(static_cast<unsigned>(a));
    w.end_array();
    w.end_object();
    impl_->network_json = w.take();
}

ConsoleApi::~ConsoleApi() = default;

const Graph& ConsoleApi::graph() const { return impl_->base; }

HttpResponse ConsoleApi::handle(const HttpRequest& req) {
    const std::string& p = req.path;
    if (p == "/api/info") return impl_->info();
    if (p == "/api/network") return json_response(impl_->network_json);
    if (p == "/api/route") return impl_->route(req);
    if (p == "/api/distance") return impl_->distance(req);
    if (p == "/api/simulate") return impl_->simulate(req);
    if (p == "/api/compare") return impl_->compare(req);
    if (p == "/api/agreement") return impl_->agreement(req);
    if (p == "/api/determinism") return impl_->determinism(req);
    if (p == "/api/selftest") return impl_->selftest(req);
    if (p == "/api/results") return json_response(build_results_bundle(impl_->opts.results_root));
    if (p.rfind("/api/", 0) == 0) return error_response(404, "unknown endpoint " + p);
    return impl_->serve_static(req);
}

HttpResponse ConsoleApi::Impl::info() {
    const Graph& g = base;
    std::uint64_t len = 0;
    for (auto l : g.lengths_mm) len += l;
    std::vector<std::uint32_t> per_class(kRoadClassCount, 0);
    for (auto c : g.road_class) per_class[c < kRoadClassCount ? c : 0]++;
    const SysInfo si = system_info();
    const CchStats& cs = base_cch->stats();
    json::Writer w;
    w.begin_object();
    w.kv("engine", "DispatchLab").kv("version", "1.0.0");
    w.key("graph").begin_object();
    w.kv("source", g.meta.source).kv("nodes", g.num_nodes()).kv("edges", g.num_edges());
    w.kv("max_speed_kmh", g.meta.max_speed_kmh).kv("total_length_km", static_cast<double>(len) / 1e6, 1);
    w.kv("memory_bytes", static_cast<std::uint64_t>(g.memory_bytes()));
    w.kv("validate", validate(g).empty() ? "ok" : validate(g));
    w.kv("strongly_connected", is_strongly_connected(g));
    w.key("classes").begin_array();
    for (std::size_t c = 0; c < kRoadClassCount; ++c) {
        if (!per_class[c]) continue;
        w.begin_object().kv("class", std::string(kRoadClasses[c].highway)).kv("speed_kmh", kRoadClasses[c].speed_kmh);
        w.kv("edges", per_class[c]).kv("arterial", kRoadClasses[c].arterial).end_object();
    }
    w.end_array();
    w.end_object();
    w.key("cch").begin_object();
    w.kv("order_ms", cs.order_ms).kv("contract_ms", cs.contract_ms).kv("customize_ms", cs.customize_ms);
    w.kv("upward_edges", cs.upward_edges).kv("original_pairs", cs.original_pairs);
    w.kv("max_up_degree", cs.max_up_degree).kv("hierarchy_bytes", static_cast<std::uint64_t>(base_cch->memory_bytes()));
    w.end_object();
    w.key("environment").begin_object();
    w.kv("cpu", si.cpu).kv("os", si.os).kv("logical_cores", si.logical_cores);
    w.kv("compiler", si.compiler).kv("build_type", si.build_type);
    w.end_object();
    w.key("algorithms").begin_array().value("dijkstra").value("bidirectional").value("astar").value("cch").end_array();
    w.key("policies").begin_array().value("nearest").value("greedy_eta").end_array();
    w.key("traffic").begin_array().value("free").value("moderate").value("heavy").end_array();
    w.end_object();
    return json_response(w.take());
}

HttpResponse ConsoleApi::Impl::route(const HttpRequest& req) {
    const std::int64_t n = base.num_nodes();
    const auto s = static_cast<NodeId>(req.int_param("s", 0, 0, n - 1));
    const auto t = static_cast<NodeId>(req.int_param("t", 0, 0, n - 1));
    const bool want_trace = req.param("trace", "1") != "0";
    auto ctx = traffic_context(req.param("traffic", "free"));
    const Graph& g = ctx->graph;
    Engines& eng = engines_for(*ctx);
    struct Run {
        const char* id;
        const char* label;
        RouteResult result;
        double latency_us = 0;
        std::vector<NodeId> trace;
    };
    Run runs[4] = {{"dijkstra", "Dijkstra", {}, 0, {}},
                   {"bidirectional", "Bidirectional", {}, 0, {}},
                   {"astar", "A*", {}, 0, {}},
                   {"cch", "CCH", {}, 0, {}}};
    auto query = [&](int a, std::vector<NodeId>* trace) -> RouteResult {
        switch (a) {
            case 0: return eng.dij.query(s, t, true, trace);
            case 1: return eng.bi.query(s, t, true, trace);
            case 2: return eng.astar.query(s, t, true, trace);
            default: return eng.cch.query(s, t, true, trace);
        }
    };
    for (int a = 0; a < 4; ++a) {
        // Latency: median of three untraced runs (with path reconstruction).
        double lat[3];
        for (double& x : lat) {
            const auto t0 = Clock::now();
            runs[a].result = query(a, nullptr);
            x = us_since(t0);
        }
        std::sort(lat, lat + 3);
        runs[a].latency_us = lat[1];
        if (want_trace) query(a, &runs[a].trace);
    }
    const Dist ref = runs[0].result.cost;
    bool agree = true;
    for (const auto& r : runs) {
        agree = agree && r.result.cost == ref && (!r.result.reachable() || path_cost(g, r.result.path) == r.result.cost);
    }

    json::Writer w;
    w.begin_object();
    w.kv("s", s).kv("t", t).kv("traffic", std::string(traffic_name(parse_traffic(req.param("traffic", "free")))));
    w.kv("reachable", runs[0].result.reachable()).kv("agree", agree);
    w.kv("cost_ms", ref == kInfDist ? static_cast<std::int64_t>(-1) : static_cast<std::int64_t>(ref));
    w.kv("length_m", runs[0].result.reachable() ? static_cast<double>(path_length_mm(g, runs[0].result.path)) / 1000.0 : 0.0, 1);
    w.key("path").begin_array();
    for (NodeId v : runs[0].result.path) w.value(v);
    w.end_array();
    w.key("results").begin_array();
    for (const auto& r : runs) {
        w.begin_object();
        w.kv("algorithm", r.id).kv("label", r.label);
        w.kv("cost_ms", r.result.reachable() ? static_cast<std::int64_t>(r.result.cost) : static_cast<std::int64_t>(-1));
        w.kv("settled", r.result.stats.settled).kv("relaxed", r.result.stats.relaxed);
        w.kv("latency_us", r.latency_us, 1);
        w.kv("path_nodes", static_cast<std::uint64_t>(r.result.path.size()));
        w.kv("path_cost_ok", !r.result.reachable() || path_cost(g, r.result.path) == r.result.cost);
        w.key("trace").begin_array();
        for (NodeId v : r.trace) w.value(v);
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return json_response(w.take());
}

// One shortest-path query with one algorithm, no path payload: the endpoint the
// load benchmark drives. Lock-free (thread-local engines).
HttpResponse ConsoleApi::Impl::distance(const HttpRequest& req) {
    const std::int64_t n = base.num_nodes();
    const auto s = static_cast<NodeId>(req.int_param("s", 0, 0, n - 1));
    const auto t = static_cast<NodeId>(req.int_param("t", 0, 0, n - 1));
    const std::string algo = req.param("algo", "cch");
    auto ctx = traffic_context(req.param("traffic", "free"));
    Engines& eng = engines_for(*ctx);
    const auto t0 = Clock::now();
    RouteResult r;
    if (algo == "cch") r = eng.cch.query(s, t, false);
    else if (algo == "astar") r = eng.astar.query(s, t, false);
    else if (algo == "bidirectional") r = eng.bi.query(s, t, false);
    else if (algo == "dijkstra") r = eng.dij.query(s, t, false);
    else throw std::invalid_argument("algo must be dijkstra, bidirectional, astar or cch");
    const double us = us_since(t0);
    json::Writer w;
    w.begin_object().kv("s", s).kv("t", t).kv("algo", algo);
    w.kv("cost_ms", r.reachable() ? static_cast<std::int64_t>(r.cost) : static_cast<std::int64_t>(-1));
    w.kv("settled", r.stats.settled).kv("latency_us", us, 1).end_object();
    return json_response(w.take());
}

HttpResponse ConsoleApi::Impl::simulate(const HttpRequest& req) {
    const ScenarioParams p = scenario_params(req, 40, 300);
    const std::string policy_name = req.param("policy", "greedy_eta");
    const Algorithm algo = parse_algorithm(req.param("algo", "astar"));
    std::lock_guard<std::mutex> heavy(heavy_mu);
    const Scenario sc = generate_scenario(base, p);
    const Graph tg = apply_traffic(base, parse_traffic(sc.params.traffic), sc.params.seed);  // as `simulate`
    auto policy = make_policy(policy_name, tg);
    SimOptions so;
    so.log_paths = true;
    Simulator sim(tg, sc, *policy, algo, so);
    const auto t0 = Clock::now();
    SimResult r = sim.run();
    const double wall_ms = us_since(t0) / 1000.0;
    const ReplayResult rep = replay(sc, r.log);
    ViewerMeta meta{std::string(policy->name()), std::string(algorithm_name(algo))};
    const std::string doc = build_viewer_replay(base, sc, r.log, meta, /*include_network=*/false);

    json::Writer w;
    w.begin_object();
    w.kv("policy", policy->name()).kv("routing", algorithm_name(algo));
    w.key("scenario");
    write_scenario_summary(w, sc);
    w.key("metrics");
    write_metrics(w, r.metrics);
    w.kv("final_state_hash", hex64(r.final_state_hash));
    w.kv("replay_state_hash", hex64(rep.final_state_hash));
    w.kv("replay_verified", rep.final_state_hash == r.final_state_hash);
    w.kv("sim_wall_ms", wall_ms, 1);
    w.key("replay").raw(doc);
    w.end_object();
    return json_response(w.take());
}

HttpResponse ConsoleApi::Impl::compare(const HttpRequest& req) {
    const ScenarioParams p = scenario_params(req, 40, 300);
    const Algorithm algo = parse_algorithm(req.param("algo", "astar"));
    std::lock_guard<std::mutex> heavy(heavy_mu);
    const Scenario sc = generate_scenario(base, p);  // ONE scenario shared by both policies
    const Graph tg = apply_traffic(base, parse_traffic(sc.params.traffic), sc.params.seed);
    json::Writer w;
    w.begin_object();
    w.key("scenario");
    write_scenario_summary(w, sc);
    w.key("runs").begin_array();
    for (const char* name : {"nearest", "greedy_eta"}) {
        auto policy = make_policy(name, tg);
        Simulator sim(tg, sc, *policy, algo);
        const auto t0 = Clock::now();
        SimResult r = sim.run();
        const double wall_ms = us_since(t0) / 1000.0;
        w.begin_object();
        w.kv("policy", name).kv("final_state_hash", hex64(r.final_state_hash)).kv("sim_wall_ms", wall_ms, 1);
        w.key("metrics");
        write_metrics(w, r.metrics);
        // Per-order outcomes (minutes) for distributions and cumulative curves.
        w.key("delivery_min").begin_array();
        for (const auto& o : r.final_state.orders) {
            if (o.status == OrderStatus::Delivered) w.value(static_cast<double>(o.delivered_at - o.created_at) / 60000.0, 2);
        }
        w.end_array();
        w.key("delivered_at_min").begin_array();
        std::vector<SimTime> times;
        for (const auto& o : r.final_state.orders) {
            if (o.status == OrderStatus::Delivered) times.push_back(o.delivered_at);
        }
        std::sort(times.begin(), times.end());
        for (SimTime x : times) w.value(static_cast<double>(x) / 60000.0, 2);
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return json_response(w.take());
}

HttpResponse ConsoleApi::Impl::agreement(const HttpRequest& req) {
    const auto queries = static_cast<std::uint32_t>(req.int_param("queries", 300, 1, 5000));
    const auto seed = static_cast<std::uint64_t>(req.int_param("seed", 20261018, 0, 2147483647));
    auto ctx = traffic_context(req.param("traffic", "free"));
    const Graph& g = ctx->graph;
    std::lock_guard<std::mutex> heavy(heavy_mu);
    Engines& eng = engines_for(*ctx);
    Rng rng(seed);
    std::vector<double> lat[4], settled[4];
    std::uint32_t failures = 0;
    Fnv1a64 digest;
    json::Writer mism;
    mism.begin_array();
    const auto t_all = Clock::now();
    for (std::uint32_t i = 0; i < queries; ++i) {
        const auto s = static_cast<NodeId>(rng.below(g.num_nodes()));
        const auto t = static_cast<NodeId>(rng.below(g.num_nodes()));
        RouteResult r[4];
        auto t0 = Clock::now();
        r[0] = eng.dij.query(s, t);
        lat[0].push_back(us_since(t0));
        t0 = Clock::now();
        r[1] = eng.bi.query(s, t);
        lat[1].push_back(us_since(t0));
        t0 = Clock::now();
        r[2] = eng.astar.query(s, t);
        lat[2].push_back(us_since(t0));
        t0 = Clock::now();
        r[3] = eng.cch.query(s, t);
        lat[3].push_back(us_since(t0));
        bool ok = true;
        for (int a = 0; a < 4; ++a) {
            settled[a].push_back(r[a].stats.settled);
            ok = ok && r[a].cost == r[0].cost && (!r[a].reachable() || path_cost(g, r[a].path) == r[a].cost);
        }
        digest.add_u64(r[0].cost);
        if (!ok) {
            if (++failures <= 10) {
                mism.begin_object().kv("s", s).kv("t", t);
                mism.kv("dijkstra", r[0].cost).kv("bidirectional", r[1].cost).kv("astar", r[2].cost).kv("cch", r[3].cost);
                mism.end_object();
            }
        }
    }
    mism.end_array();
    const char* names[4] = {"dijkstra", "bidirectional", "astar", "cch"};
    json::Writer w;
    w.begin_object();
    w.kv("queries", queries).kv("seed", seed).kv("failures", failures);
    w.kv("traffic", std::string(traffic_name(parse_traffic(req.param("traffic", "free")))));
    w.kv("cost_digest", hex64(digest.digest())).kv("wall_ms", us_since(t_all) / 1000.0, 1);
    w.key("mismatches").raw(mism.str());
    w.key("algorithms").begin_array();
    for (int a = 0; a < 4; ++a) {
        w.begin_object().kv("algorithm", names[a]);
        w.kv("median_us", percentile(lat[a], 50), 1).kv("p95_us", percentile(lat[a], 95), 1);
        w.kv("mean_settled", mean(settled[a]), 0).end_object();
    }
    w.end_array();
    w.end_object();
    return json_response(w.take());
}

HttpResponse ConsoleApi::Impl::determinism(const HttpRequest& req) {
    const ScenarioParams p = scenario_params(req, 20, 150);
    const std::string policy_name = req.param("policy", "greedy_eta");
    std::lock_guard<std::mutex> heavy(heavy_mu);
    auto run_once = [&](const ScenarioParams& params, std::string* log_text) {
        const Scenario sc = generate_scenario(base, params);
        const Graph tg = apply_traffic(base, parse_traffic(sc.params.traffic), sc.params.seed);
        auto policy = make_policy(policy_name, tg);
        Simulator sim(tg, sc, *policy, Algorithm::kAStar);
        SimResult r = sim.run();
        if (log_text) {
            for (const auto& e : r.log) *log_text += log_event_to_json(e, false) + "\n";
        }
        return std::make_pair(sc, r);
    };
    std::string log1, log2;
    auto [sc1, r1] = run_once(p, &log1);
    auto [sc2, r2] = run_once(p, &log2);
    // Replay from the serialized text only (no routing, no dispatch).
    std::vector<LogEvent> parsed;
    std::size_t start = 0;
    while (start < log1.size()) {
        const std::size_t nl = log1.find('\n', start);
        parsed.push_back(log_event_from_json(std::string_view(log1).substr(start, nl - start)));
        start = nl + 1;
    }
    const std::uint64_t routing_before = routing_call_counter().load();
    const ReplayResult rep = replay(sc1, parsed);
    const bool replay_pure = routing_call_counter().load() == routing_before;
    ScenarioParams other = p;
    other.seed = p.seed + 1;
    auto [sc3, r3] = run_once(other, nullptr);
    json::Writer w;
    w.begin_object();
    w.key("scenario");
    write_scenario_summary(w, sc1);
    w.kv("policy", policy_name);
    w.kv("events", static_cast<std::uint64_t>(r1.log.size()));
    w.kv("log_bytes", static_cast<std::uint64_t>(log1.size()));
    w.kv("log_digest_run1", hex64(fnv1a64(log1))).kv("log_digest_run2", hex64(fnv1a64(log2)));
    w.kv("logs_identical", log1 == log2);
    w.kv("hash_run1", hex64(r1.final_state_hash)).kv("hash_run2", hex64(r2.final_state_hash));
    w.kv("replay_hash", hex64(rep.final_state_hash)).kv("replay_routing_free", replay_pure);
    w.kv("other_seed", other.seed).kv("hash_other_seed", hex64(r3.final_state_hash));
    w.kv("deterministic", log1 == log2 && r1.final_state_hash == r2.final_state_hash &&
                              rep.final_state_hash == r1.final_state_hash &&
                              r3.final_state_hash != r1.final_state_hash);
    w.end_object();
    return json_response(w.take());
}

HttpResponse ConsoleApi::Impl::selftest(const HttpRequest& req) {
    const ScenarioParams p = scenario_params(req, 20, 150);
    std::lock_guard<std::mutex> heavy(heavy_mu);
    const Scenario sc = generate_scenario(base, p);
    const Graph tg = apply_traffic(base, parse_traffic(sc.params.traffic), sc.params.seed);
    auto policy = make_policy(req.param("policy", "nearest"), tg);
    Simulator sim(tg, sc, *policy, Algorithm::kAStar);
    const SimResult r = sim.run();
    const auto bad = make_corrupted_logs(r.log);
    json::Writer w;
    w.begin_object();
    w.kv("events", static_cast<std::uint64_t>(r.log.size()));
    w.key("cases").begin_array();
    std::uint32_t detected = 0;
    for (const auto& [kind, log] : bad) {
        std::string msg;
        bool caught = false;
        try {
            replay(sc, log);
        } catch (const InvariantViolation& e) {
            caught = true;
            msg = e.what();
        }
        detected += caught ? 1 : 0;
        w.begin_object().kv("kind", kind).kv("detected", caught).kv("message", msg).end_object();
    }
    w.end_array();
    w.kv("detected", detected).kv("total", static_cast<std::uint64_t>(bad.size()));
    w.end_object();
    return json_response(w.take());
}

HttpResponse ConsoleApi::Impl::serve_static(const HttpRequest& req) {
    std::string rel = req.path == "/" ? "/index.html" : req.path;
    if (rel.find("..") != std::string::npos || rel.find('\\') != std::string::npos || rel.find('\0') != std::string::npos) {
        return error_response(400, "invalid path");
    }
    const std::string body = read_file_or_empty(opts.static_root + rel);
    if (body.empty()) return error_response(404, "not found: " + rel);
    HttpResponse r;
    r.content_type = mime_type_for(rel);
    r.body = body;
    r.headers.push_back({"Cache-Control", "no-cache"});
    return r;
}

std::string build_results_bundle(const std::string& root) {
    auto raw = [&](const std::string& rel) {
        const std::string s = read_file_or_empty(root + "/" + rel);
        if (s.empty()) return std::string("null");
        try {
            json::parse(s);  // only embed well-formed JSON
        } catch (const std::exception&) {
            return std::string("null");
        }
        return s;
    };
    json::Writer w;
    w.begin_object();
    w.key("benchmarks").begin_object();
    w.key("graph").raw(raw("benchmarks/results/graph.json"));
    w.key("routing").raw(raw("benchmarks/results/routing.json"));
    w.key("dispatch").raw(raw("benchmarks/results/dispatch.json"));
    w.end_object();
    w.key("experiments").begin_object();
    w.key("verify").raw(raw("experiments/results/policy_comparison.json"));
    w.key("full").raw(raw("experiments/results/full/policy_comparison.json"));
    w.end_object();
    w.end_object();
    return w.take();
}

}  // namespace dispatchlab
