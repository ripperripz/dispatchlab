// Graph benchmark (PRD §14.1, FR-02): CSR vs adjacency list (vector of
// vectors) - memory, load time, construction time, sequential traversal and
// Dijkstra query time on the same graph and query set.

#include <cstdio>

#include "bench_common.hpp"
#include "dispatchlab/rng.hpp"
#include "dispatchlab/routing.hpp"

using namespace dispatchlab;

namespace {

template <class F>
std::vector<double> repeat_us(int reps, F&& f) {
    std::vector<double> v;
    for (int i = 0; i < reps; ++i) {
        const auto t0 = bench::Clock::now();
        f();
        v.push_back(bench::us_since(t0));
    }
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    bench::Args args(argc, argv);
    const std::uint64_t seed = args.num("seed", 20261018);
    const int reps = static_cast<int>(args.num("reps", 7));
    const std::uint64_t queries = args.num("queries", 300);
    const std::string out = args.get("out", "");

    Graph g = load_graph(args.graph);
    AdjacencyGraph adj = to_adjacency(g);
    const std::uint32_t n = g.num_nodes();
    const std::uint32_t m = g.num_edges();

    // Load from the same binary file. CSR load = read + checksum + parse +
    // reverse-CSR build + validate; adjacency load = read + checksum + parse +
    // per-node vector construction (forward only).
    const auto csr_load = repeat_us(reps, [&] { bench::do_not_optimize(load_graph(args.graph).num_edges()); });
    const auto adj_load = repeat_us(reps, [&] { bench::do_not_optimize(load_adjacency(args.graph).num_nodes()); });

    // Construction from an edge list.
    std::vector<EdgeInput> edges;
    edges.reserve(m);
    for (NodeId u = 0; u < n; ++u)
        for (auto e = g.out_begin(u); e < g.out_end(u); ++e)
            edges.push_back({u, g.targets[e], g.weights[e], g.lengths_mm[e], g.road_class[e]});
    const auto csr_build = repeat_us(reps, [&] { bench::do_not_optimize(build_csr(n, edges).num_edges()); });
    const auto rev_build = repeat_us(reps, [&] {
        Graph copy = g;
        build_reverse(copy);
        bench::do_not_optimize(copy.rev_offsets.size());
    });
    const auto adj_build = repeat_us(reps, [&] { bench::do_not_optimize(to_adjacency(g).num_nodes()); });

    // Sequential traversal: visit every out-edge of every node in id order.
    const int trav_reps = 21;
    std::uint64_t sink = 0;
    const auto csr_trav = repeat_us(trav_reps, [&] {
        std::uint64_t s = 0;
        for (NodeId u = 0; u < n; ++u)
            for (auto e = g.offsets[u]; e < g.offsets[u + 1]; ++e) s += g.weights[e] + g.targets[e];
        sink += s;
    });
    const auto adj_trav = repeat_us(trav_reps, [&] {
        std::uint64_t s = 0;
        for (NodeId u = 0; u < n; ++u)
            for (const AdjEdge& e : adj.out[u]) s += e.weight + e.target;
        sink += s;
    });
    bench::do_not_optimize(sink);

    // Dijkstra on both representations, same query set, interleaved.
    Rng rng(seed);
    Dijkstra dc = make_dijkstra(g);
    AdjacencyDijkstra da{AdjOut{&adj}};
    std::vector<double> qc, qa;
    for (std::uint64_t i = 0; i < queries + 30; ++i) {
        const auto s = static_cast<NodeId>(rng.below(n));
        const auto t = static_cast<NodeId>(rng.below(n));
        auto t0 = bench::Clock::now();
        const Dist c1 = dc.query(s, t, false).cost;
        const double u1 = bench::us_since(t0);
        t0 = bench::Clock::now();
        const Dist c2 = da.query(s, t, false).cost;
        const double u2 = bench::us_since(t0);
        if (c1 != c2) {
            std::fprintf(stderr, "CSR/adjacency cost mismatch\n");
            return 1;
        }
        if (i < 30) continue;  // warm-up
        qc.push_back(u1);
        qa.push_back(u2);
    }

    const std::size_t csr_fwd_bytes = g.csr_core_bytes();
    const std::size_t adj_bytes = adj.memory_bytes();
    std::printf("graph benchmark: %u nodes, %u edges (%d reps, medians)\n", n, m, reps);
    std::printf("  memory   CSR fwd (offsets+targets+weights) %.2f MiB | adjacency fwd %.2f MiB | full Graph %.2f MiB\n",
                csr_fwd_bytes / 1048576.0, adj_bytes / 1048576.0, g.memory_bytes() / 1048576.0);
    std::printf("  load     CSR %.1f ms | adjacency %.1f ms\n", percentile(csr_load, 50) / 1e3,
                percentile(adj_load, 50) / 1e3);
    std::printf("  build    CSR %.1f ms (+reverse %.1f ms) | adjacency %.1f ms\n", percentile(csr_build, 50) / 1e3,
                percentile(rev_build, 50) / 1e3, percentile(adj_build, 50) / 1e3);
    std::printf("  traverse CSR %.3f ms (%.2f ns/edge) | adjacency %.3f ms (%.2f ns/edge)\n",
                percentile(csr_trav, 50) / 1e3, percentile(csr_trav, 50) * 1e3 / m, percentile(adj_trav, 50) / 1e3,
                percentile(adj_trav, 50) * 1e3 / m);
    std::printf("  dijkstra CSR median %.0f us p95 %.0f us | adjacency median %.0f us p95 %.0f us\n",
                percentile(qc, 50), percentile(qc, 95), percentile(qa, 50), percentile(qa, 95));

    json::Writer w;
    w.begin_object();
    w.kv("benchmark", "graph");
    bench::write_environment(w, g, seed);
    w.kv("reps", reps).kv("dijkstra_queries", queries);
    w.kv("methodology",
         "medians over repetitions; load = from the same DLGRAPH1 file (CSR additionally builds the reverse CSR "
         "and validates); traversal = sum of target+weight over all out-edges in node order; Dijkstra = same "
         "seeded pairs on both representations, interleaved, no path reconstruction, 30 warm-up pairs; memory = "
         "vector capacities (adjacency includes one std::vector header per node)");
    auto obj = [&](const char* name, std::size_t mem_bytes, const std::vector<double>& load,
                   const std::vector<double>& build, const std::vector<double>& trav, const std::vector<double>& q) {
        w.key(name).begin_object();
        w.kv("memory_bytes", static_cast<std::uint64_t>(mem_bytes));
        w.kv("load_ms", percentile(load, 50) / 1e3);
        w.kv("build_ms", percentile(build, 50) / 1e3);
        w.kv("traversal_ms", percentile(trav, 50) / 1e3);
        w.kv("traversal_ns_per_edge", percentile(trav, 50) * 1e3 / m);
        w.kv("dijkstra_median_us", percentile(q, 50)).kv("dijkstra_p95_us", percentile(q, 95));
        w.kv("dijkstra_mean_us", mean(q));
        w.end_object();
    };
    obj("csr", csr_fwd_bytes, csr_load, csr_build, csr_trav, qc);
    obj("adjacency", adj_bytes, adj_load, adj_build, adj_trav, qa);
    w.kv("reverse_build_ms", percentile(rev_build, 50) / 1e3);
    w.kv("full_graph_memory_bytes", static_cast<std::uint64_t>(g.memory_bytes()));
    w.kv("peak_rss_bytes", peak_rss_bytes());
    w.end_object();
    if (!out.empty()) json::write_file(out, w.str());
    return 0;
}
