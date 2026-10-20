// Research benchmark (docs/RESEARCH.md): decision-aware Greedy ETA dispatch.
//  1. micro: per-decision cost of each exact strategy across courier counts,
//     in two regimes (food not ready yet: dispatch at order time; food ready:
//     queued orders), identical choices asserted on every sample;
//  2. system: full simulations, every strategy on one shared scenario per cell;
//     identical final state hashes are required (the decisions are the same).
//
// usage: benchmark_eta <graph> [--samples 200] [--seeds 3] [--sizes small,medium,large]
//                      [--couriers 20,100,500] [--strategies full,pruned,cch,bounded] [--out f.json]

#include <cstdio>
#include <sstream>

#include "bench_common.hpp"
#include "dispatchlab/cch.hpp"
#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/geo.hpp"
#include "dispatchlab/hash.hpp"
#include "dispatchlab/rng.hpp"
#include "dispatchlab/simulator.hpp"
#include "dispatchlab/traffic.hpp"

using namespace dispatchlab;

namespace {

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, ',')) if (!x.empty()) out.push_back(x);
    return out;
}

EtaStrategy parse_strategy(const std::string& s) {
    if (s == "full") return EtaStrategy::Full;
    if (s == "pruned") return EtaStrategy::Pruned;
    if (s == "cch") return EtaStrategy::Cch;
    if (s == "bounded") return EtaStrategy::Bounded;
    if (s == "adaptive") return EtaStrategy::Adaptive;
    if (s == "rphast") return EtaStrategy::Rphast;
    if (s == "hybrid") return EtaStrategy::Hybrid;
    throw std::invalid_argument("unknown strategy " + s);
}

struct SizeSpec {
    std::string name;
    std::uint32_t couriers, orders;
    SimTime window_ms;
};

}  // namespace

