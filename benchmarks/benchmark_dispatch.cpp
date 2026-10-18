// Dispatch benchmark (PRD §14.3): wall-clock cost of one assignment decision
// for each policy as the number of available couriers grows. Each sample is a
// random restaurant/customer pair (customer within 6 km) with all couriers
// available at random nodes; both policies see identical samples.

#include <cstdio>

#include "bench_common.hpp"
#include "dispatchlab/dispatch.hpp"
#include "dispatchlab/geo.hpp"
#include "dispatchlab/rng.hpp"

using namespace dispatchlab;

int main(int argc, char** argv) {
    bench::Args args(argc, argv);
    const std::uint64_t seed = args.num("seed", 20261018);
    const std::uint64_t samples = args.num("samples", 300);
    const std::string out = args.get("out", "");

    Graph g = load_graph(args.graph);
    const std::uint32_t n = g.num_nodes();
    const std::vector<std::uint32_t> courier_counts = {20, 100, 500};

    json::Writer w;
    w.begin_object();
    w.kv("benchmark", "dispatch");
    bench::write_environment(w, g, seed);
    w.kv("samples", samples);
    w.kv("methodology",
         "per (courier count, sample): random restaurant node, random customer within 6 km, all couriers "
         "available at random nodes; identical samples for both policies; latency = steady_clock around one "
         "choose() call; 20 warm-up samples discarded");
    w.key("results").begin_array();
    std::printf("dispatch benchmark: %llu samples per cell\n", static_cast<unsigned long long>(samples));
    std::printf("%-11s %9s %12s %12s %12s\n", "policy", "couriers", "median_us", "p95_us", "mean_us");
    for (std::uint32_t k : courier_counts) {
        Rng rng(seed ^ k);
        struct Sample {
            DispatchRequest req;
            std::vector<CourierCandidate> couriers;
        };
        std::vector<Sample> ss;
        for (std::uint64_t i = 0; i < samples + 20; ++i) {
            Sample s;
            const auto r = static_cast<NodeId>(rng.below(n));
            NodeId c = static_cast<NodeId>(rng.below(n));
            for (int tries = 0; tries < 2000; ++tries) {
                if (haversine_m(g.lat[r], g.lon[r], g.lat[c], g.lon[c]) <= 6000.0 && c != r) break;
                c = static_cast<NodeId>(rng.below(n));
            }
            s.req = {static_cast<std::uint32_t>(i), r, c, 0, static_cast<SimTime>(rng.range(8, 20)) * 60000};
            for (std::uint32_t j = 0; j < k; ++j) s.couriers.push_back({j, static_cast<NodeId>(rng.below(n))});
            ss.push_back(std::move(s));
        }
        for (const char* name : {"nearest", "greedy_eta"}) {
            auto policy = make_policy(name, g);
            std::vector<double> lat;
            for (std::size_t i = 0; i < ss.size(); ++i) {
                const auto t0 = bench::Clock::now();
                const std::uint32_t chosen = policy->choose(ss[i].req, ss[i].couriers);
                const double us = bench::us_since(t0);
                bench::do_not_optimize(chosen);
                if (i >= 20) lat.push_back(us);
            }
            std::printf("%-11s %9u %12.1f %12.1f %12.1f\n", name, k, percentile(lat, 50), percentile(lat, 95), mean(lat));
            w.begin_object();
            w.kv("policy", name).kv("couriers", k);
            w.kv("median_us", percentile(lat, 50)).kv("p95_us", percentile(lat, 95)).kv("mean_us", mean(lat));
            w.end_object();
        }
    }
    w.end_array();
    w.kv("peak_rss_bytes", peak_rss_bytes());
    w.end_object();
    if (!out.empty()) json::write_file(out, w.str());
    return 0;
}
