// Routing benchmark (PRD §14.2): Dijkstra vs Bidirectional Dijkstra vs A* (vs CCH) on
// one seeded query set. Queries are interleaved (D, B, A per pair) so drift in
// machine state affects all algorithms equally. Each query includes path
// reconstruction. Costs are cross-checked; any disagreement aborts.

#include <array>
#include <cstdio>

#include "bench_common.hpp"
#include "dispatchlab/cch.hpp"
#include "dispatchlab/rng.hpp"
#include "dispatchlab/routing.hpp"

using namespace dispatchlab;

namespace {

struct Bucket {
    const char* label;
    Dist lo_ms, hi_ms;
};
constexpr std::array<Bucket, 4> kBuckets{{
    {"< 5 min", 0, 5 * 60000},
    {"5-10 min", 5 * 60000, 10 * 60000},
    {"10-20 min", 10 * 60000, 20 * 60000},
    {">= 20 min", 20 * 60000, kInfDist},
}};

}  // namespace

int main(int argc, char** argv) {
    bench::Args args(argc, argv);
    const std::uint64_t queries = args.num("queries", 2000);
    const std::uint64_t warmup = args.num("warmup", 200);
    const std::uint64_t seed = args.num("seed", 20261018);
    const std::string out = args.get("out", "");

    Graph g = load_graph(args.graph);
    const std::uint32_t n = g.num_nodes();
    Rng rng(seed);
    std::vector<std::pair<NodeId, NodeId>> pairs(queries + warmup);
    for (auto& p : pairs) p = {static_cast<NodeId>(rng.below(n)), static_cast<NodeId>(rng.below(n))};

    Dijkstra dij = make_dijkstra(g);
    BidirectionalDijkstra bi(g);
    AStar astar(g);
    // CCH preprocessing (ordering + contraction + customization) is timed
    // separately from queries; re-customization alone is timed as well.
    auto tp = bench::Clock::now();
    CCH cch(g);
    const double cch_build_ms = bench::us_since(tp) / 1e3;
    tp = bench::Clock::now();
    cch.customize(g);
    const double cch_recustomize_ms = bench::us_since(tp) / 1e3;
    constexpr int A = 4;
    const char* names[A] = {"Dijkstra", "Bidirectional", "A*", "CCH"};
    std::vector<double> lat[A], settled[A];
    std::vector<Dist> cost(queries);

    for (std::uint64_t i = 0; i < pairs.size(); ++i) {
        const auto [s, t] = pairs[i];
        RouteResult r[A];
        double us[A];
        auto t0 = bench::Clock::now();
        r[0] = dij.query(s, t);
        us[0] = bench::us_since(t0);
        t0 = bench::Clock::now();
        r[1] = bi.query(s, t);
        us[1] = bench::us_since(t0);
        t0 = bench::Clock::now();
        r[2] = astar.query(s, t);
        us[2] = bench::us_since(t0);
        t0 = bench::Clock::now();
        r[3] = cch.query(s, t);
        us[3] = bench::us_since(t0);
        if (r[0].cost != r[1].cost || r[0].cost != r[2].cost || r[0].cost != r[3].cost) {
            std::fprintf(stderr, "cost mismatch on pair %llu\n", static_cast<unsigned long long>(i));
            return 1;
        }
        if (i < warmup) continue;
        cost[i - warmup] = r[0].cost;
        for (int a = 0; a < A; ++a) {
            lat[a].push_back(us[a]);
            settled[a].push_back(r[a].stats.settled);
        }
    }
    const std::size_t mem[A] = {dij.memory_bytes(), bi.memory_bytes(), astar.memory_bytes(),
                                cch.query_memory_bytes()};

    std::printf("routing benchmark: %llu queries (+%llu warm-up), seed %llu, graph %u nodes / %u edges\n",
                static_cast<unsigned long long>(queries), static_cast<unsigned long long>(warmup),
                static_cast<unsigned long long>(seed), n, g.num_edges());
    std::printf("%-14s %10s %10s %10s %10s %12s %12s\n", "algorithm", "median_us", "p95_us", "p99_us", "mean_us",
                "settled_med", "memory_KiB");
    for (int a = 0; a < A; ++a) {
        std::printf("%-14s %10.1f %10.1f %10.1f %10.1f %12.0f %12.1f\n", names[a], percentile(lat[a], 50),
                    percentile(lat[a], 95), percentile(lat[a], 99), mean(lat[a]), percentile(settled[a], 50),
                    static_cast<double>(mem[a]) / 1024.0);
    }

    json::Writer w;
    w.begin_object();
    w.kv("benchmark", "routing");
    bench::write_environment(w, g, seed);
    w.kv("queries", queries).kv("warmup", warmup);
    w.kv("methodology",
         "seeded uniform random (s,t) node pairs; same pairs for every algorithm; per pair the three algorithms run "
         "back-to-back (interleaved); latency = steady_clock around one query incl. path reconstruction; "
         "single thread; warm-up pairs discarded; nodes settled = non-stale heap pops; memory = per-query "
         "workspace (labels + heap capacity) after the run, excluding the shared graph");
    w.kv("graph_memory_bytes", static_cast<std::uint64_t>(g.memory_bytes()));
    std::printf("CCH preprocessing: total %.0f ms (order %.0f, contract %.0f, customize %.0f), re-customize %.0f ms, "
                "%llu upward edges, hierarchy %.1f MiB\n",
                cch_build_ms, cch.stats().order_ms, cch.stats().contract_ms, cch.stats().customize_ms,
                cch_recustomize_ms, static_cast<unsigned long long>(cch.stats().upward_edges),
                static_cast<double>(cch.memory_bytes()) / 1048576.0);
    w.key("cch").begin_object();
    w.kv("preprocessing_ms", cch_build_ms).kv("order_ms", cch.stats().order_ms);
    w.kv("contract_ms", cch.stats().contract_ms).kv("customize_ms", cch.stats().customize_ms);
    w.kv("recustomize_ms", cch_recustomize_ms);
    w.kv("upward_edges", cch.stats().upward_edges).kv("original_pairs", cch.stats().original_pairs);
    w.kv("triangles", cch.stats().triangles).kv("max_up_degree", cch.stats().max_up_degree);
    w.kv("hierarchy_bytes", static_cast<std::uint64_t>(cch.memory_bytes()));
    w.kv("ordering", "recursive geometric nested dissection (median split of the wider axis, smaller boundary as separator)");
    w.end_object();
    w.key("algorithms").begin_array();
    for (int a = 0; a < A; ++a) {
        w.begin_object();
        w.kv("name", names[a]);
        w.kv("median_us", percentile(lat[a], 50)).kv("p95_us", percentile(lat[a], 95));
        w.kv("p99_us", percentile(lat[a], 99)).kv("mean_us", mean(lat[a]));
        w.kv("settled_median", percentile(settled[a], 50)).kv("settled_mean", mean(settled[a]));
        w.kv("settled_p95", percentile(settled[a], 95));
        w.kv("memory_bytes", static_cast<std::uint64_t>(mem[a]));
        w.end_object();
    }
    w.end_array();
    w.key("buckets").begin_array();
    for (const auto& b : kBuckets) {
        std::vector<double> bl[A], bs[A];
        for (std::size_t i = 0; i < cost.size(); ++i) {
            if (cost[i] < b.lo_ms || cost[i] >= b.hi_ms) continue;
            for (int a = 0; a < A; ++a) {
                bl[a].push_back(lat[a][i]);
                bs[a].push_back(settled[a][i]);
            }
        }
        w.begin_object();
        w.kv("label", b.label).kv("count", static_cast<std::uint64_t>(bl[0].size()));
        for (int a = 0; a < A; ++a) {
            const char* keys[A] = {"dijkstra", "bidirectional", "astar", "cch"};
            const std::string key = keys[a];
            w.kv(key + "_median_us", percentile(bl[a], 50));
            w.kv(key + "_settled_median", percentile(bs[a], 50));
        }
        w.end_object();
    }
    w.end_array();
    w.kv("peak_rss_bytes", peak_rss_bytes());
    w.end_object();
    if (!out.empty()) json::write_file(out, w.str());
    return 0;
}