int main(int argc, char** argv) {
    bench::Args args(argc, argv);
    const std::uint64_t samples = args.num("samples", 200);
    const std::uint64_t seeds = args.num("seeds", 3);
    const std::string out = args.get("out", "");
    const std::string demand = args.get("demand", "uniform");
    const bool run_micro = args.num("micro", 1) != 0;
    std::vector<std::string> strategy_names = split(args.get("strategies", "full,pruned,cch,bounded"));
    std::vector<std::uint32_t> courier_counts;
    for (const auto& s : split(args.get("couriers", "20,100,500"))) courier_counts.push_back(static_cast<std::uint32_t>(std::stoul(s)));
    const std::vector<SizeSpec> all_sizes = {{"small", 20, 100, 240 * 60000}, {"small_peak", 20, 100, 120 * 60000},
                                             {"medium", 100, 1000, 480 * 60000}, {"medium_peak", 100, 1000, 240 * 60000},
                                             {"large", 500, 5000, 480 * 60000}, {"large_peak", 500, 5000, 240 * 60000}};
    std::vector<SizeSpec> sizes;
    for (const auto& n : split(args.get("sizes", "small,small_peak,medium,medium_peak,large,large_peak"))) {
        for (const auto& s : all_sizes) if (s.name == n) sizes.push_back(s);
    }

    Graph base = load_graph(args.graph);
    const auto t_cch = bench::Clock::now();
    bool need_cch = false;
    for (const auto& s : strategy_names) need_cch |= s != "full" && s != "pruned";
    std::unique_ptr<CCH> cch_free = need_cch ? std::make_unique<CCH>(base) : nullptr;
    const double cch_ms = bench::us_since(t_cch) / 1000.0;
    const std::uint32_t n = base.num_nodes();

    json::Writer w;
    w.begin_object();
    w.kv("benchmark", "decision_aware_eta");
    bench::write_environment(w, base, 0);
    w.kv("cch_preprocessing_ms", cch_ms);
    w.kv("demand", demand);
    w.key("strategies").begin_array();
    for (const auto& s : strategy_names) w.value(s);
    w.end_array();

    // ---------------------------------------------------------------- micro
    std::printf("decision-aware Greedy ETA on %u nodes (CCH preprocessing %.0f ms)\n", n, cch_ms);
    std::printf("\nmicro: %llu samples per cell\n", static_cast<unsigned long long>(samples));
    std::printf("%-10s %-9s %8s | %10s %10s %12s %10s\n", "regime", "strategy", "couriers", "median_us", "p95_us",
                "settled_med", "exact_med");
    w.key("micro").begin_array();
    for (const bool food_ready : {false, true}) {
        if (!run_micro) break;
        const char* regime = food_ready ? "ready" : "not_ready";
        for (const std::uint32_t k : courier_counts) {
            Rng rng(0xD15A7C4 ^ k ^ (food_ready ? 1 : 0));
            struct Sample {
                DispatchRequest req;
                std::vector<CourierCandidate> av;
            };
            std::vector<Sample> ss;
            for (std::uint64_t i = 0; i < samples + 10; ++i) {
                Sample s;
                const auto r = static_cast<NodeId>(rng.below(n));
                NodeId c = static_cast<NodeId>(rng.below(n));
                for (int tries = 0; tries < 2000; ++tries) {
                    if (haversine_m(base.lat[r], base.lon[r], base.lat[c], base.lon[c]) <= 6000.0 && c != r) break;
                    c = static_cast<NodeId>(rng.below(n));
                }
                const SimTime now = 3600000;
                const SimTime ready = food_ready ? now - rng.range(0, 600000) : now + rng.range(8, 20) * 60000;
                s.req = {static_cast<std::uint32_t>(i), r, c, now, ready};
                for (std::uint32_t j = 0; j < k; ++j) s.av.push_back({j, static_cast<NodeId>(rng.below(n))});
                ss.push_back(std::move(s));
            }
            std::vector<std::uint32_t> reference;
            for (const auto& sname : strategy_names) {
                GreedyEtaPolicy pol(base, parse_strategy(sname), cch_free.get());
                std::vector<double> lat, settled, exact;
                std::vector<std::uint32_t> chosen;
                for (std::size_t i = 0; i < ss.size(); ++i) {
                    const auto t0 = bench::Clock::now();
                    const std::uint32_t c = pol.choose(ss[i].req, ss[i].av);
                    const double us = bench::us_since(t0);
                    chosen.push_back(c);
                    if (i < 10) continue;
                    lat.push_back(us);
                    settled.push_back(static_cast<double>(pol.last_decision().nodes_settled));
                    exact.push_back(pol.last_decision().exact_distances);
                }
                if (reference.empty()) reference = chosen;
                else if (chosen != reference) {
                    std::fprintf(stderr, "EXACTNESS FAILURE: %s chose differently from %s\n", sname.c_str(), strategy_names[0].c_str());
                    return 1;
                }
                std::printf("%-10s %-9s %8u | %10.1f %10.1f %12.0f %10.0f\n", regime, sname.c_str(), k, percentile(lat, 50),
                            percentile(lat, 95), percentile(settled, 50), percentile(exact, 50));
                w.begin_object().kv("regime", regime).kv("strategy", sname).kv("couriers", k);
                w.kv("median_us", percentile(lat, 50)).kv("p95_us", percentile(lat, 95)).kv("mean_us", mean(lat));
                w.kv("settled_median", percentile(settled, 50)).kv("settled_mean", mean(settled));
                w.kv("exact_median", percentile(exact, 50)).kv("exact_mean", mean(exact)).end_object();
            }
        }
    }
    w.end_array();

    // ---------------------------------------------------------------- system
    std::printf("\nsystem: %llu seeds per size, moderate traffic; identical hashes required\n", static_cast<unsigned long long>(seeds));
    std::printf("%-12s %4s %-9s | %10s %12s %12s %12s %10s\n", "size", "seed", "strategy", "wall_s", "dec_mean_us",
                "settled/dec", "exact/dec", "hash");
    w.key("system").begin_array();
    for (const auto& sz : sizes) {
        for (std::uint64_t seed = 1; seed <= seeds; ++seed) {
            ScenarioParams p;
            p.couriers = sz.couriers;
            p.orders = sz.orders;
            p.order_window_ms = sz.window_ms;
            p.traffic = "moderate";
            p.seed = seed;
            p.demand = demand;
            const Scenario sc = generate_scenario(base, p);
            const Graph tg = apply_traffic(base, TrafficScenario::Moderate, seed);
            std::unique_ptr<CCH> cch_t;
            if (need_cch) {
                cch_t = std::make_unique<CCH>(*cch_free);
                cch_t->customize(tg);  // same topology, this run's traffic metric
            }
            std::uint64_t ref_hash = 0;
            for (const auto& sname : strategy_names) {
                GreedyEtaPolicy pol(tg, parse_strategy(sname), cch_t.get());
                Simulator sim(tg, sc, pol, Algorithm::kAStar);
                const auto t0 = bench::Clock::now();
                const SimResult r = sim.run();
                const double wall = bench::us_since(t0) / 1e6;
                if (ref_hash == 0) ref_hash = r.final_state_hash;
                else if (r.final_state_hash != ref_hash) {
                    std::fprintf(stderr, "EXACTNESS FAILURE: %s produced a different final state\n", sname.c_str());
                    return 1;
                }
                const double dec = static_cast<double>(std::max<std::uint64_t>(1, pol.decisions()));
                std::printf("%-12s %4llu %-9s | %10.2f %12.1f %12.0f %12.1f %10s\n", sz.name.c_str(),
                            static_cast<unsigned long long>(seed), sname.c_str(), wall, r.metrics.assign_latency_mean_us,
                            static_cast<double>(pol.nodes_settled()) / dec, static_cast<double>(pol.exact_distances()) / dec,
                            hex64(r.final_state_hash).substr(0, 8).c_str());
                w.begin_object().kv("size", sz.name).kv("couriers", sz.couriers).kv("orders", sz.orders).kv("seed", seed);
                w.kv("strategy", sname).kv("final_state_hash", hex64(r.final_state_hash)).kv("sim_wall_s", wall);
                w.kv("decisions", pol.decisions()).kv("decision_mean_us", r.metrics.assign_latency_mean_us);
                w.kv("decision_p95_us", r.metrics.assign_latency_p95_us);
                w.kv("settled_per_decision", static_cast<double>(pol.nodes_settled()) / dec);
                w.kv("exact_per_decision", static_cast<double>(pol.exact_distances()) / dec);
                w.kv("candidates_per_decision", static_cast<double>(pol.candidates_seen()) / dec);
                w.kv("orders_delivered", r.metrics.orders_delivered).end_object();
            }
        }
    }
    w.end_array();
    w.kv("peak_rss_bytes", peak_rss_bytes());
    w.end_object();
    if (!out.empty()) json::write_file(out, w.str());
    std::printf("\nEXACTNESS_OK: every strategy made identical decisions in every micro sample and simulation\n");
    return 0;
}
