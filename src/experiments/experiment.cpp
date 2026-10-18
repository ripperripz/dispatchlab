#include "dispatchlab/experiment.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/hash.hpp"
#include "dispatchlab/json.hpp"
#include "dispatchlab/scenario.hpp"
#include "dispatchlab/simulator.hpp"
#include "dispatchlab/sysinfo.hpp"
#include "dispatchlab/traffic.hpp"

namespace dispatchlab {

ExperimentConfig load_experiment_config(const std::string& path) {
    const json::Value v = json::parse_file(path);
    ExperimentConfig c;
    c.id = v.get_string("experiment_id", "experiment");
    for (const auto& s : v.at("sizes").as_array()) {
        c.sizes.push_back({s.at("name").as_string(), static_cast<std::uint32_t>(s.at("couriers").as_int()),
                           static_cast<std::uint32_t>(s.at("orders").as_int()), s.at("window_min").as_int() * 60000});
    }
    for (const auto& t : v.at("traffic").as_array()) {
        parse_traffic(t.as_string());  // validate
        c.traffic.push_back(t.as_string());
    }
    for (const auto& s : v.at("seeds").as_array()) c.seeds.push_back(static_cast<std::uint64_t>(s.as_int()));
    if (const json::Value* p = v.find("policies")) {
        c.policies.clear();
        for (const auto& x : p->as_array()) c.policies.push_back(x.as_string());
    }
    c.routing = parse_algorithm(v.get_string("routing_algorithm", "astar"));
    c.prep_min_ms = v.get_int("prep_min", 8) * 60000;
    c.prep_max_ms = v.get_int("prep_max", 20) * 60000;
    c.drain_ms = v.get_int("drain_min", 120) * 60000;
    c.max_delivery_km = v.get_double("max_delivery_km", 6.0);
    c.threads = static_cast<unsigned>(v.get_int("threads", 0));
    if (c.sizes.empty() || c.traffic.empty() || c.seeds.empty() || c.policies.size() < 2) {
        throw std::invalid_argument("experiment config needs sizes, traffic, seeds and >= 2 policies");
    }
    return c;
}

std::vector<ExperimentRow> run_experiment(const Graph& base, const ExperimentConfig& cfg,
                                          const std::function<void(const std::string&)>& progress) {
    struct Cell {
        std::size_t size_idx, traffic_idx, seed_idx;
    };
    std::vector<Cell> cells;
    for (std::size_t a = 0; a < cfg.sizes.size(); ++a)
        for (std::size_t b = 0; b < cfg.traffic.size(); ++b)
            for (std::size_t c = 0; c < cfg.seeds.size(); ++c) cells.push_back({a, b, c});

    std::vector<std::vector<ExperimentRow>> out(cells.size());
    std::atomic<std::size_t> next{0};
    std::mutex mu;
    std::exception_ptr error;

    auto worker = [&] {
        while (true) {
            const std::size_t i = next.fetch_add(1);
            if (i >= cells.size()) return;
            try {
                const ExperimentSize& sz = cfg.sizes[cells[i].size_idx];
                const std::string& traffic = cfg.traffic[cells[i].traffic_idx];
                const std::uint64_t seed = cfg.seeds[cells[i].seed_idx];

                // The workload is generated ONCE per cell and shared by all policies.
                ScenarioParams p;
                p.couriers = sz.couriers;
                p.orders = sz.orders;
                p.order_window_ms = sz.window_ms;
                p.prep_min_ms = cfg.prep_min_ms;
                p.prep_max_ms = cfg.prep_max_ms;
                p.drain_ms = cfg.drain_ms;
                p.max_delivery_km = cfg.max_delivery_km;
                p.traffic = traffic;
                p.seed = seed;
                const Scenario sc = generate_scenario(base, p);
                const Graph tg = apply_traffic(base, parse_traffic(traffic), seed);

                for (const std::string& policy_name : cfg.policies) {
                    auto policy = make_policy(policy_name, tg);
                    Simulator sim(tg, sc, *policy, cfg.routing);
                    const auto t0 = std::chrono::steady_clock::now();
                    SimResult r = sim.run();
                    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    ExperimentRow row;
                    row.experiment_id = cfg.id;
                    row.size = sz.name;
                    row.traffic = traffic;
                    row.policy = policy_name;
                    row.routing = std::string(algorithm_name(cfg.routing));
                    row.couriers = sz.couriers;
                    row.orders = sz.orders;
                    row.seed = seed;
                    row.scenario_hash = sc.hash();
                    row.final_state_hash = r.final_state_hash;
                    row.completed = r.metrics.orders_delivered;
                    row.undelivered = r.metrics.orders_undelivered;
                    row.avg_delivery_min = r.metrics.avg_delivery_min;
                    row.p50_delivery_min = r.metrics.p50_delivery_min;
                    row.p95_delivery_min = r.metrics.p95_delivery_min;
                    row.avg_wait_min = r.metrics.avg_wait_for_courier_min;
                    row.utilization = r.metrics.courier_utilization;
                    row.total_distance_km = r.metrics.total_distance_km;
                    row.dispatch_decisions = r.metrics.dispatch_decisions;
                    row.routing_queries = r.metrics.routing_queries;
                    row.events = r.metrics.events;
                    row.assign_latency_mean_us = r.metrics.assign_latency_mean_us;
                    row.assign_latency_p95_us = r.metrics.assign_latency_p95_us;
                    row.sim_wall_s = wall;
                    out[i].push_back(row);
                    if (progress) {
                        std::lock_guard<std::mutex> lock(mu);
                        char buf[256];
                        std::snprintf(buf, sizeof buf, "%s/%s/seed=%llu/%s: delivered=%u/%u avg=%.2f min wall=%.2fs",
                                      sz.name.c_str(), traffic.c_str(), static_cast<unsigned long long>(seed),
                                      policy_name.c_str(), row.completed, row.orders, row.avg_delivery_min, wall);
                        progress(buf);
                    }
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(mu);
                if (!error) error = std::current_exception();
                next.store(cells.size());
                return;
            }
        }
    };
    unsigned threads = cfg.threads ? cfg.threads : std::max(1u, std::thread::hardware_concurrency());
    threads = std::min<unsigned>(threads, static_cast<unsigned>(cells.size()));
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    if (error) std::rethrow_exception(error);

    std::vector<ExperimentRow> rows;
    for (auto& v : out) rows.insert(rows.end(), v.begin(), v.end());
    return rows;
}

namespace {

const char* kCsvHeader =
    "experiment_id,size,couriers,orders,traffic,seed,policy,routing,scenario_hash,final_state_hash,"
    "completed,undelivered,avg_delivery_min,p50_delivery_min,p95_delivery_min,avg_wait_min,utilization,"
    "total_distance_km,dispatch_decisions,routing_queries,events,assign_latency_mean_us,assign_latency_p95_us,"
    "sim_wall_s";

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::string fmt(double v, int prec = 3) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", prec, v);
    return buf;
}

}  // namespace

void write_experiment_results(const std::vector<ExperimentRow>& rows, const ExperimentConfig& cfg,
                              const Graph& base, double total_wall_s, const std::string& out_dir) {
    // CSV
    {
        std::ostringstream os;
        os << kCsvHeader << "\n";
        for (const auto& r : rows) {
            os << r.experiment_id << ',' << r.size << ',' << r.couriers << ',' << r.orders << ',' << r.traffic << ','
               << r.seed << ',' << r.policy << ',' << r.routing << ',' << hex64(r.scenario_hash) << ','
               << hex64(r.final_state_hash) << ',' << r.completed << ',' << r.undelivered << ','
               << fmt(r.avg_delivery_min) << ',' << fmt(r.p50_delivery_min) << ',' << fmt(r.p95_delivery_min) << ','
               << fmt(r.avg_wait_min) << ',' << fmt(r.utilization, 4) << ',' << fmt(r.total_distance_km) << ','
               << r.dispatch_decisions << ',' << r.routing_queries << ',' << r.events << ','
               << fmt(r.assign_latency_mean_us, 1) << ',' << fmt(r.assign_latency_p95_us, 1) << ','
               << fmt(r.sim_wall_s, 3) << "\n";
        }
        json::write_file(out_dir + "/policy_comparison.csv", os.str());
    }

    const SysInfo si = system_info();
    // JSON with reproducibility metadata (PRD §14.4).
    {
        json::Writer w;
        w.begin_object();
        w.kv("experiment_id", cfg.id);
        w.kv("generated_utc", si.timestamp_utc);
        w.key("dataset").begin_object();
        w.kv("source", base.meta.source);
        w.kv("nodes", base.num_nodes());
        w.kv("edges", base.num_edges());
        w.kv("max_speed_kmh", base.meta.max_speed_kmh);
        w.end_object();
        w.key("graph_build").begin_object();
        w.kv("weights", "ceil(haversine_m * 3600 / class_speed_kmh) ms; largest SCC; see docs/DATA.md");
        w.end_object();
        w.kv("routing_algorithm", algorithm_name(cfg.routing));
        w.key("policies").begin_array();
        for (const auto& p : cfg.policies) w.value(p);
        w.end_array();
        w.key("seeds").begin_array();
        for (auto s : cfg.seeds) w.value(s);
        w.end_array();
        w.key("scenario_parameters").begin_object();
        w.key("sizes").begin_array();
        for (const auto& s : cfg.sizes) {
            w.begin_object().kv("name", s.name).kv("couriers", s.couriers).kv("orders", s.orders);
            w.kv("window_min", static_cast<std::int64_t>(s.window_ms / 60000)).end_object();
        }
        w.end_array();
        w.key("traffic").begin_array();
        for (const auto& t : cfg.traffic) w.value(t);
        w.end_array();
        w.kv("prep_min", static_cast<std::int64_t>(cfg.prep_min_ms / 60000));
        w.kv("prep_max", static_cast<std::int64_t>(cfg.prep_max_ms / 60000));
        w.kv("drain_min", static_cast<std::int64_t>(cfg.drain_ms / 60000));
        w.kv("max_delivery_km", cfg.max_delivery_km);
        w.end_object();
        w.key("environment").begin_object();
        w.kv("cpu", si.cpu).kv("os", si.os).kv("logical_cores", si.logical_cores);
        w.kv("memory_bytes", si.memory_bytes).kv("compiler", si.compiler).kv("build_type", si.build_type);
        w.kv("cxx_flags", si.cxx_flags);
        w.end_object();
        w.kv("execution_time_s", total_wall_s);
        w.key("results").begin_array();
        for (const auto& r : rows) {
            w.begin_object();
            w.kv("size", r.size).kv("couriers", r.couriers).kv("orders", r.orders).kv("traffic", r.traffic);
            w.kv("seed", r.seed).kv("policy", r.policy).kv("scenario_hash", hex64(r.scenario_hash));
            w.kv("final_state_hash", hex64(r.final_state_hash)).kv("completed", r.completed);
            w.kv("undelivered", r.undelivered).kv("avg_delivery_min", r.avg_delivery_min);
            w.kv("p95_delivery_min", r.p95_delivery_min).kv("avg_wait_min", r.avg_wait_min);
            w.kv("utilization", r.utilization).kv("total_distance_km", r.total_distance_km);
            w.kv("assign_latency_mean_us", r.assign_latency_mean_us);
            w.kv("assign_latency_p95_us", r.assign_latency_p95_us);
            w.kv("routing_queries", r.routing_queries).kv("sim_wall_s", r.sim_wall_s);
            w.end_object();
        }
        w.end_array();
        w.end_object();
        json::write_file(out_dir + "/policy_comparison.json", w.str());
    }

    // Markdown summary with paired statistics. Differences are B - A where A is
    // the first policy (baseline) and B each other policy, per seed.
    {
        std::ostringstream md;
        md << "# Policy comparison: " << cfg.id << "\n\n";
        md << "Generated " << si.timestamp_utc << " on " << si.cpu << " (" << si.compiler << ", " << si.build_type
           << "). Dataset: " << base.meta.source << " (" << base.num_nodes() << " nodes). Routing: "
           << algorithm_name(cfg.routing) << ". Seeds: ";
        for (std::size_t i = 0; i < cfg.seeds.size(); ++i) md << (i ? ", " : "") << cfg.seeds[i];
        md << ".\n\nEvery (size, traffic, seed) cell uses one pre-generated scenario shared by all policies "
              "(identical orders, locations, preparation times, courier starts, traffic). Values are mean ± sample "
              "std. dev. across seeds. \"Wins\" counts seeds where the policy's value is strictly better "
              "(lower; higher for completed orders).\n\n";
        const std::string& a = cfg.policies[0];
        for (const auto& sz : cfg.sizes) {
            for (const auto& tr : cfg.traffic) {
                md << "## " << sz.name << " (" << sz.couriers << " couriers, " << sz.orders << " orders), traffic="
                   << tr << "\n\n";
                std::map<std::string, std::vector<const ExperimentRow*>> by_policy;
                for (const auto& r : rows) {
                    if (r.size == sz.name && r.traffic == tr) by_policy[r.policy].push_back(&r);
                }
                struct Metric {
                    const char* name;
                    double ExperimentRow::*field;
                    bool higher_better;
                };
                const Metric metrics[] = {
                    {"avg delivery (min)", &ExperimentRow::avg_delivery_min, false},
                    {"p95 delivery (min)", &ExperimentRow::p95_delivery_min, false},
                    {"avg wait for courier (min)", &ExperimentRow::avg_wait_min, false},
                    {"courier utilization", &ExperimentRow::utilization, false},
                    {"total distance (km)", &ExperimentRow::total_distance_km, false},
                    {"assignment latency mean (us)", &ExperimentRow::assign_latency_mean_us, false},
                };
                md << "| metric |";
                for (const auto& p : cfg.policies) md << ' ' << p << " |";
                for (std::size_t k = 1; k < cfg.policies.size(); ++k) {
                    md << ' ' << cfg.policies[k] << " - " << a << " (paired) | " << cfg.policies[k] << " wins |";
                }
                md << "\n|---|";
                for (std::size_t k = 0; k < cfg.policies.size(); ++k) md << "---|";
                for (std::size_t k = 1; k < cfg.policies.size(); ++k) md << "---|---|";
                md << "\n";
                auto values = [&](const std::string& p, auto getter) {
                    std::vector<double> v;
                    for (const auto* r : by_policy[p]) v.push_back(getter(*r));
                    return v;
                };
                auto emit_row = [&](const char* name, auto getter, bool higher_better) {
                    md << "| " << name << " |";
                    for (const auto& p : cfg.policies) {
                        const auto v = values(p, getter);
                        md << ' ' << fmt(mean(v), 2) << " ± " << fmt(stddev(v), 2) << " |";
                    }
                    const auto va = values(a, getter);
                    for (std::size_t k = 1; k < cfg.policies.size(); ++k) {
                        const auto vb = values(cfg.policies[k], getter);
                        std::vector<double> diff;
                        int wins = 0;
                        for (std::size_t i = 0; i < std::min(va.size(), vb.size()); ++i) {
                            diff.push_back(vb[i] - va[i]);
                            if (higher_better ? vb[i] > va[i] : vb[i] < va[i]) ++wins;
                        }
                        md << ' ' << fmt(mean(diff), 2) << " ± " << fmt(stddev(diff), 2) << " | " << wins << "/"
                           << diff.size() << " |";
                    }
                    md << "\n";
                };
                emit_row("completed orders", [](const ExperimentRow& r) { return static_cast<double>(r.completed); }, true);
                emit_row("undelivered orders", [](const ExperimentRow& r) { return static_cast<double>(r.undelivered); }, false);
                for (const auto& m : metrics) {
                    emit_row(m.name, [&](const ExperimentRow& r) { return r.*(m.field); }, m.higher_better);
                }
                md << "\n";
            }
        }
        md << "Courier utilization = busy time / (couriers × simulated horizon); lower is not automatically "
              "better (it also falls when fewer orders are completed). Assignment latency is wall-clock time "
              "and is the only metric that varies between identical runs.\n";
        json::write_file(out_dir + "/policy_summary.md", md.str());
    }
}

std::string check_experiment_csv(const std::string& path, std::string* report) {
    std::ifstream in(path);
    if (!in) return "cannot open " + path;
    std::string line;
    std::getline(in, line);
    if (line != kCsvHeader) return "unexpected CSV header";
    const auto header = split_csv(line);
    auto col = [&](const char* name) {
        for (std::size_t i = 0; i < header.size(); ++i) {
            if (header[i] == name) return i;
        }
        throw std::logic_error("missing column");
    };
    const std::size_t c_size = col("size"), c_traffic = col("traffic"), c_seed = col("seed"), c_policy = col("policy"),
                      c_hash = col("scenario_hash"), c_orders = col("orders"), c_done = col("completed"),
                      c_undel = col("undelivered");
    struct CellInfo {
        std::set<std::string> policies;
        std::set<std::string> hashes;
        std::size_t rows = 0;
    };
    std::map<std::string, CellInfo> cells;
    std::map<std::string, std::set<std::string>> seeds_per_group;
    std::set<std::string> all_policies;
    std::size_t nrows = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto f = split_csv(line);
        if (f.size() != header.size()) return "malformed CSV row";
        ++nrows;
        const std::string cell = f[c_size] + "/" + f[c_traffic] + "/" + f[c_seed];
        auto& ci = cells[cell];
        if (!ci.policies.insert(f[c_policy]).second) return "policy repeated in cell " + cell;
        ci.hashes.insert(f[c_hash]);
        ++ci.rows;
        all_policies.insert(f[c_policy]);
        seeds_per_group[f[c_size] + "/" + f[c_traffic]].insert(f[c_seed]);
        if (std::stoul(f[c_done]) + std::stoul(f[c_undel]) != std::stoul(f[c_orders])) {
            return "completed + undelivered != orders in cell " + cell;
        }
    }
    if (nrows == 0) return "no rows";
    if (all_policies.size() < 2) return "fewer than two policies";
    if (!all_policies.count("nearest") || !all_policies.count("greedy_eta")) return "mandatory policies missing";
    for (const auto& [cell, ci] : cells) {
        if (ci.policies != all_policies) return "cell " + cell + " lacks a policy";
        if (ci.hashes.size() != 1) return "cell " + cell + " has differing scenario hashes";
    }
    for (const auto& [group, seeds] : seeds_per_group) {
        if (seeds.size() < 2) return "group " + group + " has fewer than 2 paired seeds";
    }
    if (report) {
        std::ostringstream os;
        os << "cells=" << cells.size() << " rows=" << nrows << " policies=" << all_policies.size()
           << " groups=" << seeds_per_group.size() << " seeds_per_group=" << seeds_per_group.begin()->second.size();
        *report = os.str();
    }
    return {};
}

}  // namespace dispatchlab
